// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cutting/EHBCutSourceBuilder.h"

#include "Actors/EHBElementActorBase.h"
#include "Actors/EHBRoofBase.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Cutting/EHBGeneratedMeshCollector.h"
#include "DynamicMesh/DynamicMesh3.h"

using UE::Geometry::FDynamicMesh3;

DEFINE_LOG_CATEGORY_STATIC(LogEHBCutSource, Log, All);

namespace
{
	void SetFailure(FString* OutFailureReason, const FString& Reason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = Reason;
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

	FVector2D To2D(const FVector& Point)
	{
		return FVector2D(Point.X, Point.Y);
	}

	struct FWallFootprintSegment
	{
		const AEHB_Wall* Wall = nullptr;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector TopStart = FVector::ZeroVector;
		FVector TopEnd = FVector::ZeroVector;
		FVector2D Start2D = FVector2D::ZeroVector;
		FVector2D End2D = FVector2D::ZeroVector;
		FVector2D Direction2D = FVector2D(1.0f, 0.0f);
		float Length = 0.0f;
		float Thickness = 0.0f;
		FBox Bounds = FBox(ForceInit);
		TArray<FVector2D> FootprintPoints;
	};

	FVector TransformBuildingLocalToTarget(
		const AEHB_Wall& Wall,
		const FTransform& TargetLocalToWorld,
		const FVector& BuildingLocalPoint)
	{
		const FTransform WorldToTarget = TargetLocalToWorld.Inverse();
		if (Wall.OwningBuilding)
		{
			return (Wall.OwningBuilding->GetActorTransform() * WorldToTarget).TransformPosition(BuildingLocalPoint);
		}

		return WorldToTarget.TransformPosition(BuildingLocalPoint);
	}

	bool BuildWallFootprintSegment(
		const AEHB_Wall& Wall,
		const FEHBCutSourceBuildContext& Context,
		FWallFootprintSegment& OutSegment)
	{
		const FVector Start = TransformBuildingLocalToTarget(Wall, Context.TargetLocalToWorld, Wall.LocalStart);
		const FVector End = TransformBuildingLocalToTarget(Wall, Context.TargetLocalToWorld, Wall.LocalEnd);
		const FVector TopStart = TransformBuildingLocalToTarget(Wall, Context.TargetLocalToWorld, Wall.LocalStart + FVector::UpVector * FMath::Max(1.0f, Wall.Height));
		const FVector TopEnd = TransformBuildingLocalToTarget(Wall, Context.TargetLocalToWorld, Wall.LocalEnd + FVector::UpVector * FMath::Max(1.0f, Wall.Height));

		const FVector2D Start2D = To2D(Start);
		const FVector2D End2D = To2D(End);
		const FVector2D Segment = End2D - Start2D;
		const float Length = Segment.Size();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped wall segment reason=too short target=%s wall=%s start=%s end=%s"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				*Wall.GetName(),
				*Start.ToString(),
				*End.ToString());
			return false;
		}

		const FVector2D Direction = Segment / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const float HalfThickness = FMath::Max(1.0f, Wall.Thickness) * 0.5f;

