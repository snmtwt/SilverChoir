// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_Stair.h"

#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Railing.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Settings/EHBBuildingToolsetSettings.h"

namespace
{
	constexpr float EHBStairUVWorldSize = 100.0f;
	struct FEHBStairMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
	};

	struct FEHBStairPathFrame
	{
		FVector Center = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
	};

	constexpr int32 EHBStairIntermediateControlCount = 3;

	float ResolveDimension(bool bUseActualDimensions, float ActualValue, float DefaultValue)
	{
		return FMath::Max(1.0f, bUseActualDimensions ? ActualValue : DefaultValue);
	}

	FVector2D GetPlanarUV(const FVector& Point, const FVector& Normal)
	{
		const FVector AbsNormal = Normal.GetAbs();
		if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
		{
			return FVector2D(Point.Y, Point.Z) / EHBStairUVWorldSize;
		}
		if (AbsNormal.Y >= AbsNormal.X && AbsNormal.Y >= AbsNormal.Z)
		{
			return FVector2D(Point.X, Point.Z) / EHBStairUVWorldSize;
		}
		return FVector2D(Point.X, Point.Y) / EHBStairUVWorldSize;
	}

	void AppendTriangle(
		FEHBStairMeshBuffers& Buffers,
		const FVector& InA,
		const FVector& InB,
		const FVector& InC,
		const FVector& DesiredNormal)
	{
		FVector A = InA;
		FVector B = InB;
		FVector C = InC;
		const FVector SafeNormal = DesiredNormal.GetSafeNormal();
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (TriangleNormal.IsNearlyZero() || SafeNormal.IsNearlyZero())
		{
			return;
		}

		// Generated mesh uses the opposite winding convention from the geometric cross product.
		if (FVector::DotProduct(TriangleNormal, SafeNormal) > 0.0f)
		{
			Swap(B, C);
		}

		const int32 BaseIndex = Buffers.Vertices.Num();
		Buffers.Vertices.Append({ A, B, C });
		Buffers.Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2 });
		Buffers.Normals.Append({ SafeNormal, SafeNormal, SafeNormal });
		Buffers.UVs.Append({
			GetPlanarUV(A, SafeNormal),
			GetPlanarUV(B, SafeNormal),
			GetPlanarUV(C, SafeNormal)
		});
	}

	void AppendQuad(
		FEHBStairMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		bool bDoubleSided = false)
	{
		AppendTriangle(Buffers, A, B, C, DesiredNormal);
		AppendTriangle(Buffers, A, C, D, DesiredNormal);
		if (bDoubleSided)
		{
			AppendTriangle(Buffers, A, C, B, -DesiredNormal);
			AppendTriangle(Buffers, A, D, C, -DesiredNormal);
		}
	}

	FBox GetSafeStaticMeshBounds(UStaticMesh* StaticMesh)
	{
		if (StaticMesh)
		{
			const FBox Bounds = StaticMesh->GetBounds().GetBox();
			if (Bounds.IsValid)
			{
				return Bounds;
			}
		}
		return FBox(FVector(-50.0f), FVector(50.0f));
	}

	float GetSafeSourceSize(float SourceSize)
	{
		return FMath::Max(0.1f, FMath::Abs(SourceSize));
	}

	FRotator MakeUprightRailingPostRotation(const FVector& Forward, const FVector& OutwardRight)
	{
		const FVector HorizontalForward(Forward.X, Forward.Y, 0.0f);
		FVector HorizontalOutward(OutwardRight.X, OutwardRight.Y, 0.0f);
		HorizontalOutward = HorizontalOutward.GetSafeNormal();
		if (HorizontalOutward.IsNearlyZero())
		{
			HorizontalOutward = FVector::CrossProduct(
				FVector::UpVector,
				HorizontalForward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector)).GetSafeNormal(
					UE_SMALL_NUMBER,
					FVector::RightVector);
		}

		const FVector PostForward = FVector::CrossProduct(HorizontalOutward, FVector::UpVector)
			.GetSafeNormal(UE_SMALL_NUMBER, HorizontalForward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
		return FRotationMatrix::MakeFromXY(PostForward, HorizontalOutward).Rotator();
	}

	UStaticMesh* ResolveStairRailingPostMesh(const TSoftObjectPtr<UStaticMesh>& Mesh)
	{
		if (UStaticMesh* LoadedMesh = Mesh.LoadSynchronous())
		{
			return LoadedMesh;
		}
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))).LoadSynchronous();
	}

	void AppendRailTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& InA,
		const FVector& InB,
		const FVector& InC,
		const FVector& DesiredNormal,
		float U0,
		float U1)
	{
		FVector A = InA;
		FVector B = InB;
		FVector C = InC;
		const FVector SafeNormal = DesiredNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (!TriangleNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, SafeNormal) > 0.0f)
		{
			Swap(B, C);
		}

		const int32 BaseIndex = Vertices.Num();
		Vertices.Append({ A, B, C });
		Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2 });
		Normals.Append({ SafeNormal, SafeNormal, SafeNormal });
		UVs.Append({ FVector2D(U0, 0.0f), FVector2D(U1, 0.0f), FVector2D(U1, 1.0f) });
	}

	void AppendRailQuad(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		float U0,
		float U1)
	{
		AppendRailTriangle(Vertices, Triangles, Normals, UVs, A, B, C, DesiredNormal, U0, U1);
		AppendRailTriangle(Vertices, Triangles, Normals, UVs, A, C, D, DesiredNormal, U0, U1);
	}

	void BuildRailCrossSection(
		const FVector& Center,
		const FVector& Forward,
		const FVector& Right,
		float HalfWidth,
		float HalfHeight,
		FVector OutCorners[4])
	{
		const FVector SafeForward = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		FVector SafeUp = FVector::UpVector;
		if (FMath::Abs(FVector::DotProduct(SafeUp, SafeForward)) > 0.98f)
		{
			SafeUp = FVector::CrossProduct(SafeForward, FVector::RightVector)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		}
		FVector SafeRight = (Right - SafeUp * FVector::DotProduct(Right, SafeUp)).GetSafeNormal();
		if (SafeRight.IsNearlyZero())
		{
			SafeRight = FVector::CrossProduct(SafeUp, SafeForward)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		}

		OutCorners[0] = Center - SafeRight * HalfWidth - SafeUp * HalfHeight;
		OutCorners[1] = Center + SafeRight * HalfWidth - SafeUp * HalfHeight;
		OutCorners[2] = Center + SafeRight * HalfWidth + SafeUp * HalfHeight;
		OutCorners[3] = Center - SafeRight * HalfWidth + SafeUp * HalfHeight;
	}

	void AppendRailSweepSegment(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& Start,
		const FVector& StartForward,
		const FVector& StartRight,
		const FVector& End,
		const FVector& EndForward,
		const FVector& EndRight,
		float HalfWidth,
		float HalfHeight,
		float U0,
		float U1)
	{
		FVector StartCorners[4];
		FVector EndCorners[4];
		BuildRailCrossSection(Start, StartForward, StartRight, HalfWidth, HalfHeight, StartCorners);
		BuildRailCrossSection(End, EndForward, EndRight, HalfWidth, HalfHeight, EndCorners);

		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 NextIndex = (Index + 1) % 4;
			const FVector StartFaceCenter = (StartCorners[Index] + StartCorners[NextIndex]) * 0.5f;
			const FVector EndFaceCenter = (EndCorners[Index] + EndCorners[NextIndex]) * 0.5f;
			const FVector FaceNormal = ((StartFaceCenter - Start) + (EndFaceCenter - End)).GetSafeNormal(
				UE_SMALL_NUMBER,
				FVector::UpVector);
			AppendRailQuad(
				Vertices,
				Triangles,
				Normals,
				UVs,
				StartCorners[Index],
				EndCorners[Index],
				EndCorners[NextIndex],
				StartCorners[NextIndex],
				FaceNormal,
				U0,
				U1);
		}
	}

	FEHBStairPathFrame GetBasePathFrame(const FEHBStairData& Data, float StairLength, float Distance)
	{
		FEHBStairPathFrame Frame;
		const float SafeLength = FMath::Max(1.0f, StairLength);
		const float ClampedDistance = FMath::Clamp(Distance, 0.0f, SafeLength);
		const float TopStraightDistance = FMath::Clamp(
			ResolveDimension(Data.bUseActualDimensions, Data.TreadDepth, Data.DefaultTreadDepth),
			0.0f,
			FMath::Max(0.0f, SafeLength - 1.0f));
		if (ClampedDistance <= TopStraightDistance)
		{
			Frame.Center = FVector(ClampedDistance, 0.0f, 0.0f);
			return Frame;
		}

		const float CurvedLength = FMath::Max(1.0f, SafeLength - TopStraightDistance);
		const float Alpha = (ClampedDistance - TopStraightDistance) / CurvedLength;
		const FVector2D StartPoint(TopStraightDistance, 0.0f);
		const FVector2D EndPoint = Data.bBottomStepControlInitialized
			? Data.BottomStepLocation
			: FVector2D(SafeLength, 0.0f);
		const float ChordLength = FMath::Max(10.0f, FVector2D::Distance(StartPoint, EndPoint));
		const float EndBaseYaw = FMath::RadiansToDegrees(FMath::Atan2(EndPoint.Y, EndPoint.X));
		const FVector2D StartTangent(ChordLength, 0.0f);
		const FVector2D EndTangent =
			FVector2D(FMath::Cos(FMath::DegreesToRadians(EndBaseYaw + Data.BottomStepYawOffset)),
				FMath::Sin(FMath::DegreesToRadians(EndBaseYaw + Data.BottomStepYawOffset)))
			* ChordLength;
		const float Alpha2 = Alpha * Alpha;
		const float Alpha3 = Alpha2 * Alpha;
		const float H00 = 2.0f * Alpha3 - 3.0f * Alpha2 + 1.0f;
		const float H10 = Alpha3 - 2.0f * Alpha2 + Alpha;
		const float H01 = -2.0f * Alpha3 + 3.0f * Alpha2;
		const float H11 = Alpha3 - Alpha2;
		const FVector2D Position = H00 * StartPoint + H10 * StartTangent + H01 * EndPoint + H11 * EndTangent;

		const float DH10 = 3.0f * Alpha2 - 4.0f * Alpha + 1.0f;
		const float DH01 = -6.0f * Alpha2 + 6.0f * Alpha;
		const float DH11 = 3.0f * Alpha2 - 2.0f * Alpha;
		const FVector2D Derivative = DH10 * StartTangent + DH01 * EndPoint + DH11 * EndTangent;
		Frame.Center = FVector(Position.X, Position.Y, 0.0f);
		Frame.Forward = FVector(Derivative.X, Derivative.Y, 0.0f).GetSafeNormal(
			UE_SMALL_NUMBER,
			FVector::ForwardVector);
		Frame.Right = FVector(-Frame.Forward.Y, Frame.Forward.X, 0.0f);
		return Frame;
	}

	FVector2D ClampSplineTangent(
		const FVector2D& Tangent,
		float ParameterSpan,
		float MaxHandleLength)
	{
		const float HandleLength = Tangent.Size() * ParameterSpan / 3.0f;
		if (HandleLength <= MaxHandleLength || HandleLength <= UE_SMALL_NUMBER)
		{
			return Tangent;
		}
		return Tangent * (MaxHandleLength / HandleLength);
	}

	FEHBStairPathFrame GetControlledPathFrame(
		const FEHBStairData& Data,
		float StairLength,
		float Distance,
		float TopStraightDistance)
	{
		const float SafeLength = FMath::Max(1.0f, StairLength);
		const float ClampedDistance = FMath::Clamp(Distance, TopStraightDistance, SafeLength);
		const float Alpha = ClampedDistance / SafeLength;
		const float Ratios[] = {
			FMath::Clamp(TopStraightDistance / SafeLength, 0.0f, 0.249f),
			0.25f,
			0.5f,
			0.75f,
			1.0f
		};
		FVector2D Points[5];
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const FEHBStairPathFrame BaseFrame = GetBasePathFrame(Data, SafeLength, SafeLength * Ratios[Index]);
			Points[Index] = FVector2D(BaseFrame.Center.X, BaseFrame.Center.Y);
			if (Data.bUseIntermediateControls && Index > 0 && Index < 4 && Data.IntermediateControlOffsets.IsValidIndex(Index - 1))
			{
				Points[Index] += Data.IntermediateControlOffsets[Index - 1];
			}
		}

		FVector2D Tangents[5];
		const float TotalChordLength = FMath::Max(10.0f, FVector2D::Distance(Points[0], Points[4]));
		Tangents[0] = FVector2D(TotalChordLength, 0.0f);
		const FEHBStairPathFrame BottomBaseFrame = GetBasePathFrame(Data, SafeLength, SafeLength);
		Tangents[4] = FVector2D(BottomBaseFrame.Forward.X, BottomBaseFrame.Forward.Y) * TotalChordLength;
		for (int32 Index = 1; Index < 4; ++Index)
		{
			const float ParameterSpan = FMath::Max(UE_SMALL_NUMBER, Ratios[Index + 1] - Ratios[Index - 1]);
			Tangents[Index] = (Points[Index + 1] - Points[Index - 1]) / ParameterSpan;
		}

		int32 SegmentIndex = 0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Alpha <= Ratios[Index + 1] || Index == 3)
			{
				SegmentIndex = Index;
				break;
			}
		}

		const float SegmentSpan = FMath::Max(UE_SMALL_NUMBER, Ratios[SegmentIndex + 1] - Ratios[SegmentIndex]);
		const float T = FMath::Clamp((Alpha - Ratios[SegmentIndex]) / SegmentSpan, 0.0f, 1.0f);
		const FVector2D SegmentDelta = Points[SegmentIndex + 1] - Points[SegmentIndex];
		const float MaxHandleLength = FMath::Max(5.0f, SegmentDelta.Size() * 0.65f);
		const FVector2D Tangent0 = ClampSplineTangent(
			Tangents[SegmentIndex],
			SegmentSpan,
			MaxHandleLength);
		const FVector2D Tangent1 = ClampSplineTangent(
			Tangents[SegmentIndex + 1],
			SegmentSpan,
			MaxHandleLength);

		const float T2 = T * T;
		const float T3 = T2 * T;
		const float H00 = 2.0f * T3 - 3.0f * T2 + 1.0f;
		const float H10 = T3 - 2.0f * T2 + T;
		const float H01 = -2.0f * T3 + 3.0f * T2;
		const float H11 = T3 - T2;
		const FVector2D Position =
			H00 * Points[SegmentIndex]
			+ H10 * SegmentSpan * Tangent0
			+ H01 * Points[SegmentIndex + 1]
			+ H11 * SegmentSpan * Tangent1;

		const float DH00 = 6.0f * T2 - 6.0f * T;
		const float DH10 = 3.0f * T2 - 4.0f * T + 1.0f;
		const float DH01 = -6.0f * T2 + 6.0f * T;
		const float DH11 = 3.0f * T2 - 2.0f * T;
		const FVector2D Derivative =
			DH00 * Points[SegmentIndex]
			+ DH10 * SegmentSpan * Tangent0
			+ DH01 * Points[SegmentIndex + 1]
			+ DH11 * SegmentSpan * Tangent1;

		FEHBStairPathFrame Frame;
		Frame.Center = FVector(Position.X, Position.Y, 0.0f);
		Frame.Forward = FVector(Derivative.X, Derivative.Y, 0.0f).GetSafeNormal(
			UE_SMALL_NUMBER,
			GetBasePathFrame(Data, SafeLength, ClampedDistance).Forward);
		Frame.Right = FVector(-Frame.Forward.Y, Frame.Forward.X, 0.0f);
		return Frame;
	}

	FEHBStairPathFrame GetPathFrame(const FEHBStairData& Data, float StairLength, float Distance)
	{
		const float SafeLength = FMath::Max(1.0f, StairLength);
		const float ClampedDistance = FMath::Clamp(Distance, 0.0f, SafeLength);
		const float TopStraightDistance = FMath::Clamp(
			ResolveDimension(Data.bUseActualDimensions, Data.TreadDepth, Data.DefaultTreadDepth),
			0.0f,
			FMath::Max(0.0f, SafeLength - 1.0f));
		if (ClampedDistance <= TopStraightDistance)
		{
			return GetBasePathFrame(Data, SafeLength, ClampedDistance);
		}

		return GetControlledPathFrame(Data, SafeLength, ClampedDistance, TopStraightDistance);
	}

	FVector TransformPathPoint(const FEHBStairData& Data, float StairLength, const FVector& Point)
	{
		const FEHBStairPathFrame Frame = GetPathFrame(Data, StairLength, Point.X);
		return Frame.Center
			+ (Point.X < 0.0f ? Frame.Forward * Point.X : FVector::ZeroVector)
			+ Frame.Right * Point.Y
			+ FVector::UpVector * Point.Z;
	}

	FVector TransformTopExtendedPathPoint(
		const FEHBStairData& Data,
		float StairLength,
		float Distance,
		float LateralOffset,
		float Height)
	{
		const float SafeLength = FMath::Max(1.0f, StairLength);
		const float PathDistance = FMath::Clamp(Distance, 0.0f, SafeLength);
		const FVector PathPoint = TransformPathPoint(Data, SafeLength, FVector(PathDistance, LateralOffset, Height));
		if (Distance >= 0.0f)
		{
			return PathPoint;
		}

		const FEHBStairPathFrame TopFrame = GetPathFrame(Data, SafeLength, 0.0f);
		return PathPoint + TopFrame.Forward * Distance;
	}

	float GetDistanceSquaredToSegmentXY(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector2D Segment(B.X - A.X, B.Y - A.Y);
		const float SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= UE_SMALL_NUMBER)
		{
			return FVector::DistSquared2D(Point, A);
		}

		const FVector2D ToPoint(Point.X - A.X, Point.Y - A.Y);
		const float Alpha = FMath::Clamp(FVector2D::DotProduct(ToPoint, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
		const FVector Closest(A.X + Segment.X * Alpha, A.Y + Segment.Y * Alpha, Point.Z);
		return FVector::DistSquared2D(Point, Closest);
	}

	float GetDistanceSquaredToPolygonEdgesXY(const FVector& Point, const TArray<FVector>& Polygon)
	{
		if (Polygon.Num() < 2)
		{
			return TNumericLimits<float>::Max();
		}

		float BestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[(Index + 1) % Polygon.Num()];
			BestDistanceSquared = FMath::Min(BestDistanceSquared, GetDistanceSquaredToSegmentXY(Point, A, B));
		}
		return BestDistanceSquared;
	}

	void AppendTread(
		const FEHBStairData& Data,
		float StairLength,
		float BackDistance,
		float FrontDistance,
		float HalfWidth,
		float TopZ,
		FEHBStairMeshBuffers& Buffers)
	{
		const float Thickness = FMath::Max(0.1f, Data.PanelThickness);
		const FEHBStairPathFrame BackFrame = GetPathFrame(Data, StairLength, BackDistance);
		const FEHBStairPathFrame FrontFrame = GetPathFrame(Data, StairLength, FrontDistance);

		const FVector LeftBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, -HalfWidth, TopZ));
		const FVector RightBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, HalfWidth, TopZ));
		const FVector RightFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, HalfWidth, TopZ));
		const FVector LeftFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, -HalfWidth, TopZ));
		const FVector LeftBackUnder = LeftBack - FVector::UpVector * Thickness;
		const FVector RightBackUnder = RightBack - FVector::UpVector * Thickness;
		const FVector RightFrontUnder = RightFront - FVector::UpVector * Thickness;
		const FVector LeftFrontUnder = LeftFront - FVector::UpVector * Thickness;

		AppendQuad(Buffers, LeftFront, RightFront, RightBack, LeftBack, FVector::UpVector);
		AppendQuad(Buffers, LeftBackUnder, RightBackUnder, RightFrontUnder, LeftFrontUnder, FVector::DownVector);
		AppendQuad(Buffers, LeftFront, LeftFrontUnder, RightFrontUnder, RightFront, FrontFrame.Forward);
		AppendQuad(Buffers, RightBack, RightBackUnder, LeftBackUnder, LeftBack, -BackFrame.Forward);
		AppendQuad(Buffers, RightFront, RightFrontUnder, RightBackUnder, RightBack, FrontFrame.Right);
		AppendQuad(Buffers, LeftBack, LeftBackUnder, LeftFrontUnder, LeftFront, -BackFrame.Right);
	}

	void AppendRiser(
		const FEHBStairData& Data,
		float StairLength,
		float Distance,
		float HalfWidth,
		float TopZ,
		float BottomZ,
		FEHBStairMeshBuffers& Buffers)
	{
		const FEHBStairPathFrame Frame = GetPathFrame(Data, StairLength, Distance);
		const FVector LeftTop = TransformPathPoint(Data, StairLength, FVector(Distance, -HalfWidth, TopZ));
		const FVector LeftBottom = TransformPathPoint(Data, StairLength, FVector(Distance, -HalfWidth, BottomZ));
		const FVector RightBottom = TransformPathPoint(Data, StairLength, FVector(Distance, HalfWidth, BottomZ));
		const FVector RightTop = TransformPathPoint(Data, StairLength, FVector(Distance, HalfWidth, TopZ));
		AppendQuad(Buffers, LeftTop, LeftBottom, RightBottom, RightTop, Frame.Forward, true);
	}

	void AppendSideBoardSpanMesh(
		const FEHBStairData& Data,
		float StairLength,
		float BackDistance,
		float FrontDistance,
		float HalfWidth,
		float BackTopZ,
		float FrontTopZ,
		float BackBottomZ,
		float FrontBottomZ,
		float DirectionSign,
		FEHBStairMeshBuffers& Buffers,
		bool bCreateBackCap = true,
		bool bCreateFrontCap = true)
	{
		const float Thickness = FMath::Max(0.1f, Data.SideThickness);
		const float InnerY = DirectionSign * HalfWidth;
		const float OuterY = DirectionSign * (HalfWidth + Thickness);
		const FEHBStairPathFrame BackFrame = GetPathFrame(Data, StairLength, BackDistance);
		const FEHBStairPathFrame FrontFrame = GetPathFrame(Data, StairLength, FrontDistance);
		const FVector SideNormal = ((BackFrame.Right + FrontFrame.Right) * 0.5f).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);

		const FVector InnerTopBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, InnerY, BackTopZ));
		const FVector InnerTopFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, InnerY, FrontTopZ));
		const FVector InnerBottomFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, InnerY, FrontBottomZ));
		const FVector InnerBottomBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, InnerY, BackBottomZ));
		const FVector OuterTopBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, OuterY, BackTopZ));
		const FVector OuterTopFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, OuterY, FrontTopZ));
		const FVector OuterBottomFront = TransformPathPoint(Data, StairLength, FVector(FrontDistance, OuterY, FrontBottomZ));
		const FVector OuterBottomBack = TransformPathPoint(Data, StairLength, FVector(BackDistance, OuterY, BackBottomZ));
		const FVector TopNormal = FVector::CrossProduct(
			InnerTopFront - InnerTopBack,
			SideNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

		AppendQuad(Buffers, InnerTopBack, InnerTopFront, InnerBottomFront, InnerBottomBack, -DirectionSign * SideNormal);
		AppendQuad(Buffers, OuterTopFront, OuterTopBack, OuterBottomBack, OuterBottomFront, DirectionSign * SideNormal);
		AppendQuad(Buffers, InnerTopFront, InnerTopBack, OuterTopBack, OuterTopFront, TopNormal);
		AppendQuad(Buffers, InnerBottomBack, InnerBottomFront, OuterBottomFront, OuterBottomBack, -TopNormal);
		if (bCreateBackCap)
		{
			AppendQuad(Buffers, InnerTopBack, InnerBottomBack, OuterBottomBack, OuterTopBack, -BackFrame.Forward);
		}
		if (bCreateFrontCap)
		{
			AppendQuad(Buffers, InnerTopFront, OuterTopFront, OuterBottomFront, InnerBottomFront, FrontFrame.Forward);
		}
	}

	void AppendSideBoardSpan(
		const FEHBStairData& Data,
		float StairLength,
		float BackDistance,
		float FrontDistance,
		float HalfWidth,
		float BackTopZ,
		float FrontTopZ,
		float DirectionSign,
		FEHBStairMeshBuffers& Buffers,
		bool bCreateBackCap = true,
		bool bCreateFrontCap = true)
	{
		const float BoardHeight = FMath::Max(1.0f, Data.SideBoardHeight);
		const float BackRawBottomZ = BackTopZ - BoardHeight;
		const float FrontRawBottomZ = FrontTopZ - BoardHeight;
		constexpr float GroundZ = 0.0f;

		if (BackRawBottomZ > GroundZ + UE_KINDA_SMALL_NUMBER
			&& FrontRawBottomZ < GroundZ - UE_KINDA_SMALL_NUMBER
			&& !FMath::IsNearlyEqual(BackRawBottomZ, FrontRawBottomZ))
		{
			const float CrossingAlpha = FMath::Clamp(
				(GroundZ - BackRawBottomZ) / (FrontRawBottomZ - BackRawBottomZ),
				0.0f,
				1.0f);
			const float CrossingDistance = FMath::Lerp(BackDistance, FrontDistance, CrossingAlpha);
			const float CrossingTopZ = FMath::Lerp(BackTopZ, FrontTopZ, CrossingAlpha);
			AppendSideBoardSpanMesh(
				Data,
				StairLength,
				BackDistance,
				CrossingDistance,
				HalfWidth,
				BackTopZ,
				CrossingTopZ,
				BackRawBottomZ,
				GroundZ,
				DirectionSign,
				Buffers,
				bCreateBackCap,
				false);
			AppendSideBoardSpanMesh(
				Data,
				StairLength,
				CrossingDistance,
				FrontDistance,
				HalfWidth,
				CrossingTopZ,
				FrontTopZ,
				GroundZ,
				GroundZ,
				DirectionSign,
				Buffers,
				false,
				bCreateFrontCap);
			return;
		}

		if (BackRawBottomZ < GroundZ - UE_KINDA_SMALL_NUMBER
			&& FrontRawBottomZ > GroundZ + UE_KINDA_SMALL_NUMBER
			&& !FMath::IsNearlyEqual(BackRawBottomZ, FrontRawBottomZ))
		{
			const float CrossingAlpha = FMath::Clamp(
				(GroundZ - BackRawBottomZ) / (FrontRawBottomZ - BackRawBottomZ),
				0.0f,
				1.0f);
			const float CrossingDistance = FMath::Lerp(BackDistance, FrontDistance, CrossingAlpha);
			const float CrossingTopZ = FMath::Lerp(BackTopZ, FrontTopZ, CrossingAlpha);
			AppendSideBoardSpanMesh(
				Data,
				StairLength,
				BackDistance,
				CrossingDistance,
				HalfWidth,
				BackTopZ,
				CrossingTopZ,
				GroundZ,
				GroundZ,
				DirectionSign,
				Buffers,
				bCreateBackCap,
				false);
			AppendSideBoardSpanMesh(
				Data,
				StairLength,
				CrossingDistance,
				FrontDistance,
				HalfWidth,
				CrossingTopZ,
				FrontTopZ,
				GroundZ,
				FrontRawBottomZ,
				DirectionSign,
				Buffers,
				false,
				bCreateFrontCap);
			return;
		}

		AppendSideBoardSpanMesh(
			Data,
			StairLength,
			BackDistance,
			FrontDistance,
			HalfWidth,
			BackTopZ,
			FrontTopZ,
			FMath::Max(GroundZ, BackRawBottomZ),
			FMath::Max(GroundZ, FrontRawBottomZ),
			DirectionSign,
			Buffers,
			bCreateBackCap,
			bCreateFrontCap);
	}

	void AppendSideBoardSegment(
		const FEHBStairData& Data,
		float StairLength,
		float BackDistance,
		float FrontDistance,
		float HalfWidth,
		float StepTopZ,
		float StepHeight,
		float StepRun,
		float MaxTopZ,
		float DirectionSign,
		FEHBStairMeshBuffers& Buffers)
	{
		const float BoardTopOffset = FMath::Max(0.0f, Data.SideBoardTopOffset);
		const float SafeStepRun = FMath::Max(1.0f, StepRun);
		const float FrontStepAlpha = FMath::Max(0.0f, (FrontDistance - BackDistance) / SafeStepRun);
		const float SegmentRise = StepHeight * FrontStepAlpha;
		const float RawBackTopZ = StepTopZ + BoardTopOffset + SegmentRise;
		const float RawFrontTopZ = StepTopZ + BoardTopOffset;

		if (MaxTopZ <= 0.0f || RawBackTopZ <= MaxTopZ || FMath::IsNearlyEqual(RawBackTopZ, RawFrontTopZ))
		{
			AppendSideBoardSpan(
				Data,
				StairLength,
				BackDistance,
				FrontDistance,
				HalfWidth,
				RawBackTopZ,
				RawFrontTopZ,
				DirectionSign,
				Buffers);
			return;
		}

		if (RawFrontTopZ > MaxTopZ + UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float CrossingAlpha = FMath::Clamp(
			(MaxTopZ - RawBackTopZ) / (RawFrontTopZ - RawBackTopZ),
			0.0f,
			1.0f);
		const float CrossingDistance = FMath::Lerp(BackDistance, FrontDistance, CrossingAlpha);

		// Keep the landing extension, but retain a boundary at path distance zero.
		// A single cap quad from the negative extension to the slope crossing skips
		// that semantic/path boundary. The two spans share an open internal seam.
		float CapStartDistance = BackDistance;
		bool bCreateCapBack = true;
		if (BackDistance < -UE_KINDA_SMALL_NUMBER && CrossingDistance > UE_KINDA_SMALL_NUMBER)
		{
			AppendSideBoardSpan(Data, StairLength, BackDistance, 0.0f, HalfWidth,
				MaxTopZ, MaxTopZ, DirectionSign, Buffers, true, false);
			CapStartDistance = 0.0f;
			bCreateCapBack = false;
		}
		AppendSideBoardSpan(
			Data,
			StairLength,
			CapStartDistance,
			CrossingDistance,
			HalfWidth,
			MaxTopZ,
			MaxTopZ,
			DirectionSign,
			Buffers,
			bCreateCapBack,
			false);
		AppendSideBoardSpan(
			Data,
			StairLength,
			CrossingDistance,
			FrontDistance,
			HalfWidth,
			MaxTopZ,
			RawFrontTopZ,
			DirectionSign,
			Buffers,
			false,
			true);
	}

	void AppendSideGuards(
		const FEHBStairData& Data,
		float StairLength,
		float HalfWidth,
		float StairHeight,
		float LastStepHeight,
		int32 SegmentCount,
		FEHBStairMeshBuffers& Buffers)
	{
		const float GuardThickness = FMath::Max(0.1f, Data.SideGuardThickness);
		const float GuardHeight = FMath::Max(1.0f, Data.SideGuardHeight);
		const float ExistingSideOffset = Data.bGenerateSides ? FMath::Max(0.1f, Data.SideThickness) : 0.0f;
		const int32 SafeSegmentCount = FMath::Max(1, SegmentCount);

		for (const float DirectionSign : { -1.0f, 1.0f })
		{
			const float InnerY = DirectionSign * (HalfWidth + ExistingSideOffset);
			const float OuterY = DirectionSign * (HalfWidth + ExistingSideOffset + GuardThickness);

			for (int32 Index = 0; Index < SafeSegmentCount; ++Index)
			{
				const float Alpha0 = static_cast<float>(Index) / static_cast<float>(SafeSegmentCount);
				const float Alpha1 = static_cast<float>(Index + 1) / static_cast<float>(SafeSegmentCount);
				const float Distance0 = StairLength * Alpha0;
				const float Distance1 = StairLength * Alpha1;
				const float TopZ0 = FMath::Lerp(StairHeight, LastStepHeight, Alpha0) + Data.SideGuardTopOffset;
				const float TopZ1 = FMath::Lerp(StairHeight, LastStepHeight, Alpha1) + Data.SideGuardTopOffset;
				const float BottomZ0 = TopZ0 - GuardHeight;
				const float BottomZ1 = TopZ1 - GuardHeight;
				const FEHBStairPathFrame Frame0 = GetPathFrame(Data, StairLength, Distance0);
				const FEHBStairPathFrame Frame1 = GetPathFrame(Data, StairLength, Distance1);
				const FVector SideNormal = ((Frame0.Right + Frame1.Right) * 0.5f).GetSafeNormal(
					UE_SMALL_NUMBER,
					FVector::RightVector);

				const FVector InnerTop0 = TransformPathPoint(Data, StairLength, FVector(Distance0, InnerY, TopZ0));
				const FVector InnerTop1 = TransformPathPoint(Data, StairLength, FVector(Distance1, InnerY, TopZ1));
				const FVector InnerBottom1 = TransformPathPoint(Data, StairLength, FVector(Distance1, InnerY, BottomZ1));
				const FVector InnerBottom0 = TransformPathPoint(Data, StairLength, FVector(Distance0, InnerY, BottomZ0));
				const FVector OuterTop0 = TransformPathPoint(Data, StairLength, FVector(Distance0, OuterY, TopZ0));
				const FVector OuterTop1 = TransformPathPoint(Data, StairLength, FVector(Distance1, OuterY, TopZ1));
				const FVector OuterBottom1 = TransformPathPoint(Data, StairLength, FVector(Distance1, OuterY, BottomZ1));
				const FVector OuterBottom0 = TransformPathPoint(Data, StairLength, FVector(Distance0, OuterY, BottomZ0));
				const FVector TopNormal = FVector::CrossProduct(
					InnerTop1 - InnerTop0,
					SideNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

				AppendQuad(Buffers, InnerTop0, InnerTop1, InnerBottom1, InnerBottom0, -DirectionSign * SideNormal);
				AppendQuad(Buffers, OuterTop1, OuterTop0, OuterBottom0, OuterBottom1, DirectionSign * SideNormal);
				AppendQuad(Buffers, InnerTop1, InnerTop0, OuterTop0, OuterTop1, TopNormal);
				AppendQuad(Buffers, InnerBottom0, InnerBottom1, OuterBottom1, OuterBottom0, -TopNormal);

				if (Index == 0)
				{
					AppendQuad(Buffers, InnerTop0, InnerBottom0, OuterBottom0, OuterTop0, -Frame0.Forward);
				}
				if (Index == SafeSegmentCount - 1)
				{
					AppendQuad(Buffers, InnerTop1, OuterTop1, OuterBottom1, InnerBottom1, Frame1.Forward);
				}
			}
		}
	}

	void AppendBottomFill(
		const FEHBStairData& Data,
		float StairLength,
		float HalfWidth,
		int32 SegmentCount,
		FEHBStairMeshBuffers& Buffers)
	{
		const float TopStartZ = FMath::Max(0.0f, Data.StairHeight - FMath::Max(0.1f, Data.PanelThickness));
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const float Alpha0 = static_cast<float>(Index) / static_cast<float>(SegmentCount);
			const float Alpha1 = static_cast<float>(Index + 1) / static_cast<float>(SegmentCount);
			const float Distance0 = StairLength * Alpha0;
			const float Distance1 = StairLength * Alpha1;
			const float TopZ0 = FMath::Lerp(TopStartZ, 0.0f, Alpha0);
			const float TopZ1 = FMath::Lerp(TopStartZ, 0.0f, Alpha1);
			const FEHBStairPathFrame Frame0 = GetPathFrame(Data, StairLength, Distance0);
			const FEHBStairPathFrame Frame1 = GetPathFrame(Data, StairLength, Distance1);

			const FVector LeftTop0 = TransformPathPoint(Data, StairLength, FVector(Distance0, -HalfWidth, TopZ0));
			const FVector RightTop0 = TransformPathPoint(Data, StairLength, FVector(Distance0, HalfWidth, TopZ0));
			const FVector LeftTop1 = TransformPathPoint(Data, StairLength, FVector(Distance1, -HalfWidth, TopZ1));
			const FVector RightTop1 = TransformPathPoint(Data, StairLength, FVector(Distance1, HalfWidth, TopZ1));
			const FVector LeftBottom0 = TransformPathPoint(Data, StairLength, FVector(Distance0, -HalfWidth, 0.0f));
			const FVector RightBottom0 = TransformPathPoint(Data, StairLength, FVector(Distance0, HalfWidth, 0.0f));
			const FVector LeftBottom1 = TransformPathPoint(Data, StairLength, FVector(Distance1, -HalfWidth, 0.0f));
			const FVector RightBottom1 = TransformPathPoint(Data, StairLength, FVector(Distance1, HalfWidth, 0.0f));

			AppendQuad(Buffers, LeftTop0, RightTop0, RightTop1, LeftTop1, FVector::DownVector);
			AppendQuad(Buffers, LeftBottom1, RightBottom1, RightBottom0, LeftBottom0, FVector::DownVector);
			AppendQuad(Buffers, LeftTop1, LeftTop0, LeftBottom0, LeftBottom1, -((Frame0.Right + Frame1.Right) * 0.5f));
			AppendQuad(Buffers, RightTop0, RightTop1, RightBottom1, RightBottom0, (Frame0.Right + Frame1.Right) * 0.5f);

			if (Index == 0)
			{
				AppendQuad(Buffers, LeftTop0, LeftBottom0, RightBottom0, RightTop0, -Frame0.Forward);
			}
		}
	}

	void ApplyMesh(
		UEHBGeneratedMeshComponent* MeshComponent,
		const FEHBStairMeshBuffers& Buffers,
		UMaterialInterface* Material)
	{
		if (!MeshComponent)
		{
			return;
		}

		FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(MeshComponent);
		if (Buffers.Vertices.IsEmpty() || Buffers.Triangles.IsEmpty())
		{
			MeshComponent->ClearAllMeshSections();
			MeshComponent->SetVisibility(false);
			return;
		}

		TArray<FLinearColor> VertexColors;
		VertexColors.Init(FLinearColor::White, Buffers.Vertices.Num());
		TArray<FProcMeshTangent> Tangents;
		Tangents.Init(FProcMeshTangent(), Buffers.Vertices.Num());
		MeshComponent->CreateMeshSection_LinearColor(
			0,
			Buffers.Vertices,
			Buffers.Triangles,
			Buffers.Normals,
			Buffers.UVs,
			VertexColors,
			Tangents,
			true);
		MeshComponent->SetMeshSectionName(0, FName(TEXT("StairMesh")));
		MeshComponent->ClearMeshSectionsFrom(1);
		MeshComponent->SetMaterialIfChanged(0, Material);
		MeshComponent->SetVisibility(true);
	}

	UMaterialInterface* ResolveMaterial(const TSoftObjectPtr<UMaterialInterface>& Material)
	{
		if (UMaterialInterface* LoadedMaterial = Material.LoadSynchronous())
		{
			return LoadedMaterial;
		}

		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		if (Settings)
		{
			if (UMaterialInterface* DefaultMaterial = Settings->DefaultWhiteBoxMaterial.LoadSynchronous())
			{
				return DefaultMaterial;
			}
		}

		return UMaterial::GetDefaultMaterial(MD_Surface);
	}
}

