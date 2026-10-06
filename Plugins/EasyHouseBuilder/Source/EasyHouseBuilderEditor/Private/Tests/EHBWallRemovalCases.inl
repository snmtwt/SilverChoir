// Included after the shared native building fixtures in EHBWallTopologyAutomationTests.cpp.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRemovalTest,"EHB.Topology.WallRemoval",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallRemovalTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBWallRemovalCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 BindingMode=0;BindingMode<3;++BindingMode)for(bool Surface:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,Surface))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!TestTrue(TEXT("Activate wall removal fixture"),UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded))return false;
  const FGuid First=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==First))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const auto OldRooms=B->GetClosedLoopsByFloor(1);if(OldRooms.Num()!=2)return false;
  FGuid Shared;for(FGuid Id:OldRooms[0].WallGuids)if(OldRooms[1].WallGuids.Contains(Id))Shared=Id;if(!Shared.IsValid())return false;
  auto* Wall=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Shared));
  FGuid DonorRoom=OldRooms[0].Area>OldRooms[1].Area?OldRooms[0].LoopGuid:OldRooms[1].LoopGuid;
  AEHB_Floor* Floor=nullptr;AEHB_FloorSlab* Slab=nullptr;TArray<FGuid> Retired;
  for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E)){if(F->RoomLoopGuid==DonorRoom)Floor=F;else Retired.Add(F->ElementGuid);}
   if(auto* S=Cast<AEHB_FloorSlab>(E)){S->bHasRoomFillAnchor=true;S->RoomFillAnchorWallGuid=Shared;S->RoomFillAnchorWallSide=EEHBFloorSlabWallSide::Left;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);if(S->RoomFillLoopGuid==DonorRoom)Slab=S;else Retired.Add(S->ElementGuid);}
  }
  if(!Floor||!Slab)return false;const FGuid FloorId=Floor->ElementGuid,SlabId=Slab->ElementGuid;
  auto Commit=[&](bool Preview=false){return UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision,Preview);};
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeReceipt=LiveNodeReceipt(B);const auto OldRelations=B->ElementRelations;
  int32 Events=0,Changes=0;FEHBCommittedEdit Observed;
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);
  Observer->ObserveCommit=[&](const auto& E){++Events;Observed=E;};Observer->Observe=[&](FGuid,FName,bool){++Changes;};
  B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);
  ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);B->OnElementRelationAdded.RemoveAll(Observer);B->OnElementRelationRemoved.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);};
  const auto Ready=Commit(true);if(!TestTrue(*FString::Printf(TEXT("Binding %d surface %d preview %s"),BindingMode,Surface,*Ready.Message),Ready.bSucceeded))return false;
  TestEqual(TEXT("Removal preview is entirely read-only"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview emits no changes"),Changes,0);TestEqual(TEXT("Preview emits no receipt"),Events,0);
  TestEqual(TEXT("Stale removal intent rejected"),UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision-1,false).Message,FString(TEXT("StaleRemovalGraphRevision")));
  TestEqual(TEXT("Duplicate removal request rejected"),UEHBBuildingToolset::RemoveWalls(B,{Shared,Shared},B->RelationshipGraphRevision,false).Message,FString(TEXT("InvalidRemovalWallList")));
  for(int32 Phase=1;Phase<=7;++Phase)
  {
   EHBWallRemovalCommand::FailurePhase=Phase;const auto R=Commit();
   TestEqual(TEXT("Injected failure actually reaches requested phase"),EHBWallRemovalCommand::FailurePhase,0);
   TestEqual(*FString::Printf(TEXT("Failure phase %d is one rollback"),Phase),R.Message,FString(TEXT("WallRemovalFailedRolledBack")));
   TestEqual(TEXT("Failure restores all nodes actors room bindings contacts and geometry"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Failure restores old receipt"),LiveNodeReceipt(B),BeforeReceipt);
   TestEqual(TEXT("No partial committed event"),Events,0);TestEqual(TEXT("No partial geometry/relation events"),Changes,0);TestFalse(TEXT("Failed removal cannot be redone"),GEditor->Trans->CanRedo());
  }
  const auto Applied=Commit();if(!TestTrue(*FString::Printf(TEXT("Removal commit %s"),*Applied.Message),Applied.bSucceeded))return false;
  TestEqual(TEXT("Exactly one merged edit event"),Events,1);TestEqual(TEXT("Removal command receipt"),Observed.Command,FName(TEXT("RemoveWalls")));
  TestNull(TEXT("Requested wall is removed"),B->FindElementActorByGuid(Shared));for(FGuid Id:Retired)TestNull(TEXT("Superseded compatible finish is retired"),B->FindElementActorByGuid(Id));
  TestEqual(TEXT("Retained floor identity"),B->FindElementActorByGuid(FloorId),static_cast<AEHBElementActorBase*>(Floor));TestEqual(TEXT("Retained slab identity"),B->FindElementActorByGuid(SlabId),static_cast<AEHBElementActorBase*>(Slab));
  const auto MergedRooms=B->GetClosedLoopsByFloor(1);TestEqual(TEXT("Removing shared wall merges two actual rooms"),MergedRooms.Num(),1);if(MergedRooms.Num()!=1)return false;const auto Room=MergedRooms[0];
  TestEqual(TEXT("Floor follows new actual room ID"),Floor->RoomLoopGuid,Room.LoopGuid);TestEqual(TEXT("Slab follows new actual room ID"),Slab->RoomFillLoopGuid,Room.LoopGuid);
  TestTrue(TEXT("Removed slab anchor replaced by a real boundary wall"),Slab->bHasRoomFillAnchor&&Slab->RoomFillAnchorWallGuid!=Shared&&Room.WallGuids.Contains(Slab->RoomFillAnchorWallGuid));
  for(const auto& Old:OldRooms){FEHBNodeRoomBoundary Boundary;TestFalse(TEXT("Obsolete room no longer queries"),B->TryGetRoomBoundary(Old.LoopGuid,1,Boundary));TestTrue(TEXT("Receipt covers old room identity"),Observed.RoomGuids.Contains(Old.LoopGuid));}TestTrue(TEXT("Receipt covers new room identity"),Observed.RoomGuids.Contains(Room.LoopGuid));
  for(FGuid Id:TArray<FGuid>{Shared,Retired[0],Retired[1]}){const auto* E=Observed.Elements.FindByPredicate([&](const auto& V){return V.ElementGuid==Id;});TestTrue(TEXT("Retired element retains old bounds in receipt"),E&&E->BeforeBounds.IsValid&&!E->AfterBounds.IsValid);}
  TArray<FEHBElementRelation> Planned;FName Status;TestTrue(TEXT("Actual finish contacts resolve merged room"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Planned,Status));TestEqual(TEXT("Merged contact cache complete"),Planned.Num(),Floor->SurfaceFinishRelationGuids.Num());
  for(const auto& R:B->ElementRelations)if(R.Type==EEHBElementRelationType::SurfaceFinish)
  {
   TestEqual(TEXT("No retired floor remains as contact target"),R.Target.ElementGuid,FloorId);TestFalse(TEXT("No removed host survives in contact"),R.Source.ElementGuid==Shared||Retired.Contains(R.Source.ElementGuid));
   const auto* Old=OldRelations.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});if(Old)TestEqual(TEXT("Retained host-floor pair preserves relation ID"),R.RelationGuid,Old->RelationGuid);
  }
  TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Merged membership query"),B->QueryRoomDependencies(MergedRooms,Members));TestEqual(TEXT("One room membership"),Members.Num(),1);if(Members.Num()==1){TestEqual(TEXT("One retained floor"),Members[0].Floors,TArray<FGuid>{FloorId});TestEqual(TEXT("One retained slab"),Members[0].Slabs,TArray<FGuid>{SlabId});}
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);
  TestTrue(TEXT("Undo full merged deletion"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores every source element and relationship"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Undo restores source receipt"),LiveNodeReceipt(B),BeforeReceipt);
  TestTrue(TEXT("Redo full merged deletion"),GEditor->RedoTransaction());TestEqual(TEXT("Redo reproduces exact merged geometry and IDs"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Redo restores merged receipt"),LiveNodeReceipt(B),AfterReceipt);
  FEHBWallNodeModel Source;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source).bSucceeded)return false;
  const auto* N=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==First;});if(!N)return false;
  FEHBNodeMoveRequest Move;Move.NodeGuid=First;Move.ExpectedPosition=N->LocalTransform.GetLocation();Move.TargetPosition=Move.ExpectedPosition+FVector(-30,0,0);
  const auto Moved=EHBNodeAuthorityEditing::ExecuteMoves(B,{Move},{{First,N->GeometryRevision}},false);TestTrue(*FString::Printf(TEXT("Merged finishes remain editable: %s"),*Moved.Message),Moved.bSucceeded);if(Moved.bSucceeded){GEditor->UndoTransaction(false);TestEqual(TEXT("Undo subsequent move restores merged state"),LiveNodeAuthoritySnapshot(B),After);}
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(3000,0,0));TestTrue(*FString::Printf(TEXT("Merged building remains copyable: %s"),*Copy.Status.ToString()),Copy.bSucceeded);if(Copy.bSucceeded){Fixture.Actors.Append(Copy.Actors);GEditor->UndoTransaction(false);TestEqual(TEXT("Copy leaves merged source unchanged"),LiveNodeAuthoritySnapshot(B),After);}
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRemovalGuardsTest,"EHB.Topology.WallRemovalGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallRemovalGuardsTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  const auto Rooms=B->GetClosedLoopsByFloor(1);FGuid Shared;for(FGuid Id:Rooms[0].WallGuids)if(Rooms[1].WallGuids.Contains(Id))Shared=Id;if(!Shared.IsValid())return false;
  auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Shared));AEHB_Floor* Floor=nullptr;AEHB_FloorSlab* Slab=nullptr;
  for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* F=Cast<AEHB_Floor>(E))Floor=F;if(auto* S=Cast<AEHB_FloorSlab>(E))Slab=S;}if(!Floor||!Slab)return false;
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);int32 Events=0;
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const auto&){++Events;};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);};
  const auto Material=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
  const auto OriginalFloor=Floor->FloorMaterial;Floor->FloorMaterial=Material;
  TestTrue(TEXT("Different floor styles have a read-only preservation plan"),UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision,true).bSucceeded);TestEqual(TEXT("Preview retains authored floor material"),Floor->FloorMaterial,Material);Floor->FloorMaterial=OriginalFloor;
  const auto OriginalSlab=Slab->SurfaceMaterial;Slab->SurfaceMaterial=Material;
  TestTrue(TEXT("Different slab styles have a seam allocation plan"),UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision,true).bSucceeded);
  Slab->VisualExpansion=2;Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
  TestTrue(TEXT("Expanded style partition can be planned without modifying actors"),UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision,true).bSucceeded);
  Slab->VisualExpansion=0;Slab->SurfaceMaterial=OriginalSlab;Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
  TestEqual(TEXT("Empty delete list is explicit"),UEHBBuildingToolset::RemoveWalls(B,{},B->RelationshipGraphRevision,false).Message,FString(TEXT("EmptyRemovalWallList")));
  TestEqual(TEXT("Unknown wall cannot trigger broad deletion"),UEHBBuildingToolset::RemoveWalls(B,{FGuid::NewGuid()},B->RelationshipGraphRevision,false).Message,FString(TEXT("UnknownRemovalWall")));
  TestEqual(TEXT("Rejected edits preserve complete building"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Rejected edits preserve receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("Rejected edits publish nothing"),Events,0);
  // Use the actual mode key callback; a refused command must consume Delete.
  FEasyHouseEditorMode Mode;GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);const auto OriginalPolygon=Floor->LocalFloorPolygon;Floor->LocalFloorPolygon[0].X+=1;
  TestTrue(TEXT("Failed native Delete is consumed"),Mode.InputKey(nullptr,nullptr,EKeys::Delete,IE_Pressed));TestNotNull(TEXT("No raw Actor deletion fallback"),B->FindElementActorByGuid(Shared));TestEqual(TEXT("Native rejection leaves receipt"),LiveNodeReceipt(B),Receipt);Floor->LocalFloorPolygon=OriginalPolygon;
  GEditor->SelectActor(Floor,true,false);TestTrue(TEXT("Mixed selection with a V2 wall cannot fall back to raw deletion"),Mode.InputKey(nullptr,nullptr,EKeys::Delete,IE_Pressed));TestEqual(TEXT("Mixed deletion refusal preserves all elements"),LiveNodeAuthoritySnapshot(B),Before);GEditor->SelectActor(Floor,false,false);
  TestTrue(TEXT("Native Backspace commits supported merge"),Mode.InputKey(nullptr,nullptr,EKeys::BackSpace,IE_Pressed));TestNull(TEXT("Native command actually deletes wall"),B->FindElementActorByGuid(Shared));TestEqual(TEXT("Native command selects surviving building"),GEditor->GetSelectedActors()->GetTop<AEHBBuildingActorBase>(),static_cast<AEHBBuildingActorBase*>(B));TestEqual(TEXT("Native command emits one receipt"),Events,1);
  TestTrue(TEXT("Native delete undo"),GEditor->UndoTransaction());TestEqual(TEXT("Native delete restores original building"),LiveNodeAuthoritySnapshot(B),Before);
 }
 // Multiple requested walls in an unfinished building can remove both enclosures,
 // while preserving every logical node and optional physical column.
 for(int32 BindingMode=0;BindingMode<2;++BindingMode)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,false,false,false))return false;
  auto* B=Fixture.Building;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  if(BindingMode)for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings)){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const auto Rooms=B->GetClosedLoopsByFloor(1);TArray<FGuid> Requested;
  for(const auto& Room:Rooms)for(FGuid Id:Room.WallGuids)if(B->GetClosedLoopsByWallGuid(Id).Num()==1){Requested.Add(Id);break;}
  if(Requested.Num()!=2)return false;const auto Before=LiveNodeAuthoritySnapshot(B);const int32 Nodes=B->WallNodeAuthority.Nodes.Num(),Physical=B->WallNodeOwnership.Bindings.Num();
  const auto Applied=UEHBBuildingToolset::RemoveWalls(B,Requested,B->RelationshipGraphRevision,false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  TestTrue(TEXT("Both enclosures are actually open"),B->GetClosedLoopsByFloor(1).IsEmpty());TestEqual(TEXT("Removal retains logical endpoints"),B->WallNodeAuthority.Nodes.Num(),Nodes);TestEqual(TEXT("Removal retains authored physical columns"),B->WallNodeOwnership.Bindings.Num(),Physical);
  const auto After=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo multi-wall removal"),GEditor->UndoTransaction());TestEqual(TEXT("Multi-wall undo all identities"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo multi-wall removal"),GEditor->RedoTransaction());TestEqual(TEXT("Multi-wall redo all geometry"),LiveNodeAuthoritySnapshot(B),After);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRemovalWriteTest,"EHBValidation.Persistence.WriteWallRemoval",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallRemovalWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("WallRemoval")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite wall-removal evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<AEHBBuildingActorBase*> Buildings;TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;B->SetActorLocationAndRotation(FVector(Mode*2400,0,0),FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(Mode==2||(Mode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const auto OldRooms=B->GetClosedLoopsByFloor(1);if(OldRooms.Num()!=2)return false;FGuid Shared;for(FGuid Id:OldRooms[0].WallGuids)if(OldRooms[1].WallGuids.Contains(Id))Shared=Id;if(!Shared.IsValid())return false;
  TArray<FGuid> OldElements;for(auto* E:B->QueryElements(FEHBElementQuery()))OldElements.Add(E->ElementGuid);
  const auto Applied=UEHBBuildingToolset::RemoveWalls(B,{Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("node"),Node.ToString());C->SetNumberField(TEXT("bindingMode"),Mode);
  TArray<TSharedPtr<FJsonValue>> Removed,OldRoomIds;for(FGuid Id:OldElements)if(!B->FindElementActorByGuid(Id))Removed.Add(MakeShared<FJsonValueString>(Id.ToString()));for(const auto& R:OldRooms)OldRoomIds.Add(MakeShared<FJsonValueString>(R.LoopGuid.ToString()));
  TestEqual(TEXT("Saved merge retired exactly wall floor slab"),Removed.Num(),3);C->SetArrayField(TEXT("removed"),Removed);C->SetArrayField(TEXT("oldRooms"),OldRoomIds);C->SetStringField(TEXT("room"),B->GetClosedLoopsByFloor(1)[0].LoopGuid.ToString());
  Cases.Add(MakeShared<FJsonValueObject>(C));Buildings.Add(B);B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save all three binding modes after actual room merge"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 for(int32 I=0;I<Buildings.Num();++I){Cases[I]->AsObject()->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(Buildings[I]));Cases[I]->AsObject()->SetStringField(TEXT("receipt"),LiveNodeReceipt(Buildings[I]));}
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.WallRemoval.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save independent merge evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRemovalReadTest,"EHBValidation.Persistence.ReadWallRemoval",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallRemovalReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("WallRemoval"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.WallRemoval.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold merge map is current world"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three saved binding modes"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Value:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=Value->AsObject();FGuid BuildingId,Node,RoomId;FGuid::Parse(C->GetStringField(TEXT("building")),BuildingId);FGuid::Parse(C->GetStringField(TEXT("node")),Node);FGuid::Parse(C->GetStringField(TEXT("room")),RoomId);
  AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==BuildingId)B=*It;if(!B)return false;
  TestEqual(TEXT("Cold merged model bindings and actual geometry"),LiveNodeAuthoritySnapshot(B),C->GetStringField(TEXT("state")));TestEqual(TEXT("Cold removal receipt preserved"),LiveNodeReceipt(B),C->GetStringField(TEXT("receipt")));
  const auto Rooms=B->GetClosedLoopsByFloor(1);TestTrue(TEXT("Actual merged room identity persisted"),Rooms.Num()==1&&Rooms[0].LoopGuid==RoomId);
  for(const auto& V:C->GetArrayField(TEXT("removed"))){FGuid Id;FGuid::Parse(V->AsString(),Id);TestNull(TEXT("Removed actor not resurrected on load"),B->FindElementActorByGuid(Id));}
  for(const auto& V:C->GetArrayField(TEXT("oldRooms"))){FGuid Id;FGuid::Parse(V->AsString(),Id);FEHBNodeRoomBoundary R;TestFalse(TEXT("Old room identity not resurrected on load"),B->TryGetRoomBoundary(Id,1,R));}
  TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Cold merged memberships recover"),B->QueryRoomDependencies(Rooms,Members));TestTrue(TEXT("Cold room owns one floor and slab"),Members.Num()==1&&Members[0].Floors.Num()==1&&Members[0].Slabs.Num()==1);
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){TArray<FEHBElementRelation> Contacts;FName Reason;TestTrue(TEXT("Cold finish contacts match actual hosts"),F->BuildSurfaceFinishRelationPlan(Rooms[0],F->FloorRegions,Contacts,Reason));TestEqual(TEXT("Cold complete contact cache"),Contacts.Num(),F->SurfaceFinishRelationGuids.Num());}
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node;});if(!N)return false;FEHBNodeMoveRequest Move;Move.NodeGuid=Node;Move.ExpectedPosition=N->LocalTransform.GetLocation();Move.TargetPosition=Move.ExpectedPosition+FVector(-35,0,0);
  const auto Applied=EHBNodeAuthorityEditing::ExecuteMoves(B,{Move},{{Node,N->GeometryRevision}},false);if(!TestTrue(*FString::Printf(TEXT("Cold merged room can still move: %s"),*Applied.Message),Applied.bSucceeded))return false;
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo cold merged edit"),GEditor->UndoTransaction());TestEqual(TEXT("Cold undo geometry identities and dependencies"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Cold undo receipt"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo cold merged edit"),GEditor->RedoTransaction());TestEqual(TEXT("Cold redo geometry and dependencies"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold redo receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3000,0));if(!TestTrue(*FString::Printf(TEXT("Cold merged building can copy: %s"),*Copy.Status.ToString()),Copy.bSucceeded))return false;TestTrue(TEXT("Undo cold merged copy"),GEditor->UndoTransaction(false));TestEqual(TEXT("Copy preserves cold source"),LiveNodeAuthoritySnapshot(B),Before);
 }
 GEditor->SelectNone(false,true,false);return true;
}
