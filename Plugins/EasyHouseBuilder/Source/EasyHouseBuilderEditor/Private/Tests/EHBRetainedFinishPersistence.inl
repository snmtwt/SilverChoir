IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRetainedFinishWriteTest,"EHBValidation.Persistence.WriteRetainedFinish",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRetainedFinishWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("RetainedFinish")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite retained-finish evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<AEHBBuildingActorBase*> Buildings;TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;B->SetActorLocationAndRotation(FVector(Mode*2400,0,0),FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(Mode==2||(Mode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const FGuid Removed=Fixture.Walls[0]->ElementGuid;const auto Loops=B->GetClosedLoopsByWallGuid(Removed);if(Loops.Num()!=1)return false;const FGuid OldRoom=Loops[0].LoopGuid;
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;const auto* W=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Removed;});if(!W)return false;const auto Definition=*W;
  const auto Applied=UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("node"),Node.ToString());C->SetStringField(TEXT("start"),Definition.StartNodeGuid.ToString());C->SetStringField(TEXT("end"),Definition.EndNodeGuid.ToString());C->SetStringField(TEXT("removed"),Removed.ToString());C->SetStringField(TEXT("oldRoom"),OldRoom.ToString());
  for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E);F&&F->OutlineSource==EEHBOutlineSource::RetainedRegion)C->SetStringField(TEXT("floor"),F->ElementGuid.ToString());
   if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->OutlineSource==EEHBOutlineSource::RetainedRegion)C->SetStringField(TEXT("slab"),S->ElementGuid.ToString());
  }
  if(!C->HasField(TEXT("floor"))||!C->HasField(TEXT("slab")))return false;
  Cases.Add(MakeShared<FJsonValueObject>(C));Buildings.Add(B);B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save actual independent regions in three binding modes"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 for(int32 I=0;I<Buildings.Num();++I){Cases[I]->AsObject()->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(Buildings[I]));Cases[I]->AsObject()->SetStringField(TEXT("receipt"),LiveNodeReceipt(Buildings[I]));}
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.RetainedFinish.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save independent retained evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRetainedFinishReadTest,"EHBValidation.Persistence.ReadRetainedFinish",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRetainedFinishReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("RetainedFinish"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.RetainedFinish.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold retained map is current world"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three saved binding modes"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Value:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=Value->AsObject();auto Id=[&](const TCHAR* Key){FGuid V;FGuid::Parse(C->GetStringField(Key),V);return V;};AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id(TEXT("building")))B=*It;if(!B)return false;
  TestEqual(TEXT("Cold retained geometry bindings and source"),LiveNodeAuthoritySnapshot(B),C->GetStringField(TEXT("state")));TestEqual(TEXT("Cold retained receipt"),LiveNodeReceipt(B),C->GetStringField(TEXT("receipt")));
  auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(Id(TEXT("floor"))));auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Id(TEXT("slab"))));if(!F||!S)return false;const auto FP=F->LocalFloorPolygon,SP=S->LocalTopPolygon;
  TestTrue(TEXT("Independent source and no room restored"),F->OutlineSource==EEHBOutlineSource::RetainedRegion&&S->OutlineSource==EEHBOutlineSource::RetainedRegion&&!F->RoomLoopGuid.IsValid()&&!S->RoomFillLoopGuid.IsValid());TestTrue(TEXT("Saved region signatures verify"),F->IsRecordedOutlineUnchanged()&&S->IsRecordedOutlineUnchanged());
  FEHBNodeRoomBoundary Gone;TestFalse(TEXT("Old enclosure not resurrected"),B->TryGetRoomBoundary(Id(TEXT("oldRoom")),1,Gone));TestNull(TEXT("Removed wall not resurrected"),B->FindElementActorByGuid(Id(TEXT("removed"))));
  TArray<FEHBElementRelation> Contacts;FName Status;TestTrue(TEXT("Cold contacts recover from actual retained coverage"),F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Contacts,Status));TestEqual(TEXT("Cold contact cache complete"),Contacts.Num(),F->SurfaceFinishRelationGuids.Num());for(const auto& R:Contacts)TestEqual(TEXT("Cold contact source metadata"),R.StringMetadata.FindRef(TEXT("OutlineSource")),FString(TEXT("RetainedRegion")));
  FEHBSurfaceRoomCoverage Coverage;TestTrue(TEXT("Cold independent spatial coverage"),B->QuerySurfaceRoomCoverage(F->ElementGuid,Coverage,Status));TestTrue(TEXT("Cold open region explicitly outside all rooms"),Coverage.Rooms.IsEmpty()&&FMath::IsNearlyEqual(Coverage.AreaCm2,Coverage.OutsideAreaCm2,0.01));
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=LiveNodeAuthoritySnapshot(B);
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id(TEXT("node"));});if(!N)return false;FEHBNodeMoveRequest M;M.NodeGuid=N->NodeGuid;M.ExpectedPosition=N->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(-30,0,0);
  const auto Move=EHBNodeAuthorityEditing::ExecuteMoves(B,{M},{{N->NodeGuid,N->GeometryRevision}},false);if(!TestTrue(*FString::Printf(TEXT("Cold retained node move: %s"),*Move.Message),Move.bSucceeded))return false;TestEqual(TEXT("Cold moved floor remains fixed"),F->LocalFloorPolygon,FP);TestEqual(TEXT("Cold moved slab remains fixed"),S->LocalTopPolygon,SP);const auto Moved=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Cold move undo available"),GEditor->UndoTransaction());TestEqual(TEXT("Cold move undo"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Cold move redo available"),GEditor->RedoTransaction());TestEqual(TEXT("Cold move redo"),LiveNodeAuthoritySnapshot(B),Moved);GEditor->UndoTransaction(false);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3200,0));if(!TestTrue(*FString::Printf(TEXT("Cold retained copy: %s"),*Copy.Status.ToString()),Copy.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold copy source unchanged"),LiveNodeAuthoritySnapshot(B),Before);
  auto Endpoint=[&](FGuid Guid){FEHBWallCreationEndpoint E;const auto* P=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Guid;});E.NodeGuid=Guid;E.ExpectedNodeRevision=P->GeometryRevision;E.LocalLocation=P->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=P->FloorIndex;return E;};
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=false;const auto Closed=EHBWallCreationCommand::Commit(B,{Endpoint(Id(TEXT("start"))),Endpoint(Id(TEXT("end")))},false,Options);if(!TestTrue(*FString::Printf(TEXT("Cold reclosure: %s / %s"),*Closed.Status.ToString(),*Closed.FailureReason.ToString()),Closed.bSucceeded))return false;
  TestEqual(TEXT("Cold reclosure actually encloses room"),B->GetClosedLoopsByFloor(1).Num(),2);TestEqual(TEXT("Reclosure preserves independent floor"),F->LocalFloorPolygon,FP);TestEqual(TEXT("Reclosure preserves independent slab"),S->LocalTopPolygon,SP);TestFalse(TEXT("Reclosure does not silently rebind region"),F->RoomLoopGuid.IsValid());TestTrue(TEXT("Cold reclosed region spatial query"),B->QuerySurfaceRoomCoverage(F->ElementGuid,Coverage,Status));TestTrue(TEXT("Reclosed independent region spatially covers one actual room"),Coverage.Rooms.Num()==1&&FMath::IsNearlyZero(Coverage.OutsideAreaCm2,0.01)&&FMath::IsNearlyEqual(Coverage.Rooms[0].SurfaceFraction,1.0,0.00001));GEditor->UndoTransaction(false);TestEqual(TEXT("Cold reclosure undo"),LiveNodeAuthoritySnapshot(B),Before);
 }
 GEditor->SelectNone(false,true,false);return true;
}