AEHB_Stair::AEHB_Stair()
{
	ElementType = EEHBBuildingElementType::Stair;
	ElementCapabilities = static_cast<int32>(
		EEHBElementCapability::Structural
		| EEHBElementCapability::CanSupport
		| EEHBElementCapability::RequiresSupport);
	SemanticTags.AddUnique(TEXT("Circulation.Vertical"));

	TreadMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("StairTreadMesh"));
	TreadMeshComponent->SetupAttachment(SceneRoot);

	RiserMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("StairRiserMesh"));
	RiserMeshComponent->SetupAttachment(SceneRoot);

	SideMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("StairSideMesh"));
	SideMeshComponent->SetupAttachment(SceneRoot);

	LeftRailingPostMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StairLeftRailingPosts"));
	LeftRailingPostMeshComponent->SetupAttachment(SceneRoot);

	RightRailingPostMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StairRightRailingPosts"));
	RightRailingPostMeshComponent->SetupAttachment(SceneRoot);

	LeftRailingRailMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("StairLeftRailingRails"));
	LeftRailingRailMeshComponent->SetupAttachment(SceneRoot);

	RightRailingRailMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("StairRightRailingRails"));
	RightRailingRailMeshComponent->SetupAttachment(SceneRoot);

	for (UEHBGeneratedMeshComponent* MeshComponent : { TreadMeshComponent, RiserMeshComponent, SideMeshComponent })
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->ComponentTags.AddUnique(TEXT("EHB_Stair"));
	}

	for (UHierarchicalInstancedStaticMeshComponent* PostComponent : { LeftRailingPostMeshComponent, RightRailingPostMeshComponent })
	{
		PostComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PostComponent->SetCollisionObjectType(ECC_WorldStatic);
		PostComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		PostComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		PostComponent->ComponentTags.AddUnique(TEXT("EHB_StairRailing"));
	}

	for (UEHBGeneratedMeshComponent* RailComponent : { LeftRailingRailMeshComponent, RightRailingRailMeshComponent })
	{
		RailComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RailComponent->SetCollisionObjectType(ECC_WorldStatic);
		RailComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		RailComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		RailComponent->ComponentTags.AddUnique(TEXT("EHB_StairRailing"));
	}
}