		OutSegment = FWallFootprintSegment();
		OutSegment.Wall = &Wall;
		OutSegment.Start = Start;
		OutSegment.End = End;
		OutSegment.TopStart = TopStart;
		OutSegment.TopEnd = TopEnd;
		OutSegment.Start2D = Start2D;
		OutSegment.End2D = End2D;
		OutSegment.Direction2D = Direction;
		OutSegment.Length = Length;
		OutSegment.Thickness = FMath::Max(1.0f, Wall.Thickness);
		OutSegment.Bounds += Start;
		OutSegment.Bounds += End;
		OutSegment.Bounds += TopStart;
		OutSegment.Bounds += TopEnd;
		OutSegment.FootprintPoints.Add(Start2D + Normal * HalfThickness);
		OutSegment.FootprintPoints.Add(End2D + Normal * HalfThickness);
		OutSegment.FootprintPoints.Add(End2D - Normal * HalfThickness);
		OutSegment.FootprintPoints.Add(Start2D - Normal * HalfThickness);
		UE_LOG(
			LogEHBCutSource,
			Display,
			TEXT("[EHB WallFootprintCut] wall segment target=%s wall=%s localStart=%s localEnd=%s targetStart=%s targetEnd=%s length=%.2f thickness=%.2f height=%.2f targetBounds=%s"),
			Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
			*Wall.GetName(),
			*Wall.LocalStart.ToString(),
			*Wall.LocalEnd.ToString(),
			*Start.ToString(),
			*End.ToString(),
			Length,
			OutSegment.Thickness,
			Wall.Height,
			*OutSegment.Bounds.ToString());
		return true;
	}

	struct FDisjointSet
	{
		TArray<int32> Parent;

		explicit FDisjointSet(int32 Count)
		{
			Parent.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			if (Parent[Index] != Index)
			{
				Parent[Index] = Find(Parent[Index]);
			}
			return Parent[Index];
		}

		void Union(int32 A, int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA != RootB)
			{
				Parent[RootB] = RootA;
			}
		}
	};

	bool AreSegmentsConnected(
		const FWallFootprintSegment& A,
		const FWallFootprintSegment& B,
		float EndpointTolerance)
	{
		const float ToleranceSquared = FMath::Square(FMath::Max(0.0f, EndpointTolerance));
		return FVector2D::DistSquared(A.Start2D, B.Start2D) <= ToleranceSquared
			|| FVector2D::DistSquared(A.Start2D, B.End2D) <= ToleranceSquared
			|| FVector2D::DistSquared(A.End2D, B.Start2D) <= ToleranceSquared
			|| FVector2D::DistSquared(A.End2D, B.End2D) <= ToleranceSquared;
	}

	FVector2D GetLongestSegmentDirection(const TArray<FWallFootprintSegment>& Segments, const TArray<int32>& SegmentIndices)
	{
		float BestLength = 0.0f;
		FVector2D BestDirection = FVector2D(1.0f, 0.0f);
		for (const int32 SegmentIndex : SegmentIndices)
		{
			if (!Segments.IsValidIndex(SegmentIndex))
			{
				continue;
			}

			const FWallFootprintSegment& Segment = Segments[SegmentIndex];
			if (Segment.Length > BestLength)
			{
				BestLength = Segment.Length;
				BestDirection = Segment.Direction2D;
			}
		}

		const FVector2D SafeDirection = BestDirection.GetSafeNormal(UE_SMALL_NUMBER);
		return SafeDirection.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : SafeDirection;
	}

	float Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	float Cross2D(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		return Cross2D(A - Origin, B - Origin);
	}

	float CalculateSignedArea(const TArray<FVector2D>& Points)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector2D& A = Points[Index];
			const FVector2D& B = Points[(Index + 1) % Points.Num()];
			Area += static_cast<double>(A.X) * B.Y - static_cast<double>(A.Y) * B.X;
		}
		return static_cast<float>(Area * 0.5);
	}

	bool BuildConvexHull(TArray<FVector2D> Points, TArray<FVector2D>& OutHull)
	{
		OutHull.Reset();
		Points.Sort(
			[](const FVector2D& A, const FVector2D& B)
			{
				return !FMath::IsNearlyEqual(A.X, B.X, 0.01f) ? A.X < B.X : A.Y < B.Y;
			});

		TArray<FVector2D> UniquePoints;
		UniquePoints.Reserve(Points.Num());
		for (const FVector2D& Point : Points)
		{
			if (UniquePoints.IsEmpty() || FVector2D::DistSquared(UniquePoints.Last(), Point) > FMath::Square(0.01f))
			{
				UniquePoints.Add(Point);
			}
		}

		if (UniquePoints.Num() < 3)
		{
			return false;
		}

		TArray<FVector2D> Hull;
		Hull.Reserve(UniquePoints.Num() * 2);
		for (const FVector2D& Point : UniquePoints)
		{
			while (Hull.Num() >= 2 && Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= 0.01f)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Point);
		}

		const int32 LowerCount = Hull.Num();
		for (int32 Index = UniquePoints.Num() - 2; Index >= 0; --Index)
		{
			const FVector2D& Point = UniquePoints[Index];
			while (Hull.Num() > LowerCount && Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= 0.01f)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Point);
		}

		if (!Hull.IsEmpty())
		{
			Hull.Pop(EAllowShrinking::No);
		}

		if (Hull.Num() < 3)
		{
			return false;
		}

		if (CalculateSignedArea(Hull) < 0.0f)
		{
			Algo::Reverse(Hull);
		}

		OutHull = MoveTemp(Hull);
		return true;
	}

	bool IntersectLines2D(
		const FVector2D& PointA,
		const FVector2D& DirectionA,
		const FVector2D& PointB,
		const FVector2D& DirectionB,
		FVector2D& OutIntersection)
	{
		const float Denominator = Cross2D(DirectionA, DirectionB);
		if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const float T = Cross2D(PointB - PointA, DirectionB) / Denominator;
		OutIntersection = PointA + DirectionA * T;
		return true;
	}

	void OffsetConvexPolygon(const TArray<FVector2D>& Points, float Distance, TArray<FVector2D>& OutPoints)
	{
		OutPoints.Reset();
		if (Points.Num() < 3 || FMath::IsNearlyZero(Distance, 0.01f))
		{
			OutPoints = Points;
			return;
		}

		OutPoints.Reserve(Points.Num());
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector2D& Prev = Points[(Index + Points.Num() - 1) % Points.Num()];
			const FVector2D& Curr = Points[Index];
			const FVector2D& Next = Points[(Index + 1) % Points.Num()];

			const FVector2D PrevEdge = (Curr - Prev).GetSafeNormal();
			const FVector2D NextEdge = (Next - Curr).GetSafeNormal();
			if (PrevEdge.IsNearlyZero() || NextEdge.IsNearlyZero())
			{
				continue;
			}

			const FVector2D PrevOutward(PrevEdge.Y, -PrevEdge.X);
			const FVector2D NextOutward(NextEdge.Y, -NextEdge.X);
			FVector2D Intersection = Curr + (PrevOutward + NextOutward).GetSafeNormal() * Distance;
			if (!IntersectLines2D(Prev + PrevOutward * Distance, PrevEdge, Curr + NextOutward * Distance, NextEdge, Intersection))
			{
				Intersection = Curr + (PrevOutward + NextOutward).GetSafeNormal() * Distance;
			}
			OutPoints.Add(Intersection);
		}

		if (OutPoints.Num() < 3 || FMath::Abs(CalculateSignedArea(OutPoints)) <= UE_SMALL_NUMBER)
		{
			OutPoints = Points;
		}
	}

	void ExpandConvexPolygonToMinimum(TArray<FVector2D>& Points, float MinimumSize)
	{
		const float SafeMinimumSize = FMath::Max(0.0f, MinimumSize);
		if (Points.Num() < 3 || SafeMinimumSize <= 0.0f)
		{
			return;
		}

		FBox2D Bounds(ForceInit);
		for (const FVector2D& Point : Points)
		{
			Bounds += Point;
		}
		if (!Bounds.bIsValid)
		{
			return;
		}

		const FVector2D Size = Bounds.GetSize();
		const float ScaleX = Size.X < SafeMinimumSize && Size.X > UE_SMALL_NUMBER ? SafeMinimumSize / Size.X : 1.0f;
		const float ScaleY = Size.Y < SafeMinimumSize && Size.Y > UE_SMALL_NUMBER ? SafeMinimumSize / Size.Y : 1.0f;
		if (ScaleX <= 1.0f && ScaleY <= 1.0f)
		{
			return;
		}

		const FVector2D Center = Bounds.GetCenter();
		for (FVector2D& Point : Points)
		{
			const FVector2D Local = Point - Center;
			Point = Center + FVector2D(Local.X * ScaleX, Local.Y * ScaleY);
		}
	}

	void CalculatePointBounds(const TArray<FVector2D>& Points, FVector2D& OutMin, FVector2D& OutMax)
	{
		OutMin = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		OutMax = FVector2D(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
		for (const FVector2D& Point : Points)
		{
			OutMin.X = FMath::Min(OutMin.X, Point.X);
			OutMin.Y = FMath::Min(OutMin.Y, Point.Y);
			OutMax.X = FMath::Max(OutMax.X, Point.X);
			OutMax.Y = FMath::Max(OutMax.Y, Point.Y);
		}
	}

	void ExpandRangeToMinimum(float& InOutMin, float& InOutMax, float MinimumSize)
	{
		const float SafeMinimumSize = FMath::Max(0.0f, MinimumSize);
		if (InOutMax - InOutMin >= SafeMinimumSize)
		{
			return;
		}

		const float Center = (InOutMin + InOutMax) * 0.5f;
		const float HalfSize = SafeMinimumSize * 0.5f;
		InOutMin = Center - HalfSize;
		InOutMax = Center + HalfSize;
	}

	bool BuildOrientedQuadSourceFromWallGroup(
		const TArray<FWallFootprintSegment>& Segments,
		const TArray<int32>& SegmentIndices,
		const FEHBCutSourceBuildContext& Context,
		FEHBResolvedCutSourceData& OutSource)
	{
		if (SegmentIndices.Num() < FMath::Max(1, Context.MinWallFootprintGroupWallCount))
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped wall footprint group reason=min wall count target=%s walls=%d minWalls=%d"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				SegmentIndices.Num(),
				FMath::Max(1, Context.MinWallFootprintGroupWallCount));
			return false;
		}

		FBox GroupBounds(ForceInit);
		TArray<FGuid> GroupElementGuids;
		TArray<FVector2D> SourceFootprintPoints;

		for (const int32 SegmentIndex : SegmentIndices)
		{
			if (!Segments.IsValidIndex(SegmentIndex))
			{
				continue;
			}

			const FWallFootprintSegment& Segment = Segments[SegmentIndex];
			if (Segment.Wall)
			{
				GroupElementGuids.AddUnique(Segment.Wall->ElementGuid);
			}
			GroupBounds += Segment.Bounds;

			for (const FVector2D& Point : Segment.FootprintPoints)
			{
				SourceFootprintPoints.Add(Point);
			}
		}

		TArray<FVector2D> HullPoints;
		if (!GroupBounds.IsValid || !BuildConvexHull(SourceFootprintPoints, HullPoints))
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped wall footprint group reason=invalid convex hull target=%s walls=%d sourcePoints=%d groupBounds=%s"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				SegmentIndices.Num(),
				SourceFootprintPoints.Num(),
				*GroupBounds.ToString());
			return false;
		}

		const int32 RawHullPointCount = HullPoints.Num();
		const float Padding = Context.WallFootprintPadding;
		TArray<FVector2D> PaddedHullPoints;
		OffsetConvexPolygon(HullPoints, Padding, PaddedHullPoints);
		ExpandConvexPolygonToMinimum(PaddedHullPoints, Context.WallFootprintMinDimension);

		FVector2D BoundsMin;
		FVector2D BoundsMax;
		CalculatePointBounds(PaddedHullPoints, BoundsMin, BoundsMax);

		const float SizeX = BoundsMax.X - BoundsMin.X;
		const float SizeY = BoundsMax.Y - BoundsMin.Y;
		const float MaxDimension = FMath::Max(SizeX, SizeY);
		if (Context.WallFootprintMaxDimension > 0.0f && MaxDimension > Context.WallFootprintMaxDimension)
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped wall footprint group reason=max dimension target=%s walls=%d size=(%.2f, %.2f) max=%.2f"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				SegmentIndices.Num(),
				SizeX,
				SizeY,
				Context.WallFootprintMaxDimension);
			return false;
		}

		const float FootprintArea = FMath::Abs(CalculateSignedArea(PaddedHullPoints));
		if (FootprintArea < FMath::Max(0.0f, Context.WallFootprintMinArea))
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped wall footprint group reason=min area target=%s walls=%d area=%.2f minArea=%.2f size=(%.2f, %.2f)"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				SegmentIndices.Num(),
				FootprintArea,
				FMath::Max(0.0f, Context.WallFootprintMinArea),
				SizeX,
				SizeY);
			return false;
		}

		float MinZ = GroupBounds.Min.Z - FMath::Max(0.0f, Context.WallFootprintZPadding);
		float MaxZ = GroupBounds.Max.Z + FMath::Max(0.0f, Context.WallFootprintZPadding);
		const float MinExtrudeHeight = FMath::Max(1.0f, Context.WallFootprintMinExtrudeHeight);
		if (MaxZ - MinZ < MinExtrudeHeight)
		{
			const float CenterZ = (MinZ + MaxZ) * 0.5f;
			MinZ = CenterZ - MinExtrudeHeight * 0.5f;
			MaxZ = CenterZ + MinExtrudeHeight * 0.5f;
		}

		OutSource = FEHBResolvedCutSourceData();
		OutSource.GeometryKind = EEHBCutSourceGeometryKind::OrientedQuadFootprint3D;
		OutSource.DebugName = FName(TEXT("WallConvexFootprint"));
		OutSource.SourceElementGuids = MoveTemp(GroupElementGuids);
		OutSource.TargetLocalFootprint.Reserve(PaddedHullPoints.Num());
		for (const FVector2D& Point : PaddedHullPoints)
		{
			OutSource.TargetLocalFootprint.Add(FVector(Point.X, Point.Y, 0.0f));
		}
		OutSource.MinZ = MinZ;
		OutSource.MaxZ = MaxZ;
		OutSource.TargetLocalBounds.Init();
		for (const FVector& Point : OutSource.TargetLocalFootprint)
		{
			OutSource.TargetLocalBounds += FVector(Point.X, Point.Y, MinZ);
			OutSource.TargetLocalBounds += FVector(Point.X, Point.Y, MaxZ);
		}
		UE_LOG(
			LogEHBCutSource,
			Display,
			TEXT("[EHB WallFootprintCut] built wall footprint group target=%s walls=%d sourceGuids=%d sourcePoints=%d hullPoints=%d paddedPoints=%d area=%.2f size=(%.2f, %.2f) z=(%.2f, %.2f) bounds=%s"),
			Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
			SegmentIndices.Num(),
			OutSource.SourceElementGuids.Num(),
			SourceFootprintPoints.Num(),
			RawHullPointCount,
			OutSource.TargetLocalFootprint.Num(),
			FootprintArea,
			SizeX,
			SizeY,
			MinZ,
			MaxZ,
			*OutSource.TargetLocalBounds.ToString());
		return true;
	}

	void BuildWallFootprintSources(
		const TArray<const AEHB_Wall*>& Walls,
		const FEHBCutSourceBuildContext& Context,
		TArray<FEHBResolvedCutSourceData>& OutSources)
	{
		UE_LOG(
			LogEHBCutSource,
			Display,
			TEXT("[EHB WallFootprintCut] BuildWallFootprintSources begin target=%s walls=%d minWalls=%d endpointTol=%.2f padding=%.2f maxDim=%.2f minArea=%.2f zPadding=%.2f minExtrude=%.2f"),
			Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
			Walls.Num(),
			Context.MinWallFootprintGroupWallCount,
			Context.WallFootprintGroupEndpointTolerance,
			Context.WallFootprintPadding,
			Context.WallFootprintMaxDimension,
			Context.WallFootprintMinArea,
			Context.WallFootprintZPadding,
			Context.WallFootprintMinExtrudeHeight);

		TArray<FWallFootprintSegment> Segments;
		Segments.Reserve(Walls.Num());
		for (const AEHB_Wall* Wall : Walls)
		{
			if (!Wall)
			{
				continue;
			}

			FWallFootprintSegment Segment;
			if (BuildWallFootprintSegment(*Wall, Context, Segment))
			{
				Segments.Add(MoveTemp(Segment));
			}
		}

		if (Segments.IsEmpty())
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] BuildWallFootprintSources skipped target=%s reason=no valid wall segments inputWalls=%d"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				Walls.Num());
			return;
		}

		FDisjointSet Groups(Segments.Num());
		for (int32 A = 0; A < Segments.Num(); ++A)
		{
			for (int32 B = A + 1; B < Segments.Num(); ++B)
			{
				if (AreSegmentsConnected(Segments[A], Segments[B], Context.WallFootprintGroupEndpointTolerance))
				{
					Groups.Union(A, B);
				}
			}
		}

		TMap<int32, TArray<int32>> SegmentsByGroup;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			SegmentsByGroup.FindOrAdd(Groups.Find(SegmentIndex)).Add(SegmentIndex);
		}

		for (const TPair<int32, TArray<int32>>& Pair : SegmentsByGroup)
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] wall group candidate target=%s groupRoot=%d walls=%d"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				Pair.Key,
				Pair.Value.Num());

			FEHBResolvedCutSourceData Source;
			if (BuildOrientedQuadSourceFromWallGroup(Segments, Pair.Value, Context, Source))
			{
				UE_LOG(
					LogEHBCutSource,
					Display,
					TEXT("[EHB WallFootprintCut] appended wall oriented quad source target=%s walls=%d bounds=%s"),
					Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
					Pair.Value.Num(),
					*Source.TargetLocalBounds.ToString());
				OutSources.Add(MoveTemp(Source));
			}
		}
	}
}

