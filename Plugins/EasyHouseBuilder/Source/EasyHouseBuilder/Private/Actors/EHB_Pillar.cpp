// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_Pillar.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "Core/EHBWallJunctionMesh.h"

#include "Algo/Reverse.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Wall.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/EHBVerticalSurfaceComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Settings/EHBBuildingToolsetSettings.h"

namespace
{
	/** 鍩虹鏌变綋鐨勯粯璁?UV 瀵嗗害锛氭瘡 100cm 瀵瑰簲 1 涓?UV 鍗曚綅锛岄伩鍏嶉珮鏌变晶闈㈢汗鐞嗚寮鸿鎷変几鍒?0-1銆?*/
	constexpr float EHBBasicPillarUVWorldSize = 100.0f;
	constexpr float EHBPillarSampleClipVertexTolerance = 0.01f;
	constexpr float EHBPillarSampleMinTriangleAltitude = 0.05f;

	float GetBasicPillarUVLength(float Centimeters)
	{
		return FMath::Max(1.0f, Centimeters) / EHBBasicPillarUVWorldSize;
	}

	float GetYawFromTransform(const FTransform& Transform)
	{
		return FRotator::NormalizeAxis(Transform.GetRotation().Rotator().Yaw);
	}

	bool HasMeaningfulYawChange(const FTransform& OldTransform, const FTransform& NewTransform)
	{
		constexpr float RotationIntentTolerance = 0.05f;
		const float OldYaw = GetYawFromTransform(OldTransform);
		const float NewYaw = GetYawFromTransform(NewTransform);
		return FMath::Abs(FMath::FindDeltaAngleDegrees(OldYaw, NewYaw)) > RotationIntentTolerance;
	}

	bool HasMeaningfulLocationChange(const FTransform& OldTransform, const FTransform& NewTransform)
	{
		constexpr float LocationIntentTolerance = 0.01f;
		return FVector::DistSquared(OldTransform.GetLocation(), NewTransform.GetLocation())
			> FMath::Square(LocationIntentTolerance);
	}

	FVector RoundPillarLocalCoordinates(const FVector& LocalLocation)
	{
		return FVector(
			FMath::RoundToDouble(LocalLocation.X),
			FMath::RoundToDouble(LocalLocation.Y),
			FMath::RoundToDouble(LocalLocation.Z));
	}

