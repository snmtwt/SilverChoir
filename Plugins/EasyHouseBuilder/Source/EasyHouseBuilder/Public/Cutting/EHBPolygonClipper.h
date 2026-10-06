// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"

class EASYHOUSEBUILDER_API FEHBPolygonClipper
{
public:
	static bool DifferenceXY(
		const TArray<FVector>& SubjectPolygon,
		const TArray<TArray<FVector>>& CutterPolygons,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);

	static bool UnionXY(
		const TArray<TArray<FVector>>& Polygons,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);

	static bool IntersectionXY(
		const TArray<FVector>& SubjectPolygon,
		const TArray<FVector>& CutterPolygon,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);

	static bool DifferenceXZ(
		const TArray<FVector>& SubjectPolygon,
		const TArray<TArray<FVector>>& CutterPolygons,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);

	static bool CutHorizontalByPolygon(
		const TArray<FVector>& SubjectPolygon,
		const TArray<TArray<FVector>>& CutterPolygons,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);

	static bool CutVerticalByPolygon(
		const TArray<FVector>& SubjectPolygon,
		const TArray<TArray<FVector>>& CutterPolygons,
		FEHBPolygonClipResult& OutResult,
		double Scale = 1000.0);
};
