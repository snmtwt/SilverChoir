// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

class EASYHOUSEBUILDERMANIFOLD_API FEHBManifoldBoolean
{
public:
	static bool ApplyDifference(
		const UE::Geometry::FDynamicMesh3& TargetMesh,
		const UE::Geometry::FDynamicMesh3& SourceMesh,
		UE::Geometry::FDynamicMesh3& OutDifferenceMesh,
		double MergeTolerance,
		int32 SlopeGroupID,
		int32 SideGroupID,
		FString* OutFailureReason = nullptr);

	static bool ApplyUnion(
		const UE::Geometry::FDynamicMesh3& MeshA,
		const UE::Geometry::FDynamicMesh3& MeshB,
		UE::Geometry::FDynamicMesh3& OutUnionMesh,
		double MergeTolerance,
		int32 SlopeGroupID,
		int32 SideGroupID,
		FString* OutFailureReason = nullptr);
};
