#include "Actors/EHB_DoorWindow.h"

namespace
{
 AEHB_DoorWindow* CreateNodeHostedDoorWindow(AEHB_Wall* W,bool Door)
 {
  auto* B=W->OwningBuilding.Get();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* D=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!D)return nullptr;
  D->Kind=Door?EEHBDoorWindowElementKind::Door:EEHBDoorWindowElementKind::Window;D->SetRectangularOpeningDimensions(100,120,Door?0:90,20);
  auto Local=W->GetElementLocalTransform();Local.SetLocation(Local.TransformPosition(FVector(0,0,Door?0:90)));D->AttachToBuilding(B,Local);D->SetFloorAssignment(W->FloorIndex,EEHBBuildingFloorElementRole::HostedElement);
  D->BindToWall(W,FVector::Distance(W->LocalStart,W->LocalEnd)*.5);W->RebuildWallMesh();return D;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeDoorWindowMoveTest,"EHB.Surfaces.NodeDoorWindowMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeDoorWindowMoveTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBNodeAuthorityEditing::FailurePhase=0;};
 for(int32 Binding:{0,2})for(bool Door:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Binding,true))return false;auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
  auto* D=CreateNodeHostedDoorWindow(W,Door);if(!D)return false;Setup.Fixture.Actors.Add(D);GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
  auto Snapshot=[&](){return OpeningRegionState(B)+D->GetActorTransform().ToString()+D->OwningWallGuid.ToString()+FString::SanitizeFloat(D->DistanceFromWallStart);};const auto Before=Snapshot();
  FEHBRoomFloorWallDrag Drag;Drag.Capture(W);if(!TestTrue(*FString::Printf(TEXT("Legacy host drag capture: %s"),*Drag.Feedback.Message),Drag.Feedback.bSucceeded))return false;
  Drag.AddDelta(B->GetActorTransform().TransformVectorNoScale(FVector(25,0,0)));if(!TestTrue(*Drag.Feedback.Message,Drag.Feedback.bSucceeded))return false;TestEqual(TEXT("Legacy preview does not move authoring state"),Snapshot(),Before);
  for(int32 Phase=1;Phase<=6;++Phase){EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Drag.Execute(false);TestEqual(TEXT("Legacy fault reached"),EHBNodeAuthorityEditing::FailurePhase,0);TestEqual(TEXT("Legacy fault rolled back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Legacy actor and mesh rollback"),Snapshot(),Before);}
  const auto OldTransform=D->GetActorTransform();const auto Applied=Drag.Execute(false);if(!TestTrue(*FString::Printf(TEXT("Legacy move commit: %s"),*Applied.Message),Applied.bSucceeded))return false;
  TestTrue(TEXT("Existing door/window follows moved wall"),D->GetActorLocation().Equals(OldTransform.GetLocation()+B->GetActorTransform().TransformVectorNoScale(FVector(25,0,0)),0.001));TestEqual(TEXT("Opening source identity retained"),W->DoorWindowConnections[0].DoorWindowGuid,D->ElementGuid);
  const auto After=Snapshot();GEditor->UndoTransaction();TestEqual(TEXT("Legacy move undo"),Snapshot(),Before);GEditor->RedoTransaction();TestEqual(TEXT("Legacy move redo"),Snapshot(),After);GEditor->UndoTransaction();
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Model).bSucceeded)return false;const auto* Edge=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!Edge)return false;
  const auto* A=Model.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->StartNodeGuid;});const auto* Z=Model.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->EndNodeGuid;});if(!A||!Z)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto TooShort=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Z->NodeGuid,Z->LocalTransform.GetLocation(),A->LocalTransform.GetLocation()+(Z->LocalTransform.GetLocation()-A->LocalTransform.GetLocation()).GetSafeNormal()*100,true);
  TestFalse(TEXT("Short wall cannot silently clamp legacy door/window"),TooShort.bSucceeded);TestEqual(TEXT("Shortening rejection leaves original object"),Snapshot(),Before);
  const auto Turned=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Z->NodeGuid,Z->LocalTransform.GetLocation(),Z->LocalTransform.GetLocation()+FVector(25,0,0),false);
  if(!TestTrue(*FString::Printf(TEXT("Legacy host follows angled wall: %s"),*Turned.Message),Turned.bSucceeded))return false;
  TestTrue(TEXT("Actor orientation follows wall angle"),D->GetActorRotation().Equals(W->GetActorRotation(),0.001));const auto Angled=Snapshot();GEditor->UndoTransaction();TestEqual(TEXT("Angled legacy edit undo"),Snapshot(),Before);GEditor->RedoTransaction();TestEqual(TEXT("Angled legacy edit redo"),Snapshot(),Angled);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningRoomSlabEntryTest,"EHB.Surfaces.OpeningRoomSlabEntry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningRoomSlabEntryTest::RunTest(const FString& Parameters)
{
 for(int32 Kind:{0,1,2})for(int32 Binding:{0,2})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Binding,true))return false;
  auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));
  const auto Rooms=B->GetClosedLoopsByWallGuid(W->ElementGuid);const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==S->RoomFillLoopGuid;});if(!Room)return false;
  TArray<FVector> Before,After;TMap<FGuid,FVector> Positions;
  if(!TestTrue(TEXT("Uncut room outline available"),FEasyHouseEditorMode::BuildRoomSlabFollowOutline(S,*Room,Positions,Before)))return false;
  if(Kind<2){auto* D=CreateNodeHostedDoorWindow(W,Kind==0);if(!D)return false;Setup.Fixture.Actors.Add(D);}
  else {FName Status;TArray<FEHBLogicalSurfaceDefinition> Hosts;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;W->CutOperations={SurfaceOpening129::Make(Hosts[0],40,120)};W->RebuildWallMesh();}
  if(!TestTrue(TEXT("Room outline remains available after adding opening"),FEasyHouseEditorMode::BuildRoomSlabFollowOutline(S,*Room,Positions,After)))return false;
  TestEqual(TEXT("Opening does not change room perimeter"),After,Before);
  FEHBWallNodeModel Model;FName Status;TArray<FEHBWallJunctionWallSides> Sides;
  if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Model).bSucceeded||!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Status))return false;
  TestTrue(TEXT("Viewport slab outline accepts cut wall sides"),FEasyHouseEditorMode::BuildRoomSlabOutlineFromWallSides(S,*Room,Sides,After));TestEqual(TEXT("Viewport outline agrees"),After,Before);
  TestTrue(TEXT("Room fill button accepts room with opening"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(S));
 }
 return true;
}
