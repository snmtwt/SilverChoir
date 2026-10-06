#include "Components/EHBPlanarSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Geometry/EHBSurfaceGeometryTypes.h"
#include "ScopedTransaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutSlabRegionsTest,"EHB.FloorSlab.DisconnectedCutRegions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBCutSlabRegionsTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 auto* Slab=SpawnTransientFloorSlabForTest(World,FVector(0,0,189000));
 if(!TestNotNull(TEXT("Slab"),Slab))return false;
 auto Rect=[](double X0,double Y0,double X1,double Y1){return TArray<FVector>{{X0,Y0,0},{X1,Y0,0},{X1,Y1,0},{X0,Y1,0}};};
 Slab->SetFlags(RF_Transactional);Slab->VisualExpansion=0;Slab->LocalTopPolygon=Rect(0,0,500,500);
 Slab->LocalHoles.AddDefaulted_GetRef().LocalPolygon=Rect(300,100,350,150);
 TestTrue(TEXT("Base"),Slab->RebuildSlabMesh());
 auto* Surface=Cast<UEHBPlanarSurfaceComponent>(Slab->MeshComponent);
 auto Area=[&](){double A=0;const auto* Section=Slab->MeshComponent->GetProcMeshSection(0);if(!Section)return A;
  for(int32 I=0;I<Section->ProcIndexBuffer.Num();I+=3){const auto P=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I]].Position,Q=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+1]].Position,R=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+2]].Position;
   if(FMath::Abs(P.Z-Slab->GetTopZ())<0.001&&FMath::Abs(Q.Z-P.Z)<0.001&&FMath::Abs(R.Z-P.Z)<0.001)A+=FVector::CrossProduct(Q-P,R-P).Size()*0.5;}
  return A;};
 FEHBCutOperation Cut;Cut.OperationType=EEHBCutOperationType::Subtract;Cut.Stage=EEHBCutStage::Profile;Cut.ProjectionMode=EEHBCutProjectionMode::HorizontalXY;Cut.TransformPolicy=EEHBCutTransformPolicy::TargetLocal;
 for(const auto& P:Rect(200,-100,220,600))Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;
 Cut.EnsureGuids();
 TArray<FEHBLogicalSurfaceRegion> Candidate;FName CandidateStatus;
 TestTrue(TEXT("Read-only candidate includes supplied uncommitted cut"),Slab->BuildCandidateDisplayRegions(Slab->LocalTopPolygon,Slab->LocalHoles,{Cut},Candidate,CandidateStatus));
 TestEqual(TEXT("Candidate retains all fragments"),Candidate.Num(),2);
 FEHBLogicalSurfaceDefinition CandidateSurface;CandidateSurface.Regions=Candidate;
 TestEqual(TEXT("Candidate includes authored hole"),CandidateSurface.GetAreaCm2(),237500.0);
 TestEqual(TEXT("Candidate leaves current source uncut"),Slab->CutOperations.Num(),0);TestEqual(TEXT("Candidate leaves actual mesh unchanged"),Area(),247500.0);
 auto InvalidCut=Cut;InvalidCut.Source.ExplicitPolygon.Points.Reset();for(const auto& P:Rect(-100,-100,600,600))InvalidCut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;InvalidCut.EnsureGuids();
 TestFalse(TEXT("Erased candidate rejected before publication"),Slab->BuildCandidateDisplayRegions(Slab->LocalTopPolygon,Slab->LocalHoles,{InvalidCut},Candidate,CandidateStatus));TestTrue(TEXT("Failure clears previous candidate"),Candidate.IsEmpty());TestEqual(TEXT("Failed candidate retains old mesh"),Area(),247500.0);
 {FScopedTransaction Transaction(FText::FromString(TEXT("Cut slab regions")));Slab->Modify();for(auto* C:Slab->GetComponents()){C->SetFlags(RF_Transactional);C->Modify();}TestTrue(TEXT("Through cut"),Slab->AddCutOperation(Cut));}
 auto Verify=[&](int32 Count,double Expected){TArray<FEHBPlanarSurfaceRegion> Regions;TestTrue(TEXT("All effective regions"),Slab->BuildEffectiveDisplayRegions(Regions,true,true));TestEqual(TEXT("Region count"),Regions.Num(),Count);TestEqual(TEXT("Component count"),Surface->GetPlanarSurfaceRegions().Num(),Count);TestTrue(TEXT("All top triangles without duplicate coverage"),FMath::Abs(Area()-Expected)<0.01);
  TestTrue(TEXT("Candidate follows same expansion and union geometry"),Slab->BuildCandidateDisplayRegions(Slab->LocalTopPolygon,Slab->LocalHoles,Slab->CutOperations,Candidate,CandidateStatus));CandidateSurface.Regions=Candidate;TestEqual(TEXT("Candidate complete region count"),Candidate.Num(),Count);TestTrue(TEXT("Candidate agrees with actual top triangles"),FMath::Abs(CandidateSurface.GetAreaCm2()-Expected)<0.01);
 };
 Verify(2,237500);
 TArray<FVector> Single;TestFalse(TEXT("Single-region API must reject disconnected results"),Slab->BuildEffectiveOuterPolygon(Single,true,true));TestTrue(TEXT("No partial result"),Single.IsEmpty());
 TestTrue(TEXT("Candidate validation accepts all valid cut pieces"),Slab->ValidateSlabOutline(Slab->LocalTopPolygon,Slab->LocalHoles));
 FVector Point;FVector2D UV;TestFalse(TEXT("Cut gap not projectable"),Surface->ProjectWorldPointToSurface(Slab->GetActorTransform().TransformPosition(FVector(210,250,20)),Point,UV));
 TestFalse(TEXT("Hole stays empty"),Surface->ProjectWorldPointToSurface(Slab->GetActorTransform().TransformPosition(FVector(325,125,20)),Point,UV));
 TestTrue(TEXT("Second piece projectable"),Surface->ProjectWorldPointToSurface(Slab->GetActorTransform().TransformPosition(FVector(450,250,20)),Point,UV));
 GEditor->UndoTransaction();TestEqual(TEXT("Undo source cut"),Slab->CutOperations.Num(),0);TestTrue(TEXT("Undo rebuild"),Slab->RebuildSlabMesh());Verify(1,247500);
 GEditor->RedoTransaction();TestTrue(TEXT("Redo rebuild"),Slab->RebuildSlabMesh());Verify(2,237500);
 Slab->VisualExpansion=5;TestTrue(TEXT("Expanded disconnected pieces"),Slab->RebuildSlabMesh());Verify(2,252500);
 Slab->VisualExpansion=10;TestTrue(TEXT("Touching expansions union"),Slab->RebuildSlabMesh());Verify(1,267900);
 Slab->VisualExpansion=15;TestTrue(TEXT("Overlapping expansions union"),Slab->RebuildSlabMesh());Verify(1,278400);
 Slab->VisualExpansion=0;TestTrue(TEXT("Remove source restores surface"),Slab->RemoveCutOperation(Cut.OperationGuid));Verify(1,247500);
 World->EditorDestroyActor(Slab,true);return true;
}
