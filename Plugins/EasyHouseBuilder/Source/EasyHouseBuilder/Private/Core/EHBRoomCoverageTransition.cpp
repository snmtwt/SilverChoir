#include "Core/EHBRoomCoverageTransition.h"
#include "Geometry/EHBFloorContactGeometry.h"

namespace
{
 constexpr double CoverageTolerance=0.01; // cm2, same as existing room partition checks.
 FEHBFloorSupportSurface Surface(const FEHBNodeRoomBoundary& Room)
 {
  FEHBFloorSupportSurface Result;Result.OuterPolygon=Room.Polygon;
  for(auto& P:Result.OuterPolygon)P.Z=0;
  return Result;
 }
 bool Intersection(const TArray<const FEHBNodeRoomBoundary*>& Before,const TArray<const FEHBNodeRoomBoundary*>& After,double& Area,FName& Status)
 {
  TArray<FEHBFloorFinishRegion> Coverage;TArray<FEHBFloorSupportSurface> Tops;
  for(const auto* R:Before)Coverage.AddDefaulted_GetRef().OuterPolygon=Surface(*R).OuterPolygon;
  for(const auto* R:After)Tops.Add(Surface(*R));
  FEHBFloorContact Contact;if(!FEHBFloorContactGeometry::Build(Coverage,Tops,Contact,Status))return false;
  Area=Contact.Area;return true;
 }
 // Retain the subdivision tie-break contract, independent of transient node IDs.
 FString GeometryKey(const FEHBNodeRoomBoundary& R)
 {
  auto V=R.Polygon;V.Sort([](const FVector& A,const FVector& B){return A.X!=B.X?A.X<B.X:A.Y<B.Y;});
  FString Key;for(const auto& P:V)Key+=FString::Printf(TEXT("%.17g,%.17g;"),P.X,P.Y);
  return Key;
 }
 bool SamePlane(const FEHBNodeRoomBoundary& A,const FEHBNodeRoomBoundary& B)
 {
  return A.FloorIndex==B.FloorIndex&&FMath::IsNearlyEqual(A.Polygon[0].Z,B.Polygon[0].Z,0.001);
 }
}

FEHBRoomCoverageTransition FEHBRoomCoverageTransition::Build(FGuid BuildingGuid,const FEHBWallNodeModel& Before,
 const FEHBWallNodeModel& After,FName& Status)
{
 TArray<FEHBNodeRoomBoundary> OldRooms,NewRooms;
 if(!FEHBWallNodeRooms::Build(BuildingGuid,Before,OldRooms,Status)||!FEHBWallNodeRooms::Build(BuildingGuid,After,NewRooms,Status))return {};
 return BuildFromValidatedRooms(OldRooms,NewRooms,Status);
}

