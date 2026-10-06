#include "EHBFinishRegionCommand.h"
#include "EHBFinishRegionDrag.h"
#include "Widgets/SEHBElementEditorPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFinishRegionEditingTest,"EHB.Floors.IndependentRegionEditing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFinishRegionEditingTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Surface:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,Surface))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(Mode==2||(Mode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const FGuid Removed=Fixture.Walls[0]->ElementGuid,OldRoom=B->GetClosedLoopsByWallGuid(Removed)[0].LoopGuid;
  AEHB_Floor* F=nullptr;AEHB_FloorSlab* S=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* A=Cast<AEHB_Floor>(E);A&&A->RoomLoopGuid==OldRoom)F=A;if(auto* A=Cast<AEHB_FloorSlab>(E);A&&A->RoomFillLoopGuid==OldRoom)S=A;}if(!F||!S)return false;
  FEHBWallNodeModel Model;UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model);const auto Definition=*Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Removed;});
  if(!UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,false).bSucceeded)return false;
  auto Edit=[&](AEHBElementActorBase* E,const TArray<FVector>& Polygon,FGuid Room=FGuid(),bool Preview=false){return UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),Polygon,Room,Preview);};
  auto CheckContacts=[&]()
  {
   TArray<FEHBElementRelation> Actual;FName Why;if(!TestTrue(*Why.ToString(),F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Actual,Why)))return false;
   TestEqual(TEXT("Complete current finish contact cache"),Actual.Num(),F->SurfaceFinishRelationGuids.Num());
   for(const auto& R:Actual){const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});TestTrue(TEXT("Saved contact matches actual geometry and source"),Saved&&FMath::Abs(Saved->ContactArea-R.ContactArea)<0.01&&Saved->StringMetadata.OrderIndependentCompareEqual(R.StringMetadata));}
   FEHBWallNodeModel Current;UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Current);TestTrue(TEXT("Edited outline remains valid for future commands"),FEHBCopyOutlinePolicy::Prepare(B,Current,B->QueryElements(FEHBElementQuery())).bSucceeded);return true;
  };
  for(auto* E:TArray<AEHBElementActorBase*>{S,F})
  {
   const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const auto Original=EHBFinishRegionCommand::GetPolygon(E);auto Smaller=Original;const FBox Box(Original);for(auto& P:Smaller){P.X=Box.GetCenter().X+(P.X-Box.GetCenter().X)*0.8;P.Y=Box.GetCenter().Y+(P.Y-Box.GetCenter().Y)*0.8;}
   const auto OldRelations=B->ElementRelations;int32 Commits=0,Changes=0;
   auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const auto&){++Commits;};Observer->Observe=[&](FGuid,FName,bool){++Changes;};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);B->OnElementRelationAdded.RemoveAll(Observer);B->OnElementRelationRemoved.RemoveAll(Observer);};
   const auto Preview=Edit(E,Smaller,{},true);if(!TestTrue(*FString::Printf(TEXT("Resize preview mode%d surface%d: %s"),Mode,Surface,*Preview.Message),Preview.bSucceeded))return false;
   TestEqual(TEXT("Read-only preview"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview no events"),Changes,0);
   const int32 Queue=GEditor->Trans->GetQueueLength();TestEqual(TEXT("No-op resize"),Edit(E,Original).Message,FString(TEXT("NoChange")));TestEqual(TEXT("No-op has no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
   TestEqual(TEXT("Stale geometry intent refused"),UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid)-1,Smaller,{},false).Message,FString(TEXT("StaleRegionRevision")));
   auto Raised=Smaller;Raised[0].Z+=2;TestFalse(TEXT("Non-planar edit refused"),Edit(E,Raised).bSucceeded);
   auto Invalid=Smaller;Invalid[1]=Invalid[0];if(!TestFalse(TEXT("Degenerate edit refused"),Edit(E,Invalid).bSucceeded))return false;
   auto Crossing=Smaller;Swap(Crossing[1],Crossing[2]);if(!TestEqual(TEXT("Crossing edges explicitly refused"),Edit(E,Crossing).Message,FString(TEXT("SelfIntersectingRegionPolygon"))))return false;
   auto Overlapping=Smaller;Overlapping.Insert((Overlapping[0]+Overlapping[1])*0.5,2);if(!TestEqual(TEXT("Backtracking adjacent edges explicitly refused"),Edit(E,Overlapping).Message,FString(TEXT("SelfIntersectingRegionPolygon"))))return false;
   TestFalse(TEXT("Unrelated room cannot be used as rebind destination"),Edit(E,{},FGuid::NewGuid()).bSucceeded);
   for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;const auto R=Edit(E,Smaller);TestEqual(TEXT("Actual resize rollback phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Resize failure rolled back"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestEqual(TEXT("Resize rollback exact authored and generated state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Resize rollback receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("No failed commit"),Commits,0);TestEqual(TEXT("No failed external changes"),Changes,0);}
   const auto Applied=Edit(E,Smaller);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("One resize commit"),Commits,1);TestEqual(TEXT("Exact requested local outline"),EHBFinishRegionCommand::GetPolygon(E),Smaller);TestTrue(TEXT("Resize retains explicit independent source"),EHBFinishRegionCommand::IsIndependent(E));if(!CheckContacts())return false;
   for(const auto& R:B->ElementRelations)if(R.Type==EEHBElementRelationType::SurfaceFinish){const auto* Old=OldRelations.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});if(Old)TestEqual(TEXT("Surviving host-floor IDs stable"),R.RelationGuid,Old->RelationGuid);}
   TestTrue(TEXT("Receipt names resized surface"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& V){return V.ElementGuid==E->ElementGuid;}));
   const auto After=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo resize"),GEditor->UndoTransaction());TestEqual(TEXT("Resize undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo resize"),GEditor->RedoTransaction());TestEqual(TEXT("Resize redo exact"),LiveNodeAuthoritySnapshot(B),After);
  }
  const auto Resized=LiveNodeAuthoritySnapshot(B);FEHBFinishRegionDrag Drag;Drag.Begin(S,0,1,true);TestTrue(TEXT("Slab drag captures without mutation"),Drag.bReady);Drag.Update(S->GetActorTransform().TransformVector(FVector(4,4,0)));TestEqual(TEXT("Dragging only edits preview"),LiveNodeAuthoritySnapshot(B),Resized);Drag.bCancelled=true;TestTrue(TEXT("Cancel drag succeeds without commit"),Drag.Execute(false).bSucceeded);TestEqual(TEXT("Cancelled drag leaves exact surface"),LiveNodeAuthoritySnapshot(B),Resized);
  auto Endpoint=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* P=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=P->GeometryRevision;E.LocalLocation=P->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=P->FloorIndex;return E;};
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=Mode!=2;Options.WallHeight=Definition.Height;Options.WallThickness=Definition.Thickness;const auto Closed=EHBWallCreationCommand::Commit(B,{Endpoint(Definition.StartNodeGuid),Endpoint(Definition.EndNodeGuid)},false,Options);if(!TestTrue(*Closed.Status.ToString(),Closed.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);
  FEHBSurfaceRoomCoverage Coverage;FName Why;if(!B->QuerySurfaceRoomCoverage(F->ElementGuid,Coverage,Why)||Coverage.Rooms.Num()!=1)return false;const FGuid NewRoom=Coverage.Rooms[0].RoomGuid;
  for(auto* E:TArray<AEHBElementActorBase*>{S,F})
  {
   const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);TestTrue(TEXT("Rebind preview"),Edit(E,{},NewRoom,true).bSucceeded);TestEqual(TEXT("Rebind preview read-only"),LiveNodeAuthoritySnapshot(B),Before);
   for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;const auto R=Edit(E,{},NewRoom);TestEqual(TEXT("Rebind failure phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Rebind rollback"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestEqual(TEXT("Rebind rollback state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Rebind rollback receipt"),LiveNodeReceipt(B),Receipt);}
   const auto Applied=Edit(E,{},NewRoom);if(!TestTrue(*FString::Printf(TEXT("Actual rebind: %s"),*Applied.Message),Applied.bSucceeded))return false;
   TestFalse(TEXT("Explicit rebind ends independent ownership"),EHBFinishRegionCommand::IsIndependent(E));TestTrue(TEXT("Receipt includes overlapping room"),Applied.CommittedEdit.RoomGuids.Contains(NewRoom));if(!CheckContacts())return false;
   const auto After=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo rebind"),GEditor->UndoTransaction());TestEqual(TEXT("Rebind undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo rebind"),GEditor->RedoTransaction());TestEqual(TEXT("Rebind redo exact"),LiveNodeAuthoritySnapshot(B),After);
  }
  TestEqual(TEXT("Floor bound to chosen actual room"),F->RoomLoopGuid,NewRoom);TestEqual(TEXT("Slab bound to chosen actual room"),S->RoomFillLoopGuid,NewRoom);TestTrue(TEXT("Slab has valid new wall anchor"),S->bHasRoomFillAnchor&&B->FindElementActorByGuid(S->RoomFillAnchorWallGuid));
  const auto Final=LiveNodeAuthoritySnapshot(B);const auto Copy=EHBBuildingCopy::Execute(B,FVector(3200,0,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;Fixture.Actors.Append(Copy.Actors);GEditor->UndoTransaction(false);TestEqual(TEXT("Copy preserves rebound source"),LiveNodeAuthoritySnapshot(B),Final);
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node;});FEHBNodeMoveRequest Move;Move.NodeGuid=Node;Move.ExpectedPosition=N->LocalTransform.GetLocation();Move.TargetPosition=Move.ExpectedPosition+FVector(-25,0,0);const auto Moved=EHBNodeAuthorityEditing::ExecuteMoves(B,{Move},{{Node,N->GeometryRevision}},false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;if(!CheckContacts())return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Rebound finish resumes wall-following lifecycle"),LiveNodeAuthoritySnapshot(B),Final);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFinishRegionControlsTest,"EHB.Floors.IndependentRegionControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFinishRegionControlsTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};FTransientTopologyFixture Fixture;
 if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;auto* B=Fixture.Building;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
 const FGuid Removed=Fixture.Walls[0]->ElementGuid,Room=B->GetClosedLoopsByWallGuid(Removed)[0].LoopGuid;AEHB_Floor* F=nullptr;AEHB_FloorSlab* S=nullptr;
 for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* A=Cast<AEHB_Floor>(E);A&&A->RoomLoopGuid==Room)F=A;if(auto* A=Cast<AEHB_FloorSlab>(E);A&&A->RoomFillLoopGuid==Room)S=A;}if(!F||!S)return false;
 if(!UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,false).bSucceeded)return false;
 const auto Initial=LiveNodeAuthoritySnapshot(B);const auto Original=F->LocalFloorPolygon;auto Overlap=Original;FBox Box(Original);for(auto& P:Overlap)if(P.X>Box.GetCenter().X)P.X+=100;
 TestEqual(TEXT("Adjacent finish is protected from resize overlap"),UEHBBuildingToolset::EditFinishRegion(B,F->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(F->ElementGuid),Overlap,{},false).Message,FString(TEXT("RegionOverlapsAnotherSurface")));
 TestEqual(TEXT("Overlap refusal preserves complete state"),LiveNodeAuthoritySnapshot(B),Initial);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(F,true,false);auto Panel=SNew(SEHBElementEditorPanel);Panel->SetSelectedActor(F);
 const float Width=Panel->GetIndependentRegionSize(true);Panel->HandleIndependentRegionSize(Width*0.8,ETextCommit::OnEnter,true);
 TestTrue(TEXT("Real panel callback applies centimeter width"),FMath::IsNearlyEqual(Panel->GetIndependentRegionSize(true),Width*0.8,0.001f));TestTrue(TEXT("Panel keeps source verified"),F->IsRecordedOutlineUnchanged());
 const auto Smaller=LiveNodeAuthoritySnapshot(B);Panel->HandleIndependentRegionSize(10,ETextCommit::OnCleared,true);TestEqual(TEXT("Cancelled text input does not resize"),LiveNodeAuthoritySnapshot(B),Smaller);
 Panel->HandleIndependentRegionBind();TestEqual(TEXT("Open room button safely preserves floor"),LiveNodeAuthoritySnapshot(B),Smaller);
 TestTrue(TEXT("Panel command is undoable"),GEditor->UndoTransaction(false));TestEqual(TEXT("Panel undo exact source"),LiveNodeAuthoritySnapshot(B),Initial);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);FEasyHouseEditorMode Mode;Mode.SelectedFloorSlab=S;Mode.SelectedFloorSlabHandleKind=EEHBFloorSlabEditHandleKind::Corner;Mode.SelectedFloorSlabHandleLoopIndex=INDEX_NONE;Mode.SelectedFloorSlabHandleFirstIndex=0;Mode.SelectedFloorSlabHandleSecondIndex=1;
 TestTrue(TEXT("Native tracking captures retained corner"),Mode.StartTracking(nullptr,nullptr));TestTrue(TEXT("Retained tracking uses no legacy transaction"),Mode.FinishRegionDrag.bReady&&!Mode.ActiveFloorSlabEditTransaction.IsValid());
 FVector Delta=S->GetActorTransform().TransformVector((FBox(S->LocalTopPolygon).GetCenter()-S->LocalTopPolygon[0])*0.25);Delta.Z=0;FRotator Rotation=FRotator::ZeroRotator;FVector Scale=FVector::ZeroVector;
 TestTrue(TEXT("Native input consumes retained drag"),Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale));TestEqual(TEXT("Native drag remains read-only"),LiveNodeAuthoritySnapshot(B),Initial);TestTrue(TEXT("Native outline draft validates"),Mode.FinishRegionDrag.Feedback.bSucceeded);
 TestTrue(TEXT("Native Escape cancels retained drag"),Mode.InputKey(nullptr,nullptr,EKeys::Escape,IE_Pressed));TestTrue(TEXT("Cancelled tracking consumed"),Mode.EndTracking(nullptr,nullptr));TestEqual(TEXT("Escape cancellation exact"),LiveNodeAuthoritySnapshot(B),Initial);
 Mode.StartTracking(nullptr,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);const auto Planned=Mode.FinishRegionDrag.Polygon;if(!TestTrue(TEXT("Nontrivial corner preview"),Planned!=S->LocalTopPolygon))return false;Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Native release applies exact preview"),S->LocalTopPolygon,Planned);TestTrue(TEXT("Native release keeps provenance"),S->IsRecordedOutlineUnchanged());TestTrue(TEXT("Native corner undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Corner undo source and contacts"),LiveNodeAuthoritySnapshot(B),Initial);
 Mode.SelectedFloorSlabHandleKind=EEHBFloorSlabEditHandleKind::Edge;Mode.StartTracking(nullptr,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Mode.SelectedFloorSlab=nullptr;Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Selection loss cancels old draft"),LiveNodeAuthoritySnapshot(B),Initial);
 Mode.SelectedFloorSlab=S;Mode.SelectedFloorSlabHandleKind=EEHBFloorSlabEditHandleKind::Edge;Mode.SelectedFloorSlabHandleLoopIndex=INDEX_NONE;Mode.SelectedFloorSlabHandleFirstIndex=0;Mode.SelectedFloorSlabHandleSecondIndex=1;
 const int32 Corners=S->LocalTopPolygon.Num();Mode.HandleFloorSlabToolbarCommand(3);TestEqual(TEXT("Toolbar inserts an authored corner"),S->LocalTopPolygon.Num(),Corners+1);TestTrue(TEXT("Inserted corner source stays verified"),S->IsRecordedOutlineUnchanged());TestTrue(TEXT("Insert-corner undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Insert-corner undo exact"),LiveNodeAuthoritySnapshot(B),Initial);
 Mode.HandleFloorSlabToolbarCommand(0);TestEqual(TEXT("Unsupported cutter leaves independent provenance and contacts intact"),LiveNodeAuthoritySnapshot(B),Initial);
 // A panel-driven corner distance uses the same command, including rejection.
 Panel->SetSelectedActor(S);Panel->SetSelectedFloorSlabCorner(S,INDEX_NONE,0);const float Distance=Panel->GetFloorSlabNextCornerDistance();Panel->HandleFloorSlabCornerDistanceCommitted(Distance*0.75,ETextCommit::OnEnter,false);
 TestTrue(TEXT("Corner distance keeps independent source"),S->IsRecordedOutlineUnchanged());TestTrue(TEXT("Corner distance applies requested centimeters"),FMath::IsNearlyEqual(Panel->GetFloorSlabNextCornerDistance(),Distance*0.75,0.001));
 return true;
}
