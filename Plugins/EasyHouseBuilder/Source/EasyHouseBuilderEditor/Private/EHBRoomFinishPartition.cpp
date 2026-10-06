#include "EHBRoomFinishPartition.h"
#include "Cutting/EHBPolygonClipper.h"
#include "Geometry/EHBFloorContactGeometry.h"

bool EHBRoomFinishPartition::Build(const TArray<FVector>& Destination,const TArray<FSource>& Sources,TMap<FGuid,TArray<FVector>>& Out,FName& Status)
{
 Out.Reset();auto Fail=[&](FName Why){Out.Reset();Status=Why;return false;};
 auto Valid=[](const TArray<FVector>& P){return P.Num()>=3&&!P.ContainsByPredicate([&](const auto& V){return !FMath::IsFinite(V.X)||!FMath::IsFinite(V.Y)||!FMath::IsFinite(V.Z)||FMath::Abs(V.X)>1.e8||FMath::Abs(V.Y)>1.e8||FMath::Abs(V.Z-P[0].Z)>0.001;});};
 if(!Valid(Destination)||Sources.Num()<2)return Fail(TEXT("InvalidFinishPartition"));
 auto Region=[](const auto& P){FEHBFloorFinishRegion R;R.OuterPolygon=P;return R;};
 auto Top=[](const auto& P){FEHBFloorSupportSurface T;T.OuterPolygon=P;return T;};
 TMap<FGuid,TArray<FVector>> Planned;TArray<FEHBFloorFinishRegion> Parts;
 for(const auto& Source:Sources)
 {
  if(!Source.ElementGuid.IsValid()||Planned.Contains(Source.ElementGuid)||!Valid(Source.OwnerRoomPolygon)||!Valid(Source.OriginalPolygon))return Fail(TEXT("InvalidFinishPartitionSource"));
  if(FMath::Abs(Source.OriginalPolygon[0].Z-Destination[0].Z)>0.001)return Fail(TEXT("FinishPartitionHeightMismatch"));
  FEHBPolygonClipResult Clip;
  if(!FEHBPolygonClipper::IntersectionXY(Destination,Source.OwnerRoomPolygon,Clip)||Clip.Regions.Num()!=1||!Clip.Regions[0].HoleLoops.IsEmpty())return Fail(TEXT("FinishPartitionRequiresMultipleRegions"));
  auto Polygon=Clip.Regions[0].OuterLoop;for(auto& V:Polygon)V.Z=Destination[0].Z;if(!Valid(Polygon))return Fail(TEXT("InvalidFinishPartitionResult"));
  double OriginalArea=0,KeptArea=0;
  if(!FEHBFloorContactGeometry::MeasureArea({Region(Source.OriginalPolygon)},{Top(Source.OriginalPolygon)},OriginalArea,Status)
   ||!FEHBFloorContactGeometry::MeasureArea({Region(Source.OriginalPolygon)},{Top(Polygon)},KeptArea,Status))return Fail(Status);
  if(OriginalArea<=0.01||FMath::Abs(OriginalArea-KeptArea)>0.01)return Fail(TEXT("FinishPartitionWouldDiscardCoverage"));
  for(const auto& Previous:Parts){double Overlap=0;if(!FEHBFloorContactGeometry::MeasureArea({Previous},{Top(Polygon)},Overlap,Status))return Fail(Status);if(Overlap>0.01)return Fail(TEXT("FinishPartitionOverlap"));}
  Parts.Add(Region(Polygon));Planned.Add(Source.ElementGuid,MoveTemp(Polygon));
 }
 TArray<FEHBFloorSupportSurface> Tops;for(const auto& Part:Parts)Tops.Add(Top(Part.OuterPolygon));double DomainArea=0,UnionArea=0,Intersection=0;
 if(!FEHBFloorContactGeometry::MeasureArea({Region(Destination)},{Top(Destination)},DomainArea,Status)
  ||!FEHBFloorContactGeometry::MeasureArea(Parts,Tops,UnionArea,Status)
  ||!FEHBFloorContactGeometry::MeasureArea(Parts,{Top(Destination)},Intersection,Status))return Fail(Status);
 if(DomainArea<=0.01||FMath::Abs(DomainArea-Intersection)>0.01||FMath::Abs(UnionArea-Intersection)>0.01)return Fail(TEXT("IncompleteFinishPartitionCoverage"));
 Out=MoveTemp(Planned);Status=TEXT("Ready");return true;
}