void AEHB_Stair::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildStairMesh();
}

#if WITH_EDITOR
void AEHB_Stair::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildStairMesh();

	if (OwningBuilding)
	{
		EnsureElementGuid();

		FEHBElementQuery Query;
		Query.ElementTypes = { EEHBBuildingElementType::Railing };
		for (AEHBElementActorBase* Element : OwningBuilding->QueryElements(Query))
		{
			AEHB_Railing* Railing = Cast<AEHB_Railing>(Element);
			if (!Railing
				|| Railing->PathMode != EEHBRailingPathMode::StairHosted
				|| Railing->IsActorBeingDestroyed())
			{
				continue;
			}

			const bool bHostedByThisStair =
				Railing->HostedStair == this
				|| (Railing->HostedStairGuid.IsValid() && Railing->HostedStairGuid == ElementGuid);
			if (!bHostedByThisStair)
			{
				continue;
			}

			Railing->Modify();
			Railing->HostedStair = this;
			Railing->HostedStairGuid = ElementGuid;
			if (Railing->RebuildRailing())
			{
				Railing->MarkPackageDirty();
			}
		}
	}
}
#endif

bool AEHB_Stair::RebuildStairMesh()
{
	if (!TreadMeshComponent || !RiserMeshComponent || !SideMeshComponent)
	{
		return false;
	}

	NormalizeStairData();

	const float TreadDepth = ResolveDimension(StairData.bUseActualDimensions, StairData.TreadDepth, StairData.DefaultTreadDepth);
	const float StairWidth = ResolveDimension(StairData.bUseActualDimensions, StairData.StairWidth, StairData.DefaultStairWidth);
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	if (!CalculateStairs(StairHeight, GeneratedStepCount, GeneratedStepHeight))
	{
		return false;
	}

	const float NosingLength = FMath::Clamp(StairData.NosingLength, 0.0f, FMath::Max(0.0f, TreadDepth - 1.0f));
	const float StepRun = FMath::Max(1.0f, TreadDepth - NosingLength);
	const float StairLength = TreadDepth + StepRun * static_cast<float>(FMath::Max(0, GeneratedStepCount - 1));
	const float TopLandingExtensionDistance = TreadDepth;
	const float HalfWidth = StairWidth * 0.5f;
	const float SideBoardLandingTopZ = ResolveSideBoardLandingTopLocalZ(StairHeight, GeneratedStepHeight);
	if (!StairData.bBottomStepControlInitialized)
	{
		StairData.BottomStepLocation = FVector2D(StairLength, 0.0f);
		StairData.BottomStepYawOffset = 0.0f;
		StairData.bBottomStepControlInitialized = true;
	}

	FEHBStairMeshBuffers TreadBuffers;
	FEHBStairMeshBuffers RiserBuffers;
	FEHBStairMeshBuffers SideBuffers;

	for (int32 StepIndex = 0; StepIndex < GeneratedStepCount; ++StepIndex)
	{
		const float BackDistance = StepRun * static_cast<float>(StepIndex);
		const float TreadBackDistance = StepIndex == 0 ? -TopLandingExtensionDistance : BackDistance;
		const float FrontDistance = FMath::Min(StairLength, BackDistance + TreadDepth);
		const float TopZ = StairHeight - GeneratedStepHeight * static_cast<float>(StepIndex);

		if (StairData.bGenerateTreads)
		{
			AppendTread(StairData, StairLength, TreadBackDistance, FrontDistance, HalfWidth, TopZ, TreadBuffers);
		}

		if (StairData.bFillRisers)
		{
			const float RiserDistance = FMath::Min(StairLength, BackDistance + StepRun);
			AppendRiser(
				StairData,
				StairLength,
				RiserDistance,
				HalfWidth,
				TopZ - StairData.PanelThickness,
				FMath::Max(0.0f, TopZ - GeneratedStepHeight),
				RiserBuffers);
		}

		if (StairData.bGenerateSides)
		{
			const float SideFrontDistance = FMath::Min(
				StairLength,
				BackDistance + StepRun + StairData.SideProtruding);
			AppendSideBoardSegment(
				StairData,
				StairLength,
				TreadBackDistance,
				SideFrontDistance,
				HalfWidth,
				TopZ,
				GeneratedStepHeight,
				StepRun,
				SideBoardLandingTopZ,
				-1.0f,
				SideBuffers);
			AppendSideBoardSegment(
				StairData,
				StairLength,
				TreadBackDistance,
				SideFrontDistance,
				HalfWidth,
				TopZ,
				GeneratedStepHeight,
				StepRun,
				SideBoardLandingTopZ,
				1.0f,
				SideBuffers);
		}
	}

	if (StairData.bFillBottomPart)
	{
		AppendBottomFill(StairData, StairLength, HalfWidth, FMath::Max(1, GeneratedStepCount), RiserBuffers);
	}

	if (StairData.bGenerateSideGuards)
	{
		AppendSideGuards(
			StairData,
			StairLength,
			HalfWidth,
			StairHeight,
			GeneratedStepHeight,
			FMath::Max(1, GeneratedStepCount),
			SideBuffers);
	}

	ApplyMesh(TreadMeshComponent, TreadBuffers, ResolveMaterial(StairData.TreadMaterial));
	ApplyMesh(RiserMeshComponent, RiserBuffers, ResolveMaterial(StairData.RiserMaterial));
	ApplyMesh(SideMeshComponent, SideBuffers, ResolveMaterial(StairData.SideMaterial));
	RebuildEmbeddedRailings();
	return true;
}

