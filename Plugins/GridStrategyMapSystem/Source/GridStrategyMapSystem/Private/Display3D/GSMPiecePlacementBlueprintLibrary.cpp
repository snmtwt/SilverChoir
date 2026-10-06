#include "GridStrategyMapSystem/Display3D/GSMPiecePlacementBlueprintLibrary.h"

#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

namespace
{
	FVector2D GetCornerUnitCoordinate(EGSMTileQueueCorner Corner)
	{
		switch (Corner)
		{
		case EGSMTileQueueCorner::NegativeXNegativeY:
			return FVector2D(-1.0, -1.0);
		case EGSMTileQueueCorner::NegativeXPositiveY:
			return FVector2D(-1.0, 1.0);
		case EGSMTileQueueCorner::PositiveXPositiveY:
			return FVector2D(1.0, 1.0);
		case EGSMTileQueueCorner::PositiveXNegativeY:
			return FVector2D(1.0, -1.0);
		default:
			return FVector2D::ZeroVector;
		}
	}

	FVector2D ResolveRowDirection(
		EGSMTileQueueRowDirection RowDirection,
		const FVector2D& Start,
		const FVector2D& End)
	{
		switch (RowDirection)
		{
		case EGSMTileQueueRowDirection::PositiveX:
			return FVector2D(1.0, 0.0);
		case EGSMTileQueueRowDirection::NegativeX:
			return FVector2D(-1.0, 0.0);
		case EGSMTileQueueRowDirection::PositiveY:
			return FVector2D(0.0, 1.0);
		case EGSMTileQueueRowDirection::NegativeY:
			return FVector2D(0.0, -1.0);
		case EGSMTileQueueRowDirection::TowardCenter:
		default:
			break;
		}

		FVector2D Direction = -(Start + End) * 0.5;
		if (!Direction.Normalize())
		{
			const FVector2D Segment = End - Start;
			Direction = FVector2D(-Segment.Y, Segment.X);
			Direction.Normalize();
		}
		return Direction;
	}

	float FindDistanceToSquareBoundary(const FVector2D& Point, const FVector2D& Direction, float HalfExtent)
	{
		float Distance = TNumericLimits<float>::Max();
		if (Direction.X > KINDA_SMALL_NUMBER)
		{
			Distance = FMath::Min(Distance, (HalfExtent - Point.X) / Direction.X);
		}
		else if (Direction.X < -KINDA_SMALL_NUMBER)
		{
			Distance = FMath::Min(Distance, (-HalfExtent - Point.X) / Direction.X);
		}

		if (Direction.Y > KINDA_SMALL_NUMBER)
		{
			Distance = FMath::Min(Distance, (HalfExtent - Point.Y) / Direction.Y);
		}
		else if (Direction.Y < -KINDA_SMALL_NUMBER)
		{
			Distance = FMath::Min(Distance, (-HalfExtent - Point.Y) / Direction.Y);
		}

		return Distance == TNumericLimits<float>::Max() ? 0.0f : FMath::Max(0.0f, Distance);
	}

	FVector ResolveQueueWorldLocation(const AGSMTile3D* Tile, const FVector2D& RelativeTileXY)
	{
		const float SafeMapScale = FMath::Max(Tile->GetRuntimeMapScale(), KINDA_SMALL_NUMBER);
		const FVector TileLocalOffset(RelativeTileXY.X * SafeMapScale, RelativeTileXY.Y * SafeMapScale, 0.0);
		return Tile->GetActorLocation() + Tile->GetActorTransform().TransformVector(TileLocalOffset);
	}
}

TArray<FVector> UGSMPiecePlacementBlueprintLibrary::CalculateCenteredQueueWorldLocations(
	const AGSMTile3D* Tile,
	int32 PieceCount,
	int32 MaxColumns,
	int32 MaxRows,
	float Spacing,
	float ArrangementAngleDegrees)
{
	TArray<FVector> WorldLocations;
	if (!IsValid(Tile) || PieceCount <= 0 || MaxColumns <= 0 || MaxRows <= 0)
	{
		return WorldLocations;
	}

	const int32 Capacity = static_cast<int32>(FMath::Min<int64>(
		static_cast<int64>(MaxColumns) * static_cast<int64>(MaxRows),
		MAX_int32));
	const int32 VisibleCount = FMath::Min(PieceCount, Capacity);
	const int32 LayoutColumns = FMath::Min(VisibleCount, MaxColumns);
	const int32 LayoutRows = FMath::Min(MaxRows, FMath::DivideAndRoundUp(VisibleCount, LayoutColumns));
	const float SafeSpacing = FMath::Max(0.0f, Spacing);
	WorldLocations.Reserve(VisibleCount);

	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const int32 RowIndex = Index / LayoutColumns;
		const int32 ColumnIndex = Index % LayoutColumns;
		const int32 FirstIndexInRow = RowIndex * LayoutColumns;
		const int32 RowItemCount = FMath::Min(LayoutColumns, VisibleCount - FirstIndexInRow);

		const FVector UnrotatedOffset(
			(static_cast<float>(ColumnIndex) - (static_cast<float>(RowItemCount) - 1.0f) * 0.5f) * SafeSpacing,
			(static_cast<float>(RowIndex) - (static_cast<float>(LayoutRows) - 1.0f) * 0.5f) * SafeSpacing,
			0.0f);
		const FVector RotatedOffset = UnrotatedOffset.RotateAngleAxis(ArrangementAngleDegrees, FVector::UpVector);
		WorldLocations.Add(ResolveQueueWorldLocation(Tile, FVector2D(RotatedOffset.X, RotatedOffset.Y)));
	}

	return WorldLocations;
}

