#include "Actors/EHBHipRoof.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "Cutting/EHBGeneratedMeshCollector.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"

DEFINE_LOG_CATEGORY_STATIC(LogEHBHipRoof, Log, All);

namespace
{
	constexpr float EHBHipRoofMinDimension = 1.0f;
	constexpr float EHBHipRoofUVScale = 100.0f;
	constexpr int32 EHBHipRoofBodyGroupID = 1;
	constexpr int32 EHBHipRoofSideWallGroupID = 2;
	constexpr int32 EHBHipRoofRidgeGroupID = 10;
	constexpr int32 EHBHipRoofEaveGroupID = 11;
	constexpr int32 EHBHipRoofHipRidgeGroupID = 12;

	struct FHipRoofGeometry
	{
		FVector LowA0 = FVector::ZeroVector;
		FVector LowA1 = FVector::ZeroVector;
		FVector LowB0 = FVector::ZeroVector;
		FVector LowB1 = FVector::ZeroVector;
		FVector Ridge0 = FVector::ZeroVector;
		FVector Ridge1 = FVector::ZeroVector;
		FVector BottomLowA0 = FVector::ZeroVector;
		FVector BottomLowA1 = FVector::ZeroVector;
		FVector BottomLowB0 = FVector::ZeroVector;
		FVector BottomLowB1 = FVector::ZeroVector;
		bool bHasRidge = false;
	};

	struct FHalfHipRoofGeometry
	{
		FVector LowACut = FVector::ZeroVector;
		FVector LowAEnd = FVector::ZeroVector;
		FVector LowBCut = FVector::ZeroVector;
		FVector LowBEnd = FVector::ZeroVector;
		FVector RidgeCut = FVector::ZeroVector;
		FVector RidgeEnd = FVector::ZeroVector;
		FVector BottomLowACut = FVector::ZeroVector;
		FVector BottomLowAEnd = FVector::ZeroVector;
		FVector BottomLowBCut = FVector::ZeroVector;
		FVector BottomLowBEnd = FVector::ZeroVector;
		bool bKeepPositiveCross = false;
	};

