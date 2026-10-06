#include "Core/EHBLogicalSurfaceAdjacency.h"
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalAdjacencyTest,"EHB.Surfaces.Adjacency",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalAdjacencyTest::RunTest(const FString& Parameters)
{
 const FGuid Building=FGuid::NewGuid();
 auto Rect=[&](double X,double Y,double W,double H){FEHBLogicalSurfaceDefinition S;S.BuildingGuid=Building;S.ElementGuid=FGuid::NewGuid();S.SurfaceGuid=FGuid::NewGuid();S.SourceName=TEXT("Slab.Surface");S.FloorIndex=1;S.Regions.AddDefaulted_GetRef().Boundary={{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}};return S;};
 auto A=Rect(0,0,100,100),B=Rect(100,20,60,50);TArray<FEHBLogicalSurfaceSharedEdge> Links;FName Status;
 auto Build=[&](const TArray<FEHBLogicalSurfaceDefinition>& S){const bool Ready=FEHBLogicalSurfaceAdjacency::Build(S,Links,Status);return TestTrue(*Status.ToString(),Ready);};
 for(const auto Rotation:{FRotator::ZeroRotator,FRotator(45,37,0),FRotator(90,0,0)})
 {
  A.PlaneToBuilding=B.PlaneToBuilding=FTransform(Rotation,FVector(300,-500,700));if(!Build({A,B}))return false;TestEqual(TEXT("Partial shared boundary found once"),Links.Num(),1);if(Links.Num()!=1)return false;
  TestTrue(TEXT("Actual shared length, not entire edge"),FMath::IsNearlyEqual((Links[0].End-Links[0].Start).Size(),50.0,1.e-8));const auto Saved=Links[0];if(!Build({B,A}))return false;TestEqual(TEXT("Input order keeps qualified owner"),Links[0].A.ElementGuid,Saved.A.ElementGuid);TestTrue(TEXT("Input order keeps interval"),Links[0].Start.Equals(Saved.Start,1.e-9)&&Links[0].End.Equals(Saved.End,1.e-9));
 }
 A.PlaneToBuilding=B.PlaneToBuilding=FTransform::Identity;auto C=Rect(100,70,60,30);if(!Build({A,B,C}))return false;double Total=0;for(const auto& L:Links)Total+=(L.End-L.Start).Size();TestEqual(TEXT("Three-way boundary intervals include short neighbor edge"),Links.Num(),3);TestTrue(TEXT("Partial edges retain measured length"),FMath::IsNearlyEqual(Total,140.0,1.e-8));
 Algo::Reverse(A.Regions[0].Boundary);if(!Build({A,B}))return false;TestEqual(TEXT("Winding does not change adjacency"),Links.Num(),1);
 auto Bad=B;Bad.PlaneToBuilding.SetTranslation(FVector(0.1,0,0));if(!Build({A,Bad}))return false;TestTrue(TEXT("Real gap is not bridged"),Links.IsEmpty());
 Bad=B;Bad.PlaneToBuilding.SetTranslation(FVector(0,0,2));if(!Build({A,Bad}))return false;TestTrue(TEXT("Different elevation is not connected"),Links.IsEmpty());
 Bad=B;Bad.FloorIndex=2;if(!Build({A,Bad}))return false;TestTrue(TEXT("Floor identity isolates adjacency"),Links.IsEmpty());
 Bad=B;Bad.BuildingGuid=FGuid::NewGuid();if(!Build({A,Bad}))return false;TestTrue(TEXT("Building identity isolates adjacency"),Links.IsEmpty());
 Bad=Rect(90,20,60,50);TestFalse(TEXT("Overlap cannot masquerade as shared seam"),FEHBLogicalSurfaceAdjacency::Build({A,Bad},Links,Status));TestTrue(TEXT("Failure has no partial adjacency"),Links.IsEmpty());
 TestFalse(TEXT("Duplicate qualified identity rejected"),FEHBLogicalSurfaceAdjacency::Build({A,A},Links,Status));
 A=Rect(0,0,100,100);A.Regions[0].Holes.AddDefaulted_GetRef().Vertices={{20,20},{60,20},{60,60},{20,60}};B=Rect(20,20,40,40);if(!Build({A,B}))return false;TestEqual(TEXT("Hole and inset share four edges"),Links.Num(),4);TestTrue(TEXT("Hole references remain explicit"),Links.ContainsByPredicate([](const auto& L){return L.A.Hole!=INDEX_NONE||L.B.Hole!=INDEX_NONE;}));
 B=Rect(100,100,20,20);if(!Build({A,B}))return false;TestTrue(TEXT("Point-only touch is not an edge"),Links.IsEmpty());return true;
}
#endif
