// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"

class AEHBElementActorBase;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

enum class EEHBCutSourceIntent : uint8
{
	GenericOverlap,
	RoofIntersection,
	RoofPlanarOpening,
};

enum class EEHBCutSourceGeometryKind : uint8
{
	MeshAggregate3D,
	OrientedQuadFootprint3D,
};

struct EASYHOUSEBUILDER_API FEHBCutSourceBuildContext
{
	const AEHBElementActorBase* TargetElement = nullptr;
	FTransform TargetLocalToWorld = FTransform::Identity;
	EEHBCutSourceIntent Intent = EEHBCutSourceIntent::GenericOverlap;

	bool bUseWallFootprintCutters = true;
	int32 MinWallFootprintGroupWallCount = 2;
	float WallFootprintGroupEndpointTolerance = 80.0f;
	float WallFootprintPadding = -2.0f;
	float WallFootprintMaxDimension = 0.0f;
	float WallFootprintMinArea = 100.0f;
	float WallFootprintMinDimension = 10.0f;
	float WallFootprintZPadding = 80.0f;
	float WallFootprintMinExtrudeHeight = 120.0f;
};

struct EASYHOUSEBUILDER_API FEHBResolvedCutSourceData
{
	EEHBCutSourceGeometryKind GeometryKind = EEHBCutSourceGeometryKind::MeshAggregate3D;
	FName DebugName = NAME_None;
	int32 Priority = 0;

	TArray<FGuid> SourceElementGuids;
	FEHBMeshAggregateData MeshAggregate;

	TArray<FVector> TargetLocalFootprint;
	FBox TargetLocalBounds = FBox(ForceInit);
	float MinZ = 0.0f;
	float MaxZ = 0.0f;
};

class EASYHOUSEBUILDER_API FEHBCutSourceBuilder
{
public:
	static bool BuildSources(
		const TArray<AEHBElementActorBase*>& SourceElements,
		const FEHBCutSourceBuildContext& Context,
		TArray<FEHBResolvedCutSourceData>& OutSources,
		FString* OutFailureReason = nullptr);

	static bool BuildDynamicMeshForSource(
		const FEHBResolvedCutSourceData& Source,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutFailureReason = nullptr);
};
