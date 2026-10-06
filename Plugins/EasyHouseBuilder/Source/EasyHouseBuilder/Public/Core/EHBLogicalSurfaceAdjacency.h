#pragma once
#include "Core/EHBLogicalSurface.h"

/** Edge indices refer only to the supplied immutable snapshot, never persistent lineage. */
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceEdgeRef
{
 FGuid ElementGuid,SurfaceGuid;
 int32 Region=INDEX_NONE,Hole=INDEX_NONE,Edge=INDEX_NONE;
 double StartAlpha=0,EndAlpha=0;
};
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceSharedEdge
{
 FEHBLogicalSurfaceEdgeRef A,B;
 FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
};

/** Caller supplies one non-overlapping material layer, not stacked floor/slab layers.
 * Finds positive-length shared intervals, including partial edges and hole boundaries.
 * Different buildings, floors or oriented planes cannot be neighbors. No actor writes. */
class EASYHOUSEBUILDER_API FEHBLogicalSurfaceAdjacency
{
public:
 static bool Build(const TArray<FEHBLogicalSurfaceDefinition>& Surfaces,TArray<FEHBLogicalSurfaceSharedEdge>& Out,FName& Status);
};