void AEHB_Stair::ConfigureDefaultStair(
	AEHBBuildingActorBase* InBuilding,
	const FTransform& LocalTransform,
	float InStairHeight,
	float InStairWidth,
	float InTreadDepth)
{
	StairData.bUseActualDimensions = true;
	StairData.StairHeight = FMath::Max(1.0f, InStairHeight);
	StairData.StairWidth = FMath::Max(1.0f, InStairWidth);
	StairData.TreadDepth = FMath::Max(1.0f, InTreadDepth);
	StairData.bBottomStepControlInitialized = false;
	StairData.bUseIntermediateControls = false;
	ResetIntermediateControlOffsets();
	AttachToBuilding(InBuilding, LocalTransform);
	RebuildStairMesh();
}

bool AEHB_Stair::CalculateStairs(float TotalHeight, int32& OutNumSteps, float& OutStepHeight) const
{
	OutNumSteps = 0;
	OutStepHeight = 0.0f;
	if (TotalHeight <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float MinStepHeight = FMath::Max(1.0f, StairData.MinStepHeight);
	float MaxStepHeight = FMath::Max(1.0f, StairData.MaxStepHeight);
	if (MinStepHeight > MaxStepHeight)
	{
		Swap(MinStepHeight, MaxStepHeight);
	}

	OutNumSteps = FMath::Max(1, FMath::CeilToInt(TotalHeight / MaxStepHeight));
	OutStepHeight = TotalHeight / static_cast<float>(OutNumSteps);
	while (OutNumSteps > 1 && OutStepHeight < MinStepHeight)
	{
		--OutNumSteps;
		OutStepHeight = TotalHeight / static_cast<float>(OutNumSteps);
	}
	return true;
}

bool AEHB_Stair::TryResolveAttachedFloorSlabLandingTopLocalZ(float ExpectedLandingTopZ, float& OutLandingTopLocalZ) const
{
	OutLandingTopLocalZ = 0.0f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FTransform StairTransform = GetActorTransform();
	const FVector TopEdgeWorldLocation = StairTransform.TransformPosition(FVector::ZeroVector);
	const float SearchDistance = FMath::Max(
		120.0f,
		ResolveDimension(StairData.bUseActualDimensions, StairData.StairWidth, StairData.DefaultStairWidth) * 0.5f
		+ FMath::Max(0.1f, StairData.SideThickness)
		+ 60.0f);
	const float SearchDistanceSquared = SearchDistance * SearchDistance;
	const float HeightTolerance = FMath::Max(80.0f, FMath::Max(1.0f, ExpectedLandingTopZ) * 0.25f);
	float BestScore = TNumericLimits<float>::Max();
	bool bFound = false;

	for (TActorIterator<AEHB_FloorSlab> It(World); It; ++It)
	{
		AEHB_FloorSlab* FloorSlab = *It;
		if (!FloorSlab || FloorSlab->IsActorBeingDestroyed())
		{
			continue;
		}
		if (OwningBuilding && FloorSlab->OwningBuilding && FloorSlab->OwningBuilding != OwningBuilding)
		{
			continue;
		}

		TArray<TArray<FVector>> WorldPolygons;
		if (!FloorSlab->BuildEffectiveOuterWorldPolygons(WorldPolygons, true, true))
		{
			continue;
		}

		float EdgeDistanceSquared=TNumericLimits<float>::Max();for(const auto& Polygon:WorldPolygons)EdgeDistanceSquared=FMath::Min(EdgeDistanceSquared,GetDistanceSquaredToPolygonEdgesXY(TopEdgeWorldLocation,Polygon));
		if (EdgeDistanceSquared > SearchDistanceSquared)
		{
			continue;
		}

		const FTransform SlabTransform = FloorSlab->GetActorTransform();
		FVector SlabLocalPoint = SlabTransform.InverseTransformPosition(TopEdgeWorldLocation);
		SlabLocalPoint.Z = FloorSlab->GetTopZ();
		const FVector SlabTopWorldPoint = SlabTransform.TransformPosition(SlabLocalPoint);
		const float CandidateLocalZ = StairTransform.InverseTransformPosition(SlabTopWorldPoint).Z;
		if (CandidateLocalZ <= StairData.SideBoardTopOffset + UE_SMALL_NUMBER)
		{
			continue;
		}

		const float HeightError = FMath::Abs(CandidateLocalZ - ExpectedLandingTopZ);
		if (HeightError > HeightTolerance)
		{
			continue;
		}

		const float Score = EdgeDistanceSquared + HeightError * HeightError;
		if (Score < BestScore)
		{
			BestScore = Score;
			OutLandingTopLocalZ = CandidateLocalZ;
			bFound = true;
		}
	}

	return bFound;
}

float AEHB_Stair::ResolveSideBoardLandingTopLocalZ(float StairHeight, float StepHeight) const
{
	const float ExpectedLandingTopZ = StairHeight + FMath::Max(0.0f, StepHeight);
	float AttachedLandingTopZ = 0.0f;
	if (TryResolveAttachedFloorSlabLandingTopLocalZ(ExpectedLandingTopZ, AttachedLandingTopZ))
	{
		return FMath::Max(StairHeight, AttachedLandingTopZ);
	}

	return ExpectedLandingTopZ;
}

float AEHB_Stair::GetStairLength() const
{
	const float TreadDepth = ResolveDimension(StairData.bUseActualDimensions, StairData.TreadDepth, StairData.DefaultTreadDepth);
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (!CalculateStairs(StairHeight, StepCount, StepHeight))
	{
		return TreadDepth;
	}

	const float NosingLength = FMath::Clamp(StairData.NosingLength, 0.0f, FMath::Max(0.0f, TreadDepth - 1.0f));
	const float StepRun = FMath::Max(1.0f, TreadDepth - NosingLength);
	return TreadDepth + StepRun * static_cast<float>(FMath::Max(0, StepCount - 1));
}

FVector AEHB_Stair::GetBottomControlWorldLocation() const
{
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	CalculateStairs(StairHeight, StepCount, StepHeight);
	const FVector2D LocalXY = StairData.bBottomStepControlInitialized
		? StairData.BottomStepLocation
		: FVector2D(GetStairLength(), 0.0f);
	return GetActorTransform().TransformPosition(FVector(LocalXY.X, LocalXY.Y, StepHeight + 20.0f));
}

FRotator AEHB_Stair::GetBottomControlWorldRotation() const
{
	const FVector2D EndPoint = StairData.bBottomStepControlInitialized
		? StairData.BottomStepLocation
		: FVector2D(GetStairLength(), 0.0f);
	const float LocalYaw = FMath::RadiansToDegrees(FMath::Atan2(EndPoint.Y, EndPoint.X)) + StairData.BottomStepYawOffset;
	return FRotator(0.0f, GetActorRotation().Yaw + LocalYaw, 0.0f);
}

void AEHB_Stair::SetBottomControlWorldLocation(const FVector& WorldLocation)
{
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	FVector2D NewLocation(LocalLocation.X, LocalLocation.Y);
	if (NewLocation.SizeSquared() < 100.0f)
	{
		NewLocation = NewLocation.IsNearlyZero()
			? FVector2D(10.0f, 0.0f)
			: NewLocation.GetSafeNormal() * 10.0f;
	}
	StairData.BottomStepLocation = NewLocation;
	StairData.bBottomStepControlInitialized = true;
	StairData.bUseIntermediateControls = false;
	ResetIntermediateControlOffsets();
	RebuildStairMesh();
	MarkPackageDirty();
}

void AEHB_Stair::AddBottomControlYaw(float DeltaYaw)
{
	if (FMath::IsNearlyZero(DeltaYaw))
	{
		return;
	}
	StairData.BottomStepYawOffset = FRotator::NormalizeAxis(StairData.BottomStepYawOffset + DeltaYaw);
	StairData.bUseIntermediateControls = false;
	ResetIntermediateControlOffsets();
	RebuildStairMesh();
	MarkPackageDirty();
}

FVector AEHB_Stair::GetIntermediateControlWorldLocation(int32 ControlIndex) const
{
	if (ControlIndex < 0 || ControlIndex >= EHBStairIntermediateControlCount)
	{
		return GetActorLocation();
	}

	const float Ratio = static_cast<float>(ControlIndex + 1) / 4.0f;
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	CalculateStairs(StairHeight, StepCount, StepHeight);
	const float ControlZ = FMath::Lerp(StairHeight, StepHeight, Ratio) + 20.0f;
	return GetActorTransform().TransformPosition(
		TransformPathPoint(StairData, GetStairLength(), FVector(GetStairLength() * Ratio, 0.0f, ControlZ)));
}

void AEHB_Stair::SetIntermediateControlWorldLocation(int32 ControlIndex, const FVector& WorldLocation)
{
	if (ControlIndex < 0 || ControlIndex >= EHBStairIntermediateControlCount)
	{
		return;
	}
	if (!StairData.bUseIntermediateControls)
	{
		ResetIntermediateControlOffsets();
		RebuildStairMesh();
		MarkPackageDirty();
		return;
	}

	StairData.IntermediateControlOffsets.SetNum(EHBStairIntermediateControlCount);
	const float Ratio = static_cast<float>(ControlIndex + 1) / 4.0f;
	const FEHBStairPathFrame BaseFrame = GetBasePathFrame(StairData, GetStairLength(), GetStairLength() * Ratio);
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	StairData.IntermediateControlOffsets[ControlIndex] =
		FVector2D(LocalLocation.X - BaseFrame.Center.X, LocalLocation.Y - BaseFrame.Center.Y);
	RebuildStairMesh();
	MarkPackageDirty();
}

void AEHB_Stair::ResetIntermediateControlOffsets()
{
	StairData.IntermediateControlOffsets.Init(FVector2D::ZeroVector, EHBStairIntermediateControlCount);
}

bool AEHB_Stair::SamplePathForRailing(
	EEHBRailingSide Side,
	float Distance,
	float LateralOffset,
	float BaseHeightOffset,
	FEHBStairPathSample& OutSample) const
{
	OutSample = FEHBStairPathSample();

	// 扶手采样复用楼梯自身的路径帧。这样楼梯控制点、底部方向和弯曲逻辑只维护一份。
	const float TreadDepth = ResolveDimension(StairData.bUseActualDimensions, StairData.TreadDepth, StairData.DefaultTreadDepth);
	const float StairWidth = ResolveDimension(StairData.bUseActualDimensions, StairData.StairWidth, StairData.DefaultStairWidth);
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (!CalculateStairs(StairHeight, StepCount, StepHeight) || StepCount <= 0)
	{
		return false;
	}

	const float NosingLength = FMath::Clamp(StairData.NosingLength, 0.0f, FMath::Max(0.0f, TreadDepth - 1.0f));
	const float StepRun = FMath::Max(1.0f, TreadDepth - NosingLength);
	const float StairLength = GetStairLength();
	const float TopLandingPostDistance = -TreadDepth * 0.5f;
	const float ClampedDistance = FMath::Clamp(Distance, TopLandingPostDistance, StairLength);
	const float PathDistance = FMath::Clamp(ClampedDistance, 0.0f, StairLength);
	const float LandingTopZ = ResolveSideBoardLandingTopLocalZ(StairHeight, StepHeight);
	// 当前初版把柱脚放在对应踏步顶面。后续如果要支持斜梁式扶手，可在这里增加连续高度策略。
	const int32 StepIndex = FMath::Clamp(FMath::FloorToInt(ClampedDistance / StepRun), 0, StepCount - 1);
	const float StepTopZ = ClampedDistance < 0.0f || FMath::IsNearlyZero(ClampedDistance, 0.5f)
		? LandingTopZ
		: LandingTopZ - StepHeight * static_cast<float>(StepIndex + 1);
	const float DirectionSign = Side == EEHBRailingSide::Left ? -1.0f : 1.0f;
	const float HalfWidth = StairWidth * 0.5f;
	const float EdgeInset = FMath::Clamp(FMath::Max(0.0f, LateralOffset), 0.0f, HalfWidth);
	const float SideY = DirectionSign * FMath::Max(0.0f, HalfWidth - EdgeInset);
	const FEHBStairPathFrame Frame = GetPathFrame(StairData, StairLength, PathDistance);
	const FVector LocalLocation = TransformTopExtendedPathPoint(
		StairData,
		StairLength,
		ClampedDistance,
		SideY,
		StepTopZ + BaseHeightOffset);
	const float TangentSampleDelta = FMath::Clamp(TreadDepth * 0.5f, 5.0f, FMath::Max(5.0f, StairLength * 0.1f));
	const float PreviousDistance = FMath::Clamp(ClampedDistance - TangentSampleDelta, TopLandingPostDistance, StairLength);
	const float NextDistance = FMath::Clamp(ClampedDistance + TangentSampleDelta, 0.0f, StairLength);
	const float FirstTreadCenterDistance = FMath::Max(1.0f, TreadDepth * 0.5f);
	auto GetSmoothRailPoint = [this, StairLength, StepRun, StepHeight, StepCount, LandingTopZ, TopLandingPostDistance, FirstTreadCenterDistance, SideY, BaseHeightOffset](float SampleDistance)
	{
		const float SmoothPathDistance = FMath::Clamp(SampleDistance, 0.0f, StairLength);
		float SmoothZ = LandingTopZ;
		if (SampleDistance < FirstTreadCenterDistance)
		{
			const float LandingAlpha = FMath::Clamp(
				(SampleDistance - TopLandingPostDistance) / FMath::Max(1.0f, FirstTreadCenterDistance - TopLandingPostDistance),
				0.0f,
				1.0f);
			SmoothZ = FMath::Lerp(LandingTopZ, LandingTopZ - StepHeight, LandingAlpha);
		}
		else
		{
			const float StepCenterAlignedIndex = FMath::Clamp(
				(SmoothPathDistance - FirstTreadCenterDistance) / StepRun,
				0.0f,
				static_cast<float>(StepCount - 1));
			SmoothZ = LandingTopZ - StepHeight * (1.0f + StepCenterAlignedIndex);
		}
		return TransformTopExtendedPathPoint(
			StairData,
			StairLength,
			SampleDistance,
			SideY,
			SmoothZ + BaseHeightOffset);
	};
	const FVector SmoothForward = (GetSmoothRailPoint(NextDistance) - GetSmoothRailPoint(PreviousDistance))
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Forward);
	const FVector PlanarForward = FVector(SmoothForward.X, SmoothForward.Y, 0.0f)
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Forward);

	OutSample.Distance = ClampedDistance;
	OutSample.StepIndex = ClampedDistance < 0.0f ? INDEX_NONE : StepIndex;
	OutSample.StepTopZ = StepTopZ;
	OutSample.LocalLocation = LocalLocation;
	OutSample.LocalForward = SmoothForward;
	OutSample.LocalRight = (FVector::CrossProduct(FVector::UpVector, PlanarForward) * DirectionSign)
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Right * DirectionSign);
	return true;
}

