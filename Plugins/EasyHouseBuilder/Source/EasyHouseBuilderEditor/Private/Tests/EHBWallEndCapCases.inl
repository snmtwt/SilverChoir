IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallEndCapTest,"EHB.Surfaces.WallEndCapClipping",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallEndCapTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);ON_SCOPE_EXIT{W->Destroy();B->Destroy();};
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 auto OnPlaneArea=[](const FEHBWallJunctionMesh& Mesh,double Center,double Slope)
 {
  double Area=0;for(int32 I=0;I<Mesh.Triangles.Num();I+=3){const auto A=Mesh.Vertices[Mesh.Triangles[I]],B=Mesh.Vertices[Mesh.Triangles[I+1]],C=Mesh.Vertices[Mesh.Triangles[I+2]];auto On=[&](const FVector& P){return FMath::Abs(P.X-Center-Slope*P.Y)<0.0001;};if(On(A)&&On(B)&&On(C))Area+=FVector::CrossProduct(B-A,C-A).Size()*0.5;}return Area;
 };
 auto Capture=[&](){FEHBWallJunctionMesh Mesh;const auto* Section=W->CapMeshComponent->GetProcMeshSection(0);if(Section){for(const auto& V:Section->ProcVertexBuffer)Mesh.Vertices.Add(V.Position);for(uint32 I:Section->ProcIndexBuffer)Mesh.Triangles.Add(I);}return Mesh;};
 struct FCase{TArray<FEHBCutOperation> Cuts;double LeftRemoved,RightRemoved;};
 const TArray<FCase> Cases{
  {{MakeOpeningRectangle(-270,60,40,50)},50*24,0},
  {{MakeOpeningRectangle(230,60,40,50)},0,50*24},
  {{MakeOpeningRectangle(230,60,20,50)},0,50*24},
  {{MakeOpeningRectangle(250,60,20,50)},0,0},
  {{MakeOpeningRectangle(230,60,40,50),MakeOpeningRectangle(230,80,40,50)},0,70*24},
  {{MakeOpeningRectangle(230,-10,40,280)},0,260*24},
  {{Polygon({{230,60},{270,60},{250,100}})},0,40*24},
  {{MakeOpeningRectangle(-270,40,40,20),MakeOpeningRectangle(230,120,40,30)},20*24,30*24}
 };
 for(int32 I=0;I<Cases.Num();++I)
 {
  W->CutOperations=Cases[I].Cuts;const auto Before=Source(W);W->RebuildWallMesh();const auto Actual=Capture();FEHBWallJunctionMesh Contact;
  if(!TestTrue(TEXT("Structural contact generation succeeds"),W->BuildStructuralContactMesh(Contact)))return false;
  for(bool Left:{true,false})
  {
   const double X=Left?-250:250,Expected=24*260-(Left?Cases[I].LeftRemoved:Cases[I].RightRemoved);
   TestTrue(*FString::Printf(TEXT("Case%d end%d visible cap area"),I,Left),FMath::Abs(OnPlaneArea(Actual,X,0)-Expected)<0.02);
   TestTrue(TEXT("Structural contact uses same clipped end"),FMath::Abs(OnPlaneArea(Contact,X,0)-Expected)<0.02);
  }
  TestEqual(TEXT("Clipping never rewrites sources"),Source(W),Before);
 }
 // Explicit derived miter face: validate the end plane itself, independently
 // of the still-pending unequal-side opening reveal and dependency plans.
 W->CutOperations.Reset();auto* Pillar=World->SpawnActor<AEHB_Pillar>(Params);ON_SCOPE_EXIT{Pillar->Destroy();};Pillar->AttachToBuilding(B,FTransform(FVector(-250,0,300)));
 W->StartPillarGuid=Pillar->ElementGuid;W->bGenerateLinkedPillarEndCaps=true;
 W->SetPillarConnectionFacePoints(Pillar,W->GetElementLocalTransform().TransformPosition(FVector(-230,12,0)),W->GetElementLocalTransform().TransformPosition(FVector(-270,-12,0)));W->RebuildWallMesh();
 const double Width=FMath::Sqrt(40.0*40+24.0*24);TestTrue(TEXT("Derived miter cap fixture"),FMath::Abs(OnPlaneArea(Capture(),-250,40.0/24)-Width*260)<0.02);
 W->CutOperations={MakeOpeningRectangle(-300,80,50,60)};W->RebuildWallMesh();
 TestTrue(TEXT("Miter cap clips partial thickness"),FMath::Abs(OnPlaneArea(Capture(),-250,40.0/24)-Width*(260-30))<0.02);
 FEHBWallJunctionMesh Contact;TestTrue(TEXT("Miter structural capture"),W->BuildStructuralContactMesh(Contact));TestTrue(TEXT("Miter contact area matches clipped plane"),FMath::Abs(OnPlaneArea(Contact,-250,40.0/24)-Width*(260-30))<0.02);
 auto OnHeightArea=[](const FEHBWallJunctionMesh& Mesh,double Z)
 {
  double Area=0;for(int32 I=0;I<Mesh.Triangles.Num();I+=3){const auto A=Mesh.Vertices[Mesh.Triangles[I]],B=Mesh.Vertices[Mesh.Triangles[I+1]],C=Mesh.Vertices[Mesh.Triangles[I+2]];if(FMath::Abs(A.Z-Z)<0.0001&&FMath::Abs(B.Z-Z)<0.0001&&FMath::Abs(C.Z-Z)<0.0001)Area+=FVector::CrossProduct(B-A,C-A).Size()*0.5;}return Area;
 };
 for(const auto& Mesh:{Capture(),Contact})
 {
  // Each horizontal reveal is a 20 by 12 triangle, rendered on both faces.
  TestTrue(TEXT("Miter sill reaches end plane across partial width"),FMath::Abs(OnHeightArea(Mesh,80)-240)<0.02);
  TestTrue(TEXT("Miter lintel reaches end plane across partial width"),FMath::Abs(OnHeightArea(Mesh,140)-240)<0.02);
  TestTrue(TEXT("Miter jamb occupies only remaining half thickness"),FMath::Abs(OnPlaneArea(Mesh,-250,0)-1440)<0.02);
 }
 // Extend past both side endpoints: horizontal reveals become trapezoids.
 W->CutOperations={MakeOpeningRectangle(-300,80,90,60)};W->RebuildWallMesh();
 TestTrue(TEXT("Full-width miter sill is clipped trapezoid"),FMath::Abs(OnHeightArea(Capture(),80)-1920)<0.02);
 TestTrue(TEXT("Full-width miter jamb"),FMath::Abs(OnPlaneArea(Capture(),-210,0)-2880)<0.02);
 return true;
}
