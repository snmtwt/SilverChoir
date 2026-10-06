#include "Actors/EHBGableRoof.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "Cutting/EHBGeneratedMeshCollector.h"
#include "Cutting/EHBManifoldBoolean.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"

DEFINE_LOG_CATEGORY_STATIC(LogEHBRoof, Log, All);

namespace
{
	constexpr float EHBRoofMinDimension = 1.0f;
	constexpr float EHBRoofUVScale = 100.0f;
	constexpr int32 EHBGableRoofBodyGroupID = 1;
	constexpr int32 EHBGableRoofSideWallGroupID = 2;
	constexpr int32 EHBGableRoofRidgeGroupID = 10;
	constexpr int32 EHBGableRoofEaveGroupID = 11;
	constexpr int32 EHBGableRoofRakeGroupID = 12;
	constexpr double EHBGableRoofBooleanMergeTolerance = 0.01;

	struct FGableRoofGeometry
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
		FVector BottomRidge0 = FVector::ZeroVector;
		FVector BottomRidge1 = FVector::ZeroVector;
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
		OutMesh.UV0.Add(FVector2D(A.X, A.Y) / EHBRoofUVScale);
		OutMesh.UV0.Add(FVector2D(UseB.X, UseB.Y) / EHBRoofUVScale);
		OutMesh.UV0.Add(FVector2D(UseC.X, UseC.Y) / EHBRoofUVScale);
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

