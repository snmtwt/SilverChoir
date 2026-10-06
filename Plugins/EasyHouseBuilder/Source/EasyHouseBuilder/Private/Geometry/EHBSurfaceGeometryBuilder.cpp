// Copyright Epic Games, Inc. All Rights Reserved.

#include "Geometry/EHBSurfaceGeometryTypes.h"

#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"

namespace
{
	constexpr double EHBSurfacePointTolerance = 0.01;

	bool ArePointsNearlyEqual2D(const FVector2d& A, const FVector2d& B, double PointTolerance)
	{
		const double DX = A.X - B.X;
		const double DY = A.Y - B.Y;
		return DX * DX + DY * DY <= PointTolerance * PointTolerance;
	}

	void InsertClosedLoop(UE::Geometry::FArrangement2d& Arrangement, const TArray<FVector2d>& Loop)
	{
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			if (!ArePointsNearlyEqual2D(A, B, EHBSurfacePointTolerance))
			{
				Arrangement.Insert(A, B);
			}
		}
	}

	FVector GetSafeHorizontalVector(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.0f).GetSafeNormal();
	}

	FVector GetLoopSideNormal(const FVector& Start, const FVector& End, bool bInnerSide, bool bLoopCounterClockwise)
	{
		const FVector Edge = GetSafeHorizontalVector(End - Start);
		if (Edge.IsNearlyZero())
		{
			return FVector::ForwardVector;
		}

		const FVector RightNormal = FVector::CrossProduct(Edge, FVector::UpVector).GetSafeNormal();
		const FVector LeftNormal = FVector::CrossProduct(FVector::UpVector, Edge).GetSafeNormal();
		if (bLoopCounterClockwise)
		{
			return bInnerSide ? LeftNormal : RightNormal;
		}
		return bInnerSide ? RightNormal : LeftNormal;
	}

	void AppendQuad(
		FEHBSurfaceMeshBuildResult& InOutResult,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		float UVWorldSize,
		bool bDoubleSided)
	{
		const int32 BaseIndex = InOutResult.Vertices.Num();
		InOutResult.Vertices.Append({ A, B, C, D });

		const FVector SafeNormal = DesiredNormal.GetSafeNormal();
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SafeNormal) <= 0.0f)
		{
			InOutResult.Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2, BaseIndex, BaseIndex + 2, BaseIndex + 3 });
		}
		else
		{
			InOutResult.Triangles.Append({ BaseIndex, BaseIndex + 2, BaseIndex + 1, BaseIndex, BaseIndex + 3, BaseIndex + 2 });
		}

		InOutResult.Normals.Append({ SafeNormal, SafeNormal, SafeNormal, SafeNormal });
		const float SafeUVWorldSize = FMath::Max(1.0f, UVWorldSize);
		const float EdgeU = FMath::Max(FVector::Dist2D(A, B), 1.0f) / SafeUVWorldSize;
		const float HeightV = FMath::Max((FMath::Abs(C.Z - B.Z) + FMath::Abs(D.Z - A.Z)) * 0.5f, 1.0f) / SafeUVWorldSize;
		InOutResult.UV0.Append({
			FVector2D(0.0f, HeightV),
			FVector2D(EdgeU, HeightV),
			FVector2D(EdgeU, 0.0f),
			FVector2D(0.0f, 0.0f)
		});

		if (!bDoubleSided)
		{
			return;
		}

		const int32 BackBaseIndex = InOutResult.Vertices.Num();
		InOutResult.Vertices.Append({ A, B, C, D });
		InOutResult.Triangles.Append({
			BackBaseIndex, BackBaseIndex + 2, BackBaseIndex + 1,
			BackBaseIndex, BackBaseIndex + 3, BackBaseIndex + 2
		});
		InOutResult.Normals.Append({ -SafeNormal, -SafeNormal, -SafeNormal, -SafeNormal });
		InOutResult.UV0.Append({
			FVector2D(0.0f, HeightV),
			FVector2D(EdgeU, HeightV),
			FVector2D(EdgeU, 0.0f),
			FVector2D(0.0f, 0.0f)
		});
	}
}

