// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/EHBVerticalSurfaceComponent.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Settings/EHBBuildingToolsetSettings.h"

namespace
{
	UMaterialInterface* ResolveConfiguredDefaultWhiteBoxMaterial()
	{
		if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
		{
			if (UMaterialInterface* Material = Settings->DefaultWhiteBoxMaterial.LoadSynchronous())
			{
				return Material;
			}
		}
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FVector GetSafeHorizontalVector(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.0f).GetSafeNormal();
	}

	FVector GetOpenSurfaceOutwardNormal(
		const FVector& LocalDirection,
		EEHBArchitecturalSurfaceRole SurfaceRole)
	{
		switch (SurfaceRole)
		{
		case EEHBArchitecturalSurfaceRole::WallRightSide:
			return FVector(LocalDirection.Y, -LocalDirection.X, 0.0f).GetSafeNormal();
		case EEHBArchitecturalSurfaceRole::WallLeftSide:
		default:
			return FVector(-LocalDirection.Y, LocalDirection.X, 0.0f).GetSafeNormal();
		}
	}
}

UEHBVerticalSurfaceComponent::UEHBVerticalSurfaceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SurfaceRole = EEHBArchitecturalSurfaceRole::PillarSide;
	SurfaceName = TEXT("Vertical.Surface");
	ComponentTags.AddUnique(TEXT("EHB_VerticalSurface"));
}

void UEHBVerticalSurfaceComponent::SetVerticalSurfaceData(
	const TArray<FVector>& InBasePolyline,
	float InSurfaceHeight,
	float InBottomOffset,
	bool bInClosedLoop,
	float InUVWorldSize)
{
	LocalBasePolyline = InBasePolyline;
	SurfaceHeight = FMath::Max(1.0f, InSurfaceHeight);
	BottomOffset = InBottomOffset;
	bClosedLoop = bInClosedLoop;
	UVWorldSize = FMath::Max(1.0f, InUVWorldSize);
}

bool UEHBVerticalSurfaceComponent::RebuildSurfaceMesh()
{
	FEHBVerticalSurfaceMeshBuildInput BuildInput;
	BuildInput.LocalBasePolyline = LocalBasePolyline;
	BuildInput.SurfaceHeight = SurfaceHeight;
	BuildInput.BottomOffset = BottomOffset;
	BuildInput.bClosedLoop = bClosedLoop;
	BuildInput.UVWorldSize = UVWorldSize;

	FEHBSurfaceMeshBuildResult BuildResult;
	if (!FEHBVerticalSurfaceGeometryBuilder::BuildVerticalSurface(BuildInput, BuildResult))
	{
		ClearSurfaceMesh();
		return false;
	}

	return SubmitMeshBuildResult(
		SurfaceName,
		BuildResult,
		ResolveConfiguredDefaultWhiteBoxMaterial(),
		bEnableSurfaceCollision);
}

bool UEHBVerticalSurfaceComponent::ProjectWorldPointToSurface(
	const FVector& WorldPoint,
	FVector& OutWorldPoint,
	FVector2D& OutSurfaceUV) const
{
	if (LocalBasePolyline.Num() < 2)
	{
		return false;
	}

	const FVector LocalPoint = GetComponentTransform().InverseTransformPosition(WorldPoint);
	const int32 SegmentCount = bClosedLoop ? LocalBasePolyline.Num() : LocalBasePolyline.Num() - 1;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	FVector BestLocalPoint = FVector::ZeroVector;
	float BestDistanceAlongPath = 0.0f;
	float DistanceBeforeSegment = 0.0f;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector SegmentStart = LocalBasePolyline[SegmentIndex];
		FVector SegmentEnd = LocalBasePolyline[(SegmentIndex + 1) % LocalBasePolyline.Num()];
		const float SegmentLength = FVector::Dist2D(SegmentStart, SegmentEnd);
		if (SegmentLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const float Alpha = FEHBSurfaceGeometryUtil::GetClosestAlphaOnSegmentXY(SegmentStart, SegmentEnd, LocalPoint);
		FVector ClosestPoint = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);
		ClosestPoint.Z = FMath::Clamp(LocalPoint.Z, BottomOffset, BottomOffset + SurfaceHeight);
		const float DistanceSquared = FVector::DistSquared(ClosestPoint, LocalPoint);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestLocalPoint = ClosestPoint;
			BestDistanceAlongPath = DistanceBeforeSegment + SegmentLength * Alpha;
		}

		DistanceBeforeSegment += SegmentLength;
	}

	if (BestDistanceSquared == TNumericLimits<float>::Max())
	{
		return false;
	}

	OutWorldPoint = GetComponentTransform().TransformPosition(BestLocalPoint);
	OutSurfaceUV = FVector2D(
		BestDistanceAlongPath / FMath::Max(1.0f, UVWorldSize),
		(BestLocalPoint.Z - BottomOffset) / FMath::Max(1.0f, UVWorldSize));
	return true;
}

