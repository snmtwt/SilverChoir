#include "Core/EHBLogicalSurfaceAdjacency.h"
#include "Geometry/EHBFloorContactGeometry.h"

namespace
{
 constexpr double Epsilon=0.001;
 struct FEdge{FEHBLogicalSurfaceEdgeRef Ref;FVector A,B,Inward;};
 TArray<FEdge> Edges(const FEHBLogicalSurfaceDefinition& S)
 {
  TArray<FEdge> Result;
  auto Loop=[&](const TArray<FVector2D>& P,int32 Region,int32 Hole)
  {
   double Area=0;for(int32 I=0;I<P.Num();++I){const auto A=P[I]-P[0],B=P[(I+1)%P.Num()]-P[0];Area+=A.X*B.Y-A.Y*B.X;}
   const double Sign=(Area>0?1.0:-1.0)*(Hole==INDEX_NONE?1.0:-1.0);
   for(int32 I=0;I<P.Num();++I){auto& E=Result.AddDefaulted_GetRef();E.Ref.ElementGuid=S.ElementGuid;E.Ref.SurfaceGuid=S.SurfaceGuid;E.Ref.Region=Region;E.Ref.Hole=Hole;E.Ref.Edge=I;E.A=S.ToBuilding(P[I]);E.B=S.ToBuilding(P[(I+1)%P.Num()]);E.Inward=FVector::CrossProduct(S.PlaneToBuilding.GetUnitAxis(EAxis::Z),(E.B-E.A).GetSafeNormal())*Sign;}
  };
  for(int32 R=0;R<S.Regions.Num();++R){Loop(S.Regions[R].Boundary,R,INDEX_NONE);for(int32 H=0;H<S.Regions[R].Holes.Num();++H)Loop(S.Regions[R].Holes[H].Vertices,R,H);}return Result;
 }
 TArray<FEHBFloorFinishRegion> Project(const FEHBLogicalSurfaceDefinition& S,const FTransform& Frame)
 {
  TArray<FEHBFloorFinishRegion> Result;
  auto Loop=[&](const TArray<FVector2D>& P,TArray<FVector>& Q){for(const auto& V:P){auto W=Frame.InverseTransformPosition(S.ToBuilding(V));W.Z=0;Q.Add(W);}};
  for(const auto& R:S.Regions){auto& V=Result.AddDefaulted_GetRef();Loop(R.Boundary,V.OuterPolygon);for(const auto& H:R.Holes)Loop(H.Vertices,V.Holes.AddDefaulted_GetRef().LocalPolygon);}return Result;
 }
}

bool FEHBLogicalSurfaceAdjacency::Build(const TArray<FEHBLogicalSurfaceDefinition>& Input,TArray<FEHBLogicalSurfaceSharedEdge>& Out,FName& Status)
{
 Out.Reset();TArray<const FEHBLogicalSurfaceDefinition*> Sorted;
 for(const auto& S:Input){if(!S.Validate(Status))return false;Sorted.Add(&S);}
 Sorted.Sort([](const auto& A,const auto& B){if(A.BuildingGuid!=B.BuildingGuid)return A.BuildingGuid<B.BuildingGuid;if(A.ElementGuid!=B.ElementGuid)return A.ElementGuid<B.ElementGuid;return A.SurfaceGuid<B.SurfaceGuid;});
 for(int32 I=1;I<Sorted.Num();++I)if(Sorted[I]->BuildingGuid==Sorted[I-1]->BuildingGuid&&Sorted[I]->ElementGuid==Sorted[I-1]->ElementGuid&&Sorted[I]->SurfaceGuid==Sorted[I-1]->SurfaceGuid){Status=TEXT("DuplicateAdjacencySurface");return false;}
 TArray<FEHBLogicalSurfaceSharedEdge> Result;
 for(int32 I=0;I<Sorted.Num();++I)for(int32 J=I+1;J<Sorted.Num();++J)
 {
  const auto& A=*Sorted[I];const auto& B=*Sorted[J];if(A.BuildingGuid!=B.BuildingGuid||A.FloorIndex!=B.FloorIndex)continue;
  const auto Normal=A.PlaneToBuilding.GetUnitAxis(EAxis::Z);
  if(FVector::DotProduct(Normal,B.PlaneToBuilding.GetUnitAxis(EAxis::Z))<1.0-1.e-10||FMath::Abs(FVector::DotProduct(Normal,B.PlaneToBuilding.GetLocation()-A.PlaneToBuilding.GetLocation()))>Epsilon)continue;
  const auto PA=Project(A,A.PlaneToBuilding),PB=Project(B,A.PlaneToBuilding);TArray<FEHBFloorSupportSurface> Tops;for(const auto& R:PB){auto& T=Tops.AddDefaulted_GetRef();T.OuterPolygon=R.OuterPolygon;T.Holes=R.Holes;}double Overlap=0;
  if(!FEHBFloorContactGeometry::MeasureArea(PA,Tops,Overlap,Status))return false;if(Overlap>0.01){Status=TEXT("OverlappingAdjacencySurfaces");return false;}
  const auto EA=Edges(A),EB=Edges(B);
  for(const auto& X:EA)for(const auto& Y:EB)
  {
   const auto D=X.B-X.A;const double Length=D.Size();const auto Unit=D/Length;
   if(FVector::DotProduct(X.Inward,Y.Inward)>-1.0+1.e-10)continue;
   if(FVector::CrossProduct(Y.A-X.A,Unit).Size()>Epsilon||FVector::CrossProduct(Y.B-X.A,Unit).Size()>Epsilon)continue;
   const double U=FVector::DotProduct(Y.A-X.A,Unit),V=FVector::DotProduct(Y.B-X.A,Unit);
   const double Start=FMath::Max(0.0,FMath::Min(U,V)),End=FMath::Min(Length,FMath::Max(U,V));if(End-Start<=Epsilon)continue;
   auto& Link=Result.AddDefaulted_GetRef();Link.A=X.Ref;Link.B=Y.Ref;Link.Start=X.A+Unit*Start;Link.End=X.A+Unit*End;Link.A.StartAlpha=Start/Length;Link.A.EndAlpha=End/Length;
   const auto DY=Y.B-Y.A;Link.B.StartAlpha=FMath::Clamp(FVector::DotProduct(Link.Start-Y.A,DY)/DY.SizeSquared(),0.0,1.0);Link.B.EndAlpha=FMath::Clamp(FVector::DotProduct(Link.End-Y.A,DY)/DY.SizeSquared(),0.0,1.0);
  }
 }
 Out=MoveTemp(Result);Status=TEXT("Ready");return true;
}
