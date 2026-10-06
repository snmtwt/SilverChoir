#pragma once
#include "CoreMinimal.h"
class AEHBBuildingActorBase;
struct FEHBWallCreationEndpoint;
struct FEHBWallCreationOptions;

// Pure route identity: explicit logical node (optional physical binding), legacy
// physical pillar, or request endpoint slot. Node/element namespaces stay distinct.
struct EASYHOUSEBUILDER_API FEHBWallPathPoint
{
 FGuid ExistingPillarGuid;
 int32 EndpointIndex = INDEX_NONE;
 FVector LocalPosition = FVector::ZeroVector;
 int32 FloorIndex = 1;
 FGuid ExistingNodeGuid;
 int32 NodeRevision = INDEX_NONE;
};
struct EASYHOUSEBUILDER_API FEHBWallPathPlan
{
 bool bSucceeded = false;
 FName Status;
 TArray<FEHBWallPathPoint> Points;
 TArray<FIntPoint> Segments;
 TArray<int32> PathEdges;
};
class EASYHOUSEBUILDER_API FEHBWallPathPlanning
{
public:
 // Simulates endpoint creation in path order, preserving existing intermediate pillars.
 // On failure no partial route is returned. All positions must already be normalized.
 static FEHBWallPathPlan Build(const TArray<FEHBWallPathPoint>& Existing,
  const TArray<FEHBWallPathPoint>& Endpoints, bool bClosed, float Thickness);
 // Read-only actor/node authority adapter. Wall-surface endpoints require a separate split plan.
 static FEHBWallPathPlan BuildForBuilding(const AEHBBuildingActorBase* Building,
  const TArray<FEHBWallCreationEndpoint>& Endpoints, bool bClosed, const FEHBWallCreationOptions& Options);
};

// Source-relative intervals only: no Actor or newly allocated node/element IDs.
struct FEHBWallSplitIntervalRequest
{
 FGuid SourceGuid;
 FVector LocalStart=FVector::ZeroVector,LocalEnd=FVector::ZeroVector;
 float Distance=0,ColumnWidth=20;
};
struct FEHBWallSplitOpeningInterval
{
 FGuid SourceGuid,OpeningGuid;
 float Distance=0,Width=0;
};
struct FEHBWallSplitOpeningRoute
{
 FGuid OpeningGuid;
 bool bAfter=false;
 float NewDistance=0;
 FVector TargetStart=FVector::ZeroVector,TargetEnd=FVector::ZeroVector;
};
struct FEHBWallSplitIntervalStep
{
 int32 RequestIndex=INDEX_NONE;
 FVector EffectiveSourceEnd=FVector::ZeroVector;
 TArray<FEHBWallSplitOpeningRoute> Openings;
};
struct FEHBWallSplitIntervalPlan
{
 bool bSucceeded=false;
 FName Status;
 TArray<FEHBWallSplitIntervalStep> Steps;
};
struct EASYHOUSEBUILDER_API FEHBWallSplitIntervalPlanning
{
 // Descending distances retain the original source start; later steps consume the left remainder.
 // Every opening is routed at each step that still contains it. Failed input returns no steps.
 static FEHBWallSplitIntervalPlan Build(const TArray<FEHBWallSplitIntervalRequest>& Requests,
  const TArray<FEHBWallSplitOpeningInterval>& Openings);
};
