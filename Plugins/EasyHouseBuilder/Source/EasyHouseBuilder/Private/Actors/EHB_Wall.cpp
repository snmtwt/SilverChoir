// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_Wall.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBSurfaceOpening.h"
#include "Cutting/EHBPolygonClipper.h"

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Pillar.h"
#include "Arrangement2d.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Core/EHBPreparedWallOpening.h"
#include "Components/EHBVerticalSurfaceComponent.h"
#include "Components/PrimitiveComponent.h"
#include "ConstrainedDelaunay2.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallJunctionMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Settings/EHBBuildingToolsetSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogEHBWallMesh, Log, All);

namespace
{
	/** 墙面默认 UV 密度：每 100cm 对应 1 个 UV 单位，避免墙面纹理被压缩到 0-1 后明显拉伸。 */
	constexpr float EHBWallUVWorldSize = 100.0f;

	/**
	 * 洞口边界与墙面边界完全重合时，二维约束图中会出现两条重叠线段。
	 * 重叠线段没有唯一的拓扑归属，容易让三角化器无法判断这条边到底属于墙还是洞。
	 *
	 * 因此，对贴住墙底的门洞，把它的底边轻微延伸到墙体矩形外部。
	 * 延伸只参与二维求交，最终保留的三角形仍被严格限制在墙面矩形内部，
	 * 所以不会真的生成低于墙底的几何；它只负责让门洞与墙底形成两个明确交点。
	 */
	constexpr double EHBWallOpeningBoundaryExtension = 0.5;

	/** 连续样条点距离小于该值时视为同一点，避免零长度边破坏约束三角化。 */
	constexpr double EHBWallOpeningPointTolerance = 0.01;
	constexpr float EHBWallSampleMinTriangleEdge = 0.01f;
	constexpr float EHBWallSampleMinTriangleAltitude = 0.05f;

	bool AreOpeningPointsNearlyEqual(const FVector2d& A, const FVector2d& B)
	{
		const double DeltaX = A.X - B.X;
		const double DeltaY = A.Y - B.Y;
		return DeltaX * DeltaX + DeltaY * DeltaY
			<= EHBWallOpeningPointTolerance * EHBWallOpeningPointTolerance;
	}

	bool AreDoorWindowConnectionsEquivalent(
		const FEHBWallDoorWindowConnection& A,
		const FEHBWallDoorWindowConnection& B)
	{
		return A.DoorWindowGuid == B.DoorWindowGuid
			&& A.Kind == B.Kind
			&& FMath::IsNearlyEqual(A.DistanceFromStart, B.DistanceFromStart)
			&& FMath::IsNearlyEqual(A.BottomHeight, B.BottomHeight)
			&& FMath::IsNearlyEqual(A.OpeningWidth, B.OpeningWidth)
			&& FMath::IsNearlyEqual(A.OpeningHeight, B.OpeningHeight)
			&& FMath::IsNearlyEqual(A.OpeningThickness, B.OpeningThickness)
			&& A.DoorWindowLocalToWall.Equals(B.DoorWindowLocalToWall)
			&& A.LocalOutlinePoints == B.LocalOutlinePoints;
	}

	TSoftObjectPtr<UMaterialInterface> GetConfiguredDefaultWhiteBoxMaterial()
	{
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		return Settings ? Settings->DefaultWhiteBoxMaterial : TSoftObjectPtr<UMaterialInterface>();
	}

	/** 计算二维多边形的两倍有向面积；这里只用绝对值判断轮廓是否退化。 */
	double CalculateOpeningPolygonTwiceArea(const TArray<FVector2d>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 PointIndex = 0; PointIndex < Polygon.Num(); ++PointIndex)
		{
			const FVector2d& Current = Polygon[PointIndex];
			const FVector2d& Next = Polygon[(PointIndex + 1) % Polygon.Num()];
			TwiceArea += Current.X * Next.Y - Next.X * Current.Y;
		}
		return TwiceArea;
	}

	/**
	 * 删除相邻重复点和显式重复的闭合终点。
	 *
	 * USplineComponent 的闭合状态已经表达“最后一点连接第一点”，因此数据中不需要再保存一次首点。
	 * 若蓝图用户手工把两个控制点拖到同一位置，也会在这里合并，防止生成零长度约束边。
	 */
	bool SanitizeOpeningPolygon(TArray<FVector2d>& InOutPolygon)
	{
		TArray<FVector2d> SanitizedPoints;
		SanitizedPoints.Reserve(InOutPolygon.Num());

		for (const FVector2d& Point : InOutPolygon)
		{
			if (SanitizedPoints.IsEmpty() || !AreOpeningPointsNearlyEqual(SanitizedPoints.Last(), Point))
			{
				SanitizedPoints.Add(Point);
			}
		}

		if (SanitizedPoints.Num() >= 2 && AreOpeningPointsNearlyEqual(SanitizedPoints[0], SanitizedPoints.Last()))
		{
			SanitizedPoints.Pop(EAllowShrinking::No);
		}

		InOutPolygon = MoveTemp(SanitizedPoints);
		return InOutPolygon.Num() >= 3
			&& FMath::Abs(CalculateOpeningPolygonTwiceArea(InOutPolygon)) > UE_DOUBLE_SMALL_NUMBER;
	}

	/**
	 * 奇偶规则点在多边形测试。
	 *
	 * 这里不依赖轮廓顺时针或逆时针，原因是用户可以在蓝图中任意编辑样条点顺序。
	 * 对凹多边形同样有效；当多条洞口重叠时，调用方按“位于任意洞口内”处理，相当于洞口并集。
	 */
	bool IsPointInsideOpeningPolygon(const FVector2d& Point, const TArray<FVector2d>& Polygon)
	{
		bool bInside = false;
		for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
			CurrentIndex < Polygon.Num();
			PreviousIndex = CurrentIndex++)
		{
			const FVector2d& Current = Polygon[CurrentIndex];
			const FVector2d& Previous = Polygon[PreviousIndex];
			const bool bCrossesHorizontalRay = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
			if (!bCrossesHorizontalRay)
			{
				continue;
			}

			const double EdgeDeltaY = Previous.Y - Current.Y;
			const double IntersectionX =
				(Previous.X - Current.X) * (Point.Y - Current.Y) / EdgeDeltaY + Current.X;
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	/** 将一个闭合轮廓的所有边插入二维排列；排列会自动拆分边与边之间的交点。 */
	void InsertClosedOpeningPolygon(
		UE::Geometry::FArrangement2d& Arrangement,
		const TArray<FVector2d>& Polygon)
	{
		for (int32 PointIndex = 0; PointIndex < Polygon.Num(); ++PointIndex)
		{
			const FVector2d& Start = Polygon[PointIndex];
			const FVector2d& End = Polygon[(PointIndex + 1) % Polygon.Num()];
			if (!AreOpeningPointsNearlyEqual(Start, End))
			{
				Arrangement.Insert(Start, End);
			}
		}
	}

	float GetWallUVLength(float Centimeters)
	{
		return FMath::Max(1.0f, Centimeters) / EHBWallUVWorldSize;
	}

	const FEHBWallMeshData* FindWallMeshDataFromStyle(const FEHBWallSurfaceStyle& SurfaceStyle, const TCHAR* Context)
	{
		if (SurfaceStyle.SourceType != EEHBWallSurfaceSourceType::SampledMesh
			|| !SurfaceStyle.SampledWallRow.DataTable
			|| SurfaceStyle.SampledWallRow.RowName.IsNone()
			|| SurfaceStyle.SampledWallRow.DataTable->GetRowStruct() != FEHBWallMeshData::StaticStruct())
		{
			return nullptr;
		}

		return SurfaceStyle.SampledWallRow.DataTable->FindRow<FEHBWallMeshData>(
			SurfaceStyle.SampledWallRow.RowName,
			Context,
			false);
	}

	bool IsSampledWallSurfaceStyle(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		return FindWallMeshDataFromStyle(SurfaceStyle, TEXT("IsSampledWallSurfaceStyle")) != nullptr;
	}

	FString MakeWallSurfaceStyleContinuityKey(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		const FEHBWallMeshData* WallMeshData = FindWallMeshDataFromStyle(
			SurfaceStyle,
			TEXT("MakeWallSurfaceStyleContinuityKey"));
		if (!WallMeshData)
		{
			return FString();
		}

		FString TemplateIdentity;
		if (WallMeshData->TemplateMetadata.TemplateGuid.IsValid())
		{
			TemplateIdentity = WallMeshData->TemplateMetadata.TemplateGuid.ToString(EGuidFormats::DigitsWithHyphens);
		}
		else
		{
			TemplateIdentity = FString::Printf(
				TEXT("%s:%s"),
				*GetPathNameSafe(SurfaceStyle.SampledWallRow.DataTable),
				*SurfaceStyle.SampledWallRow.RowName.ToString());
		}

		return FString::Printf(
			TEXT("%s|Side=%d|Flip=%d|Override=%s"),
			*TemplateIdentity,
			static_cast<int32>(SurfaceStyle.SampleSide),
			SurfaceStyle.bFlipSampleSide ? 1 : 0,
			*SurfaceStyle.OverrideMaterial.ToSoftObjectPath().ToString());
	}

	bool AreWallSurfaceStylesContinuousMatch(const FEHBWallSurfaceStyle& A, const FEHBWallSurfaceStyle& B)
	{
		const FString AKey = MakeWallSurfaceStyleContinuityKey(A);
		return !AKey.IsEmpty() && AKey == MakeWallSurfaceStyleContinuityKey(B);
	}

	float GetWallMeshSampleWidth(const FEHBWallMeshData& WallMeshData)
	{
		return FMath::Max(
			1.0f,
			WallMeshData.WallWidth > UE_SMALL_NUMBER
				? WallMeshData.WallWidth
				: WallMeshData.WallLocalBoundsMax.X - WallMeshData.WallLocalBoundsMin.X);
	}

	float GetWallSurfaceManualSamplePhaseOffset(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		const FEHBWallMeshData* WallMeshData = FindWallMeshDataFromStyle(
			SurfaceStyle,
			TEXT("GetWallSurfaceManualSamplePhaseOffset"));
		if (!WallMeshData)
		{
			return 0.0f;
		}

		return FMath::Clamp(SurfaceStyle.SampleStartOffset, 0.0f, 1.0f) * GetWallMeshSampleWidth(*WallMeshData);
	}

	bool GetWallSurfaceEndpointFrame(
		const AEHB_Wall& Wall,
		bool bLeftSide,
		bool bAtStart,
		FVector& OutSidePoint,
		FVector& OutWallTangent,
		FVector& OutSideNormal)
	{
		const float WallLength = FVector::Dist2D(Wall.LocalStart, Wall.LocalEnd);
		if (WallLength <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const float EndpointX = bAtStart ? -WallLength * 0.5f : WallLength * 0.5f;
		const FVector WallLocalEndpoint(EndpointX, 0.0f, 0.0f);
		const FVector CurvedEndpoint = Wall.TransformStraightWallLocalPointToCurve(WallLocalEndpoint);
		const FVector WallLocalTangent = Wall.TransformStraightWallLocalVectorToCurve(
			WallLocalEndpoint,
			FVector::ForwardVector);

		const FTransform WallTransform = Wall.GetElementLocalTransform();
		OutWallTangent = WallTransform.TransformVectorNoScale(WallLocalTangent).GetSafeNormal2D();
		if (OutWallTangent.IsNearlyZero())
		{
			return false;
		}

		OutSideNormal = OutWallTangent
			.RotateAngleAxis(bLeftSide ? 90.0f : -90.0f, FVector::UpVector)
			.GetSafeNormal2D();
		if (OutSideNormal.IsNearlyZero())
		{
			return false;
		}

		const FVector CenterPoint = WallTransform.TransformPosition(CurvedEndpoint);
		OutSidePoint = CenterPoint + OutSideNormal * (FMath::Max(1.0f, Wall.Thickness) * 0.5f);
		return true;
	}

	/** Adds one quad face and orients triangle winding toward the requested surface normal. */
	void AddWallQuadFace(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& Normal,
		float ULength,
		float VLength)
	{
		const int32 FirstIndex = Vertices.Num();
		const FVector SafeNormal = Normal.GetSafeNormal();

		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
		Vertices.Add(D);

		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SafeNormal) <= 0.0f)
		{
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 1);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 3);
		}
		else
		{
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 1);
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 3);
			Triangles.Add(FirstIndex + 2);
		}

		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);

		UVs.Add(FVector2D(0.0f, VLength));
		UVs.Add(FVector2D(0.0f, 0.0f));
		UVs.Add(FVector2D(ULength, 0.0f));
		UVs.Add(FVector2D(ULength, VLength));
	}

	/** 添加一个四边形面，并使用调用者传入的 UV。顶面会使用本地 XY 投影 UV，避免沿墙长方向被拉伸。 */
	void AddWallQuadFaceWithUVs(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& Normal,
		const FVector2D& UVA,
		const FVector2D& UVB,
		const FVector2D& UVC,
		const FVector2D& UVD)
	{
		const int32 FirstIndex = Vertices.Num();
		const FVector SafeNormal = Normal.GetSafeNormal();

		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
		Vertices.Add(D);

		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SafeNormal) <= 0.0f)
		{
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 1);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 3);
		}
		else
		{
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 2);
			Triangles.Add(FirstIndex + 1);
			Triangles.Add(FirstIndex + 0);
			Triangles.Add(FirstIndex + 3);
			Triangles.Add(FirstIndex + 2);
		}

		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);

		UVs.Add(UVA);
		UVs.Add(UVB);
		UVs.Add(UVC);
		UVs.Add(UVD);
	}

	FVector2D MakeTopProjectedUV(const FVector& LocalVertex, float ReferenceLength, float Thickness)
	{
		return FVector2D(
			(LocalVertex.X + ReferenceLength * 0.5f) / EHBWallUVWorldSize,
			(LocalVertex.Y + Thickness * 0.5f) / EHBWallUVWorldSize);
	}

	bool GetBarycentricInTriangle2D(
		const FVector2d& Point,
		const FVector2d& A,
		const FVector2d& B,
		const FVector2d& C,
		double& OutA,
		double& OutB,
		double& OutC)
	{
		const FVector2d V0 = B - A;
		const FVector2d V1 = C - A;
		const FVector2d V2 = Point - A;
		const double Denominator = V0.X * V1.Y - V1.X * V0.Y;
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		OutB = (V2.X * V1.Y - V1.X * V2.Y) / Denominator;
		OutC = (V0.X * V2.Y - V2.X * V0.Y) / Denominator;
		OutA = 1.0 - OutB - OutC;
		return true;
	}

	bool IsPointInsideTriangle2D(
		const FVector2d& Point,
		const FVector2d& A,
		const FVector2d& B,
		const FVector2d& C,
		double Tolerance = EHBWallOpeningPointTolerance)
	{
		double WeightA = 0.0;
		double WeightB = 0.0;
		double WeightC = 0.0;
		if (!GetBarycentricInTriangle2D(Point, A, B, C, WeightA, WeightB, WeightC))
		{
			return false;
		}

		return WeightA >= -Tolerance
			&& WeightB >= -Tolerance
			&& WeightC >= -Tolerance
			&& WeightA <= 1.0 + Tolerance
			&& WeightB <= 1.0 + Tolerance
			&& WeightC <= 1.0 + Tolerance;
	}

	bool DoBoundingBoxesOverlap(
		double MinAX,
		double MaxAX,
		double MinAY,
		double MaxAY,
		double MinBX,
		double MaxBX,
		double MinBY,
		double MaxBY)
	{
		return MaxAX >= MinBX
			&& MaxBX >= MinAX
			&& MaxAY >= MinBY
			&& MaxBY >= MinAY;
	}

	double Cross2D(const FVector2d& A, const FVector2d& B, const FVector2d& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	bool IsPointOnSegment2D(const FVector2d& Point, const FVector2d& A, const FVector2d& B)
	{
		if (FMath::Abs(Cross2D(A, B, Point)) > EHBWallOpeningPointTolerance)
		{
			return false;
		}

		return Point.X >= FMath::Min(A.X, B.X) - EHBWallOpeningPointTolerance
			&& Point.X <= FMath::Max(A.X, B.X) + EHBWallOpeningPointTolerance
			&& Point.Y >= FMath::Min(A.Y, B.Y) - EHBWallOpeningPointTolerance
			&& Point.Y <= FMath::Max(A.Y, B.Y) + EHBWallOpeningPointTolerance;
	}

	bool DoSegmentsIntersect2D(const FVector2d& A, const FVector2d& B, const FVector2d& C, const FVector2d& D)
	{
		if (IsPointOnSegment2D(A, C, D) || IsPointOnSegment2D(B, C, D)
			|| IsPointOnSegment2D(C, A, B) || IsPointOnSegment2D(D, A, B))
		{
			return true;
		}

		const double CrossA = Cross2D(A, B, C);
		const double CrossB = Cross2D(A, B, D);
		const double CrossC = Cross2D(C, D, A);
		const double CrossD = Cross2D(C, D, B);
		return (CrossA > 0.0) != (CrossB > 0.0)
			&& (CrossC > 0.0) != (CrossD > 0.0);
	}

	bool IsPointInsideOrOnOpeningPolygon(const FVector2d& Point, const TArray<FVector2d>& Polygon)
	{
		for (int32 PointIndex = 0; PointIndex < Polygon.Num(); ++PointIndex)
		{
			if (IsPointOnSegment2D(Point, Polygon[PointIndex], Polygon[(PointIndex + 1) % Polygon.Num()]))
			{
				return true;
			}
		}

		return IsPointInsideOpeningPolygon(Point, Polygon);
	}

	bool DoesDegenerateTriangleTouchOpening(
		const FVector2d& A,
		const FVector2d& B,
		const FVector2d& C,
		const TArray<FVector2d>& OpeningPolygon)
	{
		if (IsPointInsideOrOnOpeningPolygon(A, OpeningPolygon)
			|| IsPointInsideOrOnOpeningPolygon(B, OpeningPolygon)
			|| IsPointInsideOrOnOpeningPolygon(C, OpeningPolygon)
			|| IsPointInsideOrOnOpeningPolygon((A + B + C) / 3.0, OpeningPolygon))
		{
			return true;
		}

		const FVector2d TrianglePoints[3] = { A, B, C };
		for (int32 TrianglePointIndex = 0; TrianglePointIndex < 3; ++TrianglePointIndex)
		{
			const FVector2d& SegmentStart = TrianglePoints[TrianglePointIndex];
			const FVector2d& SegmentEnd = TrianglePoints[(TrianglePointIndex + 1) % 3];
			if (AreOpeningPointsNearlyEqual(SegmentStart, SegmentEnd))
			{
				continue;
			}

			for (int32 OpeningPointIndex = 0; OpeningPointIndex < OpeningPolygon.Num(); ++OpeningPointIndex)
			{
				const FVector2d& OpeningStart = OpeningPolygon[OpeningPointIndex];
				const FVector2d& OpeningEnd = OpeningPolygon[(OpeningPointIndex + 1) % OpeningPolygon.Num()];
				if (DoSegmentsIntersect2D(SegmentStart, SegmentEnd, OpeningStart, OpeningEnd))
				{
					return true;
				}
			}
		}

		return false;
	}

	void AppendSurfaceTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		TArray<int32>& TriangleMaterialIndices,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& NormalA,
		const FVector& NormalB,
		const FVector& NormalC,
		const FVector2D& UVA,
		const FVector2D& UVB,
		const FVector2D& UVC,
		int32 MaterialIndex)
	{
		if (A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN())
		{
			return;
		}

		const float ABSquared = (B - A).SizeSquared();
		const float BCSquared = (C - B).SizeSquared();
		const float CASquared = (A - C).SizeSquared();
		const float MinEdgeSquared = FMath::Square(EHBWallSampleMinTriangleEdge);
		if (ABSquared <= MinEdgeSquared || BCSquared <= MinEdgeSquared || CASquared <= MinEdgeSquared)
		{
			return;
		}

		const float MaxEdgeLength = FMath::Sqrt(FMath::Max3(ABSquared, BCSquared, CASquared));
		const FVector Cross = FVector::CrossProduct(B - A, C - A);
		if (Cross.SizeSquared() <= FMath::Square(MaxEdgeLength * EHBWallSampleMinTriangleAltitude))
		{
			return;
		}

		const int32 FirstIndex = Vertices.Num();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);

		const FVector SafeNormalA = NormalA.GetSafeNormal();
		const FVector SafeNormalB = NormalB.GetSafeNormal();
		const FVector SafeNormalC = NormalC.GetSafeNormal();
		Normals.Add(SafeNormalA);
		Normals.Add(SafeNormalB);
		Normals.Add(SafeNormalC);

		UVs.Add(UVA);
		UVs.Add(UVB);
		UVs.Add(UVC);

		int32 BIndex = FirstIndex + 1;
		int32 CIndex = FirstIndex + 2;
		const FVector AverageNormal = (SafeNormalA + SafeNormalB + SafeNormalC).GetSafeNormal();
		if (!AverageNormal.IsNearlyZero())
		{
			const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (FVector::DotProduct(TriangleNormal, AverageNormal) > 0.0f)
			{
				Swap(BIndex, CIndex);
			}
		}

		Triangles.Add(FirstIndex);
		Triangles.Add(BIndex);
		Triangles.Add(CIndex);
		TriangleMaterialIndices.Add(FMath::Max(0, MaterialIndex));
	}

	int64 QuantizeOpeningCoordinate(double Value)
	{
		return FMath::RoundToInt64(Value / EHBWallOpeningPointTolerance);
	}

	FString MakeOpeningEdgeKey(const FVector2d& A, const FVector2d& B)
	{
		int64 AX = QuantizeOpeningCoordinate(A.X);
		int64 AY = QuantizeOpeningCoordinate(A.Y);
		int64 BX = QuantizeOpeningCoordinate(B.X);
		int64 BY = QuantizeOpeningCoordinate(B.Y);
		if (AX > BX || (AX == BX && AY > BY))
		{
			Swap(AX, BX);
			Swap(AY, BY);
		}

		return FString::Printf(TEXT("%lld:%lld:%lld:%lld"), AX, AY, BX, BY);
	}

	FString MakeRevealPairKey(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		const auto Quantize = [](float Value)
		{
			return FMath::RoundToInt64(static_cast<double>(Value) / EHBWallOpeningPointTolerance);
		};

		return FString::Printf(
			TEXT("%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld"),
			Quantize(A.X), Quantize(A.Y), Quantize(A.Z),
			Quantize(B.X), Quantize(B.Y), Quantize(B.Z),
			Quantize(C.X), Quantize(C.Y), Quantize(C.Z),
			Quantize(D.X), Quantize(D.Y), Quantize(D.Z));
	}

	void AppendCapTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& PreferredNormal,
		const FVector2D& UVA,
		const FVector2D& UVB,
		const FVector2D& UVC)
	{
		const int32 FirstIndex = Vertices.Num();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);

		const FVector SafeNormal = PreferredNormal.GetSafeNormal();
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		Normals.Add(SafeNormal);
		UVs.Add(UVA);
		UVs.Add(UVB);
		UVs.Add(UVC);

		int32 BIndex = FirstIndex + 1;
		int32 CIndex = FirstIndex + 2;
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (!SafeNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, SafeNormal) > 0.0f)
		{
			Swap(BIndex, CIndex);
		}

		Triangles.Add(FirstIndex);
		Triangles.Add(BIndex);
		Triangles.Add(CIndex);

		const int32 BackFirstIndex = Vertices.Num();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
		Normals.Add(-SafeNormal);
		Normals.Add(-SafeNormal);
		Normals.Add(-SafeNormal);
		UVs.Add(UVA);
		UVs.Add(UVB);
		UVs.Add(UVC);
		Triangles.Add(BackFirstIndex);
		Triangles.Add(BackFirstIndex + (CIndex - FirstIndex));
		Triangles.Add(BackFirstIndex + (BIndex - FirstIndex));
	}
}

