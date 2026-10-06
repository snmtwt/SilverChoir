// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cutting/EHBPolygonClipper.h"

#include "ThirdParty/clipper/clipper.h"

#include <algorithm>
#include <numeric>

namespace
{
	enum class EEHBClipProjection : uint8
	{
		XY,
		XZ
	};

	ClipperLib::Path ToClipperPath(const TArray<FVector>& Polygon, EEHBClipProjection Projection, double Scale)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector& Point : Polygon)
		{
			const double U = Point.X;
			const double V = Projection == EEHBClipProjection::XY ? Point.Y : Point.Z;
			Result.push_back(ClipperLib::IntPoint(
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(U * Scale)),
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(V * Scale))));
		}
		return Result;
	}

	TArray<FVector> FromClipperPath(const ClipperLib::Path& Path, EEHBClipProjection Projection, double Scale, float ConstantCoordinate)
	{
		TArray<FVector> Result;
		// PolyTree joins can leave a zero-area, backtracking spur on a hole.
        // Remove only duplicate/reversing collinear grid points, retaining
        // ordinary collinear subdivisions and all nonzero-area corners.
        ClipperLib::Path Clean=Path;bool Changed=true;
        while(Changed && Clean.size()>=3)
        {
            Changed=false;
            for(size_t I=0;I<Clean.size();++I)
            {
                const auto& P=Clean[(I+Clean.size()-1)%Clean.size()];const auto& Q=Clean[I];const auto& R=Clean[(I+1)%Clean.size()];
                const auto AX=P.X-Q.X,AY=P.Y-Q.Y,BX=R.X-Q.X,BY=R.Y-Q.Y;
                const auto GA=std::gcd(AX,AY),GB=std::gcd(BX,BY);
                // Exact grid directions avoid overflowing a cross product or
                // mistaking a very shallow corner for a collinear reversal.
                if(GA==0 || GB==0 || (AX/GA==BX/GB && AY/GA==BY/GB)){Clean.erase(Clean.begin()+I);Changed=true;break;}
            }
        }
        Result.Reserve(static_cast<int32>(Clean.size()));
		for (const ClipperLib::IntPoint& Point : Clean)
		{
			const double U = static_cast<double>(Point.X) / Scale;
			const double V = static_cast<double>(Point.Y) / Scale;
			Result.Add(Projection == EEHBClipProjection::XY
				? FVector(U, V, ConstantCoordinate)
				: FVector(U, ConstantCoordinate, V));
		}
		return Result;
	}

	double AbsPathArea(const ClipperLib::Path& Path)
	{
		return FMath::Abs(static_cast<double>(ClipperLib::Area(Path)));
	}

	bool IsUsablePath(const ClipperLib::Path& Path)
	{
		return Path.size() >= 3 && AbsPathArea(Path) > UE_DOUBLE_SMALL_NUMBER;
	}

	void NormalizePathOrientation(ClipperLib::Path& Path, bool bWantPositiveOrientation)
	{
		if (Path.size() < 3)
		{
			return;
		}
		if (ClipperLib::Orientation(Path) != bWantPositiveOrientation)
		{
			std::reverse(Path.begin(), Path.end());
		}
	}

	FEHBCutPolygon MakeCutPolygonFromPath(
		const ClipperLib::Path& Path,
		EEHBClipProjection Projection,
		double Scale,
		float ConstantCoordinate)
	{
		FEHBCutPolygon Result;
		const TArray<FVector> Positions = FromClipperPath(Path, Projection, Scale, ConstantCoordinate);
		Result.Points.Reserve(Positions.Num());
		for (const FVector& Position : Positions)
		{
			FEHBCutPolygonPoint& Point = Result.Points.AddDefaulted_GetRef();
			Point.PointGuid = FGuid::NewGuid();
			Point.LocalPosition = Position;
		}
		return Result;
	}

	float GetConstantCoordinate(const TArray<FVector>& Polygon, EEHBClipProjection Projection)
	{
		if (Polygon.IsEmpty())
		{
			return 0.0f;
		}
		return Projection == EEHBClipProjection::XY ? Polygon[0].Z : Polygon[0].Y;
	}

	void AppendRegionsFromPolyTree(
		const ClipperLib::PolyTree& Tree,
		EEHBClipProjection Projection,
		double Scale,
		float ConstantCoordinate,
		FEHBPolygonClipResult& OutResult)
	{
		for (ClipperLib::PolyNode* Node = Tree.GetFirst(); Node; Node = Node->GetNext())
		{
			if (!Node || Node->IsHole() || !IsUsablePath(Node->Contour))
			{
				continue;
			}

			FEHBPolygonRegion& Region = OutResult.Regions.AddDefaulted_GetRef();
			Region.OuterLoop = FromClipperPath(Node->Contour, Projection, Scale, ConstantCoordinate);

			for (ClipperLib::PolyNode* HoleNode : Node->Childs)
			{
				if (!HoleNode || !HoleNode->IsHole() || !IsUsablePath(HoleNode->Contour))
				{
					continue;
				}
				Region.HoleLoops.Add(MakeCutPolygonFromPath(HoleNode->Contour, Projection, Scale, ConstantCoordinate));
			}
		}
	}

	bool ExecuteClipper(
		ClipperLib::ClipType ClipType,
		const TArray<TArray<FVector>>& SubjectPolygons,
		const TArray<TArray<FVector>>& CutterPolygons,
		EEHBClipProjection Projection,
		double Scale,
		float ConstantCoordinate,
		FEHBPolygonClipResult& OutResult)
	{
		OutResult.Reset();
		if (SubjectPolygons.IsEmpty())
		{
			return false;
		}

		ClipperLib::Clipper Clipper;
		Clipper.PreserveCollinear(true);
		// Split touching contours instead of returning a bridged outer/hole loop.
		Clipper.StrictlySimple(true);

		ClipperLib::Paths SubjectPaths;
		SubjectPaths.reserve(SubjectPolygons.Num());
		for (const TArray<FVector>& SubjectPolygon : SubjectPolygons)
		{
			ClipperLib::Path Path = ToClipperPath(SubjectPolygon, Projection, Scale);
			if (!IsUsablePath(Path))
			{
				continue;
			}
			NormalizePathOrientation(Path, true);
			SubjectPaths.push_back(Path);
		}
		if (SubjectPaths.empty())
		{
			return false;
		}
		Clipper.AddPaths(SubjectPaths, ClipperLib::ptSubject, true);

		ClipperLib::Paths CutterPaths;
		CutterPaths.reserve(CutterPolygons.Num());
		for (const TArray<FVector>& CutterPolygon : CutterPolygons)
		{
			ClipperLib::Path Path = ToClipperPath(CutterPolygon, Projection, Scale);
			if (!IsUsablePath(Path))
			{
				continue;
			}
			NormalizePathOrientation(Path, true);
			CutterPaths.push_back(Path);
		}
		if (!CutterPaths.empty())
		{
			Clipper.AddPaths(CutterPaths, ClipperLib::ptClip, true);
		}

		ClipperLib::PolyTree SolutionTree;
		if (!Clipper.Execute(ClipType, SolutionTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
		{
			return false;
		}

		AppendRegionsFromPolyTree(SolutionTree, Projection, Scale, ConstantCoordinate, OutResult);
		return OutResult.HasRegions();
	}

	bool Difference(
		const TArray<FVector>& SubjectPolygon,
		const TArray<TArray<FVector>>& CutterPolygons,
		EEHBClipProjection Projection,
		FEHBPolygonClipResult& OutResult,
		double Scale)
	{
		if (CutterPolygons.IsEmpty())
		{
			OutResult.Reset();
			FEHBPolygonRegion& Region = OutResult.Regions.AddDefaulted_GetRef();
			Region.OuterLoop = SubjectPolygon;
			return SubjectPolygon.Num() >= 3;
		}

		return ExecuteClipper(
			ClipperLib::ctDifference,
			{ SubjectPolygon },
			CutterPolygons,
			Projection,
			Scale,
			GetConstantCoordinate(SubjectPolygon, Projection),
			OutResult);
	}
}

bool FEHBPolygonClipper::DifferenceXY(
	const TArray<FVector>& SubjectPolygon,
	const TArray<TArray<FVector>>& CutterPolygons,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	return Difference(SubjectPolygon, CutterPolygons, EEHBClipProjection::XY, OutResult, Scale);
}

bool FEHBPolygonClipper::UnionXY(
	const TArray<TArray<FVector>>& Polygons,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	const float ConstantCoordinate = Polygons.IsEmpty() || Polygons[0].IsEmpty() ? 0.0f : Polygons[0][0].Z;
	return ExecuteClipper(
		ClipperLib::ctUnion,
		Polygons,
		{},
		EEHBClipProjection::XY,
		Scale,
		ConstantCoordinate,
		OutResult);
}

bool FEHBPolygonClipper::IntersectionXY(
	const TArray<FVector>& SubjectPolygon,
	const TArray<FVector>& CutterPolygon,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	return ExecuteClipper(
		ClipperLib::ctIntersection,
		{ SubjectPolygon },
		{ CutterPolygon },
		EEHBClipProjection::XY,
		Scale,
		GetConstantCoordinate(SubjectPolygon, EEHBClipProjection::XY),
		OutResult);
}

bool FEHBPolygonClipper::DifferenceXZ(
	const TArray<FVector>& SubjectPolygon,
	const TArray<TArray<FVector>>& CutterPolygons,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	return Difference(SubjectPolygon, CutterPolygons, EEHBClipProjection::XZ, OutResult, Scale);
}

bool FEHBPolygonClipper::CutHorizontalByPolygon(
	const TArray<FVector>& SubjectPolygon,
	const TArray<TArray<FVector>>& CutterPolygons,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	return DifferenceXY(SubjectPolygon, CutterPolygons, OutResult, Scale);
}

bool FEHBPolygonClipper::CutVerticalByPolygon(
	const TArray<FVector>& SubjectPolygon,
	const TArray<TArray<FVector>>& CutterPolygons,
	FEHBPolygonClipResult& OutResult,
	double Scale)
{
	return DifferenceXZ(SubjectPolygon, CutterPolygons, OutResult, Scale);
}