TArray<FVector> UGSMPiecePlacementBlueprintLibrary::CalculateEdgeQueueWorldLocations(
	const AGSMTile3D* Tile,
	int32 PieceCount,
	EGSMTileQueueCorner StartCorner,
	EGSMTileQueueCorner EndCorner,
	EGSMTileQueueRowDirection RowDirection,
	int32 MaxPiecesPerRow,
	int32 MaxRows,
	float EdgeInset)
{
	TArray<FVector> WorldLocations;
	if (!IsValid(Tile) || PieceCount <= 0 || MaxPiecesPerRow <= 0 || MaxRows <= 0 || StartCorner == EndCorner)
	{
		return WorldLocations;
	}

	const FVector2D StartUnit = GetCornerUnitCoordinate(StartCorner);
	const FVector2D EndUnit = GetCornerUnitCoordinate(EndCorner);
	if (!FMath::IsNearlyEqual(StartUnit.X, EndUnit.X) && !FMath::IsNearlyEqual(StartUnit.Y, EndUnit.Y))
	{
		return WorldLocations;
	}

	const float SafeMapScale = FMath::Max(Tile->GetRuntimeMapScale(), KINDA_SMALL_NUMBER);
	const float UnscaledTileSize = Tile->GetRuntimeSquareTileSize() / SafeMapScale;
	const float HalfExtent = FMath::Max(0.0f, UnscaledTileSize * 0.5f - FMath::Max(0.0f, EdgeInset));
	const FVector2D Start = StartUnit * HalfExtent;
	const FVector2D End = EndUnit * HalfExtent;
	const FVector2D Direction = ResolveRowDirection(RowDirection, Start, End);
	const float MaxRowDistance = FMath::Min(
		FindDistanceToSquareBoundary(Start, Direction, HalfExtent),
		FindDistanceToSquareBoundary(End, Direction, HalfExtent));
	const int32 EffectiveRows = MaxRows > 1 && MaxRowDistance <= KINDA_SMALL_NUMBER ? 1 : MaxRows;
	const float RowStep = EffectiveRows > 1 ? MaxRowDistance / static_cast<float>(EffectiveRows - 1) : 0.0f;
	const int32 Capacity = static_cast<int32>(FMath::Min<int64>(
		static_cast<int64>(MaxPiecesPerRow) * static_cast<int64>(EffectiveRows),
		MAX_int32));
	const int32 VisibleCount = FMath::Min(PieceCount, Capacity);
	WorldLocations.Reserve(VisibleCount);

	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const int32 RowIndex = Index / MaxPiecesPerRow;
		const int32 ColumnIndex = Index % MaxPiecesPerRow;
		const float AlongRowAlpha = MaxPiecesPerRow > 1
			? static_cast<float>(ColumnIndex) / static_cast<float>(MaxPiecesPerRow - 1)
			: 0.0f;
		const FVector2D RelativeTileXY = FMath::Lerp(Start, End, AlongRowAlpha) + Direction * (RowStep * RowIndex);
		WorldLocations.Add(ResolveQueueWorldLocation(Tile, RelativeTileXY));
	}

	return WorldLocations;
}

bool UGSMPiecePlacementBlueprintLibrary::CalculateMapTerrainWorldZAtXY(
	const AGSMMap3D* Map3D,
	FVector WorldLocation,
	float HeightOffset,
	float& OutWorldZ)
{
	OutWorldZ = 0.0f;
	if (!IsValid(Map3D))
	{
		return false;
	}

	FVector SurfaceWorldLocation(WorldLocation.X, WorldLocation.Y, Map3D->GetActorLocation().Z);
	if (!Map3D->GetMapTerrainSurfaceWorldLocationAtWorldLocation(SurfaceWorldLocation, SurfaceWorldLocation))
	{
		return false;
	}

	OutWorldZ = SurfaceWorldLocation.Z + HeightOffset;
	return true;
}