	bool TrySnapLocalLocationToNearestOctantFromAnchor(
		const FVector& AnchorLocalLocation,
		const FVector& DesiredLocalLocation,
		FVector& OutSnappedLocalLocation)
	{
		constexpr double OctantSnapAngleToleranceDegrees = 3.0;

		const FVector LocalDelta(DesiredLocalLocation.X - AnchorLocalLocation.X, DesiredLocalLocation.Y - AnchorLocalLocation.Y, 0.0f);
		if (LocalDelta.SizeSquared2D() <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const double DesiredAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(LocalDelta.Y, LocalDelta.X));
		const double SnappedAngleDegrees = FMath::RoundToDouble(DesiredAngleDegrees / 45.0) * 45.0;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredAngleDegrees, SnappedAngleDegrees)) > OctantSnapAngleToleranceDegrees)
		{
			return false;
		}

		const double AngleRadians = FMath::DegreesToRadians(SnappedAngleDegrees);
		const FVector Direction(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0);
		OutSnappedLocalLocation = AnchorLocalLocation + Direction * FVector::DotProduct(LocalDelta, Direction);
		OutSnappedLocalLocation.Z = DesiredLocalLocation.Z;
		return true;
	}

	TSoftObjectPtr<UMaterialInterface> GetConfiguredDefaultWhiteBoxMaterial()
	{
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		return Settings ? Settings->DefaultWhiteBoxMaterial : TSoftObjectPtr<UMaterialInterface>();
	}

	FTransform MakeTransformWithYaw(const FTransform& SourceTransform, float Yaw)
	{
		FTransform Result = SourceTransform;
		FRotator Rotation = SourceTransform.GetRotation().Rotator();
		Rotation.Yaw = FRotator::NormalizeAxis(Yaw);
		Rotation.Normalize();
		Result.SetRotation(Rotation.Quaternion());
		return Result;
	}

	FRotator MakeYawRotationFromWorldXAxis(const FVector& WorldXAxis)
	{
		const FVector SafeAxis = WorldXAxis.GetSafeNormal2D();
		FRotator Rotation = FRotator::ZeroRotator;
		if (!SafeAxis.IsNearlyZero())
		{
			Rotation.Yaw = FMath::RadiansToDegrees(FMath::Atan2(SafeAxis.Y, SafeAxis.X));
			Rotation.Normalize();
		}
		return Rotation;
	}

	FVector GetWorldYAxisFromXAxis(const FVector& WorldXAxis)
	{
		return WorldXAxis.RotateAngleAxis(90.0f, FVector::UpVector).GetSafeNormal2D();
	}

	float GetRectHalfExtentAlongWorldNormal(
		const FVector& WorldNormal,
		const FVector& WorldXAxis,
		float HalfWidth,
		float HalfDepth)
	{
		const FVector SafeNormal = WorldNormal.GetSafeNormal2D();
		const FVector SafeXAxis = WorldXAxis.GetSafeNormal2D();
		const FVector SafeYAxis = GetWorldYAxisFromXAxis(SafeXAxis);
		if (SafeNormal.IsNearlyZero() || SafeXAxis.IsNearlyZero() || SafeYAxis.IsNearlyZero())
		{
			return FMath::Max(HalfWidth, HalfDepth);
		}

		return FMath::Abs(FVector::DotProduct(SafeNormal, SafeXAxis)) * FMath::Max(0.0f, HalfWidth)
			+ FMath::Abs(FVector::DotProduct(SafeNormal, SafeYAxis)) * FMath::Max(0.0f, HalfDepth);
	}

	void AddUniqueAxisXOption(TArray<FVector>& AxisOptions, const FVector& Axis)
	{
		const FVector SafeAxis = Axis.GetSafeNormal2D();
		if (SafeAxis.IsNearlyZero())
		{
			return;
		}

		constexpr float DuplicateDotThreshold = 0.999f;
		for (const FVector& ExistingAxis : AxisOptions)
		{
			if (FMath::Abs(FVector::DotProduct(ExistingAxis, SafeAxis)) >= DuplicateDotThreshold)
			{
				return;
			}
		}
		AxisOptions.Add(SafeAxis);
	}

	void AddUniqueNormalOption(TArray<FVector>& NormalOptions, const FVector& Normal)
	{
		const FVector SafeNormal = Normal.GetSafeNormal2D();
		if (SafeNormal.IsNearlyZero())
		{
			return;
		}

		constexpr float DuplicateDotThreshold = 0.999f;
		for (const FVector& ExistingNormal : NormalOptions)
		{
			if (FVector::DotProduct(ExistingNormal, SafeNormal) >= DuplicateDotThreshold)
			{
				return;
			}
		}
		NormalOptions.Add(SafeNormal);
	}

	bool HasMatchingConnectedWallFaceNormal(const TArray<FVector>& Normals, const FVector& OutwardNormal)
	{
		const FVector SafeOutwardNormal = OutwardNormal.GetSafeNormal2D();
		if (SafeOutwardNormal.IsNearlyZero())
		{
			return false;
		}

		constexpr float MatchThreshold = 0.55f;
		for (const FVector& Normal : Normals)
		{
			if (FVector::DotProduct(SafeOutwardNormal, Normal) > MatchThreshold)
			{
				return true;
			}
		}
		return false;
	}

	bool IsSlabTopNearWorldZ(const AEHB_FloorSlab* Slab, float DesiredWorldZ, float Tolerance)
	{
		if (!Slab)
		{
			return false;
		}

		const float SlabTopWorldZ = Slab->GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, Slab->GetTopZ())).Z;
		return FMath::Abs(SlabTopWorldZ - DesiredWorldZ) <= Tolerance;
	}





	struct FEHBPillarSlabBoundarySnapCandidate
	{
		float Score = TNumericLimits<float>::Max();
		FVector WorldLocation = FVector::ZeroVector;
		FRotator WorldRotation = FRotator::ZeroRotator;
	};





	float Cross2D(const FVector& A, const FVector& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	float Dot2D(const FVector& A, const FVector& B)
	{
		return A.X * B.X + A.Y * B.Y;
	}

	float GetClosestAlphaOnSegment2D(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point)
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

	float Cross2D(const FVector& Origin, const FVector& A, const FVector& B)
	{
		return Cross2D(A - Origin, B - Origin);
	}

	float CalculateSignedArea2D(const TArray<FVector>& Points)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector& Current = Points[Index];
			const FVector& Next = Points[(Index + 1) % Points.Num()];
			Area += static_cast<double>(Current.X) * Next.Y - static_cast<double>(Next.X) * Current.Y;
		}
		return static_cast<float>(Area * 0.5);
	}

	bool SolveOffsetPointFromCorner2D(
		const FVector& Corner,
		const FVector& FirstOutwardNormal,
		const FVector& SecondOutwardNormal,
		float FirstInset,
		float SecondInset,
		FVector& OutPoint)
	{
		const float Det = Cross2D(FirstOutwardNormal, SecondOutwardNormal);
		if (FMath::Abs(Det) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const float FirstSignedDistance = -FMath::Max(0.0f, FirstInset);
		const float SecondSignedDistance = -FMath::Max(0.0f, SecondInset);
		const float DeltaX = (FirstSignedDistance * SecondOutwardNormal.Y - FirstOutwardNormal.Y * SecondSignedDistance) / Det;
		const float DeltaY = (FirstOutwardNormal.X * SecondSignedDistance - FirstSignedDistance * SecondOutwardNormal.X) / Det;
		OutPoint = FVector(Corner.X + DeltaX, Corner.Y + DeltaY, Corner.Z);
		return true;
	}











	void AppendTriangleFace(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& Normal,
		const FVector2D& UVA,
		const FVector2D& UVB,
		const FVector2D& UVC)
	{
		const int32 FirstIndex = Vertices.Num();
		const FVector SafeNormal = Normal.GetSafeNormal();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
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
	}

	struct FEHBPillarSurfaceCandidate
	{
		const AEHB_Wall* Wall = nullptr;
		FEHBWallSurfaceStyle SurfaceStyle;
		FVector PreferredNormal = FVector::ForwardVector;
		FVector WallTangent = FVector::ForwardVector;
		float WallLength = 0.0f;
		float PhaseAtConnectedEndpoint = 0.0f;
		float MinimumNormalDot = 0.55f;
		bool bConnectedAtStart = true;
	};

	struct FEHBPillarTransformedSampleVertex
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector2D UV = FVector2D::ZeroVector;
		float SurfaceCoordinate = 0.0f;
	};

	bool IsSampledWallSurfaceStyle(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		return SurfaceStyle.SourceType == EEHBWallSurfaceSourceType::SampledMesh
			&& SurfaceStyle.SampledWallRow.DataTable
			&& !SurfaceStyle.SampledWallRow.RowName.IsNone()
			&& SurfaceStyle.SampledWallRow.DataTable->GetRowStruct() == FEHBWallMeshData::StaticStruct();
	}

	bool HasUsablePillarSurfaceStyle(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		return IsSampledWallSurfaceStyle(SurfaceStyle)
			|| !SurfaceStyle.OverrideMaterial.IsNull();
	}

	bool HasUsablePillarSurfaceOverride(const FEHBPillarConnectedSurfaceOverride& Override)
	{
		if (!Override.bEnabled)
		{
			return false;
		}

		if (!Override.bUsePerSideSurfaceStyles)
		{
			return HasUsablePillarSurfaceStyle(Override.SurfaceStyle);
		}

		return (Override.bApplyLeftWallSide && HasUsablePillarSurfaceStyle(Override.LeftWallSideSurfaceStyle))
			|| (Override.bApplyRightWallSide && HasUsablePillarSurfaceStyle(Override.RightWallSideSurfaceStyle))
			|| (Override.bApplyWallEndCap && HasUsablePillarSurfaceStyle(Override.WallEndCapSurfaceStyle));
	}

	const FEHBWallMeshData* FindWallMeshDataFromSurfaceStyle(const FEHBWallSurfaceStyle& SurfaceStyle, const TCHAR* Context)
	{
		if (!IsSampledWallSurfaceStyle(SurfaceStyle))
		{
			return nullptr;
		}

		return SurfaceStyle.SampledWallRow.DataTable->FindRow<FEHBWallMeshData>(
			SurfaceStyle.SampledWallRow.RowName,
			Context,
			false);
	}

	FString MakeWallSurfaceStyleContinuityKey(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		const FEHBWallMeshData* WallMeshData = FindWallMeshDataFromSurfaceStyle(
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

	bool AreWallSurfaceSamplesEquivalent(const FEHBWallSurfaceStyle& A, const FEHBWallSurfaceStyle& B)
	{
		const FString AKey = MakeWallSurfaceStyleContinuityKey(A);
		return !AKey.IsEmpty() && AKey == MakeWallSurfaceStyleContinuityKey(B);
	}

	bool TryGetCompatibleEndCapSurfaceStyle(const AEHB_Wall& Wall, FEHBWallSurfaceStyle& OutSurfaceStyle)
	{
		const bool bLeftSampled = IsSampledWallSurfaceStyle(Wall.LeftSurfaceStyle);
		const bool bRightSampled = IsSampledWallSurfaceStyle(Wall.RightSurfaceStyle);
		if (bLeftSampled && bRightSampled)
		{
			if (!AreWallSurfaceSamplesEquivalent(Wall.LeftSurfaceStyle, Wall.RightSurfaceStyle))
			{
				return false;
			}

			OutSurfaceStyle = Wall.LeftSurfaceStyle;
			return true;
		}

		if (bLeftSampled)
		{
			OutSurfaceStyle = Wall.LeftSurfaceStyle;
			return true;
		}

		if (bRightSampled)
		{
			OutSurfaceStyle = Wall.RightSurfaceStyle;
			return true;
		}

		return false;
	}

	const FEHBPillarConnectedSurfaceOverride* FindSurfaceOverrideForWall(const AEHB_Pillar& Pillar, const FGuid& WallGuid)
	{
		for (const FEHBPillarConnectedSurfaceOverride& Override : Pillar.ConnectedSurfaceOverrides)
		{
			if (Override.WallGuid == WallGuid
				&& HasUsablePillarSurfaceOverride(Override))
			{
				return &Override;
			}
		}

		return nullptr;
	}

	AEHB_Wall* FindConnectedWallByGuid(const AEHB_Pillar& Pillar, const FGuid& WallGuid)
	{
		if (!Pillar.OwningBuilding || !WallGuid.IsValid())
		{
			return nullptr;
		}

		if (AEHB_Wall* IndexedWall = Cast<AEHB_Wall>(Pillar.OwningBuilding->FindElementActorByGuid(WallGuid)))
		{
			return IndexedWall;
		}

		TArray<AActor*> AttachedActors;
		Pillar.OwningBuilding->GetAttachedActors(AttachedActors);
		for (AActor* AttachedActor : AttachedActors)
		{
			AEHB_Wall* CandidateWall = Cast<AEHB_Wall>(AttachedActor);
			if (CandidateWall && CandidateWall->ElementGuid == WallGuid)
			{
				return CandidateWall;
			}
		}

		return nullptr;
	}

	bool ResolveWallEndpointFrameForPillar(
		const AEHB_Pillar& Pillar,
		const AEHB_Wall& Wall,
		FVector& OutDirectionFromPillar,
		FVector& OutOriginalWallTangent,
		FVector& OutWallLeftNormal,
		bool& bOutConnectedAtStart,
		float& OutWallLength)
	{
		OutWallLength = FVector::Dist2D(Wall.LocalStart, Wall.LocalEnd);
		if (OutWallLength <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const bool bConnectedAtStart = Wall.StartPillarGuid == Pillar.ElementGuid;
		const bool bConnectedAtEnd = Wall.EndPillarGuid == Pillar.ElementGuid;
		if (!bConnectedAtStart && !bConnectedAtEnd)
		{
			return false;
		}

		FVector WallLocalTangent = FVector::ForwardVector;
		if (FMath::Abs(Wall.CurveControlOffset) > 0.1f)
		{
			const float EndpointX = bConnectedAtStart ? -OutWallLength * 0.5f : OutWallLength * 0.5f;
			WallLocalTangent = Wall.TransformStraightWallLocalVectorToCurve(
				FVector(EndpointX, 0.0f, 0.0f),
				FVector::ForwardVector);
		}

		const FTransform WallLocalTransform = Wall.GetElementLocalTransform();
		const FTransform PillarLocalTransform = Pillar.GetElementLocalTransform();
		const FVector OriginalWallTangentInBuilding = WallLocalTransform
			.TransformVectorNoScale(WallLocalTangent)
			.GetSafeNormal2D();
		if (OriginalWallTangentInBuilding.IsNearlyZero())
		{
			return false;
		}

		const FVector DirectionFromPillarInBuilding = bConnectedAtStart
			? OriginalWallTangentInBuilding
			: -OriginalWallTangentInBuilding;
		const FVector WallLeftNormalInBuilding = OriginalWallTangentInBuilding
			.RotateAngleAxis(90.0f, FVector::UpVector)
			.GetSafeNormal2D();

		OutDirectionFromPillar = PillarLocalTransform
			.InverseTransformVectorNoScale(DirectionFromPillarInBuilding)
			.GetSafeNormal2D();
		OutOriginalWallTangent = PillarLocalTransform
			.InverseTransformVectorNoScale(OriginalWallTangentInBuilding)
			.GetSafeNormal2D();
		OutWallLeftNormal = PillarLocalTransform
			.InverseTransformVectorNoScale(WallLeftNormalInBuilding)
			.GetSafeNormal2D();
		bOutConnectedAtStart = bConnectedAtStart;

		return !OutDirectionFromPillar.IsNearlyZero()
			&& !OutOriginalWallTangent.IsNearlyZero()
			&& !OutWallLeftNormal.IsNearlyZero();
	}

	void AddPillarSurfaceCandidate(
		TArray<FEHBPillarSurfaceCandidate>& OutCandidates,
		const AEHB_Wall& Wall,
		const FEHBWallSurfaceStyle& SurfaceStyle,
		const FVector& PreferredNormal,
		const FVector& WallTangent,
		float WallLength,
		float PhaseAtConnectedEndpoint,
		bool bConnectedAtStart)
	{
		if (!HasUsablePillarSurfaceStyle(SurfaceStyle) || PreferredNormal.IsNearlyZero())
		{
			return;
		}

		const bool bIsSampledSurface = IsSampledWallSurfaceStyle(SurfaceStyle);
		FEHBPillarSurfaceCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
		Candidate.Wall = &Wall;
		Candidate.SurfaceStyle = SurfaceStyle;
		Candidate.PreferredNormal = PreferredNormal.GetSafeNormal2D();
		Candidate.WallTangent = WallTangent.GetSafeNormal2D();
		Candidate.WallLength = WallLength;
		Candidate.PhaseAtConnectedEndpoint = PhaseAtConnectedEndpoint;
		Candidate.MinimumNormalDot = bIsSampledSurface ? 0.55f : 0.9f;
		Candidate.bConnectedAtStart = bConnectedAtStart;
	}

	float ResolveWallSurfacePhaseAtPillarEndpoint(
		const AEHB_Wall& Wall,
		const FEHBWallSurfaceStyle& SurfaceStyle,
		bool bConnectedAtStart,
		float WallLength)
	{
		float Phase = 0.0f;
		if (AreWallSurfaceSamplesEquivalent(SurfaceStyle, Wall.LeftSurfaceStyle)
			&& Wall.GetSurfaceSamplePhaseAtEndpoint(true, bConnectedAtStart, Phase))
		{
			return Phase;
		}

		if (AreWallSurfaceSamplesEquivalent(SurfaceStyle, Wall.RightSurfaceStyle)
			&& Wall.GetSurfaceSamplePhaseAtEndpoint(false, bConnectedAtStart, Phase))
		{
			return Phase;
		}

		return bConnectedAtStart ? 0.0f : FMath::Max(0.0f, WallLength);
	}

	void GatherPillarSurfaceCandidates(const AEHB_Pillar& Pillar, TArray<FEHBPillarSurfaceCandidate>& OutCandidates)
	{
		OutCandidates.Reset();
		if (!Pillar.OwningBuilding || !Pillar.ElementGuid.IsValid())
		{
			return;
		}

		for (const FGuid& WallGuid : Pillar.ConnectedWallGuids)
		{
			const AEHB_Wall* Wall = FindConnectedWallByGuid(Pillar, WallGuid);
			if (!Wall)
			{
				continue;
			}

			FVector DirectionFromPillar = FVector::ZeroVector;
			FVector OriginalWallTangent = FVector::ZeroVector;
			FVector WallLeftNormal = FVector::ZeroVector;
			bool bConnectedAtStart = true;
			float WallLength = 0.0f;
			if (!ResolveWallEndpointFrameForPillar(
				Pillar,
				*Wall,
				DirectionFromPillar,
				OriginalWallTangent,
				WallLeftNormal,
				bConnectedAtStart,
				WallLength))
			{
				continue;
			}

			if (const FEHBPillarConnectedSurfaceOverride* Override = FindSurfaceOverrideForWall(Pillar, Wall->ElementGuid))
			{
				auto ResolveOverrideSurfaceStyle = [Override](const FEHBWallSurfaceStyle& PerSideSurfaceStyle)
				{
					return Override->bUsePerSideSurfaceStyles
						? PerSideSurfaceStyle
						: Override->SurfaceStyle;
				};
				if (Override->bApplyLeftWallSide)
				{
					const FEHBWallSurfaceStyle& LeftSurfaceStyle = ResolveOverrideSurfaceStyle(Override->LeftWallSideSurfaceStyle);
					const float LeftPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
						*Wall,
						LeftSurfaceStyle,
						bConnectedAtStart,
						WallLength);
					AddPillarSurfaceCandidate(OutCandidates, *Wall, LeftSurfaceStyle, WallLeftNormal, OriginalWallTangent, WallLength, LeftPhase, bConnectedAtStart);
				}
				if (Override->bApplyRightWallSide)
				{
					const FEHBWallSurfaceStyle& RightSurfaceStyle = ResolveOverrideSurfaceStyle(Override->RightWallSideSurfaceStyle);
					const float RightPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
						*Wall,
						RightSurfaceStyle,
						bConnectedAtStart,
						WallLength);
					AddPillarSurfaceCandidate(OutCandidates, *Wall, RightSurfaceStyle, -WallLeftNormal, OriginalWallTangent, WallLength, RightPhase, bConnectedAtStart);
				}
				if (Override->bApplyWallEndCap)
				{
					const FEHBWallSurfaceStyle& EndCapSurfaceStyle = ResolveOverrideSurfaceStyle(Override->WallEndCapSurfaceStyle);
					const float EndCapPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
						*Wall,
						EndCapSurfaceStyle,
						bConnectedAtStart,
						WallLength);
					AddPillarSurfaceCandidate(OutCandidates, *Wall, EndCapSurfaceStyle, DirectionFromPillar, OriginalWallTangent, WallLength, EndCapPhase, bConnectedAtStart);
				}
				continue;
			}

			if (!Pillar.bInheritConnectedWallSurfaceSamples)
			{
				continue;
			}

			const float LeftPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
				*Wall,
				Wall->LeftSurfaceStyle,
				bConnectedAtStart,
				WallLength);
			const float RightPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
				*Wall,
				Wall->RightSurfaceStyle,
				bConnectedAtStart,
				WallLength);
			AddPillarSurfaceCandidate(OutCandidates, *Wall, Wall->LeftSurfaceStyle, WallLeftNormal, OriginalWallTangent, WallLength, LeftPhase, bConnectedAtStart);
			AddPillarSurfaceCandidate(OutCandidates, *Wall, Wall->RightSurfaceStyle, -WallLeftNormal, OriginalWallTangent, WallLength, RightPhase, bConnectedAtStart);

			FEHBWallSurfaceStyle EndCapSurfaceStyle;
			if (TryGetCompatibleEndCapSurfaceStyle(*Wall, EndCapSurfaceStyle))
			{
				const float EndCapPhase = ResolveWallSurfacePhaseAtPillarEndpoint(
					*Wall,
					EndCapSurfaceStyle,
					bConnectedAtStart,
					WallLength);
				AddPillarSurfaceCandidate(OutCandidates, *Wall, EndCapSurfaceStyle, DirectionFromPillar, OriginalWallTangent, WallLength, EndCapPhase, bConnectedAtStart);
			}
		}
	}

	void GatherPillarConnectedWallEndCapNormals(const AEHB_Pillar& Pillar, TArray<FVector>& OutNormals)
	{
		OutNormals.Reset();
		if (!Pillar.OwningBuilding || !Pillar.ElementGuid.IsValid())
		{
			return;
		}

		for (const FGuid& WallGuid : Pillar.ConnectedWallGuids)
		{
			const AEHB_Wall* Wall = FindConnectedWallByGuid(Pillar, WallGuid);
			if (!Wall)
			{
				continue;
			}

			FVector DirectionFromPillar = FVector::ZeroVector;
			FVector OriginalWallTangent = FVector::ZeroVector;
			FVector WallLeftNormal = FVector::ZeroVector;
			bool bConnectedAtStart = true;
			float WallLength = 0.0f;
			if (!ResolveWallEndpointFrameForPillar(
				Pillar,
				*Wall,
				DirectionFromPillar,
				OriginalWallTangent,
				WallLeftNormal,
				bConnectedAtStart,
				WallLength))
			{
				continue;
			}

			AddUniqueNormalOption(OutNormals, DirectionFromPillar);
		}
	}

	bool FindBestSurfaceCandidateForEdge(
		const TArray<FEHBPillarSurfaceCandidate>& Candidates,
		const FVector& OutwardNormal,
		FEHBPillarSurfaceCandidate& OutCandidate)
	{
		const FVector SafeOutwardNormal = OutwardNormal.GetSafeNormal2D();
		if (SafeOutwardNormal.IsNearlyZero())
		{
			return false;
		}

		float BestScore = -1.0f;
		bool bFound = false;
		for (const FEHBPillarSurfaceCandidate& Candidate : Candidates)
		{
			const float Score = FVector::DotProduct(SafeOutwardNormal, Candidate.PreferredNormal);
			if (Score > Candidate.MinimumNormalDot && (!bFound || Score > BestScore))
			{
				BestScore = Score;
				OutCandidate = Candidate;
				bFound = true;
			}
		}

		return bFound;
	}

	FEHBPillarTransformedSampleVertex InterpolatePillarSampleVertex(
		const FEHBPillarTransformedSampleVertex& A,
		const FEHBPillarTransformedSampleVertex& B,
		float Alpha)
	{
		FEHBPillarTransformedSampleVertex Result;
		Result.Position = FMath::Lerp(A.Position, B.Position, Alpha);
		Result.Normal = FMath::Lerp(A.Normal, B.Normal, Alpha).GetSafeNormal();
		Result.UV = FMath::Lerp(A.UV, B.UV, Alpha);
		Result.SurfaceCoordinate = FMath::Lerp(A.SurfaceCoordinate, B.SurfaceCoordinate, Alpha);
		return Result;
	}

	bool ArePillarSampleVerticesNearlyEqual(
		const FEHBPillarTransformedSampleVertex& A,
		const FEHBPillarTransformedSampleVertex& B)
	{
		return (A.Position - B.Position).SizeSquared() <= FMath::Square(EHBPillarSampleClipVertexTolerance)
			&& FMath::Abs(A.SurfaceCoordinate - B.SurfaceCoordinate) <= EHBPillarSampleClipVertexTolerance;
	}

	TArray<FEHBPillarTransformedSampleVertex> SanitizePillarSamplePolygon(
		const TArray<FEHBPillarTransformedSampleVertex>& Polygon)
	{
		TArray<FEHBPillarTransformedSampleVertex> Result;
		Result.Reserve(Polygon.Num());

		for (const FEHBPillarTransformedSampleVertex& Vertex : Polygon)
		{
			if (Result.IsEmpty() || !ArePillarSampleVerticesNearlyEqual(Result.Last(), Vertex))
			{
				Result.Add(Vertex);
			}
		}

		while (Result.Num() > 1 && ArePillarSampleVerticesNearlyEqual(Result[0], Result.Last()))
		{
			Result.Pop(EAllowShrinking::No);
		}

		return Result;
	}

	bool IsPillarSampleTriangleUsable(
		const FEHBPillarTransformedSampleVertex& A,
		const FEHBPillarTransformedSampleVertex& B,
		const FEHBPillarTransformedSampleVertex& C)
	{
		if (A.Position.ContainsNaN() || B.Position.ContainsNaN() || C.Position.ContainsNaN())
		{
			return false;
		}

		const float ABSquared = (B.Position - A.Position).SizeSquared();
		const float BCSquared = (C.Position - B.Position).SizeSquared();
		const float CASquared = (A.Position - C.Position).SizeSquared();
		const float MinEdgeSquared = FMath::Square(EHBPillarSampleClipVertexTolerance);
		if (ABSquared <= MinEdgeSquared || BCSquared <= MinEdgeSquared || CASquared <= MinEdgeSquared)
		{
			return false;
		}

		const float MaxEdgeLength = FMath::Sqrt(FMath::Max3(ABSquared, BCSquared, CASquared));
		const float MinCrossMagnitude = MaxEdgeLength * EHBPillarSampleMinTriangleAltitude;
		const FVector Cross = FVector::CrossProduct(B.Position - A.Position, C.Position - A.Position);
		return Cross.SizeSquared() > FMath::Square(MinCrossMagnitude);
	}

	TArray<FEHBPillarTransformedSampleVertex> ClipPillarSamplePolygonByCoordinate(
		const TArray<FEHBPillarTransformedSampleVertex>& Polygon,
		float ClipCoordinate,
		bool bKeepGreater)
	{
		TArray<FEHBPillarTransformedSampleVertex> Result;
		if (Polygon.IsEmpty())
		{
			return Result;
		}

		FEHBPillarTransformedSampleVertex Previous = Polygon.Last();
		bool bPreviousInside = bKeepGreater
			? Previous.SurfaceCoordinate >= ClipCoordinate - KINDA_SMALL_NUMBER
			: Previous.SurfaceCoordinate <= ClipCoordinate + KINDA_SMALL_NUMBER;
		for (const FEHBPillarTransformedSampleVertex& Current : Polygon)
		{
			const bool bCurrentInside = bKeepGreater
				? Current.SurfaceCoordinate >= ClipCoordinate - KINDA_SMALL_NUMBER
				: Current.SurfaceCoordinate <= ClipCoordinate + KINDA_SMALL_NUMBER;
			if (bCurrentInside != bPreviousInside)
			{
				const float Delta = Current.SurfaceCoordinate - Previous.SurfaceCoordinate;
				if (FMath::Abs(Delta) > UE_SMALL_NUMBER)
				{
					const float Alpha = FMath::Clamp((ClipCoordinate - Previous.SurfaceCoordinate) / Delta, 0.0f, 1.0f);
					Result.Add(InterpolatePillarSampleVertex(Previous, Current, Alpha));
				}
			}

			if (bCurrentInside)
			{
				Result.Add(Current);
			}

			Previous = Current;
			bPreviousInside = bCurrentInside;
		}

		return SanitizePillarSamplePolygon(Result);
	}

	TArray<FEHBPillarTransformedSampleVertex> ClipPillarSamplePolygonByZ(
		const TArray<FEHBPillarTransformedSampleVertex>& Polygon,
		float ClipZ,
		bool bKeepGreater)
	{
		TArray<FEHBPillarTransformedSampleVertex> Result;
		if (Polygon.IsEmpty())
		{
			return Result;
		}

		FEHBPillarTransformedSampleVertex Previous = Polygon.Last();
		bool bPreviousInside = bKeepGreater
			? Previous.Position.Z >= ClipZ - KINDA_SMALL_NUMBER
			: Previous.Position.Z <= ClipZ + KINDA_SMALL_NUMBER;
		for (const FEHBPillarTransformedSampleVertex& Current : Polygon)
		{
			const bool bCurrentInside = bKeepGreater
				? Current.Position.Z >= ClipZ - KINDA_SMALL_NUMBER
				: Current.Position.Z <= ClipZ + KINDA_SMALL_NUMBER;
			if (bCurrentInside != bPreviousInside)
			{
				const float Delta = Current.Position.Z - Previous.Position.Z;
				if (FMath::Abs(Delta) > UE_SMALL_NUMBER)
				{
					const float Alpha = FMath::Clamp((ClipZ - Previous.Position.Z) / Delta, 0.0f, 1.0f);
					Result.Add(InterpolatePillarSampleVertex(Previous, Current, Alpha));
				}
			}

			if (bCurrentInside)
			{
				Result.Add(Current);
			}

			Previous = Current;
			bPreviousInside = bCurrentInside;
		}

		return SanitizePillarSamplePolygon(Result);
	}

	bool AppendSampledPillarSurfaceTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		TArray<int32>& TriangleMaterialIndices,
		const FEHBPillarTransformedSampleVertex& A,
		const FEHBPillarTransformedSampleVertex& B,
		const FEHBPillarTransformedSampleVertex& C,
		int32 MaterialIndex)
	{
		if (!IsPillarSampleTriangleUsable(A, B, C))
		{
			return false;
		}

		const int32 FirstIndex = Vertices.Num();
		Vertices.Add(A.Position);
		Vertices.Add(B.Position);
		Vertices.Add(C.Position);
		Normals.Add(A.Normal.IsNearlyZero() ? FVector::UpVector : A.Normal.GetSafeNormal());
		Normals.Add(B.Normal.IsNearlyZero() ? FVector::UpVector : B.Normal.GetSafeNormal());
		Normals.Add(C.Normal.IsNearlyZero() ? FVector::UpVector : C.Normal.GetSafeNormal());
		UVs.Add(A.UV);
		UVs.Add(B.UV);
		UVs.Add(C.UV);

		int32 BIndex = FirstIndex + 1;
		int32 CIndex = FirstIndex + 2;
		const FVector AverageNormal = (Normals[FirstIndex] + Normals[FirstIndex + 1] + Normals[FirstIndex + 2]).GetSafeNormal();
		if (!AverageNormal.IsNearlyZero())
		{
			const FVector TriangleNormal = FVector::CrossProduct(B.Position - A.Position, C.Position - A.Position).GetSafeNormal();
			if (FVector::DotProduct(TriangleNormal, AverageNormal) > 0.0f)
			{
				Swap(BIndex, CIndex);
			}
		}

		Triangles.Add(FirstIndex);
		Triangles.Add(BIndex);
		Triangles.Add(CIndex);
		TriangleMaterialIndices.Add(FMath::Max(0, MaterialIndex));
		return true;
	}

	bool TryBuildSampledPillarFaceFromWallSurface(
		const AEHB_Pillar& Pillar,
		const FEHBPillarSurfaceCandidate& Candidate,
		const FVector& BottomA,
		const FVector& BottomB,
		const FVector& OutwardNormal,
		TArray<FVector>& InOutVertices,
		TArray<int32>& InOutTriangles,
		TArray<FVector>& InOutNormals,
		TArray<FVector2D>& InOutUVs,
		TArray<int32>& InOutTriangleMaterialIndices,
		TArray<TSoftObjectPtr<UMaterialInterface>>& InOutMaterials)
	{
		if (!Candidate.Wall || !IsSampledWallSurfaceStyle(Candidate.SurfaceStyle))
		{
			return false;
		}

		const FEHBWallMeshData* WallMeshData = Candidate.SurfaceStyle.SampledWallRow.DataTable->FindRow<FEHBWallMeshData>(
			Candidate.SurfaceStyle.SampledWallRow.RowName,
			TEXT("AEHB_Pillar::TryBuildSampledPillarFaceFromWallSurface"),
			false);
		if (!WallMeshData)
		{
			return false;
		}

		const FEHBWallMeshSampleSurface& SampleSurface =
			Candidate.SurfaceStyle.SampleSide == EEHBWallMeshSampleSide::Front
				? WallMeshData->FrontSurface
				: WallMeshData->BackSurface;
		if (SampleSurface.Vertices.IsEmpty() || SampleSurface.Triangles.Num() < 3)
		{
			return false;
		}

		const float FaceLength = FVector::Dist2D(BottomA, BottomB);
		const float SafeHeight = FMath::Max(1.0f, Pillar.Height);
		if (FaceLength <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector RawFaceDirection = (BottomB - BottomA).GetSafeNormal2D();
		const FVector SafeOutwardNormal = OutwardNormal.GetSafeNormal2D();
		if (RawFaceDirection.IsNearlyZero() || SafeOutwardNormal.IsNearlyZero())
		{
			return false;
		}

		const bool bFaceDirectionMatchesWall = FVector::DotProduct(RawFaceDirection, Candidate.WallTangent) >= 0.0f;
		const FVector FaceStart = bFaceDirectionMatchesWall ? BottomA : BottomB;
		const FVector FaceEnd = bFaceDirectionMatchesWall ? BottomB : BottomA;
		const FVector FaceDirection = (FaceEnd - FaceStart).GetSafeNormal2D();
		const float SurfaceCoordinateStart = Candidate.bConnectedAtStart
			? Candidate.PhaseAtConnectedEndpoint - FaceLength
			: Candidate.PhaseAtConnectedEndpoint;
		const float SurfaceCoordinateEnd = SurfaceCoordinateStart + FaceLength;

		const float SourceWidth = FMath::Max(
			1.0f,
			WallMeshData->WallWidth > UE_SMALL_NUMBER
				? WallMeshData->WallWidth
				: WallMeshData->WallLocalBoundsMax.X - WallMeshData->WallLocalBoundsMin.X);
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
		const float SourceSideSign = Candidate.SurfaceStyle.SampleSide == EEHBWallMeshSampleSide::Front ? 1.0f : -1.0f;
		const float SourceAnchorY = SourceSideSign * SourceThickness * 0.5f;
		const float ZScale = SafeHeight / SourceHeight;
		const float ThicknessScale = FMath::Max(1.0f, Candidate.Wall->Thickness) / SourceThickness;

		const int32 MaterialBaseIndex = InOutMaterials.Num();
		if (!Candidate.SurfaceStyle.OverrideMaterial.IsNull())
		{
			InOutMaterials.Add(Candidate.SurfaceStyle.OverrideMaterial);
		}
		else
		{
			InOutMaterials.Append(WallMeshData->Materials);
		}

		auto ResolveMaterialIndex = [&](int32 SourceMaterialIndex)
		{
			if (!Candidate.SurfaceStyle.OverrideMaterial.IsNull())
			{
				return MaterialBaseIndex;
			}

			return MaterialBaseIndex + FMath::Max(0, SourceMaterialIndex);
		};

		auto TransformSampleVertex = [&](const FEHBWallMeshSampleVertex& SourceVertex, float TileStartX)
		{
			FEHBPillarTransformedSampleVertex Result;
			const float SourceCoordinate = TileStartX + (SourceVertex.Position.X - SourceMinX);
			const float SourceOutwardOffset = (SourceVertex.Position.Y - SourceAnchorY) * SourceSideSign;
			Result.SurfaceCoordinate = SourceCoordinate;
			Result.Position =
				FaceStart
				+ FaceDirection * (SourceCoordinate - SurfaceCoordinateStart)
				+ SafeOutwardNormal * SourceOutwardOffset * ThicknessScale
				+ FVector::UpVector * ((SourceVertex.Position.Z - SourceMinZ) * ZScale);

			Result.Normal =
				FaceDirection * SourceVertex.Normal.X
				+ SafeOutwardNormal * SourceVertex.Normal.Y * SourceSideSign / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(ThicknessScale))
				+ FVector::UpVector * SourceVertex.Normal.Z / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(ZScale));
			Result.Normal = Result.Normal.GetSafeNormal();
			if (Result.Normal.IsNearlyZero())
			{
				Result.Normal = SafeOutwardNormal;
			}
			Result.UV = SourceVertex.UV0;
			return Result;
		};

		const int32 FirstTileIndex = FMath::FloorToInt(SurfaceCoordinateStart / SourceWidth);
		const int32 LastTileIndex = FMath::FloorToInt((SurfaceCoordinateEnd - KINDA_SMALL_NUMBER) / SourceWidth);
		const int32 SourceTriangleCount = SampleSurface.Triangles.Num() / 3;
		int32 AddedTriangleCount = 0;
		for (int32 TileIndex = FirstTileIndex; TileIndex <= LastTileIndex; ++TileIndex)
		{
			const float TileStartX = static_cast<float>(TileIndex) * SourceWidth;
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

				TArray<FEHBPillarTransformedSampleVertex> Polygon =
				{
					TransformSampleVertex(SampleSurface.Vertices[SourceIndexA], TileStartX),
					TransformSampleVertex(SampleSurface.Vertices[SourceIndexB], TileStartX),
					TransformSampleVertex(SampleSurface.Vertices[SourceIndexC], TileStartX)
				};
				Polygon = ClipPillarSamplePolygonByCoordinate(Polygon, SurfaceCoordinateStart, true);
				Polygon = ClipPillarSamplePolygonByCoordinate(Polygon, SurfaceCoordinateEnd, false);
				Polygon = ClipPillarSamplePolygonByZ(Polygon, 0.0f, true);
				Polygon = ClipPillarSamplePolygonByZ(Polygon, SafeHeight, false);
				Polygon = SanitizePillarSamplePolygon(Polygon);
				if (Polygon.Num() < 3)
				{
					continue;
				}

				const int32 SourceMaterialIndex = SampleSurface.TriangleMaterialIndices.IsValidIndex(TriangleIndex)
					? SampleSurface.TriangleMaterialIndices[TriangleIndex]
					: 0;
				const int32 MaterialIndex = ResolveMaterialIndex(SourceMaterialIndex);
				for (int32 PolygonIndex = 1; PolygonIndex + 1 < Polygon.Num(); ++PolygonIndex)
				{
					if (AppendSampledPillarSurfaceTriangle(
						InOutVertices,
						InOutTriangles,
						InOutNormals,
						InOutUVs,
						InOutTriangleMaterialIndices,
						Polygon[0],
						Polygon[PolygonIndex],
						Polygon[PolygonIndex + 1],
						MaterialIndex))
					{
						++AddedTriangleCount;
					}
				}
			}
		}

		return AddedTriangleCount > 0;
	}
}

