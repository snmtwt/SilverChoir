#pragma once
#include "CoreMinimal.h"
#include "Core/EHBWallJunctionMesh.h"
#include "Cutting/EHBCutTypes.h"
#include "Actors/EHB_Floor.h"

/** Read-only preview result, not a publication token. A commit must prepare again
 * against current dependencies. No actor/component ownership or persistent IDs. */
struct EASYHOUSEBUILDER_API FEHBPreparedWallOpening
{
 FGuid BuildingGuid, ElementGuid;
 int32 GraphRevision=INDEX_NONE, GeometryRevision=INDEX_NONE;
 TArray<FEHBCutOperation> Sources;
 FEHBWallJunctionMesh Left, Right, Caps;
 TArray<FEHBFloorSupportSurface> HorizontalTops;
 TArray<FEHBFloorFinishRegion> HorizontalBottoms;
 bool bTouchesHeightBoundary=false;
 double RemovedArea=0;
};

#if WITH_DEV_AUTOMATION_TESTS
namespace EHBWallOpeningPreparation
{
 // One-shot fault hooks: 1 generation refusal, 2 incorrect uncut candidate.
 extern EASYHOUSEBUILDER_API int32 FailurePhase;
}
#endif
