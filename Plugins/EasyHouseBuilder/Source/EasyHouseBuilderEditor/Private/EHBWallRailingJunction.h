#pragma once
#include "CoreMinimal.h"

namespace EHBWallRailingJunction
{
	inline bool CanTrimToWallFace(const FVector& BranchDirection, const FVector& WallDirection,
		double ColumnWidth, double WallThickness, double PostWidth, double FirstPostDistance)
	{
		const FVector Branch = BranchDirection.GetSafeNormal2D(), Axis = WallDirection.GetSafeNormal2D();
		const double Along = FMath::Abs(FVector::DotProduct(Branch, Axis));
		const double Across = FMath::Abs(FVector::CrossProduct(Branch, Axis).Z);
		// Centerline must meet the column's wall-normal face, not the adjacent wall.
		// The first visible square post must be completely outside the wall band.
		return Across > 0.001 && ColumnWidth * Across + 0.001 >= WallThickness * Along
			&& FirstPostDistance * Across > WallThickness * 0.5 + PostWidth * 0.5 * (Along + Across) + 0.01;
	}
	// In the wall's XY frame, the rail strip must cross the wall-thickness
	// boundary before reaching either edge of the inserted square column.
	// Strip half-width is measured perpendicular to the outgoing path.
	inline bool ClearsRetainedWall(const FVector& BranchDirection, const FVector& WallDirection,
		double ColumnWidth, double WallThickness, double RailThickness)
	{
		const FVector Branch = BranchDirection.GetSafeNormal2D();
		const FVector Axis = WallDirection.GetSafeNormal2D();
		if (Branch.IsNearlyZero() || Axis.IsNearlyZero()) return false;
		const double Along = FMath::Abs(FVector::DotProduct(Branch, Axis));
		const double Across = FMath::Abs(FVector::CrossProduct(Branch, Axis).Z);
		return ColumnWidth * Across >= WallThickness * Along + RailThickness + 0.01;
	}
}