AEHB_Pillar::AEHB_Pillar()
{
	ElementType = EEHBBuildingElementType::Pillar;
	ElementCapabilities = static_cast<int32>(
		EEHBElementCapability::Structural
		| EEHBElementCapability::CanSupport
		| EEHBElementCapability::RequiresSupport
		| EEHBElementCapability::RoomBoundary);
	SemanticTags.AddUnique(TEXT("Structure.Vertical"));

	PillarMeshComponent = CreateDefaultSubobject<UEHBVerticalSurfaceComponent>(TEXT("PillarMesh"));
	PillarMeshComponent->SetupAttachment(SceneRoot);
	PillarMeshComponent->SetMobility(EComponentMobility::Movable);
	PillarMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PillarMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	PillarMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	PillarMeshComponent->bUseAsyncCooking = true;
	PillarMeshComponent->ComponentTags.AddUnique(TEXT("EHB_Pillar"));
	if (UEHBArchitecturalSurfaceComponent* SurfaceComponent = Cast<UEHBArchitecturalSurfaceComponent>(PillarMeshComponent))
	{
		SurfaceComponent->InitializeSurface(this, EEHBArchitecturalSurfaceRole::PillarSide, TEXT("Pillar.Side"), INDEX_NONE);
	}

	Tags.AddUnique(TEXT("EHB_Pillar"));
}

