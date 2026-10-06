// Included after wall-removal fixtures. Independent means fixed building-local
// coverage, not a missing-room fallback or a gravity/support assertion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRetainedFinishTest,"EHB.Topology.RetainedFinishLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRetainedFinishTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBWallRemovalCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Surface:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,Surface))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(Mode==2||(Mode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const FGuid Removed=Fixture.Walls[0]->ElementGuid;const auto Loops=B->GetClosedLoopsByWallGuid(Removed);if(Loops.Num()!=1)return false;const FGuid OldRoom=Loops[0].LoopGuid;
  AEHB_Floor* F=nullptr;AEHB_FloorSlab* S=nullptr;
  for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* A=Cast<AEHB_Floor>(E);A&&A->RoomLoopGuid==OldRoom)F=A;if(auto* A=Cast<AEHB_FloorSlab>(E);A&&A->RoomFillLoopGuid==OldRoom)S=A;}if(!F||!S)return false;
  S->bHasRoomFillAnchor=true;S->RoomFillAnchorWallGuid=Removed;S->RoomFillAnchorWallSide=EEHBFloorSlabWallSide::Left;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
  FEHBWallNodeModel Model;UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model);const auto* W=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Removed;});if(!W)return false;const auto Definition=*W;
  const auto FloorPolygon=F->LocalFloorPolygon,SlabPolygon=S->LocalTopPolygon;const FGuid FId=F->ElementGuid,SId=S->ElementGuid;const auto OldRelations=B->ElementRelations;
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeReceipt=LiveNodeReceipt(B);int32 Events=0,Changes=0;
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const auto&){++Events;};Observer->Observe=[&](FGuid,FName,bool){++Changes;};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);B->OnElementRelationAdded.RemoveAll(Observer);B->OnElementRelationRemoved.RemoveAll(Observer);};
  auto Remove=[&](bool Preview=false){return UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,Preview);};
  const auto Preview=Remove(true);if(!TestTrue(*FString::Printf(TEXT("Independent preview mode%d surface%d: %s"),Mode,Surface,*Preview.Message),Preview.bSucceeded))return false;
  TestEqual(TEXT("Preservation preview read only"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview emits no change"),Changes,0);
  for(int32 Phase=1;Phase<=7;++Phase){EHBWallRemovalCommand::FailurePhase=Phase;const auto R=Remove();TestEqual(TEXT("Preservation failure reaches phase"),EHBWallRemovalCommand::FailurePhase,0);TestEqual(TEXT("Preservation failure rolled back"),R.Message,FString(TEXT("WallRemovalFailedRolledBack")));TestEqual(TEXT("No partial source migration"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Previous receipt restored"),LiveNodeReceipt(B),BeforeReceipt);TestEqual(TEXT("No failed external commit"),Events,0);TestEqual(TEXT("No failed geometry or relation events"),Changes,0);}
  const auto Applied=Remove();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("Single preservation commit"),Events,1);
  auto Check=[&]()
  {
   TestEqual(TEXT("Floor identity retained"),B->FindElementActorByGuid(FId),static_cast<AEHBElementActorBase*>(F));TestEqual(TEXT("Slab identity retained"),B->FindElementActorByGuid(SId),static_cast<AEHBElementActorBase*>(S));
   TestEqual(TEXT("Floor coverage exactly retained"),F->LocalFloorPolygon,FloorPolygon);TestEqual(TEXT("Slab coverage exactly retained"),S->LocalTopPolygon,SlabPolygon);
   TestEqual(TEXT("Explicit independent floor source"),F->OutlineSource,EEHBOutlineSource::RetainedRegion);TestEqual(TEXT("Explicit independent slab source"),S->OutlineSource,EEHBOutlineSource::RetainedRegion);TestTrue(TEXT("Provenance verifies both independent outlines"),F->IsRecordedOutlineUnchanged()&&S->IsRecordedOutlineUnchanged());TestFalse(TEXT("No stale floor room"),F->RoomLoopGuid.IsValid());TestFalse(TEXT("No stale slab room"),S->RoomFillLoopGuid.IsValid());TestTrue(TEXT("No stale slab anchor"),!S->bHasRoomFillAnchor&&!S->RoomFillAnchorWallGuid.IsValid()&&S->RoomFillAnchorWallSide==EEHBFloorSlabWallSide::None);
   FEHBBuildingClosedLoop Room;TestFalse(TEXT("Independent region does not fabricate a room query"),F->TryGetRoomLoop(Room));
   TArray<FEHBElementRelation> Contacts;FName Status;if(!TestTrue(*FString::Printf(TEXT("Real retained contacts: %s"),*Status.ToString()),F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Contacts,Status)))return false;
   TestEqual(TEXT("Complete retained contact cache"),Contacts.Num(),F->SurfaceFinishRelationGuids.Num());
   for(const auto& R:Contacts){TestFalse(TEXT("No removed host contact"),R.Source.ElementGuid==Removed);TestEqual(TEXT("Contacts declare explicit region source"),R.StringMetadata.FindRef(TEXT("OutlineSource")),FString(TEXT("RetainedRegion")));TestFalse(TEXT("Contacts carry no obsolete room reference"),R.StringMetadata.Contains(TEXT("RoomLoopGuid")));const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});TestTrue(TEXT("Actual coverage matches saved contact"),Saved&&FMath::Abs(Saved->ContactArea-R.ContactArea)<0.01);}
   FEHBWallNodeModel Current;UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Current);TestTrue(TEXT("Independent lifecycle validates source policy"),FEHBCopyOutlinePolicy::Prepare(B,Current,B->QueryElements(FEHBElementQuery())).bSucceeded);return true;
  };
  if(!Check())return false;TestEqual(TEXT("Only opened room disappears"),B->GetClosedLoopsByFloor(1).Num(),1);FEHBNodeRoomBoundary Gone;TestFalse(TEXT("Old enclosure no longer resolves"),B->TryGetRoomBoundary(OldRoom,1,Gone));
  for(const auto& R:B->ElementRelations)if(R.Target.ElementGuid==FId){const auto* Old=OldRelations.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});if(Old)TestEqual(TEXT("Surviving host relation identity retained"),R.RelationGuid,Old->RelationGuid);}
  for(FGuid Changed:{FId,SId})TestTrue(TEXT("Receipt includes source-only change with unchanged outline"),B->LastCommittedEdit.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==Changed;}));
  const auto After=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo preservation"),GEditor->UndoTransaction());TestEqual(TEXT("Preservation undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo preservation"),GEditor->RedoTransaction());TestEqual(TEXT("Preservation redo exact"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Preservation redo receipt"),LiveNodeReceipt(B),Receipt);
  // Stale intent may not be relabeled as independent to bypass provenance checks.
  F->RoomLoopGuid=OldRoom;TestEqual(TEXT("Independent source cannot claim a missing room"),Remove(true).Message,FString(TEXT("StaleFloorRoomBinding")));F->RoomLoopGuid={};
  F->LocalFloorPolygon[0].X+=1;TestEqual(TEXT("Modified independent outline refuses before wall lookup"),Remove(true).Message,FString(TEXT("UnsupportedFloorCopyOutline")));F->LocalFloorPolygon=FloorPolygon;
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node;});FEHBNodeMoveRequest M;M.NodeGuid=Node;M.ExpectedPosition=N->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(-30,0,0);
  const auto Move=EHBNodeAuthorityEditing::ExecuteMoves(B,{M},{{Node,N->GeometryRevision}},false);if(!TestTrue(*FString::Printf(TEXT("Move after open: %s"),*Move.Message),Move.bSucceeded))return false;if(!Check())return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Undo post-open move"),LiveNodeAuthoritySnapshot(B),After);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(3200,0,0));if(!TestTrue(*FString::Printf(TEXT("Copy independent finishes: %s"),*Copy.Status.ToString()),Copy.bSucceeded))return false;Fixture.Actors.Append(Copy.Actors);GEditor->UndoTransaction(false);TestEqual(TEXT("Copy preserves source"),LiveNodeAuthoritySnapshot(B),After);
  auto Endpoint=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* P=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=P->GeometryRevision;E.LocalLocation=P->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=P->FloorIndex;return E;};
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=Mode!=2;Options.WallHeight=Definition.Height;Options.WallThickness=Definition.Thickness;
  const auto Closed=EHBWallCreationCommand::Commit(B,{Endpoint(Definition.StartNodeGuid),Endpoint(Definition.EndNodeGuid)},false,Options);
  if(!TestTrue(*FString::Printf(TEXT("Reclose over retained region: %s / %s"),*Closed.Status.ToString(),*Closed.FailureReason.ToString()),Closed.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);
  TestEqual(TEXT("New enclosure actually exists"),B->GetClosedLoopsByFloor(1).Num(),2);if(!Check())return false;
  const auto Reclosed=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo reclosure"),GEditor->UndoTransaction());TestEqual(TEXT("Undo reclosure restores open region"),LiveNodeAuthoritySnapshot(B),After);TestTrue(TEXT("Redo reclosure"),GEditor->RedoTransaction());TestEqual(TEXT("Redo reclosure retains independent source"),LiveNodeAuthoritySnapshot(B),Reclosed);
 }
 return true;
}