bool FEHBCutSourceBuilder::BuildSources(
	const TArray<AEHBElementActorBase*>& SourceElements,
	const FEHBCutSourceBuildContext& Context,
	TArray<FEHBResolvedCutSourceData>& OutSources,
	FString* OutFailureReason)
{
	OutSources.Reset();
	UE_LOG(
		LogEHBCutSource,
		Display,
		TEXT("[EHB WallFootprintCut] BuildSources begin target=%s inputElements=%d useWallFootprint=%d intent=%d"),
		Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
		SourceElements.Num(),
		Context.bUseWallFootprintCutters ? 1 : 0,
		static_cast<int32>(Context.Intent));
	if (!Context.TargetElement)
	{
		SetFailure(OutFailureReason, TEXT("Cut source context has no target element"));
		return false;
	}

	TArray<const AEHB_Wall*> WallSources;
	TArray<AEHBElementActorBase*> MeshAggregateSources;
	for (AEHBElementActorBase* SourceElement : SourceElements)
	{
		if (!IsValid(SourceElement) || SourceElement == Context.TargetElement)
		{
			continue;
		}

		if (Context.bUseWallFootprintCutters)
		{
			if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(SourceElement))
			{
				WallSources.Add(Wall);
				continue;
			}
		}

		MeshAggregateSources.AddUnique(SourceElement);
	}
	UE_LOG(
		LogEHBCutSource,
		Display,
		TEXT("[EHB WallFootprintCut] BuildSources split target=%s wallSources=%d meshAggregateSources=%d"),
		Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
		WallSources.Num(),
		MeshAggregateSources.Num());

	for (AEHBElementActorBase* SourceElement : MeshAggregateSources)
	{
		if (!WallSources.IsEmpty() && !SourceElement->IsA<AEHBRoofBase>())
		{
			UE_LOG(
				LogEHBCutSource,
				Display,
				TEXT("[EHB WallFootprintCut] skipped mesh aggregate source target=%s source=%s class=%s reason=wall footprint source owns this opening"),
				Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
				SourceElement ? *SourceElement->GetName() : TEXT("None"),
				SourceElement ? *SourceElement->GetClass()->GetName() : TEXT("None"));
			continue;
		}

		FEHBResolvedCutSourceData& ResolvedSource = OutSources.AddDefaulted_GetRef();
		ResolvedSource.GeometryKind = EEHBCutSourceGeometryKind::MeshAggregate3D;
		ResolvedSource.DebugName = SourceElement ? FName(*SourceElement->GetName()) : NAME_None;
		ResolvedSource.SourceElementGuids.Add(SourceElement->ElementGuid);
		if (!FEHBGeneratedMeshCollector::CollectFromElement(SourceElement, Context.TargetLocalToWorld, ResolvedSource.MeshAggregate))
		{
			OutSources.Pop(EAllowShrinking::No);
			UE_LOG(
				LogEHBCutSource,
				Warning,
				TEXT("[EHB CutSource] source aggregate failed target=%s source=%s"),
				*Context.TargetElement->GetName(),
				SourceElement ? *SourceElement->GetName() : TEXT("None"));
			continue;
		}
		ResolvedSource.TargetLocalBounds = ResolvedSource.MeshAggregate.LocalBounds;
	}

	BuildWallFootprintSources(WallSources, Context, OutSources);

	if (OutSources.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("No usable cut sources were built"));
		UE_LOG(
			LogEHBCutSource,
			Display,
			TEXT("[EHB WallFootprintCut] BuildSources end target=%s result=empty"),
			Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"));
		return false;
	}

	UE_LOG(
		LogEHBCutSource,
		Display,
		TEXT("[EHB WallFootprintCut] BuildSources end target=%s resolvedSources=%d"),
		Context.TargetElement ? *Context.TargetElement->GetName() : TEXT("None"),
		OutSources.Num());
	return true;
}