void AEHB_Pillar::OnConstruction(const FTransform& Transform)
{
	if (FEHBActorImportScope::IsActive()) { Super::OnConstruction(Transform); return; }
	Super::OnConstruction(Transform);
	RebuildPillarMesh();
}

#if WITH_EDITOR
void AEHB_Pillar::PostEditMove(bool bFinished)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditMove(bFinished); return; }
	if (bNotifyWhenMovedInEditor)
	{
		FTransform CurrentLocalTransform = GetElementLocalTransform();
		if (bEnableEditorRotationSnap)
		{
			const bool bHasRotationIntent = HasMeaningfulYawChange(CachedPreEditLocalTransform, CurrentLocalTransform);
			if (bIsTrackingEditorRotationDrag || bHasRotationIntent)
			{
				if (!bIsTrackingEditorRotationDrag)
				{
					bIsTrackingEditorRotationDrag = true;
					EditorRotationDragStartLocalTransform = CachedPreEditLocalTransform;
					EditorRotationDragUnsnappedYaw = GetYawFromTransform(EditorRotationDragStartLocalTransform);
					EditorRotationDragLastAppliedYaw = GetYawFromTransform(EditorRotationDragStartLocalTransform);
				}

				const float CurrentYaw = GetYawFromTransform(CurrentLocalTransform);
				const float UserFrameDeltaYaw = FMath::FindDeltaAngleDegrees(EditorRotationDragLastAppliedYaw, CurrentYaw);
				EditorRotationDragUnsnappedYaw = FRotator::NormalizeAxis(EditorRotationDragUnsnappedYaw + UserFrameDeltaYaw);

				const FTransform DesiredLocalTransform = MakeTransformWithYaw(CurrentLocalTransform, EditorRotationDragUnsnappedYaw);
				FTransform FinalLocalTransform = DesiredLocalTransform;
				ResolvePillarRotationSnapTransform(DesiredLocalTransform, FinalLocalTransform);

				if (!FinalLocalTransform.Equals(CurrentLocalTransform))
				{
					SetActorRelativeTransform(FinalLocalTransform);
					CurrentLocalTransform = FinalLocalTransform;
				}

				EditorRotationDragLastAppliedYaw = GetYawFromTransform(CurrentLocalTransform);
			}
		}

		const bool bHasTranslationIntent = HasMeaningfulLocationChange(CachedPreEditLocalTransform, CurrentLocalTransform);
		if (bIsTrackingEditorTranslationDrag || bHasTranslationIntent)
		{
			if (!bIsTrackingEditorTranslationDrag)
			{
				bIsTrackingEditorTranslationDrag = true;
				EditorTranslationDragStartLocalTransform = CachedPreEditLocalTransform;
				EditorTranslationDragUnsnappedLocalLocation = EditorTranslationDragStartLocalTransform.GetLocation();
				EditorTranslationDragLastAppliedLocalLocation = EditorTranslationDragStartLocalTransform.GetLocation();
			}

			const FVector CurrentLocalLocation = CurrentLocalTransform.GetLocation();
			const FVector UserFrameDeltaLocation = CurrentLocalLocation - EditorTranslationDragLastAppliedLocalLocation;
			EditorTranslationDragUnsnappedLocalLocation += UserFrameDeltaLocation;

			FTransform DesiredLocalTransform = CurrentLocalTransform;
			DesiredLocalTransform.SetLocation(EditorTranslationDragUnsnappedLocalLocation);
			FTransform FinalLocalTransform = DesiredLocalTransform;

			FTransform DesiredWorldTransform = DesiredLocalTransform;
			if (const USceneComponent* AttachParent = RootComponent ? RootComponent->GetAttachParent() : nullptr)
			{
				DesiredWorldTransform = DesiredLocalTransform * AttachParent->GetComponentTransform();
			}

			FVector SnappedWorldLocation = FVector::ZeroVector;
			FRotator SnappedWorldRotation = FRotator::ZeroRotator;
			const bool bResolvedSlabBoundary = ResolveFloorSlabBoundarySnap(
				DesiredWorldTransform.GetLocation(),
				DesiredWorldTransform.GetRotation().Rotator(),
				30.0f,
				SnappedWorldLocation,
				SnappedWorldRotation);
			if (bResolvedSlabBoundary)
			{
				SetActorLocationAndRotation(
					SnappedWorldLocation,
					SnappedWorldRotation,
					false,
					nullptr,
					ETeleportType::TeleportPhysics);
				FinalLocalTransform = GetElementLocalTransform();
			}

			// A resolved host boundary has priority over free wall-angle/grid snapping.
			// Rewriting its point here causes the later movement callback to snap back,
			// making the drag accumulator mistake that correction for user movement.
			if (!bResolvedSlabBoundary && OwningBuilding && ElementGuid.IsValid() && ConnectedWallGuids.Num() > 0)
			{
				FVector BestSnappedLocalLocation = FinalLocalTransform.GetLocation();
				double BestSnapDistanceSquared = TNumericLimits<double>::Max();
				for (const FGuid& ConnectedWallGuid : ConnectedWallGuids)
				{
					const AEHB_Wall* ConnectedWall = Cast<AEHB_Wall>(OwningBuilding->FindElementActorByGuid(ConnectedWallGuid));
					if (!ConnectedWall || ConnectedWall->IsActorBeingDestroyed())
					{
						continue;
					}

					const FGuid AnchorPillarGuid = ConnectedWall->EndPillarGuid == ElementGuid
						? ConnectedWall->StartPillarGuid
						: (ConnectedWall->StartPillarGuid == ElementGuid ? ConnectedWall->EndPillarGuid : FGuid());
					const AEHB_Pillar* AnchorPillar = AnchorPillarGuid.IsValid()
						? Cast<AEHB_Pillar>(OwningBuilding->FindElementActorByGuid(AnchorPillarGuid))
						: nullptr;
					if (!AnchorPillar || AnchorPillar->IsActorBeingDestroyed())
					{
						continue;
					}

					FVector CandidateLocalLocation = FVector::ZeroVector;
					if (!TrySnapLocalLocationToNearestOctantFromAnchor(
						AnchorPillar->GetElementLocalTransform().GetLocation(),
						FinalLocalTransform.GetLocation(),
						CandidateLocalLocation))
					{
						continue;
					}

					const double CandidateDistanceSquared = FVector::DistSquared2D(
						CandidateLocalLocation,
						FinalLocalTransform.GetLocation());
					if (CandidateDistanceSquared < BestSnapDistanceSquared)
					{
						BestSnapDistanceSquared = CandidateDistanceSquared;
						BestSnappedLocalLocation = CandidateLocalLocation;
					}
				}

				if (BestSnapDistanceSquared < TNumericLimits<double>::Max())
				{
					FinalLocalTransform.SetLocation(BestSnappedLocalLocation);
				}
			}

			if (!bResolvedSlabBoundary)
			{
				FinalLocalTransform.SetLocation(RoundPillarLocalCoordinates(FinalLocalTransform.GetLocation()));
			}
			const FTransform AppliedLocalTransform = GetElementLocalTransform();
			if (!FinalLocalTransform.Equals(AppliedLocalTransform))
			{
				SetActorRelativeTransform(FinalLocalTransform);
			}

		}
	}

	Super::PostEditMove(bFinished);
	if (bIsTrackingEditorTranslationDrag)
	{
		// Notification handlers may adjust the pose; only the final position is
		// a valid reference for computing the next user's drag delta.
		EditorTranslationDragLastAppliedLocalLocation = GetElementLocalTransform().GetLocation();
	}

	if (bFinished)
	{
		bIsTrackingEditorRotationDrag = false;
		EditorRotationDragStartLocalTransform = FTransform::Identity;
		EditorRotationDragUnsnappedYaw = 0.0f;
		EditorRotationDragLastAppliedYaw = 0.0f;
		bIsTrackingEditorTranslationDrag = false;
		EditorTranslationDragStartLocalTransform = FTransform::Identity;
		EditorTranslationDragUnsnappedLocalLocation = FVector::ZeroVector;
		EditorTranslationDragLastAppliedLocalLocation = FVector::ZeroVector;
	}
}

