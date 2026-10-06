#include "Core/EHBSurfaceOpening.h"
#include "Geometry/EHBFloorContactGeometry.h"

bool FEHBSurfaceOpening::Resolve(const FEHBCutOperation& Operation,const FEHBLogicalSurfaceDefinition& Host,
 const FTransform& ElementToBuilding,TArray<FVector>& ElementPolygon,FName& Status)
{
 ElementPolygon.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 const auto& Binding=Operation.SurfaceHost;
 if(Binding.Version!=1)return Fail(TEXT("UnsupportedSurfaceOpeningVersion"));
 if(!Binding.BuildingGuid.IsValid()||Binding.BuildingGuid!=Host.BuildingGuid||Binding.ElementGuid!=Host.ElementGuid||Binding.SurfaceGuid!=Host.SurfaceGuid)return Fail(TEXT("SurfaceOpeningHostMismatch"));
 if(!Host.Validate(Status))return false;
 if(!Operation.OperationGuid.IsValid()||!Operation.bEnabled||Operation.OperationType!=EEHBCutOperationType::Subtract||Operation.Stage!=EEHBCutStage::SurfaceOpening||Operation.ProjectionMode!=EEHBCutProjectionMode::TargetPlane||Operation.TransformPolicy!=EEHBCutTransformPolicy::FollowTargetElement)return Fail(TEXT("UnsupportedSurfaceOpeningPolicy"));
 const auto& Source=Operation.Source;
 if(Source.SourceType!=EEHBCutSourceType::ExplicitPolygon||Source.PrimitiveShape!=EEHBCutPrimitiveShape::Polygon||Source.SourceElement||Source.SourceElementGuid.IsValid())return Fail(TEXT("UnsupportedSurfaceOpeningSource"));
 if(!FMath::IsFinite(Source.Height)||Source.Height<=0||Source.Height+0.001<Host.Thickness)return Fail(TEXT("SurfaceOpeningMustPenetrateHost"));
 if(ElementToBuilding.ContainsNaN()||ElementToBuilding.GetLocation().GetAbsMax()>1.e8||!ElementToBuilding.GetRotation().IsNormalized()||!ElementToBuilding.GetScale3D().Equals(FVector::OneVector,1.e-6)||Source.LocalTransform.ContainsNaN()||!Source.LocalTransform.GetRotation().IsNormalized())return Fail(TEXT("InvalidSurfaceOpeningTransform"));
 TSet<FGuid> PointIds;FEHBLogicalSurfaceDefinition Shape=Host;Shape.Regions.Reset();auto& Loop=Shape.Regions.AddDefaulted_GetRef().Boundary;
 for(const auto& Point:Source.ExplicitPolygon.Points)
 {
  if(!Point.PointGuid.IsValid()||PointIds.Contains(Point.PointGuid))return Fail(TEXT("InvalidSurfaceOpeningPointIdentity"));PointIds.Add(Point.PointGuid);
  if(Point.LocalPosition.ContainsNaN()||FMath::Abs(Point.LocalPosition.Z)>0.001)return Fail(TEXT("NonPlanarSurfaceOpening"));
  const auto P=Source.LocalTransform.TransformPosition(Point.LocalPosition);
  if(P.ContainsNaN()||FMath::Abs(P.Z)>0.001)return Fail(TEXT("NonPlanarSurfaceOpening"));Loop.Add(FVector2D(P.X,P.Y));
 }
 if(!Shape.Validate(Status))return false;
 // Whole-area coverage also detects crossing a host hole or bridging disjoint
 // regions, which checking the opening vertices alone cannot establish.
 FEHBFloorFinishRegion Opening;for(const auto& P:Loop)Opening.OuterPolygon.Add(FVector(P.X,P.Y,0));
 TArray<FEHBFloorSupportSurface> Supports;
 for(const auto& R:Host.Regions)
 {
  auto& S=Supports.AddDefaulted_GetRef();for(const auto& P:R.Boundary)S.OuterPolygon.Add(FVector(P.X,P.Y,0));
  for(const auto& H:R.Holes){auto& Hole=S.Holes.AddDefaulted_GetRef();for(const auto& P:H.Vertices)Hole.LocalPolygon.Add(FVector(P.X,P.Y,0));}
 }
 double Covered=0;if(!FEHBFloorContactGeometry::MeasureArea({Opening},Supports,Covered,Status))return false;
 FEHBFloorSupportSurface OwnDomain;OwnDomain.OuterPolygon=Opening.OuterPolygon;
 double Required=0;if(!FEHBFloorContactGeometry::MeasureArea({Opening},{OwnDomain},Required,Status))return false;
 // Compare in the same 0.001 cm clipping domain, not rounded overlap against
 // unquantized input area (which incorrectly rejects rotated valid polygons).
 if(FMath::Abs(Covered-Required)>0.01)return Fail(TEXT("SurfaceOpeningOutsideHostDomain"));
 TArray<FVector> Candidate;for(const auto& P:Loop)Candidate.Add(ElementToBuilding.InverseTransformPosition(Host.ToBuilding(P)));
 ElementPolygon=MoveTemp(Candidate);Status=TEXT("Ready");return true;
}