AEHB_Wall::AEHB_Wall()
{
	ElementType = EEHBBuildingElementType::Wall;
	ElementCapabilities = static_cast<int32>(
		EEHBElementCapability::Structural
		| EEHBElementCapability::CanSupport
		| EEHBElementCapability::RequiresSupport
		| EEHBElementCapability::RoomBoundary
		| EEHBElementCapability::CanHost);
	SemanticTags.AddUnique(TEXT("Structure.Wall"));

	LeftWallMeshComponent = CreateDefaultSubobject<UEHBVerticalSurfaceComponent>(TEXT("LeftWallMesh"));
	LeftWallMeshComponent->SetupAttachment(SceneRoot);
	LeftWallMeshComponent->SetMobility(EComponentMobility::Movable);
	LeftWallMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LeftWallMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	LeftWallMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	LeftWallMeshComponent->bUseAsyncCooking = true;
	LeftWallMeshComponent->ComponentTags.AddUnique(TEXT("EHB_LeftWall"));
	if (UEHBArchitecturalSurfaceComponent* LeftSurface = Cast<UEHBArchitecturalSurfaceComponent>(LeftWallMeshComponent))
	{
		LeftSurface->InitializeSurface(this, EEHBArchitecturalSurfaceRole::WallLeftSide, TEXT("Wall.Left"), INDEX_NONE);
	}

	RightWallMeshComponent = CreateDefaultSubobject<UEHBVerticalSurfaceComponent>(TEXT("RightWallMesh"));
	RightWallMeshComponent->SetupAttachment(SceneRoot);
	RightWallMeshComponent->SetMobility(EComponentMobility::Movable);
	RightWallMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RightWallMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	RightWallMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	RightWallMeshComponent->bUseAsyncCooking = true;
	RightWallMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RightWall"));
	if (UEHBArchitecturalSurfaceComponent* RightSurface = Cast<UEHBArchitecturalSurfaceComponent>(RightWallMeshComponent))
	{
		RightSurface->InitializeSurface(this, EEHBArchitecturalSurfaceRole::WallRightSide, TEXT("Wall.Right"), INDEX_NONE);
	}

	CapMeshComponent = CreateDefaultSubobject<UEHBArchitecturalSurfaceComponent>(TEXT("WallCapMesh"));
	CapMeshComponent->SetupAttachment(SceneRoot);
	CapMeshComponent->SetMobility(EComponentMobility::Movable);
	CapMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CapMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CapMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CapMeshComponent->bUseAsyncCooking = true;
	CapMeshComponent->ComponentTags.AddUnique(TEXT("EHB_WallCap"));
	if (UEHBArchitecturalSurfaceComponent* CapSurface = Cast<UEHBArchitecturalSurfaceComponent>(CapMeshComponent))
	{
		CapSurface->InitializeSurface(this, EEHBArchitecturalSurfaceRole::WallCap, TEXT("Wall.Cap"), INDEX_NONE);
	}

	LeftSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Back;
	RightSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Front;

	Tags.AddUnique(TEXT("EHB_Wall"));
}

void AEHB_Wall::OnConstruction(const FTransform& Transform)
{
	if (FEHBActorImportScope::IsActive()) { Super::OnConstruction(Transform); return; }
	Super::OnConstruction(Transform);

	RebuildWallMesh();
}

void AEHB_Wall::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	Super::OnElementActorMoved_Implementation(OldLocalTransform, NewLocalTransform, bFinished);

	if (bRefreshingFromConnectedPillars)
	{
		return;
	}

	const float Length = GetWallLength();
	const FVector Direction = NewLocalTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	if (Length <= UE_SMALL_NUMBER || Direction.IsNearlyZero())
	{
		return;
	}

	const FVector Center = NewLocalTransform.GetLocation();
	LocalStart = Center - Direction * (Length * 0.5f);
	LocalEnd = Center + Direction * (Length * 0.5f);
	RebuildConnectedPillarMeshes();
	RebuildWallMesh();
}

void AEHB_Wall::OnElementActorDeleted_Implementation()
{
	DeleteConnectedDoorWindows();

	if (OwningBuilding)
	{
		OwningBuilding->HandleWallDeleted(this);
	}

	Super::OnElementActorDeleted_Implementation();
}

#if WITH_EDITOR
void AEHB_Wall::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditChangeProperty(PropertyChangedEvent); return; }
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AEHB_Wall, LocalStart) || PropertyName == GET_MEMBER_NAME_CHECKED(AEHB_Wall, LocalEnd))
		{
			SetElementLocalTransform(MakeWallLocalTransform(), true);
		}
	}

	RebuildConnectedPillarMeshes();
	RebuildWallMesh();
}
#endif

void AEHB_Wall::ConfigureAsSimpleWall(AEHBBuildingActorBase* InBuilding, AEHB_Pillar* InStartPillar, AEHB_Pillar* InEndPillar, const FVector& InLocalStart, const FVector& InLocalEnd, float InHeight, float InThickness, bool bFinished)
{
	OwningBuilding = InBuilding;
	LocalStart = InLocalStart;
	LocalEnd = InLocalEnd;
	Height = FMath::Max(1.0f, InHeight);
	Thickness = FMath::Max(1.0f, InThickness);
	CurveControlOffset = 0.0f;
	StartPillarGuid = InStartPillar ? InStartPillar->ElementGuid : FGuid();
	EndPillarGuid = InEndPillar ? InEndPillar->ElementGuid : FGuid();
	bHasStartPillarConnectionFace = false;
	bHasEndPillarConnectionFace = false;

	LeftSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::Simple;
	RightSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::Simple;
	LeftSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Back;
	RightSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Front;
	LeftSurfaceStyle.bFlipSampleSide = false;
	RightSurfaceStyle.bFlipSampleSide = false;
	const TSoftObjectPtr<UMaterialInterface> DefaultWhiteBoxMaterial = GetConfiguredDefaultWhiteBoxMaterial();
	if (LeftSurfaceStyle.OverrideMaterial.IsNull())
	{
		LeftSurfaceStyle.OverrideMaterial = DefaultWhiteBoxMaterial;
	}
	if (RightSurfaceStyle.OverrideMaterial.IsNull())
	{
		RightSurfaceStyle.OverrideMaterial = DefaultWhiteBoxMaterial;
	}

	AttachToBuilding(InBuilding, MakeWallLocalTransform());
	SetElementLocalTransform(MakeWallLocalTransform(), bFinished);
	RefreshFromConnectedPillars(bFinished);
}

bool AEHB_Wall::CanApplyNodeDefinition(const FEHBNodeConnectedWallDefinition& Definition,bool bValidatedSurfaceOpenings) const
{
	return GetClass()==AEHB_Wall::StaticClass()&&CurveControlOffset==0&&(CutOperations.IsEmpty()||bValidatedSurfaceOpenings)
		&&(DoorWindowConnections.IsEmpty()||bValidatedSurfaceOpenings)&&!bHasPreviewDoorWindowConnection
		&&LeftSurfaceStyle.SourceType==EEHBWallSurfaceSourceType::Simple&&RightSurfaceStyle.SourceType==EEHBWallSurfaceSourceType::Simple
		&&GetElementLocalTransform().GetScale3D().Equals(FVector::OneVector,0.0001)
		&&Definition.WallGuid==ElementGuid&&Definition.Height==Height&&Definition.Thickness==Thickness;
}

void AEHB_Wall::StageResolvedNodeGeometry(const FEHBWallJunctionWallSides& Geometry,bool bFinished)
{
	LocalStart = Geometry.LocalStart;
	LocalEnd = Geometry.LocalEnd;
	const FVector TopOffset(0, 0, FMath::Max(1.0f, Height));
	StartPillarConnectionLeftLocal = Geometry.StartLeft - TopOffset;
	StartPillarConnectionRightLocal = Geometry.StartRight - TopOffset;
	EndPillarConnectionLeftLocal = Geometry.EndLeft - TopOffset;
	EndPillarConnectionRightLocal = Geometry.EndRight - TopOffset;
	bHasStartPillarConnectionFace = bHasEndPillarConnectionFace = true;
	TGuardValue<bool> RefreshGuard(bRefreshingFromConnectedPillars, true);
	SetElementLocalTransform(Geometry.LocalTransform, bFinished);
}

void AEHB_Wall::ApplyResolvedNodeGeometry(const FEHBWallJunctionWallSides& Geometry,bool bFinished)
{
	StageResolvedNodeGeometry(Geometry,bFinished);
	TGuardValue<bool> ResolvedGuard(bApplyingResolvedNodeGeometry,true);
	RebuildWallMesh();
#if WITH_DEV_AUTOMATION_TESTS
	++NodeDefinitionRefreshSerial;
#endif
}

bool AEHB_Wall::RefreshFromNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Definitions,bool bFinished)
{
	if(!UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Definitions).IsEmpty())return false;
	return RefreshFromNodeModel(Definitions,bFinished);
}

bool AEHB_Wall::RefreshFromNodeModel(const FEHBWallNodeModel& Definitions,bool bFinished)
{
	const auto* Definition=Definitions.Walls.FindByPredicate([this](const auto& W){return W.WallGuid==ElementGuid;});
	if(!Definition||!CanApplyNodeDefinition(*Definition,true))return false;
	TSet<FGuid> Requested;Requested.Add(ElementGuid);TArray<FEHBWallJunctionWallSides> Sides;FName Reason;
	if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Definitions,Sides,Reason,&Requested)||Sides.Num()!=1)return false;
	FEHBPreparedWallOpening Prepared;if(!CutOperations.IsEmpty()&&!PrepareNodeSurfaceOpening(Sides[0],Prepared,Reason))return false;
	ApplyResolvedNodeGeometry(Sides[0],bFinished);return true;
}

void AEHB_Wall::RefreshFromConnectedPillars(bool bFinished)
{
 if(OwningBuilding&&OwningBuilding->WallNodeAuthority.Version==2)
 {FEHBWallNodeModel Model;if(UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(OwningBuilding,Model).bSucceeded)RefreshFromNodeModel(Model,bFinished);return;}
	// Prepared data is not authority yet: only use it when the complete legacy source still matches.
	if (OwningBuilding && OwningBuilding->PreparedWallNodeDefinitions.Version == 1)
	{
		if (UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(OwningBuilding, false).Status == TEXT("AlreadyPrepared")
			&& RefreshFromNodeDefinitions(OwningBuilding->PreparedWallNodeDefinitions, bFinished)) return;
		// A changed source may still have its old cached physical footprint. Resolve the legacy
		// reference line only after that cache is rebuilt; otherwise the first refresh lags behind.
		RebuildConnectedPillarMeshes();
	}
	AEHB_Pillar* StartPillar = FindPillarByGuid(StartPillarGuid);
	AEHB_Pillar* EndPillar = FindPillarByGuid(EndPillarGuid);

	const FVector StartPillarLocation = StartPillar
		? StartPillar->GetElementLocalTransform().GetLocation()
		: LocalStart;
	const FVector EndPillarLocation = EndPillar
		? EndPillar->GetElementLocalTransform().GetLocation()
		: LocalEnd;

	const bool bUseCurveReferencePath = IsCurveDeformationEnabled();
	FVector ResolvedStart = bUseCurveReferencePath ? StartPillarLocation : LocalStart;
	FVector ResolvedEnd = bUseCurveReferencePath ? EndPillarLocation : LocalEnd;
	if (StartPillar)
	{
		ResolvedStart = StartPillarLocation;
		if (!bUseCurveReferencePath)
		{
			StartPillar->ResolveWallConnectionPointToward(EndPillarLocation, Thickness, ResolvedStart);
		}
	}

	if (EndPillar)
	{
		ResolvedEnd = EndPillarLocation;
		if (!bUseCurveReferencePath)
		{
			EndPillar->ResolveWallConnectionPointToward(StartPillarLocation, Thickness, ResolvedEnd);
		}
	}

	const FVector Direction = (ResolvedEnd - ResolvedStart).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		LocalStart = ResolvedStart;
		LocalEnd = ResolvedEnd;
		TGuardValue<bool> RefreshGuard(bRefreshingFromConnectedPillars, true);
		SetElementLocalTransform(MakeWallLocalTransform(), bFinished);
	}

	RebuildConnectedPillarMeshes();
	RebuildWallMesh();
}

void AEHB_Wall::SetPillarConnectionFacePoints(const AEHB_Pillar* InPillar, const FVector& LeftLocalPoint, const FVector& RightLocalPoint)
{
	if (!InPillar)
	{
		return;
	}

	if (InPillar->ElementGuid == StartPillarGuid)
	{
		bHasStartPillarConnectionFace = true;
		StartPillarConnectionLeftLocal = LeftLocalPoint;
		StartPillarConnectionRightLocal = RightLocalPoint;
		return;
	}

	if (InPillar->ElementGuid == EndPillarGuid)
	{
		bHasEndPillarConnectionFace = true;
		EndPillarConnectionLeftLocal = LeftLocalPoint;
		EndPillarConnectionRightLocal = RightLocalPoint;
	}
}

void AEHB_Wall::ClearPillarConnectionFacePoints(const AEHB_Pillar* InPillar)
{
	if (!InPillar)
	{
		return;
	}

	if (InPillar->ElementGuid == StartPillarGuid)
	{
		bHasStartPillarConnectionFace = false;
		StartPillarConnectionLeftLocal = FVector::ZeroVector;
		StartPillarConnectionRightLocal = FVector::ZeroVector;
		return;
	}

	if (InPillar->ElementGuid == EndPillarGuid)
	{
		bHasEndPillarConnectionFace = false;
		EndPillarConnectionLeftLocal = FVector::ZeroVector;
		EndPillarConnectionRightLocal = FVector::ZeroVector;
	}
}

bool AEHB_Wall::ApplyMaterialToHitSurface(const UPrimitiveComponent* HitComponent, UMaterialInterface* Material, bool bApplyAllWallSurfaces)
{
	if (!Material)
	{
		return false;
	}

	const TSoftObjectPtr<UMaterialInterface> MaterialOverride(Material);
	const auto GetCurrentCapMaterial = [this]()
	{
		if (!CapOverrideMaterial.IsNull())
		{
			return CapOverrideMaterial;
		}
		if (!LeftSurfaceStyle.OverrideMaterial.IsNull())
		{
			return LeftSurfaceStyle.OverrideMaterial;
		}
		if (!RightSurfaceStyle.OverrideMaterial.IsNull())
		{
			return RightSurfaceStyle.OverrideMaterial;
		}
		return GetConfiguredDefaultWhiteBoxMaterial();
	};

	Modify();

	if (bApplyAllWallSurfaces)
	{
		LeftSurfaceStyle.OverrideMaterial = MaterialOverride;
		RightSurfaceStyle.OverrideMaterial = MaterialOverride;
		CapOverrideMaterial = MaterialOverride;
		RebuildWallMesh();
		MarkPackageDirty();
		return true;
	}

	if (HitComponent == LeftWallMeshComponent || (HitComponent && HitComponent->ComponentTags.Contains(TEXT("EHB_LeftWall"))))
	{
		if (CapOverrideMaterial.IsNull())
		{
			CapOverrideMaterial = GetCurrentCapMaterial();
		}
		LeftSurfaceStyle.OverrideMaterial = MaterialOverride;
		RebuildLeftWallMesh();
		MarkPackageDirty();
		return true;
	}

	if (HitComponent == RightWallMeshComponent || (HitComponent && HitComponent->ComponentTags.Contains(TEXT("EHB_RightWall"))))
	{
		if (CapOverrideMaterial.IsNull())
		{
			CapOverrideMaterial = GetCurrentCapMaterial();
		}
		RightSurfaceStyle.OverrideMaterial = MaterialOverride;
		RebuildRightWallMesh();
		MarkPackageDirty();
		return true;
	}

	if (HitComponent == CapMeshComponent || (HitComponent && HitComponent->ComponentTags.Contains(TEXT("EHB_WallCap"))))
	{
		CapOverrideMaterial = MaterialOverride;
		RebuildWallCapMesh();
		MarkPackageDirty();
		return true;
	}

	return false;
}

bool AEHB_Wall::ApplyMaterialToSide(bool bLeftSide, UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}

	const TSoftObjectPtr<UMaterialInterface> MaterialOverride(Material);
	const auto GetCurrentCapMaterial = [this]()
	{
		if (!CapOverrideMaterial.IsNull())
		{
			return CapOverrideMaterial;
		}
		if (!LeftSurfaceStyle.OverrideMaterial.IsNull())
		{
			return LeftSurfaceStyle.OverrideMaterial;
		}
		if (!RightSurfaceStyle.OverrideMaterial.IsNull())
		{
			return RightSurfaceStyle.OverrideMaterial;
		}
		return GetConfiguredDefaultWhiteBoxMaterial();
	};

	Modify();
	if (CapOverrideMaterial.IsNull())
	{
		CapOverrideMaterial = GetCurrentCapMaterial();
	}

	if (bLeftSide)
	{
		LeftSurfaceStyle.OverrideMaterial = MaterialOverride;
		RebuildLeftWallMesh();
	}
	else
	{
		RightSurfaceStyle.OverrideMaterial = MaterialOverride;
		RebuildRightWallMesh();
	}

	MarkPackageDirty();
	return true;
}

void AEHB_Wall::AddOrUpdateDoorWindowConnection(AEHB_DoorWindow* DoorWindow, float DistanceFromStart)
{
	if (!DoorWindow)
	{
		return;
	}

	EnsureElementGuid();
	DoorWindow->EnsureElementGuid();

	const FEHBWallDoorWindowConnection NewConnection =
		MakeDoorWindowConnection(DoorWindow, DistanceFromStart);

	for (FEHBWallDoorWindowConnection& Connection : DoorWindowConnections)
	{
		if (Connection.DoorWindowGuid == DoorWindow->ElementGuid)
		{
			if (AreDoorWindowConnectionsEquivalent(Connection, NewConnection))
			{
				SyncDoorWindowCutOperation(Connection, DoorWindow);
				return;
			}

			Connection = NewConnection;
			SyncDoorWindowCutOperation(Connection, DoorWindow);
			MarkPackageDirty();
			return;
		}
	}

	DoorWindowConnections.Add(NewConnection);
	SyncDoorWindowCutOperation(NewConnection, DoorWindow);
	MarkPackageDirty();
}

void AEHB_Wall::SetPreviewDoorWindowOpening(AEHB_DoorWindow* DoorWindow, float DistanceFromStart)
{
	if (!DoorWindow)
	{
		ClearPreviewDoorWindowOpening();
		return;
	}

	DoorWindow->EnsureElementGuid();
	const FEHBWallDoorWindowConnection NewPreviewConnection =
		MakeDoorWindowConnection(DoorWindow, DistanceFromStart);

	// Placement preview also updates from editor Tick. Do not rebuild an unchanged wall every frame.
	if (bHasPreviewDoorWindowConnection
		&& AreDoorWindowConnectionsEquivalent(
			PreviewDoorWindowConnection,
			NewPreviewConnection))
	{
		return;
	}

	PreviewDoorWindowConnection = NewPreviewConnection;
	bHasPreviewDoorWindowConnection = true;
	RebuildWallMesh();
}

void AEHB_Wall::ClearPreviewDoorWindowOpening(bool bRebuildWall)
{
	if (!bHasPreviewDoorWindowConnection)
	{
		return;
	}

	bHasPreviewDoorWindowConnection = false;
	PreviewDoorWindowConnection = FEHBWallDoorWindowConnection();
	if (bRebuildWall)
	{
		RebuildWallMesh();
	}
}

void AEHB_Wall::RemoveDoorWindowConnectionByGuid(const FGuid& DoorWindowGuid)
{
	if (!DoorWindowGuid.IsValid())
	{
		return;
	}

	const int32 RemovedCount = DoorWindowConnections.RemoveAll(
		[DoorWindowGuid](const FEHBWallDoorWindowConnection& Connection)
		{
			return Connection.DoorWindowGuid == DoorWindowGuid;
		});
	RemoveDoorWindowCutOperationByGuid(DoorWindowGuid);

	if (RemovedCount > 0)
	{
		MarkPackageDirty();
	}
}

