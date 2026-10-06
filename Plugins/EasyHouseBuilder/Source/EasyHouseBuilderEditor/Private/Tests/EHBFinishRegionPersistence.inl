IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFinishRegionWriteTest,"EHBValidation.Persistence.WriteFinishRegionEdit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFinishRegionWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("FinishRegionEdit")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite edited-region evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<AEHBBuildingActorBase*> Buildings;TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;auto* B=Fixture.Building;
  B->SetActorLocationAndRotation(FVector(Mode*2400,0,0),FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(Mode==2||(Mode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const FGuid Removed=Fixture.Walls[0]->ElementGuid,OldRoom=B->GetClosedLoopsByWallGuid(Removed)[0].LoopGuid;FEHBWallNodeModel Model;UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model);const auto Definition=*Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Removed;});
  AEHB_Floor* F=nullptr;AEHB_FloorSlab* S=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* A=Cast<AEHB_Floor>(E);A&&A->RoomLoopGuid==OldRoom)F=A;if(auto* A=Cast<AEHB_FloorSlab>(E);A&&A->RoomFillLoopGuid==OldRoom)S=A;}if(!F||!S)return false;
  if(!UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,false).bSucceeded)return false;
  for(auto* E:TArray<AEHBElementActorBase*>{S,F})
  {
   auto P=EHBFinishRegionCommand::GetPolygon(E);const auto Center=FBox(P).GetCenter();for(auto& V:P){V.X=Center.X+(V.X-Center.X)*0.8;V.Y=Center.Y+(V.Y-Center.Y)*0.8;}
   const auto R=UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),P,{},false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  }
  if(Mode>0)
  {
   auto Endpoint=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=N->GeometryRevision;E.LocalLocation=N->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=N->FloorIndex;return E;};
   FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=false;const auto Closed=EHBWallCreationCommand::Commit(B,{Endpoint(Definition.StartNodeGuid),Endpoint(Definition.EndNodeGuid)},false,Options);if(!TestTrue(*Closed.Status.ToString(),Closed.bSucceeded))return false;
   FEHBSurfaceRoomCoverage Coverage;FName Status;if(!B->QuerySurfaceRoomCoverage(F->ElementGuid,Coverage,Status)||Coverage.Rooms.Num()!=1)return false;
   for(auto* E:TArray<AEHBElementActorBase*>{S,F})if(Mode==1||E==S)
   {const auto R=UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),{},Coverage.Rooms[0].RoomGuid,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  }
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("node"),Node.ToString());C->SetStringField(TEXT("floor"),F->ElementGuid.ToString());C->SetStringField(TEXT("slab"),S->ElementGuid.ToString());Cases.Add(MakeShared<FJsonValueObject>(C));Buildings.Add(B);
  B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save actual resized and rebound surfaces"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 for(int32 I=0;I<Buildings.Num();++I){Cases[I]->AsObject()->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(Buildings[I]));Cases[I]->AsObject()->SetStringField(TEXT("receipt"),LiveNodeReceipt(Buildings[I]));}
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.FinishRegionEdit.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save edited surface evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFinishRegionReadTest,"EHBValidation.Persistence.ReadFinishRegionEdit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFinishRegionReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("FinishRegionEdit"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.FinishRegionEdit.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold edited map current"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three edited source combinations"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Value:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=Value->AsObject();auto Id=[&](const TCHAR* Key){FGuid V;FGuid::Parse(C->GetStringField(Key),V);return V;};AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id(TEXT("building")))B=*It;if(!B)return false;
  const auto Before=LiveNodeAuthoritySnapshot(B);TestEqual(TEXT("Cold edited outlines geometry and bindings"),Before,C->GetStringField(TEXT("state")));TestEqual(TEXT("Cold edit receipt"),LiveNodeReceipt(B),C->GetStringField(TEXT("receipt")));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(Id(TEXT("floor"))));auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Id(TEXT("slab"))));if(!F||!S)return false;TestTrue(TEXT("Cold source signatures"),F->IsRecordedOutlineUnchanged()&&S->IsRecordedOutlineUnchanged());
  for(auto* E:TArray<AEHBElementActorBase*>{S,F})if(EHBFinishRegionCommand::IsIndependent(E))
  {
   auto P=EHBFinishRegionCommand::GetPolygon(E);const auto Center=FBox(P).GetCenter();for(auto& V:P){V.X=Center.X+(V.X-Center.X)*0.95;V.Y=Center.Y+(V.Y-Center.Y)*0.95;}
   const auto R=UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),P,{},false);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto After=LiveNodeAuthoritySnapshot(B);
   TestTrue(TEXT("Cold resize undo"),GEditor->UndoTransaction());TestEqual(TEXT("Cold resize undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Cold resize redo"),GEditor->RedoTransaction());TestEqual(TEXT("Cold resize redo exact"),LiveNodeAuthoritySnapshot(B),After);GEditor->UndoTransaction(false);
  }
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id(TEXT("node"));});if(!N)return false;FEHBNodeMoveRequest M;M.NodeGuid=N->NodeGuid;M.ExpectedPosition=N->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(-25,0,0);
  const auto Moved=EHBNodeAuthorityEditing::ExecuteMoves(B,{M},{{N->NodeGuid,N->GeometryRevision}},false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold wall-following and fixed sources undo"),LiveNodeAuthoritySnapshot(B),Before);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3200,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold copied edited source remains unchanged"),LiveNodeAuthoritySnapshot(B),Before);
 }
 GEditor->SelectNone(false,true,false);return true;
}
