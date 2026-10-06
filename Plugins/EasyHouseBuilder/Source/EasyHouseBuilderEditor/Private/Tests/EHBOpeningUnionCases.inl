namespace OpeningUnion132
{
 FEHBCutOperation Rectangle(const FEHBLogicalSurfaceDefinition& Host,double X,double Y,double Width,double Height)
 {
  auto Cut=SurfaceOpening129::Make(Host,X,Y);
  const TArray<FVector> Points{{X,Y,0},{X+Width,Y,0},{X+Width,Y+Height,0},{X,Y+Height,0}};
  for(int32 I=0;I<4;++I)Cut.Source.ExplicitPolygon.Points[I].LocalPosition=Points[I];return Cut;
 }
 double Area(const FEHBWallJunctionMesh& Mesh)
 {
  double Result=0;for(int32 I=0;I<Mesh.Triangles.Num();I+=3)Result+=FVector::CrossProduct(Mesh.Vertices[Mesh.Triangles[I+1]]-Mesh.Vertices[Mesh.Triangles[I]],Mesh.Vertices[Mesh.Triangles[I+2]]-Mesh.Vertices[Mesh.Triangles[I]]).Size()*0.5;return Result;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningUnionTest,"EHB.Surfaces.OpeningUnionReveals",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningUnionTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;using namespace OpeningUnion132;
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);W->Destroy();B->Destroy();};
 B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(800,-500,60)));W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 FEHBPreparedWallOpening Uncut;if(!W->PrepareSurfaceOpening({},Uncut,Status))return false;const double CapBase=Area(Uncut.Caps);
 const auto& Host=Hosts[0];auto Rect=[&](double X,double Y,double A,double H){return Rectangle(Host,X,Y,A,H);};
 struct FCase{TArray<FEHBCutOperation> Cuts;double Area,Perimeter;};
 TArray<FCase> Cases{
  {{Rect(100,60,20,30),Rect(140,60,20,30)},1200,200},
  {{Rect(100,60,20,30),Rect(110,60,20,30)},900,120},
  {{Rect(100,60,40,50),Rect(110,70,20,30)},2000,180},
  {{Rect(100,60,20,30),Rect(120,60,20,30)},1200,140},
  {{Rect(100,60,20,30),Rect(120,90,20,30)},1200,200},
  {{Rect(100,60,20,30),Rect(100,60,20,30)},600,100},
  {{Rect(100,60,40,20),Rect(100,60,20,40)},1200,160},
  {{Rect(100,60,60,20),Rect(100,100,60,20),Rect(100,80,20,20),Rect(140,80,20,20)},3200,320},
  {{Rect(100,60,60,20),Rect(100,100,60,20),Rect(100,80,20,20),Rect(140,80,20,20),Rect(125,85,10,10)},3300,360}
 };
 auto Edit=[&](const TArray<FEHBCutOperation>& Cuts){return UEHBBuildingToolset::SetWallOpenings(W,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);};
 for(int32 Case=0;Case<Cases.Num();++Case)for(bool Reverse:{false,true})
 {
  auto Cuts=Cases[Case].Cuts;if(Reverse){Algo::Reverse(Cuts);for(auto& Cut:Cuts)Algo::Reverse(Cut.Source.ExplicitPolygon.Points);}
  FEHBPreparedWallOpening Prepared;const bool Ready=W->PrepareSurfaceOpening(Cuts,Prepared,Status);
  if(!TestTrue(*FString::Printf(TEXT("Union case %d reverse%d: %s"),Case,Reverse,*Status.ToString()),Ready))return false;
  TestTrue(TEXT("Independent union area"),FMath::Abs(Prepared.RemovedArea-Cases[Case].Area)<0.01);
  TestTrue(TEXT("No hidden internal reveal area"),FMath::Abs(Area(Prepared.Caps)-CapBase-2*24*Cases[Case].Perimeter)<0.1);
  auto Inside=[&](const FVector2D& P){for(const auto& Cut:Cases[Case].Cuts){const auto& A=Cut.Source.ExplicitPolygon.Points[0].LocalPosition;const auto& C=Cut.Source.ExplicitPolygon.Points[2].LocalPosition;if(P.X>A.X&&P.X<C.X&&P.Y>A.Y&&P.Y<C.Y)return true;}return false;};
  // Inspect every generated reveal triangle, not only its total area. Across
  // each projected edge, exactly one side must be cut and the other solid.
  for(int32 I=Uncut.Caps.Triangles.Num();I<Prepared.Caps.Triangles.Num();I+=3)
  {
   FVector2D P[3];for(int32 J=0;J<3;++J){const auto V=Prepared.Caps.Vertices[Prepared.Caps.Triangles[I+J]];const auto Local=Host.PlaneToBuilding.InverseTransformPosition(W->GetElementLocalTransform().TransformPosition(V));P[J]=FVector2D(Local.X,Local.Y);}
   int32 A=0,C=1;if((P[2]-P[0]).SquaredLength()>(P[C]-P[A]).SquaredLength()){A=0;C=2;}if((P[2]-P[1]).SquaredLength()>(P[C]-P[A]).SquaredLength()){A=1;C=2;}
   const auto Direction=(P[C]-P[A]).GetSafeNormal();const FVector2D Offset(-Direction.Y*0.01,Direction.X*0.01);const auto Mid=(P[A]+P[C])*0.5;
   TestTrue(TEXT("Every reveal separates cut volume and retained material"),Inside(Mid+Offset)!=Inside(Mid-Offset));
  }
  const auto Before=Source(W);FEHBWallOpeningCommand::FailurePhase=2;TestFalse(TEXT("Union receipt failure rolls back"),Edit(Cuts).bSucceeded);TestEqual(TEXT("Union rollback retains sources"),Source(W),Before);
  const auto Applied=Edit(Cuts);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  const auto After=Source(W);TestTrue(TEXT("Actual union cap area"),FMath::Abs(MeshArea(W->CapMeshComponent)-CapBase-2*24*Cases[Case].Perimeter)<0.1);
  GEditor->UndoTransaction();TestEqual(TEXT("Union undo preserves source order and IDs"),Source(W),Before);GEditor->RedoTransaction();TestEqual(TEXT("Union redo preserves source order and IDs"),Source(W),After);
  W->RebuildWallMesh();TestTrue(TEXT("Rebuild uses same union boundary"),FMath::Abs(MeshArea(W->CapMeshComponent)-CapBase-2*24*Cases[Case].Perimeter)<0.1);TestEqual(TEXT("Rebuild preserves authored sources"),Source(W),After);
 }
 return true;
}