bool AEHB_Stair::SampleRailPathForRailing(
	EEHBRailingSide Side,
	float Distance,
	float LateralOffset,
	float BaseHeightOffset,
	FEHBStairPathSample& OutSample) const
{
	OutSample = FEHBStairPathSample();

	const float TreadDepth = ResolveDimension(StairData.bUseActualDimensions, StairData.TreadDepth, StairData.DefaultTreadDepth);
	const float StairWidth = ResolveDimension(StairData.bUseActualDimensions, StairData.StairWidth, StairData.DefaultStairWidth);
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (!CalculateStairs(StairHeight, StepCount, StepHeight) || StepCount <= 0)
	{
		return false;
	}

	const float NosingLength = FMath::Clamp(StairData.NosingLength, 0.0f, FMath::Max(0.0f, TreadDepth - 1.0f));
	const float StepRun = FMath::Max(1.0f, TreadDepth - NosingLength);
	const float StairLength = GetStairLength();
	const float TopLandingPostDistance = -TreadDepth * 0.5f;
	const float ClampedDistance = FMath::Clamp(Distance, TopLandingPostDistance, StairLength);
	const float PathDistance = FMath::Clamp(ClampedDistance, 0.0f, StairLength);
	const float LandingTopZ = ResolveSideBoardLandingTopLocalZ(StairHeight, StepHeight);
	const int32 StepIndex = FMath::Clamp(FMath::FloorToInt(ClampedDistance / StepRun), 0, StepCount - 1);
	const float DirectionSign = Side == EEHBRailingSide::Left ? -1.0f : 1.0f;
	const float HalfWidth = StairWidth * 0.5f;
	const float EdgeInset = FMath::Clamp(FMath::Max(0.0f, LateralOffset), 0.0f, HalfWidth);
	const float SideY = DirectionSign * FMath::Max(0.0f, HalfWidth - EdgeInset);
	const FEHBStairPathFrame Frame = GetPathFrame(StairData, StairLength, PathDistance);

	const float FirstTreadCenterDistance = FMath::Max(1.0f, TreadDepth * 0.5f);
	auto GetSmoothRailZ = [StepRun, StepHeight, StepCount, LandingTopZ, TopLandingPostDistance, FirstTreadCenterDistance](float SampleDistance)
	{
		const float SmoothPathDistance = FMath::Max(0.0f, SampleDistance);
		if (SampleDistance < FirstTreadCenterDistance)
		{
			const float LandingAlpha = FMath::Clamp(
				(SampleDistance - TopLandingPostDistance) / FMath::Max(1.0f, FirstTreadCenterDistance - TopLandingPostDistance),
				0.0f,
				1.0f);
			return FMath::Lerp(LandingTopZ, LandingTopZ - StepHeight, LandingAlpha);
		}

		const float StepCenterAlignedIndex = FMath::Clamp(
			(SmoothPathDistance - FirstTreadCenterDistance) / StepRun,
			0.0f,
			static_cast<float>(StepCount - 1));
		return LandingTopZ - StepHeight * (1.0f + StepCenterAlignedIndex);
	};
	auto GetSmoothRailPoint = [this, StairLength, SideY, BaseHeightOffset, &GetSmoothRailZ](float SampleDistance)
	{
		return TransformTopExtendedPathPoint(
			StairData,
			StairLength,
			SampleDistance,
			SideY,
			GetSmoothRailZ(SampleDistance) + BaseHeightOffset);
	};

	const FVector LocalLocation = GetSmoothRailPoint(ClampedDistance);
	const float TangentSampleDelta = FMath::Clamp(TreadDepth * 0.5f, 5.0f, FMath::Max(5.0f, StairLength * 0.1f));
	const float PreviousDistance = FMath::Clamp(ClampedDistance - TangentSampleDelta, TopLandingPostDistance, StairLength);
	const float NextDistance = FMath::Clamp(ClampedDistance + TangentSampleDelta, 0.0f, StairLength);
	const FVector SmoothForward = (GetSmoothRailPoint(NextDistance) - GetSmoothRailPoint(PreviousDistance))
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Forward);
	const FVector PlanarForward = FVector(SmoothForward.X, SmoothForward.Y, 0.0f)
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Forward);

	OutSample.Distance = ClampedDistance;
	OutSample.StepIndex = ClampedDistance < 0.0f ? INDEX_NONE : StepIndex;
	OutSample.StepTopZ = GetSmoothRailZ(ClampedDistance);
	OutSample.LocalLocation = LocalLocation;
	OutSample.LocalForward = SmoothForward;
	OutSample.LocalRight = (FVector::CrossProduct(FVector::UpVector, PlanarForward) * DirectionSign)
		.GetSafeNormal(UE_SMALL_NUMBER, Frame.Right * DirectionSign);
	return true;
}