	FVector CalculateFaceNormal(const TArray<FVector>& Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return FVector::UpVector;
		}

		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			const FVector Normal = FVector::CrossProduct(Polygon[Index] - Polygon[0], Polygon[Index + 1] - Polygon[0]).GetSafeNormal();
			if (!Normal.IsNearlyZero())
			{
				return Normal;
			}
		}

		return FVector::UpVector;
	}

	FVector CalculateCentroid(const TArray<FVector>& Polygon)
	{
		FVector Centroid = FVector::ZeroVector;
		for (const FVector& Point : Polygon)
		{
			Centroid += Point;
		}
		return Polygon.IsEmpty() ? FVector::ZeroVector : Centroid / static_cast<float>(Polygon.Num());
	}

	void AppendTriangleWithNormal(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& DesiredNormal,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		const FVector GeometricNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		FVector UseB = B;
		FVector UseC = C;
		if (!GeometricNormal.IsNearlyZero() && FVector::DotProduct(GeometricNormal, DesiredNormal) > 0.0)
		{
			Swap(UseB, UseC);
		}

		const int32 BaseIndex = OutMesh.Vertices.Num();
		OutMesh.Vertices.Add(A);
		OutMesh.Vertices.Add(UseB);
		OutMesh.Vertices.Add(UseC);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.UV0.Add(FVector2D(A.X, A.Y) / EHBHipRoofUVScale);
		OutMesh.UV0.Add(FVector2D(UseB.X, UseB.Y) / EHBHipRoofUVScale);
		OutMesh.UV0.Add(FVector2D(UseC.X, UseC.Y) / EHBHipRoofUVScale);
		OutMesh.Triangles.Add(BaseIndex);
		OutMesh.Triangles.Add(BaseIndex + 1);
		OutMesh.Triangles.Add(BaseIndex + 2);
		OutMesh.TriangleGroups.Add(TriangleGroupID);
	}

	void AppendTriangleWithNormalAndUVs(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& DesiredNormal,
		const FVector2D& UVA,
		const FVector2D& UVB,
		const FVector2D& UVC,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		const FVector GeometricNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		FVector UseB = B;
		FVector UseC = C;
		FVector2D UseUVB = UVB;
		FVector2D UseUVC = UVC;
		if (!GeometricNormal.IsNearlyZero() && FVector::DotProduct(GeometricNormal, DesiredNormal) > 0.0)
		{
			Swap(UseB, UseC);
			Swap(UseUVB, UseUVC);
		}

		const int32 BaseIndex = OutMesh.Vertices.Num();
		OutMesh.Vertices.Add(A);
		OutMesh.Vertices.Add(UseB);
		OutMesh.Vertices.Add(UseC);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.Normals.Add(DesiredNormal);
		OutMesh.UV0.Add(UVA);
		OutMesh.UV0.Add(UseUVB);
		OutMesh.UV0.Add(UseUVC);
		OutMesh.Triangles.Add(BaseIndex);
		OutMesh.Triangles.Add(BaseIndex + 1);
		OutMesh.Triangles.Add(BaseIndex + 2);
		OutMesh.TriangleGroups.Add(TriangleGroupID);
	}

	void AppendConvexFace(
		const TArray<FVector>& Polygon,
		const FVector& BodyCenter,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}

		const FVector Centroid = CalculateCentroid(Polygon);
		FVector DesiredNormal = CalculateFaceNormal(Polygon);
		if (FVector::DotProduct(DesiredNormal, Centroid - BodyCenter) < 0.0f)
		{
			DesiredNormal *= -1.0f;
		}
		if (DesiredNormal.IsNearlyZero())
		{
			DesiredNormal = FVector::UpVector;
		}

		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			AppendTriangleWithNormal(Polygon[0], Polygon[Index], Polygon[Index + 1], DesiredNormal, OutMesh, TriangleGroupID);
		}
	}

	TArray<FVector> CleanPolygonVertices(const TArray<FVector>& Polygon)
	{
		TArray<FVector> Cleaned;
		constexpr float DuplicateTolerance = 0.01f;
		for (const FVector& Point : Polygon)
		{
			if (Cleaned.IsEmpty() || !Cleaned.Last().Equals(Point, DuplicateTolerance))
			{
				Cleaned.Add(Point);
			}
		}

		if (Cleaned.Num() > 1 && Cleaned[0].Equals(Cleaned.Last(), DuplicateTolerance))
		{
			Cleaned.Pop(EAllowShrinking::No);
		}
		return Cleaned;
	}

	void AppendCleanConvexFace(
		const TArray<FVector>& Polygon,
		const FVector& BodyCenter,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		const TArray<FVector> Cleaned = CleanPolygonVertices(Polygon);
		if (Cleaned.Num() >= 3)
		{
			AppendConvexFace(Cleaned, BodyCenter, OutMesh, TriangleGroupID);
		}
	}

	FVector2D MakeHipSlopeUV(const FVector& Point, const FVector& UAxis, const FVector& DownSlopeAxis)
	{
		return FVector2D(
			static_cast<float>(FVector::DotProduct(Point, UAxis) / EHBHipRoofUVScale),
			static_cast<float>(FVector::DotProduct(Point, DownSlopeAxis) / EHBHipRoofUVScale));
	}

	FVector CalculateOutwardFaceNormal(const TArray<FVector>& Polygon, const FVector& BodyCenter)
	{
		if (Polygon.Num() < 3)
		{
			return FVector::UpVector;
		}

		const FVector Centroid = CalculateCentroid(Polygon);
		FVector DesiredNormal = CalculateFaceNormal(Polygon);
		if (FVector::DotProduct(DesiredNormal, Centroid - BodyCenter) < 0.0f)
		{
			DesiredNormal *= -1.0f;
		}
		if (DesiredNormal.IsNearlyZero())
		{
			DesiredNormal = FVector::UpVector;
		}
		return DesiredNormal;
	}

	void AppendHipSlopeFace(
		const TArray<FVector>& Polygon,
		const FVector& BodyCenter,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}

		const FVector DesiredNormal = CalculateOutwardFaceNormal(Polygon, BodyCenter);
		FVector DownSlopeAxis = (-FVector::UpVector - DesiredNormal * FVector::DotProduct(-FVector::UpVector, DesiredNormal)).GetSafeNormal();
		if (DownSlopeAxis.IsNearlyZero())
		{
			AppendConvexFace(Polygon, BodyCenter, OutMesh, TriangleGroupID);
			return;
		}

		FVector UAxis = FVector::CrossProduct(DesiredNormal, DownSlopeAxis).GetSafeNormal();
		if (UAxis.IsNearlyZero())
		{
			AppendConvexFace(Polygon, BodyCenter, OutMesh, TriangleGroupID);
			return;
		}

		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			AppendTriangleWithNormalAndUVs(
				Polygon[0],
				Polygon[Index],
				Polygon[Index + 1],
				DesiredNormal,
				MakeHipSlopeUV(Polygon[0], UAxis, DownSlopeAxis),
				MakeHipSlopeUV(Polygon[Index], UAxis, DownSlopeAxis),
				MakeHipSlopeUV(Polygon[Index + 1], UAxis, DownSlopeAxis),
				OutMesh,
				TriangleGroupID);
		}
	}

	void AppendCleanHipSlopeFace(
		const TArray<FVector>& Polygon,
		const FVector& BodyCenter,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		const TArray<FVector> Cleaned = CleanPolygonVertices(Polygon);
		if (Cleaned.Num() >= 3)
		{
			AppendHipSlopeFace(Cleaned, BodyCenter, OutMesh, TriangleGroupID);
		}
	}

	FVector CalculateHipBodyCenter(const FHipRoofGeometry& Geometry)
	{
		const TArray<FVector> BodyPoints = {
			Geometry.LowA0,
			Geometry.LowA1,
			Geometry.LowB1,
			Geometry.LowB0,
			Geometry.Ridge0,
			Geometry.Ridge1,
			Geometry.BottomLowA0,
			Geometry.BottomLowA1,
			Geometry.BottomLowB1,
			Geometry.BottomLowB0,
		};

		FVector BodyCenter = FVector::ZeroVector;
		for (const FVector& Point : BodyPoints)
		{
			BodyCenter += Point;
		}
		return BodyCenter / static_cast<float>(BodyPoints.Num());
	}

	FVector CalculateHalfHipBodyCenter(const FHalfHipRoofGeometry& Geometry)
	{
		const TArray<FVector> BodyPoints = {
			Geometry.LowACut,
			Geometry.LowAEnd,
			Geometry.LowBEnd,
			Geometry.LowBCut,
			Geometry.RidgeCut,
			Geometry.RidgeEnd,
			Geometry.BottomLowACut,
			Geometry.BottomLowAEnd,
			Geometry.BottomLowBEnd,
			Geometry.BottomLowBCut
		};

		FVector BodyCenter = FVector::ZeroVector;
		for (const FVector& Point : BodyPoints)
		{
			BodyCenter += Point;
		}
		return BodyCenter / static_cast<float>(BodyPoints.Num());
	}

	FVector NormalizeCapTopDirection(const FVector& InDirection)
	{
		FVector TopDirection = InDirection.GetSafeNormal();
		if (TopDirection.IsNearlyZero())
		{
			TopDirection = FVector::UpVector;
		}
		if (FVector::DotProduct(TopDirection, FVector::UpVector) < 0.0f)
		{
			TopDirection *= -1.0f;
		}
		return TopDirection;
	}

	void AppendRoofEdgeCapWithNodeTops(
		const FVector& Start,
		const FVector& End,
		const FVector& FaceNormalA,
		const FVector& FaceNormalB,
		const FVector& StartTopDirection,
		const FVector& EndTopDirection,
		float Width,
		float Height,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		const FVector Tangent = (End - Start).GetSafeNormal();
		if (Tangent.IsNearlyZero())
		{
			return;
		}

		FVector NormalA = FaceNormalA.GetSafeNormal();
		FVector NormalB = FaceNormalB.GetSafeNormal();
		if (NormalA.IsNearlyZero())
		{
			NormalA = FVector::UpVector;
		}
		if (NormalB.IsNearlyZero())
		{
			NormalB = FVector::UpVector;
		}

		FVector SideA = FVector::CrossProduct(Tangent, NormalA).GetSafeNormal();
		FVector SideB = FVector::CrossProduct(NormalB, Tangent).GetSafeNormal();
		if (SideA.IsNearlyZero())
		{
			SideA = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
		}
		if (SideB.IsNearlyZero())
		{
			SideB = -SideA;
		}
		if (FVector::DotProduct(SideA, SideB) > 0.0f)
		{
			SideB *= -1.0f;
		}

		const float HalfWidth = FMath::Max(Width, 0.1f) * 0.5f;
		const float SafeHeight = FMath::Max(Height, 0.1f);
		const float SinkDepth = FMath::Min(SafeHeight * 0.25f, 2.0f);
		const FVector StartTop = NormalizeCapTopDirection(StartTopDirection);
		const FVector EndTop = NormalizeCapTopDirection(EndTopDirection);
		const FVector S0 = Start + SideA * HalfWidth - NormalA * SinkDepth;
		const FVector S1 = Start + SideB * HalfWidth - NormalB * SinkDepth;
		const FVector ST = Start + StartTop * SafeHeight;
		const FVector E0 = End + SideA * HalfWidth - NormalA * SinkDepth;
		const FVector E1 = End + SideB * HalfWidth - NormalB * SinkDepth;
		const FVector ET = End + EndTop * SafeHeight;
		const FVector BodyCenter = (S0 + S1 + ST + E0 + E1 + ET) / 6.0f;

		AppendConvexFace({ S0, E0, E1, S1 }, BodyCenter, OutMesh);
		AppendConvexFace({ S0, ST, ET, E0 }, BodyCenter, OutMesh);
		AppendConvexFace({ ST, S1, E1, ET }, BodyCenter, OutMesh);
		AppendConvexFace({ S0, S1, ST }, BodyCenter, OutMesh);
		AppendConvexFace({ E0, ET, E1 }, BodyCenter, OutMesh);
	}

	double SignedAreaXY(const TArray<FVector>& Loop)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector& A = Loop[Index];
			const FVector& B = Loop[(Index + 1) % Loop.Num()];
			Area += static_cast<double>(A.X) * static_cast<double>(B.Y)
				- static_cast<double>(B.X) * static_cast<double>(A.Y);
		}
		return Area * 0.5;
	}

	FVector2D GetEdgeNormal2D(const FVector2D& Direction, bool bUseLeftNormal)
	{
		return bUseLeftNormal
			? FVector2D(-Direction.Y, Direction.X)
			: FVector2D(Direction.Y, -Direction.X);
	}

	bool IntersectLines2D(
		const FVector2D& PointA,
		const FVector2D& DirectionA,
		const FVector2D& PointB,
		const FVector2D& DirectionB,
		FVector2D& OutPoint)
	{
		const double Cross = static_cast<double>(DirectionA.X) * static_cast<double>(DirectionB.Y)
			- static_cast<double>(DirectionA.Y) * static_cast<double>(DirectionB.X);
		if (FMath::Abs(Cross) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FVector2D Delta = PointB - PointA;
		const double T = (static_cast<double>(Delta.X) * static_cast<double>(DirectionB.Y)
			- static_cast<double>(Delta.Y) * static_cast<double>(DirectionB.X)) / Cross;
		OutPoint = PointA + DirectionA * static_cast<float>(T);
		return true;
	}

	bool BuildOffsetLoop2D(
		const TArray<FVector>& CenterLoop,
		float Offset,
		bool bOffsetInward,
		TArray<FVector>& OutLoop)
	{
		OutLoop.Reset();
		const int32 PointCount = CenterLoop.Num();
		if (PointCount < 3 || FMath::IsNearlyZero(Offset))
		{
			return false;
		}

		const bool bCounterClockwise = SignedAreaXY(CenterLoop) > 0.0;
		const bool bInwardUsesLeftNormal = bCounterClockwise;
		OutLoop.Reserve(PointCount);
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const FVector& Previous3D = CenterLoop[(Index + PointCount - 1) % PointCount];
			const FVector& Current3D = CenterLoop[Index];
			const FVector& Next3D = CenterLoop[(Index + 1) % PointCount];

			const FVector2D Previous(Previous3D.X, Previous3D.Y);
			const FVector2D Current(Current3D.X, Current3D.Y);
			const FVector2D Next(Next3D.X, Next3D.Y);
			FVector2D PreviousDirection = Current - Previous;
			FVector2D NextDirection = Next - Current;
			if (!PreviousDirection.Normalize() || !NextDirection.Normalize())
			{
				return false;
			}

			const FVector2D PreviousInwardNormal = GetEdgeNormal2D(PreviousDirection, bInwardUsesLeftNormal);
			const FVector2D NextInwardNormal = GetEdgeNormal2D(NextDirection, bInwardUsesLeftNormal);
			const FVector2D PreviousOffsetNormal = bOffsetInward ? PreviousInwardNormal : -PreviousInwardNormal;
			const FVector2D NextOffsetNormal = bOffsetInward ? NextInwardNormal : -NextInwardNormal;
			const FVector2D PreviousOffsetPoint = Current + PreviousOffsetNormal * Offset;
			const FVector2D NextOffsetPoint = Current + NextOffsetNormal * Offset;

			FVector2D OffsetPoint = FVector2D::ZeroVector;
			if (!IntersectLines2D(PreviousOffsetPoint, PreviousDirection, NextOffsetPoint, NextDirection, OffsetPoint))
			{
				const FVector2D FallbackNormal = (PreviousOffsetNormal + NextOffsetNormal).GetSafeNormal();
				OffsetPoint = Current + (FallbackNormal.IsNearlyZero() ? PreviousOffsetNormal : FallbackNormal) * Offset;
			}

			const FVector2D Delta = OffsetPoint - Current;
			const float MaxMiterDistance = FMath::Max(Offset * 2.5f, Offset + 1.0f);
			if (Delta.SizeSquared() > FMath::Square(MaxMiterDistance))
			{
				const FVector2D FallbackNormal = (PreviousOffsetNormal + NextOffsetNormal).GetSafeNormal();
				OffsetPoint = Current + (FallbackNormal.IsNearlyZero() ? PreviousOffsetNormal : FallbackNormal) * MaxMiterDistance;
			}

			OutLoop.Add(FVector(OffsetPoint.X, OffsetPoint.Y, Current3D.Z));
		}

		return OutLoop.Num() == PointCount;
	}

	bool BuildMiteredEaveFrame(
		const TArray<FVector>& CenterLoop,
		float Width,
		float Height,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		OutMesh.Reset();
		if (CenterLoop.Num() < 3)
		{
			return false;
		}

		const float HalfWidth = FMath::Max(Width, 0.1f) * 0.5f;
		const float HalfHeight = FMath::Max(Height, 0.1f) * 0.5f;
		TArray<FVector> InnerLoop;
		TArray<FVector> OuterLoop;
		if (!BuildOffsetLoop2D(CenterLoop, HalfWidth, true, InnerLoop)
			|| !BuildOffsetLoop2D(CenterLoop, HalfWidth, false, OuterLoop))
		{
			return false;
		}

		TArray<FVector> AllPoints;
		AllPoints.Reserve(InnerLoop.Num() * 4);
		for (int32 Index = 0; Index < InnerLoop.Num(); ++Index)
		{
			AllPoints.Add(InnerLoop[Index] + FVector(0.0f, 0.0f, HalfHeight));
			AllPoints.Add(InnerLoop[Index] - FVector(0.0f, 0.0f, HalfHeight));
			AllPoints.Add(OuterLoop[Index] + FVector(0.0f, 0.0f, HalfHeight));
			AllPoints.Add(OuterLoop[Index] - FVector(0.0f, 0.0f, HalfHeight));
		}

		FVector BodyCenter = FVector::ZeroVector;
		for (const FVector& Point : AllPoints)
		{
			BodyCenter += Point;
		}
		BodyCenter /= static_cast<float>(AllPoints.Num());

		const int32 PointCount = CenterLoop.Num();
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const int32 NextIndex = (Index + 1) % PointCount;
			const FVector InnerTopA = InnerLoop[Index] + FVector(0.0f, 0.0f, HalfHeight);
			const FVector InnerTopB = InnerLoop[NextIndex] + FVector(0.0f, 0.0f, HalfHeight);
			const FVector OuterTopA = OuterLoop[Index] + FVector(0.0f, 0.0f, HalfHeight);
			const FVector OuterTopB = OuterLoop[NextIndex] + FVector(0.0f, 0.0f, HalfHeight);
			const FVector InnerBottomA = InnerLoop[Index] - FVector(0.0f, 0.0f, HalfHeight);
			const FVector InnerBottomB = InnerLoop[NextIndex] - FVector(0.0f, 0.0f, HalfHeight);
			const FVector OuterBottomA = OuterLoop[Index] - FVector(0.0f, 0.0f, HalfHeight);
			const FVector OuterBottomB = OuterLoop[NextIndex] - FVector(0.0f, 0.0f, HalfHeight);

			AppendConvexFace({ OuterTopA, OuterTopB, InnerTopB, InnerTopA }, BodyCenter, OutMesh);
			AppendConvexFace({ OuterBottomB, OuterBottomA, InnerBottomA, InnerBottomB }, BodyCenter, OutMesh);
			AppendConvexFace({ OuterBottomA, OuterBottomB, OuterTopB, OuterTopA }, BodyCenter, OutMesh);
			AppendConvexFace({ InnerBottomB, InnerBottomA, InnerTopA, InnerTopB }, BodyCenter, OutMesh);
		}

		OutMesh.RebuildBounds();
		return OutMesh.IsValid();
	}

	void AppendMeshWithGroup(
		const FEHBRoofUnifiedMeshData& SourceMesh,
		int32 TriangleGroupID,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		if (!SourceMesh.IsValid())
		{
			return;
		}

		const int32 VertexBase = OutMesh.Vertices.Num();
		OutMesh.Vertices.Append(SourceMesh.Vertices);
		if (SourceMesh.Normals.Num() == SourceMesh.Vertices.Num())
		{
			OutMesh.Normals.Append(SourceMesh.Normals);
		}
		else
		{
			for (int32 Index = 0; Index < SourceMesh.Vertices.Num(); ++Index)
			{
				OutMesh.Normals.Add(FVector::UpVector);
			}
		}
		if (SourceMesh.UV0.Num() == SourceMesh.Vertices.Num())
		{
			OutMesh.UV0.Append(SourceMesh.UV0);
		}
		else
		{
			for (const FVector& Vertex : SourceMesh.Vertices)
			{
				OutMesh.UV0.Add(FVector2D(Vertex.X, Vertex.Y) / EHBHipRoofUVScale);
			}
		}

		const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
		for (const int32 SourceIndex : SourceMesh.Triangles)
		{
			OutMesh.Triangles.Add(VertexBase + SourceIndex);
		}
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			OutMesh.TriangleGroups.Add(TriangleGroupID);
		}
		OutMesh.RebuildBounds();
	}

	bool AppendTrianglePreservingGroup(
		const FEHBRoofUnifiedMeshData& SourceMesh,
		int32 TriangleIndex,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		if (!SourceMesh.Triangles.IsValidIndex(TriangleIndex * 3 + 2))
		{
			return false;
		}

		const int32 SourceA = SourceMesh.Triangles[TriangleIndex * 3 + 0];
		const int32 SourceB = SourceMesh.Triangles[TriangleIndex * 3 + 1];
		const int32 SourceC = SourceMesh.Triangles[TriangleIndex * 3 + 2];
		if (!SourceMesh.Vertices.IsValidIndex(SourceA)
			|| !SourceMesh.Vertices.IsValidIndex(SourceB)
			|| !SourceMesh.Vertices.IsValidIndex(SourceC))
		{
			return false;
		}

		const int32 NewBaseIndex = OutMesh.Vertices.Num();
		const int32 SourceIndices[3] = { SourceA, SourceB, SourceC };
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			const int32 SourceIndex = SourceIndices[CornerIndex];
			OutMesh.Vertices.Add(SourceMesh.Vertices[SourceIndex]);
			OutMesh.Normals.Add(SourceMesh.Normals.IsValidIndex(SourceIndex) ? SourceMesh.Normals[SourceIndex] : FVector::UpVector);
			OutMesh.UV0.Add(SourceMesh.UV0.IsValidIndex(SourceIndex)
				? SourceMesh.UV0[SourceIndex]
				: FVector2D(SourceMesh.Vertices[SourceIndex].X, SourceMesh.Vertices[SourceIndex].Y) / EHBHipRoofUVScale);
		}
		OutMesh.Triangles.Add(NewBaseIndex);
		OutMesh.Triangles.Add(NewBaseIndex + 1);
		OutMesh.Triangles.Add(NewBaseIndex + 2);
		OutMesh.TriangleGroups.Add(SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
			? SourceMesh.TriangleGroups[TriangleIndex]
			: EHBHipRoofBodyGroupID);
		return true;
	}

	bool ExtractGroupMesh(
		const FEHBRoofUnifiedMeshData& SourceMesh,
		int32 TriangleGroupID,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		OutMesh.Reset();
		if (!SourceMesh.IsValid())
		{
			return false;
		}

		const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 GroupID = SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? SourceMesh.TriangleGroups[TriangleIndex]
				: EHBHipRoofBodyGroupID;
			if (GroupID != TriangleGroupID)
			{
				continue;
			}

			AppendTrianglePreservingGroup(SourceMesh, TriangleIndex, OutMesh);
		}

		OutMesh.RebuildBounds();
		return OutMesh.IsValid();
	}

	int32 CountGroupTriangles(const FEHBRoofUnifiedMeshData& SourceMesh, int32 TriangleGroupID)
	{
		const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
		int32 MatchCount = 0;
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 GroupID = SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? SourceMesh.TriangleGroups[TriangleIndex]
				: EHBHipRoofBodyGroupID;
			if (GroupID == TriangleGroupID)
			{
				++MatchCount;
			}
		}
		return MatchCount;
	}

	int32 ResolveHipSectionGroupID(FName SectionName)
	{
		if (SectionName == TEXT("HipSideFaces") || SectionName == TEXT("RoofSideWalls") || SectionName == TEXT("GableEndWalls"))
		{
			return EHBHipRoofSideWallGroupID;
		}
		if (SectionName == TEXT("HipRidge") || SectionName == TEXT("GableRidge"))
		{
			return EHBHipRoofRidgeGroupID;
		}
		if (SectionName == TEXT("HipEaves") || SectionName == TEXT("GableEaves"))
		{
			return EHBHipRoofEaveGroupID;
		}
		if (SectionName == TEXT("HipRidges") || SectionName == TEXT("GableRakes"))
		{
			return EHBHipRoofHipRidgeGroupID;
		}
		return EHBHipRoofBodyGroupID;
	}

	bool DidUnifiedMeshChange(const FEHBRoofUnifiedMeshData& Before, const FEHBRoofUnifiedMeshData& After)
	{
		if (Before.Triangles.Num() != After.Triangles.Num()
			|| Before.Vertices.Num() != After.Vertices.Num()
			|| Before.LocalBounds.IsValid != After.LocalBounds.IsValid)
		{
			return true;
		}

		return Before.LocalBounds.IsValid
			&& (!Before.LocalBounds.Min.Equals(After.LocalBounds.Min, 0.1f)
				|| !Before.LocalBounds.Max.Equals(After.LocalBounds.Max, 0.1f));
	}

	bool ProjectBoxOnDirection(const FBox& Box, const FVector& Direction, double& OutMinProjection, double& OutMaxProjection)
	{
		if (!Box.IsValid || Direction.IsNearlyZero())
		{
			return false;
		}

		OutMinProjection = TNumericLimits<double>::Max();
		OutMaxProjection = -TNumericLimits<double>::Max();
		for (int32 XIndex = 0; XIndex < 2; ++XIndex)
		{
			for (int32 YIndex = 0; YIndex < 2; ++YIndex)
			{
				for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
				{
					const FVector Corner(
						XIndex == 0 ? Box.Min.X : Box.Max.X,
						YIndex == 0 ? Box.Min.Y : Box.Max.Y,
						ZIndex == 0 ? Box.Min.Z : Box.Max.Z);
					const double Projection = static_cast<double>(FVector::DotProduct(Corner, Direction));
					OutMinProjection = FMath::Min(OutMinProjection, Projection);
					OutMaxProjection = FMath::Max(OutMaxProjection, Projection);
				}
			}
		}

		return OutMinProjection <= OutMaxProjection;
	}

	struct FQuantizedEdgeKey
	{
		FIntVector A = FIntVector::ZeroValue;
		FIntVector B = FIntVector::ZeroValue;

		bool operator==(const FQuantizedEdgeKey& Other) const
		{
			return A == Other.A && B == Other.B;
		}
	};

	uint32 GetTypeHash(const FQuantizedEdgeKey& Key)
	{
		const uint32 HashA = HashCombine(HashCombine(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y)), ::GetTypeHash(Key.A.Z));
		const uint32 HashB = HashCombine(HashCombine(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y)), ::GetTypeHash(Key.B.Z));
		return HashCombine(HashA, HashB);
	}

	bool IsQuantizedPointLess(const FIntVector& A, const FIntVector& B)
	{
		if (A.X != B.X)
		{
			return A.X < B.X;
		}
		if (A.Y != B.Y)
		{
			return A.Y < B.Y;
		}
		return A.Z < B.Z;
	}

	FIntVector QuantizeMeshPosition(const FVector& Position, float Tolerance)
	{
		const float SafeTolerance = FMath::Max(Tolerance, 0.001f);
		return FIntVector(
			FMath::RoundToInt(Position.X / SafeTolerance),
			FMath::RoundToInt(Position.Y / SafeTolerance),
			FMath::RoundToInt(Position.Z / SafeTolerance));
	}

	FQuantizedEdgeKey MakeQuantizedEdgeKey(const FIntVector& A, const FIntVector& B)
	{
		FQuantizedEdgeKey Key;
		if (IsQuantizedPointLess(B, A))
		{
			Key.A = B;
			Key.B = A;
		}
		else
		{
			Key.A = A;
			Key.B = B;
		}
		return Key;
	}

	bool IsHipPrimaryRoofGroupID(int32 GroupID)
	{
		return GroupID == EHBHipRoofBodyGroupID
			|| GroupID == EHBHipRoofSideWallGroupID
			|| GroupID == 0;
	}

	bool IsHipTrimOrSideGroupID(int32 GroupID)
	{
		return GroupID == EHBHipRoofSideWallGroupID
			|| GroupID == EHBHipRoofRidgeGroupID
			|| GroupID == EHBHipRoofEaveGroupID
			|| GroupID == EHBHipRoofHipRidgeGroupID;
	}

	double GetTriangleArea(const FVector& A, const FVector& B, const FVector& C)
	{
		return 0.5 * static_cast<double>(FVector::CrossProduct(B - A, C - A).Length());
	}

	bool RemoveSmallHipCutArtifacts(FEHBRoofUnifiedMeshData& Mesh, float FeatureScale, const FString& RoofName)
	{
		if (!Mesh.IsValid())
		{
			return false;
		}

		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
		if (TriangleCount <= 0)
		{
			return false;
		}

		const float SafeFeatureScale = FMath::Max(FeatureScale, 1.0f);
		const double TrimThinThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * 0.35, 2.0, 8.0);
		const double TrimAreaThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * static_cast<double>(SafeFeatureScale) * 0.45, 12.0, 220.0);
		const double TrimMaxEdgeThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * 3.5, 24.0, 90.0);
		const double BodyThinThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * 0.08, 0.5, 2.0);
		const double BodyAreaThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * static_cast<double>(SafeFeatureScale) * 0.03, 1.0, 25.0);
		const double BodyMaxEdgeThreshold = FMath::Clamp(static_cast<double>(SafeFeatureScale) * 2.0, 12.0, 50.0);

		FEHBRoofUnifiedMeshData CleanedMesh;
		int32 RemovedTrimTriangles = 0;
		int32 RemovedBodyTriangles = 0;
		int32 RemovedDegenerateTriangles = 0;
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex * 3 + 0];
			const int32 BIndex = Mesh.Triangles[TriangleIndex * 3 + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex * 3 + 2];
			if (!Mesh.Vertices.IsValidIndex(AIndex)
				|| !Mesh.Vertices.IsValidIndex(BIndex)
				|| !Mesh.Vertices.IsValidIndex(CIndex))
			{
				++RemovedDegenerateTriangles;
				continue;
			}

			const FVector& A = Mesh.Vertices[AIndex];
			const FVector& B = Mesh.Vertices[BIndex];
			const FVector& C = Mesh.Vertices[CIndex];
			const double AB = static_cast<double>(FVector::Distance(A, B));
			const double BC = static_cast<double>(FVector::Distance(B, C));
			const double CA = static_cast<double>(FVector::Distance(C, A));
			const double LongestEdge = FMath::Max3(AB, BC, CA);
			const double ShortestEdge = FMath::Min3(AB, BC, CA);
			const double Area = GetTriangleArea(A, B, C);
			const double SliverHeight = LongestEdge > UE_DOUBLE_SMALL_NUMBER ? (2.0 * Area) / LongestEdge : 0.0;
			const int32 GroupID = Mesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? Mesh.TriangleGroups[TriangleIndex]
				: EHBHipRoofBodyGroupID;

			bool bRemoveTriangle = false;
			if (Area <= UE_DOUBLE_SMALL_NUMBER || LongestEdge <= UE_DOUBLE_SMALL_NUMBER)
			{
				bRemoveTriangle = true;
				++RemovedDegenerateTriangles;
			}
			else if (IsHipTrimOrSideGroupID(GroupID))
			{
				bRemoveTriangle = LongestEdge <= TrimMaxEdgeThreshold
					&& Area <= TrimAreaThreshold
					&& (ShortestEdge <= TrimThinThreshold || SliverHeight <= TrimThinThreshold);
				if (bRemoveTriangle)
				{
					++RemovedTrimTriangles;
				}
			}
			else
			{
				bRemoveTriangle = LongestEdge <= BodyMaxEdgeThreshold
					&& Area <= BodyAreaThreshold
					&& (ShortestEdge <= BodyThinThreshold || SliverHeight <= BodyThinThreshold);
				if (bRemoveTriangle)
				{
					++RemovedBodyTriangles;
				}
			}

			if (!bRemoveTriangle)
			{
				AppendTrianglePreservingGroup(Mesh, TriangleIndex, CleanedMesh);
			}
		}

		const int32 RemovedTotal = RemovedTrimTriangles + RemovedBodyTriangles + RemovedDegenerateTriangles;
		if (RemovedTotal <= 0)
		{
			return true;
		}

		CleanedMesh.RebuildBounds();
		if (!CleanedMesh.IsValid())
		{
			return false;
		}

		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] tiny cut artifact cleanup roof=%s removed=%d trim=%d body=%d degenerate=%d featureScale=%.2f trimArea=%.2f trimThin=%.2f bodyArea=%.2f bodyThin=%.2f trianglesBefore=%d trianglesAfter=%d"),
			*RoofName,
			RemovedTotal,
			RemovedTrimTriangles,
			RemovedBodyTriangles,
			RemovedDegenerateTriangles,
			SafeFeatureScale,
			TrimAreaThreshold,
			TrimThinThreshold,
			BodyAreaThreshold,
			BodyThinThreshold,
			TriangleCount,
			CleanedMesh.Triangles.Num() / 3);

		Mesh = MoveTemp(CleanedMesh);
		return true;
	}

	struct FHipProjectionClipRule
	{
		bool bValid = false;
		bool bKeepCutAwayPieces = false;
		FVector KeepDirection = FVector::ZeroVector;
		double KeepProjectionBoundary = 0.0;
	};

	struct FHipCompositeFilterStats
	{
		int32 TriangleCount = 0;
		int32 ComponentCount = 0;
		int32 SelectedComponentIndex = INDEX_NONE;
		int32 KeptComponentCount = 0;
		int32 RemovedTriangleCount = 0;
		bool bUsedProjectionBoundary = false;
		bool bFilterRan = false;
		FHipProjectionClipRule ProjectionClipRule;
	};

	bool FilterCompositeHipRoofPieces(
		FEHBRoofUnifiedMeshData& Mesh,
		bool bRemoveDisconnectedPieces,
		bool bKeepCutAwayPieces,
		const FString& RoofName,
		float JoinTolerance,
		bool bUseKeepDirection,
		const FVector& KeepDirectionOrigin,
		const FVector& KeepDirection,
		bool bUseKeepProjectionBoundary,
		double KeepProjectionBoundary,
		const FString& KeepProjectionBoundaryMode,
		FHipCompositeFilterStats* OutStats = nullptr)
	{
		if (OutStats)
		{
			*OutStats = FHipCompositeFilterStats();
		}

		if (!bRemoveDisconnectedPieces || !Mesh.IsValid())
		{
			return true;
		}

		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
		if (OutStats)
		{
			OutStats->TriangleCount = TriangleCount;
			OutStats->bFilterRan = true;
			if (bUseKeepDirection && bUseKeepProjectionBoundary && !KeepDirection.IsNearlyZero())
			{
				OutStats->ProjectionClipRule.bValid = true;
				OutStats->ProjectionClipRule.bKeepCutAwayPieces = bKeepCutAwayPieces;
				OutStats->ProjectionClipRule.KeepDirection = KeepDirection.GetSafeNormal();
				OutStats->ProjectionClipRule.KeepProjectionBoundary = KeepProjectionBoundary;
			}
		}
		if (TriangleCount <= 1)
		{
			return true;
		}

		struct FTriangleInfo
		{
			FBox Bounds = FBox(ForceInit);
			FVector Centroid = FVector::ZeroVector;
			int32 GroupID = 0;
			TStaticArray<FQuantizedEdgeKey, 3> Edges;
			bool bValid = false;
		};

		TArray<FTriangleInfo> Triangles;
		Triangles.SetNum(TriangleCount);
		TMap<FQuantizedEdgeKey, TArray<int32>> TrianglesByEdge;
		const float SafeJoinTolerance = FMath::Max(0.0f, JoinTolerance);
		const float TopologyWeldTolerance = FMath::Clamp(SafeJoinTolerance * 0.05f, 0.05f, 0.5f);
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex * 3 + 0];
			const int32 BIndex = Mesh.Triangles[TriangleIndex * 3 + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex * 3 + 2];
			if (!Mesh.Vertices.IsValidIndex(AIndex)
				|| !Mesh.Vertices.IsValidIndex(BIndex)
				|| !Mesh.Vertices.IsValidIndex(CIndex))
			{
				continue;
			}

			const FVector& A = Mesh.Vertices[AIndex];
			const FVector& B = Mesh.Vertices[BIndex];
			const FVector& C = Mesh.Vertices[CIndex];
			FTriangleInfo& Info = Triangles[TriangleIndex];
			Info.Bounds += A;
			Info.Bounds += B;
			Info.Bounds += C;
			Info.Centroid = (A + B + C) / 3.0f;
			Info.GroupID = Mesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? Mesh.TriangleGroups[TriangleIndex]
				: EHBHipRoofBodyGroupID;
			Info.bValid = true;

			const FIntVector QA = QuantizeMeshPosition(A, TopologyWeldTolerance);
			const FIntVector QB = QuantizeMeshPosition(B, TopologyWeldTolerance);
			const FIntVector QC = QuantizeMeshPosition(C, TopologyWeldTolerance);
			Info.Edges[0] = MakeQuantizedEdgeKey(QA, QB);
			Info.Edges[1] = MakeQuantizedEdgeKey(QB, QC);
			Info.Edges[2] = MakeQuantizedEdgeKey(QC, QA);
			for (const FQuantizedEdgeKey& Edge : Info.Edges)
			{
				TrianglesByEdge.FindOrAdd(Edge).Add(TriangleIndex);
			}
		}

		struct FCompositeComponent
		{
			TArray<int32> TriangleIndices;
			FBox Bounds = FBox(ForceInit);
			FVector CentroidSum = FVector::ZeroVector;
			int32 PrimaryTriangleCount = 0;

			FVector GetCentroid() const
			{
				return TriangleIndices.IsEmpty()
					? FVector::ZeroVector
					: CentroidSum / static_cast<float>(TriangleIndices.Num());
			}
		};

		TArray<int32> ComponentByTriangle;
		ComponentByTriangle.Init(INDEX_NONE, TriangleCount);
		TArray<FCompositeComponent> Components;
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			if (!Triangles[TriangleIndex].bValid || ComponentByTriangle[TriangleIndex] != INDEX_NONE)
			{
				continue;
			}

			const int32 ComponentIndex = Components.AddDefaulted();
			FCompositeComponent& Component = Components[ComponentIndex];
			TArray<int32> Queue;
			Queue.Add(TriangleIndex);
			ComponentByTriangle[TriangleIndex] = ComponentIndex;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 CurrentTriangle = Queue[QueueIndex];
				const FTriangleInfo& CurrentInfo = Triangles[CurrentTriangle];
				Component.TriangleIndices.Add(CurrentTriangle);
				Component.Bounds += CurrentInfo.Bounds;
				Component.CentroidSum += CurrentInfo.Centroid;
				if (IsHipPrimaryRoofGroupID(CurrentInfo.GroupID))
				{
					++Component.PrimaryTriangleCount;
				}

				for (const FQuantizedEdgeKey& Edge : CurrentInfo.Edges)
				{
					const TArray<int32>* CandidateTriangles = TrianglesByEdge.Find(Edge);
					if (!CandidateTriangles)
					{
						continue;
					}

					for (const int32 CandidateTriangle : *CandidateTriangles)
					{
						if (!Triangles[CandidateTriangle].bValid || ComponentByTriangle[CandidateTriangle] != INDEX_NONE)
						{
							continue;
						}

						ComponentByTriangle[CandidateTriangle] = ComponentIndex;
						Queue.Add(CandidateTriangle);
					}
				}
			}
		}

		if (OutStats)
		{
			OutStats->ComponentCount = Components.Num();
		}
		if (Components.Num() <= 1)
		{
			UE_LOG(
				LogEHBHipRoof,
				Display,
				TEXT("[EHB HipRoof] connected-piece filter skipped roof=%s reason=single-component components=%d triangles=%d topologyTolerance=%.2f joinTolerance=%.2f"),
				*RoofName,
				Components.Num(),
				TriangleCount,
				TopologyWeldTolerance,
				SafeJoinTolerance);
			return true;
		}

		bool bHasPrimaryComponents = false;
		for (const FCompositeComponent& Component : Components)
		{
			if (Component.PrimaryTriangleCount > 0)
			{
				bHasPrimaryComponents = true;
				break;
			}
		}

		int32 SelectedComponentIndex = INDEX_NONE;
		const FVector SafeKeepDirection = KeepDirection.GetSafeNormal();
		const bool bUseDirectionalScore = bUseKeepDirection && !SafeKeepDirection.IsNearlyZero();
		double SelectedScore = bUseDirectionalScore
			? (bKeepCutAwayPieces ? TNumericLimits<double>::Max() : -TNumericLimits<double>::Max())
			: (bKeepCutAwayPieces ? -TNumericLimits<double>::Max() : TNumericLimits<double>::Max());
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
		{
			const FCompositeComponent& Component = Components[ComponentIndex];
			if (Component.TriangleIndices.IsEmpty()
				|| (bHasPrimaryComponents && Component.PrimaryTriangleCount <= 0))
			{
				continue;
			}

			const FVector ComponentCentroid = Component.GetCentroid();
			const double Score = bUseDirectionalScore
				? static_cast<double>(FVector::DotProduct(ComponentCentroid - KeepDirectionOrigin, SafeKeepDirection))
				: ComponentCentroid.SquaredLength();
			const bool bBetterComponent = bUseDirectionalScore
				? (bKeepCutAwayPieces ? Score < SelectedScore : Score > SelectedScore)
				: (bKeepCutAwayPieces ? Score > SelectedScore : Score < SelectedScore);
			if (bBetterComponent)
			{
				SelectedScore = Score;
				SelectedComponentIndex = ComponentIndex;
			}
		}

		if (!Components.IsValidIndex(SelectedComponentIndex))
		{
			return false;
		}

		TArray<bool> bKeepComponent;
		bKeepComponent.Init(false, Components.Num());
		bool bUsedProjectionBoundary = false;
		int32 BoundaryKeptPrimaryComponents = 0;
		int32 BoundaryKeptComponents = 0;
		if (bUseDirectionalScore && bUseKeepProjectionBoundary)
		{
			for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
			{
				const FCompositeComponent& Component = Components[ComponentIndex];
				if (Component.TriangleIndices.IsEmpty())
				{
					continue;
				}

				double ComponentMinProjection = 0.0;
				double ComponentMaxProjection = 0.0;
				if (!ProjectBoxOnDirection(Component.Bounds, SafeKeepDirection, ComponentMinProjection, ComponentMaxProjection))
				{
					continue;
				}

				const bool bKeepByProjection = bKeepCutAwayPieces
					? ComponentMinProjection <= KeepProjectionBoundary
					: ComponentMaxProjection >= KeepProjectionBoundary;
				if (bKeepByProjection)
				{
					bKeepComponent[ComponentIndex] = true;
					++BoundaryKeptComponents;
					if (Component.PrimaryTriangleCount > 0)
					{
						++BoundaryKeptPrimaryComponents;
					}
				}
			}

			bUsedProjectionBoundary = BoundaryKeptComponents > 0
				&& (!bHasPrimaryComponents || BoundaryKeptPrimaryComponents > 0);
			if (!bUsedProjectionBoundary)
			{
				bKeepComponent.Init(false, Components.Num());
				BoundaryKeptComponents = 0;
				BoundaryKeptPrimaryComponents = 0;
			}
		}

		if (!bUsedProjectionBoundary)
		{
			bKeepComponent[SelectedComponentIndex] = true;

			const FBox SelectedExpandedBounds = Components[SelectedComponentIndex].Bounds.ExpandBy(SafeJoinTolerance);
			for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
			{
				if (ComponentIndex == SelectedComponentIndex || Components[ComponentIndex].PrimaryTriangleCount > 0)
				{
					continue;
				}

				if (SelectedExpandedBounds.Intersect(Components[ComponentIndex].Bounds))
				{
					bKeepComponent[ComponentIndex] = true;
				}
			}
		}

		FEHBRoofUnifiedMeshData FilteredMesh;
		int32 KeptComponents = 0;
		int32 RemovedTriangleCount = 0;
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
		{
			if (bKeepComponent[ComponentIndex])
			{
				++KeptComponents;
			}
		}

		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 ComponentIndex = ComponentByTriangle.IsValidIndex(TriangleIndex)
				? ComponentByTriangle[TriangleIndex]
				: INDEX_NONE;
			if (!bKeepComponent.IsValidIndex(ComponentIndex) || !bKeepComponent[ComponentIndex])
			{
				++RemovedTriangleCount;
				continue;
			}

			AppendTrianglePreservingGroup(Mesh, TriangleIndex, FilteredMesh);
		}

		FilteredMesh.RebuildBounds();
		if (OutStats)
		{
			OutStats->SelectedComponentIndex = SelectedComponentIndex;
			OutStats->KeptComponentCount = KeptComponents;
			OutStats->RemovedTriangleCount = RemovedTriangleCount;
			OutStats->bUsedProjectionBoundary = bUsedProjectionBoundary;
		}
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] connected-piece filter roof=%s components=%d selected=%d keptComponents=%d removedTris=%d keepCutAway=%d directional=%d projectionBoundary=%d boundaryKept=%d boundaryPrimaryKept=%d boundary=%.2f boundaryMode=%s score=%.2f origin=%s direction=%s topologyTolerance=%.2f joinTolerance=%.2f"),
			*RoofName,
			Components.Num(),
			SelectedComponentIndex,
			KeptComponents,
			RemovedTriangleCount,
			bKeepCutAwayPieces ? 1 : 0,
			bUseDirectionalScore ? 1 : 0,
			bUsedProjectionBoundary ? 1 : 0,
			BoundaryKeptComponents,
			BoundaryKeptPrimaryComponents,
			KeepProjectionBoundary,
			*KeepProjectionBoundaryMode,
			SelectedScore,
			*KeepDirectionOrigin.ToCompactString(),
			*SafeKeepDirection.ToCompactString(),
			TopologyWeldTolerance,
			SafeJoinTolerance);

		if (!FilteredMesh.IsValid())
		{
			return false;
		}

		Mesh = MoveTemp(FilteredMesh);
		return true;
	}

	struct FHipClipVertex
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector2D UV = FVector2D::ZeroVector;
	};

	bool IsInsideProjectionClip(const FHipClipVertex& Vertex, const FHipProjectionClipRule& Rule)
	{
		const double Projection = FVector::DotProduct(Vertex.Position, Rule.KeepDirection);
		constexpr double ClipTolerance = 0.01;
		return Rule.bKeepCutAwayPieces
			? Projection <= Rule.KeepProjectionBoundary + ClipTolerance
			: Projection >= Rule.KeepProjectionBoundary - ClipTolerance;
	}

	FHipClipVertex InterpolateProjectionClipVertex(
		const FHipClipVertex& From,
		const FHipClipVertex& To,
		const FHipProjectionClipRule& Rule)
	{
		const double FromProjection = FVector::DotProduct(From.Position, Rule.KeepDirection);
		const double ToProjection = FVector::DotProduct(To.Position, Rule.KeepDirection);
		const double Denominator = ToProjection - FromProjection;
		const float Alpha = FMath::IsNearlyZero(Denominator)
			? 0.0f
			: static_cast<float>((Rule.KeepProjectionBoundary - FromProjection) / Denominator);
		const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		FHipClipVertex Result;
		Result.Position = FMath::Lerp(From.Position, To.Position, SafeAlpha);
		Result.Normal = FMath::Lerp(From.Normal, To.Normal, SafeAlpha).GetSafeNormal();
		if (Result.Normal.IsNearlyZero())
		{
			Result.Normal = From.Normal.IsNearlyZero() ? FVector::UpVector : From.Normal.GetSafeNormal();
		}
		Result.UV = FMath::Lerp(From.UV, To.UV, SafeAlpha);
		return Result;
	}

	void AppendProjectionClippedTriangle(
		const FHipClipVertex& A,
		const FHipClipVertex& B,
		const FHipClipVertex& C,
		int32 TriangleGroupID,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		if (FVector::CrossProduct(B.Position - A.Position, C.Position - A.Position).SizeSquared() <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const int32 BaseIndex = OutMesh.Vertices.Num();
		OutMesh.Vertices.Add(A.Position);
		OutMesh.Vertices.Add(B.Position);
		OutMesh.Vertices.Add(C.Position);
		OutMesh.Normals.Add(A.Normal.IsNearlyZero() ? FVector::UpVector : A.Normal.GetSafeNormal());
		OutMesh.Normals.Add(B.Normal.IsNearlyZero() ? FVector::UpVector : B.Normal.GetSafeNormal());
		OutMesh.Normals.Add(C.Normal.IsNearlyZero() ? FVector::UpVector : C.Normal.GetSafeNormal());
		OutMesh.UV0.Add(A.UV);
		OutMesh.UV0.Add(B.UV);
		OutMesh.UV0.Add(C.UV);
		OutMesh.Triangles.Add(BaseIndex);
		OutMesh.Triangles.Add(BaseIndex + 1);
		OutMesh.Triangles.Add(BaseIndex + 2);
		OutMesh.TriangleGroups.Add(TriangleGroupID);
	}

	bool ClipMeshByProjectionRule(
		FEHBRoofUnifiedMeshData& Mesh,
		const FHipProjectionClipRule& Rule,
		const FString& RoofName,
		const TCHAR* MeshName)
	{
		if (!Mesh.IsValid() || !Rule.bValid || Rule.KeepDirection.IsNearlyZero())
		{
			return true;
		}

		FEHBRoofUnifiedMeshData ClippedMesh;
		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
		int32 RemovedTriangleCount = 0;
		int32 SplitTriangleCount = 0;
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex * 3 + 0];
			const int32 BIndex = Mesh.Triangles[TriangleIndex * 3 + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex * 3 + 2];
			if (!Mesh.Vertices.IsValidIndex(AIndex)
				|| !Mesh.Vertices.IsValidIndex(BIndex)
				|| !Mesh.Vertices.IsValidIndex(CIndex))
			{
				++RemovedTriangleCount;
				continue;
			}

			const auto MakeClipVertex = [&Mesh](int32 VertexIndex)
			{
				FHipClipVertex Vertex;
				Vertex.Position = Mesh.Vertices[VertexIndex];
				Vertex.Normal = Mesh.Normals.IsValidIndex(VertexIndex)
					? Mesh.Normals[VertexIndex].GetSafeNormal()
					: FVector::UpVector;
				if (Vertex.Normal.IsNearlyZero())
				{
					Vertex.Normal = FVector::UpVector;
				}
				Vertex.UV = Mesh.UV0.IsValidIndex(VertexIndex)
					? Mesh.UV0[VertexIndex]
					: FVector2D(Vertex.Position.X, Vertex.Position.Y) / EHBHipRoofUVScale;
				return Vertex;
			};

			TArray<FHipClipVertex> InputPolygon;
			InputPolygon.Reserve(3);
			InputPolygon.Add(MakeClipVertex(AIndex));
			InputPolygon.Add(MakeClipVertex(BIndex));
			InputPolygon.Add(MakeClipVertex(CIndex));

			TArray<FHipClipVertex> OutputPolygon;
			OutputPolygon.Reserve(4);
			for (int32 VertexIndex = 0; VertexIndex < InputPolygon.Num(); ++VertexIndex)
			{
				const FHipClipVertex& Current = InputPolygon[VertexIndex];
				const FHipClipVertex& Next = InputPolygon[(VertexIndex + 1) % InputPolygon.Num()];
				const bool bCurrentInside = IsInsideProjectionClip(Current, Rule);
				const bool bNextInside = IsInsideProjectionClip(Next, Rule);

				if (bCurrentInside && bNextInside)
				{
					OutputPolygon.Add(Next);
				}
				else if (bCurrentInside && !bNextInside)
				{
					OutputPolygon.Add(InterpolateProjectionClipVertex(Current, Next, Rule));
				}
				else if (!bCurrentInside && bNextInside)
				{
					OutputPolygon.Add(InterpolateProjectionClipVertex(Current, Next, Rule));
					OutputPolygon.Add(Next);
				}
			}

			if (OutputPolygon.Num() < 3)
			{
				++RemovedTriangleCount;
				continue;
			}
			if (OutputPolygon.Num() != 3)
			{
				++SplitTriangleCount;
			}

			const int32 GroupID = Mesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? Mesh.TriangleGroups[TriangleIndex]
				: EHBHipRoofBodyGroupID;
			for (int32 OutputIndex = 1; OutputIndex + 1 < OutputPolygon.Num(); ++OutputIndex)
			{
				AppendProjectionClippedTriangle(OutputPolygon[0], OutputPolygon[OutputIndex], OutputPolygon[OutputIndex + 1], GroupID, ClippedMesh);
			}
		}

		ClippedMesh.RebuildBounds();
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] projection clip accessory roof=%s mesh=%s before=%d after=%d removed=%d split=%d keepCutAway=%d direction=%s boundary=%.2f bounds=%s"),
			*RoofName,
			MeshName,
			TriangleCount,
			ClippedMesh.Triangles.Num() / 3,
			RemovedTriangleCount,
			SplitTriangleCount,
			Rule.bKeepCutAwayPieces ? 1 : 0,
			*Rule.KeepDirection.ToCompactString(),
			Rule.KeepProjectionBoundary,
			*ClippedMesh.LocalBounds.ToString());

		Mesh = MoveTemp(ClippedMesh);
		return true;
	}

	FHipProjectionClipRule MakeProjectionRuleFromKeptMeshBounds(
		const FHipProjectionClipRule& SourceRule,
		const FBox& KeptMeshBounds)
	{
		FHipProjectionClipRule Result = SourceRule;
		if (!Result.bValid || Result.KeepDirection.IsNearlyZero() || !KeptMeshBounds.IsValid)
		{
			return Result;
		}

		double KeptMinProjection = 0.0;
		double KeptMaxProjection = 0.0;
		if (!ProjectBoxOnDirection(KeptMeshBounds, Result.KeepDirection.GetSafeNormal(), KeptMinProjection, KeptMaxProjection))
		{
			return Result;
		}

		Result.KeepProjectionBoundary = Result.bKeepCutAwayPieces
			? KeptMaxProjection
			: KeptMinProjection;
		return Result;
	}

	bool PruneMeshByProjectionCentroid(
		FEHBRoofUnifiedMeshData& Mesh,
		const FHipProjectionClipRule& Rule,
		const FString& RoofName,
		const TCHAR* MeshName,
		float BoundaryTolerance,
		int32* OutRemovedTriangleCount = nullptr)
	{
		if (OutRemovedTriangleCount)
		{
			*OutRemovedTriangleCount = 0;
		}
		if (!Mesh.IsValid() || !Rule.bValid || Rule.KeepDirection.IsNearlyZero())
		{
			return true;
		}

		const FVector SafeKeepDirection = Rule.KeepDirection.GetSafeNormal();
		const double SafeBoundaryTolerance = FMath::Max(static_cast<double>(BoundaryTolerance), 0.0);
		FEHBRoofUnifiedMeshData PrunedMesh;
		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
		int32 RemovedTriangleCount = 0;
		int32 BoundaryKeptTriangleCount = 0;

		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex * 3 + 0];
			const int32 BIndex = Mesh.Triangles[TriangleIndex * 3 + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex * 3 + 2];
			if (!Mesh.Vertices.IsValidIndex(AIndex)
				|| !Mesh.Vertices.IsValidIndex(BIndex)
				|| !Mesh.Vertices.IsValidIndex(CIndex))
			{
				++RemovedTriangleCount;
				continue;
			}

			const FVector& A = Mesh.Vertices[AIndex];
			const FVector& B = Mesh.Vertices[BIndex];
			const FVector& C = Mesh.Vertices[CIndex];
			const FVector Centroid = (A + B + C) / 3.0f;
			const double CentroidProjection = FVector::DotProduct(Centroid, SafeKeepDirection);
			const bool bKeepByCentroid = Rule.bKeepCutAwayPieces
				? CentroidProjection <= Rule.KeepProjectionBoundary + SafeBoundaryTolerance
				: CentroidProjection >= Rule.KeepProjectionBoundary - SafeBoundaryTolerance;

			bool bKeepTriangle = bKeepByCentroid;
			if (!bKeepTriangle)
			{
				const double AProjection = FVector::DotProduct(A, SafeKeepDirection);
				const double BProjection = FVector::DotProduct(B, SafeKeepDirection);
				const double CProjection = FVector::DotProduct(C, SafeKeepDirection);
				const double MinProjection = FMath::Min(AProjection, FMath::Min(BProjection, CProjection));
				const double MaxProjection = FMath::Max(AProjection, FMath::Max(BProjection, CProjection));
				const bool bTouchesBoundary = MinProjection <= Rule.KeepProjectionBoundary + SafeBoundaryTolerance
					&& MaxProjection >= Rule.KeepProjectionBoundary - SafeBoundaryTolerance;
				if (bTouchesBoundary)
				{
					bKeepTriangle = true;
					++BoundaryKeptTriangleCount;
				}
			}

			if (bKeepTriangle)
			{
				AppendTrianglePreservingGroup(Mesh, TriangleIndex, PrunedMesh);
			}
			else
			{
				++RemovedTriangleCount;
			}
		}

		PrunedMesh.RebuildBounds();
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] projection prune mesh roof=%s mesh=%s before=%d after=%d removed=%d boundaryKept=%d keepCutAway=%d direction=%s boundary=%.2f tolerance=%.2f bounds=%s"),
			*RoofName,
			MeshName,
			TriangleCount,
			PrunedMesh.Triangles.Num() / 3,
			RemovedTriangleCount,
			BoundaryKeptTriangleCount,
			Rule.bKeepCutAwayPieces ? 1 : 0,
			*SafeKeepDirection.ToCompactString(),
			Rule.KeepProjectionBoundary,
			BoundaryTolerance,
			*PrunedMesh.LocalBounds.ToString());

		if (OutRemovedTriangleCount)
		{
			*OutRemovedTriangleCount = RemovedTriangleCount;
		}
		if (RemovedTriangleCount <= 0)
		{
			return true;
		}
		if (!PrunedMesh.IsValid())
		{
			return false;
		}

		Mesh = MoveTemp(PrunedMesh);
		return true;
	}

	bool BuildHipGeometryFromExtents(
		const AEHBHipRoof& Roof,
		float AlongMin,
		float AlongMax,
		float CrossMin,
		float CrossMax,
		FHipRoofGeometry& OutGeometry)
	{
		const float SafeThickness = FMath::Max(Roof.Thickness, 0.1f);
		const float SafePitchDegrees = FMath::Clamp(Roof.PitchDegrees, 1.0f, 89.0f);
		const float AlongSpan = AlongMax - AlongMin;
		const float CrossSpan = CrossMax - CrossMin;
		if (AlongSpan < EHBHipRoofMinDimension || CrossSpan < EHBHipRoofMinDimension)
		{
			return false;
		}

		const float RidgeCross = FMath::Clamp(
			(CrossMin + CrossMax) * 0.5f + CrossSpan * Roof.RidgeOffsetRatio,
			CrossMin + 1.0f,
			CrossMax - 1.0f);
		const float RidgeRun = FMath::Max(FMath::Min(RidgeCross - CrossMin, CrossMax - RidgeCross), 1.0f);
		const float RidgeHeight = RidgeRun * FMath::Tan(FMath::DegreesToRadians(SafePitchDegrees));
		const float AlongInset = FMath::Min(RidgeRun, AlongSpan * 0.5f);
		const float RidgeAlongMin = AlongMin + AlongInset;
		const float RidgeAlongMax = AlongMax - AlongInset;

		const auto MakePoint = [&Roof](float Along, float Cross, float Z)
		{
			return Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
				? FVector(Along, Cross, Z)
				: FVector(Cross, Along, Z);
		};

		OutGeometry.LowA0 = MakePoint(AlongMin, CrossMin, 0.0f);
		OutGeometry.LowA1 = MakePoint(AlongMax, CrossMin, 0.0f);
		OutGeometry.LowB0 = MakePoint(AlongMin, CrossMax, 0.0f);
		OutGeometry.LowB1 = MakePoint(AlongMax, CrossMax, 0.0f);
		OutGeometry.bHasRidge = RidgeAlongMax - RidgeAlongMin > 1.0f;
		if (OutGeometry.bHasRidge)
		{
			OutGeometry.Ridge0 = MakePoint(RidgeAlongMin, RidgeCross, RidgeHeight);
			OutGeometry.Ridge1 = MakePoint(RidgeAlongMax, RidgeCross, RidgeHeight);
		}
		else
		{
			const FVector Apex = MakePoint((AlongMin + AlongMax) * 0.5f, RidgeCross, RidgeHeight);
			OutGeometry.Ridge0 = Apex;
			OutGeometry.Ridge1 = Apex;
		}

		const FVector ThicknessOffset(0.0f, 0.0f, SafeThickness);
		OutGeometry.BottomLowA0 = OutGeometry.LowA0 - ThicknessOffset;
		OutGeometry.BottomLowA1 = OutGeometry.LowA1 - ThicknessOffset;
		OutGeometry.BottomLowB0 = OutGeometry.LowB0 - ThicknessOffset;
		OutGeometry.BottomLowB1 = OutGeometry.LowB1 - ThicknessOffset;
		return true;
	}

	bool BuildHipGeometry(const AEHBHipRoof& Roof, FHipRoofGeometry& OutGeometry)
	{
		const float SafeLength = FMath::Max(Roof.Length, EHBHipRoofMinDimension);
		const float SafeWidth = FMath::Max(Roof.Width, EHBHipRoofMinDimension);
		const float AlongMin = -SafeLength * 0.5f - Roof.EaveOffset;
		const float AlongMax = SafeLength * 0.5f + Roof.EaveOffset;
		const float CrossMin = -SafeWidth * 0.5f - Roof.EaveOffset;
		const float CrossMax = SafeWidth * 0.5f + Roof.EaveOffset;
		return BuildHipGeometryFromExtents(Roof, AlongMin, AlongMax, CrossMin, CrossMax, OutGeometry);
	}

	bool BuildRawHipMeshFromGeometry(const FHipRoofGeometry& Geometry, FEHBRoofUnifiedMeshData& OutMesh)
	{
		OutMesh.Reset();

		const FVector BodyCenter = CalculateHipBodyCenter(Geometry);

		if (Geometry.bHasRidge)
		{
			AppendHipSlopeFace({ Geometry.LowA0, Geometry.LowA1, Geometry.Ridge1, Geometry.Ridge0 }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowB1, Geometry.LowB0, Geometry.Ridge0, Geometry.Ridge1 }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowB0, Geometry.LowA0, Geometry.Ridge0 }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowA1, Geometry.LowB1, Geometry.Ridge1 }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
		}
		else
		{
			const FVector Apex = Geometry.Ridge0;
			AppendHipSlopeFace({ Geometry.LowA0, Geometry.LowA1, Apex }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowA1, Geometry.LowB1, Apex }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowB1, Geometry.LowB0, Apex }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
			AppendHipSlopeFace({ Geometry.LowB0, Geometry.LowA0, Apex }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
		}

		AppendConvexFace(
			{ Geometry.BottomLowB0, Geometry.BottomLowB1, Geometry.BottomLowA1, Geometry.BottomLowA0 },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendConvexFace(
			{ Geometry.LowA0, Geometry.BottomLowA0, Geometry.BottomLowA1, Geometry.LowA1 },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendConvexFace(
			{ Geometry.LowA1, Geometry.BottomLowA1, Geometry.BottomLowB1, Geometry.LowB1 },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendConvexFace(
			{ Geometry.LowB1, Geometry.BottomLowB1, Geometry.BottomLowB0, Geometry.LowB0 },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendConvexFace(
			{ Geometry.LowB0, Geometry.BottomLowB0, Geometry.BottomLowA0, Geometry.LowA0 },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);

		OutMesh.RebuildBounds();
		return OutMesh.IsValid();
	}

	FVector MakeHalfHipPoint(const AEHBHipRoof& Roof, float Along, float Cross, float Z)
	{
		return Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FVector(Along, Cross, Z)
			: FVector(Cross, Along, Z);
	}

	bool BuildHalfHipGeometry(const AEHBHipRoof& Roof, FHalfHipRoofGeometry& OutGeometry)
	{
		FHipRoofGeometry FullGeometry;
		if (!BuildHipGeometry(Roof, FullGeometry))
		{
			return false;
		}

		const float AlongMin = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FullGeometry.LowA0.X
			: FullGeometry.LowA0.Y;
		const float AlongMax = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FullGeometry.LowA1.X
			: FullGeometry.LowA1.Y;
		const float CrossMin = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FullGeometry.LowA0.Y
			: FullGeometry.LowA0.X;
		const float CrossMax = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FullGeometry.LowB0.Y
			: FullGeometry.LowB0.X;
		const float RidgeCross = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FullGeometry.Ridge0.Y
			: FullGeometry.Ridge0.X;
		const float RidgeHeight = FullGeometry.Ridge0.Z;
		const float SafeThickness = FMath::Max(Roof.Thickness, 0.1f);

		const bool bKeepPositiveCross = Roof.HalfHipKeepSide == EEHBHalfHipRoofKeepSide::PositiveAlongAxis;
		const float KeptCross = bKeepPositiveCross ? CrossMax : CrossMin;
		OutGeometry.LowACut = MakeHalfHipPoint(Roof, AlongMin, KeptCross, 0.0f);
		OutGeometry.LowAEnd = MakeHalfHipPoint(Roof, AlongMax, KeptCross, 0.0f);
		OutGeometry.LowBCut = MakeHalfHipPoint(Roof, AlongMin, RidgeCross, 0.0f);
		OutGeometry.LowBEnd = MakeHalfHipPoint(Roof, AlongMax, RidgeCross, 0.0f);
		OutGeometry.RidgeCut = FullGeometry.Ridge0;
		OutGeometry.RidgeEnd = FullGeometry.Ridge1;
		OutGeometry.bKeepPositiveCross = bKeepPositiveCross;

		const FVector ThicknessOffset(0.0f, 0.0f, SafeThickness);
		OutGeometry.BottomLowACut = OutGeometry.LowACut - ThicknessOffset;
		OutGeometry.BottomLowBCut = OutGeometry.LowBCut - ThicknessOffset;
		OutGeometry.BottomLowAEnd = OutGeometry.LowAEnd - ThicknessOffset;
		OutGeometry.BottomLowBEnd = OutGeometry.LowBEnd - ThicknessOffset;
		return true;
	}

	bool BuildRawHalfHipMeshFromGeometry(const FHalfHipRoofGeometry& Geometry, FEHBRoofUnifiedMeshData& OutMesh)
	{
		OutMesh.Reset();

		const FVector BodyCenter = CalculateHalfHipBodyCenter(Geometry);

		AppendCleanHipSlopeFace({ Geometry.LowACut, Geometry.LowAEnd, Geometry.RidgeEnd, Geometry.RidgeCut }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
		AppendCleanHipSlopeFace({ Geometry.LowBCut, Geometry.LowACut, Geometry.RidgeCut }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);
		AppendCleanHipSlopeFace({ Geometry.LowAEnd, Geometry.LowBEnd, Geometry.RidgeEnd }, BodyCenter, OutMesh, EHBHipRoofBodyGroupID);

		AppendCleanConvexFace(
			{ Geometry.BottomLowBCut, Geometry.BottomLowBEnd, Geometry.BottomLowAEnd, Geometry.BottomLowACut },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendCleanConvexFace(
			{ Geometry.LowACut, Geometry.BottomLowACut, Geometry.BottomLowAEnd, Geometry.LowAEnd },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendCleanConvexFace(
			{ Geometry.LowAEnd, Geometry.BottomLowAEnd, Geometry.BottomLowBEnd, Geometry.LowBEnd, Geometry.RidgeEnd },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendCleanConvexFace(
			{ Geometry.LowBEnd, Geometry.BottomLowBEnd, Geometry.BottomLowBCut, Geometry.LowBCut, Geometry.RidgeCut, Geometry.RidgeEnd },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);
		AppendCleanConvexFace(
			{ Geometry.BottomLowACut, Geometry.BottomLowBCut, Geometry.LowBCut, Geometry.RidgeCut, Geometry.LowACut },
			BodyCenter,
			OutMesh,
			EHBHipRoofSideWallGroupID);

		OutMesh.RebuildBounds();
		return OutMesh.IsValid();
	}

	struct FHipVisualAccessoryMeshes
	{
		FEHBRoofUnifiedMeshData RidgeMesh;
		FEHBRoofUnifiedMeshData EaveMesh;
		FEHBRoofUnifiedMeshData HipRidgeMesh;

		void Reset()
		{
			RidgeMesh.Reset();
			EaveMesh.Reset();
			HipRidgeMesh.Reset();
		}
	};

	bool BuildHipVisualAccessoryMeshesFromGeometry(
		const AEHBHipRoof& Roof,
		const FHipRoofGeometry& Geometry,
		FHipVisualAccessoryMeshes& OutMeshes)
	{
		OutMeshes.Reset();

		const FVector BodyCenter = CalculateHipBodyCenter(Geometry);
		const FVector SideANormal = Geometry.bHasRidge
			? CalculateOutwardFaceNormal({ Geometry.LowA0, Geometry.LowA1, Geometry.Ridge1, Geometry.Ridge0 }, BodyCenter)
			: CalculateOutwardFaceNormal({ Geometry.LowA0, Geometry.LowA1, Geometry.Ridge0 }, BodyCenter);
		const FVector SideBNormal = Geometry.bHasRidge
			? CalculateOutwardFaceNormal({ Geometry.LowB1, Geometry.LowB0, Geometry.Ridge0, Geometry.Ridge1 }, BodyCenter)
			: CalculateOutwardFaceNormal({ Geometry.LowB1, Geometry.LowB0, Geometry.Ridge0 }, BodyCenter);
		const FVector End0Normal = CalculateOutwardFaceNormal({ Geometry.LowB0, Geometry.LowA0, Geometry.Ridge0 }, BodyCenter);
		const FVector End1Normal = Geometry.bHasRidge
			? CalculateOutwardFaceNormal({ Geometry.LowA1, Geometry.LowB1, Geometry.Ridge1 }, BodyCenter)
			: CalculateOutwardFaceNormal({ Geometry.LowA1, Geometry.LowB1, Geometry.Ridge0 }, BodyCenter);

		const FVector Ridge0TopDirection = Geometry.bHasRidge
			? NormalizeCapTopDirection(SideANormal + SideBNormal + End0Normal)
			: NormalizeCapTopDirection(SideANormal + SideBNormal + End0Normal + End1Normal);
		const FVector Ridge1TopDirection = Geometry.bHasRidge
			? NormalizeCapTopDirection(SideANormal + SideBNormal + End1Normal)
			: Ridge0TopDirection;
		const FVector LowA0TopDirection = NormalizeCapTopDirection(SideANormal + End0Normal);
		const FVector LowB0TopDirection = NormalizeCapTopDirection(SideBNormal + End0Normal);
		const FVector LowA1TopDirection = NormalizeCapTopDirection(SideANormal + End1Normal);
		const FVector LowB1TopDirection = NormalizeCapTopDirection(SideBNormal + End1Normal);

		if (Roof.bGenerateRidge && Geometry.bHasRidge)
		{
			FEHBRoofUnifiedMeshData RawRidgeMesh;
			AppendRoofEdgeCapWithNodeTops(
				Geometry.Ridge0,
				Geometry.Ridge1,
				SideANormal,
				SideBNormal,
				Ridge0TopDirection,
				Ridge1TopDirection,
				Roof.RidgeWidth,
				Roof.RidgeHeight,
				RawRidgeMesh);
			AppendMeshWithGroup(RawRidgeMesh, EHBHipRoofRidgeGroupID, OutMeshes.RidgeMesh);
		}

		if (Roof.bGenerateEaves)
		{
			FEHBRoofUnifiedMeshData RawEaveMesh;
			if (BuildMiteredEaveFrame(
					{ Geometry.LowA0, Geometry.LowA1, Geometry.LowB1, Geometry.LowB0 },
					Roof.EaveWidth,
					Roof.EaveHeight,
					RawEaveMesh))
			{
				AppendMeshWithGroup(RawEaveMesh, EHBHipRoofEaveGroupID, OutMeshes.EaveMesh);
			}
		}

		if (Roof.bGenerateGableRakes)
		{
			const auto AppendHipRidge = [&OutMeshes, &Roof](
				const FVector& Start,
				const FVector& End,
				const FVector& FaceNormalA,
				const FVector& FaceNormalB,
				const FVector& StartTopDirection,
				const FVector& EndTopDirection)
			{
				FEHBRoofUnifiedMeshData RawHipRidgeMesh;
				AppendRoofEdgeCapWithNodeTops(
					Start,
					End,
					FaceNormalA,
					FaceNormalB,
					StartTopDirection,
					EndTopDirection,
					Roof.GableRakeWidth,
					Roof.GableRakeHeight,
					RawHipRidgeMesh);
				AppendMeshWithGroup(RawHipRidgeMesh, EHBHipRoofHipRidgeGroupID, OutMeshes.HipRidgeMesh);
			};

			if (Geometry.bHasRidge)
			{
				AppendHipRidge(Geometry.LowA0, Geometry.Ridge0, SideANormal, End0Normal, LowA0TopDirection, Ridge0TopDirection);
				AppendHipRidge(Geometry.LowB0, Geometry.Ridge0, End0Normal, SideBNormal, LowB0TopDirection, Ridge0TopDirection);
				AppendHipRidge(Geometry.LowA1, Geometry.Ridge1, SideANormal, End1Normal, LowA1TopDirection, Ridge1TopDirection);
				AppendHipRidge(Geometry.LowB1, Geometry.Ridge1, End1Normal, SideBNormal, LowB1TopDirection, Ridge1TopDirection);
			}
			else
			{
				AppendHipRidge(Geometry.LowA0, Geometry.Ridge0, SideANormal, End0Normal, LowA0TopDirection, Ridge0TopDirection);
				AppendHipRidge(Geometry.LowA1, Geometry.Ridge0, End1Normal, SideANormal, LowA1TopDirection, Ridge0TopDirection);
				AppendHipRidge(Geometry.LowB1, Geometry.Ridge0, SideBNormal, End1Normal, LowB1TopDirection, Ridge0TopDirection);
				AppendHipRidge(Geometry.LowB0, Geometry.Ridge0, End0Normal, SideBNormal, LowB0TopDirection, Ridge0TopDirection);
			}
		}

		OutMeshes.RidgeMesh.RebuildBounds();
		OutMeshes.EaveMesh.RebuildBounds();
		OutMeshes.HipRidgeMesh.RebuildBounds();
		return true;
	}

	bool BuildHalfHipVisualAccessoryMeshesFromGeometry(
		const AEHBHipRoof& Roof,
		const FHalfHipRoofGeometry& Geometry,
		FHipVisualAccessoryMeshes& OutMeshes)
	{
		OutMeshes.Reset();

		const FVector BodyCenter = CalculateHalfHipBodyCenter(Geometry);
		const FVector KeptSlopeNormal = CalculateOutwardFaceNormal(
			CleanPolygonVertices({ Geometry.LowACut, Geometry.LowAEnd, Geometry.RidgeEnd, Geometry.RidgeCut }),
			BodyCenter);
		const FVector EndCutNormal = CalculateOutwardFaceNormal(
			CleanPolygonVertices({ Geometry.LowBCut, Geometry.LowACut, Geometry.RidgeCut }),
			BodyCenter);
		const FVector EndNormal = CalculateOutwardFaceNormal(
			CleanPolygonVertices({ Geometry.LowAEnd, Geometry.LowBEnd, Geometry.RidgeEnd }),
			BodyCenter);
		const FVector CutNormal = CalculateOutwardFaceNormal(
			CleanPolygonVertices({ Geometry.LowBEnd, Geometry.LowBCut, Geometry.RidgeCut, Geometry.RidgeEnd }),
			BodyCenter);

		const FVector RidgeCutTopDirection = NormalizeCapTopDirection(KeptSlopeNormal + EndCutNormal + CutNormal);
		const FVector RidgeEndTopDirection = NormalizeCapTopDirection(KeptSlopeNormal + CutNormal + EndNormal);
		const FVector LowACutTopDirection = NormalizeCapTopDirection(KeptSlopeNormal + EndCutNormal);
		const FVector LowBCutTopDirection = NormalizeCapTopDirection(EndCutNormal + CutNormal);
		const FVector LowAEndTopDirection = NormalizeCapTopDirection(KeptSlopeNormal + EndNormal);
		const FVector LowBEndTopDirection = NormalizeCapTopDirection(CutNormal + EndNormal);

		if (Roof.bGenerateRidge && !Geometry.RidgeCut.Equals(Geometry.RidgeEnd, 0.01f))
		{
			FEHBRoofUnifiedMeshData RawRidgeMesh;
			AppendRoofEdgeCapWithNodeTops(
				Geometry.RidgeCut,
				Geometry.RidgeEnd,
				KeptSlopeNormal,
				CutNormal,
				RidgeCutTopDirection,
				RidgeEndTopDirection,
				Roof.RidgeWidth,
				Roof.RidgeHeight,
				RawRidgeMesh);
			AppendMeshWithGroup(RawRidgeMesh, EHBHipRoofRidgeGroupID, OutMeshes.RidgeMesh);
		}

		if (Roof.bGenerateEaves)
		{
			TArray<FVector> CenterLoop;
			if (Geometry.bKeepPositiveCross)
			{
				CenterLoop.Add(Geometry.LowACut);
				CenterLoop.Add(Geometry.LowAEnd);
				CenterLoop.Add(Geometry.LowBEnd);
				CenterLoop.Add(Geometry.LowBCut);
			}
			else
			{
				CenterLoop.Add(Geometry.LowBCut);
				CenterLoop.Add(Geometry.LowBEnd);
				CenterLoop.Add(Geometry.LowAEnd);
				CenterLoop.Add(Geometry.LowACut);
			}

			FEHBRoofUnifiedMeshData RawEaveMesh;
			if (BuildMiteredEaveFrame(
					CenterLoop,
					Roof.EaveWidth,
					Roof.EaveHeight,
					RawEaveMesh))
			{
				AppendMeshWithGroup(RawEaveMesh, EHBHipRoofEaveGroupID, OutMeshes.EaveMesh);
			}
		}

		if (Roof.bGenerateGableRakes)
		{
			const auto AppendHalfHipRidge = [&OutMeshes, &Roof](
				const FVector& Start,
				const FVector& End,
				const FVector& FaceNormalA,
				const FVector& FaceNormalB,
				const FVector& StartTopDirection,
				const FVector& EndTopDirection)
			{
				if (Start.Equals(End, 0.01f))
				{
					return;
				}

				FEHBRoofUnifiedMeshData RawHipRidgeMesh;
				AppendRoofEdgeCapWithNodeTops(
					Start,
					End,
					FaceNormalA,
					FaceNormalB,
					StartTopDirection,
					EndTopDirection,
					Roof.GableRakeWidth,
					Roof.GableRakeHeight,
					RawHipRidgeMesh);
				AppendMeshWithGroup(RawHipRidgeMesh, EHBHipRoofHipRidgeGroupID, OutMeshes.HipRidgeMesh);
			};

			AppendHalfHipRidge(Geometry.LowAEnd, Geometry.RidgeEnd, KeptSlopeNormal, EndNormal, LowAEndTopDirection, RidgeEndTopDirection);
			AppendHalfHipRidge(Geometry.LowBEnd, Geometry.RidgeEnd, EndNormal, CutNormal, LowBEndTopDirection, RidgeEndTopDirection);
			AppendHalfHipRidge(Geometry.LowACut, Geometry.RidgeCut, KeptSlopeNormal, EndCutNormal, LowACutTopDirection, RidgeCutTopDirection);
			AppendHalfHipRidge(Geometry.LowBCut, Geometry.RidgeCut, EndCutNormal, CutNormal, LowBCutTopDirection, RidgeCutTopDirection);
		}

		OutMeshes.RidgeMesh.RebuildBounds();
		OutMeshes.EaveMesh.RebuildBounds();
		OutMeshes.HipRidgeMesh.RebuildBounds();
		return true;
	}

	bool BuildHalfHipVisualAccessoryMeshes(const AEHBHipRoof& Roof, FHipVisualAccessoryMeshes& OutMeshes)
	{
		FHalfHipRoofGeometry Geometry;
		if (!BuildHalfHipGeometry(Roof, Geometry))
		{
			return false;
		}
		return BuildHalfHipVisualAccessoryMeshesFromGeometry(Roof, Geometry, OutMeshes);
	}

	bool BuildHipVisualAccessoryMeshes(const AEHBHipRoof& Roof, FHipVisualAccessoryMeshes& OutMeshes)
	{
		FHipRoofGeometry Geometry;
		if (!BuildHipGeometry(Roof, Geometry))
		{
			return false;
		}
		return BuildHipVisualAccessoryMeshesFromGeometry(Roof, Geometry, OutMeshes);
	}

	bool TryBuildSemanticTrimmedHipRoofMesh(
		const AEHBHipRoof& Roof,
		const FEHBRoofUnifiedMeshData& OriginalMesh,
		const FEHBRoofUnifiedMeshData& FilteredMesh,
		const FHipProjectionClipRule& ClipRule,
		FEHBRoofUnifiedMeshData& OutMesh,
		FHipRoofGeometry& OutGeometry)
	{
		OutMesh.Reset();
		if (!OriginalMesh.LocalBounds.IsValid || !FilteredMesh.LocalBounds.IsValid || !ClipRule.bValid || ClipRule.KeepDirection.IsNearlyZero())
		{
			return false;
		}

		FVector LocalMin = OriginalMesh.LocalBounds.Min;
		FVector LocalMax = OriginalMesh.LocalBounds.Max;
		const FVector OriginalSize = OriginalMesh.LocalBounds.GetSize();
		const bool bTrimLocalX = FMath::Abs(ClipRule.KeepDirection.X) >= FMath::Abs(ClipRule.KeepDirection.Y);
		const bool bTrimAlongRoofAxis = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? bTrimLocalX
			: !bTrimLocalX;
		if (!bTrimAlongRoofAxis)
		{
			UE_LOG(
				LogEHBHipRoof,
				Display,
				TEXT("[EHB HipRoof] semantic trim skipped roof=%s reason=not-along-ridge-axis clipDirection=%s axisMode=%d"),
				*Roof.GetName(),
				*ClipRule.KeepDirection.ToCompactString(),
				static_cast<int32>(Roof.AxisMode));
			return false;
		}

		const float MinTrimDistance = FMath::Max(2.0f, FMath::Max(Roof.Thickness, 1.0f) * 0.25f);

		if (bTrimLocalX)
		{
			if (ClipRule.KeepDirection.X < 0.0f)
			{
				LocalMax.X = FMath::Min(LocalMax.X, FilteredMesh.LocalBounds.Max.X);
			}
			else
			{
				LocalMin.X = FMath::Max(LocalMin.X, FilteredMesh.LocalBounds.Min.X);
			}
			if (OriginalSize.X - (LocalMax.X - LocalMin.X) < MinTrimDistance)
			{
				return false;
			}
		}
		else
		{
			if (ClipRule.KeepDirection.Y < 0.0f)
			{
				LocalMax.Y = FMath::Min(LocalMax.Y, FilteredMesh.LocalBounds.Max.Y);
			}
			else
			{
				LocalMin.Y = FMath::Max(LocalMin.Y, FilteredMesh.LocalBounds.Min.Y);
			}
			if (OriginalSize.Y - (LocalMax.Y - LocalMin.Y) < MinTrimDistance)
			{
				return false;
			}
		}

		FHipRoofGeometry OriginalGeometry;
		if (!BuildHipGeometry(Roof, OriginalGeometry))
		{
			return false;
		}

		if (OriginalGeometry.bHasRidge)
		{
			const float CutCoordinate = bTrimLocalX
				? (ClipRule.KeepDirection.X < 0.0f ? LocalMax.X : LocalMin.X)
				: (ClipRule.KeepDirection.Y < 0.0f ? LocalMax.Y : LocalMin.Y);
			const float RidgeCoordinate0 = bTrimLocalX ? OriginalGeometry.Ridge0.X : OriginalGeometry.Ridge0.Y;
			const float RidgeCoordinate1 = bTrimLocalX ? OriginalGeometry.Ridge1.X : OriginalGeometry.Ridge1.Y;
			const float RidgeMin = FMath::Min(RidgeCoordinate0, RidgeCoordinate1);
			const float RidgeMax = FMath::Max(RidgeCoordinate0, RidgeCoordinate1);
			const float RidgeInteriorTolerance = FMath::Max(10.0f, Roof.Thickness);
			if (CutCoordinate > RidgeMin + RidgeInteriorTolerance && CutCoordinate < RidgeMax - RidgeInteriorTolerance)
			{
				UE_LOG(
					LogEHBHipRoof,
					Display,
					TEXT("[EHB HipRoof] semantic trim skipped roof=%s reason=cut-through-ridge cut=%.2f ridge=(%.2f, %.2f) tolerance=%.2f originalBounds=%s filteredBounds=%s"),
					*Roof.GetName(),
					CutCoordinate,
					RidgeMin,
					RidgeMax,
					RidgeInteriorTolerance,
					*OriginalMesh.LocalBounds.ToString(),
					*FilteredMesh.LocalBounds.ToString());
				return false;
			}
		}

		if (LocalMax.X - LocalMin.X < EHBHipRoofMinDimension || LocalMax.Y - LocalMin.Y < EHBHipRoofMinDimension)
		{
			return false;
		}

		const float AlongMin = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX ? LocalMin.X : LocalMin.Y;
		const float AlongMax = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX ? LocalMax.X : LocalMax.Y;
		const float CrossMin = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX ? LocalMin.Y : LocalMin.X;
		const float CrossMax = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX ? LocalMax.Y : LocalMax.X;
		if (!BuildHipGeometryFromExtents(Roof, AlongMin, AlongMax, CrossMin, CrossMax, OutGeometry))
		{
			return false;
		}
		if (!BuildRawHipMeshFromGeometry(OutGeometry, OutMesh))
		{
			return false;
		}

		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] semantic trim rebuilt roof=%s axis=%s direction=%s originalBounds=%s filteredBounds=%s semanticBounds=%s"),
			*Roof.GetName(),
			bTrimLocalX ? TEXT("X") : TEXT("Y"),
			*ClipRule.KeepDirection.ToCompactString(),
			*OriginalMesh.LocalBounds.ToString(),
			*FilteredMesh.LocalBounds.ToString(),
			*OutMesh.LocalBounds.ToString());
		return true;
	}

	bool SubmitUnifiedGroupSection(
		UEHBGeneratedMeshComponent* Component,
		const FEHBRoofUnifiedMeshData& SourceMesh,
		int32 TriangleGroupID,
		int32 SectionIndex,
		FName SectionName,
		UMaterialInterface* Material)
	{
		if (!Component)
		{
			return false;
		}

		FEHBRoofUnifiedMeshData GroupMesh;
		if (!ExtractGroupMesh(SourceMesh, TriangleGroupID, GroupMesh))
		{
			return false;
		}

		TArray<FLinearColor> VertexColors;
		VertexColors.Init(FLinearColor::White, GroupMesh.Vertices.Num());
		TArray<FEHBMeshTangent> Tangents;
		Tangents.Init(FEHBMeshTangent(), GroupMesh.Vertices.Num());
		Component->CreateMeshSection_LinearColor(
			SectionIndex,
			GroupMesh.Vertices,
			GroupMesh.Triangles,
			GroupMesh.Normals,
			GroupMesh.UV0,
			VertexColors,
			Tangents,
			true);
		Component->SetMeshSectionName(SectionIndex, SectionName);
		Component->SetMaterialIfChanged(
			SectionIndex,
			Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface));
		return true;
	}

	UMaterialInterface* ResolveHipComponentMaterial(
		UEHBGeneratedMeshComponent* Component,
		UMaterialInterface* ExplicitMaterial,
		UMaterialInterface* ConfiguredDefaultMaterial,
		UMaterialInterface* FallbackMaterial)
	{
		if (ExplicitMaterial)
		{
			return ExplicitMaterial;
		}

		if (ConfiguredDefaultMaterial)
		{
			return ConfiguredDefaultMaterial;
		}

		if (Component)
		{
			if (UMaterialInterface* ExistingMaterial = Component->GetMaterial(0))
			{
				return ExistingMaterial;
			}
		}

		return FallbackMaterial
			? FallbackMaterial
			: UMaterial::GetDefaultMaterial(MD_Surface);
	}
}

AEHBHipRoof::AEHBHipRoof()
{
	ElementName = TEXT("Hip Roof");
	bGenerateGableEndWalls = false;
}

bool AEHBHipRoof::BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const
{
	OutData.Reset();
	OutData.SourceElementGuid = ElementGuid;

	FEHBRoofUnifiedMeshData SourceMesh;
	if (!BuildUncutUnifiedHipRoofMesh(SourceMesh) || !SourceMesh.IsValid())
	{
		return false;
	}

	const FTransform WorldToTarget = TargetLocalToWorld.Inverse();
	const FTransform ActorToTarget = GetActorTransform() * WorldToTarget;
	OutData.Vertices.Reserve(SourceMesh.Vertices.Num());
	OutData.Normals.Reserve(SourceMesh.Normals.Num());
	OutData.UV0.Reserve(SourceMesh.UV0.Num());
	for (int32 VertexIndex = 0; VertexIndex < SourceMesh.Vertices.Num(); ++VertexIndex)
	{
		const FVector TargetPosition = ActorToTarget.TransformPosition(SourceMesh.Vertices[VertexIndex]);
		OutData.Vertices.Add(TargetPosition);
		OutData.Normals.Add(SourceMesh.Normals.IsValidIndex(VertexIndex)
			? ActorToTarget.TransformVectorNoScale(SourceMesh.Normals[VertexIndex]).GetSafeNormal()
			: FVector::UpVector);
		OutData.UV0.Add(SourceMesh.UV0.IsValidIndex(VertexIndex)
			? SourceMesh.UV0[VertexIndex]
			: FVector2D(SourceMesh.Vertices[VertexIndex].X, SourceMesh.Vertices[VertexIndex].Y) / EHBHipRoofUVScale);
		OutData.LocalBounds += TargetPosition;
	}

	const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
	OutData.Triangles.Reserve(TriangleCount);
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 AIndex = SourceMesh.Triangles[TriangleIndex * 3 + 0];
		const int32 BIndex = SourceMesh.Triangles[TriangleIndex * 3 + 1];
		const int32 CIndex = SourceMesh.Triangles[TriangleIndex * 3 + 2];
		if (!SourceMesh.Vertices.IsValidIndex(AIndex)
			|| !SourceMesh.Vertices.IsValidIndex(BIndex)
			|| !SourceMesh.Vertices.IsValidIndex(CIndex))
		{
			continue;
		}

		FEHBMeshTriangleRef& TriangleRef = OutData.Triangles.AddDefaulted_GetRef();
		TriangleRef.VertexA = AIndex;
		TriangleRef.VertexB = BIndex;
		TriangleRef.VertexC = CIndex;
		TriangleRef.SourceComponentIndex = 0;
		TriangleRef.SourceSectionIndex = SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
			? SourceMesh.TriangleGroups[TriangleIndex]
			: EHBHipRoofBodyGroupID;
		TriangleRef.SourceTriangleIndex = TriangleIndex;
	}

	UE_LOG(
		LogEHBHipRoof,
		Verbose,
		TEXT("[EHB HipRoof] exported uncut cut source roof=%s vertices=%d triangles=%d bounds=%s"),
		*GetName(),
		OutData.Vertices.Num(),
		OutData.Triangles.Num(),
		*OutData.LocalBounds.ToString());
	return OutData.Vertices.Num() > 0 && OutData.Triangles.Num() > 0;
}

bool AEHBHipRoof::GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();

	TArray<UEHBGeneratedMeshComponent*> Components;
	GetGeneratedMeshComponents(Components);
	for (UEHBGeneratedMeshComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		const FTransform ComponentToActor = Component->GetComponentTransform() * GetActorTransform().Inverse();
		const TArray<FEHBMeshSection>& Sections = Component->GetMeshSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FEHBMeshSection& Section = Sections[SectionIndex];
			if (!Section.bSectionVisible || Section.ProcVertexBuffer.IsEmpty() || Section.ProcIndexBuffer.Num() < 3)
			{
				continue;
			}

			const int32 GroupID = ResolveHipSectionGroupID(Section.SectionName);
			const int32 VertexBase = OutMesh.Vertices.Num();
			for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
			{
				OutMesh.Vertices.Add(ComponentToActor.TransformPosition(Vertex.Position));
				OutMesh.Normals.Add(ComponentToActor.TransformVectorNoScale(Vertex.Normal).GetSafeNormal());
				OutMesh.UV0.Add(Vertex.UV0);
			}

			const int32 TriangleCount = Section.ProcIndexBuffer.Num() / 3;
			for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
			{
				const int32 LocalA = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 0]);
				const int32 LocalB = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 1]);
				const int32 LocalC = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 2]);
				if (!Section.ProcVertexBuffer.IsValidIndex(LocalA)
					|| !Section.ProcVertexBuffer.IsValidIndex(LocalB)
					|| !Section.ProcVertexBuffer.IsValidIndex(LocalC))
				{
					continue;
				}

				OutMesh.Triangles.Add(VertexBase + LocalA);
				OutMesh.Triangles.Add(VertexBase + LocalB);
				OutMesh.Triangles.Add(VertexBase + LocalC);
				OutMesh.TriangleGroups.Add(GroupID);
			}
		}
	}

	OutMesh.RebuildBounds();
	return OutMesh.IsValid() || BuildUncutUnifiedHipRoofMesh(OutMesh);
}

