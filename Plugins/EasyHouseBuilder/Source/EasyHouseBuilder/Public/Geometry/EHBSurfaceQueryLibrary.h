// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Geometry/EHBSurfaceInteractionTypes.h"

struct FHitResult;
class UEHBArchitecturalSurfaceComponent;

class EASYHOUSEBUILDER_API FEHBSurfaceQueryLibrary
{
public:
	static bool ResolveHitSurface(const FHitResult& HitResult, FEHBSurfaceHit& OutSurfaceHit);

	static void QuerySurfacesInBox(
		const FEHBSurfaceQueryParams& Params,
		TArray<UEHBArchitecturalSurfaceComponent*>& OutSurfaces);

	static bool FindBestSnap(
		const FEHBSnapQuery& Query,
		FEHBSnapResult& OutResult);

	static bool ResolveYawSnap(
		float DesiredYaw,
		const TArray<FEHBSnapAxisFeature>& AxisFeatures,
		float AngleThreshold,
		float& OutSnappedYaw,
		FEHBSnapAxisFeature& OutFeature);
};
