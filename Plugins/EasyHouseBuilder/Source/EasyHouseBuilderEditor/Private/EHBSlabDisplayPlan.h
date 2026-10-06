#pragma once
#include "Actors/EHB_FloorSlab.h"
#include "Geometry/EHBFloorContactGeometry.h"

// Candidate-only allocation shared by topology and authored region commands.
// Prepare never mutates actors; Verify checks generated coverage before publish.
struct FEHBSlabDisplayPlan
{
 struct FCandidate { AEHB_FloorSlab* Actor=nullptr; TArray<FVector> Polygon; FEHBSlabDisplayPartition Display; TArray<FEHBFloorSlabHole> Holes; TArray<FEHBCutOperation> Cuts; FGuid PlannedElementGuid; bool bOverrideSources=false; bool bNew=false; FGuid Identity() const {return PlannedElementGuid.IsValid()?PlannedElementGuid:Actor->ElementGuid;} bool InheritsPriority() const {return !bNew&&Actor->DisplayPartition.IsActive();} };
 struct FDisplayGroup { TArray<int32> Indices; TArray<FEHBFloorSupportSurface> Expected; };
 bool bPreserveSlabSources=false;
 TArray<FCandidate> Slabs;
 TArray<AEHB_FloorSlab*> DisplayLayers;
 TArray<FDisplayGroup> DisplayGroups;
 TArray<FGuid> AuthoredElements;
 bool HasConstraintsInLayer(const AEHB_FloorSlab* Reference) const;
 bool Prepare(FName& Status);
 bool Verify(FName& Status) const;
};