AEHB_DoorWindow* AEHB_Wall::FindOverlappingDoorWindow(AEHB_DoorWindow* DoorWindow, float DistanceFromStart, float Tolerance) const
{
	if (!DoorWindow || DoorWindowConnections.IsEmpty())
	{
		return nullptr;
	}

	FEHBWallResolvedGeometry Geometry;
	if (!BuildResolvedWallGeometry(Geometry) || Geometry.ReferenceLength <= UE_SMALL_NUMBER)
	{
		return nullptr;
	}

	auto BuildOpeningBounds = [this, &Geometry](const FEHBWallDoorWindowConnection& Connection, FBox2D& OutBounds)
	{
		TArray<FVector2d> OpeningPolygon;
		if (!BuildConnectionOpeningPolygon(Geometry, Connection, OpeningPolygon) || OpeningPolygon.IsEmpty())
		{
			return false;
		}

		OutBounds = FBox2D(ForceInit);
		for (const FVector2d& Point : OpeningPolygon)
		{
			OutBounds += FVector2D(Point);
		}
		return OutBounds.bIsValid;
	};

	FBox2D CandidateBounds(ForceInit);
	const FEHBWallDoorWindowConnection CandidateConnection =
		MakeDoorWindowConnection(DoorWindow, DistanceFromStart);
	if (!BuildOpeningBounds(CandidateConnection, CandidateBounds))
	{
		return nullptr;
	}

	const float SafeTolerance = FMath::Max(0.0f, Tolerance);
	CandidateBounds.Min -= FVector2D(SafeTolerance, SafeTolerance);
	CandidateBounds.Max += FVector2D(SafeTolerance, SafeTolerance);

	for (const FEHBWallDoorWindowConnection& ExistingConnection : DoorWindowConnections)
	{
		if (DoorWindow->ElementGuid.IsValid() && ExistingConnection.DoorWindowGuid == DoorWindow->ElementGuid)
		{
			continue;
		}

		AEHB_DoorWindow* ExistingDoorWindow = FindDoorWindowByGuid(ExistingConnection.DoorWindowGuid);
		if (!ExistingDoorWindow || ExistingDoorWindow == DoorWindow || ExistingDoorWindow->IsActorBeingDestroyed())
		{
			continue;
		}

		FBox2D ExistingBounds(ForceInit);
		if (!BuildOpeningBounds(ExistingConnection, ExistingBounds))
		{
			continue;
		}

		if (CandidateBounds.Intersect(ExistingBounds))
		{
			return ExistingDoorWindow;
		}
	}

	return nullptr;
}

float AEHB_Wall::CalculateDistanceFromStartForWorldLocation(const FVector& WorldLocation) const
{
	const FVector Segment = LocalEnd - LocalStart;
	const FVector Direction = Segment.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return 0.0f;
	}

	FVector LocalLocation = WorldLocation;
	if (OwningBuilding)
	{
		LocalLocation = OwningBuilding->GetActorTransform().InverseTransformPosition(WorldLocation);
	}
	else
	{
		LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
		LocalLocation = MakeWallLocalTransform().TransformPosition(LocalLocation);
	}

	const float Distance = FVector::DotProduct(LocalLocation - LocalStart, Direction);
	return FMath::Clamp(Distance, 0.0f, GetWallLength());
}

FVector AEHB_Wall::GetBuildingLocalLocationOnCenterAxisAtDistance(float DistanceFromStart, float BottomHeight) const
{
	const float Length = GetWallLength();
	const FVector Direction = (LocalEnd - LocalStart).GetSafeNormal2D();
	if (Length <= UE_SMALL_NUMBER || Direction.IsNearlyZero())
	{
		return LocalStart;
	}

	const float ClampedDistance = FMath::Clamp(DistanceFromStart, 0.0f, Length);
	const float Alpha = ClampedDistance / Length;
	FVector LocalAxisLocation = LocalStart + Direction * ClampedDistance;
	LocalAxisLocation.Z = FMath::Lerp(LocalStart.Z, LocalEnd.Z, Alpha) + FMath::Max(0.0f, BottomHeight);

	return LocalAxisLocation;
}

FVector AEHB_Wall::GetWorldLocationOnCenterAxisAtDistance(float DistanceFromStart, float BottomHeight) const
{
	const FVector LocalAxisLocation = GetBuildingLocalLocationOnCenterAxisAtDistance(DistanceFromStart, BottomHeight);
	return OwningBuilding
		? OwningBuilding->GetActorTransform().TransformPosition(LocalAxisLocation)
		: LocalAxisLocation;
}

FVector AEHB_Wall::ProjectWorldLocationToCenterAxis(const FVector& WorldLocation, float BottomHeight) const
{
	const float DistanceFromStart = CalculateDistanceFromStartForWorldLocation(WorldLocation);
	return GetWorldLocationOnCenterAxisAtDistance(DistanceFromStart, BottomHeight);
}

FVector AEHB_Wall::GetCurveControlWorldLocation() const
{
	const float ControlHeight = FMath::Max(1.0f, Height) + 40.0f;
	return GetActorTransform().TransformPosition(TransformStraightWallLocalPointToCurve(FVector(0.0f, 0.0f, ControlHeight)));
}

void AEHB_Wall::SetCurveControlWorldLocation(const FVector& WorldLocation)
{
	const FVector WallLocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float NewOffset = WallLocalLocation.Y;
	if (FMath::IsNearlyEqual(CurveControlOffset, NewOffset, 0.01f))
	{
		return;
	}

	ApplyCurveSettings(NewOffset, CurveSegmentLength, false);
}

void AEHB_Wall::ApplyCurveSettings(float ControlOffset, float SegmentLength, bool bFinished)
{
	const float ClampedSegmentLength = FMath::Max(10.0f, SegmentLength);
	const bool bCurveChanged =
		!FMath::IsNearlyEqual(CurveControlOffset, ControlOffset, 0.01f)
		|| !FMath::IsNearlyEqual(CurveSegmentLength, ClampedSegmentLength, 0.01f);

	CurveControlOffset = ControlOffset;
	CurveSegmentLength = ClampedSegmentLength;
	RefreshFromConnectedPillars(bFinished);
	RebuildConnectedPillarMeshes();
	RebuildWallMesh();

	if (bCurveChanged)
	{
		NotifyElementGeometryChanged(bFinished);
	}

	MarkPackageDirty();
}

bool AEHB_Wall::IsCurveDeformationEnabled() const
{
	return FMath::Abs(CurveControlOffset) > 0.1f && GetWallLength() > 1.0f;
}

FVector AEHB_Wall::TransformStraightWallLocalPointToCurve(const FVector& WallLocalPoint) const
{
	if (!IsCurveDeformationEnabled())
	{
		return WallLocalPoint;
	}

	const float Length = GetWallLength();
	const float HalfLength = Length * 0.5f;
	const float Sagitta = CurveControlOffset;
	const float SagittaSign = Sagitta >= 0.0f ? 1.0f : -1.0f;
	const float AbsSagitta = FMath::Max(0.1f, FMath::Abs(Sagitta));
	const float Radius = (HalfLength * HalfLength + AbsSagitta * AbsSagitta) / (2.0f * AbsSagitta);
	if (Radius <= UE_SMALL_NUMBER)
	{
		return WallLocalPoint;
	}

	const float CenterY = -SagittaSign * (Radius - AbsSagitta);
	const float HalfAngle = FMath::Acos(FMath::Clamp((Radius - AbsSagitta) / Radius, -1.0f, 1.0f));
	const float MidAngle = SagittaSign > 0.0f ? HALF_PI : -HALF_PI;
	const float StartAngle = MidAngle + SagittaSign * HalfAngle;
	const float EndAngle = MidAngle - SagittaSign * HalfAngle;
	const float Alpha = FMath::Clamp((WallLocalPoint.X + HalfLength) / Length, 0.0f, 1.0f);
	const float Angle = FMath::Lerp(StartAngle, EndAngle, Alpha);

	const FVector CenterlinePoint(
		Radius * FMath::Cos(Angle),
		CenterY + Radius * FMath::Sin(Angle),
		WallLocalPoint.Z);

	const float AngleDelta = EndAngle - StartAngle;
	FVector Tangent(
		-Radius * FMath::Sin(Angle) * AngleDelta,
		Radius * FMath::Cos(Angle) * AngleDelta,
		0.0f);
	Tangent = Tangent.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
	const FVector CurveNormal(-Tangent.Y, Tangent.X, 0.0f);

	return CenterlinePoint + CurveNormal * WallLocalPoint.Y;
}

FVector AEHB_Wall::TransformStraightWallLocalVectorToCurve(const FVector& WallLocalPoint, const FVector& WallLocalVector) const
{
	if (!IsCurveDeformationEnabled())
	{
		return WallLocalVector.GetSafeNormal();
	}

	const float Length = GetWallLength();
	const float HalfLength = Length * 0.5f;
	const float Sagitta = CurveControlOffset;
	const float SagittaSign = Sagitta >= 0.0f ? 1.0f : -1.0f;
	const float AbsSagitta = FMath::Max(0.1f, FMath::Abs(Sagitta));
	const float Radius = (HalfLength * HalfLength + AbsSagitta * AbsSagitta) / (2.0f * AbsSagitta);
	if (Radius <= UE_SMALL_NUMBER)
	{
		return WallLocalVector.GetSafeNormal();
	}

	const float HalfAngle = FMath::Acos(FMath::Clamp((Radius - AbsSagitta) / Radius, -1.0f, 1.0f));
	const float MidAngle = SagittaSign > 0.0f ? HALF_PI : -HALF_PI;
	const float StartAngle = MidAngle + SagittaSign * HalfAngle;
	const float EndAngle = MidAngle - SagittaSign * HalfAngle;
	const float Alpha = FMath::Clamp((WallLocalPoint.X + HalfLength) / Length, 0.0f, 1.0f);
	const float Angle = FMath::Lerp(StartAngle, EndAngle, Alpha);
	const float AngleDelta = EndAngle - StartAngle;

	FVector Tangent(
		-Radius * FMath::Sin(Angle) * AngleDelta,
		Radius * FMath::Cos(Angle) * AngleDelta,
		0.0f);
	Tangent = Tangent.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
	const FVector CurveNormal(-Tangent.Y, Tangent.X, 0.0f);

	return (Tangent * WallLocalVector.X + CurveNormal * WallLocalVector.Y + FVector::UpVector * WallLocalVector.Z).GetSafeNormal();
}

bool AEHB_Wall::BuildSideTopPolylineInBuildingSpace(bool bLeftSide, TArray<FVector>& OutBuildingLocalPoints, float MaxSegmentLength) const
{
	OutBuildingLocalPoints.Reset();

	FEHBWallResolvedGeometry Geometry;
	if (!BuildResolvedWallGeometry(Geometry))
	{
		return false;
	}

	const float HalfThickness = FMath::Max(1.0f, Thickness) * 0.5f;
	const float WallY = bLeftSide ? HalfThickness : -HalfThickness;
	const float StartX = bLeftSide ? Geometry.StartLeftX : Geometry.StartRightX;
	const float EndX = bLeftSide ? Geometry.EndLeftX : Geometry.EndRightX;
	const float SideLength = FMath::Abs(EndX - StartX);
	if (SideLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const int32 SegmentCount = IsCurveDeformationEnabled()
		? FMath::Clamp(FMath::CeilToInt(SideLength / FMath::Max(10.0f, MaxSegmentLength)), 4, 256)
		: 1;
	const FTransform WallLocalTransform = GetElementLocalTransform();
	const float SafeHeight = FMath::Max(1.0f, Height);

	OutBuildingLocalPoints.Reserve(SegmentCount + 1);
	for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
		const float X = FMath::Lerp(StartX, EndX, Alpha);
		const FVector WallLocalPoint = TransformStraightWallLocalPointToCurve(FVector(X, WallY, SafeHeight));
		OutBuildingLocalPoints.Add(WallLocalTransform.TransformPosition(WallLocalPoint));
	}

	return OutBuildingLocalPoints.Num() >= 2;
}

void AEHB_Wall::ApplyCurveDeformationToMesh(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<int32>* TriangleMaterialIndices) const
{
	if (!IsCurveDeformationEnabled() || Vertices.IsEmpty() || Triangles.Num() < 3)
	{
		return;
	}

	struct FCurveMeshVertex
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector2D UV = FVector2D::ZeroVector;
	};

	const float MaxSegmentLength = FMath::Max(10.0f, CurveSegmentLength);
	const bool bHasMaterialIndices = TriangleMaterialIndices && !TriangleMaterialIndices->IsEmpty();
	TArray<FCurveMeshVertex> SubdividedVertices;
	TArray<int32> SubdividedTriangles;
	TArray<int32> SubdividedTriangleMaterialIndices;
	SubdividedVertices.Reserve(Vertices.Num() * 2);
	SubdividedTriangles.Reserve(Triangles.Num() * 2);
	if (bHasMaterialIndices)
	{
		SubdividedTriangleMaterialIndices.Reserve(Triangles.Num() / 3 * 2);
	}

	auto MakeVertex = [&Vertices, &Normals, &UVs](int32 VertexIndex)
	{
		FCurveMeshVertex Result;
		if (Vertices.IsValidIndex(VertexIndex))
		{
			Result.Position = Vertices[VertexIndex];
		}
		if (Normals.IsValidIndex(VertexIndex))
		{
			Result.Normal = Normals[VertexIndex].GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		}
		if (UVs.IsValidIndex(VertexIndex))
		{
			Result.UV = UVs[VertexIndex];
		}
		return Result;
	};

	auto MakeMidpoint = [](const FCurveMeshVertex& A, const FCurveMeshVertex& B)
	{
		FCurveMeshVertex Result;
		Result.Position = (A.Position + B.Position) * 0.5f;
		Result.Normal = (A.Normal + B.Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		Result.UV = (A.UV + B.UV) * 0.5f;
		return Result;
	};

	auto AppendTriangle = [&SubdividedVertices, &SubdividedTriangles, &SubdividedTriangleMaterialIndices, bHasMaterialIndices](
		const FCurveMeshVertex& A,
		const FCurveMeshVertex& B,
		const FCurveMeshVertex& C,
		int32 MaterialIndex)
	{
		const int32 FirstIndex = SubdividedVertices.Num();
		SubdividedVertices.Add(A);
		SubdividedVertices.Add(B);
		SubdividedVertices.Add(C);
		SubdividedTriangles.Add(FirstIndex);
		SubdividedTriangles.Add(FirstIndex + 1);
		SubdividedTriangles.Add(FirstIndex + 2);
		if (bHasMaterialIndices)
		{
			SubdividedTriangleMaterialIndices.Add(MaterialIndex);
		}
	};

	TFunction<void(const FCurveMeshVertex&, const FCurveMeshVertex&, const FCurveMeshVertex&, int32, int32)> AppendSubdividedTriangle;
	AppendSubdividedTriangle =
		[&AppendSubdividedTriangle, &AppendTriangle, &MakeMidpoint, MaxSegmentLength](
			const FCurveMeshVertex& A,
			const FCurveMeshVertex& B,
			const FCurveMeshVertex& C,
			int32 MaterialIndex,
			int32 Depth)
	{
		const float SpanAB = FMath::Abs(A.Position.X - B.Position.X);
		const float SpanBC = FMath::Abs(B.Position.X - C.Position.X);
		const float SpanCA = FMath::Abs(C.Position.X - A.Position.X);
		const float MaxSpan = FMath::Max3(SpanAB, SpanBC, SpanCA);
		constexpr int32 MaxDepth = 16;
		if (MaxSpan <= MaxSegmentLength || Depth >= MaxDepth)
		{
			AppendTriangle(A, B, C, MaterialIndex);
			return;
		}

		if (SpanAB >= SpanBC && SpanAB >= SpanCA)
		{
			const FCurveMeshVertex M = MakeMidpoint(A, B);
			AppendSubdividedTriangle(A, M, C, MaterialIndex, Depth + 1);
			AppendSubdividedTriangle(M, B, C, MaterialIndex, Depth + 1);
		}
		else if (SpanBC >= SpanAB && SpanBC >= SpanCA)
		{
			const FCurveMeshVertex M = MakeMidpoint(B, C);
			AppendSubdividedTriangle(A, B, M, MaterialIndex, Depth + 1);
			AppendSubdividedTriangle(A, M, C, MaterialIndex, Depth + 1);
		}
		else
		{
			const FCurveMeshVertex M = MakeMidpoint(C, A);
			AppendSubdividedTriangle(A, B, M, MaterialIndex, Depth + 1);
			AppendSubdividedTriangle(M, B, C, MaterialIndex, Depth + 1);
		}
	};

	const int32 TriangleCount = Triangles.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 IndexA = Triangles[TriangleIndex * 3 + 0];
		const int32 IndexB = Triangles[TriangleIndex * 3 + 1];
		const int32 IndexC = Triangles[TriangleIndex * 3 + 2];
		if (!Vertices.IsValidIndex(IndexA) || !Vertices.IsValidIndex(IndexB) || !Vertices.IsValidIndex(IndexC))
		{
			continue;
		}

		const int32 MaterialIndex = bHasMaterialIndices && TriangleMaterialIndices->IsValidIndex(TriangleIndex)
			? (*TriangleMaterialIndices)[TriangleIndex]
			: 0;
		AppendSubdividedTriangle(MakeVertex(IndexA), MakeVertex(IndexB), MakeVertex(IndexC), MaterialIndex, 0);
	}

	Vertices.Reset(SubdividedVertices.Num());
	Normals.Reset(SubdividedVertices.Num());
	UVs.Reset(SubdividedVertices.Num());
	for (const FCurveMeshVertex& Vertex : SubdividedVertices)
	{
		Vertices.Add(TransformStraightWallLocalPointToCurve(Vertex.Position));
		Normals.Add(TransformStraightWallLocalVectorToCurve(Vertex.Position, Vertex.Normal));
		UVs.Add(Vertex.UV);
	}

	Triangles = MoveTemp(SubdividedTriangles);
	if (bHasMaterialIndices)
	{
		*TriangleMaterialIndices = MoveTemp(SubdividedTriangleMaterialIndices);
	}
}

void AEHB_Wall::RebuildWallMesh()
{
#if WITH_EDITOR
 // Native wall source, side caches and generated sections are transactional.
 // Reconstructing during undo overwrites the sections just restored by UE,
 // including per-component materials, collision and visibility.
 if(GIsTransacting && LeftWallMeshComponent && RightWallMeshComponent && CapMeshComponent
  && LeftWallMeshComponent->HasAnyFlags(RF_Transactional) && RightWallMeshComponent->HasAnyFlags(RF_Transactional) && CapMeshComponent->HasAnyFlags(RF_Transactional))return;
#endif
	FName OpeningStatus;
	if(!ValidateSurfaceOpeningBindings(CutOperations,OpeningStatus))return;
	if (bRebuildingWallMesh)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bRebuildingWallMesh, true);
	// A legacy pillar refresh may replace these transient faces using world/local
	// round trips. For an active, matching node definition always restore its
	// authoritative faces once per wall rebuild, independent of cache warmth.
	if (!bApplyingResolvedNodeGeometry && OwningBuilding && OwningBuilding->WallNodeOwnership.Version == 1)
	{
		FEHBWallNodeModel Source;
		if (UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(OwningBuilding, Source).bSucceeded)
		{
			const auto* Definition = Source.Walls.FindByPredicate([this](const auto& W) { return W.WallGuid == ElementGuid; });
			TSet<FGuid> Requested; Requested.Add(ElementGuid);
			TArray<FEHBWallJunctionWallSides> Sides; FName Status;
			if (Definition && CanApplyNodeDefinition(*Definition,true)
				&& UEHBWallTopologyLibrary::BuildWallNodeModelSides(Source, Sides, Status, &Requested) && Sides.Num() == 1)
			{
				const auto& Side = Sides[0];
				if (GetElementLocalTransform().Equals(Side.LocalTransform, 0.0001)
					&& LocalStart.Equals(Side.LocalStart, 0.0001) && LocalEnd.Equals(Side.LocalEnd, 0.0001))
				{
					const FVector TopOffset(0, 0, FMath::Max(1.0f, Height));
					StartPillarConnectionLeftLocal = Side.StartLeft - TopOffset;
					StartPillarConnectionRightLocal = Side.StartRight - TopOffset;
					EndPillarConnectionLeftLocal = Side.EndLeft - TopOffset;
					EndPillarConnectionRightLocal = Side.EndRight - TopOffset;
					bHasStartPillarConnectionFace = bHasEndPillarConnectionFace = true;
				}
			}
		}
	}

	RefreshConnectedDoorWindowTransforms();
	RebuildLeftWallMesh();
	RebuildRightWallMesh();
	RebuildWallCapMesh();
}

void AEHB_Wall::RebuildLeftWallMesh()
{
	FName OpeningStatus;
	if(!bRebuildingWallMesh&&!ValidateSurfaceOpeningBindings(CutOperations,OpeningStatus))return;
	const auto UpdateWallSurfaceData = [this](UEHBGeneratedMeshComponent* TargetComponent, bool bLeftSide)
	{
		UEHBVerticalSurfaceComponent* SurfaceComponent = Cast<UEHBVerticalSurfaceComponent>(TargetComponent);
		if (!SurfaceComponent)
		{
			return;
		}

		const float WallLength = GetWallLength();
		if (WallLength <= UE_SMALL_NUMBER)
		{
			return;
		}

		const float HalfLength = WallLength * 0.5f;
		const float SideSign = bLeftSide ? 1.0f : -1.0f;
		const float SideY = SideSign * FMath::Max(1.0f, Thickness) * 0.5f;
		const int32 SegmentCount = IsCurveDeformationEnabled()
			? FMath::Clamp(FMath::CeilToInt(WallLength / FMath::Max(10.0f, CurveSegmentLength)), 8, 128)
			: 1;

		TArray<FVector> BasePolyline;
		BasePolyline.Reserve(SegmentCount + 1);
		for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float X = FMath::Lerp(-HalfLength, HalfLength, Alpha);
			BasePolyline.Add(TransformStraightWallLocalPointToCurve(FVector(X, SideY, 0.0f)));
		}

		SurfaceComponent->InitializeSurface(
			this,
			bLeftSide ? EEHBArchitecturalSurfaceRole::WallLeftSide : EEHBArchitecturalSurfaceRole::WallRightSide,
			bLeftSide ? FName(TEXT("Wall.Left")) : FName(TEXT("Wall.Right")),
			INDEX_NONE);
		SurfaceComponent->SetVerticalSurfaceData(
			BasePolyline,
			FMath::Max(1.0f, Height),
			0.0f,
			false,
			EHBWallUVWorldSize);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> TriangleMaterialIndices;
	TArray<TSoftObjectPtr<UMaterialInterface>> SourceMaterials;

	if (BuildSampledWallSurfaceMesh(
		true,
		LeftSurfaceStyle,
		Vertices,
		Triangles,
		Normals,
		UVs,
		TriangleMaterialIndices,
		SourceMaterials))
	{
		CachedLeftWallVertices = Vertices;
		CachedLeftWallTriangles = Triangles;
		ApplyCurveDeformationToMesh(Vertices, Triangles, Normals, UVs, &TriangleMaterialIndices);
		ApplyMeshToComponent(LeftWallMeshComponent, Vertices, Triangles, Normals, UVs, LeftSurfaceStyle.OverrideMaterial, TriangleMaterialIndices, SourceMaterials);
		UpdateWallSurfaceData(LeftWallMeshComponent, true);
		return;
	}

	if (LeftSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::SampledMesh && LeftSurfaceStyle.SampledWallRow.DataTable)
	{
		// 采样墙面重放逻辑会在这里读取 LeftSurfaceStyle.SampledWallRow；当前先回退为简单墙面，保证创建流程可用。
	}

	BuildSimpleWallSurfaceMesh(true, Vertices, Triangles, Normals, UVs);
	CachedLeftWallVertices = Vertices;
	CachedLeftWallTriangles = Triangles;
	ApplyCurveDeformationToMesh(Vertices, Triangles, Normals, UVs);
	ApplyMeshToComponent(LeftWallMeshComponent, Vertices, Triangles, Normals, UVs, LeftSurfaceStyle.OverrideMaterial);
	UpdateWallSurfaceData(LeftWallMeshComponent, true);
}