	void AppendFaceWithNormal(
		const TArray<FVector>& Polygon,
		const FVector& DesiredNormal,
		FEHBRoofUnifiedMeshData& OutMesh,
		int32 TriangleGroupID = 0)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}

		const FVector SafeNormal = DesiredNormal.GetSafeNormal();
		if (SafeNormal.IsNearlyZero())
		{
			return;
		}

		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			AppendTriangleWithNormal(Polygon[0], Polygon[Index], Polygon[Index + 1], SafeNormal, OutMesh, TriangleGroupID);
		}
	}

	bool BuildGableGeometry(const AEHBGableRoof& Roof, FGableRoofGeometry& OutGeometry)
	{
		const float SafeLength = FMath::Max(Roof.Length, EHBRoofMinDimension);
		const float SafeWidth = FMath::Max(Roof.Width, EHBRoofMinDimension);
		const float SafeThickness = FMath::Max(Roof.Thickness, 0.1f);
		const float SafePitchDegrees = FMath::Clamp(Roof.PitchDegrees, 1.0f, 89.0f);

		const float AlongMin = -SafeLength * 0.5f - Roof.EaveOffset;
		const float AlongMax = SafeLength * 0.5f + Roof.EaveOffset;
		const float CrossMin = -SafeWidth * 0.5f - Roof.EaveOffset;
		const float CrossMax = SafeWidth * 0.5f + Roof.EaveOffset;
		const float CrossSpan = CrossMax - CrossMin;
		const float RidgeCross = FMath::Clamp(
			(CrossMin + CrossMax) * 0.5f + CrossSpan * Roof.RidgeOffsetRatio,
			CrossMin + 1.0f,
			CrossMax - 1.0f);
		const float RidgeRun = FMath::Max(FMath::Min(RidgeCross - CrossMin, CrossMax - RidgeCross), 1.0f);
		const float RidgeHeight = RidgeRun * FMath::Tan(FMath::DegreesToRadians(SafePitchDegrees));

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
		OutGeometry.Ridge0 = MakePoint(AlongMin, RidgeCross, RidgeHeight);
		OutGeometry.Ridge1 = MakePoint(AlongMax, RidgeCross, RidgeHeight);

		const FVector ThicknessOffset(0.0f, 0.0f, SafeThickness);
		OutGeometry.BottomLowA0 = OutGeometry.LowA0 - ThicknessOffset;
		OutGeometry.BottomLowA1 = OutGeometry.LowA1 - ThicknessOffset;
		OutGeometry.BottomLowB0 = OutGeometry.LowB0 - ThicknessOffset;
		OutGeometry.BottomLowB1 = OutGeometry.LowB1 - ThicknessOffset;
		OutGeometry.BottomRidge0 = OutGeometry.Ridge0 - ThicknessOffset;
		OutGeometry.BottomRidge1 = OutGeometry.Ridge1 - ThicknessOffset;
		return true;
	}

	void AppendLinePrism(
		const FVector& Start,
		const FVector& End,
		float Width,
		float Height,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		const FVector Tangent = (End - Start).GetSafeNormal();
		if (Tangent.IsNearlyZero())
		{
			return;
		}

		FVector Side = FVector::CrossProduct(FVector::UpVector, Tangent).GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			Side = FVector::CrossProduct(FVector::RightVector, Tangent).GetSafeNormal();
		}
		if (Side.IsNearlyZero())
		{
			Side = FVector::YAxisVector;
		}

		FVector Up = FVector::CrossProduct(Tangent, Side).GetSafeNormal();
		if (FVector::DotProduct(Up, FVector::UpVector) < 0.0f)
		{
			Up *= -1.0f;
		}
		if (Up.IsNearlyZero())
		{
			Up = FVector::UpVector;
		}

		const FVector HalfSide = Side * FMath::Max(Width, 0.1f) * 0.5f;
		const FVector HalfUp = Up * FMath::Max(Height, 0.1f) * 0.5f;
		const FVector A0 = Start - HalfSide - HalfUp;
		const FVector A1 = Start + HalfSide - HalfUp;
		const FVector A2 = Start + HalfSide + HalfUp;
		const FVector A3 = Start - HalfSide + HalfUp;
		const FVector B0 = End - HalfSide - HalfUp;
		const FVector B1 = End + HalfSide - HalfUp;
		const FVector B2 = End + HalfSide + HalfUp;
		const FVector B3 = End - HalfSide + HalfUp;
		const FVector BodyCenter = (A0 + A1 + A2 + A3 + B0 + B1 + B2 + B3) / 8.0f;

		AppendConvexFace({ A0, B0, B1, A1 }, BodyCenter, OutMesh);
		AppendConvexFace({ A1, B1, B2, A2 }, BodyCenter, OutMesh);
		AppendConvexFace({ A2, B2, B3, A3 }, BodyCenter, OutMesh);
		AppendConvexFace({ A3, B3, B0, A0 }, BodyCenter, OutMesh);
		AppendConvexFace({ A0, A1, A2, A3 }, BodyCenter, OutMesh);
		AppendConvexFace({ B3, B2, B1, B0 }, BodyCenter, OutMesh);
	}

	void AppendTriangularPrism(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& ExtrudeNormal,
		float Depth,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		const FVector Normal = ExtrudeNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return;
		}

		const FVector HalfDepth = Normal * FMath::Max(Depth, 0.1f) * 0.5f;
		const FVector A0 = A + HalfDepth;
		const FVector B0 = B + HalfDepth;
		const FVector C0 = C + HalfDepth;
		const FVector A1 = A - HalfDepth;
		const FVector B1 = B - HalfDepth;
		const FVector C1 = C - HalfDepth;
		const FVector BodyCenter = (A0 + B0 + C0 + A1 + B1 + C1) / 6.0f;

		AppendConvexFace({ A0, B0, C0 }, BodyCenter, OutMesh);
		AppendConvexFace({ C1, B1, A1 }, BodyCenter, OutMesh);
		AppendConvexFace({ A0, A1, B1, B0 }, BodyCenter, OutMesh);
		AppendConvexFace({ B0, B1, C1, C0 }, BodyCenter, OutMesh);
		AppendConvexFace({ C0, C1, A1, A0 }, BodyCenter, OutMesh);
	}

	bool IntersectLines2D(
		const FVector2D& OriginA,
		const FVector2D& DirectionA,
		const FVector2D& OriginB,
		const FVector2D& DirectionB,
		FVector2D& OutIntersection)
	{
		const float Cross = DirectionA.X * DirectionB.Y - DirectionA.Y * DirectionB.X;
		if (FMath::IsNearlyZero(Cross, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		const FVector2D Delta = OriginB - OriginA;
		const float T = (Delta.X * DirectionB.Y - Delta.Y * DirectionB.X) / Cross;
		OutIntersection = OriginA + DirectionA * T;
		return true;
	}

	float Cross2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const FVector2D AB = B - A;
		const FVector2D AC = C - A;
		return AB.X * AC.Y - AB.Y * AC.X;
	}

	float SignedArea2D(const TArray<FVector2D>& Polygon)
	{
		float Area = 0.0f;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5f;
	}

	bool IsPointInTriangle2D(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		bool bTriangleCCW)
	{
		constexpr float Tolerance = 0.001f;
		const float AB = Cross2D(A, B, Point);
		const float BC = Cross2D(B, C, Point);
		const float CA = Cross2D(C, A, Point);
		return bTriangleCCW
			? AB >= -Tolerance && BC >= -Tolerance && CA >= -Tolerance
			: AB <= Tolerance && BC <= Tolerance && CA <= Tolerance;
	}

	bool TriangulateSimplePolygon2D(const TArray<FVector2D>& Polygon, TArray<FIntVector>& OutTriangles)
	{
		OutTriangles.Reset();
		if (Polygon.Num() < 3)
		{
			return false;
		}

		const bool bPolygonCCW = SignedArea2D(Polygon) > 0.0f;
		TArray<int32> Remaining;
		Remaining.Reserve(Polygon.Num());
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			Remaining.Add(Index);
		}

		int32 Guard = Polygon.Num() * Polygon.Num();
		while (Remaining.Num() > 3 && Guard-- > 0)
		{
			bool bFoundEar = false;
			for (int32 EarIndex = 0; EarIndex < Remaining.Num(); ++EarIndex)
			{
				const int32 PrevIndex = Remaining[(EarIndex - 1 + Remaining.Num()) % Remaining.Num()];
				const int32 CurrIndex = Remaining[EarIndex];
				const int32 NextIndex = Remaining[(EarIndex + 1) % Remaining.Num()];
				const FVector2D& A = Polygon[PrevIndex];
				const FVector2D& B = Polygon[CurrIndex];
				const FVector2D& C = Polygon[NextIndex];
				const float CornerCross = Cross2D(A, B, C);
				const bool bIsConvex = bPolygonCCW ? CornerCross > KINDA_SMALL_NUMBER : CornerCross < -KINDA_SMALL_NUMBER;
				if (!bIsConvex)
				{
					continue;
				}

				bool bContainsPoint = false;
				for (const int32 TestIndex : Remaining)
				{
					if (TestIndex == PrevIndex || TestIndex == CurrIndex || TestIndex == NextIndex)
					{
						continue;
					}
					if (IsPointInTriangle2D(Polygon[TestIndex], A, B, C, bPolygonCCW))
					{
						bContainsPoint = true;
						break;
					}
				}
				if (bContainsPoint)
				{
					continue;
				}

				OutTriangles.Add(bPolygonCCW
					? FIntVector(PrevIndex, CurrIndex, NextIndex)
					: FIntVector(PrevIndex, NextIndex, CurrIndex));
				Remaining.RemoveAt(EarIndex);
				bFoundEar = true;
				break;
			}

			if (!bFoundEar)
			{
				return false;
			}
		}

		if (Remaining.Num() == 3)
		{
			OutTriangles.Add(bPolygonCCW
				? FIntVector(Remaining[0], Remaining[1], Remaining[2])
				: FIntVector(Remaining[0], Remaining[2], Remaining[1]));
		}
		return !OutTriangles.IsEmpty();
	}

	void AppendExtrudedVFrame(
		const FVector& LeftEave,
		const FVector& Ridge,
		const FVector& RightEave,
		const FVector& ExtrudeNormal,
		float Width,
		float Depth,
		FEHBRoofUnifiedMeshData& OutMesh)
	{
		const FVector Normal = ExtrudeNormal.GetSafeNormal();
		const FVector Across = (RightEave - LeftEave).GetSafeNormal();
		const FVector Up = FVector::UpVector;
		if (Normal.IsNearlyZero() || Across.IsNearlyZero())
		{
			return;
		}

		const FVector2D Left2D(0.0f, 0.0f);
		const FVector2D Ridge2D(
			FVector::DotProduct(Ridge - LeftEave, Across),
			FVector::DotProduct(Ridge - LeftEave, Up));
		const FVector2D Right2D(
			FVector::DotProduct(RightEave - LeftEave, Across),
			FVector::DotProduct(RightEave - LeftEave, Up));

		const FVector2D SegmentA = (Ridge2D - Left2D).GetSafeNormal();
		const FVector2D SegmentB = (Right2D - Ridge2D).GetSafeNormal();
		if (SegmentA.IsNearlyZero() || SegmentB.IsNearlyZero())
		{
			return;
		}

		const float HalfWidth = FMath::Max(Width, 0.1f) * 0.5f;
		const float HalfDepth = FMath::Max(Depth, 0.1f) * 0.5f;
		const FVector2D SegmentANormal(-SegmentA.Y, SegmentA.X);
		const FVector2D SegmentBNormal(-SegmentB.Y, SegmentB.X);

		FVector2D OuterJoint;
		if (!IntersectLines2D(
				Left2D + SegmentANormal * HalfWidth,
				SegmentA,
				Ridge2D + SegmentBNormal * HalfWidth,
				SegmentB,
				OuterJoint))
		{
			OuterJoint = Ridge2D + (SegmentANormal + SegmentBNormal).GetSafeNormal() * HalfWidth;
		}

		FVector2D InnerJoint;
		if (!IntersectLines2D(
				Left2D - SegmentANormal * HalfWidth,
				SegmentA,
				Ridge2D - SegmentBNormal * HalfWidth,
				SegmentB,
				InnerJoint))
		{
			InnerJoint = Ridge2D - (SegmentANormal + SegmentBNormal).GetSafeNormal() * HalfWidth;
		}

		const TArray<FVector2D> Outline2D = {
			Left2D + SegmentANormal * HalfWidth,
			OuterJoint,
			Right2D + SegmentBNormal * HalfWidth,
			Right2D - SegmentBNormal * HalfWidth,
			InnerJoint,
			Left2D - SegmentANormal * HalfWidth,
		};

		auto To3D = [&LeftEave, &Across, &Up](const FVector2D& Point)
		{
			return LeftEave + Across * Point.X + Up * Point.Y;
		};

		TArray<FVector> Front;
		TArray<FVector> Back;
		Front.Reserve(Outline2D.Num());
		Back.Reserve(Outline2D.Num());
		for (const FVector2D& Point : Outline2D)
		{
			const FVector CenterPoint = To3D(Point);
			Front.Add(CenterPoint + Normal * HalfDepth);
			Back.Add(CenterPoint - Normal * HalfDepth);
		}

		TArray<FIntVector> CapTriangles;
		if (!TriangulateSimplePolygon2D(Outline2D, CapTriangles))
		{
			UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] failed to triangulate gable rake V frame cap"));
			return;
		}

		for (const FIntVector& Triangle : CapTriangles)
		{
			AppendTriangleWithNormal(Front[Triangle.X], Front[Triangle.Y], Front[Triangle.Z], Normal, OutMesh);
			AppendTriangleWithNormal(Back[Triangle.Z], Back[Triangle.Y], Back[Triangle.X], -Normal, OutMesh);
		}

		const bool bOutlineCCW = SignedArea2D(Outline2D) > 0.0f;
		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			const int32 NextIndex = (Index + 1) % Front.Num();
			const FVector2D Edge2D = Outline2D[NextIndex] - Outline2D[Index];
			FVector2D SideNormal2D = bOutlineCCW
				? FVector2D(Edge2D.Y, -Edge2D.X)
				: FVector2D(-Edge2D.Y, Edge2D.X);
			if (!SideNormal2D.Normalize())
			{
				continue;
			}

			const FVector SideNormal = (Across * SideNormal2D.X + Up * SideNormal2D.Y).GetSafeNormal();
			AppendFaceWithNormal({ Front[Index], Front[NextIndex], Back[NextIndex], Back[Index] }, SideNormal, OutMesh);
		}
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
				OutMesh.UV0.Add(FVector2D(Vertex.X, Vertex.Y) / EHBRoofUVScale);
			}
		}

		const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
		for (int32 Index = 0; Index < SourceMesh.Triangles.Num(); ++Index)
		{
			OutMesh.Triangles.Add(VertexBase + SourceMesh.Triangles[Index]);
		}
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			OutMesh.TriangleGroups.Add(TriangleGroupID);
		}
		OutMesh.RebuildBounds();
	}

	void AppendMeshPreservingGroups(
		const FEHBRoofUnifiedMeshData& SourceMesh,
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
				OutMesh.UV0.Add(FVector2D(Vertex.X, Vertex.Y) / EHBRoofUVScale);
			}
		}

		const int32 TriangleCount = SourceMesh.Triangles.Num() / 3;
		for (const int32 SourceIndex : SourceMesh.Triangles)
		{
			OutMesh.Triangles.Add(VertexBase + SourceIndex);
		}
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			OutMesh.TriangleGroups.Add(SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
				? SourceMesh.TriangleGroups[TriangleIndex]
				: EHBGableRoofBodyGroupID);
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
				: FVector2D(SourceMesh.Vertices[SourceIndex].X, SourceMesh.Vertices[SourceIndex].Y) / EHBRoofUVScale);
		}
		OutMesh.Triangles.Add(NewBaseIndex);
		OutMesh.Triangles.Add(NewBaseIndex + 1);
		OutMesh.Triangles.Add(NewBaseIndex + 2);
		OutMesh.TriangleGroups.Add(SourceMesh.TriangleGroups.IsValidIndex(TriangleIndex)
			? SourceMesh.TriangleGroups[TriangleIndex]
			: EHBGableRoofBodyGroupID);
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
				: EHBGableRoofBodyGroupID;
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
				: EHBGableRoofBodyGroupID;
			if (GroupID == TriangleGroupID)
			{
				++MatchCount;
			}
		}
		return MatchCount;
	}

	void ClassifyGableBodyGroupsByNormal(FEHBRoofUnifiedMeshData& Mesh)
	{
		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
		Mesh.TriangleGroups.SetNum(TriangleCount);
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex * 3 + 0];
			const int32 BIndex = Mesh.Triangles[TriangleIndex * 3 + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex * 3 + 2];
			if (!Mesh.Vertices.IsValidIndex(AIndex)
				|| !Mesh.Vertices.IsValidIndex(BIndex)
				|| !Mesh.Vertices.IsValidIndex(CIndex))
			{
				Mesh.TriangleGroups[TriangleIndex] = EHBGableRoofSideWallGroupID;
				continue;
			}

			FVector Normal = FVector::ZeroVector;
			if (Mesh.Normals.IsValidIndex(AIndex))
			{
				Normal = Mesh.Normals[AIndex].GetSafeNormal();
			}
			if (Normal.IsNearlyZero())
			{
				Normal = FVector::CrossProduct(
					Mesh.Vertices[BIndex] - Mesh.Vertices[AIndex],
					Mesh.Vertices[CIndex] - Mesh.Vertices[AIndex]).GetSafeNormal();
			}

			Mesh.TriangleGroups[TriangleIndex] = Normal.Z > 0.08f
				? EHBGableRoofBodyGroupID
				: EHBGableRoofSideWallGroupID;
		}
	}

	int32 ResolveGableSectionGroupID(FName SectionName)
	{
		if (SectionName == TEXT("RoofSideWalls") || SectionName == TEXT("GableEndWalls"))
		{
			return EHBGableRoofSideWallGroupID;
		}
		if (SectionName == TEXT("GableRidge"))
		{
			return EHBGableRoofRidgeGroupID;
		}
		if (SectionName == TEXT("GableEaves"))
		{
			return EHBGableRoofEaveGroupID;
		}
		if (SectionName == TEXT("GableRakes"))
		{
			return EHBGableRoofRakeGroupID;
		}
		return EHBGableRoofBodyGroupID;
	}

	bool IsGableAccessoryGroupID(int32 GroupID)
	{
		return GroupID == EHBGableRoofRidgeGroupID
			|| GroupID == EHBGableRoofEaveGroupID
			|| GroupID == EHBGableRoofRakeGroupID;
	}

	bool IsGablePrimaryRoofGroupID(int32 GroupID)
	{
		return GroupID == EHBGableRoofBodyGroupID
			|| GroupID == EHBGableRoofSideWallGroupID
			|| GroupID == 0;
	}

	bool DidUnifiedMeshChange(const FEHBRoofUnifiedMeshData& Before, const FEHBRoofUnifiedMeshData& After)
	{
		if (Before.Triangles.Num() != After.Triangles.Num()
			|| Before.Vertices.Num() != After.Vertices.Num())
		{
			return true;
		}

		if (Before.LocalBounds.IsValid != After.LocalBounds.IsValid)
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

	bool FilterCompositeGableRoofPieces(
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
		const FString& KeepProjectionBoundaryMode)
	{
		if (!bRemoveDisconnectedPieces || !Mesh.IsValid())
		{
			return true;
		}

		const int32 TriangleCount = Mesh.Triangles.Num() / 3;
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
				: EHBGableRoofBodyGroupID;
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
				if (IsGablePrimaryRoofGroupID(CurrentInfo.GroupID))
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

		if (Components.Num() <= 1)
		{
			UE_LOG(
				LogEHBRoof,
				Verbose,
				TEXT("[EHB RoofV3] connected-piece filter skipped roof=%s reason=single-component components=%d triangles=%d topologyTolerance=%.2f joinTolerance=%.2f"),
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
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] connected-piece filter roof=%s components=%d selected=%d keptComponents=%d removedTris=%d keepCutAway=%d directional=%d projectionBoundary=%d boundaryKept=%d boundaryPrimaryKept=%d boundary=%.2f boundaryMode=%s score=%.2f origin=%s direction=%s topologyTolerance=%.2f joinTolerance=%.2f"),
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
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] submitted section component=%s section=%s triangles=%d"),
			*Component->GetName(),
			*SectionName.ToString(),
			GroupMesh.Triangles.Num() / 3);
		return true;
	}

	UMaterialInterface* ResolveGableComponentMaterial(
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

AEHBGableRoof::AEHBGableRoof()
{
	ElementName = TEXT("Gable Roof");

	RidgeMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("GableRidge"));
	RidgeMeshComponent->SetupAttachment(SceneRoot);
	RidgeMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RidgeMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RoofV2_Accessory"));

	EaveMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("GableEaves"));
	EaveMeshComponent->SetupAttachment(SceneRoot);
	EaveMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EaveMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RoofV2_Accessory"));

	GableRakeMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("GableRakes"));
	GableRakeMeshComponent->SetupAttachment(SceneRoot);
	GableRakeMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GableRakeMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RoofV2_Accessory"));

	GableEndWallMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("GableEndWalls"));
	GableEndWallMeshComponent->SetupAttachment(SceneRoot);
	GableEndWallMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GableEndWallMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RoofV2_Accessory"));
}

