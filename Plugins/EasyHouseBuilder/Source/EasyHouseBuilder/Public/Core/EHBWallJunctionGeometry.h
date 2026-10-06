// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** One outgoing wall tangent expressed in the node/pillar local XY frame. */
struct EASYHOUSEBUILDER_API FEHBWallJunctionLeg
{
	FVector Direction=FVector::ForwardVector; // Must be unit XY.
	float WallThickness=20;
};

/** Candidate node pose in building coordinates; unit scale and yaw rotation only. IDs need no Actor binding. */
struct EASYHOUSEBUILDER_API FEHBWallJunctionNodeInput
{
	FGuid NodeGuid;
	FTransform LocalTransform;
	float Width=20,Depth=20;
	bool bExactWallThickness=false; // Unbound fill keeps full wall thickness; bound legacy pillars retain their inset.
};
struct EASYHOUSEBUILDER_API FEHBWallJunctionWallInput
{
	FGuid WallGuid,StartNodeGuid,EndNodeGuid;
	float Thickness=20,Height=300;
};
struct EASYHOUSEBUILDER_API FEHBWallJunctionWallSides
{
	FGuid WallGuid;
	FVector LocalStart,LocalEnd; // Centers of the resolved connection faces.
	FTransform LocalTransform;
	FVector StartLeft,EndLeft,StartRight,EndRight; // Top boundary, building coordinates.
};

/** Completed geometry work only; all input identities/spans are still validated. Zero on failure. */
struct EASYHOUSEBUILDER_API FEHBWallJunctionSolveStats
{
	int32 NodeFootprintsSolved=0;
	int32 WallSidesSolved=0;
};

/** Value-only legacy polygon junction solver. No Actor, world, mesh cache, or transaction access. */
struct EASYHOUSEBUILDER_API FEHBWallJunctionGeometry
{
	/** Derived-output contract revision: v2 closes the float 360/0 angle-sort seam.
	 * Not an element identity or an automatic serialized-asset migration. */
	static constexpr int32 OutputVersion = 2;
	/** Atomic read-only candidate solve; reason identifies the first unsupported/invalid input. Not a topology collision validator. */
	static bool BuildStraightWallSides(const TArray<FEHBWallJunctionNodeInput>& Nodes,const TArray<FEHBWallJunctionWallInput>& Walls,TArray<FEHBWallJunctionWallSides>& OutWalls,FName& OutReason,
		const TSet<FGuid>* RequestedWallGuids=nullptr,FEHBWallJunctionSolveStats* OutStats=nullptr,
		TMap<FGuid,TArray<FVector>>* OutNodeFootprints=nullptr);
	// Optional footprints are the exact solved local outlines, published only on success.
	// Null selection solves all walls; explicit empty selection solves none. Every incident leg at
	// requested endpoints remains part of the junction, including walls outside the output selection.
	static bool BuildFootprint(float Width,float Depth,const TArray<FEHBWallJunctionLeg>& Legs,TArray<FVector>& OutLocalFootprint,bool bExactWallThickness=false);
	static bool ResolveFace(float Width,float Depth,const TArray<FVector>& LocalFootprint,const FVector& Direction,float WallThickness,FVector& OutLeft,FVector& OutRight,bool bExactWallThickness=false);
};
