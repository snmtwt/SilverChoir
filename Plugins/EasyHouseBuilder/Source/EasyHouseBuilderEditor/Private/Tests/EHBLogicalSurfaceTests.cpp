#include "Core/EHBLogicalSurface.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EHB_Building.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
 TArray<FVector2D> SurfaceRect(double X,double Y,double W,double H){return {{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}};}
 TArray<FVector> SurfaceLoop3(const TArray<FVector2D>& P){TArray<FVector> R;for(const auto& V:P)R.Add(FVector(V.X,V.Y,0));return R;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSurfaceValueTest,"EHB.Surfaces.LogicalValue",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSurfaceValueTest::RunTest(const FString& Parameters)
{
 FEHBLogicalSurfaceDefinition S;S.ElementGuid=FGuid::NewGuid();S.SurfaceGuid=FGuid::NewGuid();S.SourceName=TEXT("Test.Plane");S.Thickness=20;S.bStructural=S.bCanSupport=true;
 auto& R=S.Regions.AddDefaulted_GetRef();R.Boundary=SurfaceRect(0,0,100,100);R.Holes.AddDefaulted_GetRef().Vertices=SurfaceRect(20,20,20,30);FName Status;
 for(auto Rotation:{FRotator::ZeroRotator,FRotator(0,37,0),FRotator(45,37,0),FRotator(90,0,0)})
 {
  S.PlaneToBuilding=FTransform(Rotation,FVector(200,-300,400));if(!TestTrue(*Status.ToString(),S.Validate(Status)))return false;TestEqual(TEXT("True plane area unaffected by orientation"),S.GetAreaCm2(),9400.0);
  const FVector2D Point(50,60);const auto World=S.ToBuilding(Point)+S.PlaneToBuilding.GetUnitAxis(EAxis::Z)*17;FVector2D Projected;double Distance;
  TestTrue(TEXT("Arbitrary plane projection"),S.Project(World,Projected,Distance));TestTrue(TEXT("Projection round trip in centimeters"),Projected.Equals(Point,1.e-9)&&FMath::IsNearlyEqual(Distance,17.0,1.e-9));
  TestTrue(TEXT("Interior material hit"),S.ContainsProjectedPoint(Point));TestFalse(TEXT("Hole interior excluded"),S.ContainsProjectedPoint({25,25}));TestFalse(TEXT("Hole boundary excluded"),S.ContainsProjectedPoint({20,25}));TestFalse(TEXT("Exterior excluded"),S.ContainsProjectedPoint({-1,50}));
 }
 auto Bad=S;Bad.Regions[0].Boundary={{0,0},{100,100},{0,100},{100,0}};TestFalse(TEXT("Crossing edges"),Bad.Validate(Status));
 Bad=S;Bad.Regions[0].Boundary.Insert({0,0},1);TestFalse(TEXT("Duplicate corner"),Bad.Validate(Status));
 Bad=S;Bad.Regions[0].Boundary={{0,0},{100,0},{50,0},{100,100},{0,100}};TestFalse(TEXT("Adjacent edge reversal"),Bad.Validate(Status));
 Bad=S;Bad.Regions[0].Holes[0].Vertices=SurfaceRect(90,20,20,20);TestFalse(TEXT("Hole crosses outer boundary"),Bad.Validate(Status));
 Bad=S;Bad.Regions[0].Holes.AddDefaulted_GetRef().Vertices=SurfaceRect(25,25,5,5);TestFalse(TEXT("Nested holes rejected"),Bad.Validate(Status));
 Bad=S;Bad.Regions.Add(S.Regions[0]);TestFalse(TEXT("Overlapping regions rejected"),Bad.Validate(Status));
 Bad=S;Bad.PlaneToBuilding.SetScale3D({1,2,1});TestFalse(TEXT("A plane frame must not hide scale"),Bad.Validate(Status));
 Bad=S;Bad.bStructural=false;TestFalse(TEXT("Nonstructural finish cannot claim support"),Bad.Validate(Status));
 Bad=S;Bad.Regions.AddDefaulted_GetRef().Boundary=SurfaceRect(200,0,20,30);TestTrue(TEXT("Disconnected region shares semantic surface identity"),Bad.Validate(Status));TestEqual(TEXT("Disconnected area"),Bad.GetAreaCm2(),10000.0);
 Bad=S;Bad.Regions.AddDefaulted_GetRef().Boundary=SurfaceRect(25,25,5,5);TestTrue(TEXT("Island in another region's hole"),Bad.Validate(Status));TestEqual(TEXT("Island area"),Bad.GetAreaCm2(),9425.0);
 Bad=S;Bad.Regions.SetNum(1);Bad.Regions[0].Holes.Reset();Bad.Regions[0].Boundary=SurfaceRect(1.e8-10,1.e8-10,1,1);TestTrue(TEXT("Small region at declared coordinate limit remains valid"),Bad.Validate(Status));TestEqual(TEXT("Far-coordinate area avoids cancellation"),Bad.GetAreaCm2(),1.0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSurfaceActorTest,"EHB.Surfaces.LogicalActor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSurfaceActorTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();auto* Building=World->SpawnActor<AEHB_Building>();auto* Floor=World->SpawnActor<AEHB_Floor>();auto* Other=World->SpawnActor<AEHB_Floor>();auto* Slab=World->SpawnActor<AEHB_FloorSlab>();ON_SCOPE_EXIT{Floor->Destroy();Other->Destroy();Slab->Destroy();Building->Destroy();};
 for(auto* E:TArray<AEHBElementActorBase*>{Floor,Other,Slab})E->AttachToBuilding(Building,FTransform::Identity);
 FEHBFloorFinishRegion Region;Region.OuterPolygon=SurfaceLoop3(SurfaceRect(0,0,200,100));Region.Holes.AddDefaulted_GetRef().LocalPolygon=SurfaceLoop3(SurfaceRect(20,20,20,30));
 if(!TestTrue(TEXT("Build actual floor with authored hole"),Floor->SetFloorRegions({Region},false)))return false;Slab->LocalTopPolygon=Region.OuterPolygon;Slab->LocalHoles.AddDefaulted_GetRef().LocalPolygon=Region.Holes[0].LocalPolygon;if(!TestTrue(TEXT("Build actual slab with authored hole"),Slab->RebuildSlabMesh()))return false;
 auto* Component=CastChecked<UEHBArchitecturalSurfaceComponent>(Slab->MeshComponent);const FGuid Legacy=Component->SurfaceGuid;
 Slab->LogicalSurfaceIdentities.Reset();Slab->LogicalSurfaceIdentityOwner={};Slab->RefreshLogicalSurfaceIdentities();TestEqual(TEXT("Migration adopts original surface GUID"),Slab->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")),Legacy);
 TArray<FEHBLogicalSurfaceDefinition> F,S;FName Status;
 if(!TestTrue(*Status.ToString(),Floor->QueryLogicalBaseSurfaces(F,Status))||!TestTrue(*Status.ToString(),Slab->QueryLogicalBaseSurfaces(S,Status)))return false;
 TestFalse(TEXT("Fresh floors have distinct surface IDs"),F[0].SurfaceGuid==Other->FindLogicalSurfaceIdentity(TEXT("Floor.Top")));TestEqual(TEXT("Floor logical area with hole"),F[0].GetAreaCm2(),19400.0);TestEqual(TEXT("Slab logical area"),S[0].GetAreaCm2(),19400.0);TestFalse(TEXT("Floor remains non-supporting"),F[0].bCanSupport);TestTrue(TEXT("Slab may support"),S[0].bCanSupport);
 const auto FloorId=F[0].SurfaceGuid,SlabId=S[0].SurfaceGuid;Floor->VisualOffset=15;Slab->VisualExpansion=10;Floor->RebuildFloorMesh();Slab->RebuildSlabMesh();
 TArray<FEHBFloorSupportSurface> Actual;const bool Captured=FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(Slab,Actual,Status);if(!TestTrue(*FString::Printf(TEXT("Capture actual expanded slab: %s"),*Status.ToString()),Captured))return false;TArray<FEHBFloorFinishRegion> RenderRegions;for(const auto& T:Actual){auto& R=RenderRegions.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double RenderArea=0;const bool Measured=FEHBFloorContactGeometry::MeasureArea(RenderRegions,Actual,RenderArea,Status);if(!TestTrue(*FString::Printf(TEXT("Measure actual expanded slab: %s"),*Status.ToString()),Measured))return false;TestTrue(TEXT("Visual expansion actually changes render area"),RenderArea>19400.0);
 TArray<FVector> DisplayOutline;
 TestTrue(TEXT("Effective display outline available"),Slab->BuildEffectiveOuterPolygon(DisplayOutline));
 FEHBFloorFinishRegion DisplayRegion;DisplayRegion.OuterPolygon=DisplayOutline;DisplayRegion.Holes=Region.Holes;
 FEHBFloorSupportSurface DisplayTop;DisplayTop.OuterPolygon=DisplayOutline;DisplayTop.Holes=DisplayRegion.Holes;
 double DisplayArea=0,DisplayIntersection=0;
 TestTrue(TEXT("Measure queried display"),FEHBFloorContactGeometry::MeasureArea({DisplayRegion},{DisplayTop},DisplayArea,Status));
 TestTrue(TEXT("Intersect queried and generated display"),FEHBFloorContactGeometry::MeasureArea({DisplayRegion},Actual,DisplayIntersection,Status));
 TestTrue(*FString::Printf(TEXT("Effective and actual expanded coverage agree: display %.4f, actual %.4f, intersection %.4f"),DisplayArea,RenderArea,DisplayIntersection),FMath::IsNearlyEqual(DisplayArea,RenderArea,0.01)&&FMath::IsNearlyEqual(DisplayIntersection,RenderArea,0.01));
 const auto SavedHoles=Slab->LocalHoles;
 Slab->LocalHoles.Reset();Slab->LocalHoles.AddDefaulted_GetRef().LocalPolygon=SurfaceLoop3(SurfaceRect(-1000,-1000,2000,2000));
 TestFalse(TEXT("Fully cut slab has no effective boundary"),Slab->BuildEffectiveOuterPolygon(DisplayOutline));
 TestTrue(TEXT("Fully cut query clears previous output"),DisplayOutline.IsEmpty());
 Slab->LocalHoles=SavedHoles;
 for(auto* E:TArray<AEHBElementActorBase*>{Floor,Slab})
 {
  E->SetActorTransform(FTransform(FRotator(45,37,0),FVector(300,-200,500)));TArray<FEHBLogicalSurfaceDefinition> Values;const bool Queried=E->QueryLogicalBaseSurfaces(Values,Status);if(!TestTrue(*FString::Printf(TEXT("Query tilted authored surface: %s"),*Status.ToString()),Queried))return false;const auto Before=Values[0];TestEqual(TEXT("Visual treatment cannot change logical area"),Before.GetAreaCm2(),19400.0);
  const auto Identities=E->LogicalSurfaceIdentities;TArray<UEHBGeneratedMeshComponent*> Meshes;E->GetGeneratedMeshComponents(Meshes);for(auto* M:Meshes)M->DestroyComponent();
  if(E==Floor){Floor->MeshComponent=nullptr;Floor->RegionMeshComponents.Reset();}else Slab->MeshComponent=nullptr;
  if(!TestTrue(TEXT("Query succeeds without any generated mesh component"),E->QueryLogicalBaseSurfaces(Values,Status)))return false;
  TestEqual(TEXT("Baked-away renderer preserves source identity"),Values[0].SurfaceGuid,Before.SurfaceGuid);TestEqual(TEXT("Renderer-free area preserved"),Values[0].GetAreaCm2(),Before.GetAreaCm2());TestTrue(TEXT("Renderer-free plane preserved"),Values[0].PlaneToBuilding.Equals(Before.PlaneToBuilding,1.e-9));TestEqual(TEXT("Read-only identity table count"),E->LogicalSurfaceIdentities.Num(),Identities.Num());
 }
 TestEqual(TEXT("Floor source ID survived all rebuilds"),Floor->FindLogicalSurfaceIdentity(TEXT("Floor.Top")),FloorId);TestEqual(TEXT("Slab source ID survived all rebuilds"),Slab->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")),SlabId);
 const auto OtherOldId=Other->FindLogicalSurfaceIdentity(TEXT("Floor.Top"));Other->RegenerateElementGuid();TestFalse(TEXT("Ordinary identity regeneration creates a new surface identity"),Other->FindLogicalSurfaceIdentity(TEXT("Floor.Top"))==OtherOldId);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSlabAuthoredCutsTest,"EHB.Surfaces.SlabAuthoredCuts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSlabAuthoredCutsTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();auto* Building=World->SpawnActor<AEHB_Building>();auto* Slab=World->SpawnActor<AEHB_FloorSlab>();
 ON_SCOPE_EXIT{Slab->Destroy();Building->Destroy();};Slab->AttachToBuilding(Building,FTransform(FRotator(0,37,0),FVector(20,30,40)));
 Slab->LocalTopPolygon=SurfaceLoop3(SurfaceRect(0,0,500,500));Slab->VisualExpansion=0;
 auto AddHole=[&](double X,double Y,double W,double H){Slab->LocalHoles.AddDefaulted_GetRef().LocalPolygon=SurfaceLoop3(SurfaceRect(X,Y,W,H));};
 AddHole(200,-10,20,520);AddHole(300,100,50,50);AddHole(325,100,50,50);AddHole(-10,100,30,50);AddHole(700,700,20,20);
 if(!TestTrue(TEXT("Native mesh accepts split and overlapping authored cuts"),Slab->RebuildSlabMesh()))return false;
 const auto Source=Slab->LocalHoles;const auto Id=Slab->FindLogicalSurfaceIdentity(TEXT("Slab.Surface"));TArray<FEHBLogicalSurfaceDefinition> Values;FName Status;
 if(!TestTrue(TEXT("Logical base normalizes authored cuts"),Slab->QueryLogicalBaseSurfaces(Values,Status)))return false;
 TestEqual(TEXT("All disconnected regions retained"),Values[0].Regions.Num(),2);TestEqual(TEXT("Overlapping holes union, notch subtracts, exterior hole ignored"),Values[0].GetAreaCm2(),235250.0);
 const auto* Mesh=Slab->MeshComponent->GetProcMeshSection(0);double MeshArea=0;if(!Mesh)return false;
 for(int32 I=0;I<Mesh->ProcIndexBuffer.Num();I+=3)
 {
  const auto A=Mesh->ProcVertexBuffer[Mesh->ProcIndexBuffer[I]].Position,B=Mesh->ProcVertexBuffer[Mesh->ProcIndexBuffer[I+1]].Position,C=Mesh->ProcVertexBuffer[Mesh->ProcIndexBuffer[I+2]].Position;
  if(FMath::Abs(A.Z-Slab->GetTopZ())<0.001&&FMath::Abs(B.Z-A.Z)<0.001&&FMath::Abs(C.Z-A.Z)<0.001)MeshArea+=FVector::CrossProduct(B-A,C-A).Size()*0.5;
 }
 TestTrue(TEXT("Logical area matches actual generated top triangles"),FMath::Abs(MeshArea-Values[0].GetAreaCm2())<0.01);
 TestFalse(TEXT("Stripe excludes points"),Values[0].ContainsProjectedPoint({210,250}));TestFalse(TEXT("Overlapping hole excludes points"),Values[0].ContainsProjectedPoint({360,120}));TestTrue(TEXT("Other fragment retained"),Values[0].ContainsProjectedPoint({400,400}));
 TestEqual(TEXT("Source loop count unchanged"),Slab->LocalHoles.Num(),Source.Num());for(int32 I=0;I<Source.Num();++I)TestTrue(TEXT("Read-only source loops"),Slab->LocalHoles[I].LocalPolygon==Source[I].LocalPolygon);
 Slab->VisualExpansion=15;Slab->MeshComponent->DestroyComponent();Slab->MeshComponent=nullptr;
 TestTrue(TEXT("Normalization independent of renderer"),Slab->QueryLogicalBaseSurfaces(Values,Status));if(!Values.IsEmpty()){TestEqual(TEXT("Identity unchanged"),Values[0].SurfaceGuid,Id);TestEqual(TEXT("Expansion excluded"),Values[0].GetAreaCm2(),235250.0);}
 Slab->LocalHoles[0].LocalPolygon=SurfaceLoop3({{0,0},{100,100},{0,100},{100,0}});TestFalse(TEXT("Invalid source is not repaired by clipping"),Slab->QueryLogicalBaseSurfaces(Values,Status));TestTrue(TEXT("Failed query clears output"),Values.IsEmpty());
 Slab->LocalHoles.Reset();AddHole(-10,-10,520,520);TestFalse(TEXT("Fully erased base rejected"),Slab->QueryLogicalBaseSurfaces(Values,Status));
 return true;
}
#include "EHBLogicalWallCases.inl"
#endif