bool AEHB_Stair::BuildRailingPostSamples(
	EEHBRailingSide Side,
	EEHBRailingPostSpacingMode SpacingMode,
	float PostSpacing,
	int32 StepsPerPost,
	float LateralOffset,
	float BaseHeightOffset,
	TArray<FEHBStairRailingPostSample>& OutSamples) const
{
	OutSamples.Reset();

	const float TreadDepth = ResolveDimension(StairData.bUseActualDimensions, StairData.TreadDepth, StairData.DefaultTreadDepth);
	const float StairHeight = ResolveDimension(StairData.bUseActualDimensions, StairData.StairHeight, StairData.DefaultStairHeight);
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (!CalculateStairs(StairHeight, StepCount, StepHeight) || StepCount <= 0)
	{
		return false;
	}

	const float NosingLength = FMath::Clamp(StairData.NosingLength, 0.0f, FMath::Max(0.0f, TreadDepth - 1.0f));
	const float StepRun = FMath::Max(1.0f, TreadDepth - NosingLength);
	const float StairLength = GetStairLength();
	const float TopLandingPostDistance = -TreadDepth * 0.5f;
	const float PostForwardOffset = StairData.RailingPostForwardOffset;
	const float SafePostSpacing = FMath::Max(1.0f, PostSpacing);
	const int32 SafeStepsPerPost = FMath::Max(1, StepsPerPost);
	auto GetStepCenterDistance = [StepRun, TreadDepth, StairLength, SafeStepsPerPost, StepCount](int32 StepIndex)
	{
		const int32 ActualStepIndex = StepIndex == StepCount - 1
			? StepIndex
			: FMath::Min(StepCount - 1, StepIndex + SafeStepsPerPost - 1);
		return FMath::Clamp(StepRun * static_cast<float>(ActualStepIndex) + TreadDepth * 0.5f, 0.0f, StairLength);
	};

	// 只负责产生候选柱位，不处理门洞、Guid 复用或重复点合并；这些属于扶手装配层。
	auto AddSampleAtDistance = [this, Side, LateralOffset, BaseHeightOffset, PostForwardOffset, &OutSamples](float Distance)
	{
		FEHBStairPathSample PathSample;
		if (!SamplePathForRailing(Side, Distance + PostForwardOffset, LateralOffset, BaseHeightOffset, PathSample))
		{
			return;
		}

		if (!OutSamples.IsEmpty() && FMath::Abs(OutSamples.Last().Distance - PathSample.Distance) <= 0.5f)
		{
			return;
		}

		FEHBStairRailingPostSample& PostSample = OutSamples.AddDefaulted_GetRef();
		PostSample.Distance = PathSample.Distance;
		PostSample.StepIndex = PathSample.StepIndex;
		PostSample.LocalBaseLocation = PathSample.LocalLocation;
		PostSample.LocalRotation = MakeUprightRailingPostRotation(PathSample.LocalForward, PathSample.LocalRight);
	};

	if (SpacingMode == EEHBRailingPostSpacingMode::StepAligned)
	{
		AddSampleAtDistance(TopLandingPostDistance);
		// 楼梯扶手默认按踏步对齐，避免柱子落在踏步中间造成视觉不稳定。
		for (int32 StepIndex = 0; StepIndex < StepCount; StepIndex += SafeStepsPerPost)
		{
			AddSampleAtDistance(GetStepCenterDistance(StepIndex));
		}
		AddSampleAtDistance(GetStepCenterDistance(StepCount - 1));
	}
	else
	{
		for (float Distance = 0.0f; Distance < StairLength; Distance += SafePostSpacing)
		{
			AddSampleAtDistance(Distance);
		}
	}

	if (SpacingMode == EEHBRailingPostSpacingMode::StepAligned)
	{
		return !OutSamples.IsEmpty();
	}

	// 无论步距模式如何，末端都必须有一根候选柱。
	AddSampleAtDistance(StairLength);
	if (SpacingMode == EEHBRailingPostSpacingMode::StepAligned)
	{
		return !OutSamples.IsEmpty();
	}
	return !OutSamples.IsEmpty();
}

const TArray<FEHBRailingPost>& AEHB_Stair::GetEmbeddedRailingPosts(EEHBRailingSide Side) const
{
	return Side == EEHBRailingSide::Left ? LeftGeneratedRailingPosts : RightGeneratedRailingPosts;
}

TArray<FEHBRailingPost>& AEHB_Stair::GetMutableEmbeddedRailingPosts(EEHBRailingSide Side)
{
	return Side == EEHBRailingSide::Left ? LeftGeneratedRailingPosts : RightGeneratedRailingPosts;
}

TArray<FGuid>& AEHB_Stair::GetMutableEmbeddedRailingPostInstanceGuids(EEHBRailingSide Side)
{
	return Side == EEHBRailingSide::Left ? LeftRailingPostInstanceGuids : RightRailingPostInstanceGuids;
}

TArray<FGuid>& AEHB_Stair::GetMutableEmbeddedRailingPostOverrideComponentGuids(EEHBRailingSide Side)
{
	return Side == EEHBRailingSide::Left
		? LeftRailingPostOverrideComponentGuids
		: RightRailingPostOverrideComponentGuids;
}

TArray<TObjectPtr<UStaticMeshComponent>>& AEHB_Stair::GetMutableEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side)
{
	return Side == EEHBRailingSide::Left
		? LeftRailingPostOverrideMeshComponents
		: RightRailingPostOverrideMeshComponents;
}

const TArray<FGuid>& AEHB_Stair::GetEmbeddedRailingPostInstanceGuids(EEHBRailingSide Side) const
{
	return Side == EEHBRailingSide::Left ? LeftRailingPostInstanceGuids : RightRailingPostInstanceGuids;
}

const TArray<FGuid>& AEHB_Stair::GetEmbeddedRailingPostOverrideComponentGuids(EEHBRailingSide Side) const
{
	return Side == EEHBRailingSide::Left
		? LeftRailingPostOverrideComponentGuids
		: RightRailingPostOverrideComponentGuids;
}

const TArray<TObjectPtr<UStaticMeshComponent>>& AEHB_Stair::GetEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side) const
{
	return Side == EEHBRailingSide::Left
		? LeftRailingPostOverrideMeshComponents
		: RightRailingPostOverrideMeshComponents;
}

bool AEHB_Stair::FindEmbeddedRailingPostByGuid(FGuid PostGuid, FEHBRailingPost& OutPost) const
{
	EEHBRailingSide IgnoredSide = EEHBRailingSide::Left;
	return FindEmbeddedRailingPostByGuidWithSide(PostGuid, OutPost, IgnoredSide);
}

bool AEHB_Stair::FindEmbeddedRailingPostByGuidWithSide(FGuid PostGuid, FEHBRailingPost& OutPost, EEHBRailingSide& OutSide) const
{
	if (!PostGuid.IsValid())
	{
		return false;
	}

	for (const FEHBRailingPost& Post : LeftGeneratedRailingPosts)
	{
		if (Post.PostGuid == PostGuid)
		{
			OutPost = Post;
			OutSide = EEHBRailingSide::Left;
			return true;
		}
	}

	for (const FEHBRailingPost& Post : RightGeneratedRailingPosts)
	{
		if (Post.PostGuid == PostGuid)
		{
			OutPost = Post;
			OutSide = EEHBRailingSide::Right;
			return true;
		}
	}

	return false;
}

