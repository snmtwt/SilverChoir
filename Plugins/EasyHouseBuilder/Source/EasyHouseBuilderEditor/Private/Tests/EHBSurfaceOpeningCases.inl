#include "Core/EHBSurfaceOpening.h"

namespace SurfaceOpening129
{
 FEHBCutOperation Make(const FEHBLogicalSurfaceDefinition& Host,double X,double Y)
 {
  FEHBCutOperation Cut;Cut.SurfaceHost.Version=1;Cut.SurfaceHost.BuildingGuid=Host.BuildingGuid;Cut.SurfaceHost.ElementGuid=Host.ElementGuid;Cut.SurfaceHost.SurfaceGuid=Host.SurfaceGuid;
  Cut.Stage=EEHBCutStage::SurfaceOpening;Cut.ProjectionMode=EEHBCutProjectionMode::TargetPlane;Cut.TransformPolicy=EEHBCutTransformPolicy::FollowTargetElement;Cut.Source.Height=24;
  for(const auto& P:TArray<FVector>{{X,Y,0},{X+20,Y,0},{X+20,Y+30,0},{X,Y+30,0}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;
  Cut.EnsureGuids();return Cut;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSurfaceOpeningValueTest,"EHB.Surfaces.OpeningHostContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSurfaceOpeningValueTest::RunTest(const FString& Parameters)
{
 FEHBLogicalSurfaceDefinition Host;Host.BuildingGuid=FGuid::NewGuid();Host.ElementGuid=FGuid::NewGuid();Host.SurfaceGuid=FGuid::NewGuid();Host.SourceName=TEXT("Test.Surface");Host.Thickness=24;
 Host.Regions.AddDefaulted_GetRef().Boundary={{0,0},{200,0},{200,150},{0,150}};
 const FTransform Element(FRotator(13,25,8),FVector(70,80,90));FName Status;TArray<FVector> Result;
 auto Cut=SurfaceOpening129::Make(Host,20,20);FString Before;FJsonObjectConverter::UStructToJsonObjectString(Cut,Before);
 for(const auto& Pose:{FTransform::Identity,FTransform(FRotator(90,0,0),FVector(10,20,30)),FTransform(FRotator(37,23,17),FVector(100,200,300))})
 {
  Host.PlaneToBuilding=Pose;if(!TestTrue(TEXT("Arbitrary captured plane resolves"),FEHBSurfaceOpening::Resolve(Cut,Host,Element,Result,Status)))return false;
  TestEqual(TEXT("All polygon points resolved"),Result.Num(),4);
  for(int32 I=0;I<Result.Num();++I)TestTrue(TEXT("Element conversion preserves surface coordinates"),Element.TransformPosition(Result[I]).Equals(Host.ToBuilding(FVector2D(Cut.Source.ExplicitPolygon.Points[I].LocalPosition)),1.e-6));
 }
 FString After;FJsonObjectConverter::UStructToJsonObjectString(Cut,After);TestEqual(TEXT("Resolution does not mutate source or IDs"),After,Before);
 for(double Angle:{13.0,37.0,89.0,123.0,-27.0})
 {
  auto Rotated=Cut;Rotated.Source.LocalTransform=FTransform(FRotator(0,Angle,0),FVector(80,70,0),FVector(1.1,0.9,1));
  TestTrue(TEXT("Chart rotation and scaling use consistent clipping precision"),FEHBSurfaceOpening::Resolve(Rotated,Host,Element,Result,Status));
 }
 for(int32 Failure=0;Failure<12;++Failure)
 {
  auto Bad=Cut;
  switch(Failure)
  {
   case 0:Bad.SurfaceHost.Version=2;break;
   case 1:Bad.SurfaceHost.BuildingGuid=FGuid::NewGuid();break;
   case 2:Bad.SurfaceHost.ElementGuid=FGuid::NewGuid();break;
   case 3:Bad.SurfaceHost.SurfaceGuid=FGuid::NewGuid();break;
   case 4:Bad.TransformPolicy=EEHBCutTransformPolicy::StaticWorld;break;
   case 5:Bad.Source.Height=12;break;
   case 6:Bad.Source.ExplicitPolygon.Points[1].PointGuid=Bad.Source.ExplicitPolygon.Points[0].PointGuid;break;
   case 7:Bad.Source.ExplicitPolygon.Points[1].LocalPosition.Z=1;break;
   case 8:Bad.Source.LocalTransform.SetLocation(FVector(190,0,0));break;
   case 9:Swap(Bad.Source.ExplicitPolygon.Points[1],Bad.Source.ExplicitPolygon.Points[2]);break;
   case 10:Bad.Source.SourceElementGuid=FGuid::NewGuid();break;
   case 11:Bad.Source.LocalTransform.SetLocation(FVector(0,0,10));break;
  }
  Result={FVector(1,2,3)};TestFalse(*FString::Printf(TEXT("Invalid contract rejected %d"),Failure),FEHBSurfaceOpening::Resolve(Bad,Host,Element,Result,Status));TestTrue(TEXT("No partial output"),Result.IsEmpty());
 }
 Host.Regions[0].Holes.AddDefaulted_GetRef().Vertices={{25,25},{35,25},{35,40},{25,40}};
 TestFalse(TEXT("Host hole enclosed by opening is still uncovered material"),FEHBSurfaceOpening::Resolve(Cut,Host,Element,Result,Status));TestEqual(TEXT("Whole-area host coverage reason"),Status,FName(TEXT("SurfaceOpeningOutsideHostDomain")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSurfaceOpeningWallTest,"EHB.Surfaces.BoundWallOpening",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSurfaceOpeningWallTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);
 ON_SCOPE_EXIT{W->Destroy();B->Destroy();};B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(800,-500,60)));
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=2)return false;
 for(int32 Side=0;Side<2;++Side)
 {
  auto Cut=SurfaceOpening129::Make(Hosts[Side],240,60);if(!TestTrue(TEXT("Candidate binding validates without mutation"),W->ValidateSurfaceOpeningBindings({Cut},Status)))return false;
  W->CutOperations={Cut};const auto SourceBefore=Source(W);W->RebuildWallMesh();if(!CheckArea(*this,W,500*260-600))return false;TestEqual(TEXT("Bound source survives generation"),Source(W),SourceBefore);
  auto Bad=Cut;Bad.SurfaceHost.SurfaceGuid=FGuid::NewGuid();TestFalse(TEXT("Wrong semantic host is rejected"),W->ValidateSurfaceOpeningBindings({Bad},Status));
  W->CutOperations={Bad};W->RebuildWallMesh();TestTrue(TEXT("Invalid binding cannot replace existing mesh with uncut wall"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-(500*260-600))<0.1);W->CutOperations={Cut};
  {FEHBChangeNotificationBatch Batch(*B);TArray<FEHBLogicalSurfaceDefinition> Hidden;TestFalse(TEXT("Public logical query retains in-flight guard"),W->QueryLogicalBaseSurfaces(Hidden,Status));W->RebuildWallMesh();if(!CheckArea(*this,W,500*260-600))return false;Batch.Rollback();}
  W->Height=300;W->RebuildWallMesh();if(!CheckArea(*this,W,500*300-600))return false;TestEqual(TEXT("Following host never rewrites opening coordinates"),Source(W),SourceBefore);W->Height=260;W->RebuildWallMesh();
 }
 const auto SavedIdentities=W->LogicalSurfaceIdentities;const auto SavedCuts=W->CutOperations;
 auto* LeftIdentity=W->LogicalSurfaceIdentities.FindByPredicate([](const auto& I){return I.Name==TEXT("Wall.Left");});
 auto* RightIdentity=W->LogicalSurfaceIdentities.FindByPredicate([](const auto& I){return I.Name==TEXT("Wall.Right");});
 if(!LeftIdentity||!RightIdentity)return false;
 RightIdentity->SurfaceGuid=LeftIdentity->SurfaceGuid;W->CutOperations[0].SurfaceHost.SurfaceGuid=LeftIdentity->SurfaceGuid;
 TestFalse(TEXT("Ambiguous saved surface IDs never select the first face"),W->ValidateSurfaceOpeningBindings(W->CutOperations,Status));TestEqual(TEXT("Specific ambiguity refusal"),Status,FName(TEXT("SurfaceOpeningHostAmbiguous")));
 W->LogicalSurfaceIdentities=SavedIdentities;W->CutOperations=SavedCuts;
 const auto SavedSource=Source(W);TArray<UEHBGeneratedMeshComponent*> Meshes;W->GetGeneratedMeshComponents(Meshes);for(auto* Mesh:Meshes)Mesh->DestroyComponent();
 W->LeftWallMeshComponent=nullptr;W->RightWallMeshComponent=nullptr;W->CapMeshComponent=nullptr;
 TestTrue(TEXT("Bound host validation needs no generated renderer"),W->ValidateSurfaceOpeningBindings(W->CutOperations,Status));TestEqual(TEXT("Renderer-free validation preserves source"),Source(W),SavedSource);
 return true;
}
