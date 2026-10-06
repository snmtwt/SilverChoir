// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** Derived local geometry only. Does not confer physical pillar or support identity. */
struct EASYHOUSEBUILDER_API FEHBWallJunctionMesh
{
	FGuid NodeGuid;
	FTransform LocalTransform;
	TArray<FVector> Footprint,Vertices,Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;
};

struct EASYHOUSEBUILDER_API FEHBWallJunctionMeshBuilder
{
	/** Increment when prism tessellation or vertex attributes intentionally change. */
	static constexpr int32 OutputVersion = 1;
	/** Closed simple planar footprint, no holes; CCW normalized. Atomic output, no World access.
	 * Keeps legacy fan order for strictly convex input; ear-clips concave/collinear input. */
	static bool BuildPrism(const TArray<FVector>& Footprint,float Height,FEHBWallJunctionMesh& OutMesh);
};
