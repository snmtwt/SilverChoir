#include "Core/EHBLogicalSurface.h"
#include "Geometry/EHBFloorContactGeometry.h"

namespace
{
 constexpr double Tolerance=0.001;
 double Cross(const FVector2D& A,const FVector2D& B){return A.X*B.Y-A.Y*B.X;}
 // Subtract an origin before cross products: a 1 cm square near 1e8 cm
 // otherwise loses its entire area through cancellation of ~1e16 products.
 double Area(const TArray<FVector2D>& P){if(P.IsEmpty())return 0;double A=0;for(int32 I=0;I<P.Num();++I)A+=Cross(P[I]-P[0],P[(I+1)%P.Num()]-P[0]);return FMath::Abs(A)*0.5;}
 bool OnEdge(const FVector2D& P,const FVector2D& A,const FVector2D& B)
 {
  const auto D=B-A;const double L=D.SizeSquared();if(L<=Tolerance*Tolerance)return (P-A).SizeSquared()<=Tolerance*Tolerance;
  const double T=FMath::Clamp(FVector2D::DotProduct(P-A,D)/L,0.0,1.0);return (P-A-D*T).SizeSquared()<=Tolerance*Tolerance;
 }
 bool Touch(const FVector2D& A,const FVector2D& B,const FVector2D& C,const FVector2D& D)
 {
  if(OnEdge(A,C,D)||OnEdge(B,C,D)||OnEdge(C,A,B)||OnEdge(D,A,B))return true;
  const double AC=Cross(B-A,C-A),AD=Cross(B-A,D-A),CA=Cross(D-C,A-C),CB=Cross(D-C,B-C);
  return ((AC<0)!=(AD<0))&&((CA<0)!=(CB<0));
 }
 int32 Inside(const FVector2D& P,const TArray<FVector2D>& Loop)
 {
  bool In=false;for(int32 I=0,J=Loop.Num()-1;I<Loop.Num();J=I++)
  {const auto A=Loop[J],B=Loop[I];if(OnEdge(P,A,B))return 0;if((A.Y>P.Y)!=(B.Y>P.Y)&&P.X<(B.X-A.X)*(P.Y-A.Y)/(B.Y-A.Y)+A.X)In=!In;}return In?1:-1;
 }
 bool LoopsTouch(const TArray<FVector2D>& A,const TArray<FVector2D>& B)
 {for(int32 I=0;I<A.Num();++I)for(int32 J=0;J<B.Num();++J)if(Touch(A[I],A[(I+1)%A.Num()],B[J],B[(J+1)%B.Num()]))return true;return false;}
 bool Simple(const TArray<FVector2D>& P)
 {
  if(P.Num()<3)return false;for(const auto& V:P)if(V.ContainsNaN()||FMath::Max(FMath::Abs(V.X),FMath::Abs(V.Y))>1.e8)return false;
  if(Area(P)<=0.01)return false;
  for(int32 I=0;I<P.Num();++I)
  {
   const auto A=P[I],B=P[(I+1)%P.Num()],C=P[(I+2)%P.Num()];if((B-A).SizeSquared()<=Tolerance*Tolerance)return false;
   // Adjacent collinear backtracking is also a self overlap; normal added points are valid.
   if(FVector2D::DotProduct(B-A,C-B)<0&&FMath::Abs(Cross(B-A,C-B))<=1.e-8*(B-A).Size()*(C-B).Size())return false;
   for(int32 J=I+1;J<P.Num();++J)if(J!=I+1&&!(I==0&&J==P.Num()-1)&&Touch(A,B,P[J],P[(J+1)%P.Num()]))return false;
  }
  return true;
 }
 FEHBFloorFinishRegion ContactRegion(const FEHBLogicalSurfaceRegion& Region)
 {
  FEHBFloorFinishRegion R;for(const auto& P:Region.Boundary)R.OuterPolygon.Add(FVector(P.X,P.Y,0));for(const auto& H:Region.Holes){auto& V=R.Holes.AddDefaulted_GetRef();for(const auto& P:H.Vertices)V.LocalPolygon.Add(FVector(P.X,P.Y,0));}return R;
 }
}

bool FEHBLogicalSurfaceDefinition::Validate(FName& Status) const
{
 auto Fail=[&](FName Why){Status=Why;return false;};
 if(!ElementGuid.IsValid()||!SurfaceGuid.IsValid()||SourceName.IsNone()||Regions.IsEmpty())return Fail(TEXT("InvalidLogicalSurfaceIdentity"));
 if(PlaneToBuilding.ContainsNaN()||!PlaneToBuilding.GetRotation().IsNormalized()||!PlaneToBuilding.GetScale3D().Equals(FVector::OneVector,1.e-9)||PlaneToBuilding.GetLocation().GetAbsMax()>1.e8)return Fail(TEXT("InvalidLogicalSurfaceFrame"));
 if(!FMath::IsFinite(Thickness)||Thickness<0||(bCanSupport&&(!bStructural||Thickness<=0)))return Fail(TEXT("InvalidLogicalSurfaceThickness"));
 TArray<FEHBFloorFinishRegion> Prior;
 for(const auto& R:Regions)
 {
  if(!Simple(R.Boundary))return Fail(TEXT("InvalidLogicalSurfaceBoundary"));
  for(int32 I=0;I<R.Holes.Num();++I)
  {
   const auto& H=R.Holes[I].Vertices;if(!Simple(H)||Inside(H[0],R.Boundary)!=1||LoopsTouch(H,R.Boundary))return Fail(TEXT("InvalidLogicalSurfaceHole"));
   for(int32 J=0;J<I;++J){const auto& Other=R.Holes[J].Vertices;if(LoopsTouch(H,Other)||Inside(H[0],Other)>=0||Inside(Other[0],H)>=0)return Fail(TEXT("OverlappingLogicalSurfaceHoles"));}
  }
  const auto Current=ContactRegion(R);FEHBFloorSupportSurface Top;Top.OuterPolygon=Current.OuterPolygon;Top.Holes=Current.Holes;
  if(!Prior.IsEmpty()){double Overlap=0;if(!FEHBFloorContactGeometry::MeasureArea(Prior,{Top},Overlap,Status))return false;if(Overlap>0.01)return Fail(TEXT("OverlappingLogicalSurfaceRegions"));}Prior.Add(Current);
 }
 Status=TEXT("Ready");return true;
}

double FEHBLogicalSurfaceDefinition::GetAreaCm2() const
{double A=0;for(const auto& R:Regions){A+=Area(R.Boundary);for(const auto& H:R.Holes)A-=Area(H.Vertices);}return A;}

bool FEHBLogicalSurfaceDefinition::Project(const FVector& Point,FVector2D& PlanePoint,double& SignedDistance) const
{
 PlanePoint=FVector2D::ZeroVector;SignedDistance=0;FName Status;if(Point.ContainsNaN()||!Validate(Status))return false;
 const auto P=PlaneToBuilding.InverseTransformPosition(Point);PlanePoint=FVector2D(P.X,P.Y);SignedDistance=P.Z;return true;
}

bool FEHBLogicalSurfaceDefinition::ContainsProjectedPoint(const FVector2D& Point) const
{
 if(Point.ContainsNaN())return false;
 for(const auto& R:Regions)if(Inside(Point,R.Boundary)>=0&&!R.Holes.ContainsByPredicate([&](const auto& H){return Inside(Point,H.Vertices)>=0;}))return true;return false;
}
