// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cutting/EHBMeshEnvelopeBuilder.h"

#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "ThirdParty/clipper/clipper.h"

#include <algorithm>

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

DEFINE_LOG_CATEGORY_STATIC(LogEHBEnvelope, Log, All);

namespace
{
	void SetFailure(FString* OutFailureReason, const FString& Reason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = Reason;
		}
	}

	FString FormatBox(const FBox& Box)
	{
		if (!Box.IsValid)
		{
			return TEXT("Invalid");
		}

		return FString::Printf(
			TEXT("Min=%s Max=%s Size=%s"),
			*Box.Min.ToString(),
			*Box.Max.ToString(),
			*Box.GetSize().ToString());
	}

	FBox CalculateDynamicMeshBounds(const FDynamicMesh3& Mesh)
	{
		FBox Bounds(ForceInit);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			Bounds += FVector(Mesh.GetVertex(VertexID));
		}
		return Bounds;
	}

	double GetSafeScale(const FEHBMeshEnvelopeBuildOptions& Options)
	{
		return FMath::Max(1.0, Options.ClipperScale);
	}

	ClipperLib::cInt ToClipperCoord(double Value, double Scale)
	{
		return static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Value * Scale));
	}

	ClipperLib::Path ToClipperPath(const TArray<FVector>& Polygon, double Scale)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector& Point : Polygon)
		{
			Result.push_back(ClipperLib::IntPoint(ToClipperCoord(Point.X, Scale), ToClipperCoord(Point.Y, Scale)));
		}
		return Result;
	}

	ClipperLib::Path ToClipperPath(const TArray<FVector2D>& Polygon, double Scale)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector2D& Point : Polygon)
		{
			Result.push_back(ClipperLib::IntPoint(ToClipperCoord(Point.X, Scale), ToClipperCoord(Point.Y, Scale)));
		}
		return Result;
	}

	TArray<FVector> FromClipperPath(const ClipperLib::Path& Path, double Scale)
	{
		TArray<FVector> Result;
		Result.Reserve(static_cast<int32>(Path.size()));
		for (const ClipperLib::IntPoint& Point : Path)
		{
			Result.Add(FVector(
				static_cast<double>(Point.X) / Scale,
				static_cast<double>(Point.Y) / Scale,
				0.0));
		}
		return Result;
	}

	double AbsPathArea(const ClipperLib::Path& Path)
	{
		return FMath::Abs(static_cast<double>(ClipperLib::Area(Path)));
	}

	double PathAreaWorld(const ClipperLib::Path& Path, double Scale)
	{
		return AbsPathArea(Path) / FMath::Square(Scale);
	}

	bool IsUsablePath(const ClipperLib::Path& Path, double Scale, double MinArea)
	{
		return Path.size() >= 3 && PathAreaWorld(Path, Scale) >= MinArea;
	}

	void NormalizePathOrientation(ClipperLib::Path& Path, bool bWantPositive)
	{
		if (Path.size() >= 3 && ClipperLib::Orientation(Path) != bWantPositive)
		{
			std::reverse(Path.begin(), Path.end());
		}
	}

	double SignedArea2D(const TArray<FVector2d>& Polygon)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2d& A = Polygon[Index];
			const FVector2d& B = Polygon[(Index + 1) % Polygon.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5;
	}

	bool IsPointInsidePolygon2D(const FVector2d& Point, const TArray<FVector2d>& Polygon)
	{
		bool bInside = false;
		for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
		{
			const FVector2d& A = Polygon[Index];
			const FVector2d& B = Polygon[Previous];
			const bool bCrosses = ((A.Y > Point.Y) != (B.Y > Point.Y))
				&& (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y + UE_DOUBLE_SMALL_NUMBER) + A.X);
			if (bCrosses)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	TArray<FVector2d> ToLoop2D(const TArray<FVector>& Loop)
	{
		TArray<FVector2d> Result;
		Result.Reserve(Loop.Num());
		for (const FVector& Point : Loop)
		{
			Result.Add(FVector2d(Point.X, Point.Y));
		}
		if (Result.Num() > 1 && (Result[0] - Result.Last()).SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
		{
			Result.Pop(EAllowShrinking::No);
		}
		return Result;
	}

	void NormalizeLoop(TArray<FVector2d>& Loop, bool bWantCounterClockwise)
	{
		if (Loop.Num() < 3)
		{
			return;
		}
		const bool bCounterClockwise = SignedArea2D(Loop) > 0.0;
		if (bCounterClockwise != bWantCounterClockwise)
		{
			Algo::Reverse(Loop);
		}
	}

	void InsertClosedLoop(UE::Geometry::FArrangement2d& Arrangement, const TArray<FVector2d>& Loop)
	{
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			if (!A.Equals(B, UE_DOUBLE_SMALL_NUMBER))
			{
				Arrangement.Insert(A, B);
			}
		}
	}

	void AppendTriangleWithNormal(FDynamicMesh3& Mesh, int32 A, int32 B, int32 C, const FVector3d& DesiredNormal)
	{
		const FVector3d VA = Mesh.GetVertex(A);
		const FVector3d VB = Mesh.GetVertex(B);
		const FVector3d VC = Mesh.GetVertex(C);
		const FVector3d Normal = FVector3d::CrossProduct(VB - VA, VC - VA).GetSafeNormal();
		if (!Normal.IsZero() && Normal.Dot(DesiredNormal) < 0.0)
		{
			Swap(B, C);
		}
		Mesh.AppendTriangle(A, B, C);
	}

	void AppendQuadWithNormal(
		FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3d& DesiredNormal)
	{
		const int32 AID = Mesh.AppendVertex(A);
		const int32 BID = Mesh.AppendVertex(B);
		const int32 CID = Mesh.AppendVertex(C);
		const int32 DID = Mesh.AppendVertex(D);
		AppendTriangleWithNormal(Mesh, AID, BID, CID, DesiredNormal);
		AppendTriangleWithNormal(Mesh, AID, CID, DID, DesiredNormal);
	}

	void AppendRegionsFromOuterNode(const ClipperLib::PolyNode* OuterNode, double Scale, TArray<FEHBPolygonRegion>& OutRegions);

	void AppendRegionsFromNode(const ClipperLib::PolyNode* Node, double Scale, TArray<FEHBPolygonRegion>& OutRegions)
	{
		if (!Node)
		{
			return;
		}

		for (const ClipperLib::PolyNode* Child : Node->Childs)
		{
			if (!Child)
			{
				continue;
			}

			if (Child->IsHole())
			{
				AppendRegionsFromNode(Child, Scale, OutRegions);
			}
			else
			{
				AppendRegionsFromOuterNode(Child, Scale, OutRegions);
			}
		}
	}

	void AppendRegionsFromOuterNode(const ClipperLib::PolyNode* OuterNode, double Scale, TArray<FEHBPolygonRegion>& OutRegions)
	{
		if (!OuterNode || OuterNode->IsHole() || !IsUsablePath(OuterNode->Contour, Scale, UE_DOUBLE_SMALL_NUMBER))
		{
			return;
		}

		FEHBPolygonRegion& Region = OutRegions.AddDefaulted_GetRef();
		Region.OuterLoop = FromClipperPath(OuterNode->Contour, Scale);

		for (const ClipperLib::PolyNode* HoleNode : OuterNode->Childs)
		{
			if (!HoleNode)
			{
				continue;
			}

			if (HoleNode->IsHole() && IsUsablePath(HoleNode->Contour, Scale, UE_DOUBLE_SMALL_NUMBER))
			{
				FEHBCutPolygon& Hole = Region.HoleLoops.AddDefaulted_GetRef();
				const TArray<FVector> HolePoints = FromClipperPath(HoleNode->Contour, Scale);
				Hole.Points.Reserve(HolePoints.Num());
				for (const FVector& HolePoint : HolePoints)
				{
					FEHBCutPolygonPoint& Point = Hole.Points.AddDefaulted_GetRef();
					Point.PointGuid = FGuid::NewGuid();
					Point.LocalPosition = HolePoint;
				}
			}

			for (const ClipperLib::PolyNode* IslandNode : HoleNode->Childs)
			{
				if (IslandNode && !IslandNode->IsHole())
				{
					AppendRegionsFromOuterNode(IslandNode, Scale, OutRegions);
				}
			}
		}
	}

	bool UnionPathsToRegions(
		ClipperLib::Paths Paths,
		double Scale,
		double MinArea,
		bool bNormalizeAllAsFilled,
		TArray<FEHBPolygonRegion>& OutRegions,
		FString* OutFailureReason)
	{
		OutRegions.Reset();
		if (Paths.empty())
		{
			SetFailure(OutFailureReason, TEXT("Envelope has no projected paths"));
			return false;
		}

		ClipperLib::Paths CleanPaths;
		CleanPaths.reserve(Paths.size());
		for (ClipperLib::Path& Path : Paths)
		{
			if (Path.size() < 3)
			{
				continue;
			}
			if (bNormalizeAllAsFilled)
			{
				NormalizePathOrientation(Path, true);
			}
			if (!IsUsablePath(Path, Scale, UE_DOUBLE_SMALL_NUMBER))
			{
				continue;
			}
			CleanPaths.push_back(Path);
		}

		if (CleanPaths.empty())
		{
			SetFailure(OutFailureReason, TEXT("Envelope projected paths are degenerate"));
			return false;
		}

		ClipperLib::Clipper Clipper;
		Clipper.PreserveCollinear(true);
		Clipper.AddPaths(CleanPaths, ClipperLib::ptSubject, true);

		ClipperLib::PolyTree SolutionTree;
		if (!Clipper.Execute(ClipperLib::ctUnion, SolutionTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
		{
			SetFailure(OutFailureReason, TEXT("Envelope path union failed"));
			return false;
		}

		AppendRegionsFromNode(&SolutionTree, Scale, OutRegions);
		OutRegions.RemoveAll(
			[Scale, MinArea](const FEHBPolygonRegion& Region)
			{
				const ClipperLib::Path OuterPath = ToClipperPath(Region.OuterLoop, Scale);
				return !IsUsablePath(OuterPath, Scale, MinArea);
			});

		if (OutRegions.IsEmpty())
		{
			SetFailure(OutFailureReason, TEXT("Envelope union produced no usable regions"));
			return false;
		}
		return true;
	}

	ClipperLib::Paths RegionsToPaths(const TArray<FEHBPolygonRegion>& Regions, double Scale)
	{
		ClipperLib::Paths Paths;
		for (const FEHBPolygonRegion& Region : Regions)
		{
			ClipperLib::Path OuterPath = ToClipperPath(Region.OuterLoop, Scale);
			if (OuterPath.size() >= 3)
			{
				NormalizePathOrientation(OuterPath, true);
				Paths.push_back(MoveTemp(OuterPath));
			}

			for (const FEHBCutPolygon& Hole : Region.HoleLoops)
			{
				TArray<FVector> HoleLoop;
				HoleLoop.Reserve(Hole.Points.Num());
				for (const FEHBCutPolygonPoint& Point : Hole.Points)
				{
					HoleLoop.Add(Point.LocalPosition);
				}

				ClipperLib::Path HolePath = ToClipperPath(HoleLoop, Scale);
				if (HolePath.size() >= 3)
				{
					NormalizePathOrientation(HolePath, false);
					Paths.push_back(MoveTemp(HolePath));
				}
			}
		}
		return Paths;
	}

	bool OffsetRegions(
		const TArray<FEHBPolygonRegion>& InRegions,
		double OffsetDistance,
		double Scale,
		double MinArea,
		TArray<FEHBPolygonRegion>& OutRegions,
		FString* OutFailureReason)
	{
		OutRegions.Reset();
		if (InRegions.IsEmpty())
		{
			SetFailure(OutFailureReason, TEXT("Cannot offset empty envelope regions"));
			return false;
		}

		ClipperLib::Paths SourcePaths = RegionsToPaths(InRegions, Scale);
		if (SourcePaths.empty())
		{
			SetFailure(OutFailureReason, TEXT("Envelope offset has no source paths"));
			return false;
		}

		ClipperLib::ClipperOffset Offsetter;
		Offsetter.MiterLimit = 4.0;
		Offsetter.AddPaths(SourcePaths, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);

		ClipperLib::Paths OffsetPaths;
		Offsetter.Execute(OffsetPaths, OffsetDistance * Scale);
		if (OffsetPaths.empty())
		{
			SetFailure(OutFailureReason, TEXT("Envelope offset produced no paths"));
			return false;
		}

		return UnionPathsToRegions(OffsetPaths, Scale, MinArea, false, OutRegions, OutFailureReason);
	}

	void AppendFallbackBoundsPath(const FBox& Bounds, double Scale, ClipperLib::Paths& OutPaths)
	{
		if (!Bounds.IsValid)
		{
			return;
		}

		TArray<FVector2D> Polygon;
		Polygon.Add(FVector2D(Bounds.Min.X, Bounds.Min.Y));
		Polygon.Add(FVector2D(Bounds.Max.X, Bounds.Min.Y));
		Polygon.Add(FVector2D(Bounds.Max.X, Bounds.Max.Y));
		Polygon.Add(FVector2D(Bounds.Min.X, Bounds.Max.Y));
		ClipperLib::Path Path = ToClipperPath(Polygon, Scale);
		NormalizePathOrientation(Path, true);
		OutPaths.push_back(MoveTemp(Path));
	}

	void AppendThinTriangleFallbackPath(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		float Width,
		double Scale,
		ClipperLib::Paths& OutPaths)
	{
		if (Width <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector2D Points[3] = {
			FVector2D(A.X, A.Y),
			FVector2D(B.X, B.Y),
			FVector2D(C.X, C.Y)
		};

		int32 BestA = INDEX_NONE;
		int32 BestB = INDEX_NONE;
		double BestLengthSq = 0.0;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const int32 NextIndex = (Index + 1) % 3;
			const double LengthSq = FVector2D::DistSquared(Points[Index], Points[NextIndex]);
			if (LengthSq > BestLengthSq)
			{
				BestLengthSq = LengthSq;
				BestA = Index;
				BestB = NextIndex;
			}
		}

		if (BestA == INDEX_NONE || BestLengthSq <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}

		const FVector2D Start = Points[BestA];
		const FVector2D End = Points[BestB];
		const FVector2D Direction = (End - Start).GetSafeNormal();
		const FVector2D Side(-Direction.Y, Direction.X);
		const FVector2D HalfSide = Side * Width * 0.5f;

		TArray<FVector2D> Polygon;
		Polygon.Add(Start - HalfSide);
		Polygon.Add(End - HalfSide);
		Polygon.Add(End + HalfSide);
		Polygon.Add(Start + HalfSide);

		ClipperLib::Path Path = ToClipperPath(Polygon, Scale);
		NormalizePathOrientation(Path, true);
		OutPaths.push_back(MoveTemp(Path));
	}

	bool BuildProjectionPathsFromAggregates(
		const TArray<FEHBMeshAggregateData>& Aggregates,
		const FEHBMeshEnvelopeBuildOptions& Options,
		ClipperLib::Paths& OutPaths,
		FBox& OutBounds,
		FString* OutFailureReason)
	{
		OutPaths.clear();
		OutBounds.Init();

		const double Scale = GetSafeScale(Options);
		for (const FEHBMeshAggregateData& Aggregate : Aggregates)
		{
			for (const FVector& Vertex : Aggregate.Vertices)
			{
				OutBounds += Vertex;
			}

			for (const FEHBMeshTriangleRef& Triangle : Aggregate.Triangles)
			{
				if (!Aggregate.Vertices.IsValidIndex(Triangle.VertexA)
					|| !Aggregate.Vertices.IsValidIndex(Triangle.VertexB)
					|| !Aggregate.Vertices.IsValidIndex(Triangle.VertexC))
				{
					continue;
				}

				const FVector& A = Aggregate.Vertices[Triangle.VertexA];
				const FVector& B = Aggregate.Vertices[Triangle.VertexB];
				const FVector& C = Aggregate.Vertices[Triangle.VertexC];

				ClipperLib::Path Path;
				Path.push_back(ClipperLib::IntPoint(ToClipperCoord(A.X, Scale), ToClipperCoord(A.Y, Scale)));
				Path.push_back(ClipperLib::IntPoint(ToClipperCoord(B.X, Scale), ToClipperCoord(B.Y, Scale)));
				Path.push_back(ClipperLib::IntPoint(ToClipperCoord(C.X, Scale), ToClipperCoord(C.Y, Scale)));
				if (IsUsablePath(Path, Scale, UE_DOUBLE_SMALL_NUMBER))
				{
					NormalizePathOrientation(Path, true);
					OutPaths.push_back(MoveTemp(Path));
				}
				else
				{
					AppendThinTriangleFallbackPath(A, B, C, Options.ThinProjectionFallbackWidth, Scale, OutPaths);
				}
			}
		}

		if (OutPaths.empty())
		{
			AppendFallbackBoundsPath(OutBounds, Scale, OutPaths);
		}

		if (OutPaths.empty())
		{
			SetFailure(OutFailureReason, TEXT("No source geometry could be projected for envelope"));
			return false;
		}
		return true;
	}

	bool TriangulateRegion(
		const FEHBPolygonRegion& Region,
		TArray<FVector2d>& OutVertices,
		TArray<FIndex3i>& OutTriangles,
		TArray<FVector2d>& OutOuterLoop,
		TArray<TArray<FVector2d>>& OutHoleLoops)
	{
		OutVertices.Reset();
		OutTriangles.Reset();
		OutOuterLoop = ToLoop2D(Region.OuterLoop);
		if (OutOuterLoop.Num() < 3 || FMath::Abs(SignedArea2D(OutOuterLoop)) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		NormalizeLoop(OutOuterLoop, true);

		OutHoleLoops.Reset();
		OutHoleLoops.Reserve(Region.HoleLoops.Num());
		for (const FEHBCutPolygon& Hole : Region.HoleLoops)
		{
			TArray<FVector> HolePoints;
			HolePoints.Reserve(Hole.Points.Num());
			for (const FEHBCutPolygonPoint& Point : Hole.Points)
			{
				HolePoints.Add(Point.LocalPosition);
			}

			TArray<FVector2d> HoleLoop = ToLoop2D(HolePoints);
			if (HoleLoop.Num() >= 3 && FMath::Abs(SignedArea2D(HoleLoop)) > UE_DOUBLE_SMALL_NUMBER)
			{
				NormalizeLoop(HoleLoop, false);
				OutHoleLoops.Add(MoveTemp(HoleLoop));
			}
		}

		FBox2d Bounds(ForceInit);
		for (const FVector2d& Point : OutOuterLoop)
		{
			Bounds += Point;
		}

		const double ArrangementTolerance = FMath::Max(0.01, FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) / 1024.0);
		UE::Geometry::FArrangement2d Arrangement(ArrangementTolerance);
		InsertClosedLoop(Arrangement, OutOuterLoop);
		for (const TArray<FVector2d>& HoleLoop : OutHoleLoops)
		{
			InsertClosedLoop(Arrangement, HoleLoop);
		}

		UE::Geometry::FConstrainedDelaunay2d Triangulator;
		Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
		Triangulator.bOrientedEdges = false;
		Triangulator.bSplitBowties = true;
		Triangulator.Add(Arrangement.Graph);

		const bool bSucceeded = Triangulator.Triangulate(
			[&OutOuterLoop, &OutHoleLoops](const TArray<FVector2d>& CandidateVertices, const FIndex3i& Triangle)
			{
				const FVector2d Centroid =
					(CandidateVertices[Triangle.A] + CandidateVertices[Triangle.B] + CandidateVertices[Triangle.C]) / 3.0;
				if (!IsPointInsidePolygon2D(Centroid, OutOuterLoop))
				{
					return false;
				}

				for (const TArray<FVector2d>& HoleLoop : OutHoleLoops)
				{
					if (IsPointInsidePolygon2D(Centroid, HoleLoop))
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

		OutVertices = Triangulator.Vertices;
		OutTriangles = Triangulator.Triangles;
		return OutVertices.Num() >= 3 && OutTriangles.Num() > 0;
	}

	void AppendLoopSides(FDynamicMesh3& Mesh, const TArray<FVector2d>& Loop, double MinZ, double MaxZ)
	{
		if (Loop.Num() < 3)
		{
			return;
		}

		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A2 = Loop[Index];
			const FVector2d& B2 = Loop[(Index + 1) % Loop.Num()];
			const FVector2d Edge = B2 - A2;
			if (Edge.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2d RightNormal2D(Edge.Y, -Edge.X);
			const FVector3d DesiredNormal = FVector3d(RightNormal2D.X, RightNormal2D.Y, 0.0).GetSafeNormal();
			AppendQuadWithNormal(
				Mesh,
				FVector3d(A2.X, A2.Y, MinZ),
				FVector3d(B2.X, B2.Y, MinZ),
				FVector3d(B2.X, B2.Y, MaxZ),
				FVector3d(A2.X, A2.Y, MaxZ),
				DesiredNormal);
		}
	}

	bool BuildExtrudedRegionMesh(const FEHBPolygonRegion& Region, double MinZ, double MaxZ, FDynamicMesh3& OutMesh)
	{
		OutMesh.Clear();
		if (MaxZ <= MinZ)
		{
			return false;
		}

		TArray<FVector2d> Vertices2D;
		TArray<FIndex3i> Triangles2D;
		TArray<FVector2d> OuterLoop;
		TArray<TArray<FVector2d>> HoleLoops;
		if (!TriangulateRegion(Region, Vertices2D, Triangles2D, OuterLoop, HoleLoops))
		{
			return false;
		}

		TArray<int32> BottomVertexIDs;
		TArray<int32> TopVertexIDs;
		BottomVertexIDs.Reserve(Vertices2D.Num());
		TopVertexIDs.Reserve(Vertices2D.Num());
		for (const FVector2d& Point : Vertices2D)
		{
			BottomVertexIDs.Add(OutMesh.AppendVertex(FVector3d(Point.X, Point.Y, MinZ)));
			TopVertexIDs.Add(OutMesh.AppendVertex(FVector3d(Point.X, Point.Y, MaxZ)));
		}

		for (const FIndex3i& Triangle : Triangles2D)
		{
			if (!BottomVertexIDs.IsValidIndex(Triangle.A)
				|| !BottomVertexIDs.IsValidIndex(Triangle.B)
				|| !BottomVertexIDs.IsValidIndex(Triangle.C))
			{
				continue;
			}

			AppendTriangleWithNormal(
				OutMesh,
				TopVertexIDs[Triangle.A],
				TopVertexIDs[Triangle.B],
				TopVertexIDs[Triangle.C],
				FVector3d::UnitZ());
			AppendTriangleWithNormal(
				OutMesh,
				BottomVertexIDs[Triangle.A],
				BottomVertexIDs[Triangle.B],
				BottomVertexIDs[Triangle.C],
				-FVector3d::UnitZ());
		}

		AppendLoopSides(OutMesh, OuterLoop, MinZ, MaxZ);
		for (const TArray<FVector2d>& HoleLoop : HoleLoops)
		{
			AppendLoopSides(OutMesh, HoleLoop, MinZ, MaxZ);
		}

		return OutMesh.TriangleCount() > 0;
	}
}

bool FEHBMeshEnvelopeBuilder::BuildProjectedEnvelopeRegions(
	const TArray<FEHBMeshAggregateData>& Aggregates,
	const FEHBMeshEnvelopeBuildOptions& Options,
	TArray<FEHBPolygonRegion>& OutRegions,
	FBox& OutSourceBounds,
	FString* OutFailureReason)
{
	OutRegions.Reset();
	OutSourceBounds.Init();
	if (Aggregates.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("No source aggregates supplied"));
		return false;
	}

	const double Scale = GetSafeScale(Options);
	ClipperLib::Paths ProjectionPaths;
	if (!BuildProjectionPathsFromAggregates(Aggregates, Options, ProjectionPaths, OutSourceBounds, OutFailureReason))
	{
		return false;
	}

	UE_LOG(
		LogEHBEnvelope,
		Verbose,
		TEXT("[EHB Envelope] Projected source aggregates=%d projectionPaths=%d sourceBounds=%s bridge=%.2f padding=%.2f zPadding=%.2f"),
		Aggregates.Num(),
		static_cast<int32>(ProjectionPaths.size()),
		*FormatBox(OutSourceBounds),
		Options.ConcavityBridgeDistance,
		Options.ProjectionPadding,
		Options.ZPadding);

	TArray<FEHBPolygonRegion> Regions;
	if (!UnionPathsToRegions(
			ProjectionPaths,
			Scale,
			FMath::Max(0.0f, Options.MinProjectedArea),
			true,
			Regions,
			OutFailureReason))
	{
		return false;
	}

	UE_LOG(
		LogEHBEnvelope,
		Verbose,
		TEXT("[EHB Envelope] Initial union regions=%d"),
		Regions.Num());

	const double BridgeDistance = FMath::Max(0.0f, Options.ConcavityBridgeDistance) * 0.5;
	if (BridgeDistance > UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		TArray<FEHBPolygonRegion> ExpandedRegions;
		if (OffsetRegions(Regions, BridgeDistance, Scale, Options.MinProjectedArea, ExpandedRegions, OutFailureReason))
		{
			TArray<FEHBPolygonRegion> ClosedRegions;
			if (OffsetRegions(ExpandedRegions, -BridgeDistance, Scale, Options.MinProjectedArea, ClosedRegions, OutFailureReason))
			{
				UE_LOG(
					LogEHBEnvelope,
					Verbose,
					TEXT("[EHB Envelope] Bridge concavity distance=%.2f expandedRegions=%d closedRegions=%d"),
					BridgeDistance * 2.0,
					ExpandedRegions.Num(),
					ClosedRegions.Num());
				Regions = MoveTemp(ClosedRegions);
			}
		}
	}

	const double Padding = FMath::Max(0.0f, Options.ProjectionPadding);
	if (Padding > UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		TArray<FEHBPolygonRegion> PaddedRegions;
		if (OffsetRegions(Regions, Padding, Scale, Options.MinProjectedArea, PaddedRegions, OutFailureReason))
		{
			UE_LOG(
				LogEHBEnvelope,
				Verbose,
				TEXT("[EHB Envelope] Padding projection distance=%.2f regionsBefore=%d regionsAfter=%d"),
				Padding,
				Regions.Num(),
				PaddedRegions.Num());
			Regions = MoveTemp(PaddedRegions);
		}
	}

	OutRegions = MoveTemp(Regions);
	UE_LOG(LogEHBEnvelope, Verbose, TEXT("[EHB Envelope] Projected envelope finalRegions=%d"), OutRegions.Num());
	return !OutRegions.IsEmpty();
}

bool FEHBMeshEnvelopeBuilder::BuildEnvelopeMeshes(
	const TArray<FEHBMeshAggregateData>& Aggregates,
	const FEHBMeshEnvelopeBuildOptions& Options,
	TArray<FDynamicMesh3>& OutMeshes,
	FString* OutFailureReason)
{
	OutMeshes.Reset();

	TArray<FEHBPolygonRegion> Regions;
	FBox SourceBounds(ForceInit);
	if (!BuildProjectedEnvelopeRegions(Aggregates, Options, Regions, SourceBounds, OutFailureReason))
	{
		return false;
	}

	if (!SourceBounds.IsValid)
	{
		SetFailure(OutFailureReason, TEXT("Envelope source bounds are invalid"));
		return false;
	}

	double MinZ = SourceBounds.Min.Z - FMath::Max(0.0f, Options.ZPadding);
	double MaxZ = SourceBounds.Max.Z + FMath::Max(0.0f, Options.ZPadding);
	const double MinHeight = FMath::Max(1.0f, Options.MinExtrudeHeight);
	if (MaxZ - MinZ < MinHeight)
	{
		const double CenterZ = (MinZ + MaxZ) * 0.5;
		MinZ = CenterZ - MinHeight * 0.5;
		MaxZ = CenterZ + MinHeight * 0.5;
	}

	UE_LOG(
		LogEHBEnvelope,
		Verbose,
		TEXT("[EHB Envelope] Extrude regions=%d minZ=%.2f maxZ=%.2f sourceBounds=%s"),
		Regions.Num(),
		MinZ,
		MaxZ,
		*FormatBox(SourceBounds));

	OutMeshes.Reserve(Regions.Num());
	for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
	{
		const FEHBPolygonRegion& Region = Regions[RegionIndex];
		FDynamicMesh3& Mesh = OutMeshes.AddDefaulted_GetRef();
		if (!BuildExtrudedRegionMesh(Region, MinZ, MaxZ, Mesh))
		{
			UE_LOG(
				LogEHBEnvelope,
				Warning,
				TEXT("[EHB Envelope] Extrude region failed region=%d outerPoints=%d holes=%d"),
				RegionIndex,
				Region.OuterLoop.Num(),
				Region.HoleLoops.Num());
			OutMeshes.Pop(EAllowShrinking::No);
		}
		else
		{
			UE_LOG(
				LogEHBEnvelope,
				Verbose,
				TEXT("[EHB Envelope] Extrude region success region=%d outerPoints=%d holes=%d triangles=%d bounds=%s"),
				RegionIndex,
				Region.OuterLoop.Num(),
				Region.HoleLoops.Num(),
				Mesh.TriangleCount(),
				*FormatBox(CalculateDynamicMeshBounds(Mesh)));
		}
	}

	if (OutMeshes.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("Envelope extrusion produced no meshes"));
		return false;
	}
	return true;
}
