namespace
{
 FString SlabEditState118(const AEHB_FloorSlab* S)
 {
  FString State,Json;for(const auto& P:S->LocalTopPolygon)State+=P.ToString();for(const auto& H:S->LocalHoles){FJsonObjectConverter::UStructToJsonObjectString(H,Json);State+=Json;}
  for(const auto& C:S->CutOperations){FJsonObjectConverter::UStructToJsonObjectString(C,Json);State+=Json;}for(const auto& C:S->PreviewCutters){FJsonObjectConverter::UStructToJsonObjectString(C,Json);State+=Json;}
  for(const auto& R:CastChecked<UEHBPlanarSurfaceComponent>(S->MeshComponent)->GetPlanarSurfaceRegions()){FJsonObjectConverter::UStructToJsonObjectString(R,Json);State+=Json;}
  for(int32 I=0;I<S->MeshComponent->GetNumSections();++I){auto M=*S->MeshComponent->GetProcMeshSection(I);M.Revision=0;FJsonObjectConverter::UStructToJsonObjectString(M,Json);State+=Json;}return State;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabOutlineEditTest,"EHB.Topology.SlabOutlineEditAtomic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabOutlineEditTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;auto* B=Fixture.Building;
 FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;auto* S=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),B->GetActorLocation(),FRotator::ZeroRotator,Params);if(!S)return false;Fixture.Actors.Add(S);
 S->ConfigureDefaultSlab(B,FTransform(FRotator(0,17,0),FVector(0,0,300)),500,20,false);S->VisualExpansion=0;S->LocalTopPolygon={{0,0,0},{500,0,0},{500,500,0},{0,500,0}};if(!S->RebuildSlabMesh())return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 FEHBCutOperation Cut;for(const auto& P:TArray<FVector>{{200,-100,0},{220,-100,0},{220,600,0},{200,600,0}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Cut.EnsureGuids();if(!S->AddCutOperation(Cut))return false;
 auto Hole=[&](FVector Center,float W,float D){return UEHBBuildingToolset::AddFloorSlabRectangularHole(S,Center,W,D,0);};
 const auto Base=SlabEditState118(S);auto Left=Hole(FVector(100,100,0),50,50);TestTrue(*Left.Message,Left.bSucceeded);auto Right=Hole(FVector(400,100,0),50,50);TestTrue(*Right.Message,Right.bSucceeded);TestEqual(TEXT("Holes in both pieces"),S->LocalHoles.Num(),2);
 if(S->LocalHoles.Num()!=2)return false;
 const auto Both=SlabEditState118(S);
 const auto Rotated=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,FVector(100,350,0),50,50,45);TestTrue(TEXT("Rotated hole compares quantized areas consistently"),Rotated.bSucceeded);if(Rotated.bSucceeded){GEditor->UndoTransaction(false);TestEqual(TEXT("Rotated hole undo"),SlabEditState118(S),Both);}
 const int32 Queue=GEditor->Trans->GetQueueLength();
 TestFalse(TEXT("Gap-crossing hole rejected"),Hole(FVector(210,300,0),80,50).bSucceeded);TestFalse(TEXT("Existing hole overlap rejected"),Hole(FVector(100,100,0),40,40).bSucceeded);TestEqual(TEXT("Failed holes preserve all source and mesh"),SlabEditState118(S),Both);TestEqual(TEXT("Failed holes create no transaction"),GEditor->Trans->GetQueueLength(),Queue);
 TestTrue(TEXT("Undo second hole"),GEditor->UndoTransaction());TestEqual(TEXT("Undo leaves first hole"),S->LocalHoles.Num(),1);TestTrue(TEXT("Redo second hole"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores both exact results"),SlabEditState118(S),Both);
 S->Thickness=0;TestFalse(TEXT("Failed insertion"),S->InsertCornerOnEdge(INDEX_NONE,0,1));TestFalse(TEXT("Failed removal"),S->RemoveCorner(0,0));S->Thickness=20;TestEqual(TEXT("Invalid mesh parameters preserve edit source"),SlabEditState118(S),Both);
 TestFalse(TEXT("Failed corner movement"),S->UpdateCornerWorldLocation(0,0,S->GetActorTransform().TransformPosition(FVector(1.e12,0,0))));TestFalse(TEXT("Failed outer edge movement"),S->OffsetEdgeWorldLocation(INDEX_NONE,0,1,FVector(1.e12,0,0)));TestEqual(TEXT("Rejected coordinates preserve complete state"),SlabEditState118(S),Both);
 TestFalse(TEXT("Self-crossing author outline rejects disconnected normalized source"),S->SetSlabOutline({{0,0,0},{500,500,0},{0,500,0},{300,0,0}},{}));TestEqual(TEXT("Rejected source topology unchanged"),SlabEditState118(S),Both);
 if(!TestTrue(TEXT("Failed candidate retains holes before further edits"),S->LocalHoles.Num()==2))return false;
 {FScopedTransaction Transaction(FText::FromString(TEXT("Native hole edit")));const FVector Point=S->LocalHoles[0].LocalPolygon[0]+FVector(5,0,0);TestTrue(TEXT("Native hole corner edit"),S->UpdateCornerWorldLocation(0,0,S->GetActorTransform().TransformPosition(Point)));}
 const auto Edited=SlabEditState118(S);GEditor->UndoTransaction();TestEqual(TEXT("Native hole undo includes mesh"),SlabEditState118(S),Both);GEditor->RedoTransaction();TestEqual(TEXT("Native hole redo"),SlabEditState118(S),Edited);GEditor->UndoTransaction(false);
 const auto Erased=UEHBBuildingToolset::SetFloorSlabTopPolygon(S,{{200,0,0},{220,0,0},{220,500,0},{200,500,0}},false);TestFalse(TEXT("Toolset erasure rejected before replacing source"),Erased.bSucceeded);TestEqual(TEXT("Failed replacement preserves complete state"),SlabEditState118(S),Both);
 TestTrue(TEXT("Preview creation"),S->AddSquarePreviewCutterInEditor());const auto Preview=SlabEditState118(S);FTransform Huge= S->PreviewCutters[0].LocalTransform*S->GetActorTransform();Huge.SetScale3D(FVector(100));TestFalse(TEXT("Preview transform erasure rejected"),S->UpdateCutterWorldTransform(0,Huge));TestEqual(TEXT("Preview transform failure unchanged"),SlabEditState118(S),Preview);
 S->Thickness=0;TestFalse(TEXT("Preview deletion failure"),S->RemovePreviewCutter(0));S->Thickness=20;TestEqual(TEXT("Failed preview deletion unchanged"),SlabEditState118(S),Preview);TestTrue(TEXT("Preview removal"),S->RemovePreviewCutter(0));
 TestTrue(TEXT("Remove split source"),S->RemoveCutOperation(Cut.OperationGuid));
 const TArray<FVector> U={{0,0,0},{500,0,0},{500,500,0},{400,500,0},{400,100,0},{100,100,0},{100,500,0},{0,500,0}};TestTrue(TEXT("Concave author source"),S->SetSlabOutline(U,{}));const auto Concave=SlabEditState118(S);
 TestFalse(TEXT("All four corners inside but edges cross concavity rejected"),Hole(FVector(250,250,0),400,400).bSucceeded);TestEqual(TEXT("Concave rejection unchanged"),SlabEditState118(S),Concave);
 GEditor->SelectNone(false,true,false);return true;
}