bool FEHBCutSourceBuilder::BuildDynamicMeshForSource(
	const FEHBResolvedCutSourceData& Source,
	FDynamicMesh3& OutMesh,
	FString* OutFailureReason)
{
	OutMesh.Clear();
	if (Source.GeometryKind != EEHBCutSourceGeometryKind::OrientedQuadFootprint3D)
	{
		SetFailure(OutFailureReason, TEXT("Cut source is not a direct dynamic mesh source"));
		return false;
	}
	if (Source.TargetLocalFootprint.Num() < 3 || Source.MaxZ <= Source.MinZ)
	{
		SetFailure(OutFailureReason, TEXT("Oriented quad footprint source is invalid"));
		return false;
	}

	TArray<int32> BottomVertexIDs;
	TArray<int32> TopVertexIDs;
	BottomVertexIDs.Reserve(Source.TargetLocalFootprint.Num());
	TopVertexIDs.Reserve(Source.TargetLocalFootprint.Num());
	for (const FVector& Point : Source.TargetLocalFootprint)
	{
		BottomVertexIDs.Add(OutMesh.AppendVertex(FVector3d(Point.X, Point.Y, Source.MinZ)));
		TopVertexIDs.Add(OutMesh.AppendVertex(FVector3d(Point.X, Point.Y, Source.MaxZ)));
	}

	for (int32 Index = 1; Index + 1 < Source.TargetLocalFootprint.Num(); ++Index)
	{
		AppendTriangleWithNormal(OutMesh, TopVertexIDs[0], TopVertexIDs[Index], TopVertexIDs[Index + 1], FVector3d::UnitZ());
		AppendTriangleWithNormal(OutMesh, BottomVertexIDs[0], BottomVertexIDs[Index + 1], BottomVertexIDs[Index], -FVector3d::UnitZ());
	}

	for (int32 Index = 0; Index < Source.TargetLocalFootprint.Num(); ++Index)
	{
		const int32 NextIndex = (Index + 1) % Source.TargetLocalFootprint.Num();
		const FVector2D A2 = To2D(Source.TargetLocalFootprint[Index]);
		const FVector2D B2 = To2D(Source.TargetLocalFootprint[NextIndex]);
		const FVector2D Edge = B2 - A2;
		if (Edge.SizeSquared() <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D RightNormal(Edge.Y, -Edge.X);
		const FVector3d DesiredNormal = FVector3d(RightNormal.X, RightNormal.Y, 0.0).GetSafeNormal();
		AppendTriangleWithNormal(
			OutMesh,
			BottomVertexIDs[Index],
			BottomVertexIDs[NextIndex],
			TopVertexIDs[NextIndex],
			DesiredNormal);
		AppendTriangleWithNormal(
			OutMesh,
			BottomVertexIDs[Index],
			TopVertexIDs[NextIndex],
			TopVertexIDs[Index],
			DesiredNormal);
	}

	return OutMesh.TriangleCount() > 0;
}