bool AEHB_Stair::GetEmbeddedRailingPostGuidForInstanceIndex(EEHBRailingSide Side, int32 InstanceIndex, FGuid& OutPostGuid) const
{
	OutPostGuid.Invalidate();
	const TArray<FGuid>& InstanceGuids = GetEmbeddedRailingPostInstanceGuids(Side);
	if (!InstanceGuids.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	OutPostGuid = InstanceGuids[InstanceIndex];
	return OutPostGuid.IsValid();
}

bool AEHB_Stair::GetEmbeddedRailingPostGuidForOverrideComponent(
	const UActorComponent* Component,
	FGuid& OutPostGuid,
	EEHBRailingSide& OutSide) const
{
	OutPostGuid.Invalidate();
	if (!Component)
	{
		return false;
	}

	for (EEHBRailingSide Side : { EEHBRailingSide::Left, EEHBRailingSide::Right })
	{
		const TArray<TObjectPtr<UStaticMeshComponent>>& Components = GetEmbeddedRailingPostOverrideMeshComponents(Side);
		const TArray<FGuid>& ComponentGuids = GetEmbeddedRailingPostOverrideComponentGuids(Side);
		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			if (Components[Index] == Component && ComponentGuids.IsValidIndex(Index))
			{
				OutPostGuid = ComponentGuids[Index];
				OutSide = Side;
				return OutPostGuid.IsValid();
			}
		}
	}

	return false;
}

bool AEHB_Stair::GetEmbeddedRailingPostWorldLocation(FGuid PostGuid, FVector& OutWorldLocation) const
{
	FEHBRailingPost Post;
	EEHBRailingSide Side = EEHBRailingSide::Left;
	if (!FindEmbeddedRailingPostByGuidWithSide(PostGuid, Post, Side))
	{
		return false;
	}

	OutWorldLocation = GetActorTransform().TransformPosition(Post.LocalBaseLocation);
	return true;
}

bool AEHB_Stair::ApplyRailingMeshSampleToEmbeddedPost(
	FGuid PostGuid,
	UDataTable* InTable,
	FName InRowName,
	bool bFinished)
{
	if (!PostGuid.IsValid()
		|| !InTable
		|| InRowName.IsNone()
		|| InTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	FEHBRailingPost TargetPost;
	if (!FindEmbeddedRailingPostByGuid(PostGuid, TargetPost) || TargetPost.bSuppressInstance)
	{
		return false;
	}

	const FEHBRailingMeshData* Row = InTable->FindRow<FEHBRailingMeshData>(
		InRowName,
		TEXT("AEHB_Stair::ApplyRailingMeshSampleToEmbeddedPost"),
		false);
	if (!Row || Row->PostMesh.SourceStaticMesh.IsNull())
	{
		return false;
	}

	Modify();
	StairData.RailingPostMeshOverrides.RemoveAll(
		[PostGuid](const FEHBRailingPostMeshOverride& Override)
		{
			return Override.PostGuid == PostGuid;
		});

	FEHBRailingPostMeshOverride& Override = StairData.RailingPostMeshOverrides.AddDefaulted_GetRef();
	Override.PostGuid = PostGuid;
	Override.SampledRailingRow.DataTable = InTable;
	Override.SampledRailingRow.RowName = InRowName;
	Override.PostMesh = Row->PostMesh.SourceStaticMesh;
	Override.PostMaterial = Row->PostMaterial;
	Override.PostWidth = FMath::Max(0.1f, Row->RecommendedPostWidth);
	Override.PostHeight = FMath::Max(1.0f, Row->RecommendedPostHeight);

	RebuildEmbeddedRailings();
	if (bFinished)
	{
		NotifyElementGeometryChanged(true);
	}
	return true;
}

bool AEHB_Stair::ApplyRailingMeshSampleToEmbeddedRailings(UDataTable* InTable, FName InRowName, bool bFinished)
{
	if (!InTable
		|| InRowName.IsNone()
		|| InTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBRailingMeshData* Row = InTable->FindRow<FEHBRailingMeshData>(
		InRowName,
		TEXT("AEHB_Stair::ApplyRailingMeshSampleToEmbeddedRailings"),
		false);
	if (!Row)
	{
		return false;
	}

	const bool bHasSampledPost = !Row->PostMesh.SourceStaticMesh.IsNull();
	const bool bHasSampledRail = !Row->RailMesh.SourceStaticMesh.IsNull();
	if (!bHasSampledPost && !bHasSampledRail)
	{
		return false;
	}

	Modify();
	StairData.RailingPostMeshOverrides.Reset();
	TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Left, 0);
	TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Right, 0);
	StairData.SampledRailingRow.DataTable = InTable;
	StairData.SampledRailingRow.RowName = InRowName;

	if (bHasSampledPost)
	{
		StairData.RailingPostMesh = Row->PostMesh.SourceStaticMesh;
		StairData.RailingPostMaterial = Row->PostMaterial;
		StairData.RailingPostWidth = FMath::Max(0.1f, Row->RecommendedPostWidth);
		StairData.RailingPostHeight = FMath::Max(1.0f, Row->RecommendedPostHeight);
	}

	if (bHasSampledRail)
	{
		StairData.RailingRailMesh = Row->RailMesh.SourceStaticMesh;
		StairData.RailingRailMaterial = Row->RailMaterial;
		StairData.RailingRailHeight = FMath::Max(1.0f, Row->RecommendedRailHeight);
		StairData.RailingRailThickness = FMath::Max(0.1f, Row->RecommendedRailThickness);
		const float TreadDepth = ResolveDimension(
			StairData.bUseActualDimensions,
			StairData.TreadDepth,
			StairData.DefaultTreadDepth);
		StairData.RailingMaxRailSegmentLength = FMath::Min(
			FMath::Max(1.0f, Row->RecommendedMaxRailSegmentLength),
			FMath::Max(4.0f, TreadDepth * 0.5f));
	}

	RebuildEmbeddedRailings();
	if (bFinished)
	{
		NotifyElementGeometryChanged(true);
	}
	return true;
}

bool AEHB_Stair::IsEmbeddedRailingSideGenerated(EEHBRailingSide Side) const
{
	if (!StairData.bGenerateRailing)
	{
		return false;
	}

	if (Side == EEHBRailingSide::Left && !StairData.bGenerateLeftRailing)
	{
		return false;
	}
	if (Side == EEHBRailingSide::Right && !StairData.bGenerateRightRailing)
	{
		return false;
	}

	return !GetEmbeddedRailingPosts(Side).IsEmpty();
}

void AEHB_Stair::TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side, int32 DesiredCount)
{
	TArray<TObjectPtr<UStaticMeshComponent>>& Components = GetMutableEmbeddedRailingPostOverrideMeshComponents(Side);
	for (int32 Index = Components.Num() - 1; Index >= DesiredCount; --Index)
	{
		if (UStaticMeshComponent* Component = Components[Index])
		{
			Component->DestroyComponent();
		}
		Components.RemoveAt(Index);
	}

	TArray<FGuid>& ComponentGuids = GetMutableEmbeddedRailingPostOverrideComponentGuids(Side);
	if (ComponentGuids.Num() > DesiredCount)
	{
		ComponentGuids.SetNum(DesiredCount);
	}
}

UStaticMeshComponent* AEHB_Stair::GetOrCreateEmbeddedRailingPostOverrideMeshComponent(
	EEHBRailingSide Side,
	int32 ComponentIndex)
{
	if (ComponentIndex < 0)
	{
		return nullptr;
	}

	TArray<TObjectPtr<UStaticMeshComponent>>& Components = GetMutableEmbeddedRailingPostOverrideMeshComponents(Side);
	while (Components.Num() <= ComponentIndex)
	{
		const TCHAR* SideName = Side == EEHBRailingSide::Left ? TEXT("Left") : TEXT("Right");
		const FName ComponentName(*FString::Printf(TEXT("Stair%sRailingPostOverride_%d"), SideName, Components.Num()));
		UStaticMeshComponent* NewComponent = NewObject<UStaticMeshComponent>(this, ComponentName, RF_Transactional);
		if (!NewComponent)
		{
			return nullptr;
		}

		NewComponent->CreationMethod = EComponentCreationMethod::Instance;
		NewComponent->SetupAttachment(SceneRoot);
		NewComponent->SetMobility(EComponentMobility::Movable);
		NewComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		NewComponent->SetCollisionObjectType(ECC_WorldStatic);
		NewComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		NewComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		NewComponent->ComponentTags.AddUnique(TEXT("EHB_StairRailing"));
		AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		Components.Add(NewComponent);
	}

	return Components[ComponentIndex];
}

void AEHB_Stair::RebuildEmbeddedRailingPostOverrideMeshes(EEHBRailingSide Side)
{
	int32 ComponentIndex = 0;
	TArray<FGuid>& ComponentGuids = GetMutableEmbeddedRailingPostOverrideComponentGuids(Side);
	ComponentGuids.Reset();
	for (const FEHBRailingPostMeshOverride& Override : StairData.RailingPostMeshOverrides)
	{
		if (!Override.PostGuid.IsValid())
		{
			continue;
		}

		FEHBRailingPost Post;
		EEHBRailingSide PostSide = EEHBRailingSide::Left;
		if (!FindEmbeddedRailingPostByGuidWithSide(Override.PostGuid, Post, PostSide)
			|| PostSide != Side
			|| Post.bSuppressInstance)
		{
			continue;
		}

		UStaticMesh* LoadedPostMesh = ResolveStairRailingPostMesh(Override.PostMesh);
		if (!LoadedPostMesh)
		{
			continue;
		}

		UStaticMeshComponent* Component = GetOrCreateEmbeddedRailingPostOverrideMeshComponent(Side, ComponentIndex++);
		if (!Component)
		{
			continue;
		}

		const FBox SourceBounds = GetSafeStaticMeshBounds(LoadedPostMesh);
		const FVector SourceSize = SourceBounds.GetSize();
		const float PostHorizontalScale = Override.PostWidth / GetSafeSourceSize(FMath::Max(SourceSize.X, SourceSize.Y));
		const float PostVerticalScale = Override.PostHeight / GetSafeSourceSize(SourceSize.Z);
		const FVector PostInstanceScale(PostHorizontalScale, PostHorizontalScale, PostVerticalScale);
		const FVector SourceAnchorLocal(
			SourceBounds.GetCenter().X * PostHorizontalScale,
			SourceBounds.GetCenter().Y * PostHorizontalScale,
			SourceBounds.Min.Z * PostVerticalScale);
		const FVector InstanceLocation = Post.LocalBaseLocation - Post.LocalRotation.RotateVector(SourceAnchorLocal);

		Component->SetStaticMesh(LoadedPostMesh);
		Component->SetRelativeTransform(FTransform(Post.LocalRotation, InstanceLocation, PostInstanceScale));
		Component->SetMaterial(0, ResolveMaterial(Override.PostMaterial));
		Component->SetVisibility(true);
		ComponentGuids.Add(Override.PostGuid);
	}

	TrimEmbeddedRailingPostOverrideMeshComponents(Side, ComponentIndex);
}

const FEHBRailingPostMeshOverride* AEHB_Stair::FindEmbeddedRailingPostMeshOverride(FGuid PostGuid) const
{
	if (!PostGuid.IsValid())
	{
		return nullptr;
	}

	return StairData.RailingPostMeshOverrides.FindByPredicate(
		[PostGuid](const FEHBRailingPostMeshOverride& Override)
		{
			return Override.PostGuid == PostGuid && !Override.PostMesh.IsNull();
		});
}

bool AEHB_Stair::HasEmbeddedRailingPostMeshOverride(FGuid PostGuid) const
{
	return FindEmbeddedRailingPostMeshOverride(PostGuid) != nullptr;
}

void AEHB_Stair::ClearEmbeddedRailingSide(
	UHierarchicalInstancedStaticMeshComponent* PostComponent,
	UEHBGeneratedMeshComponent* RailComponent) const
{
	if (PostComponent)
	{
		PostComponent->ClearInstances();
		PostComponent->SetVisibility(false);
	}
	if (RailComponent)
	{
		RailComponent->ClearAllMeshSections();
		RailComponent->SetVisibility(false);
	}
}