void AEHB_Pillar::SynchronizePlannedEditorMove()
{
	Super::SynchronizePlannedEditorMove();
	bIsTrackingEditorRotationDrag=false;EditorRotationDragStartLocalTransform=FTransform::Identity;
	EditorRotationDragUnsnappedYaw=0;EditorRotationDragLastAppliedYaw=0;
	bIsTrackingEditorTranslationDrag=false;EditorTranslationDragStartLocalTransform=FTransform::Identity;
	EditorTranslationDragUnsnappedLocalLocation=FVector::ZeroVector;EditorTranslationDragLastAppliedLocalLocation=FVector::ZeroVector;
}

void AEHB_Pillar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditChangeProperty(PropertyChangedEvent); return; }
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, true);
	}
}
#endif

void AEHB_Pillar::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	Super::OnElementActorMoved_Implementation(OldLocalTransform, NewLocalTransform, bFinished);
	const bool bSnappedToSlabBoundary = SnapToAdjacentFloorSlabBoundary(30.0f, bFinished);
	if (!bSnappedToSlabBoundary)
	{
		RebuildPillarMesh();
		if (OwningBuilding)
		{
			OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
		}
	}
}

void AEHB_Pillar::OnElementActorDeleted_Implementation()
{
	if (OwningBuilding)
	{
		OwningBuilding->HandlePillarDeleted(this);
	}
	Super::OnElementActorDeleted_Implementation();
}

void AEHB_Pillar::ConfigureAsPolygonPillar(float InHeight, float InWidth, float InDepth, const FTransform& InLocalTransform, bool bFinished)
{
	ShapeType = EEHBPillarShapeType::Polygon;
	SampledPillarRow = FDataTableRowHandle();
	Height = FMath::Max(1.0f, InHeight);
	Width = FMath::Max(1.0f, InWidth);
	Depth = FMath::Max(1.0f, InDepth);
	if (OverrideMaterial.IsNull())
	{
		OverrideMaterial = GetConfiguredDefaultWhiteBoxMaterial();
	}

	SetElementLocalTransform(InLocalTransform, bFinished);
	SnapToAdjacentFloorSlabCorner();
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
}

bool AEHB_Pillar::SnapToAdjacentFloorSlabCorner(float MaxDistance)
{
	return SnapToAdjacentFloorSlabBoundary(MaxDistance, true);
}

FEHBPillarSlabSnapPreview AEHB_Pillar::PreviewFloorSlabBoundarySnap(FVector DesiredWorldLocation, FRotator DesiredWorldRotation, float MaxDistance) const
{
	FEHBPillarSlabSnapPreview Result;
	Result.WorldLocation = DesiredWorldLocation;
	Result.WorldRotation = DesiredWorldRotation;
	if (DesiredWorldLocation.ContainsNaN() || DesiredWorldRotation.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance < 0) return Result;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}

	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeDepth = FMath::Max(1.0f, Depth);
	const float HalfWidth = ShapeType == EEHBPillarShapeType::Cylinder
		? FMath::Max(1.0f, Radius)
		: SafeWidth * 0.5f;
	const float HalfDepth = ShapeType == EEHBPillarShapeType::Cylinder
		? FMath::Max(1.0f, Radius)
		: SafeDepth * 0.5f;
	const float PillarCornerReach = FMath::Sqrt(HalfWidth * HalfWidth + HalfDepth * HalfDepth);
	const float SafeMaxDistance = FMath::Max(0.0f, MaxDistance);
	const float HeightTolerance = FMath::Max(45.0f, SafeMaxDistance * 1.5f);
	bool bHasBestCandidate = false;
	FEHBPillarSlabBoundarySnapCandidate BestCandidate;
	constexpr float EdgeCandidatePriorityPenalty = 1.0f;

	auto ConsiderCandidate =
		[&](
			const FVector& CandidateLocation,
			const FVector& AxisX,
			float ProximityScore,
			float PriorityPenalty, const FGuid& SlabGuid)
	{
		const FRotator CandidateRotation = MakeYawRotationFromWorldXAxis(AxisX);
		const float RotationDelta = FMath::Abs(FRotator::NormalizeAxis(CandidateRotation.Yaw - DesiredWorldRotation.Yaw));
		const float Score = ProximityScore + RotationDelta * 0.05f + PriorityPenalty;
		if (!bHasBestCandidate || Score < BestCandidate.Score)
		{
			bHasBestCandidate = true;
			BestCandidate.Score = Score;
			BestCandidate.WorldLocation = CandidateLocation;
			BestCandidate.WorldRotation = CandidateRotation;
			Result.SlabGuid = SlabGuid;
			Result.bCorner = PriorityPenalty == 0.0f;
		}
	};

	TArray<AEHB_FloorSlab*> Slabs;
	if (OwningBuilding)
	{
		TArray<AActor*> Attached;
		OwningBuilding->GetAttachedActors(Attached, true, true);
		for (AActor* Actor : Attached)
			if (auto* Slab = Cast<AEHB_FloorSlab>(Actor))
				if (!Slab->IsActorBeingDestroyed() && Slab->OwningBuilding == OwningBuilding) Slabs.Add(Slab);
	}
	else
	{
		// Unowned editor elements retain the legacy world-search fallback.
		for (TActorIterator<AEHB_FloorSlab> It(World); It; ++It)
			if (!It->IsActorBeingDestroyed() && !It->OwningBuilding) Slabs.Add(*It);
	}
	Slabs.Sort([](const AEHB_FloorSlab& A, const AEHB_FloorSlab& B)
	{
		if (A.ElementGuid != B.ElementGuid) return A.ElementGuid.ToString() < B.ElementGuid.ToString();
		return A.GetPathName() < B.GetPathName();
	});
	Result.CandidateSlabCount = Slabs.Num();
	for (AEHB_FloorSlab* Slab : Slabs)
	{
		TArray<TArray<FVector>> WorldPolygons;
		if (!Slab->BuildEffectiveOuterWorldPolygons(WorldPolygons, true, true))
		{
			continue;
		}
 for(const auto& WorldPolygon:WorldPolygons)
 {

		if (!IsSlabTopNearWorldZ(Slab, DesiredWorldLocation.Z, HeightTolerance))
		{
			continue;
		}

		const float SignedArea = CalculateSignedArea2D(WorldPolygon);
		if (FMath::Abs(SignedArea) <= UE_SMALL_NUMBER)
		{
			continue;
		}
		const bool bCounterClockwise = SignedArea > 0.0f;

		for (int32 EdgeIndex = 0; EdgeIndex < WorldPolygon.Num(); ++EdgeIndex)
		{
			const FVector& EdgeStart = WorldPolygon[EdgeIndex];
			const FVector& EdgeEnd = WorldPolygon[(EdgeIndex + 1) % WorldPolygon.Num()];
			FVector EdgeDirection(EdgeEnd.X - EdgeStart.X, EdgeEnd.Y - EdgeStart.Y, 0.0f);
			if (!EdgeDirection.Normalize())
			{
				continue;
			}

			const float Alpha = GetClosestAlphaOnSegment2D(EdgeStart, EdgeEnd, DesiredWorldLocation);
			if (Alpha <= KINDA_SMALL_NUMBER || Alpha >= 1.0f - KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector OutwardNormal = bCounterClockwise
				? FVector(EdgeDirection.Y, -EdgeDirection.X, 0.0f).GetSafeNormal()
				: FVector(-EdgeDirection.Y, EdgeDirection.X, 0.0f).GetSafeNormal();
			if (OutwardNormal.IsNearlyZero())
			{
				continue;
			}

			TArray<FVector> AxisXOptions;
			AddUniqueAxisXOption(AxisXOptions, EdgeDirection);
			AddUniqueAxisXOption(AxisXOptions, -OutwardNormal);

			for (const FVector& AxisX : AxisXOptions)
			{
				const float Inset = GetRectHalfExtentAlongWorldNormal(OutwardNormal, AxisX, HalfWidth, HalfDepth);
				FVector CandidateLocation = FMath::Lerp(EdgeStart, EdgeEnd, Alpha) - OutwardNormal * Inset;
				CandidateLocation.Z = DesiredWorldLocation.Z;

				const float CenterDistance = FVector::Dist2D(DesiredWorldLocation, CandidateLocation);
				if (CenterDistance > SafeMaxDistance)
				{
					continue;
				}

				ConsiderCandidate(CandidateLocation, AxisX, CenterDistance, EdgeCandidatePriorityPenalty, Slab->ElementGuid);
			}
		}

		for (int32 CornerIndex = 0; CornerIndex < WorldPolygon.Num(); ++CornerIndex)
		{
			const FVector& PrevPoint = WorldPolygon[(CornerIndex + WorldPolygon.Num() - 1) % WorldPolygon.Num()];
			const FVector& CornerPoint = WorldPolygon[CornerIndex];
			const FVector& NextPoint = WorldPolygon[(CornerIndex + 1) % WorldPolygon.Num()];

			FVector PrevDirection(CornerPoint.X - PrevPoint.X, CornerPoint.Y - PrevPoint.Y, 0.0f);
			FVector NextDirection(NextPoint.X - CornerPoint.X, NextPoint.Y - CornerPoint.Y, 0.0f);
			if (!PrevDirection.Normalize() || !NextDirection.Normalize())
			{
				continue;
			}

			const FVector PrevOutwardNormal = bCounterClockwise
				? FVector(PrevDirection.Y, -PrevDirection.X, 0.0f).GetSafeNormal()
				: FVector(-PrevDirection.Y, PrevDirection.X, 0.0f).GetSafeNormal();
			const FVector NextOutwardNormal = bCounterClockwise
				? FVector(NextDirection.Y, -NextDirection.X, 0.0f).GetSafeNormal()
				: FVector(-NextDirection.Y, NextDirection.X, 0.0f).GetSafeNormal();
			if (PrevOutwardNormal.IsNearlyZero() || NextOutwardNormal.IsNearlyZero())
			{
				continue;
			}

			TArray<FVector> AxisXOptions;
			AddUniqueAxisXOption(AxisXOptions, -PrevOutwardNormal);
			AddUniqueAxisXOption(AxisXOptions, -NextOutwardNormal);

			for (const FVector& AxisX : AxisXOptions)
			{
				const float PrevInset = GetRectHalfExtentAlongWorldNormal(PrevOutwardNormal, AxisX, HalfWidth, HalfDepth);
				const float NextInset = GetRectHalfExtentAlongWorldNormal(NextOutwardNormal, AxisX, HalfWidth, HalfDepth);
				FVector CandidateLocation = FVector::ZeroVector;
				if (!SolveOffsetPointFromCorner2D(
					CornerPoint,
					PrevOutwardNormal,
					NextOutwardNormal,
					PrevInset,
					NextInset,
					CandidateLocation))
				{
					continue;
				}
				CandidateLocation.Z = DesiredWorldLocation.Z;

				const float CenterDistance = FVector::Dist2D(DesiredWorldLocation, CandidateLocation);
				const float CornerReachDistance = FMath::Max(0.0f, FVector::Dist2D(DesiredWorldLocation, CornerPoint) - PillarCornerReach);
				if (CenterDistance > SafeMaxDistance && CornerReachDistance > SafeMaxDistance)
				{
					continue;
				}

				ConsiderCandidate(CandidateLocation, AxisX, FMath::Min(CenterDistance, CornerReachDistance), 0.0f, Slab->ElementGuid);
			}
		}

 }
}

	if (!bHasBestCandidate)
	{
		return Result;
	}

	Result.WorldLocation = BestCandidate.WorldLocation;
	Result.WorldRotation = BestCandidate.WorldRotation;
	Result.bFound = true;
	return Result;
}

bool AEHB_Pillar::ResolveFloorSlabBoundarySnap(const FVector& DesiredWorldLocation, const FRotator& DesiredWorldRotation, float MaxDistance, FVector& OutWorldLocation, FRotator& OutWorldRotation) const
{
	const auto Result = PreviewFloorSlabBoundarySnap(DesiredWorldLocation, DesiredWorldRotation, MaxDistance);
	if (!Result.bFound) return false;
	OutWorldLocation = Result.WorldLocation;
	OutWorldRotation = Result.WorldRotation;
	return true;
}

bool AEHB_Pillar::SnapToAdjacentFloorSlabBoundary(float MaxDistance, bool bFinished)
{
	FVector SnappedWorldLocation = FVector::ZeroVector;
	FRotator SnappedWorldRotation = FRotator::ZeroRotator;
	if (!ResolveFloorSlabBoundarySnap(
		GetActorLocation(),
		GetActorRotation(),
		MaxDistance,
		SnappedWorldLocation,
		SnappedWorldRotation))
	{
		return false;
	}

	const FVector CurrentWorldLocation = GetActorLocation();
	if (FVector::DistSquared2D(CurrentWorldLocation, SnappedWorldLocation) <= 0.01f
		&& FMath::Abs(FRotator::NormalizeAxis(GetActorRotation().Yaw - SnappedWorldRotation.Yaw)) <= 0.01f)
	{
		return false;
	}

	if (bFinished)
	{
		Modify();
	}
	SetActorLocationAndRotation(SnappedWorldLocation, SnappedWorldRotation, false, nullptr, ETeleportType::TeleportPhysics);
	if(OwningBuilding)OwningBuilding->RecordAuthoredWallNode(this);
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
	if (bFinished)
	{
		MarkPackageDirty();
	}
	return true;
}

