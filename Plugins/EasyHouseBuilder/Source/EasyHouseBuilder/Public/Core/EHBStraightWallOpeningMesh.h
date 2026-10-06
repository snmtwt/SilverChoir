#pragma once
#include "Core/EHBWallJunctionMesh.h"

/** Resolved wall extent in element-local coordinates, including endpoint trims. */
struct FEHBWallResolvedGeometry
{
 float ReferenceLength=0;
 float StartLeftX=0,StartRightX=0,EndLeftX=0,EndRightX=0;
};

struct EASYHOUSEBUILDER_API FEHBStraightWallOpeningMeshSource
{
 FEHBWallResolvedGeometry Geometry;
 float Height=0,Thickness=0;
 bool bGenerateStartCap=true,bGenerateEndCap=true;
 /** Already resolved element-local XZ polygons. Overlap is a subtractive union.
  * Host binding/coverage and dependency policy are checked by the caller. */
 TArray<TArray<FVector2d>> Openings;
};
struct EASYHOUSEBUILDER_API FEHBStraightWallOpeningMeshes
{
 FEHBWallJunctionMesh Left,Right,Caps;
 double BoundaryCapArea=0;
};
struct EASYHOUSEBUILDER_API FEHBStraightWallOpeningMesh
{
 /** Read-only candidate geometry. No Actor, world, component or source fallback.
  * Caps include top, bottom, selected ends and double-sided opening reveals. */
 static bool Build(const FEHBStraightWallOpeningMeshSource& Source,
  FEHBStraightWallOpeningMeshes& Out,FName& Status);
};