TArray<FVector2d> FEHBSurfaceGeometryUtil::To2DLoop(const TArray<FVector>& Loop, double PointTolerance)
{
	TArray<FVector2d> Result;
	Result.Reserve(Loop.Num());
	for (const FVector& Point : Loop)
	{
		const FVector2d Point2D(Point.X, Point.Y);
		if (Result.IsEmpty() || !ArePointsNearlyEqual2D(Result.Last(), Point2D, PointTolerance))
		{
			Result.Add(Point2D);
		}
	}
	if (Result.Num() >= 2 && ArePointsNearlyEqual2D(Result[0], Result.Last(), PointTolerance))
	{
		Result.Pop(EAllowShrinking::No);
	}
	return Result;
}

double FEHBSurfaceGeometryUtil::CalculateSignedArea2D(const TArray<FVector2d>& Loop)
{
	double Area = 0.0;
	for (int32 Index = 0; Index < Loop.Num(); ++Index)
	{
		const FVector2d& A = Loop[Index];
		const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
		Area += A.X * B.Y - B.X * A.Y;
	}
	return Area * 0.5;
}

double FEHBSurfaceGeometryUtil::CalculateSignedAreaXY(const TArray<FVector>& Loop)
{
	return CalculateSignedArea2D(To2DLoop(Loop));
}