void AEHBGableRoof::GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const
{
	Super::GetGeneratedMeshComponents(OutComponents);
	if (RidgeMeshComponent)
	{
		OutComponents.Add(RidgeMeshComponent);
	}
	if (EaveMeshComponent)
	{
		OutComponents.Add(EaveMeshComponent);
	}
	if (GableRakeMeshComponent)
	{
		OutComponents.Add(GableRakeMeshComponent);
	}
	if (GableEndWallMeshComponent)
	{
		OutComponents.Add(GableEndWallMeshComponent);
	}
}

bool AEHBGableRoof::ApplyMaterialToRoofComponent(
	UPrimitiveComponent* HitComponent,
	UMaterialInterface* Material,
	bool bApplyAllRoofParts)
{
	if (!Material)
	{
		return false;
	}

	bool bHandled = false;
	const auto ApplyToPart = [this, Material, &bHandled](
		UEHBGeneratedMeshComponent* Component,
		TObjectPtr<UMaterialInterface>& MaterialSlot)
	{
		if (!Component)
		{
			return;
		}

		Component->Modify();
		MaterialSlot = Material;
		Component->SetMaterialIfChanged(0, Material);
		Component->MarkPackageDirty();
		bHandled = true;
	};

	Modify();
	if (bApplyAllRoofParts)
	{
		ApplyToPart(RoofBodyMeshComponent, RoofBodyMaterial);
		ApplyToPart(GableEndWallMeshComponent, GableEndWallMaterial);
		ApplyToPart(RidgeMeshComponent, RidgeMaterial);
		ApplyToPart(EaveMeshComponent, EaveMaterial);
		ApplyToPart(GableRakeMeshComponent, GableRakeMaterial);
	}
	else if (!HitComponent || HitComponent == RoofBodyMeshComponent)
	{
		ApplyToPart(RoofBodyMeshComponent, RoofBodyMaterial);
	}
	else if (HitComponent == GableEndWallMeshComponent)
	{
		ApplyToPart(GableEndWallMeshComponent, GableEndWallMaterial);
	}
	else if (HitComponent == RidgeMeshComponent)
	{
		ApplyToPart(RidgeMeshComponent, RidgeMaterial);
	}
	else if (HitComponent == EaveMeshComponent)
	{
		ApplyToPart(EaveMeshComponent, EaveMaterial);
	}
	else if (HitComponent == GableRakeMeshComponent)
	{
		ApplyToPart(GableRakeMeshComponent, GableRakeMaterial);
	}
	else
	{
		return Super::ApplyMaterialToRoofComponent(HitComponent, Material, bApplyAllRoofParts);
	}

	if (bHandled)
	{
		MarkPackageDirty();
	}
	return bHandled;
}

