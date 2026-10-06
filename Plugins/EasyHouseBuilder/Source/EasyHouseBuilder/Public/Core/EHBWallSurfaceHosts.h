#pragma once
#include "Core/EHBLogicalSurface.h"
#include "Core/EHBWallJunctionGeometry.h"

/** Owned candidate geometry plus existing semantic identities. No Actor, world,
 * renderer or permission to publish a topology edit. Coordinates are in cm. */
struct EASYHOUSEBUILDER_API FEHBWallSurfaceHostSource
{
 FGuid BuildingGuid, LeftSurfaceGuid, RightSurfaceGuid;
 int32 FloorIndex=INDEX_NONE;
 double Height=0, Thickness=0;
 bool bStructural=false;
 FEHBWallJunctionWallSides Sides;
};

struct EASYHOUSEBUILDER_API FEHBWallSurfaceHosts
{
 /** Left then right. Uses supplied identities; never allocates or repairs them.
  * Both surfaces publish together; failure leaves Out empty. */
 static bool Build(const FEHBWallSurfaceHostSource& Source,
  TArray<FEHBLogicalSurfaceDefinition>& Out,FName& Status);
};
