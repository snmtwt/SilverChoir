#pragma once
#include "CoreMinimal.h"
#include "Core/EHBLogicalSurface.h"
#include "Cutting/EHBCutTypes.h"

/** Pure resolution of a captured logical host; no actor queries, ID creation or
 * renderer access. Commands must capture candidate hosts before publication. */
struct EASYHOUSEBUILDER_API FEHBSurfaceOpening
{
 static bool Resolve(const FEHBCutOperation& Operation,const FEHBLogicalSurfaceDefinition& Host,
  const FTransform& ElementToBuilding,TArray<FVector>& ElementPolygon,FName& Status);
};
