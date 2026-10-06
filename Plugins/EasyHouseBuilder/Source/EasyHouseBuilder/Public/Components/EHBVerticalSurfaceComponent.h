// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "EHBVerticalSurfaceComponent.generated.h"

UCLASS(ClassGroup = Rendering, HideCategories = (Object, LOD), meta = (BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBVerticalSurfaceComponent : public UEHBArchitecturalSurfaceComponent
{
	GENERATED_BODY()

public:
	UEHBVerticalSurfaceComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical")
	TArray<FVector> LocalBasePolyline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical", meta = (ClampMin = "1.0", Units = "cm"))
	float SurfaceHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical", meta = (Units = "cm"))
	float BottomOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical")
	bool bClosedLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical", meta = (ClampMin = "1.0", Units = "cm"))
	float UVWorldSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Vertical")
	TArray<FEHBSurfaceOpeningLoop> OpeningLoops;

	void SetVerticalSurfaceData(
		const TArray<FVector>& InBasePolyline,
		float InSurfaceHeight,
		float InBottomOffset,
		bool bInClosedLoop,
		float InUVWorldSize);

	virtual bool RebuildSurfaceMesh() override;
	virtual bool ProjectWorldPointToSurface(
		const FVector& WorldPoint,
		FVector& OutWorldPoint,
		FVector2D& OutSurfaceUV) const override;

	virtual bool BuildSnapCandidates(
		const FVector& WorldPoint,
		TArray<FEHBSurfaceSnapCandidate>& OutCandidates) const override;

	virtual bool BuildSideSnapEdges(TArray<FEHBSurfaceSideSnapEdge>& OutEdges) const override;

	bool FindClosestBoundaryPoint(
		const FVector& WorldPoint,
		FEHBSurfaceBoundaryHit& OutBoundaryHit) const;
};