void AEHB_Wall::RebuildRightWallMesh()
{
	FName OpeningStatus;
	if(!bRebuildingWallMesh&&!ValidateSurfaceOpeningBindings(CutOperations,OpeningStatus))return;
	const auto UpdateWallSurfaceData = [this](UEHBGeneratedMeshComponent* TargetComponent, bool bLeftSide)
	{
		UEHBVerticalSurfaceComponent* SurfaceComponent = Cast<UEHBVerticalSurfaceComponent>(TargetComponent);
		if (!SurfaceComponent)
		{
			return;
		}

		const float WallLength = GetWallLength();
		if (WallLength <= UE_SMALL_NUMBER)
		{
			return;
		}

		const float HalfLength = WallLength * 0.5f;
		const float SideSign = bLeftSide ? 1.0f : -1.0f;
		const float SideY = SideSign * FMath::Max(1.0f, Thickness) * 0.5f;
		const int32 SegmentCount = IsCurveDeformationEnabled()
			? FMath::Clamp(FMath::CeilToInt(WallLength / FMath::Max(10.0f, CurveSegmentLength)), 8, 128)
			: 1;

		TArray<FVector> BasePolyline;
		BasePolyline.Reserve(SegmentCount + 1);
		for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float X = FMath::Lerp(-HalfLength, HalfLength, Alpha);
			BasePolyline.Add(TransformStraightWallLocalPointToCurve(FVector(X, SideY, 0.0f)));
		}

		SurfaceComponent->InitializeSurface(
			this,
			bLeftSide ? EEHBArchitecturalSurfaceRole::WallLeftSide : EEHBArchitecturalSurfaceRole::WallRightSide,
			bLeftSide ? FName(TEXT("Wall.Left")) : FName(TEXT("Wall.Right")),
			INDEX_NONE);
		SurfaceComponent->SetVerticalSurfaceData(
			BasePolyline,
			FMath::Max(1.0f, Height),
			0.0f,
			false,
			EHBWallUVWorldSize);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> TriangleMaterialIndices;
	TArray<TSoftObjectPtr<UMaterialInterface>> SourceMaterials;

	if (BuildSampledWallSurfaceMesh(
		false,
		RightSurfaceStyle,
		Vertices,
		Triangles,
		Normals,
		UVs,
		TriangleMaterialIndices,
		SourceMaterials))
	{
		CachedRightWallVertices = Vertices;
		CachedRightWallTriangles = Triangles;
		ApplyCurveDeformationToMesh(Vertices, Triangles, Normals, UVs, &TriangleMaterialIndices);
		ApplyMeshToComponent(RightWallMeshComponent, Vertices, Triangles, Normals, UVs, RightSurfaceStyle.OverrideMaterial, TriangleMaterialIndices, SourceMaterials);
		UpdateWallSurfaceData(RightWallMeshComponent, false);
		return;
	}

	if (RightSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::SampledMesh && RightSurfaceStyle.SampledWallRow.DataTable)
	{
		// 采样墙面重放逻辑会在这里读取 RightSurfaceStyle.SampledWallRow；当前先回退为简单墙面，保证创建流程可用。
	}

	BuildSimpleWallSurfaceMesh(false, Vertices, Triangles, Normals, UVs);
	CachedRightWallVertices = Vertices;
	CachedRightWallTriangles = Triangles;
	ApplyCurveDeformationToMesh(Vertices, Triangles, Normals, UVs);
	ApplyMeshToComponent(RightWallMeshComponent, Vertices, Triangles, Normals, UVs, RightSurfaceStyle.OverrideMaterial);
	UpdateWallSurfaceData(RightWallMeshComponent, false);
}

void AEHB_Wall::RebuildWallCapMesh()
{
	FName OpeningStatus;
	if(!bRebuildingWallMesh&&!ValidateSurfaceOpeningBindings(CutOperations,OpeningStatus))return;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	bool CapsReady=false;BuildSimpleWallCapMesh(Vertices, Triangles, Normals, UVs,nullptr,&CapsReady);if(!CapsReady)return;
	BuildOpeningRevealCapMesh(Vertices, Triangles, Normals, UVs);
	ApplyCurveDeformationToMesh(Vertices, Triangles, Normals, UVs);
	TSoftObjectPtr<UMaterialInterface> CapMaterial = CapOverrideMaterial;
	if (CapMaterial.IsNull())
	{
		CapMaterial = LeftSurfaceStyle.OverrideMaterial;
	}
	if (CapMaterial.IsNull())
	{
		CapMaterial = RightSurfaceStyle.OverrideMaterial;
	}
	if (CapMaterial.IsNull())
	{
		CapMaterial = GetConfiguredDefaultWhiteBoxMaterial();
	}
	ApplyMeshToComponent(CapMeshComponent, Vertices, Triangles, Normals, UVs, CapMaterial);
}

float AEHB_Wall::GetWallLength() const
{
	return FVector::Dist2D(LocalStart, LocalEnd);
}

FTransform AEHB_Wall::MakeWallLocalTransform() const
{
	const FVector Direction = (LocalEnd - LocalStart).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return FTransform(FRotator::ZeroRotator, LocalStart);
	}

	const FVector Center = FMath::Lerp(LocalStart, LocalEnd, 0.5f);
	return FTransform(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f), Center);
}

AEHB_Pillar* AEHB_Wall::FindPillarByGuid(const FGuid& PillarGuid) const
{
	if (!PillarGuid.IsValid() || !OwningBuilding)
	{
		return nullptr;
	}

	TArray<AActor*> AttachedActors;
	OwningBuilding->GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(AttachedActor);
		if (Pillar && Pillar->ElementGuid == PillarGuid)
		{
			return Pillar;
		}
	}

	return nullptr;
}

void AEHB_Wall::RebuildConnectedPillarMeshes() const
{
	AEHB_Pillar* StartPillar = FindPillarByGuid(StartPillarGuid);
	AEHB_Pillar* EndPillar = FindPillarByGuid(EndPillarGuid);

	if (StartPillar)
	{
		StartPillar->RebuildPillarMesh();
	}

	if (EndPillar && EndPillar != StartPillar)
	{
		EndPillar->RebuildPillarMesh();
	}
}

AEHB_DoorWindow* AEHB_Wall::FindDoorWindowByGuid(const FGuid& DoorWindowGuid) const
{
	if (!DoorWindowGuid.IsValid())
	{
		return nullptr;
	}

	if (OwningBuilding)
	{
		if (AEHB_DoorWindow* IndexedDoorWindow =
			Cast<AEHB_DoorWindow>(OwningBuilding->FindElementActorByGuid(DoorWindowGuid)))
		{
			return IndexedDoorWindow;
		}

		TArray<AActor*> AttachedActors;
		OwningBuilding->GetAttachedActors(AttachedActors);
		for (AActor* AttachedActor : AttachedActors)
		{
			AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(AttachedActor);
			if (DoorWindow && DoorWindow->ElementGuid == DoorWindowGuid)
			{
				OwningBuilding->RegisterElementActor(DoorWindow);
				return DoorWindow;
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AEHB_DoorWindow> It(World); It; ++It)
	{
		AEHB_DoorWindow* DoorWindow = *It;
		if (!DoorWindow
			|| DoorWindow->IsActorBeingDestroyed()
			|| DoorWindow->ElementGuid != DoorWindowGuid)
		{
			continue;
		}

		if (OwningBuilding
			&& DoorWindow->OwningBuilding != OwningBuilding
			&& DoorWindow->GetOwner() != OwningBuilding
			&& DoorWindow->GetAttachParentActor() != OwningBuilding)
		{
			continue;
		}

		if (OwningBuilding && DoorWindow->OwningBuilding == OwningBuilding)
		{
			OwningBuilding->RegisterElementActor(DoorWindow);
		}
		return DoorWindow;
	}

	return nullptr;
}

bool AEHB_Wall::MakeDoorWindowWorldTransform(float DistanceFromStart, float BottomHeight, const FVector& WorldScale, FTransform& OutWorldTransform) const
{
	const float Length = GetWallLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float ClampedDistance = FMath::Clamp(DistanceFromStart, 0.0f, Length);
	const FVector WallLocalPoint(
		-Length * 0.5f + ClampedDistance,
		0.0f,
		FMath::Max(0.0f, BottomHeight));

	const FVector CurvedLocalPoint = TransformStraightWallLocalPointToCurve(WallLocalPoint);
	const FVector LocalForward =
		TransformStraightWallLocalVectorToCurve(WallLocalPoint, FVector::ForwardVector);
	const FVector LocalUp =
		TransformStraightWallLocalVectorToCurve(WallLocalPoint, FVector::UpVector);

	const FTransform WallTransform = GetActorTransform();
	const FVector WorldLocation = WallTransform.TransformPosition(CurvedLocalPoint);
	const FVector WorldForward =
		WallTransform.TransformVectorNoScale(LocalForward).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector WorldUp =
		WallTransform.TransformVectorNoScale(LocalUp).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

	OutWorldTransform = FTransform(
		FRotationMatrix::MakeFromXZ(WorldForward, WorldUp).ToQuat(),
		WorldLocation,
		WorldScale);
	return true;
}

void AEHB_Wall::RefreshConnectedDoorWindowTransforms()
{
	if (DoorWindowConnections.IsEmpty() || GetWallLength() <= UE_SMALL_NUMBER)
	{
		return;
	}

	EnsureElementGuid();

	bool bConnectionsChanged = false;
	for (int32 ConnectionIndex = DoorWindowConnections.Num() - 1; ConnectionIndex >= 0; --ConnectionIndex)
	{
		FEHBWallDoorWindowConnection& Connection = DoorWindowConnections[ConnectionIndex];
		if (!Connection.DoorWindowGuid.IsValid())
		{
			Modify();
			RemoveDoorWindowCutOperationByGuid(Connection.DoorWindowGuid);
			DoorWindowConnections.RemoveAt(ConnectionIndex);
			bConnectionsChanged = true;
			continue;
		}

		AEHB_DoorWindow* DoorWindow = FindDoorWindowByGuid(Connection.DoorWindowGuid);
		if (!DoorWindow)
		{
			// The serialized connection is still a valid opening source. Actor lookup can fail
			// during load, undo/redo, or editor reconstruction; deletion paths explicitly remove it.
			SyncDoorWindowCutOperation(Connection, nullptr);
			continue;
		}

		const float ClampedDistance = FMath::Clamp(Connection.DistanceFromStart, 0.0f, GetWallLength());
		FTransform TargetWorldTransform = FTransform::Identity;
		if (!MakeDoorWindowWorldTransform(
			ClampedDistance,
			DoorWindow->GetOpeningBottomHeight(),
			DoorWindow->GetActorScale3D(),
			TargetWorldTransform))
		{
			continue;
		}

		if (!DoorWindow->GetActorTransform().Equals(TargetWorldTransform)
			|| DoorWindow->OwningWallGuid != ElementGuid
			|| !FMath::IsNearlyEqual(DoorWindow->DistanceFromWallStart, ClampedDistance))
		{
			DoorWindow->ApplyWallDrivenTransform(this, TargetWorldTransform, ClampedDistance);
		}

		const FEHBWallDoorWindowConnection UpdatedConnection =
			MakeDoorWindowConnection(DoorWindow, ClampedDistance);
		if (!AreDoorWindowConnectionsEquivalent(Connection, UpdatedConnection))
		{
			Modify();
			Connection = UpdatedConnection;
			SyncDoorWindowCutOperation(Connection, DoorWindow);
			bConnectionsChanged = true;
		}
		else
		{
			SyncDoorWindowCutOperation(Connection, DoorWindow);
		}
	}

	if (bConnectionsChanged)
	{
		MarkPackageDirty();
	}
}

void AEHB_Wall::DeleteConnectedDoorWindows()
{
	if (DoorWindowConnections.Num() <= 0)
	{
		return;
	}

	TArray<FGuid> DoorWindowGuidsToDelete;
	DoorWindowGuidsToDelete.Reserve(DoorWindowConnections.Num());
	for (const FEHBWallDoorWindowConnection& Connection : DoorWindowConnections)
	{
		if (Connection.DoorWindowGuid.IsValid())
		{
			DoorWindowGuidsToDelete.AddUnique(Connection.DoorWindowGuid);
		}
	}

	for (const FGuid& DoorWindowGuid : DoorWindowGuidsToDelete)
	{
		AEHB_DoorWindow* DoorWindow = FindDoorWindowByGuid(DoorWindowGuid);
		if (!DoorWindow)
		{
			continue;
		}

		DoorWindow->Modify();
		DoorWindow->ClearWallBinding();
		DoorWindow->Destroy();
	}

	Modify();
	RemoveAllDoorWindowCutOperations();
	DoorWindowConnections.Reset();
	MarkPackageDirty();
}

FEHBWallDoorWindowConnection AEHB_Wall::MakeDoorWindowConnection(
	AEHB_DoorWindow* DoorWindow,
	float DistanceFromStart) const
{
	FEHBWallDoorWindowConnection Connection;
	if (!DoorWindow)
	{
		return Connection;
	}

	const FEHBDoorWindowOpeningData OpeningData = DoorWindow->MakeOpeningData();
	Connection.DoorWindowGuid = DoorWindow->ElementGuid;
	Connection.Kind = OpeningData.Kind;
	Connection.DistanceFromStart =
		FMath::Clamp(DistanceFromStart, 0.0f, GetWallLength());
	Connection.BottomHeight = Connection.Kind == EEHBDoorWindowElementKind::Door
		? 0.0f
		: OpeningData.BottomHeight;
	Connection.OpeningWidth = OpeningData.Width;
	Connection.OpeningHeight = OpeningData.Height;
	Connection.OpeningThickness = OpeningData.Thickness;
	Connection.DoorWindowLocalToWall =
		DoorWindow->GetActorTransform().GetRelativeTransform(GetActorTransform());
	Connection.LocalOutlinePoints = OpeningData.LocalOutlinePoints;
	return Connection;
}

bool AEHB_Wall::BuildConnectionOpeningPolygon(
	const FEHBWallResolvedGeometry& Geometry,
	const FEHBWallDoorWindowConnection& Connection,
	TArray<FVector2d>& OutOpeningPolygon) const
{
	OutOpeningPolygon.Reset();

	TArray<FVector> LocalOutlinePoints = Connection.LocalOutlinePoints;
	if (LocalOutlinePoints.Num() < 3)
	{
		const float HalfOpeningWidth = FMath::Max(1.0f, Connection.OpeningWidth) * 0.5f;
		const float OpeningHeight = FMath::Max(1.0f, Connection.OpeningHeight);
		LocalOutlinePoints = {
			FVector(-HalfOpeningWidth, 0.0f, 0.0f),
			FVector(-HalfOpeningWidth, 0.0f, OpeningHeight),
			FVector(HalfOpeningWidth, 0.0f, OpeningHeight),
			FVector(HalfOpeningWidth, 0.0f, 0.0f)
		};
	}

	const double ReferenceStartX = -static_cast<double>(Geometry.ReferenceLength) * 0.5;
	const double OpeningOriginX =
		ReferenceStartX + FMath::Clamp(
			static_cast<double>(Connection.DistanceFromStart),
			0.0,
			static_cast<double>(Geometry.ReferenceLength));

	const bool bDoorOpening = Connection.Kind == EEHBDoorWindowElementKind::Door;
	double DoorLocalBaseZ = 0.0;
	if (bDoorOpening)
	{
		DoorLocalBaseZ = TNumericLimits<double>::Max();
		for (const FVector& LocalOutlinePoint : LocalOutlinePoints)
		{
			const FVector WallSpaceOffset =
				Connection.DoorWindowLocalToWall.TransformVector(LocalOutlinePoint);
			DoorLocalBaseZ = FMath::Min(DoorLocalBaseZ, static_cast<double>(WallSpaceOffset.Z));
		}

		if (DoorLocalBaseZ == TNumericLimits<double>::Max())
		{
			DoorLocalBaseZ = 0.0;
		}
	}

	OutOpeningPolygon.Reserve(LocalOutlinePoints.Num());
	for (const FVector& LocalOutlinePoint : LocalOutlinePoints)
	{
		const FVector WallSpaceOffset =
			Connection.DoorWindowLocalToWall.TransformVector(LocalOutlinePoint);

		double WallZ = static_cast<double>(
			(bDoorOpening ? 0.0f : Connection.BottomHeight)
			+ WallSpaceOffset.Z)
			- (bDoorOpening ? DoorLocalBaseZ : 0.0);
		if (WallZ >= -EHBWallOpeningPointTolerance
			&& WallZ <= EHBWallOpeningPointTolerance)
		{
			WallZ = -EHBWallOpeningBoundaryExtension;
		}

		OutOpeningPolygon.Add(FVector2d(
			OpeningOriginX + static_cast<double>(WallSpaceOffset.X),
			WallZ));
	}

	return SanitizeOpeningPolygon(OutOpeningPolygon);
}

bool AEHB_Wall::BuildCutOperationOpeningPolygon(
	const FEHBCutOperation& Operation,
	TArray<FVector2d>& OutOpeningPolygon) const
{
	OutOpeningPolygon.Reset();
	if(Operation.SurfaceHost.Version!=0){FName Status;return ResolveSurfaceBoundOpening(Operation,OutOpeningPolygon,Status);}
	if (!Operation.bEnabled
		|| Operation.OperationType != EEHBCutOperationType::Subtract
		|| Operation.Stage != EEHBCutStage::SurfaceOpening
		|| Operation.ProjectionMode != EEHBCutProjectionMode::VerticalXZ)
	{
		return false;
	}

	const TArray<FEHBCutPolygonPoint>& Points = Operation.Source.ExplicitPolygon.Points;
	if (Points.Num() < 3)
	{
		return false;
	}

	OutOpeningPolygon.Reserve(Points.Num());
	for (const FEHBCutPolygonPoint& Point : Points)
	{
		const FVector LocalPoint = Operation.Source.LocalTransform.TransformPosition(Point.LocalPosition);
		double WallZ = static_cast<double>(LocalPoint.Z);
		if (WallZ >= -EHBWallOpeningPointTolerance
			&& WallZ <= EHBWallOpeningPointTolerance)
		{
			WallZ = -EHBWallOpeningBoundaryExtension;
		}
		OutOpeningPolygon.Add(FVector2d(LocalPoint.X, WallZ));
	}

	return SanitizeOpeningPolygon(OutOpeningPolygon);
}

void AEHB_Wall::SyncDoorWindowCutOperation(
	const FEHBWallDoorWindowConnection& Connection,
	AEHB_DoorWindow* DoorWindow)
{
	if (!Connection.DoorWindowGuid.IsValid())
	{
		return;
	}

	FEHBWallResolvedGeometry Geometry;
	TArray<FVector2d> OpeningPolygon;
	if (!BuildResolvedWallGeometry(Geometry)
		|| !BuildConnectionOpeningPolygon(Geometry, Connection, OpeningPolygon))
	{
		RemoveDoorWindowCutOperationByGuid(Connection.DoorWindowGuid);
		return;
	}

	FEHBCutOperation Operation;
	Operation.bEnabled = true;
	Operation.OperationType = EEHBCutOperationType::Subtract;
	Operation.Stage = EEHBCutStage::SurfaceOpening;
	Operation.ProjectionMode = EEHBCutProjectionMode::VerticalXZ;
	Operation.TransformPolicy = EEHBCutTransformPolicy::SourceActorDriven;
	Operation.Priority = 100;
	Operation.Source.SourceType = EEHBCutSourceType::Element;
	Operation.Source.SourceElementGuid = Connection.DoorWindowGuid;
	Operation.Source.SourceElement = DoorWindow;
	Operation.Source.LocalTransform = FTransform::Identity;
	Operation.Source.PrimitiveShape = EEHBCutPrimitiveShape::Polygon;
	Operation.Source.ExplicitPolygon.Points.Reserve(OpeningPolygon.Num());
	for (const FVector2d& Point : OpeningPolygon)
	{
		FEHBCutPolygonPoint CutPoint;
		CutPoint.LocalPosition = FVector(Point.X, 0.0, Point.Y);
		Operation.Source.ExplicitPolygon.Points.Add(MoveTemp(CutPoint));
	}

	FEHBCutOperation* ExistingOperation = CutOperations.FindByPredicate(
		[&Connection](const FEHBCutOperation& Existing)
		{
			return Existing.Source.SourceElementGuid == Connection.DoorWindowGuid
				&& Existing.OperationType == EEHBCutOperationType::Subtract
				&& Existing.Stage == EEHBCutStage::SurfaceOpening
				&& Existing.ProjectionMode == EEHBCutProjectionMode::VerticalXZ;
		});

	if (ExistingOperation)
	{
		Operation.OperationGuid = ExistingOperation->OperationGuid;
		for (int32 PointIndex = 0;
			PointIndex < Operation.Source.ExplicitPolygon.Points.Num()
			&& PointIndex < ExistingOperation->Source.ExplicitPolygon.Points.Num();
			++PointIndex)
		{
			Operation.Source.ExplicitPolygon.Points[PointIndex].PointGuid =
				ExistingOperation->Source.ExplicitPolygon.Points[PointIndex].PointGuid;
		}
	}

	Operation.EnsureGuids();
	if (ExistingOperation)
	{
		*ExistingOperation = MoveTemp(Operation);
	}
	else
	{
		CutOperations.Add(MoveTemp(Operation));
	}
	MarkPackageDirty();
}

void AEHB_Wall::RemoveDoorWindowCutOperationByGuid(const FGuid& DoorWindowGuid)
{
	if (!DoorWindowGuid.IsValid())
	{
		return;
	}

	const int32 RemovedCount = CutOperations.RemoveAll(
		[&DoorWindowGuid](const FEHBCutOperation& Operation)
		{
			return Operation.Source.SourceElementGuid == DoorWindowGuid
				&& Operation.OperationType == EEHBCutOperationType::Subtract
				&& Operation.Stage == EEHBCutStage::SurfaceOpening
				&& Operation.ProjectionMode == EEHBCutProjectionMode::VerticalXZ;
		});

	if (RemovedCount > 0)
	{
		MarkPackageDirty();
	}
}

void AEHB_Wall::RemoveAllDoorWindowCutOperations()
{
	TSet<FGuid> DoorWindowGuids;
	for (const FEHBWallDoorWindowConnection& Connection : DoorWindowConnections)
	{
		if (Connection.DoorWindowGuid.IsValid())
		{
			DoorWindowGuids.Add(Connection.DoorWindowGuid);
		}
	}

	const int32 RemovedCount = CutOperations.RemoveAll(
		[&DoorWindowGuids](const FEHBCutOperation& Operation)
		{
			return DoorWindowGuids.Contains(Operation.Source.SourceElementGuid)
				&& Operation.OperationType == EEHBCutOperationType::Subtract
				&& Operation.Stage == EEHBCutStage::SurfaceOpening
				&& Operation.ProjectionMode == EEHBCutProjectionMode::VerticalXZ;
		});

	if (RemovedCount > 0)
	{
		MarkPackageDirty();
	}
}

bool AEHB_Wall::BuildResolvedWallGeometry(FEHBWallResolvedGeometry& OutGeometry) const
{
	const float Length = GetWallLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeThickness = FMath::Max(1.0f, Thickness);
	const float HalfLength = Length * 0.5f;
	const float HalfThickness = SafeThickness * 0.5f;

	OutGeometry.ReferenceLength = Length;
	OutGeometry.StartLeftX = -HalfLength;
	OutGeometry.StartRightX = -HalfLength;
	OutGeometry.EndLeftX = HalfLength;
	OutGeometry.EndRightX = HalfLength;
	const bool bCurveDeformationEnabled = IsCurveDeformationEnabled();
	bool bUsedCachedStartFace = !bCurveDeformationEnabled && ApplyCachedPillarConnectionFaceToGeometry(true, OutGeometry);
	bool bUsedCachedEndFace = !bCurveDeformationEnabled && ApplyCachedPillarConnectionFaceToGeometry(false, OutGeometry);
 if(!bCurveDeformationEnabled&&!bUsedCachedStartFace&&!bUsedCachedEndFace&&OwningBuilding&&OwningBuilding->WallNodeOwnership.Version==1)
 {
  FEHBWallNodeModel Source;
  if(UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(OwningBuilding,Source).bSucceeded)
  {
   const auto* D=Source.Walls.FindByPredicate([this](const auto& W){return W.WallGuid==ElementGuid;});
   TSet<FGuid> Requested{ElementGuid};TArray<FEHBWallJunctionWallSides> Sides;FName Status;
   if(D&&CanApplyNodeDefinition(*D,true)&&UEHBWallTopologyLibrary::BuildWallNodeModelSides(Source,Sides,Status,&Requested)&&Sides.Num()==1)
   {
    const auto& Side=Sides[0];const auto Transform=GetElementLocalTransform();
    if(Transform.Equals(Side.LocalTransform,0.0001)&&LocalStart.Equals(Side.LocalStart,0.0001)&&LocalEnd.Equals(Side.LocalEnd,0.0001))
    {
     OutGeometry.StartLeftX=Transform.InverseTransformPosition(Side.StartLeft).X;OutGeometry.StartRightX=Transform.InverseTransformPosition(Side.StartRight).X;
     OutGeometry.EndLeftX=Transform.InverseTransformPosition(Side.EndLeft).X;OutGeometry.EndRightX=Transform.InverseTransformPosition(Side.EndRight).X;
     bUsedCachedStartFace=bUsedCachedEndFace=true;
    }
   }
  }
 }


	auto FindPillarTangentX = [](const AEHB_Pillar* Pillar, const TArray<FVector2D>& PillarCorners, bool bStartPillar)
	{
		FVector2D Center = FVector2D::ZeroVector;
		for (const FVector2D& Corner : PillarCorners)
		{
			Center += Corner;
		}
		Center /= FMath::Max(1, PillarCorners.Num());

		float SupportRadiusX = 0.0f;
		for (const FVector2D& Corner : PillarCorners)
		{
			SupportRadiusX = FMath::Max(SupportRadiusX, FMath::Abs(Corner.X - Center.X));
		}

		if (Pillar)
		{
			const float ShapeRadius = Pillar->ShapeType == EEHBPillarShapeType::Cylinder
				? FMath::Max(1.0f, Pillar->Radius)
				: FMath::Max(Pillar->Width, Pillar->Depth) * 0.5f;
			SupportRadiusX = FMath::Max(SupportRadiusX, ShapeRadius);
		}

		return Center.X + (bStartPillar ? SupportRadiusX : -SupportRadiusX);
	};

	auto FindCurvePillarIntersectionX =
		[this, HalfLength, Length](
			const TArray<FVector2D>& PillarCorners,
			float WallY,
			bool bStartPillar,
			float& OutX)
	{
		if (PillarCorners.Num() < 3)
		{
			return false;
		}

		auto Cross = [](const FVector2D& A, const FVector2D& B)
		{
			return A.X * B.Y - A.Y * B.X;
		};

		auto AddCandidate = [bStartPillar, HalfLength, &OutX](float CandidateX, bool& bFound)
		{
			constexpr float MidTolerance = 1.0f;
			if (bStartPillar)
			{
				if (CandidateX > MidTolerance || CandidateX < -HalfLength - MidTolerance)
				{
					return;
				}

				if (!bFound || CandidateX > OutX)
				{
					OutX = CandidateX;
					bFound = true;
				}
			}
			else
			{
				if (CandidateX < -MidTolerance || CandidateX > HalfLength + MidTolerance)
				{
					return;
				}

				if (!bFound || CandidateX < OutX)
				{
					OutX = CandidateX;
					bFound = true;
				}
			}
		};

		auto IsPointOnSegment = [&Cross](const FVector2D& Point, const FVector2D& A, const FVector2D& B)
		{
			constexpr float Tolerance = 0.05f;
			if (FMath::Abs(Cross(B - A, Point - A)) > Tolerance)
			{
				return false;
			}

			return Point.X >= FMath::Min(A.X, B.X) - Tolerance
				&& Point.X <= FMath::Max(A.X, B.X) + Tolerance
				&& Point.Y >= FMath::Min(A.Y, B.Y) - Tolerance
				&& Point.Y <= FMath::Max(A.Y, B.Y) + Tolerance;
		};

		auto AlphaOnSegment = [](const FVector2D& Point, const FVector2D& A, const FVector2D& B)
		{
			const FVector2D Delta = B - A;
			if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
			{
				return FMath::IsNearlyZero(Delta.X) ? 0.0f : (Point.X - A.X) / Delta.X;
			}

			return FMath::IsNearlyZero(Delta.Y) ? 0.0f : (Point.Y - A.Y) / Delta.Y;
		};

		auto CurvePoint2D = [this, WallY](float X)
		{
			const FVector CurvePoint = TransformStraightWallLocalPointToCurve(FVector(X, WallY, 0.0f));
			return FVector2D(CurvePoint.X, CurvePoint.Y);
		};

		const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(Length / 4.0f), 48, 1024);
		bool bFound = false;
		float PreviousX = -HalfLength;
		FVector2D PreviousPoint = CurvePoint2D(PreviousX);

		for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float CurrentX = FMath::Lerp(-HalfLength, HalfLength, static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount));
			const FVector2D CurrentPoint = CurvePoint2D(CurrentX);
			const FVector2D PathDirection = CurrentPoint - PreviousPoint;

			for (int32 CornerIndex = 0; CornerIndex < PillarCorners.Num(); ++CornerIndex)
			{
				const FVector2D EdgeStart = PillarCorners[CornerIndex];
				const FVector2D EdgeEnd = PillarCorners[(CornerIndex + 1) % PillarCorners.Num()];
				const FVector2D EdgeDirection = EdgeEnd - EdgeStart;
				const float Denominator = Cross(PathDirection, EdgeDirection);

				if (FMath::Abs(Denominator) <= 0.0001f)
				{
					if (IsPointOnSegment(EdgeStart, PreviousPoint, CurrentPoint))
					{
						AddCandidate(FMath::Lerp(PreviousX, CurrentX, FMath::Clamp(AlphaOnSegment(EdgeStart, PreviousPoint, CurrentPoint), 0.0f, 1.0f)), bFound);
					}
					if (IsPointOnSegment(EdgeEnd, PreviousPoint, CurrentPoint))
					{
						AddCandidate(FMath::Lerp(PreviousX, CurrentX, FMath::Clamp(AlphaOnSegment(EdgeEnd, PreviousPoint, CurrentPoint), 0.0f, 1.0f)), bFound);
					}
					if (IsPointOnSegment(PreviousPoint, EdgeStart, EdgeEnd))
					{
						AddCandidate(PreviousX, bFound);
					}
					if (IsPointOnSegment(CurrentPoint, EdgeStart, EdgeEnd))
					{
						AddCandidate(CurrentX, bFound);
					}
					continue;
				}

				const float PathAlpha = Cross(EdgeStart - PreviousPoint, EdgeDirection) / Denominator;
				const float EdgeAlpha = Cross(EdgeStart - PreviousPoint, PathDirection) / Denominator;
				constexpr float Tolerance = 0.001f;
				if (PathAlpha >= -Tolerance && PathAlpha <= 1.0f + Tolerance
					&& EdgeAlpha >= -Tolerance && EdgeAlpha <= 1.0f + Tolerance)
				{
					AddCandidate(FMath::Lerp(PreviousX, CurrentX, FMath::Clamp(PathAlpha, 0.0f, 1.0f)), bFound);
				}
			}

			PreviousX = CurrentX;
			PreviousPoint = CurrentPoint;
		}

		return bFound;
	};

	TArray<FVector2D> StartPillarCorners;
	AEHB_Pillar* StartPillar = bUsedCachedStartFace ? nullptr : FindPillarByGuid(StartPillarGuid);
	if (!bUsedCachedStartFace && GetPillarCornersInWallLocal(StartPillar, StartPillarCorners))
	{
		if (bCurveDeformationEnabled)
		{
			if (!FindCurvePillarIntersectionX(StartPillarCorners, HalfThickness, true, OutGeometry.StartLeftX))
			{
				OutGeometry.StartLeftX = FindPillarTangentX(StartPillar, StartPillarCorners, true);
			}
			if (!FindCurvePillarIntersectionX(StartPillarCorners, -HalfThickness, true, OutGeometry.StartRightX))
			{
				OutGeometry.StartRightX = FindPillarTangentX(StartPillar, StartPillarCorners, true);
			}
		}
		else
		{
			FindPillarIntersectionXAtWallY(StartPillarCorners, HalfThickness, true, OutGeometry.StartLeftX);
			FindPillarIntersectionXAtWallY(StartPillarCorners, -HalfThickness, true, OutGeometry.StartRightX);
		}
	}

	TArray<FVector2D> EndPillarCorners;
	AEHB_Pillar* EndPillar = bUsedCachedEndFace ? nullptr : FindPillarByGuid(EndPillarGuid);
	if (!bUsedCachedEndFace && GetPillarCornersInWallLocal(EndPillar, EndPillarCorners))
	{
		if (bCurveDeformationEnabled)
		{
			if (!FindCurvePillarIntersectionX(EndPillarCorners, HalfThickness, false, OutGeometry.EndLeftX))
			{
				OutGeometry.EndLeftX = FindPillarTangentX(EndPillar, EndPillarCorners, false);
			}
			if (!FindCurvePillarIntersectionX(EndPillarCorners, -HalfThickness, false, OutGeometry.EndRightX))
			{
				OutGeometry.EndRightX = FindPillarTangentX(EndPillar, EndPillarCorners, false);
			}
		}
		else
		{
			FindPillarIntersectionXAtWallY(EndPillarCorners, HalfThickness, false, OutGeometry.EndLeftX);
			FindPillarIntersectionXAtWallY(EndPillarCorners, -HalfThickness, false, OutGeometry.EndRightX);
		}
	}

	constexpr float MinSideLength = 1.0f;
	return OutGeometry.EndLeftX - OutGeometry.StartLeftX > MinSideLength
		&& OutGeometry.EndRightX - OutGeometry.StartRightX > MinSideLength;
}

