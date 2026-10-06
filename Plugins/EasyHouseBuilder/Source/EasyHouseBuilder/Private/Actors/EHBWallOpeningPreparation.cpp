#include "Actors/EHB_Wall.h"
#include "Core/EHBPreparedWallOpening.h"
#include "Core/EHBBuildingActorBase.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "Cutting/EHBPolygonClipper.h"
#include "Core/EHBWallOpeningMeasure.h"
#include "EHBWallOpeningMeasure.inl"

#if WITH_DEV_AUTOMATION_TESTS
int32 EHBWallOpeningPreparation::FailurePhase=0;
#endif

namespace
{
 bool FailAt(int32 Phase)
 {
#if WITH_DEV_AUTOMATION_TESTS
  if(EHBWallOpeningPreparation::FailurePhase==Phase){EHBWallOpeningPreparation::FailurePhase=0;return true;}
#endif
  return false;
 }
 bool Area(const FEHBWallJunctionMesh& Mesh,double& Result)
 {
  Result=0;
  if(Mesh.Triangles.Num()%3||Mesh.Normals.Num()!=Mesh.Vertices.Num()||Mesh.UVs.Num()!=Mesh.Vertices.Num())return false;
  for(int32 I=0;I<Mesh.Vertices.Num();++I)
   if(Mesh.Vertices[I].ContainsNaN()||Mesh.Normals[I].ContainsNaN()||Mesh.UVs[I].ContainsNaN())return false;
  for(int32 I=0;I<Mesh.Triangles.Num();I+=3)
  {
   const int32 A=Mesh.Triangles[I],B=Mesh.Triangles[I+1],C=Mesh.Triangles[I+2];
   if(!Mesh.Vertices.IsValidIndex(A)||!Mesh.Vertices.IsValidIndex(B)||!Mesh.Vertices.IsValidIndex(C))return false;
   Result+=FVector::CrossProduct(Mesh.Vertices[B]-Mesh.Vertices[A],Mesh.Vertices[C]-Mesh.Vertices[A]).Size()*0.5;
  }
  return FMath::IsFinite(Result);
 }
}

#include "EHBWallOpeningCandidate.inl"

