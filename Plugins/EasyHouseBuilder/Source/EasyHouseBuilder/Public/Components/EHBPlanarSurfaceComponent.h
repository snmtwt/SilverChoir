// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "EHBPlanarSurfaceComponent.generated.h"

UCLASS(ClassGroup = Rendering, HideCategories = (Object, LOD), meta = (BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBPlanarSurfaceComponent : public UEHBArchitecturalSurfaceComponent
{
	GENERATED_BODY()

public:
	UEHBPlanarSurfaceComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Planar")
	TArray<FVector> LocalBoundaryLoop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Planar")
	TArray<FEHBSurfaceHoleLoop> LocalHoleLoops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Planar")
	FVector LocalPlaneNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Planar", meta = (ClampMin = "1.0", Units = "cm"))
	float UVWorldSize = 100.0f;

 // Empty for the legacy single-region representation. Multiple regions are
 // kept explicitly so disconnected display pieces retain query and snap edges.
 UPROPERTY(VisibleAnywhere, Category="EHB Surface|Planar")
 TArray<FEHBPlanarSurfaceRegion> LocalRegions;
 void SetPlanarSurfaceRegions(const TArray<FEHBPlanarSurfaceRegion>& Regions,const FVector& Normal,float InUVWorldSize);
 TArray<FEHBPlanarSurfaceRegion> GetPlanarSurfaceRegions() const;

	void SetPlanarSurfaceData(
		const TArray<FVector>& InBoundaryLoop,
		const TArray<TArray<FVector>>& InHoleLoops,
		const FVector& InLocalPlaneNormal,
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
