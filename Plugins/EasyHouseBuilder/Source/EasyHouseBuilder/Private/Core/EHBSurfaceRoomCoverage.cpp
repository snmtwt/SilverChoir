#include "Core/EHBSurfaceRoomCoverage.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBWallNodeRooms.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Geometry/EHBFloorContactGeometry.h"

namespace
{
 bool Flatten(TArray<FVector>& Points)
 {
  for(auto& P:Points){if(P.ContainsNaN()||P.GetAbsMax()>1.e8)return false;P.Z=0;}return true;
 }
 TArray<FEHBFloorSupportSurface> Tops(const TArray<FEHBFloorFinishRegion>& Regions)
 {
  TArray<FEHBFloorSupportSurface> Result;for(const auto& R:Regions){auto& T=Result.AddDefaulted_GetRef();T.OuterPolygon=R.OuterPolygon;T.Holes=R.Holes;}return Result;
 }
}

bool FEHBSurfaceRoomCoverageSolver::Build(FGuid ElementGuid,int32 FloorIndex,
 const TArray<FEHBFloorFinishRegion>& Regions,const TArray<FEHBNodeRoomBoundary>& Rooms,
 FEHBSurfaceRoomCoverage& Out,FName& Status)
{
 Out={};auto Fail=[&](FName Why){Status=Why;return false;};
 if(!ElementGuid.IsValid()||FloorIndex<1||Regions.IsEmpty())return Fail(TEXT("InvalidSurfaceCoverageInput"));
 auto Projected=Regions;for(auto& R:Projected){if(!Flatten(R.OuterPolygon))return Fail(TEXT("InvalidSurfaceFootprint"));for(auto& H:R.Holes)if(!Flatten(H.LocalPolygon))return Fail(TEXT("InvalidSurfaceFootprint"));}
 FEHBSurfaceRoomCoverage Result;Result.ElementGuid=ElementGuid;Result.FloorIndex=FloorIndex;
 if(!FEHBFloorContactGeometry::MeasureArea(Projected,Tops(Projected),Result.AreaCm2,Status))return false;
 if(Result.AreaCm2<=0)return Fail(TEXT("EmptySurfaceFootprint"));
 struct FRoom {FGuid Id;FEHBFloorFinishRegion Region;double Area=0;};TArray<FRoom> Validated;TSet<FGuid> Ids;
 for(const auto& R:Rooms)
 {
  if(!R.RoomGuid.IsValid()||R.FloorIndex<1||Ids.Contains(R.RoomGuid))return Fail(TEXT("InvalidCoverageRoomIdentity"));Ids.Add(R.RoomGuid);
  if(R.FloorIndex!=FloorIndex)continue;
  FRoom Value;Value.Id=R.RoomGuid;Value.Region.OuterPolygon=R.Polygon;
  if(!Flatten(Value.Region.OuterPolygon))return Fail(TEXT("InvalidCoverageRoomPolygon"));
  if(!FEHBFloorContactGeometry::MeasureArea({Value.Region},Tops({Value.Region}),Value.Area,Status)||Value.Area<=0)return Fail(TEXT("InvalidCoverageRoomPolygon"));
  Validated.Add(MoveTemp(Value));
 }
 Validated.Sort([](const auto& A,const auto& Z){return A.Id<Z.Id;});
 TArray<FEHBFloorFinishRegion> RoomRegions;
 for(int32 I=0;I<Validated.Num();++I)
 {
  const auto& R=Validated[I];
  // Room footprints on one floor must not overlap. Do not double-count a bad
  // room query and then conceal the problem by clamping fractions.
  for(int32 J=0;J<I;++J){double Area=0;if(!FEHBFloorContactGeometry::MeasureArea({R.Region},Tops({Validated[J].Region}),Area,Status))return false;if(Area>0.000001)return Fail(TEXT("OverlappingCoverageRooms"));}
  RoomRegions.Add(R.Region);double Area=0;
  if(!FEHBFloorContactGeometry::MeasureArea(Projected,Tops({R.Region}),Area,Status))return false;
  if(Area>0){auto& Share=Result.Rooms.AddDefaulted_GetRef();Share.RoomGuid=R.Id;Share.AreaCm2=Area;Share.SurfaceFraction=Area/Result.AreaCm2;Share.RoomFraction=Area/R.Area;}
 }
 double Inside=0;if(!FEHBFloorContactGeometry::MeasureArea(Projected,Tops(RoomRegions),Inside,Status))return false;
 Result.OutsideAreaCm2=FMath::Max(0.0,Result.AreaCm2-Inside);
 Out=MoveTemp(Result);Status=TEXT("Ready");return true;
}

bool AEHBBuildingActorBase::QuerySurfaceRoomCoverage(FGuid ElementGuid,FEHBSurfaceRoomCoverage& Coverage,FName& Status) const
{
 Coverage={};auto Fail=[&](FName Why){Status=Why;return false;};
 // Reads are valid inside the final committed callback; only unpublished edits
 // are blocked. Publishing a finished event is not itself a partial model.
 if(ActiveChangeNotificationBatch)return Fail(TEXT("SurfaceCoverageEditInFlight"));
 const auto* E=FindElementActorByGuid(ElementGuid);
 if(!IsValid(E)||E->IsActorBeingDestroyed()||E->OwningBuilding!=this||E->FloorIndex<1)return Fail(TEXT("UnknownCoverageElement"));
 TArray<FEHBFloorFinishRegion> Regions;
 if(const auto* F=Cast<AEHB_Floor>(E))
 {
  if(!F->CutOperations.IsEmpty())return Fail(TEXT("SurfaceCoverageRequiresOpeningOutline"));
  Regions=F->FloorRegions;if(Regions.IsEmpty())Regions.AddDefaulted_GetRef().OuterPolygon=F->LocalFloorPolygon;
  if(!F->ValidateFloorRegions(Regions))return Fail(TEXT("InvalidSurfaceFootprint"));
 }
 else if(const auto* S=Cast<AEHB_FloorSlab>(E))
 {
  if(!S->CutOperations.IsEmpty()||!S->PreviewCutters.IsEmpty())return Fail(TEXT("SurfaceCoverageRequiresOpeningOutline"));
  if(!S->ValidateSlabOutline(S->LocalTopPolygon,S->LocalHoles))return Fail(TEXT("InvalidSurfaceFootprint"));
  auto& R=Regions.AddDefaulted_GetRef();R.OuterPolygon=S->LocalTopPolygon;
  for(const auto& H:S->LocalHoles)R.Holes.AddDefaulted_GetRef().LocalPolygon=H.LocalPolygon;
 }
 else return Fail(TEXT("UnsupportedCoverageElement"));
 const auto Pose=E->GetElementLocalTransform();
 if(Pose.ContainsNaN()||!Pose.GetRotation().RotateVector(FVector::UpVector).Equals(FVector::UpVector,0.0001)||Pose.GetScale3D().GetMin()<=0)return Fail(TEXT("NonHorizontalCoverageSurface"));
 for(auto& R:Regions){for(auto& P:R.OuterPolygon)P=Pose.TransformPosition(P);for(auto& H:R.Holes)for(auto& P:H.LocalPolygon)P=Pose.TransformPosition(P);}
 FEHBWallNodeModel Model;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(this,Model);
 if(!Capture.bSucceeded)return Fail(Capture.Status);
 TArray<FEHBNodeRoomBoundary> Rooms;if(!FEHBWallNodeRooms::Build(BuildingGuid,Model,Rooms,Status))return false;
 return FEHBSurfaceRoomCoverageSolver::Build(ElementGuid,E->FloorIndex,Regions,Rooms,Coverage,Status);
}