bool AEHB_Wall::ApplyCachedPillarConnectionFaceToGeometry(bool bStartPillar, FEHBWallResolvedGeometry& OutGeometry) const
{
	const bool bHasCachedFace = bStartPillar ? bHasStartPillarConnectionFace : bHasEndPillarConnectionFace;
	if (!bHasCachedFace)
	{
		return false;
	}

	const FVector LeftLocalPoint = bStartPillar ? StartPillarConnectionLeftLocal : EndPillarConnectionLeftLocal;
	const FVector RightLocalPoint = bStartPillar ? StartPillarConnectionRightLocal : EndPillarConnectionRightLocal;
	if (LeftLocalPoint.ContainsNaN() || RightLocalPoint.ContainsNaN())
	{
		return false;
	}

	const FTransform WallLocalTransform = GetElementLocalTransform();
	const FVector FirstWallLocalPoint = WallLocalTransform.InverseTransformPosition(LeftLocalPoint);
	const FVector SecondWallLocalPoint = WallLocalTransform.InverseTransformPosition(RightLocalPoint);

	if (IsCurveDeformationEnabled())
	{
		const float HalfThickness = FMath::Max(1.0f, Thickness) * 0.5f;
		float FirstLeftX = 0.0f;
		float FirstLeftDistanceSquared = 0.0f;
		float FirstRightX = 0.0f;
		float FirstRightDistanceSquared = 0.0f;
		float SecondLeftX = 0.0f;
		float SecondLeftDistanceSquared = 0.0f;
		float SecondRightX = 0.0f;
		float SecondRightDistanceSquared = 0.0f;

		const bool bProjectedAll =
			ProjectCachedPillarFacePointToCurveSide(FirstWallLocalPoint, HalfThickness, bStartPillar, FirstLeftX, FirstLeftDistanceSquared)
			&& ProjectCachedPillarFacePointToCurveSide(FirstWallLocalPoint, -HalfThickness, bStartPillar, FirstRightX, FirstRightDistanceSquared)
			&& ProjectCachedPillarFacePointToCurveSide(SecondWallLocalPoint, HalfThickness, bStartPillar, SecondLeftX, SecondLeftDistanceSquared)
			&& ProjectCachedPillarFacePointToCurveSide(SecondWallLocalPoint, -HalfThickness, bStartPillar, SecondRightX, SecondRightDistanceSquared);

		if (bProjectedAll)
		{
			const float FirstAsLeftCost = FirstLeftDistanceSquared + SecondRightDistanceSquared;
			const float SecondAsLeftCost = SecondLeftDistanceSquared + FirstRightDistanceSquared;
			const float LeftX = FirstAsLeftCost <= SecondAsLeftCost ? FirstLeftX : SecondLeftX;
			const float RightX = FirstAsLeftCost <= SecondAsLeftCost ? SecondRightX : FirstRightX;

			if (bStartPillar)
			{
				OutGeometry.StartLeftX = LeftX;
				OutGeometry.StartRightX = RightX;
			}
			else
			{
				OutGeometry.EndLeftX = LeftX;
				OutGeometry.EndRightX = RightX;
			}
			return true;
		}
	}

	const FVector& LeftSidePoint = FirstWallLocalPoint.Y >= SecondWallLocalPoint.Y
		? FirstWallLocalPoint
		: SecondWallLocalPoint;
	const FVector& RightSidePoint = FirstWallLocalPoint.Y >= SecondWallLocalPoint.Y
		? SecondWallLocalPoint
		: FirstWallLocalPoint;

	if (bStartPillar)
	{
		OutGeometry.StartLeftX = LeftSidePoint.X;
		OutGeometry.StartRightX = RightSidePoint.X;
	}
	else
	{
		OutGeometry.EndLeftX = LeftSidePoint.X;
		OutGeometry.EndRightX = RightSidePoint.X;
	}

	return true;
}

bool AEHB_Wall::ProjectCachedPillarFacePointToCurveSide(const FVector& WallLocalPoint, float WallY, bool bStartPillar, float& OutX, float& OutDistanceSquared) const
{
	const float Length = GetWallLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float HalfLength = Length * 0.5f;
	float MinX = bStartPillar ? -HalfLength : 0.0f;
	float MaxX = bStartPillar ? 0.0f : HalfLength;
	if (MaxX - MinX <= UE_SMALL_NUMBER)
	{
		MinX = -HalfLength;
		MaxX = HalfLength;
	}

	auto DistanceSquaredAtX = [this, &WallLocalPoint, WallY](float X)
	{
		const FVector CurvePoint = TransformStraightWallLocalPointToCurve(FVector(X, WallY, WallLocalPoint.Z));
		return FVector::DistSquared2D(CurvePoint, WallLocalPoint);
	};

	float A = MinX;
	float B = MaxX;
	for (int32 Iteration = 0; Iteration < 48; ++Iteration)
	{
		const float FirstThird = A + (B - A) / 3.0f;
		const float SecondThird = B - (B - A) / 3.0f;
		if (DistanceSquaredAtX(FirstThird) < DistanceSquaredAtX(SecondThird))
		{
			B = SecondThird;
		}
		else
		{
			A = FirstThird;
		}
	}

	OutX = (A + B) * 0.5f;
	OutDistanceSquared = DistanceSquaredAtX(OutX);
	return true;
}

bool AEHB_Wall::GetPillarCornersInWallLocal(const AEHB_Pillar* Pillar, TArray<FVector2D>& OutCorners) const
{
	OutCorners.Reset();
	if (!Pillar)
	{
		return false;
	}

	const FTransform PillarToWallTransform = Pillar->GetElementLocalTransform() * GetElementLocalTransform().Inverse();

	TArray<FVector> LocalFootprint;
	Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
	for (const FVector& LocalPoint : LocalFootprint)
	{
		const FVector WallLocalCorner = PillarToWallTransform.TransformPosition(FVector(LocalPoint.X, LocalPoint.Y, 0.0f));
		OutCorners.Add(FVector2D(WallLocalCorner.X, WallLocalCorner.Y));
	}

	return OutCorners.Num() >= 3;
}

bool AEHB_Wall::FindPillarIntersectionXAtWallY(const TArray<FVector2D>& PillarCorners, float WallY, bool bUseMaxX, float& OutX) const
{
	if (PillarCorners.Num() < 3)
	{
		return false;
	}

	TArray<float> IntersectionXs;
	constexpr float Tolerance = 0.01f;
	for (int32 Index = 0; Index < PillarCorners.Num(); ++Index)
	{
		const FVector2D A = PillarCorners[Index];
		const FVector2D B = PillarCorners[(Index + 1) % PillarCorners.Num()];

		if (FMath::IsNearlyEqual(A.Y, B.Y, Tolerance))
		{
			if (FMath::IsNearlyEqual(WallY, A.Y, Tolerance))
			{
				IntersectionXs.Add(A.X);
				IntersectionXs.Add(B.X);
			}
			continue;
		}

		const float MinY = FMath::Min(A.Y, B.Y);
		const float MaxY = FMath::Max(A.Y, B.Y);
		if (WallY < MinY - Tolerance || WallY > MaxY + Tolerance)
		{
			continue;
		}

		const float Alpha = (WallY - A.Y) / (B.Y - A.Y);
		if (Alpha >= -Tolerance && Alpha <= 1.0f + Tolerance)
		{
			IntersectionXs.Add(FMath::Lerp(A.X, B.X, FMath::Clamp(Alpha, 0.0f, 1.0f)));
		}
	}

	if (IntersectionXs.Num() > 0)
	{
		OutX = IntersectionXs[0];
		for (float IntersectionX : IntersectionXs)
		{
			OutX = bUseMaxX ? FMath::Max(OutX, IntersectionX) : FMath::Min(OutX, IntersectionX);
		}
		return true;
	}

	// 当墙体侧边线刚好没有穿过柱子横截面时，退回到柱子在墙体方向上的外侧边界，保证墙体仍然不会退回到柱子中心。
	OutX = PillarCorners[0].X;
	for (const FVector2D& Corner : PillarCorners)
	{
		OutX = bUseMaxX ? FMath::Max(OutX, Corner.X) : FMath::Min(OutX, Corner.X);
	}
	return true;
}

