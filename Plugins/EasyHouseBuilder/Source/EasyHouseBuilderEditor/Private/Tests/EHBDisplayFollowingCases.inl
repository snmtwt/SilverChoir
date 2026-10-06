#include "Components/EHBPlanarSurfaceComponent.h"
namespace
{
 bool InitializeBoundDisplays(FStyledMergeFixture& Setup)
 {
  auto* B=Setup.Building();
  FEHBSlabDisplayPlan Initial;FName Status;
  for(FGuid Id:Setup.Slabs)
  {
   auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=40;if(!S->RebuildSlabMesh())return false;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
   auto& P=Initial.Slabs.AddDefaulted_GetRef();P.Actor=S;P.Polygon=S->LocalTopPolygon;
  }
  Initial.DisplayLayers.Add(Initial.Slabs[0].Actor);if(!Initial.Prepare(Status))return false;
  int32 Masks=0;for(const auto& P:Initial.Slabs)if(P.Display.IsActive()){if(!P.Actor->SetPartitionedSlabOutline(P.Polygon,P.Display))return false;P.Actor->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);++Masks;}
  if(Masks==0||!Initial.Verify(Status))return false;

  return true;
 }
 FEHBToolsetOperationResult MoveBoundDisplayNode(AEHBBuildingActorBase* B,FGuid NodeId)
 {
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==NodeId;});if(!N)return {};
  FEHBNodeMoveRequest R;R.NodeGuid=NodeId;R.ExpectedPosition=N->LocalTransform.GetLocation();R.TargetPosition=R.ExpectedPosition+FVector(0,-40,0);
  return EHBNodeAuthorityEditing::ExecuteMoves(B,{R},{{NodeId,N->GeometryRevision}},false);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayFollowingTest,"EHB.Topology.PartitionedSlabFollowing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayFollowingTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBNodeAuthorityEditing::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 {
  auto* Surface=NewObject<UEHBPlanarSurfaceComponent>();TArray<FEHBPlanarSurfaceRegion> Regions;
  for(double X:{0.0,200.0}){auto& R=Regions.AddDefaulted_GetRef();R.BoundaryLoop={{X,0,0},{X+100,0,0},{X+100,100,0},{X,100,0}};R.HoleLoops.AddDefaulted_GetRef().LocalLoop={{X+30,30,0},{X+30,70,0},{X+70,70,0},{X+70,30,0}};}
  Surface->SetPlanarSurfaceRegions(Regions,FVector::UpVector,100);TArray<FEHBSurfaceSideSnapEdge> Edges;TestTrue(TEXT("Two disconnected perforated regions expose edges"),Surface->BuildSideSnapEdges(Edges));TestEqual(TEXT("Both outer and hole edges kept"),Edges.Num(),16);
  TSet<int32> HoleIds;for(const auto& Edge:Edges)if(Edge.Endpoint.SurfaceKind==EEHBElementSurfaceKind::Opening)HoleIds.Add(Edge.Endpoint.SubIndex);TestEqual(TEXT("Hole loop indices distinct across fragments"),HoleIds.Num(),2);
  FVector Projected;FVector2D UV;TestFalse(TEXT("Gap is not a plane snap target"),Surface->ProjectWorldPointToSurface(FVector(150,50,10),Projected,UV));TestFalse(TEXT("Second fragment hole is not a plane target"),Surface->ProjectWorldPointToSurface(FVector(250,50,10),Projected,UV));TestTrue(TEXT("Second fragment solid is selectable"),Surface->ProjectWorldPointToSurface(FVector(210,10,10),Projected,UV));
 }

 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  if(!InitializeBoundDisplays(Setup))return false;
  auto MaskState=[&](){FString Result;for(FGuid Id:Setup.Slabs){FString V;FJsonObjectConverter::UStructToJsonObjectString(CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id))->DisplayPartition,V);Result+=V;}return Result;};
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;
  const auto* Wall=Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Setup.Shared;});if(!Wall)return false;const FGuid NodeId=Wall->StartNodeGuid;
  auto Move=[&](bool Preview){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==NodeId;});FEHBNodeMoveRequest R;R.NodeGuid=NodeId;R.ExpectedPosition=N->LocalTransform.GetLocation();R.TargetPosition=R.ExpectedPosition+FVector(0,-40,0);return EHBNodeAuthorityEditing::ExecuteMoves(B,{R},{{NodeId,N->GeometryRevision}},Preview);};
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeMasks=MaskState(),Receipt=LiveNodeReceipt(B);const auto Preview=Move(true);if(!TestTrue(*FString::Printf(TEXT("Partition follow preview mode%d: %s"),Mode,*Preview.Message),Preview.bSucceeded))return false;
  TestEqual(TEXT("Following preview preserves source"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Following preview preserves masks"),MaskState(),BeforeMasks);
  for(int32 Phase:{4,5,6,3})
  {
   EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Move(false);TestFalse(TEXT("Following injected failure refuses"),R.bSucceeded);TestEqual(TEXT("Following failure point reached"),EHBNodeAuthorityEditing::FailurePhase,0);
   TestEqual(TEXT("Following rollback source"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Following rollback masks"),MaskState(),BeforeMasks);TestEqual(TEXT("Following rollback receipt"),LiveNodeReceipt(B),Receipt);
  }
  const auto R=Move(false);if(!TestTrue(*FString::Printf(TEXT("Partition follow apply: %s"),*R.Message),R.bSucceeded))return false;
  TestNotEqual(TEXT("Room outlines changed"),LiveNodeAuthoritySnapshot(B),Before);TestNotEqual(TEXT("Display allocation changed"),MaskState(),BeforeMasks);
  for(FGuid Id:Setup.Slabs){const auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));TestEqual(TEXT("Following keeps room ownership"),S->OutlineSource,EEHBOutlineSource::RoomBoundary);TestTrue(TEXT("Following provenance recorded"),S->IsRecordedOutlineUnchanged());if(S->DisplayPartition.IsActive())TestEqual(TEXT("Following mask source updated"),S->DisplayPartition.SourcePolygon,S->LocalTopPolygon);}
  int32 MultiRegionSlabs=0;
  for(FGuid Id:Setup.Slabs)
  {
   auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));if(S->DisplayPartition.Regions.Num()<2)continue;++MultiRegionSlabs;
   TArray<FEHBPlanarSurfaceRegion> Regions;TestTrue(TEXT("Full display query supports islands"),S->BuildEffectiveDisplayRegions(Regions));TestEqual(TEXT("No display region discarded"),Regions.Num(),S->DisplayPartition.Regions.Num());
   TArray<FVector> Single;TestFalse(TEXT("Single-loop API refuses multi-region truncation"),S->BuildEffectiveOuterPolygon(Single));TestTrue(TEXT("Refused single-loop output is empty"),Single.IsEmpty());
   TArray<TArray<FVector>> WorldLoops;TestTrue(TEXT("World query supports islands"),S->BuildEffectiveOuterWorldPolygons(WorldLoops));TestEqual(TEXT("World query includes all regions"),WorldLoops.Num(),Regions.Num());
   TArray<UEHBGeneratedMeshComponent*> Meshes;S->GetGeneratedMeshComponents(Meshes);auto* Surface=Meshes.IsEmpty()?nullptr:Cast<UEHBPlanarSurfaceComponent>(Meshes[0]);if(!Surface)return false;
   TestEqual(TEXT("Surface component keeps all regions"),Surface->GetPlanarSurfaceRegions().Num(),Regions.Num());TArray<FEHBSurfaceSideSnapEdge> Edges;TestTrue(TEXT("Multi-region side edges available"),Surface->BuildSideSnapEdges(Edges));int32 ExpectedEdges=0;for(const auto& V:Regions){ExpectedEdges+=V.BoundaryLoop.Num();for(const auto& H:V.HoleLoops)ExpectedEdges+=H.LocalLoop.Num();}TestEqual(TEXT("Every fragment edge is represented"),Edges.Num(),ExpectedEdges);
   for(const auto& Region:Regions)if(Region.BoundaryLoop.Num()==3){const auto Center=(Region.BoundaryLoop[0]+Region.BoundaryLoop[1]+Region.BoundaryLoop[2])/3;FVector Projected;FVector2D UV;TestTrue(TEXT("Disconnected triangle remains a selectable surface"),Surface->ProjectWorldPointToSurface(Surface->GetComponentTransform().TransformPosition(Center+FVector(0,0,5)),Projected,UV));}
   TestTrue(TEXT("Component rebuild preserves slab extrusion"),Surface->RebuildSurfaceMesh());
  }
  if(Mode==2)TestTrue(TEXT("Logical-column scenario exercises real multiple fragments"),MultiRegionSlabs>0);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterMasks=MaskState();GEditor->UndoTransaction();TestEqual(TEXT("Following undo source"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Following undo masks"),MaskState(),BeforeMasks);GEditor->RedoTransaction();TestEqual(TEXT("Following redo source"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Following redo masks"),MaskState(),AfterMasks);
  const auto Again=Move(false);if(!TestTrue(*FString::Printf(TEXT("Repeated follow: %s"),*Again.Message),Again.bSucceeded))return false;GEditor->UndoTransaction();TestEqual(TEXT("Repeated follow undo source"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Repeated follow undo masks"),MaskState(),AfterMasks);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(3000,0,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;
  for(FGuid Id:Setup.Slabs){auto* Original=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));auto* Copied=CastChecked<AEHB_FloorSlab>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(Id)));FString A,C;FJsonObjectConverter::UStructToJsonObjectString(Original->DisplayPartition,A);FJsonObjectConverter::UStructToJsonObjectString(Copied->DisplayPartition,C);TestEqual(TEXT("Copy preserves followed multi-region data"),C,A);TestTrue(TEXT("Copied followed slab rebuilds"),Copied->RebuildSlabMesh());}GEditor->UndoTransaction(false);
 }
 return true;
}
