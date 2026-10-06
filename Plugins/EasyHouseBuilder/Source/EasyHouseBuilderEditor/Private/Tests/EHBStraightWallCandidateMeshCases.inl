IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStraightWallCandidateMeshTest,"EHB.Surfaces.StraightWallCandidateMesh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStraightWallCandidateMeshTest::RunTest(const FString& Parameters)
{
 FEHBStraightWallOpeningMeshSource S;S.Geometry.ReferenceLength=500;S.Geometry.StartLeftX=S.Geometry.StartRightX=-250;S.Geometry.EndLeftX=S.Geometry.EndRightX=250;S.Height=260;S.Thickness=24;
 auto Area=[](const FEHBWallJunctionMesh& M){double A=0;for(int32 I=0;I<M.Triangles.Num();I+=3)A+=FVector::CrossProduct(M.Vertices[M.Triangles[I+1]]-M.Vertices[M.Triangles[I]],M.Vertices[M.Triangles[I+2]]-M.Vertices[M.Triangles[I]]).Size()*0.5;return A;};
 auto Rect=[](double X,double Z){return TArray<FVector2d>{{X,Z},{X+20,Z},{X+20,Z+30},{X,Z+30}};};
 FEHBStraightWallOpeningMeshes Mesh;FName Status;
 if(!TestTrue(TEXT("Actor-free solid wall"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;
 TestEqual(TEXT("Uncut wall retains four-vertex side"),Mesh.Left.Vertices.Num(),4);TestTrue(TEXT("Independent solid side area"),FMath::Abs(Area(Mesh.Left)-130000)<0.01);TestTrue(TEXT("Independent solid cap area"),FMath::Abs(Area(Mesh.Caps)-36480)<0.01);
 S.Openings={Rect(-10,60)};if(!TestTrue(TEXT("Actor-free interior cut"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;
 TestTrue(TEXT("Interior cut subtracts600 from both sides"),FMath::Abs(Area(Mesh.Left)-129400)<0.01&&FMath::Abs(Area(Mesh.Right)-129400)<0.01);TestTrue(TEXT("Interior reveal adds4800 double-sided area"),FMath::Abs(Area(Mesh.Caps)-41280)<0.02);TestTrue(TEXT("Boundary cap area excludes reveals"),FMath::Abs(Mesh.BoundaryCapArea-36480)<0.01);
 const auto Duplicate=S.Openings[0];S.Openings.Add(Duplicate);if(!TestTrue(TEXT("Overlap resolves as union"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;TestTrue(TEXT("Duplicate opening never doubles removed area"),FMath::Abs(Area(Mesh.Left)-129400)<0.01&&FMath::Abs(Area(Mesh.Caps)-41280)<0.02);
 S.Openings={Rect(-10,0)};if(!TestTrue(TEXT("Actor-free bottom cut"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;TestTrue(TEXT("Bottom exit removes480 and adds3840 reveal"),FMath::Abs(Area(Mesh.Caps)-39840)<0.02);
 S.Openings={Rect(-10,60)};S.bGenerateStartCap=false;S.bGenerateEndCap=false;if(!TestTrue(TEXT("Candidate cap policy"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;TestTrue(TEXT("Disabled ends leave top bottom and reveals"),FMath::Abs(Area(Mesh.Caps)-28800)<0.02);
 S.bGenerateStartCap=S.bGenerateEndCap=true;S.Geometry.EndLeftX=S.Geometry.EndRightX=100;if(!TestTrue(TEXT("Shortened candidate mesh generated without actor"),FEHBStraightWallOpeningMesh::Build(S,Mesh,Status)))return false;TestTrue(TEXT("Shortened side follows new dimensions"),FMath::Abs(Area(Mesh.Left)-(350*260-600))<0.01);
 auto Bad=S;Bad.Openings[0][1].X=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Invalid candidate input rejected"),FEHBStraightWallOpeningMesh::Build(Bad,Mesh,Status));TestTrue(TEXT("Invalid candidate publishes no mesh"),Mesh.Left.Vertices.IsEmpty()&&Mesh.Right.Vertices.IsEmpty()&&Mesh.Caps.Vertices.IsEmpty()&&Mesh.BoundaryCapArea==0);
 Bad=S;Bad.Geometry.EndRightX=Bad.Geometry.StartRightX;TestFalse(TEXT("Degenerate second side rejected atomically"),FEHBStraightWallOpeningMesh::Build(Bad,Mesh,Status));TestTrue(TEXT("No partial valid left side"),Mesh.Left.Vertices.IsEmpty());
 return true;
}