bool FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(const FVector2d& Point, const TArray<FVector2d>& Polygon)
{
	if (Polygon.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
		CurrentIndex < Polygon.Num();
		PreviousIndex = CurrentIndex++)
	{
		const FVector2d& Current = Polygon[CurrentIndex];
		const FVector2d& Previous = Polygon[PreviousIndex];
		const bool bCrosses = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
		if (!bCrosses)
		{
			continue;
		}

		const double IntersectionX =
			(Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y) + Current.X;
		if (Point.X < IntersectionX)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

float FEHBSurfaceGeometryUtil::GetClosestAlphaOnSegmentXY(
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	const FVector& Point)
{
	const FVector Segment(SegmentEnd.X - SegmentStart.X, SegmentEnd.Y - SegmentStart.Y, 0.0f);
	const float SegmentLengthSquared = Segment.SizeSquared2D();
	if (SegmentLengthSquared <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const FVector ToPoint(Point.X - SegmentStart.X, Point.Y - SegmentStart.Y, 0.0f);
	return FMath::Clamp(FVector::DotProduct(ToPoint, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
}

bool FEHBSurfaceGeometryUtil::FindClosestBoundaryPointXY(
	const TArray<FVector>& LocalLoop,
	const FTransform& LocalToWorld,
	const FVector& WorldPoint,
	float LocalZ,
	bool bClosed,
	FEHBSurfaceBoundaryHit& OutHit)
{
	OutHit = FEHBSurfaceBoundaryHit();
	if (LocalLoop.Num() < 2)
	{
		return false;
	}

	const FVector LocalPoint = LocalToWorld.InverseTransformPosition(WorldPoint);
	const int32 SegmentCount = bClosed ? LocalLoop.Num() : LocalLoop.Num() - 1;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	int32 BestSegmentIndex = INDEX_NONE;
	float BestAlpha = 0.0f;
	FVector BestLocalPoint = FVector::ZeroVector;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector SegmentStart = LocalLoop[SegmentIndex];
		FVector SegmentEnd = LocalLoop[(SegmentIndex + 1) % LocalLoop.Num()];
		if (FVector::DistSquared2D(SegmentStart, SegmentEnd) <= UE_SMALL_NUMBER)
		{
			continue;
		}

		SegmentStart.Z = LocalZ;
		SegmentEnd.Z = LocalZ;
		const float Alpha = GetClosestAlphaOnSegmentXY(SegmentStart, SegmentEnd, LocalPoint);
		FVector ClosestPoint = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);
		ClosestPoint.Z = LocalZ;
		const float DistanceSquared = FVector::DistSquared2D(ClosestPoint, LocalPoint);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestSegmentIndex = SegmentIndex;
			BestAlpha = Alpha;
			BestLocalPoint = ClosestPoint;
		}
	}

	if (!LocalLoop.IsValidIndex(BestSegmentIndex))
	{
		return false;
	}

	const FVector EdgeStart = LocalLoop[BestSegmentIndex];
	const FVector EdgeEnd = LocalLoop[(BestSegmentIndex + 1) % LocalLoop.Num()];
	const FVector LocalTangent = GetSafeHorizontalVector(EdgeEnd - EdgeStart);
	if (LocalTangent.IsNearlyZero())
	{
		return false;
	}

	const bool bCounterClockwise = CalculateSignedAreaXY(LocalLoop) > 0.0;
	const FVector LocalNormal = bCounterClockwise
		? FVector(LocalTangent.Y, -LocalTangent.X, 0.0f)
		: FVector(-LocalTangent.Y, LocalTangent.X, 0.0f);

	OutHit.WorldPoint = LocalToWorld.TransformPosition(BestLocalPoint);
	OutHit.WorldTangent = GetSafeHorizontalVector(LocalToWorld.TransformVectorNoScale(LocalTangent));
	OutHit.WorldNormal = GetSafeHorizontalVector(LocalToWorld.TransformVectorNoScale(LocalNormal));
	OutHit.SegmentIndex = BestSegmentIndex;
	OutHit.SegmentAlpha = BestAlpha;
	OutHit.Distance = FVector::Distance(WorldPoint, OutHit.WorldPoint);
	return true;
}

bool FEHBPlanarSurfaceGeometryBuilder::BuildPlanarSurface(
	const FEHBPlanarSurfaceMeshBuildInput& Input,
	FEHBSurfaceMeshBuildResult& OutResult)
{
	OutResult.Reset();

	TArray<FVector2d> OuterLoop = FEHBSurfaceGeometryUtil::To2DLoop(Input.BoundaryLoop);
	if (OuterLoop.Num() < 3 || FMath::Abs(FEHBSurfaceGeometryUtil::CalculateSignedArea2D(OuterLoop)) <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	TArray<TArray<FVector2d>> HoleLoops;
	HoleLoops.Reserve(Input.HoleLoops.Num());
	for (const TArray<FVector>& Hole : Input.HoleLoops)
	{
		TArray<FVector2d> HoleLoop = FEHBSurfaceGeometryUtil::To2DLoop(Hole);
		if (HoleLoop.Num() >= 3 && FMath::Abs(FEHBSurfaceGeometryUtil::CalculateSignedArea2D(HoleLoop)) > UE_DOUBLE_SMALL_NUMBER)
		{
			HoleLoops.Add(MoveTemp(HoleLoop));
		}
	}

	FBox2d Bounds(ForceInit);
	for (const FVector2d& Point : OuterLoop)
	{
		Bounds += Point;
	}
	const double ArrangementTolerance = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) / 128.0;
	UE::Geometry::FArrangement2d Arrangement(FMath::Max(0.01, ArrangementTolerance));
	InsertClosedLoop(Arrangement, OuterLoop);
	for (const TArray<FVector2d>& HoleLoop : HoleLoops)
	{
		InsertClosedLoop(Arrangement, HoleLoop);
	}

	UE::Geometry::FConstrainedDelaunay2d Triangulator;
	Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
	Triangulator.bOrientedEdges = false;
	Triangulator.bSplitBowties = true;
	Triangulator.Add(Arrangement.Graph);

	const bool bSucceeded = Triangulator.Triangulate(
		[&OuterLoop, &HoleLoops](const TArray<FVector2d>& CandidateVertices, const UE::Geometry::FIndex3i& Triangle)
		{
			const FVector2d Centroid =
				(CandidateVertices[Triangle.A] + CandidateVertices[Triangle.B] + CandidateVertices[Triangle.C]) / 3.0;
			if (!FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(Centroid, OuterLoop))
			{
				return false;
			}
			for (const TArray<FVector2d>& HoleLoop : HoleLoops)
			{
				if (FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(Centroid, HoleLoop))
				{
					return false;
				}
			}
			return true;
		});

	if (!bSucceeded)
	{
		return false;
	}

	const FVector SurfaceNormal = Input.PlaneNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float SafeUVWorldSize = FMath::Max(1.0f, Input.UVWorldSize);
	for (const FVector2d& Vertex2D : Triangulator.Vertices)
	{
		OutResult.Vertices.Add(FVector(Vertex2D.X, Vertex2D.Y, Input.PlaneZ));
		OutResult.Normals.Add(SurfaceNormal);
		OutResult.UV0.Add(FVector2D(Vertex2D.X / SafeUVWorldSize, Vertex2D.Y / SafeUVWorldSize));
	}

	for (const UE::Geometry::FIndex3i& Triangle : Triangulator.Triangles)
	{
		int32 A = Triangle.A;
		int32 B = Triangle.B;
		int32 C = Triangle.C;
		const FVector TriangleNormal = FVector::CrossProduct(
			OutResult.Vertices[B] - OutResult.Vertices[A],
			OutResult.Vertices[C] - OutResult.Vertices[A]).GetSafeNormal();
		// GeneratedMesh follows UE's clockwise front-face convention, so the default
		// index winding has a mathematical cross product opposite to the vertex normal.
		const bool bTriangleCrossMatchesSurfaceNormal = FVector::DotProduct(TriangleNormal, SurfaceNormal) > 0.0f;
		const bool bShouldCrossMatchSurfaceNormal = Input.bFlipWinding;
		if (bTriangleCrossMatchesSurfaceNormal != bShouldCrossMatchSurfaceNormal)
		{
			Swap(B, C);
		}
		OutResult.Triangles.Append({ A, B, C });
	}

	return OutResult.IsValidMesh();
}

bool FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface(
	const FEHBSlabSurfaceMeshBuildInput& Input,
	FEHBSurfaceMeshBuildResult& OutResult)
{
	OutResult.Reset();
	if (Input.TopBoundaryLoop.Num() < 3)
	{
		return false;
	}

	if (Input.bBuildTop)
	{
		FEHBPlanarSurfaceMeshBuildInput TopInput;
		TopInput.BoundaryLoop = Input.TopBoundaryLoop;
		TopInput.HoleLoops = Input.TopHoleLoops;
		TopInput.PlaneZ = Input.TopZ;
		TopInput.PlaneNormal = FVector::UpVector;
		TopInput.UVWorldSize = Input.UVWorldSize;
		TopInput.bFlipWinding = false;

		FEHBSurfaceMeshBuildResult TopResult;
		if (FEHBPlanarSurfaceGeometryBuilder::BuildPlanarSurface(TopInput, TopResult))
		{
			OutResult.Vertices.Append(TopResult.Vertices);
			OutResult.Triangles.Append(TopResult.Triangles);
			OutResult.Normals.Append(TopResult.Normals);
			OutResult.UV0.Append(TopResult.UV0);
		}
	}

	const bool bOuterLoopCounterClockwise = FEHBSurfaceGeometryUtil::CalculateSignedAreaXY(Input.TopBoundaryLoop) > 0.0;
	if (Input.bBuildSides)
	{
		FEHBVerticalSurfaceGeometryBuilder::AppendSideLoop(
			Input.TopBoundaryLoop,
			Input.TopZ,
			Input.DefaultBottomZ,
			false,
			bOuterLoopCounterClockwise,
			Input.UVWorldSize,
			OutResult,
			Input.ResolveBottomZ,
			false,
			Input.SideSegmentLength);

		for (const TArray<FVector>& HoleLoop : Input.TopHoleLoops)
		{
			if (HoleLoop.Num() < 3)
			{
				continue;
			}
			const bool bHoleLoopCounterClockwise = FEHBSurfaceGeometryUtil::CalculateSignedAreaXY(HoleLoop) > 0.0;
			FEHBVerticalSurfaceGeometryBuilder::AppendSideLoop(
				HoleLoop,
				Input.TopZ,
				Input.DefaultBottomZ,
				true,
				bHoleLoopCounterClockwise,
				Input.UVWorldSize,
				OutResult,
				Input.ResolveBottomZ,
				Input.bDoubleSideInnerLoops,
				Input.SideSegmentLength);
		}
	}

	if (Input.bBuildBottom)
	{
		FEHBPlanarSurfaceMeshBuildInput BottomInput;
		BottomInput.BoundaryLoop = Input.TopBoundaryLoop;
		BottomInput.HoleLoops = Input.TopHoleLoops;
		BottomInput.PlaneZ = Input.DefaultBottomZ;
		BottomInput.PlaneNormal = FVector::DownVector;
		BottomInput.UVWorldSize = Input.UVWorldSize;
		BottomInput.bFlipWinding = false;

		FEHBSurfaceMeshBuildResult BottomResult;
		if (FEHBPlanarSurfaceGeometryBuilder::BuildPlanarSurface(BottomInput, BottomResult))
		{
			const int32 BaseIndex = OutResult.Vertices.Num();
			OutResult.Vertices.Append(BottomResult.Vertices);
			for (const int32 TriangleIndex : BottomResult.Triangles)
			{
				OutResult.Triangles.Add(BaseIndex + TriangleIndex);
			}
			OutResult.Normals.Append(BottomResult.Normals);
			OutResult.UV0.Append(BottomResult.UV0);
		}
	}

	return OutResult.IsValidMesh();
}

bool FEHBVerticalSurfaceGeometryBuilder::BuildVerticalSurface(
	const FEHBVerticalSurfaceMeshBuildInput& Input,
	FEHBSurfaceMeshBuildResult& OutResult)
{
	OutResult.Reset();
	if (Input.LocalBasePolyline.Num() < 2)
	{
		return false;
	}

	TArray<FVector> Loop = Input.LocalBasePolyline;
	if (!Input.bClosedLoop && Loop.Num() >= 2)
	{
		for (int32 Index = 0; Index + 1 < Loop.Num(); ++Index)
		{
			AppendSideLoop(
				{ Loop[Index], Loop[Index + 1] },
				Input.BottomOffset + Input.SurfaceHeight,
				Input.BottomOffset,
				false,
				true,
				Input.UVWorldSize,
				OutResult);
		}
	}
	else
	{
		const bool bCounterClockwise = FEHBSurfaceGeometryUtil::CalculateSignedAreaXY(Loop) > 0.0;
		AppendSideLoop(
			Loop,
			Input.BottomOffset + Input.SurfaceHeight,
			Input.BottomOffset,
			false,
			bCounterClockwise,
			Input.UVWorldSize,
			OutResult);
	}

	return OutResult.IsValidMesh();
}

void FEHBVerticalSurfaceGeometryBuilder::AppendSideLoop(
	const TArray<FVector>& Loop,
	float TopZ,
	float DefaultBottomZ,
	bool bInnerSide,
	bool bLoopCounterClockwise,
	float UVWorldSize,
	FEHBSurfaceMeshBuildResult& InOutResult,
	const TFunction<float(const FVector& LocalTopPoint)>& ResolveBottomZ,
	bool bDoubleSided,
	float SideSegmentLength)
{
	if (Loop.Num() < 2)
	{
		return;
	}

	const int32 EdgeCount = Loop.Num() == 2 ? 1 : Loop.Num();
	const float SafeSegmentLength = FMath::Max(1.0f, SideSegmentLength);
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeCount; ++EdgeIndex)
	{
		const FVector EdgeStart = Loop[EdgeIndex];
		const FVector EdgeEnd = Loop[(EdgeIndex + 1) % Loop.Num()];
		const float EdgeLength = FVector::Dist2D(EdgeStart, EdgeEnd);
		if (EdgeLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const int32 SegmentCount = ResolveBottomZ
			? FMath::Clamp(FMath::CeilToInt(EdgeLength / SafeSegmentLength), 1, 128)
			: 1;

		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			const float StartAlpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float EndAlpha = static_cast<float>(SegmentIndex + 1) / static_cast<float>(SegmentCount);
			FVector TopA = FMath::Lerp(EdgeStart, EdgeEnd, StartAlpha);
			FVector TopB = FMath::Lerp(EdgeStart, EdgeEnd, EndAlpha);
			TopA.Z = TopZ;
			TopB.Z = TopZ;

			FVector BottomA = TopA;
			FVector BottomB = TopB;
			BottomA.Z = ResolveBottomZ ? ResolveBottomZ(TopA) : DefaultBottomZ;
			BottomB.Z = ResolveBottomZ ? ResolveBottomZ(TopB) : DefaultBottomZ;

			const FVector Normal = GetLoopSideNormal(TopA, TopB, bInnerSide, bLoopCounterClockwise);
			AppendQuad(InOutResult, BottomA, BottomB, TopB, TopA, Normal, UVWorldSize, bInnerSide && bDoubleSided);
		}
	}
}
