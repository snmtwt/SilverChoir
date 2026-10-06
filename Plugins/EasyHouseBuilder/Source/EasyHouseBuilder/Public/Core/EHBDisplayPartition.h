#pragma once
#include "Core/EHBLogicalSurface.h"

struct EASYHOUSEBUILDER_API FEHBDisplayPartitionInput
{
 FEHBLogicalSurfaceDefinition Base;
 TArray<FEHBLogicalSurfaceRegion> RequestedDisplay;
 // Explicit authoring order, preserved by copy; never derive from regenerated GUIDs.
 int32 Priority=0;
};

/** Read-only display ownership for one coplanar material layer. Logical regions never change.
 * Lower priority owns contested expansion only; all original material and authored voids win.
 * Output uses each input's plane and identity, sorted by priority. Not support geometry. */
class EASYHOUSEBUILDER_API FEHBDisplayPartition
{
public:
 static bool Build(const TArray<FEHBDisplayPartitionInput>& Input,
  TArray<FEHBLogicalSurfaceDefinition>& Output,FName& Status, bool bAllowGridRoundTrip=false);
 /** Revalidation only: both coverages must fit within a two-grid-unit (0.002 cm)
  * envelope of each other. This is not permission to discard authored material. */
 static bool MatchesWithinGrid(const FEHBLogicalSurfaceDefinition& A,
  const FEHBLogicalSurfaceDefinition& B,FName& Status);
};