bool AEHB_Pillar::ConfigureFromSampledPillarRow(UDataTable* InTable, FName InRowName, bool bFinished)
{
	if (!InTable || InRowName.IsNone() || InTable->GetRowStruct() != FEHBPillarMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBPillarMeshData* Row = InTable->FindRow<FEHBPillarMeshData>(
		InRowName,
		TEXT("AEHB_Pillar::ConfigureFromSampledPillarRow"),
		false);
	if (!Row || Row->Vertices.IsEmpty() || Row->Triangles.Num() < 3)
	{
		return false;
	}

	Modify();
	ShapeType = EEHBPillarShapeType::StaticMesh;
	SourceStaticMesh = Row->SourceStaticMesh;
	SampledPillarRow.DataTable = InTable;
	SampledPillarRow.RowName = InRowName;
	Width = FMath::Max(1.0f, Row->Width);
	Depth = FMath::Max(1.0f, Row->Depth);
	Height = FMath::Max(1.0f, Row->Height);

	SnapToAdjacentFloorSlabCorner();
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
	MarkPackageDirty();
	return true;
}

bool AEHB_Pillar::SetConnectedWallSurfaceOverride(FGuid InWallGuid, const FEHBWallSurfaceStyle& InSurfaceStyle, bool bFinished)
{
	if (!InWallGuid.IsValid() || !HasUsablePillarSurfaceStyle(InSurfaceStyle))
	{
		return false;
	}

	Modify();
	FEHBPillarConnectedSurfaceOverride* ExistingOverride = ConnectedSurfaceOverrides.FindByPredicate(
		[InWallGuid](const FEHBPillarConnectedSurfaceOverride& Override)
		{
			return Override.WallGuid == InWallGuid;
		});

	if (!ExistingOverride)
	{
		ExistingOverride = &ConnectedSurfaceOverrides.AddDefaulted_GetRef();
		ExistingOverride->WallGuid = InWallGuid;
	}

	ExistingOverride->bEnabled = true;
	ExistingOverride->SurfaceStyle = InSurfaceStyle;
	ExistingOverride->bUsePerSideSurfaceStyles = false;
	ExistingOverride->LeftWallSideSurfaceStyle = InSurfaceStyle;
	ExistingOverride->RightWallSideSurfaceStyle = InSurfaceStyle;
	ExistingOverride->WallEndCapSurfaceStyle = InSurfaceStyle;
	ExistingOverride->bApplyLeftWallSide = true;
	ExistingOverride->bApplyRightWallSide = true;
	ExistingOverride->bApplyWallEndCap = true;
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
	MarkPackageDirty();
	return true;
}

bool AEHB_Pillar::SetConnectedWallSurfaceOverrideForSide(
	FGuid InWallGuid,
	const FEHBWallSurfaceStyle& InSurfaceStyle,
	bool bLeftSide,
	bool bIncludeEndCap,
	bool bFinished)
{
	if (!InWallGuid.IsValid() || !HasUsablePillarSurfaceStyle(InSurfaceStyle))
	{
		return false;
	}

	Modify();
	FEHBPillarConnectedSurfaceOverride* ExistingOverride = ConnectedSurfaceOverrides.FindByPredicate(
		[InWallGuid](const FEHBPillarConnectedSurfaceOverride& Override)
		{
			return Override.WallGuid == InWallGuid;
		});

	if (!ExistingOverride)
	{
		ExistingOverride = &ConnectedSurfaceOverrides.AddDefaulted_GetRef();
		ExistingOverride->WallGuid = InWallGuid;
		ExistingOverride->bApplyLeftWallSide = false;
		ExistingOverride->bApplyRightWallSide = false;
		ExistingOverride->bApplyWallEndCap = false;
	}
	else if (!ExistingOverride->bUsePerSideSurfaceStyles)
	{
		ExistingOverride->LeftWallSideSurfaceStyle = ExistingOverride->SurfaceStyle;
		ExistingOverride->RightWallSideSurfaceStyle = ExistingOverride->SurfaceStyle;
		ExistingOverride->WallEndCapSurfaceStyle = ExistingOverride->SurfaceStyle;
	}

	ExistingOverride->bEnabled = true;
	ExistingOverride->bUsePerSideSurfaceStyles = true;
	ExistingOverride->SurfaceStyle = InSurfaceStyle;
	if (bLeftSide)
	{
		ExistingOverride->bApplyLeftWallSide = true;
		ExistingOverride->LeftWallSideSurfaceStyle = InSurfaceStyle;
	}
	else
	{
		ExistingOverride->bApplyRightWallSide = true;
		ExistingOverride->RightWallSideSurfaceStyle = InSurfaceStyle;
	}
	if (bIncludeEndCap)
	{
		ExistingOverride->bApplyWallEndCap = true;
		ExistingOverride->WallEndCapSurfaceStyle = InSurfaceStyle;
	}
	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
	MarkPackageDirty();
	return true;
}

bool AEHB_Pillar::ClearConnectedWallSurfaceOverride(FGuid InWallGuid, bool bFinished)
{
	if (!InWallGuid.IsValid())
	{
		return false;
	}

	Modify();
	const int32 RemovedCount = ConnectedSurfaceOverrides.RemoveAll(
		[InWallGuid](const FEHBPillarConnectedSurfaceOverride& Override)
		{
			return Override.WallGuid == InWallGuid;
		});
	if (RemovedCount <= 0)
	{
		return false;
	}

	RebuildPillarMesh();
	if (OwningBuilding)
	{
		OwningBuilding->RefreshWallsConnectedToPillar(ElementGuid, bFinished);
	}
	MarkPackageDirty();
	return true;
}

bool AEHB_Pillar::ResolvePillarRotationSnapTransform(const FTransform& DesiredLocalTransform, FTransform& ResolvedLocalTransform) const
{
	ResolvedLocalTransform = DesiredLocalTransform;
	if (!bEnableEditorRotationSnap || RotationSnapAngleThreshold <= 0.0f)
	{
		return false;
	}

	switch (ShapeType)
	{
	case EEHBPillarShapeType::Polygon:
		return ResolvePolygonPillarRotationSnapTransform(DesiredLocalTransform, ResolvedLocalTransform);

	case EEHBPillarShapeType::Cylinder:
	case EEHBPillarShapeType::StaticMesh:
	default:
		return false;
	}
}

bool AEHB_Pillar::ResolvePolygonPillarRotationSnapTransform(const FTransform& DesiredLocalTransform, FTransform& ResolvedLocalTransform) const
{
	float SnappedYaw = 0.0f;
	float BestDelta = 0.0f;
	if (!FindBestConnectedWallSideSnapYaw(GetYawFromTransform(DesiredLocalTransform), SnappedYaw, BestDelta))
	{
		return false;
	}

	ResolvedLocalTransform = DesiredLocalTransform;
	FRotator SnappedRotation = DesiredLocalTransform.GetRotation().Rotator();
	SnappedRotation.Yaw = SnappedYaw;
	SnappedRotation.Normalize();
	ResolvedLocalTransform.SetRotation(SnappedRotation.Quaternion());
	return true;
}

bool AEHB_Pillar::FindBestConnectedWallSideSnapYaw(float DesiredYaw, float& OutSnappedYaw, float& OutBestDelta) const
{
	if (!OwningBuilding || ConnectedWallGuids.Num() <= 0)
	{
		return false;
	}

	TArray<AActor*> AttachedActors;
	OwningBuilding->GetAttachedActors(AttachedActors);

	const float SnapThreshold = FMath::Clamp(RotationSnapAngleThreshold, 0.0f, 45.0f);
	float BestDelta = SnapThreshold;
	float BestYaw = DesiredYaw;
	bool bFoundSnapTarget = false;

	for (const FGuid& WallGuid : ConnectedWallGuids)
	{
		if (!WallGuid.IsValid())
		{
			continue;
		}

		const AEHB_Wall* ConnectedWall = nullptr;
		for (AActor* AttachedActor : AttachedActors)
		{
			const AEHB_Wall* CandidateWall = Cast<AEHB_Wall>(AttachedActor);
			if (CandidateWall && CandidateWall->ElementGuid == WallGuid)
			{
				ConnectedWall = CandidateWall;
				break;
			}
		}

		if (!ConnectedWall)
		{
			continue;
		}

		const FVector WallDirection = (ConnectedWall->LocalEnd - ConnectedWall->LocalStart).GetSafeNormal2D();
		if (WallDirection.IsNearlyZero())
		{
			continue;
		}

		const float WallYaw = WallDirection.Rotation().Yaw;
		const float CandidateYaws[4] = {
			WallYaw,
			WallYaw + 90.0f,
			WallYaw + 180.0f,
			WallYaw + 270.0f
		};

		for (float CandidateYaw : CandidateYaws)
		{
			const float NormalizedCandidateYaw = FRotator::NormalizeAxis(CandidateYaw);
			const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredYaw, NormalizedCandidateYaw));
			if (Delta <= BestDelta)
			{
				BestDelta = Delta;
				BestYaw = NormalizedCandidateYaw;
				bFoundSnapTarget = true;
			}
		}
	}

	if (!bFoundSnapTarget)
	{
		return false;
	}

	OutSnappedYaw = BestYaw;
	OutBestDelta = BestDelta;
	return true;
}

bool AEHB_Pillar::ResolveWallConnectionPointToward(const FVector& TargetLocalLocation, float WallThickness, FVector& OutLocalLocation) const
{
	const FTransform PillarLocalTransform = GetElementLocalTransform();
	const FVector PillarCenter = PillarLocalTransform.GetLocation();
	OutLocalLocation = PillarCenter;

	FVector LeftLocalLocation = FVector::ZeroVector;
	FVector RightLocalLocation = FVector::ZeroVector;
	if (!ResolveWallConnectionFaceToward(TargetLocalLocation, WallThickness, LeftLocalLocation, RightLocalLocation))
	{
		return ShapeType == EEHBPillarShapeType::StaticMesh;
	}

	OutLocalLocation = (LeftLocalLocation + RightLocalLocation) * 0.5f;
	OutLocalLocation.Z = PillarCenter.Z;
	return true;
}

bool AEHB_Pillar::ResolveWallConnectionFaceToward(const FVector& TargetLocalLocation, float WallThickness, FVector& OutLeftLocalLocation, FVector& OutRightLocalLocation) const
{
	const FTransform PillarLocalTransform = GetElementLocalTransform();
	const FVector PillarCenter = PillarLocalTransform.GetLocation();
	OutLeftLocalLocation = PillarCenter;
	OutRightLocalLocation = PillarCenter;

	if (ShapeType == EEHBPillarShapeType::StaticMesh)
	{
		return false;
	}

	FVector DirectionInBuilding = TargetLocalLocation - PillarCenter;
	DirectionInBuilding.Z = 0.0f;
	if (!DirectionInBuilding.Normalize())
	{
		return false;
	}

	if (ShapeType == EEHBPillarShapeType::Cylinder)
	{
		const float SafeRadius = FMath::Max(1.0f, Radius);
		const float HalfWallThickness = FMath::Clamp(WallThickness * 0.5f, 1.0f, FMath::Max(1.0f, SafeRadius * 0.98f));
		const float CutDepth = FMath::Sqrt(FMath::Max(0.0f, SafeRadius * SafeRadius - HalfWallThickness * HalfWallThickness));
		const FVector LeftNormal = DirectionInBuilding.RotateAngleAxis(-90.0f, FVector::UpVector).GetSafeNormal2D();
		OutLeftLocalLocation = PillarCenter + DirectionInBuilding * CutDepth + LeftNormal * HalfWallThickness;
		OutRightLocalLocation = PillarCenter + DirectionInBuilding * CutDepth - LeftNormal * HalfWallThickness;
		OutLeftLocalLocation.Z = PillarCenter.Z;
		OutRightLocalLocation.Z = PillarCenter.Z;
		return true;
	}

	FVector DirectionInPillar = PillarLocalTransform
		.InverseTransformVectorNoScale(DirectionInBuilding)
		.GetSafeNormal2D();
	if (DirectionInPillar.IsNearlyZero())
	{
		return false;
	}

	TArray<FVector> LocalFootprint;
	LocalFootprint = PolygonPillarFootprint;
	if (LocalFootprint.Num() < 3 && !BuildPolygonPillarFootprint(LocalFootprint))
	{
		return false;
	}
	if (LocalFootprint.Num() < 3)
	{
		return false;
	}

	FVector ConnectionLeftLocalPoint,ConnectionRightLocalPoint;
	if(!FEHBWallJunctionGeometry::ResolveFace(Width,Depth,LocalFootprint,DirectionInPillar,WallThickness,ConnectionLeftLocalPoint,ConnectionRightLocalPoint))return false;

	OutLeftLocalLocation = PillarLocalTransform.TransformPosition(ConnectionLeftLocalPoint);
	OutRightLocalLocation = PillarLocalTransform.TransformPosition(ConnectionRightLocalPoint);
	OutLeftLocalLocation.Z = PillarCenter.Z;
	OutRightLocalLocation.Z = PillarCenter.Z;
	return true;
}