bool AEHBGableRoof::BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const
{
	OutData.Reset();
	OutData.SourceElementGuid = ElementGuid;

	FEHBRoofUnifiedMeshData SourceMesh;
	if (!BuildUncutUnifiedRoofMesh(SourceMesh) || !SourceMesh.IsValid())
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
			: FVector2D(SourceMesh.Vertices[VertexIndex].X, SourceMesh.Vertices[VertexIndex].Y) / EHBRoofUVScale);
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
			: EHBGableRoofBodyGroupID;
		TriangleRef.SourceTriangleIndex = TriangleIndex;
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] exported uncut unified cut source roof=%s vertices=%d triangles=%d bounds=%s"),
		*GetName(),
		OutData.Vertices.Num(),
		OutData.Triangles.Num(),
		*OutData.LocalBounds.ToString());
	return OutData.Vertices.Num() > 0 && OutData.Triangles.Num() > 0;
}

bool AEHBGableRoof::GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const
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

			const int32 GroupID = ResolveGableSectionGroupID(Section.SectionName);
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
	return OutMesh.IsValid() || BuildUncutUnifiedRoofMesh(OutMesh);
}

bool AEHBGableRoof::GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const
{
	OutBounds.Reset();

	const float SafeLength = FMath::Max(Length, EHBRoofMinDimension);
	const float SafeWidth = FMath::Max(Width, EHBRoofMinDimension);
	const float ProjectionLength = SafeLength + EaveOffset * 2.0f;
	const float ProjectionWidth = SafeWidth + EaveOffset * 2.0f;

	if (AxisMode == EEHBRoofAxisMode::RidgeAlongX)
	{
		OutBounds.Min = FVector2D(-ProjectionLength * 0.5f, -ProjectionWidth * 0.5f);
		OutBounds.Max = FVector2D(ProjectionLength * 0.5f, ProjectionWidth * 0.5f);
		OutBounds.LocalSize = FVector(ProjectionLength, ProjectionWidth, 0.0f);
	}
	else
	{
		OutBounds.Min = FVector2D(-ProjectionWidth * 0.5f, -ProjectionLength * 0.5f);
		OutBounds.Max = FVector2D(ProjectionWidth * 0.5f, ProjectionLength * 0.5f);
		OutBounds.LocalSize = FVector(ProjectionWidth, ProjectionLength, 0.0f);
	}

	OutBounds.bIsValid = true;
	OutBounds.LocalCenter = FVector::ZeroVector;
	return true;
}

