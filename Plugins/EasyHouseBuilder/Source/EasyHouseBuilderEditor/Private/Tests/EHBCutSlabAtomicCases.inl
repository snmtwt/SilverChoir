#include "JsonObjectConverter.h"
#include "Editor/TransBuffer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutSlabAtomicTest,"EHB.FloorSlab.CutStateAtomic",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBCutSlabAtomicTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 auto* Slab=SpawnTransientFloorSlabForTest(World,FVector(0,0,191000));if(!Slab)return false;
 Slab->VisualExpansion=0;Slab->LocalTopPolygon={{0,0,0},{500,0,0},{500,500,0},{0,500,0}};if(!Slab->RebuildSlabMesh())return false;
 auto Snapshot=[&](){FString State,Json;for(const auto& C:Slab->CutOperations){FJsonObjectConverter::UStructToJsonObjectString(C,Json);State+=Json;}for(const auto& C:Slab->PreviewCutters){FJsonObjectConverter::UStructToJsonObjectString(C,Json);State+=Json;}
  auto* Surface=CastChecked<UEHBPlanarSurfaceComponent>(Slab->MeshComponent);for(const auto& R:Surface->GetPlanarSurfaceRegions()){FJsonObjectConverter::UStructToJsonObjectString(R,Json);State+=Json;}
  for(int32 I=0;I<Slab->MeshComponent->GetNumSections();++I){auto S=*Slab->MeshComponent->GetProcMeshSection(I);S.Revision=0;FJsonObjectConverter::UStructToJsonObjectString(S,Json);State+=Json;}return State;};
 auto MakeCut=[](double X0,double Y0,double X1,double Y1){FEHBCutOperation C;for(const auto& P:TArray<FVector>{{X0,Y0,0},{X1,Y0,0},{X1,Y1,0},{X0,Y1,0}})C.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;C.EnsureGuids();return C;};
 const auto Initial=Snapshot();const int32 Queue=GEditor->Trans->GetQueueLength();auto Erase=MakeCut(-100,-100,600,600);
 TestFalse(TEXT("Erase candidate rejected"),Slab->ValidateCutOperations({Erase}));TestFalse(TEXT("Erase add rejected before mutation"),Slab->AddCutOperation(Erase));TestEqual(TEXT("Failed add preserves source component and mesh"),Snapshot(),Initial);TestEqual(TEXT("Failure no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
 auto Cut=MakeCut(200,-100,220,600);TestTrue(TEXT("Read-only split preview"),Slab->ValidateCutOperations({Cut}));TestEqual(TEXT("Valid preview unchanged"),Snapshot(),Initial);
 {FScopedTransaction Transaction(FText::FromString(TEXT("Atomic slab cut")));TestTrue(TEXT("Split commit"),Slab->AddCutOperation(Cut));}
 const auto Split=Snapshot();TestFalse(TEXT("Duplicate identity rejected"),Slab->AddCutOperation(Cut));TestEqual(TEXT("Duplicate no mutation"),Snapshot(),Split);
 auto Away=Cut;Away.Source.LocalTransform.SetLocation(FVector(0,0,2000));TestTrue(TEXT("Valid source outside depth can preview restoration"),Slab->ValidateCutOperations({Away}));TestEqual(TEXT("Depth preview read-only"),Snapshot(),Split);
 {FScopedTransaction Transaction(FText::FromString(TEXT("Move cut depth")));TestTrue(TEXT("Move source out of depth"),Slab->UpdateCutOperation(Away));}
 TestEqual(TEXT("Out-of-depth keeps source"),Slab->CutOperations.Num(),1);TestEqual(TEXT("Out-of-depth restores one surface"),CastChecked<UEHBPlanarSurfaceComponent>(Slab->MeshComponent)->GetPlanarSurfaceRegions().Num(),1);
 GEditor->UndoTransaction(false);TestEqual(TEXT("Undo depth restores source and split geometry"),Snapshot(),Split);
 auto Bad=Erase;Bad.OperationGuid=Cut.OperationGuid;TestFalse(TEXT("Erase update rejected"),Slab->UpdateCutOperation(Bad));TestEqual(TEXT("Update failure preserves complete split"),Snapshot(),Split);
 Bad=Cut;Bad.Source.ExplicitPolygon.Points[0].LocalPosition.X=1.e12;TestFalse(TEXT("Unbounded point update rejected"),Slab->UpdateCutOperation(Bad));TestEqual(TEXT("Bad point no mutation"),Snapshot(),Split);
 TestFalse(TEXT("Direct point API rejects unbounded"),Slab->UpdateCutPolygonPoint(Cut.OperationGuid,Cut.Source.ExplicitPolygon.Points[0].PointGuid,FVector(1.e12,0,0)));TestEqual(TEXT("Direct point failure unchanged"),Snapshot(),Split);
 const int32 Loop=AEHB_FloorSlab::MakeCutOperationLoopIndex(0);
 TestFalse(TEXT("Editor corner API rejects unbounded"),Slab->UpdateCornerWorldLocation(Loop,0,Slab->GetActorTransform().TransformPosition(FVector(1.e12,0,0))));TestEqual(TEXT("Corner failure unchanged"),Snapshot(),Split);
 TestFalse(TEXT("Editor edge API rejects unbounded"),Slab->OffsetEdgeWorldLocation(Loop,0,1,FVector(1.e12,0,0)));TestEqual(TEXT("Edge failure unchanged"),Snapshot(),Split);
 Slab->Thickness=0;TestFalse(TEXT("Removal rebuild failure rejected"),Slab->RemoveCutOperation(Cut.OperationGuid));Slab->Thickness=20;TestEqual(TEXT("Removal failure preserves source and geometry"),Snapshot(),Split);
 TestTrue(TEXT("Undo cut"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores full initial state"),Snapshot(),Initial);TestTrue(TEXT("Redo cut"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores split"),Snapshot(),Split);
 {FScopedTransaction Transaction(FText::FromString(TEXT("Remove atomic slab cut")));TestTrue(TEXT("Source deletion"),Slab->RemoveCutOperation(Cut.OperationGuid));}TestEqual(TEXT("Removal restores initial geometry"),Snapshot(),Initial);GEditor->UndoTransaction(false);TestEqual(TEXT("Undo removal"),Snapshot(),Split);
 TestTrue(TEXT("Add preview"),Slab->AddSquarePreviewCutterInEditor());const auto Preview=Snapshot();Slab->Thickness=0;TestFalse(TEXT("Preview commit failure"),Slab->CommitPreviewCutters());Slab->Thickness=20;TestEqual(TEXT("Preview commit failure preserves both sources"),Snapshot(),Preview);
 World->EditorDestroyActor(Slab,true);
 auto* Tiny=SpawnTransientFloorSlabForTest(World,FVector(0,0,192000));if(!Tiny)return false;Tiny->VisualExpansion=0;Tiny->LocalTopPolygon={{0,0,0},{100,0,0},{100,100,0},{0,100,0}};if(!Tiny->RebuildSlabMesh())return false;
 const auto Hash=Tiny->MeshComponent->GetProcMeshSection(0)->ContentHash;TestFalse(TEXT("Oversized preview rejected"),Tiny->AddSquarePreviewCutterInEditor());TestTrue(TEXT("Failed preview retains empty source"),Tiny->PreviewCutters.IsEmpty());TestEqual(TEXT("Failed preview retains mesh"),Tiny->MeshComponent->GetProcMeshSection(0)->ContentHash,Hash);
 World->EditorDestroyActor(Tiny,true);return true;
}
