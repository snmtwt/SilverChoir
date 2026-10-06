// Copyright Epic Games, Inc. All Rights Reserved.

#include "Geometry/EHBRoomFillSolver.h"

#include "ThirdParty/clipper/clipper.h"

namespace
{
	constexpr double EHBRoomFillClipperScale = 1000.0;
	constexpr double EHBRoomFillMinArea = 100.0 * EHBRoomFillClipperScale * EHBRoomFillClipperScale;

	ClipperLib::IntPoint ToClipperPoint(const FVector& Point)
	{
		return ClipperLib::IntPoint(
			static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.X * EHBRoomFillClipperScale)),
			static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.Y * EHBRoomFillClipperScale)));
	}

	ClipperLib::Path ToClipperPath(const TArray<FVector>& Polygon)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector& Point : Polygon)
		{
			Result.push_back(ToClipperPoint(Point));
		}
		return Result;
	}

	TArray<FVector> FromClipperPath(const ClipperLib::Path& Path)
	{
		TArray<FVector> Result;
		Result.Reserve(static_cast<int32>(Path.size()));
		for (const ClipperLib::IntPoint& Point : Path)
		{
			Result.Add(FVector(
				static_cast<double>(Point.X) / EHBRoomFillClipperScale,
				static_cast<double>(Point.Y) / EHBRoomFillClipperScale,
				0.0));
		}
		return Result;
	}

	bool IsUsablePath(const ClipperLib::Path& Path)
	{
		return Path.size() >= 3
			&& FMath::Abs(static_cast<double>(ClipperLib::Area(Path))) >= EHBRoomFillMinArea;
	}

	bool DoesPathContainPoint(const ClipperLib::Path& Path, const ClipperLib::IntPoint& Point)
	{
		return ClipperLib::PointInPolygon(Point, Path) != 0;
	}

	bool DoesAnyDirectHoleContainPoint(const ClipperLib::PolyNode* Node, const ClipperLib::IntPoint& Point)
	{
		if (!Node)
		{
			return false;
		}

		for (const ClipperLib::PolyNode* ChildNode : Node->Childs)
		{
			if (ChildNode && ChildNode->IsHole() && DoesPathContainPoint(ChildNode->Contour, Point))
			{
				return true;
			}
		}
		return false;
	}

	bool DoesPathTouchOuterBoundary(
		const ClipperLib::Path& Path,
		ClipperLib::cInt MinX,
		ClipperLib::cInt MinY,
		ClipperLib::cInt MaxX,
		ClipperLib::cInt MaxY)
	{
		const ClipperLib::cInt Tolerance = static_cast<ClipperLib::cInt>(EHBRoomFillClipperScale);
		for (const ClipperLib::IntPoint& Point : Path)
		{
			if (FMath::Abs(static_cast<double>(Point.X - MinX)) <= Tolerance
				|| FMath::Abs(static_cast<double>(Point.X - MaxX)) <= Tolerance
				|| FMath::Abs(static_cast<double>(Point.Y - MinY)) <= Tolerance
				|| FMath::Abs(static_cast<double>(Point.Y - MaxY)) <= Tolerance)
			{
				return true;
			}
		}
		return false;
	}

	void FindContainingRegionNode(
		ClipperLib::PolyNode* Node,
		const ClipperLib::IntPoint& TargetPoint,
		ClipperLib::cInt MinX,
		ClipperLib::cInt MinY,
		ClipperLib::cInt MaxX,
		ClipperLib::cInt MaxY,
		ClipperLib::PolyNode*& BestNode,
		double& BestArea)
	{
		if (!Node)
		{
			return;
		}

		for (ClipperLib::PolyNode* ChildNode : Node->Childs)
		{
			if (!ChildNode)
			{
				continue;
			}

			if (!ChildNode->IsHole()
				&& IsUsablePath(ChildNode->Contour)
				&& !DoesPathTouchOuterBoundary(ChildNode->Contour, MinX, MinY, MaxX, MaxY)
				&& DoesPathContainPoint(ChildNode->Contour, TargetPoint)
				&& !DoesAnyDirectHoleContainPoint(ChildNode, TargetPoint))
			{
				const double Area = FMath::Abs(static_cast<double>(ClipperLib::Area(ChildNode->Contour)));
				if (Area < BestArea)
				{
					BestArea = Area;
					BestNode = ChildNode;
				}
			}

			FindContainingRegionNode(ChildNode, TargetPoint, MinX, MinY, MaxX, MaxY, BestNode, BestArea);
		}
	}
}

