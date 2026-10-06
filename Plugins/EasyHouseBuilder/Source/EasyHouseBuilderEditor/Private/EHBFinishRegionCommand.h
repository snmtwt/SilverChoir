#pragma once
#include "CoreMinimal.h"
#include "Actors/EHB_FloorSlab.h"
class AEHBBuildingActorBase;
class AEHBElementActorBase;
struct FEHBToolsetOperationResult;

namespace EHBFinishRegionCommand
{
 // Polygon uses the target's local coordinates; elevation and source identity
 // stay fixed. Rebind explicitly chooses one overlapping current room instead.
 FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* Building,FGuid ElementGuid,
  int32 ExpectedGraphRevision,int32 ExpectedGeometryRevision,const TArray<FVector>& Polygon,FGuid RoomGuid,bool bPreview);
 FEHBToolsetOperationResult EditSlabSources(AEHB_FloorSlab* Slab,int32 GraphRevision,int32 GeometryRevision,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,bool bPreview);
 bool IsIndependent(const AEHBElementActorBase* Element);
 TArray<FVector> GetPolygon(const AEHBElementActorBase* Element);
 FText DescribeResult(const FEHBToolsetOperationResult& Result);
#if WITH_DEV_AUTOMATION_TESTS
 inline int32 FailurePhase=0;
#endif
}
