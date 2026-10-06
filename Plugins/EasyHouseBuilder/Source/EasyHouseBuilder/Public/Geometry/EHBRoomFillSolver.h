// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct EASYHOUSEBUILDER_API FEHBPlanarRoomFillResult
{
	TArray<FVector> OuterPolygon;
	TArray<TArray<FVector>> HolePolygons;
};

class EASYHOUSEBUILDER_API FEHBPlanarRoomFillSolver
{
public:
	static bool BuildContainingFreeRegion(
		const TArray<TArray<FVector>>& ObstaclePolygons,
		const FVector& TargetPoint,
		FEHBPlanarRoomFillResult& OutResult);
};
