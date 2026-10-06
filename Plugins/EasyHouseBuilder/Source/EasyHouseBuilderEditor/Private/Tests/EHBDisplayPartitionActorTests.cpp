#include "Core/EHBDisplayPartition.h"
#include "Actors/EHB_FloorSlab.h"
#include "EHB_Building.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayPartitionActorTest,"EHB.Surfaces.DisplayPartitionActor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayPartitionActorTest::RunTest(const FString& Parameters)
{
 auto* W=GEditor->GetEditorWorldContext().World();auto* B=W->SpawnActor<AEHB_Building>();auto* A=W->SpawnActor<AEHB_FloorSlab>();auto* C=W->SpawnActor<AEHB_FloorSlab>();ON_SCOPE_EXIT{A->Destroy();C->Destroy();B->Destroy();};
 TArray<FEHBDisplayPartitionInput> Inputs;TArray<AEHB_FloorSlab*> Actors={A,C};FName Status;
 for(int32 I=0;I<2;++I)
 {
  auto* S=Actors[I];S->AttachToBuilding(B,FTransform::Identity);S->FloorIndex=1;S->RoomFillFloorIndex=1;
  const double X=I*100;TArray<FVector> P={{X,0,0},{X+100,0,0},{X+100,100,0},{X,100,0}};
  if(!TestTrue(TEXT("Create native source slab"),S->SetSlabOutline(P,{})))return false;S->VisualExpansion=10;S->RebuildSlabMesh();
  TArray<FEHBLogicalSurfaceDefinition> Base;if(!S->QueryLogicalBaseSurfaces(Base,Status))return false;
  auto& In=Inputs.AddDefaulted_GetRef();In.Base=Base[0];In.Priority=I;auto& R=In.RequestedDisplay.AddDefaulted_GetRef();R.Boundary={{X-10,-10},{X+110,-10},{X+110,110},{X-10,110}};
 }
 TArray<FEHBLogicalSurfaceDefinition> Allocated;if(!FEHBDisplayPartition::Build(Inputs,Allocated,Status))return false;
 {
  FScopedTransaction Transaction(NSLOCTEXT("EHBTest","DisplayPartition","Apply slab display partition"));
  for(int32 I=0;I<2;++I){auto* S=Actors[I];FEHBSlabDisplayPartition P;P.SourcePolygon=S->LocalTopPolygon;P.SourceExpansion=S->VisualExpansion;P.Priority=I;P.Regions=Allocated[I].Regions;
   TestTrue(TEXT("Read-only candidate accepts partition"),S->ValidatePartitionedSlabOutline(P.SourcePolygon,P));TestFalse(TEXT("Candidate check did not change actor"),S->DisplayPartition.IsActive());
   if(!TestTrue(TEXT("Apply actual partitioned slab"),S->SetPartitionedSlabOutline(P.SourcePolygon,P)))return false;
  }
 }
 TArray<FEHBFloorSupportSurface> Tops;TArray<FEHBFloorFinishRegion> Regions;double Sum=0;
 for(auto* S:Actors)
 {
  TArray<FEHBFloorSupportSurface> Actual;if(!TestTrue(TEXT("Capture actual allocated mesh"),FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(S,Actual,Status)))return false;
  TArray<FEHBFloorFinishRegion> Own;for(const auto& T:Actual){auto& R=Own.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double Area=0;FEHBFloorContactGeometry::MeasureArea(Own,Actual,Area,Status);Sum+=Area;Tops.Append(Actual);Regions.Append(Own);
  TArray<FVector> Query;TestTrue(TEXT("Effective allocated query"),S->BuildEffectiveOuterPolygon(Query));TestEqual(TEXT("Query is persisted display boundary"),Query.Num(),S->DisplayPartition.Regions[0].Boundary.Num());
  TArray<FEHBLogicalSurfaceDefinition> Base;TestTrue(TEXT("Logical source remains queryable"),S->QueryLogicalBaseSurfaces(Base,Status));TestEqual(TEXT("Expansion allocation cannot alter support footprint"),Base[0].GetAreaCm2(),10000.0);
 }
 double Union=0;FEHBFloorContactGeometry::MeasureArea(Regions,Tops,Union,Status);TestTrue(TEXT("Actual mesh union covered without overlap"),FMath::IsNearlyEqual(Sum,26400.0,0.01)&&FMath::IsNearlyEqual(Union,Sum,0.01));
 auto Changed=A->LocalTopPolygon;Changed[0].X+=1;TestFalse(TEXT("Ordinary edit cannot reuse stale partition"),A->ValidateSlabOutline(Changed,{}));
 GEditor->UndoTransaction();TestFalse(TEXT("Undo restores old display policy"),A->DisplayPartition.IsActive());TestFalse(TEXT("Undo restores both actors"),C->DisplayPartition.IsActive());
 GEditor->RedoTransaction();TestTrue(TEXT("Redo restores display ownership"),A->DisplayPartition.IsActive()&&C->DisplayPartition.IsActive());
 A->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);TestTrue(TEXT("Partition included in provenance"),A->IsRecordedOutlineUnchanged());A->DisplayPartition.Priority+=1;TestFalse(TEXT("Changed display invalidates provenance"),A->IsRecordedOutlineUnchanged());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayPartitionCutStateTest,"EHB.Surfaces.DisplayPartitionCutState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayPartitionCutStateTest::RunTest(const FString& Parameters)
{
 auto* W=GEditor->GetEditorWorldContext().World();auto* B=W->SpawnActor<AEHB_Building>();auto* S=W->SpawnActor<AEHB_FloorSlab>();ON_SCOPE_EXIT{S->Destroy();B->Destroy();};S->AttachToBuilding(B,FTransform::Identity);S->FloorIndex=1;S->RoomFillFloorIndex=1;S->VisualExpansion=0;
 const TArray<FVector> Outer={{0,0,0},{500,0,0},{500,500,0},{0,500,0}};TArray<FEHBFloorSlabHole> Holes;Holes.AddDefaulted_GetRef().LocalPolygon={{300,100,0},{350,100,0},{350,150,0},{300,150,0}};
 FEHBCutOperation Cut;for(const auto& P:TArray<FVector>{{200,-100,0},{220,-100,0},{220,600,0},{200,600,0}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Cut.EnsureGuids();
 FEHBSlabDisplayPartition Partition;FName Status;if(!S->BuildCandidateDisplayRegions(Outer,Holes,{Cut},Partition.Regions,Status))return false;S->CaptureDisplayPartitionSource(Outer,Holes,{Cut},Partition);
 if(!TestTrue(TEXT("Complete state applied atomically"),S->SetPartitionedSlabState(Outer,Holes,{Cut},Partition)))return false;
 TestEqual(TEXT("Hole preserved"),S->LocalHoles.Num(),1);TestEqual(TEXT("Cut preserved"),S->CutOperations.Num(),1);TestTrue(TEXT("Rebuild validates persisted complete source"),S->RebuildSlabMesh());
 auto ChangedCut=Cut;ChangedCut.Source.Height+=1;TestFalse(TEXT("Cut depth drift rejects stale partition"),S->SetPartitionedSlabState(Outer,Holes,{ChangedCut},Partition));
 auto ChangedHoles=Holes;ChangedHoles[0].LocalPolygon[0].X+=1;TestFalse(TEXT("Hole drift rejected"),S->SetPartitionedSlabState(Outer,ChangedHoles,{Cut},Partition));
 auto Unknown=Partition;Unknown.SourceVersion=99;TestFalse(TEXT("Unknown source version rejected"),S->SetPartitionedSlabState(Outer,Holes,{Cut},Unknown));
 S->Thickness+=1;TestFalse(TEXT("Thickness drift rejected"),S->RebuildSlabMesh());S->Thickness-=1;TestTrue(TEXT("Original state intact"),S->RebuildSlabMesh());
 TestFalse(TEXT("Legacy outline apply cannot erase source holes"),S->SetPartitionedSlabOutline(Outer,Partition));TestFalse(TEXT("Cut removal needs replanning"),S->RemoveCutOperation(Cut.OperationGuid));
 auto Filled=Partition;for(auto& R:Filled.Regions)R.Holes.Reset();TestFalse(TEXT("Allocation cannot silently fill authored hole"),S->SetPartitionedSlabState(Outer,Holes,{Cut},Filled));
 for(auto& P:ChangedCut.Source.ExplicitPolygon.Points)P.LocalPosition.X+=30;
 auto Next=Partition;if(!S->BuildCandidateDisplayRegions(Outer,Holes,{ChangedCut},Next.Regions,Status))return false;S->CaptureDisplayPartitionSource(Outer,Holes,{ChangedCut},Next);
 {FScopedTransaction Transaction(FText::FromString(TEXT("Replan partition cut")));if(!TestTrue(TEXT("Replanned complete state applies"),S->SetPartitionedSlabState(Outer,Holes,{ChangedCut},Next)))return false;}
 GEditor->UndoTransaction();TestEqual(TEXT("Undo restores original cut and snapshot"),S->CutOperations[0].Source.Height,Cut.Source.Height);TestTrue(TEXT("Undo complete state rebuilds"),S->RebuildSlabMesh());GEditor->RedoTransaction();TestEqual(TEXT("Redo restores planned source"),S->CutOperations[0].Source.Height,ChangedCut.Source.Height);TestTrue(TEXT("Redo snapshot matches"),S->RebuildSlabMesh());
 S->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);TestTrue(TEXT("Versioned partition provenance stable"),S->IsRecordedOutlineUnchanged());S->DisplayPartition.SourceCuts[0].OperationTag=TEXT("Changed");TestFalse(TEXT("Source snapshot change invalidates provenance"),S->IsRecordedOutlineUnchanged());
 return true;
}
#endif
