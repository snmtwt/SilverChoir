#pragma once
#include "EHBFinishRegionCommand.h"
#include "EHBSlabOpeningEdit.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Core/EHBBuildingActorBase.h"
#include "Actors/EHB_FloorSlab.h"

/** The visible outline is a draft. No generated mesh or relation changes until release. */
struct FEHBFinishRegionDrag
{
 TWeakObjectPtr<AEHB_FloorSlab> Slab;
 FTransform WorldTransform;
 TArray<FVector> Original,Polygon;
 TArray<FEHBFloorSlabHole> Holes;
 TArray<FEHBCutOperation> Cuts,OriginalCuts;
 int32 LoopIndex=INDEX_NONE;
 FVector Delta=FVector::ZeroVector;
 int32 GraphRevision=0,GeometryRevision=0,First=INDEX_NONE,Second=INDEX_NONE;
 bool bCaptured=false,bTracking=false,bCancelled=false,bReady=false,bCorner=false;
 FEHBToolsetOperationResult Feedback;
 void Begin(AEHB_FloorSlab* S,int32 A,int32 B,bool Corner,int32 Loop=INDEX_NONE)
 {
  *this={};bTracking=true;Slab=S;First=A;Second=B;bCorner=Corner;LoopIndex=Loop;
  if(!S||!S->OwningBuilding)return;
  if(Loop==INDEX_NONE){if(!EHBFinishRegionCommand::IsIndependent(S))return;}
  else if(S->OwningBuilding->WallNodeAuthority.Version!=2||(S->OutlineSource!=EEHBOutlineSource::RoomBoundary&&S->OutlineSource!=EEHBOutlineSource::RetainedRegion))return;
  if(!S->GetEditableLoopCopy(Loop,Original))return;Polygon=Original;WorldTransform=S->GetActorTransform();Holes=S->LocalHoles;Cuts=OriginalCuts=S->CutOperations;
  if(AEHB_FloorSlab::IsCutOperationLoopIndex(Loop))
  {
   const int32 Index=AEHB_FloorSlab::GetCutOperationIndexFromLoopIndex(Loop);if(!Cuts.IsValidIndex(Index))return;auto& Cut=Cuts[Index];
   if(!EHBSlabOpeningEdit::NormalizeCut(Cut,Original))return;
  }
  if(!Polygon.IsValidIndex(A)||(!Corner&&!Polygon.IsValidIndex(B)))return;
  GraphRevision=S->OwningBuilding->RelationshipGraphRevision;GeometryRevision=S->OwningBuilding->GetElementGeometryRevision(S->ElementGuid);
  bCaptured=true;Feedback=Execute(true);bReady=Feedback.bSucceeded;
 }
 FEHBToolsetOperationResult Execute(bool Preview) const
 {
  auto* S=Slab.Get();if(bCancelled){FEHBToolsetOperationResult R;R.bSucceeded=true;R.Message=TEXT("Cancelled");return R;}
  if(!bCaptured||!S||!S->OwningBuilding||!WorldTransform.Equals(S->GetActorTransform(),0.0001)){FEHBToolsetOperationResult R;R.Message=TEXT("StaleRegionDrag");return R;}
  if(LoopIndex!=INDEX_NONE)
  {
   auto CandidateHoles=Holes;auto CandidateCuts=Polygon==Original?OriginalCuts:Cuts;
   if(CandidateHoles.IsValidIndex(LoopIndex))CandidateHoles[LoopIndex].LocalPolygon=Polygon;
   else if(Polygon!=Original)
   {
    const int32 Index=AEHB_FloorSlab::GetCutOperationIndexFromLoopIndex(LoopIndex);
    if(!CandidateCuts.IsValidIndex(Index)||CandidateCuts[Index].Source.ExplicitPolygon.Points.Num()!=Polygon.Num()){FEHBToolsetOperationResult R;R.Message=TEXT("InvalidOpeningHandle");return R;}
    for(int32 I=0;I<Polygon.Num();++I)CandidateCuts[Index].Source.ExplicitPolygon.Points[I].LocalPosition=FVector(Polygon[I].X,Polygon[I].Y,0);
   }
   return UEHBBuildingToolset::SetFloorSlabOpenings(S,CandidateHoles,CandidateCuts,GraphRevision,GeometryRevision,Preview);
  }
  return UEHBBuildingToolset::EditFinishRegion(S->OwningBuilding,S->ElementGuid,GraphRevision,GeometryRevision,Polygon,{},Preview);
 }
 void Update(const FVector& WorldDelta)
 {
  if(!bReady||bCancelled)return;auto* S=Slab.Get();if(!S)return;Delta+=FVector(WorldDelta.X,WorldDelta.Y,0);Polygon=Original;
  if(bCorner)
  {
   FVector Target=WorldTransform.TransformPosition(Original[First])+Delta;
   S->SnapCornerToAdjacentAxesWorldLocation(LoopIndex,First,Target);if(LoopIndex==INDEX_NONE)S->SnapOuterCornerHandleWorldLocation(First,Target);
   Polygon[First]=WorldTransform.InverseTransformPosition(Target);Polygon[First].Z=Original[First].Z;
  }
  else
  {
   FVector Snapped=Delta;if(LoopIndex==INDEX_NONE)S->SnapOuterEdgeHandleWorldDelta(First,Second,Snapped);FVector Local=WorldTransform.InverseTransformVector(Snapped);Local.Z=0;
   Polygon[First]+=Local;Polygon[Second]+=Local;
  }
  Feedback=Execute(true);
 }
 FVector WidgetLocation() const
 {
  if(!Polygon.IsValidIndex(First))return FVector::ZeroVector;
  return WorldTransform.TransformPosition(bCorner||!Polygon.IsValidIndex(Second)?Polygon[First]:(Polygon[First]+Polygon[Second])*0.5);
 }
};