bool AEHBHipRoof::GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const
{
	if (!bHalfHipRoof)
	{
		return Super::GetRoofProjectionBounds(OutBounds);
	}

	FEHBRoofUnifiedMeshData RawMesh;
	if (!BuildRawRoofMesh(RawMesh) || !RawMesh.LocalBounds.IsValid)
	{
		return Super::GetRoofProjectionBounds(OutBounds);
	}

	OutBounds.Reset();
	OutBounds.Min = FVector2D(RawMesh.LocalBounds.Min.X, RawMesh.LocalBounds.Min.Y);
	OutBounds.Max = FVector2D(RawMesh.LocalBounds.Max.X, RawMesh.LocalBounds.Max.Y);
	const FVector MeshCenter = RawMesh.LocalBounds.GetCenter();
	const FVector MeshSize = RawMesh.LocalBounds.GetSize();
	OutBounds.LocalCenter = FVector(MeshCenter.X, MeshCenter.Y, 0.0f);
	OutBounds.LocalSize = FVector(MeshSize.X, MeshSize.Y, 0.0f);
	OutBounds.bIsValid = true;
	return true;
}

bool AEHBHipRoof::BuildRawRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();

	if (bHalfHipRoof)
	{
		FHalfHipRoofGeometry Geometry;
		if (!BuildHalfHipGeometry(*this, Geometry))
		{
			return false;
		}

		return BuildRawHalfHipMeshFromGeometry(Geometry, OutMesh);
	}

	FHipRoofGeometry Geometry;
	if (!BuildHipGeometry(*this, Geometry))
	{
		return false;
	}

	return BuildRawHipMeshFromGeometry(Geometry, OutMesh);
}

