#pragma once
#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Definitions/EHBElementRelations.h"

/** Value snapshot for floor propagation. Does not own actors or publish revisions. */
struct FEHBFloorAssignmentState
{
 FGuid ElementGuid;
 EEHBBuildingElementType Type=EEHBBuildingElementType::None;
 int32 FloorIndex=0;
 EEHBBuildingFloorElementRole Role=EEHBBuildingFloorElementRole::None;
 EEHBFloorAssignmentPolicy Policy=EEHBFloorAssignmentPolicy::Automatic;
 EEHBFloorAssignmentSource Source=EEHBFloorAssignmentSource::Unassigned;
 TArray<int32> Candidates;
 bool Conflict=false;
};

struct EASYHOUSEBUILDER_API FEHBFloorAssignmentPlan
{
 /** Caller supplies geometry-validated relations, excluding stale edges.
  * Returns complete candidate states without changing either input. */
 static bool Build(const TArray<FEHBFloorAssignmentState>& Elements,
  const TArray<FEHBElementRelation>& Relations,TArray<FEHBFloorAssignmentState>& Out,FName& Status);
};
