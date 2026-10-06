// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"
#include "EHBMeshEnvelopeBuilder.generated.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshEnvelopeBuildOptions
{
	GENERATED_BODY()

	/** Bridges projected gaps and concave bites up to this size. A value of 0 keeps the raw projected union. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0", Units = "cm"))
	float ConcavityBridgeDistance = 80.0f;

	/** Expands the final projected envelope footprint before extrusion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0", Units = "cm"))
	float ProjectionPadding = 2.0f;

	/** Extends the envelope vertically so boolean cutters fully pass through target roof surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0", Units = "cm"))
	float ZPadding = 80.0f;

	/** Ensures very flat source aggregates still produce a useful cutter volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "1.0", Units = "cm"))
	float MinExtrudeHeight = 120.0f;

	/** Minimum projected region area in square centimeters. Tiny slivers are discarded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0"))
	float MinProjectedArea = 4.0f;

	/** Gives nearly vertical or degenerate triangle projections a small footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0", Units = "cm"))
	float ThinProjectionFallbackWidth = 4.0f;

	/** Removes very short path segments after Clipper operations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "0.0", Units = "cm"))
	float PathCleanTolerance = 0.05f;

	/** Fixed-point scale used by Clipper. Higher values preserve more detail but may overflow for huge scenes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh Envelope", meta = (ClampMin = "1.0"))
	double ClipperScale = 1000.0;
};

class EASYHOUSEBUILDER_API FEHBMeshEnvelopeBuilder
{
public:
	static bool BuildProjectedEnvelopeRegions(
		const TArray<FEHBMeshAggregateData>& Aggregates,
		const FEHBMeshEnvelopeBuildOptions& Options,
		TArray<FEHBPolygonRegion>& OutRegions,
		FBox& OutSourceBounds,
		FString* OutFailureReason = nullptr);

	static bool BuildEnvelopeMeshes(
		const TArray<FEHBMeshAggregateData>& Aggregates,
		const FEHBMeshEnvelopeBuildOptions& Options,
		TArray<UE::Geometry::FDynamicMesh3>& OutMeshes,
		FString* OutFailureReason = nullptr);
};