bool AEHBGableRoof::BuildUncutUnifiedRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();

	FGableRoofGeometry Geometry;
	BuildGableGeometry(*this, Geometry);

	FEHBRoofUnifiedMeshData BodyMesh;
	if (!BuildRawRoofMesh(BodyMesh) || !BodyMesh.IsValid())
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] raw body build failed roof=%s"), *GetName());
		return false;
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] raw closed body roof=%s triangles=%d body=%d side=%d cutOps=%d bounds=%s"),
		*GetName(),
		BodyMesh.Triangles.Num() / 3,
		CountGroupTriangles(BodyMesh, EHBGableRoofBodyGroupID),
		CountGroupTriangles(BodyMesh, EHBGableRoofSideWallGroupID),
		CutOperations.Num(),
		*BodyMesh.LocalBounds.ToString());

	UE::Geometry::FDynamicMesh3 UnifiedRoofDynamicMesh;
	if (!ConvertUnifiedMeshToDynamicMesh(BodyMesh, UnifiedRoofDynamicMesh))
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] raw body dynamic conversion failed roof=%s"), *GetName());
		return false;
	}

	int32 UnifiedSolidPartCount = 1;
	const auto UnionAccessoryPart = [this, &UnifiedRoofDynamicMesh, &UnifiedSolidPartCount](
		FEHBRoofUnifiedMeshData PartMesh,
		int32 GroupID,
		const TCHAR* DebugName)
	{
		if (!PartMesh.IsValid())
		{
			return true;
		}

		FEHBRoofUnifiedMeshData GroupedPart;
		AppendMeshWithGroup(PartMesh, GroupID, GroupedPart);
		if (!GroupedPart.IsValid())
		{
			return true;
		}

		UE::Geometry::FDynamicMesh3 PartDynamicMesh;
		if (!ConvertUnifiedMeshToDynamicMesh(GroupedPart, PartDynamicMesh))
		{
			UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] accessory dynamic conversion failed roof=%s part=%s group=%d"), *GetName(), DebugName, GroupID);
			return false;
		}

		UE::Geometry::FDynamicMesh3 UnionMesh;
		FString FailureReason;
		const int32 BeforeTriangles = UnifiedRoofDynamicMesh.TriangleCount();
		const bool bUnionApplied = FEHBManifoldBoolean::ApplyUnion(
			UnifiedRoofDynamicMesh,
			PartDynamicMesh,
			UnionMesh,
			EHBGableRoofBooleanMergeTolerance,
			EHBGableRoofBodyGroupID,
			EHBGableRoofSideWallGroupID,
			&FailureReason);
		if (!bUnionApplied)
		{
			UE_LOG(
				LogEHBRoof,
				Warning,
				TEXT("[EHB RoofV3] unified roof union failed roof=%s part=%s group=%d reason=%s"),
				*GetName(),
				DebugName,
				GroupID,
				*FailureReason);
			return false;
		}

		UnifiedRoofDynamicMesh = MoveTemp(UnionMesh);
		++UnifiedSolidPartCount;
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] unified roof union applied roof=%s part=%s group=%d before=%d part=%d after=%d"),
			*GetName(),
			DebugName,
			GroupID,
			BeforeTriangles,
			PartDynamicMesh.TriangleCount(),
			UnifiedRoofDynamicMesh.TriangleCount());
		return true;
	};

	const FVector AlongDirection = (Geometry.Ridge1 - Geometry.Ridge0).GetSafeNormal();
	if (bGenerateRidge)
	{
		FEHBRoofUnifiedMeshData RidgeMesh;
		AppendLinePrism(Geometry.Ridge0, Geometry.Ridge1, RidgeWidth, RidgeHeight, RidgeMesh);
		RidgeMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(RidgeMesh), EHBGableRoofRidgeGroupID, TEXT("Ridge")))
		{
			return false;
		}
	}

	if (bGenerateEaves)
	{
		FEHBRoofUnifiedMeshData EaveMesh;
		AppendLinePrism(Geometry.LowA0, Geometry.LowA1, EaveWidth, EaveHeight, EaveMesh);
		EaveMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(EaveMesh), EHBGableRoofEaveGroupID, TEXT("EaveLowA")))
		{
			return false;
		}

		EaveMesh.Reset();
		AppendLinePrism(Geometry.LowB0, Geometry.LowB1, EaveWidth, EaveHeight, EaveMesh);
		EaveMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(EaveMesh), EHBGableRoofEaveGroupID, TEXT("EaveLowB")))
		{
			return false;
		}
	}

	if (bGenerateGableEndWalls && !AlongDirection.IsNearlyZero())
	{
		const float AlongSpan = (Geometry.Ridge1 - Geometry.Ridge0).Size();
		const float SafeEndWallDepth = FMath::Max(Thickness, 1.0f);
		const float MaxBoundaryInset = FMath::Max(0.0f, (AlongSpan - SafeEndWallDepth - 1.0f) * 0.5f);
		const float BoundaryInset = FMath::Clamp(GableEndWallBoundaryInset, 0.0f, MaxBoundaryInset);
		const float CenterInset = BoundaryInset + SafeEndWallDepth * 0.5f;
		const FVector StartWallCenterOffset = AlongDirection * CenterInset;
		const FVector EndWallCenterOffset = -AlongDirection * CenterInset;

		FEHBRoofUnifiedMeshData GableEndWallMesh;
		AppendTriangularPrism(
			Geometry.LowA0 + StartWallCenterOffset,
			Geometry.Ridge0 + StartWallCenterOffset,
			Geometry.LowB0 + StartWallCenterOffset,
			-AlongDirection,
			SafeEndWallDepth,
			GableEndWallMesh);
		GableEndWallMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(GableEndWallMesh), EHBGableRoofSideWallGroupID, TEXT("GableEndStart")))
		{
			return false;
		}

		GableEndWallMesh.Reset();
		AppendTriangularPrism(
			Geometry.LowB1 + EndWallCenterOffset,
			Geometry.Ridge1 + EndWallCenterOffset,
			Geometry.LowA1 + EndWallCenterOffset,
			AlongDirection,
			SafeEndWallDepth,
			GableEndWallMesh);
		GableEndWallMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(GableEndWallMesh), EHBGableRoofSideWallGroupID, TEXT("GableEndEnd")))
		{
			return false;
		}
	}

	if (bGenerateGableRakes && !AlongDirection.IsNearlyZero())
	{
		FEHBRoofUnifiedMeshData RakeMesh;
		AppendExtrudedVFrame(
			Geometry.LowA0,
			Geometry.Ridge0,
			Geometry.LowB0,
			-AlongDirection,
			GableRakeWidth,
			GableRakeHeight,
			RakeMesh);
		RakeMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(RakeMesh), EHBGableRoofRakeGroupID, TEXT("GableRakeStart")))
		{
			return false;
		}

		RakeMesh.Reset();
		AppendExtrudedVFrame(
			Geometry.LowB1,
			Geometry.Ridge1,
			Geometry.LowA1,
			AlongDirection,
			GableRakeWidth,
			GableRakeHeight,
			RakeMesh);
		RakeMesh.RebuildBounds();
		if (!UnionAccessoryPart(MoveTemp(RakeMesh), EHBGableRoofRakeGroupID, TEXT("GableRakeEnd")))
		{
			return false;
		}
	}

	if (!ConvertDynamicMeshToUnifiedMesh(UnifiedRoofDynamicMesh, OutMesh) || !OutMesh.IsValid())
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] unified roof conversion failed roof=%s parts=%d triangles=%d"), *GetName(), UnifiedSolidPartCount, UnifiedRoofDynamicMesh.TriangleCount());
		return false;
	}
	OutMesh.RebuildBounds();
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] uncut unified roof built roof=%s parts=%d triangles=%d body=%d side=%d ridge=%d eaves=%d rakes=%d bounds=%s"),
		*GetName(),
		UnifiedSolidPartCount,
		OutMesh.Triangles.Num() / 3,
		CountGroupTriangles(OutMesh, EHBGableRoofBodyGroupID),
		CountGroupTriangles(OutMesh, EHBGableRoofSideWallGroupID),
		CountGroupTriangles(OutMesh, EHBGableRoofRidgeGroupID),
		CountGroupTriangles(OutMesh, EHBGableRoofEaveGroupID),
		CountGroupTriangles(OutMesh, EHBGableRoofRakeGroupID),
		*OutMesh.LocalBounds.ToString());
	return true;
}

