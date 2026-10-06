#include "GridStrategyMapSystem/Data/GSMTypes.h"

namespace GSMTypes
{
	// 判断点是否落在线段上。边界也算范围内，避免鼠标在边界附近因为浮点误差反复进出。
	static bool IsPointOnSegment(const FVector2D& Point, const FVector2D& Start, const FVector2D& End, float Tolerance)
	{
		const FVector2D Segment = End - Start;
		const FVector2D ToPoint = Point - Start;
		const double SegmentLengthSquared = Segment.SizeSquared();

		if (SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::Distance(Point, Start) <= Tolerance;
		}

		const double Cross = FMath::Abs(static_cast<double>(Segment.X) * ToPoint.Y - static_cast<double>(Segment.Y) * ToPoint.X);
		const double DistanceToLine = Cross / FMath::Sqrt(SegmentLengthSquared);
		if (DistanceToLine > Tolerance)
		{
			return false;
		}

		const double Dot = FVector2D::DotProduct(ToPoint, Segment);
		return Dot >= -Tolerance && Dot <= SegmentLengthSquared + Tolerance;
	}
}

bool FGSMQuadBounds::ContainsPoint(const FVector2D& Point, float EdgeTolerance) const
{
	const FVector2D Corners[4] = { CornerA, CornerB, CornerC, CornerD };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2D& Start = Corners[Index];
		const FVector2D& End = Corners[(Index + 1) % 4];
		if (GSMTypes::IsPointOnSegment(Point, Start, End, EdgeTolerance))
		{
			return true;
		}
	}

	bool bInside = false;
	for (int32 Index = 0, PreviousIndex = 3; Index < 4; PreviousIndex = Index++)
	{
		const FVector2D& Current = Corners[Index];
		const FVector2D& Previous = Corners[PreviousIndex];

		const bool bCrossesY = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
		if (!bCrossesY)
		{
			continue;
		}

		const double Denominator = static_cast<double>(Previous.Y - Current.Y);
		if (FMath::IsNearlyZero(Denominator))
		{
			continue;
		}

		const double IntersectX =
			(static_cast<double>(Previous.X - Current.X) * (Point.Y - Current.Y)) /
			Denominator +
			Current.X;

		if (Point.X < IntersectX)
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

FBox2D FGSMQuadBounds::GetBoundsBox() const
{
	FBox2D Box(ForceInit);
	Box += CornerA;
	Box += CornerB;
	Box += CornerC;
	Box += CornerD;
	return Box;
}

void FGSMPathResult::Reset()
{
	bSuccess = false;
	TotalCost = 0.0f;
	PathTileIds.Reset();
	PathTiles.Reset();
	PathWorldLocations.Reset();
	PathLinkTypes.Reset();
	FailureReason = FText::GetEmpty();
}

namespace GSMLayout
{
	FString MakeColumnLabel(int32 ColumnIndex)
	{
		FString Label;
		int32 RemainingIndex = FMath::Max(0, ColumnIndex);

		do
		{
			const int32 LetterIndex = RemainingIndex % 26;
			Label.InsertAt(0, TCHAR('A' + LetterIndex));
			RemainingIndex = RemainingIndex / 26 - 1;
		}
		while (RemainingIndex >= 0);

		return Label;
	}

	FString MakeTileCoordinateLabel(FIntPoint GridCoordinate)
	{
		return MakeTileCoordinateLabel(
			GridCoordinate,
			EGSMCoordinateLabelType::Letters,
			EGSMCoordinateLabelType::Numbers
		);
	}

	FString MakeCoordinateLabel(int32 CoordinateIndex, EGSMCoordinateLabelType LabelType)
	{
		return LabelType == EGSMCoordinateLabelType::Letters
			? MakeColumnLabel(CoordinateIndex)
			: FString::FromInt(FMath::Max(0, CoordinateIndex) + 1);
	}

	FString MakeTileCoordinateLabel(
		FIntPoint GridCoordinate,
		EGSMCoordinateLabelType HorizontalLabelType,
		EGSMCoordinateLabelType VerticalLabelType)
	{
		return MakeCoordinateLabel(GridCoordinate.X, HorizontalLabelType)
			+ MakeCoordinateLabel(GridCoordinate.Y, VerticalLabelType);
	}

	void GetGridSizeFromTileEntries(
		const TArray<FGSMTileEntry>& TileEntries,
		int32 MinimumColumns,
		int32 MinimumRows,
		int32& OutColumns,
		int32& OutRows
	)
	{
		OutColumns = FMath::Max(1, MinimumColumns);
		OutRows = FMath::Max(1, MinimumRows);

		for (const FGSMTileEntry& TileEntry : TileEntries)
		{
			OutColumns = FMath::Max(OutColumns, TileEntry.GridCoordinate.X + 1);
			OutRows = FMath::Max(OutRows, TileEntry.GridCoordinate.Y + 1);
		}
	}

	FVector2D GetGridContentSize(int32 ColumnCount, int32 RowCount, float TileSize)
	{
		const float SafeTileSize = FMath::Max(1.0f, TileSize);
		return FVector2D(
			static_cast<float>(FMath::Max(1, ColumnCount)) * SafeTileSize,
			static_cast<float>(FMath::Max(1, RowCount)) * SafeTileSize
		);
	}

	FVector2D MakeTopLeftGridTileCenter2D(
		FIntPoint GridCoordinate,
		int32 ColumnCount,
		int32 RowCount,
		float TileSize
	)
	{
		const float SafeTileSize = FMath::Max(1.0f, TileSize);
		const float SafeColumnCount = static_cast<float>(FMath::Max(1, ColumnCount));
		const float SafeRowCount = static_cast<float>(FMath::Max(1, RowCount));

		return FVector2D(
			(SafeColumnCount * 0.5f - static_cast<float>(GridCoordinate.X) - 0.5f) * SafeTileSize,
			(SafeRowCount * 0.5f - static_cast<float>(GridCoordinate.Y) - 0.5f) * SafeTileSize
		);
	}

	FVector MakeTopLeftGridTileLocalLocation(
		FIntPoint GridCoordinate,
		int32 ColumnCount,
		int32 RowCount,
		float TileSize,
		float LocalZ
	)
	{
		const FVector2D Center = MakeTopLeftGridTileCenter2D(
			GridCoordinate,
			ColumnCount,
			RowCount,
			TileSize
		);
		return FVector(Center.X, Center.Y, LocalZ);
	}
}