void AEHB_Wall::BuildDoorWindowOpeningPolygons(const FEHBWallResolvedGeometry& Geometry, TArray<TArray<FVector2d>>& OutOpeningPolygons) const
{
	OutOpeningPolygons.Reset();
	OutOpeningPolygons.Reserve(CutOperations.Num() + DoorWindowConnections.Num() + (bHasPreviewDoorWindowConnection ? 1 : 0));

	TSet<FGuid> ResolvedDoorWindowCutSources;
	TArray<const FEHBCutOperation*> SortedOperations;
	SortedOperations.Reserve(CutOperations.Num());
	for (const FEHBCutOperation& Operation : CutOperations)
	{
		if (Operation.bEnabled && (Operation.SurfaceHost.Version!=0 ||
			(Operation.OperationType == EEHBCutOperationType::Subtract
			&& Operation.Stage == EEHBCutStage::SurfaceOpening
			&& Operation.ProjectionMode == EEHBCutProjectionMode::VerticalXZ)))
		{
			SortedOperations.Add(&Operation);
		}
	}
	SortedOperations.Sort(
		[](const FEHBCutOperation& Left, const FEHBCutOperation& Right)
		{
			return Left.Priority < Right.Priority;
		});

	for (const FEHBCutOperation* Operation : SortedOperations)
	{
		if (!Operation)
		{
			continue;
		}

		TArray<FVector2d> OpeningPolygon;
		if (BuildCutOperationOpeningPolygon(*Operation, OpeningPolygon))
		{
			if (Operation->Source.SourceElementGuid.IsValid())
			{
				ResolvedDoorWindowCutSources.Add(Operation->Source.SourceElementGuid);
			}
			OutOpeningPolygons.Add(MoveTemp(OpeningPolygon));
		}
	}

	for (const FEHBWallDoorWindowConnection& Connection : DoorWindowConnections)
	{
		if (Connection.DoorWindowGuid.IsValid()
			&& ResolvedDoorWindowCutSources.Contains(Connection.DoorWindowGuid))
		{
			continue;
		}

		TArray<FVector2d> OpeningPolygon;
		if (BuildConnectionOpeningPolygon(Geometry, Connection, OpeningPolygon))
		{
			OutOpeningPolygons.Add(MoveTemp(OpeningPolygon));
		}
	}

	if (bHasPreviewDoorWindowConnection)
	{
		TArray<FVector2d> OpeningPolygon;
		if (BuildConnectionOpeningPolygon(Geometry, PreviewDoorWindowConnection, OpeningPolygon))
		{
			OutOpeningPolygons.Add(MoveTemp(OpeningPolygon));
		}
	}
}

bool AEHB_Wall::GetResolvedSurfaceLength(bool bLeftSide, float& OutLength) const
{
	OutLength = 0.0f;

	FEHBWallResolvedGeometry Geometry;
	if (!BuildResolvedWallGeometry(Geometry))
	{
		return false;
	}

	const float StartX = bLeftSide ? Geometry.StartLeftX : Geometry.StartRightX;
	const float EndX = bLeftSide ? Geometry.EndLeftX : Geometry.EndRightX;
	OutLength = EndX - StartX;
	return OutLength > UE_SMALL_NUMBER;
}

float AEHB_Wall::CalculateSurfaceSamplePhaseAtStart(bool bLeftSide, const FEHBWallSurfaceStyle& SurfaceStyle) const
{
	TSet<FString> VisitedSurfaces;
	return CalculateSurfaceSamplePhaseAtStartInternal(bLeftSide, SurfaceStyle, VisitedSurfaces);
}

float AEHB_Wall::CalculateSurfaceSamplePhaseAtStartInternal(
	bool bLeftSide,
	const FEHBWallSurfaceStyle& SurfaceStyle,
	TSet<FString>& VisitedSurfaces) const
{
	if (!IsSampledWallSurfaceStyle(SurfaceStyle))
	{
		return 0.0f;
	}

	const float ManualPhaseOffset = GetWallSurfaceManualSamplePhaseOffset(SurfaceStyle);
	if (!OwningBuilding || !StartPillarGuid.IsValid())
	{
		return ManualPhaseOffset;
	}

	const FString StyleKey = MakeWallSurfaceStyleContinuityKey(SurfaceStyle);
	if (StyleKey.IsEmpty())
	{
		return ManualPhaseOffset;
	}

	const FString SurfaceVisitKey = FString::Printf(
		TEXT("%s|%s|%s"),
		*ElementGuid.ToString(EGuidFormats::DigitsWithHyphens),
		bLeftSide ? TEXT("Left") : TEXT("Right"),
		*StyleKey);
	if (VisitedSurfaces.Contains(SurfaceVisitKey))
	{
		return ManualPhaseOffset;
	}

	VisitedSurfaces.Add(SurfaceVisitKey);

	const AEHB_Pillar* StartPillar = FindPillarByGuid(StartPillarGuid);
	if (!StartPillar)
	{
		VisitedSurfaces.Remove(SurfaceVisitKey);
		return ManualPhaseOffset;
	}

	FVector CurrentSidePoint = FVector::ZeroVector;
	FVector CurrentTangent = FVector::ZeroVector;
	FVector CurrentNormal = FVector::ZeroVector;
	if (!GetWallSurfaceEndpointFrame(*this, bLeftSide, true, CurrentSidePoint, CurrentTangent, CurrentNormal))
	{
		VisitedSurfaces.Remove(SurfaceVisitKey);
		return ManualPhaseOffset;
	}

	bool bFoundPredecessor = false;
	float BestScore = -TNumericLimits<float>::Max();
	float BestPhase = 0.0f;

	auto TryConnectedSurface = [&](
		const AEHB_Wall& OtherWall,
		bool bOtherLeftSide,
		const FEHBWallSurfaceStyle& OtherStyle,
		bool bOtherConnectedAtStart)
	{
		if (!AreWallSurfaceStylesContinuousMatch(SurfaceStyle, OtherStyle))
		{
			return;
		}

		FVector OtherSidePoint = FVector::ZeroVector;
		FVector OtherTangent = FVector::ZeroVector;
		FVector OtherNormal = FVector::ZeroVector;
		if (!GetWallSurfaceEndpointFrame(
			OtherWall,
			bOtherLeftSide,
			bOtherConnectedAtStart,
			OtherSidePoint,
			OtherTangent,
			OtherNormal))
		{
			return;
		}

		const float NormalDot = FVector::DotProduct(CurrentNormal, OtherNormal);
		if (NormalDot < -0.25f)
		{
			return;
		}

		float OtherSurfaceLength = 0.0f;
		if (!OtherWall.GetResolvedSurfaceLength(bOtherLeftSide, OtherSurfaceLength))
		{
			return;
		}

		const float OtherStartPhase = OtherWall.CalculateSurfaceSamplePhaseAtStartInternal(
			bOtherLeftSide,
			OtherStyle,
			VisitedSurfaces);
		const float OtherEndpointPhase = OtherStartPhase + (bOtherConnectedAtStart ? 0.0f : OtherSurfaceLength);
		const float BridgeLength = FVector::Dist2D(CurrentSidePoint, OtherSidePoint);
		const float Score = NormalDot * 1000.0f - BridgeLength;
		if (!bFoundPredecessor || Score > BestScore)
		{
			bFoundPredecessor = true;
			BestScore = Score;
			BestPhase = OtherEndpointPhase + BridgeLength;
		}
	};

	for (const FGuid& ConnectedWallGuid : StartPillar->ConnectedWallGuids)
	{
		if (!ConnectedWallGuid.IsValid() || ConnectedWallGuid == ElementGuid)
		{
			continue;
		}

		const AEHB_Wall* OtherWall = Cast<AEHB_Wall>(OwningBuilding->FindElementActorByGuid(ConnectedWallGuid));
		if (!OtherWall)
		{
			continue;
		}

		const bool bOtherConnectedAtStart = OtherWall->StartPillarGuid == StartPillarGuid;
		const bool bOtherConnectedAtEnd = OtherWall->EndPillarGuid == StartPillarGuid;
		if (!bOtherConnectedAtStart && !bOtherConnectedAtEnd)
		{
			continue;
		}

		TryConnectedSurface(*OtherWall, true, OtherWall->LeftSurfaceStyle, bOtherConnectedAtStart);
		TryConnectedSurface(*OtherWall, false, OtherWall->RightSurfaceStyle, bOtherConnectedAtStart);
	}

	VisitedSurfaces.Remove(SurfaceVisitKey);
	return bFoundPredecessor ? BestPhase : ManualPhaseOffset;
}

bool AEHB_Wall::GetSurfaceSamplePhaseAtEndpoint(bool bLeftSide, bool bConnectedAtStart, float& OutPhase) const
{
	OutPhase = 0.0f;

	const FEHBWallSurfaceStyle& SurfaceStyle = bLeftSide ? LeftSurfaceStyle : RightSurfaceStyle;
	if (!IsSampledWallSurfaceStyle(SurfaceStyle))
	{
		return false;
	}

	float SurfaceLength = 0.0f;
	if (!GetResolvedSurfaceLength(bLeftSide, SurfaceLength))
	{
		return false;
	}

	OutPhase = CalculateSurfaceSamplePhaseAtStart(bLeftSide, SurfaceStyle)
		+ (bConnectedAtStart ? 0.0f : SurfaceLength);
	return true;
}