bool UEHBVerticalSurfaceComponent::BuildSnapCandidates(
	const FVector& WorldPoint,
	TArray<FEHBSurfaceSnapCandidate>& OutCandidates) const
{
	if (!bCanBeSnapTarget)
	{
		return false;
	}

	const int32 InitialCount = OutCandidates.Num();
	FVector ClosestWorldPoint;
	FVector2D SurfaceUV;
	if (ProjectWorldPointToSurface(WorldPoint, ClosestWorldPoint, SurfaceUV))
	{
		FEHBSurfaceSnapCandidate& SurfaceCandidate = OutCandidates.AddDefaulted_GetRef();
		SurfaceCandidate.SnapKind = EEHBSurfaceSnapKind::Plane;
		SurfaceCandidate.WorldLocation = ClosestWorldPoint;
		SurfaceCandidate.WorldNormal = GetRightVector();
		SurfaceCandidate.Distance = FVector::Distance(WorldPoint, ClosestWorldPoint);
		SurfaceCandidate.Endpoint = MakeSurfaceEndpoint();
	}

	FEHBSurfaceBoundaryHit BoundaryHit;
	if (FindClosestBoundaryPoint(WorldPoint, BoundaryHit))
	{
		FEHBSurfaceSnapCandidate& EdgeCandidate = OutCandidates.AddDefaulted_GetRef();
		EdgeCandidate.SnapKind = EEHBSurfaceSnapKind::Edge;
		EdgeCandidate.WorldLocation = BoundaryHit.WorldPoint;
		EdgeCandidate.WorldNormal = BoundaryHit.WorldNormal;
		EdgeCandidate.WorldTangent = BoundaryHit.WorldTangent;
		EdgeCandidate.Distance = BoundaryHit.Distance;
		EdgeCandidate.Endpoint = MakeSurfaceEndpoint();
	}

	return OutCandidates.Num() > InitialCount;
}

bool UEHBVerticalSurfaceComponent::BuildSideSnapEdges(TArray<FEHBSurfaceSideSnapEdge>& OutEdges) const
{
	if (!bCanBeSnapTarget || LocalBasePolyline.Num() < 2)
	{
		return false;
	}

	const int32 InitialCount = OutEdges.Num();
	const int32 SegmentCount = bClosedLoop ? LocalBasePolyline.Num() : LocalBasePolyline.Num() - 1;
	if (SegmentCount <= 0)
	{
		return false;
	}

	const bool bCanUseLoopWinding = bClosedLoop && LocalBasePolyline.Num() >= 3;
	const bool bCounterClockwise = bCanUseLoopWinding
		&& FEHBSurfaceGeometryUtil::CalculateSignedAreaXY(LocalBasePolyline) > 0.0;
	const FTransform ComponentTransform = GetComponentTransform();
	const FEHBSurfaceEndpoint SurfaceEndpoint = MakeSurfaceEndpoint();
	const float TopZ = BottomOffset + SurfaceHeight;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector LocalStart = LocalBasePolyline[SegmentIndex];
		FVector LocalEnd = LocalBasePolyline[(SegmentIndex + 1) % LocalBasePolyline.Num()];
		const FVector LocalDirection = GetSafeHorizontalVector(LocalEnd - LocalStart);
		if (LocalDirection.IsNearlyZero())
		{
			continue;
		}

		FVector LocalOutwardNormal;
		if (bCanUseLoopWinding)
		{
			LocalOutwardNormal = bCounterClockwise
				? FVector(LocalDirection.Y, -LocalDirection.X, 0.0f)
				: FVector(-LocalDirection.Y, LocalDirection.X, 0.0f);
		}
		else
		{
			LocalOutwardNormal = GetOpenSurfaceOutwardNormal(LocalDirection, SurfaceRole);
		}

		LocalStart.Z = TopZ;
		LocalEnd.Z = TopZ;
		const FVector WorldStart = ComponentTransform.TransformPosition(LocalStart);
		const FVector WorldEnd = ComponentTransform.TransformPosition(LocalEnd);
		const FVector WorldDirection = GetSafeHorizontalVector(WorldEnd - WorldStart);
		const FVector WorldOutwardNormal = GetSafeHorizontalVector(ComponentTransform.TransformVectorNoScale(LocalOutwardNormal));
		if (WorldDirection.IsNearlyZero() || WorldOutwardNormal.IsNearlyZero())
		{
			continue;
		}

		FVector LocalBottomStart = LocalStart;
		FVector LocalBottomEnd = LocalEnd;
		LocalBottomStart.Z = BottomOffset;
		LocalBottomEnd.Z = BottomOffset;
		const FVector WorldBottomStart = ComponentTransform.TransformPosition(LocalBottomStart);
		const FVector WorldBottomEnd = ComponentTransform.TransformPosition(LocalBottomEnd);

		FEHBSurfaceSideSnapEdge& Edge = OutEdges.AddDefaulted_GetRef();
		Edge.WorldStart = WorldStart;
		Edge.WorldEnd = WorldEnd;
		Edge.WorldDirection = WorldDirection;
		Edge.WorldOutwardNormal = WorldOutwardNormal;
		Edge.WorldMidpoint = (WorldStart + WorldEnd) * 0.5f;
		Edge.MinWorldZ = FMath::Min3(WorldStart.Z, WorldEnd.Z, FMath::Min(WorldBottomStart.Z, WorldBottomEnd.Z));
		Edge.MaxWorldZ = FMath::Max3(WorldStart.Z, WorldEnd.Z, FMath::Max(WorldBottomStart.Z, WorldBottomEnd.Z));
		Edge.Endpoint = SurfaceEndpoint;
		Edge.SourceSurface = const_cast<UEHBVerticalSurfaceComponent*>(this);
	}

	return OutEdges.Num() > InitialCount;
}

bool UEHBVerticalSurfaceComponent::FindClosestBoundaryPoint(
	const FVector& WorldPoint,
	FEHBSurfaceBoundaryHit& OutBoundaryHit) const
{
	return FEHBSurfaceGeometryUtil::FindClosestBoundaryPointXY(
		LocalBasePolyline,
		GetComponentTransform(),
		WorldPoint,
		BottomOffset + SurfaceHeight,
		bClosedLoop,
		OutBoundaryHit);
}