bool FEHBPlanarRoomFillSolver::BuildContainingFreeRegion(
	const TArray<TArray<FVector>>& ObstaclePolygons,
	const FVector& TargetPoint,
	FEHBPlanarRoomFillResult& OutResult)
{
	OutResult = FEHBPlanarRoomFillResult();

	FBox2D Bounds(EForceInit::ForceInit);
	Bounds += FVector2D(TargetPoint.X, TargetPoint.Y);

	ClipperLib::Paths ObstaclePaths;
	for (const TArray<FVector>& ObstaclePolygon : ObstaclePolygons)
	{
		if (ObstaclePolygon.Num() < 3)
		{
			continue;
		}

		for (const FVector& Point : ObstaclePolygon)
		{
			Bounds += FVector2D(Point.X, Point.Y);
		}

		ClipperLib::Path ObstaclePath = ToClipperPath(ObstaclePolygon);
		if (IsUsablePath(ObstaclePath))
		{
			ObstaclePaths.push_back(MoveTemp(ObstaclePath));
		}
	}

	if (ObstaclePaths.empty() || !Bounds.bIsValid)
	{
		return false;
	}

	const FVector2D BoundsSize = Bounds.GetSize();
	const double Expand = FMath::Max(2000.0, FMath::Max(BoundsSize.X, BoundsSize.Y) * 0.5 + 1000.0);
	const double OuterMinX = Bounds.Min.X - Expand;
	const double OuterMinY = Bounds.Min.Y - Expand;
	const double OuterMaxX = Bounds.Max.X + Expand;
	const double OuterMaxY = Bounds.Max.Y + Expand;

	ClipperLib::Path OuterPath;
	OuterPath.reserve(4);
	OuterPath.push_back(ToClipperPoint(FVector(OuterMinX, OuterMinY, 0.0)));
	OuterPath.push_back(ToClipperPoint(FVector(OuterMaxX, OuterMinY, 0.0)));
	OuterPath.push_back(ToClipperPoint(FVector(OuterMaxX, OuterMaxY, 0.0)));
	OuterPath.push_back(ToClipperPoint(FVector(OuterMinX, OuterMaxY, 0.0)));

	ClipperLib::Clipper DifferenceClipper;
	DifferenceClipper.StrictlySimple(true);
	DifferenceClipper.AddPath(OuterPath, ClipperLib::ptSubject, true);
	DifferenceClipper.AddPaths(ObstaclePaths, ClipperLib::ptClip, true);

	ClipperLib::PolyTree SolutionTree;
	if (!DifferenceClipper.Execute(ClipperLib::ctDifference, SolutionTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
	{
		return false;
	}

	const ClipperLib::IntPoint TargetClipperPoint = ToClipperPoint(TargetPoint);
	const ClipperLib::cInt MinX = OuterPath[0].X;
	const ClipperLib::cInt MinY = OuterPath[0].Y;
	const ClipperLib::cInt MaxX = OuterPath[2].X;
	const ClipperLib::cInt MaxY = OuterPath[2].Y;

	ClipperLib::PolyNode* BestNode = nullptr;
	double BestArea = TNumericLimits<double>::Max();
	FindContainingRegionNode(&SolutionTree, TargetClipperPoint, MinX, MinY, MaxX, MaxY, BestNode, BestArea);
	if (!BestNode)
	{
		return false;
	}

	OutResult.OuterPolygon = FromClipperPath(BestNode->Contour);
	for (ClipperLib::PolyNode* ChildNode : BestNode->Childs)
	{
		if (ChildNode && ChildNode->IsHole() && IsUsablePath(ChildNode->Contour))
		{
			OutResult.HolePolygons.Add(FromClipperPath(ChildNode->Contour));
		}
	}

	return OutResult.OuterPolygon.Num() >= 3;
}