bool AEHB_Wall::PrepareNodeSurfaceOpening(const FEHBWallJunctionWallSides& Geometry,FEHBPreparedWallOpening& Out,FName& Status) const
{
 Out={};
 if(!OwningBuilding||GetClass()!=StaticClass()||Geometry.WallGuid!=ElementGuid||CurveControlOffset!=0||bHasPreviewDoorWindowConnection
  ||LeftSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple||RightSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple)
 {Status=TEXT("NodeOpeningSourceRequiresPlan");return false;}
 FEHBWallOpeningCandidateSource Source;Source.BuildingToWorld=OwningBuilding->GetActorTransform();
 Source.bGenerateStartCap=bGenerateLinkedPillarEndCaps||!StartPillarGuid.IsValid();Source.bGenerateEndCap=bGenerateLinkedPillarEndCaps||!EndPillarGuid.IsValid();
 auto& H=Source.Host;H.BuildingGuid=OwningBuilding->BuildingGuid;H.FloorIndex=FloorIndex;H.Height=Height;H.Thickness=Thickness;H.bStructural=HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural));
 H.LeftSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Left"));H.RightSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Right"));H.Sides=Geometry;
 TArray<FEHBCutOperation> Inputs;TSet<FGuid> Consumed;
 for(const auto& Cut:CutOperations)if(Cut.SurfaceHost.Version==1)Inputs.Add(Cut);
 else if(!DoorWindowConnections.ContainsByPredicate([&](const auto& C){return C.DoorWindowGuid==Cut.Source.SourceElementGuid;}))
 {Status=TEXT("NodeLegacyOpeningRequiresPlan");return false;}
 FEHBWallResolvedGeometry CandidateGeometry;CandidateGeometry.ReferenceLength=FVector::Distance(Geometry.LocalStart,Geometry.LocalEnd);
 const double MinX=FMath::Max(Geometry.LocalTransform.InverseTransformPosition(Geometry.StartLeft).X,Geometry.LocalTransform.InverseTransformPosition(Geometry.StartRight).X);
 const double MaxX=FMath::Min(Geometry.LocalTransform.InverseTransformPosition(Geometry.EndLeft).X,Geometry.LocalTransform.InverseTransformPosition(Geometry.EndRight).X);
 for(const auto& C:DoorWindowConnections)
 {
  if(!C.DoorWindowGuid.IsValid()||Consumed.Contains(C.DoorWindowGuid)||!FMath::IsFinite(C.DistanceFromStart)||C.DistanceFromStart<0||C.DistanceFromStart>CandidateGeometry.ReferenceLength)
  {Status=TEXT("NodeDoorWindowOutsideWall");return false;}Consumed.Add(C.DoorWindowGuid);
  const auto Matches=CutOperations.FilterByPredicate([&](const auto& V){return V.SurfaceHost.Version==0&&V.Source.SourceElementGuid==C.DoorWindowGuid;});
  if(Matches.Num()!=1||!Matches[0].bEnabled||Matches[0].OperationType!=EEHBCutOperationType::Subtract||Matches[0].Stage!=EEHBCutStage::SurfaceOpening||Matches[0].ProjectionMode!=EEHBCutProjectionMode::VerticalXZ||Matches[0].TransformPolicy!=EEHBCutTransformPolicy::SourceActorDriven)
  {Status=TEXT("NodeDoorWindowCutMismatch");return false;}
  TArray<FVector2d> Polygon;if(!BuildConnectionOpeningPolygon(CandidateGeometry,C,Polygon)||Polygon.Num()!=Matches[0].Source.ExplicitPolygon.Points.Num())
  {Status=TEXT("NodeDoorWindowOutlineMismatch");return false;}
  for(const auto P:Polygon)if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)||P.X<MinX-0.001||P.X>MaxX+0.001||P.Y<-.5001||P.Y>Height+0.001)
  {Status=TEXT("NodeDoorWindowOutsideWall");return false;}
  Source.ConnectionOpenings.Add(MoveTemp(Polygon));
 }
 return FEHBWallOpeningCandidate::Build(Source,Inputs,Out,Status);
}

bool AEHB_Wall::PrepareSurfaceOpening(const TArray<FEHBCutOperation>& Candidate,FEHBPreparedWallOpening& Out,FName& Status) const
{
 const auto Inputs=Candidate;Out={};auto Fail=[&](FName Why){Status=Why;return false;};
 if(!IsValid(OwningBuilding)||OwningBuilding->HasUnpublishedEdit())return Fail(TEXT("OpeningPreparationUnavailable"));
 if(GetClass()!=StaticClass()||!FMath::IsNearlyZero(CurveControlOffset)||LeftSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple||RightSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple||!DoorWindowConnections.IsEmpty()||bHasPreviewDoorWindowConnection)return Fail(TEXT("OpeningPreparationRequiresHostPlan"));
 for(const auto& Cut:CutOperations)if(Cut.SurfaceHost.Version!=1)return Fail(TEXT("OpeningPreparationRequiresLegacyPlan"));
 FEHBWallOpeningCandidateSource Source;Source.BuildingToWorld=OwningBuilding->GetActorTransform();
 Source.bGenerateStartCap=bGenerateLinkedPillarEndCaps||!StartPillarGuid.IsValid();Source.bGenerateEndCap=bGenerateLinkedPillarEndCaps||!EndPillarGuid.IsValid();
 auto& H=Source.Host;H.BuildingGuid=OwningBuilding->BuildingGuid;H.FloorIndex=FloorIndex;H.Height=Height;H.Thickness=Thickness;H.bStructural=HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural));
 H.LeftSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Left"));H.RightSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Right"));H.Sides.WallGuid=ElementGuid;H.Sides.LocalTransform=GetElementLocalTransform();H.Sides.LocalStart=LocalStart;H.Sides.LocalEnd=LocalEnd;
 for(bool Left:{true,false}){TArray<FVector> Top;if(!BuildSideTopPolylineInBuildingSpace(Left,Top)||Top.Num()!=2)return Fail(TEXT("InvalidOpeningHostGeometry"));if(Left){H.Sides.StartLeft=Top[0];H.Sides.EndLeft=Top[1];}else{H.Sides.StartRight=Top[0];H.Sides.EndRight=Top[1];}}
 if(!FEHBWallOpeningCandidate::Build(Source,Inputs,Out,Status))return false;
 Out.GraphRevision=OwningBuilding->RelationshipGraphRevision;Out.GeometryRevision=OwningBuilding->GetElementGeometryRevision(ElementGuid);return true;
}

