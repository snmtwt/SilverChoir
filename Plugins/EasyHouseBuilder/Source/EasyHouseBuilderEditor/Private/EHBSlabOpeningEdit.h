#pragma once
#include "EHBFinishRegionCommand.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Core/EHBBuildingActorBase.h"

namespace EHBSlabOpeningEdit
{
 inline bool Supports(const AEHB_FloorSlab* S)
 {
  return IsValid(S)&&S->OwningBuilding&&S->OwningBuilding->WallNodeAuthority.Version==2&&(S->OutlineSource==EEHBOutlineSource::RoomBoundary||S->OutlineSource==EEHBOutlineSource::RetainedRegion);
 }
 // Normalize only the candidate's XY representation; preserve the depth span
 // and existing operation/point identities. Used by drag and vertex commands.
 inline bool NormalizeCut(FEHBCutOperation& Cut,const TArray<FVector>& Resolved)
 {
   if(Cut.Source.SourceElement||Cut.Source.SourceElementGuid.IsValid()||(Cut.TransformPolicy!=EEHBCutTransformPolicy::TargetLocal&&!(Cut.SurfaceHost.Version==1&&Cut.TransformPolicy==EEHBCutTransformPolicy::FollowTargetElement)))return false;
   // Resolve primitive/local transforms once in the draft. Preserve point IDs
   // for an already editable polygon and keep the operation identity stable.
   const auto& Transform=Cut.Source.LocalTransform;
   const bool XYIdentity=Transform.GetRotation().IsIdentity(KINDA_SMALL_NUMBER)&&FMath::IsNearlyZero(Transform.GetLocation().X)&&FMath::IsNearlyZero(Transform.GetLocation().Y)&&FMath::IsNearlyEqual(Transform.GetScale3D().X,1.0)&&FMath::IsNearlyEqual(Transform.GetScale3D().Y,1.0);
   if(Cut.Source.SourceType!=EEHBCutSourceType::ExplicitPolygon||Cut.Source.PrimitiveShape!=EEHBCutPrimitiveShape::Polygon||!XYIdentity)
   {
    Cut.Source.SourceType=EEHBCutSourceType::ExplicitPolygon;Cut.Source.PrimitiveShape=EEHBCutPrimitiveShape::Polygon;const double CenterZ=Transform.GetLocation().Z,ScaleZ=Transform.GetScale3D().Z;Cut.Source.LocalTransform=FTransform(FQuat::Identity,FVector(0,0,CenterZ),FVector(1,1,ScaleZ));
    if(Cut.Source.ExplicitPolygon.Points.Num()!=Resolved.Num()){Cut.Source.ExplicitPolygon.Points.Reset();Cut.Source.ExplicitPolygon.Points.SetNum(Resolved.Num());}
    for(int32 I=0;I<Resolved.Num();++I)Cut.Source.ExplicitPolygon.Points[I].LocalPosition=FVector(Resolved[I].X,Resolved[I].Y,0);Cut.EnsureGuids();
   }
  return true;
 }
 inline FEHBToolsetOperationResult Execute(AEHB_FloorSlab* S,int32 Loop,int32 First,int32 Second,bool Insert,bool Preview=false)
 {
  auto Fail=[](const TCHAR* Why){FEHBToolsetOperationResult R;R.Message=Why;return R;};
  if(!Supports(S)||Loop==INDEX_NONE)return Fail(TEXT("RequiresTopologyOpening"));
  TArray<FVector> Polygon;if(!S->GetEditableLoopCopy(Loop,Polygon)||!Polygon.IsValidIndex(First)||(Insert&&(!Polygon.IsValidIndex(Second)||Second!=(First+1)%Polygon.Num())))return Fail(TEXT("InvalidOpeningHandle"));
  auto Holes=S->LocalHoles;auto Cuts=S->CutOperations;
  if(Holes.IsValidIndex(Loop))
  {
   if(Insert)Holes[Loop].LocalPolygon.Insert((Polygon[First]+Polygon[Second])*0.5,Second);
   else if(Polygon.Num()<=3)Holes.RemoveAt(Loop);
   else Holes[Loop].LocalPolygon.RemoveAt(First);
  }
  else
  {
   const int32 Index=AEHB_FloorSlab::GetCutOperationIndexFromLoopIndex(Loop);if(!Cuts.IsValidIndex(Index))return Fail(TEXT("InvalidOpeningHandle"));
   if(!Insert&&Polygon.Num()<=3)Cuts.RemoveAt(Index);
   else
   {
    if(!NormalizeCut(Cuts[Index],Polygon))return Fail(TEXT("LinkedOpeningRequiresPlan"));
    auto& Points=Cuts[Index].Source.ExplicitPolygon.Points;
    if(Insert){FEHBCutPolygonPoint Point;const auto Mid=(Polygon[First]+Polygon[Second])*0.5;Point.LocalPosition=FVector(Mid.X,Mid.Y,0);Point.EnsureGuid();Points.Insert(Point,Second);}
    else Points.RemoveAt(First);
   }
  }
  auto* B=S->OwningBuilding.Get();return UEHBBuildingToolset::SetFloorSlabOpenings(S,Holes,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Preview);
 }
}