FEHBRoomCoverageTransition FEHBRoomCoverageTransition::BuildFromValidatedRooms(const TArray<FEHBNodeRoomBoundary>& Before,
 const TArray<FEHBNodeRoomBoundary>& After,FName& Status)
{
 FEHBRoomCoverageTransition Result;auto Fail=[&](FName Why){Status=Why;return FEHBRoomCoverageTransition();};
 TArray<TArray<int32>> OldToNew,NewToOld;OldToNew.SetNum(Before.Num());NewToOld.SetNum(After.Num());
 TArray<double> OldAreas,NewAreas;OldAreas.SetNum(Before.Num());NewAreas.SetNum(After.Num());
 for(int32 I=0;I<Before.Num();++I)if(!Intersection({&Before[I]},{&Before[I]},OldAreas[I],Status))return Fail(Status);
 for(int32 J=0;J<After.Num();++J)if(!Intersection({&After[J]},{&After[J]},NewAreas[J],Status))return Fail(Status);
 for(int32 I=0;I<Before.Num();++I)for(int32 J=0;J<After.Num();++J)
 {
  if(!SamePlane(Before[I],After[J]))continue;
  double Area=0;if(!Intersection({&Before[I]},{&After[J]},Area,Status))return Fail(Status);
  if(Area>CoverageTolerance){OldToNew[I].Add(J);NewToOld[J].Add(I);}
 }
 TSet<int32> SeenOld,SeenNew;
 auto SortRooms=[](TArray<const FEHBNodeRoomBoundary*>& Rooms)
 {
  Rooms.Sort([](const auto& A,const auto& B){return A.Area!=B.Area?A.Area>B.Area:GeometryKey(A)<GeometryKey(B);});
 };
 auto Append=[&](EEHBRoomCoverageChange Kind,TArray<const FEHBNodeRoomBoundary*> Old,TArray<const FEHBNodeRoomBoundary*> New)
 {
  SortRooms(Old);SortRooms(New);auto& C=Result.Changes.AddDefaulted_GetRef();C.Kind=Kind;
  C.FloorIndex=Old.IsEmpty()?New[0]->FloorIndex:Old[0]->FloorIndex;
  for(const auto* R:Old)C.BeforeRooms.Add(R->RoomGuid);for(const auto* R:New)C.AfterRooms.Add(R->RoomGuid);
 };
 for(int32 Root=0;Root<Before.Num();++Root)
 {
  if(SeenOld.Contains(Root))continue;
  TArray<int32> Old={Root},New;SeenOld.Add(Root);
  for(int32 At=0;At<Old.Num();++At)for(int32 J:OldToNew[Old[At]])if(!SeenNew.Contains(J))
  {
   SeenNew.Add(J);New.Add(J);
   for(int32 I:NewToOld[J])if(!SeenOld.Contains(I)){SeenOld.Add(I);Old.Add(I);}
  }
  TArray<const FEHBNodeRoomBoundary*> OldRooms,NewRooms;double OldArea=0,NewArea=0;
  for(int32 I:Old){OldRooms.Add(&Before[I]);OldArea+=OldAreas[I];}
  for(int32 J:New){NewRooms.Add(&After[J]);NewArea+=NewAreas[J];}
  if(New.IsEmpty())
  {
   if(After.ContainsByPredicate([&](const auto& R){return R.RoomGuid==Before[Root].RoomGuid;}))return Fail(TEXT("RoomIdentityLeftCoverage"));
   Append(EEHBRoomCoverageChange::Removed,OldRooms,{});continue;
  }
  if(Old.Num()>1&&New.Num()>1)return Fail(TEXT("RoomRepartitionRequiresPlan"));
  double Area=0;if(!Intersection(OldRooms,NewRooms,Area,Status))return Fail(Status);
  if(FMath::Abs(OldArea-Area)>CoverageTolerance||FMath::Abs(NewArea-Area)>CoverageTolerance)return Fail(TEXT("IncompleteRoomTransitionCoverage"));
  Append(Old.Num()>1?EEHBRoomCoverageChange::Merged:New.Num()>1?EEHBRoomCoverageChange::Split:EEHBRoomCoverageChange::Retained,OldRooms,NewRooms);
 }
 for(int32 J=0;J<After.Num();++J)if(!SeenNew.Contains(J))
 {
  if(Before.ContainsByPredicate([&](const auto& R){return R.RoomGuid==After[J].RoomGuid;}))return Fail(TEXT("RoomIdentityLeftCoverage"));
  Append(EEHBRoomCoverageChange::Added,{}, {&After[J]});
 }
 // Input ordering must not choose a different correspondence or inheritance source.
 auto Key=[&](const auto& C)
 {
  const bool Old=!C.BeforeRooms.IsEmpty();const auto& Rooms=Old?Before:After;const FGuid Id=Old?C.BeforeRooms[0]:C.AfterRooms[0];
  return GeometryKey(*Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Id;}));
 };
 Result.Changes.Sort([&](const auto& A,const auto& B){return A.FloorIndex!=B.FloorIndex?A.FloorIndex<B.FloorIndex:Key(A)!=Key(B)?Key(A)<Key(B):static_cast<uint8>(A.Kind)<static_cast<uint8>(B.Kind);});
 Result.bReady=true;Status=TEXT("Ready");return Result;
}