bool AEHBGableRoof::RebuildRoofMesh()
{
	FEHBRoofUnifiedMeshData RenderMesh;
	if (!BuildUncutUnifiedRoofMesh(RenderMesh) || !RenderMesh.IsValid())
	{
		return false;
	}

	RenderMesh.RebuildBounds();
	if (bDebugDrawRawRoofBounds)
	{
		DrawRoofCutDebugMeshBounds(RenderMesh, FColor::Cyan, TEXT("raw unified roof"));
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] unified roof solid ready roof=%s triangles=%d body=%d side=%d ridge=%d eaves=%d rakes=%d bounds=%s"),
		*GetName(),
		RenderMesh.Triangles.Num() / 3,
		CountGroupTriangles(RenderMesh, EHBGableRoofBodyGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofSideWallGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofRidgeGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofEaveGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofRakeGroupID),
		*RenderMesh.LocalBounds.ToString());

	bool bRoofCutChanged = false;
	if (!CutOperations.IsEmpty())
	{
		FEHBRoofUnifiedMeshData BeforeCutMesh = RenderMesh;
		FEHBRoofUnifiedMeshData CutMesh = RenderMesh;
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] unified source-cut begin roof=%s triangles=%d bounds=%s"),
			*GetName(),
			CutMesh.Triangles.Num() / 3,
			*CutMesh.LocalBounds.ToString());

		if (!ApplySourceMeshCuts(CutMesh, false) || !CutMesh.IsValid())
		{
			UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] unified source-cut failed roof=%s"), *GetName());
			return false;
		}

		bRoofCutChanged = DidUnifiedMeshChange(BeforeCutMesh, CutMesh);
		RenderMesh = MoveTemp(CutMesh);
		RenderMesh.RebuildBounds();
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] unified source-cut result roof=%s changed=%d triangles=%d body=%d side=%d ridge=%d eaves=%d rakes=%d bounds=%s"),
			*GetName(),
			bRoofCutChanged ? 1 : 0,
			RenderMesh.Triangles.Num() / 3,
			CountGroupTriangles(RenderMesh, EHBGableRoofBodyGroupID),
			CountGroupTriangles(RenderMesh, EHBGableRoofSideWallGroupID),
			CountGroupTriangles(RenderMesh, EHBGableRoofRidgeGroupID),
			CountGroupTriangles(RenderMesh, EHBGableRoofEaveGroupID),
			CountGroupTriangles(RenderMesh, EHBGableRoofRakeGroupID),
			*RenderMesh.LocalBounds.ToString());
	}

	if (bRoofCutChanged)
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
			const FVector TargetCenter = RenderMesh.LocalBounds.IsValid
				? RenderMesh.LocalBounds.GetCenter()
				: FVector::ZeroVector;
			CompositeRawKeepDirection = TargetCenter - CompositeKeepOrigin;
			CompositeRawKeepDirection.Z = 0.0f;
			CompositeKeepDirection = CompositeRawKeepDirection;

			if (!CompositeRawKeepDirection.IsNearlyZero() && RenderMesh.LocalBounds.IsValid)
			{
				const FVector TargetSize = RenderMesh.LocalBounds.GetSize();
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

		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV3] unified filter direction roof=%s hasSource=%d sourceCenter=%s rawKeepDirection=%s keepDirection=%s mode=%s targetBounds=%s sourceBounds=%s"),
			*GetName(),
			CompositeSourceBounds.IsValid ? 1 : 0,
			*CompositeKeepOrigin.ToCompactString(),
			*CompositeRawKeepDirection.GetSafeNormal().ToCompactString(),
			*CompositeKeepDirection.GetSafeNormal().ToCompactString(),
			*CompositeKeepDirectionMode,
			*RenderMesh.LocalBounds.ToString(),
			*CompositeSourceBounds.ToString());

		bool bUseCompositeProjectionBoundary = false;
		double CompositeProjectionBoundary = 0.0;
		FString CompositeProjectionBoundaryMode = TEXT("none");
		if (bUseCompositeKeepDirection && CompositeSourceBounds.IsValid && RenderMesh.LocalBounds.IsValid)
		{
			const FVector SafeKeepDirection = CompositeKeepDirection.GetSafeNormal();
			double SourceMinProjection = 0.0;
			double SourceMaxProjection = 0.0;
			if (ProjectBoxOnDirection(CompositeSourceBounds, SafeKeepDirection, SourceMinProjection, SourceMaxProjection))
			{
				const float ProjectionPadding = FMath::Clamp(FMath::Max(Thickness, 1.0f) * 0.1f, 0.5f, 5.0f);
				const double TargetCenterProjection = static_cast<double>(FVector::DotProduct(RenderMesh.LocalBounds.GetCenter(), SafeKeepDirection));
				const double SourceCenterProjection = static_cast<double>(FVector::DotProduct(CompositeSourceBounds.GetCenter(), SafeKeepDirection));
				const double BoundaryBaseProjection = SourceMaxProjection;
				CompositeProjectionBoundary = BoundaryBaseProjection
					+ static_cast<double>(bKeepCutAwayDisconnectedPieces ? ProjectionPadding : -ProjectionPadding);
				CompositeProjectionBoundaryMode = TEXT("source-keep-face");
				bUseCompositeProjectionBoundary = true;
				UE_LOG(
					LogEHBRoof,
					Verbose,
					TEXT("[EHB RoofV3] component projection boundary roof=%s sourceProj=[%.2f, %.2f] sourceCenterProj=%.2f targetCenterProj=%.2f boundary=%.2f mode=%s padding=%.2f direction=%s"),
					*GetName(),
					SourceMinProjection,
					SourceMaxProjection,
					SourceCenterProjection,
					TargetCenterProjection,
					CompositeProjectionBoundary,
					*CompositeProjectionBoundaryMode,
					ProjectionPadding,
					*SafeKeepDirection.ToCompactString());
			}
		}

		if (!FilterCompositeGableRoofPieces(
				RenderMesh,
				bRemoveDisconnectedCutPieces,
				bKeepCutAwayDisconnectedPieces,
				GetName(),
				CompositeJoinTolerance,
				bUseCompositeKeepDirection,
				CompositeKeepOrigin,
				CompositeKeepDirection,
				bUseCompositeProjectionBoundary,
				CompositeProjectionBoundary,
				CompositeProjectionBoundaryMode))
		{
			UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] unified connected-piece filter failed roof=%s"), *GetName());
			return false;
		}
	}

	RenderMesh.RebuildBounds();
	if (bDebugDrawResultBounds)
	{
		DrawRoofCutDebugMeshBounds(RenderMesh, FColor::Green, TEXT("final unified roof"));
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] group counts before submit roof=%s body=%d side=%d ridge=%d eaves=%d rakes=%d cutChanged=%d bounds=%s"),
		*GetName(),
		CountGroupTriangles(RenderMesh, EHBGableRoofBodyGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofSideWallGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofRidgeGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofEaveGroupID),
		CountGroupTriangles(RenderMesh, EHBGableRoofRakeGroupID),
		bRoofCutChanged ? 1 : 0,
		*RenderMesh.LocalBounds.ToString());

	if (!RoofBodyMeshComponent || !RenderMesh.IsValid())
	{
		return false;
	}

	const FEHBResolvedDefaultRoofMaterials DefaultRoofMaterials = ResolveConfiguredDefaultRoofMaterials();

	UMaterialInterface* BodyMat = ResolveGableComponentMaterial(
		RoofBodyMeshComponent,
		RoofBodyMaterial ? RoofBodyMaterial.Get() : nullptr,
		DefaultRoofMaterials.Slope,
		UMaterial::GetDefaultMaterial(MD_Surface));
	UMaterialInterface* GableMat = ResolveGableComponentMaterial(
		GableEndWallMeshComponent,
		GableEndWallMaterial ? GableEndWallMaterial.Get() : nullptr,
		DefaultRoofMaterials.SideWall,
		BodyMat);
	UMaterialInterface* RidgeMat = ResolveGableComponentMaterial(
		RidgeMeshComponent,
		RidgeMaterial ? RidgeMaterial.Get() : (AccessoryMaterial ? AccessoryMaterial.Get() : nullptr),
		DefaultRoofMaterials.Ridge,
		BodyMat);
	UMaterialInterface* EaveMat = ResolveGableComponentMaterial(
		EaveMeshComponent,
		EaveMaterial ? EaveMaterial.Get() : (AccessoryMaterial ? AccessoryMaterial.Get() : nullptr),
		DefaultRoofMaterials.Eave,
		BodyMat);
	UMaterialInterface* RakeMat = ResolveGableComponentMaterial(
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

	ClearComponentSections(RoofBodyMeshComponent);
	ClearComponentSections(GableEndWallMeshComponent);
	ClearComponentSections(RidgeMeshComponent);
	ClearComponentSections(EaveMeshComponent);
	ClearComponentSections(GableRakeMeshComponent);

	SubmitGroupToComponent(RoofBodyMeshComponent, EHBGableRoofBodyGroupID, TEXT("RoofBody"), BodyMat);
	SubmitGroupToComponent(GableEndWallMeshComponent, EHBGableRoofSideWallGroupID, TEXT("RoofSideWalls"), GableMat);
	if (bGenerateRidge)
	{
		SubmitGroupToComponent(RidgeMeshComponent, EHBGableRoofRidgeGroupID, TEXT("GableRidge"), RidgeMat);
	}
	if (bGenerateEaves)
	{
		SubmitGroupToComponent(EaveMeshComponent, EHBGableRoofEaveGroupID, TEXT("GableEaves"), EaveMat);
	}
	if (bGenerateGableRakes)
	{
		SubmitGroupToComponent(GableRakeMeshComponent, EHBGableRoofRakeGroupID, TEXT("GableRakes"), RakeMat);
	}

	if (SubmittedComponentCount == 0)
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] no render components submitted roof=%s"), *GetName());
		return false;
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV3] submitted roof=%s triangles=%d"),
		*GetName(),
		RenderMesh.Triangles.Num() / 3);

	MarkPackageDirty();
	return true;
}

