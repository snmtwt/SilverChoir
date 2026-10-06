#pragma once
#include "Layout/Geometry.h"

/** Snap inward to the physical pixel grid, after DPI and parent render transforms. */
struct FPixelAlignedButtonFrame
{
	FVector2f Min;
	FVector2f Max;
	FVector2f Thickness;

	static FPixelAlignedButtonFrame Make(const FGeometry& Geometry, float LogicalThickness)
	{
		const FVector2D Origin = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D X(TransformVector(Geometry.GetAccumulatedRenderTransform(), FVector2f(1, 0)));
		const FVector2D Y(TransformVector(Geometry.GetAccumulatedRenderTransform(), FVector2f(0, 1)));
		const float ScaleX = FMath::Max(static_cast<float>(X.Size()), 0.001f);
		const float ScaleY = FMath::Max(static_cast<float>(Y.Size()), 0.001f);
		FPixelAlignedButtonFrame Result { FVector2f::ZeroVector, FVector2f(Geometry.GetLocalSize()),
			FVector2f(FMath::Max(1.0f, FMath::RoundToFloat(LogicalThickness * ScaleX)) / ScaleX,
				FMath::Max(1.0f, FMath::RoundToFloat(LogicalThickness * ScaleY)) / ScaleY) };
		if (X.X > 0 && Y.Y > 0 && FMath::IsNearlyZero(X.Y) && FMath::IsNearlyZero(Y.X))
		{
			const FVector2D End = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
			Result.Min = FVector2f(Geometry.AbsoluteToLocal(FVector2D(FMath::CeilToDouble(Origin.X), FMath::CeilToDouble(Origin.Y))));
			Result.Max = FVector2f(Geometry.AbsoluteToLocal(FVector2D(FMath::FloorToDouble(End.X), FMath::FloorToDouble(End.Y))));
		}
		return Result;
	}
};