bool AEHBHipRoof::BuildUncutUnifiedHipRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();

	if (!BuildRawRoofMesh(OutMesh) || !OutMesh.IsValid())
	{
		UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] raw body build failed roof=%s"), *GetName());
		return false;
	}

	OutMesh.RebuildBounds();
	UE_LOG(
		LogEHBHipRoof,
		Verbose,
		TEXT("[EHB HipRoof] uncut roof body built roof=%s triangles=%d body=%d side=%d bounds=%s"),
		*GetName(),
		OutMesh.Triangles.Num() / 3,
		CountGroupTriangles(OutMesh, EHBHipRoofBodyGroupID),
		CountGroupTriangles(OutMesh, EHBHipRoofSideWallGroupID),
		*OutMesh.LocalBounds.ToString());
	return true;
}

bool AEHBHipRoof::RebuildRoofMesh()
{
	FEHBRoofUnifiedMeshData RenderMesh;
	if (!BuildUncutUnifiedHipRoofMesh(RenderMesh) || !RenderMesh.IsValid())
	{
		return false;
	}

	RenderMesh.RebuildBounds();
	const FEHBRoofUnifiedMeshData UncutRenderMesh = RenderMesh;
	if (bDebugDrawRawRoofBounds)
	{
		DrawRoofCutDebugMeshBounds(RenderMesh, FColor::Cyan, TEXT("raw unified hip roof"));
	}

	bool bRoofCutChanged = false;
	bool bHasSelectedSourceCutPass = false;
	FHipProjectionClipRule SelectedProjectionClipRule;
	bool bUseSemanticTrimGeometry = false;
	bool bUseExactProjectionPrunedRoofCut = false;
	bool bUseExactAccessorySourceCut = false;
	bool bHasRoofSourceCutOperation = false;
	FHipRoofGeometry SemanticTrimGeometry;
	if (!CutOperations.IsEmpty())
	{
		FEHBRoofUnifiedMeshData BeforeCutMesh = RenderMesh;
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] unified source-cut begin roof=%s triangles=%d bounds=%s exact=%d controlledEnvelope=%d"),
			*GetName(),
			BeforeCutMesh.Triangles.Num() / 3,
			*BeforeCutMesh.LocalBounds.ToString(),
			bUseExactSourceMeshCutters ? 1 : 0,
			bUseControlledEnvelopeCutters ? 1 : 0);

		for (const FEHBCutOperation& Operation : CutOperations)
		{
			if (!Operation.bEnabled
				|| Operation.Stage != EEHBCutStage::SourceOverlap
				|| Operation.ProjectionMode != EEHBCutProjectionMode::SourceMesh
				|| Operation.OperationType != EEHBCutOperationType::Subtract
				|| Operation.Source.SourceType != EEHBCutSourceType::Element)
			{
				continue;
			}

			AEHBElementActorBase* SourceElement = nullptr;
			if (ResolveCutSourceElement(Operation, SourceElement)
				&& IsValid(SourceElement)
				&& SourceElement != this
				&& SourceElement->IsA<AEHBRoofBase>())
			{
				bHasRoofSourceCutOperation = true;
				break;
			}
		}

		const auto ApplyCompositeFilter = [this](FEHBRoofUnifiedMeshData& CandidateMesh, const TCHAR* PassName, FHipCompositeFilterStats& OutStats)
		{
			const float MaxAccessoryDimension = FMath::Max(
				FMath::Max(RidgeWidth, RidgeHeight),
				FMath::Max(FMath::Max(EaveWidth, EaveHeight), FMath::Max(GableRakeWidth, GableRakeHeight)));
			const float CompositeJoinTolerance = FMath::Clamp(FMath::Max(MaxAccessoryDimension, Thickness) * 0.5f, 4.0f, 30.0f);

			FBox CompositeSourceBounds(ForceInit);
			for (const FEHBCutOperation& Operation : CutOperations)
			{
				if (!Operation.bEnabled
					|| Operation.Stage != EEHBCutStage::SourceOverlap
					|| Operation.ProjectionMode != EEHBCutProjectionMode::SourceMesh
					|| Operation.OperationType != EEHBCutOperationType::Subtract
					|| Operation.Source.SourceType != EEHBCutSourceType::Element)
				{
					continue;
				}

				AEHBElementActorBase* SourceElement = nullptr;
				if (!ResolveCutSourceElement(Operation, SourceElement) || !IsValid(SourceElement) || SourceElement == this)
				{
					continue;
				}

				FEHBMeshAggregateData SourceAggregate;
				if (FEHBGeneratedMeshCollector::CollectFromElement(SourceElement, GetActorTransform(), SourceAggregate)
					&& SourceAggregate.LocalBounds.IsValid)
				{
					CompositeSourceBounds += SourceAggregate.LocalBounds;
				}
			}

			FVector CompositeKeepOrigin = FVector::ZeroVector;
			FVector CompositeKeepDirection = FVector::ZeroVector;
			FVector CompositeRawKeepDirection = FVector::ZeroVector;
			FString CompositeKeepDirectionMode = TEXT("none");
			bool bUseCompositeKeepDirection = false;
			if (CompositeSourceBounds.IsValid)
			{
				CompositeKeepOrigin = CompositeSourceBounds.GetCenter();
				const FVector TargetCenter = CandidateMesh.LocalBounds.IsValid
					? CandidateMesh.LocalBounds.GetCenter()
					: FVector::ZeroVector;
				CompositeRawKeepDirection = TargetCenter - CompositeKeepOrigin;
				CompositeRawKeepDirection.Z = 0.0f;
				CompositeKeepDirection = CompositeRawKeepDirection;

				if (!CompositeRawKeepDirection.IsNearlyZero() && CandidateMesh.LocalBounds.IsValid)
				{
					const FVector TargetSize = CandidateMesh.LocalBounds.GetSize();
					const FVector SourceSize = CompositeSourceBounds.GetSize();
					const bool bSourceCrossesTargetY = TargetSize.Y > KINDA_SMALL_NUMBER
						&& SourceSize.Y >= TargetSize.Y * 0.75f
						&& SourceSize.X < TargetSize.X * 0.9f;
					const bool bSourceCrossesTargetX = TargetSize.X > KINDA_SMALL_NUMBER
						&& SourceSize.X >= TargetSize.X * 0.75f
						&& SourceSize.Y < TargetSize.Y * 0.9f;

					FVector SnapAxis = FVector::ZeroVector;
					if (bSourceCrossesTargetY && !bSourceCrossesTargetX)
					{
						SnapAxis = FVector::XAxisVector;
						CompositeKeepDirectionMode = TEXT("snap-target-x-cross-y");
					}
					else if (bSourceCrossesTargetX && !bSourceCrossesTargetY)
					{
						SnapAxis = FVector::YAxisVector;
						CompositeKeepDirectionMode = TEXT("snap-target-y-cross-x");
					}
					else if (TargetSize.X >= TargetSize.Y)
					{
						SnapAxis = FVector::XAxisVector;
						CompositeKeepDirectionMode = TEXT("snap-target-x-major");
					}
					else
					{
						SnapAxis = FVector::YAxisVector;
						CompositeKeepDirectionMode = TEXT("snap-target-y-major");
					}

					float AxisProjection = FVector::DotProduct(CompositeRawKeepDirection, SnapAxis);
					if (FMath::IsNearlyZero(AxisProjection, 1.0f))
					{
						AxisProjection = FVector::DotProduct(-CompositeKeepOrigin, SnapAxis);
					}
					if (FMath::IsNearlyZero(AxisProjection, 1.0f))
					{
						AxisProjection = 1.0f;
					}
					CompositeKeepDirection = SnapAxis * (AxisProjection >= 0.0f ? 1.0f : -1.0f);
				}
				else
				{
					CompositeKeepDirectionMode = TEXT("raw-center");
				}
				bUseCompositeKeepDirection = !CompositeKeepDirection.IsNearlyZero();
			}

			bool bUseCompositeProjectionBoundary = false;
			double CompositeProjectionBoundary = 0.0;
			FString CompositeProjectionBoundaryMode = TEXT("none");
			if (bUseCompositeKeepDirection && CompositeSourceBounds.IsValid && CandidateMesh.LocalBounds.IsValid)
			{
				const FVector SafeKeepDirection = CompositeKeepDirection.GetSafeNormal();
				double SourceMinProjection = 0.0;
				double SourceMaxProjection = 0.0;
				if (ProjectBoxOnDirection(CompositeSourceBounds, SafeKeepDirection, SourceMinProjection, SourceMaxProjection))
				{
					const float ProjectionPadding = FMath::Clamp(FMath::Max(Thickness, 1.0f) * 0.1f, 0.5f, 5.0f);
					const double BoundaryBaseProjection = SourceMaxProjection;
					CompositeProjectionBoundary = BoundaryBaseProjection
						+ static_cast<double>(bKeepCutAwayDisconnectedPieces ? ProjectionPadding : -ProjectionPadding);
					CompositeProjectionBoundaryMode = TEXT("source-keep-face");
					bUseCompositeProjectionBoundary = true;
				}
			}

			UE_LOG(
				LogEHBHipRoof,
				Display,
				TEXT("[EHB HipRoof] unified filter direction roof=%s pass=%s hasSource=%d sourceCenter=%s rawKeepDirection=%s keepDirection=%s mode=%s targetBounds=%s sourceBounds=%s projectionBoundary=%d boundary=%.2f"),
				*GetName(),
				PassName,
				CompositeSourceBounds.IsValid ? 1 : 0,
				*CompositeKeepOrigin.ToCompactString(),
				*CompositeRawKeepDirection.GetSafeNormal().ToCompactString(),
				*CompositeKeepDirection.GetSafeNormal().ToCompactString(),
				*CompositeKeepDirectionMode,
				*CandidateMesh.LocalBounds.ToString(),
				*CompositeSourceBounds.ToString(),
				bUseCompositeProjectionBoundary ? 1 : 0,
				CompositeProjectionBoundary);

			if (!FilterCompositeHipRoofPieces(
					CandidateMesh,
					bRemoveDisconnectedCutPieces,
					bKeepCutAwayDisconnectedPieces,
					GetName(),
					CompositeJoinTolerance,
					bUseCompositeKeepDirection,
					CompositeKeepOrigin,
					CompositeKeepDirection,
					bUseCompositeProjectionBoundary,
					CompositeProjectionBoundary,
					CompositeProjectionBoundaryMode,
					&OutStats))
			{
				UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] unified connected-piece filter failed roof=%s pass=%s"), *GetName(), PassName);
				return false;
			}

			return true;
		};

		struct FHipCutPassResult
		{
			FEHBRoofUnifiedMeshData Mesh;
			FHipCompositeFilterStats FilterStats;
			bool bSucceeded = false;
			bool bChanged = false;
		};

		const auto RunCutPass = [this, &BeforeCutMesh, &ApplyCompositeFilter](const TCHAR* PassName, bool bUseExactCutters, FHipCutPassResult& OutPass)
		{
			OutPass = FHipCutPassResult();
			OutPass.Mesh = BeforeCutMesh;
			const bool bOriginalUseExactSourceMeshCutters = bUseExactSourceMeshCutters;
			bUseExactSourceMeshCutters = bUseExactCutters;
			const bool bSourceCutSucceeded = ApplySourceMeshCuts(OutPass.Mesh, false) && OutPass.Mesh.IsValid();
			bUseExactSourceMeshCutters = bOriginalUseExactSourceMeshCutters;
			if (!bSourceCutSucceeded)
			{
				UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] source-cut pass failed roof=%s pass=%s exact=%d"), *GetName(), PassName, bUseExactCutters ? 1 : 0);
				return false;
			}

			OutPass.Mesh.RebuildBounds();
			OutPass.bChanged = DidUnifiedMeshChange(BeforeCutMesh, OutPass.Mesh);
			if (OutPass.bChanged)
			{
				if (!ApplyCompositeFilter(OutPass.Mesh, PassName, OutPass.FilterStats))
				{
					return false;
				}
			}

			OutPass.bSucceeded = true;
			UE_LOG(
				LogEHBHipRoof,
				Display,
				TEXT("[EHB HipRoof] source-cut pass result roof=%s pass=%s exact=%d changed=%d triangles=%d body=%d side=%d ridge=%d eaves=%d hipRidges=%d components=%d removedTris=%d bounds=%s"),
				*GetName(),
				PassName,
				bUseExactCutters ? 1 : 0,
				OutPass.bChanged ? 1 : 0,
				OutPass.Mesh.Triangles.Num() / 3,
				CountGroupTriangles(OutPass.Mesh, EHBHipRoofBodyGroupID),
				CountGroupTriangles(OutPass.Mesh, EHBHipRoofSideWallGroupID),
				CountGroupTriangles(OutPass.Mesh, EHBHipRoofRidgeGroupID),
				CountGroupTriangles(OutPass.Mesh, EHBHipRoofEaveGroupID),
				CountGroupTriangles(OutPass.Mesh, EHBHipRoofHipRidgeGroupID),
				OutPass.FilterStats.ComponentCount,
				OutPass.FilterStats.RemovedTriangleCount,
				*OutPass.Mesh.LocalBounds.ToString());
			return true;
		};

		FHipCutPassResult ExactPass;
		FHipCutPassResult EnvelopePass;
		if (bUseExactSourceMeshCutters)
		{
			const bool bExactPassOk = RunCutPass(TEXT("exact"), true, ExactPass);
			if (ExactPass.bSucceeded && ExactPass.bChanged)
			{
				const bool bExactProducedExpectedSeparation = !bRemoveDisconnectedCutPieces
					|| ExactPass.FilterStats.RemovedTriangleCount > 0
					|| !bUseControlledEnvelopeCutters;
				if (bExactProducedExpectedSeparation)
				{
					RenderMesh = MoveTemp(ExactPass.Mesh);
					bRoofCutChanged = true;
					bHasSelectedSourceCutPass = true;
					SelectedProjectionClipRule = ExactPass.FilterStats.ProjectionClipRule;
					bUseExactProjectionPrunedRoofCut = false;
					bUseExactAccessorySourceCut = bHasRoofSourceCutOperation;
				}
				else if (bHasRoofSourceCutOperation && ExactPass.FilterStats.ProjectionClipRule.bValid)
				{
					FEHBRoofUnifiedMeshData ProjectionPrunedExactMesh = ExactPass.Mesh;
					const float MaxAccessoryDimension = FMath::Max(
						FMath::Max(RidgeWidth, RidgeHeight),
						FMath::Max(FMath::Max(EaveWidth, EaveHeight), FMath::Max(GableRakeWidth, GableRakeHeight)));
					const float ProjectionPruneTolerance = FMath::Clamp(
						FMath::Max(MaxAccessoryDimension, Thickness) * 0.75f,
						4.0f,
						35.0f);
					int32 ProjectionPrunedRemovedTriangles = 0;
					if (PruneMeshByProjectionCentroid(
							ProjectionPrunedExactMesh,
							ExactPass.FilterStats.ProjectionClipRule,
							GetName(),
							TEXT("exact-roof-source"),
							ProjectionPruneTolerance,
							&ProjectionPrunedRemovedTriangles)
						&& ProjectionPrunedExactMesh.IsValid()
						&& ProjectionPrunedRemovedTriangles > 0)
					{
						RenderMesh = MoveTemp(ProjectionPrunedExactMesh);
						bRoofCutChanged = true;
						bHasSelectedSourceCutPass = true;
						SelectedProjectionClipRule = ExactPass.FilterStats.ProjectionClipRule;
						bUseExactProjectionPrunedRoofCut = true;
						bUseExactAccessorySourceCut = true;
						UE_LOG(
							LogEHBHipRoof,
							Display,
							TEXT("[EHB HipRoof] selected exact projection-pruned roof cut roof=%s removed=%d components=%d"),
							*GetName(),
							ProjectionPrunedRemovedTriangles,
							ExactPass.FilterStats.ComponentCount);
					}
					else
					{
						UE_LOG(
							LogEHBHipRoof,
							Display,
							TEXT("[EHB HipRoof] exact projection prune did not produce a usable roof cut roof=%s removed=%d components=%d"),
							*GetName(),
							ProjectionPrunedRemovedTriangles,
							ExactPass.FilterStats.ComponentCount);
					}
				}
				else
				{
					UE_LOG(
						LogEHBHipRoof,
						Display,
						TEXT("[EHB HipRoof] exact pass changed geometry but did not produce a removable disconnected side; deferring to controlled envelope roof=%s components=%d"),
						*GetName(),
						ExactPass.FilterStats.ComponentCount);
				}
			}
			else if (!bExactPassOk && !bUseControlledEnvelopeCutters)
			{
				return false;
			}
		}

		if (!bHasSelectedSourceCutPass && bUseControlledEnvelopeCutters)
		{
			if (!RunCutPass(TEXT("controlled-envelope"), false, EnvelopePass))
			{
				return false;
			}

			if (EnvelopePass.bChanged)
			{
				RenderMesh = MoveTemp(EnvelopePass.Mesh);
				bRoofCutChanged = true;
				bHasSelectedSourceCutPass = true;
				SelectedProjectionClipRule = EnvelopePass.FilterStats.ProjectionClipRule;
				bUseExactProjectionPrunedRoofCut = false;
				bUseExactAccessorySourceCut = false;
			}
		}

		if (!bHasSelectedSourceCutPass && ExactPass.bSucceeded && ExactPass.bChanged)
		{
			RenderMesh = MoveTemp(ExactPass.Mesh);
			bRoofCutChanged = true;
			bHasSelectedSourceCutPass = true;
			SelectedProjectionClipRule = ExactPass.FilterStats.ProjectionClipRule;
			bUseExactProjectionPrunedRoofCut = false;
			bUseExactAccessorySourceCut = bHasRoofSourceCutOperation;
		}

		RenderMesh.RebuildBounds();
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] unified source-cut selected roof=%s changed=%d selected=%d clipRule=%d clipDirection=%s clipBoundary=%.2f triangles=%d body=%d side=%d ridge=%d eaves=%d hipRidges=%d bounds=%s"),
			*GetName(),
			bRoofCutChanged ? 1 : 0,
			bHasSelectedSourceCutPass ? 1 : 0,
			SelectedProjectionClipRule.bValid ? 1 : 0,
			*SelectedProjectionClipRule.KeepDirection.ToCompactString(),
			SelectedProjectionClipRule.KeepProjectionBoundary,
			RenderMesh.Triangles.Num() / 3,
			CountGroupTriangles(RenderMesh, EHBHipRoofBodyGroupID),
			CountGroupTriangles(RenderMesh, EHBHipRoofSideWallGroupID),
			CountGroupTriangles(RenderMesh, EHBHipRoofRidgeGroupID),
			CountGroupTriangles(RenderMesh, EHBHipRoofEaveGroupID),
			CountGroupTriangles(RenderMesh, EHBHipRoofHipRidgeGroupID),
			*RenderMesh.LocalBounds.ToString());
	}

	if (!bHalfHipRoof && bRoofCutChanged && bHasSelectedSourceCutPass && SelectedProjectionClipRule.bValid)
	{
		FEHBRoofUnifiedMeshData SemanticTrimMesh;
		FHipRoofGeometry CandidateSemanticTrimGeometry;
		if (TryBuildSemanticTrimmedHipRoofMesh(
				*this,
				UncutRenderMesh,
				RenderMesh,
				SelectedProjectionClipRule,
				SemanticTrimMesh,
				CandidateSemanticTrimGeometry))
		{
			RenderMesh = MoveTemp(SemanticTrimMesh);
			SemanticTrimGeometry = CandidateSemanticTrimGeometry;
			bUseSemanticTrimGeometry = true;
		}
	}

	if (bRoofCutChanged)
	{
		const float CleanupFeatureScale = FMath::Max(
			FMath::Max(Thickness, FMath::Max(RidgeWidth, RidgeHeight)),
			FMath::Max(FMath::Max(EaveWidth, EaveHeight), FMath::Max(GableRakeWidth, GableRakeHeight)));
		if (!RemoveSmallHipCutArtifacts(RenderMesh, CleanupFeatureScale, GetName()))
		{
			UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] tiny cut artifact cleanup failed roof=%s"), *GetName());
			return false;
		}
	}

	if (bDebugDrawResultBounds)
	{
		DrawRoofCutDebugMeshBounds(RenderMesh, FColor::Green, TEXT("final unified hip roof"));
	}

	FHipVisualAccessoryMeshes AccessoryMeshes;
	const bool bBuiltAccessories = bUseSemanticTrimGeometry
		? BuildHipVisualAccessoryMeshesFromGeometry(*this, SemanticTrimGeometry, AccessoryMeshes)
		: (bHalfHipRoof
			? BuildHalfHipVisualAccessoryMeshes(*this, AccessoryMeshes)
			: BuildHipVisualAccessoryMeshes(*this, AccessoryMeshes));
	if (!bBuiltAccessories)
	{
		UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] visual accessory build failed roof=%s"), *GetName());
		return false;
	}

	if (!bUseSemanticTrimGeometry && !CutOperations.IsEmpty() && bHasSelectedSourceCutPass && SelectedProjectionClipRule.bValid)
	{
		const FHipProjectionClipRule AccessoryProjectionClipRule = bUseExactAccessorySourceCut
			? MakeProjectionRuleFromKeptMeshBounds(SelectedProjectionClipRule, RenderMesh.LocalBounds)
			: SelectedProjectionClipRule;
		UE_LOG(
			LogEHBHipRoof,
			Display,
			TEXT("[EHB HipRoof] accessory cut rule roof=%s exactAccessory=%d exactPruned=%d originalBoundary=%.2f accessoryBoundary=%.2f direction=%s renderBounds=%s"),
			*GetName(),
			bUseExactAccessorySourceCut ? 1 : 0,
			bUseExactProjectionPrunedRoofCut ? 1 : 0,
			SelectedProjectionClipRule.KeepProjectionBoundary,
			AccessoryProjectionClipRule.KeepProjectionBoundary,
			*AccessoryProjectionClipRule.KeepDirection.ToCompactString(),
			*RenderMesh.LocalBounds.ToString());

		const auto ClipAccessoryMesh = [this, &AccessoryProjectionClipRule](FEHBRoofUnifiedMeshData& InOutMesh, const TCHAR* MeshName)
		{
			if (!InOutMesh.IsValid())
			{
				return;
			}

			if (!ClipMeshByProjectionRule(InOutMesh, AccessoryProjectionClipRule, GetName(), MeshName))
			{
				UE_LOG(
					LogEHBHipRoof,
					Warning,
					TEXT("[EHB HipRoof] accessory projection clip failed roof=%s mesh=%s"),
					*GetName(),
					MeshName);
			}
		};

		ClipAccessoryMesh(AccessoryMeshes.RidgeMesh, TEXT("ridge"));
		ClipAccessoryMesh(AccessoryMeshes.EaveMesh, TEXT("eaves"));
		ClipAccessoryMesh(AccessoryMeshes.HipRidgeMesh, TEXT("hip-ridges"));
	}

	if (!RoofBodyMeshComponent || !RenderMesh.IsValid())
	{
		return false;
	}

	const FEHBResolvedDefaultRoofMaterials DefaultRoofMaterials = ResolveConfiguredDefaultRoofMaterials();

	UMaterialInterface* BodyMat = ResolveHipComponentMaterial(
		RoofBodyMeshComponent,
		RoofBodyMaterial ? RoofBodyMaterial.Get() : nullptr,
		DefaultRoofMaterials.Slope,
		UMaterial::GetDefaultMaterial(MD_Surface));
	UMaterialInterface* SideMat = ResolveHipComponentMaterial(
		GableEndWallMeshComponent,
		GableEndWallMaterial ? GableEndWallMaterial.Get() : nullptr,
		DefaultRoofMaterials.SideWall,
		BodyMat);
	UMaterialInterface* RidgeMat = ResolveHipComponentMaterial(
		RidgeMeshComponent,
		RidgeMaterial ? RidgeMaterial.Get() : (AccessoryMaterial ? AccessoryMaterial.Get() : nullptr),
		DefaultRoofMaterials.Ridge,
		BodyMat);
	UMaterialInterface* EaveMat = ResolveHipComponentMaterial(
		EaveMeshComponent,
		EaveMaterial ? EaveMaterial.Get() : (AccessoryMaterial ? AccessoryMaterial.Get() : nullptr),
		DefaultRoofMaterials.Eave,
		BodyMat);
	UMaterialInterface* HipRidgeMat = ResolveHipComponentMaterial(
		GableRakeMeshComponent,
		GableRakeMaterial ? GableRakeMaterial.Get() : (AccessoryMaterial ? AccessoryMaterial.Get() : nullptr),
		DefaultRoofMaterials.DiagonalRidge,
		BodyMat);

	int32 SubmittedComponentCount = 0;
	const auto ClearComponentSections = [](UEHBGeneratedMeshComponent* Component)
	{
		if (Component)
		{
			FEHBScopedGeneratedMeshUpdate ScopedUpdate(Component);
			Component->ClearAllMeshSections();
		}
	};

	const auto SubmitGroupToComponent = [&RenderMesh, &SubmittedComponentCount](
		UEHBGeneratedMeshComponent* Component,
		int32 GroupID,
		FName SectionName,
		UMaterialInterface* Material)
	{
		if (!Component)
		{
			return false;
		}

		FEHBScopedGeneratedMeshUpdate ScopedUpdate(Component);
		if (SubmitUnifiedGroupSection(Component, RenderMesh, GroupID, 0, SectionName, Material))
		{
			++SubmittedComponentCount;
			return true;
		}
		return false;
	};

	const auto SubmitMeshGroupToComponent = [&SubmittedComponentCount](
		UEHBGeneratedMeshComponent* Component,
		const FEHBRoofUnifiedMeshData& SourceMesh,
		int32 GroupID,
		FName SectionName,
		UMaterialInterface* Material)
	{
		if (!Component || !SourceMesh.IsValid())
		{
			return false;
		}

		FEHBScopedGeneratedMeshUpdate ScopedUpdate(Component);
		if (SubmitUnifiedGroupSection(Component, SourceMesh, GroupID, 0, SectionName, Material))
		{
			++SubmittedComponentCount;
			return true;
		}
		return false;
	};

	ClearComponentSections(RoofBodyMeshComponent);
	ClearComponentSections(GableEndWallMeshComponent);
	ClearComponentSections(RidgeMeshComponent);
	ClearComponentSections(EaveMeshComponent);
	ClearComponentSections(GableRakeMeshComponent);

	SubmitGroupToComponent(RoofBodyMeshComponent, EHBHipRoofBodyGroupID, TEXT("RoofBody"), BodyMat);
	SubmitGroupToComponent(GableEndWallMeshComponent, EHBHipRoofSideWallGroupID, TEXT("HipSideFaces"), SideMat);
	if (bGenerateRidge)
	{
		SubmitMeshGroupToComponent(RidgeMeshComponent, AccessoryMeshes.RidgeMesh, EHBHipRoofRidgeGroupID, TEXT("HipRidge"), RidgeMat);
	}
	if (bGenerateEaves)
	{
		SubmitMeshGroupToComponent(EaveMeshComponent, AccessoryMeshes.EaveMesh, EHBHipRoofEaveGroupID, TEXT("HipEaves"), EaveMat);
	}
	if (bGenerateGableRakes)
	{
		SubmitMeshGroupToComponent(GableRakeMeshComponent, AccessoryMeshes.HipRidgeMesh, EHBHipRoofHipRidgeGroupID, TEXT("HipRidges"), HipRidgeMat);
	}

	if (SubmittedComponentCount == 0)
	{
		UE_LOG(LogEHBHipRoof, Warning, TEXT("[EHB HipRoof] no render components submitted roof=%s"), *GetName());
		return false;
	}

	UE_LOG(
		LogEHBHipRoof,
		Verbose,
		TEXT("[EHB HipRoof] submitted roof=%s cutChanged=%d triangles=%d body=%d side=%d ridge=%d eaves=%d hipRidges=%d bounds=%s"),
		*GetName(),
		bRoofCutChanged ? 1 : 0,
		RenderMesh.Triangles.Num() / 3,
		CountGroupTriangles(RenderMesh, EHBHipRoofBodyGroupID),
		CountGroupTriangles(RenderMesh, EHBHipRoofSideWallGroupID),
		CountGroupTriangles(AccessoryMeshes.RidgeMesh, EHBHipRoofRidgeGroupID),
		CountGroupTriangles(AccessoryMeshes.EaveMesh, EHBHipRoofEaveGroupID),
		CountGroupTriangles(AccessoryMeshes.HipRidgeMesh, EHBHipRoofHipRidgeGroupID),
		*RenderMesh.LocalBounds.ToString());

	MarkPackageDirty();
	return true;
}