bool AEHBGableRoof::BuildRawRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();

	FGableRoofGeometry Geometry;
	BuildGableGeometry(*this, Geometry);

	const TArray<FVector> StartLoop = {
		Geometry.LowA0,
		Geometry.Ridge0,
		Geometry.LowB0,
		Geometry.BottomLowB0,
		Geometry.BottomRidge0,
		Geometry.BottomLowA0,
	};
	const TArray<FVector> EndLoop = {
		Geometry.LowA1,
		Geometry.Ridge1,
		Geometry.LowB1,
		Geometry.BottomLowB1,
		Geometry.BottomRidge1,
		Geometry.BottomLowA1,
	};

	const FVector AlongDirection = (Geometry.Ridge1 - Geometry.Ridge0).GetSafeNormal();
	const FVector CrossDirection = (Geometry.LowB0 - Geometry.LowA0).GetSafeNormal();
	if (AlongDirection.IsNearlyZero() || CrossDirection.IsNearlyZero())
	{
		return false;
	}

	TArray<FVector2D> Section2D;
	Section2D.Reserve(StartLoop.Num());
	for (const FVector& Point : StartLoop)
	{
		Section2D.Add(FVector2D(
			FVector::DotProduct(Point - Geometry.LowA0, CrossDirection),
			Point.Z - Geometry.LowA0.Z));
	}

	TArray<FIntVector> CapTriangles;
	if (!TriangulateSimplePolygon2D(Section2D, CapTriangles))
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV3] failed to triangulate raw gable body section roof=%s"), *GetName());
		return false;
	}

	for (const FIntVector& Triangle : CapTriangles)
	{
		AppendTriangleWithNormal(
			StartLoop[Triangle.X],
			StartLoop[Triangle.Y],
			StartLoop[Triangle.Z],
			-AlongDirection,
			OutMesh,
			EHBGableRoofSideWallGroupID);
		AppendTriangleWithNormal(
			EndLoop[Triangle.X],
			EndLoop[Triangle.Y],
			EndLoop[Triangle.Z],
			AlongDirection,
			OutMesh,
			EHBGableRoofSideWallGroupID);
	}

	for (int32 Index = 0; Index < StartLoop.Num(); ++Index)
	{
		const int32 NextIndex = (Index + 1) % StartLoop.Num();
		const FVector Edge = StartLoop[NextIndex] - StartLoop[Index];
		const FVector DesiredNormal = FVector::CrossProduct(AlongDirection, Edge).GetSafeNormal();
		const int32 GroupID = Index <= 1 ? EHBGableRoofBodyGroupID : EHBGableRoofSideWallGroupID;
		AppendFaceWithNormal(
			{ StartLoop[Index], EndLoop[Index], EndLoop[NextIndex], StartLoop[NextIndex] },
			DesiredNormal,
			OutMesh,
			GroupID);
	}

	OutMesh.RebuildBounds();
	return OutMesh.IsValid();
}

void AEHBGableRoof::RebuildRoofAccessories(const FEHBRoofUnifiedMeshData& UnusedUnifiedMesh)
{
	(void)UnusedUnifiedMesh;
	if (RidgeMeshComponent)
	{
		RidgeMeshComponent->ClearAllMeshSections();
	}
	if (EaveMeshComponent)
	{
		EaveMeshComponent->ClearAllMeshSections();
	}
	if (GableRakeMeshComponent)
	{
		GableRakeMeshComponent->ClearAllMeshSections();
	}
	if (GableEndWallMeshComponent)
	{
		GableEndWallMeshComponent->ClearAllMeshSections();
	}
}
