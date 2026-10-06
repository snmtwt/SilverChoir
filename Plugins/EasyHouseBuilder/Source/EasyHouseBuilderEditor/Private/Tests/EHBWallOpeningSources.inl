namespace WallOpeningSources128
{
 double MeshArea(const UEHBGeneratedMeshComponent* Mesh)
 {
  double Area=0;
  if(!Mesh)return Area;
  for(int32 Section=0;Section<Mesh->GetNumSections();++Section)
   if(const auto* Part=Mesh->GetProcMeshSection(Section))
    for(int32 I=0;I<Part->ProcIndexBuffer.Num();I+=3)
    {
     const auto A=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I]].Position;
     const auto B=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+1]].Position;
     const auto C=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+2]].Position;
     Area+=FVector::CrossProduct(B-A,C-A).Size()*0.5;
    }
  return Area;
 }
 FEHBCutOperation Polygon(const TArray<FVector2D>& Points)
 {
  FEHBCutOperation Cut;Cut.Stage=EEHBCutStage::SurfaceOpening;Cut.ProjectionMode=EEHBCutProjectionMode::VerticalXZ;
  for(const auto& P:Points)Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=FVector(P.X,0,P.Y);
  Cut.EnsureGuids();return Cut;
 }
 FEHBCutOperation MakeOpeningRectangle(double X,double Z,double Width,double Height)
 {return Polygon({{X,Z},{X+Width,Z},{X+Width,Z+Height},{X,Z+Height}});}
 bool CheckArea(FAutomationTestBase& Test,AEHB_Wall* Wall,double Expected)
 {
  const double Left=MeshArea(Wall->LeftWallMeshComponent),Right=MeshArea(Wall->RightWallMeshComponent);
  return Test.TestTrue(*FString::Printf(TEXT("Both wall sides %.3f/%.3f match %.3f cm2"),Left,Right,Expected),FMath::Abs(Left-Expected)<0.1&&FMath::Abs(Right-Expected)<0.1);
 }
 FString Source(AEHB_Wall* Wall)
 {
  FString Result;
  for(const auto& Cut:Wall->CutOperations){FString Value;FJsonObjectConverter::UStructToJsonObjectString(Cut,Value);Result+=Value;}
  return Result;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningSourceTest,"EHB.Surfaces.WallOpeningSources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningSourceTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);auto* Door=World->SpawnActor<AEHB_DoorWindow>(Params);
 ON_SCOPE_EXIT{Door->Destroy();W->Destroy();B->Destroy();GEditor->SelectNone(false,true,false);};
 B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(900,-600,40)));
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 const double Base=500*260,BaseCaps=MeshArea(W->CapMeshComponent);
 auto Apply=[&](TArray<FEHBCutOperation> Cuts,double Removed)
 {
  W->CutOperations=MoveTemp(Cuts);const FString Before=Source(W);W->RebuildWallMesh();
  TestEqual(TEXT("Rebuild preserves generic operation and point identities"),Source(W),Before);
  return CheckArea(*this,W,Base-Removed);
 };
 const auto Rectangle=MakeOpeningRectangle(-20,50,40,50);
 if(!Apply({Rectangle},2000))return false;
 AddInfo(FString::Printf(TEXT("Base cap area %.3f, window cap area %.3f, expected addition %.3f"),BaseCaps,MeshArea(W->CapMeshComponent),2.0*180.0*24));TestTrue(TEXT("Closed window produces two-sided thickness reveal area"),FMath::Abs(MeshArea(W->CapMeshComponent)-BaseCaps-2*180*24)<0.1);
 if(!Apply({Rectangle,MakeOpeningRectangle(0,50,40,50)},3000))return false;
 auto Reversed=Rectangle;Algo::Reverse(Reversed.Source.ExplicitPolygon.Points);if(!Apply({Reversed},2000))return false;
 auto Transformed=MakeOpeningRectangle(-20,0,40,50);Transformed.Source.LocalTransform=FTransform(FRotator(15,0,0),FVector(70,0,80),FVector(2,1,1.5));if(!Apply({Transformed},6000))return false;
 if(!Apply({Polygon({{0,50},{60,50},{60,70},{20,70},{20,110},{0,110}})},2000))return false;
 if(!Apply({MakeOpeningRectangle(-20,0,40,100)},4000)||!Apply({MakeOpeningRectangle(240,50,40,50)},500))return false;
 for(int32 Mode=0;Mode<4;++Mode)
 {
  auto Ignored=Rectangle;
  if(Mode==0)Ignored.bEnabled=false;
  if(Mode==1)Ignored.Stage=EEHBCutStage::Profile;
  if(Mode==2)Ignored.ProjectionMode=EEHBCutProjectionMode::HorizontalXY;
  if(Mode==3)Ignored.OperationType=EEHBCutOperationType::Union;
  if(!Apply({Ignored},0))return false;
 }
 // Saved pre-spline connections still work, and their derived operation is consumed once.
 FEHBWallDoorWindowConnection Connection;Connection.DoorWindowGuid=FGuid::NewGuid();Connection.DistanceFromStart=250;
 Connection.OpeningWidth=40;Connection.OpeningHeight=50;Connection.BottomHeight=50;
 W->CutOperations.Reset();W->DoorWindowConnections.Add(Connection);W->RebuildWallMesh();
 if(!CheckArea(*this,W,Base-2000))return false;
 TestEqual(TEXT("One derived operation for one legacy connection"),W->CutOperations.Num(),1);
 W->CutOperations.Add(MakeOpeningRectangle(0,50,40,50));W->RebuildWallMesh();if(!CheckArea(*this,W,Base-3000))return false;
 const FString SavedSources=Source(W);const double SavedCaps=MeshArea(W->CapMeshComponent);
 Door->SetRectangularOpeningDimensions(30,40,120,10);Door->AttachToBuilding(B,FTransform::Identity);
 Door->SetActorLocationAndRotation(W->GetWorldLocationOnCenterAxisAtDistance(100,120),W->GetActorQuat());
 W->SetPreviewDoorWindowOpening(Door,100);if(!CheckArea(*this,W,Base-4200))return false;
 TestEqual(TEXT("Preview never persists an operation"),Source(W),SavedSources);
 W->ClearPreviewDoorWindowOpening();if(!CheckArea(*this,W,Base-3000))return false;
 TestTrue(TEXT("Cancel restores reveal geometry"),FMath::Abs(MeshArea(W->CapMeshComponent)-SavedCaps)<0.1);
 TestEqual(TEXT("Cancel preserves sources"),Source(W),SavedSources);
 TArray<FEHBLogicalSurfaceDefinition> Surfaces;FName Status;
 if(!TestTrue(TEXT("Base query remains independent of all opening sources"),W->QueryLogicalBaseSurfaces(Surfaces,Status))||Surfaces.Num()!=2)return false;
 TestEqual(TEXT("Uncut logical base"),Surfaces[0].GetAreaCm2(),Base);
 // A source edit inside the same editor transaction as its generated components.
 {
  FScopedTransaction Transaction(NSLOCTEXT("EHBTests","GenericWallOpening","Edit generic wall opening"));W->Modify();
  TArray<UEHBGeneratedMeshComponent*> Meshes;W->GetGeneratedMeshComponents(Meshes);for(auto* Mesh:Meshes)Mesh->Modify();
  W->CutOperations.RemoveAt(1);W->RebuildWallMesh();
 }
 if(!CheckArea(*this,W,Base-2000))return false;
 GEditor->UndoTransaction();if(!CheckArea(*this,W,Base-3000))return false;TestEqual(TEXT("Undo restores full source and point IDs"),Source(W),SavedSources);
 GEditor->RedoTransaction();if(!CheckArea(*this,W,Base-2000))return false;
 W->CurveControlOffset=60;W->RebuildWallMesh();const double CurvedLeft=MeshArea(W->LeftWallMeshComponent),CurvedRight=MeshArea(W->RightWallMeshComponent),CurvedCaps=MeshArea(W->CapMeshComponent);
 W->DoorWindowConnections.Reset();W->CutOperations={Rectangle};W->RebuildWallMesh();
 TestTrue(TEXT("Curved legacy and generic opening geometry agree"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-CurvedLeft)<0.1&&FMath::Abs(MeshArea(W->RightWallMeshComponent)-CurvedRight)<0.1&&FMath::Abs(MeshArea(W->CapMeshComponent)-CurvedCaps)<0.1);
 return true;
}
