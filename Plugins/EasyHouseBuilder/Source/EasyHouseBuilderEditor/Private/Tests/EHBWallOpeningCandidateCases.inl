#include "Core/EHBWallOpeningCandidate.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningCandidateTest,"EHB.Surfaces.WallOpeningCandidate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningCandidateTest::RunTest(const FString& Parameters)
{
 FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),2,true))return false;auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
 FEHBWallNodeModel Model;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model);if(!TestTrue(*Capture.Status.ToString(),Capture.bSucceeded))return false;FName Status;auto Draft=FEHBWallNodeSideDraft::Build(Model,Status);if(!TestTrue(*Status.ToString(),Draft.IsReady()))return false;
 // Capture the permitted node model first. The value/contact layer also has to
 // handle a building transform; this does not enable scaled node editing.
 B->SetActorScale3D(FVector(2,3,1));
 FEHBWallOpeningCandidateSource Source;Source.BuildingToWorld=B->GetActorTransform();auto& H=Source.Host;H.BuildingGuid=B->BuildingGuid;H.LeftSurfaceGuid=W->FindLogicalSurfaceIdentity(TEXT("Wall.Left"));H.RightSurfaceGuid=W->FindLogicalSurfaceIdentity(TEXT("Wall.Right"));H.FloorIndex=W->FloorIndex;H.Height=W->Height;H.Thickness=W->Thickness;H.bStructural=true;
 const auto* Side=Draft.GetWallSides().FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!Side)return false;H.Sides=*Side;
 TArray<FEHBLogicalSurfaceDefinition> Hosts;if(!FEHBWallSurfaceHosts::Build(H,Hosts,Status))return false;const auto Before=OpeningRegionState(B);
 auto SameMesh=[&](const FEHBWallJunctionMesh& A,const FEHBWallJunctionMesh& C){if(A.Triangles!=C.Triangles||A.Vertices.Num()!=C.Vertices.Num())return false;for(int32 I=0;I<A.Vertices.Num();++I)if(!A.Vertices[I].Equals(C.Vertices[I],1.e-5)||!A.Normals[I].Equals(C.Normals[I],1.e-5)||!A.UVs[I].Equals(C.UVs[I],1.e-5))return false;return true;};
 for(int32 I:{0,1})
 {
  const auto Cut=SurfaceOpening129::Make(Hosts[I],40,60);FEHBPreparedWallOpening Value,Live;
  if(!TestTrue(*Status.ToString(),FEHBWallOpeningCandidate::Build(Source,{Cut},Value,Status))||!TestTrue(*Status.ToString(),W->PrepareSurfaceOpening({Cut},Live,Status)))return false;
  TestTrue(TEXT("Value plan matches all live prepared geometry"),SameMesh(Value.Left,Live.Left)&&SameMesh(Value.Right,Live.Right)&&SameMesh(Value.Caps,Live.Caps));TestEqual(TEXT("Pure graph revision is unassigned"),Value.GraphRevision,INDEX_NONE);TestEqual(TEXT("Pure geometry revision is unassigned"),Value.GeometryRevision,INDEX_NONE);TestEqual(TEXT("Actor adapter assigns current graph"),Live.GraphRevision,B->RelationshipGraphRevision);
  TestTrue(TEXT("Candidate has top and bottom contact faces"),!Value.HorizontalTops.IsEmpty()&&!Value.HorizontalBottoms.IsEmpty());
  const auto Old=Value;TestTrue(TEXT("Source alias is safe"),FEHBWallOpeningCandidate::Build(Source,Value.Sources,Value,Status));TestEqual(TEXT("Aliased source survives reset"),Value.Sources.Num(),1);TestTrue(TEXT("Aliased candidate preserves geometry"),SameMesh(Value.Caps,Old.Caps));
  auto Moved=Model;const FVector Delta(30,70,0);for(auto& Node:Moved.Nodes)Node.LocalTransform.AddToTranslation(Delta);auto MovedDraft=FEHBWallNodeSideDraft::Build(Moved,Status);if(!MovedDraft.IsReady())return false;auto Next=Source;const auto* NextSide=MovedDraft.GetWallSides().FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!NextSide)return false;Next.Host.Sides=*NextSide;
  FEHBPreparedWallOpening MovedValue;if(!TestTrue(TEXT("Complete moved value plan"),FEHBWallOpeningCandidate::Build(Next,{Cut},MovedValue,Status)))return false;TestTrue(TEXT("Translated plan retains local mesh"),SameMesh(Value.Left,MovedValue.Left)&&SameMesh(Value.Caps,MovedValue.Caps));
  TestEqual(TEXT("Translated contacts retain topology"),MovedValue.HorizontalTops.Num(),Value.HorizontalTops.Num());for(int32 T=0;T<Value.HorizontalTops.Num()&&T<MovedValue.HorizontalTops.Num();++T)for(int32 P=0;P<3;++P)TestTrue(TEXT("Contact follows candidate in building coordinates"),MovedValue.HorizontalTops[T].OuterPolygon[P].Equals(Value.HorizontalTops[T].OuterPolygon[P]+Delta,0.001));
 }
 auto Bad=Source;Bad.Host.Sides.EndRight=Bad.Host.Sides.StartRight+(Bad.Host.Sides.EndRight-Bad.Host.Sides.StartRight).GetSafeNormal()*50;const auto Cut=SurfaceOpening129::Make(Hosts[1],40,60);FEHBPreparedWallOpening Rejected;
 TestTrue(TEXT("Seed prior candidate"),FEHBWallOpeningCandidate::Build(Source,{Cut},Rejected,Status));TestFalse(TEXT("Shortened host rejects complete opening plan"),FEHBWallOpeningCandidate::Build(Bad,{Cut},Rejected,Status));TestTrue(TEXT("Failed complete plan leaves no sources mesh or contact"),Rejected.Sources.IsEmpty()&&Rejected.Left.Vertices.IsEmpty()&&Rejected.Caps.Vertices.IsEmpty()&&Rejected.HorizontalTops.IsEmpty()&&Rejected.HorizontalBottoms.IsEmpty());
 Bad=Source;Bad.BuildingToWorld.SetScale3D(FVector::ZeroVector);TestFalse(TEXT("Invalid contact frame rejects complete candidate"),FEHBWallOpeningCandidate::Build(Bad,{Cut},Rejected,Status));TestTrue(TEXT("Late failure never publishes generated mesh"),Rejected.Caps.Vertices.IsEmpty());
 TestFalse(TEXT("Duplicate source operation refuses"),FEHBWallOpeningCandidate::Build(Source,{Cut,Cut},Rejected,Status));TestEqual(TEXT("Duplicate identity contract"),Status,FName(TEXT("DuplicateSurfaceOpeningIdentity")));TestEqual(TEXT("All candidate computations preserve original building"),OpeningRegionState(B),Before);
 return true;
}
