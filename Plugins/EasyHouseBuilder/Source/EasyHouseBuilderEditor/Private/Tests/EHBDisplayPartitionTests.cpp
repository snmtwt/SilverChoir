#include "Core/EHBDisplayPartition.h"
#include "Core/EHBLogicalSurfaceAdjacency.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayPartitionTest,"EHB.Surfaces.DisplayPartition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayPartitionTest::RunTest(const FString& Parameters)
{
 auto Rect=[](double X,double Y,double W,double H){FEHBLogicalSurfaceRegion R;R.Boundary={{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}};return R;};
 FEHBDisplayPartitionInput A,B;A.Base.ElementGuid=FGuid::NewGuid();A.Base.SurfaceGuid=FGuid::NewGuid();A.Base.SourceName=TEXT("Slab.Surface");A.Base.Regions={Rect(0,0,100,100)};A.RequestedDisplay={Rect(-10,-10,120,120)};A.Priority=10;
 B=A;B.Priority=20;B.Base.ElementGuid=FGuid::NewGuid();B.Base.SurfaceGuid=FGuid::NewGuid();B.Base.Regions={Rect(100,0,100,100)};B.RequestedDisplay={Rect(90,-10,120,120)};
 FName Status;TArray<FEHBLogicalSurfaceDefinition> Out;
 for(const auto Rotation:{FRotator::ZeroRotator,FRotator(45,37,0),FRotator(90,0,0)})
 {
  A.Base.PlaneToBuilding=B.Base.PlaneToBuilding=FTransform(Rotation,FVector(300,-500,700));
  if(!TestTrue(*Status.ToString(),FEHBDisplayPartition::Build({B,A},Out,Status)))return false;
  TestEqual(TEXT("Explicit priority controls output"),Out[0].ElementGuid,A.Base.ElementGuid);
  TestTrue(TEXT("Expanded union remains fully covered"),FMath::IsNearlyEqual(Out[0].GetAreaCm2()+Out[1].GetAreaCm2(),26400.0,0.01));
  TArray<FEHBLogicalSurfaceSharedEdge> Shared;
  TestTrue(TEXT("Output regions do not overlap"),FEHBLogicalSurfaceAdjacency::Build(Out,Shared,Status));
  TestFalse(TEXT("Display cannot advertise support"),Out[0].bCanSupport);
  TestTrue(TEXT("Original neighbor material cannot be stolen"),!Out[0].ContainsProjectedPoint({105,50})&&Out[1].ContainsProjectedPoint({105,50}));
  TestTrue(TEXT("Contested exterior corner has single owner"),Out[0].ContainsProjectedPoint({105,105})&&!Out[1].ContainsProjectedPoint({105,105}));
  const double FirstArea=Out[0].GetAreaCm2();A.Base.ElementGuid=FGuid::NewGuid();B.Base.ElementGuid=FGuid::NewGuid();
  TestTrue(TEXT("Identity remapping preserves display ownership"),FEHBDisplayPartition::Build({A,B},Out,Status)&&FMath::IsNearlyEqual(Out[0].GetAreaCm2(),FirstArea,0.01));
 }
 auto Bad=B;Bad.Priority=A.Priority;TestFalse(TEXT("Ambiguous priority refused"),FEHBDisplayPartition::Build({A,Bad},Out,Status));TestTrue(TEXT("No partial output"),Out.IsEmpty());
 Bad=A;Bad.RequestedDisplay={Rect(10,10,80,80)};TestFalse(TEXT("Expansion cannot remove original material"),FEHBDisplayPartition::Build({Bad,B},Out,Status));
 TestFalse(TEXT("Frame rounding allowance cannot hide actual shrinkage"),FEHBDisplayPartition::Build({Bad,B},Out,Status,true));
 A.Base.Regions[0].Holes.AddDefaulted_GetRef().Vertices=Rect(20,20,20,20).Boundary;
 TestTrue(TEXT("Authored void protected even if display requests filling it"),FEHBDisplayPartition::Build({A,B},Out,Status));
 TestFalse(TEXT("Hole remains clear"),Out[0].ContainsProjectedPoint({30,30}));
 TestTrue(TEXT("Hole area excluded from expanded union"),FMath::IsNearlyEqual(Out[0].GetAreaCm2()+Out[1].GetAreaCm2(),26000.0,0.01));
 A.Base.Regions={Rect(0,0,100,100)};A.Base.Regions[0].Boundary={{0,0},{100,0},{100,50},{50,50},{50,100},{0,100}};
 B.Base.Regions={Rect(50,50,50,50)};B.RequestedDisplay={Rect(40,40,70,70)};
 TestTrue(TEXT("Concave notch and partial shared edges supported"),FEHBDisplayPartition::Build({B,A},Out,Status));
 TArray<FEHBLogicalSurfaceSharedEdge> Shared;
 TestTrue(TEXT("Concave display remains disjoint"),FEHBLogicalSurfaceAdjacency::Build(Out,Shared,Status));
 TestTrue(TEXT("Concave domain preserves complete requested union"),FMath::IsNearlyEqual(Out[0].GetAreaCm2()+Out[1].GetAreaCm2(),14400.0,0.01));
 auto Reference=A.Base;Reference.Regions={Rect(0,0,1000,1000)};auto Shifted=Reference;
 for(auto& P:Shifted.Regions[0].Boundary)P.X+=0.001;
 TestTrue(TEXT("Roundtrip validation bounds edge displacement rather than growing area tolerance"),FEHBDisplayPartition::MatchesWithinGrid(Reference,Shifted,Status));
 for(auto& P:Shifted.Regions[0].Boundary)P.X+=0.1;
 TestFalse(TEXT("Equal-area shifted allocation rejected"),FEHBDisplayPartition::MatchesWithinGrid(Reference,Shifted,Status));
 Reference.Regions[0].Holes.AddDefaulted_GetRef().Vertices=Rect(20,20,20,20).Boundary;Shifted=Reference;Shifted.Regions[0].Holes.Reset();
 TestFalse(TEXT("Grid allowance cannot fill authored hole"),FEHBDisplayPartition::MatchesWithinGrid(Reference,Shifted,Status));
 Shifted=Reference;Shifted.PlaneToBuilding.AddToTranslation(Reference.PlaneToBuilding.GetUnitAxis(EAxis::Z));
 TestFalse(TEXT("Different elevation cannot compare equal"),FEHBDisplayPartition::MatchesWithinGrid(Reference,Shifted,Status));
 return true;
}
#endif