void AEHB_Stair::RebuildEmbeddedRailings()
{
	if (!StairData.bGenerateRailing)
	{
		ClearEmbeddedRailingSide(LeftRailingPostMeshComponent, LeftRailingRailMeshComponent);
		ClearEmbeddedRailingSide(RightRailingPostMeshComponent, RightRailingRailMeshComponent);
		LeftGeneratedRailingPosts.Reset();
		RightGeneratedRailingPosts.Reset();
		LeftRailingPostInstanceGuids.Reset();
		RightRailingPostInstanceGuids.Reset();
		TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Left, 0);
		TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Right, 0);
		return;
	}

	if (StairData.bGenerateLeftRailing)
	{
		RebuildEmbeddedRailingSide(EEHBRailingSide::Left, LeftRailingPostMeshComponent, LeftRailingRailMeshComponent);
	}
	else
	{
		ClearEmbeddedRailingSide(LeftRailingPostMeshComponent, LeftRailingRailMeshComponent);
		LeftGeneratedRailingPosts.Reset();
		LeftRailingPostInstanceGuids.Reset();
		TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Left, 0);
	}

	if (StairData.bGenerateRightRailing)
	{
		RebuildEmbeddedRailingSide(EEHBRailingSide::Right, RightRailingPostMeshComponent, RightRailingRailMeshComponent);
	}
	else
	{
		ClearEmbeddedRailingSide(RightRailingPostMeshComponent, RightRailingRailMeshComponent);
		RightGeneratedRailingPosts.Reset();
		RightRailingPostInstanceGuids.Reset();
		TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide::Right, 0);
	}
}

void AEHB_Stair::RebuildEmbeddedRailingSide(
	EEHBRailingSide Side,
	UHierarchicalInstancedStaticMeshComponent* PostComponent,
	UEHBGeneratedMeshComponent* RailComponent)
{
	if (!PostComponent || !RailComponent)
	{
		return;
	}

	TArray<FEHBStairRailingPostSample> Samples;
	if (!BuildRailingPostSamples(
		Side,
		EEHBRailingPostSpacingMode::StepAligned,
		StairData.TreadDepth * static_cast<float>(StairData.RailingStepsPerPost),
		StairData.RailingStepsPerPost,
		StairData.RailingEdgeInset,
		0.0f,
		Samples)
		|| Samples.Num() < 1)
	{
		ClearEmbeddedRailingSide(PostComponent, RailComponent);
		GetMutableEmbeddedRailingPosts(Side).Reset();
		GetMutableEmbeddedRailingPostInstanceGuids(Side).Reset();
		TrimEmbeddedRailingPostOverrideMeshComponents(Side, 0);
		return;
	}

	TArray<FEHBRailingPost>& GeneratedPosts = GetMutableEmbeddedRailingPosts(Side);
	const TArray<FEHBRailingPost> PreviousPosts = GeneratedPosts;
	GeneratedPosts.Reset();

	auto FindPreviousGuid = [&PreviousPosts](float Distance)
	{
		for (const FEHBRailingPost& PreviousPost : PreviousPosts)
		{
			if (FMath::Abs(PreviousPost.Distance - Distance) <= 1.0f && PreviousPost.PostGuid.IsValid())
			{
				return PreviousPost.PostGuid;
			}
		}
		return FGuid::NewGuid();
	};

	const float StairLength = GetStairLength();
	for (const FEHBStairRailingPostSample& Sample : Samples)
	{
		FEHBRailingPost& Post = GeneratedPosts.AddDefaulted_GetRef();
		Post.PostGuid = FindPreviousGuid(Sample.Distance);
		Post.Distance = Sample.Distance;
		Post.StepIndex = Sample.StepIndex;
		Post.LocalBaseLocation = Sample.LocalBaseLocation;
		Post.LocalRotation = Sample.LocalRotation;
		Post.bExplicit =
			Sample.Distance < 0.0f
			|| FMath::IsNearlyZero(Sample.Distance, 0.5f)
			|| FMath::IsNearlyEqual(Sample.Distance, StairLength, 0.5f);
	}

	UStaticMesh* LoadedPostMesh = ResolveStairRailingPostMesh(StairData.RailingPostMesh);
	if (LoadedPostMesh)
	{
		PostComponent->SetStaticMesh(LoadedPostMesh);
	}
	PostComponent->ClearInstances();
	TArray<FGuid>& InstanceGuids = GetMutableEmbeddedRailingPostInstanceGuids(Side);
	InstanceGuids.Reset();
	const FBox PostSourceBounds = GetSafeStaticMeshBounds(LoadedPostMesh);
	const FVector PostSourceSize = PostSourceBounds.GetSize();
	const float PostHorizontalScale = StairData.RailingPostWidth / GetSafeSourceSize(FMath::Max(PostSourceSize.X, PostSourceSize.Y));
	const float PostVerticalScale = StairData.RailingPostHeight / GetSafeSourceSize(PostSourceSize.Z);
	const FVector PostScale(PostHorizontalScale, PostHorizontalScale, PostVerticalScale);
	const FVector SourceAnchorLocal(
		PostSourceBounds.GetCenter().X * PostHorizontalScale,
		PostSourceBounds.GetCenter().Y * PostHorizontalScale,
		PostSourceBounds.Min.Z * PostVerticalScale);

	for (const FEHBRailingPost& Post : GeneratedPosts)
	{
		if (Post.bSuppressInstance || HasEmbeddedRailingPostMeshOverride(Post.PostGuid))
		{
			continue;
		}

		const FVector InstanceLocation = Post.LocalBaseLocation - Post.LocalRotation.RotateVector(SourceAnchorLocal);
		PostComponent->AddInstance(FTransform(Post.LocalRotation, InstanceLocation, PostScale), false);
		InstanceGuids.Add(Post.PostGuid);
	}
	PostComponent->SetMaterial(0, ResolveMaterial(StairData.RailingPostMaterial));
	PostComponent->SetVisibility(true);
	PostComponent->MarkRenderStateDirty();
	RebuildEmbeddedRailingPostOverrideMeshes(Side);

	TArray<FVector> RailVertices;
	TArray<int32> RailTriangles;
	TArray<FVector> RailNormals;
	TArray<FVector2D> RailUVs;
	UStaticMesh* LoadedRailMesh = ResolveStairRailingPostMesh(StairData.RailingRailMesh);
	const FBox RailSourceBounds = GetSafeStaticMeshBounds(LoadedRailMesh);
	const FVector RailSourceSize = RailSourceBounds.GetSize();
	const float RailCrossScale = StairData.RailingRailThickness / GetSafeSourceSize(FMath::Max(RailSourceSize.Y, RailSourceSize.Z));
	const float HalfRailWidth = FMath::Max(0.1f, FMath::Abs(RailSourceSize.Y) * RailCrossScale * 0.5f);
	const float HalfRailHeight = FMath::Max(0.1f, FMath::Abs(RailSourceSize.Z) * RailCrossScale * 0.5f);
	const float MaxSegmentLength = FMath::Max(1.0f, StairData.RailingMaxRailSegmentLength);

	for (int32 Index = 0; Index + 1 < GeneratedPosts.Num(); ++Index)
	{
		const float D0 = GeneratedPosts[Index].Distance;
		const float D1 = GeneratedPosts[Index + 1].Distance;
		if (D1 <= D0 + UE_SMALL_NUMBER)
		{
			continue;
		}

		const int32 SubSegmentCount = FMath::Max(1, FMath::CeilToInt((D1 - D0) / MaxSegmentLength));
		for (int32 SubIndex = 0; SubIndex < SubSegmentCount; ++SubIndex)
		{
			const float Alpha0 = static_cast<float>(SubIndex) / static_cast<float>(SubSegmentCount);
			const float Alpha1 = static_cast<float>(SubIndex + 1) / static_cast<float>(SubSegmentCount);
			const float SegmentD0 = FMath::Lerp(D0, D1, Alpha0);
			const float SegmentD1 = FMath::Lerp(D0, D1, Alpha1);
			FEHBStairPathSample StartSample;
			FEHBStairPathSample EndSample;
			if (!SampleRailPathForRailing(Side, SegmentD0, StairData.RailingEdgeInset, 0.0f, StartSample)
				|| !SampleRailPathForRailing(Side, SegmentD1, StairData.RailingEdgeInset, 0.0f, EndSample))
			{
				continue;
			}

			AppendRailSweepSegment(
				RailVertices,
				RailTriangles,
				RailNormals,
				RailUVs,
				StartSample.LocalLocation + FVector::UpVector * StairData.RailingRailHeight,
				StartSample.LocalForward,
				StartSample.LocalRight,
				EndSample.LocalLocation + FVector::UpVector * StairData.RailingRailHeight,
				EndSample.LocalForward,
				EndSample.LocalRight,
				HalfRailWidth,
				HalfRailHeight,
				SegmentD0 / EHBStairUVWorldSize,
				SegmentD1 / EHBStairUVWorldSize);
		}
	}

	if (RailVertices.IsEmpty() || RailTriangles.IsEmpty())
	{
		RailComponent->ClearAllMeshSections();
		RailComponent->SetVisibility(false);
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, RailVertices.Num());
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(), RailVertices.Num());
	RailComponent->CreateMeshSection_LinearColor(
		0,
		RailVertices,
		RailTriangles,
		RailNormals,
		RailUVs,
		VertexColors,
		Tangents,
		true);
	RailComponent->SetMeshSectionName(0, Side == EEHBRailingSide::Left ? FName(TEXT("LeftStairRailingRail")) : FName(TEXT("RightStairRailingRail")));
	RailComponent->ClearMeshSectionsFrom(1);
	RailComponent->SetMaterialIfChanged(0, ResolveMaterial(StairData.RailingRailMaterial));
	RailComponent->SetVisibility(true);
}

FVector AEHB_Stair::TransformStraightStairLocalPointToPath(const FVector& StairLocalPoint) const
{
	return TransformPathPoint(StairData, GetStairLength(), StairLocalPoint);
}

void AEHB_Stair::NormalizeStairData()
{
	StairData.TreadDepth = FMath::Max(1.0f, StairData.TreadDepth);
	StairData.StairWidth = FMath::Max(1.0f, StairData.StairWidth);
	StairData.StairHeight = FMath::Max(1.0f, StairData.StairHeight);
	StairData.DefaultTreadDepth = FMath::Max(1.0f, StairData.DefaultTreadDepth);
	StairData.DefaultStairWidth = FMath::Max(1.0f, StairData.DefaultStairWidth);
	StairData.DefaultStairHeight = FMath::Max(1.0f, StairData.DefaultStairHeight);
	StairData.MinStepHeight = FMath::Max(1.0f, StairData.MinStepHeight);
	StairData.MaxStepHeight = FMath::Max(1.0f, StairData.MaxStepHeight);
	StairData.PanelThickness = FMath::Max(0.1f, StairData.PanelThickness);
	StairData.NosingLength = FMath::Max(0.0f, StairData.NosingLength);
	StairData.SideProtruding = FMath::Max(0.0f, StairData.SideProtruding);
	StairData.SideThickness = FMath::Max(0.1f, StairData.SideThickness);
	StairData.SideBoardHeight = FMath::Max(1.0f, StairData.SideBoardHeight);
	StairData.SideBoardTopOffset = FMath::Max(0.0f, StairData.SideBoardTopOffset);
	StairData.SideGuardThickness = FMath::Max(0.1f, StairData.SideGuardThickness);
	StairData.SideGuardHeight = FMath::Max(1.0f, StairData.SideGuardHeight);
	StairData.RailingStepsPerPost = FMath::Max(1, StairData.RailingStepsPerPost);
	StairData.RailingEdgeInset = FMath::Max(0.0f, StairData.RailingEdgeInset);
	StairData.RailingPostForwardOffset = FMath::Clamp(StairData.RailingPostForwardOffset, -10000.0f, 10000.0f);
	StairData.RailingPostWidth = FMath::Max(0.1f, StairData.RailingPostWidth);
	StairData.RailingPostHeight = FMath::Max(1.0f, StairData.RailingPostHeight);
	StairData.RailingRailHeight = FMath::Max(1.0f, StairData.RailingRailHeight);
	StairData.RailingRailThickness = FMath::Max(0.1f, StairData.RailingRailThickness);
	StairData.RailingMaxRailSegmentLength = FMath::Max(1.0f, StairData.RailingMaxRailSegmentLength);
	if (StairData.bUseIntermediateControls)
	{
		StairData.IntermediateControlOffsets.SetNum(EHBStairIntermediateControlCount);
	}
	else
	{
		ResetIntermediateControlOffsets();
	}

	StairData.BottomStepYawOffset = FRotator::NormalizeAxis(StairData.BottomStepYawOffset);
}