bool AEHB_Wall::BuildSampledWallSurfaceMesh(
	bool bLeftSide,
	const FEHBWallSurfaceStyle& SurfaceStyle,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs,
	TArray<int32>& OutTriangleMaterialIndices,
	TArray<TSoftObjectPtr<UMaterialInterface>>& OutMaterials) const
{
	OutVertices.Reset();
	OutTriangles.Reset();
	OutNormals.Reset();
	OutUVs.Reset();
	OutTriangleMaterialIndices.Reset();
	OutMaterials.Reset();

	if (SurfaceStyle.SourceType != EEHBWallSurfaceSourceType::SampledMesh
		|| !SurfaceStyle.SampledWallRow.DataTable
		|| SurfaceStyle.SampledWallRow.RowName.IsNone()
		|| SurfaceStyle.SampledWallRow.DataTable->GetRowStruct() != FEHBWallMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBWallMeshData* WallMeshData = SurfaceStyle.SampledWallRow.DataTable->FindRow<FEHBWallMeshData>(
		SurfaceStyle.SampledWallRow.RowName,
		TEXT("AEHB_Wall::BuildSampledWallSurfaceMesh"),
		false);
	if (!WallMeshData)
	{
		return false;
	}

	const FEHBWallMeshSampleSurface& SampleSurface =
		SurfaceStyle.SampleSide == EEHBWallMeshSampleSide::Front
			? WallMeshData->FrontSurface
			: WallMeshData->BackSurface;
	if (SampleSurface.Vertices.IsEmpty() || SampleSurface.Triangles.Num() < 3)
	{
		return false;
	}

	FEHBWallResolvedGeometry Geometry;
	if (!BuildResolvedWallGeometry(Geometry))
	{
		return false;
	}

	const float SafeHeight = FMath::Max(1.0f, Height);
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	const float HalfThickness = SafeThickness * 0.5f;
	const float TargetSideSign = bLeftSide ? 1.0f : -1.0f;
	const float TargetSideY = TargetSideSign * HalfThickness;
	const float StartX = bLeftSide ? Geometry.StartLeftX : Geometry.StartRightX;
	const float EndX = bLeftSide ? Geometry.EndLeftX : Geometry.EndRightX;
	const float SurfaceLength = EndX - StartX;
	if (SurfaceLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float SourceWidth = GetWallMeshSampleWidth(*WallMeshData);
	const float SourceHeight = FMath::Max(
		1.0f,
		WallMeshData->WallHeight > UE_SMALL_NUMBER
			? WallMeshData->WallHeight
			: WallMeshData->WallLocalBoundsMax.Z - WallMeshData->WallLocalBoundsMin.Z);
	const float SourceThickness = FMath::Max(
		1.0f,
		WallMeshData->WallThickness > UE_SMALL_NUMBER
			? WallMeshData->WallThickness
			: WallMeshData->WallLocalBoundsMax.Y - WallMeshData->WallLocalBoundsMin.Y);
	const float SourceMinX = WallMeshData->WallLocalBoundsMax.X > WallMeshData->WallLocalBoundsMin.X
		? WallMeshData->WallLocalBoundsMin.X
		: 0.0f;
	const float SourceMinZ = WallMeshData->WallLocalBoundsMax.Z > WallMeshData->WallLocalBoundsMin.Z
		? WallMeshData->WallLocalBoundsMin.Z
		: 0.0f;
	const float SourceSideSign = SurfaceStyle.SampleSide == EEHBWallMeshSampleSide::Front ? 1.0f : -1.0f;
	const float SourceAnchorY = SourceSideSign * SourceThickness * 0.5f;
	const float XScale = 1.0f;
	const float ZScale = SafeHeight / SourceHeight;
	const float ThicknessScale = SafeThickness / SourceThickness;
	const float PhaseAtSurfaceStart = CalculateSurfaceSamplePhaseAtStart(bLeftSide, SurfaceStyle);
	float PhaseRemainder = FMath::Fmod(PhaseAtSurfaceStart, SourceWidth);
	if (PhaseRemainder < 0.0f)
	{
		PhaseRemainder += SourceWidth;
	}

	struct FTransformedSampleVertex
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector2D UV = FVector2D::ZeroVector;
	};

	auto TransformSampleVertex = [&](const FEHBWallMeshSampleVertex& SourceVertex, float TileStartX)
	{
		FTransformedSampleVertex Result;
		const float SourceOutwardOffset = (SourceVertex.Position.Y - SourceAnchorY) * SourceSideSign;
		Result.Position = FVector(
			TileStartX + (SourceVertex.Position.X - SourceMinX),
			TargetSideY + SourceOutwardOffset * TargetSideSign * ThicknessScale,
			(SourceVertex.Position.Z - SourceMinZ) * ZScale);

		Result.Normal = FVector(
			SourceVertex.Normal.X / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(XScale)),
			SourceVertex.Normal.Y * SourceSideSign * TargetSideSign / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(ThicknessScale)),
			SourceVertex.Normal.Z / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(ZScale))).GetSafeNormal();
		if (Result.Normal.IsNearlyZero())
		{
			Result.Normal = FVector(0.0f, TargetSideSign, 0.0f);
		}

		Result.UV = SourceVertex.UV0;
		return Result;
	};

	OutMaterials = WallMeshData->Materials;
	TArray<TArray<FVector2d>> OpeningPolygons;
	BuildDoorWindowOpeningPolygons(Geometry, OpeningPolygons);

	const int32 SourceTriangleCount = SampleSurface.Triangles.Num() / 3;
	const int32 RepeatCount = FMath::Max(1, FMath::CeilToInt((SurfaceLength + PhaseRemainder) / SourceWidth) + 1);
	const int32 MaxRepeatCount = 1024;
	if (RepeatCount > MaxRepeatCount)
	{
		UE_LOG(
			LogEHBWallMesh,
			Warning,
			TEXT("采样墙面重复次数过多，已回退为简单墙面。Wall=%s Side=%s RepeatCount=%d SourceWidth=%.2f SurfaceLength=%.2f"),
			*GetNameSafe(this),
			bLeftSide ? TEXT("Left") : TEXT("Right"),
			RepeatCount,
			SourceWidth,
			SurfaceLength);
		return false;
	}

	OutVertices.Reserve(SourceTriangleCount * RepeatCount * 3);
	OutNormals.Reserve(SourceTriangleCount * RepeatCount * 3);
	OutUVs.Reserve(SourceTriangleCount * RepeatCount * 3);
	OutTriangles.Reserve(SourceTriangleCount * RepeatCount * 3);
	OutTriangleMaterialIndices.Reserve(SourceTriangleCount * RepeatCount);

	auto IsPointInsideAnyOpening = [&OpeningPolygons](const FVector2d& Point)
	{
		for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
		{
			if (IsPointInsideOpeningPolygon(Point, OpeningPolygon))
			{
				return true;
			}
		}
		return false;
	};

	auto MergeIntervals = [](TArray<TPair<double, double>>& Intervals)
	{
		Intervals.Sort([](const TPair<double, double>& A, const TPair<double, double>& B)
		{
			return A.Key < B.Key;
		});

		TArray<TPair<double, double>> MergedIntervals;
		for (const TPair<double, double>& Interval : Intervals)
		{
			if (MergedIntervals.IsEmpty()
				|| Interval.Key > MergedIntervals.Last().Value + EHBWallOpeningPointTolerance)
			{
				MergedIntervals.Add(Interval);
			}
			else
			{
				MergedIntervals.Last().Value = FMath::Max(MergedIntervals.Last().Value, Interval.Value);
			}
		}
		return MergedIntervals;
	};

	auto BuildOpeningZIntervalsAtX = [&MergeIntervals](double QueryX, const TArray<TArray<FVector2d>>& Polygons)
	{
		TArray<TPair<double, double>> Intervals;
		for (const TArray<FVector2d>& Polygon : Polygons)
		{
			double MinX = TNumericLimits<double>::Max();
			double MaxX = TNumericLimits<double>::Lowest();
			for (const FVector2d& Point : Polygon)
			{
				MinX = FMath::Min(MinX, Point.X);
				MaxX = FMath::Max(MaxX, Point.X);
			}

			if (QueryX < MinX - EHBWallOpeningPointTolerance
				|| QueryX > MaxX + EHBWallOpeningPointTolerance)
			{
				continue;
			}

			TArray<double> Crossings;
			for (int32 PointIndex = 0; PointIndex < Polygon.Num(); ++PointIndex)
			{
				const FVector2d& Start = Polygon[PointIndex];
				const FVector2d& End = Polygon[(PointIndex + 1) % Polygon.Num()];
				if (FMath::Abs(Start.X - End.X) <= EHBWallOpeningPointTolerance)
				{
					if (FMath::Abs(QueryX - Start.X) <= EHBWallOpeningPointTolerance)
					{
						Crossings.Add(Start.Y);
						Crossings.Add(End.Y);
					}
					continue;
				}

				const double MinEdgeX = FMath::Min(Start.X, End.X);
				const double MaxEdgeX = FMath::Max(Start.X, End.X);
				if (QueryX < MinEdgeX - EHBWallOpeningPointTolerance
					|| QueryX > MaxEdgeX + EHBWallOpeningPointTolerance)
				{
					continue;
				}

				const double Alpha = (QueryX - Start.X) / (End.X - Start.X);
				if (Alpha >= -EHBWallOpeningPointTolerance && Alpha <= 1.0 + EHBWallOpeningPointTolerance)
				{
					Crossings.Add(FMath::Lerp(Start.Y, End.Y, Alpha));
				}
			}

			Crossings.Sort();
			TArray<double> UniqueCrossings;
			for (double Crossing : Crossings)
			{
				if (UniqueCrossings.IsEmpty()
					|| FMath::Abs(UniqueCrossings.Last() - Crossing) > EHBWallOpeningPointTolerance)
				{
					UniqueCrossings.Add(Crossing);
				}
			}

			for (int32 CrossingIndex = 0; CrossingIndex + 1 < UniqueCrossings.Num(); CrossingIndex += 2)
			{
				const double StartZ = UniqueCrossings[CrossingIndex];
				const double EndZ = UniqueCrossings[CrossingIndex + 1];
				if (EndZ > StartZ + EHBWallOpeningPointTolerance)
				{
					Intervals.Add(TPair<double, double>(StartZ, EndZ));
				}
			}
		}

		return MergeIntervals(Intervals);
	};

	auto BuildOpeningXIntervalsAtZ = [&MergeIntervals](double QueryZ, const TArray<TArray<FVector2d>>& Polygons)
	{
		TArray<TPair<double, double>> Intervals;
		for (const TArray<FVector2d>& Polygon : Polygons)
		{
			double MinZ = TNumericLimits<double>::Max();
			double MaxZ = TNumericLimits<double>::Lowest();
			for (const FVector2d& Point : Polygon)
			{
				MinZ = FMath::Min(MinZ, Point.Y);
				MaxZ = FMath::Max(MaxZ, Point.Y);
			}

			if (QueryZ < MinZ - EHBWallOpeningPointTolerance
				|| QueryZ > MaxZ + EHBWallOpeningPointTolerance)
			{
				continue;
			}

			TArray<double> Crossings;
			for (int32 PointIndex = 0; PointIndex < Polygon.Num(); ++PointIndex)
			{
				const FVector2d& Start = Polygon[PointIndex];
				const FVector2d& End = Polygon[(PointIndex + 1) % Polygon.Num()];
				if (FMath::Abs(Start.Y - End.Y) <= EHBWallOpeningPointTolerance)
				{
					if (FMath::Abs(QueryZ - Start.Y) <= EHBWallOpeningPointTolerance)
					{
						Crossings.Add(Start.X);
						Crossings.Add(End.X);
					}
					continue;
				}

				const double MinEdgeZ = FMath::Min(Start.Y, End.Y);
				const double MaxEdgeZ = FMath::Max(Start.Y, End.Y);
				if (QueryZ < MinEdgeZ - EHBWallOpeningPointTolerance
					|| QueryZ > MaxEdgeZ + EHBWallOpeningPointTolerance)
				{
					continue;
				}

				const double Alpha = (QueryZ - Start.Y) / (End.Y - Start.Y);
				if (Alpha >= -EHBWallOpeningPointTolerance && Alpha <= 1.0 + EHBWallOpeningPointTolerance)
				{
					Crossings.Add(FMath::Lerp(Start.X, End.X, Alpha));
				}
			}

			Crossings.Sort();
			TArray<double> UniqueCrossings;
			for (double Crossing : Crossings)
			{
				if (UniqueCrossings.IsEmpty()
					|| FMath::Abs(UniqueCrossings.Last() - Crossing) > EHBWallOpeningPointTolerance)
				{
					UniqueCrossings.Add(Crossing);
				}
			}

			for (int32 CrossingIndex = 0; CrossingIndex + 1 < UniqueCrossings.Num(); CrossingIndex += 2)
			{
				const double StartXInterval = UniqueCrossings[CrossingIndex];
				const double EndXInterval = UniqueCrossings[CrossingIndex + 1];
				if (EndXInterval > StartXInterval + EHBWallOpeningPointTolerance)
				{
					Intervals.Add(TPair<double, double>(StartXInterval, EndXInterval));
				}
			}
		}

		return MergeIntervals(Intervals);
	};

	auto InterpolateSampleVertex = [](const FTransformedSampleVertex& A, const FTransformedSampleVertex& B, double Alpha)
	{
		const float FloatAlpha = static_cast<float>(Alpha);
		FTransformedSampleVertex Result;
		Result.Position = FMath::Lerp(A.Position, B.Position, FloatAlpha);
		Result.Normal = FMath::Lerp(A.Normal, B.Normal, FloatAlpha).GetSafeNormal();
		Result.UV = FMath::Lerp(A.UV, B.UV, FloatAlpha);
		return Result;
	};

	auto ClipPolygonByZ = [&InterpolateSampleVertex](
		const TArray<FTransformedSampleVertex>& Polygon,
		double ClipZ,
		bool bKeepGreater)
	{
		TArray<FTransformedSampleVertex> Result;
		if (Polygon.IsEmpty())
		{
			return Result;
		}

		FTransformedSampleVertex Previous = Polygon.Last();
		bool bPreviousInside = bKeepGreater
			? Previous.Position.Z >= ClipZ - EHBWallOpeningPointTolerance
			: Previous.Position.Z <= ClipZ + EHBWallOpeningPointTolerance;
		for (const FTransformedSampleVertex& Current : Polygon)
		{
			const bool bCurrentInside = bKeepGreater
				? Current.Position.Z >= ClipZ - EHBWallOpeningPointTolerance
				: Current.Position.Z <= ClipZ + EHBWallOpeningPointTolerance;

			if (bCurrentInside != bPreviousInside)
			{
				const double DeltaZ = static_cast<double>(Current.Position.Z - Previous.Position.Z);
				if (FMath::Abs(DeltaZ) > UE_DOUBLE_SMALL_NUMBER)
				{
					const double Alpha = (ClipZ - static_cast<double>(Previous.Position.Z)) / DeltaZ;
					Result.Add(InterpolateSampleVertex(Previous, Current, FMath::Clamp(Alpha, 0.0, 1.0)));
				}
			}

			if (bCurrentInside)
			{
				Result.Add(Current);
			}

			Previous = Current;
			bPreviousInside = bCurrentInside;
		}
		return Result;
	};

	auto ClipPolygonByX = [&InterpolateSampleVertex](
		const TArray<FTransformedSampleVertex>& Polygon,
		double ClipX,
		bool bKeepGreater)
	{
		TArray<FTransformedSampleVertex> Result;
		if (Polygon.IsEmpty())
		{
			return Result;
		}

		FTransformedSampleVertex Previous = Polygon.Last();
		bool bPreviousInside = bKeepGreater
			? Previous.Position.X >= ClipX - EHBWallOpeningPointTolerance
			: Previous.Position.X <= ClipX + EHBWallOpeningPointTolerance;
		for (const FTransformedSampleVertex& Current : Polygon)
		{
			const bool bCurrentInside = bKeepGreater
				? Current.Position.X >= ClipX - EHBWallOpeningPointTolerance
				: Current.Position.X <= ClipX + EHBWallOpeningPointTolerance;

			if (bCurrentInside != bPreviousInside)
			{
				const double DeltaX = static_cast<double>(Current.Position.X - Previous.Position.X);
				if (FMath::Abs(DeltaX) > UE_DOUBLE_SMALL_NUMBER)
				{
					const double Alpha = (ClipX - static_cast<double>(Previous.Position.X)) / DeltaX;
					Result.Add(InterpolateSampleVertex(Previous, Current, FMath::Clamp(Alpha, 0.0, 1.0)));
				}
			}

			if (bCurrentInside)
			{
				Result.Add(Current);
			}

			Previous = Current;
			bPreviousInside = bCurrentInside;
		}
		return Result;
	};

	auto AreTransformedSampleVerticesNearlyEqual = [](const FTransformedSampleVertex& A, const FTransformedSampleVertex& B)
	{
		return (A.Position - B.Position).SizeSquared() <= FMath::Square(EHBWallSampleMinTriangleEdge);
	};

	auto SanitizeTransformedSamplePolygon = [&AreTransformedSampleVerticesNearlyEqual](
		const TArray<FTransformedSampleVertex>& Polygon)
	{
		TArray<FTransformedSampleVertex> Result;
		Result.Reserve(Polygon.Num());
		for (const FTransformedSampleVertex& Vertex : Polygon)
		{
			if (Result.IsEmpty() || !AreTransformedSampleVerticesNearlyEqual(Result.Last(), Vertex))
			{
				Result.Add(Vertex);
			}
		}

		while (Result.Num() > 1 && AreTransformedSampleVerticesNearlyEqual(Result[0], Result.Last()))
		{
			Result.Pop(EAllowShrinking::No);
		}
		return Result;
	};

	auto ClipPolygonToWallBoundary = [&ClipPolygonByX, &ClipPolygonByZ, &SanitizeTransformedSamplePolygon, StartX, EndX, SafeHeight](
		const TArray<FTransformedSampleVertex>& Polygon)
	{
		TArray<FTransformedSampleVertex> Clipped = ClipPolygonByX(Polygon, StartX, true);
		Clipped = ClipPolygonByX(Clipped, EndX, false);
		Clipped = ClipPolygonByZ(Clipped, 0.0, true);
		Clipped = ClipPolygonByZ(Clipped, SafeHeight, false);
		return SanitizeTransformedSamplePolygon(Clipped);
	};

	auto AppendTransformedSamplePolygon =
		[this, &OutVertices, &OutTriangles, &OutNormals, &OutUVs, &OutTriangleMaterialIndices](
			const TArray<FTransformedSampleVertex>& Polygon,
			int32 MaterialIndex)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}

		for (int32 PolygonIndex = 1; PolygonIndex + 1 < Polygon.Num(); ++PolygonIndex)
		{
			AppendSurfaceTriangle(
				OutVertices,
				OutTriangles,
				OutNormals,
				OutUVs,
				OutTriangleMaterialIndices,
				Polygon[0].Position,
				Polygon[PolygonIndex].Position,
				Polygon[PolygonIndex + 1].Position,
				Polygon[0].Normal,
				Polygon[PolygonIndex].Normal,
				Polygon[PolygonIndex + 1].Normal,
				Polygon[0].UV,
				Polygon[PolygonIndex].UV,
				Polygon[PolygonIndex + 1].UV,
				MaterialIndex);
		}
	};

	for (int32 RepeatIndex = 0; RepeatIndex < RepeatCount; ++RepeatIndex)
	{
		const float TileStartX = StartX - PhaseRemainder + static_cast<float>(RepeatIndex) * SourceWidth;
		if (TileStartX >= EndX - KINDA_SMALL_NUMBER)
		{
			break;
		}

		for (int32 TriangleIndex = 0; TriangleIndex < SourceTriangleCount; ++TriangleIndex)
		{
			const int32 SourceIndexA = SampleSurface.Triangles[TriangleIndex * 3 + 0];
			const int32 SourceIndexB = SampleSurface.Triangles[TriangleIndex * 3 + 1];
			const int32 SourceIndexC = SampleSurface.Triangles[TriangleIndex * 3 + 2];
			if (!SampleSurface.Vertices.IsValidIndex(SourceIndexA)
				|| !SampleSurface.Vertices.IsValidIndex(SourceIndexB)
				|| !SampleSurface.Vertices.IsValidIndex(SourceIndexC))
			{
				continue;
			}

			const FTransformedSampleVertex A = TransformSampleVertex(SampleSurface.Vertices[SourceIndexA], TileStartX);
			const FTransformedSampleVertex B = TransformSampleVertex(SampleSurface.Vertices[SourceIndexB], TileStartX);
			const FTransformedSampleVertex C = TransformSampleVertex(SampleSurface.Vertices[SourceIndexC], TileStartX);
			const FVector2d A2D(A.Position.X, A.Position.Z);
			const FVector2d B2D(B.Position.X, B.Position.Z);
			const FVector2d C2D(C.Position.X, C.Position.Z);
			const TArray<FVector2d> SourceTriangle2D = { A2D, B2D, C2D };
			const int32 MaterialIndex = SampleSurface.TriangleMaterialIndices.IsValidIndex(TriangleIndex)
				? SampleSurface.TriangleMaterialIndices[TriangleIndex]
				: 0;

		const double TriangleMinX = FMath::Min3(A2D.X, B2D.X, C2D.X);
		const double TriangleMaxX = FMath::Max3(A2D.X, B2D.X, C2D.X);
		const double TriangleMinY = FMath::Min3(A2D.Y, B2D.Y, C2D.Y);
		const double TriangleMaxY = FMath::Max3(A2D.Y, B2D.Y, C2D.Y);
		if (FMath::Abs(CalculateOpeningPolygonTwiceArea(SourceTriangle2D)) <= UE_DOUBLE_SMALL_NUMBER)
		{
			TArray<FTransformedSampleVertex> BoundaryClippedPolygon = ClipPolygonToWallBoundary({ A, B, C });
			if (BoundaryClippedPolygon.Num() < 3)
			{
				continue;
			}

			const bool bNearlyConstantX =
				TriangleMaxX - TriangleMinX <= EHBWallOpeningPointTolerance * 2.0;
			const bool bNearlyConstantZ =
				TriangleMaxY - TriangleMinY <= EHBWallOpeningPointTolerance * 2.0;
			if (bNearlyConstantX)
			{
				TArray<TArray<FTransformedSampleVertex>> RemainingPolygons;
				RemainingPolygons.Add(MoveTemp(BoundaryClippedPolygon));
				const double QueryX = (A2D.X + B2D.X + C2D.X) / 3.0;
				const TArray<TPair<double, double>> CutIntervals =
					BuildOpeningZIntervalsAtX(QueryX, OpeningPolygons);
				for (const TPair<double, double>& CutInterval : CutIntervals)
				{
					TArray<TArray<FTransformedSampleVertex>> NextPolygons;
					for (const TArray<FTransformedSampleVertex>& Polygon : RemainingPolygons)
					{
						TArray<FTransformedSampleVertex> Below = ClipPolygonByZ(Polygon, CutInterval.Key, false);
						if (Below.Num() >= 3)
						{
							NextPolygons.Add(MoveTemp(Below));
						}

						TArray<FTransformedSampleVertex> Above = ClipPolygonByZ(Polygon, CutInterval.Value, true);
						if (Above.Num() >= 3)
						{
							NextPolygons.Add(MoveTemp(Above));
						}
					}
					RemainingPolygons = MoveTemp(NextPolygons);
				}

				for (const TArray<FTransformedSampleVertex>& Polygon : RemainingPolygons)
				{
					AppendTransformedSamplePolygon(Polygon, MaterialIndex);
				}
				continue;
			}
			if (bNearlyConstantZ)
			{
				TArray<TArray<FTransformedSampleVertex>> RemainingPolygons;
				RemainingPolygons.Add(MoveTemp(BoundaryClippedPolygon));
				const double QueryZ = (A2D.Y + B2D.Y + C2D.Y) / 3.0;
				const TArray<TPair<double, double>> CutIntervals =
					BuildOpeningXIntervalsAtZ(QueryZ, OpeningPolygons);
				for (const TPair<double, double>& CutInterval : CutIntervals)
				{
					TArray<TArray<FTransformedSampleVertex>> NextPolygons;
					for (const TArray<FTransformedSampleVertex>& Polygon : RemainingPolygons)
					{
						TArray<FTransformedSampleVertex> Left = ClipPolygonByX(Polygon, CutInterval.Key, false);
						if (Left.Num() >= 3)
						{
							NextPolygons.Add(MoveTemp(Left));
						}

						TArray<FTransformedSampleVertex> Right = ClipPolygonByX(Polygon, CutInterval.Value, true);
						if (Right.Num() >= 3)
						{
							NextPolygons.Add(MoveTemp(Right));
						}
					}
					RemainingPolygons = MoveTemp(NextPolygons);
				}

				for (const TArray<FTransformedSampleVertex>& Polygon : RemainingPolygons)
				{
					AppendTransformedSamplePolygon(Polygon, MaterialIndex);
				}
				continue;
			}

			bool bTouchesOpening = false;
			for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
			{
				if (DoesDegenerateTriangleTouchOpening(A2D, B2D, C2D, OpeningPolygon))
				{
					bTouchesOpening = true;
					break;
				}
			}

			const FVector2d Centroid = (A2D + B2D + C2D) / 3.0;
			if (!bTouchesOpening && !IsPointInsideAnyOpening(Centroid))
			{
				AppendTransformedSamplePolygon(BoundaryClippedPolygon, MaterialIndex);
			}
			continue;
		}

		bool bNeedsClipping =
			TriangleMinX < static_cast<double>(StartX) - EHBWallOpeningPointTolerance
			|| TriangleMaxX > static_cast<double>(EndX) + EHBWallOpeningPointTolerance
			|| TriangleMinY < -EHBWallOpeningPointTolerance
			|| TriangleMaxY > static_cast<double>(SafeHeight) + EHBWallOpeningPointTolerance;
		if (!OpeningPolygons.IsEmpty())
		{
			for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
			{
				double OpeningMinX = TNumericLimits<double>::Max();
				double OpeningMaxX = TNumericLimits<double>::Lowest();
				double OpeningMinY = TNumericLimits<double>::Max();
				double OpeningMaxY = TNumericLimits<double>::Lowest();
				for (const FVector2d& OpeningPoint : OpeningPolygon)
				{
					OpeningMinX = FMath::Min(OpeningMinX, OpeningPoint.X);
					OpeningMaxX = FMath::Max(OpeningMaxX, OpeningPoint.X);
					OpeningMinY = FMath::Min(OpeningMinY, OpeningPoint.Y);
					OpeningMaxY = FMath::Max(OpeningMaxY, OpeningPoint.Y);
				}

				if (DoBoundingBoxesOverlap(
					TriangleMinX,
					TriangleMaxX,
					TriangleMinY,
					TriangleMaxY,
					OpeningMinX,
					OpeningMaxX,
					OpeningMinY,
					OpeningMaxY))
				{
					bNeedsClipping = true;
					break;
				}
			}
		}

		if (!bNeedsClipping)
		{
			const FVector2d Centroid = (A2D + B2D + C2D) / 3.0;
			if (!IsPointInsideAnyOpening(Centroid))
			{
				AppendSurfaceTriangle(OutVertices, OutTriangles, OutNormals, OutUVs, OutTriangleMaterialIndices, A.Position, B.Position, C.Position, A.Normal, B.Normal, C.Normal, A.UV, B.UV, C.UV, MaterialIndex);
			}
			continue;
		}

		UE::Geometry::FArrangement2d Arrangement(FMath::Max(static_cast<double>(SurfaceLength), static_cast<double>(SafeHeight)) / 128.0);
		InsertClosedOpeningPolygon(Arrangement, SourceTriangle2D);
		const TArray<FVector2d> WallBoundary = {
			FVector2d(StartX, 0.0),
			FVector2d(StartX, SafeHeight),
			FVector2d(EndX, SafeHeight),
			FVector2d(EndX, 0.0)
		};
		InsertClosedOpeningPolygon(Arrangement, WallBoundary);
		for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
		{
			InsertClosedOpeningPolygon(Arrangement, OpeningPolygon);
		}

		UE::Geometry::FConstrainedDelaunay2d Triangulator;
		Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
		Triangulator.bOrientedEdges = false;
		Triangulator.bSplitBowties = true;
		Triangulator.Add(Arrangement.Graph);

		const bool bTriangulationSucceeded = Triangulator.Triangulate(
			[&OpeningPolygons, A2D, B2D, C2D, StartX, EndX, SafeHeight](
				const TArray<FVector2d>& Vertices,
				const UE::Geometry::FIndex3i& Triangle)
		{
			const FVector2d Centroid =
				(Vertices[Triangle.A] + Vertices[Triangle.B] + Vertices[Triangle.C]) / 3.0;
			if (!IsPointInsideTriangle2D(Centroid, A2D, B2D, C2D))
			{
				return false;
			}

			const bool bInsideWall =
				Centroid.X >= static_cast<double>(StartX) - EHBWallOpeningPointTolerance
				&& Centroid.X <= static_cast<double>(EndX) + EHBWallOpeningPointTolerance
				&& Centroid.Y >= -EHBWallOpeningPointTolerance
				&& Centroid.Y <= static_cast<double>(SafeHeight) + EHBWallOpeningPointTolerance;
			if (!bInsideWall)
			{
				return false;
			}

			for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
			{
				if (IsPointInsideOpeningPolygon(Centroid, OpeningPolygon))
				{
					return false;
				}
			}
			return true;
		});

		if (!bTriangulationSucceeded)
		{
			const FVector2d Centroid = (A2D + B2D + C2D) / 3.0;
			if (!IsPointInsideAnyOpening(Centroid))
			{
				const TArray<FTransformedSampleVertex> BoundaryClippedPolygon = ClipPolygonToWallBoundary({ A, B, C });
				AppendTransformedSamplePolygon(BoundaryClippedPolygon, MaterialIndex);
			}
			continue;
		}

		for (const UE::Geometry::FIndex3i& CutTriangle : Triangulator.Triangles)
		{
			FTransformedSampleVertex CutVertices[3];
			const int32 CutIndices[3] = { CutTriangle.A, CutTriangle.B, CutTriangle.C };
			bool bCanInterpolate = true;
			for (int32 CutVertexIndex = 0; CutVertexIndex < 3; ++CutVertexIndex)
			{
				const FVector2d& CutPoint = Triangulator.Vertices[CutIndices[CutVertexIndex]];
				double WeightA = 0.0;
				double WeightB = 0.0;
				double WeightC = 0.0;
				if (!GetBarycentricInTriangle2D(CutPoint, A2D, B2D, C2D, WeightA, WeightB, WeightC))
				{
					bCanInterpolate = false;
					break;
				}

				CutVertices[CutVertexIndex].Position =
					A.Position * WeightA + B.Position * WeightB + C.Position * WeightC;
				CutVertices[CutVertexIndex].Position.X = static_cast<float>(CutPoint.X);
				CutVertices[CutVertexIndex].Position.Z = static_cast<float>(CutPoint.Y);
				CutVertices[CutVertexIndex].Normal =
					(A.Normal * WeightA + B.Normal * WeightB + C.Normal * WeightC).GetSafeNormal();
				if (CutVertices[CutVertexIndex].Normal.IsNearlyZero())
				{
					CutVertices[CutVertexIndex].Normal = FVector(0.0f, TargetSideSign, 0.0f);
				}
				CutVertices[CutVertexIndex].UV =
					A.UV * WeightA + B.UV * WeightB + C.UV * WeightC;
			}

			if (bCanInterpolate)
			{
				AppendSurfaceTriangle(
					OutVertices,
					OutTriangles,
					OutNormals,
					OutUVs,
					OutTriangleMaterialIndices,
					CutVertices[0].Position,
					CutVertices[1].Position,
					CutVertices[2].Position,
					CutVertices[0].Normal,
					CutVertices[1].Normal,
					CutVertices[2].Normal,
					CutVertices[0].UV,
					CutVertices[1].UV,
					CutVertices[2].UV,
					MaterialIndex);
			}
		}
	}
	}

	return true;
}

#include "EHBStraightWallOpeningMesh.inl"

void AEHB_Wall::BuildSimpleWallSurfaceMesh(bool bLeftSide, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const TArray<TArray<FVector2d>>* PreparedOpenings, bool* Succeeded) const
{
 OutVertices.Reset();OutTriangles.Reset();OutNormals.Reset();OutUVs.Reset();if(Succeeded)*Succeeded=false;
 FEHBWallResolvedGeometry Geometry;if(!BuildResolvedWallGeometry(Geometry))return;
 TArray<TArray<FVector2d>> Openings;if(PreparedOpenings)Openings=*PreparedOpenings;else BuildDoorWindowOpeningPolygons(Geometry,Openings);
 EHBStraightWallMesh150::Side(Geometry,Height,Thickness,bLeftSide,Openings,OutVertices,OutTriangles,OutNormals,OutUVs,Succeeded,*GetNameSafe(this));
}

