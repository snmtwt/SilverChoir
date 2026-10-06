#pragma once
#include "EHBFinishRegionCommand.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Core/EHBBuildingActorBase.h"

/** Editor-owned cutter preview. No persistent source or transaction until commit. */
struct FEHBSlabCutterDraft
{
 TWeakObjectPtr<AEHB_FloorSlab> Slab;
 FTransform SlabTransform;
 FEHBFloorSlabCutterData Cutter;
 FEHBCutOperation Operation;
 TArray<FEHBFloorSlabHole> Holes;
 TArray<FEHBCutOperation> Cuts;
 int32 GraphRevision=0,GeometryRevision=0;
 bool bCaptured=false,bTracking=false;
 FEHBToolsetOperationResult Feedback;
 static bool Supports(const AEHB_FloorSlab* S)
 {
  return IsValid(S)&&S->OwningBuilding&&S->OwningBuilding->WallNodeAuthority.Version==2&&(S->OutlineSource==EEHBOutlineSource::RoomBoundary||S->OutlineSource==EEHBOutlineSource::RetainedRegion);
 }
 bool IsFor(const AEHB_FloorSlab* S) const {return bCaptured&&Slab.IsValid()&&Slab.Get()==S;}
 bool Begin(AEHB_FloorSlab* S,EEHBFloorSlabCutterShape Shape)
 {
  *this={};if(!Supports(S)||S->LocalTopPolygon.Num()<3)return false;
  Slab=S;SlabTransform=S->GetActorTransform();Holes=S->LocalHoles;Cuts=S->CutOperations;
  GraphRevision=S->OwningBuilding->RelationshipGraphRevision;GeometryRevision=S->OwningBuilding->GetElementGeometryRevision(S->ElementGuid);
  FVector Center=FVector::ZeroVector;for(const auto& V:S->LocalTopPolygon)Center+=V;Center/=S->LocalTopPolygon.Num();Center.Z=(S->GetTopZ()+S->GetBottomZ())*0.5;
  Cutter.Shape=Shape;Cutter.Height=FMath::Max(S->Thickness+80.0f,120.0f);Cutter.LocalTransform=FTransform(Center);
  Operation.Source.SourceType=EEHBCutSourceType::ExplicitPrism;Operation.Source.PrimitiveShape=Shape==EEHBFloorSlabCutterShape::Circle?EEHBCutPrimitiveShape::Circle:EEHBCutPrimitiveShape::Square;Operation.TransformPolicy=EEHBCutTransformPolicy::TargetLocal;Operation.EnsureGuids();
  bCaptured=true;Feedback=Execute(true);return true; // An invalid initial placement remains movable.
 }
 FEHBToolsetOperationResult Execute(bool Preview) const
 {
  auto* S=Slab.Get();if(!IsFor(S)||!S->OwningBuilding||!SlabTransform.Equals(S->GetActorTransform(),0.0001)){FEHBToolsetOperationResult R;R.Message=TEXT("StaleCutterDraft");return R;}
  auto Candidate=Cuts;auto Op=Operation;Op.Source.LocalTransform=Cutter.LocalTransform;Op.Source.Size=Cutter.Size;Op.Source.Height=Cutter.Height;Op.Source.CircleSideCount=Cutter.CircleSideCount;Candidate.Add(Op);
  return UEHBBuildingToolset::SetFloorSlabOpenings(S,Holes,Candidate,GraphRevision,GeometryRevision,Preview);
 }
 void Update(const FVector& Drag,const FRotator& Rotation,const FVector& Scale)
 {
  if(!bCaptured)return;
  auto World=Cutter.LocalTransform*SlabTransform;World.AddToTranslation(Drag);
  if(!Rotation.IsNearlyZero()){World.ConcatenateRotation(Rotation.Quaternion());World.NormalizeRotation();}
  if(!Scale.IsNearlyZero()){const auto Next=World.GetScale3D()+Scale;World.SetScale3D(FVector(FMath::Max(0.05,Next.X),FMath::Max(0.05,Next.Y),FMath::Max(0.05,Next.Z)));}
  Cutter.LocalTransform=World*SlabTransform.Inverse();Feedback=Execute(true);
 }
 TArray<FVector> Polygon() const {return Slab.IsValid()?Slab->BuildCutterLocalPolygon(Cutter):TArray<FVector>{};}
 FVector WidgetLocation() const {return (Cutter.LocalTransform*SlabTransform).GetLocation();}
 void Cancel(){*this={};}
};