void AEHB_Pillar::UpdateConnectedWallConnectionFaces()
{
	if (!OwningBuilding || !ElementGuid.IsValid())
	{
		return;
	}

	TArray<AActor*> AttachedActors;
	OwningBuilding->GetAttachedActors(AttachedActors);

	auto FindWallByGuid = [&AttachedActors](const FGuid& WallGuid) -> AEHB_Wall*
	{
		if (!WallGuid.IsValid())
		{
			return nullptr;
		}

		for (AActor* AttachedActor : AttachedActors)
		{
			AEHB_Wall* CandidateWall = Cast<AEHB_Wall>(AttachedActor);
			if (CandidateWall && CandidateWall->ElementGuid == WallGuid)
			{
				return CandidateWall;
			}
		}

		return nullptr;
	};

	auto FindPillarByGuid = [&AttachedActors](const FGuid& PillarGuid) -> AEHB_Pillar*
	{
		if (!PillarGuid.IsValid())
		{
			return nullptr;
		}

		for (AActor* AttachedActor : AttachedActors)
		{
			AEHB_Pillar* CandidatePillar = Cast<AEHB_Pillar>(AttachedActor);
			if (CandidatePillar && CandidatePillar->ElementGuid == PillarGuid)
			{
				return CandidatePillar;
			}
		}

		return nullptr;
	};

	for (const FGuid& WallGuid : ConnectedWallGuids)
	{
		AEHB_Wall* ConnectedWall = FindWallByGuid(WallGuid);
		if (!ConnectedWall)
		{
			continue;
		}

		FGuid TargetPillarGuid = FGuid();
		FVector TargetLocalLocation = FVector::ZeroVector;
		if (ConnectedWall->StartPillarGuid == ElementGuid)
		{
			TargetPillarGuid = ConnectedWall->EndPillarGuid;
			TargetLocalLocation = ConnectedWall->LocalEnd;
		}
		else if (ConnectedWall->EndPillarGuid == ElementGuid)
		{
			TargetPillarGuid = ConnectedWall->StartPillarGuid;
			TargetLocalLocation = ConnectedWall->LocalStart;
		}
		else
		{
			ConnectedWall->ClearPillarConnectionFacePoints(this);
			continue;
		}

		if (const AEHB_Pillar* TargetPillar = FindPillarByGuid(TargetPillarGuid))
		{
			TargetLocalLocation = TargetPillar->GetElementLocalTransform().GetLocation();
		}

		const float ConnectedWallLength = FVector::Dist2D(ConnectedWall->LocalStart, ConnectedWall->LocalEnd);
		const bool bConnectedWallIsCurved =
			FMath::Abs(ConnectedWall->CurveControlOffset) > 0.1f
			&& ConnectedWallLength > 1.0f;
		if (bConnectedWallIsCurved)
		{
			const bool bConnectedAtStart = ConnectedWall->StartPillarGuid == ElementGuid;
			const float EndpointX = bConnectedAtStart ? -ConnectedWallLength * 0.5f : ConnectedWallLength * 0.5f;
			FVector WallLocalTangent = ConnectedWall->TransformStraightWallLocalVectorToCurve(
				FVector(EndpointX, 0.0f, 0.0f),
				FVector::ForwardVector);
			if (!bConnectedAtStart)
			{
				WallLocalTangent *= -1.0f;
			}

			const FVector DirectionInBuilding = ConnectedWall->GetElementLocalTransform()
				.TransformVectorNoScale(WallLocalTangent)
				.GetSafeNormal2D();
			if (!DirectionInBuilding.IsNearlyZero())
			{
				TargetLocalLocation = GetElementLocalTransform().GetLocation() + DirectionInBuilding;
			}
		}

		FVector LeftLocalPoint = FVector::ZeroVector;
		FVector RightLocalPoint = FVector::ZeroVector;
		if (ResolveWallConnectionFaceToward(TargetLocalLocation, ConnectedWall->Thickness, LeftLocalPoint, RightLocalPoint))
		{
			ConnectedWall->SetPillarConnectionFacePoints(this, LeftLocalPoint, RightLocalPoint);
		}
		else
		{
			ConnectedWall->ClearPillarConnectionFacePoints(this);
		}
	}
}

void AEHB_Pillar::RebuildPillarMesh()
{
	// Connection IDs are transient. Restore relationship-derived boundary caches
	// before generating junction geometry after component registration/load.
	if (OwningBuilding && ElementGuid.IsValid())
		OwningBuilding->GetClosedLoopsByPillarGuid(ElementGuid);

	const auto UpdatePillarSurfaceData = [this]()
	{
		UEHBVerticalSurfaceComponent* SurfaceComponent = Cast<UEHBVerticalSurfaceComponent>(PillarMeshComponent);
		if (!SurfaceComponent)
		{
			return;
		}

		SurfaceComponent->InitializeSurface(this, EEHBArchitecturalSurfaceRole::PillarSide, TEXT("Pillar.Side"), INDEX_NONE);
		if (PolygonPillarFootprint.Num() >= 3)
		{
			SurfaceComponent->SetVerticalSurfaceData(
				PolygonPillarFootprint,
				FMath::Max(1.0f, Height),
				0.0f,
				true,
				EHBBasicPillarUVWorldSize);
		}
		else
		{
			SurfaceComponent->LocalBasePolyline.Reset();
		}
	};

	if (ShapeType == EEHBPillarShapeType::Polygon)
	{
		RebuildPolygonPillarMeshData();
		UpdatePillarSurfaceData();
		ApplyPillarMeshToComponent();
		UpdateConnectedWallConnectionFaces();
		return;
	}

	if (ShapeType == EEHBPillarShapeType::StaticMesh)
	{
		TArray<FVector> SampledVertices;
		TArray<int32> SampledTriangles;
		TArray<FVector> SampledNormals;
		TArray<FVector2D> SampledUVs;
		TArray<int32> SampledTriangleMaterialIndices;
		TArray<TSoftObjectPtr<UMaterialInterface>> SampledMaterials;
		if (BuildSampledPillarMeshData(
			SampledVertices,
			SampledTriangles,
			SampledNormals,
			SampledUVs,
			SampledTriangleMaterialIndices,
			SampledMaterials))
		{
			PolygonPillarVertices = SampledVertices;
			PolygonPillarTriangles = SampledTriangles;
			PolygonPillarNormals = SampledNormals;
			PolygonPillarUVs = SampledUVs;
			PolygonPillarFootprint.Reset();
			UpdatePillarSurfaceData();
			ApplyPillarMeshToComponent(
				SampledVertices,
				SampledTriangles,
				SampledNormals,
				SampledUVs,
				SampledTriangleMaterialIndices,
				SampledMaterials);
			UpdateConnectedWallConnectionFaces();
			return;
		}
	}

	RebuildPolygonPillarMeshData();
	UpdatePillarSurfaceData();
	ApplyPillarMeshToComponent();
	UpdateConnectedWallConnectionFaces();
}

bool AEHB_Pillar::BuildSampledPillarMeshData(
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

	if (!SampledPillarRow.DataTable
		|| SampledPillarRow.RowName.IsNone()
		|| SampledPillarRow.DataTable->GetRowStruct() != FEHBPillarMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBPillarMeshData* Row = SampledPillarRow.DataTable->FindRow<FEHBPillarMeshData>(
		SampledPillarRow.RowName,
		TEXT("AEHB_Pillar::BuildSampledPillarMeshData"),
		false);
	if (!Row || Row->Vertices.IsEmpty() || Row->Triangles.Num() < 3)
	{
		return false;
	}

	const bool bHasValidBounds =
		Row->SourceBoundsMax.X > Row->SourceBoundsMin.X
		&& Row->SourceBoundsMax.Y > Row->SourceBoundsMin.Y
		&& Row->SourceBoundsMax.Z > Row->SourceBoundsMin.Z;
	const FVector SourceMin = bHasValidBounds
		? Row->SourceBoundsMin
		: FVector(-FMath::Max(1.0f, Row->Width) * 0.5f, -FMath::Max(1.0f, Row->Depth) * 0.5f, 0.0f);
	const FVector SourceMax = bHasValidBounds
		? Row->SourceBoundsMax
		: FVector(FMath::Max(1.0f, Row->Width) * 0.5f, FMath::Max(1.0f, Row->Depth) * 0.5f, FMath::Max(1.0f, Row->Height));
	const FVector SourceSize = SourceMax - SourceMin;
	const FVector SourceCenter(
		(SourceMin.X + SourceMax.X) * 0.5f,
		(SourceMin.Y + SourceMax.Y) * 0.5f,
		SourceMin.Z);

	const float SafeSourceWidth = FMath::Max(1.0f, SourceSize.X);
	const float SafeSourceDepth = FMath::Max(1.0f, SourceSize.Y);
	const float SafeSourceHeight = FMath::Max(1.0f, SourceSize.Z);
	const FVector Scale(
		FMath::Max(1.0f, Width) / SafeSourceWidth,
		FMath::Max(1.0f, Depth) / SafeSourceDepth,
		FMath::Max(1.0f, Height) / SafeSourceHeight);

	OutVertices.Reserve(Row->Vertices.Num());
	OutNormals.Reserve(Row->Vertices.Num());
	OutUVs.Reserve(Row->Vertices.Num());
	for (const FEHBPillarMeshSampleVertex& SourceVertex : Row->Vertices)
	{
		const FVector RelativePosition = SourceVertex.Position - SourceCenter;
		OutVertices.Add(FVector(
			RelativePosition.X * Scale.X,
			RelativePosition.Y * Scale.Y,
			RelativePosition.Z * Scale.Z));

		FVector Normal(
			SourceVertex.Normal.X / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Scale.X)),
			SourceVertex.Normal.Y / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Scale.Y)),
			SourceVertex.Normal.Z / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Scale.Z)));
		Normal = Normal.GetSafeNormal();
		OutNormals.Add(Normal.IsNearlyZero() ? FVector::UpVector : Normal);
		OutUVs.Add(SourceVertex.UV0);
	}

	OutTriangles = Row->Triangles;
	OutTriangleMaterialIndices = Row->TriangleMaterialIndices;
	OutMaterials = Row->Materials;
	return true;
}