void AEHB_Wall::BuildOpeningRevealCapMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const FEHBWallJunctionMesh* Left, const FEHBWallJunctionMesh* Right, const TArray<TArray<FVector2d>>* PreparedOpenings) const
{
 const auto& LeftVertices=Left?Left->Vertices:CachedLeftWallVertices;
 const auto& LeftTriangles=Left?Left->Triangles:CachedLeftWallTriangles;
 const auto& RightVertices=Right?Right->Vertices:CachedRightWallVertices;
 const auto& RightTriangles=Right?Right->Triangles:CachedRightWallTriangles;
	FEHBWallResolvedGeometry Geometry;
	if (!BuildResolvedWallGeometry(Geometry))
	{
		return;
	}

	TArray<TArray<FVector2d>> OpeningPolygons;
	if (PreparedOpenings) OpeningPolygons=*PreparedOpenings; else BuildDoorWindowOpeningPolygons(Geometry, OpeningPolygons);
	if (OpeningPolygons.IsEmpty())
	{
		return;
	}

 // Extrude the union boundary through the wall volume, then clip each face
 // against its actual end planes. Pairing the two side meshes loses reveals
 // that reach a miter end before reaching the opposite wall side.
 if(!IsCurveDeformationEnabled()
  && LeftSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::SampledMesh
  && RightSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::SampledMesh)
 {
  EHBStraightWallMesh150::Reveals(Geometry,Height,Thickness,OpeningPolygons,OutVertices,OutTriangles,OutNormals,OutUVs);
  return;
 }
 if(LeftVertices.IsEmpty()||LeftTriangles.Num()<3||RightVertices.IsEmpty()||RightTriangles.Num()<3)return;

	struct FRevealBoundaryEdge
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		FVector2d A2D = FVector2d::Zero();
		FVector2d B2D = FVector2d::Zero();
	};

	auto IsEdgeOnOpeningBoundary = [&OpeningPolygons](const FVector2d& A, const FVector2d& B)
	{
		if (AreOpeningPointsNearlyEqual(A, B))
		{
			return false;
		}

		const FVector2d Midpoint = (A + B) * 0.5;
		for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
		{
			for (int32 PointIndex = 0; PointIndex < OpeningPolygon.Num(); ++PointIndex)
			{
				const FVector2d& SegmentStart = OpeningPolygon[PointIndex];
				const FVector2d& SegmentEnd = OpeningPolygon[(PointIndex + 1) % OpeningPolygon.Num()];
				if (IsPointOnSegment2D(A, SegmentStart, SegmentEnd)
					&& IsPointOnSegment2D(B, SegmentStart, SegmentEnd)
					&& IsPointOnSegment2D(Midpoint, SegmentStart, SegmentEnd))
				{
					return true;
				}
			}
		}
		return false;
	};

	auto ExtractOpeningBoundaryEdges =
		[&IsEdgeOnOpeningBoundary](
			const TArray<FVector>& Vertices,
			const TArray<int32>& Triangles,
			TArray<FRevealBoundaryEdge>& OutEdges)
	{
		OutEdges.Reset();
		TSet<FString> AddedEdgeKeys;
		for (int32 TriangleIndex = 0; TriangleIndex + 2 < Triangles.Num(); TriangleIndex += 3)
		{
			const int32 TriangleVertexIndices[3] = {
				Triangles[TriangleIndex + 0],
				Triangles[TriangleIndex + 1],
				Triangles[TriangleIndex + 2]
			};

			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				const int32 IndexA = TriangleVertexIndices[EdgeIndex];
				const int32 IndexB = TriangleVertexIndices[(EdgeIndex + 1) % 3];
				if (!Vertices.IsValidIndex(IndexA) || !Vertices.IsValidIndex(IndexB))
				{
					continue;
				}

				const FVector& A = Vertices[IndexA];
				const FVector& B = Vertices[IndexB];
				const FVector2d A2D(A.X, A.Z);
				const FVector2d B2D(B.X, B.Z);
				if (!IsEdgeOnOpeningBoundary(A2D, B2D))
				{
					continue;
				}

				const FString EdgeKey = MakeOpeningEdgeKey(A2D, B2D);
				if (AddedEdgeKeys.Contains(EdgeKey))
				{
					continue;
				}

				AddedEdgeKeys.Add(EdgeKey);
				FRevealBoundaryEdge& NewEdge = OutEdges.AddDefaulted_GetRef();
				NewEdge.A = A;
				NewEdge.B = B;
				NewEdge.A2D = A2D;
				NewEdge.B2D = B2D;
			}
		}
	};

	TArray<FRevealBoundaryEdge> LeftEdges;
	TArray<FRevealBoundaryEdge> RightEdges;
	ExtractOpeningBoundaryEdges(LeftVertices, LeftTriangles, LeftEdges);
	ExtractOpeningBoundaryEdges(RightVertices, RightTriangles, RightEdges);

	const double WallMinZ = 0.0;
	const double WallMaxZ = FMath::Max(1.0, static_cast<double>(Height));
	auto AddReferenceOpeningEdges = [&OpeningPolygons, WallMinZ, WallMaxZ](float WallY, TArray<FRevealBoundaryEdge>& OutEdges)
	{
		TSet<FString> ExistingKeys;
		for (const FRevealBoundaryEdge& Edge : OutEdges)
		{
			ExistingKeys.Add(MakeOpeningEdgeKey(Edge.A2D, Edge.B2D));
		}

		auto MakePointOnSegment = [](const FVector2d& A, const FVector2d& B, double Coordinate, bool bUseX)
		{
			const double StartCoordinate = bUseX ? A.X : A.Y;
			const double EndCoordinate = bUseX ? B.X : B.Y;
			const double Denominator = EndCoordinate - StartCoordinate;
			if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
			{
				return A;
			}

			const double Alpha = FMath::Clamp((Coordinate - StartCoordinate) / Denominator, 0.0, 1.0);
			return FMath::Lerp(A, B, Alpha);
		};

		auto AddReferenceEdge = [&OutEdges, &ExistingKeys, WallY](const FVector2d& A2D, const FVector2d& B2D)
		{
			if (AreOpeningPointsNearlyEqual(A2D, B2D))
			{
				return;
			}

			const FString EdgeKey = MakeOpeningEdgeKey(A2D, B2D);
			if (ExistingKeys.Contains(EdgeKey))
			{
				return;
			}

			ExistingKeys.Add(EdgeKey);
			FRevealBoundaryEdge& NewEdge = OutEdges.AddDefaulted_GetRef();
			NewEdge.A2D = A2D;
			NewEdge.B2D = B2D;
			NewEdge.A = FVector(static_cast<float>(A2D.X), WallY, static_cast<float>(A2D.Y));
			NewEdge.B = FVector(static_cast<float>(B2D.X), WallY, static_cast<float>(B2D.Y));
		};

		for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
		{
			for (int32 PointIndex = 0; PointIndex < OpeningPolygon.Num(); ++PointIndex)
			{
				const FVector2d& A2D = OpeningPolygon[PointIndex];
				const FVector2d& B2D = OpeningPolygon[(PointIndex + 1) % OpeningPolygon.Num()];
				if (AreOpeningPointsNearlyEqual(A2D, B2D))
				{
					continue;
				}

				if ((A2D.Y < WallMinZ - EHBWallOpeningPointTolerance
						&& B2D.Y < WallMinZ - EHBWallOpeningPointTolerance)
					|| (A2D.Y > WallMaxZ + EHBWallOpeningPointTolerance
						&& B2D.Y > WallMaxZ + EHBWallOpeningPointTolerance))
				{
					continue;
				}

				auto PointAtZ = [](const FVector2d& A, const FVector2d& B, double Z)
				{
					const double Denominator = B.Y - A.Y;
					if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
					{
						return A;
					}

					const double Alpha = FMath::Clamp((Z - A.Y) / Denominator, 0.0, 1.0);
					return FMath::Lerp(A, B, Alpha);
				};

				FVector2d ClippedA2D = A2D;
				FVector2d ClippedB2D = B2D;
				if (ClippedA2D.Y < WallMinZ)
				{
					ClippedA2D = PointAtZ(A2D, B2D, WallMinZ);
				}
				else if (ClippedA2D.Y > WallMaxZ)
				{
					ClippedA2D = PointAtZ(A2D, B2D, WallMaxZ);
				}

				if (ClippedB2D.Y < WallMinZ)
				{
					ClippedB2D = PointAtZ(A2D, B2D, WallMinZ);
				}
				else if (ClippedB2D.Y > WallMaxZ)
				{
					ClippedB2D = PointAtZ(A2D, B2D, WallMaxZ);
				}

				const FVector2d SegmentDirection = ClippedB2D - ClippedA2D;
				const bool bUseX = FMath::Abs(SegmentDirection.X) >= FMath::Abs(SegmentDirection.Y);
				const double SegmentStartCoordinate = bUseX ? ClippedA2D.X : ClippedA2D.Y;
				const double SegmentEndCoordinate = bUseX ? ClippedB2D.X : ClippedB2D.Y;
				const double SegmentMin = FMath::Min(SegmentStartCoordinate, SegmentEndCoordinate);
				const double SegmentMax = FMath::Max(SegmentStartCoordinate, SegmentEndCoordinate);
				if (SegmentMax <= SegmentMin + EHBWallOpeningPointTolerance)
				{
					continue;
				}

				TArray<TPair<double, double>> CoveredIntervals;
				for (const FRevealBoundaryEdge& ExistingEdge : OutEdges)
				{
					const FVector2d ExistingMidpoint = (ExistingEdge.A2D + ExistingEdge.B2D) * 0.5;
					if (!IsPointOnSegment2D(ExistingEdge.A2D, ClippedA2D, ClippedB2D)
						|| !IsPointOnSegment2D(ExistingEdge.B2D, ClippedA2D, ClippedB2D)
						|| !IsPointOnSegment2D(ExistingMidpoint, ClippedA2D, ClippedB2D))
					{
						continue;
					}

					const double ExistingStartCoordinate = bUseX ? ExistingEdge.A2D.X : ExistingEdge.A2D.Y;
					const double ExistingEndCoordinate = bUseX ? ExistingEdge.B2D.X : ExistingEdge.B2D.Y;
					const double CoveredStart = FMath::Max(SegmentMin, FMath::Min(ExistingStartCoordinate, ExistingEndCoordinate));
					const double CoveredEnd = FMath::Min(SegmentMax, FMath::Max(ExistingStartCoordinate, ExistingEndCoordinate));
					if (CoveredEnd > CoveredStart + EHBWallOpeningPointTolerance)
					{
						CoveredIntervals.Add(TPair<double, double>(CoveredStart, CoveredEnd));
					}
				}

				CoveredIntervals.Sort([](const TPair<double, double>& Left, const TPair<double, double>& Right)
				{
					return Left.Key < Right.Key;
				});

				TArray<TPair<double, double>> MergedCoveredIntervals;
				for (const TPair<double, double>& Interval : CoveredIntervals)
				{
					if (MergedCoveredIntervals.IsEmpty()
						|| Interval.Key > MergedCoveredIntervals.Last().Value + EHBWallOpeningPointTolerance)
					{
						MergedCoveredIntervals.Add(Interval);
					}
					else
					{
						MergedCoveredIntervals.Last().Value = FMath::Max(MergedCoveredIntervals.Last().Value, Interval.Value);
					}
				}

				double GapStart = SegmentMin;
				for (const TPair<double, double>& CoveredInterval : MergedCoveredIntervals)
				{
					if (CoveredInterval.Key > GapStart + EHBWallOpeningPointTolerance)
					{
						AddReferenceEdge(
							MakePointOnSegment(ClippedA2D, ClippedB2D, GapStart, bUseX),
							MakePointOnSegment(ClippedA2D, ClippedB2D, CoveredInterval.Key, bUseX));
					}
					GapStart = FMath::Max(GapStart, CoveredInterval.Value);
				}

				if (SegmentMax > GapStart + EHBWallOpeningPointTolerance)
				{
					AddReferenceEdge(
						MakePointOnSegment(ClippedA2D, ClippedB2D, GapStart, bUseX),
						MakePointOnSegment(ClippedA2D, ClippedB2D, SegmentMax, bUseX));
				}
			}
		}
	};

	const float HalfThickness = FMath::Max(1.0f, Thickness) * 0.5f;
 // Simple side triangulations already contain the final subtraction boundary.
 // Adding raw cutter edges would reinsert edges hidden by another cutter.
 // Sampled sides still need the legacy reference fallback until their own
 // sampled-surface boundary contract is implemented.
 if(!PreparedOpenings && (LeftSurfaceStyle.SourceType==EEHBWallSurfaceSourceType::SampledMesh || RightSurfaceStyle.SourceType==EEHBWallSurfaceSourceType::SampledMesh))
 {
  AddReferenceOpeningEdges(HalfThickness, LeftEdges);
  AddReferenceOpeningEdges(-HalfThickness, RightEdges);
 }
	if (LeftEdges.IsEmpty() || RightEdges.IsEmpty())
	{
		return;
	}

	auto InterpolateEdgePoint =
		[](const FRevealBoundaryEdge& Edge, double Coordinate, bool bUseX)
	{
		const double StartCoordinate = bUseX ? Edge.A2D.X : Edge.A2D.Y;
		const double EndCoordinate = bUseX ? Edge.B2D.X : Edge.B2D.Y;
		const double Denominator = EndCoordinate - StartCoordinate;
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Edge.A;
		}

		const double Alpha = FMath::Clamp((Coordinate - StartCoordinate) / Denominator, 0.0, 1.0);
		return FMath::Lerp(Edge.A, Edge.B, static_cast<float>(Alpha));
	};

	auto MakePointFromCoordinate =
		[](const FRevealBoundaryEdge& Edge, double Coordinate, bool bUseX)
	{
		const double StartCoordinate = bUseX ? Edge.A2D.X : Edge.A2D.Y;
		const double EndCoordinate = bUseX ? Edge.B2D.X : Edge.B2D.Y;
		const double Denominator = EndCoordinate - StartCoordinate;
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Edge.A2D;
		}

		const double Alpha = FMath::Clamp((Coordinate - StartCoordinate) / Denominator, 0.0, 1.0);
		return FMath::Lerp(Edge.A2D, Edge.B2D, Alpha);
	};

	TSet<FString> AddedRevealPairKeys;
	for (const FRevealBoundaryEdge& LeftEdge : LeftEdges)
	{
		const FVector2d LeftDirection = LeftEdge.B2D - LeftEdge.A2D;
		if (LeftDirection.SquaredLength() <= EHBWallOpeningPointTolerance * EHBWallOpeningPointTolerance)
		{
			continue;
		}

		for (const FRevealBoundaryEdge& RightEdge : RightEdges)
		{
			const FVector2d RightDirection = RightEdge.B2D - RightEdge.A2D;
			if (RightDirection.SquaredLength() <= EHBWallOpeningPointTolerance * EHBWallOpeningPointTolerance)
			{
				continue;
			}

			const double DirectionCross =
				LeftDirection.X * RightDirection.Y - LeftDirection.Y * RightDirection.X;
			if (FMath::Abs(DirectionCross) > EHBWallOpeningPointTolerance)
			{
				continue;
			}

			if (FMath::Abs(Cross2D(LeftEdge.A2D, LeftEdge.B2D, RightEdge.A2D)) > EHBWallOpeningPointTolerance
				|| FMath::Abs(Cross2D(LeftEdge.A2D, LeftEdge.B2D, RightEdge.B2D)) > EHBWallOpeningPointTolerance)
			{
				continue;
			}

			const bool bUseX = FMath::Abs(LeftDirection.X) >= FMath::Abs(LeftDirection.Y);
			const double LeftStart = bUseX ? LeftEdge.A2D.X : LeftEdge.A2D.Y;
			const double LeftEnd = bUseX ? LeftEdge.B2D.X : LeftEdge.B2D.Y;
			const double RightStart = bUseX ? RightEdge.A2D.X : RightEdge.A2D.Y;
			const double RightEnd = bUseX ? RightEdge.B2D.X : RightEdge.B2D.Y;
			const double OverlapStart = FMath::Max(FMath::Min(LeftStart, LeftEnd), FMath::Min(RightStart, RightEnd));
			const double OverlapEnd = FMath::Min(FMath::Max(LeftStart, LeftEnd), FMath::Max(RightStart, RightEnd));
			if (OverlapEnd <= OverlapStart + EHBWallOpeningPointTolerance)
			{
				continue;
			}

			const FVector LeftA = InterpolateEdgePoint(LeftEdge, OverlapStart, bUseX);
			const FVector LeftB = InterpolateEdgePoint(LeftEdge, OverlapEnd, bUseX);
			const FVector RightA = InterpolateEdgePoint(RightEdge, OverlapStart, bUseX);
			const FVector RightB = InterpolateEdgePoint(RightEdge, OverlapEnd, bUseX);
			// A reveal on the top boundary would seal a top-reaching opening again.
			const double WallTop=FMath::Max(1.0f,Height);
			if(LeftA.Z>=WallTop-1.e-6&&LeftB.Z>=WallTop-1.e-6&&RightA.Z>=WallTop-1.e-6&&RightB.Z>=WallTop-1.e-6)continue;
            if(LeftA.Z<=1.e-6&&LeftB.Z<=1.e-6&&RightA.Z<=1.e-6&&RightB.Z<=1.e-6)continue;
            auto OnEnd=[&](double LX,double RX){return FMath::Abs(LeftA.X-LX)<1.e-6&&FMath::Abs(LeftB.X-LX)<1.e-6&&FMath::Abs(RightA.X-RX)<1.e-6&&FMath::Abs(RightB.X-RX)<1.e-6;};
            if(OnEnd(Geometry.StartLeftX,Geometry.StartRightX)||OnEnd(Geometry.EndLeftX,Geometry.EndRightX))continue;
			const FString RevealPairKey = MakeRevealPairKey(LeftA, LeftB, RightA, RightB);
			if (AddedRevealPairKeys.Contains(RevealPairKey))
			{
				continue;
			}
			AddedRevealPairKeys.Add(RevealPairKey);

			const FVector2d RevealA2D = MakePointFromCoordinate(LeftEdge, OverlapStart, bUseX);
			const FVector2d RevealB2D = MakePointFromCoordinate(LeftEdge, OverlapEnd, bUseX);
			const FVector PreferredNormal = FVector::CrossProduct(LeftB - LeftA, RightB - LeftA).GetSafeNormal();
			if (PreferredNormal.IsNearlyZero())
			{
				continue;
			}

			const float ULength = static_cast<float>(FVector2d::Distance(RevealA2D, RevealB2D) / EHBWallUVWorldSize);
			const float VLength = FVector::Distance(LeftA, RightA) / EHBWallUVWorldSize;
			AppendCapTriangle(
				OutVertices,
				OutTriangles,
				OutNormals,
				OutUVs,
				LeftA,
				LeftB,
				RightB,
				PreferredNormal,
				FVector2D(0.0f, 0.0f),
				FVector2D(ULength, 0.0f),
				FVector2D(ULength, VLength));
			AppendCapTriangle(
				OutVertices,
				OutTriangles,
				OutNormals,
				OutUVs,
				LeftA,
				RightB,
				RightA,
				PreferredNormal,
				FVector2D(0.0f, 0.0f),
				FVector2D(ULength, VLength),
				FVector2D(0.0f, VLength));
		}
	}
}

void AEHB_Wall::BuildSimpleWallCapMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const TArray<TArray<FVector2d>>* PreparedOpenings, bool* Succeeded) const
{
 OutVertices.Reset();OutTriangles.Reset();OutNormals.Reset();OutUVs.Reset();if(Succeeded)*Succeeded=false;
 FEHBWallResolvedGeometry Geometry;if(!BuildResolvedWallGeometry(Geometry))return;
 TArray<TArray<FVector2d>> Openings;if(PreparedOpenings)Openings=*PreparedOpenings;else BuildDoorWindowOpeningPolygons(Geometry,Openings);
 EHBStraightWallMesh150::Caps(Geometry,Height,Thickness,bGenerateLinkedPillarEndCaps||!StartPillarGuid.IsValid(),bGenerateLinkedPillarEndCaps||!EndPillarGuid.IsValid(),Openings,OutVertices,OutTriangles,OutNormals,OutUVs,Succeeded);
}

void AEHB_Wall::ApplyMeshToComponent(UEHBGeneratedMeshComponent* TargetComponent, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, const TSoftObjectPtr<UMaterialInterface>& MaterialOverride) const
{
	const TArray<int32> EmptyTriangleMaterialIndices;
	const TArray<TSoftObjectPtr<UMaterialInterface>> EmptySourceMaterials;
	ApplyMeshToComponent(TargetComponent, Vertices, Triangles, Normals, UVs, MaterialOverride, EmptyTriangleMaterialIndices, EmptySourceMaterials);
}

void AEHB_Wall::ApplyMeshToComponent(
	UEHBGeneratedMeshComponent* TargetComponent,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs,
	const TSoftObjectPtr<UMaterialInterface>& MaterialOverride,
	const TArray<int32>& TriangleMaterialIndices,
	const TArray<TSoftObjectPtr<UMaterialInterface>>& SourceMaterials) const
{
	if (!TargetComponent)
	{
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, Vertices.Num());
	TArray<FProcMeshTangent> Tangents;

	TargetComponent->Modify();
	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(TargetComponent);

	UMaterialInterface* OverrideMaterial = MaterialOverride.LoadSynchronous();
	if (TriangleMaterialIndices.IsEmpty())
	{
		TargetComponent->EmptyOverrideMaterials();
		TargetComponent->CreateMeshSection_LinearColor(
			0,
			Vertices,
			Triangles,
			Normals,
			UVs,
			VertexColors,
			Tangents,
			true);
		TargetComponent->SetMeshSectionName(0, FName(TEXT("WallSurface")));
		TargetComponent->ClearMeshSectionsFrom(1);

		if (OverrideMaterial)
		{
			TargetComponent->SetMaterialIfChanged(0, OverrideMaterial);
		}
		return;
	}

	TargetComponent->EmptyOverrideMaterials();
	TMap<int32, TArray<int32>> TrianglesByMaterialIndex;
	TArray<int32> SortedMaterialIndices;
	const int32 TriangleCount = Triangles.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 MaterialIndex = TriangleMaterialIndices.IsValidIndex(TriangleIndex)
			? FMath::Max(0, TriangleMaterialIndices[TriangleIndex])
			: 0;

		TArray<int32>& SectionTriangles = TrianglesByMaterialIndex.FindOrAdd(MaterialIndex);
		if (SectionTriangles.IsEmpty())
		{
			SortedMaterialIndices.Add(MaterialIndex);
		}

		SectionTriangles.Add(Triangles[TriangleIndex * 3 + 0]);
		SectionTriangles.Add(Triangles[TriangleIndex * 3 + 1]);
		SectionTriangles.Add(Triangles[TriangleIndex * 3 + 2]);
	}

	SortedMaterialIndices.Sort();
	for (int32 SectionIndex = 0; SectionIndex < SortedMaterialIndices.Num(); ++SectionIndex)
	{
		const int32 MaterialIndex = SortedMaterialIndices[SectionIndex];
		const TArray<int32>* SectionTriangles = TrianglesByMaterialIndex.Find(MaterialIndex);
		if (!SectionTriangles)
		{
			continue;
		}

		TargetComponent->CreateMeshSection_LinearColor(
			SectionIndex,
			Vertices,
			*SectionTriangles,
			Normals,
			UVs,
			VertexColors,
			Tangents,
			true);
		TargetComponent->SetMeshSectionName(
			SectionIndex,
			FName(*FString::Printf(TEXT("WallMaterial_%d"), MaterialIndex)));

		UMaterialInterface* SectionMaterial = OverrideMaterial;
		if (!SectionMaterial && SourceMaterials.IsValidIndex(MaterialIndex))
		{
			SectionMaterial = SourceMaterials[MaterialIndex].LoadSynchronous();
		}

		if (SectionMaterial)
		{
			TargetComponent->SetMaterialIfChanged(SectionIndex, SectionMaterial);
		}
	}

	TargetComponent->ClearMeshSectionsFrom(SortedMaterialIndices.Num());
}

bool AEHB_Wall::BuildStructuralContactMesh(FEHBWallJunctionMesh& Out) const
{
 Out={};FName OpeningStatus;if(!ValidateSurfaceOpeningBindings(CutOperations,OpeningStatus))return false;FEHBWallResolvedGeometry Geometry;
 if(!FMath::IsFinite(Height)||!FMath::IsFinite(Thickness)||!BuildResolvedWallGeometry(Geometry))return false;
 FEHBWallJunctionMesh Result,Left,Right;
 bool CapsReady=false;BuildSimpleWallCapMesh(Result.Vertices,Result.Triangles,Result.Normals,Result.UVs,nullptr,&CapsReady);if(!CapsReady)return false;
 // Reveals use fresh base faces rather than the sampled/display face caches.
 BuildSimpleWallSurfaceMesh(true,Left.Vertices,Left.Triangles,Left.Normals,Left.UVs);
 BuildSimpleWallSurfaceMesh(false,Right.Vertices,Right.Triangles,Right.Normals,Right.UVs);
 BuildOpeningRevealCapMesh(Result.Vertices,Result.Triangles,Result.Normals,Result.UVs,&Left,&Right);
 ApplyCurveDeformationToMesh(Result.Vertices,Result.Triangles,Result.Normals,Result.UVs);
 // A valid opening can remove all contact geometry. Empty output means no support.
 Out=MoveTemp(Result);return true;
}