#include "Components/EHBGeneratedMeshComponent.h"

bool AEHB_Wall::ApplyPreparedSurfaceOpening(const FEHBPreparedWallOpening& Prepared,FName& Status)
{
 auto Fail=[&](FName Why){Status=Why;return false;};
 if(!OwningBuilding||!OwningBuilding->HasUnpublishedEdit()||Prepared.BuildingGuid!=OwningBuilding->BuildingGuid||Prepared.ElementGuid!=ElementGuid||Prepared.GraphRevision!=OwningBuilding->RelationshipGraphRevision||Prepared.GeometryRevision!=OwningBuilding->GetElementGeometryRevision(ElementGuid))return Fail(TEXT("StalePreparedOpening"));
 TArray<UEHBGeneratedMeshComponent*> Components{LeftWallMeshComponent,RightWallMeshComponent,CapMeshComponent};
 for(auto* Component:Components)if(!IsValid(Component)||Component->GetOwner()!=this||Component->GetNumSections()!=1||!Component->GetProcMeshSection(0))return Fail(TEXT("OpeningComponentsRequirePlan"));
 Modify();CutOperations=Prepared.Sources;
 if(FailAt(3))return Fail(TEXT("InjectedOpeningSourceFailure"));
 const FEHBWallJunctionMesh* Meshes[]={&Prepared.Left,&Prepared.Right,&Prepared.Caps};
 for(int32 I=0;I<3;++I)
 {
  auto* Component=Components[I];const auto& Mesh=*Meshes[I];
  const auto* Old=Component->GetProcMeshSection(0);const FName Name=Old->SectionName;
  const bool Visible=Old->bSectionVisible,Collision=Old->bEnableCollision;
  Component->Modify();
  {
   FEHBScopedGeneratedMeshUpdate Update(Component);
   TArray<FLinearColor> Colors;Colors.Init(FLinearColor::White,Mesh.Vertices.Num());TArray<FEHBMeshTangent> Tangents;
   // Geometry-only publication: never empty the author's material slots.
   Component->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Triangles,Mesh.Normals,Mesh.UVs,Colors,Tangents,Collision);
   Component->SetMeshSectionName(0,Name);Component->SetMeshSectionVisible(0,Visible);
  }
  const auto* Actual=Component->GetProcMeshSection(0);
  if(!Actual||Actual->ProcVertexBuffer.Num()!=Mesh.Vertices.Num()||Actual->ProcIndexBuffer.Num()!=Mesh.Triangles.Num())return Fail(TEXT("OpeningPublicationMismatch"));
  for(int32 V=0;V<Mesh.Vertices.Num();++V)if(!Actual->ProcVertexBuffer[V].Position.Equals(Mesh.Vertices[V],0.00001)||!Actual->ProcVertexBuffer[V].Normal.Equals(Mesh.Normals[V],0.00001)||!Actual->ProcVertexBuffer[V].UV0.Equals(Mesh.UVs[V],0.00001))return Fail(TEXT("OpeningPublicationMismatch"));
  for(int32 T=0;T<Mesh.Triangles.Num();++T)if(Actual->ProcIndexBuffer[T]!=static_cast<uint32>(Mesh.Triangles[T]))return Fail(TEXT("OpeningPublicationMismatch"));
  if(I==0){CachedLeftWallVertices=Mesh.Vertices;CachedLeftWallTriangles=Mesh.Triangles;}
  if(I==1){CachedRightWallVertices=Mesh.Vertices;CachedRightWallTriangles=Mesh.Triangles;}
  if(FailAt(4+I))return Fail(TEXT("InjectedOpeningMeshFailure"));
 }
 Status=TEXT("Ready");return true;
}