bool AEHB_Pillar::BuildPolygonPillarFootprint(TArray<FVector>& OutLocalFootprint) const
{
	TArray<FEHBWallJunctionLeg> Legs;
	if (OwningBuilding && ConnectedWallGuids.Num() > 0 && ElementGuid.IsValid())
	{
		TArray<AActor*> AttachedActors;
		OwningBuilding->GetAttachedActors(AttachedActors);

		const FTransform PillarLocalTransform = GetElementLocalTransform();
		const FVector PillarLocalLocation = PillarLocalTransform.GetLocation();

		for (const FGuid& WallGuid : ConnectedWallGuids)
		{
			if (!WallGuid.IsValid())
			{
				continue;
			}

			const AEHB_Wall* ConnectedWall = nullptr;
			for (AActor* AttachedActor : AttachedActors)
			{
				const AEHB_Wall* CandidateWall = Cast<AEHB_Wall>(AttachedActor);
				if (CandidateWall && CandidateWall->ElementGuid == WallGuid)
				{
					ConnectedWall = CandidateWall;
					break;
				}
			}

			if (!ConnectedWall)
			{
				continue;
			}

			const float WallLength = FVector::Dist2D(ConnectedWall->LocalStart, ConnectedWall->LocalEnd);
			if (WallLength <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const bool bConnectedAtStart = ConnectedWall->StartPillarGuid == ElementGuid;
			const bool bConnectedAtEnd = ConnectedWall->EndPillarGuid == ElementGuid;
			FVector DirectionInBuilding = FVector::ZeroVector;
			if (bConnectedAtStart || bConnectedAtEnd)
			{
				const float EndpointX = bConnectedAtStart ? -WallLength * 0.5f : WallLength * 0.5f;
				const FVector WallLocalEndpoint(EndpointX, 0.0f, 0.0f);
				FVector WallLocalTangent = ConnectedWall->TransformStraightWallLocalVectorToCurve(
					WallLocalEndpoint,
					FVector::ForwardVector);
				if (bConnectedAtEnd)
				{
					WallLocalTangent *= -1.0f;
				}

				DirectionInBuilding = ConnectedWall->GetElementLocalTransform()
					.TransformVectorNoScale(WallLocalTangent)
					.GetSafeNormal2D();
			}

			if (DirectionInBuilding.IsNearlyZero())
			{
				const float StartDistance = FVector::DistSquared2D(ConnectedWall->LocalStart, PillarLocalLocation);
				const float EndDistance = FVector::DistSquared2D(ConnectedWall->LocalEnd, PillarLocalLocation);
				DirectionInBuilding = StartDistance <= EndDistance
					? ConnectedWall->LocalEnd - PillarLocalLocation
					: ConnectedWall->LocalStart - PillarLocalLocation;
			}

			FVector Direction = PillarLocalTransform.InverseTransformVectorNoScale(DirectionInBuilding).GetSafeNormal2D();
			if (Direction.IsNearlyZero())
			{
				continue;
			}

			FEHBWallJunctionLeg& Leg=Legs.AddDefaulted_GetRef();
			Leg.Direction=Direction;
			Leg.WallThickness=ConnectedWall->Thickness;
		}
	}
	return FEHBWallJunctionGeometry::BuildFootprint(Width,Depth,Legs,OutLocalFootprint);
}

void AEHB_Pillar::RebuildPolygonPillarMeshData()
{
	PolygonPillarVertices.Reset();
	PolygonPillarTriangles.Reset();
	PolygonPillarNormals.Reset();
	PolygonPillarUVs.Reset();
	PolygonPillarTriangleMaterialIndices.Reset();
	PolygonPillarSourceMaterials.Reset();
	PolygonPillarFootprint.Reset();

	{
		const float SafeHeight = FMath::Max(1.0f, Height);
		if (!BuildPolygonPillarFootprint(PolygonPillarFootprint) || PolygonPillarFootprint.Num() < 3)
		{
			return;
		}

		if (CalculateSignedArea2D(PolygonPillarFootprint) < 0.0f)
		{
			Algo::Reverse(PolygonPillarFootprint);
		}

		TSoftObjectPtr<UMaterialInterface> BaseMaterial = OverrideMaterial;
		if (BaseMaterial.IsNull())
		{
			BaseMaterial = GetConfiguredDefaultWhiteBoxMaterial();
		}
		PolygonPillarSourceMaterials.Add(BaseMaterial);

		TArray<FEHBPillarSurfaceCandidate> SurfaceCandidates;
		GatherPillarSurfaceCandidates(*this, SurfaceCandidates);
		// Plain linked columns use the validated prism triangulation for both convex and concave caps.
		const bool bPlainSharedMaterial=!SurfaceCandidates.ContainsByPredicate([&](const auto& Candidate){return Candidate.SurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple||(!Candidate.SurfaceStyle.OverrideMaterial.IsNull()&&Candidate.SurfaceStyle.OverrideMaterial!=BaseMaterial);});
		FEHBWallJunctionMesh SharedMesh;
		const bool bValidPrism = FEHBWallJunctionMeshBuilder::BuildPrism(PolygonPillarFootprint, SafeHeight, SharedMesh);
		if (!bValidPrism) { return; }
		if(bGenerateLinkedWallFaces&&ConnectedSurfaceOverrides.IsEmpty()&&bPlainSharedMaterial
			&&bValidPrism)
		{
			PolygonPillarVertices=MoveTemp(SharedMesh.Vertices);PolygonPillarTriangles=MoveTemp(SharedMesh.Triangles);
			PolygonPillarNormals=MoveTemp(SharedMesh.Normals);PolygonPillarUVs=MoveTemp(SharedMesh.UVs);
			PolygonPillarTriangleMaterialIndices.Init(0,PolygonPillarTriangles.Num()/3);
			return;
		}
		TArray<FVector> ConnectedWallEndCapNormals;
		GatherPillarConnectedWallEndCapNormals(*this, ConnectedWallEndCapNormals);

		const FVector TopOffset(0.0f, 0.0f, SafeHeight);
		// Keep side-specific styles and hidden linked faces below, but use the same
		// validated caps as the plain mesh. Fan caps overfill concave junctions.
		for (int32 Index = 0; Index + 2 < SharedMesh.Triangles.Num(); Index += 3)
		{
			const int32 IA = SharedMesh.Triangles[Index];
			const FVector Normal = SharedMesh.Normals[IA];
			if (FMath::Abs(Normal.Z) < 0.999f) { continue; }
			const int32 First = PolygonPillarVertices.Num();
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 Source = SharedMesh.Triangles[Index + Corner];
				PolygonPillarVertices.Add(SharedMesh.Vertices[Source]);
				PolygonPillarNormals.Add(SharedMesh.Normals[Source]);
				PolygonPillarUVs.Add(SharedMesh.UVs[Source]);
				PolygonPillarTriangles.Add(First + Corner);
			}
			PolygonPillarTriangleMaterialIndices.Add(0);
		}

		auto AddQuadFace = [this](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, float ULength, float VLength, int32 MaterialIndex = 0)
		{
			const int32 FirstIndex = PolygonPillarVertices.Num();
			const FVector SafeNormal = Normal.GetSafeNormal();

			PolygonPillarVertices.Add(A);
			PolygonPillarVertices.Add(B);
			PolygonPillarVertices.Add(C);
			PolygonPillarVertices.Add(D);

			const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (FVector::DotProduct(TriangleNormal, SafeNormal) <= 0.0f)
			{
				PolygonPillarTriangles.Add(FirstIndex + 0);
				PolygonPillarTriangles.Add(FirstIndex + 1);
				PolygonPillarTriangles.Add(FirstIndex + 2);
				PolygonPillarTriangles.Add(FirstIndex + 0);
				PolygonPillarTriangles.Add(FirstIndex + 2);
				PolygonPillarTriangles.Add(FirstIndex + 3);
			}
			else
			{
				PolygonPillarTriangles.Add(FirstIndex + 0);
				PolygonPillarTriangles.Add(FirstIndex + 2);
				PolygonPillarTriangles.Add(FirstIndex + 1);
				PolygonPillarTriangles.Add(FirstIndex + 0);
				PolygonPillarTriangles.Add(FirstIndex + 3);
				PolygonPillarTriangles.Add(FirstIndex + 2);
			}

			PolygonPillarNormals.Add(SafeNormal);
			PolygonPillarNormals.Add(SafeNormal);
			PolygonPillarNormals.Add(SafeNormal);
			PolygonPillarNormals.Add(SafeNormal);

			PolygonPillarUVs.Add(FVector2D(0.0f, VLength));
			PolygonPillarUVs.Add(FVector2D(0.0f, 0.0f));
			PolygonPillarUVs.Add(FVector2D(ULength, 0.0f));
			PolygonPillarUVs.Add(FVector2D(ULength, VLength));
			PolygonPillarTriangleMaterialIndices.Add(FMath::Max(0, MaterialIndex));
			PolygonPillarTriangleMaterialIndices.Add(FMath::Max(0, MaterialIndex));
		};

		const float HeightUV = GetBasicPillarUVLength(SafeHeight);
		for (int32 Index = 0; Index < PolygonPillarFootprint.Num(); ++Index)
		{
			const FVector BottomA = PolygonPillarFootprint[Index];
			const FVector BottomB = PolygonPillarFootprint[(Index + 1) % PolygonPillarFootprint.Num()];
			const FVector Edge = BottomB - BottomA;
			if (Edge.SizeSquared2D() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector TopA = BottomA + TopOffset;
			const FVector TopB = BottomB + TopOffset;
			const FVector OutwardNormal(Edge.Y, -Edge.X, 0.0f);
			const float EdgeUV = GetBasicPillarUVLength(FVector::Dist2D(BottomA, BottomB));

			FEHBPillarSurfaceCandidate SurfaceCandidate;
			const bool bHasSurfaceCandidate = FindBestSurfaceCandidateForEdge(SurfaceCandidates, OutwardNormal, SurfaceCandidate);
			const bool bIsLinkedWallFace = HasMatchingConnectedWallFaceNormal(ConnectedWallEndCapNormals, OutwardNormal);
			if (bIsLinkedWallFace && !bGenerateLinkedWallFaces)
			{
				continue;
			}

			if (bHasSurfaceCandidate
				&& TryBuildSampledPillarFaceFromWallSurface(
					*this,
					SurfaceCandidate,
					BottomA,
					BottomB,
					OutwardNormal,
					PolygonPillarVertices,
					PolygonPillarTriangles,
					PolygonPillarNormals,
					PolygonPillarUVs,
					PolygonPillarTriangleMaterialIndices,
					PolygonPillarSourceMaterials))
			{
				continue;
			}

			int32 MaterialIndex = 0;
			if (bHasSurfaceCandidate && !SurfaceCandidate.SurfaceStyle.OverrideMaterial.IsNull())
			{
				MaterialIndex = PolygonPillarSourceMaterials.AddUnique(SurfaceCandidate.SurfaceStyle.OverrideMaterial);
			}

			AddQuadFace(BottomA, TopA, TopB, BottomB, OutwardNormal, EdgeUV, HeightUV, MaterialIndex);
		}

		return;
	}

}

void AEHB_Pillar::ApplyPillarMeshToComponent()
{
	if (!PillarMeshComponent)
	{
		return;
	}

	if (!PolygonPillarTriangleMaterialIndices.IsEmpty())
	{
		ApplyPillarMeshToComponent(
			PolygonPillarVertices,
			PolygonPillarTriangles,
			PolygonPillarNormals,
			PolygonPillarUVs,
			PolygonPillarTriangleMaterialIndices,
			PolygonPillarSourceMaterials,
			false);
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, PolygonPillarVertices.Num());
	TArray<FProcMeshTangent> Tangents;

	PillarMeshComponent->Modify();
	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(PillarMeshComponent);
	PillarMeshComponent->CreateMeshSection_LinearColor(
		0,
		PolygonPillarVertices,
		PolygonPillarTriangles,
		PolygonPillarNormals,
		PolygonPillarUVs,
		VertexColors,
		Tangents,
		true);
	PillarMeshComponent->SetMeshSectionName(0, FName(TEXT("PillarSurface")));
	PillarMeshComponent->ClearMeshSectionsFrom(1);

	if (UMaterialInterface* Material = OverrideMaterial.LoadSynchronous())
	{
		PillarMeshComponent->SetMaterialIfChanged(0, Material);
	}
}

void AEHB_Pillar::ApplyPillarMeshToComponent(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs,
	const TArray<int32>& TriangleMaterialIndices,
	const TArray<TSoftObjectPtr<UMaterialInterface>>& SourceMaterials,
	bool bUseOverrideMaterialForAllSections)
{
	if (!PillarMeshComponent)
	{
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, Vertices.Num());
	TArray<FProcMeshTangent> Tangents;

	PillarMeshComponent->Modify();
	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(PillarMeshComponent);

	UMaterialInterface* OverrideLoadedMaterial = bUseOverrideMaterialForAllSections
		? OverrideMaterial.LoadSynchronous()
		: nullptr;
	if (TriangleMaterialIndices.IsEmpty())
	{
		PillarMeshComponent->CreateMeshSection_LinearColor(
			0,
			Vertices,
			Triangles,
			Normals,
			UVs,
			VertexColors,
			Tangents,
			true);
		PillarMeshComponent->SetMeshSectionName(0, FName(TEXT("PillarSurface")));
		PillarMeshComponent->ClearMeshSectionsFrom(1);

		if (OverrideLoadedMaterial)
		{
			PillarMeshComponent->SetMaterialIfChanged(0, OverrideLoadedMaterial);
		}
		return;
	}

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

		PillarMeshComponent->CreateMeshSection_LinearColor(
			SectionIndex,
			Vertices,
			*SectionTriangles,
			Normals,
			UVs,
			VertexColors,
			Tangents,
			true);
		PillarMeshComponent->SetMeshSectionName(
			SectionIndex,
			FName(*FString::Printf(TEXT("PillarMaterial_%d"), MaterialIndex)));

		UMaterialInterface* SectionMaterial = OverrideLoadedMaterial;
		if (!SectionMaterial && SourceMaterials.IsValidIndex(MaterialIndex))
		{
			SectionMaterial = SourceMaterials[MaterialIndex].LoadSynchronous();
		}

		if (SectionMaterial)
		{
			PillarMeshComponent->SetMaterialIfChanged(SectionIndex, SectionMaterial);
		}
	}

	PillarMeshComponent->ClearMeshSectionsFrom(SortedMaterialIndices.Num());
}

void AEHB_Pillar::GetPolygonPillarMeshData(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs) const
{
	OutVertices = PolygonPillarVertices;
	OutTriangles = PolygonPillarTriangles;
	OutNormals = PolygonPillarNormals;
	OutUVs = PolygonPillarUVs;
}

void AEHB_Pillar::GetPillarFootprintLocalPoints(TArray<FVector>& OutLocalPoints) const
{
	OutLocalPoints = PolygonPillarFootprint;
	if (OutLocalPoints.Num() >= 3)
	{
		return;
	}

	const float HalfWidth = FMath::Max(1.0f, Width) * 0.5f;
	const float HalfDepth = FMath::Max(1.0f, Depth) * 0.5f;
	OutLocalPoints = {
		FVector(-HalfWidth, -HalfDepth, 0.0f),
		FVector(HalfWidth, -HalfDepth, 0.0f),
		FVector(HalfWidth, HalfDepth, 0.0f),
		FVector(-HalfWidth, HalfDepth, 0.0f)
	};
}

bool AEHB_Pillar::BuildStructuralContactMesh(FEHBWallJunctionMesh& Out) const
{
 Out={};
 if(ShapeType!=EEHBPillarShapeType::Polygon||!FMath::IsFinite(Height)||!FMath::IsFinite(Width)||!FMath::IsFinite(Depth))return false;
 TArray<FVector> Footprint;
 if(OwningBuilding&&ElementGuid.IsValid())OwningBuilding->GetClosedLoopsByPillarGuid(ElementGuid);
 return BuildPolygonPillarFootprint(Footprint)&&FEHBWallJunctionMeshBuilder::BuildPrism(Footprint,FMath::Max(1.0f,Height),Out);
}
