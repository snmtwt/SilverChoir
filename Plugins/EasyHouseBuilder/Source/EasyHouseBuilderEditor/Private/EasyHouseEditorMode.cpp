// Copyright Epic Games, Inc. All Rights Reserved.

#include "EasyHouseEditorMode.h"
#include "EHBRoomFinishMove.h"
#include "Core/EHBWallNodeRooms.h"
#include "EHBWallCreationCommand.h"
#include "Core/EHBWallPathPlanning.h"
#include "Tests/EHBNodeMoveTestHooks.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "EHBRailingCreationSnap.h"
#include "EHBRailingFeedback.h"
#include "EHBDragAngleSnap.h"
#include "EHBWallRailingJunction.h"

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHBHipRoof.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Stair.h"
#include "Actors/EHB_Wall.h"
#include "CanvasItem.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Canvas.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "Engine/World.h"
#include "Geometry/EHBRoomFillSolver.h"
#include "Geometry/EHBSurfaceQueryLibrary.h"
#include "HitProxies.h"
#include "Input/Events.h"
#include "LevelEditorViewport.h"
#include "MeshDescription.h"
#include "EasyHouseEditorModeToolkit.h"
#include "SceneManagement.h"
#include "Materials/MaterialRenderProxy.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "StaticMeshAttributes.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "Toolkits/ToolkitManager.h"
#include "UnrealWidget.h"
#include "Widgets/SViewport.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FEasyHouseEditorMode"

struct HEHBFloorSlabHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHB_FloorSlab> FloorSlab;
	EEHBFloorSlabEditHandleKind HandleKind = EEHBFloorSlabEditHandleKind::None;
	int32 LoopIndex = INDEX_NONE;
	int32 FirstIndex = INDEX_NONE;
	int32 SecondIndex = INDEX_NONE;

	HEHBFloorSlabHandleProxy(AEHB_FloorSlab* InFloorSlab, EEHBFloorSlabEditHandleKind InHandleKind, int32 InFirstIndex, int32 InSecondIndex = INDEX_NONE, int32 InLoopIndex = INDEX_NONE)
		: HHitProxy(HPP_Foreground)
		, FloorSlab(InFloorSlab)
		, HandleKind(InHandleKind)
		, LoopIndex(InLoopIndex)
		, FirstIndex(InFirstIndex)
		, SecondIndex(InSecondIndex)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBFloorSlabHandleProxy, HHitProxy);

struct HEHBRoofHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHBGableRoof> Roof;
	EEHBRoofEditHandleKind HandleKind = EEHBRoofEditHandleKind::None;
	int32 FirstIndex = INDEX_NONE;
	int32 SecondIndex = INDEX_NONE;

	HEHBRoofHandleProxy(AEHBGableRoof* InRoof, EEHBRoofEditHandleKind InHandleKind, int32 InFirstIndex, int32 InSecondIndex = INDEX_NONE)
		: HHitProxy(HPP_Foreground)
		, Roof(InRoof)
		, HandleKind(InHandleKind)
		, FirstIndex(InFirstIndex)
		, SecondIndex(InSecondIndex)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBRoofHandleProxy, HHitProxy);

struct HEHBWallCurveControlProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHB_Wall> Wall;

	explicit HEHBWallCurveControlProxy(AEHB_Wall* InWall)
		: HHitProxy(HPP_Foreground)
		, Wall(InWall)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBWallCurveControlProxy, HHitProxy);

struct HEHBRailingEndpointHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHB_Railing> Railing;
	EEHBRailingEndpointHandleKind HandleKind = EEHBRailingEndpointHandleKind::None;

	HEHBRailingEndpointHandleProxy(AEHB_Railing* InRailing, EEHBRailingEndpointHandleKind InHandleKind)
		: HHitProxy(HPP_Foreground)
		, Railing(InRailing)
		, HandleKind(InHandleKind)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBRailingEndpointHandleProxy, HHitProxy);

struct HEHBStairBottomControlProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHB_Stair> Stair;

	explicit HEHBStairBottomControlProxy(AEHB_Stair* InStair)
		: HHitProxy(HPP_Foreground)
		, Stair(InStair)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBStairBottomControlProxy, HHitProxy);

struct HEHBStairIntermediateControlProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	TWeakObjectPtr<AEHB_Stair> Stair;
	int32 ControlIndex = INDEX_NONE;

	HEHBStairIntermediateControlProxy(AEHB_Stair* InStair, int32 InControlIndex)
		: HHitProxy(HPP_Foreground)
		, Stair(InStair)
		, ControlIndex(InControlIndex)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBStairIntermediateControlProxy, HHitProxy);

struct HEHBFloorSlabToolbarProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	int32 CommandIndex = INDEX_NONE;

	explicit HEHBFloorSlabToolbarProxy(int32 InCommandIndex)
		: HHitProxy(HPP_UI)
		, CommandIndex(InCommandIndex)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBFloorSlabToolbarProxy, HHitProxy);

struct HEHBFloorToolbarProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	int32 CommandIndex = INDEX_NONE;

	explicit HEHBFloorToolbarProxy(int32 InCommandIndex)
		: HHitProxy(HPP_UI)
		, CommandIndex(InCommandIndex)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBFloorToolbarProxy, HHitProxy);

struct HEHBPillarConnectionToolbarProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	HEHBPillarConnectionToolbarProxy()
		: HHitProxy(HPP_UI)
	{
	}
};

IMPLEMENT_HIT_PROXY(HEHBPillarConnectionToolbarProxy, HHitProxy);

struct HEHBWallNodeProxy : public HHitProxy
{
 DECLARE_HIT_PROXY();
 TWeakObjectPtr<AEHBBuildingActorBase> Building;FGuid NodeGuid,PhysicalGuid;int32 Revision=0;bool bRemove=false;
 HEHBWallNodeProxy(AEHBBuildingActorBase* B,FGuid Id,bool Remove=false):HHitProxy(Remove?HPP_UI:HPP_Foreground),Building(B),NodeGuid(Id),bRemove(Remove)
 {PhysicalGuid=B->FindPhysicalPillarForNode(Id);if(const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;}))Revision=N->GeometryRevision;}
};
IMPLEMENT_HIT_PROXY(HEHBWallNodeProxy,HHitProxy);

namespace
{
	void DrawBuildingHudTile(FCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	void DrawBuildingHudButton(
		FCanvas* Canvas,
		HHitProxy* HitProxy,
		const FVector2D& Position,
		const FVector2D& Size,
		const FText& Label,
		bool bHovered)
	{
		if (!Canvas)
		{
			return;
		}

		DrawBuildingHudTile(Canvas, Position + FVector2D(1.0f, 2.0f), Size, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));

		Canvas->SetHitProxy(HitProxy);
		const FLinearColor FillColor = bHovered
			? FLinearColor(0.02f, 0.43f, 0.95f, 0.98f)
			: FLinearColor(0.02f, 0.30f, 0.74f, 0.96f);
		const FLinearColor TopLineColor = bHovered
			? FLinearColor(0.42f, 0.74f, 1.0f, 0.95f)
			: FLinearColor(0.22f, 0.55f, 0.95f, 0.88f);
		const FLinearColor BottomLineColor = FLinearColor(0.0f, 0.10f, 0.28f, 0.78f);
		const FLinearColor SideLineColor = FLinearColor(0.12f, 0.42f, 0.90f, 0.70f);

		DrawBuildingHudTile(Canvas, Position, Size, FillColor);
		DrawBuildingHudTile(Canvas, Position, FVector2D(Size.X, 1.0f), TopLineColor);
		DrawBuildingHudTile(Canvas, Position + FVector2D(0.0f, Size.Y - 1.0f), FVector2D(Size.X, 1.0f), BottomLineColor);
		DrawBuildingHudTile(Canvas, Position, FVector2D(1.0f, Size.Y), SideLineColor);
		DrawBuildingHudTile(Canvas, Position + FVector2D(Size.X - 1.0f, 0.0f), FVector2D(1.0f, Size.Y), SideLineColor);

		FCanvasTextItem TextItem(
			Position + FVector2D(Size.X * 0.5f, Size.Y * 0.5f - 1.0f),
			Label,
			GEngine ? GEngine->GetSmallFont() : nullptr,
			FLinearColor::White);
		TextItem.bCentreX = true;
		TextItem.bCentreY = true;
		TextItem.EnableShadow(FLinearColor(0.0f, 0.02f, 0.08f, 0.85f));
		Canvas->DrawItem(TextItem);
		Canvas->SetHitProxy(nullptr);
	}

	void DrawBuildingHudPanel(FCanvas* Canvas, const FVector2D& Position, const FVector2D& Size)
	{
		if (!Canvas)
		{
			return;
		}

		DrawBuildingHudTile(Canvas, Position + FVector2D(1.0f, 2.0f), Size, FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
		DrawBuildingHudTile(Canvas, Position, Size, FLinearColor(0.025f, 0.03f, 0.04f, 0.72f));
		DrawBuildingHudTile(Canvas, Position, FVector2D(Size.X, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 0.16f));
		DrawBuildingHudTile(Canvas, Position + FVector2D(0.0f, Size.Y - 1.0f), FVector2D(Size.X, 1.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	}

	void DrawBuildingHudLabel(FCanvas* Canvas, const FVector2D& Position, const FString& Label, const FLinearColor& AccentColor)
	{
		if (!Canvas || Label.IsEmpty())
		{
			return;
		}

		const FVector2D Size(
			FMath::Clamp(static_cast<float>(Label.Len()) * 7.0f + 18.0f, 96.0f, 260.0f),
			24.0f);
		DrawBuildingHudTile(Canvas, Position + FVector2D(1.0f, 2.0f), Size, FLinearColor(0.0f, 0.0f, 0.0f, 0.32f));
		DrawBuildingHudTile(Canvas, Position, Size, FLinearColor(0.025f, 0.03f, 0.035f, 0.82f));
		DrawBuildingHudTile(Canvas, Position, FVector2D(3.0f, Size.Y), AccentColor);
		DrawBuildingHudTile(Canvas, Position, FVector2D(Size.X, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 0.14f));

		FCanvasTextItem TextItem(
			Position + FVector2D(10.0f, Size.Y * 0.5f - 1.0f),
			FText::FromString(Label),
			GEngine ? GEngine->GetSmallFont() : nullptr,
			FLinearColor::White);
		TextItem.bCentreY = true;
		TextItem.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		Canvas->DrawItem(TextItem);
	}

	float NormalizeMeasurementAngle(float AngleDegrees)
	{
		float Result = FMath::Fmod(AngleDegrees, 360.0f);
		if (Result < 0.0f)
		{
			Result += 360.0f;
		}
		return Result;
	}

	FVector RoundBuildingLocalCoordinates(const FVector& LocalLocation)
	{
		return FVector(
			FMath::RoundToDouble(LocalLocation.X),
			FMath::RoundToDouble(LocalLocation.Y),
			FMath::RoundToDouble(LocalLocation.Z));
	}

	FVector RoundBuildingLocalXYCoordinates(const FVector& LocalLocation)
	{
		return FVector(
			FMath::RoundToDouble(LocalLocation.X),
			FMath::RoundToDouble(LocalLocation.Y),
			LocalLocation.Z);
	}

	FVector SnapWorldLocationToIntegerBuildingCoordinates(
		const AEHBBuildingActorBase* Building,
		const FVector& WorldLocation)
	{
		if (!Building)
		{
			return FVector(
				FMath::RoundToDouble(WorldLocation.X),
				FMath::RoundToDouble(WorldLocation.Y),
				FMath::RoundToDouble(WorldLocation.Z));
		}

		const FTransform BuildingTransform = Building->GetActorTransform();
		return BuildingTransform.TransformPosition(
			RoundBuildingLocalCoordinates(BuildingTransform.InverseTransformPosition(WorldLocation)));
	}

	FVector SnapWorldLocationToIntegerBuildingXYCoordinates(
		const AEHBBuildingActorBase* Building,
		const FVector& WorldLocation)
	{
		if (!Building)
		{
			return FVector(
				FMath::RoundToDouble(WorldLocation.X),
				FMath::RoundToDouble(WorldLocation.Y),
				WorldLocation.Z);
		}

		const FTransform BuildingTransform = Building->GetActorTransform();
		return BuildingTransform.TransformPosition(
			RoundBuildingLocalXYCoordinates(BuildingTransform.InverseTransformPosition(WorldLocation)));
	}

	bool SnapWallEndpointPerpendicularToTargetWall(
		const AEHBBuildingActorBase* Building,
		const AEHB_Wall* TargetWall,
		const FVector& StartWorldLocation,
		float MinWallEndpointDistance,
		float& OutWallDistance,
		FVector& OutWorldLocation)
	{
		constexpr double PerpendicularSnapAngleToleranceDegrees = 3.0;

		if (!Building || !TargetWall || TargetWall->OwningBuilding != Building)
		{
			return false;
		}

		const FVector TargetWallDelta = TargetWall->LocalEnd - TargetWall->LocalStart;
		const float TargetWallLength = TargetWallDelta.Size2D();
		const FVector TargetWallDirection = TargetWallDelta.GetSafeNormal2D();
		if (TargetWallLength <= UE_SMALL_NUMBER || TargetWallDirection.IsNearlyZero())
		{
			return false;
		}

		const FVector LocalStart = Building->GetActorTransform().InverseTransformPosition(StartWorldLocation);
		const float ProjectedDistance = FVector::DotProduct(LocalStart - TargetWall->LocalStart, TargetWallDirection);
		const float SafeEndpointDistance = FMath::Max(0.0f, MinWallEndpointDistance);
		if (ProjectedDistance <= SafeEndpointDistance
			|| ProjectedDistance >= TargetWallLength - SafeEndpointDistance)
		{
			return false;
		}

		const FVector ProjectedWorldLocation =
			TargetWall->GetWorldLocationOnCenterAxisAtDistance(ProjectedDistance, 0.0f);
		const FVector NewWallDirection = (ProjectedWorldLocation - StartWorldLocation).GetSafeNormal2D();
		if (NewWallDirection.IsNearlyZero())
		{
			return false;
		}

		const double PerpendicularDeviationDegrees = FMath::RadiansToDegrees(
			FMath::Asin(FMath::Clamp(
				FMath::Abs(FVector::DotProduct(NewWallDirection, TargetWallDirection)),
				0.0f,
				1.0f)));
		if (PerpendicularDeviationDegrees > PerpendicularSnapAngleToleranceDegrees)
		{
			return false;
		}

		OutWallDistance = ProjectedDistance;
		OutWorldLocation = ProjectedWorldLocation;
		return true;
	}

	void SnapPillarToIntegerBuildingCoordinates(AEHB_Pillar* Pillar, bool bFinished)
	{
		if (!Pillar)
		{
			return;
		}

		FTransform LocalTransform = Pillar->GetElementLocalTransform();
		const FVector SnappedLocation = RoundBuildingLocalCoordinates(LocalTransform.GetLocation());
		if (LocalTransform.GetLocation().Equals(SnappedLocation))
		{
			return;
		}

		Pillar->Modify();
		LocalTransform.SetLocation(SnappedLocation);
		Pillar->SetActorRelativeTransform(LocalTransform);
		Pillar->NotifyElementGeometryChanged(bFinished);
		if (bFinished)
		{
			Pillar->MarkPackageDirty();
		}
	}

	void DrawBuildingHudWorldLabel(
		FViewport* Viewport,
		const FSceneView* View,
		FCanvas* Canvas,
		const FVector& WorldLocation,
		const FString& Label,
		const FLinearColor& AccentColor,
		const FVector2D& PixelOffset = FVector2D(10.0f, -12.0f))
	{
		if (!Viewport || !View || !Canvas || Label.IsEmpty())
		{
			return;
		}

		FVector2D PixelLocation = FVector2D::ZeroVector;
		if (!View->WorldToPixel(WorldLocation, PixelLocation))
		{
			return;
		}

		const FIntPoint ViewportSize = Viewport->GetSizeXY();
		if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
		{
			return;
		}

		PixelLocation += PixelOffset;
		PixelLocation.X = FMath::Clamp(PixelLocation.X, 6.0f, FMath::Max(6.0f, static_cast<float>(ViewportSize.X) - 170.0f));
		PixelLocation.Y = FMath::Clamp(PixelLocation.Y, 6.0f, FMath::Max(6.0f, static_cast<float>(ViewportSize.Y) - 32.0f));

		DrawBuildingHudLabel(Canvas, PixelLocation, Label, AccentColor);
	}

	constexpr int32 EHBRoofEdgeHandleCount = 4;
	constexpr float EHBRoofBottomHandleDrop = 8.0f;
	constexpr float EHBRoofHeightHandleLift = 38.0f;
	constexpr float EHBRoofMinEditableBodySize = 1.0f;

	bool IsValidRoofEdgeHandleIndex(int32 EdgeIndex)
	{
		return EdgeIndex >= 0 && EdgeIndex < EHBRoofEdgeHandleCount;
	}

	FVector GetRoofEdgeHandleLocalAxis(int32 EdgeIndex)
	{
		switch (EdgeIndex)
		{
		case 0:
			return FVector(0.0f, -1.0f, 0.0f);
		case 1:
			return FVector(1.0f, 0.0f, 0.0f);
		case 2:
			return FVector(0.0f, 1.0f, 0.0f);
		case 3:
			return FVector(-1.0f, 0.0f, 0.0f);
		default:
			return FVector::ZeroVector;
		}
	}

	bool GetRoofProjectionHandleData(
		const AEHBGableRoof* Roof,
		FEHBRoofProjectionBounds& OutBounds,
		float& OutHandleZ)
	{
		if (!Roof || !Roof->GetRoofProjectionBounds(OutBounds) || !OutBounds.bIsValid)
		{
			return false;
		}

		OutHandleZ = -FMath::Max(0.1f, Roof->Thickness) - EHBRoofBottomHandleDrop;
		return true;
	}

	float GetRoofGableCrossSpan(const AEHBGableRoof& Roof)
	{
		return FMath::Max(Roof.Width, EHBRoofMinEditableBodySize) + FMath::Max(0.0f, Roof.EaveOffset) * 2.0f;
	}

	float GetRoofGableRidgeCross(const AEHBGableRoof& Roof)
	{
		const float CrossSpan = GetRoofGableCrossSpan(Roof);
		const float CrossMin = -CrossSpan * 0.5f;
		const float CrossMax = CrossSpan * 0.5f;
		return FMath::Clamp(
			CrossSpan * Roof.RidgeOffsetRatio,
			CrossMin + 1.0f,
			CrossMax - 1.0f);
	}

	float GetRoofGableRidgeRun(const AEHBGableRoof& Roof)
	{
		const float CrossSpan = GetRoofGableCrossSpan(Roof);
		const float CrossMin = -CrossSpan * 0.5f;
		const float CrossMax = CrossSpan * 0.5f;
		const float RidgeCross = GetRoofGableRidgeCross(Roof);
		return FMath::Max(FMath::Min(RidgeCross - CrossMin, CrossMax - RidgeCross), 1.0f);
	}

	float GetRoofGableRidgeHeight(const AEHBGableRoof& Roof)
	{
		const float SafePitchDegrees = FMath::Clamp(Roof.PitchDegrees, 1.0f, 89.0f);
		return GetRoofGableRidgeRun(Roof) * FMath::Tan(FMath::DegreesToRadians(SafePitchDegrees));
	}

	FVector GetRoofHeightHandleLocalLocation(const AEHBGableRoof& Roof)
	{
		const float RidgeCross = GetRoofGableRidgeCross(Roof);
		const float RidgeHeight = GetRoofGableRidgeHeight(Roof);
		return Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FVector(0.0f, RidgeCross, RidgeHeight + EHBRoofHeightHandleLift)
			: FVector(RidgeCross, 0.0f, RidgeHeight + EHBRoofHeightHandleLift);
	}

	void GetRoofProjectionHandleCorners(
		const FEHBRoofProjectionBounds& Bounds,
		float HandleZ,
		FVector OutCorners[EHBRoofEdgeHandleCount])
	{
		OutCorners[0] = FVector(Bounds.Min.X, Bounds.Min.Y, HandleZ);
		OutCorners[1] = FVector(Bounds.Max.X, Bounds.Min.Y, HandleZ);
		OutCorners[2] = FVector(Bounds.Max.X, Bounds.Max.Y, HandleZ);
		OutCorners[3] = FVector(Bounds.Min.X, Bounds.Max.Y, HandleZ);
	}

	bool GetRoofEdgeHandleLocalLocation(const AEHBGableRoof* Roof, int32 EdgeIndex, FVector& OutLocalLocation)
	{
		FEHBRoofProjectionBounds Bounds;
		float HandleZ = 0.0f;
		if (!IsValidRoofEdgeHandleIndex(EdgeIndex) || !GetRoofProjectionHandleData(Roof, Bounds, HandleZ))
		{
			return false;
		}

		FVector Corners[EHBRoofEdgeHandleCount];
		GetRoofProjectionHandleCorners(Bounds, HandleZ, Corners);
		OutLocalLocation = FMath::Lerp(Corners[EdgeIndex], Corners[(EdgeIndex + 1) % EHBRoofEdgeHandleCount], 0.5f);
		return true;
	}

	bool ApplyRoofHeightHandleWorldDelta(AEHBGableRoof& Roof, const FVector& WorldDelta)
	{
		if (WorldDelta.IsNearlyZero())
		{
			return false;
		}

		const FTransform RoofTransform = Roof.GetActorTransform();
		const FVector LocalDelta = RoofTransform.InverseTransformVectorNoScale(WorldDelta);
		const float RequestedHeightDelta = LocalDelta.Z;
		if (FMath::IsNearlyZero(RequestedHeightDelta, 0.01f))
		{
			return false;
		}

		const float RidgeRun = GetRoofGableRidgeRun(Roof);
		const float CurrentHeight = GetRoofGableRidgeHeight(Roof);
		const float NewHeight = FMath::Max(1.0f, CurrentHeight + RequestedHeightDelta);
		const float NewPitchDegrees = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(NewHeight / RidgeRun)), 1.0f, 89.0f);
		if (FMath::IsNearlyEqual(Roof.PitchDegrees, NewPitchDegrees, 0.01f))
		{
			return false;
		}

		Roof.PitchDegrees = NewPitchDegrees;
		Roof.RebuildRoofMesh();
		Roof.NotifyElementGeometryChanged(false);
		Roof.MarkPackageDirty();
		return true;
	}

	bool DoesRoofLocalAxisEditLength(const AEHBGableRoof& Roof, const FVector& LocalAxis)
	{
		const bool bLocalX = FMath::Abs(LocalAxis.X) >= FMath::Abs(LocalAxis.Y);
		return Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX ? bLocalX : !bLocalX;
	}

	bool GetHalfHipVisibleCrossRange(
		const AEHBHipRoof& Roof,
		float FullProjectionSpan,
		float& OutCrossMin,
		float& OutCrossMax)
	{
		if (!Roof.bHalfHipRoof)
		{
			return false;
		}

		const float SafeFullSpan = FMath::Max(FullProjectionSpan, EHBRoofMinEditableBodySize);
		const float CrossMin = -SafeFullSpan * 0.5f;
		const float CrossMax = SafeFullSpan * 0.5f;
		const float RidgeCross = FMath::Clamp(
			SafeFullSpan * Roof.RidgeOffsetRatio,
			CrossMin + 1.0f,
			CrossMax - 1.0f);

		if (Roof.HalfHipKeepSide == EEHBHalfHipRoofKeepSide::PositiveAlongAxis)
		{
			OutCrossMin = RidgeCross;
			OutCrossMax = CrossMax;
		}
		else
		{
			OutCrossMin = CrossMin;
			OutCrossMax = RidgeCross;
		}

		return OutCrossMax - OutCrossMin > UE_KINDA_SMALL_NUMBER;
	}

	bool ApplyHalfHipRoofCrossEdgeHandleWorldDelta(
		AEHBHipRoof& Roof,
		const FEHBRoofProjectionBounds& Bounds,
		const FVector& LocalAxis,
		float RequestedDelta)
	{
		const FVector CrossAxis = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? FVector(0.0f, 1.0f, 0.0f)
			: FVector(1.0f, 0.0f, 0.0f);
		const float AxisSign = FVector::DotProduct(LocalAxis, CrossAxis);
		if (FMath::Abs(AxisSign) < 0.5f)
		{
			return false;
		}

		const float CurrentCrossMin = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? Bounds.Min.Y
			: Bounds.Min.X;
		const float CurrentCrossMax = Roof.AxisMode == EEHBRoofAxisMode::RidgeAlongX
			? Bounds.Max.Y
			: Bounds.Max.X;
		const float CurrentVisibleSize = FMath::Max(0.0f, CurrentCrossMax - CurrentCrossMin);
		if (CurrentVisibleSize <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float SafeEaveOffset = FMath::Max(0.0f, Roof.EaveOffset);
		const float CurrentFullProjectionSpan =
			FMath::Max(Roof.Width, EHBRoofMinEditableBodySize) + SafeEaveOffset * 2.0f;
		float ParamCrossMin = 0.0f;
		float ParamCrossMax = 0.0f;
		if (!GetHalfHipVisibleCrossRange(Roof, CurrentFullProjectionSpan, ParamCrossMin, ParamCrossMax))
		{
			return false;
		}

		const float ParamVisibleSize = FMath::Max(UE_KINDA_SMALL_NUMBER, ParamCrossMax - ParamCrossMin);
		const float VisibleToFullScale = ParamVisibleSize / CurrentFullProjectionSpan;
		if (VisibleToFullScale <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float MinFullProjectionSpan = EHBRoofMinEditableBodySize + SafeEaveOffset * 2.0f;
		const float MinVisibleSize = FMath::Max(EHBRoofMinEditableBodySize, MinFullProjectionSpan * VisibleToFullScale);
		const float NewVisibleSize = FMath::Max(MinVisibleSize, CurrentVisibleSize + RequestedDelta);
		const float AppliedVisibleDelta = NewVisibleSize - CurrentVisibleSize;
		if (FMath::IsNearlyZero(AppliedVisibleDelta, 0.01f))
		{
			return false;
		}

		const float NewFullProjectionSpan = FMath::Max(MinFullProjectionSpan, NewVisibleSize / VisibleToFullScale);
		float NewCrossMin = 0.0f;
		float NewCrossMax = 0.0f;
		if (!GetHalfHipVisibleCrossRange(Roof, NewFullProjectionSpan, NewCrossMin, NewCrossMax))
		{
			return false;
		}

		const float OppositeOldCross = AxisSign > 0.0f ? CurrentCrossMin : CurrentCrossMax;
		const float OppositeNewCross = AxisSign > 0.0f ? NewCrossMin : NewCrossMax;
		const float LocalCenterOffsetAlongCross = OppositeOldCross - OppositeNewCross;

		Roof.Width = FMath::Max(EHBRoofMinEditableBodySize, NewFullProjectionSpan - SafeEaveOffset * 2.0f);

		const FTransform RoofTransform = Roof.GetActorTransform();
		const FVector WorldCenterOffset =
			RoofTransform.TransformVectorNoScale(CrossAxis * LocalCenterOffsetAlongCross);
		Roof.SetActorLocation(Roof.GetActorLocation() + WorldCenterOffset, false, nullptr, ETeleportType::TeleportPhysics);
		Roof.RebuildRoofMesh();
		Roof.NotifyElementGeometryChanged(false);
		Roof.MarkPackageDirty();
		return true;
	}

	bool ApplyRoofEdgeHandleWorldDelta(AEHBGableRoof& Roof, int32 EdgeIndex, const FVector& WorldDelta)
	{
		if (!IsValidRoofEdgeHandleIndex(EdgeIndex) || WorldDelta.IsNearlyZero())
		{
			return false;
		}

		FEHBRoofProjectionBounds Bounds;
		float HandleZ = 0.0f;
		if (!GetRoofProjectionHandleData(&Roof, Bounds, HandleZ))
		{
			return false;
		}

		const FVector LocalAxis = GetRoofEdgeHandleLocalAxis(EdgeIndex);
		if (LocalAxis.IsNearlyZero())
		{
			return false;
		}

		const FTransform RoofTransform = Roof.GetActorTransform();
		FVector LocalDelta = RoofTransform.InverseTransformVectorNoScale(FVector(WorldDelta.X, WorldDelta.Y, 0.0f));
		LocalDelta.Z = 0.0f;

		const float RequestedDelta = FVector::DotProduct(LocalDelta, LocalAxis);
		if (FMath::IsNearlyZero(RequestedDelta, 0.01f))
		{
			return false;
		}

		const bool bEditLength = DoesRoofLocalAxisEditLength(Roof, LocalAxis);
		if (!bEditLength)
		{
			if (AEHBHipRoof* HipRoof = Cast<AEHBHipRoof>(&Roof); HipRoof && HipRoof->bHalfHipRoof)
			{
				return ApplyHalfHipRoofCrossEdgeHandleWorldDelta(*HipRoof, Bounds, LocalAxis, RequestedDelta);
			}
		}

		const bool bLocalX = FMath::Abs(LocalAxis.X) >= FMath::Abs(LocalAxis.Y);
		const float CurrentProjectionSize = bLocalX
			? FMath::Max(0.0f, Bounds.Max.X - Bounds.Min.X)
			: FMath::Max(0.0f, Bounds.Max.Y - Bounds.Min.Y);
		const float SafeEaveOffset = FMath::Max(0.0f, Roof.EaveOffset);
		const float MinProjectionSize = EHBRoofMinEditableBodySize + SafeEaveOffset * 2.0f;
		const float NewProjectionSize = FMath::Max(MinProjectionSize, CurrentProjectionSize + RequestedDelta);
		const float AppliedDelta = NewProjectionSize - CurrentProjectionSize;
		if (FMath::IsNearlyZero(AppliedDelta, 0.01f))
		{
			return false;
		}

		const float NewBodySize = FMath::Max(EHBRoofMinEditableBodySize, NewProjectionSize - SafeEaveOffset * 2.0f);
		if (bEditLength)
		{
			Roof.Length = NewBodySize;
		}
		else
		{
			Roof.Width = NewBodySize;
		}

		const FVector WorldCenterOffset = RoofTransform.TransformVectorNoScale(LocalAxis * (AppliedDelta * 0.5f));
		Roof.SetActorLocation(Roof.GetActorLocation() + WorldCenterOffset, false, nullptr, ETeleportType::TeleportPhysics);
		Roof.RebuildRoofMesh();
		Roof.NotifyElementGeometryChanged(false);
		Roof.MarkPackageDirty();
		return true;
	}

	void LogSampleValidationResult(const TCHAR* Context, const FEHBMeshSampleValidationResult& Result)
	{
		if (Result.Issues.IsEmpty())
		{
			return;
		}

		if (Result.HasErrors())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("EHB sample validation [%s]: %s EstimatedCost=%d"),
				Context,
				*Result.MakeSummaryText().ToString(),
				Result.EstimatedApplyCost);
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("EHB sample validation [%s]: %s EstimatedCost=%d"),
				Context,
				*Result.MakeSummaryText().ToString(),
				Result.EstimatedApplyCost);
		}

		for (const FEHBMeshSampleValidationIssue& Issue : Result.Issues)
		{
			const TCHAR* SeverityText = TEXT("Info");
			if (Issue.Severity == EEHBMeshSampleValidationSeverity::Warning)
			{
				SeverityText = TEXT("Warning");
			}
			else if (Issue.Severity == EEHBMeshSampleValidationSeverity::Error)
			{
				SeverityText = TEXT("Error");
			}

			UE_LOG(LogTemp, Display, TEXT("  - %s: %s"), SeverityText, *Issue.Message.ToString());
		}
	}

	bool GetWallSegmentBetweenPillarSides(const FVector& StartCenter, const FVector& EndCenter, float PillarSize, FVector& OutWallStart, FVector& OutWallEnd)
	{
		const FVector Direction = (EndCenter - StartCenter).GetSafeNormal2D();
		if (Direction.IsNearlyZero())
		{
			return false;
		}

		const float HalfPillarSize = FMath::Max(1.0f, PillarSize) * 0.5f;
		if (FVector::Dist2D(StartCenter, EndCenter) <= HalfPillarSize * 2.0f + 1.0f)
		{
			return false;
		}

		OutWallStart = StartCenter + Direction * HalfPillarSize;
		OutWallEnd = EndCenter - Direction * HalfPillarSize;
		return true;
	}

	bool IntersectRayWithWallCenterPlane(
		const AEHB_Wall* Wall,
		const FVector& RayStart,
		const FVector& RayDirection,
		FVector& OutWorldIntersection)
	{
		if (!Wall)
		{
			return false;
		}

		const FTransform WallTransform = Wall->GetActorTransform();
		const FVector LocalRayStart = WallTransform.InverseTransformPosition(RayStart);
		const FVector LocalRayDirection =
			WallTransform.InverseTransformVectorNoScale(RayDirection).GetSafeNormal();
		if (FMath::Abs(LocalRayDirection.Y) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const double DistanceAlongRay = -LocalRayStart.Y / LocalRayDirection.Y;
		if (DistanceAlongRay < 0.0)
		{
			return false;
		}

		const FVector LocalIntersection =
			LocalRayStart + LocalRayDirection * DistanceAlongRay;
		const float HalfWallLength =
			FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd) * 0.5f;
		const float WallHeight = FMath::Max(1.0f, Wall->Height);
		if (LocalIntersection.X < -HalfWallLength
			|| LocalIntersection.X > HalfWallLength
			|| LocalIntersection.Z < 0.0f
			|| LocalIntersection.Z > WallHeight)
		{
			return false;
		}

		OutWorldIntersection = WallTransform.TransformPosition(LocalIntersection);
		return true;
	}

	struct FEHBFloorSlabSideSnapResult
	{
		FVector WorldTopEdgePoint = FVector::ZeroVector;
		FVector WorldTangent = FVector::ForwardVector;
		FVector WorldOutward = FVector::RightVector;
		TWeakObjectPtr<AActor> AnchorActor;
		TWeakObjectPtr<AEHB_Wall> AnchorWall;
		EEHBFloorSlabWallSide AnchorWallSide = EEHBFloorSlabWallSide::None;
		int32 AnchorFloorIndex = 0;
		bool bAnchoredOnWallOrPillar = false;
	};

	struct FEHBPillarCreationTopSnapResult
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 SourceFloorIndex = 0;
		int32 PillarFloorIndex = 1;
		TWeakObjectPtr<AActor> SourceActor;
	};

	enum class EEHBPillarBaseSnapSource : uint8
	{
		PillarTop,
		WallTop,
		FloorSlabCornerOrEdge
	};

	struct FEHBPillarBaseSnapCandidate
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 PillarFloorIndex = 1;
		float Distance = TNumericLimits<float>::Max();
		EEHBPillarBaseSnapSource Source = EEHBPillarBaseSnapSource::FloorSlabCornerOrEdge;
		bool bIsValid = false;
	};

	int32 GetPillarBaseSnapSourcePriority(EEHBPillarBaseSnapSource Source)
	{
		switch (Source)
		{
		case EEHBPillarBaseSnapSource::PillarTop:
			return 0;
		case EEHBPillarBaseSnapSource::WallTop:
			return 1;
		case EEHBPillarBaseSnapSource::FloorSlabCornerOrEdge:
		default:
			return 2;
		}
	}

	bool IsBetterPillarBaseSnapCandidate(
		const FEHBPillarBaseSnapCandidate& Candidate,
		const FEHBPillarBaseSnapCandidate& CurrentBest)
	{
		if (!Candidate.bIsValid)
		{
			return false;
		}

		if (!CurrentBest.bIsValid)
		{
			return true;
		}

		constexpr float DistanceTieTolerance = 1.0f;
		if (Candidate.Distance < CurrentBest.Distance - DistanceTieTolerance)
		{
			return true;
		}

		if (FMath::Abs(Candidate.Distance - CurrentBest.Distance) <= DistanceTieTolerance)
		{
			return GetPillarBaseSnapSourcePriority(Candidate.Source) < GetPillarBaseSnapSourcePriority(CurrentBest.Source);
		}

		return false;
	}

	template <typename TActorType>
	TActorType* ResolveActorFromHit(const FHitResult& HitResult)
	{
		FEHBSurfaceHit SurfaceHit;
		if (FEHBSurfaceQueryLibrary::ResolveHitSurface(HitResult, SurfaceHit))
		{
			if (TActorType* Actor = Cast<TActorType>(SurfaceHit.Element))
			{
				return Actor;
			}
		}

		if (TActorType* Actor = Cast<TActorType>(HitResult.GetActor()))
		{
			return Actor;
		}

		if (const UActorComponent* Component = HitResult.GetComponent())
		{
			return Cast<TActorType>(Component->GetOwner());
		}

		return nullptr;
	}

	bool IsLikelyVerticalSideHit(const FHitResult& HitResult)
	{
		const FVector HitNormal = HitResult.ImpactNormal.GetSafeNormal();
		return HitNormal.IsNearlyZero()
			|| FMath::Abs(FVector::DotProduct(HitNormal, FVector::UpVector)) < 0.65f;
	}

	bool IsLikelyTopSurfaceHit(const FHitResult& HitResult)
	{
		const FVector HitNormal = HitResult.ImpactNormal.GetSafeNormal();
		return !HitNormal.IsNearlyZero()
			&& FVector::DotProduct(HitNormal, FVector::UpVector) > 0.65f;
	}

	int32 ResolveNextFloorIndexFromElement(const AEHBElementActorBase* ElementActor)
	{
		if (!ElementActor || ElementActor->FloorRole == EEHBBuildingFloorElementRole::None)
		{
			return 1;
		}

		return FMath::Max(0, ElementActor->FloorIndex) + 1;
	}

	bool ResolvePillarCreationTopSnap(const FHitResult& HitResult, const AEHBBuildingActorBase* Building, FEHBPillarCreationTopSnapResult& OutSnapResult)
	{
		OutSnapResult = FEHBPillarCreationTopSnapResult();
		if (!Building || !HitResult.bBlockingHit || !IsLikelyTopSurfaceHit(HitResult))
		{
			return false;
		}

		if (AEHB_Pillar* Pillar = ResolveActorFromHit<AEHB_Pillar>(HitResult))
		{
			if (Pillar->OwningBuilding != Building || Pillar->IsActorBeingDestroyed())
			{
				return false;
			}

			OutSnapResult.WorldLocation = Pillar->GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, FMath::Max(1.0f, Pillar->Height)));
			OutSnapResult.SourceFloorIndex = FMath::Max(0, Pillar->FloorIndex);
			OutSnapResult.PillarFloorIndex = ResolveNextFloorIndexFromElement(Pillar);
			OutSnapResult.SourceActor = Pillar;
			return true;
		}

		if (AEHB_Wall* Wall = ResolveActorFromHit<AEHB_Wall>(HitResult))
		{
			if (Wall->OwningBuilding != Building || Wall->IsActorBeingDestroyed())
			{
				return false;
			}

			const float DistanceFromStart = Wall->CalculateDistanceFromStartForWorldLocation(HitResult.ImpactPoint);
			OutSnapResult.WorldLocation = Wall->GetWorldLocationOnCenterAxisAtDistance(DistanceFromStart, Wall->Height);
			OutSnapResult.SourceFloorIndex = FMath::Max(0, Wall->FloorIndex);
			OutSnapResult.PillarFloorIndex = ResolveNextFloorIndexFromElement(Wall);
			OutSnapResult.SourceActor = Wall;
			return true;
		}

		if (AEHB_FloorSlab* FloorSlab = ResolveActorFromHit<AEHB_FloorSlab>(HitResult))
		{
			if (FloorSlab->OwningBuilding != Building || FloorSlab->IsActorBeingDestroyed())
			{
				return false;
			}

			const FTransform SlabTransform = FloorSlab->GetActorTransform();
			const FVector LocalHitPoint = SlabTransform.InverseTransformPosition(HitResult.ImpactPoint);
			OutSnapResult.WorldLocation = SlabTransform.TransformPosition(FVector(LocalHitPoint.X, LocalHitPoint.Y, FloorSlab->GetTopZ()));
			OutSnapResult.SourceFloorIndex = FMath::Max(0, FloorSlab->FloorIndex);
			OutSnapResult.PillarFloorIndex = ResolveNextFloorIndexFromElement(FloorSlab);
			OutSnapResult.SourceActor = FloorSlab;
			return true;
		}

		return false;
	}

	float CalculateSignedAreaXY(const TArray<FVector>& Points)
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

	float CrossForPillarCornerSnapXY(const FVector& A, const FVector& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool SolvePillarCornerSnapOffsetPointXY(
		const FVector& Corner,
		const FVector& FirstOutwardNormal,
		const FVector& SecondOutwardNormal,
		float FirstInset,
		float SecondInset,
		FVector& OutPoint)
	{
		const float Det = CrossForPillarCornerSnapXY(FirstOutwardNormal, SecondOutwardNormal);
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

	FVector GetPillarCornerSnapYAxisFromXAxis(const FVector& WorldXAxis)
	{
		return WorldXAxis.RotateAngleAxis(90.0f, FVector::UpVector).GetSafeNormal2D();
	}

	float GetPillarCornerSnapRectHalfExtentAlongNormal(
		const FVector& WorldNormal,
		const FVector& WorldXAxis,
		float HalfWidth,
		float HalfDepth)
	{
		const FVector SafeNormal = WorldNormal.GetSafeNormal2D();
		const FVector SafeXAxis = WorldXAxis.GetSafeNormal2D();
		const FVector SafeYAxis = GetPillarCornerSnapYAxisFromXAxis(SafeXAxis);
		if (SafeNormal.IsNearlyZero() || SafeXAxis.IsNearlyZero() || SafeYAxis.IsNearlyZero())
		{
			return FMath::Max(HalfWidth, HalfDepth);
		}

		return FMath::Abs(FVector::DotProduct(SafeNormal, SafeXAxis)) * FMath::Max(0.0f, HalfWidth)
			+ FMath::Abs(FVector::DotProduct(SafeNormal, SafeYAxis)) * FMath::Max(0.0f, HalfDepth);
	}

	void AddUniquePillarCornerSnapAxis(TArray<FVector>& AxisOptions, const FVector& Axis)
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

	bool ResolvePillarCreationFloorSlabCornerSnap(
		const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation,
		float PillarWidth,
		float PillarDepth,
		float MaxDistance,
		FVector& OutWorldLocation,
		float* OutSnapDistance = nullptr,
		int32* OutPillarFloorIndex = nullptr)
	{
		UWorld* World = Building ? Building->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		const float HalfWidth = FMath::Max(1.0f, PillarWidth) * 0.5f;
		const float HalfDepth = FMath::Max(1.0f, PillarDepth) * 0.5f;
		const float PillarCornerReach = FMath::Sqrt(HalfWidth * HalfWidth + HalfDepth * HalfDepth);
		const float SafeMaxDistance = FMath::Max(0.0f, MaxDistance);
		const float HeightTolerance = FMath::Max(45.0f, SafeMaxDistance * 1.5f);
		bool bHasBestCandidate = false;
		float BestScore = TNumericLimits<float>::Max();
		float BestDistance = TNumericLimits<float>::Max();
		FVector BestLocation = DesiredWorldLocation;
		int32 BestPillarFloorIndex = 1;
		constexpr float EdgeCandidatePriorityPenalty = 1.0f;

		auto GetSegmentAlpha =
			[](const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point)
			-> float
		{
			const FVector Segment(SegmentEnd.X - SegmentStart.X, SegmentEnd.Y - SegmentStart.Y, 0.0f);
			const float SegmentLengthSquared = Segment.SizeSquared2D();
			if (SegmentLengthSquared <= UE_SMALL_NUMBER)
			{
				return 0.0f;
			}

			const FVector ToPoint(Point.X - SegmentStart.X, Point.Y - SegmentStart.Y, 0.0f);
			return FMath::Clamp(FVector::DotProduct(ToPoint, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
		};

		auto ConsiderCandidate =
			[&](const FVector& CandidateLocation, float ProximityScore, float PriorityPenalty, int32 CandidatePillarFloorIndex)
		{
			const float Score = ProximityScore + PriorityPenalty;
			if (!bHasBestCandidate || Score < BestScore)
			{
				bHasBestCandidate = true;
				BestScore = Score;
				BestDistance = ProximityScore;
				BestLocation = CandidateLocation;
				BestPillarFloorIndex = FMath::Max(1, CandidatePillarFloorIndex);
			}
		};

		for (TActorIterator<AEHB_FloorSlab> It(World); It; ++It)
		{
			AEHB_FloorSlab* Slab = *It;
			if (!Slab || Slab->IsActorBeingDestroyed())
			{
				continue;
			}

			if ((Building || Slab->OwningBuilding) && Slab->OwningBuilding != Building)
			{
				continue;
			}

			TArray<TArray<FVector>> WorldPolygons;
			if (!Slab->BuildEffectiveOuterWorldPolygons(WorldPolygons, true, true))
			{
				continue;
			}
 for(const auto& WorldPolygon:WorldPolygons)
 {


			const float SlabTopWorldZ = WorldPolygon[0].Z;
			if (FMath::Abs(SlabTopWorldZ - DesiredWorldLocation.Z) > HeightTolerance)
			{
				continue;
			}

			const float SignedArea = CalculateSignedAreaXY(WorldPolygon);
			if (FMath::Abs(SignedArea) <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const bool bCounterClockwise = SignedArea > 0.0f;
			const int32 CandidatePillarFloorIndex = ResolveNextFloorIndexFromElement(Slab);

			for (int32 EdgeIndex = 0; EdgeIndex < WorldPolygon.Num(); ++EdgeIndex)
			{
				const FVector& EdgeStart = WorldPolygon[EdgeIndex];
				const FVector& EdgeEnd = WorldPolygon[(EdgeIndex + 1) % WorldPolygon.Num()];
				FVector EdgeDirection(EdgeEnd.X - EdgeStart.X, EdgeEnd.Y - EdgeStart.Y, 0.0f);
				if (!EdgeDirection.Normalize())
				{
					continue;
				}

				const float Alpha = GetSegmentAlpha(EdgeStart, EdgeEnd, DesiredWorldLocation);
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

				TArray<FVector> AxisOptions;
				AddUniquePillarCornerSnapAxis(AxisOptions, EdgeDirection);
				AddUniquePillarCornerSnapAxis(AxisOptions, -OutwardNormal);

				for (const FVector& AxisX : AxisOptions)
				{
					const float Inset = GetPillarCornerSnapRectHalfExtentAlongNormal(OutwardNormal, AxisX, HalfWidth, HalfDepth);
					const FVector EdgePoint = FMath::Lerp(EdgeStart, EdgeEnd, Alpha);
					FVector CandidateLocation = EdgePoint - OutwardNormal * Inset;
					CandidateLocation.Z = EdgePoint.Z;

					const float CenterDistance = FVector::Dist2D(DesiredWorldLocation, CandidateLocation);
					if (CenterDistance > SafeMaxDistance)
					{
						continue;
					}

					ConsiderCandidate(CandidateLocation, CenterDistance, EdgeCandidatePriorityPenalty, CandidatePillarFloorIndex);
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

				TArray<FVector> AxisOptions;
				AddUniquePillarCornerSnapAxis(AxisOptions, -PrevOutwardNormal);
				AddUniquePillarCornerSnapAxis(AxisOptions, -NextOutwardNormal);
				for (const FVector& AxisX : AxisOptions)
				{
					const float PrevInset = GetPillarCornerSnapRectHalfExtentAlongNormal(PrevOutwardNormal, AxisX, HalfWidth, HalfDepth);
					const float NextInset = GetPillarCornerSnapRectHalfExtentAlongNormal(NextOutwardNormal, AxisX, HalfWidth, HalfDepth);
					FVector CandidateLocation = FVector::ZeroVector;
					if (!SolvePillarCornerSnapOffsetPointXY(
						CornerPoint,
						PrevOutwardNormal,
						NextOutwardNormal,
						PrevInset,
						NextInset,
						CandidateLocation))
					{
						continue;
					}
					CandidateLocation.Z = CornerPoint.Z;

					const float CenterDistance = FVector::Dist2D(DesiredWorldLocation, CandidateLocation);
					const float CornerReachDistance = FMath::Max(0.0f, FVector::Dist2D(DesiredWorldLocation, CornerPoint) - PillarCornerReach);
					if (CenterDistance > SafeMaxDistance && CornerReachDistance > SafeMaxDistance)
					{
						continue;
					}

					ConsiderCandidate(CandidateLocation, FMath::Min(CenterDistance, CornerReachDistance), 0.0f, CandidatePillarFloorIndex);
				}
			}

 }
}

		if (!bHasBestCandidate || FVector::DistSquared(DesiredWorldLocation, BestLocation) <= 0.01f)
		{
			return false;
		}

		OutWorldLocation = BestLocation;
		if (OutSnapDistance)
		{
			*OutSnapDistance = BestDistance;
		}
		if (OutPillarFloorIndex)
		{
			*OutPillarFloorIndex = BestPillarFloorIndex;
		}
		return true;
	}

	float GetClosestAlphaOnSegmentXY(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point)
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

	float GetDistanceSquaredXY(const FVector& A, const FVector& B)
	{
		const float DeltaX = A.X - B.X;
		const float DeltaY = A.Y - B.Y;
		return DeltaX * DeltaX + DeltaY * DeltaY;
	}

	float GetDistanceSquaredToSegmentXY(const FVector& Point, const FVector& SegmentStart, const FVector& SegmentEnd)
	{
		const float Alpha = GetClosestAlphaOnSegmentXY(SegmentStart, SegmentEnd, Point);
		const FVector ClosestPoint = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);
		return GetDistanceSquaredXY(Point, ClosestPoint);
	}

	float GetDistanceSquaredToPolylineXY(const FVector& Point, const TArray<FVector>& Polyline)
	{
		float BestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 PointIndex = 0; PointIndex + 1 < Polyline.Num(); ++PointIndex)
		{
			if (FVector::DistSquared2D(Polyline[PointIndex], Polyline[PointIndex + 1]) <= UE_SMALL_NUMBER)
			{
				continue;
			}

			BestDistanceSquared = FMath::Min(
				BestDistanceSquared,
				GetDistanceSquaredToSegmentXY(Point, Polyline[PointIndex], Polyline[PointIndex + 1]));
		}

		return BestDistanceSquared;
	}

	FVector GetSafeHorizontalVector(const FVector& Vector)
	{
		FVector HorizontalVector(Vector.X, Vector.Y, 0.0f);
		return HorizontalVector.GetSafeNormal();
	}

	bool ResolveSideSnapPlacement(const FEHBFloorSlabSideSnapResult& SnapResult, float SlabSize, FVector& OutWorldLocation, FRotator& OutWorldRotation)
	{
		FVector Outward = GetSafeHorizontalVector(SnapResult.WorldOutward);
		if (Outward.IsNearlyZero())
		{
			return false;
		}

		FVector Tangent = GetSafeHorizontalVector(SnapResult.WorldTangent - Outward * FVector::DotProduct(SnapResult.WorldTangent, Outward));
		if (Tangent.IsNearlyZero())
		{
			Tangent = GetSafeHorizontalVector(FVector::CrossProduct(Outward, FVector::UpVector));
		}
		if (Tangent.IsNearlyZero())
		{
			return false;
		}

		if (FVector::DotProduct(FVector::CrossProduct(Tangent, Outward), FVector::UpVector) < 0.0f)
		{
			Tangent *= -1.0f;
		}

		OutWorldLocation = SnapResult.WorldTopEdgePoint + Outward * (FMath::Max(10.0f, SlabSize) * 0.5f);
		OutWorldRotation = FRotationMatrix::MakeFromXY(Tangent, Outward).Rotator();
		return true;
	}

	bool ResolvePolygonTopSideSnap(
		const TArray<FVector>& LocalPolygon,
		const FTransform& ActorTransform,
		const FVector& WorldHitPoint,
		const FVector& WorldHitNormal,
		float TopLocalZ,
		FEHBFloorSlabSideSnapResult& OutSnapResult)
	{
		if (LocalPolygon.Num() < 3)
		{
			return false;
		}

		const float SignedArea = CalculateSignedAreaXY(LocalPolygon);
		if (FMath::Abs(SignedArea) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const bool bCounterClockwise = SignedArea > 0.0f;
		const FVector LocalHitPoint = ActorTransform.InverseTransformPosition(WorldHitPoint);
		float BestDistanceSquared = TNumericLimits<float>::Max();
		int32 BestEdgeIndex = INDEX_NONE;
		float BestAlpha = 0.0f;

		for (int32 Index = 0; Index < LocalPolygon.Num(); ++Index)
		{
			const FVector& EdgeStart = LocalPolygon[Index];
			const FVector& EdgeEnd = LocalPolygon[(Index + 1) % LocalPolygon.Num()];
			if (FVector::DistSquared2D(EdgeStart, EdgeEnd) <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const float Alpha = GetClosestAlphaOnSegmentXY(EdgeStart, EdgeEnd, LocalHitPoint);
			FVector ClosestPoint = FMath::Lerp(EdgeStart, EdgeEnd, Alpha);
			ClosestPoint.Z = TopLocalZ;
			const float DistanceSquared = GetDistanceSquaredXY(ClosestPoint, LocalHitPoint);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestEdgeIndex = Index;
				BestAlpha = Alpha;
			}
		}

		if (!LocalPolygon.IsValidIndex(BestEdgeIndex))
		{
			return false;
		}

		const FVector& EdgeStart = LocalPolygon[BestEdgeIndex];
		const FVector& EdgeEnd = LocalPolygon[(BestEdgeIndex + 1) % LocalPolygon.Num()];
		const FVector EdgeDirection = GetSafeHorizontalVector(EdgeEnd - EdgeStart);
		if (EdgeDirection.IsNearlyZero())
		{
			return false;
		}

		FVector LocalTopEdgePoint = FMath::Lerp(EdgeStart, EdgeEnd, BestAlpha);
		LocalTopEdgePoint.Z = TopLocalZ;

		FVector LocalOutward = bCounterClockwise
			? FVector(EdgeDirection.Y, -EdgeDirection.X, 0.0f)
			: FVector(-EdgeDirection.Y, EdgeDirection.X, 0.0f);
		LocalOutward = GetSafeHorizontalVector(LocalOutward);

		FVector WorldOutward = GetSafeHorizontalVector(ActorTransform.TransformVectorNoScale(LocalOutward));
		const FVector WorldHitNormalXY = GetSafeHorizontalVector(WorldHitNormal);
		if (!WorldHitNormalXY.IsNearlyZero()
			&& FVector::DotProduct(WorldOutward, WorldHitNormalXY) < 0.0f)
		{
			WorldOutward *= -1.0f;
		}

		OutSnapResult.WorldTopEdgePoint = ActorTransform.TransformPosition(LocalTopEdgePoint);
		OutSnapResult.WorldTangent = GetSafeHorizontalVector(ActorTransform.TransformVectorNoScale(EdgeDirection));
		OutSnapResult.WorldOutward = WorldOutward;
		return true;
	}

	bool ResolveWallTopSideSnap(const FHitResult& HitResult, FEHBFloorSlabSideSnapResult& OutSnapResult)
	{
		AEHB_Wall* Wall = ResolveActorFromHit<AEHB_Wall>(HitResult);
		if (!Wall || !IsLikelyVerticalSideHit(HitResult))
		{
			return false;
		}

		const UActorComponent* HitComponent = HitResult.GetComponent();
		const bool bLeftSide = HitComponent && HitComponent->ComponentTags.Contains(TEXT("EHB_LeftWall"));
		const bool bRightSide = HitComponent && HitComponent->ComponentTags.Contains(TEXT("EHB_RightWall"));
		if (!bLeftSide && !bRightSide)
		{
			return false;
		}

		const float SideSign = bLeftSide ? 1.0f : -1.0f;
		const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		if (WallLength <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FTransform WallTransform = Wall->GetActorTransform();
		const FVector LocalHitPoint = WallTransform.InverseTransformPosition(HitResult.ImpactPoint);
		const float HalfLength = WallLength * 0.5f;
		const float SideY = SideSign * FMath::Max(1.0f, Wall->Thickness) * 0.5f;
		const float TopZ = FMath::Max(1.0f, Wall->Height);
		const bool bCurvedWall = FMath::Abs(Wall->CurveControlOffset) > 0.1f && WallLength > 1.0f;
		const int32 SegmentCount = bCurvedWall
			? FMath::Clamp(FMath::CeilToInt(WallLength / FMath::Max(10.0f, Wall->CurveSegmentLength)), 8, 128)
			: 1;

		float BestDistanceSquared = TNumericLimits<float>::Max();
		float BestStraightX = 0.0f;
		FVector BestLocalTopEdgePoint = FVector::ZeroVector;
		FVector BestLocalTangent = FVector::ForwardVector;

		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			const float SegmentStartAlpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float SegmentEndAlpha = static_cast<float>(SegmentIndex + 1) / static_cast<float>(SegmentCount);
			const float StartX = FMath::Lerp(-HalfLength, HalfLength, SegmentStartAlpha);
			const float EndX = FMath::Lerp(-HalfLength, HalfLength, SegmentEndAlpha);
			const FVector SegmentStart = Wall->TransformStraightWallLocalPointToCurve(FVector(StartX, SideY, TopZ));
			const FVector SegmentEnd = Wall->TransformStraightWallLocalPointToCurve(FVector(EndX, SideY, TopZ));
			if (FVector::DistSquared2D(SegmentStart, SegmentEnd) <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const float SegmentAlpha = GetClosestAlphaOnSegmentXY(SegmentStart, SegmentEnd, LocalHitPoint);
			const FVector ClosestPoint = FMath::Lerp(SegmentStart, SegmentEnd, SegmentAlpha);
			const float DistanceSquared = GetDistanceSquaredXY(ClosestPoint, LocalHitPoint);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestStraightX = FMath::Lerp(StartX, EndX, SegmentAlpha);
				BestLocalTopEdgePoint = ClosestPoint;
				BestLocalTangent = SegmentEnd - SegmentStart;
			}
		}

		if (BestDistanceSquared == TNumericLimits<float>::Max())
		{
			return false;
		}

		const FVector StraightSidePoint(BestStraightX, SideY, TopZ);
		FVector LocalOutward = Wall->TransformStraightWallLocalVectorToCurve(StraightSidePoint, FVector(0.0f, SideSign, 0.0f));
		FVector WorldOutward = GetSafeHorizontalVector(WallTransform.TransformVectorNoScale(LocalOutward));
		const FVector WorldHitNormalXY = GetSafeHorizontalVector(HitResult.ImpactNormal);
		if (!WorldHitNormalXY.IsNearlyZero()
			&& FVector::DotProduct(WorldOutward, WorldHitNormalXY) < 0.0f)
		{
			WorldOutward *= -1.0f;
		}

		OutSnapResult.WorldTopEdgePoint = WallTransform.TransformPosition(BestLocalTopEdgePoint);
		OutSnapResult.WorldTangent = GetSafeHorizontalVector(WallTransform.TransformVectorNoScale(BestLocalTangent));
		OutSnapResult.WorldOutward = WorldOutward;
		OutSnapResult.AnchorActor = Wall;
		OutSnapResult.AnchorWall = Wall;
		OutSnapResult.AnchorWallSide = bLeftSide ? EEHBFloorSlabWallSide::Left : EEHBFloorSlabWallSide::Right;
		OutSnapResult.AnchorFloorIndex = Wall->FloorIndex > 0 ? Wall->FloorIndex : 1;
		OutSnapResult.bAnchoredOnWallOrPillar = true;
		return true;
	}

	bool ResolvePillarTopSideSnap(const FHitResult& HitResult, FEHBFloorSlabSideSnapResult& OutSnapResult)
	{
		AEHB_Pillar* Pillar = ResolveActorFromHit<AEHB_Pillar>(HitResult);
		if (!Pillar || !IsLikelyVerticalSideHit(HitResult))
		{
			return false;
		}

		TArray<FVector> LocalFootprint;
		Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
		if (!ResolvePolygonTopSideSnap(
			LocalFootprint,
			Pillar->GetActorTransform(),
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			FMath::Max(1.0f, Pillar->Height),
			OutSnapResult))
		{
			return false;
		}

		OutSnapResult.AnchorFloorIndex = Pillar->FloorIndex > 0 ? Pillar->FloorIndex : 1;
		OutSnapResult.AnchorActor = Pillar;
		OutSnapResult.bAnchoredOnWallOrPillar = true;
		return true;
	}

	bool ResolveExistingFloorSlabTopSideSnap(const FHitResult& HitResult, const AEHB_FloorSlab* IgnoredFloorSlab, FEHBFloorSlabSideSnapResult& OutSnapResult)
	{
		AEHB_FloorSlab* FloorSlab = ResolveActorFromHit<AEHB_FloorSlab>(HitResult);
		if (!FloorSlab || FloorSlab == IgnoredFloorSlab || !IsLikelyVerticalSideHit(HitResult))
		{
			return false;
		}

		TArray<UEHBArchitecturalSurfaceComponent*> SurfaceComponents;
		FloorSlab->GetComponents(SurfaceComponents);
		if (SurfaceComponents.IsEmpty())
		{
			return false;
		}

		TArray<FEHBSurfaceSideSnapEdge> SurfaceEdges;
		for (UEHBArchitecturalSurfaceComponent* SurfaceComponent : SurfaceComponents)
		{
			if (SurfaceComponent)
			{
				SurfaceComponent->BuildSideSnapEdges(SurfaceEdges);
			}
		}
		if (SurfaceEdges.IsEmpty())
		{
			return false;
		}

		float BestDistanceSquared = TNumericLimits<float>::Max();
		const FEHBSurfaceSideSnapEdge* BestEdge = nullptr;
		float BestAlpha = 0.0f;
		for (const FEHBSurfaceSideSnapEdge& Edge : SurfaceEdges)
		{
			if (!Edge.IsValid() || FVector::DistSquared2D(Edge.WorldStart, Edge.WorldEnd) <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const float Alpha = GetClosestAlphaOnSegmentXY(Edge.WorldStart, Edge.WorldEnd, HitResult.ImpactPoint);
			const FVector ClosestPoint = FMath::Lerp(Edge.WorldStart, Edge.WorldEnd, Alpha);
			const float DistanceSquared = GetDistanceSquaredXY(ClosestPoint, HitResult.ImpactPoint);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestEdge = &Edge;
				BestAlpha = Alpha;
			}
		}

		if (!BestEdge)
		{
			return false;
		}

		FVector WorldOutward = GetSafeHorizontalVector(BestEdge->WorldOutwardNormal);
		const FVector WorldHitNormalXY = GetSafeHorizontalVector(HitResult.ImpactNormal);
		if (!WorldHitNormalXY.IsNearlyZero()
			&& FVector::DotProduct(WorldOutward, WorldHitNormalXY) < 0.0f)
		{
			WorldOutward *= -1.0f;
		}

		OutSnapResult.WorldTopEdgePoint = FMath::Lerp(BestEdge->WorldStart, BestEdge->WorldEnd, BestAlpha);
		OutSnapResult.WorldTangent = GetSafeHorizontalVector(BestEdge->WorldDirection);
		OutSnapResult.WorldOutward = WorldOutward;
		OutSnapResult.AnchorActor = FloorSlab;
		return true;
	}

	bool ResolveFloorSlabSideSnapFromHit(const FHitResult& HitResult, const AEHB_FloorSlab* IgnoredFloorSlab, FEHBFloorSlabSideSnapResult& OutSnapResult)
	{
		if (!HitResult.bBlockingHit)
		{
			return false;
		}

		if (ResolvePillarTopSideSnap(HitResult, OutSnapResult))
		{
			return true;
		}
		if (ResolveWallTopSideSnap(HitResult, OutSnapResult))
		{
			return true;
		}
		if (ResolveExistingFloorSlabTopSideSnap(HitResult, IgnoredFloorSlab, OutSnapResult))
		{
			return true;
		}

		return false;
	}

	bool CalculatePlacementStairStepsForHeight(float StairHeight, int32& OutStepCount, float& OutStepHeight)
	{
		OutStepCount = 0;
		OutStepHeight = 0.0f;
		if (StairHeight <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FEHBStairData Defaults;
		float MinStepHeight = FMath::Max(1.0f, Defaults.MinStepHeight);
		float MaxStepHeight = FMath::Max(1.0f, Defaults.MaxStepHeight);
		if (MinStepHeight > MaxStepHeight)
		{
			Swap(MinStepHeight, MaxStepHeight);
		}

		OutStepCount = FMath::Max(1, FMath::CeilToInt(StairHeight / MaxStepHeight));
		OutStepHeight = StairHeight / static_cast<float>(OutStepCount);
		while (OutStepCount > 1 && OutStepHeight < MinStepHeight)
		{
			--OutStepCount;
			OutStepHeight = StairHeight / static_cast<float>(OutStepCount);
		}
		return true;
	}

	float CalculatePlacementStairLengthForSteps(float TreadDepth, int32 StepCount)
	{
		const FEHBStairData Defaults;
		const float SafeTreadDepth = FMath::Max(1.0f, TreadDepth);
		const float NosingLength = FMath::Clamp(Defaults.NosingLength, 0.0f, FMath::Max(0.0f, SafeTreadDepth - 1.0f));
		const float StepRun = FMath::Max(1.0f, SafeTreadDepth - NosingLength);
		return SafeTreadDepth + StepRun * static_cast<float>(FMath::Max(0, StepCount - 1));
	}

	bool ResolvePlacementStairHeightFromPlatformDrop(float PlatformDrop, float& OutStairHeight, float& OutStepHeight, int32& OutVisibleStepCount)
	{
		OutStairHeight = 0.0f;
		OutStepHeight = 0.0f;
		OutVisibleStepCount = 0;
		if (PlatformDrop <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FEHBStairData Defaults;
		float MinStepHeight = FMath::Max(1.0f, Defaults.MinStepHeight);
		float MaxStepHeight = FMath::Max(1.0f, Defaults.MaxStepHeight);
		if (MinStepHeight > MaxStepHeight)
		{
			Swap(MinStepHeight, MaxStepHeight);
		}

		int32 TotalRiserCount = FMath::Max(2, FMath::CeilToInt(PlatformDrop / MaxStepHeight));
		OutStepHeight = PlatformDrop / static_cast<float>(TotalRiserCount);
		while (TotalRiserCount > 2 && OutStepHeight < MinStepHeight)
		{
			--TotalRiserCount;
			OutStepHeight = PlatformDrop / static_cast<float>(TotalRiserCount);
		}

		OutVisibleStepCount = FMath::Max(1, TotalRiserCount - 1);
		OutStairHeight = OutStepHeight * static_cast<float>(OutVisibleStepCount);
		return OutStairHeight > KINDA_SMALL_NUMBER;
	}

	struct FEHBFloorSlabRoomHalfEdge
	{
		int32 Index = INDEX_NONE;
		TWeakObjectPtr<AEHB_Wall> Wall;
		FGuid WallGuid;
		EEHBFloorSlabWallSide WallSide = EEHBFloorSlabWallSide::None;
		FGuid FromPillarGuid;
		FGuid ToPillarGuid;
		TArray<FVector> BuildingLocalPolyline;
		FVector StartDirection = FVector::ForwardVector;
		FVector EndDirection = FVector::ForwardVector;
	};

	struct FEHBFloorSlabRoomCycle
	{
		FString Key;
		TArray<int32> HalfEdgeIndices;
		TArray<FVector> BuildingLocalPolygon;
		float SignedArea = 0.0f;
		float AbsArea = 0.0f;
	};

	void ReversePoints(TArray<FVector>& Points)
	{
		for (int32 FirstIndex = 0, LastIndex = Points.Num() - 1; FirstIndex < LastIndex; ++FirstIndex, --LastIndex)
		{
			Swap(Points[FirstIndex], Points[LastIndex]);
		}
	}

	void AddUniquePointXY(TArray<FVector>& Points, const FVector& Point, float Tolerance = 0.25f)
	{
		if (!Points.IsEmpty() && FVector::DistSquared2D(Points.Last(), Point) <= Tolerance * Tolerance)
		{
			return;
		}
		Points.Add(Point);
	}

	void CleanPolygonXY(TArray<FVector>& Points)
	{
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (FVector::DistSquared2D(Points[Index], Points[Index - 1]) <= 0.25f * 0.25f)
			{
				Points.RemoveAt(Index);
			}
		}

		if (Points.Num() > 2 && FVector::DistSquared2D(Points[0], Points.Last()) <= 0.25f * 0.25f)
		{
			Points.Pop(EAllowShrinking::No);
		}
	}

	bool CleanAutoFillPolygonXY(TArray<FVector>& Points, float MergeTolerance = 2.0f)
	{
		if (Points.Num() < 2)
		{
			return false;
		}

		const float MergeToleranceSquared = MergeTolerance * MergeTolerance;
		for (int32 GuardIndex = 0; GuardIndex < 256 && Points.Num() > 1; ++GuardIndex)
		{
			bool bMergedAnyPoint = false;
			for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 1; --Index)
			{
				const int32 PreviousIndex = (Index - 1 + Points.Num()) % Points.Num();
				if (PreviousIndex == Index)
				{
					continue;
				}

				if (FVector::DistSquared2D(Points[PreviousIndex], Points[Index]) <= MergeToleranceSquared)
				{
					Points[PreviousIndex] = (Points[PreviousIndex] + Points[Index]) * 0.5f;
					Points.RemoveAt(Index, 1, EAllowShrinking::No);
					bMergedAnyPoint = true;
				}
			}

			if (!bMergedAnyPoint)
			{
				break;
			}
		}

		constexpr float CollinearTolerance = 0.05f;
		const float CollinearToleranceSquared = CollinearTolerance * CollinearTolerance;
		for (int32 GuardIndex = 0; GuardIndex < 256 && Points.Num() > 3; ++GuardIndex)
		{
			bool bRemovedAnyPoint = false;
			for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 3; --Index)
			{
				const int32 PreviousIndex = (Index - 1 + Points.Num()) % Points.Num();
				const int32 NextIndex = (Index + 1) % Points.Num();
				if (PreviousIndex == NextIndex
					|| FVector::DistSquared2D(Points[PreviousIndex], Points[NextIndex]) <= MergeToleranceSquared)
				{
					continue;
				}

				if (GetDistanceSquaredToSegmentXY(Points[Index], Points[PreviousIndex], Points[NextIndex]) <= CollinearToleranceSquared)
				{
					Points.RemoveAt(Index, 1, EAllowShrinking::No);
					bRemovedAnyPoint = true;
				}
			}

			if (!bRemovedAnyPoint)
			{
				break;
			}
		}

		return Points.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(Points)) > 1.0f;
	}

	AEHB_Pillar* FindPillarByGuid(AEHBBuildingActorBase* Building, const FGuid& PillarGuid)
	{
		if (!Building || !PillarGuid.IsValid())
		{
			return nullptr;
		}

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor);
			if (Pillar && Pillar->OwningBuilding == Building && Pillar->ElementGuid == PillarGuid)
			{
				return Pillar;
			}
		}

		return nullptr;
	}

	bool BuildPillarFootprintInBuildingSpace(
		AEHBBuildingActorBase* Building,
		const FGuid& PillarGuid,
		TArray<FVector>& OutBuildingLocalFootprint)
	{
		OutBuildingLocalFootprint.Reset();
		AEHB_Pillar* Pillar = FindPillarByGuid(Building, PillarGuid);
		if (!Pillar)
		{
			return false;
		}

		TArray<FVector> LocalFootprint;
		Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
		if (LocalFootprint.Num() < 3)
		{
			return false;
		}

		const FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
		OutBuildingLocalFootprint.Reserve(LocalFootprint.Num());
		for (FVector LocalPoint : LocalFootprint)
		{
			LocalPoint.Z = 0.0f;
			FVector BuildingLocalPoint = PillarLocalTransform.TransformPosition(LocalPoint);
			BuildingLocalPoint.Z = 0.0f;
			AddUniquePointXY(OutBuildingLocalFootprint, BuildingLocalPoint, 0.5f);
		}

		CleanPolygonXY(OutBuildingLocalFootprint);
		return OutBuildingLocalFootprint.Num() >= 3
			&& FMath::Abs(CalculateSignedAreaXY(OutBuildingLocalFootprint)) > 1.0f;
	}

	int32 FindClosestLoopPointIndexXY(const TArray<FVector>& Loop, const FVector& Point, float& OutDistanceSquared)
	{
		int32 BestIndex = INDEX_NONE;
		OutDistanceSquared = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const float DistanceSquared = FVector::DistSquared2D(Loop[Index], Point);
			if (DistanceSquared < OutDistanceSquared)
			{
				OutDistanceSquared = DistanceSquared;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	float CalculateLoopPathLengthXY(const TArray<FVector>& Loop, int32 FromIndex, int32 ToIndex, int32 Step)
	{
		if (Loop.Num() < 2 || !Loop.IsValidIndex(FromIndex) || !Loop.IsValidIndex(ToIndex) || Step == 0)
		{
			return TNumericLimits<float>::Max();
		}

		float Length = 0.0f;
		int32 CurrentIndex = FromIndex;
		for (int32 GuardIndex = 0; GuardIndex < Loop.Num(); ++GuardIndex)
		{
			if (CurrentIndex == ToIndex)
			{
				return Length;
			}

			const int32 NextIndex = (CurrentIndex + Step + Loop.Num()) % Loop.Num();
			Length += FVector::Dist2D(Loop[CurrentIndex], Loop[NextIndex]);
			CurrentIndex = NextIndex;
		}

		return TNumericLimits<float>::Max();
	}

	void AppendLoopPathPointsXY(
		const TArray<FVector>& Loop,
		int32 FromIndex,
		int32 ToIndex,
		int32 Step,
		TArray<FVector>& OutPoints)
	{
		if (Loop.Num() < 2 || !Loop.IsValidIndex(FromIndex) || !Loop.IsValidIndex(ToIndex) || Step == 0)
		{
			return;
		}

		int32 CurrentIndex = FromIndex;
		for (int32 GuardIndex = 0; GuardIndex < Loop.Num(); ++GuardIndex)
		{
			if (CurrentIndex == ToIndex)
			{
				break;
			}

			CurrentIndex = (CurrentIndex + Step + Loop.Num()) % Loop.Num();
			AddUniquePointXY(OutPoints, Loop[CurrentIndex], 0.5f);
		}
	}

	void AppendPillarBoundaryConnector(
		AEHBBuildingActorBase* Building,
		const FGuid& PillarGuid,
		const FVector& FromPoint,
		const FVector& ToPoint,
		TArray<FVector>& OutPolygon)
	{
		TArray<FVector> Footprint;
		if (!BuildPillarFootprintInBuildingSpace(Building, PillarGuid, Footprint))
		{
			AddUniquePointXY(OutPolygon, ToPoint, 0.5f);
			return;
		}

		float FromDistanceSquared = 0.0f;
		float ToDistanceSquared = 0.0f;
		const int32 FromIndex = FindClosestLoopPointIndexXY(Footprint, FromPoint, FromDistanceSquared);
		const int32 ToIndex = FindClosestLoopPointIndexXY(Footprint, ToPoint, ToDistanceSquared);
		constexpr float ConnectorSnapTolerance = 50.0f;
		if (!Footprint.IsValidIndex(FromIndex)
			|| !Footprint.IsValidIndex(ToIndex)
			|| FromDistanceSquared > ConnectorSnapTolerance * ConnectorSnapTolerance
			|| ToDistanceSquared > ConnectorSnapTolerance * ConnectorSnapTolerance)
		{
			AddUniquePointXY(OutPolygon, ToPoint, 0.5f);
			return;
		}

		const float ForwardLength = CalculateLoopPathLengthXY(Footprint, FromIndex, ToIndex, 1);
		const float BackwardLength = CalculateLoopPathLengthXY(Footprint, FromIndex, ToIndex, -1);
		const int32 Step = ForwardLength <= BackwardLength ? 1 : -1;
		AppendLoopPathPointsXY(Footprint, FromIndex, ToIndex, Step, OutPolygon);
		AddUniquePointXY(OutPolygon, ToPoint, 0.5f);
	}

	bool BuildRoomHalfEdges(
		AEHBBuildingActorBase* Building,
		TArray<FEHBFloorSlabRoomHalfEdge>& OutHalfEdges,
		TMultiMap<FGuid, int32>& OutOutgoingByPillar)
	{
		OutHalfEdges.Reset();
		OutOutgoingByPillar.Reset();
		if (!Building)
		{
			return false;
		}

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);

		auto AddHalfEdge =
			[&OutHalfEdges, &OutOutgoingByPillar](
				AEHB_Wall* Wall,
				EEHBFloorSlabWallSide Side,
				const FGuid& FromPillarGuid,
				const FGuid& ToPillarGuid,
				TArray<FVector>&& BuildingLocalPolyline)
		{
			if (!Wall
				|| !Wall->ElementGuid.IsValid()
				|| !FromPillarGuid.IsValid()
				|| !ToPillarGuid.IsValid()
				|| BuildingLocalPolyline.Num() < 2)
			{
				return;
			}

			FEHBFloorSlabRoomHalfEdge HalfEdge;
			HalfEdge.Index = OutHalfEdges.Num();
			HalfEdge.Wall = Wall;
			HalfEdge.WallGuid = Wall->ElementGuid;
			HalfEdge.WallSide = Side;
			HalfEdge.FromPillarGuid = FromPillarGuid;
			HalfEdge.ToPillarGuid = ToPillarGuid;
			HalfEdge.BuildingLocalPolyline = MoveTemp(BuildingLocalPolyline);
			HalfEdge.StartDirection = GetSafeHorizontalVector(HalfEdge.BuildingLocalPolyline[1] - HalfEdge.BuildingLocalPolyline[0]);
			HalfEdge.EndDirection = GetSafeHorizontalVector(HalfEdge.BuildingLocalPolyline.Last() - HalfEdge.BuildingLocalPolyline[HalfEdge.BuildingLocalPolyline.Num() - 2]);
			if (HalfEdge.StartDirection.IsNearlyZero() || HalfEdge.EndDirection.IsNearlyZero())
			{
				return;
			}

			OutOutgoingByPillar.Add(FromPillarGuid, HalfEdge.Index);
			OutHalfEdges.Add(MoveTemp(HalfEdge));
		};

		for (AActor* Actor : AttachedActors)
		{
			AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor);
			if (!Wall
				|| Wall->OwningBuilding != Building
				|| !Wall->ElementGuid.IsValid()
				|| !Wall->StartPillarGuid.IsValid()
				|| !Wall->EndPillarGuid.IsValid()
				|| Wall->StartPillarGuid == Wall->EndPillarGuid)
			{
				continue;
			}

			TArray<FVector> LeftPolyline;
			if (Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 35.0f))
			{
				AddHalfEdge(Wall, EEHBFloorSlabWallSide::Left, Wall->StartPillarGuid, Wall->EndPillarGuid, MoveTemp(LeftPolyline));
			}

			TArray<FVector> RightPolyline;
			if (Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 35.0f))
			{
				ReversePoints(RightPolyline);
				AddHalfEdge(Wall, EEHBFloorSlabWallSide::Right, Wall->EndPillarGuid, Wall->StartPillarGuid, MoveTemp(RightPolyline));
			}
		}

		return !OutHalfEdges.IsEmpty();
	}

	double GetCounterClockwiseTurnAngle(const FVector& FromDirection, const FVector& ToDirection)
	{
		const FVector From = GetSafeHorizontalVector(FromDirection);
		const FVector To = GetSafeHorizontalVector(ToDirection);
		if (From.IsNearlyZero() || To.IsNearlyZero())
		{
			return TNumericLimits<double>::Max();
		}

		const double Cross = static_cast<double>(From.X) * To.Y - static_cast<double>(From.Y) * To.X;
		const double Dot = FMath::Clamp(static_cast<double>(FVector::DotProduct(From, To)), -1.0, 1.0);
		double Angle = FMath::Atan2(Cross, Dot);
		if (Angle < 0.0)
		{
			Angle += 2.0 * PI;
		}
		return Angle;
	}

	int32 FindNextRoomHalfEdge(
		const FEHBFloorSlabRoomHalfEdge& CurrentHalfEdge,
		const TArray<int32>& CandidateIndices,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges)
	{
		int32 BestIndex = INDEX_NONE;
		double BestAngle = TNumericLimits<double>::Max();
		for (const int32 CandidateIndex : CandidateIndices)
		{
			if (!HalfEdges.IsValidIndex(CandidateIndex))
			{
				continue;
			}

			const FEHBFloorSlabRoomHalfEdge& Candidate = HalfEdges[CandidateIndex];
			const double Angle = GetCounterClockwiseTurnAngle(CurrentHalfEdge.EndDirection, Candidate.StartDirection);
			if (Angle < BestAngle)
			{
				BestAngle = Angle;
				BestIndex = CandidateIndex;
			}
		}

		return BestIndex;
	}

	bool TraceRoomCycle(
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const TMultiMap<FGuid, int32>& OutgoingByPillar,
		int32 StartHalfEdgeIndex,
		TArray<int32>& OutHalfEdgeIndices)
	{
		OutHalfEdgeIndices.Reset();
		if (!HalfEdges.IsValidIndex(StartHalfEdgeIndex))
		{
			return false;
		}

		TSet<int32> VisitedHalfEdges;
		int32 CurrentHalfEdgeIndex = StartHalfEdgeIndex;
		const int32 MaxStepCount = HalfEdges.Num() + 1;
		for (int32 StepIndex = 0; StepIndex < MaxStepCount; ++StepIndex)
		{
			if (!HalfEdges.IsValidIndex(CurrentHalfEdgeIndex) || VisitedHalfEdges.Contains(CurrentHalfEdgeIndex))
			{
				return false;
			}

			VisitedHalfEdges.Add(CurrentHalfEdgeIndex);
			OutHalfEdgeIndices.Add(CurrentHalfEdgeIndex);

			const FEHBFloorSlabRoomHalfEdge& CurrentHalfEdge = HalfEdges[CurrentHalfEdgeIndex];
			TArray<int32> CandidateIndices;
			OutgoingByPillar.MultiFind(CurrentHalfEdge.ToPillarGuid, CandidateIndices);
			if (CandidateIndices.IsEmpty())
			{
				return false;
			}

			const int32 NextHalfEdgeIndex = FindNextRoomHalfEdge(CurrentHalfEdge, CandidateIndices, HalfEdges);
			if (NextHalfEdgeIndex == INDEX_NONE)
			{
				return false;
			}

			if (NextHalfEdgeIndex == StartHalfEdgeIndex)
			{
				return OutHalfEdgeIndices.Num() >= 3;
			}

			CurrentHalfEdgeIndex = NextHalfEdgeIndex;
		}

		return false;
	}

	FString MakeRoomCycleKey(const TArray<int32>& HalfEdgeIndices)
	{
		TArray<int32> SortedIndices = HalfEdgeIndices;
		SortedIndices.Sort();

		FString Key;
		for (const int32 HalfEdgeIndex : SortedIndices)
		{
			Key += FString::FromInt(HalfEdgeIndex);
			Key += TEXT("|");
		}
		return Key;
	}

	bool BuildRoomCyclePolygon(
		AEHBBuildingActorBase* Building,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const TArray<int32>& HalfEdgeIndices,
		TArray<FVector>& OutBuildingLocalPolygon)
	{
		OutBuildingLocalPolygon.Reset();
		for (int32 CycleEdgeIndex = 0; CycleEdgeIndex < HalfEdgeIndices.Num(); ++CycleEdgeIndex)
		{
			const int32 HalfEdgeIndex = HalfEdgeIndices[CycleEdgeIndex];
			if (!HalfEdges.IsValidIndex(HalfEdgeIndex))
			{
				return false;
			}

			const FEHBFloorSlabRoomHalfEdge& HalfEdge = HalfEdges[HalfEdgeIndex];
			const TArray<FVector>& Polyline = HalfEdge.BuildingLocalPolyline;
			for (const FVector& Point : Polyline)
			{
				AddUniquePointXY(OutBuildingLocalPolygon, Point);
			}

			const int32 NextCycleEdgeIndex = (CycleEdgeIndex + 1) % HalfEdgeIndices.Num();
			const int32 NextHalfEdgeIndex = HalfEdgeIndices[NextCycleEdgeIndex];
			if (HalfEdges.IsValidIndex(NextHalfEdgeIndex)
				&& !Polyline.IsEmpty()
				&& !HalfEdges[NextHalfEdgeIndex].BuildingLocalPolyline.IsEmpty())
			{
				AppendPillarBoundaryConnector(
					Building,
					HalfEdge.ToPillarGuid,
					Polyline.Last(),
					HalfEdges[NextHalfEdgeIndex].BuildingLocalPolyline[0],
					OutBuildingLocalPolygon);
			}
		}

		return CleanAutoFillPolygonXY(OutBuildingLocalPolygon);
	}

	bool BuildRoomCycle(
		AEHBBuildingActorBase* Building,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const TArray<int32>& HalfEdgeIndices,
		FEHBFloorSlabRoomCycle& OutCycle)
	{
		OutCycle = FEHBFloorSlabRoomCycle();
		OutCycle.HalfEdgeIndices = HalfEdgeIndices;
		OutCycle.Key = MakeRoomCycleKey(HalfEdgeIndices);
		if (!BuildRoomCyclePolygon(Building, HalfEdges, HalfEdgeIndices, OutCycle.BuildingLocalPolygon))
		{
			return false;
		}

		OutCycle.SignedArea = CalculateSignedAreaXY(OutCycle.BuildingLocalPolygon);
		OutCycle.AbsArea = FMath::Abs(OutCycle.SignedArea);
		return OutCycle.AbsArea > 1.0f;
	}

	void BuildAllRoomCycles(
		AEHBBuildingActorBase* Building,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const TMultiMap<FGuid, int32>& OutgoingByPillar,
		TArray<FEHBFloorSlabRoomCycle>& OutCycles)
	{
		OutCycles.Reset();
		TSet<FString> VisitedCycleKeys;
		for (int32 HalfEdgeIndex = 0; HalfEdgeIndex < HalfEdges.Num(); ++HalfEdgeIndex)
		{
			TArray<int32> HalfEdgeIndices;
			if (!TraceRoomCycle(HalfEdges, OutgoingByPillar, HalfEdgeIndex, HalfEdgeIndices))
			{
				continue;
			}

			FEHBFloorSlabRoomCycle Cycle;
			if (!BuildRoomCycle(Building, HalfEdges, HalfEdgeIndices, Cycle) || VisitedCycleKeys.Contains(Cycle.Key))
			{
				continue;
			}

			VisitedCycleKeys.Add(Cycle.Key);
			OutCycles.Add(MoveTemp(Cycle));
		}
	}

	FVector GetPolygonCentroidXY(const TArray<FVector>& Polygon)
	{
		FVector Centroid = FVector::ZeroVector;
		for (const FVector& Point : Polygon)
		{
			Centroid += Point;
		}
		return Polygon.IsEmpty() ? FVector::ZeroVector : Centroid / static_cast<float>(Polygon.Num());
	}

	bool IsPointInsidePolygonXY(const FVector& Point, const TArray<FVector>& Polygon)
	{
		bool bInside = false;
		for (int32 Index = 0, PreviousIndex = Polygon.Num() - 1; Index < Polygon.Num(); PreviousIndex = Index++)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[PreviousIndex];
			const bool bIntersects =
				((A.Y > Point.Y) != (B.Y > Point.Y))
				&& (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X);
			if (bIntersects)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool IsPointInsideOrNearPolygonXY(const FVector& Point, const TArray<FVector>& Polygon, float BoundaryTolerance = 2.0f)
	{
		if (IsPointInsidePolygonXY(Point, Polygon))
		{
			return true;
		}

		if (Polygon.Num() < 2)
		{
			return false;
		}

		const float BoundaryToleranceSquared = BoundaryTolerance * BoundaryTolerance;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[(Index + 1) % Polygon.Num()];
			if (GetDistanceSquaredToSegmentXY(Point, A, B) <= BoundaryToleranceSquared)
			{
				return true;
			}
		}

		return false;
	}

	bool ResolvePriorityTopSnapNearLocation(
		const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation,
		float SearchDistance,
		FVector& OutWorldLocation,
		int32& OutPillarFloorIndex,
		float* OutSnapDistance = nullptr,
		EEHBPillarBaseSnapSource* OutSnapSource = nullptr)
	{
		UWorld* World = Building ? Building->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		const float SafeSearchDistance = FMath::Max(0.0f, SearchDistance);
		const float HeightTolerance = FMath::Max(8.0f, SafeSearchDistance * 0.5f);
		FEHBPillarBaseSnapCandidate BestCandidate;

		for (TActorIterator<AEHB_Pillar> It(World); It; ++It)
		{
			AEHB_Pillar* Pillar = *It;
			if (!Pillar
				|| Pillar->IsActorBeingDestroyed()
				|| Pillar->OwningBuilding != Building)
			{
				continue;
			}

			const float TopZ = FMath::Max(1.0f, Pillar->Height);
			const FTransform PillarTransform = Pillar->GetActorTransform();
			TArray<FVector> LocalFootprint;
			Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
			TArray<FVector> WorldFootprint;
			WorldFootprint.Reserve(LocalFootprint.Num());
			for (FVector LocalPoint : LocalFootprint)
			{
				LocalPoint.Z = TopZ;
				WorldFootprint.Add(PillarTransform.TransformPosition(LocalPoint));
			}

			if (WorldFootprint.Num() < 3)
			{
				continue;
			}

			const FVector TopCenter = PillarTransform.TransformPosition(FVector(0.0f, 0.0f, TopZ));
			if (FMath::Abs(TopCenter.Z - DesiredWorldLocation.Z) > HeightTolerance
				|| !IsPointInsideOrNearPolygonXY(DesiredWorldLocation, WorldFootprint, FMath::Max(4.0f, SafeSearchDistance * 0.25f)))
			{
				continue;
			}

			FEHBPillarBaseSnapCandidate Candidate;
			Candidate.WorldLocation = TopCenter;
			Candidate.PillarFloorIndex = ResolveNextFloorIndexFromElement(Pillar);
			Candidate.Distance = FVector::Dist2D(DesiredWorldLocation, TopCenter);
			Candidate.Source = EEHBPillarBaseSnapSource::PillarTop;
			Candidate.bIsValid = true;
			if (IsBetterPillarBaseSnapCandidate(Candidate, BestCandidate))
			{
				BestCandidate = Candidate;
			}
		}

		for (TActorIterator<AEHB_Wall> It(World); It; ++It)
		{
			AEHB_Wall* Wall = *It;
			if (!Wall
				|| Wall->IsActorBeingDestroyed()
				|| Wall->OwningBuilding != Building)
			{
				continue;
			}

			const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
			if (WallLength <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const float DistanceFromStart = FMath::Clamp(
				Wall->CalculateDistanceFromStartForWorldLocation(DesiredWorldLocation),
				0.0f,
				WallLength);
			const FVector TopCenter = Wall->GetWorldLocationOnCenterAxisAtDistance(DistanceFromStart, Wall->Height);
			const float AllowedDistance = SafeSearchDistance + FMath::Max(1.0f, Wall->Thickness) * 0.5f;
			if (FMath::Abs(TopCenter.Z - DesiredWorldLocation.Z) > HeightTolerance
				|| FVector::DistSquared2D(DesiredWorldLocation, TopCenter) > AllowedDistance * AllowedDistance)
			{
				continue;
			}

			FEHBPillarBaseSnapCandidate Candidate;
			Candidate.WorldLocation = TopCenter;
			Candidate.PillarFloorIndex = ResolveNextFloorIndexFromElement(Wall);
			Candidate.Distance = FVector::Dist2D(DesiredWorldLocation, TopCenter);
			Candidate.Source = EEHBPillarBaseSnapSource::WallTop;
			Candidate.bIsValid = true;
			if (IsBetterPillarBaseSnapCandidate(Candidate, BestCandidate))
			{
				BestCandidate = Candidate;
			}
		}

		if (!BestCandidate.bIsValid)
		{
			return false;
		}

		OutWorldLocation = BestCandidate.WorldLocation;
		OutPillarFloorIndex = BestCandidate.PillarFloorIndex;
		if (OutSnapDistance)
		{
			*OutSnapDistance = BestCandidate.Distance;
		}
		if (OutSnapSource)
		{
			*OutSnapSource = BestCandidate.Source;
		}
		return true;
	}

	bool ResolvePillarBaseSnapNearLocation(
		const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation,
		float PillarWidth,
		float PillarDepth,
		float SearchDistance,
		FVector& OutWorldLocation,
		int32& OutPillarFloorIndex)
	{
		const float SafeSearchDistance = FMath::Max(0.0f, SearchDistance);
		FEHBPillarBaseSnapCandidate BestCandidate;

		auto ConsiderCandidate =
			[&](const FVector& CandidateLocation, int32 PillarFloorIndex, float Distance, EEHBPillarBaseSnapSource Source)
		{
			FEHBPillarBaseSnapCandidate Candidate;
			Candidate.WorldLocation = CandidateLocation;
			Candidate.PillarFloorIndex = FMath::Max(1, PillarFloorIndex);
			Candidate.Distance = FMath::Max(0.0f, Distance);
			Candidate.Source = Source;
			Candidate.bIsValid = true;
			if (IsBetterPillarBaseSnapCandidate(Candidate, BestCandidate))
			{
				BestCandidate = Candidate;
			}
		};

		FVector TopSnapLocation = FVector::ZeroVector;
		int32 TopSnapPillarFloorIndex = 1;
		float TopSnapDistance = 0.0f;
		EEHBPillarBaseSnapSource TopSnapSource = EEHBPillarBaseSnapSource::WallTop;
		if (ResolvePriorityTopSnapNearLocation(
			Building,
			DesiredWorldLocation,
			SafeSearchDistance,
			TopSnapLocation,
			TopSnapPillarFloorIndex,
			&TopSnapDistance,
			&TopSnapSource))
		{
			ConsiderCandidate(TopSnapLocation, TopSnapPillarFloorIndex, TopSnapDistance, TopSnapSource);
		}

		FVector FloorSlabSnapLocation = FVector::ZeroVector;
		int32 FloorSlabSnapPillarFloorIndex = 1;
		float FloorSlabSnapDistance = 0.0f;
		if (ResolvePillarCreationFloorSlabCornerSnap(
			Building,
			DesiredWorldLocation,
			PillarWidth,
			PillarDepth,
			SafeSearchDistance,
			FloorSlabSnapLocation,
			&FloorSlabSnapDistance,
			&FloorSlabSnapPillarFloorIndex))
		{
			ConsiderCandidate(
				FloorSlabSnapLocation,
				FloorSlabSnapPillarFloorIndex,
				FloorSlabSnapDistance,
				EEHBPillarBaseSnapSource::FloorSlabCornerOrEdge);
		}

		if (!BestCandidate.bIsValid)
		{
			return false;
		}

		OutWorldLocation = BestCandidate.WorldLocation;
		OutPillarFloorIndex = BestCandidate.PillarFloorIndex;
		return true;
	}

	bool ResolveRailingEndpointSurfaceSnap(
		const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation,
		float PostWidth,
		float PostDepth,
		float SearchDistance,
		FVector& OutWorldLocation)
	{
		const float SafeSearchDistance = FMath::Max(0.0f, SearchDistance);
		int32 IgnoredFloorIndex = 1;
		return ResolvePillarBaseSnapNearLocation(
			Building,
			DesiredWorldLocation,
			PostWidth,
			PostDepth,
			SafeSearchDistance,
			OutWorldLocation,
			IgnoredFloorIndex);
	}

	bool BuildLogicalRoomPolygon(const FEHBNodeRoomBoundary& Room,const FEHBWallNodeModel& Model,
		const TArray<FEHBWallJunctionWallSides>& Sides,TArray<FVector>& Polygon)
	{
		Polygon.Reset();if(Room.NodeGuids.Num()<3||Room.NodeGuids.Num()!=Room.WallGuids.Num())return false;
		TArray<FVector> Candidate;
		// Nearby-point merging is order-sensitive. Room identities canonicalize by
		// GUID, which changes when provisional split IDs become actual identities.
		// Use the same geometric start before merging, in both preview and apply.
		int32 First = 0; FVector FirstPosition = FVector::ZeroVector;
		for(int32 I=0;I<Room.NodeGuids.Num();++I)
		{
			const auto* N=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Room.NodeGuids[I];});if(!N)return false;
			const FVector P=N->LocalTransform.GetLocation();
			if(I==0||P.X<FirstPosition.X||(P.X==FirstPosition.X&&(P.Y<FirstPosition.Y||(P.Y==FirstPosition.Y&&P.Z<FirstPosition.Z)))){First=I;FirstPosition=P;}
		}
		for(int32 Offset=0;Offset<Room.WallGuids.Num();++Offset)
		{
			const int32 I=(First+Offset)%Room.WallGuids.Num();
			const auto* W=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Room.WallGuids[I];});
			const auto* S=Sides.FindByPredicate([&](const auto& V){return V.WallGuid==Room.WallGuids[I];});if(!W||!S)return false;
			const FGuid From=Room.NodeGuids[I],To=Room.NodeGuids[(I+1)%Room.NodeGuids.Num()];TArray<FVector> Edge;
			if(W->StartNodeGuid==From&&W->EndNodeGuid==To)Edge={S->StartLeft,S->EndLeft};
			else if(W->EndNodeGuid==From&&W->StartNodeGuid==To)Edge={S->EndRight,S->StartRight};else return false;
			for(auto Point:Edge){Point.Z=0;AddUniquePointXY(Candidate,Point,0.5f);}
		}
		if(!CleanAutoFillPolygonXY(Candidate))return false;Polygon=MoveTemp(Candidate);return true;
	}

	bool BuildClosedLoopPolygonInBuildingSpace(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingClosedLoop& Loop,
		TArray<FVector>& OutBuildingLocalPolygon, const TArray<FEHBWallJunctionWallSides>* PlannedSides = nullptr);

	bool ConvertBuildingPolygonToFloorSlabLocal(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		const TArray<FVector>& BuildingLocalPolygon,
		TArray<FVector>& OutLocalPolygon)
	{
		OutLocalPolygon.Reset();
		if (!Building || !FloorSlab || BuildingLocalPolygon.Num() < 3)
		{
			return false;
		}

		const FTransform BuildingTransform = Building->GetActorTransform();
		const FTransform FloorSlabTransform = FloorSlab->GetActorTransform();
		for (const FVector& BuildingLocalPoint : BuildingLocalPolygon)
		{
			FVector LocalPoint = FloorSlabTransform.InverseTransformPosition(BuildingTransform.TransformPosition(BuildingLocalPoint));
			LocalPoint.Z = 0.0f;
			AddUniquePointXY(OutLocalPolygon, LocalPoint, 0.5f);
		}

		return CleanAutoFillPolygonXY(OutLocalPolygon);
	}

	bool ConvertBuildingPolygonToFloorLocal(
		const AEHBBuildingActorBase* Building,
		const AEHB_Floor* Floor,
		const TArray<FVector>& BuildingLocalPolygon,
		TArray<FVector>& OutLocalPolygon)
	{
		OutLocalPolygon.Reset();
		if (!Building || !Floor || BuildingLocalPolygon.Num() < 3)
		{
			return false;
		}

		const FTransform BuildingTransform = Building->GetActorTransform();
		const FTransform FloorTransform = Floor->GetActorTransform();
		for (const FVector& BuildingLocalPoint : BuildingLocalPolygon)
		{
			FVector LocalPoint = FloorTransform.InverseTransformPosition(BuildingTransform.TransformPosition(BuildingLocalPoint));
			AddUniquePointXY(OutLocalPolygon, LocalPoint, 0.5f);
		}

		return CleanAutoFillPolygonXY(OutLocalPolygon);
	}

	FEHBFloorFinishRegion ConvertFloorRegionFromBuildingToFloorLocal(
		const AEHBBuildingActorBase* Building,
		const AEHB_Floor* Floor,
		const FEHBFloorFinishRegion& BuildingRegion)
	{
		FEHBFloorFinishRegion LocalRegion;
		ConvertBuildingPolygonToFloorLocal(Building, Floor, BuildingRegion.OuterPolygon, LocalRegion.OuterPolygon);
		for (const FEHBFloorFinishHole& BuildingHole : BuildingRegion.Holes)
		{
			FEHBFloorFinishHole LocalHole;
			if (ConvertBuildingPolygonToFloorLocal(Building, Floor, BuildingHole.LocalPolygon, LocalHole.LocalPolygon))
			{
				LocalRegion.Holes.Add(MoveTemp(LocalHole));
			}
		}
		return LocalRegion;
	}

	int32 ResolveFloorFinishIndexFromSupportElement(const AEHBElementActorBase* ElementActor, const AEHBBuildingActorBase* Building, const FVector& WorldLocation)
	{
		if (ElementActor)
		{
			if (ElementActor->FloorRole == EEHBBuildingFloorElementRole::Foundation)
			{
				return 1;
			}

			if (ElementActor->FloorRole == EEHBBuildingFloorElementRole::FloorCeiling
				|| ElementActor->FloorRole == EEHBBuildingFloorElementRole::Roof
				|| ElementActor->FloorRole == EEHBBuildingFloorElementRole::FloorBody)
			{
				return FMath::Max(0, ElementActor->FloorIndex) + 1;
			}

			if (ElementActor->FloorIndex > 0)
			{
				return ElementActor->FloorIndex;
			}
		}

		const int32 ResolvedFloor = Building ? Building->ResolveFloorIndexFromWorldLocation(WorldLocation) : INDEX_NONE;
		return ResolvedFloor > 0 ? ResolvedFloor : 1;
	}

	bool BuildFloorRoomPolygonInBuildingSpace(
		AEHB_Floor* Floor,
		FEHBBuildingClosedLoop& OutRoomLoop,
		TArray<FVector>& OutRoomPolygon,
		float& OutSurfaceZ)
	{
		OutRoomLoop = FEHBBuildingClosedLoop();
		OutRoomPolygon.Reset();
		OutSurfaceZ = 0.0f;
		if (!Floor || !Floor->OwningBuilding)
		{
			return false;
		}

		AEHBBuildingActorBase* Building = Floor->OwningBuilding;
		Building->RebuildClosedLoops();

		if (!Floor->TryGetRoomLoop(OutRoomLoop))
		{
			auto TryFindRoomLoop = [&Building, &Floor, &OutRoomLoop](int32 QueryFloorIndex)
			{
				return Building->FindClosedLoopByWorldHit(
					Floor->GetActorLocation(),
					FVector::ZeroVector,
					OutRoomLoop,
					QueryFloorIndex);
			};

			const int32 PrimaryFloorIndex = Floor->FloorIndex > 0 ? Floor->FloorIndex : 1;
			bool bFoundRoomLoop = TryFindRoomLoop(PrimaryFloorIndex);
			// An explicitly assigned upper floor must not fall back into a room on floor 1.
			if (!bFoundRoomLoop && Floor->FloorIndex <= 0)
			{
				bFoundRoomLoop = TryFindRoomLoop(-99);
			}
			if (!bFoundRoomLoop)
			{
				return false;
			}
		}

		if (!BuildClosedLoopPolygonInBuildingSpace(Building, OutRoomLoop, OutRoomPolygon))
		{
			return false;
		}

		OutSurfaceZ = Building->GetActorTransform().InverseTransformPosition(Floor->GetActorLocation()).Z;
		for (FVector& Point : OutRoomPolygon)
		{
			Point.Z = OutSurfaceZ;
		}
		return OutRoomPolygon.Num() >= 3;
	}

	void AddFloorSupportSurfaceFromFloorSlab(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		float TargetSurfaceZ,
		TArray<FEHBFloorSupportSurface>& OutSurfaces)
	{
		if (!Building || !FloorSlab || FloorSlab->LocalTopPolygon.Num() < 3)
		{
			return;
		}

		const FTransform BuildingTransform = Building->GetActorTransform();
		const FTransform SlabTransform = FloorSlab->GetActorTransform();
		const float TopZ = FloorSlab->GetTopZ();
		const FVector TopCenterWorld = SlabTransform.TransformPosition(FVector(0.0f, 0.0f, TopZ));
		const float TopBuildingZ = BuildingTransform.InverseTransformPosition(TopCenterWorld).Z;
		if (FMath::Abs(TopBuildingZ - TargetSurfaceZ) > 1.0f)
		{
			return;
		}

		FEHBFloorSupportSurface Surface;
		Surface.OuterPolygon.Reserve(FloorSlab->LocalTopPolygon.Num());
		for (FVector LocalPoint : FloorSlab->LocalTopPolygon)
		{
			LocalPoint.Z = TopZ;
			FVector BuildingPoint = BuildingTransform.InverseTransformPosition(SlabTransform.TransformPosition(LocalPoint));
			BuildingPoint.Z = TargetSurfaceZ;
			AddUniquePointXY(Surface.OuterPolygon, BuildingPoint, 0.5f);
		}
		if (!CleanAutoFillPolygonXY(Surface.OuterPolygon))
		{
			return;
		}

		for (const TArray<FVector>& HolePolygon : FloorSlab->BuildPreviewHolePolygons())
		{
			FEHBFloorFinishHole Hole;
			for (FVector LocalPoint : HolePolygon)
			{
				LocalPoint.Z = TopZ;
				FVector BuildingPoint = BuildingTransform.InverseTransformPosition(SlabTransform.TransformPosition(LocalPoint));
				BuildingPoint.Z = TargetSurfaceZ;
				AddUniquePointXY(Hole.LocalPolygon, BuildingPoint, 0.5f);
			}
			if (CleanAutoFillPolygonXY(Hole.LocalPolygon))
			{
				Surface.Holes.Add(MoveTemp(Hole));
			}
		}

		OutSurfaces.Add(MoveTemp(Surface));
	}

	void AddFloorSupportSurfaceFromWall(
		const AEHBBuildingActorBase* Building,
		const AEHB_Wall* Wall,
		float TargetSurfaceZ,
		TArray<FEHBFloorSupportSurface>& OutSurfaces)
	{
		if (!Building || !Wall)
		{
			return;
		}

		const FBox Bounds = Wall->GetBuildingLocalBounds();
		if (!Bounds.IsValid || FMath::Abs(Bounds.Max.Z - TargetSurfaceZ) > 1.0f)
		{
			return;
		}

		TArray<FVector> LeftPolyline;
		TArray<FVector> RightPolyline;
		if (!Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 25.0f)
			|| !Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 25.0f)
			|| LeftPolyline.Num() < 2
			|| RightPolyline.Num() < 2)
		{
			return;
		}

		ReversePoints(RightPolyline);
		FEHBFloorSupportSurface Surface;
		Surface.OuterPolygon.Reserve(LeftPolyline.Num() + RightPolyline.Num());
		for (FVector Point : LeftPolyline)
		{
			Point.Z = TargetSurfaceZ;
			AddUniquePointXY(Surface.OuterPolygon, Point, 0.5f);
		}
		for (FVector Point : RightPolyline)
		{
			Point.Z = TargetSurfaceZ;
			AddUniquePointXY(Surface.OuterPolygon, Point, 0.5f);
		}
		if (CleanAutoFillPolygonXY(Surface.OuterPolygon))
		{
			OutSurfaces.Add(MoveTemp(Surface));
		}
	}

	void AddFloorSupportSurfaceFromPillar(
		const AEHBBuildingActorBase* Building,
		const AEHB_Pillar* Pillar,
		float TargetSurfaceZ,
		TArray<FEHBFloorSupportSurface>& OutSurfaces)
	{
		if (!Building || !Pillar)
		{
			return;
		}

		const FBox Bounds = Pillar->GetBuildingLocalBounds();
		if (!Bounds.IsValid || FMath::Abs(Bounds.Max.Z - TargetSurfaceZ) > 1.0f)
		{
			return;
		}

		TArray<FVector> LocalFootprint;
		Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
		if (LocalFootprint.Num() < 3)
		{
			return;
		}

		FEHBFloorSupportSurface Surface;
		Surface.OuterPolygon.Reserve(LocalFootprint.Num());
		const FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
		for (FVector LocalPoint : LocalFootprint)
		{
			LocalPoint.Z = Pillar->Height;
			FVector BuildingPoint = PillarLocalTransform.TransformPosition(LocalPoint);
			BuildingPoint.Z = TargetSurfaceZ;
			AddUniquePointXY(Surface.OuterPolygon, BuildingPoint, 0.5f);
		}

		if (CleanAutoFillPolygonXY(Surface.OuterPolygon))
		{
			OutSurfaces.Add(MoveTemp(Surface));
		}
	}

	void CollectFloorSupportSurfaces(
		AEHBBuildingActorBase* Building,
		float TargetSurfaceZ,
		TArray<FEHBFloorSupportSurface>& OutSurfaces)
	{
		OutSurfaces.Reset();
		if (!Building)
		{
			return;
		}

		FEHBElementQuery Query;
		Query.RequiredCapabilities = static_cast<int32>(EEHBElementCapability::CanSupport);
		for (AEHBElementActorBase* ElementActor : Building->QueryElements(Query))
		{
			if (!ElementActor || ElementActor->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)))
			{
				continue;
			}

			if (const AEHB_FloorSlab* FloorSlab = Cast<AEHB_FloorSlab>(ElementActor))
			{
				AddFloorSupportSurfaceFromFloorSlab(Building, FloorSlab, TargetSurfaceZ, OutSurfaces);
			}
			else if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(ElementActor))
			{
				AddFloorSupportSurfaceFromWall(Building, Wall, TargetSurfaceZ, OutSurfaces);
			}
			else if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(ElementActor))
			{
				AddFloorSupportSurfaceFromPillar(Building, Pillar, TargetSurfaceZ, OutSurfaces);
			}
		}
	}

	FVector GetFloorSlabLocalPolygonCenter(const AEHB_FloorSlab* FloorSlab)
	{
		if (!FloorSlab || FloorSlab->LocalTopPolygon.IsEmpty())
		{
			return FVector::ZeroVector;
		}

		FVector LocalCenter = FVector::ZeroVector;
		for (const FVector& LocalPoint : FloorSlab->LocalTopPolygon)
		{
			LocalCenter += LocalPoint;
		}
		return LocalCenter / static_cast<float>(FloorSlab->LocalTopPolygon.Num());
	}

	bool TransformFloorSlabLocalPointToBuildingSpace(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		const FVector& FloorSlabLocalPoint,
		FVector& OutBuildingLocalPoint)
	{
		if (!Building || !FloorSlab)
		{
			return false;
		}

		OutBuildingLocalPoint = Building->GetActorTransform().InverseTransformPosition(
			FloorSlab->GetActorTransform().TransformPosition(FloorSlabLocalPoint));
		return true;
	}

	bool GetFloorSlabLocalPolygonCenterInBuildingSpace(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		FVector& OutBuildingLocalCenter)
	{
		return TransformFloorSlabLocalPointToBuildingSpace(
			Building,
			FloorSlab,
			GetFloorSlabLocalPolygonCenter(FloorSlab),
			OutBuildingLocalCenter);
	}

	bool BuildFloorSlabProbePointsInBuildingSpace(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutProbePoints)
	{
		OutProbePoints.Reset();
		if (!Building || !FloorSlab)
		{
			return false;
		}

		for (const FVector& LocalPoint : FloorSlab->LocalTopPolygon)
		{
			FVector BuildingLocalPoint = FVector::ZeroVector;
			if (TransformFloorSlabLocalPointToBuildingSpace(Building, FloorSlab, LocalPoint, BuildingLocalPoint))
			{
				OutProbePoints.Add(BuildingLocalPoint);
			}
		}

		if (!FloorSlab->LocalTopPolygon.IsEmpty())
		{
			FVector BuildingLocalCenter = FVector::ZeroVector;
			if (GetFloorSlabLocalPolygonCenterInBuildingSpace(Building, FloorSlab, BuildingLocalCenter))
			{
				OutProbePoints.Add(BuildingLocalCenter);
			}
		}

		return !OutProbePoints.IsEmpty();
	}

	AEHB_Wall* FindWallByGuid(AEHBBuildingActorBase* Building, const FGuid& WallGuid)
	{
		if (!Building || !WallGuid.IsValid())
		{
			return nullptr;
		}

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor);
			if (Wall && Wall->OwningBuilding == Building && Wall->ElementGuid == WallGuid)
			{
				return Wall;
			}
		}

		return nullptr;
	}

	EEHBFloorSlabWallSide ResolveRoomFillAnchorSideFromFloorSlabLocation(
		AEHB_FloorSlab* FloorSlab,
		EEHBFloorSlabWallSide FallbackSide)
	{
		if (!FloorSlab || !FloorSlab->OwningBuilding || !FloorSlab->RoomFillAnchorWallGuid.IsValid())
		{
			return FallbackSide;
		}

		AEHB_Wall* AnchorWall = FindWallByGuid(FloorSlab->OwningBuilding, FloorSlab->RoomFillAnchorWallGuid);
		if (!AnchorWall)
		{
			return FallbackSide;
		}

		FVector SlabBuildingLocalCenter = FVector::ZeroVector;
		if (!GetFloorSlabLocalPolygonCenterInBuildingSpace(FloorSlab->OwningBuilding, FloorSlab, SlabBuildingLocalCenter))
		{
			return FallbackSide;
		}

		TArray<FVector> LeftPolyline;
		TArray<FVector> RightPolyline;
		if (!AnchorWall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 30.0f)
			|| !AnchorWall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 30.0f))
		{
			return FallbackSide;
		}

		const float LeftDistanceSquared = GetDistanceSquaredToPolylineXY(SlabBuildingLocalCenter, LeftPolyline);
		const float RightDistanceSquared = GetDistanceSquaredToPolylineXY(SlabBuildingLocalCenter, RightPolyline);
		if (LeftDistanceSquared == TNumericLimits<float>::Max()
			|| RightDistanceSquared == TNumericLimits<float>::Max())
		{
			return FallbackSide;
		}

		constexpr float SideSwitchTolerance = 4.0f * 4.0f;
		if (LeftDistanceSquared + SideSwitchTolerance < RightDistanceSquared)
		{
			return EEHBFloorSlabWallSide::Left;
		}
		if (RightDistanceSquared + SideSwitchTolerance < LeftDistanceSquared)
		{
			return EEHBFloorSlabWallSide::Right;
		}

		return FallbackSide;
	}

	struct FEHBFloorSlabRoomGraphSegment
	{
		FVector2d A = FVector2d::Zero();
		FVector2d B = FVector2d::Zero();
		bool bAnchorSide = false;
		TArray<double> Splits;
	};

	struct FEHBFloorSlabRoomGraphFace
	{
		TArray<FVector> Polygon;
		TArray<uint64> DirectedEdgeKeys;
		float SignedArea = 0.0f;
		float AbsArea = 0.0f;
	};

	double Cross2D(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	constexpr double RoomGraphNodeMergeTolerance = 0.5;
	constexpr double RoomGraphEndpointSnapTolerance = 8.0;

	uint64 MakeDirectedRoomGraphEdgeKey(int32 FromNodeIndex, int32 ToNodeIndex)
	{
		return (static_cast<uint64>(static_cast<uint32>(FromNodeIndex)) << 32)
			| static_cast<uint64>(static_cast<uint32>(ToNodeIndex));
	}

	uint64 MakeUndirectedRoomGraphEdgeKey(int32 A, int32 B)
	{
		const int32 MinIndex = FMath::Min(A, B);
		const int32 MaxIndex = FMath::Max(A, B);
		return MakeDirectedRoomGraphEdgeKey(MinIndex, MaxIndex);
	}

	int32 GetDirectedRoomGraphEdgeFrom(uint64 EdgeKey)
	{
		return static_cast<int32>(static_cast<uint32>(EdgeKey >> 32));
	}

	int32 GetDirectedRoomGraphEdgeTo(uint64 EdgeKey)
	{
		return static_cast<int32>(static_cast<uint32>(EdgeKey & 0xffffffffu));
	}

	uint64 MakeReversedDirectedRoomGraphEdgeKey(uint64 EdgeKey)
	{
		return MakeDirectedRoomGraphEdgeKey(
			GetDirectedRoomGraphEdgeTo(EdgeKey),
			GetDirectedRoomGraphEdgeFrom(EdgeKey));
	}

	EEHBFloorSlabWallSide GetOppositeRoomFillWallSide(EEHBFloorSlabWallSide Side)
	{
		if (Side == EEHBFloorSlabWallSide::Left)
		{
			return EEHBFloorSlabWallSide::Right;
		}
		if (Side == EEHBFloorSlabWallSide::Right)
		{
			return EEHBFloorSlabWallSide::Left;
		}
		return EEHBFloorSlabWallSide::None;
	}

	EEHBFloorSlabWallSide ResolveLoopSideForWall(
		const FEHBBuildingClosedLoop& Loop,
		const AEHB_Wall* Wall)
	{
		if(Wall&&Wall->OwningBuilding&&Wall->OwningBuilding->WallNodeAuthority.Version==2)
		{
			FEHBNodeRoomBoundary Room;FEHBWallNodeModel Model;
			if(!Wall->OwningBuilding->TryGetRoomBoundary(Loop.LoopGuid,Loop.FloorIndex,Room)||!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(Wall->OwningBuilding,Model).bSucceeded)return EEHBFloorSlabWallSide::None;
			const int32 Index=Room.WallGuids.Find(Wall->ElementGuid);const auto* Definition=Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Wall->ElementGuid;});
			if(Index==INDEX_NONE||!Definition)return EEHBFloorSlabWallSide::None;
			const FGuid A=Room.NodeGuids[Index],Z=Room.NodeGuids[(Index+1)%Room.NodeGuids.Num()];
			if(A==Definition->StartNodeGuid&&Z==Definition->EndNodeGuid)return EEHBFloorSlabWallSide::Left;
			if(Z==Definition->StartNodeGuid&&A==Definition->EndNodeGuid)return EEHBFloorSlabWallSide::Right;
			return EEHBFloorSlabWallSide::None;
		}
		if (!Wall
			|| !Wall->StartPillarGuid.IsValid()
			|| !Wall->EndPillarGuid.IsValid()
			|| Loop.PillarGuids.Num() < 3
			|| Loop.WallGuids.Num() != Loop.PillarGuids.Num())
		{
			return EEHBFloorSlabWallSide::None;
		}

		for (int32 Index = 0; Index < Loop.WallGuids.Num(); ++Index)
		{
			if (Loop.WallGuids[Index] != Wall->ElementGuid)
			{
				continue;
			}

			const FGuid FromPillarGuid = Loop.PillarGuids[Index];
			const FGuid ToPillarGuid = Loop.PillarGuids[(Index + 1) % Loop.PillarGuids.Num()];
			if (FromPillarGuid == Wall->StartPillarGuid && ToPillarGuid == Wall->EndPillarGuid)
			{
				return Loop.bClockwise ? EEHBFloorSlabWallSide::Right : EEHBFloorSlabWallSide::Left;
			}

			if (FromPillarGuid == Wall->EndPillarGuid && ToPillarGuid == Wall->StartPillarGuid)
			{
				return Loop.bClockwise ? EEHBFloorSlabWallSide::Left : EEHBFloorSlabWallSide::Right;
			}
		}

		return EEHBFloorSlabWallSide::None;
	}

	bool BuildClosedLoopPolygonInBuildingSpace(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingClosedLoop& Loop,
		TArray<FVector>& OutBuildingLocalPolygon, const TArray<FEHBWallJunctionWallSides>* PlannedSides)
	{
		OutBuildingLocalPolygon.Reset();
		if (!Building
			|| Loop.PillarGuids.Num() < 3
			|| Loop.WallGuids.Num() != Loop.PillarGuids.Num())
		{
			return false;
		}
		if(Building->WallNodeAuthority.Version==2)
		{
			FEHBWallNodeModel Model;FEHBNodeRoomBoundary Boundary;FName Reason;TArray<FEHBWallJunctionWallSides> Sides;
			if(!Building->TryGetRoomBoundary(Loop.LoopGuid,Loop.FloorIndex,Boundary)||!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(Building,Model).bSucceeded)return false;
			if(PlannedSides)Sides=*PlannedSides;else if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason))return false;
			return BuildLogicalRoomPolygon(Boundary,Model,Sides,OutBuildingLocalPolygon);
		}

		const bool bPlan=PlannedSides!=nullptr;
		OutBuildingLocalPolygon.Reserve(Loop.PillarGuids.Num() * 2);
		for (int32 Index = 0; Index < Loop.WallGuids.Num(); ++Index)
		{
			bool bSameDirection = true;
			AEHB_Wall* Wall = Building->FindWallBetweenPillars(
				Loop.PillarGuids[Index],
				Loop.PillarGuids[(Index + 1) % Loop.PillarGuids.Num()],
				bSameDirection);
			if (!Wall || Wall->ElementGuid != Loop.WallGuids[Index])
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			const EEHBFloorSlabWallSide RoomSide = ResolveLoopSideForWall(Loop, Wall);
			if (RoomSide == EEHBFloorSlabWallSide::None)
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			TArray<FVector> SidePolyline;
			if(bPlan)
			{
				const auto* W=PlannedSides->FindByPredicate([&](const auto& Candidate){return Candidate.WallGuid==Wall->ElementGuid;});
				if(!W)return false;
				SidePolyline=RoomSide==EEHBFloorSlabWallSide::Left?TArray<FVector>{W->StartLeft,W->EndLeft}:TArray<FVector>{W->StartRight,W->EndRight};
			}
			else if (!Wall->BuildSideTopPolylineInBuildingSpace(RoomSide == EEHBFloorSlabWallSide::Left, SidePolyline, 25.0f)
				|| SidePolyline.Num() < 2)
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			if (!bSameDirection)
			{
				ReversePoints(SidePolyline);
			}

			for (FVector Point : SidePolyline)
			{
				Point.Z = 0.0f;
				AddUniquePointXY(OutBuildingLocalPolygon, Point, 0.5f);
			}
		}

		return CleanAutoFillPolygonXY(OutBuildingLocalPolygon);
	}

	void AddRoomGraphSplit(FEHBFloorSlabRoomGraphSegment& Segment, double Alpha)
	{
		const double ClampedAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
		for (const double ExistingAlpha : Segment.Splits)
		{
			if (FMath::Abs(ExistingAlpha - ClampedAlpha) <= 0.0001)
			{
				return;
			}
		}
		Segment.Splits.Add(ClampedAlpha);
	}

	void SplitRoomGraphSegmentsAtIntersection(FEHBFloorSlabRoomGraphSegment& First, FEHBFloorSlabRoomGraphSegment& Second)
	{
		const FVector2d P = First.A;
		const FVector2d R = First.B - First.A;
		const FVector2d Q = Second.A;
		const FVector2d S = Second.B - Second.A;
		const double RLengthSquared = R.SquaredLength();
		const double SLengthSquared = S.SquaredLength();
		if (RLengthSquared <= UE_DOUBLE_SMALL_NUMBER || SLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}

		auto AddEndpointProjectionIfClose = [](
			const FVector2d& Point,
			const FVector2d& SegmentStart,
			const FVector2d& SegmentDirection,
			double SegmentLengthSquared,
			FEHBFloorSlabRoomGraphSegment& Segment)
		{
			const double Alpha = FVector2d::DotProduct(Point - SegmentStart, SegmentDirection) / SegmentLengthSquared;
			if (Alpha < -0.001 || Alpha > 1.001)
			{
				return;
			}

			const FVector2d ProjectedPoint = SegmentStart + SegmentDirection * FMath::Clamp(Alpha, 0.0, 1.0);
			if ((ProjectedPoint - Point).SquaredLength() <= RoomGraphEndpointSnapTolerance * RoomGraphEndpointSnapTolerance)
			{
				AddRoomGraphSplit(Segment, Alpha);
			}
		};

		AddEndpointProjectionIfClose(First.A, Second.A, S, SLengthSquared, Second);
		AddEndpointProjectionIfClose(First.B, Second.A, S, SLengthSquared, Second);
		AddEndpointProjectionIfClose(Second.A, First.A, R, RLengthSquared, First);
		AddEndpointProjectionIfClose(Second.B, First.A, R, RLengthSquared, First);

		const double Denominator = Cross2D(R, S);
		const FVector2d QMinusP = Q - P;
		constexpr double IntersectionTolerance = 0.01;
		if (FMath::Abs(Denominator) > UE_DOUBLE_SMALL_NUMBER)
		{
			const double T = Cross2D(QMinusP, S) / Denominator;
			const double U = Cross2D(QMinusP, R) / Denominator;
			if (T >= -IntersectionTolerance && T <= 1.0 + IntersectionTolerance
				&& U >= -IntersectionTolerance && U <= 1.0 + IntersectionTolerance)
			{
				AddRoomGraphSplit(First, T);
				AddRoomGraphSplit(Second, U);
			}
			return;
		}

		if (FMath::Abs(Cross2D(QMinusP, R)) > IntersectionTolerance)
		{
			return;
		}

		auto AddPointIfOnSegment = [](const FVector2d& Point, const FVector2d& SegmentStart, const FVector2d& SegmentDirection, double SegmentLengthSquared, FEHBFloorSlabRoomGraphSegment& Segment)
		{
			const double Alpha = FVector2d::DotProduct(Point - SegmentStart, SegmentDirection) / SegmentLengthSquared;
			if (Alpha >= -0.001 && Alpha <= 1.001)
			{
				AddRoomGraphSplit(Segment, Alpha);
			}
		};

		AddPointIfOnSegment(Second.A, First.A, R, RLengthSquared, First);
		AddPointIfOnSegment(Second.B, First.A, R, RLengthSquared, First);
		AddPointIfOnSegment(First.A, Second.A, S, SLengthSquared, Second);
		AddPointIfOnSegment(First.B, Second.A, S, SLengthSquared, Second);
	}

	void AddRoomGraphEndpointSnapConnectors(TArray<FEHBFloorSlabRoomGraphSegment>& Segments)
	{
		TArray<FEHBFloorSlabRoomGraphSegment> Connectors;
		const int32 OriginalSegmentCount = Segments.Num();

		auto AddConnector = [&Connectors](const FVector2d& A, const FVector2d& B)
		{
			if ((B - A).SquaredLength() <= RoomGraphNodeMergeTolerance * RoomGraphNodeMergeTolerance)
			{
				return;
			}

			FEHBFloorSlabRoomGraphSegment Connector;
			Connector.A = A;
			Connector.B = B;
			Connector.bAnchorSide = false;
			Connector.Splits = { 0.0, 1.0 };
			Connectors.Add(MoveTemp(Connector));
		};

		auto SnapPointToSegment = [&AddConnector](
			const FVector2d& Point,
			const FVector2d& SegmentStart,
			const FVector2d& SegmentDirection,
			double SegmentLengthSquared,
			FEHBFloorSlabRoomGraphSegment& Segment)
		{
			if (SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
			{
				return;
			}

			const double Alpha = FVector2d::DotProduct(Point - SegmentStart, SegmentDirection) / SegmentLengthSquared;
			if (Alpha < -0.001 || Alpha > 1.001)
			{
				return;
			}

			const double ClampedAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
			const FVector2d ProjectedPoint = SegmentStart + SegmentDirection * ClampedAlpha;
			if ((ProjectedPoint - Point).SquaredLength() > RoomGraphEndpointSnapTolerance * RoomGraphEndpointSnapTolerance)
			{
				return;
			}

			AddRoomGraphSplit(Segment, ClampedAlpha);
			AddConnector(Point, ProjectedPoint);
		};

		for (int32 FirstIndex = 0; FirstIndex < OriginalSegmentCount; ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < OriginalSegmentCount; ++SecondIndex)
			{
				FEHBFloorSlabRoomGraphSegment& First = Segments[FirstIndex];
				FEHBFloorSlabRoomGraphSegment& Second = Segments[SecondIndex];
				const FVector2d FirstDirection = First.B - First.A;
				const FVector2d SecondDirection = Second.B - Second.A;
				const double FirstLengthSquared = FirstDirection.SquaredLength();
				const double SecondLengthSquared = SecondDirection.SquaredLength();

				SnapPointToSegment(First.A, Second.A, SecondDirection, SecondLengthSquared, Second);
				SnapPointToSegment(First.B, Second.A, SecondDirection, SecondLengthSquared, Second);
				SnapPointToSegment(Second.A, First.A, FirstDirection, FirstLengthSquared, First);
				SnapPointToSegment(Second.B, First.A, FirstDirection, FirstLengthSquared, First);
			}
		}

		Segments.Append(MoveTemp(Connectors));
	}

	bool BuildRoomGraphSegmentsForBuilding(
		AEHBBuildingActorBase* Building,
		const FGuid& AnchorWallGuid,
		EEHBFloorSlabWallSide AnchorWallSide,
		const FVector& SlabBuildingLocalCenter,
		FVector& OutAnchorInteriorSample,
		bool& bOutHasAnchorInteriorSample,
		TArray<FEHBFloorSlabRoomGraphSegment>& OutSegments)
	{
		OutSegments.Reset();
		OutAnchorInteriorSample = FVector::ZeroVector;
		bOutHasAnchorInteriorSample = false;
		if (!Building)
		{
			return false;
		}

		double BestAnchorSampleDistanceSquared = TNumericLimits<double>::Max();
		auto AddPolylineSegments = [
			&OutSegments,
			&SlabBuildingLocalCenter,
			&OutAnchorInteriorSample,
			&bOutHasAnchorInteriorSample,
			&BestAnchorSampleDistanceSquared](const TArray<FVector>& Polyline, bool bAnchorSide)
		{
			for (int32 PointIndex = 0; PointIndex + 1 < Polyline.Num(); ++PointIndex)
			{
				const FVector& A = Polyline[PointIndex];
				const FVector& B = Polyline[PointIndex + 1];
				if (FVector::DistSquared2D(A, B) <= 1.0)
				{
					continue;
				}

				FEHBFloorSlabRoomGraphSegment Segment;
				Segment.A = FVector2d(A.X, A.Y);
				Segment.B = FVector2d(B.X, B.Y);
				Segment.bAnchorSide = bAnchorSide;
				Segment.Splits = { 0.0, 1.0 };
				OutSegments.Add(MoveTemp(Segment));

				if (bAnchorSide)
				{
					const FVector SegmentDirection = (B - A).GetSafeNormal2D();
					if (!SegmentDirection.IsNearlyZero())
					{
						const FVector InteriorNormal(-SegmentDirection.Y, SegmentDirection.X, 0.0f);
						const FVector SamplePoint = (A + B) * 0.5f + InteriorNormal * 25.0f;
						const double SampleDistanceSquared = FVector::DistSquared2D(SamplePoint, SlabBuildingLocalCenter);
						if (!bOutHasAnchorInteriorSample || SampleDistanceSquared < BestAnchorSampleDistanceSquared)
						{
							BestAnchorSampleDistanceSquared = SampleDistanceSquared;
							OutAnchorInteriorSample = SamplePoint;
							bOutHasAnchorInteriorSample = true;
						}
					}
				}
			}
		};

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			if (AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
			{
				if (Wall->OwningBuilding != Building || !Wall->ElementGuid.IsValid())
				{
					continue;
				}

				TArray<FVector> LeftPolyline;
				if (Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 30.0f))
				{
					AddPolylineSegments(
						LeftPolyline,
						Wall->ElementGuid == AnchorWallGuid && AnchorWallSide == EEHBFloorSlabWallSide::Left);
				}

				TArray<FVector> RightPolyline;
				if (Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 30.0f))
				{
					ReversePoints(RightPolyline);
					AddPolylineSegments(
						RightPolyline,
						Wall->ElementGuid == AnchorWallGuid && AnchorWallSide == EEHBFloorSlabWallSide::Right);
				}
				continue;
			}

			if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
			{
				if (Pillar->OwningBuilding != Building)
				{
					continue;
				}

				TArray<FVector> LocalFootprint;
				Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
				if (LocalFootprint.Num() < 3)
				{
					continue;
				}

				TArray<FVector> BuildingLocalFootprint;
				BuildingLocalFootprint.Reserve(LocalFootprint.Num() + 1);
				const FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
				for (FVector LocalPoint : LocalFootprint)
				{
					LocalPoint.Z = 0.0f;
					BuildingLocalFootprint.Add(PillarLocalTransform.TransformPosition(LocalPoint));
				}
				const FVector FirstBuildingLocalFootprintPoint = BuildingLocalFootprint[0];
				BuildingLocalFootprint.Add(FirstBuildingLocalFootprintPoint);
				AddPolylineSegments(BuildingLocalFootprint, false);
			}
		}

		return OutSegments.Num() >= 3;
	}

	int32 FindOrAddRoomGraphNode(
		const FVector2d& Point,
		TArray<FVector2d>& Nodes,
		TMap<FIntPoint, int32>& NodeIndexByKey)
	{
		const FIntPoint Key(
			FMath::RoundToInt(Point.X / RoomGraphNodeMergeTolerance),
			FMath::RoundToInt(Point.Y / RoomGraphNodeMergeTolerance));
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				const FIntPoint NearbyKey(Key.X + OffsetX, Key.Y + OffsetY);
				const int32* ExistingIndex = NodeIndexByKey.Find(NearbyKey);
				if (ExistingIndex
					&& Nodes.IsValidIndex(*ExistingIndex)
					&& (Nodes[*ExistingIndex] - Point).SquaredLength() <= RoomGraphNodeMergeTolerance * RoomGraphNodeMergeTolerance)
				{
					return *ExistingIndex;
				}
			}
		}

		const int32 NewIndex = Nodes.Num();
		Nodes.Add(Point);
		NodeIndexByKey.Add(Key, NewIndex);
		return NewIndex;
	}

	bool BuildRoomPlanarGraph(
		TArray<FEHBFloorSlabRoomGraphSegment>& Segments,
		TArray<FVector2d>& OutNodes,
		TMap<int32, TArray<int32>>& OutAdjacency,
		TSet<uint64>& OutAnchorDirectedEdges)
	{
		OutNodes.Reset();
		OutAdjacency.Reset();
		OutAnchorDirectedEdges.Reset();
		AddRoomGraphEndpointSnapConnectors(Segments);
		for (int32 FirstIndex = 0; FirstIndex < Segments.Num(); ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Segments.Num(); ++SecondIndex)
			{
				SplitRoomGraphSegmentsAtIntersection(Segments[FirstIndex], Segments[SecondIndex]);
			}
		}

		TMap<FIntPoint, int32> NodeIndexByKey;
		TSet<uint64> AddedUndirectedEdges;
		for (FEHBFloorSlabRoomGraphSegment& Segment : Segments)
		{
			Segment.Splits.Sort();
			for (int32 SplitIndex = 0; SplitIndex + 1 < Segment.Splits.Num(); ++SplitIndex)
			{
				const double StartAlpha = Segment.Splits[SplitIndex];
				const double EndAlpha = Segment.Splits[SplitIndex + 1];
				if (EndAlpha - StartAlpha <= 0.0001)
				{
					continue;
				}

				const FVector2d A = FMath::Lerp(Segment.A, Segment.B, StartAlpha);
				const FVector2d B = FMath::Lerp(Segment.A, Segment.B, EndAlpha);
				if ((B - A).SquaredLength() <= 1.0)
				{
					continue;
				}

				const int32 AIndex = FindOrAddRoomGraphNode(A, OutNodes, NodeIndexByKey);
				const int32 BIndex = FindOrAddRoomGraphNode(B, OutNodes, NodeIndexByKey);
				if (AIndex == BIndex)
				{
					continue;
				}

				const uint64 EdgeKey = MakeUndirectedRoomGraphEdgeKey(AIndex, BIndex);
				if (!AddedUndirectedEdges.Contains(EdgeKey))
				{
					AddedUndirectedEdges.Add(EdgeKey);
					OutAdjacency.FindOrAdd(AIndex).Add(BIndex);
					OutAdjacency.FindOrAdd(BIndex).Add(AIndex);
				}

				if (Segment.bAnchorSide)
				{
					OutAnchorDirectedEdges.Add(MakeDirectedRoomGraphEdgeKey(AIndex, BIndex));
				}
			}
		}

		return !OutNodes.IsEmpty() && !OutAdjacency.IsEmpty();
	}

	int32 ChooseNextRoomGraphNode(int32 PreviousNode, int32 CurrentNode, const TArray<FVector2d>& Nodes, const TArray<int32>& CandidateNodes)
	{
		if (!Nodes.IsValidIndex(PreviousNode) || !Nodes.IsValidIndex(CurrentNode))
		{
			return INDEX_NONE;
		}

		const FVector CurrentDirection(Nodes[CurrentNode].X - Nodes[PreviousNode].X, Nodes[CurrentNode].Y - Nodes[PreviousNode].Y, 0.0f);
		int32 BestNode = INDEX_NONE;
		double BestAngle = TNumericLimits<double>::Max();
		for (const int32 CandidateNode : CandidateNodes)
		{
			if (!Nodes.IsValidIndex(CandidateNode) || CandidateNode == CurrentNode)
			{
				continue;
			}

			const FVector CandidateDirection(Nodes[CandidateNode].X - Nodes[CurrentNode].X, Nodes[CandidateNode].Y - Nodes[CurrentNode].Y, 0.0f);
			const double Angle = GetCounterClockwiseTurnAngle(CurrentDirection, CandidateDirection);
			if (Angle < BestAngle)
			{
				BestAngle = Angle;
				BestNode = CandidateNode;
			}
		}

		return BestNode;
	}

	bool TraceRoomGraphFace(
		int32 StartNode,
		int32 NextNode,
		const TArray<FVector2d>& Nodes,
		const TMap<int32, TArray<int32>>& Adjacency,
		FEHBFloorSlabRoomGraphFace& OutFace)
	{
		OutFace = FEHBFloorSlabRoomGraphFace();
		if (!Nodes.IsValidIndex(StartNode) || !Nodes.IsValidIndex(NextNode))
		{
			return false;
		}

		int32 PreviousNode = StartNode;
		int32 CurrentNode = NextNode;
		OutFace.Polygon.Add(FVector(Nodes[StartNode].X, Nodes[StartNode].Y, 0.0f));
		const int32 MaxStepCount = FMath::Max(16, Adjacency.Num() * 4);
		for (int32 StepIndex = 0; StepIndex < MaxStepCount; ++StepIndex)
		{
			OutFace.DirectedEdgeKeys.Add(MakeDirectedRoomGraphEdgeKey(PreviousNode, CurrentNode));
			if (CurrentNode == StartNode && PreviousNode == NextNode)
			{
				break;
			}

			if (CurrentNode == StartNode)
			{
				CleanPolygonXY(OutFace.Polygon);
				OutFace.SignedArea = CalculateSignedAreaXY(OutFace.Polygon);
				OutFace.AbsArea = FMath::Abs(OutFace.SignedArea);
				return OutFace.Polygon.Num() >= 3 && OutFace.AbsArea > 4.0f;
			}

			OutFace.Polygon.Add(FVector(Nodes[CurrentNode].X, Nodes[CurrentNode].Y, 0.0f));
			const TArray<int32>* CandidateNodes = Adjacency.Find(CurrentNode);
			if (!CandidateNodes || CandidateNodes->IsEmpty())
			{
				return false;
			}

			const int32 ChosenNode = ChooseNextRoomGraphNode(PreviousNode, CurrentNode, Nodes, *CandidateNodes);
			if (ChosenNode == INDEX_NONE)
			{
				return false;
			}

			PreviousNode = CurrentNode;
			CurrentNode = ChosenNode;
		}

		return false;
	}

	void BuildRoomGraphFaces(
		const TArray<FVector2d>& Nodes,
		const TMap<int32, TArray<int32>>& Adjacency,
		TArray<FEHBFloorSlabRoomGraphFace>& OutFaces,
		TMap<uint64, int32>& OutFaceIndexByDirectedEdge)
	{
		OutFaces.Reset();
		OutFaceIndexByDirectedEdge.Reset();
		for (const TPair<int32, TArray<int32>>& Pair : Adjacency)
		{
			const int32 FromNode = Pair.Key;
			for (const int32 ToNode : Pair.Value)
			{
				const uint64 StartEdgeKey = MakeDirectedRoomGraphEdgeKey(FromNode, ToNode);
				if (OutFaceIndexByDirectedEdge.Contains(StartEdgeKey))
				{
					continue;
				}

				FEHBFloorSlabRoomGraphFace Face;
				if (!TraceRoomGraphFace(FromNode, ToNode, Nodes, Adjacency, Face))
				{
					OutFaceIndexByDirectedEdge.Add(StartEdgeKey, INDEX_NONE);
					continue;
				}

				const int32 FaceIndex = OutFaces.Num();
				for (const uint64 DirectedEdgeKey : Face.DirectedEdgeKeys)
				{
					OutFaceIndexByDirectedEdge.Add(DirectedEdgeKey, FaceIndex);
				}
				OutFaces.Add(MoveTemp(Face));
			}
		}
	}

	void AddClosedRoomGraphLoopSegments(
		const TArray<FVector>& Loop,
		TArray<FEHBFloorSlabRoomGraphSegment>& OutSegments)
	{
		if (Loop.Num() < 3)
		{
			return;
		}

		for (int32 PointIndex = 0; PointIndex < Loop.Num(); ++PointIndex)
		{
			const FVector& A = Loop[PointIndex];
			const FVector& B = Loop[(PointIndex + 1) % Loop.Num()];
			if (FVector::DistSquared2D(A, B) <= 1.0)
			{
				continue;
			}

			FEHBFloorSlabRoomGraphSegment Segment;
			Segment.A = FVector2d(A.X, A.Y);
			Segment.B = FVector2d(B.X, B.Y);
			Segment.bAnchorSide = false;
			Segment.Splits = { 0.0, 1.0 };
			OutSegments.Add(MoveTemp(Segment));
		}
	}

	bool BuildRoomObstacleSegmentsForBuilding(
		AEHBBuildingActorBase* Building,
		TArray<FEHBFloorSlabRoomGraphSegment>& OutSegments)
	{
		OutSegments.Reset();
		if (!Building)
		{
			return false;
		}

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			if (AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
			{
				if (Wall->OwningBuilding != Building || !Wall->ElementGuid.IsValid())
				{
					continue;
				}

				TArray<FVector> LeftPolyline;
				TArray<FVector> RightPolyline;
				if (!Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 25.0f)
					|| !Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 25.0f)
					|| LeftPolyline.Num() < 2
					|| RightPolyline.Num() < 2)
				{
					continue;
				}

				ReversePoints(RightPolyline);
				TArray<FVector> WallObstacleLoop;
				WallObstacleLoop.Reserve(LeftPolyline.Num() + RightPolyline.Num());
				for (FVector Point : LeftPolyline)
				{
					Point.Z = 0.0f;
					AddUniquePointXY(WallObstacleLoop, Point, 0.5f);
				}
				for (FVector Point : RightPolyline)
				{
					Point.Z = 0.0f;
					AddUniquePointXY(WallObstacleLoop, Point, 0.5f);
				}

				CleanPolygonXY(WallObstacleLoop);
				if (WallObstacleLoop.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(WallObstacleLoop)) > 1.0f)
				{
					AddClosedRoomGraphLoopSegments(WallObstacleLoop, OutSegments);
				}
				continue;
			}

			if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
			{
				if (Pillar->OwningBuilding != Building)
				{
					continue;
				}

				TArray<FVector> LocalFootprint;
				Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
				if (LocalFootprint.Num() < 3)
				{
					continue;
				}

				TArray<FVector> BuildingLocalFootprint;
				BuildingLocalFootprint.Reserve(LocalFootprint.Num());
				const FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
				for (FVector LocalPoint : LocalFootprint)
				{
					LocalPoint.Z = 0.0f;
					FVector BuildingLocalPoint = PillarLocalTransform.TransformPosition(LocalPoint);
					BuildingLocalPoint.Z = 0.0f;
					AddUniquePointXY(BuildingLocalFootprint, BuildingLocalPoint, 0.5f);
				}

				CleanPolygonXY(BuildingLocalFootprint);
				if (BuildingLocalFootprint.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(BuildingLocalFootprint)) > 1.0f)
				{
					AddClosedRoomGraphLoopSegments(BuildingLocalFootprint, OutSegments);
				}
			}
		}

		return OutSegments.Num() >= 3;
	}

	bool RoomCycleUsesWallSide(
		const FEHBFloorSlabRoomCycle& Cycle,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const FGuid& WallGuid,
		EEHBFloorSlabWallSide WallSide)
	{
		if (!WallGuid.IsValid() || WallSide == EEHBFloorSlabWallSide::None)
		{
			return false;
		}

		for (const int32 HalfEdgeIndex : Cycle.HalfEdgeIndices)
		{
			if (!HalfEdges.IsValidIndex(HalfEdgeIndex))
			{
				continue;
			}

			const FEHBFloorSlabRoomHalfEdge& HalfEdge = HalfEdges[HalfEdgeIndex];
			if (HalfEdge.WallGuid == WallGuid && HalfEdge.WallSide == WallSide)
			{
				return true;
			}
		}

		return false;
	}

	bool RoomCycleUsesWall(
		const FEHBFloorSlabRoomCycle& Cycle,
		const TArray<FEHBFloorSlabRoomHalfEdge>& HalfEdges,
		const FGuid& WallGuid)
	{
		if (!WallGuid.IsValid())
		{
			return false;
		}

		for (const int32 HalfEdgeIndex : Cycle.HalfEdgeIndices)
		{
			if (HalfEdges.IsValidIndex(HalfEdgeIndex)
				&& HalfEdges[HalfEdgeIndex].WallGuid == WallGuid)
			{
				return true;
			}
		}

		return false;
	}

	bool DoesPolygonContainAnyPointXY(const TArray<FVector>& Polygon, const TArray<FVector>& Points, float BoundaryTolerance = 4.0f)
	{
		for (const FVector& Point : Points)
		{
			if (IsPointInsideOrNearPolygonXY(Point, Polygon, BoundaryTolerance))
			{
				return true;
			}
		}

		return false;
	}

	bool BuildTopologyRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles)
	{
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		if (!FloorSlab
			|| !FloorSlab->OwningBuilding
			|| !FloorSlab->bHasRoomFillAnchor
			|| !FloorSlab->RoomFillAnchorWallGuid.IsValid())
		{
			return false;
		}

		AEHBBuildingActorBase* Building = FloorSlab->OwningBuilding;
		FVector SlabBuildingLocalCenter = FVector::ZeroVector;
		if (!GetFloorSlabLocalPolygonCenterInBuildingSpace(Building, FloorSlab, SlabBuildingLocalCenter))
		{
			return false;
		}

		TArray<FEHBFloorSlabRoomHalfEdge> HalfEdges;
		TMultiMap<FGuid, int32> OutgoingByPillar;
		if (!BuildRoomHalfEdges(Building, HalfEdges, OutgoingByPillar))
		{
			UE_LOG(LogTemp, Warning, TEXT("EHB floor slab topology room fill failed: no wall-pillar half edges were built."));
			return false;
		}

		TArray<FEHBFloorSlabRoomCycle> Cycles;
		BuildAllRoomCycles(Building, HalfEdges, OutgoingByPillar, Cycles);
		if (Cycles.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("EHB floor slab topology room fill failed: no closed wall-pillar cycles were found. HalfEdges=%d"), HalfEdges.Num());
			return false;
		}

		TArray<FVector> FloorSlabProbePoints;
		BuildFloorSlabProbePointsInBuildingSpace(Building, FloorSlab, FloorSlabProbePoints);

		const EEHBFloorSlabWallSide AnchorWallSide = ResolveRoomFillAnchorSideFromFloorSlabLocation(
			FloorSlab,
			FloorSlab->RoomFillAnchorWallSide);

		int32 BestCycleIndex = INDEX_NONE;
		int32 BestTier = TNumericLimits<int32>::Max();
		float BestArea = TNumericLimits<float>::Max();
		for (int32 CycleIndex = 0; CycleIndex < Cycles.Num(); ++CycleIndex)
		{
			const FEHBFloorSlabRoomCycle& Cycle = Cycles[CycleIndex];
			if (Cycle.SignedArea <= 4.0f || Cycle.AbsArea <= 4.0f)
			{
				continue;
			}

			const bool bContainsCenter = IsPointInsideOrNearPolygonXY(SlabBuildingLocalCenter, Cycle.BuildingLocalPolygon, 4.0f);
			const bool bContainsProbe = bContainsCenter || DoesPolygonContainAnyPointXY(Cycle.BuildingLocalPolygon, FloorSlabProbePoints, 4.0f);
			if (!bContainsProbe)
			{
				continue;
			}

			const bool bUsesAnchorSide = RoomCycleUsesWallSide(
				Cycle,
				HalfEdges,
				FloorSlab->RoomFillAnchorWallGuid,
				AnchorWallSide);
			const bool bUsesAnchorWall = bUsesAnchorSide
				|| RoomCycleUsesWall(Cycle, HalfEdges, FloorSlab->RoomFillAnchorWallGuid);

			const int32 Tier = bContainsCenter
				? 0
				: (bUsesAnchorSide ? 1 : (bUsesAnchorWall ? 2 : 3));
			if (Tier < BestTier || (Tier == BestTier && Cycle.AbsArea < BestArea))
			{
				BestCycleIndex = CycleIndex;
				BestTier = Tier;
				BestArea = Cycle.AbsArea;
			}
		}

		if (!Cycles.IsValidIndex(BestCycleIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("EHB floor slab topology room fill failed: %d cycles found, but none contains the selected slab."), Cycles.Num());
			return false;
		}

		const FEHBFloorSlabRoomCycle& TargetCycle = Cycles[BestCycleIndex];
		if (!ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, TargetCycle.BuildingLocalPolygon, OutLocalOuterPolygon))
		{
			return false;
		}

		struct FTopologyHoleCandidate
		{
			int32 CycleIndex = INDEX_NONE;
			float Area = 0.0f;
			FVector Centroid = FVector::ZeroVector;
		};

		TArray<FTopologyHoleCandidate> HoleCandidates;
		for (int32 CycleIndex = 0; CycleIndex < Cycles.Num(); ++CycleIndex)
		{
			if (CycleIndex == BestCycleIndex)
			{
				continue;
			}

			const FEHBFloorSlabRoomCycle& Candidate = Cycles[CycleIndex];
			if (Candidate.SignedArea <= 4.0f
				|| Candidate.AbsArea <= 4.0f
				|| Candidate.AbsArea >= TargetCycle.AbsArea - 1.0f)
			{
				continue;
			}

			const FVector CandidateCentroid = GetPolygonCentroidXY(Candidate.BuildingLocalPolygon);
			if (!IsPointInsideOrNearPolygonXY(CandidateCentroid, TargetCycle.BuildingLocalPolygon, 2.0f))
			{
				continue;
			}

			if (IsPointInsideOrNearPolygonXY(SlabBuildingLocalCenter, Candidate.BuildingLocalPolygon, 2.0f))
			{
				continue;
			}

			FTopologyHoleCandidate& HoleCandidate = HoleCandidates.AddDefaulted_GetRef();
			HoleCandidate.CycleIndex = CycleIndex;
			HoleCandidate.Area = Candidate.AbsArea;
			HoleCandidate.Centroid = CandidateCentroid;
		}

		for (const FTopologyHoleCandidate& Candidate : HoleCandidates)
		{
			if (!Cycles.IsValidIndex(Candidate.CycleIndex))
			{
				continue;
			}

			bool bNestedInsideAnotherHole = false;
			for (const FTopologyHoleCandidate& OtherCandidate : HoleCandidates)
			{
				if (OtherCandidate.CycleIndex == Candidate.CycleIndex
					|| OtherCandidate.Area <= Candidate.Area
					|| !Cycles.IsValidIndex(OtherCandidate.CycleIndex))
				{
					continue;
				}

				if (IsPointInsideOrNearPolygonXY(
					Candidate.Centroid,
					Cycles[OtherCandidate.CycleIndex].BuildingLocalPolygon,
					2.0f))
				{
					bNestedInsideAnotherHole = true;
					break;
				}
			}

			if (bNestedInsideAnotherHole)
			{
				continue;
			}

			FEHBFloorSlabHole Hole;
			if (ConvertBuildingPolygonToFloorSlabLocal(
				Building,
				FloorSlab,
				Cycles[Candidate.CycleIndex].BuildingLocalPolygon,
				Hole.LocalPolygon))
			{
				OutLocalHoles.Add(MoveTemp(Hole));
			}
		}

		return OutLocalOuterPolygon.Num() >= 3;
	}

	bool BuildBuildingClosedLoopRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles,
		bool& bAnchorSideHasKnownNoRoom)
	{
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		bAnchorSideHasKnownNoRoom = false;
		if (!FloorSlab
			|| !FloorSlab->OwningBuilding
			|| !FloorSlab->bHasRoomFillAnchor
			|| !FloorSlab->RoomFillAnchorWallGuid.IsValid())
		{
			return false;
		}

		AEHBBuildingActorBase* Building = FloorSlab->OwningBuilding;
		AEHB_Wall* AnchorWall = FindWallByGuid(Building, FloorSlab->RoomFillAnchorWallGuid);
		if (!AnchorWall)
		{
			return false;
		}

		const EEHBFloorSlabWallSide AnchorWallSide = ResolveRoomFillAnchorSideFromFloorSlabLocation(
			FloorSlab,
			FloorSlab->RoomFillAnchorWallSide);
		if (AnchorWallSide == EEHBFloorSlabWallSide::None)
		{
			return false;
		}

		Building->RebuildClosedLoops();
		const TArray<FEHBBuildingClosedLoop> AnchorWallLoops =
			Building->GetClosedLoopsByWallGuid(FloorSlab->RoomFillAnchorWallGuid);
		if (AnchorWallLoops.IsEmpty())
		{
			return false;
		}

		int32 BestLoopIndex = INDEX_NONE;
		float BestArea = TNumericLimits<float>::Max();
		for (int32 LoopIndex = 0; LoopIndex < AnchorWallLoops.Num(); ++LoopIndex)
		{
			const FEHBBuildingClosedLoop& Loop = AnchorWallLoops[LoopIndex];
			if (ResolveLoopSideForWall(Loop, AnchorWall) != AnchorWallSide)
			{
				continue;
			}

			if (Loop.Area > 1.0f && Loop.Area < BestArea)
			{
				BestLoopIndex = LoopIndex;
				BestArea = Loop.Area;
			}
		}

		if (!AnchorWallLoops.IsValidIndex(BestLoopIndex))
		{
			bAnchorSideHasKnownNoRoom = true;
			return false;
		}

		const FEHBBuildingClosedLoop& TargetLoop = AnchorWallLoops[BestLoopIndex];
		TArray<FVector> TargetBuildingPolygon;
		if (!BuildClosedLoopPolygonInBuildingSpace(Building, TargetLoop, TargetBuildingPolygon)
			|| !ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, TargetBuildingPolygon, OutLocalOuterPolygon))
		{
			return false;
		}

		struct FClosedLoopHoleCandidate
		{
			FEHBBuildingClosedLoop Loop;
			TArray<FVector> BuildingPolygon;
			FVector Centroid = FVector::ZeroVector;
		};

		TArray<FClosedLoopHoleCandidate> HoleCandidates;
		for (const FEHBBuildingClosedLoop& CandidateLoop : Building->ClosedLoops)
		{
			if (CandidateLoop.LoopGuid == TargetLoop.LoopGuid
				|| CandidateLoop.Area <= 1.0f
				|| CandidateLoop.Area >= TargetLoop.Area - 1.0f)
			{
				continue;
			}

			TArray<FVector> CandidatePolygon;
			if (!BuildClosedLoopPolygonInBuildingSpace(Building, CandidateLoop, CandidatePolygon))
			{
				continue;
			}

			const FVector CandidateCentroid = GetPolygonCentroidXY(CandidatePolygon);
			if (!IsPointInsideOrNearPolygonXY(CandidateCentroid, TargetBuildingPolygon, 2.0f))
			{
				continue;
			}

			FClosedLoopHoleCandidate& HoleCandidate = HoleCandidates.AddDefaulted_GetRef();
			HoleCandidate.Loop = CandidateLoop;
			HoleCandidate.BuildingPolygon = MoveTemp(CandidatePolygon);
			HoleCandidate.Centroid = CandidateCentroid;
		}

		for (const FClosedLoopHoleCandidate& Candidate : HoleCandidates)
		{
			bool bNestedInsideAnotherHole = false;
			for (const FClosedLoopHoleCandidate& OtherCandidate : HoleCandidates)
			{
				if (OtherCandidate.Loop.LoopGuid == Candidate.Loop.LoopGuid
					|| OtherCandidate.Loop.Area <= Candidate.Loop.Area)
				{
					continue;
				}

				if (IsPointInsideOrNearPolygonXY(Candidate.Centroid, OtherCandidate.BuildingPolygon, 2.0f))
				{
					bNestedInsideAnotherHole = true;
					break;
				}
			}

			if (bNestedInsideAnotherHole)
			{
				continue;
			}

			FEHBFloorSlabHole Hole;
			if (ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, Candidate.BuildingPolygon, Hole.LocalPolygon))
			{
				OutLocalHoles.Add(MoveTemp(Hole));
			}
		}

		return OutLocalOuterPolygon.Num() >= 3;
	}

	int32 ResolveFloorSlabRoomFillFloorIndex(
		const AEHBBuildingActorBase* Building,
		const AEHB_FloorSlab* FloorSlab,
		const FVector& SlabWorldCenter)
	{
		if (FloorSlab && FloorSlab->FloorIndex > 0)
		{
			return FloorSlab->FloorIndex;
		}

		const int32 FloorIndexFromZ = Building
			? Building->ResolveFloorIndexFromWorldLocation(SlabWorldCenter)
			: INDEX_NONE;
		return FloorIndexFromZ > 0 ? FloorIndexFromZ : INDEX_NONE;
	}

	bool BuildLocationRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles,
		FEHBBuildingClosedLoop& OutRoom)
	{
		OutRoom=FEHBBuildingClosedLoop();
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		if (!FloorSlab || !FloorSlab->OwningBuilding || FloorSlab->bIsFoundation)
		{
			return false;
		}

		AEHBBuildingActorBase* Building = FloorSlab->OwningBuilding;
		// Loading can leave the transient wall connection cache incomplete until
		// all attached actors are available. RebuildClosedLoops alone only reads
		// that cache. Prepare it from serialized relations BEFORE the first query;
		// do not rely on a failed MCP command's MarkBuildingChanged to repair it.
		Building->RebuildElementAndRelationshipIndexes();
		FVector SlabBuildingLocalCenter = FVector::ZeroVector;
		if (!GetFloorSlabLocalPolygonCenterInBuildingSpace(Building, FloorSlab, SlabBuildingLocalCenter))
		{
			return false;
		}
		const FVector SlabWorldCenter = Building->GetActorTransform().TransformPosition(SlabBuildingLocalCenter);

		Building->RebuildClosedLoops();
		const int32 QueryFloorIndex = ResolveFloorSlabRoomFillFloorIndex(Building, FloorSlab, SlabWorldCenter);
		if (QueryFloorIndex <= 0)
		{
			return false;
		}

		const TArray<FEHBBuildingClosedLoop> CandidateLoops =
			Building->FindClosedLoopsContainingWorldLocation(SlabWorldCenter, QueryFloorIndex);
		if (CandidateLoops.IsEmpty())
		{
			return false;
		}

		int32 BestLoopIndex = INDEX_NONE;
		float BestArea = TNumericLimits<float>::Max();
		for (int32 LoopIndex = 0; LoopIndex < CandidateLoops.Num(); ++LoopIndex)
		{
			const FEHBBuildingClosedLoop& Loop = CandidateLoops[LoopIndex];
			if (Loop.Area > 1.0f && Loop.Area < BestArea)
			{
				BestLoopIndex = LoopIndex;
				BestArea = Loop.Area;
			}
		}

		if (!CandidateLoops.IsValidIndex(BestLoopIndex))
		{
			return false;
		}

		const FEHBBuildingClosedLoop& TargetLoop = CandidateLoops[BestLoopIndex];
		// Multiple containing rooms are only acceptable for strict nesting. A
		// point on a shared wall must not choose a room by TMap iteration order.
		TArray<FVector> SelectedPolygon;
		if(!BuildClosedLoopPolygonInBuildingSpace(Building,TargetLoop,SelectedPolygon))return false;
		for(int32 I=0;I<CandidateLoops.Num();++I)if(I!=BestLoopIndex)
		{
			TArray<FVector> OtherPolygon;
			if(!BuildClosedLoopPolygonInBuildingSpace(Building,CandidateLoops[I],OtherPolygon))return false;
			for(const FVector& P:SelectedPolygon)
				if(!IsPointInsideOrNearPolygonXY(P,OtherPolygon,0.5f))return false;
		}

		TArray<FVector> TargetBuildingPolygon;
		if (!BuildClosedLoopPolygonInBuildingSpace(Building, TargetLoop, TargetBuildingPolygon)
			|| !ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, TargetBuildingPolygon, OutLocalOuterPolygon))
		{
			return false;
		}

		struct FClosedLoopHoleCandidate
		{
			FEHBBuildingClosedLoop Loop;
			TArray<FVector> BuildingPolygon;
			FVector Centroid = FVector::ZeroVector;
		};

		TArray<FClosedLoopHoleCandidate> HoleCandidates;
		for (const FEHBBuildingClosedLoop& CandidateLoop : Building->ClosedLoops)
		{
			if (CandidateLoop.LoopGuid == TargetLoop.LoopGuid
				|| CandidateLoop.FloorIndex != TargetLoop.FloorIndex
				|| CandidateLoop.Area <= 1.0f
				|| CandidateLoop.Area >= TargetLoop.Area - 1.0f)
			{
				continue;
			}

			TArray<FVector> CandidatePolygon;
			if (!BuildClosedLoopPolygonInBuildingSpace(Building, CandidateLoop, CandidatePolygon))
			{
				continue;
			}

			const FVector CandidateCentroid = GetPolygonCentroidXY(CandidatePolygon);
			if (!IsPointInsideOrNearPolygonXY(CandidateCentroid, TargetBuildingPolygon, 2.0f)
				|| IsPointInsideOrNearPolygonXY(SlabBuildingLocalCenter, CandidatePolygon, 2.0f))
			{
				continue;
			}

			FClosedLoopHoleCandidate& HoleCandidate = HoleCandidates.AddDefaulted_GetRef();
			HoleCandidate.Loop = CandidateLoop;
			HoleCandidate.BuildingPolygon = MoveTemp(CandidatePolygon);
			HoleCandidate.Centroid = CandidateCentroid;
		}

		for (const FClosedLoopHoleCandidate& Candidate : HoleCandidates)
		{
			bool bNestedInsideAnotherHole = false;
			for (const FClosedLoopHoleCandidate& OtherCandidate : HoleCandidates)
			{
				if (OtherCandidate.Loop.LoopGuid == Candidate.Loop.LoopGuid
					|| OtherCandidate.Loop.Area <= Candidate.Loop.Area)
				{
					continue;
				}

				if (IsPointInsideOrNearPolygonXY(Candidate.Centroid, OtherCandidate.BuildingPolygon, 2.0f))
				{
					bNestedInsideAnotherHole = true;
					break;
				}
			}

			if (bNestedInsideAnotherHole)
			{
				continue;
			}

			FEHBFloorSlabHole Hole;
			if (ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, Candidate.BuildingPolygon, Hole.LocalPolygon))
			{
				OutLocalHoles.Add(MoveTemp(Hole));
			}
		}

		if(OutLocalOuterPolygon.Num()<3)return false;
		OutRoom=TargetLoop;
		return true;
	}

	bool BuildObstacleRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles)
	{
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		if (!FloorSlab || !FloorSlab->OwningBuilding)
		{
			return false;
		}

		AEHBBuildingActorBase* Building = FloorSlab->OwningBuilding;
		FVector SlabBuildingLocalCenter = FVector::ZeroVector;
		if (!GetFloorSlabLocalPolygonCenterInBuildingSpace(Building, FloorSlab, SlabBuildingLocalCenter))
		{
			return false;
		}

		TArray<TArray<FVector>> ObstaclePolygons;
		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			if (AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
			{
				if (Wall->OwningBuilding != Building || !Wall->ElementGuid.IsValid())
				{
					continue;
				}

				TArray<FVector> LeftPolyline;
				TArray<FVector> RightPolyline;
				if (!Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 25.0f)
					|| !Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 25.0f)
					|| LeftPolyline.Num() < 2
					|| RightPolyline.Num() < 2)
				{
					continue;
				}

				ReversePoints(RightPolyline);
				TArray<FVector> WallObstacleLoop;
				WallObstacleLoop.Reserve(LeftPolyline.Num() + RightPolyline.Num());
				for (FVector Point : LeftPolyline)
				{
					Point.Z = 0.0f;
					AddUniquePointXY(WallObstacleLoop, Point, 0.5f);
				}
				for (FVector Point : RightPolyline)
				{
					Point.Z = 0.0f;
					AddUniquePointXY(WallObstacleLoop, Point, 0.5f);
				}

				CleanPolygonXY(WallObstacleLoop);
				if (WallObstacleLoop.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(WallObstacleLoop)) > 1.0f)
				{
					ObstaclePolygons.Add(MoveTemp(WallObstacleLoop));
				}
				continue;
			}

			if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
			{
				if (Pillar->OwningBuilding != Building)
				{
					continue;
				}

				TArray<FVector> LocalFootprint;
				Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
				if (LocalFootprint.Num() < 3)
				{
					continue;
				}

				TArray<FVector> BuildingLocalFootprint;
				BuildingLocalFootprint.Reserve(LocalFootprint.Num());
				const FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
				for (FVector LocalPoint : LocalFootprint)
				{
					LocalPoint.Z = 0.0f;
					FVector BuildingLocalPoint = PillarLocalTransform.TransformPosition(LocalPoint);
					BuildingLocalPoint.Z = 0.0f;
					AddUniquePointXY(BuildingLocalFootprint, BuildingLocalPoint, 0.5f);
				}

				CleanPolygonXY(BuildingLocalFootprint);
				if (BuildingLocalFootprint.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(BuildingLocalFootprint)) > 1.0f)
				{
					ObstaclePolygons.Add(MoveTemp(BuildingLocalFootprint));
				}
			}
		}

		FEHBPlanarRoomFillResult RoomFillResult;
		if (!FEHBPlanarRoomFillSolver::BuildContainingFreeRegion(
			ObstaclePolygons,
			SlabBuildingLocalCenter,
			RoomFillResult))
		{
			return false;
		}

		if (!ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, RoomFillResult.OuterPolygon, OutLocalOuterPolygon))
		{
			return false;
		}

		for (const TArray<FVector>& HolePolygon : RoomFillResult.HolePolygons)
		{
			FEHBFloorSlabHole Hole;
			if (ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, HolePolygon, Hole.LocalPolygon))
			{
				OutLocalHoles.Add(MoveTemp(Hole));
			}
		}

		return OutLocalOuterPolygon.Num() >= 3;
	}

	bool BuildGeometricRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		EEHBFloorSlabWallSide AnchorWallSide,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles)
	{
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		if (!FloorSlab
			|| !FloorSlab->bHasRoomFillAnchor
			|| !FloorSlab->RoomFillAnchorWallGuid.IsValid()
			|| AnchorWallSide == EEHBFloorSlabWallSide::None)
		{
			return false;
		}

		AEHBBuildingActorBase* Building = FloorSlab->OwningBuilding;
		FVector SlabBuildingLocalCenter = FVector::ZeroVector;
		if (!GetFloorSlabLocalPolygonCenterInBuildingSpace(Building, FloorSlab, SlabBuildingLocalCenter))
		{
			return false;
		}

		TArray<FEHBFloorSlabRoomGraphSegment> Segments;
		FVector AnchorInteriorSample = FVector::ZeroVector;
		bool bHasAnchorInteriorSample = false;
		if (!BuildRoomGraphSegmentsForBuilding(
			Building,
			FloorSlab->RoomFillAnchorWallGuid,
			AnchorWallSide,
			SlabBuildingLocalCenter,
			AnchorInteriorSample,
			bHasAnchorInteriorSample,
			Segments))
		{
			return false;
		}

		TArray<FVector2d> Nodes;
		TMap<int32, TArray<int32>> Adjacency;
		TSet<uint64> AnchorDirectedEdges;
		if (!BuildRoomPlanarGraph(Segments, Nodes, Adjacency, AnchorDirectedEdges))
		{
			return false;
		}

		TArray<FEHBFloorSlabRoomGraphFace> Faces;
		TMap<uint64, int32> FaceIndexByDirectedEdge;
		BuildRoomGraphFaces(Nodes, Adjacency, Faces, FaceIndexByDirectedEdge);
		if (Faces.IsEmpty())
		{
			return false;
		}

		TArray<FVector> FloorSlabProbePoints;
		BuildFloorSlabProbePointsInBuildingSpace(Building, FloorSlab, FloorSlabProbePoints);

		auto DoesFaceContainAnyPoint = [](const FEHBFloorSlabRoomGraphFace& Face, const TArray<FVector>& Points)
		{
			for (const FVector& Point : Points)
			{
				if (IsPointInsidePolygonXY(Point, Face.Polygon))
				{
					return true;
				}
			}
			return false;
		};

		auto FindSmallestFaceContainingPoint = [&Faces](const FVector& Point)
		{
			int32 BestFaceIndex = INDEX_NONE;
			float BestFaceArea = TNumericLimits<float>::Max();
			for (int32 FaceIndex = 0; FaceIndex < Faces.Num(); ++FaceIndex)
			{
				const FEHBFloorSlabRoomGraphFace& Face = Faces[FaceIndex];
				if (Face.AbsArea <= 4.0f || Face.AbsArea >= BestFaceArea)
				{
					continue;
				}

				if (IsPointInsidePolygonXY(Point, Face.Polygon))
				{
					BestFaceIndex = FaceIndex;
					BestFaceArea = Face.AbsArea;
				}
			}

			return BestFaceIndex;
		};

		auto FindSmallestAnchorFace = [
			&Faces,
			&FaceIndexByDirectedEdge,
			&AnchorDirectedEdges,
			&DoesFaceContainAnyPoint](const TArray<FVector>& ProbePoints, bool bRequireProbePoint, bool bAllowOppositeSide)
		{
			int32 BestFaceIndex = INDEX_NONE;
			float BestFaceArea = TNumericLimits<float>::Max();
			TSet<int32> VisitedFaceIndices;
			for (const uint64 AnchorEdgeKey : AnchorDirectedEdges)
			{
				TArray<uint64, TInlineAllocator<2>> CandidateEdgeKeys;
				CandidateEdgeKeys.Add(AnchorEdgeKey);
				if (bAllowOppositeSide)
				{
					CandidateEdgeKeys.Add(MakeReversedDirectedRoomGraphEdgeKey(AnchorEdgeKey));
				}

				for (const uint64 CandidateEdgeKey : CandidateEdgeKeys)
				{
					const int32* FaceIndex = FaceIndexByDirectedEdge.Find(CandidateEdgeKey);
					if (!FaceIndex || !Faces.IsValidIndex(*FaceIndex) || VisitedFaceIndices.Contains(*FaceIndex))
					{
						continue;
					}

					VisitedFaceIndices.Add(*FaceIndex);
					const FEHBFloorSlabRoomGraphFace& Face = Faces[*FaceIndex];
					if (Face.AbsArea <= 4.0f || Face.AbsArea >= BestFaceArea)
					{
						continue;
					}

					if (bRequireProbePoint && !DoesFaceContainAnyPoint(Face, ProbePoints))
					{
						continue;
					}

					BestFaceIndex = *FaceIndex;
					BestFaceArea = Face.AbsArea;
				}
			}

			return BestFaceIndex;
		};

		TArray<FVector> CenterProbePoints;
		CenterProbePoints.Add(SlabBuildingLocalCenter);

		int32 TargetFaceIndex = FindSmallestFaceContainingPoint(SlabBuildingLocalCenter);

		if (!Faces.IsValidIndex(TargetFaceIndex))
		{
			TargetFaceIndex = FindSmallestAnchorFace(CenterProbePoints, true, false);
		}

		if (!Faces.IsValidIndex(TargetFaceIndex))
		{
			TargetFaceIndex = FindSmallestAnchorFace(FloorSlabProbePoints, true, false);
		}

		if (!Faces.IsValidIndex(TargetFaceIndex) && bHasAnchorInteriorSample)
		{
			TargetFaceIndex = FindSmallestFaceContainingPoint(AnchorInteriorSample);
		}

		if (!Faces.IsValidIndex(TargetFaceIndex))
		{
			TargetFaceIndex = FindSmallestAnchorFace(FloorSlabProbePoints, false, false);
		}

		if (!Faces.IsValidIndex(TargetFaceIndex))
		{
			TargetFaceIndex = FindSmallestAnchorFace(FloorSlabProbePoints, true, true);
		}

		if (!Faces.IsValidIndex(TargetFaceIndex))
		{
			return false;
		}

		const FEHBFloorSlabRoomGraphFace& TargetFace = Faces[TargetFaceIndex];
		if (!ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, TargetFace.Polygon, OutLocalOuterPolygon))
		{
			return false;
		}

		for (int32 FaceIndex = 0; FaceIndex < Faces.Num(); ++FaceIndex)
		{
			if (FaceIndex == TargetFaceIndex)
			{
				continue;
			}

			const FEHBFloorSlabRoomGraphFace& Candidate = Faces[FaceIndex];
			if (Candidate.AbsArea <= 4.0f || Candidate.AbsArea >= TargetFace.AbsArea - 1.0f)
			{
				continue;
			}

			if (!IsPointInsidePolygonXY(GetPolygonCentroidXY(Candidate.Polygon), TargetFace.Polygon))
			{
				continue;
			}

			FEHBFloorSlabHole Hole;
			if (ConvertBuildingPolygonToFloorSlabLocal(Building, FloorSlab, Candidate.Polygon, Hole.LocalPolygon))
			{
				OutLocalHoles.Add(MoveTemp(Hole));
			}
		}

		return OutLocalOuterPolygon.Num() >= 3;
	}

	bool BuildRoomFillPolygonsForFloorSlab(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector>& OutLocalOuterPolygon,
		TArray<FEHBFloorSlabHole>& OutLocalHoles,
		FEHBBuildingClosedLoop& OutRoom)
	{
		OutLocalOuterPolygon.Reset();
		OutLocalHoles.Reset();
		if (!FloorSlab || FloorSlab->bIsFoundation)
		{
			return false;
		}

		if (BuildLocationRoomFillPolygonsForFloorSlab(FloorSlab, OutLocalOuterPolygon, OutLocalHoles, OutRoom))
		{
			return true;
		}

		return false;
	}
}

const FEditorModeID FEasyHouseEditorMode::EM_EasyHouseEditorModeId = TEXT("EM_EasyHouseEditorMode");

void FEasyHouseEditorMode::Enter()
{
	FEdMode::Enter();

	// 模式进入时创建 Toolkit，并交给编辑器宿主窗口管理。
	// Toolkit 内部会创建 SEasyHouseBuilderPanel，也就是左侧按钮和右侧工具面板。
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FEasyHouseEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	RefreshSelectedElementEditor();
}

void FEasyHouseEditorMode::Exit()
{
	FinishRegionDrag={};SlabCutterDraft.Cancel();
 NodeHandleDrag={};SelectedWallNode.Invalidate();NodeHandleBuilding.Reset();
	RoomFloorWallDrag = {};
	bRoomFloorWallMoveEnabled = false;
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	ClearWallSelection();
	ClearRailingSelection();
	ClearStairSelection();
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	ClearHoveredControls();
	ElementEditorSelectedActor.Reset();
	if (Toolkit.IsValid())
	{
		if (FEasyHouseEditorModeToolkit* BuildingToolkit = static_cast<FEasyHouseEditorModeToolkit*>(Toolkit.Get()))
		{
			BuildingToolkit->ShutdownElementEditor();
		}
	}

	// 退出模式时主动关闭 Toolkit，确保面板和 Slate 引用被释放。
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

void FEasyHouseEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	SyncWallSelectionFromEditor();
	SyncStairSelectionFromEditor();
	SyncFloorSlabSelectionFromEditor();
	SyncFloorSelectionFromEditor();
	SyncRoofSelectionFromEditor();
	RefreshSelectedElementEditor();
	UpdateHoveredControls(ViewportClient ? ViewportClient->Viewport : nullptr);

	if (ForceViewportCursorVisibleFrames > 0)
	{
		RestoreViewportMouse(ViewportClient ? ViewportClient->Viewport : nullptr);
		--ForceViewportCursorVisibleFrames;
	}

	if (bRightMouseNavigationDown && ViewportClient && ViewportClient->Viewport)
	{
		constexpr int32 RightMouseMoveThresholdPixels = 8;
		const int32 MouseDeltaX = ViewportClient->Viewport->GetMouseX() - RightMouseNavigationStart.X;
		const int32 MouseDeltaY = ViewportClient->Viewport->GetMouseY() - RightMouseNavigationStart.Y;
		if (MouseDeltaX * MouseDeltaX + MouseDeltaY * MouseDeltaY > RightMouseMoveThresholdPixels * RightMouseMoveThresholdPixels)
		{
			bRightMouseNavigationMoved = true;
		}

		if (ViewportClient->IsPerspective())
		{
			const float ForwardInput =
				(ViewportClient->Viewport->KeyState(EKeys::W) ? 1.0f : 0.0f)
				- (ViewportClient->Viewport->KeyState(EKeys::S) ? 1.0f : 0.0f);
			const float RightInput =
				(ViewportClient->Viewport->KeyState(EKeys::D) ? 1.0f : 0.0f)
				- (ViewportClient->Viewport->KeyState(EKeys::A) ? 1.0f : 0.0f);
			const float UpInput =
				(ViewportClient->Viewport->KeyState(EKeys::E) ? 1.0f : 0.0f)
				- (ViewportClient->Viewport->KeyState(EKeys::Q) ? 1.0f : 0.0f);

			const FRotator ViewRotation = ViewportClient->GetViewRotation();
			FVector MovementDirection =
				ViewRotation.Vector() * ForwardInput
				+ FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y) * RightInput
				+ FVector::UpVector * UpInput;

			if (!MovementDirection.IsNearlyZero())
			{
				MovementDirection.Normalize();

				// Keep movement close to UE's flight-camera speed and respect the viewport toolbar.
				const float MovementSpeed =
					2000.0f * FMath::Max(0.0001f, ViewportClient->GetCameraSpeed());
				const FVector MovementDelta =
					MovementDirection * MovementSpeed * FMath::Max(0.0f, DeltaTime);

				ViewportClient->SetViewLocation(
					ViewportClient->GetViewLocation() + MovementDelta);
				ViewportClient->SetLookAtLocation(
					ViewportClient->GetLookAtLocation() + MovementDelta);
				bRightMouseNavigationUsed = true;
				ViewportClient->Viewport->Invalidate();
			}
		}
	}

	if (!bRightMouseNavigationDown && UpdateWallCreationMouseLocation(ViewportClient) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (!bRightMouseNavigationDown && UpdateRailingCreationMouseLocation(ViewportClient) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bDoorWindowPlacementActive && !bRightMouseNavigationDown && UpdateDoorWindowPlacement(nullptr) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bWallSurfacePlacementActive && !bRightMouseNavigationDown && UpdateWallSurfacePlacement(nullptr) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bPillarMeshPlacementActive && !bRightMouseNavigationDown && UpdatePillarMeshPlacement(nullptr) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bRailingMeshPlacementActive && !bRightMouseNavigationDown && UpdateRailingMeshPlacement(nullptr) && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bFloorSlabPlacementActive && !bRightMouseNavigationDown && UpdateFloorSlabPlacement() && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bFloorPlacementActive && !bRightMouseNavigationDown && UpdateFloorPlacement() && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bRoofPlacementActive && !bRightMouseNavigationDown && UpdateRoofPlacement() && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}

	if (bStairPlacementActive && !bRightMouseNavigationDown && UpdateStairPlacement() && ViewportClient && ViewportClient->Viewport)
	{
		ViewportClient->Viewport->Invalidate();
	}
}

void FEasyHouseEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	if(PDI&&FinishRegionDrag.bTracking&&!FinishRegionDrag.bCancelled&&FinishRegionDrag.Slab.Get()==SelectedFloorSlab.Get())
	{
		const auto& D=FinishRegionDrag;const auto Color=D.Feedback.bSucceeded?FLinearColor(0,1,1):FLinearColor(1,0.2f,0.1f);
		for(int32 I=0;I<D.Polygon.Num();++I)PDI->DrawLine(D.WorldTransform.TransformPosition(D.Polygon[I]),D.WorldTransform.TransformPosition(D.Polygon[(I+1)%D.Polygon.Num()]),Color,SDPG_Foreground,2);
	}
 if(PDI&&SlabCutterDraft.IsFor(SelectedFloorSlab.Get()))
 {
  const auto Polygon=SlabCutterDraft.Polygon();const auto Color=SlabCutterDraft.Feedback.bSucceeded?FLinearColor(0,1,1):FLinearColor(1,0.2f,0.1f);
  for(int32 I=0;I<Polygon.Num();++I)PDI->DrawLine(SlabCutterDraft.SlabTransform.TransformPosition(Polygon[I]),SlabCutterDraft.SlabTransform.TransformPosition(Polygon[(I+1)%Polygon.Num()]),Color,SDPG_Foreground,3);
 }
	DrawUnboundWallNodeControls(PDI);
	if(NodeHandleDrag.bCaptured&&!NodeHandleDrag.bCancelled)DrawNodeEditPreview(PDI,NodeHandleDrag.Building.Get(),NodeHandleDrag.Geometry);
	if(RoomFloorWallDrag.bOptionalNodes&&RoomFloorWallDrag.bCaptured&&!RoomFloorWallDrag.bCancelled)DrawNodeEditPreview(PDI,RoomFloorWallDrag.Building.Get(),RoomFloorWallDrag.NodeGeometry);
	SyncWallSelectionFromEditor();
	SyncRailingSelectionFromEditor();
	SyncStairSelectionFromEditor();
	SyncFloorSlabSelectionFromEditor();
	SyncFloorSelectionFromEditor();
	SyncRoofSelectionFromEditor();
	if (PDI && RoomFloorWallDrag.IsSet() && RoomFloorWallDrag.bCaptured && !RoomFloorWallDrag.bCancelled)
	{
		const auto& Draft = RoomFloorWallDrag;
		const FLinearColor Color = Draft.Feedback.bSucceeded ? FLinearColor(0,1,1) : FLinearColor(1,0.2f,0.1f);
		auto Line = [&](FVector A,FVector B){PDI->DrawLine(Draft.BuildingTransform.TransformPosition(A),Draft.BuildingTransform.TransformPosition(B),Color,SDPG_Foreground,2.0f);};
		if (Draft.Geometry.bSucceeded && Draft.Geometry.bWouldChange)
		{
			for (const auto& Edge : Draft.Geometry.ProposedTopology.Walls)
			{
				if (!Draft.Geometry.DirectWallGuids.Contains(Edge.WallGuid)) continue;
				const auto* A=Draft.Geometry.ProposedTopology.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge.StartNodeGuid;});
				const auto* B=Draft.Geometry.ProposedTopology.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge.EndNodeGuid;});
				const auto* Wall=Draft.Building.IsValid()?Cast<AEHB_Wall>(Draft.Building->FindElementActorByGuid(Edge.WallGuid)):nullptr;
				if(A&&B&&Wall){const FVector Up(0,0,Wall->Height);Line(A->LocalPosition,B->LocalPosition);Line(A->LocalPosition+Up,B->LocalPosition+Up);Line(A->LocalPosition,A->LocalPosition+Up);Line(B->LocalPosition,B->LocalPosition+Up);}
			}
			TMap<FGuid,FVector> Positions;
			for(const auto& N:Draft.Geometry.ProposedTopology.Nodes)Positions.Add(N.SourcePillarGuid,N.LocalPosition);
			TSet<FGuid> RequestedRoomWalls;
			for(const auto& Change:Draft.Geometry.RoomBoundaryChanges)if(!Change.BoundRoomSlabGuids.IsEmpty())
			{
				const auto Rooms=Draft.Building->GetClosedLoopsByFloor(Change.FloorIndex);
				if(const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==Change.RoomGuid;}))
					for(FGuid Id:Room->WallGuids)RequestedRoomWalls.Add(Id);
			}
			TArray<FEHBWallJunctionWallSides> RoomWallSides;bool bSideSolveAttempted=false,bSidesReady=false;
			for(const auto& Room:Draft.Geometry.RoomBoundaryChanges)
			{
				for(int32 I=0;I<Room.ProposedPolygon.Num();++I)
				{FVector A=Room.ProposedPolygon[I],B=Room.ProposedPolygon[(I+1)%Room.ProposedPolygon.Num()];A.Z=B.Z=1;Line(A,B);}
				const auto Rooms=Draft.Building->GetClosedLoopsByFloor(Room.FloorIndex);
				const auto* BoundRoom=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==Room.RoomGuid;});
				if(BoundRoom)for(FGuid Id:Room.BoundRoomSlabGuids)if(auto* Slab=Cast<AEHB_FloorSlab>(Draft.Building->FindElementActorByGuid(Id)))
				{
					TArray<FVector> Polygon;
					if(!bSideSolveAttempted){bSideSolveAttempted=true;bSidesReady=BuildCandidateRoomWallSides(Draft.Building.Get(),Positions,RoomWallSides,&RequestedRoomWalls);}
					if(bSidesReady&&BuildRoomSlabOutlineFromWallSides(Slab,*BoundRoom,RoomWallSides,Polygon))for(int32 I=0;I<Polygon.Num();++I)
					{FVector A=Slab->GetElementLocalTransform().TransformPosition(Polygon[I]),B=Slab->GetElementLocalTransform().TransformPosition(Polygon[(I+1)%Polygon.Num()]);A.Z+=0.5;B.Z+=0.5;Line(A,B);}
				}
			}
		}
		else
		{
			const FVector Delta=Draft.BuildingTransform.InverseTransformVectorNoScale(Draft.WorldDelta),Up(0,0,Draft.Height);
			Line(Draft.Start+Delta,Draft.End+Delta);Line(Draft.Start+Delta+Up,Draft.End+Delta+Up);Line(Draft.Start+Delta,Draft.Start+Delta+Up);Line(Draft.End+Delta,Draft.End+Delta+Up);
		}
	}
	DrawWallCreationPreview(PDI);
	DrawRailingCreationPreview(PDI);
	DrawWallSurfacePlacementPreview(PDI);
	DrawPillarMeshPlacementPreview(PDI);
	DrawRailingMeshPlacementPreview(PDI);
	DrawWallCurveControl(PDI);
	DrawRailingEndpointHandles(PDI);
	DrawStairBottomControl(PDI);
	DrawFloorSlabEditHandles(PDI);
	DrawFloorSlabCornerSnapGuides(PDI);
	DrawRoofEditHandles(PDI);
}

void FEasyHouseEditorMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
	if(Canvas&&GEngine&&NodeHandleDrag.bTracking)
	{
		FCanvasTextItem Text(FVector2D(24,145),NodeHandleDrag.bCancelled?LOCTEXT("NodeDragCancelled","已取消墙角移动"):NodeHandleDrag.Feedback.bSucceeded?LOCTEXT("NodeDragReady","墙角预览：松开应用，Esc 取消"):LOCTEXT("NodeDragRejected","无法移动：仅支持同层水平移动，请检查交叉墙面及关联构件"),GEngine->GetSmallFont(),NodeHandleDrag.Feedback.bSucceeded?FLinearColor::White:FLinearColor(1,0.3f,0.2f));Canvas->DrawItem(Text);
	}
	if(Canvas&&Viewport&&GEditor&&GEditor->GetSelectedActors()->Num()==1)
		for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* P=Cast<AEHB_Pillar>(*It))if(auto* B=P->OwningBuilding.Get();B&&B->HasWallNodeAuthority())
		{const FGuid Id=B->FindNodeForPhysicalPillar(P->ElementGuid);if(Id.IsValid())DrawBuildingHudButton(Canvas,new HEHBWallNodeProxy(B,Id,true),FVector2D((Viewport->GetSizeXY().X-190)*0.5f,Viewport->GetSizeXY().Y-84),FVector2D(190,34),LOCTEXT("RemoveColumnKeepNode","移除柱身，保留墙角"),false);}
	if (Canvas && GEngine && bRailingCreationToolActive && !RailingWallStatus.IsEmpty())
	{
		const FText Message = EHBRailingFeedback::Describe(RailingWallStatus);
		FCanvasTextItem Text(FVector2D(24, 90), Message, GEngine->GetSmallFont(), bRailingWallReady ? FLinearColor::White : FLinearColor(1, 0.3f, 0.2f));
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
	}
	if (Canvas && GEngine && RoomFloorWallDrag.IsSet())
	{
		const FString& Status = RoomFloorWallDrag.Feedback.Message;
		FText Message = RoomFloorWallDrag.bCancelled ? LOCTEXT("RoomWallMoveCancelled","已取消联动拖动")
			: RoomFloorWallDrag.Feedback.bSucceeded ? LOCTEXT("RoomWallMoveReady","房间联动预览：释放鼠标应用，Esc 取消")
			: Status == TEXT("MigrationRequired") ? LOCTEXT("RoomWallMoveNeedsPrepare","请先选中建筑，在墙体页点击“准备墙体联动”")
			: Status == TEXT("TargetNotSelected") ? LOCTEXT("RoomWallMoveSingleSelection","请只选择一面墙进行房间联动")
			: Status == TEXT("InvalidHorizontalDelta") ? LOCTEXT("RoomWallMoveHorizontalOnly","房间联动仅支持水平平移")
			: LOCTEXT("RoomWallMoveRejected","当前拖动无法联动：请检查轮廓是否手动修改、位置是否冲突，或建筑是否含尚未支持的构件");
		FCanvasTextItem Text(FVector2D(24,115),Message,GEngine->GetSmallFont(),RoomFloorWallDrag.Feedback.bSucceeded?FLinearColor::White:FLinearColor(1,0.3f,0.2f));
		Text.EnableShadow(FLinearColor::Black);Canvas->DrawItem(Text);
	}
	DrawFloorSlabCuttingToolbar(ViewportClient, Viewport, Canvas);
	DrawFloorFillToolbar(ViewportClient, Viewport, Canvas);
	DrawPillarConnectionToolbar(ViewportClient, Viewport, Canvas);
	DrawWallMeasurementHud(ViewportClient, Viewport, View, Canvas);
	DrawFloorSlabHandleMeasurementHud(Viewport, View, Canvas);
}

bool FEasyHouseEditorMode::RemoveSelectedWalls()
{
 if(!GEditor||GEditor->GetSelectedActors()->Num()==0)return false;
 AEHBBuildingActorBase* Building=nullptr;TArray<FGuid> Walls;bool ValidSelection=true;
 for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
 {
  auto* Wall=Cast<AEHB_Wall>(*It);if(!Wall||!Wall->OwningBuilding||Wall->OwningBuilding->WallNodeAuthority.Version!=2){ValidSelection=false;continue;}
  if(Building&&Building!=Wall->OwningBuilding)ValidSelection=false;Building=Wall->OwningBuilding;Walls.Add(Wall->ElementGuid);
 }
 if(Walls.IsEmpty())return false;
 FEHBToolsetOperationResult Result;
 if(!ValidSelection)Result.Message=TEXT("SelectSameBuildingWalls");
 else if(bWallCreationToolActive||bRailingCreationToolActive||NodeHandleDrag.bTracking||RoomFloorWallDrag.IsSet())Result.Message=TEXT("FinishCurrentWallEdit");
 else Result=UEHBBuildingToolset::RemoveWalls(Building,Walls,Building->RelationshipGraphRevision,false);
 if(Result.bSucceeded){GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,true);return true;}
 FText Message=LOCTEXT("WallRemovalNeedsPlan","无法安全迁移这面墙关联的构件，本次删墙已取消。");
 if(Result.Message==TEXT("OverlappingAdjacencySurfaces"))Message=LOCTEXT("WallRemovalDisplayOverlap","同一高度的层板实际轮廓重叠，无法确定保留区域。本次删墙已取消。");
 else if(Result.Message==TEXT("InvalidAllocatedSlab")||Result.Message==TEXT("InvalidDisplayCandidate"))Message=LOCTEXT("WallRemovalDisplayInvalid","层板显示分区未能通过轮廓或网格检查，本次删墙已取消。");
 else if(Result.Message==TEXT("DuplicateDisplayPriority"))Message=LOCTEXT("WallRemovalDisplayConflict","现有层板的显示分区存在冲突，本次删墙已取消。");
 else if(Result.Message==TEXT("FinishPartitionHeightMismatch"))Message=LOCTEXT("WallRemovalFinishHeight","两侧铺面的顶面高度不同，不能自动分配拆墙后的区域。本次删墙已取消。");
 else if(Result.Message==TEXT("FinishCurrentWallEdit"))Message=LOCTEXT("WallRemovalDuringDrag","请先完成或取消当前绘制和拖动，再删除墙面。");
 else if(Result.Message==TEXT("SelectSameBuildingWalls"))Message=LOCTEXT("WallRemovalMixedSelection","请只选择同一栋建筑中的墙面。本次混合选择的删除已取消。");
 UE_LOG(LogTemp,Warning,TEXT("EHB selected wall removal: %s"),*Result.Message);FNotificationInfo Notice(Message);Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);
 return true;
}

FVector FEasyHouseEditorMode::ResolveCreationRectangleEnd(const FVector& Start,const FVector& End,const FTransform& Frame,FEditorViewportClient* ViewportClient)
{
 const auto* Client=ViewportClient?ViewportClient:GCurrentLevelEditingViewportClient;
 const auto* Viewport=Client?Client->Viewport:nullptr;
 const bool Bypass=Viewport&&(Viewport->KeyState(EKeys::LeftControl)||Viewport->KeyState(EKeys::RightControl));
 bWallCreationRectangleConstrained=CreationAssist.bFixedRectangle&&!Bypass;
 return EHBDragAngleSnap::ResolveRectangle(Start,End,Frame,CreationAssist,Bypass);
}

FVector FEasyHouseEditorMode::ResolveCreationFreeEnd(const FVector& Start,const FVector& End,FEditorViewportClient* ViewportClient) const
{
 const auto* Client=ViewportClient?ViewportClient:GCurrentLevelEditingViewportClient;
 const auto* Viewport=Client?Client->Viewport:nullptr;
 const bool Bypass=Viewport&&(Viewport->KeyState(EKeys::LeftControl)||Viewport->KeyState(EKeys::RightControl));
 return EHBDragAngleSnap::Resolve(Start,End,CreationAssist,Bypass);
}

bool FEasyHouseEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
 if((Key==EKeys::Delete||Key==EKeys::BackSpace)&&Event==IE_Pressed&&RemoveSelectedWalls())return true;
	if((Key==EKeys::LeftControl||Key==EKeys::RightControl)&&(Event==IE_Pressed||Event==IE_Released))
 {
  if(bWallCreationToolActive)UpdateWallCreationMouseLocation(ViewportClient);
  if(bRailingCreationToolActive)UpdateRailingCreationMouseLocation(ViewportClient);
  if(Viewport)Viewport->Invalidate();
 }
 const bool bWallCreationShiftKey = Key == EKeys::LeftShift || Key == EKeys::RightShift;
	if (bWallCreationToolActive
		&& bWallCreationShiftKey
		&& (Event == IE_Pressed || Event == IE_Released))
	{
		if (bWallCreationDragging && Event == IE_Pressed)
		{
			bWallCreationRectangleDragLatched = true;
		}
		UpdateWallCreationMouseLocation(ViewportClient);
		if (Viewport)
		{
			Viewport->Invalidate();
		}
	}

	if (Key == EKeys::Escape && Event == IE_Pressed)
	{
		if(SlabCutterDraft.IsFor(SelectedFloorSlab.Get())){CancelSelectedFloorSlabPreviewCutter();if(Viewport)Viewport->Invalidate();return true;}
		if(FinishRegionDrag.bTracking){FinishRegionDrag.bCancelled=true;if(Viewport)Viewport->Invalidate();return true;}
  if(NodeHandleDrag.bTracking){NodeHandleDrag.Cancel();if(Viewport)Viewport->Invalidate();return true;}
		if (RoomFloorWallDrag.IsSet()) { RoomFloorWallDrag.Cancel(); if(Viewport)Viewport->Invalidate(); return true; }
		if (bWallCreationToolActive)
		{
			CancelWallCreation();
			return true;
		}
		if (bRailingCreationToolActive)
		{
			CancelRailingCreation();
			return true;
		}
		if (bDoorWindowPlacementActive)
		{
			CancelDoorWindowPlacement();
			return true;
		}
		if (bWallSurfacePlacementActive)
		{
			CancelWallSurfacePlacement();
			return true;
		}
		if (bPillarMeshPlacementActive)
		{
			CancelPillarMeshPlacement();
			return true;
		}
		if (bRailingMeshPlacementActive)
		{
			CancelRailingMeshPlacement();
			return true;
		}
		if (bFloorSlabPlacementActive)
		{
			CancelFloorSlabPlacement();
			return true;
		}
		if (bFloorPlacementActive)
		{
			CancelFloorPlacement();
			return true;
		}
		if (bRoofPlacementActive)
		{
			CancelRoofPlacement();
			return true;
		}
		if (bStairPlacementActive)
		{
			CancelStairPlacement();
			return true;
		}
		if (IsWallCurveControlSelected() || IsStairBottomControlSelected() || IsStairIntermediateControlSelected()
			|| IsRailingEndpointHandleSelected()
			|| IsFloorSlabHandleSelected() || IsRoofHandleSelected()
			|| SelectedWall.IsValid() || SelectedRailing.IsValid() || SelectedStair.IsValid() || SelectedFloorSlab.IsValid() || SelectedFloor.IsValid() || SelectedRoof.IsValid())
		{
			return ExitCurrentBuildingStateByRightClick();
		}
	}

	if ((Key == EKeys::Delete || Key == EKeys::BackSpace) && Event == IE_Pressed && IsFloorSlabHandleSelected())
	{
		AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
		if (FloorSlab && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter)
		{
			CancelSelectedFloorSlabPreviewCutter();
			return true;
		}

		if (FloorSlab && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner)
		{
   if(EHBSlabOpeningEdit::Supports(FloorSlab)&&SelectedFloorSlabHandleLoopIndex!=INDEX_NONE)
   {
    if(FinishRegionDrag.bTracking||SlabCutterDraft.bTracking)return true;
    const auto Result=EHBSlabOpeningEdit::Execute(FloorSlab,SelectedFloorSlabHandleLoopIndex,SelectedFloorSlabHandleFirstIndex,INDEX_NONE,false);
    if(Result.bSucceeded)SelectFloorSlab(FloorSlab);else{FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}return true;
   }
			const FScopedTransaction Transaction(LOCTEXT("DeleteFloorSlabCornerTransaction", "Delete Floor Slab Corner"));
			FloorSlab->Modify();
			if (FloorSlab->RemoveCorner(SelectedFloorSlabHandleLoopIndex, SelectedFloorSlabHandleFirstIndex))
			{
				SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
				SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
				SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
				SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
				if (GEditor)
				{
					GEditor->RedrawLevelEditingViewports();
				}
			}
			return true;
		}

		return true;
	}

	if (Key == EKeys::RightMouseButton)
	{
		if (Event == IE_Pressed && ShouldUseBuildingViewportNavigation())
		{
			BeginRightMouseNavigation(Viewport);
			return true;
		}

		if (Event == IE_Released && bRightMouseNavigationDown)
		{
			const bool bQuickClick = FinishRightMouseNavigation(Viewport);
			if (bQuickClick)
			{
				return ExitCurrentBuildingStateByRightClick();
			}

			if (bWallCreationToolActive)
			{
				UpdateWallCreationMouseLocation(ViewportClient);
			}
			if (bRailingCreationToolActive)
			{
				UpdateRailingCreationMouseLocation(ViewportClient);
			}
			if (bDoorWindowPlacementActive)
			{
				UpdateDoorWindowPlacement(nullptr);
			}
			if (bWallSurfacePlacementActive)
			{
				UpdateWallSurfacePlacement(nullptr);
			}
			if (bPillarMeshPlacementActive)
			{
				UpdatePillarMeshPlacement(nullptr);
			}
			if (bRailingMeshPlacementActive)
			{
				UpdateRailingMeshPlacement(nullptr);
			}
			if (bFloorSlabPlacementActive)
			{
				UpdateFloorSlabPlacement();
			}
			if (bFloorPlacementActive)
			{
				UpdateFloorPlacement();
			}
			if (bRoofPlacementActive)
			{
				UpdateRoofPlacement();
			}
			if (bStairPlacementActive)
			{
				UpdateStairPlacement();
			}
			if (Viewport)
			{
				Viewport->Invalidate();
			}
			return true;
		}
	}

	if (bRightMouseNavigationDown && IsViewportNavigationKey(Key))
	{
		if (Event == IE_Pressed || Event == IE_Repeat)
		{
			bRightMouseNavigationUsed = true;
		}
		return true;
	}

	if (bFloorSlabPlacementActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Released)
		{
			CommitFloorSlabPlacement();
			RequestViewportMouseRestore(Viewport);
			return true;
		}
	}

	if (bFloorPlacementActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Released)
		{
			UpdateFloorPlacement();
			CommitFloorPlacement();
			RequestViewportMouseRestore(Viewport);
			return true;
		}
	}

	if (bRoofPlacementActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Released)
		{
			UpdateRoofPlacement();
			CommitRoofPlacement();
			RequestViewportMouseRestore(Viewport);
			return true;
		}
	}

	if (bStairPlacementActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			UpdateStairPlacement();
			CommitStairPlacement();
			RequestViewportMouseRestore(Viewport);
			return true;
		}
	}

	if (!bDoorWindowPlacementActive && !bWallSurfacePlacementActive && !bPillarMeshPlacementActive && !bRailingMeshPlacementActive
		&& !bWallCreationToolActive && !bRailingCreationToolActive && !bFloorSlabPlacementActive && !bFloorPlacementActive && !bRoofPlacementActive && !bStairPlacementActive)
	{
		const bool bAltViewportNavigation =
			Viewport
			&& (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt));
		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed && !bAltViewportNavigation)
		{
			if (Viewport)
			{
				HHitProxy* HitProxyUnderMouse = Viewport->GetHitProxy(Viewport->GetMouseX(), Viewport->GetMouseY());
				if (HitProxyUnderMouse
					&& (HitProxyUnderMouse->IsA(HWidgetAxis::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBWallCurveControlProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBStairBottomControlProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBStairIntermediateControlProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBFloorSlabHandleProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBRoofHandleProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBFloorSlabToolbarProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBFloorToolbarProxy::StaticGetType())
						|| HitProxyUnderMouse->IsA(HEHBPillarConnectionToolbarProxy::StaticGetType())
      || HitProxyUnderMouse->IsA(HEHBWallNodeProxy::StaticGetType())))
				{
					return false;
				}
			}

			FHitResult HitResult;
			if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
			{
				if (AEHB_DoorWindow* DoorWindow = FindDoorWindowUnderCursor(HitResult))
				{
					if (GEditor)
					{
						GEditor->SelectNone(false, true, false);
						GEditor->SelectActor(DoorWindow, true, true, true);
					}
					return true;
				}

				if (AEHBGableRoof* Roof = ResolveRoofFromHit(HitResult))
				{
					if (GEditor)
					{
						const bool bAdditiveSelection = Viewport
							&& (Viewport->KeyState(EKeys::LeftControl)
								|| Viewport->KeyState(EKeys::RightControl)
								|| Viewport->KeyState(EKeys::LeftShift)
								|| Viewport->KeyState(EKeys::RightShift));
						if (!bAdditiveSelection)
						{
							GEditor->SelectNone(false, true, false);
						}
						GEditor->SelectActor(
							Roof,
							!bAdditiveSelection || !Roof->IsSelected(),
							true,
							true);
						SyncRoofSelectionFromEditor();
					}
					else
					{
						SelectRoof(Roof);
					}
					return true;
				}

				if (AEHB_Floor* Floor = ResolveFloorFromHit(HitResult))
				{
					SelectFloor(Floor);
					if (GEditor)
					{
						GEditor->SelectNone(false, true, false);
						GEditor->SelectActor(Floor, true, true, true);
					}
					return true;
				}

				if (AEHB_FloorSlab* FloorSlab = ResolveFloorSlabFromHit(HitResult))
				{
					SelectFloorSlab(FloorSlab);
					if (GEditor)
					{
						GEditor->SelectNone(false, true, false);
						GEditor->SelectActor(FloorSlab, true, true, true);
					}
					return true;
				}

				if (AEHB_Wall* Wall = ResolveWallFromHit(HitResult))
				{
					SelectWall(Wall);
					if (GEditor)
					{
						GEditor->SelectNone(false, true, false);
						GEditor->SelectActor(Wall, true, true, true);
					}
					return true;
				}
			}
		}
	}

	if (bWallCreationToolActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			UpdateWallCreationMouseLocation(ViewportClient);
			CaptureWallCreationStart(Viewport);
			if (Viewport)
			{
				Viewport->Invalidate();
			}
			return true;
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Released && bWallCreationDragging)
		{
			UpdateWallCreationMouseLocation(ViewportClient);
			if (IsWallCreationRectangleModeActive(Viewport))
			{
				FinishWallCreationRectangleDrag();
			}
			else
			{
				FinishWallCreationDrag();
			}
			RequestViewportMouseRestore(Viewport);
			if (Viewport)
			{
				Viewport->Invalidate();
			}
			return true;
		}
	}

	if (bRailingCreationToolActive && !bRightMouseNavigationDown)
	{
		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			if (!UpdateRailingCreationMouseLocation(ViewportClient)) return true;
			RailingWallDrag = HoveredRailingWall;
			RailingCreationStartLocation = RailingCreationMouseLocation;
			RailingCreationStartPillar = HoveredRailingCreationPillar.Get();
			RailingCreationStartRailing = HoveredRailingCreationRailing.Get();
			RailingCreationStartStair = HoveredRailingCreationStair.Get();
			RailingCreationStartPostGuid = HoveredRailingCreationPostGuid;
			RailingCreationStartStairSide = HoveredRailingCreationStairSide;
			bRailingCreationDragging = true;
			if (Viewport)
			{
				Viewport->Invalidate();
			}
			return true;
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Released && bRailingCreationDragging)
		{
			UpdateRailingCreationMouseLocation(ViewportClient);
			FinishRailingCreationDrag();
			RequestViewportMouseRestore(Viewport);
			if (Viewport)
			{
				Viewport->Invalidate();
			}
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FEasyHouseEditorMode::InputAxis(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	if (bRightMouseNavigationDown && (Key == EKeys::MouseX || Key == EKeys::MouseY))
	{
		if (!FMath::IsNearlyZero(Delta))
		{
			bRightMouseNavigationMoved = true;
		}

		if (ViewportClient && ViewportClient->IsPerspective())
		{
			const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
			const float MouseSensitivity = ViewportSettings ? ViewportSettings->MouseSensitivty : 1.0f;
			const float PitchDirection = ViewportSettings && ViewportSettings->bInvertMouseLookYAxis ? -1.0f : 1.0f;

			FVector Drag = FVector::ZeroVector;
			FVector Scale = FVector::ZeroVector;
			FRotator Rot = FRotator::ZeroRotator;
			if (Key == EKeys::MouseX)
			{
				Rot.Yaw = Delta * MouseSensitivity;
			}
			else
			{
				Rot.Pitch = Delta * MouseSensitivity * PitchDirection;
			}

			ViewportClient->MoveViewportCamera(Drag, Rot);
			if (Viewport)
			{
				Viewport->Invalidate();
			}
		}

		return true;
	}

	return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}

bool FEasyHouseEditorMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
 if(SlabCutterDraft.bTracking)
 {
  if(SlabCutterDraft.IsFor(SelectedFloorSlab.Get()))SlabCutterDraft.Update(InDrag,InRot,InScale);else SlabCutterDraft.Cancel();if(InViewport)InViewport->Invalidate();return true;
 }
	if(FinishRegionDrag.bTracking)
	{
		if(FinishRegionDrag.Slab.Get()!=SelectedFloorSlab.Get()||!InRot.IsNearlyZero()||!InScale.IsNearlyZero())FinishRegionDrag.bCancelled=true;
		FinishRegionDrag.Update(InDrag);if(InViewport)InViewport->Invalidate();return true;
	}
 if(NodeHandleDrag.bTracking){NodeHandleDrag.AddDelta(InDrag,InRot,InScale);if(InViewport)InViewport->Invalidate();return true;}
	if (RoomFloorWallDrag.IsSet())
	{
		RoomFloorWallDrag.AddDelta(InDrag,InRot,InScale);
		if(InViewport)InViewport->Invalidate();
		return true;
	}
	if (AEHB_Wall* Wall = SelectedWall.Get(); Wall && IsWallCurveControlSelected())
	{
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		Wall->Modify();
		const FVector CurrentWorld = GetWallCurveControlWorldLocation();
		Wall->SetCurveControlWorldLocation(CurrentWorld + InDrag);
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (AEHB_Wall* Wall = SelectedWall.Get(); Wall && !IsWallCurveControlSelected())
	{
		if (bRoomFloorWallMoveEnabled||(Wall->OwningBuilding&&Wall->OwningBuilding->WallNodeAuthority.Version==2)) return true; // A cancelled/replaced draft must never fall through to legacy mutation.
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		if (InDrag.IsNearlyZero())
		{
			return true;
		}

		const bool bMovedWall = MoveSelectedWallByDelta(InDrag, false);
		if (bMovedWall && InViewport)
		{
			InViewport->Invalidate();
		}
		return bMovedWall;
	}

	if (AEHB_Railing* Railing = SelectedRailing.Get(); Railing && IsRailingEndpointHandleSelected())
	{
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		const FVector WorldDelta(InDrag.X, InDrag.Y, 0.0f);
		if (!WorldDelta.IsNearlyZero())
		{
			MoveSelectedRailingEndpointHandle(WorldDelta);
		}
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (AEHB_Stair* Stair = SelectedStair.Get(); Stair && IsStairBottomControlSelected())
	{
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		Stair->Modify();
		if (!InDrag.IsNearlyZero())
		{
			Stair->SetBottomControlWorldLocation(GetStairBottomControlWorldLocation() + FVector(InDrag.X, InDrag.Y, 0.0f));
		}
		if (!FMath::IsNearlyZero(InRot.Yaw))
		{
			Stair->AddBottomControlYaw(InRot.Yaw);
		}
		RebuildRailingsHostedByStair(Stair, false);
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (AEHB_Stair* Stair = SelectedStair.Get(); Stair && IsStairIntermediateControlSelected())
	{
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		Stair->Modify();
		Stair->SetIntermediateControlWorldLocation(
			SelectedStairIntermediateControlIndex,
			GetStairIntermediateControlWorldLocation() + FVector(InDrag.X, InDrag.Y, 0.0f));
		RebuildRailingsHostedByStair(Stair, false);
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (AEHBGableRoof* Roof = SelectedRoof.Get(); Roof && IsRoofHandleSelected())
	{
		if (!InViewportClient || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
		{
			return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
		}

		Roof->Modify();
		if (SelectedRoofHandleKind == EEHBRoofEditHandleKind::Height)
		{
			if (ApplyRoofHeightHandleWorldDelta(*Roof, InDrag))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[EHB Roof Edit] Height handle moved roof=%s pitch=%.2f ridgeHeight=%.2f"),
					*Roof->GetName(),
					Roof->PitchDegrees,
					GetRoofGableRidgeHeight(*Roof));
			}
		}
		else if (ApplyRoofEdgeHandleWorldDelta(*Roof, SelectedRoofHandleFirstIndex, InDrag))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[EHB Roof Edit] Edge handle moved roof=%s edge=%d length=%.2f width=%.2f location=%s"),
				*Roof->GetName(),
				SelectedRoofHandleFirstIndex,
				Roof->Length,
				Roof->Width,
				*Roof->GetActorLocation().ToCompactString());
		}
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!FloorSlab || !IsFloorSlabHandleSelected())
	{
		return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
	}

	// Retained outlines require the captured command, never a legacy direct setter.
	if(EHBFinishRegionCommand::IsIndependent(FloorSlab))return true;
	FloorSlab->Modify();
	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner)
	{
		const FVector CurrentWorld = GetFloorSlabHandleWorldLocation();
		FVector TargetWorld = CurrentWorld + FVector(InDrag.X, InDrag.Y, 0.0f);
		FloorSlab->SnapCornerToAdjacentAxesWorldLocation(
			SelectedFloorSlabHandleLoopIndex,
			SelectedFloorSlabHandleFirstIndex,
			TargetWorld);
		if (SelectedFloorSlabHandleLoopIndex == INDEX_NONE)
		{
			FloorSlab->SnapOuterCornerHandleWorldLocation(SelectedFloorSlabHandleFirstIndex, TargetWorld);
		}
		if (FloorSlab->bIsFoundation)
		{
			TargetWorld = SnapWorldLocationToIntegerBuildingCoordinates(FloorSlab->OwningBuilding, TargetWorld);
		}
		const bool bUpdated = FloorSlab->UpdateCornerWorldLocation(
			SelectedFloorSlabHandleLoopIndex,
			SelectedFloorSlabHandleFirstIndex,
			TargetWorld);
		if (bUpdated && FloorSlab->bIsFoundation && FloorSlab->SnapFoundationBottomToGround())
		{
			FloorSlab->RebuildSlabMesh();
		}
	}
	else if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge)
	{
		FVector WorldDelta(InDrag.X, InDrag.Y, 0.0f);
		if (SelectedFloorSlabHandleLoopIndex == INDEX_NONE)
		{
			FloorSlab->SnapOuterEdgeHandleWorldDelta(
				SelectedFloorSlabHandleFirstIndex,
				SelectedFloorSlabHandleSecondIndex,
				WorldDelta);
		}
		if (FloorSlab->bIsFoundation)
		{
			TArray<FVector> Loop;
			if (FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop)
				&& Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
			{
				const FVector CurrentFirstWorld = FloorSlab->GetActorTransform().TransformPosition(Loop[SelectedFloorSlabHandleFirstIndex]);
				const FVector DesiredFirstWorld = CurrentFirstWorld + WorldDelta;
				WorldDelta = SnapWorldLocationToIntegerBuildingCoordinates(FloorSlab->OwningBuilding, DesiredFirstWorld) - CurrentFirstWorld;
				WorldDelta.Z = 0.0f;
			}
		}
		const bool bUpdated = FloorSlab->OffsetEdgeWorldLocation(
			SelectedFloorSlabHandleLoopIndex,
			SelectedFloorSlabHandleFirstIndex,
			SelectedFloorSlabHandleSecondIndex,
			WorldDelta);
		if (bUpdated && FloorSlab->bIsFoundation && FloorSlab->SnapFoundationBottomToGround())
		{
			FloorSlab->RebuildSlabMesh();
		}
	}
	else if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter)
	{
		if (FloorSlab->PreviewCutters.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
		{
			FTransform WorldTransform = FloorSlab->PreviewCutters[SelectedFloorSlabHandleFirstIndex].LocalTransform * FloorSlab->GetActorTransform();
			WorldTransform.AddToTranslation(InDrag);
			if (!InRot.IsNearlyZero())
			{
				WorldTransform.ConcatenateRotation(InRot.Quaternion());
				WorldTransform.NormalizeRotation();
			}
			if (!InScale.IsNearlyZero())
			{
				const FVector CurrentScale = WorldTransform.GetScale3D();
				WorldTransform.SetScale3D(FVector(
					FMath::Max(0.05f, CurrentScale.X + InScale.X),
					FMath::Max(0.05f, CurrentScale.Y + InScale.Y),
					FMath::Max(0.05f, CurrentScale.Z + InScale.Z)));
			}
			if (FloorSlab->bIsFoundation)
			{
				WorldTransform.SetLocation(SnapWorldLocationToIntegerBuildingCoordinates(FloorSlab->OwningBuilding, WorldTransform.GetLocation()));
			}
			FloorSlab->UpdateCutterWorldTransform(SelectedFloorSlabHandleFirstIndex, WorldTransform);
		}
	}

	if (InViewport)
	{
		InViewport->Invalidate();
	}
	return true;
}

bool FEasyHouseEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
 if(HasSelectedUnboundWallNode()&&InViewportClient&&InViewportClient->GetCurrentWidgetAxis()!=EAxisList::None){NodeHandleDrag.Capture(NodeHandleBuilding.Get(),SelectedWallNode);return true;}
	if(!bWallCreationToolActive&&!bRailingCreationToolActive&&InViewportClient&&InViewportClient->GetCurrentWidgetAxis()!=EAxisList::None&&GEditor&&GEditor->GetSelectedActors()->Num()==1)
		for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* P=Cast<AEHB_Pillar>(*It);P&&P->OwningBuilding&&P->OwningBuilding->WallNodeAuthority.Version==2)
		{SetActiveBuilding(P->OwningBuilding);NodeHandleDrag.Capture(P->OwningBuilding,P->OwningBuilding->FindNodeForPhysicalPillar(P->ElementGuid),true);return true;}
	if (bWallCreationToolActive && !bRightMouseNavigationDown)
	{
		if (!bWallCreationDragging)
		{
			UpdateWallCreationMouseLocation(InViewportClient);
			CaptureWallCreationStart(InViewport);
		}
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (InViewportClient
		&& InViewportClient->GetCurrentWidgetAxis() != EAxisList::None
		&& HasSelectedWallOrPillarForMeasurement())
	{
		bWallPillarMeasurementTracking = true;
		if (InViewport)
		{
			InViewport->Invalidate();
		}
	}

	if (SelectedWall.IsValid()
		&& IsWallCurveControlSelected()
		&& InViewportClient
		&& InViewportClient->GetCurrentWidgetAxis() != EAxisList::None
		&& !ActiveWallCurveControlEditTransaction.IsValid())
	{
		ActiveWallCurveControlEditTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditWallCurveControlTransaction", "Edit Wall Curve Control"));
		SelectedWall->Modify();
		return true;
	}

	if (SelectedWall.IsValid()
		&& !IsWallCurveControlSelected()
		&& InViewportClient
		&& InViewportClient->GetCurrentWidgetAxis() != EAxisList::None
		&& !ActiveWallMoveTransaction.IsValid())
	{
		if (bRoomFloorWallMoveEnabled||(SelectedWall->OwningBuilding&&SelectedWall->OwningBuilding->WallNodeAuthority.Version==2))
		{
			RoomFloorWallDrag.Capture(SelectedWall.Get());
			return true;
		}
		ActiveWallMoveTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveSelectedWallTransaction", "Move Wall"));
		return true;
	}

	if (SelectedRailing.IsValid()
		&& IsRailingEndpointHandleSelected()
		&& InViewportClient
		&& InViewportClient->GetCurrentWidgetAxis() != EAxisList::None
		&& !ActiveRailingEndpointEditTransaction.IsValid())
	{
		ActiveRailingEndpointEditTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditRailingEndpointHandleTransaction", "Edit Railing Endpoint"));
		SelectedRailing->Modify();
		return true;
	}

	if (SelectedStair.IsValid()
		&& (IsStairBottomControlSelected() || IsStairIntermediateControlSelected())
		&& InViewportClient
		&& InViewportClient->GetCurrentWidgetAxis() != EAxisList::None
		&& !ActiveStairBottomControlEditTransaction.IsValid())
	{
		ActiveStairBottomControlEditTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditStairBottomControlTransaction", "Edit Stair Bottom Control"));
		SelectedStair->Modify();
		return true;
	}

	if (SelectedFloorSlab.IsValid() && IsFloorSlabHandleSelected() && !ActiveFloorSlabEditTransaction.IsValid())
	{
  if(SlabCutterDraft.IsFor(SelectedFloorSlab.Get())&&SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Cutter){SlabCutterDraft.bTracking=true;return true;}
  const auto* Slab=SelectedFloorSlab.Get();
  const bool OpeningHandle=SelectedFloorSlabHandleLoopIndex!=INDEX_NONE&&Slab->OwningBuilding&&Slab->OwningBuilding->WallNodeAuthority.Version==2&&(Slab->OutlineSource==EEHBOutlineSource::RoomBoundary||Slab->OutlineSource==EEHBOutlineSource::RetainedRegion)&&(SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Corner||SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Edge);
  if(OpeningHandle)
  {
   FinishRegionDrag.Begin(SelectedFloorSlab.Get(),SelectedFloorSlabHandleFirstIndex,SelectedFloorSlabHandleSecondIndex,SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Corner,SelectedFloorSlabHandleLoopIndex);return true;
  }
		if(EHBFinishRegionCommand::IsIndependent(SelectedFloorSlab.Get()))
		{
			if(SelectedFloorSlabHandleLoopIndex==INDEX_NONE&&(SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Corner||SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Edge))
				FinishRegionDrag.Begin(SelectedFloorSlab.Get(),SelectedFloorSlabHandleFirstIndex,SelectedFloorSlabHandleSecondIndex,SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Corner);
			return true;
		}
		ActiveFloorSlabEditTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditFloorSlabHandleTransaction", "Edit Floor Slab Handle"));
		SelectedFloorSlab->Modify();
		return true;
	}

	if (SelectedRoof.IsValid() && IsRoofHandleSelected() && !ActiveRoofEditTransaction.IsValid())
	{
		ActiveRoofEditTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditRoofHandleTransaction", "Edit Roof Handle"));
		SelectedRoof->Modify();
		bRoofEditInputDeltaLogged = false;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[EHB RoofCut] EditorMode StartTracking roof=%s handle=%d first=%d second=%d cutColliding=%d"),
			*SelectedRoof->GetName(),
			static_cast<int32>(SelectedRoofHandleKind),
			SelectedRoofHandleFirstIndex,
			SelectedRoofHandleSecondIndex,
			SelectedRoof->bCutCollidingElements ? 1 : 0);
		return true;
	}

	return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FEasyHouseEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
 if(SlabCutterDraft.bTracking){SlabCutterDraft.bTracking=false;if(InViewport)InViewport->Invalidate();return true;}
	if(FinishRegionDrag.bTracking)
	{
		if(FinishRegionDrag.Slab.Get()!=SelectedFloorSlab.Get())FinishRegionDrag.bCancelled=true;
		const auto Result=FinishRegionDrag.Execute(false);FinishRegionDrag={};
		if(!Result.bSucceeded){FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
		if(InViewport)InViewport->Invalidate();return true;
	}

 if(NodeHandleDrag.bTracking)
 {
  const auto Result=NodeHandleDrag.Execute(false);NodeHandleDrag={};
  if(!Result.bSucceeded){UE_LOG(LogTemp,Warning,TEXT("EHB wall node drag rejected: %s"),*Result.Message);FNotificationInfo Notice(LOCTEXT("NodeDragNotApplied","墙角移动未应用，请检查位置冲突或关联构件。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
  if(InViewport)InViewport->Invalidate();return true;
 }
	if (bWallCreationToolActive && bWallCreationDragging && !bRightMouseNavigationDown)
	{
		UpdateWallCreationMouseLocation(InViewportClient);
		if (IsWallCreationRectangleModeActive(InViewport))
		{
			FinishWallCreationRectangleDrag();
		}
		else
		{
			FinishWallCreationDrag();
		}
		RequestViewportMouseRestore(InViewport);
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	const bool bWasWallPillarMeasurementTracking = bWallPillarMeasurementTracking;
	bWallPillarMeasurementTracking = false;
	if (bWasWallPillarMeasurementTracking && InViewport)
	{
		InViewport->Invalidate();
	}

	if (ActiveWallCurveControlEditTransaction.IsValid())
	{
		if (AEHB_Wall* Wall = SelectedWall.Get())
		{
			Wall->Modify();
			Wall->RebuildWallMesh();
			Wall->MarkPackageDirty();
		}

		ActiveWallCurveControlEditTransaction.Reset();
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (RoomFloorWallDrag.IsSet())
	{
		const auto Result = RoomFloorWallDrag.Execute(false);
		RoomFloorWallDrag = {};
		if (!Result.bSucceeded)
		{
			UE_LOG(LogTemp,Warning,TEXT("EHB room wall drag rejected: %s"),*Result.Message);
			FNotificationInfo Notice(Result.Message == TEXT("NodeMoveFailedRollbackFailed")
				? LOCTEXT("RoomWallMoveRecoveryFailed","墙体联动恢复失败，请检查当前关卡状态后再保存。")
				: LOCTEXT("RoomWallMoveNotApplied","墙体联动未应用，原建筑已保留。请检查房间轮廓、位置冲突和支持范围。"));
			Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);
		}
		if(InViewport)InViewport->Invalidate();
		return true;
	}
	if (ActiveWallMoveTransaction.IsValid())
	{
		if (AEHB_Wall* Wall = SelectedWall.Get())
		{
			RefreshSelectedWallMoveNeighborhood(Wall, true);
			Wall->MarkPackageDirty();
			if (Wall->OwningBuilding)
			{
				Wall->OwningBuilding->RebuildClosedLoops();
				Wall->OwningBuilding->MarkPackageDirty();
			}
		}

		ActiveWallMoveTransaction.Reset();
		RoomFloorWallDrag = {};
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (ActiveRailingEndpointEditTransaction.IsValid())
	{
		if (AEHB_Railing* Railing = SelectedRailing.Get())
		{
			Railing->Modify();
			Railing->RebuildRailing();
			Railing->MarkPackageDirty();
		}

		ActiveRailingEndpointEditTransaction.Reset();
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (ActiveStairBottomControlEditTransaction.IsValid())
	{
		if (AEHB_Stair* Stair = SelectedStair.Get())
		{
			Stair->Modify();
			Stair->RebuildStairMesh();
			RebuildRailingsHostedByStair(Stair, true);
			Stair->MarkPackageDirty();
		}

		ActiveStairBottomControlEditTransaction.Reset();
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (ActiveFloorSlabEditTransaction.IsValid())
	{
		if (AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get())
		{
			FloorSlab->Modify();
			if (FloorSlab->bIsFoundation)
			{
				FloorSlab->SnapFoundationBottomToGround();
			}
			FloorSlab->RebuildSlabMesh();
			FloorSlab->NotifyElementGeometryChanged(true);
			FloorSlab->MarkPackageDirty();
		}
		ActiveFloorSlabEditTransaction.Reset();
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	if (ActiveRoofEditTransaction.IsValid())
	{
		if (AEHBGableRoof* Roof = SelectedRoof.Get())
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[EHB RoofCut] EditorMode EndTracking roof=%s cutColliding=%d"),
				*Roof->GetName(),
				Roof->bCutCollidingElements ? 1 : 0);
			Roof->Modify();
			Roof->RebuildRoofMesh();
			const bool bCutOperationsChanged = Roof->RefreshAutoCollisionCutOperations();
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[EHB RoofCut] EditorMode EndTracking refresh result roof=%s changed=%d cutOpCount=%d"),
				*Roof->GetName(),
				bCutOperationsChanged ? 1 : 0,
				Roof->CutOperations.Num());
			Roof->NotifyElementGeometryChanged(true);
			Roof->MarkPackageDirty();
		}
		ActiveRoofEditTransaction.Reset();
		bRoofEditInputDeltaLogged = false;
		if (InViewport)
		{
			InViewport->Invalidate();
		}
		return true;
	}

	return FEdMode::EndTracking(InViewportClient, InViewport);
}

FVector FEasyHouseEditorMode::GetWidgetLocation() const
{
	if(FinishRegionDrag.bTracking&&!FinishRegionDrag.bCancelled&&FinishRegionDrag.Slab.Get()==SelectedFloorSlab.Get())return FinishRegionDrag.WidgetLocation();

 if(NodeHandleDrag.bCaptured&&NodeHandleDrag.PhysicalActor.IsValid())return NodeHandleDrag.PhysicalActor->GetActorLocation()+NodeHandleDrag.BuildingTransform.TransformVectorNoScale(NodeHandleDrag.Target-NodeHandleDrag.Start);
 if(HasSelectedUnboundWallNode())
 {const auto* N=NodeHandleBuilding->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==SelectedWallNode;});const FVector Local=NodeHandleDrag.bCaptured&&!NodeHandleDrag.bCancelled?NodeHandleDrag.Target:N->LocalTransform.GetLocation();return NodeHandleBuilding->GetActorTransform().TransformPosition(Local+FVector(0,0,N->JunctionDimensions.Z));}
	if (RoomFloorWallDrag.IsSet() && RoomFloorWallDrag.Wall.IsValid())
		return RoomFloorWallDrag.Wall->GetActorLocation() + RoomFloorWallDrag.WorldDelta;
	if (IsWallCurveControlSelected())
	{
		return GetWallCurveControlWorldLocation();
	}

	if (IsRailingEndpointHandleSelected())
	{
		return GetRailingEndpointHandleWorldLocation();
	}

	if (IsStairBottomControlSelected())
	{
		return GetStairBottomControlWorldLocation();
	}

	if (IsStairIntermediateControlSelected())
	{
		return GetStairIntermediateControlWorldLocation();
	}

	if (IsFloorSlabHandleSelected())
	{
		return GetFloorSlabHandleWorldLocation();
	}

	if (IsRoofHandleSelected())
	{
		return GetRoofHandleWorldLocation();
	}

	return FEdMode::GetWidgetLocation();
}

bool FEasyHouseEditorMode::ShouldDrawWidget() const
{
 if(HasSelectedUnboundWallNode())return true;
	return IsWallCurveControlSelected() || IsRailingEndpointHandleSelected()
		|| IsStairBottomControlSelected() || IsStairIntermediateControlSelected()
		|| IsFloorSlabHandleSelected() || IsRoofHandleSelected() || FEdMode::ShouldDrawWidget();
}

bool FEasyHouseEditorMode::UsesTransformWidget() const
{
 if(HasSelectedUnboundWallNode())return true;
	if (SelectedWall.IsValid() && !IsWallCurveControlSelected())
	{
		return true;
	}

	return IsWallCurveControlSelected() || IsRailingEndpointHandleSelected()
		|| IsStairBottomControlSelected() || IsStairIntermediateControlSelected()
		|| IsFloorSlabHandleSelected() || IsRoofHandleSelected() || FEdMode::UsesTransformWidget();
}

bool FEasyHouseEditorMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
 if(HasSelectedUnboundWallNode())return CheckMode==UE::Widget::WM_Translate;
	if(GEditor&&!bWallCreationToolActive&&!bRailingCreationToolActive&&GEditor->GetSelectedActors()->Num()==1)
		for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
			if(const auto* P=Cast<AEHB_Pillar>(*It);P&&P->OwningBuilding&&P->OwningBuilding->WallNodeAuthority.Version==2)return CheckMode==UE::Widget::WM_Translate;
	if (SelectedWall.IsValid() && !IsWallCurveControlSelected())
	{
		return CheckMode == UE::Widget::WM_Translate;
	}

	if (IsFloorSlabHandleSelected())
	{
		if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter)
		{
			return CheckMode == UE::Widget::WM_Translate
				|| CheckMode == UE::Widget::WM_Rotate
				|| CheckMode == UE::Widget::WM_Scale;
		}

		return CheckMode == UE::Widget::WM_Translate;
	}

	if (IsRoofHandleSelected())
	{
		return CheckMode == UE::Widget::WM_Translate;
	}

	if (IsRailingEndpointHandleSelected())
	{
		return CheckMode == UE::Widget::WM_Translate;
	}

	if (IsStairBottomControlSelected())
	{
		return CheckMode == UE::Widget::WM_Translate || CheckMode == UE::Widget::WM_Rotate;
	}

	if (IsStairIntermediateControlSelected())
	{
		return CheckMode == UE::Widget::WM_Translate;
	}

	return IsWallCurveControlSelected() || IsRailingEndpointHandleSelected()
		|| IsFloorSlabHandleSelected() || IsRoofHandleSelected() || FEdMode::UsesTransformWidget(CheckMode);
}

bool FEasyHouseEditorMode::GetCursor(EMouseCursor::Type& OutCursor) const
{
	if (bRightMouseNavigationDown
		|| bWallCreationToolActive
		|| bRailingCreationToolActive
		|| bDoorWindowPlacementActive
		|| bWallSurfacePlacementActive
		|| bPillarMeshPlacementActive
		|| bRailingMeshPlacementActive
		|| bFloorSlabPlacementActive
		|| bFloorPlacementActive
		|| bRoofPlacementActive
		|| bStairPlacementActive)
	{
		OutCursor = EMouseCursor::Default;
		return true;
	}

	return FEdMode::GetCursor(OutCursor);
}

bool FEasyHouseEditorMode::GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const
{
	if (ForceViewportCursorVisibleFrames > 0)
	{
		bWantsOverride = true;
		bHardwareCursorVisible = true;
		return true;
	}

	return FEdMode::GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
}

bool FEasyHouseEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{

 if(HitProxy&&HitProxy->IsA(HEHBWallNodeProxy::StaticGetType())&&Click.GetKey()==EKeys::LeftMouseButton)
 {
  const auto* Proxy=static_cast<HEHBWallNodeProxy*>(HitProxy);
  if(!Proxy->bRemove)SelectUnboundWallNode(Proxy->Building.Get(),Proxy->NodeGuid);
  else
  {
   const auto Result=UEHBBuildingToolset::RemovePhysicalColumn(Proxy->Building.Get(),Proxy->NodeGuid,Proxy->PhysicalGuid,Proxy->Revision,false);
   if(Result.bSucceeded)SelectUnboundWallNode(Proxy->Building.Get(),Proxy->NodeGuid);
   else{UE_LOG(LogTemp,Warning,TEXT("EHB physical column removal rejected: %s"),*Result.Message);FNotificationInfo Notice(LOCTEXT("ColumnRemovalNeedsPlan","尚不能单独移除此柱身：请先处理关联的地板、层板、支撑或其他构件。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
  }
  return true;
 }
	if (bWallCreationToolActive && Click.GetKey() == EKeys::LeftMouseButton && !bRightMouseNavigationDown)
	{
		if (!bWallCreationDragging)
		{
			UpdateWallCreationMouseLocation(InViewportClient);
			CaptureWallCreationStart(InViewportClient ? InViewportClient->Viewport : nullptr);
		}

		if (Click.GetEvent() == IE_Released)
		{
			UpdateWallCreationMouseLocation(InViewportClient);
			if (Click.IsShiftDown() || IsWallCreationRectangleModeActive(InViewportClient ? InViewportClient->Viewport : nullptr))
			{
				FinishWallCreationRectangleDrag();
			}
			else
			{
				FinishWallCreationDrag();
			}
			RequestViewportMouseRestore(InViewportClient ? InViewportClient->Viewport : nullptr);
		}

		if (InViewportClient && InViewportClient->Viewport)
		{
			InViewportClient->Viewport->Invalidate();
		}
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBWallCurveControlProxy::StaticGetType()))
	{
		HEHBWallCurveControlProxy* HandleProxy = static_cast<HEHBWallCurveControlProxy*>(HitProxy);
		SelectWallCurveControl(HandleProxy->Wall.Get());
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBRailingEndpointHandleProxy::StaticGetType()))
	{
		HEHBRailingEndpointHandleProxy* HandleProxy = static_cast<HEHBRailingEndpointHandleProxy*>(HitProxy);
		SelectRailingEndpointHandle(HandleProxy->Railing.Get(), HandleProxy->HandleKind);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBStairBottomControlProxy::StaticGetType()))
	{
		HEHBStairBottomControlProxy* HandleProxy = static_cast<HEHBStairBottomControlProxy*>(HitProxy);
		SelectStairBottomControl(HandleProxy->Stair.Get());
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBStairIntermediateControlProxy::StaticGetType()))
	{
		HEHBStairIntermediateControlProxy* HandleProxy = static_cast<HEHBStairIntermediateControlProxy*>(HitProxy);
		SelectStairIntermediateControl(HandleProxy->Stair.Get(), HandleProxy->ControlIndex);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBRoofHandleProxy::StaticGetType()))
	{
		HEHBRoofHandleProxy* HandleProxy = static_cast<HEHBRoofHandleProxy*>(HitProxy);
		AEHBGableRoof* Roof = HandleProxy->Roof.Get();
		SelectRoofHandle(
			Roof,
			HandleProxy->HandleKind,
			HandleProxy->FirstIndex,
			HandleProxy->SecondIndex);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBFloorSlabHandleProxy::StaticGetType()))
	{
		HEHBFloorSlabHandleProxy* HandleProxy = static_cast<HEHBFloorSlabHandleProxy*>(HitProxy);
		SelectFloorSlabHandle(
			HandleProxy->FloorSlab.Get(),
			HandleProxy->HandleKind,
			HandleProxy->FirstIndex,
			HandleProxy->SecondIndex,
			HandleProxy->LoopIndex);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBFloorSlabToolbarProxy::StaticGetType()))
	{
		HEHBFloorSlabToolbarProxy* ToolbarProxy = static_cast<HEHBFloorSlabToolbarProxy*>(HitProxy);
		HandleFloorSlabToolbarCommand(ToolbarProxy->CommandIndex);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBFloorToolbarProxy::StaticGetType()))
	{
		HEHBFloorToolbarProxy* ToolbarProxy = static_cast<HEHBFloorToolbarProxy*>(HitProxy);
		HandleFloorToolbarCommand(ToolbarProxy->CommandIndex);
		return true;
	}

	if (HitProxy && HitProxy->IsA(HEHBPillarConnectionToolbarProxy::StaticGetType()))
	{
		ConnectSelectedPillars();
		return true;
	}

	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		if (AEHBGableRoof* Roof = Cast<AEHBGableRoof>(ActorProxy->Actor))
		{
			if (GEditor)
			{
				const bool bAdditiveSelection = Click.IsControlDown() || Click.IsShiftDown();
				if (!bAdditiveSelection)
				{
					GEditor->SelectNone(false, true, false);
				}
				GEditor->SelectActor(
					Roof,
					!bAdditiveSelection || !Roof->IsSelected(),
					true,
					true);
				SyncRoofSelectionFromEditor();
			}
			else
			{
				SelectRoof(Roof);
			}
			return true;
		}

		if (AEHB_Floor* Floor = Cast<AEHB_Floor>(ActorProxy->Actor))
		{
			SelectFloor(Floor);
			if (GEditor)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Floor, true, true, true);
			}
			return true;
		}

		if (AEHB_FloorSlab* FloorSlab = Cast<AEHB_FloorSlab>(ActorProxy->Actor))
		{
			SelectFloorSlab(FloorSlab);
			if (GEditor)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(FloorSlab, true, true, true);
			}
			return true;
		}

		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(ActorProxy->Actor))
		{
			SelectWall(Wall);
			if (GEditor)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Wall, true, true, true);
			}
			return true;
		}

		if (AEHB_Railing* Railing = Cast<AEHB_Railing>(ActorProxy->Actor))
		{
			SelectRailing(Railing);
			if (GEditor)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Railing, true, true, true);
			}
			return true;
		}

		if (AEHB_Stair* Stair = Cast<AEHB_Stair>(ActorProxy->Actor))
		{
			SelectStair(Stair);
			if (GEditor)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Stair, true, true, true);
			}
			return true;
		}
	}

	return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
}

void FEasyHouseEditorMode::SetActiveBuilding(AEHBBuildingActorBase* Building)
{
	ActiveBuilding = Building;
	if (!ActiveBuilding.IsValid())
	{
		CancelWallCreation();
		CancelRailingCreation();
		CancelDoorWindowPlacement();
		CancelWallSurfacePlacement();
		CancelPillarMeshPlacement();
		CancelRailingMeshPlacement();
		CancelFloorSlabPlacement();
		CancelFloorPlacement();
		CancelRoofPlacement();
		CancelStairPlacement();
		ClearWallSelection();
		ClearRailingSelection();
		ClearStairSelection();
		ClearFloorSelection();
		ClearRoofSelection();
	}
}

AEHBBuildingActorBase* FEasyHouseEditorMode::GetActiveBuilding() const
{
	return ActiveBuilding.Get();
}

void FEasyHouseEditorMode::AdoptCopiedBuilding(AEHBBuildingActorBase* Building)
{
	NodeHandleDrag = {};
	RoomFloorWallDrag = {};
	SelectedWallNode.Invalidate();
	NodeHandleBuilding.Reset();
	SetActiveBuilding(nullptr);
	SetActiveBuilding(Building);
	if (Toolkit.IsValid())
		static_cast<FEasyHouseEditorModeToolkit*>(Toolkit.Get())->AdoptCopiedBuilding(Building);
}

void FEasyHouseEditorMode::BeginWallCreation(AEHBBuildingActorBase* Building, float InWallHeight, float InWallThickness)
{
	SetActiveBuilding(Building);
	CancelRailingCreation();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();

	SetWallCreationDefaults(InWallHeight, InWallThickness);
	bWallCreationToolActive = ActiveBuilding.IsValid();
	bWallCreationDragging = false;
	HoveredWallCreationPillar.Reset();
	WallCreationStartPillar.Reset();
	HoveredWallCreationNode.Invalidate(); WallCreationStartNode.Invalidate();
	HoveredWallCreationNodeRevision = WallCreationStartNodeRevision = INDEX_NONE;
	HoveredWallCreationWall.Reset();
	WallCreationStartWall.Reset();
	HoveredWallCreationWallDistance = 0.0f;
	WallCreationStartWallDistance = 0.0f;
	HoveredWallCreationPillarFloorIndex = 1;
	WallCreationStartPillarFloorIndex = 1;
	bHoveredWallCreationTopSnap = false;
	bWallCreationStartTopSnap = false;
	bWallCreationRectangleDragLatched = false;
 bWallCreationRectangleConstrained = false;
	ResetRightMouseNavigation();
	WallCreationStartLocation = FVector::ZeroVector;

	UpdateWallCreationMouseLocation(GCurrentLevelEditingViewportClient);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::SetWallCreationPhysicalColumns(bool bEnabled)
{
	if(bWallCreationPhysicalColumns!=bEnabled){ResetWallCreationDragState();bWallCreationPhysicalColumns=bEnabled;}
}
bool FEasyHouseEditorMode::ShouldCreateWallColumns() const
{
	return bWallCreationPhysicalColumns||!ActiveBuilding.IsValid()||ActiveBuilding->WallNodeAuthority.Version!=2;
}

void FEasyHouseEditorMode::SetWallCreationDefaults(float InWallHeight, float InWallThickness)
{
	WallCreationHeight = FMath::Max(1.0f, InWallHeight);
	WallCreationThickness = FMath::Max(1.0f, InWallThickness);
}

void FEasyHouseEditorMode::SetWallsPanelActive(bool bInWallsPanelActive)
{
	if (bWallsPanelActive == bInWallsPanelActive)
	{
		return;
	}

	bWallsPanelActive = bInWallsPanelActive;
	bHoveredPillarConnectionToolbarButton = false;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEasyHouseEditorMode::IsWallCreationRectangleModeActive(FViewport* Viewport) const
{
	if (!bWallCreationToolActive)
	{
		return false;
	}

	if (bWallCreationDragging && bWallCreationRectangleDragLatched)
	{
		return true;
	}

	if (Viewport && (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift)))
	{
		return true;
	}

	if (GCurrentLevelEditingViewportClient
		&& GCurrentLevelEditingViewportClient->Viewport
		&& (GCurrentLevelEditingViewportClient->Viewport->KeyState(EKeys::LeftShift)
			|| GCurrentLevelEditingViewportClient->Viewport->KeyState(EKeys::RightShift)))
	{
		return true;
	}

	return FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsShiftDown();
}

bool FEasyHouseEditorMode::CanConnectSelectedPillars() const
{
	if (!bWallsPanelActive
		|| bWallCreationToolActive
		|| bRailingCreationToolActive
		|| bDoorWindowPlacementActive
		|| bWallSurfacePlacementActive
		|| bPillarMeshPlacementActive
		|| bRailingMeshPlacementActive
		|| bFloorSlabPlacementActive
		|| bFloorPlacementActive
		|| bStairPlacementActive)
	{
		return false;
	}

	AEHB_Pillar* FirstPillar = nullptr;
	AEHB_Pillar* SecondPillar = nullptr;
	if (!GetSelectedPillarPair(FirstPillar, SecondPillar))
	{
		return false;
	}

	if (!FirstPillar || !SecondPillar || FirstPillar->OwningBuilding != SecondPillar->OwningBuilding)
	{
		return false;
	}

	AEHBBuildingActorBase* Building = FirstPillar->OwningBuilding;
	if (!Building)
	{
		return false;
	}

	return Building->CanConnectPillars(FirstPillar, SecondPillar);
}

AEHB_Wall* FEasyHouseEditorMode::ConnectSelectedPillars()
{
	AEHB_Pillar* FirstPillar = nullptr;
	AEHB_Pillar* SecondPillar = nullptr;
	if (!GetSelectedPillarPair(FirstPillar, SecondPillar) || !FirstPillar || !SecondPillar)
	{
		return nullptr;
	}

	FirstPillar->EnsureElementGuid();
	SecondPillar->EnsureElementGuid();

	AEHBBuildingActorBase* Building = FirstPillar->OwningBuilding;
	if (!Building || SecondPillar->OwningBuilding != Building)
	{
		return nullptr;
	}

	bool bSameDirection = true;
	if (AEHB_Wall* ExistingWall = Building->FindWallBetweenPillars(FirstPillar->ElementGuid, SecondPillar->ElementGuid, bSameDirection))
	{
		SelectWall(ExistingWall);
		if (GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(ExistingWall, true, true, true);
		}
		return ExistingWall;
	}

	const FScopedTransaction Transaction(LOCTEXT("ConnectSelectedPillarsTransaction", "Connect Selected Pillars"));
	Building->Modify();
	FirstPillar->Modify();
	SecondPillar->Modify();

	ActiveBuilding = Building;
	AEHB_Wall* Wall = Building->ConnectPillars(FirstPillar, SecondPillar, WallCreationHeight, WallCreationThickness);
	if (!Wall)
	{
		return nullptr;
	}

	Building->MarkPackageDirty();
	SelectWall(Wall);

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Wall, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return Wall;
}

void FEasyHouseEditorMode::CancelWallCreation()
{
	bWallCreationToolActive = false;
	bWallCreationDragging = false;
	HoveredWallCreationPillar.Reset();
	WallCreationStartPillar.Reset();
	HoveredWallCreationNode.Invalidate(); WallCreationStartNode.Invalidate();
	HoveredWallCreationNodeRevision = WallCreationStartNodeRevision = INDEX_NONE;
	HoveredWallCreationWall.Reset();
	WallCreationStartWall.Reset();
	HoveredWallCreationWallDistance = 0.0f;
	WallCreationStartWallDistance = 0.0f;
	HoveredWallCreationPillarFloorIndex = 1;
	WallCreationStartPillarFloorIndex = 1;
	bHoveredWallCreationTopSnap = false;
	bWallCreationStartTopSnap = false;
	bWallCreationRectangleDragLatched = false;
 bWallCreationRectangleConstrained = false;
	ResetRightMouseNavigation();
	WallCreationStartLocation = FVector::ZeroVector;

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginRailingCreation(
	AEHBBuildingActorBase* Building,
	float InRailingHeight,
	float InPostSpacing,
	float InRailThickness)
{
	SetActiveBuilding(Building);
	CancelWallCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();

	SetRailingCreationDefaults(InRailingHeight, InPostSpacing, InRailThickness);
	bRailingCreationToolActive = ActiveBuilding.IsValid();
	bRailingCreationDragging = false;
	HoveredRailingCreationPillar.Reset();
	RailingCreationStartPillar.Reset();
	RailingWallDrag = {};
	HoveredRailingWall = {};
	HoveredRailingCreationRailing.Reset();
	RailingCreationStartRailing.Reset();
	HoveredRailingCreationStair.Reset();
	RailingCreationStartStair.Reset();
	HoveredRailingCreationPostGuid.Invalidate();
	RailingCreationStartPostGuid.Invalidate();
	HoveredRailingCreationStairSide = EEHBRailingSide::Left;
	RailingCreationStartStairSide = EEHBRailingSide::Left;
	RailingCreationStartLocation = FVector::ZeroVector;
	RailingCreationMouseLocation = FVector::ZeroVector;
	ResetRightMouseNavigation();

	UpdateRailingCreationMouseLocation(GCurrentLevelEditingViewportClient);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::SetRailingCreationDefaults(float InRailingHeight, float InPostSpacing, float InRailThickness)
{
	RailingCreationHeight = FMath::Max(1.0f, InRailingHeight);
	RailingCreationPostSpacing = FMath::Max(1.0f, InPostSpacing);
	RailingCreationThickness = FMath::Max(0.1f, InRailThickness);
}

void FEasyHouseEditorMode::CancelRailingCreation()
{
	bRailingCreationToolActive = false;
	bRailingCreationDragging = false;
	HoveredRailingCreationPillar.Reset();
	RailingCreationStartPillar.Reset();
	RailingWallDrag = {};
	HoveredRailingWall = {};
	HoveredRailingCreationRailing.Reset();
	RailingCreationStartRailing.Reset();
	HoveredRailingCreationStair.Reset();
	RailingCreationStartStair.Reset();
	HoveredRailingCreationPostGuid.Invalidate();
	RailingCreationStartPostGuid.Invalidate();
	HoveredRailingCreationStairSide = EEHBRailingSide::Left;
	RailingCreationStartStairSide = EEHBRailingSide::Left;
	RailingCreationStartLocation = FVector::ZeroVector;
	RailingCreationMouseLocation = FVector::ZeroVector;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginDoorWindowPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);
	ClearFloorSlabSelection();
	ClearRoofSelection();
	ClearWallSelection();
	ClearStairSelection();

	if (!ActiveBuilding.IsValid() || !SourceTable || RowName.IsNone() || SourceTable->GetRowStruct() != FEHBDoorWindowMeshData::StaticStruct())
	{
		return;
	}

	const FEHBDoorWindowMeshData* RowData = SourceTable->FindRow<FEHBDoorWindowMeshData>(RowName, TEXT("FEasyHouseEditorMode::BeginDoorWindowPlacement"), false);
	if (!RowData)
	{
		return;
	}
	const FEHBMeshSampleValidationResult ValidationResult =
		FEHBMeshSampleValidation::ValidateDoorWindowForTarget(*RowData, nullptr);
	LogSampleValidationResult(TEXT("BeginDoorWindowPlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return;
	}

	DoorWindowPlacementTable = SourceTable;
	DoorWindowPlacementRowName = RowName;
	bDoorWindowPlacementUsesDefaultClass = false;
	DoorWindowPlacementDefaultKind = EEHBDoorWindowElementKind::Window;
	DoorWindowPlacementDefaultClass.Reset();
	bDoorWindowPlacementActive = true;
	ResetRightMouseNavigation();

	PreviewDoorWindowActor = CreateDoorWindowPreviewActor(ActiveBuilding.Get(), *RowData);
	if (!PreviewDoorWindowActor.IsValid())
	{
		CancelDoorWindowPlacement();
		return;
	}
	UpdateDoorWindowPlacement(nullptr);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginDefaultDoorWindowPlacement(AEHBBuildingActorBase* Building, EEHBDoorWindowElementKind Kind)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);
	ClearFloorSlabSelection();
	ClearRoofSelection();
	ClearWallSelection();
	ClearStairSelection();

	if (!ActiveBuilding.IsValid())
	{
		return;
	}

	DoorWindowPlacementTable.Reset();
	DoorWindowPlacementRowName = NAME_None;
	bDoorWindowPlacementUsesDefaultClass = true;
	DoorWindowPlacementDefaultKind = Kind;
	DoorWindowPlacementDefaultClass.Reset();
	bDoorWindowPlacementActive = true;
	ResetRightMouseNavigation();

	PreviewDoorWindowActor = CreateDefaultDoorWindowPreviewActor(ActiveBuilding.Get(), Kind);
	if (!PreviewDoorWindowActor.IsValid())
	{
		CancelDoorWindowPlacement();
		return;
	}

	DoorWindowPlacementDefaultClass = PreviewDoorWindowActor->GetClass();
	UpdateDoorWindowPlacement(nullptr);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::UpdateDoorWindowPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	UpdateDoorWindowPlacement(&MouseEvent);
}

void FEasyHouseEditorMode::FinishDoorWindowPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	if (!bDoorWindowPlacementActive)
	{
		return;
	}

	UpdateDoorWindowPlacement(&MouseEvent);
	const bool bPlacedOnWall = CommitDoorWindowPlacement();

	if (!bPlacedOnWall)
	{
		if (AEHB_Wall* PreviewWall = HoveredDoorWindowWall.Get())
		{
			PreviewWall->ClearPreviewDoorWindowOpening();
		}

		if (AEHB_DoorWindow* PreviewActor = PreviewDoorWindowActor.Get())
		{
			PreviewActor->Destroy();
		}
	}

	bDoorWindowPlacementActive = false;
	DoorWindowPlacementTable.Reset();
	DoorWindowPlacementRowName = NAME_None;
	bDoorWindowPlacementUsesDefaultClass = false;
	DoorWindowPlacementDefaultKind = EEHBDoorWindowElementKind::Window;
	DoorWindowPlacementDefaultClass.Reset();
	PreviewDoorWindowActor.Reset();
	HoveredDoorWindowWall.Reset();
	DoorWindowPlacementWorldLocation = FVector::ZeroVector;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::CancelDoorWindowPlacement()
{
	if (AEHB_Wall* PreviewWall = HoveredDoorWindowWall.Get())
	{
		PreviewWall->ClearPreviewDoorWindowOpening();
	}

	if (AEHB_DoorWindow* PreviewActor = PreviewDoorWindowActor.Get())
	{
		PreviewActor->Destroy();
	}

	bDoorWindowPlacementActive = false;
	DoorWindowPlacementTable.Reset();
	DoorWindowPlacementRowName = NAME_None;
	bDoorWindowPlacementUsesDefaultClass = false;
	DoorWindowPlacementDefaultKind = EEHBDoorWindowElementKind::Window;
	DoorWindowPlacementDefaultClass.Reset();
	PreviewDoorWindowActor.Reset();
	HoveredDoorWindowWall.Reset();
	DoorWindowPlacementWorldLocation = FVector::ZeroVector;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginWallSurfacePlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName, bool bInCoverBothSides, bool bInFlipSampleSides)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid() || !SourceTable || RowName.IsNone() || SourceTable->GetRowStruct() != FEHBWallMeshData::StaticStruct())
	{
		return;
	}

	const FEHBWallMeshData* RowData = SourceTable->FindRow<FEHBWallMeshData>(RowName, TEXT("FEasyHouseEditorMode::BeginWallSurfacePlacement"), false);
	if (!RowData)
	{
		return;
	}
	const FEHBMeshSampleValidationResult ValidationResult =
		FEHBMeshSampleValidation::ValidateWallSurfaceForTarget(*RowData, nullptr, bInCoverBothSides);
	LogSampleValidationResult(TEXT("BeginWallSurfacePlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return;
	}

	WallSurfacePlacementTable = SourceTable;
	WallSurfacePlacementRowName = RowName;
	bWallSurfacePlacementCoverBothSides = bInCoverBothSides;
	bWallSurfacePlacementFlipSampleSides = bInFlipSampleSides;
	bWallSurfacePlacementActive = true;
	ResetRightMouseNavigation();
	UpdateWallSurfacePlacement(nullptr);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::UpdateWallSurfacePlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	UpdateWallSurfacePlacement(&MouseEvent);
}

void FEasyHouseEditorMode::FinishWallSurfacePlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	if (!bWallSurfacePlacementActive)
	{
		return;
	}

	UpdateWallSurfacePlacement(&MouseEvent);
	CommitWallSurfacePlacement();
	CancelWallSurfacePlacement();
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelWallSurfacePlacement()
{
	bWallSurfacePlacementActive = false;
	WallSurfacePlacementTable.Reset();
	WallSurfacePlacementRowName = NAME_None;
	bWallSurfacePlacementCoverBothSides = true;
	bWallSurfacePlacementFlipSampleSides = false;
	HoveredWallSurfaceWall.Reset();
	bHoveredWallSurfaceLeftSide = true;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginPillarMeshPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid() || !SourceTable || RowName.IsNone() || SourceTable->GetRowStruct() != FEHBPillarMeshData::StaticStruct())
	{
		return;
	}

	const FEHBPillarMeshData* RowData = SourceTable->FindRow<FEHBPillarMeshData>(RowName, TEXT("FEasyHouseEditorMode::BeginPillarMeshPlacement"), false);
	if (!RowData)
	{
		return;
	}
	const FEHBMeshSampleValidationResult ValidationResult =
		FEHBMeshSampleValidation::ValidatePillarForTarget(*RowData, nullptr);
	LogSampleValidationResult(TEXT("BeginPillarMeshPlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return;
	}

	PillarMeshPlacementTable = SourceTable;
	PillarMeshPlacementRowName = RowName;
	bPillarMeshPlacementActive = true;
	ResetRightMouseNavigation();
	UpdatePillarMeshPlacement(nullptr);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::UpdatePillarMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	UpdatePillarMeshPlacement(&MouseEvent);
}

void FEasyHouseEditorMode::FinishPillarMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	if (!bPillarMeshPlacementActive)
	{
		return;
	}

	UpdatePillarMeshPlacement(&MouseEvent);
	CommitPillarMeshPlacement();
	CancelPillarMeshPlacement();
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelPillarMeshPlacement()
{
	bPillarMeshPlacementActive = false;
	PillarMeshPlacementTable.Reset();
	PillarMeshPlacementRowName = NAME_None;
	HoveredPillarMeshPillar.Reset();
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginRailingMeshPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName, bool bApplyToSinglePost)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid() || !SourceTable || RowName.IsNone() || SourceTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return;
	}

	const FEHBRailingMeshData* RowData = SourceTable->FindRow<FEHBRailingMeshData>(
		RowName,
		TEXT("FEasyHouseEditorMode::BeginRailingMeshPlacement"),
		false);
	if (!RowData)
	{
		return;
	}

	const FEHBMeshSampleValidationResult ValidationResult =
		FEHBMeshSampleValidation::ValidateRailingForTarget(*RowData, nullptr);
	LogSampleValidationResult(TEXT("BeginRailingMeshPlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return;
	}

	RailingMeshPlacementTable = SourceTable;
	RailingMeshPlacementRowName = RowName;
	bRailingMeshPlacementApplyToSinglePost = bApplyToSinglePost;
	bRailingMeshPlacementActive = true;
	ResetRightMouseNavigation();
	UpdateRailingMeshPlacement(nullptr);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::UpdateRailingMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	UpdateRailingMeshPlacement(&MouseEvent);
}

void FEasyHouseEditorMode::FinishRailingMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent)
{
	if (!bRailingMeshPlacementActive)
	{
		return;
	}

	UpdateRailingMeshPlacement(&MouseEvent);
	CommitRailingMeshPlacement();
	CancelRailingMeshPlacement();
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelRailingMeshPlacement()
{
	bRailingMeshPlacementActive = false;
	RailingMeshPlacementTable.Reset();
	RailingMeshPlacementRowName = NAME_None;
	HoveredRailingMeshRailing.Reset();
	HoveredRailingMeshStair.Reset();
	HoveredRailingMeshPostGuid.Invalidate();
	HoveredRailingMeshStairSide = EEHBRailingSide::Left;
	bRailingMeshPlacementApplyToSinglePost = false;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::BeginFloorSlabPlacement(AEHBBuildingActorBase* Building, bool bIsFoundation, float InSize, float InThickness)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EHB RoofCreate] BeginRoofPlacement aborted: active building is invalid"));
		return;
	}

	bFloorSlabPlacementActive = true;
	bFloorSlabPlacementIsFoundation = bIsFoundation;
	FloorSlabPlacementSize = FMath::Max(10.0f, InSize);
	FloorSlabPlacementThickness = FMath::Max(1.0f, InThickness);
	FloorSlabPlacementWorldRotation = FRotator::ZeroRotator;
	bFloorSlabPlacementHasRoomFillAnchor = false;
	FloorSlabPlacementRoomFillAnchorWallGuid = FGuid();
	FloorSlabPlacementRoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;
	FloorSlabPlacementFloorIndex = 0;
	FloorSlabPlacementFloorRole = EEHBBuildingFloorElementRole::None;
	ResetRightMouseNavigation();
	UpdateFloorSlabPlacement();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::CancelFloorSlabPlacement()
{
	if (AEHB_FloorSlab* PreviewActor = PreviewFloorSlabActor.Get())
	{
		PreviewActor->Destroy();
	}

	PreviewFloorSlabActor.Reset();
	bFloorSlabPlacementActive = false;
	FloorSlabPlacementWorldRotation = FRotator::ZeroRotator;
	bFloorSlabPlacementHasRoomFillAnchor = false;
	FloorSlabPlacementRoomFillAnchorWallGuid = FGuid();
	FloorSlabPlacementRoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;
	FloorSlabPlacementFloorIndex = 0;
	FloorSlabPlacementFloorRole = EEHBBuildingFloorElementRole::None;
	ResetRightMouseNavigation();
}

bool FEasyHouseEditorMode::UpdateFloorSlabPlacement()
{
	if (!bFloorSlabPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	FVector DropLocation = ActiveBuilding->GetActorLocation();
	FRotator DropRotation = FRotator::ZeroRotator;
	bool bUseSideSnap = false;
	bool bHasRoomFillAnchor = false;
	FGuid RoomFillAnchorWallGuid;
	EEHBFloorSlabWallSide RoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;
	int32 ResolvedFloorIndex = 0;
	EEHBBuildingFloorElementRole ResolvedFloorRole = bFloorSlabPlacementIsFoundation
		? EEHBBuildingFloorElementRole::Foundation
		: EEHBBuildingFloorElementRole::None;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, nullptr))
	{
		FEHBFloorSlabSideSnapResult SnapResult;
		bUseSideSnap = ResolveFloorSlabSideSnapFromHit(HitResult, PreviewFloorSlabActor.Get(), SnapResult)
			&& ResolveSideSnapPlacement(SnapResult, FloorSlabPlacementSize, DropLocation, DropRotation);
		if (bUseSideSnap && SnapResult.AnchorWall.IsValid() && SnapResult.AnchorWall->ElementGuid.IsValid())
		{
			bHasRoomFillAnchor = true;
			RoomFillAnchorWallGuid = SnapResult.AnchorWall->ElementGuid;
			RoomFillAnchorWallSide = SnapResult.AnchorWallSide;
		}
		if (!bFloorSlabPlacementIsFoundation && bUseSideSnap && SnapResult.bAnchoredOnWallOrPillar)
		{
			ResolvedFloorIndex = SnapResult.AnchorFloorIndex > 0 ? SnapResult.AnchorFloorIndex : 1;
			ResolvedFloorRole = EEHBBuildingFloorElementRole::FloorCeiling;
			const FString AnchorName = SnapResult.AnchorActor.IsValid() ? SnapResult.AnchorActor->GetName() : TEXT("None");
			UE_LOG(
				LogTemp,
				Display,
				TEXT("EHB FloorSlab TopSnap Debug: Anchor=%s AnchorFloor=%d -> SlabFloor=%d Role=FloorCeiling Location=%s"),
				*AnchorName,
				SnapResult.AnchorFloorIndex,
				ResolvedFloorIndex,
				*DropLocation.ToCompactString());
		}
		else if (!bFloorSlabPlacementIsFoundation && bUseSideSnap)
		{
			const FString AnchorName = SnapResult.AnchorActor.IsValid() ? SnapResult.AnchorActor->GetName() : TEXT("None");
			UE_LOG(
				LogTemp,
				Display,
				TEXT("EHB FloorSlab TopSnap Debug: Anchor=%s did not provide a wall/pillar floor assignment. SlabFloor=0 Role=None Location=%s"),
				*AnchorName,
				*DropLocation.ToCompactString());
		}
	}

	if (!bUseSideSnap)
	{
		if (GCurrentLevelEditingViewportClient)
		{
			GetViewportDropLocation(GCurrentLevelEditingViewportClient, DropLocation);
		}
		DropLocation.Z += FMath::Max(1.0f, FloorSlabPlacementThickness);
	}
	if (bFloorSlabPlacementIsFoundation)
	{
		DropLocation = SnapWorldLocationToIntegerBuildingXYCoordinates(ActiveBuilding.Get(), DropLocation);
	}
	FloorSlabPlacementWorldLocation = DropLocation;
	FloorSlabPlacementWorldRotation = DropRotation;
	bFloorSlabPlacementHasRoomFillAnchor = bHasRoomFillAnchor;
	FloorSlabPlacementRoomFillAnchorWallGuid = RoomFillAnchorWallGuid;
	FloorSlabPlacementRoomFillAnchorWallSide = RoomFillAnchorWallSide;
	FloorSlabPlacementFloorIndex = ResolvedFloorIndex;
	FloorSlabPlacementFloorRole = ResolvedFloorRole;

	AEHB_FloorSlab* PreviewActor = PreviewFloorSlabActor.Get();
	if (!PreviewActor)
	{
		PreviewActor = SpawnFloorSlabActor(
			ActiveBuilding.Get(),
			FloorSlabPlacementWorldLocation,
			FloorSlabPlacementWorldRotation,
			bFloorSlabPlacementIsFoundation,
			FloorSlabPlacementSize,
			FloorSlabPlacementThickness,
			true);
		PreviewFloorSlabActor = PreviewActor;
	}
	else
	{
		const FTransform WorldTransform(FloorSlabPlacementWorldRotation, FloorSlabPlacementWorldLocation);
		const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ActiveBuilding->GetActorTransform());
		PreviewActor->ConfigureDefaultSlab(
			ActiveBuilding.Get(),
			LocalTransform,
			FloorSlabPlacementSize,
			FloorSlabPlacementThickness,
			bFloorSlabPlacementIsFoundation);
	}

	if (PreviewActor && bFloorSlabPlacementIsFoundation)
	{
		PreviewActor->SnapFoundationBottomToGround();
		PreviewActor->RebuildSlabMesh();
		FloorSlabPlacementWorldLocation = PreviewActor->GetActorLocation();
	}

	if (PreviewActor)
	{
		PreviewActor->bHasRoomFillAnchor = bFloorSlabPlacementHasRoomFillAnchor;
		PreviewActor->RoomFillAnchorWallGuid = FloorSlabPlacementRoomFillAnchorWallGuid;
		PreviewActor->RoomFillAnchorWallSide = FloorSlabPlacementRoomFillAnchorWallSide;
	}

	return PreviewActor != nullptr;
}

bool FEasyHouseEditorMode::CommitFloorSlabPlacement()
{
	if (!bFloorSlabPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	const FScopedTransaction Transaction(bFloorSlabPlacementIsFoundation
		? LOCTEXT("CreateFoundationByPlacementTransaction", "Create Foundation Slab")
		: LOCTEXT("CreateFloorSlabByPlacementTransaction", "Create Floor Slab"));

	AEHB_FloorSlab* FloorSlab = SpawnFloorSlabActor(
		ActiveBuilding.Get(),
		FloorSlabPlacementWorldLocation,
		FloorSlabPlacementWorldRotation,
		bFloorSlabPlacementIsFoundation,
		FloorSlabPlacementSize,
		FloorSlabPlacementThickness,
		false);

	if (FloorSlab)
	{
		FloorSlab->bHasRoomFillAnchor = bFloorSlabPlacementHasRoomFillAnchor;
		FloorSlab->RoomFillAnchorWallGuid = FloorSlabPlacementRoomFillAnchorWallGuid;
		FloorSlab->RoomFillAnchorWallSide = FloorSlabPlacementRoomFillAnchorWallSide;
		FloorSlab->SetFloorAssignment(FloorSlabPlacementFloorIndex, FloorSlabPlacementFloorRole);
	}
	CancelFloorSlabPlacement();
	SelectFloorSlab(FloorSlab);
	if (FloorSlab && GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(FloorSlab, true, true, true);
	}
	RequestViewportMouseRestore();
	return FloorSlab != nullptr;
}

AEHB_FloorSlab* FEasyHouseEditorMode::SpawnFloorSlabActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, bool bIsFoundation, float InSize, float InThickness, bool bPreview)
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Building;
	SpawnParams.ObjectFlags |= RF_Transactional;
	if (bPreview)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AEHB_FloorSlab* FloorSlab = Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(
		AEHB_FloorSlab::StaticClass(),
		WorldLocation,
		WorldRotation,
		SpawnParams);
	if (!FloorSlab)
	{
		return nullptr;
	}

	FloorSlab->Modify();
	FloorSlab->SetActorEnableCollision(!bPreview);
	const FTransform WorldTransform(WorldRotation, WorldLocation);
	FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
	if (bIsFoundation)
	{
		LocalTransform.SetLocation(RoundBuildingLocalXYCoordinates(LocalTransform.GetLocation()));
	}
	FloorSlab->ConfigureDefaultSlab(Building, LocalTransform, InSize, InThickness, bIsFoundation);
	if (bIsFoundation)
	{
		FloorSlab->SnapFoundationBottomToGround();
		FloorSlab->RebuildSlabMesh();
	}
#if WITH_EDITOR
	FloorSlab->SetActorLabel(bPreview
		? TEXT("EHB_FloorSlabPreview")
		: (bIsFoundation ? TEXT("EHB_FoundationSlab") : TEXT("EHB_FloorSlab")));
#endif
	return FloorSlab;
}

void FEasyHouseEditorMode::BeginFloorPlacement(AEHBBuildingActorBase* Building, float InSize)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid())
	{
		return;
	}

	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	ClearWallSelection();
	ClearStairSelection();

	bFloorPlacementActive = true;
	FloorPlacementSize = FMath::Max(10.0f, InSize);
	FloorPlacementWorldRotation = FRotator(0.0f, ActiveBuilding->GetActorRotation().Yaw, 0.0f);
	FloorPlacementFloorIndex = 1;
	ResetRightMouseNavigation();
	UpdateFloorPlacement();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelFloorPlacement()
{
	if (AEHB_Floor* PreviewActor = PreviewFloorActor.Get())
	{
		PreviewActor->Destroy();
	}

	PreviewFloorActor.Reset();
	bFloorPlacementActive = false;
	FloorPlacementWorldLocation = FVector::ZeroVector;
	FloorPlacementWorldRotation = FRotator::ZeroRotator;
	FloorPlacementFloorIndex = 1;
	ResetRightMouseNavigation();
}

bool FEasyHouseEditorMode::UpdateFloorPlacement()
{
	if (!bFloorPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	FVector DropLocation = Building->GetActorLocation();
	AEHBElementActorBase* SupportElement = nullptr;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
	{
		SupportElement = ResolveActorFromHit<AEHBElementActorBase>(HitResult);
		if (SupportElement
			&& SupportElement->OwningBuilding == Building
			&& SupportElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport))
			&& !SupportElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)))
		{
			FVector BuildingLocalPoint = Building->GetActorTransform().InverseTransformPosition(HitResult.ImpactPoint);
			const FBox SupportBounds = SupportElement->GetBuildingLocalBounds();
			if (SupportBounds.IsValid)
			{
				BuildingLocalPoint.Z = SupportBounds.Max.Z;
			}
			DropLocation = Building->GetActorTransform().TransformPosition(BuildingLocalPoint);
		}
		else
		{
			SupportElement = nullptr;
			DropLocation = HitResult.ImpactPoint;
		}
	}
	else if (GCurrentLevelEditingViewportClient)
	{
		GetViewportDropLocation(GCurrentLevelEditingViewportClient, DropLocation);
	}

	FloorPlacementWorldLocation = DropLocation;
	FloorPlacementWorldRotation = FRotator(0.0f, Building->GetActorRotation().Yaw, 0.0f);
	FloorPlacementFloorIndex = SupportElement
		? ResolveFloorFinishIndexFromSupportElement(SupportElement, Building, DropLocation)
		: 1;

	AEHB_Floor* PreviewActor = PreviewFloorActor.Get();
	if (!PreviewActor)
	{
		PreviewActor = SpawnFloorActor(
			Building,
			FloorPlacementWorldLocation,
			FloorPlacementWorldRotation,
			FloorPlacementSize,
			FloorPlacementFloorIndex,
			true);
		PreviewFloorActor = PreviewActor;
	}
	else
	{
		const FTransform WorldTransform(FloorPlacementWorldRotation, FloorPlacementWorldLocation);
		const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
		PreviewActor->ConfigureDefaultFloor(Building, LocalTransform, FloorPlacementSize, FloorPlacementFloorIndex);
		PreviewActor->SetActorEnableCollision(false);
	}

	return PreviewActor != nullptr;
}

bool FEasyHouseEditorMode::CommitFloorPlacement()
{
	if (!bFloorPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateFloorByPlacementTransaction", "Create Floor Finish"));
	AEHB_Floor* Floor = SpawnFloorActor(
		ActiveBuilding.Get(),
		FloorPlacementWorldLocation,
		FloorPlacementWorldRotation,
		FloorPlacementSize,
		FloorPlacementFloorIndex,
		false);

	CancelFloorPlacement();
	SelectFloor(Floor);
	if (Floor && GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Floor, true, true, true);
	}
	RequestViewportMouseRestore();
	return Floor != nullptr;
}

AEHB_Floor* FEasyHouseEditorMode::SpawnFloorActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, float InSize, int32 InFloorIndex, bool bPreview)
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Building;
	SpawnParams.ObjectFlags |= RF_Transactional;
	if (bPreview)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AEHB_Floor* Floor = Building->GetWorld()->SpawnActor<AEHB_Floor>(
		AEHB_Floor::StaticClass(),
		WorldLocation,
		WorldRotation,
		SpawnParams);
	if (!Floor)
	{
		return nullptr;
	}

	Floor->Modify();
	Floor->SetActorEnableCollision(!bPreview);
	Floor->bEnableEditorCollision = false;
	if (bPreview)
	{
		Floor->bEnableRuntimeCollision = false;
	}

	const FTransform WorldTransform(WorldRotation, WorldLocation);
	const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
	Floor->ConfigureDefaultFloor(Building, LocalTransform, InSize, InFloorIndex);
#if WITH_EDITOR
	Floor->SetActorLabel(bPreview ? TEXT("EHB_FloorPreview") : TEXT("EHB_Floor"));
#endif
	return Floor;
}

void FEasyHouseEditorMode::BeginRoofPlacement(
	AEHBBuildingActorBase* Building,
	float InLength,
	float InWidth,
	float InPitchDegrees,
	float InThickness,
	float InEaveOffset,
	bool bInGenerateRidge,
	bool bInGenerateEaves,
	bool bInGenerateGableRakes,
	bool bInGenerateGableEndWalls,
	float InGableEndWallBoundaryInset,
	bool bInUseHipRoof,
	bool bInUseHalfHipRoof)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid())
	{
		return;
	}

	ClearFloorSlabSelection();
	ClearRoofSelection();
	ClearWallSelection();
	ClearStairSelection();
	bRoofPlacementActive = true;
	RoofPlacementLength = FMath::Max(10.0f, InLength);
	RoofPlacementWidth = FMath::Max(10.0f, InWidth);
	RoofPlacementPitchDegrees = FMath::Clamp(InPitchDegrees, 1.0f, 80.0f);
	RoofPlacementThickness = FMath::Max(1.0f, InThickness);
	RoofPlacementEaveOffset = FMath::Clamp(InEaveOffset, -200.0f, 500.0f);
	bRoofPlacementGenerateRidge = bInGenerateRidge;
	bRoofPlacementGenerateEaves = bInGenerateEaves;
	bRoofPlacementGenerateGableRakes = bInGenerateGableRakes;
	bRoofPlacementGenerateGableEndWalls = bInGenerateGableEndWalls;
	RoofPlacementGableEndWallBoundaryInset = FMath::Max(0.0f, InGableEndWallBoundaryInset);
	bRoofPlacementUseHipRoof = bInUseHipRoof || bInUseHalfHipRoof;
	bRoofPlacementUseHalfHipRoof = bInUseHalfHipRoof;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[EHB RoofCreate] BeginRoofPlacement building=%s useHip=%d useHalfHip=%d length=%.2f width=%.2f pitch=%.2f thickness=%.2f eave=%.2f"),
		*ActiveBuilding->GetName(),
		bRoofPlacementUseHipRoof ? 1 : 0,
		bRoofPlacementUseHalfHipRoof ? 1 : 0,
		RoofPlacementLength,
		RoofPlacementWidth,
		RoofPlacementPitchDegrees,
		RoofPlacementThickness,
		RoofPlacementEaveOffset);
	RoofPlacementFittedFootprint.Reset();
	bRoofPlacementFitsFloorSlab = false;
	RoofPlacementFloorIndex = 1;
	RoofPlacementWorldRotation = FRotator(0.0f, ActiveBuilding->GetActorRotation().Yaw, 0.0f);
	ResetRightMouseNavigation();
	UpdateRoofPlacement();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelRoofPlacement()
{
	if (AEHBGableRoof* PreviewActor = PreviewRoofActor.Get())
	{
		PreviewActor->Destroy();
	}

	PreviewRoofActor.Reset();
	bRoofPlacementActive = false;
	RoofPlacementFittedFootprint.Reset();
	bRoofPlacementFitsFloorSlab = false;
	RoofPlacementFloorIndex = 1;
	bRoofPlacementUseHipRoof = false;
	bRoofPlacementUseHalfHipRoof = false;
	RoofPlacementWorldLocation = FVector::ZeroVector;
	RoofPlacementWorldRotation = FRotator::ZeroRotator;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ApplyRoofPlacementSettings(AEHBGableRoof* Roof) const
{
	if (!Roof)
	{
		return;
	}

	Roof->Length = FMath::Max(1.0f, RoofPlacementLength);
	Roof->Width = FMath::Max(1.0f, RoofPlacementWidth);
	Roof->PitchDegrees = FMath::Clamp(RoofPlacementPitchDegrees, 1.0f, 89.0f);
	Roof->Thickness = FMath::Max(0.1f, RoofPlacementThickness);
	Roof->EaveOffset = FMath::Max(0.0f, RoofPlacementEaveOffset);
	Roof->RidgeOffsetRatio = 0.0f;
	Roof->AxisMode = bRoofPlacementUseHipRoof && RoofPlacementWidth > RoofPlacementLength
		? EEHBRoofAxisMode::RidgeAlongY
		: EEHBRoofAxisMode::RidgeAlongX;
	Roof->bGenerateRidge = bRoofPlacementGenerateRidge;
	Roof->bGenerateEaves = bRoofPlacementGenerateEaves;
	Roof->bGenerateGableRakes = bRoofPlacementGenerateGableRakes;
	Roof->bGenerateGableEndWalls = bRoofPlacementUseHipRoof ? false : bRoofPlacementGenerateGableEndWalls;
	Roof->GableEndWallBoundaryInset = FMath::Max(0.0f, RoofPlacementGableEndWallBoundaryInset);
	if (AEHBHipRoof* HipRoof = Cast<AEHBHipRoof>(Roof))
	{
		HipRoof->bHalfHipRoof = bRoofPlacementUseHalfHipRoof;
		HipRoof->ElementName = bRoofPlacementUseHalfHipRoof ? TEXT("Half Hip Roof") : TEXT("Hip Roof");
	}
	else
	{
		Roof->ElementName = TEXT("Gable Roof");
	}
}

bool FEasyHouseEditorMode::UpdateRoofPlacement()
{
	if (!bRoofPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	FVector DropLocation = ActiveBuilding->GetActorLocation();
	RoofPlacementFloorIndex = 1;
	RoofPlacementFittedFootprint.Reset();
	bRoofPlacementFitsFloorSlab = false;
	RoofPlacementWorldRotation = FRotator(0.0f, ActiveBuilding->GetActorRotation().Yaw, 0.0f);
	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
	{
		DropLocation = HitResult.ImpactPoint;
		if (AEHB_FloorSlab* HitFloorSlab = ResolveActorFromHit<AEHB_FloorSlab>(HitResult);
			HitFloorSlab
			&& HitFloorSlab->OwningBuilding == ActiveBuilding.Get()
			&& HitFloorSlab->LocalTopPolygon.Num() >= 3)
		{
			const FTransform SlabTransform = HitFloorSlab->GetActorTransform();
			DropLocation = SlabTransform.TransformPosition(FVector(0.0f, 0.0f, HitFloorSlab->GetTopZ()));
			RoofPlacementWorldRotation = FRotator(0.0f, SlabTransform.Rotator().Yaw, 0.0f);
			RoofPlacementFloorIndex = FMath::Max(1, HitFloorSlab->FloorIndex);
		}
		if (const AEHBElementActorBase* HitElement = Cast<AEHBElementActorBase>(HitResult.GetActor()))
		{
			RoofPlacementFloorIndex = FMath::Max(1, HitElement->FloorIndex);
		}
	}
	else if (GCurrentLevelEditingViewportClient)
	{
		GetViewportDropLocation(GCurrentLevelEditingViewportClient, DropLocation);
	}
	RoofPlacementWorldLocation = DropLocation;

	AEHBGableRoof* PreviewActor = PreviewRoofActor.Get();
	if (!PreviewActor)
	{
		PreviewActor = SpawnRoofActor(
			ActiveBuilding.Get(),
			RoofPlacementWorldLocation,
			RoofPlacementWorldRotation,
			true);
		PreviewRoofActor = PreviewActor;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[EHB RoofCreate] preview roof spawn result useHip=%d useHalfHip=%d actor=%s"),
			bRoofPlacementUseHipRoof ? 1 : 0,
			bRoofPlacementUseHalfHipRoof ? 1 : 0,
			PreviewActor ? *PreviewActor->GetName() : TEXT("None"));
	}
	else
	{
		const FTransform WorldTransform(RoofPlacementWorldRotation, RoofPlacementWorldLocation);
		const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ActiveBuilding->GetActorTransform());
		PreviewActor->AttachToBuilding(ActiveBuilding.Get(), LocalTransform);
		ApplyRoofPlacementSettings(PreviewActor);
		const bool bPreviewRebuilt = PreviewActor->RebuildRoofMesh();
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[EHB RoofCreate] preview roof rebuild useHip=%d useHalfHip=%d actor=%s success=%d"),
			bRoofPlacementUseHipRoof ? 1 : 0,
			bRoofPlacementUseHalfHipRoof ? 1 : 0,
			*PreviewActor->GetName(),
			bPreviewRebuilt ? 1 : 0);
	}

	return PreviewActor != nullptr;
}

bool FEasyHouseEditorMode::CommitRoofPlacement()
{
	if (!bRoofPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRoofByPlacementTransaction", "Create Roof"));
	AEHBGableRoof* Roof = SpawnRoofActor(
		ActiveBuilding.Get(),
		RoofPlacementWorldLocation,
		RoofPlacementWorldRotation,
		false);
	if (Roof)
	{
		Roof->SetFloorAssignment(RoofPlacementFloorIndex, EEHBBuildingFloorElementRole::Roof);
	}

	CancelRoofPlacement();
	SelectRoof(Roof);
	if (Roof && GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Roof, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}
	RequestViewportMouseRestore();
	return Roof != nullptr;
}

AEHBGableRoof* FEasyHouseEditorMode::SpawnRoofActor(
	AEHBBuildingActorBase* Building,
	const FVector& WorldLocation,
	const FRotator& WorldRotation,
	bool bPreview)
{
	if (!Building || !Building->GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EHB RoofCreate] SpawnRoofActor aborted: invalid building/world"));
		return nullptr;
	}

	TSubclassOf<AEHBGableRoof> RoofClass = bRoofPlacementUseHipRoof
		? AEHBHipRoof::StaticClass()
		: AEHBGableRoof::StaticClass();
	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	if (bRoofPlacementUseHipRoof && ToolsetSettings && !ToolsetSettings->HipRoofActorClass.IsNull())
	{
		if (UClass* LoadedRoofClass = ToolsetSettings->HipRoofActorClass.LoadSynchronous())
		{
			if (LoadedRoofClass->IsChildOf(AEHBHipRoof::StaticClass()))
			{
				RoofClass = LoadedRoofClass;
			}
		}
	}
	else if (ToolsetSettings && !ToolsetSettings->GableRoofActorClass.IsNull())
	{
		if (UClass* LoadedRoofClass = ToolsetSettings->GableRoofActorClass.LoadSynchronous())
		{
			if (LoadedRoofClass->IsChildOf(AEHBGableRoof::StaticClass()))
			{
				RoofClass = LoadedRoofClass;
			}
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Building;
	SpawnParams.ObjectFlags |= RF_Transactional;
	if (bPreview)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AEHBGableRoof* Roof = Building->GetWorld()->SpawnActor<AEHBGableRoof>(
		RoofClass,
		WorldLocation,
		WorldRotation,
		SpawnParams);
	if (!Roof)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[EHB RoofCreate] SpawnActor failed useHip=%d useHalfHip=%d class=%s preview=%d"),
			bRoofPlacementUseHipRoof ? 1 : 0,
			bRoofPlacementUseHalfHipRoof ? 1 : 0,
			RoofClass ? *RoofClass->GetName() : TEXT("None"),
			bPreview ? 1 : 0);
		return nullptr;
	}

	Roof->Modify();
	Roof->SetActorEnableCollision(!bPreview);
	ApplyRoofPlacementSettings(Roof);
	const FTransform WorldTransform(WorldRotation, WorldLocation);
	const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
	Roof->AttachToBuilding(Building, LocalTransform);
	Roof->SetFloorAssignment(RoofPlacementFloorIndex, EEHBBuildingFloorElementRole::Roof);
	const bool bRebuilt = Roof->RebuildRoofMesh();
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[EHB RoofCreate] SpawnRoofActor result useHip=%d useHalfHip=%d class=%s actor=%s preview=%d rebuilt=%d location=%s"),
		bRoofPlacementUseHipRoof ? 1 : 0,
		bRoofPlacementUseHalfHipRoof ? 1 : 0,
		RoofClass ? *RoofClass->GetName() : TEXT("None"),
		*Roof->GetName(),
		bPreview ? 1 : 0,
		bRebuilt ? 1 : 0,
		*WorldLocation.ToCompactString());
#if WITH_EDITOR
	Roof->SetActorLabel(bRoofPlacementUseHalfHipRoof
		? (bPreview ? TEXT("EHB_HalfHipRoofPreview") : TEXT("EHB_HalfHipRoof"))
		: bRoofPlacementUseHipRoof
			? (bPreview ? TEXT("EHB_HipRoofPreview") : TEXT("EHB_HipRoof"))
			: (bPreview ? TEXT("EHB_GableRoofPreview") : TEXT("EHB_GableRoof")));
#endif
	return Roof;
}

void FEasyHouseEditorMode::BeginStairPlacement(
	AEHBBuildingActorBase* Building,
	float InStairHeight,
	float InStairWidth,
	float InTreadDepth,
	bool bInGenerateTreads,
	bool bInFillRisers,
	bool bInFillBottomPart,
	bool bInGenerateSides,
	bool bInGenerateSideGuards,
	bool bInGenerateRailing,
	bool bInGenerateLeftRailing,
	bool bInGenerateRightRailing,
	int32 InRailingStepsPerPost,
	float InRailingEdgeInset,
	float InRailingPostForwardOffset)
{
	CancelWallCreation();
	CancelRailingCreation();
	CancelDoorWindowPlacement();
	CancelWallSurfacePlacement();
	CancelPillarMeshPlacement();
	CancelRailingMeshPlacement();
	CancelFloorSlabPlacement();
	CancelFloorPlacement();
	CancelRoofPlacement();
	CancelStairPlacement();
	SetActiveBuilding(Building);

	if (!ActiveBuilding.IsValid())
	{
		return;
	}

	bStairPlacementActive = true;
	StairPlacementHeight = FMath::Max(1.0f, InStairHeight);
	StairPlacementWidth = FMath::Max(1.0f, InStairWidth);
	StairPlacementTreadDepth = FMath::Max(1.0f, InTreadDepth);
	bStairPlacementGenerateTreads = bInGenerateTreads;
	bStairPlacementFillRisers = bInFillRisers;
	bStairPlacementFillBottomPart = bInFillBottomPart;
	bStairPlacementGenerateSides = bInGenerateSides;
	bStairPlacementGenerateSideGuards = bInGenerateSideGuards;
	bStairPlacementGenerateRailing = bInGenerateRailing;
	bStairPlacementGenerateLeftRailing = bInGenerateLeftRailing;
	bStairPlacementGenerateRightRailing = bInGenerateRightRailing;
	StairPlacementRailingStepsPerPost = FMath::Max(1, InRailingStepsPerPost);
	StairPlacementRailingEdgeInset = FMath::Max(0.0f, InRailingEdgeInset);
	StairPlacementRailingPostForwardOffset = FMath::Clamp(InRailingPostForwardOffset, -10000.0f, 10000.0f);
	StairPlacementResolvedHeight = StairPlacementHeight;
	bStairPlacementUseAutoBottom = false;
	StairPlacementAutoBottomWorldLocation = FVector::ZeroVector;
	const FRotator BuildingRotation = ActiveBuilding->GetActorRotation();
	StairPlacementWorldRotation = FRotator(0.0f, BuildingRotation.Yaw, 0.0f);
	ResetRightMouseNavigation();
	UpdateStairPlacement();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	RestoreViewportMouse();
}

void FEasyHouseEditorMode::CancelStairPlacement()
{
	ClearPreviewStairRailings();

	if (AEHB_Stair* PreviewActor = PreviewStairActor.Get())
	{
		PreviewActor->Destroy();
	}

	PreviewStairActor.Reset();
	bStairPlacementActive = false;
	StairPlacementResolvedHeight = StairPlacementHeight;
	bStairPlacementUseAutoBottom = false;
	StairPlacementAutoBottomWorldLocation = FVector::ZeroVector;
	StairPlacementWorldLocation = FVector::ZeroVector;
	StairPlacementWorldRotation = FRotator::ZeroRotator;
	ResetRightMouseNavigation();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearPreviewStairRailings()
{
	if (AEHB_Railing* PreviewLeftRailing = PreviewStairLeftRailingActor.Get())
	{
		PreviewLeftRailing->Destroy();
	}
	if (AEHB_Railing* PreviewRightRailing = PreviewStairRightRailingActor.Get())
	{
		PreviewRightRailing->Destroy();
	}

	PreviewStairLeftRailingActor.Reset();
	PreviewStairRightRailingActor.Reset();
}

bool FEasyHouseEditorMode::ResolveStairSidePlacementFromHit(
	const FHitResult& HitResult,
	FVector& OutWorldLocation,
	FRotator& OutWorldRotation,
	float& OutResolvedHeight,
	FVector& OutBottomWorldLocation,
	bool& bOutUseAutoBottom) const
{
	OutWorldLocation = FVector::ZeroVector;
	OutWorldRotation = FRotator::ZeroRotator;
	OutResolvedHeight = StairPlacementHeight;
	OutBottomWorldLocation = FVector::ZeroVector;
	bOutUseAutoBottom = false;

	if (!ActiveBuilding.IsValid() || !HitResult.bBlockingHit)
	{
		return false;
	}

	FEHBFloorSlabSideSnapResult SnapResult;
	const bool bResolvedSide =
		ResolvePillarTopSideSnap(HitResult, SnapResult)
		|| ResolveWallTopSideSnap(HitResult, SnapResult)
		|| ResolveExistingFloorSlabTopSideSnap(HitResult, nullptr, SnapResult);
	if (!bResolvedSide)
	{
		return false;
	}

	const AEHBElementActorBase* AnchorElement = Cast<AEHBElementActorBase>(SnapResult.AnchorActor.Get());
	if (!AnchorElement
		|| AnchorElement->OwningBuilding != ActiveBuilding.Get()
		|| AnchorElement->IsActorBeingDestroyed())
	{
		return false;
	}

	const FVector OutwardDirection = GetSafeHorizontalVector(SnapResult.WorldOutward);
	if (OutwardDirection.IsNearlyZero())
	{
		return false;
	}

	const float PlatformSurfaceZ = SnapResult.WorldTopEdgePoint.Z;
	const FVector TopEdgeWorldLocation = SnapResult.WorldTopEdgePoint;
	float ResolvedHeight = StairPlacementHeight;
	float TopStepHeight = 0.0f;
	FVector BottomWorldLocation = FVector::ZeroVector;
	const bool bUseAutoBottom = FindStairAutoBottomPlacement(
		TopEdgeWorldLocation,
		OutwardDirection,
		PlatformSurfaceZ,
		BottomWorldLocation,
		ResolvedHeight,
		TopStepHeight);

	if (!bUseAutoBottom)
	{
		int32 StepCount = 0;
		if (!CalculatePlacementStairStepsForHeight(StairPlacementHeight, StepCount, TopStepHeight))
		{
			return false;
		}
		ResolvedHeight = StairPlacementHeight;
	}

	OutWorldLocation = TopEdgeWorldLocation;
	OutWorldLocation.Z = PlatformSurfaceZ - TopStepHeight - ResolvedHeight;
	OutWorldRotation = FRotator(0.0f, OutwardDirection.Rotation().Yaw, 0.0f);
	OutResolvedHeight = FMath::Max(1.0f, ResolvedHeight);
	OutBottomWorldLocation = BottomWorldLocation;
	bOutUseAutoBottom = bUseAutoBottom;
	return true;
}

bool FEasyHouseEditorMode::FindStairAutoBottomPlacement(
	const FVector& TopEdgeWorldLocation,
	const FVector& OutwardDirection,
	float PlatformSurfaceZ,
	FVector& OutBottomWorldLocation,
	float& OutResolvedHeight,
	float& OutTopStepHeight) const
{
	OutBottomWorldLocation = FVector::ZeroVector;
	OutResolvedHeight = StairPlacementHeight;
	OutTopStepHeight = 0.0f;

	UWorld* World = ActiveBuilding.IsValid() ? ActiveBuilding->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	const FVector TraceDirection = GetSafeHorizontalVector(OutwardDirection);
	if (TraceDirection.IsNearlyZero())
	{
		return false;
	}

	const FEHBStairData Defaults;
	const float MinStepHeight = FMath::Max(1.0f, Defaults.MinStepHeight);
	const float SafeTreadDepth = FMath::Max(1.0f, StairPlacementTreadDepth);
	int32 HintStepCount = 0;
	float HintStepHeight = 0.0f;
	CalculatePlacementStairStepsForHeight(StairPlacementHeight, HintStepCount, HintStepHeight);
	const float HintLength = CalculatePlacementStairLengthForSteps(SafeTreadDepth, FMath::Max(1, HintStepCount));
	const float MaxSearchDistance = FMath::Clamp(
		FMath::Max(FMath::Max(HintLength * 3.0f, SafeTreadDepth * 64.0f), 1200.0f),
		300.0f,
		10000.0f);
	const float SampleStep = FMath::Clamp(SafeTreadDepth * 0.5f, 10.0f, 50.0f);
	const float StartDistance = FMath::Max(5.0f, SafeTreadDepth * 0.25f);
	const float MaxVerticalDrop = FMath::Clamp(
		FMath::Max(FMath::Max(StairPlacementHeight * 6.0f, 3000.0f), FMath::Abs(PlatformSurfaceZ - ActiveBuilding->GetActorLocation().Z) + 1000.0f),
		1000.0f,
		20000.0f);
	const float TraceStartHeight = FMath::Max(100.0f, MinStepHeight * 4.0f);
	const float MinAutoDrop = FMath::Max(2.0f, MinStepHeight * 1.5f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_StairAutoBottomTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;
	if (AActor* PreviewActor = PreviewStairActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}

	auto TraceDownAtDistance = [&](float Distance, FHitResult& OutSupportHit) -> bool
	{
		const FVector TracePoint = TopEdgeWorldLocation + TraceDirection * Distance;
		const FVector RayStart(TracePoint.X, TracePoint.Y, PlatformSurfaceZ + TraceStartHeight);
		const FVector RayEnd(TracePoint.X, TracePoint.Y, PlatformSurfaceZ - MaxVerticalDrop);
		TArray<FHitResult> Hits;
		if (!World->LineTraceMultiByChannel(Hits, RayStart, RayEnd, ECC_Visibility, QueryParams))
		{
			return false;
		}

		for (const FHitResult& SupportHit : Hits)
		{
			if (!SupportHit.bBlockingHit || !IsLikelyTopSurfaceHit(SupportHit))
			{
				continue;
			}

			const float PlatformDrop = PlatformSurfaceZ - SupportHit.ImpactPoint.Z;
			if (PlatformDrop < MinAutoDrop)
			{
				continue;
			}

			OutSupportHit = SupportHit;
			return true;
		}

		return false;
	};

	bool bHasBestCandidate = false;
	float BestScore = TNumericLimits<float>::Max();
	float BestExpectedRun = 0.0f;
	float BestResolvedHeight = StairPlacementHeight;
	float BestStepHeight = 0.0f;
	FVector BestBottomWorldLocation = FVector::ZeroVector;

	const int32 SampleCount = FMath::Clamp(FMath::CeilToInt((MaxSearchDistance - StartDistance) / SampleStep), 1, 256);
	for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
	{
		const float Distance = FMath::Min(MaxSearchDistance, StartDistance + SampleStep * static_cast<float>(SampleIndex));
		FHitResult SupportHit;
		if (!TraceDownAtDistance(Distance, SupportHit))
		{
			continue;
		}

		const float PlatformDrop = PlatformSurfaceZ - SupportHit.ImpactPoint.Z;
		float CandidateResolvedHeight = 0.0f;
		float CandidateStepHeight = 0.0f;
		int32 CandidateVisibleStepCount = 0;
		if (!ResolvePlacementStairHeightFromPlatformDrop(
			PlatformDrop,
			CandidateResolvedHeight,
			CandidateStepHeight,
			CandidateVisibleStepCount))
		{
			continue;
		}

		const float ExpectedRun = CalculatePlacementStairLengthForSteps(SafeTreadDepth, CandidateVisibleStepCount);
		const float RunError = FMath::Abs(Distance - ExpectedRun);
		const float Score = RunError + FMath::Max(0.0f, Distance - ExpectedRun) * 0.05f;
		if (!bHasBestCandidate || Score < BestScore)
		{
			bHasBestCandidate = true;
			BestScore = Score;
			BestExpectedRun = ExpectedRun;
			BestResolvedHeight = CandidateResolvedHeight;
			BestStepHeight = CandidateStepHeight;
			BestBottomWorldLocation = SupportHit.ImpactPoint;
		}
	}

	if (!bHasBestCandidate)
	{
		return false;
	}

	FHitResult RefinedSupportHit;
	if (BestExpectedRun > 0.0f
		&& BestExpectedRun <= MaxSearchDistance + SampleStep
		&& TraceDownAtDistance(BestExpectedRun, RefinedSupportHit))
	{
		const float PlatformDrop = PlatformSurfaceZ - RefinedSupportHit.ImpactPoint.Z;
		float RefinedResolvedHeight = 0.0f;
		float RefinedStepHeight = 0.0f;
		int32 RefinedVisibleStepCount = 0;
		if (ResolvePlacementStairHeightFromPlatformDrop(
			PlatformDrop,
			RefinedResolvedHeight,
			RefinedStepHeight,
			RefinedVisibleStepCount)
			&& FMath::Abs(RefinedSupportHit.ImpactPoint.Z - BestBottomWorldLocation.Z) <= FMath::Max(5.0f, BestStepHeight * 0.5f))
		{
			BestResolvedHeight = RefinedResolvedHeight;
			BestStepHeight = RefinedStepHeight;
			BestBottomWorldLocation = RefinedSupportHit.ImpactPoint;
		}
	}

	OutBottomWorldLocation = BestBottomWorldLocation;
	OutResolvedHeight = FMath::Max(1.0f, BestResolvedHeight);
	OutTopStepHeight = FMath::Max(1.0f, BestStepHeight);
	return true;
}

void FEasyHouseEditorMode::ApplyCurrentStairPlacementData(AEHB_Stair* Stair) const
{
	if (!Stair)
	{
		return;
	}

	Stair->StairData.bGenerateTreads = bStairPlacementGenerateTreads;
	Stair->StairData.bFillRisers = bStairPlacementFillRisers;
	Stair->StairData.bFillBottomPart = bStairPlacementFillBottomPart;
	Stair->StairData.bGenerateSides = bStairPlacementGenerateSides;
	Stair->StairData.bGenerateSideGuards = bStairPlacementGenerateSideGuards;
	Stair->StairData.bGenerateRailing = bStairPlacementGenerateRailing;
	Stair->StairData.bGenerateLeftRailing = bStairPlacementGenerateLeftRailing;
	Stair->StairData.bGenerateRightRailing = bStairPlacementGenerateRightRailing;
	Stair->StairData.RailingStepsPerPost = FMath::Max(1, StairPlacementRailingStepsPerPost);
	Stair->StairData.RailingEdgeInset = FMath::Max(0.0f, StairPlacementRailingEdgeInset);
	Stair->StairData.RailingPostForwardOffset = FMath::Clamp(StairPlacementRailingPostForwardOffset, -10000.0f, 10000.0f);
	Stair->StairData.RailingPostWidth = RailingCreationThickness;
	Stair->StairData.RailingPostHeight = RailingCreationHeight;
	Stair->StairData.RailingRailHeight = RailingCreationHeight;
	Stair->StairData.RailingRailThickness = RailingCreationThickness;
	Stair->StairData.RailingMaxRailSegmentLength = FMath::Max(4.0f, StairPlacementTreadDepth * 0.5f);

	if (bStairPlacementUseAutoBottom)
	{
		const FVector LocalBottomLocation = Stair->GetActorTransform().InverseTransformPosition(StairPlacementAutoBottomWorldLocation);
		FVector2D BottomStepLocation(LocalBottomLocation.X, LocalBottomLocation.Y);
		if (BottomStepLocation.SizeSquared() < 100.0f)
		{
			BottomStepLocation = BottomStepLocation.IsNearlyZero()
				? FVector2D(10.0f, 0.0f)
				: BottomStepLocation.GetSafeNormal() * 10.0f;
		}

		Stair->StairData.BottomStepLocation = BottomStepLocation;
		Stair->StairData.BottomStepYawOffset = 0.0f;
		Stair->StairData.bBottomStepControlInitialized = true;
		Stair->ResetIntermediateControlOffsets();
	}

	Stair->RebuildStairMesh();
}

bool FEasyHouseEditorMode::UpdateStairPlacement()
{
	if (!bStairPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	FVector DropLocation = ActiveBuilding->GetActorLocation();
	const FRotator BuildingRotation = ActiveBuilding->GetActorRotation();
	FRotator DropRotation(0.0f, BuildingRotation.Yaw, 0.0f);
	StairPlacementResolvedHeight = StairPlacementHeight;
	bStairPlacementUseAutoBottom = false;
	StairPlacementAutoBottomWorldLocation = FVector::ZeroVector;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
	{
		FVector SideSnapLocation = FVector::ZeroVector;
		FRotator SideSnapRotation = FRotator::ZeroRotator;
		float SideSnapHeight = StairPlacementHeight;
		FVector AutoBottomLocation = FVector::ZeroVector;
		bool bUseAutoBottom = false;
		if (ResolveStairSidePlacementFromHit(
			HitResult,
			SideSnapLocation,
			SideSnapRotation,
			SideSnapHeight,
			AutoBottomLocation,
			bUseAutoBottom))
		{
			DropLocation = SideSnapLocation;
			DropRotation = SideSnapRotation;
			StairPlacementResolvedHeight = SideSnapHeight;
			bStairPlacementUseAutoBottom = bUseAutoBottom;
			StairPlacementAutoBottomWorldLocation = AutoBottomLocation;
		}
		else
		{
			DropLocation = HitResult.ImpactPoint;
		}
	}
	else if (GCurrentLevelEditingViewportClient)
	{
		GetViewportDropLocation(GCurrentLevelEditingViewportClient, DropLocation);
	}
	StairPlacementWorldLocation = DropLocation;
	StairPlacementWorldRotation = DropRotation;

	AEHB_Stair* PreviewActor = PreviewStairActor.Get();
	if (!PreviewActor)
	{
		PreviewActor = SpawnStairActor(
			ActiveBuilding.Get(),
			StairPlacementWorldLocation,
			StairPlacementWorldRotation,
			true);
		PreviewStairActor = PreviewActor;
	}
	if (PreviewActor)
	{
		const FTransform WorldTransform(StairPlacementWorldRotation, StairPlacementWorldLocation);
		const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ActiveBuilding->GetActorTransform());
		PreviewActor->ConfigureDefaultStair(
			ActiveBuilding.Get(),
			LocalTransform,
			StairPlacementResolvedHeight,
			StairPlacementWidth,
			StairPlacementTreadDepth);
		ApplyCurrentStairPlacementData(PreviewActor);
		ClearPreviewStairRailings();
	}

	return PreviewActor != nullptr;
}

bool FEasyHouseEditorMode::CommitStairPlacement()
{
	if (!bStairPlacementActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateStairByPlacementTransaction", "Create Stair"));
	AEHB_Stair* Stair = SpawnStairActor(
		ActiveBuilding.Get(),
		StairPlacementWorldLocation,
		StairPlacementWorldRotation,
		false);

	CancelStairPlacement();

	if (Stair && GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Stair, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}
	return Stair != nullptr;
}

AEHB_Stair* FEasyHouseEditorMode::SpawnStairActor(
	AEHBBuildingActorBase* Building,
	const FVector& WorldLocation,
	const FRotator& WorldRotation,
	bool bPreview)
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<AEHB_Stair> StairClass = AEHB_Stair::StaticClass();
	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	if (ToolsetSettings && !ToolsetSettings->StairActorClass.IsNull())
	{
		if (UClass* LoadedStairClass = ToolsetSettings->StairActorClass.LoadSynchronous())
		{
			if (LoadedStairClass->IsChildOf(AEHB_Stair::StaticClass()))
			{
				StairClass = LoadedStairClass;
			}
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Building;
	SpawnParams.ObjectFlags |= RF_Transactional;
	if (bPreview)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AEHB_Stair* Stair = Building->GetWorld()->SpawnActor<AEHB_Stair>(
		StairClass,
		WorldLocation,
		WorldRotation,
		SpawnParams);
	if (!Stair)
	{
		return nullptr;
	}

	Stair->Modify();
	Stair->SetActorEnableCollision(!bPreview);
	const FTransform WorldTransform(WorldRotation, WorldLocation);
	const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
	Stair->ConfigureDefaultStair(
		Building,
		LocalTransform,
		StairPlacementResolvedHeight,
		StairPlacementWidth,
		StairPlacementTreadDepth);
	ApplyCurrentStairPlacementData(Stair);
#if WITH_EDITOR
	Stair->SetActorLabel(bPreview ? TEXT("EHB_StairPreview") : TEXT("EHB_Stair"));
#endif
	return Stair;
}

AEHB_Railing* FEasyHouseEditorMode::SpawnStairRailingActor(
	AEHBBuildingActorBase* Building,
	AEHB_Stair* Stair,
	EEHBRailingSide Side,
	bool bPreview)
{
	if (!Building || !Stair || !Building->GetWorld())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Railing> RailingClass = AEHB_Railing::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->RailingActorClass.IsNull())
	{
		if (UClass* LoadedRailingClass = ToolsetSettings->RailingActorClass.LoadSynchronous())
		{
			if (LoadedRailingClass->IsChildOf(AEHB_Railing::StaticClass()))
			{
				RailingClass = LoadedRailingClass;
			}
		}
	}

	const FName RailingBaseName(
		Side == EEHBRailingSide::Left
			? (bPreview ? TEXT("EHB_StairRailingLeftPreview") : TEXT("EHB_StairRailing_Left"))
			: (bPreview ? TEXT("EHB_StairRailingRightPreview") : TEXT("EHB_StairRailing_Right")));
	const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), RailingClass, RailingBaseName);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = Building;
	SpawnParams.OverrideLevel = Building->GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;
	if (bPreview)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}

	AEHB_Railing* Railing = Building->GetWorld()->SpawnActor<AEHB_Railing>(
		RailingClass,
		Stair->GetActorTransform(),
		SpawnParams);
	if (!Railing)
	{
		return nullptr;
	}

	if (!bPreview)
	{
		Building->Modify();
	}
	Railing->SetFlags(RF_Transactional);
	if (Railing)
	{
		Railing->Modify();
	}
	if (Stair)
	{
		Stair->Modify();
	}
	Railing->SetActorEnableCollision(!bPreview);
	Railing->ElementName = ActorName;
	ApplyCurrentStairRailingPlacementData(Railing, Stair, Side);

#if WITH_EDITOR
	Railing->SetActorLabel(ActorName.ToString());
#endif
	return Railing;
}

void FEasyHouseEditorMode::ApplyCurrentStairRailingPlacementData(
	AEHB_Railing* Railing,
	AEHB_Stair* Stair,
	EEHBRailingSide Side) const
{
	if (!Railing || !Stair)
	{
		return;
	}

	AEHBBuildingActorBase* Building = Stair->OwningBuilding.Get();
	if (!Building)
	{
		Building = ActiveBuilding.Get();
	}
	if (!Building)
	{
		return;
	}

	const int32 SafeStepsPerPost = FMath::Max(1, StairPlacementRailingStepsPerPost);
	const float SafeTreadDepth = FMath::Max(1.0f, StairPlacementTreadDepth);
	const float SafeRailSegmentLength = FMath::Max(4.0f, SafeTreadDepth * 0.5f);

	if (Railing)
	{
		Railing->Modify();
	}
	if (Stair)
	{
		Stair->Modify();
	}
	Railing->PostHeight = RailingCreationHeight;
	Railing->RailHeight = RailingCreationHeight;
	Railing->PostWidth = RailingCreationThickness;
	Railing->RailThickness = RailingCreationThickness;
	Railing->PostSpacingMode = EEHBRailingPostSpacingMode::StepAligned;
	Railing->PostSpacing = SafeTreadDepth * static_cast<float>(SafeStepsPerPost);
	Railing->StepsPerPost = SafeStepsPerPost;
	Railing->MaxRailSegmentLength = SafeRailSegmentLength;
	Railing->FillMode = EEHBRailingFillMode::PostsAndRails;
	Railing->StairSideOffset = FMath::Max(0.0f, StairPlacementRailingEdgeInset);
	Railing->StairPostBaseHeightOffset = 0.0f;
	Railing->bOmitStartPost = false;
	Railing->bOmitEndPost = false;
	Railing->ConfigureOnStair(
		Building,
		Stair,
		Side,
		FMath::Max(1, Stair->FloorIndex));
}

int32 FEasyHouseEditorMode::RebuildRailingsHostedByStair(AEHB_Stair* Stair, bool bModifyRailings) const
{
	if (!Stair || !Stair->OwningBuilding)
	{
		return 0;
	}

	Stair->EnsureElementGuid();
	AEHBBuildingActorBase* Building = Stair->OwningBuilding;
	FEHBElementQuery Query;
	Query.ElementTypes = { EEHBBuildingElementType::Railing };

	int32 RebuiltCount = 0;
	for (AEHBElementActorBase* Element : Building->QueryElements(Query))
	{
		AEHB_Railing* Railing = Cast<AEHB_Railing>(Element);
		if (!Railing
			|| Railing->PathMode != EEHBRailingPathMode::StairHosted
			|| Railing->IsActorBeingDestroyed())
		{
			continue;
		}

		const bool bHostedByStair =
			Railing->HostedStair == Stair
			|| (Railing->HostedStairGuid.IsValid() && Railing->HostedStairGuid == Stair->ElementGuid);
		if (!bHostedByStair)
		{
			continue;
		}

		if (bModifyRailings)
		{
			Railing->Modify();
		}
		Railing->HostedStair = Stair;
		Railing->HostedStairGuid = Stair->ElementGuid;
		if (Railing->RebuildRailing())
		{
			Railing->MarkPackageDirty();
			++RebuiltCount;
		}
	}

	return RebuiltCount;
}

bool FEasyHouseEditorMode::BuildCandidateRoomWallSides(AEHBBuildingActorBase* Building,const TMap<FGuid,FVector>& Positions,TArray<FEHBWallJunctionWallSides>& Sides,const TSet<FGuid>* RequestedWallGuids)
{
	Sides.Reset();if(!Building)return false;
#if WITH_DEV_AUTOMATION_TESTS
	++EHBNodeMoveTestHooks::CandidateJunctionSolveCount;
#endif
	FEHBPreparedWallNodeDefinitions Candidate;
	if(Building->PreparedWallNodeDefinitions.Version==1&&UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status==TEXT("AlreadyPrepared"))
	{
		Candidate=Building->PreparedWallNodeDefinitions;
#if WITH_DEV_AUTOMATION_TESTS
		++EHBNodeMoveTestHooks::PreparedDefinitionReuseCount;
#endif
	}
	else
	{
		// Inactive stale preparations never drive an edit or overwrite the current legacy source.
		Candidate.Version=1;
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			if(auto* Pillar=Cast<AEHB_Pillar>(Element))
			{
				if(Pillar->GetClass()!=AEHB_Pillar::StaticClass()||Pillar->ShapeType!=EEHBPillarShapeType::Polygon)return false;
				auto& N=Candidate.Nodes.AddDefaulted_GetRef();N.NodeGuid=Building->WallNodeOwnership.Version==1?Building->FindNodeForPhysicalPillar(Pillar->ElementGuid):Pillar->ElementGuid;N.LocalTransform=Pillar->GetElementLocalTransform();N.FloorIndex=Pillar->FloorIndex;N.JunctionDimensions=FVector(Pillar->Width,Pillar->Depth,Pillar->Height);
				auto& B=Candidate.PillarBindings.AddDefaulted_GetRef();B.NodeGuid=N.NodeGuid;B.PhysicalPillarGuid=Pillar->ElementGuid;
			}
			else if(auto* Wall=Cast<AEHB_Wall>(Element))
			{
				if(Wall->GetClass()!=AEHB_Wall::StaticClass()||Wall->CurveControlOffset!=0)return false;
				auto& W=Candidate.Walls.AddDefaulted_GetRef();W.WallGuid=Wall->ElementGuid;W.StartNodeGuid=Building->WallNodeOwnership.Version==1?Building->FindNodeForPhysicalPillar(Wall->StartPillarGuid):Wall->StartPillarGuid;W.EndNodeGuid=Building->WallNodeOwnership.Version==1?Building->FindNodeForPhysicalPillar(Wall->EndPillarGuid):Wall->EndPillarGuid;W.Thickness=Wall->Thickness;W.Height=Wall->Height;
			}
		}
	}
	TMap<FGuid,FGuid> NodesByPillar;
	for(const auto& Binding:Candidate.PillarBindings)NodesByPillar.Add(Binding.PhysicalPillarGuid,Binding.NodeGuid);
	TArray<FEHBNodeMoveRequest> Requests;
	for(const auto& Pair:Positions)
	{
		const FGuid NodeId=NodesByPillar.FindRef(Pair.Key);
		const auto* Node=Candidate.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeId;});if(!Node)return false;
		auto& Request=Requests.AddDefaulted_GetRef();Request.NodeGuid=NodeId;Request.ExpectedPosition=Node->LocalTransform.GetLocation();Request.TargetPosition=Pair.Value;
	}
	const auto Draft=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Candidate,Requests);if(!Draft.bSucceeded)return false;
	FName Reason;return UEHBWallTopologyLibrary::BuildPreparedWallSides(Draft.Definitions,Sides,Reason,RequestedWallGuids);
}

bool FEasyHouseEditorMode::BuildRoomSlabOutlineFromWallSides(AEHB_FloorSlab* Slab,const FEHBBuildingClosedLoop& Room,const TArray<FEHBWallJunctionWallSides>& Sides,TArray<FVector>& Polygon)
{
	Polygon.Reset();TArray<FVector> BuildingPolygon;
	return Slab && Slab->OwningBuilding
		&& BuildClosedLoopPolygonInBuildingSpace(Slab->OwningBuilding,Room,BuildingPolygon,&Sides)
		&& ConvertBuildingPolygonToFloorSlabLocal(Slab->OwningBuilding,Slab,BuildingPolygon,Polygon);
}

bool FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(AEHB_FloorSlab* Slab,const FEHBNodeRoomBoundary& Room,const FEHBWallNodeModel& Model,const TArray<FEHBWallJunctionWallSides>& Sides,TArray<FVector>& Polygon)
{
 Polygon.Reset();if(!Slab||!Slab->OwningBuilding||Room.NodeGuids.Num()<3||Room.NodeGuids.Num()!=Room.WallGuids.Num())return false;
 TArray<FVector> BuildingPolygon;
 return BuildLogicalRoomPolygon(Room,Model,Sides,BuildingPolygon)&&ConvertBuildingPolygonToFloorSlabLocal(Slab->OwningBuilding,Slab,BuildingPolygon,Polygon);
}

bool FEasyHouseEditorMode::BuildRoomSlabFollowOutline(AEHB_FloorSlab* Slab,const FEHBBuildingClosedLoop& Room,const TMap<FGuid,FVector>& Positions,TArray<FVector>& Polygon)
{
	Polygon.Reset();if(!Slab||!Slab->OwningBuilding)return false;
	if(!Positions.IsEmpty())
	{
		TArray<FEHBWallJunctionWallSides> Sides;
		TSet<FGuid> RequestedWalls;for(FGuid Id:Room.WallGuids)RequestedWalls.Add(Id);
		return BuildCandidateRoomWallSides(Slab->OwningBuilding,Positions,Sides,&RequestedWalls)&&BuildRoomSlabOutlineFromWallSides(Slab,Room,Sides,Polygon);
	}
	TArray<FVector> BuildingPolygon;
	return BuildClosedLoopPolygonInBuildingSpace(Slab->OwningBuilding,Room,BuildingPolygon)
		&& ConvertBuildingPolygonToFloorSlabLocal(Slab->OwningBuilding,Slab,BuildingPolygon,Polygon);
}

bool FEasyHouseEditorMode::FillFloorSlabRoom(AEHB_FloorSlab* FloorSlab)
{
	if(EHBFinishRegionCommand::IsIndependent(FloorSlab))
	{
		auto* B=FloorSlab->OwningBuilding.Get();FEHBSurfaceRoomCoverage Coverage;FName Status;
		if(!B->QuerySurfaceRoomCoverage(FloorSlab->ElementGuid,Coverage,Status)||Coverage.Rooms.Num()!=1)return false;
		return UEHBBuildingToolset::EditFinishRegion(B,FloorSlab->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(FloorSlab->ElementGuid),{},Coverage.Rooms[0].RoomGuid,false).bSucceeded;
	}
	if (!FloorSlab)
	{
		return false;
	}

	TArray<FVector> LocalOuterPolygon;
	TArray<FEHBFloorSlabHole> LocalHoles;
	FEHBBuildingClosedLoop FilledRoom;
	if (!BuildRoomFillPolygonsForFloorSlab(FloorSlab, LocalOuterPolygon, LocalHoles, FilledRoom))
	{
		UE_LOG(LogTemp, Warning, TEXT("EHB floor slab room fill failed: no closed room polygon contains the selected slab location."));
		return false;
	}

	const bool bRebuilt = FloorSlab->SetSlabOutline(LocalOuterPolygon,LocalHoles,true);
	if (bRebuilt)
	{
		FloorSlab->RoomFillLoopGuid=FilledRoom.LoopGuid;
		FloorSlab->RoomFillFloorIndex=FilledRoom.FloorIndex;
		FloorSlab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
		FloorSlab->MarkPackageDirty();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
	return bRebuilt;
}

bool FEasyHouseEditorMode::FillFloorRoom(AEHB_Floor* Floor)
{
	if (!Floor || !Floor->OwningBuilding) return false;
	if(EHBFinishRegionCommand::IsIndependent(Floor))
	{
		auto* B=Floor->OwningBuilding.Get();FEHBSurfaceRoomCoverage Coverage;FName Status;
		if(!B->QuerySurfaceRoomCoverage(Floor->ElementGuid,Coverage,Status)||Coverage.Rooms.Num()!=1)return false;
		return UEHBBuildingToolset::EditFinishRegion(B,Floor->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(Floor->ElementGuid),{},Coverage.Rooms[0].RoomGuid,false).bSucceeded;
	}
	AEHBBuildingActorBase* Building = Floor->OwningBuilding;
	FEHBBuildingClosedLoop RoomLoop;
	TArray<FVector> RoomPolygon;
	float SurfaceZ = 0.0f;
	if (!BuildFloorRoomPolygonInBuildingSpace(Floor, RoomLoop, RoomPolygon, SurfaceZ))
	{
		UE_LOG(LogTemp, Warning, TEXT("EHB floor fill failed: no unique room was found for the selected floor."));
		return false;
	}
	TArray<FEHBFloorSupportSurface> SupportSurfaces;
	CollectFloorSupportSurfaces(Building, SurfaceZ, SupportSurfaces);
	TArray<FEHBFloorFinishRegion> BuildingRegions;
	if (SupportSurfaces.IsEmpty())
	{
		// Preserve the existing first-floor room fallback, but never erase an
		// upper-floor finish when no support exists at its logical surface.
		if (RoomLoop.FloorIndex != 1 && Floor->FloorIndex != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("EHB floor fill failed: no structural top surface exists at this room height; previous floor retained."));
			return false;
		}
		BuildingRegions.AddDefaulted_GetRef().OuterPolygon = RoomPolygon;
	}
	else if (!AEHB_Floor::BuildFloorFinishRegionsFromSupportSurfaces(RoomPolygon, SupportSurfaces, SurfaceZ, BuildingRegions))
	{
		return false;
	}
	TArray<FEHBFloorFinishRegion> LocalRegions;
	for (const auto& BuildingRegion : BuildingRegions)
	{
		auto LocalRegion = ConvertFloorRegionFromBuildingToFloorLocal(Building, Floor, BuildingRegion);
		if (LocalRegion.OuterPolygon.Num() >= 3 && FMath::Abs(CalculateSignedAreaXY(LocalRegion.OuterPolygon)) > 1.0f)
			LocalRegions.Add(MoveTemp(LocalRegion));
	}
	// SetFloorRegions validates the whole result before replacing any mesh.
	// Room assignment and source relations only change after successful geometry.
	if (!Floor->SetFloorRegions(LocalRegions, false)) return false;
	Building->Modify();
	Floor->RoomLoopGuid = RoomLoop.LoopGuid;
	Floor->RoomFloorIndex = RoomLoop.FloorIndex;
	Floor->SetFloorAssignment(RoomLoop.FloorIndex, EEHBBuildingFloorElementRole::FloorFinish);
	if (SupportSurfaces.IsEmpty()) Floor->ClearSurfaceFinishRelations();
	else Floor->RefreshSurfaceFinishRelationsFromRoomLoop(RoomLoop);
	Floor->RecordOutlineSource(EEHBOutlineSource::RoomSupportFill);
	if (GEditor) GEditor->RedrawLevelEditingViewports();
	return true;
}

AEHB_FloorSlab* FEasyHouseEditorMode::ResolveFloorSlabFromHit(const FHitResult& HitResult) const
{
	if (AEHB_FloorSlab* FloorSlab = Cast<AEHB_FloorSlab>(HitResult.GetActor()))
	{
		return FloorSlab;
	}

	if (const UActorComponent* Component = HitResult.GetComponent())
	{
		return Cast<AEHB_FloorSlab>(Component->GetOwner());
	}

	return nullptr;
}

AEHB_Floor* FEasyHouseEditorMode::ResolveFloorFromHit(const FHitResult& HitResult) const
{
	if (AEHB_Floor* Floor = Cast<AEHB_Floor>(HitResult.GetActor()))
	{
		return Floor;
	}

	if (const UActorComponent* Component = HitResult.GetComponent())
	{
		return Cast<AEHB_Floor>(Component->GetOwner());
	}

	return nullptr;
}

AEHBGableRoof* FEasyHouseEditorMode::ResolveRoofFromHit(const FHitResult& HitResult) const
{
	if (AEHBGableRoof* Roof = Cast<AEHBGableRoof>(HitResult.GetActor()))
	{
		return Roof;
	}

	if (const UActorComponent* Component = HitResult.GetComponent())
	{
		return Cast<AEHBGableRoof>(Component->GetOwner());
	}

	return nullptr;
}

AActor* FEasyHouseEditorMode::ResolveSelectedElementForEditor() const
{
	if (AEHB_Wall* Wall = SelectedWall.Get())
	{
		return Wall;
	}
	if (AEHB_Railing* Railing = SelectedRailing.Get())
	{
		return Railing;
	}
	if (AEHB_Stair* Stair = SelectedStair.Get())
	{
		return Stair;
	}
	if (AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get())
	{
		return FloorSlab;
	}
	if (AEHB_Floor* Floor = SelectedFloor.Get())
	{
		return Floor;
	}
	if (AEHBGableRoof* Roof = SelectedRoof.Get())
	{
		return Roof;
	}

	if (!GEditor || !GEditor->GetSelectedActors())
	{
		return nullptr;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	for (AActor* Actor : SelectedActors)
	{
		if (Cast<AEHBElementActorBase>(Actor) || Cast<AEHBBuildingActorBase>(Actor))
		{
			return Actor;
		}
	}

	return nullptr;
}

void FEasyHouseEditorMode::RefreshSelectedElementEditor()
{
	AActor* SelectedActor = ResolveSelectedElementForEditor();
	if (ElementEditorSelectedActor.Get() == SelectedActor && !SelectedActor)
	{
		return;
	}

	if (!Toolkit.IsValid())
	{
		ElementEditorSelectedActor = SelectedActor;
		return;
	}

	if (FEasyHouseEditorModeToolkit* BuildingToolkit = static_cast<FEasyHouseEditorModeToolkit*>(Toolkit.Get()))
	{
		BuildingToolkit->SetSelectedElement(SelectedActor);
		if (AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
			FloorSlab
			&& SelectedActor == FloorSlab
			&& SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner)
		{
			BuildingToolkit->SetSelectedFloorSlabCorner(
				FloorSlab,
				SelectedFloorSlabHandleLoopIndex,
				SelectedFloorSlabHandleFirstIndex);
		}
		else
		{
			BuildingToolkit->SetSelectedFloorSlabCorner(nullptr, INDEX_NONE, INDEX_NONE);
		}
	}

	ElementEditorSelectedActor = SelectedActor;
}

void FEasyHouseEditorMode::SyncWallSelectionFromEditor()
{
	if (!GEditor || !GEditor->GetSelectedActors())
	{
		if (SelectedWall.IsValid() || bWallCurveControlSelected)
		{
			SelectedWall.Reset();
			bWallCurveControlSelected = false;
			ActiveWallMoveTransaction.Reset();
			RoomFloorWallDrag = {};
			ActiveWallCurveControlEditTransaction.Reset();
		}
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	AEHB_Wall* EditorSelectedWall = nullptr;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
		{
			EditorSelectedWall = Wall;
			break;
		}
	}

	if (!EditorSelectedWall)
	{
		if (SelectedWall.IsValid() || bWallCurveControlSelected)
		{
			SelectedWall.Reset();
			bWallCurveControlSelected = false;
			ActiveWallMoveTransaction.Reset();
			RoomFloorWallDrag = {};
			ActiveWallCurveControlEditTransaction.Reset();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return;
	}

	if (SelectedWall.Get() != EditorSelectedWall)
	{
		SelectedWall = EditorSelectedWall;
		bWallCurveControlSelected = false;
		ActiveWallMoveTransaction.Reset();
		RoomFloorWallDrag = {};
		ActiveWallCurveControlEditTransaction.Reset();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SyncRailingSelectionFromEditor()
{
	if (bRailingCreationToolActive || bRailingMeshPlacementActive || !GEditor || !GEditor->GetSelectedActors())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	AEHB_Railing* EditorSelectedRailing = nullptr;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_Railing* Railing = Cast<AEHB_Railing>(Actor))
		{
			EditorSelectedRailing = Railing;
			break;
		}
	}

	if (!EditorSelectedRailing)
	{
		if (SelectedRailing.IsValid())
		{
			ClearRailingSelection();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return;
	}

	if (SelectedRailing.Get() != EditorSelectedRailing)
	{
		ClearWallSelection();
		ClearFloorSlabSelection();
		ClearFloorSelection();
		ClearStairSelection();
		ClearRoofSelection();
		SelectedRailing = EditorSelectedRailing;
		SelectedRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
		ActiveRailingEndpointEditTransaction.Reset();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SyncFloorSlabSelectionFromEditor()
{
	if (bFloorSlabPlacementActive || !GEditor || !GEditor->GetSelectedActors())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	AEHB_FloorSlab* EditorSelectedFloorSlab = nullptr;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_FloorSlab* FloorSlab = Cast<AEHB_FloorSlab>(Actor))
		{
			EditorSelectedFloorSlab = FloorSlab;
			break;
		}
	}

	if (!EditorSelectedFloorSlab)
	{
		if (SelectedFloorSlab.IsValid())
		{
			ClearFloorSlabSelection();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return;
	}

	if (SelectedFloorSlab.Get() != EditorSelectedFloorSlab)
	{
		CancelSelectedFloorSlabPreviewCutter();
		ClearWallSelection();
		ClearRailingSelection();
		ClearFloorSelection();
		SelectedFloorSlab = EditorSelectedFloorSlab;
		SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
		SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
		SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
		SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SyncFloorSelectionFromEditor()
{
	if (bFloorPlacementActive || !GEditor || !GEditor->GetSelectedActors())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	AEHB_Floor* EditorSelectedFloor = nullptr;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_Floor* Floor = Cast<AEHB_Floor>(Actor))
		{
			EditorSelectedFloor = Floor;
			break;
		}
	}

	if (!EditorSelectedFloor)
	{
		if (SelectedFloor.IsValid())
		{
			ClearFloorSelection();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return;
	}

	if (SelectedFloor.Get() != EditorSelectedFloor)
	{
		ClearWallSelection();
		ClearRailingSelection();
		ClearStairSelection();
		ClearFloorSlabSelection();
		ClearRoofSelection();
		SelectedFloor = EditorSelectedFloor;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SyncRoofSelectionFromEditor()
{
	if (bRoofPlacementActive || !GEditor || !GEditor->GetSelectedActors())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	TArray<TWeakObjectPtr<AEHBGableRoof>> EditorSelectedRoofs;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHBGableRoof* Roof = Cast<AEHBGableRoof>(Actor))
		{
			EditorSelectedRoofs.Add(Roof);
		}
	}

	if (EditorSelectedRoofs.IsEmpty())
	{
		if (SelectedRoof.IsValid() || !SelectedRoofs.IsEmpty())
		{
			ClearRoofSelection();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return;
	}

	bool bSelectionChanged = SelectedRoofs.Num() != EditorSelectedRoofs.Num();
	if (!bSelectionChanged)
	{
		for (int32 Index = 0; Index < EditorSelectedRoofs.Num(); ++Index)
		{
			if (SelectedRoofs[Index] != EditorSelectedRoofs[Index])
			{
				bSelectionChanged = true;
				break;
			}
		}
	}

	if (bSelectionChanged)
	{
		ClearWallSelection();
		ClearRailingSelection();
		ClearFloorSlabSelection();
		ClearFloorSelection();
		ClearStairSelection();
		SelectedRoofs = MoveTemp(EditorSelectedRoofs);
		SelectedRoof = SelectedRoofs[0];
		SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
		SelectedRoofHandleFirstIndex = INDEX_NONE;
		SelectedRoofHandleSecondIndex = INDEX_NONE;
		ActiveRoofEditTransaction.Reset();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SyncStairSelectionFromEditor()
{
	if (bStairPlacementActive || !GEditor || !GEditor->GetSelectedActors())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	AEHB_Stair* EditorSelectedStair = nullptr;
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_Stair* Stair = Cast<AEHB_Stair>(Actor))
		{
			EditorSelectedStair = Stair;
			break;
		}
	}

	if (!EditorSelectedStair)
	{
		if (SelectedStair.IsValid() || bStairBottomControlSelected)
		{
			ClearStairSelection();
		}
		return;
	}

	if (SelectedStair.Get() != EditorSelectedStair)
	{
		ClearWallSelection();
		ClearRailingSelection();
		ClearFloorSlabSelection();
		ClearFloorSelection();
		SelectedStair = EditorSelectedStair;
		bStairBottomControlSelected = false;
		SelectedStairIntermediateControlIndex = INDEX_NONE;
		ActiveStairBottomControlEditTransaction.Reset();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FEasyHouseEditorMode::SelectStair(AEHB_Stair* Stair)
{
	ClearWallSelection();
	ClearRailingSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	SelectedStair = Stair;
	bStairBottomControlSelected = false;
	SelectedStairIntermediateControlIndex = INDEX_NONE;
	ActiveStairBottomControlEditTransaction.Reset();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearStairSelection()
{
	SelectedStair.Reset();
	bStairBottomControlSelected = false;
	SelectedStairIntermediateControlIndex = INDEX_NONE;
	ActiveStairBottomControlEditTransaction.Reset();
	HoveredStairBottomControl.Reset();
	HoveredStairIntermediateControlIndex = INDEX_NONE;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::SelectStairBottomControl(AEHB_Stair* Stair)
{
	if (!Stair)
	{
		ClearStairSelection();
		return;
	}

	ClearWallSelection();
	ClearRailingSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	SelectedStair = Stair;
	bStairBottomControlSelected = true;
	SelectedStairIntermediateControlIndex = INDEX_NONE;
	if (GEditor)
	{
		if (!GEditor->GetSelectedActors() || !GEditor->GetSelectedActors()->IsSelected(Stair))
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Stair, true, true, true);
		}
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEasyHouseEditorMode::IsStairBottomControlSelected() const
{
	return SelectedStair.IsValid() && bStairBottomControlSelected;
}

FVector FEasyHouseEditorMode::GetStairBottomControlWorldLocation() const
{
	if (const AEHB_Stair* Stair = SelectedStair.Get())
	{
		return Stair->GetBottomControlWorldLocation();
	}
	return FVector::ZeroVector;
}

void FEasyHouseEditorMode::SelectStairIntermediateControl(AEHB_Stair* Stair, int32 ControlIndex)
{
	if (!Stair || ControlIndex < 0 || ControlIndex >= 3)
	{
		ClearStairSelection();
		return;
	}

	ClearWallSelection();
	ClearRailingSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	SelectedStair = Stair;
	bStairBottomControlSelected = false;
	SelectedStairIntermediateControlIndex = ControlIndex;
	if (GEditor)
	{
		if (!GEditor->GetSelectedActors() || !GEditor->GetSelectedActors()->IsSelected(Stair))
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Stair, true, true, true);
		}
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEasyHouseEditorMode::IsStairIntermediateControlSelected() const
{
	return SelectedStair.IsValid()
		&& SelectedStairIntermediateControlIndex >= 0
		&& SelectedStairIntermediateControlIndex < 3;
}

FVector FEasyHouseEditorMode::GetStairIntermediateControlWorldLocation() const
{
	if (const AEHB_Stair* Stair = SelectedStair.Get(); Stair && IsStairIntermediateControlSelected())
	{
		return Stair->GetIntermediateControlWorldLocation(SelectedStairIntermediateControlIndex);
	}
	return FVector::ZeroVector;
}

void FEasyHouseEditorMode::SelectWall(AEHB_Wall* Wall)
{
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRailingSelection();
	ClearStairSelection();
	ClearRoofSelection();
	SelectedWall = Wall;
	bWallCurveControlSelected = false;
	ActiveWallMoveTransaction.Reset();
	RoomFloorWallDrag = {};
	ActiveWallCurveControlEditTransaction.Reset();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearWallSelection()
{
	SelectedWall.Reset();
	bWallCurveControlSelected = false;
	ActiveWallMoveTransaction.Reset();
	RoomFloorWallDrag = {};
	ActiveWallCurveControlEditTransaction.Reset();
	HoveredWallCurveControl.Reset();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::SelectWallCurveControl(AEHB_Wall* Wall)
{
	if (!Wall)
	{
		ClearWallSelection();
		return;
	}

	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearRailingSelection();
	ClearStairSelection();
	ClearRoofSelection();
	SelectedWall = Wall;
	bWallCurveControlSelected = true;
	ActiveWallMoveTransaction.Reset();
	RoomFloorWallDrag = {};
	if (GEditor)
	{
		if (!GEditor->GetSelectedActors() || !GEditor->GetSelectedActors()->IsSelected(Wall))
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Wall, true, true, true);
		}
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEasyHouseEditorMode::IsWallCurveControlSelected() const
{
	return SelectedWall.IsValid() && bWallCurveControlSelected;
}

FVector FEasyHouseEditorMode::GetWallCurveControlWorldLocation() const
{
	if (const AEHB_Wall* Wall = SelectedWall.Get())
	{
		return Wall->GetCurveControlWorldLocation();
	}

	return FVector::ZeroVector;
}

void FEasyHouseEditorMode::DrawWallCurveControl(FPrimitiveDrawInterface* PDI) const
{
	AEHB_Wall* Wall = SelectedWall.Get();
	if (!PDI || !Wall)
	{
		return;
	}

	const FTransform WallTransform = Wall->GetActorTransform();
	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	if (WallLength <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float PreviewHeight = FMath::Max(1.0f, Wall->Height) + 40.0f;
	const FVector WallTopCenterWorld = WallTransform.TransformPosition(Wall->TransformStraightWallLocalPointToCurve(FVector(0.0f, 0.0f, Wall->Height)));
	const FVector ControlWorld = Wall->GetCurveControlWorldLocation();
	const FVector LocalRightWorld = WallTransform.TransformVectorNoScale(FVector(0.0f, 1.0f, 0.0f)).GetSafeNormal();
	const FVector LocalForwardWorld = WallTransform.TransformVectorNoScale(FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal();
	const FVector LocalUpWorld = WallTransform.TransformVectorNoScale(FVector(0.0f, 0.0f, 1.0f)).GetSafeNormal();
	const FLinearColor BaseColor(0.05f, 0.62f, 1.0f, 1.0f);
	const FLinearColor SelectedColor(1.0f, 0.66f, 0.08f, 1.0f);
	const FLinearColor HoverColor(0.35f, 0.92f, 1.0f, 1.0f);
	const FLinearColor PreviewColor(0.02f, 0.95f, 0.68f, 1.0f);
	const bool bHovered = HoveredWallCurveControl.Get() == Wall;
	const FLinearColor ControlColor = bWallCurveControlSelected ? SelectedColor : (bHovered ? HoverColor : BaseColor);
	const float HandleRadius = 20.0f;

	PDI->DrawLine(WallTopCenterWorld, ControlWorld, ControlColor, SDPG_Foreground, 2.5f);

	const FVector CurveStart = WallTransform.TransformPosition(Wall->TransformStraightWallLocalPointToCurve(FVector(-WallLength * 0.5f, 0.0f, PreviewHeight)));
	FVector PreviousCurvePoint = CurveStart;
	constexpr int32 CurveSegmentCount = 32;
	for (int32 SegmentIndex = 1; SegmentIndex <= CurveSegmentCount; ++SegmentIndex)
	{
		const float T = static_cast<float>(SegmentIndex) / static_cast<float>(CurveSegmentCount);
		const float LocalX = FMath::Lerp(-WallLength * 0.5f, WallLength * 0.5f, T);
		const FVector CurvePoint = WallTransform.TransformPosition(Wall->TransformStraightWallLocalPointToCurve(FVector(LocalX, 0.0f, PreviewHeight)));
		PDI->DrawLine(PreviousCurvePoint, CurvePoint, PreviewColor, SDPG_Foreground, 4.0f);
		PreviousCurvePoint = CurvePoint;
	}

	PDI->SetHitProxy(new HEHBWallCurveControlProxy(Wall));
	PDI->DrawPoint(ControlWorld, ControlColor, bWallCurveControlSelected ? 24.0f : (bHovered ? 22.0f : 20.0f), SDPG_Foreground);
	PDI->DrawLine(ControlWorld + LocalForwardWorld * HandleRadius, ControlWorld + LocalRightWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld + LocalRightWorld * HandleRadius, ControlWorld - LocalForwardWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalForwardWorld * HandleRadius, ControlWorld - LocalRightWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalRightWorld * HandleRadius, ControlWorld + LocalForwardWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalUpWorld * 12.0f, ControlWorld + LocalUpWorld * 12.0f, ControlColor, SDPG_Foreground, 3.0f);
	PDI->SetHitProxy(nullptr);
}

void FEasyHouseEditorMode::SelectRailing(AEHB_Railing* Railing)
{
	ClearWallSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearStairSelection();
	ClearRoofSelection();
	SelectedRailing = Railing;
	SelectedRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	ActiveRailingEndpointEditTransaction.Reset();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearRailingSelection()
{
	SelectedRailing.Reset();
	SelectedRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	ActiveRailingEndpointEditTransaction.Reset();
	HoveredRailingEndpointHandleOwner.Reset();
	HoveredRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::SelectRailingEndpointHandle(AEHB_Railing* Railing, EEHBRailingEndpointHandleKind HandleKind)
{
	if (!Railing
		|| Railing->PathMode != EEHBRailingPathMode::Linear
		|| HandleKind == EEHBRailingEndpointHandleKind::None)
	{
		ClearRailingSelection();
		return;
	}

	ClearWallSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearStairSelection();
	ClearRoofSelection();
	SelectedRailing = Railing;
	SelectedRailingEndpointHandleKind = HandleKind;
	if (GEditor)
	{
		if (!GEditor->GetSelectedActors() || !GEditor->GetSelectedActors()->IsSelected(Railing))
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Railing, true, true, true);
		}
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEasyHouseEditorMode::IsRailingEndpointHandleSelected() const
{
	const AEHB_Railing* Railing = SelectedRailing.Get();
	return Railing
		&& Railing->PathMode == EEHBRailingPathMode::Linear
		&& SelectedRailingEndpointHandleKind != EEHBRailingEndpointHandleKind::None;
}

FVector FEasyHouseEditorMode::GetRailingEndpointHandleWorldLocation() const
{
	FVector WorldLocation = FVector::ZeroVector;
	GetRailingEndpointHandleWorldLocation(SelectedRailing.Get(), SelectedRailingEndpointHandleKind, WorldLocation);
	return WorldLocation;
}

bool FEasyHouseEditorMode::GetRailingEndpointHandleWorldLocation(
	const AEHB_Railing* Railing,
	EEHBRailingEndpointHandleKind HandleKind,
	FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;
	if (!Railing
		|| Railing->PathMode != EEHBRailingPathMode::Linear
		|| HandleKind == EEHBRailingEndpointHandleKind::None)
	{
		return false;
	}

	FEHBRailingPathSample Sample;
	if (!Railing->EvaluatePathAtDistance(
		HandleKind == EEHBRailingEndpointHandleKind::Start ? 0.0f : Railing->GetRailingLength(),
		Sample))
	{
		return false;
	}

	const FTransform RailingTransform = Railing->GetActorTransform();
	const FVector BaseWorld = RailingTransform.TransformPosition(Sample.LocalLocation);
	const FVector UpWorld = RailingTransform.TransformVectorNoScale(FVector::UpVector)
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float ControlHeight = FMath::Max(Railing->PostHeight, Railing->GetRailTopHeight()) + 28.0f;
	OutWorldLocation = BaseWorld + UpWorld * ControlHeight;
	return true;
}

bool FEasyHouseEditorMode::MoveSelectedRailingEndpointHandle(const FVector& WorldDelta)
{
	AEHB_Railing* Railing = SelectedRailing.Get();
	if (!Railing
		|| Railing->PathMode != EEHBRailingPathMode::Linear
		|| SelectedRailingEndpointHandleKind == EEHBRailingEndpointHandleKind::None
		|| WorldDelta.IsNearlyZero())
	{
		return false;
	}

	auto GetEndpointBaseWorld = [](const AEHB_Railing* TargetRailing, EEHBRailingEndpointHandleKind HandleKind, FVector& OutBaseWorld)
	{
		OutBaseWorld = FVector::ZeroVector;
		if (!TargetRailing || TargetRailing->PathMode != EEHBRailingPathMode::Linear)
		{
			return false;
		}

		FEHBRailingPathSample Sample;
		if (!TargetRailing->EvaluatePathAtDistance(
			HandleKind == EEHBRailingEndpointHandleKind::Start ? 0.0f : TargetRailing->GetRailingLength(),
			Sample))
		{
			return false;
		}

		OutBaseWorld = TargetRailing->GetActorTransform().TransformPosition(Sample.LocalLocation);
		return true;
	};

	struct FRailingEndpointMoveTarget
	{
		TWeakObjectPtr<AEHB_Railing> Railing;
		EEHBRailingEndpointHandleKind HandleKind = EEHBRailingEndpointHandleKind::None;
		FVector OldBaseWorld = FVector::ZeroVector;
	};

	TArray<FRailingEndpointMoveTarget> MoveTargets;
	auto AddMoveTarget = [&MoveTargets, &GetEndpointBaseWorld](AEHB_Railing* TargetRailing, EEHBRailingEndpointHandleKind HandleKind)
	{
		if (!TargetRailing || HandleKind == EEHBRailingEndpointHandleKind::None)
		{
			return;
		}

		for (const FRailingEndpointMoveTarget& Existing : MoveTargets)
		{
			if (Existing.Railing.Get() == TargetRailing && Existing.HandleKind == HandleKind)
			{
				return;
			}
		}

		FVector BaseWorld = FVector::ZeroVector;
		if (!GetEndpointBaseWorld(TargetRailing, HandleKind, BaseWorld))
		{
			return;
		}

		FRailingEndpointMoveTarget& Target = MoveTargets.AddDefaulted_GetRef();
		Target.Railing = TargetRailing;
		Target.HandleKind = HandleKind;
		Target.OldBaseWorld = BaseWorld;
	};

	FVector SelectedBaseWorld = FVector::ZeroVector;
	if (!GetEndpointBaseWorld(Railing, SelectedRailingEndpointHandleKind, SelectedBaseWorld))
	{
		return false;
	}

	constexpr float ConnectedEndpointTolerance = 10.0f;
	const float ConnectedEndpointToleranceSquared = FMath::Square(ConnectedEndpointTolerance);
	AddMoveTarget(Railing, SelectedRailingEndpointHandleKind);

	if (AEHBBuildingActorBase* Building = Railing->OwningBuilding)
	{
		FEHBElementQuery Query;
		Query.ElementTypes = { EEHBBuildingElementType::Railing };
		for (AEHBElementActorBase* Element : Building->QueryElements(Query))
		{
			AEHB_Railing* CandidateRailing = Cast<AEHB_Railing>(Element);
			if (!CandidateRailing
				|| CandidateRailing->PathMode != EEHBRailingPathMode::Linear
				|| CandidateRailing->IsActorBeingDestroyed())
			{
				continue;
			}

			for (const EEHBRailingEndpointHandleKind CandidateKind : {
				EEHBRailingEndpointHandleKind::Start,
				EEHBRailingEndpointHandleKind::End })
			{
				FVector CandidateBaseWorld = FVector::ZeroVector;
				if (GetEndpointBaseWorld(CandidateRailing, CandidateKind, CandidateBaseWorld)
					&& FVector::DistSquared(SelectedBaseWorld, CandidateBaseWorld) <= ConnectedEndpointToleranceSquared)
				{
					AddMoveTarget(CandidateRailing, CandidateKind);
				}
			}
		}
	}

	FVector EffectiveWorldDelta = WorldDelta;
	if (AEHBBuildingActorBase* Building = Railing->OwningBuilding)
	{
		const float RailingPostSize = FMath::Max(1.0f, Railing->PostWidth);
		const float SnapDistance = FMath::Max(30.0f, FMath::Max(RailingPostSize, Railing->RailThickness) * 4.0f);
		FVector SnappedBaseWorld = FVector::ZeroVector;
		if (ResolveRailingEndpointSurfaceSnap(
			Building,
			SelectedBaseWorld + WorldDelta,
			RailingPostSize,
			RailingPostSize,
			SnapDistance,
			SnappedBaseWorld))
		{
			EffectiveWorldDelta = SnappedBaseWorld - SelectedBaseWorld;
		}
	}

	TSet<AEHB_Railing*> ChangedRailings;
	for (const FRailingEndpointMoveTarget& Target : MoveTargets)
	{
		AEHB_Railing* TargetRailing = Target.Railing.Get();
		if (!TargetRailing)
		{
			continue;
		}

		const FVector NewBaseWorld = Target.OldBaseWorld + EffectiveWorldDelta;
		const FVector NewLocal = TargetRailing->GetActorTransform().InverseTransformPosition(NewBaseWorld);
		const FVector OtherLocal = Target.HandleKind == EEHBRailingEndpointHandleKind::Start
			? TargetRailing->LinearEnd
			: TargetRailing->LinearStart;
		if (FVector::Dist2D(NewLocal, OtherLocal) <= 10.0f)
		{
			continue;
		}

		TargetRailing->Modify();
		if (Target.HandleKind == EEHBRailingEndpointHandleKind::Start)
		{
			TargetRailing->LinearStart = NewLocal;
			TargetRailing->StartAnchor = FEHBRailingAnchor();
		}
		else
		{
			TargetRailing->LinearEnd = NewLocal;
			TargetRailing->EndAnchor = FEHBRailingAnchor();
		}
		ChangedRailings.Add(TargetRailing);
	}

	for (AEHB_Railing* ChangedRailing : ChangedRailings)
	{
		if (ChangedRailing)
		{
			ChangedRailing->RebuildRailing();
			ChangedRailing->MarkPackageDirty();
		}
	}

	if (AEHBBuildingActorBase* Building = Railing->OwningBuilding)
	{
		Building->Modify();
		Building->MarkPackageDirty();
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return ChangedRailings.Num() > 0;
}

void FEasyHouseEditorMode::DrawRailingEndpointHandles(FPrimitiveDrawInterface* PDI) const
{
	AEHB_Railing* Railing = SelectedRailing.Get();
	if (!PDI || !Railing || Railing->PathMode != EEHBRailingPathMode::Linear)
	{
		return;
	}

	const FTransform RailingTransform = Railing->GetActorTransform();
	const FVector UpWorld = RailingTransform.TransformVectorNoScale(FVector::UpVector)
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FLinearColor BaseColor(0.05f, 0.62f, 1.0f, 1.0f);
	const FLinearColor EndColor(0.48f, 0.38f, 1.0f, 1.0f);
	const FLinearColor SelectedColor(1.0f, 0.66f, 0.08f, 1.0f);
	const FLinearColor HoverColor(0.35f, 0.92f, 1.0f, 1.0f);

	for (const EEHBRailingEndpointHandleKind HandleKind : {
		EEHBRailingEndpointHandleKind::Start,
		EEHBRailingEndpointHandleKind::End })
	{
		FEHBRailingPathSample Sample;
		if (!Railing->EvaluatePathAtDistance(
			HandleKind == EEHBRailingEndpointHandleKind::Start ? 0.0f : Railing->GetRailingLength(),
			Sample))
		{
			continue;
		}

		const FVector BaseWorld = RailingTransform.TransformPosition(Sample.LocalLocation);
		FVector ControlWorld = FVector::ZeroVector;
		if (!GetRailingEndpointHandleWorldLocation(Railing, HandleKind, ControlWorld))
		{
			continue;
		}

		const bool bSelected = IsRailingEndpointHandleSelected()
			&& SelectedRailingEndpointHandleKind == HandleKind;
		const bool bHovered = HoveredRailingEndpointHandleOwner.Get() == Railing
			&& HoveredRailingEndpointHandleKind == HandleKind;
		const FLinearColor EndpointBaseColor = HandleKind == EEHBRailingEndpointHandleKind::Start ? BaseColor : EndColor;
		const FLinearColor HandleColor = bSelected ? SelectedColor : (bHovered ? HoverColor : EndpointBaseColor);
		const float HandleRadius = bSelected ? 22.0f : (bHovered ? 20.0f : 18.0f);
		const FVector ForwardWorld = RailingTransform.TransformVectorNoScale(Sample.LocalForward)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		FVector RightWorld = FVector::CrossProduct(UpWorld, ForwardWorld).GetSafeNormal();
		if (RightWorld.IsNearlyZero())
		{
			RightWorld = RailingTransform.TransformVectorNoScale(Sample.LocalRight)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		}

		PDI->SetHitProxy(new HEHBRailingEndpointHandleProxy(Railing, HandleKind));
		PDI->DrawPoint(ControlWorld, FLinearColor(HandleColor.R, HandleColor.G, HandleColor.B, 0.0f), 48.0f, SDPG_Foreground);
		PDI->DrawLine(BaseWorld + UpWorld * Railing->PostHeight, ControlWorld, HandleColor, SDPG_Foreground, 2.5f);
		PDI->DrawPoint(ControlWorld, HandleColor, HandleRadius, SDPG_Foreground);
		const float DiamondRadius = 18.0f;
		PDI->DrawLine(ControlWorld + ForwardWorld * DiamondRadius, ControlWorld + RightWorld * DiamondRadius, HandleColor, SDPG_Foreground, 4.0f);
		PDI->DrawLine(ControlWorld + RightWorld * DiamondRadius, ControlWorld - ForwardWorld * DiamondRadius, HandleColor, SDPG_Foreground, 4.0f);
		PDI->DrawLine(ControlWorld - ForwardWorld * DiamondRadius, ControlWorld - RightWorld * DiamondRadius, HandleColor, SDPG_Foreground, 4.0f);
		PDI->DrawLine(ControlWorld - RightWorld * DiamondRadius, ControlWorld + ForwardWorld * DiamondRadius, HandleColor, SDPG_Foreground, 4.0f);
		PDI->DrawLine(ControlWorld - UpWorld * 10.0f, ControlWorld + UpWorld * 10.0f, HandleColor, SDPG_Foreground, 3.0f);
		PDI->SetHitProxy(nullptr);
	}
}

void FEasyHouseEditorMode::DrawStairBottomControl(FPrimitiveDrawInterface* PDI) const
{
	AEHB_Stair* Stair = SelectedStair.Get();
	if (!PDI || !Stair)
	{
		return;
	}

	const float StairLength = Stair->GetStairLength();
	if (StairLength <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float StairHeight = Stair->StairData.bUseActualDimensions
		? Stair->StairData.StairHeight
		: Stair->StairData.DefaultStairHeight;
	const FTransform StairTransform = Stair->GetActorTransform();
	const FVector ControlWorld = Stair->GetBottomControlWorldLocation();
	const FRotator BottomWorldRotation = Stair->GetBottomControlWorldRotation();
	const FVector LocalForwardWorld = BottomWorldRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
	const FVector LocalRightWorld = BottomWorldRotation.RotateVector(FVector::RightVector).GetSafeNormal();
	const FVector LocalUpWorld = StairTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const FLinearColor BaseColor(0.05f, 0.62f, 1.0f, 1.0f);
	const FLinearColor SelectedColor(1.0f, 0.66f, 0.08f, 1.0f);
	const FLinearColor HoverColor(0.35f, 0.92f, 1.0f, 1.0f);
	const FLinearColor PreviewColor(0.02f, 0.95f, 0.68f, 1.0f);
	const bool bHovered = HoveredStairBottomControl.Get() == Stair;
	const FLinearColor ControlColor = bStairBottomControlSelected ? SelectedColor : (bHovered ? HoverColor : BaseColor);
	const float HandleRadius = 20.0f;
	const float BottomPreviewZ = StairTransform.InverseTransformPosition(ControlWorld).Z;

	FVector PreviousCurvePoint = StairTransform.TransformPosition(
		Stair->TransformStraightStairLocalPointToPath(FVector(0.0f, 0.0f, StairHeight + 15.0f)));
	constexpr int32 CurveSegmentCount = 32;
	for (int32 SegmentIndex = 1; SegmentIndex <= CurveSegmentCount; ++SegmentIndex)
	{
		const float T = static_cast<float>(SegmentIndex) / static_cast<float>(CurveSegmentCount);
		const float PreviewZ = FMath::Lerp(StairHeight + 15.0f, BottomPreviewZ, T);
		const FVector CurvePoint = StairTransform.TransformPosition(
			Stair->TransformStraightStairLocalPointToPath(FVector(StairLength * T, 0.0f, PreviewZ)));
		PDI->DrawLine(PreviousCurvePoint, CurvePoint, PreviewColor, SDPG_Foreground, 4.0f);
		PreviousCurvePoint = CurvePoint;
	}

	const FLinearColor IntermediateColor(0.55f, 0.2f, 1.0f, 1.0f);
	const FLinearColor IntermediateHoverColor(0.78f, 0.55f, 1.0f, 1.0f);
	for (int32 ControlIndex = 0; ControlIndex < 3; ++ControlIndex)
	{
		const FVector IntermediateWorld = Stair->GetIntermediateControlWorldLocation(ControlIndex);
		const bool bIntermediateSelected =
			IsStairIntermediateControlSelected() && SelectedStairIntermediateControlIndex == ControlIndex;
		const bool bIntermediateHovered = HoveredStairIntermediateControlIndex == ControlIndex;
		const FLinearColor PointColor = bIntermediateSelected
			? SelectedColor
			: (bIntermediateHovered ? IntermediateHoverColor : IntermediateColor);
		PDI->SetHitProxy(new HEHBStairIntermediateControlProxy(Stair, ControlIndex));
		PDI->DrawPoint(
			IntermediateWorld,
			PointColor,
			bIntermediateSelected ? 22.0f : (bIntermediateHovered ? 20.0f : 18.0f),
			SDPG_Foreground);
		PDI->DrawLine(
			IntermediateWorld - LocalUpWorld * 10.0f,
			IntermediateWorld + LocalUpWorld * 10.0f,
			PointColor,
			SDPG_Foreground,
			2.5f);
		PDI->SetHitProxy(nullptr);
	}

	PDI->SetHitProxy(new HEHBStairBottomControlProxy(Stair));
	PDI->DrawPoint(ControlWorld, ControlColor, bStairBottomControlSelected ? 24.0f : (bHovered ? 22.0f : 20.0f), SDPG_Foreground);
	PDI->DrawLine(ControlWorld + LocalForwardWorld * HandleRadius, ControlWorld + LocalRightWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld + LocalRightWorld * HandleRadius, ControlWorld - LocalForwardWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalForwardWorld * HandleRadius, ControlWorld - LocalRightWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalRightWorld * HandleRadius, ControlWorld + LocalForwardWorld * HandleRadius, ControlColor, SDPG_Foreground, 4.0f);
	PDI->DrawLine(ControlWorld - LocalUpWorld * 12.0f, ControlWorld + LocalUpWorld * 12.0f, ControlColor, SDPG_Foreground, 3.0f);
	PDI->SetHitProxy(nullptr);
}

void FEasyHouseEditorMode::SelectFloorSlab(AEHB_FloorSlab* FloorSlab)
{
	CancelSelectedFloorSlabPreviewCutter();
	ClearWallSelection();
	ClearRailingSelection();
	ClearStairSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	SelectedFloorSlab = FloorSlab;
	SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
	SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
	SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearFloorSlabSelection()
{
	CancelSelectedFloorSlabPreviewCutter();SlabCutterDraft.Cancel();
	SelectedFloorSlab.Reset();
	SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
	SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
	SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
	ActiveFloorSlabEditTransaction.Reset();
	HoveredFloorSlabHandleOwner.Reset();
	HoveredFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	HoveredFloorSlabHandleLoopIndex = INDEX_NONE;
	HoveredFloorSlabHandleFirstIndex = INDEX_NONE;
	HoveredFloorSlabHandleSecondIndex = INDEX_NONE;
}

void FEasyHouseEditorMode::SelectFloor(AEHB_Floor* Floor)
{
	ClearWallSelection();
	ClearRailingSelection();
	ClearStairSelection();
	ClearFloorSlabSelection();
	ClearRoofSelection();
	SelectedFloor = Floor;
	HoveredFloorToolbarCommandIndex = INDEX_NONE;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearFloorSelection()
{
	SelectedFloor.Reset();
	HoveredFloorToolbarCommandIndex = INDEX_NONE;
}

void FEasyHouseEditorMode::SelectRoof(AEHBGableRoof* Roof)
{
	ClearWallSelection();
	ClearRailingSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearStairSelection();
	SelectedRoof = Roof;
	SelectedRoofs.Reset();
	if (Roof)
	{
		SelectedRoofs.Add(Roof);
	}
	SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
	SelectedRoofHandleFirstIndex = INDEX_NONE;
	SelectedRoofHandleSecondIndex = INDEX_NONE;
	ActiveRoofEditTransaction.Reset();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearRoofSelection()
{
	SelectedRoof.Reset();
	SelectedRoofs.Reset();
	SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
	SelectedRoofHandleFirstIndex = INDEX_NONE;
	SelectedRoofHandleSecondIndex = INDEX_NONE;
	ActiveRoofEditTransaction.Reset();
	HoveredRoofHandleOwner.Reset();
	HoveredRoofHandleKind = EEHBRoofEditHandleKind::None;
	HoveredRoofHandleFirstIndex = INDEX_NONE;
	HoveredRoofHandleSecondIndex = INDEX_NONE;
}

void FEasyHouseEditorMode::SelectRoofHandle(
	AEHBGableRoof* Roof,
	EEHBRoofEditHandleKind HandleKind,
	int32 FirstIndex,
	int32 SecondIndex)
{
	if (!Roof)
	{
		ClearRoofSelection();
		return;
	}

	ClearWallSelection();
	ClearRailingSelection();
	ClearFloorSlabSelection();
	ClearFloorSelection();
	ClearStairSelection();
	SelectedRoof = Roof;
	SelectedRoofs = { Roof };
	if (HandleKind == EEHBRoofEditHandleKind::Edge && IsValidRoofEdgeHandleIndex(FirstIndex))
	{
		SelectedRoofHandleKind = HandleKind;
		SelectedRoofHandleFirstIndex = FirstIndex;
		SelectedRoofHandleSecondIndex = SecondIndex;
	}
	else if (HandleKind == EEHBRoofEditHandleKind::Height)
	{
		SelectedRoofHandleKind = HandleKind;
		SelectedRoofHandleFirstIndex = INDEX_NONE;
		SelectedRoofHandleSecondIndex = INDEX_NONE;
	}
	else
	{
		SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
		SelectedRoofHandleFirstIndex = INDEX_NONE;
		SelectedRoofHandleSecondIndex = INDEX_NONE;
	}
	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Roof, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::SelectFloorSlabHandle(
	AEHB_FloorSlab* FloorSlab,
	EEHBFloorSlabEditHandleKind HandleKind,
	int32 FirstIndex,
	int32 SecondIndex,
	int32 LoopIndex)
{
	if (!FloorSlab)
	{
		ClearFloorSlabSelection();
		return;
	}

	int32 AdjustedFirstIndex = FirstIndex;
	const bool bSelectingSameCutter =
		SelectedFloorSlab.Get() == FloorSlab
		&& SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter
		&& HandleKind == EEHBFloorSlabEditHandleKind::Cutter
		&& SelectedFloorSlabHandleFirstIndex == FirstIndex;
	if (!bSelectingSameCutter
		&& SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter
		&& SelectedFloorSlab.IsValid())
	{
		const AEHB_FloorSlab* PreviousFloorSlab = SelectedFloorSlab.Get();
		const int32 RemovedCutterIndex = SelectedFloorSlabHandleFirstIndex;
		const bool bRemovedCutter = CancelSelectedFloorSlabPreviewCutter();
		if (bRemovedCutter
			&& PreviousFloorSlab == FloorSlab
			&& HandleKind == EEHBFloorSlabEditHandleKind::Cutter
			&& AdjustedFirstIndex > RemovedCutterIndex)
		{
			--AdjustedFirstIndex;
		}
	}

	ClearWallSelection();
	ClearRailingSelection();
	ClearStairSelection();
	ClearFloorSelection();
	ClearRoofSelection();
	SelectedFloorSlab = FloorSlab;
	SelectedFloorSlabHandleKind = HandleKind;
	SelectedFloorSlabHandleLoopIndex = HandleKind == EEHBFloorSlabEditHandleKind::Cutter ? INDEX_NONE : LoopIndex;
	SelectedFloorSlabHandleFirstIndex = AdjustedFirstIndex;
	SelectedFloorSlabHandleSecondIndex = SecondIndex;
	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(FloorSlab, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}
	RefreshSelectedElementEditor();
}

bool FEasyHouseEditorMode::CancelSelectedFloorSlabPreviewCutter()
{
 if(SlabCutterDraft.IsFor(SelectedFloorSlab.Get())){SlabCutterDraft.Cancel();SelectedFloorSlabHandleKind=EEHBFloorSlabEditHandleKind::None;SelectedFloorSlabHandleLoopIndex=SelectedFloorSlabHandleFirstIndex=SelectedFloorSlabHandleSecondIndex=INDEX_NONE;if(GEditor)GEditor->RedrawLevelEditingViewports();return true;}
	AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!FloorSlab
		|| SelectedFloorSlabHandleKind != EEHBFloorSlabEditHandleKind::Cutter
		|| !FloorSlab->PreviewCutters.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
	{
		return false;
	}

	ActiveFloorSlabEditTransaction.Reset();
	const FScopedTransaction Transaction(LOCTEXT("CancelFloorSlabPreviewCutterTransaction", "Cancel Floor Slab Preview Cutter"));
	const bool bRemoved = FloorSlab->RemovePreviewCutter(SelectedFloorSlabHandleFirstIndex);
	SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
	SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
	SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return bRemoved;
}

bool FEasyHouseEditorMode::IsFloorSlabOperationActive() const
{
	return bFloorSlabPlacementActive
		|| (!bWallCreationToolActive
			&& !bRailingCreationToolActive
			&& !bDoorWindowPlacementActive
			&& !bWallSurfacePlacementActive
			&& !bPillarMeshPlacementActive
			&& !bRailingMeshPlacementActive
			&& !bRoofPlacementActive
			&& !bStairPlacementActive
			&& SelectedFloorSlab.IsValid());
}

bool FEasyHouseEditorMode::ShouldUseBuildingViewportNavigation() const
{
	return bWallCreationToolActive
		|| bRailingCreationToolActive
		|| bDoorWindowPlacementActive
		|| bWallSurfacePlacementActive
		|| bPillarMeshPlacementActive
		|| bRailingMeshPlacementActive
		|| bFloorSlabPlacementActive
		|| bFloorPlacementActive
		|| bRoofPlacementActive
		|| bStairPlacementActive
		|| SelectedWall.IsValid()
		|| SelectedRailing.IsValid()
		|| SelectedStair.IsValid()
		|| SelectedFloorSlab.IsValid()
		|| SelectedFloor.IsValid()
		|| SelectedRoof.IsValid();
}

void FEasyHouseEditorMode::BeginRightMouseNavigation(FViewport* Viewport)
{
	bRightMouseNavigationDown = true;
	bRightMouseNavigationMoved = false;
	bRightMouseNavigationUsed = false;
	RightMouseNavigationStart = Viewport
		? FIntPoint(Viewport->GetMouseX(), Viewport->GetMouseY())
		: FIntPoint::ZeroValue;
}

bool FEasyHouseEditorMode::FinishRightMouseNavigation(FViewport* Viewport)
{
	if (Viewport)
	{
		constexpr int32 RightMouseMoveThresholdPixels = 8;
		const int32 MouseDeltaX = Viewport->GetMouseX() - RightMouseNavigationStart.X;
		const int32 MouseDeltaY = Viewport->GetMouseY() - RightMouseNavigationStart.Y;
		if (MouseDeltaX * MouseDeltaX + MouseDeltaY * MouseDeltaY > RightMouseMoveThresholdPixels * RightMouseMoveThresholdPixels)
		{
			bRightMouseNavigationMoved = true;
		}
	}

	const bool bQuickClick = bRightMouseNavigationDown
		&& !bRightMouseNavigationMoved
		&& !bRightMouseNavigationUsed;

	ResetRightMouseNavigation(Viewport);
	return bQuickClick;
}

void FEasyHouseEditorMode::ResetRightMouseNavigation(FViewport* Viewport)
{
	bRightMouseNavigationDown = false;
	bRightMouseNavigationMoved = false;
	bRightMouseNavigationUsed = false;
	RightMouseNavigationStart = FIntPoint::ZeroValue;
	RestoreViewportMouse(Viewport);
}

void FEasyHouseEditorMode::RestoreViewportMouse(FViewport* Viewport) const
{
	FViewport* TargetViewport = Viewport;
	if (!TargetViewport && GCurrentLevelEditingViewportClient)
	{
		TargetViewport = GCurrentLevelEditingViewportClient->Viewport;
	}

	if (!TargetViewport)
	{
		return;
	}

	TargetViewport->CaptureMouse(false);
	TargetViewport->LockMouseToViewport(false);
	TargetViewport->ShowCursor(true);
	TargetViewport->ShowSoftwareCursor(false);
	TargetViewport->Invalidate();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetPlatformCursorVisibility(true);
	}
}

void FEasyHouseEditorMode::RequestViewportMouseRestore(FViewport* Viewport)
{
	ForceViewportCursorVisibleFrames = FMath::Max(ForceViewportCursorVisibleFrames, 3);
	RestoreViewportMouse(Viewport);
}

bool FEasyHouseEditorMode::IsViewportNavigationKey(FKey Key) const
{
	return Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D
		|| Key == EKeys::Q || Key == EKeys::E;
}

bool FEasyHouseEditorMode::ExitCurrentBuildingStateByRightClick()
{
	if (bWallCreationToolActive)
	{
		CancelWallCreation();
		return true;
	}

	if (bRailingCreationToolActive)
	{
		CancelRailingCreation();
		return true;
	}

	if (bDoorWindowPlacementActive)
	{
		CancelDoorWindowPlacement();
		return true;
	}

	if (bWallSurfacePlacementActive)
	{
		CancelWallSurfacePlacement();
		return true;
	}

	if (bPillarMeshPlacementActive)
	{
		CancelPillarMeshPlacement();
		return true;
	}
	if (bRailingMeshPlacementActive)
	{
		CancelRailingMeshPlacement();
		return true;
	}

	if (bFloorSlabPlacementActive)
	{
		CancelFloorSlabPlacement();
		return true;
	}

	if (bFloorPlacementActive)
	{
		CancelFloorPlacement();
		return true;
	}

	if (bRoofPlacementActive)
	{
		CancelRoofPlacement();
		return true;
	}

	if (bStairPlacementActive)
	{
		CancelStairPlacement();
		return true;
	}

	if (IsFloorSlabHandleSelected())
	{
		if (!CancelSelectedFloorSlabPreviewCutter())
		{
			SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
			SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
			SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
			SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports();
			}
		}
		return true;
	}

	if (IsRoofHandleSelected())
	{
		SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
		SelectedRoofHandleFirstIndex = INDEX_NONE;
		SelectedRoofHandleSecondIndex = INDEX_NONE;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	if (IsWallCurveControlSelected())
	{
		bWallCurveControlSelected = false;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	if (IsRailingEndpointHandleSelected())
	{
		SelectedRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	if (IsStairBottomControlSelected())
	{
		bStairBottomControlSelected = false;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	if (IsStairIntermediateControlSelected())
	{
		SelectedStairIntermediateControlIndex = INDEX_NONE;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	if (SelectedFloorSlab.IsValid())
	{
		if (GEditor)
		{
			GEditor->SelectActor(SelectedFloorSlab.Get(), false, true, true);
		}
		ClearFloorSlabSelection();
		return true;
	}

	if (SelectedFloor.IsValid())
	{
		if (GEditor)
		{
			GEditor->SelectActor(SelectedFloor.Get(), false, true, true);
		}
		ClearFloorSelection();
		return true;
	}

	if (SelectedRoof.IsValid())
	{
		if (GEditor)
		{
			for (const TWeakObjectPtr<AEHBGableRoof>& Selected : SelectedRoofs)
			{
				if (AEHBGableRoof* Roof = Selected.Get())
				{
					GEditor->SelectActor(Roof, false, true, true);
				}
			}
		}
		ClearRoofSelection();
		return true;
	}

	if (SelectedWall.IsValid())
	{
		if (GEditor)
		{
			GEditor->SelectActor(SelectedWall.Get(), false, true, true);
		}
		ClearWallSelection();
		return true;
	}

	if (SelectedRailing.IsValid())
	{
		if (GEditor)
		{
			GEditor->SelectActor(SelectedRailing.Get(), false, true, true);
		}
		ClearRailingSelection();
		return true;
	}

	if (SelectedStair.IsValid())
	{
		if (GEditor)
		{
			GEditor->SelectActor(SelectedStair.Get(), false, true, true);
		}
		ClearStairSelection();
		return true;
	}

	return false;
}

bool FEasyHouseEditorMode::IsFloorSlabHandleSelected() const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!FloorSlab || SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::None)
	{
		return false;
	}

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter)
	{
		return SlabCutterDraft.IsFor(FloorSlab)||FloorSlab->PreviewCutters.IsValidIndex(SelectedFloorSlabHandleFirstIndex);
	}

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge)
	{
		TArray<FVector> Loop;
		return FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop)
			&& Loop.Num() >= 3
			&& Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex)
			&& Loop.IsValidIndex(SelectedFloorSlabHandleSecondIndex)
			&& SelectedFloorSlabHandleSecondIndex == (SelectedFloorSlabHandleFirstIndex + 1) % Loop.Num();
	}

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner)
	{
		TArray<FVector> Loop;
		return FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop)
			&& Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex);
	}

	return false;
}

FVector FEasyHouseEditorMode::GetFloorSlabHandleWorldLocation() const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!FloorSlab)
	{
		return FVector::ZeroVector;
	}

	if(SlabCutterDraft.IsFor(FloorSlab)&&SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Cutter)return SlabCutterDraft.WidgetLocation();
	const FTransform SlabTransform = FloorSlab->GetActorTransform();
	const FVector HandleHeightOffset = SlabTransform.TransformVectorNoScale(FVector(0.0f, 0.0f, 8.0f));
	TArray<FVector> Loop;
	FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop);
	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner
		&& Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
	{
		return SlabTransform.TransformPosition(Loop[SelectedFloorSlabHandleFirstIndex]) + HandleHeightOffset;
	}

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge
		&& Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex)
		&& Loop.IsValidIndex(SelectedFloorSlabHandleSecondIndex))
	{
		const FVector A = SlabTransform.TransformPosition(Loop[SelectedFloorSlabHandleFirstIndex]);
		const FVector B = SlabTransform.TransformPosition(Loop[SelectedFloorSlabHandleSecondIndex]);
		return (A + B) * 0.5f + HandleHeightOffset;
	}

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter
		&& FloorSlab->PreviewCutters.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
	{
		return (FloorSlab->PreviewCutters[SelectedFloorSlabHandleFirstIndex].LocalTransform * FloorSlab->GetActorTransform()).GetLocation();
	}

	return FloorSlab->GetActorLocation();
}

bool FEasyHouseEditorMode::IsRoofHandleSelected() const
{
	if (!SelectedRoof.IsValid())
	{
		return false;
	}

	if (SelectedRoofHandleKind == EEHBRoofEditHandleKind::Height)
	{
		return true;
	}

	return SelectedRoofHandleKind == EEHBRoofEditHandleKind::Edge
		&& IsValidRoofEdgeHandleIndex(SelectedRoofHandleFirstIndex);
}

FVector FEasyHouseEditorMode::GetRoofHandleWorldLocation() const
{
	AEHBGableRoof* Roof = SelectedRoof.Get();
	if (Roof && SelectedRoofHandleKind == EEHBRoofEditHandleKind::Height)
	{
		return Roof->GetActorTransform().TransformPosition(GetRoofHeightHandleLocalLocation(*Roof));
	}

	FVector LocalLocation = FVector::ZeroVector;
	if (!Roof || !GetRoofEdgeHandleLocalLocation(Roof, SelectedRoofHandleFirstIndex, LocalLocation))
	{
		return Roof ? Roof->GetActorLocation() : FVector::ZeroVector;
	}

	return Roof->GetActorTransform().TransformPosition(LocalLocation);
}

void FEasyHouseEditorMode::UpdateHoveredControls(FViewport* Viewport)
{
	TWeakObjectPtr<AEHB_FloorSlab> NewHoveredFloorSlabHandleOwner;
	EEHBFloorSlabEditHandleKind NewHoveredFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	int32 NewHoveredFloorSlabHandleLoopIndex = INDEX_NONE;
	int32 NewHoveredFloorSlabHandleFirstIndex = INDEX_NONE;
	int32 NewHoveredFloorSlabHandleSecondIndex = INDEX_NONE;
	TWeakObjectPtr<AEHBGableRoof> NewHoveredRoofHandleOwner;
	EEHBRoofEditHandleKind NewHoveredRoofHandleKind = EEHBRoofEditHandleKind::None;
	int32 NewHoveredRoofHandleFirstIndex = INDEX_NONE;
	int32 NewHoveredRoofHandleSecondIndex = INDEX_NONE;
	int32 NewHoveredFloorSlabToolbarCommandIndex = INDEX_NONE;
	int32 NewHoveredFloorToolbarCommandIndex = INDEX_NONE;
	bool bNewHoveredPillarConnectionToolbarButton = false;
	TWeakObjectPtr<AEHB_Wall> NewHoveredWallCurveControl;
	TWeakObjectPtr<AEHB_Railing> NewHoveredRailingEndpointHandleOwner;
	EEHBRailingEndpointHandleKind NewHoveredRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	TWeakObjectPtr<AEHB_Stair> NewHoveredStairBottomControl;
	int32 NewHoveredStairIntermediateControlIndex = INDEX_NONE;

	const FIntPoint ViewportSize = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	const int32 MouseX = Viewport ? Viewport->GetMouseX() : INDEX_NONE;
	const int32 MouseY = Viewport ? Viewport->GetMouseY() : INDEX_NONE;
	if (Viewport
		&& !bRightMouseNavigationDown
		&& ViewportSize.X > 0
		&& ViewportSize.Y > 0
		&& MouseX >= 0
		&& MouseY >= 0
		&& MouseX < ViewportSize.X
		&& MouseY < ViewportSize.Y)
	{
		if (HHitProxy* HitProxy = Viewport->GetHitProxy(MouseX, MouseY))
		{
			if (HitProxy->IsA(HEHBFloorSlabHandleProxy::StaticGetType()))
			{
				HEHBFloorSlabHandleProxy* HandleProxy = static_cast<HEHBFloorSlabHandleProxy*>(HitProxy);
				NewHoveredFloorSlabHandleOwner = HandleProxy->FloorSlab;
				NewHoveredFloorSlabHandleKind = HandleProxy->HandleKind;
				NewHoveredFloorSlabHandleLoopIndex = HandleProxy->LoopIndex;
				NewHoveredFloorSlabHandleFirstIndex = HandleProxy->FirstIndex;
				NewHoveredFloorSlabHandleSecondIndex = HandleProxy->SecondIndex;
			}
			else if (HitProxy->IsA(HEHBRoofHandleProxy::StaticGetType()))
			{
				HEHBRoofHandleProxy* HandleProxy = static_cast<HEHBRoofHandleProxy*>(HitProxy);
				NewHoveredRoofHandleOwner = HandleProxy->Roof;
				NewHoveredRoofHandleKind = HandleProxy->HandleKind;
				NewHoveredRoofHandleFirstIndex = HandleProxy->FirstIndex;
				NewHoveredRoofHandleSecondIndex = HandleProxy->SecondIndex;
			}
			else if (HitProxy->IsA(HEHBWallCurveControlProxy::StaticGetType()))
			{
				HEHBWallCurveControlProxy* HandleProxy = static_cast<HEHBWallCurveControlProxy*>(HitProxy);
				NewHoveredWallCurveControl = HandleProxy->Wall;
			}
			else if (HitProxy->IsA(HEHBRailingEndpointHandleProxy::StaticGetType()))
			{
				HEHBRailingEndpointHandleProxy* HandleProxy = static_cast<HEHBRailingEndpointHandleProxy*>(HitProxy);
				NewHoveredRailingEndpointHandleOwner = HandleProxy->Railing;
				NewHoveredRailingEndpointHandleKind = HandleProxy->HandleKind;
			}
			else if (HitProxy->IsA(HEHBStairBottomControlProxy::StaticGetType()))
			{
				HEHBStairBottomControlProxy* HandleProxy = static_cast<HEHBStairBottomControlProxy*>(HitProxy);
				NewHoveredStairBottomControl = HandleProxy->Stair;
			}
			else if (HitProxy->IsA(HEHBStairIntermediateControlProxy::StaticGetType()))
			{
				HEHBStairIntermediateControlProxy* HandleProxy = static_cast<HEHBStairIntermediateControlProxy*>(HitProxy);
				if (HandleProxy->Stair.Get() == SelectedStair.Get())
				{
					NewHoveredStairIntermediateControlIndex = HandleProxy->ControlIndex;
				}
			}
			else if (HitProxy->IsA(HEHBFloorSlabToolbarProxy::StaticGetType()))
			{
				HEHBFloorSlabToolbarProxy* ToolbarProxy = static_cast<HEHBFloorSlabToolbarProxy*>(HitProxy);
				NewHoveredFloorSlabToolbarCommandIndex = ToolbarProxy->CommandIndex;
			}
			else if (HitProxy->IsA(HEHBFloorToolbarProxy::StaticGetType()))
			{
				HEHBFloorToolbarProxy* ToolbarProxy = static_cast<HEHBFloorToolbarProxy*>(HitProxy);
				NewHoveredFloorToolbarCommandIndex = ToolbarProxy->CommandIndex;
			}
			else if (HitProxy->IsA(HEHBPillarConnectionToolbarProxy::StaticGetType()))
			{
				bNewHoveredPillarConnectionToolbarButton = true;
			}
		}
	}

	const bool bChanged =
		HoveredFloorSlabHandleOwner != NewHoveredFloorSlabHandleOwner
		|| HoveredFloorSlabHandleKind != NewHoveredFloorSlabHandleKind
		|| HoveredFloorSlabHandleLoopIndex != NewHoveredFloorSlabHandleLoopIndex
		|| HoveredFloorSlabHandleFirstIndex != NewHoveredFloorSlabHandleFirstIndex
		|| HoveredFloorSlabHandleSecondIndex != NewHoveredFloorSlabHandleSecondIndex
		|| HoveredFloorToolbarCommandIndex != NewHoveredFloorToolbarCommandIndex
		|| HoveredRoofHandleOwner != NewHoveredRoofHandleOwner
		|| HoveredRoofHandleKind != NewHoveredRoofHandleKind
		|| HoveredRoofHandleFirstIndex != NewHoveredRoofHandleFirstIndex
		|| HoveredRoofHandleSecondIndex != NewHoveredRoofHandleSecondIndex
		|| HoveredFloorSlabToolbarCommandIndex != NewHoveredFloorSlabToolbarCommandIndex
		|| bHoveredPillarConnectionToolbarButton != bNewHoveredPillarConnectionToolbarButton
		|| HoveredWallCurveControl != NewHoveredWallCurveControl
		|| HoveredRailingEndpointHandleOwner != NewHoveredRailingEndpointHandleOwner
		|| HoveredRailingEndpointHandleKind != NewHoveredRailingEndpointHandleKind
		|| HoveredStairBottomControl != NewHoveredStairBottomControl
		|| HoveredStairIntermediateControlIndex != NewHoveredStairIntermediateControlIndex;

	if (!bChanged)
	{
		return;
	}

	HoveredFloorSlabHandleOwner = NewHoveredFloorSlabHandleOwner;
	HoveredFloorSlabHandleKind = NewHoveredFloorSlabHandleKind;
	HoveredFloorSlabHandleLoopIndex = NewHoveredFloorSlabHandleLoopIndex;
	HoveredFloorSlabHandleFirstIndex = NewHoveredFloorSlabHandleFirstIndex;
	HoveredFloorSlabHandleSecondIndex = NewHoveredFloorSlabHandleSecondIndex;
	HoveredFloorToolbarCommandIndex = NewHoveredFloorToolbarCommandIndex;
	HoveredRoofHandleOwner = NewHoveredRoofHandleOwner;
	HoveredRoofHandleKind = NewHoveredRoofHandleKind;
	HoveredRoofHandleFirstIndex = NewHoveredRoofHandleFirstIndex;
	HoveredRoofHandleSecondIndex = NewHoveredRoofHandleSecondIndex;
	HoveredFloorSlabToolbarCommandIndex = NewHoveredFloorSlabToolbarCommandIndex;
	bHoveredPillarConnectionToolbarButton = bNewHoveredPillarConnectionToolbarButton;
	HoveredWallCurveControl = NewHoveredWallCurveControl;
	HoveredRailingEndpointHandleOwner = NewHoveredRailingEndpointHandleOwner;
	HoveredRailingEndpointHandleKind = NewHoveredRailingEndpointHandleKind;
	HoveredStairBottomControl = NewHoveredStairBottomControl;
	HoveredStairIntermediateControlIndex = NewHoveredStairIntermediateControlIndex;

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEasyHouseEditorMode::ClearHoveredControls()
{
	HoveredFloorSlabHandleOwner.Reset();
	HoveredFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	HoveredFloorSlabHandleLoopIndex = INDEX_NONE;
	HoveredFloorSlabHandleFirstIndex = INDEX_NONE;
	HoveredFloorSlabHandleSecondIndex = INDEX_NONE;
	HoveredFloorToolbarCommandIndex = INDEX_NONE;
	HoveredRoofHandleOwner.Reset();
	HoveredRoofHandleKind = EEHBRoofEditHandleKind::None;
	HoveredRoofHandleFirstIndex = INDEX_NONE;
	HoveredRoofHandleSecondIndex = INDEX_NONE;
	HoveredFloorSlabToolbarCommandIndex = INDEX_NONE;
	bHoveredPillarConnectionToolbarButton = false;
	HoveredWallCurveControl.Reset();
	HoveredRailingEndpointHandleOwner.Reset();
	HoveredRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	HoveredStairBottomControl.Reset();
	HoveredStairIntermediateControlIndex = INDEX_NONE;
}

bool FEasyHouseEditorMode::IsHoveredFloorSlabHandle(
	AEHB_FloorSlab* FloorSlab,
	EEHBFloorSlabEditHandleKind HandleKind,
	int32 FirstIndex,
	int32 SecondIndex,
	int32 LoopIndex) const
{
	return FloorSlab
		&& HoveredFloorSlabHandleOwner.Get() == FloorSlab
		&& HoveredFloorSlabHandleKind == HandleKind
		&& HoveredFloorSlabHandleLoopIndex == (HandleKind == EEHBFloorSlabEditHandleKind::Cutter ? INDEX_NONE : LoopIndex)
		&& HoveredFloorSlabHandleFirstIndex == FirstIndex
		&& (SecondIndex == INDEX_NONE || HoveredFloorSlabHandleSecondIndex == SecondIndex);
}

bool FEasyHouseEditorMode::IsHoveredRoofHandle(
	AEHBGableRoof* Roof,
	EEHBRoofEditHandleKind HandleKind,
	int32 FirstIndex,
	int32 SecondIndex) const
{
	return Roof
		&& HoveredRoofHandleOwner.Get() == Roof
		&& HoveredRoofHandleKind == HandleKind
		&& HoveredRoofHandleFirstIndex == FirstIndex
		&& (SecondIndex == INDEX_NONE || HoveredRoofHandleSecondIndex == SecondIndex);
}

void FEasyHouseEditorMode::DrawFloorSlabEditHandles(FPrimitiveDrawInterface* PDI) const
{
	AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!PDI || !FloorSlab || FloorSlab->LocalTopPolygon.Num() < 3)
	{
		return;
	}

	const FTransform SlabTransform = FloorSlab->GetActorTransform();
	const FVector CornerOffset = SlabTransform.TransformVectorNoScale(FVector(0.0f, 0.0f, 8.0f));
	const FVector EdgeOffset = SlabTransform.TransformVectorNoScale(FVector(0.0f, 0.0f, 6.0f));
	const FLinearColor SelectedColor(1.0f, 0.85f, 0.05f, 1.0f);
	const FLinearColor HoverColor(0.6f, 0.95f, 1.0f, 1.0f);
	const FLinearColor CornerColor(0.1f, 0.55f, 1.0f, 1.0f);
	const FLinearColor EdgeColor(0.0f, 0.8f, 0.9f, 1.0f);
	const FLinearColor SquareCutterColor(1.0f, 0.38f, 0.02f, 1.0f);
	const FLinearColor CircleCutterColor(0.0f, 0.78f, 0.95f, 1.0f);

	auto DrawLoopHandles = [&](const TArray<FVector>& Loop, int32 LoopIndex)
	{
		if (Loop.Num() < 3)
		{
			return;
		}

		for (int32 PointIndex = 0; PointIndex < Loop.Num(); ++PointIndex)
		{
			const int32 NextIndex = (PointIndex + 1) % Loop.Num();
			const bool bSelectedEdge =
				SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge
				&& SelectedFloorSlabHandleLoopIndex == LoopIndex
				&& SelectedFloorSlabHandleFirstIndex == PointIndex
				&& SelectedFloorSlabHandleSecondIndex == NextIndex;
			const bool bHoveredEdge = IsHoveredFloorSlabHandle(
				FloorSlab,
				EEHBFloorSlabEditHandleKind::Edge,
				PointIndex,
				NextIndex,
				LoopIndex);
			const FLinearColor DrawColor = bSelectedEdge ? SelectedColor : (bHoveredEdge ? HoverColor : EdgeColor);
			const float LineThickness = bSelectedEdge ? 8.0f : (bHoveredEdge ? 6.5f : 5.0f);
			const float MidPointSize = bSelectedEdge ? 19.0f : (bHoveredEdge ? 17.0f : 14.0f);
			const FVector A = SlabTransform.TransformPosition(Loop[PointIndex]) + EdgeOffset;
			const FVector B = SlabTransform.TransformPosition(Loop[NextIndex]) + EdgeOffset;

			PDI->SetHitProxy(new HEHBFloorSlabHandleProxy(FloorSlab, EEHBFloorSlabEditHandleKind::Edge, PointIndex, NextIndex, LoopIndex));
			PDI->DrawLine(A, B, DrawColor, SDPG_Foreground, LineThickness);
			PDI->DrawPoint(FMath::Lerp(A, B, 0.5f), DrawColor, MidPointSize, SDPG_Foreground);
			PDI->SetHitProxy(nullptr);
		}

		for (int32 PointIndex = 0; PointIndex < Loop.Num(); ++PointIndex)
		{
			const bool bSelectedCorner =
				SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner
				&& SelectedFloorSlabHandleLoopIndex == LoopIndex
				&& SelectedFloorSlabHandleFirstIndex == PointIndex;
			const bool bHoveredCorner = IsHoveredFloorSlabHandle(
				FloorSlab,
				EEHBFloorSlabEditHandleKind::Corner,
				PointIndex,
				INDEX_NONE,
				LoopIndex);
			const FVector WorldPoint = SlabTransform.TransformPosition(Loop[PointIndex]) + CornerOffset;

			PDI->SetHitProxy(new HEHBFloorSlabHandleProxy(FloorSlab, EEHBFloorSlabEditHandleKind::Corner, PointIndex, INDEX_NONE, LoopIndex));
			PDI->DrawPoint(
				WorldPoint,
				bSelectedCorner ? SelectedColor : (bHoveredCorner ? HoverColor : CornerColor),
				bSelectedCorner ? 22.0f : (bHoveredCorner ? 20.0f : 17.0f),
				SDPG_Foreground);
			PDI->SetHitProxy(nullptr);
		}
	};

	DrawLoopHandles(FloorSlab->LocalTopPolygon, INDEX_NONE);
	for (int32 HoleIndex = 0; HoleIndex < FloorSlab->LocalHoles.Num(); ++HoleIndex)
	{
		DrawLoopHandles(FloorSlab->LocalHoles[HoleIndex].LocalPolygon, HoleIndex);
	}
	for (int32 CutOperationIndex = 0; CutOperationIndex < FloorSlab->GetEditableCutOperationCount(); ++CutOperationIndex)
	{
		TArray<FVector> CutOperationLoop;
		if (FloorSlab->GetEditableCutOperationLoop(CutOperationIndex, CutOperationLoop))
		{
			DrawLoopHandles(CutOperationLoop, AEHB_FloorSlab::MakeCutOperationLoopIndex(CutOperationIndex));
		}
	}

	for (int32 CutterIndex = 0; CutterIndex < FloorSlab->PreviewCutters.Num(); ++CutterIndex)
	{
		const FEHBFloorSlabCutterData& CutterData = FloorSlab->PreviewCutters[CutterIndex];
		TArray<FVector> CutterLocalPoints;
		const float HalfSize = FMath::Max(1.0f, CutterData.Size) * 0.5f;
		if (CutterData.Shape == EEHBFloorSlabCutterShape::Circle)
		{
			const int32 SideCount = FMath::Clamp(CutterData.CircleSideCount, 8, 96);
			CutterLocalPoints.Reserve(SideCount);
			for (int32 SideIndex = 0; SideIndex < SideCount; ++SideIndex)
			{
				const float Angle = 2.0f * PI * static_cast<float>(SideIndex) / static_cast<float>(SideCount);
				CutterLocalPoints.Add(FVector(FMath::Cos(Angle) * HalfSize, FMath::Sin(Angle) * HalfSize, 0.0f));
			}
		}
		else
		{
			CutterLocalPoints = {
				FVector(-HalfSize, -HalfSize, 0.0f),
				FVector(-HalfSize, HalfSize, 0.0f),
				FVector(HalfSize, HalfSize, 0.0f),
				FVector(HalfSize, -HalfSize, 0.0f)
			};
		}

		if (CutterLocalPoints.Num() < 3)
		{
			continue;
		}

		const bool bSelectedCutter =
			SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter
			&& SelectedFloorSlabHandleFirstIndex == CutterIndex;
		const bool bHoveredCutter = IsHoveredFloorSlabHandle(FloorSlab, EEHBFloorSlabEditHandleKind::Cutter, CutterIndex);
		const FLinearColor CutterColor = bSelectedCutter
			? SelectedColor
			: (bHoveredCutter ? HoverColor : (CutterData.Shape == EEHBFloorSlabCutterShape::Circle ? CircleCutterColor : SquareCutterColor));
		const float MainLineThickness = bSelectedCutter ? 4.0f : (bHoveredCutter ? 3.25f : 2.5f);
		const float VerticalLineThickness = bSelectedCutter ? 3.0f : (bHoveredCutter ? 2.5f : 2.0f);
		const float CenterPointSize = bSelectedCutter ? 19.0f : (bHoveredCutter ? 17.0f : 14.0f);
		const float HalfHeight = FMath::Max(1.0f, CutterData.Height) * 0.5f;
		const FTransform CutterToWorld = CutterData.LocalTransform * SlabTransform;

		PDI->SetHitProxy(new HEHBFloorSlabHandleProxy(FloorSlab, EEHBFloorSlabEditHandleKind::Cutter, CutterIndex));
		for (int32 PointIndex = 0; PointIndex < CutterLocalPoints.Num(); ++PointIndex)
		{
			const FVector& LocalA = CutterLocalPoints[PointIndex];
			const FVector& LocalB = CutterLocalPoints[(PointIndex + 1) % CutterLocalPoints.Num()];
			const FVector BottomA = CutterToWorld.TransformPosition(FVector(LocalA.X, LocalA.Y, -HalfHeight));
			const FVector BottomB = CutterToWorld.TransformPosition(FVector(LocalB.X, LocalB.Y, -HalfHeight));
			const FVector TopA = CutterToWorld.TransformPosition(FVector(LocalA.X, LocalA.Y, HalfHeight));
			const FVector TopB = CutterToWorld.TransformPosition(FVector(LocalB.X, LocalB.Y, HalfHeight));
			PDI->DrawLine(BottomA, BottomB, CutterColor, SDPG_Foreground, MainLineThickness);
			PDI->DrawLine(TopA, TopB, CutterColor, SDPG_Foreground, MainLineThickness);
			PDI->DrawLine(BottomA, TopA, CutterColor, SDPG_Foreground, VerticalLineThickness);
		}
		PDI->DrawPoint(CutterToWorld.GetLocation(), CutterColor, CenterPointSize, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	}
}

void FEasyHouseEditorMode::DrawFloorSlabCornerSnapGuides(FPrimitiveDrawInterface* PDI) const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!PDI
		|| !FloorSlab
		|| !ActiveFloorSlabEditTransaction.IsValid()
		|| SelectedFloorSlabHandleKind != EEHBFloorSlabEditHandleKind::Corner)
	{
		return;
	}

	TArray<FVector> Loop;
	if (!FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop)
		|| Loop.Num() < 3
		|| !Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
	{
		return;
	}

	const int32 PointIndex = SelectedFloorSlabHandleFirstIndex;
	const int32 PreviousIndex = (PointIndex - 1 + Loop.Num()) % Loop.Num();
	const int32 NextIndex = (PointIndex + 1) % Loop.Num();
	const FTransform SlabTransform = FloorSlab->GetActorTransform();
	const FTransform CoordinateTransform = FloorSlab->OwningBuilding
		? FloorSlab->OwningBuilding->GetActorTransform()
		: FTransform::Identity;
	const FVector CurrentWorld = SlabTransform.TransformPosition(Loop[PointIndex]);
	const FVector CurrentCoordinate = CoordinateTransform.InverseTransformPosition(CurrentWorld);
	const FVector NeighborWorldLocations[] = {
		SlabTransform.TransformPosition(Loop[PreviousIndex]),
		SlabTransform.TransformPosition(Loop[NextIndex])
	};
	const FVector GuideOffset(0.0f, 0.0f, 14.0f);
	const FLinearColor XSnapColor(1.0f, 0.35f, 0.1f, 1.0f);
	const FLinearColor YSnapColor(0.8f, 0.15f, 1.0f, 1.0f);

	for (const FVector& NeighborWorld : NeighborWorldLocations)
	{
		const FVector NeighborCoordinate = CoordinateTransform.InverseTransformPosition(NeighborWorld);
		if (FMath::IsNearlyEqual(CurrentCoordinate.X, NeighborCoordinate.X, 0.05))
		{
			PDI->DrawLine(CurrentWorld + GuideOffset, NeighborWorld + GuideOffset, XSnapColor, SDPG_Foreground, 3.5f);
		}
		if (FMath::IsNearlyEqual(CurrentCoordinate.Y, NeighborCoordinate.Y, 0.05))
		{
			PDI->DrawLine(CurrentWorld + GuideOffset, NeighborWorld + GuideOffset, YSnapColor, SDPG_Foreground, 3.5f);
		}
	}
}

void FEasyHouseEditorMode::DrawFloorSlabHandleMeasurementHud(
	FViewport* Viewport,
	const FSceneView* View,
	FCanvas* Canvas) const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!Viewport
		|| !View
		|| !Canvas
		|| !FloorSlab
		|| !ActiveFloorSlabEditTransaction.IsValid()
		|| (SelectedFloorSlabHandleKind != EEHBFloorSlabEditHandleKind::Corner
			&& SelectedFloorSlabHandleKind != EEHBFloorSlabEditHandleKind::Edge))
	{
		return;
	}

	TArray<FVector> Loop;
	if (!FloorSlab->GetEditableLoopCopy(SelectedFloorSlabHandleLoopIndex, Loop)
		|| Loop.Num() < 3
		|| !Loop.IsValidIndex(SelectedFloorSlabHandleFirstIndex))
	{
		return;
	}

	const FTransform SlabTransform = FloorSlab->GetActorTransform();
	const FLinearColor SelectedLengthColor(0.0f, 0.95f, 0.35f, 0.98f);
	const FLinearColor AdjacentLengthColor(0.0f, 0.78f, 0.95f, 0.98f);
	const FLinearColor AngleColor(1.0f, 0.72f, 0.0f, 0.98f);
	const FVector HeightOffset(0.0f, 0.0f, 72.0f);

	auto DrawLength = [&](const FVector& Start, const FVector& End, const FLinearColor& Color)
	{
		const float Length = FVector::Dist2D(Start, End);
		if (Length <= 0.1f)
		{
			return;
		}

		DrawBuildingHudWorldLabel(
			Viewport,
			View,
			Canvas,
			(Start + End) * 0.5f + HeightOffset,
			FString::Printf(TEXT("L %.1fcm"), Length),
			Color);
	};

	auto DrawIncludedAngle = [&](const FVector& Endpoint, const FVector& FirstRay, const FVector& SecondRay, const FVector2D& PixelOffset)
	{
		const FVector DirectionA = FirstRay.GetSafeNormal2D();
		const FVector DirectionB = SecondRay.GetSafeNormal2D();
		if (DirectionA.IsNearlyZero() || DirectionB.IsNearlyZero())
		{
			return;
		}

		const float AngleDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(DirectionA, DirectionB), -1.0f, 1.0f)));
		DrawBuildingHudWorldLabel(
			Viewport,
			View,
			Canvas,
			Endpoint + (DirectionA + DirectionB).GetSafeNormal2D() * 32.0f + FVector(0.0f, 0.0f, 92.0f),
			FString::Printf(TEXT("A %.1f\u00B0"), AngleDegrees),
			AngleColor,
			PixelOffset);
	};

	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Corner)
	{
		const int32 PointIndex = SelectedFloorSlabHandleFirstIndex;
		const int32 PreviousIndex = (PointIndex - 1 + Loop.Num()) % Loop.Num();
		const int32 NextIndex = (PointIndex + 1) % Loop.Num();
		const FVector Previous = SlabTransform.TransformPosition(Loop[PreviousIndex]);
		const FVector Current = SlabTransform.TransformPosition(Loop[PointIndex]);
		const FVector Next = SlabTransform.TransformPosition(Loop[NextIndex]);

		DrawLength(Previous, Current, AdjacentLengthColor);
		DrawLength(Current, Next, SelectedLengthColor);
		DrawIncludedAngle(Current, Previous - Current, Next - Current, FVector2D(-42.0f, -32.0f));

		const FTransform CoordinateTransform = FloorSlab->OwningBuilding
			? FloorSlab->OwningBuilding->GetActorTransform()
			: FTransform::Identity;
		const FVector CurrentCoordinate = CoordinateTransform.InverseTransformPosition(Current);
		const FVector PreviousCoordinate = CoordinateTransform.InverseTransformPosition(Previous);
		const FVector NextCoordinate = CoordinateTransform.InverseTransformPosition(Next);
		const bool bXSnap =
			FMath::IsNearlyEqual(CurrentCoordinate.X, PreviousCoordinate.X, 0.05)
			|| FMath::IsNearlyEqual(CurrentCoordinate.X, NextCoordinate.X, 0.05);
		const bool bYSnap =
			FMath::IsNearlyEqual(CurrentCoordinate.Y, PreviousCoordinate.Y, 0.05)
			|| FMath::IsNearlyEqual(CurrentCoordinate.Y, NextCoordinate.Y, 0.05);
		if (bXSnap)
		{
			DrawBuildingHudWorldLabel(
				Viewport,
				View,
				Canvas,
				Current + FVector(0.0f, 0.0f, 122.0f),
				TEXT("X SNAP"),
				FLinearColor(1.0f, 0.35f, 0.1f, 0.98f),
				FVector2D(-92.0f, 0.0f));
		}
		if (bYSnap)
		{
			DrawBuildingHudWorldLabel(
				Viewport,
				View,
				Canvas,
				Current + FVector(0.0f, 0.0f, 122.0f),
				TEXT("Y SNAP"),
				FLinearColor(0.8f, 0.15f, 1.0f, 0.98f),
				FVector2D(12.0f, 0.0f));
		}
		return;
	}

	if (!Loop.IsValidIndex(SelectedFloorSlabHandleSecondIndex))
	{
		return;
	}

	const int32 FirstIndex = SelectedFloorSlabHandleFirstIndex;
	const int32 SecondIndex = SelectedFloorSlabHandleSecondIndex;
	const int32 PreviousIndex = (FirstIndex - 1 + Loop.Num()) % Loop.Num();
	const int32 NextIndex = (SecondIndex + 1) % Loop.Num();
	const FVector Previous = SlabTransform.TransformPosition(Loop[PreviousIndex]);
	const FVector First = SlabTransform.TransformPosition(Loop[FirstIndex]);
	const FVector Second = SlabTransform.TransformPosition(Loop[SecondIndex]);
	const FVector Next = SlabTransform.TransformPosition(Loop[NextIndex]);
	DrawLength(Previous, First, AdjacentLengthColor);
	DrawLength(First, Second, SelectedLengthColor);
	DrawLength(Second, Next, AdjacentLengthColor);
	DrawIncludedAngle(First, Previous - First, Second - First, FVector2D(-92.0f, -26.0f));
	DrawIncludedAngle(Second, First - Second, Next - Second, FVector2D(8.0f, -26.0f));
}

void FEasyHouseEditorMode::DrawRoofEditHandles(FPrimitiveDrawInterface* PDI) const
{
	AEHBGableRoof* Roof = SelectedRoof.Get();
	FEHBRoofProjectionBounds Bounds;
	float HandleZ = 0.0f;
	if (!PDI || !Roof || !GetRoofProjectionHandleData(Roof, Bounds, HandleZ))
	{
		return;
	}

	FVector LocalCorners[EHBRoofEdgeHandleCount];
	GetRoofProjectionHandleCorners(Bounds, HandleZ, LocalCorners);

	const FTransform RoofTransform = Roof->GetActorTransform();
	FVector WorldCorners[EHBRoofEdgeHandleCount];
	for (int32 CornerIndex = 0; CornerIndex < EHBRoofEdgeHandleCount; ++CornerIndex)
	{
		WorldCorners[CornerIndex] = RoofTransform.TransformPosition(LocalCorners[CornerIndex]);
	}

	const FLinearColor SelectedColor(1.0f, 0.85f, 0.05f, 1.0f);
	const FLinearColor HoverColor(0.6f, 0.95f, 1.0f, 1.0f);
	const FLinearColor EdgeColor(0.08f, 0.70f, 1.0f, 1.0f);
	const FLinearColor DirectionColor(0.2f, 0.45f, 1.0f, 0.9f);

	for (int32 EdgeIndex = 0; EdgeIndex < EHBRoofEdgeHandleCount; ++EdgeIndex)
	{
		const int32 NextIndex = (EdgeIndex + 1) % EHBRoofEdgeHandleCount;
		const bool bSelectedEdge =
			SelectedRoofHandleKind == EEHBRoofEditHandleKind::Edge
			&& SelectedRoofHandleFirstIndex == EdgeIndex;
		const bool bHoveredEdge = IsHoveredRoofHandle(
			Roof,
			EEHBRoofEditHandleKind::Edge,
			EdgeIndex,
			NextIndex);
		const FLinearColor DrawColor = bSelectedEdge ? SelectedColor : (bHoveredEdge ? HoverColor : EdgeColor);
		const float LineThickness = bSelectedEdge ? 8.0f : (bHoveredEdge ? 6.5f : 5.0f);
		const float PointSize = bSelectedEdge ? 21.0f : (bHoveredEdge ? 18.0f : 15.0f);
		const FVector WorldA = WorldCorners[EdgeIndex];
		const FVector WorldB = WorldCorners[NextIndex];
		const FVector WorldMid = FMath::Lerp(WorldA, WorldB, 0.5f);
		const FVector WorldAxis = RoofTransform.TransformVectorNoScale(GetRoofEdgeHandleLocalAxis(EdgeIndex)).GetSafeNormal();

		PDI->SetHitProxy(new HEHBRoofHandleProxy(Roof, EEHBRoofEditHandleKind::Edge, EdgeIndex, NextIndex));
		PDI->DrawLine(WorldA, WorldB, DrawColor, SDPG_Foreground, LineThickness);
		PDI->DrawPoint(WorldMid, DrawColor, PointSize, SDPG_Foreground);
		PDI->DrawLine(WorldMid, WorldMid + WorldAxis * 32.0f, DirectionColor, SDPG_Foreground, bSelectedEdge ? 4.0f : 3.0f);
		PDI->SetHitProxy(nullptr);
	}

	const FVector HeightHandleLocal = GetRoofHeightHandleLocalLocation(*Roof);
	const FVector HeightBaseLocal(
		HeightHandleLocal.X,
		HeightHandleLocal.Y,
		HeightHandleLocal.Z - EHBRoofHeightHandleLift);
	const FVector HeightHandleWorld = RoofTransform.TransformPosition(HeightHandleLocal);
	const FVector HeightBaseWorld = RoofTransform.TransformPosition(HeightBaseLocal);
	const bool bSelectedHeight = SelectedRoofHandleKind == EEHBRoofEditHandleKind::Height;
	const bool bHoveredHeight = IsHoveredRoofHandle(Roof, EEHBRoofEditHandleKind::Height, INDEX_NONE, INDEX_NONE);
	const FLinearColor HeightColor = bSelectedHeight ? SelectedColor : (bHoveredHeight ? HoverColor : FLinearColor(0.95f, 0.25f, 0.18f, 1.0f));
	const float HeightLineThickness = bSelectedHeight ? 6.0f : (bHoveredHeight ? 5.0f : 4.0f);
	const float HeightPointSize = bSelectedHeight ? 23.0f : (bHoveredHeight ? 20.0f : 17.0f);
	const FVector WorldRight = RoofTransform.TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
	const FVector WorldForward = RoofTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
	const float CrossHalfSize = bSelectedHeight ? 22.0f : 18.0f;

	PDI->SetHitProxy(new HEHBRoofHandleProxy(Roof, EEHBRoofEditHandleKind::Height, INDEX_NONE, INDEX_NONE));
	PDI->DrawLine(HeightBaseWorld, HeightHandleWorld, HeightColor, SDPG_Foreground, HeightLineThickness);
	PDI->DrawPoint(HeightHandleWorld, HeightColor, HeightPointSize, SDPG_Foreground);
	PDI->DrawLine(HeightHandleWorld - WorldRight * CrossHalfSize, HeightHandleWorld + WorldRight * CrossHalfSize, HeightColor, SDPG_Foreground, HeightLineThickness * 0.65f);
	PDI->DrawLine(HeightHandleWorld - WorldForward * CrossHalfSize, HeightHandleWorld + WorldForward * CrossHalfSize, HeightColor, SDPG_Foreground, HeightLineThickness * 0.65f);
	PDI->SetHitProxy(nullptr);
}

void FEasyHouseEditorMode::DrawFloorSlabCuttingToolbar(FEditorViewportClient* ViewportClient, FViewport* Viewport, FCanvas* Canvas) const
{
	AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!Viewport || !Canvas || !FloorSlab)
	{
		return;
	}

	struct FToolbarButton
	{
		FText Label;
		int32 CommandIndex = INDEX_NONE;
	};

	TArray<FToolbarButton> Buttons;
	if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge && IsFloorSlabHandleSelected())
	{
		Buttons.Add({ LOCTEXT("FloorSlabSplitEdgeHudButton", "切割边"), 3 });
	}
	else if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter && IsFloorSlabHandleSelected())
	{
		Buttons.Add({ LOCTEXT("FloorSlabConfirmCutHudButton", "确认切割"), 2 });
	}
	else if (SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::None)
	{
		Buttons.Add({ LOCTEXT("FloorSlabSquareCutHudButton", "方形切割"), 0 });
		Buttons.Add({ LOCTEXT("FloorSlabCircleCutHudButton", "圆形切割"), 1 });
	}

	const float ButtonWidth = 112.0f;
	const float ButtonHeight = 34.0f;
	const float Gap = 8.0f;
	const float Y = Viewport->GetSizeXY().Y - 84.0f;
	const FVector2D PanelPadding(8.0f, 8.0f);
	if (!Buttons.IsEmpty())
	{
		const float TotalWidth = Buttons.Num() * ButtonWidth + FMath::Max(0, Buttons.Num() - 1) * Gap;
		const float StartX = (Viewport->GetSizeXY().X - TotalWidth) * 0.5f;
		DrawBuildingHudPanel(
			Canvas,
			FVector2D(StartX, Y) - PanelPadding,
			FVector2D(TotalWidth, ButtonHeight) + PanelPadding * 2.0f);

		for (int32 ButtonIndex = 0; ButtonIndex < Buttons.Num(); ++ButtonIndex)
		{
			const float X = StartX + ButtonIndex * (ButtonWidth + Gap);
			const int32 CommandIndex = Buttons[ButtonIndex].CommandIndex;
			DrawBuildingHudButton(
				Canvas,
				new HEHBFloorSlabToolbarProxy(CommandIndex),
				FVector2D(X, Y),
				FVector2D(ButtonWidth, ButtonHeight),
				Buttons[ButtonIndex].Label,
				HoveredFloorSlabToolbarCommandIndex == CommandIndex);
		}
	}

	if (!FloorSlab->bIsFoundation && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::None)
	{
		constexpr int32 FillRoomCommandIndex = 4;
		const float RoomButtonWidth = 128.0f;
		const float RoomStartX = (Viewport->GetSizeXY().X - RoomButtonWidth) * 0.5f;
		const float RoomY = Y - 50.0f;
		DrawBuildingHudPanel(
			Canvas,
			FVector2D(RoomStartX, RoomY) - PanelPadding,
			FVector2D(RoomButtonWidth, ButtonHeight) + PanelPadding * 2.0f);
		DrawBuildingHudButton(
			Canvas,
			new HEHBFloorSlabToolbarProxy(FillRoomCommandIndex),
			FVector2D(RoomStartX, RoomY),
			FVector2D(RoomButtonWidth, ButtonHeight),
			LOCTEXT("FloorSlabFillRoomHudButton", "填充房间"),
			HoveredFloorSlabToolbarCommandIndex == FillRoomCommandIndex);
	}
}

void FEasyHouseEditorMode::DrawFloorFillToolbar(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FCanvas* Canvas) const
{
	(void)ViewportClient;
	if (!Viewport || !Canvas || !SelectedFloor.IsValid())
	{
		return;
	}

	constexpr int32 FillCommandIndex = 0;
	const FVector2D ButtonSize(128.0f, 34.0f);
	const FVector2D PanelPadding(8.0f, 8.0f);
	const FVector2D ButtonPosition(
		(Viewport->GetSizeXY().X - ButtonSize.X) * 0.5f,
		Viewport->GetSizeXY().Y - 84.0f);
	DrawBuildingHudPanel(
		Canvas,
		ButtonPosition - PanelPadding,
		ButtonSize + PanelPadding * 2.0f);
	DrawBuildingHudButton(
		Canvas,
		new HEHBFloorToolbarProxy(FillCommandIndex),
		ButtonPosition,
		ButtonSize,
		LOCTEXT("FloorFillRoomHudButton", "填充房间"),
		HoveredFloorToolbarCommandIndex == FillCommandIndex);
}

void FEasyHouseEditorMode::HandleFloorSlabToolbarCommand(int32 CommandIndex)
{
	AEHB_FloorSlab* FloorSlab = SelectedFloorSlab.Get();
	if (!FloorSlab)
	{
		return;
	}

 if(CommandIndex==3&&EHBSlabOpeningEdit::Supports(FloorSlab)&&SelectedFloorSlabHandleLoopIndex!=INDEX_NONE&&SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Edge)
 {
  if(FinishRegionDrag.bTracking||SlabCutterDraft.bTracking)return;
  const int32 Point=SelectedFloorSlabHandleSecondIndex,Loop=SelectedFloorSlabHandleLoopIndex;
  const auto Result=EHBSlabOpeningEdit::Execute(FloorSlab,Loop,SelectedFloorSlabHandleFirstIndex,Point,true);
  if(Result.bSucceeded)SelectFloorSlabHandle(FloorSlab,EEHBFloorSlabEditHandleKind::Corner,Point,INDEX_NONE,Loop);else{FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}return;
 }
 if(FEHBSlabCutterDraft::Supports(FloorSlab)&&(CommandIndex==0||CommandIndex==1||CommandIndex==2))
 {
  if(FinishRegionDrag.bTracking||SlabCutterDraft.bTracking)return;
  if(CommandIndex==2)
  {
   if(!SlabCutterDraft.IsFor(FloorSlab))return;const auto Result=SlabCutterDraft.Execute(false);
   if(Result.bSucceeded){SlabCutterDraft.Cancel();SelectFloorSlab(FloorSlab);}else{FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
  }
  else
  {
   CancelSelectedFloorSlabPreviewCutter();SelectFloorSlabHandle(FloorSlab,EEHBFloorSlabEditHandleKind::Cutter,INDEX_NONE);
   if(!SlabCutterDraft.Begin(FloorSlab,CommandIndex==0?EEHBFloorSlabCutterShape::Square:EEHBFloorSlabCutterShape::Circle))SelectFloorSlab(FloorSlab);
  }
  if(GEditor)GEditor->RedrawLevelEditingViewports();return;
 }
	if(EHBFinishRegionCommand::IsIndependent(FloorSlab))
	{
		if(FinishRegionDrag.bTracking)return;
		if(CommandIndex==4&&SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::None)
		{
			if(FillFloorSlabRoom(FloorSlab))SelectFloorSlab(FloorSlab);
			else {FNotificationInfo Notice(LOCTEXT("IndependentSlabFillRefused","填充未应用。请确认当前层板只覆盖一个房间，且没有重叠层板或未规划的关联构件。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
		}
		else if(CommandIndex==3&&SelectedFloorSlabHandleKind==EEHBFloorSlabEditHandleKind::Edge&&SelectedFloorSlabHandleLoopIndex==INDEX_NONE)
		{
			auto P=FloorSlab->LocalTopPolygon;const int32 A=SelectedFloorSlabHandleFirstIndex,C=SelectedFloorSlabHandleSecondIndex;
			if(!P.IsValidIndex(A)||!P.IsValidIndex(C)||(A+1)%P.Num()!=C)return;P.Insert((P[A]+P[C])*0.5,C);
			auto* B=FloorSlab->OwningBuilding.Get();const auto R=UEHBBuildingToolset::EditFinishRegion(B,FloorSlab->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(FloorSlab->ElementGuid),P,{},false);
			if(R.bSucceeded)SelectFloorSlabHandle(FloorSlab,EEHBFloorSlabEditHandleKind::Corner,C,INDEX_NONE,INDEX_NONE);
			else {FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(R));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
		}
		else {FNotificationInfo Notice(LOCTEXT("IndependentSlabOpeningPending","此轮廓操作尚未接入统一事务，本次操作保留原结果。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
		return;
	}
	if (CommandIndex == 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("CreateSquareFloorSlabCutterToolbarTransaction", "Create Square Floor Slab Cutter"));
		if (FloorSlab->AddSquarePreviewCutterInEditor())
		{
			SelectFloorSlabHandle(FloorSlab, EEHBFloorSlabEditHandleKind::Cutter, FloorSlab->PreviewCutters.Num() - 1);
		}
	}
	else if (CommandIndex == 1)
	{
		const FScopedTransaction Transaction(LOCTEXT("CreateCircleFloorSlabCutterToolbarTransaction", "Create Circle Floor Slab Cutter"));
		if (FloorSlab->AddCirclePreviewCutterInEditor())
		{
			SelectFloorSlabHandle(FloorSlab, EEHBFloorSlabEditHandleKind::Cutter, FloorSlab->PreviewCutters.Num() - 1);
		}
	}
	else if (CommandIndex == 2 && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Cutter)
	{
		const FScopedTransaction Transaction(LOCTEXT("CommitFloorSlabCuttersToolbarTransaction", "Commit Floor Slab Cutters"));
		if (FloorSlab->CommitPreviewCutters())
		{
			SelectFloorSlab(FloorSlab);
		}
	}
	else if (CommandIndex == 3 && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::Edge)
	{
		const FScopedTransaction Transaction(LOCTEXT("SplitFloorSlabEdgeToolbarTransaction", "Split Floor Slab Edge"));
		const int32 NewCornerIndex = SelectedFloorSlabHandleSecondIndex;
		if (FloorSlab->InsertCornerOnEdge(
			SelectedFloorSlabHandleLoopIndex,
			SelectedFloorSlabHandleFirstIndex,
			SelectedFloorSlabHandleSecondIndex))
		{
			SelectFloorSlabHandle(
				FloorSlab,
				EEHBFloorSlabEditHandleKind::Corner,
				NewCornerIndex,
				INDEX_NONE,
				SelectedFloorSlabHandleLoopIndex);
		}
	}
	else if (CommandIndex == 4 && SelectedFloorSlabHandleKind == EEHBFloorSlabEditHandleKind::None)
	{
		const FScopedTransaction Transaction(LOCTEXT("FillFloorSlabRoomToolbarTransaction", "Fill Floor Slab Room"));
		if (FillFloorSlabRoom(FloorSlab))
		{
			SelectFloorSlab(FloorSlab);
		}
	}
}

void FEasyHouseEditorMode::HandleFloorToolbarCommand(int32 CommandIndex)
{
	if (CommandIndex != 0)
	{
		return;
	}

	AEHB_Floor* Floor = SelectedFloor.Get();
	if (!Floor)
	{
		return;
	}

	if(EHBFinishRegionCommand::IsIndependent(Floor))
	{
		if(FillFloorRoom(Floor))SelectFloor(Floor);
		else {FNotificationInfo Notice(LOCTEXT("IndependentFloorFillRefused","填充未应用。请确认当前区域只覆盖一个房间，且没有重叠地面或未规划的关联构件。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);}
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("FillFloorRoomToolbarTransaction", "Fill Floor Room"));
	if (FillFloorRoom(Floor))
	{
		SelectFloor(Floor);
	}
}

bool FEasyHouseEditorMode::GetMousePointOnPlane(float PlaneZ, FVector& OutLocation) const
{
	FVector RayStart = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!GetViewportDropRay(RayStart, RayDirection, nullptr) || FMath::Abs(RayDirection.Z) <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const double DistanceAlongRay = (PlaneZ - RayStart.Z) / RayDirection.Z;
	if (DistanceAlongRay < 0.0)
	{
		return false;
	}

	OutLocation = RayStart + RayDirection * DistanceAlongRay;
	return true;
}

bool FEasyHouseEditorMode::GetViewportDropLocation(FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	FEditorViewportClient* EffectiveViewportClient = ViewportClient ? ViewportClient : GCurrentLevelEditingViewportClient;
	if (!EffectiveViewportClient || !EffectiveViewportClient->Viewport || !EffectiveViewportClient->GetWorld())
	{
		return false;
	}

	const FViewportCursorLocation CursorLocation = EffectiveViewportClient->GetCursorWorldLocationFromMousePos();
	const FVector RayStart = CursorLocation.GetOrigin();
	const FVector RayDirection = CursorLocation.GetDirection().GetSafeNormal();
	if (RayDirection.IsNearlyZero())
	{
		return false;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_WallCreationTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;

	const FVector RayEnd = RayStart + RayDirection * 1000000.0;
	if (EffectiveViewportClient->GetWorld()->LineTraceSingleByChannel(HitResult, RayStart, RayEnd, ECC_Visibility, QueryParams) && HitResult.bBlockingHit)
	{
		OutLocation = HitResult.ImpactPoint;
		return true;
	}

	const AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	const double PlaneZ = Building ? Building->GetActorLocation().Z : 0.0;
	if (FMath::Abs(RayDirection.Z) <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const double DistanceAlongRay = (PlaneZ - RayStart.Z) / RayDirection.Z;
	if (DistanceAlongRay < 0.0)
	{
		return false;
	}

	OutLocation = RayStart + RayDirection * DistanceAlongRay;
	return true;
}

bool FEasyHouseEditorMode::GetViewportDropRay(FVector& OutRayStart, FVector& OutRayDirection, const FPointerEvent* MouseEvent) const
{
	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport || !GCurrentLevelEditingViewportClient->GetWorld())
	{
		return false;
	}

	if (MouseEvent)
	{
		if (FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(GCurrentLevelEditingViewportClient->Viewport))
		{
			if (TSharedPtr<SViewport> ViewportWidget = SceneViewport->GetViewportWidget().Pin())
			{
				const FVector2D LocalMousePosition = ViewportWidget->GetCachedGeometry().AbsoluteToLocal(MouseEvent->GetScreenSpacePosition());
				const FIntPoint ViewportSize = SceneViewport->GetSizeXY();
				if (ViewportSize.X > 0 && ViewportSize.Y > 0)
				{
					SceneViewport->SetMouse(
						FMath::Clamp(FMath::RoundToInt(LocalMousePosition.X), 0, ViewportSize.X - 1),
						FMath::Clamp(FMath::RoundToInt(LocalMousePosition.Y), 0, ViewportSize.Y - 1));
				}
			}
		}
	}

	const FViewportCursorLocation CursorLocation = GCurrentLevelEditingViewportClient->GetCursorWorldLocationFromMousePos();
	OutRayStart = CursorLocation.GetOrigin();
	OutRayDirection = CursorLocation.GetDirection().GetSafeNormal();
	return !OutRayDirection.IsNearlyZero();
}

bool FEasyHouseEditorMode::GetViewportDropHitResult(FHitResult& OutHitResult, const FPointerEvent* MouseEvent) const
{
	FVector RayStart = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!GetViewportDropRay(RayStart, RayDirection, MouseEvent) || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->GetWorld())
	{
		return false;
	}

	const FVector RayEnd = RayStart + RayDirection * HALF_WORLD_MAX;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_DoorWindowDropTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;
	if (AActor* PreviewActor = PreviewDoorWindowActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	if (AActor* PreviewActor = PreviewFloorSlabActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	if (AActor* PreviewActor = PreviewRoofActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	if (AActor* PreviewActor = PreviewStairActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}

	return GCurrentLevelEditingViewportClient->GetWorld()->LineTraceSingleByChannel(
		OutHitResult,
		RayStart,
		RayEnd,
		ECC_Visibility,
		QueryParams);
}

void FEasyHouseEditorMode::CaptureWallCreationStart(FViewport* Viewport)
{
	WallCreationStartLocation = WallCreationMouseLocation;
	WallCreationStartPillar = HoveredWallCreationPillar;
	WallCreationStartWall = HoveredWallCreationWall;
	WallCreationStartWallDistance = HoveredWallCreationWallDistance;
	WallCreationStartPillarFloorIndex = HoveredWallCreationPillarFloorIndex;
	WallCreationStartNode = HoveredWallCreationNode;
	WallCreationStartNodeRevision = HoveredWallCreationNodeRevision;
	bWallCreationStartTopSnap = bHoveredWallCreationTopSnap;
	bWallCreationDragging = true;
	bWallCreationRectangleDragLatched = IsWallCreationRectangleModeActive(Viewport);
}

void FEasyHouseEditorMode::ResetWallCreationDragState()
{
	bWallCreationDragging = false;
	WallCreationStartPillar.Reset(); WallCreationStartWall.Reset();
	WallCreationStartNode.Invalidate(); WallCreationStartNodeRevision = INDEX_NONE;
	WallCreationStartWallDistance = 0;
	WallCreationStartPillarFloorIndex = 1;
	bWallCreationStartTopSnap = false;
	bWallCreationRectangleDragLatched = false;
 bWallCreationRectangleConstrained = false;
}

bool FEasyHouseEditorMode::ResolveWallCreationLogicalNodeSnap(const AEHBBuildingActorBase* Building,
	const FVector& DesiredWorldLocation, int32 FloorIndex, const FHitResult* Hit,
	FWallCreationEndpointSnap& OutSnap) const
{
	OutSnap = {};
	if (!IsValid(Building) || Building->WallNodeAuthority.Version != 2) return false;
	FGuid NodeId;
	if (const auto* Junction = Hit ? Cast<UEHBWallJunctionComponent>(Hit->GetComponent()) : nullptr)
	{
		if (Junction->GetOwner() != Building || !Junction->IsRegistered()) return false;
		NodeId = Junction->NodeGuid;
	}
	else
	{
		FEHBWallCreationEndpoint Endpoint;
		if (!Building->ResolveWallCreationEndpoint(DesiredWorldLocation, FMath::Max(30.0f, WallCreationThickness * 2),
			WallCreationThickness, FloorIndex, nullptr, Endpoint)) return false;
		NodeId = Endpoint.NodeGuid;
	}
	const auto* Node = Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid == NodeId;});
	if (!Node || Building->FindPhysicalPillarForNode(NodeId).IsValid()) return false;
	OutSnap.NodeGuid = NodeId; OutSnap.ExpectedNodeRevision = Node->GeometryRevision;
	OutSnap.LocalLocation = Node->LocalTransform.GetLocation();
	OutSnap.WorldLocation = Building->GetActorTransform().TransformPosition(OutSnap.LocalLocation);
	OutSnap.FloorIndex = Node->FloorIndex;
	return true;
}

void FEasyHouseEditorMode::BuildWallCreationRectangleEndpoints(const AEHBBuildingActorBase* Building,
	const TArray<FVector>& LocalCorners, TArray<FWallCreationEndpointSnap>& OutEndpoints) const
{
	OutEndpoints.Reset();
	for (int32 I = 0; I < LocalCorners.Num(); ++I)
	{
		auto& E = OutEndpoints.AddDefaulted_GetRef();
		E.LocalLocation = LocalCorners[I]; E.WorldLocation = Building->GetActorTransform().TransformPosition(E.LocalLocation);
		E.FloorIndex = FMath::Max(1, WallCreationStartPillarFloorIndex);
		// Keep the captured pose and revision. Re-resolving the first corner at release
		// would silently replace a stale logical anchor with a free physical column.
		if ((I == 0 && WallCreationStartNode.IsValid()) || (I == 2 && HoveredWallCreationNode.IsValid()))
		{
			E.NodeGuid = I == 0 ? WallCreationStartNode : HoveredWallCreationNode;
			E.ExpectedNodeRevision = I == 0 ? WallCreationStartNodeRevision : HoveredWallCreationNodeRevision;
			E.FloorIndex = I == 0 ? WallCreationStartPillarFloorIndex : HoveredWallCreationPillarFloorIndex;
			// World position retains the selected node height, allowing the planner to
			// reject a rectangle whose end node lies on another floor.
			E.WorldLocation = I == 0 ? WallCreationStartLocation : WallCreationMouseLocation;
		}
		else
		{
			FWallCreationEndpointSnap Snapped;
			if ((!bWallCreationRectangleConstrained || I==0) && ResolveWallCreationEndpointSnap(Building, E.WorldLocation, nullptr, Snapped)) E = Snapped;
		}
	}
}

bool FEasyHouseEditorMode::UpdateWallCreationMouseLocation(FEditorViewportClient* ViewportClient)
{
	if (!bWallCreationToolActive)
	{
		return false;
	}

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	AEHB_Pillar* HitPillar = nullptr;
	AEHB_Wall* HitWall = nullptr;
	float HitWallDistance = 0.0f;
	FVector TopSnapLocation = FVector::ZeroVector;
	int32 TopSnapPillarFloorIndex = 1;
	bool bHasTopSnapLocation = false;
	bool bUseTopSnap = false;
	bool bShouldResolvePillarBaseSnap = false;
	FWallCreationEndpointSnap LogicalSnap;
	bool bLogicalSnap = false;
	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
	{
		FEHBPillarCreationTopSnapResult TopSnapResult;
		// A derived junction is an existing logical corner, not an upper-floor support.
		if (Cast<UEHBWallJunctionComponent>(HitResult.GetComponent())
			&& ResolveWallCreationLogicalNodeSnap(Building, HitResult.ImpactPoint, 1, &HitResult, LogicalSnap))
		{
			bLogicalSnap = true;
		}
		else if (ResolvePillarCreationTopSnap(HitResult, Building, TopSnapResult))
		{
			TopSnapLocation = TopSnapResult.WorldLocation;
			TopSnapPillarFloorIndex = TopSnapResult.PillarFloorIndex;
			bHasTopSnapLocation = true;
			bUseTopSnap = Cast<AEHB_FloorSlab>(TopSnapResult.SourceActor.Get()) == nullptr;
			bShouldResolvePillarBaseSnap = true;
		}
		else
		{
			HitPillar = ResolvePillarFromHit(HitResult);
			if (!HitPillar
				|| HitPillar->OwningBuilding != Building
				|| (bWallCreationDragging && HitPillar == WallCreationStartPillar.Get()))
			{
				HitPillar = nullptr;
			}

			if (!HitPillar)
			{
				HitWall = ResolveWallFromHit(HitResult);
				if (HitWall && HitWall->OwningBuilding == Building && !HitWall->IsActorBeingDestroyed())
				{
					HitWallDistance = HitWall->CalculateDistanceFromStartForWorldLocation(HitResult.ImpactPoint);
					const float WallLength = FVector::Dist2D(HitWall->LocalStart, HitWall->LocalEnd);
					const float MinWallSnapDistance = FMath::Max(10.0f, FMath::Max(1.0f, WallCreationThickness) * 0.5f + 1.0f);
					if (WallLength <= MinWallSnapDistance * 2.0f
						|| HitWallDistance <= MinWallSnapDistance
						|| HitWallDistance >= WallLength - MinWallSnapDistance)
					{
						HitWall = nullptr;
						HitWallDistance = 0.0f;
					}
				}
				else
				{
					HitWall = nullptr;
					HitWallDistance = 0.0f;
				}
			}
		}
	}

	FVector DropLocation = FVector::ZeroVector;
	if (bLogicalSnap)
	{
		DropLocation = LogicalSnap.WorldLocation;
	}
	else if (bHasTopSnapLocation)
	{
		DropLocation = TopSnapLocation;
	}
	else if (HitPillar)
	{
		DropLocation = HitPillar->GetActorLocation();
	}
	else if (HitWall)
	{
		DropLocation = HitWall->GetWorldLocationOnCenterAxisAtDistance(HitWallDistance, 0.0f);
	}
	else if (!GetViewportDropLocation(ViewportClient, DropLocation))
	{
		return false;
	}

	if (!bLogicalSnap && (bShouldResolvePillarBaseSnap || (!bUseTopSnap && !HitPillar && !HitWall))
		&& ResolvePillarBaseSnapNearLocation(
			Building,
			DropLocation,
			WallCreationThickness,
			WallCreationThickness,
			45.0f,
			DropLocation,
			TopSnapPillarFloorIndex))
	{
		HitPillar = nullptr;
		HitWall = nullptr;
		HitWallDistance = 0.0f;
		bUseTopSnap = true;
	}

	const int32 CandidateFloor = bUseTopSnap ? TopSnapPillarFloorIndex
		: HitPillar ? HitPillar->FloorIndex : HitWall ? HitWall->FloorIndex
		: bWallCreationDragging ? WallCreationStartPillarFloorIndex : 1;
	if (!bLogicalSnap && !bUseTopSnap && !HitPillar)
		bLogicalSnap = ResolveWallCreationLogicalNodeSnap(Building, DropLocation, CandidateFloor, nullptr, LogicalSnap);
	if (bLogicalSnap)
	{
		DropLocation = LogicalSnap.WorldLocation;
		HitPillar = nullptr; HitWall = nullptr; HitWallDistance = 0;
	}

	if (!HitPillar && !bLogicalSnap)
	{
		DropLocation = SnapWorldLocationToIntegerBuildingCoordinates(Building, DropLocation);
		if (HitWall)
		{
			HitWallDistance = HitWall->CalculateDistanceFromStartForWorldLocation(DropLocation);
			DropLocation = HitWall->GetWorldLocationOnCenterAxisAtDistance(HitWallDistance, 0.0f);
		}
	}

	if (bWallCreationDragging
		&& !IsWallCreationRectangleModeActive(ViewportClient ? ViewportClient->Viewport : nullptr)
		&& !HitPillar
		&& !bLogicalSnap
		&& !bUseTopSnap)
	{
		if (HitWall)
		{
			const float MinWallSnapDistance =
				FMath::Max(10.0f, FMath::Max(1.0f, WallCreationThickness) * 0.5f + 1.0f);
			SnapWallEndpointPerpendicularToTargetWall(
				Building,
				HitWall,
				WallCreationStartLocation,
				MinWallSnapDistance,
				HitWallDistance,
				DropLocation);
		}
		else
		{
			DropLocation = ResolveCreationFreeEnd(WallCreationStartLocation, DropLocation, ViewportClient);
		}
	}

 bWallCreationRectangleConstrained=false;
 if(bWallCreationDragging&&IsWallCreationRectangleModeActive(ViewportClient?ViewportClient->Viewport:nullptr)
  &&!HitPillar&&!HitWall&&!bLogicalSnap&&!bUseTopSnap)
  DropLocation=ResolveCreationRectangleEnd(WallCreationStartLocation,DropLocation,Building->GetActorTransform(),ViewportClient);
	WallCreationMouseLocation = DropLocation;
	HoveredWallCreationPillar = HitPillar;
	HoveredWallCreationWall = HitWall;
	HoveredWallCreationWallDistance = HitWallDistance;
	HoveredWallCreationPillarFloorIndex = bLogicalSnap ? LogicalSnap.FloorIndex : CandidateFloor;
	HoveredWallCreationNode = bLogicalSnap ? LogicalSnap.NodeGuid : FGuid();
	HoveredWallCreationNodeRevision = bLogicalSnap ? LogicalSnap.ExpectedNodeRevision : INDEX_NONE;
	bHoveredWallCreationTopSnap = bUseTopSnap;
	return true;
}

bool FEasyHouseEditorMode::FinishWallCreationSinglePillar()
{
	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		CancelWallCreation();
		return false;
	}
	if (WallCreationStartNode.IsValid())
	{
		const FGuid NodeId = WallCreationStartNode;
		const auto* Node = Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid == NodeId;});
		const bool bCurrent = Node && Building->WallNodeAuthority.Version == 2
			&& Node->GeometryRevision == WallCreationStartNodeRevision
			&& Node->FloorIndex == WallCreationStartPillarFloorIndex
			&& Building->GetActorTransform().TransformPosition(Node->LocalTransform.GetLocation()).Equals(WallCreationStartLocation, 0.001)
			&& !Building->FindPhysicalPillarForNode(NodeId).IsValid();
		ResetWallCreationDragState();
		return bCurrent && SelectUnboundWallNode(Building, NodeId);
	}

	AEHB_Pillar* ExistingStartPillar = WallCreationStartPillar.Get();
	if (ExistingStartPillar && ExistingStartPillar->OwningBuilding != Building)
	{
		ExistingStartPillar = nullptr;
	}

	if (ExistingStartPillar)
	{
		ResetWallCreationDragState();
		if (GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(ExistingStartPillar, true, true, true);
			GEditor->RedrawLevelEditingViewports();
		}
		return true;
	}

	AEHB_Wall* ExistingStartWall = WallCreationStartWall.Get();
	if (ExistingStartWall && (ExistingStartWall->OwningBuilding != Building || ExistingStartWall->IsActorBeingDestroyed()))
	{
		ExistingStartWall = nullptr;
	}

	if (ExistingStartWall && Building->WallNodeAuthority.Version==2)
	{
		const auto Result=EHBWallCreationCommand::InsertNodeOnWall(Building,ExistingStartWall,WallCreationStartWallDistance,WallCreationHeight,WallCreationThickness,ShouldCreateWallColumns());
		ResetWallCreationDragState();
		if (!Result.bSucceeded)
		{
			const FText Message=Result.Status==TEXT("WallColumnDimensionsRequirePlan")
				? LOCTEXT("OptionalSplitColumnDimensions","当前墙上插柱需与墙高、墙厚一致；不同尺寸的铺面联动尚未适配。")
				: FText::Format(LOCTEXT("OptionalSplitRejected","未能在墙上插入柱子：{0}。"),FText::FromName(Result.Status));
			FNotificationInfo Notice(Message);Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);return false;
		}
		if (GEditor && !Result.Endpoints.IsEmpty() && Result.Endpoints[0].Pillar)
		{
			GEditor->SelectNone(false,true,false);GEditor->SelectActor(Result.Endpoints[0].Pillar,true,true,true);GEditor->RedrawLevelEditingViewports();
		}
		else if(!Result.Endpoints.IsEmpty())return SelectUnboundWallNode(Building,Result.Endpoints[0].NodeGuid);
		return true;
	}
	// A free single click in wall-only mode does not create an orphan logical node.
	if(!ShouldCreateWallColumns()){ResetWallCreationDragState();return true;}
	const FScopedTransaction Transaction(LOCTEXT("CreateSinglePillarByClickTransaction", "Create Pillar By Click"));
	Building->Modify();

	TArray<AEHB_Wall*> SplitWalls;
	AEHB_Pillar* CreatedPillar = nullptr;
	if (ExistingStartWall)
	{
		CreatedPillar = Building->InsertPillarOnWall(
			ExistingStartWall,
			WallCreationStartWallDistance,
			WallCreationHeight,
			WallCreationThickness,
			SplitWalls);
		if (CreatedPillar)
		{
			SnapPillarToIntegerBuildingCoordinates(CreatedPillar, true);
			Building->RefreshWallsConnectedToPillar(CreatedPillar->ElementGuid, true);
		}
	}
	else
	{
		const FVector LocalLocation = Building->GetActorTransform().InverseTransformPosition(WallCreationStartLocation);
		CreatedPillar = CreateSimplePillar(
			Building,
			LocalLocation,
			FRotator::ZeroRotator,
			TEXT("EHB_Pillar"),
			WallCreationStartPillarFloorIndex);
	}

	if (!CreatedPillar)
	{
		ResetWallCreationDragState();
		return false;
	}

	Building->MarkPackageDirty();
	ResetWallCreationDragState();

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(CreatedPillar, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

bool FEasyHouseEditorMode::FinishWallCreationDrag()
{
	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		CancelWallCreation();
		return false;
	}

	AEHB_Pillar* ExistingStartPillar = WallCreationStartPillar.Get();
	if (ExistingStartPillar && ExistingStartPillar->OwningBuilding != Building)
	{
		ExistingStartPillar = nullptr;
	}

	AEHB_Pillar* ExistingEndPillar = HoveredWallCreationPillar.Get();
	if (ExistingEndPillar
		&& (ExistingEndPillar->OwningBuilding != Building || ExistingEndPillar == ExistingStartPillar))
	{
		ExistingEndPillar = nullptr;
	}

	AEHB_Wall* ExistingStartWall = ExistingStartPillar ? nullptr : WallCreationStartWall.Get();
	if (ExistingStartWall && (ExistingStartWall->OwningBuilding != Building || ExistingStartWall->IsActorBeingDestroyed()))
	{
		ExistingStartWall = nullptr;
	}

	AEHB_Wall* ExistingEndWall = ExistingEndPillar ? nullptr : HoveredWallCreationWall.Get();
	if (ExistingEndWall && (ExistingEndWall->OwningBuilding != Building || ExistingEndWall->IsActorBeingDestroyed()))
	{
		ExistingEndWall = nullptr;
	}


	const FVector StartLocation = ExistingStartPillar ? ExistingStartPillar->GetActorLocation() : WallCreationStartLocation;
	const FVector EndLocation = ExistingEndPillar ? ExistingEndPillar->GetActorLocation() : WallCreationMouseLocation;
	if (FVector::Dist2D(StartLocation, EndLocation) <= 10.0f)
	{
		return FinishWallCreationSinglePillar();
	}

	const FVector LocalStart = ExistingStartPillar
		? ExistingStartPillar->GetElementLocalTransform().GetLocation()
		: Building->GetActorTransform().InverseTransformPosition(StartLocation);
	const FVector LocalEnd = ExistingEndPillar
		? ExistingEndPillar->GetElementLocalTransform().GetLocation()
		: Building->GetActorTransform().InverseTransformPosition(EndLocation);
	const FVector LocalDirection = (LocalEnd - LocalStart).GetSafeNormal2D();
	if (LocalDirection.IsNearlyZero())
	{
		ResetWallCreationDragState();
		return false;
	}

	FVector LocalWallStart = FVector::ZeroVector;
	FVector LocalWallEnd = FVector::ZeroVector;
	if (!GetWallSegmentBetweenPillarSides(LocalStart, LocalEnd, WallCreationThickness, LocalWallStart, LocalWallEnd))
	{
		ResetWallCreationDragState();
		return false;
	}



	FWallCreationEndpointSnap StartEndpoint;
	StartEndpoint.WorldLocation = StartLocation;
	StartEndpoint.LocalLocation = LocalStart;
	StartEndpoint.Pillar = ExistingStartPillar;
	StartEndpoint.Wall = ExistingStartWall;
	StartEndpoint.WallDistance = WallCreationStartWallDistance;
	StartEndpoint.FloorIndex = FMath::Max(1, WallCreationStartPillarFloorIndex);
	StartEndpoint.NodeGuid = WallCreationStartNode;
	StartEndpoint.ExpectedNodeRevision = WallCreationStartNodeRevision;

	FWallCreationEndpointSnap EndEndpoint;
	EndEndpoint.WorldLocation = EndLocation;
	EndEndpoint.LocalLocation = LocalEnd;
	EndEndpoint.Pillar = ExistingEndPillar;
	EndEndpoint.Wall = ExistingEndWall;
	EndEndpoint.WallDistance = HoveredWallCreationWallDistance;
	EndEndpoint.FloorIndex = FMath::Max(1, HoveredWallCreationPillarFloorIndex);
	EndEndpoint.NodeGuid = HoveredWallCreationNode;
	EndEndpoint.ExpectedNodeRevision = HoveredWallCreationNodeRevision;

	// Independent command owns recovery; do not retry a failed mutation through legacy fallbacks.
 {
  AEHB_Wall* PrimaryWall = nullptr;
  const bool bSucceeded = CommitWallCreationPath(Building, {StartEndpoint, EndEndpoint}, false, PrimaryWall);
  ResetWallCreationDragState();
  if (!bSucceeded) return false;
  if (GEditor)
  {
   GEditor->SelectNone(false, true, false);
   GEditor->SelectActor(PrimaryWall ? Cast<AActor>(PrimaryWall) : Cast<AActor>(Building), true, true, true);
   GEditor->RedrawLevelEditingViewports();
  }
  if (PrimaryWall) SelectWall(PrimaryWall);
  return true;
 }
}

bool FEasyHouseEditorMode::ResolveWallCreationEndpointSnap(
	const AEHBBuildingActorBase* Building,
	const FVector& DesiredWorldLocation,
	const AEHB_Pillar* IgnoredPillar,
	FWallCreationEndpointSnap& OutSnap) const
{
	OutSnap = FWallCreationEndpointSnap();
	if (!Building || !Building->GetWorld())
	{
		return false;
	}

	const float SnapDistance = FMath::Max(30.0f, FMath::Max(1.0f, WallCreationThickness) * 2.0f);
	FVector FreeWorldLocation = SnapWorldLocationToIntegerBuildingCoordinates(Building, DesiredWorldLocation);
	if (ResolvePillarCreationFloorSlabCornerSnap(
		Building,
		FreeWorldLocation,
		WallCreationThickness,
		WallCreationThickness,
		SnapDistance,
		FreeWorldLocation))
	{
		FreeWorldLocation = SnapWorldLocationToIntegerBuildingCoordinates(Building, FreeWorldLocation);
	}

	FEHBWallCreationEndpoint Endpoint;
	if (!Building->ResolveWallCreationEndpoint(
		FreeWorldLocation,
		SnapDistance,
		WallCreationThickness,
		WallCreationStartPillarFloorIndex,
		IgnoredPillar,
		Endpoint))
	{
		return false;
	}

	OutSnap.WorldLocation = Endpoint.WorldLocation;
	OutSnap.LocalLocation = Endpoint.LocalLocation;
	OutSnap.Pillar = Endpoint.Pillar;
	OutSnap.Wall = Endpoint.Wall;
	OutSnap.WallDistance = Endpoint.WallDistance;
	OutSnap.FloorIndex = Endpoint.FloorIndex;
	OutSnap.NodeGuid=Endpoint.NodeGuid;OutSnap.ExpectedNodeRevision=Endpoint.ExpectedNodeRevision;
	return OutSnap.Pillar != nullptr || OutSnap.Wall != nullptr || OutSnap.NodeGuid.IsValid();
}

bool FEasyHouseEditorMode::CommitWallCreationPath(AEHBBuildingActorBase* Building,
 const TArray<FWallCreationEndpointSnap>& Endpoints, bool bClosed, AEHB_Wall*& OutPrimaryWall) const
{
 TArray<FEHBWallCreationEndpoint> RuntimeEndpoints;
 for (const auto& Endpoint : Endpoints)
 {
  auto& Runtime = RuntimeEndpoints.AddDefaulted_GetRef();
  Runtime.WorldLocation = Endpoint.WorldLocation; Runtime.LocalLocation = Endpoint.LocalLocation;
  Runtime.Pillar = Endpoint.Pillar; Runtime.Wall = Endpoint.Wall;
  Runtime.WallDistance = Endpoint.WallDistance; Runtime.FloorIndex = Endpoint.FloorIndex;
  Runtime.NodeGuid=Endpoint.NodeGuid;Runtime.ExpectedNodeRevision=Endpoint.ExpectedNodeRevision;
 }
 FEHBWallCreationOptions Options;
 // Mouse resolution already applies host/grid/drawing assistance. Preserve that exact preview.
 Options.bSnapToIntegerBuildingCoordinates=false;
 Options.bCreatePhysicalColumns=ShouldCreateWallColumns();
 Options.WallHeight = Options.PillarHeight = WallCreationHeight;
 Options.WallThickness = Options.PillarWidth = Options.PillarDepth = WallCreationThickness;
 Options.FloorIndex = FMath::Max(1, WallCreationStartPillarFloorIndex);
 Options.NewPillarNamePrefix = TEXT("EHB_WallPathPillar");
 const bool bHasWallEndpoint = RuntimeEndpoints.ContainsByPredicate([](const auto& E){return E.Wall!=nullptr;});
 const auto Result = bHasWallEndpoint
  ? EHBWallCreationCommand::CommitAnchoredPath(Building,RuntimeEndpoints,bClosed,Options)
  : EHBWallCreationCommand::Commit(Building,RuntimeEndpoints,bClosed,Options);
 OutPrimaryWall = Result.PrimaryWall;
 if (!Result.bSucceeded) UE_LOG(LogTemp, Warning, TEXT("EHB wall creation: %s %s"), *Result.Status.ToString(), *Result.FailureReason.ToString());
 return Result.bSucceeded;
}

bool FEasyHouseEditorMode::FinishWallCreationRectangleDrag()
{
	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		CancelWallCreation();
		return false;
	}

	const FTransform BuildingTransform = Building->GetActorTransform();
	FVector LocalStart = BuildingTransform.InverseTransformPosition(WallCreationStartLocation);
	FVector LocalEnd = BuildingTransform.InverseTransformPosition(WallCreationMouseLocation);
	LocalEnd.Z = LocalStart.Z;

	const float Width = FMath::Abs(LocalEnd.X - LocalStart.X);
	const float Depth = FMath::Abs(LocalEnd.Y - LocalStart.Y);
	const float MinRectangleSide = FMath::Max(10.0f, WallCreationThickness + 1.0f);
	if (Width <= MinRectangleSide || Depth <= MinRectangleSide)
	{
		return FinishWallCreationDrag();
	}

	const FVector LocalCorner0(LocalStart.X, LocalStart.Y, LocalStart.Z);
	const FVector LocalCorner1(LocalEnd.X, LocalStart.Y, LocalStart.Z);
	const FVector LocalCorner2(LocalEnd.X, LocalEnd.Y, LocalStart.Z);
	const FVector LocalCorner3(LocalStart.X, LocalEnd.Y, LocalStart.Z);
	const TArray<FVector> LocalCorners = { LocalCorner0, LocalCorner1, LocalCorner2, LocalCorner3 };

	TArray<FWallCreationEndpointSnap> CornerSnaps;
	BuildWallCreationRectangleEndpoints(Building, LocalCorners, CornerSnaps);

	auto GetResolvedCornerLocalLocation = [](const FWallCreationEndpointSnap& CornerSnap) -> FVector
	{
		if (CornerSnap.Pillar && !CornerSnap.Pillar->IsActorBeingDestroyed())
		{
			return CornerSnap.Pillar->GetElementLocalTransform().GetLocation();
		}

		return CornerSnap.LocalLocation;
	};

	for (int32 EdgeIndex = 0; EdgeIndex < CornerSnaps.Num(); ++EdgeIndex)
	{
		const FWallCreationEndpointSnap& StartCorner = CornerSnaps[EdgeIndex];
		const FWallCreationEndpointSnap& EndCorner = CornerSnaps[(EdgeIndex + 1) % CornerSnaps.Num()];
		if (StartCorner.Pillar && StartCorner.Pillar == EndCorner.Pillar)
		{
			ResetWallCreationDragState();
			return false;
		}

		const FVector StartLocal = GetResolvedCornerLocalLocation(StartCorner);
		const FVector EndLocal = GetResolvedCornerLocalLocation(EndCorner);
		if (FVector::Dist2D(StartLocal, EndLocal) <= MinRectangleSide)
		{
			ResetWallCreationDragState();
			return false;
		}
	}

 // Every anchored path uses the same planned split transaction.

 {
  AEHB_Wall* PrimaryWall = nullptr;
  const bool bSucceeded = CommitWallCreationPath(Building, CornerSnaps, true, PrimaryWall);
  ResetWallCreationDragState();
  if (!bSucceeded) return false;
  if (GEditor)
  {
   GEditor->SelectNone(false, true, false);
   GEditor->SelectActor(PrimaryWall ? Cast<AActor>(PrimaryWall) : Cast<AActor>(Building), true, true, true);
   GEditor->RedrawLevelEditingViewports();
  }
  if (PrimaryWall) SelectWall(PrimaryWall);
  return true;
 }

}

bool FEasyHouseEditorMode::UpdateRailingCreationMouseLocation(FEditorViewportClient* ViewportClient)
{
	if (!bRailingCreationToolActive || !ActiveBuilding.IsValid())
	{
		return false;
	}

	if (bRailingCreationDragging && RailingWallDrag.IsSet())
	{
		FVector Mouse = RailingWallDrag.WorldStart;
		auto* EffectiveViewport = ViewportClient ? ViewportClient : GCurrentLevelEditingViewportClient;
		if (EffectiveViewport && EffectiveViewport->Viewport)
		{
			const auto Cursor = EffectiveViewport->GetCursorWorldLocationFromMousePos();
			const FVector Ray = Cursor.GetDirection();
			if (FMath::Abs(Ray.Z) > UE_SMALL_NUMBER)
			{
				const double T = (Mouse.Z - Cursor.GetOrigin().Z) / Ray.Z;
				if (T >= 0) Mouse = Cursor.GetOrigin() + Ray * T;
			}
		}
		RailingCreationMouseLocation = ResolveCreationFreeEnd(RailingWallDrag.WorldStart, FVector(Mouse.X,Mouse.Y,RailingWallDrag.WorldStart.Z), ViewportClient);
		const auto Check = RailingWallDrag.Execute(ActiveBuilding.Get(), RailingCreationMouseLocation,
			RailingCreationHeight, RailingCreationThickness, RailingCreationPostSpacing, true);
		bRailingWallReady = Check.bSucceeded;
		RailingWallStatus = Check.Message;
		return true;
	}
	HoveredRailingWall = {};
	RailingWallStatus.Reset();
	bRailingWallReady = false;
	FVector DropLocation = ActiveBuilding->GetActorLocation();
	AEHB_Pillar* HitPillar = nullptr;
	AEHB_Railing* HitRailing = nullptr;
	AEHB_Stair* HitStair = nullptr;
	EEHBRailingSide HitStairSide = EEHBRailingSide::Left;
	FGuid HitPostGuid;
	FHitResult HitResult;
	bool bResolvedSnap = false;
	bool bHasDropLocation = false;
	if (GetViewportDropHitResult(HitResult, nullptr) && HitResult.bBlockingHit)
	{
		// Existing structural endpoints take precedence over their top surfaces.
		AEHB_Pillar* ExistingPillar = ResolvePillarFromHit(HitResult);
		if (!ExistingPillar || ExistingPillar->OwningBuilding != ActiveBuilding.Get() || ExistingPillar->IsActorBeingDestroyed())
		{
			ExistingPillar = EHBRailingCreationSnap::FindWallEndpointPillar(
				ResolveWallFromHit(HitResult), ActiveBuilding.Get(), HitResult.ImpactPoint,
				FMath::Max(30.0f, RailingCreationThickness * 4.0f));
		}
		if (ExistingPillar)
		{
			HitPillar = ExistingPillar;
			DropLocation = ExistingPillar->GetActorLocation();
			bHasDropLocation = bResolvedSnap = true;
		}
		// Side hits create a wall branch. Top-face placement retains its old workflow.
		if (!bRailingCreationDragging && !bHasDropLocation && FMath::Abs(HitResult.ImpactNormal.Z) < 0.5f)
		{
			auto* Wall = ResolveWallFromHit(HitResult);
			if (Wall && Wall->OwningBuilding == ActiveBuilding.Get() && !Wall->IsActorBeingDestroyed())
			{
				HoveredRailingWall.Capture(Wall, HitResult.ImpactPoint);
				DropLocation = HoveredRailingWall.WorldStart;
				const FVector Normal = FVector::CrossProduct(FVector::UpVector, Wall->GetActorForwardVector());
				const auto Check = HoveredRailingWall.Execute(ActiveBuilding.Get(), DropLocation + Normal * 100,
					RailingCreationHeight, RailingCreationThickness, RailingCreationPostSpacing, true);
				bRailingWallReady = Check.bSucceeded;
				RailingWallStatus = Check.Message;
				bHasDropLocation = bResolvedSnap = true;
			}
		}
		FEHBPillarCreationTopSnapResult TopSnapResult;
		if (!bHasDropLocation && ResolvePillarCreationTopSnap(HitResult, ActiveBuilding.Get(), TopSnapResult))
		{
			DropLocation = TopSnapResult.WorldLocation;
			bHasDropLocation = true;
			const bool bTopSnapFromFloorSlab = Cast<AEHB_FloorSlab>(TopSnapResult.SourceActor.Get()) != nullptr;
			const float SnapDistance = FMath::Max(30.0f, RailingCreationThickness * 4.0f);
			int32 IgnoredFloorIndex = 1;
			if (ResolvePillarBaseSnapNearLocation(
				ActiveBuilding.Get(),
				DropLocation,
				RailingCreationThickness,
				RailingCreationThickness,
				SnapDistance,
				DropLocation,
				IgnoredFloorIndex))
			{
				bResolvedSnap = true;
			}
			else if (!bTopSnapFromFloorSlab)
			{
				bResolvedSnap = true;
			}
		}

		if (!bHasDropLocation)
		{
			bResolvedSnap = ResolveRailingCreationSnapFromHit(
				HitResult,
				ActiveBuilding.Get(),
				DropLocation,
				HitPillar,
				HitRailing,
				HitStair,
				HitStairSide,
				HitPostGuid);
			bHasDropLocation = bResolvedSnap;
		}

		if (!bHasDropLocation)
		{
			DropLocation = HitResult.ImpactPoint;
			bHasDropLocation = true;
		}
	}
	else if (!GetViewportDropLocation(ViewportClient, DropLocation))
	{
		return false;
	}

	if (!bResolvedSnap)
	{
		const float SnapDistance = FMath::Max(30.0f, RailingCreationThickness * 4.0f);
		bResolvedSnap = ResolveNearestRailingPostSnap(
			ActiveBuilding.Get(),
			DropLocation,
			SnapDistance,
			HitRailing,
			HitStair,
			HitStairSide,
			HitPostGuid,
			DropLocation);
	}

	if (!bResolvedSnap)
	{
		const float SnapDistance = FMath::Max(30.0f, RailingCreationThickness * 4.0f);
		bResolvedSnap = ResolveRailingEndpointSurfaceSnap(
			ActiveBuilding.Get(),
			DropLocation,
			RailingCreationThickness,
			RailingCreationThickness,
			SnapDistance,
			DropLocation);
	}

	// Existing endpoints and surface hosts retain priority over angular assistance.
	if (bRailingCreationDragging && !bResolvedSnap && !RailingCreationStartStair.IsValid())
	{
		DropLocation = ResolveCreationFreeEnd(RailingCreationStartLocation, DropLocation, ViewportClient);
	}
	RailingCreationMouseLocation = DropLocation;
	HoveredRailingCreationPillar = HitPillar;
	HoveredRailingCreationRailing = HitRailing;
	HoveredRailingCreationStair = HitStair;
	HoveredRailingCreationStairSide = HitStairSide;
	HoveredRailingCreationPostGuid = HitPostGuid;
	return true;
}

bool FEasyHouseEditorMode::ResolveRailingCreationSnapFromHit(
	const FHitResult& HitResult,
	AEHBBuildingActorBase* Building,
	FVector& OutWorldLocation,
	AEHB_Pillar*& OutPillar,
	AEHB_Railing*& OutRailing,
	AEHB_Stair*& OutStair,
	EEHBRailingSide& OutStairSide,
	FGuid& OutPostGuid) const
{
	OutPillar = nullptr;
	OutRailing = nullptr;
	OutStair = nullptr;
	OutStairSide = EEHBRailingSide::Left;
	OutPostGuid.Invalidate();
	if (!Building)
	{
		return false;
	}

	if (AEHB_Pillar* HitPillar = ResolvePillarFromHit(HitResult))
	{
		if (HitPillar->OwningBuilding == Building && !HitPillar->IsActorBeingDestroyed())
		{
			OutPillar = HitPillar;
			OutWorldLocation = HitPillar->GetActorLocation();
			return true;
		}
	}

	AEHB_Wall* HitWall = ResolveWallFromHit(HitResult);
	if (!HitWall || HitWall->OwningBuilding != Building || HitWall->IsActorBeingDestroyed())
	{
		HitWall = nullptr;
	}

	if (AEHB_Pillar* WallPillar = EHBRailingCreationSnap::FindWallEndpointPillar(
		HitWall, Building, HitResult.ImpactPoint, FMath::Max(30.0f, RailingCreationThickness * 4.0f)))
	{
		OutPillar = WallPillar;
		OutWorldLocation = WallPillar->GetActorLocation();
		return true;
	}

	AEHB_Railing* HitRailing = ResolveRailingFromHit(HitResult);
	if (!HitRailing || HitRailing->OwningBuilding != Building || HitRailing->IsActorBeingDestroyed())
	{
		HitRailing = nullptr;
	}

	const float SnapDistance = FMath::Max(30.0f, RailingCreationThickness * 4.0f);
	FGuid PostGuid;
	FEHBRailingPost SnappedPost;
	bool bHasPost = false;
	if (HitRailing
		&& HitRailing->OwningBuilding == Building
		&& !HitRailing->IsActorBeingDestroyed()
		&& HitResult.GetComponent() == HitRailing->PostMeshComponent.Get()
		&& HitRailing->GetPostGuidForInstanceIndex(HitResult.Item, PostGuid)
		&& HitRailing->FindPostByGuid(PostGuid, SnappedPost))
	{
		bHasPost = true;
	}
	else if (HitRailing && HitRailing->OwningBuilding == Building && !HitRailing->IsActorBeingDestroyed())
	{
		float BestDistanceSquared = FMath::Square(SnapDistance);
		for (const FEHBRailingPost& CandidatePost : HitRailing->GeneratedPosts)
		{
			if (CandidatePost.bSuppressInstance || !CandidatePost.PostGuid.IsValid())
			{
				continue;
			}

			const FVector CandidateWorldLocation = HitRailing->GetActorTransform().TransformPosition(CandidatePost.LocalBaseLocation);
			const float DistanceSquared = FVector::DistSquared2D(CandidateWorldLocation, HitResult.ImpactPoint);
			if (DistanceSquared <= BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				SnappedPost = CandidatePost;
				PostGuid = CandidatePost.PostGuid;
				bHasPost = true;
			}
		}
	}

	if (!bHasPost || !PostGuid.IsValid())
	{
		AEHB_Stair* HitStair = ResolveStairFromHit(HitResult);
		if (HitStair && (HitStair->OwningBuilding != Building || HitStair->IsActorBeingDestroyed()))
		{
			HitStair = nullptr;
		}

		if (HitStair)
		{
			UPrimitiveComponent* HitComponent = HitResult.GetComponent();
			EEHBRailingSide HitSide = EEHBRailingSide::Left;
			bool bHasSide = false;
			if (HitComponent == HitStair->LeftRailingPostMeshComponent.Get()
				|| HitComponent == HitStair->LeftRailingRailMeshComponent.Get())
			{
				HitSide = EEHBRailingSide::Left;
				bHasSide = true;
			}
			else if (HitComponent == HitStair->RightRailingPostMeshComponent.Get()
				|| HitComponent == HitStair->RightRailingRailMeshComponent.Get())
			{
				HitSide = EEHBRailingSide::Right;
				bHasSide = true;
			}

			FGuid StairPostGuid;
			FEHBRailingPost StairPost;
			bool bHasStairPost = false;
			if (bHasSide
				&& (HitComponent == HitStair->LeftRailingPostMeshComponent.Get()
					|| HitComponent == HitStair->RightRailingPostMeshComponent.Get())
				&& HitStair->GetEmbeddedRailingPostGuidForInstanceIndex(HitSide, HitResult.Item, StairPostGuid)
				&& HitStair->FindEmbeddedRailingPostByGuid(StairPostGuid, StairPost))
			{
				bHasStairPost = true;
			}
			else if (HitStair->GetEmbeddedRailingPostGuidForOverrideComponent(HitComponent, StairPostGuid, HitSide)
				&& HitStair->FindEmbeddedRailingPostByGuid(StairPostGuid, StairPost))
			{
				bHasStairPost = true;
			}
			else if (bHasSide)
			{
				float BestDistanceSquared = FMath::Square(SnapDistance);
				for (const FEHBRailingPost& CandidatePost : HitStair->GetEmbeddedRailingPosts(HitSide))
				{
					if (CandidatePost.bSuppressInstance || !CandidatePost.PostGuid.IsValid())
					{
						continue;
					}

					const FVector CandidateWorldLocation = HitStair->GetActorTransform().TransformPosition(CandidatePost.LocalBaseLocation);
					const float DistanceSquared = FVector::DistSquared2D(CandidateWorldLocation, HitResult.ImpactPoint);
					if (DistanceSquared <= BestDistanceSquared)
					{
						BestDistanceSquared = DistanceSquared;
						StairPost = CandidatePost;
						StairPostGuid = CandidatePost.PostGuid;
						bHasStairPost = true;
					}
				}
			}

			if (bHasStairPost && StairPostGuid.IsValid())
			{
				OutStair = HitStair;
				OutStairSide = HitSide;
				OutPostGuid = StairPostGuid;
				OutWorldLocation = HitStair->GetActorTransform().TransformPosition(StairPost.LocalBaseLocation);
				return true;
			}
		}
	}

	if (!bHasPost || !PostGuid.IsValid())
	{
		return ResolveNearestRailingPostSnap(
			Building,
			HitResult.ImpactPoint,
			SnapDistance,
			OutRailing,
			OutStair,
			OutStairSide,
			OutPostGuid,
			OutWorldLocation);
	}

	OutRailing = HitRailing;
	OutPostGuid = PostGuid;
	OutWorldLocation = HitRailing->GetActorTransform().TransformPosition(SnappedPost.LocalBaseLocation);
	return true;
}

bool FEasyHouseEditorMode::ResolveNearestRailingPostSnap(
	AEHBBuildingActorBase* Building,
	const FVector& ReferenceWorldLocation,
	float SnapDistance,
	AEHB_Railing*& OutRailing,
	AEHB_Stair*& OutStair,
	EEHBRailingSide& OutStairSide,
	FGuid& OutPostGuid,
	FVector& OutWorldLocation) const
{
	OutRailing = nullptr;
	OutStair = nullptr;
	OutStairSide = EEHBRailingSide::Left;
	OutPostGuid.Invalidate();
	if (!Building)
	{
		return false;
	}

	float BestDistanceSquared = FMath::Square(FMath::Max(1.0f, SnapDistance));
	FEHBRailingPost BestPost;
	AEHB_Railing* BestRailing = nullptr;
	AEHB_Stair* BestStair = nullptr;
	EEHBRailingSide BestStairSide = EEHBRailingSide::Left;

	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors);
	for (AActor* Actor : AttachedActors)
	{
		if (AEHB_Railing* CandidateRailing = Cast<AEHB_Railing>(Actor))
		{
			if (CandidateRailing->OwningBuilding != Building || CandidateRailing->IsActorBeingDestroyed())
			{
				continue;
			}

			const FTransform RailingTransform = CandidateRailing->GetActorTransform();
			for (const FEHBRailingPost& CandidatePost : CandidateRailing->GeneratedPosts)
			{
				if (CandidatePost.bSuppressInstance || !CandidatePost.PostGuid.IsValid())
				{
					continue;
				}

				const FVector CandidateWorldLocation = RailingTransform.TransformPosition(CandidatePost.LocalBaseLocation);
				const float DistanceSquared = FVector::DistSquared2D(CandidateWorldLocation, ReferenceWorldLocation);
				if (DistanceSquared <= BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					BestPost = CandidatePost;
					BestRailing = CandidateRailing;
					BestStair = nullptr;
				}
			}
		}

		if (AEHB_Stair* CandidateStair = Cast<AEHB_Stair>(Actor))
		{
			if (CandidateStair->OwningBuilding != Building || CandidateStair->IsActorBeingDestroyed())
			{
				continue;
			}

			const FTransform StairTransform = CandidateStair->GetActorTransform();
			for (EEHBRailingSide Side : { EEHBRailingSide::Left, EEHBRailingSide::Right })
			{
				if (!CandidateStair->IsEmbeddedRailingSideGenerated(Side))
				{
					continue;
				}

				for (const FEHBRailingPost& CandidatePost : CandidateStair->GetEmbeddedRailingPosts(Side))
				{
					if (CandidatePost.bSuppressInstance || !CandidatePost.PostGuid.IsValid())
					{
						continue;
					}

					const FVector CandidateWorldLocation = StairTransform.TransformPosition(CandidatePost.LocalBaseLocation);
					const float DistanceSquared = FVector::DistSquared2D(CandidateWorldLocation, ReferenceWorldLocation);
					if (DistanceSquared <= BestDistanceSquared)
					{
						BestDistanceSquared = DistanceSquared;
						BestPost = CandidatePost;
						BestRailing = nullptr;
						BestStair = CandidateStair;
						BestStairSide = Side;
					}
				}
			}
		}
	}

	if (!BestPost.PostGuid.IsValid() || (!BestRailing && !BestStair))
	{
		return false;
	}

	OutRailing = BestRailing;
	OutStair = BestStair;
	OutStairSide = BestStairSide;
	OutPostGuid = BestPost.PostGuid;
	OutWorldLocation = BestRailing
		? BestRailing->GetActorTransform().TransformPosition(BestPost.LocalBaseLocation)
		: BestStair->GetActorTransform().TransformPosition(BestPost.LocalBaseLocation);
	return true;
}

bool FEasyHouseEditorMode::FinishRailingCreationDrag()
{
	const auto ResetRailingCreationDragState = [this]()
	{
		bRailingCreationDragging = false;
		RailingCreationStartPillar.Reset();
		RailingWallDrag = {};
		HoveredRailingWall = {};
		RailingCreationStartRailing.Reset();
		RailingCreationStartStair.Reset();
		RailingCreationStartPostGuid.Invalidate();
		RailingCreationStartStairSide = EEHBRailingSide::Left;
		RailingCreationStartLocation = FVector::ZeroVector;
	};

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		CancelRailingCreation();
		return false;
	}

	if (RailingWallDrag.IsSet())
	{
		const auto Result = RailingWallDrag.Execute(Building, RailingCreationMouseLocation,
			RailingCreationHeight, RailingCreationThickness, RailingCreationPostSpacing, false);
		RailingWallStatus = Result.Message;
		bRailingWallReady = Result.bSucceeded;
		ResetRailingCreationDragState();
		if (!Result.bSucceeded)
		{
			UE_LOG(LogTemp, Display, TEXT("EHB wall railing rejected: %s"), *Result.Message);
			FNotificationInfo Notice(EHBRailingFeedback::Describe(Result.Message));
			Notice.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Notice);
		}
		if (GEditor) GEditor->RedrawLevelEditingViewports();
		return Result.bSucceeded;
	}
	AEHB_Pillar* ExistingStartPillar = RailingCreationStartPillar.Get();
	if (ExistingStartPillar && ExistingStartPillar->OwningBuilding != Building)
	{
		ExistingStartPillar = nullptr;
		RailingCreationStartPillar.Reset();
		RailingWallDrag = {};
		HoveredRailingWall = {};
	}

	AEHB_Pillar* ExistingEndPillar = HoveredRailingCreationPillar.Get();
	if (ExistingEndPillar && (ExistingEndPillar->OwningBuilding != Building || ExistingEndPillar == ExistingStartPillar))
	{
		ResetRailingCreationDragState();
		return false;
	}

	AEHB_Railing* ExistingStartRailing = RailingCreationStartRailing.Get();
	if (ExistingStartRailing && (ExistingStartRailing->OwningBuilding != Building || !RailingCreationStartPostGuid.IsValid()))
	{
		ExistingStartRailing = nullptr;
		RailingCreationStartRailing.Reset();
		RailingCreationStartPostGuid.Invalidate();
	}

	AEHB_Stair* ExistingStartStair = RailingCreationStartStair.Get();
	if (ExistingStartStair && (ExistingStartStair->OwningBuilding != Building || !RailingCreationStartPostGuid.IsValid()))
	{
		ExistingStartStair = nullptr;
		RailingCreationStartStair.Reset();
		RailingCreationStartPostGuid.Invalidate();
	}

	AEHB_Railing* ExistingEndRailing = HoveredRailingCreationRailing.Get();
	if (ExistingEndRailing && (ExistingEndRailing->OwningBuilding != Building || !HoveredRailingCreationPostGuid.IsValid()))
	{
		ExistingEndRailing = nullptr;
		HoveredRailingCreationRailing.Reset();
		HoveredRailingCreationPostGuid.Invalidate();
	}

	AEHB_Stair* ExistingEndStair = HoveredRailingCreationStair.Get();
	if (ExistingEndStair && (ExistingEndStair->OwningBuilding != Building || !HoveredRailingCreationPostGuid.IsValid()))
	{
		ExistingEndStair = nullptr;
		HoveredRailingCreationStair.Reset();
		HoveredRailingCreationPostGuid.Invalidate();
	}
	if (ExistingStartRailing && ExistingEndRailing
		&& ExistingStartRailing == ExistingEndRailing
		&& RailingCreationStartPostGuid == HoveredRailingCreationPostGuid)
	{
		ResetRailingCreationDragState();
		return false;
	}
	if (ExistingStartStair && ExistingEndStair
		&& ExistingStartStair == ExistingEndStair
		&& RailingCreationStartPostGuid == HoveredRailingCreationPostGuid)
	{
		ResetRailingCreationDragState();
		return false;
	}

	if (FVector::Dist2D(RailingCreationStartLocation, RailingCreationMouseLocation) <= 10.0f)
	{
		ResetRailingCreationDragState();
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateSimpleRailingByDragTransaction", "\u62d6\u62fd\u521b\u5efa\u7b80\u5355\u6276\u624b"));
	Building->Modify();
	AEHB_Railing* Railing = CreateSimpleRailing(Building, RailingCreationStartLocation, RailingCreationMouseLocation);
	Building->MarkPackageDirty();
	ResetRailingCreationDragState();

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Railing ? Cast<AActor>(Railing) : Cast<AActor>(Building), true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return Railing != nullptr;
}

AEHB_Pillar* FEasyHouseEditorMode::CreateSimplePillar(AEHBBuildingActorBase* Building, const FVector& LocalLocation, const FRotator& LocalRotation, const FString& NamePrefix, int32 FloorIndex) const
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Pillar> PillarClass = AEHB_Pillar::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->PillarActorClass.IsNull())
	{
		if (UClass* LoadedPillarClass = ToolsetSettings->PillarActorClass.LoadSynchronous())
		{
			PillarClass = LoadedPillarClass;
		}
	}

	const FName ActorName = MakeUniqueObjectName(
		Building->GetLevel(),
		PillarClass,
		FName(*NamePrefix));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = Building;
	SpawnParams.OverrideLevel = Building->GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	const FVector SnappedLocalLocation = RoundBuildingLocalCoordinates(LocalLocation);
	const FTransform LocalTransform(LocalRotation, SnappedLocalLocation);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_Pillar* Pillar = Building->GetWorld()->SpawnActor<AEHB_Pillar>(PillarClass, WorldTransform, SpawnParams);
	if (!Pillar)
	{
		return nullptr;
	}

	Pillar->SetFlags(RF_Transactional);
	Pillar->Modify();
	Pillar->ElementName = ActorName;
	Pillar->AttachToBuilding(Building, LocalTransform);
	Pillar->ConfigureAsPolygonPillar(
		WallCreationHeight,
		WallCreationThickness,
		WallCreationThickness,
		LocalTransform,
		true);
	Pillar->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::FloorBody);

#if WITH_EDITOR
	Pillar->SetActorLabel(ActorName.ToString());
#endif

	return Pillar;
}

AEHB_Wall* FEasyHouseEditorMode::CreateSimpleWall(AEHBBuildingActorBase* Building, AEHB_Pillar* StartPillar, AEHB_Pillar* EndPillar, const FVector& /*LocalStart*/, const FVector& /*LocalEnd*/) const
{
	return Building
		? Building->ConnectPillars(StartPillar, EndPillar, WallCreationHeight, WallCreationThickness)
		: nullptr;
}

AEHB_Railing* FEasyHouseEditorMode::CreateSimpleRailing(AEHBBuildingActorBase* Building, const FVector& WorldStart, const FVector& WorldEnd) const
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	const FVector WorldDirection2D = (WorldEnd - WorldStart).GetSafeNormal2D();
	if (WorldDirection2D.IsNearlyZero())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Railing> RailingClass = AEHB_Railing::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->RailingActorClass.IsNull())
	{
		if (UClass* LoadedRailingClass = ToolsetSettings->RailingActorClass.LoadSynchronous())
		{
			if (LoadedRailingClass->IsChildOf(AEHB_Railing::StaticClass()))
			{
				RailingClass = LoadedRailingClass;
			}
		}
	}

	const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), RailingClass, TEXT("EHB_Railing"));
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = Building;
	SpawnParams.OverrideLevel = Building->GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	const FVector WorldCenter = FMath::Lerp(WorldStart, WorldEnd, 0.5f);
	const FRotator WorldRotation(0.0f, WorldDirection2D.Rotation().Yaw, 0.0f);
	const FTransform WorldTransform(WorldRotation, WorldCenter);
	const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());

	AEHB_Railing* Railing = Building->GetWorld()->SpawnActor<AEHB_Railing>(RailingClass, WorldTransform, SpawnParams);
	if (!Railing)
	{
		return nullptr;
	}

	Railing->SetFlags(RF_Transactional);
	Railing->Modify();
	Railing->ElementName = ActorName;
	Railing->PostHeight = RailingCreationHeight;
	Railing->RailHeight = RailingCreationHeight;
	Railing->PostSpacing = RailingCreationPostSpacing;
	Railing->RailThickness = RailingCreationThickness;
	Railing->PostWidth = RailingCreationThickness;
	Railing->MaxRailSegmentLength = FMath::Max(RailingCreationPostSpacing, RailingCreationThickness);
	Railing->FillMode = EEHBRailingFillMode::PostsAndRails;
	Railing->bOmitStartPost = RailingCreationStartPillar.IsValid()
		|| (RailingCreationStartRailing.IsValid() && RailingCreationStartPostGuid.IsValid())
		|| (RailingCreationStartStair.IsValid() && RailingCreationStartPostGuid.IsValid());
	Railing->bOmitEndPost = HoveredRailingCreationPillar.IsValid()
		|| (HoveredRailingCreationRailing.IsValid() && HoveredRailingCreationPostGuid.IsValid())
		|| (HoveredRailingCreationStair.IsValid() && HoveredRailingCreationPostGuid.IsValid());

	const FVector LocalStart = WorldTransform.InverseTransformPosition(WorldStart);
	const FVector LocalEnd = WorldTransform.InverseTransformPosition(WorldEnd);
	if (!Railing->ConfigureLinear(Building, LocalTransform, LocalStart, LocalEnd, 1))
	{
		Railing->Destroy();
		return nullptr;
	}

	if (AEHB_Pillar* StartPillar = RailingCreationStartPillar.Get())
	{
		if (StartPillar->OwningBuilding == Building && !StartPillar->IsActorBeingDestroyed())
		{
			StartPillar->EnsureElementGuid();
			Railing->StartAnchor.ElementGuid = StartPillar->ElementGuid;
			Railing->StartAnchor.LocalPoint = LocalStart;
		}
	}
	else if (AEHB_Railing* StartRailing = RailingCreationStartRailing.Get())
	{
		if (StartRailing->OwningBuilding == Building && !StartRailing->IsActorBeingDestroyed() && RailingCreationStartPostGuid.IsValid())
		{
			StartRailing->EnsureElementGuid();
			Railing->StartAnchor.ElementGuid = StartRailing->ElementGuid;
			Railing->StartAnchor.PostGuid = RailingCreationStartPostGuid;
			Railing->StartAnchor.LocalPoint = LocalStart;
		}
	}
	else if (AEHB_Stair* StartStair = RailingCreationStartStair.Get())
	{
		if (StartStair->OwningBuilding == Building && !StartStair->IsActorBeingDestroyed() && RailingCreationStartPostGuid.IsValid())
		{
			StartStair->EnsureElementGuid();
			Railing->StartAnchor.ElementGuid = StartStair->ElementGuid;
			Railing->StartAnchor.PostGuid = RailingCreationStartPostGuid;
			Railing->StartAnchor.LocalPoint = LocalStart;
		}
	}
	if (AEHB_Pillar* EndPillar = HoveredRailingCreationPillar.Get())
	{
		if (EndPillar->OwningBuilding == Building && !EndPillar->IsActorBeingDestroyed())
		{
			EndPillar->EnsureElementGuid();
			Railing->EndAnchor.ElementGuid = EndPillar->ElementGuid;
			Railing->EndAnchor.LocalPoint = LocalEnd;
		}
	}
	else if (AEHB_Railing* EndRailing = HoveredRailingCreationRailing.Get())
	{
		if (EndRailing->OwningBuilding == Building && !EndRailing->IsActorBeingDestroyed() && HoveredRailingCreationPostGuid.IsValid())
		{
			EndRailing->EnsureElementGuid();
			Railing->EndAnchor.ElementGuid = EndRailing->ElementGuid;
			Railing->EndAnchor.PostGuid = HoveredRailingCreationPostGuid;
			Railing->EndAnchor.LocalPoint = LocalEnd;
		}
	}
	else if (AEHB_Stair* EndStair = HoveredRailingCreationStair.Get())
	{
		if (EndStair->OwningBuilding == Building && !EndStair->IsActorBeingDestroyed() && HoveredRailingCreationPostGuid.IsValid())
		{
			EndStair->EnsureElementGuid();
			Railing->EndAnchor.ElementGuid = EndStair->ElementGuid;
			Railing->EndAnchor.PostGuid = HoveredRailingCreationPostGuid;
			Railing->EndAnchor.LocalPoint = LocalEnd;
		}
	}
	Railing->RebuildRailing();

#if WITH_EDITOR
	Railing->SetActorLabel(ActorName.ToString());
#endif

	return Railing;
}

bool FEasyHouseEditorMode::GetSelectedPillarPair(AEHB_Pillar*& OutFirstPillar, AEHB_Pillar*& OutSecondPillar) const
{
	OutFirstPillar = nullptr;
	OutSecondPillar = nullptr;

	if (!GEditor || !GEditor->GetSelectedActors())
	{
		return false;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	TArray<AEHB_Pillar*> SelectedPillars;
	SelectedPillars.Reserve(2);
	for (AActor* Actor : SelectedActors)
	{
		if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
		{
			SelectedPillars.Add(Pillar);
			if (SelectedPillars.Num() > 2)
			{
				return false;
			}
		}
	}

	if (SelectedPillars.Num() != 2 || SelectedPillars[0] == SelectedPillars[1])
	{
		return false;
	}

	OutFirstPillar = SelectedPillars[0];
	OutSecondPillar = SelectedPillars[1];
	return true;
}

void FEasyHouseEditorMode::DrawPillarConnectionToolbar(FEditorViewportClient* ViewportClient, FViewport* Viewport, FCanvas* Canvas) const
{
	if (!Viewport || !Canvas || !CanConnectSelectedPillars())
	{
		return;
	}

	const float ButtonWidth = 128.0f;
	const float ButtonHeight = 34.0f;
	const float X = (Viewport->GetSizeXY().X - ButtonWidth) * 0.5f;
	const float Y = Viewport->GetSizeXY().Y - 84.0f;
	const FVector2D PanelPadding(8.0f, 8.0f);

	DrawBuildingHudPanel(
		Canvas,
		FVector2D(X, Y) - PanelPadding,
		FVector2D(ButtonWidth, ButtonHeight) + PanelPadding * 2.0f);

	DrawBuildingHudButton(
		Canvas,
		new HEHBPillarConnectionToolbarProxy(),
		FVector2D(X, Y),
		FVector2D(ButtonWidth, ButtonHeight),
		LOCTEXT("ConnectSelectedPillarsHudButton", "连接墙面"),
		bHoveredPillarConnectionToolbarButton);
}

bool FEasyHouseEditorMode::MoveSelectedWallByDelta(const FVector& WorldDelta, bool bFinished)
{
	AEHB_Wall* Wall = SelectedWall.Get();
	if (!Wall || WorldDelta.IsNearlyZero())
	{
		return false;
	}

	AEHBBuildingActorBase* Building = Wall->OwningBuilding;
	const FTransform BuildingTransform = Building ? Building->GetActorTransform() : FTransform::Identity;
	const FVector LocalDelta = BuildingTransform.InverseTransformVectorNoScale(WorldDelta);
	if (LocalDelta.IsNearlyZero())
	{
		return false;
	}

	auto FindPillarByGuid = [Building](const FGuid& PillarGuid) -> AEHB_Pillar*
	{
		return Building && PillarGuid.IsValid()
			? Cast<AEHB_Pillar>(Building->FindElementActorByGuid(PillarGuid))
			: nullptr;
	};

	AEHB_Pillar* StartPillar = FindPillarByGuid(Wall->StartPillarGuid);
	AEHB_Pillar* EndPillar = FindPillarByGuid(Wall->EndPillarGuid);

	if (Building)
	{
		Building->Modify();
	}
	Wall->Modify();

	auto MovePillar = [LocalDelta, bFinished](AEHB_Pillar* Pillar)
	{
		if (!Pillar)
		{
			return;
		}

		Pillar->Modify();
		FTransform PillarLocalTransform = Pillar->GetElementLocalTransform();
		PillarLocalTransform.AddToTranslation(LocalDelta);
		PillarLocalTransform.SetLocation(RoundBuildingLocalCoordinates(PillarLocalTransform.GetLocation()));
		Pillar->SetActorRelativeTransform(PillarLocalTransform);
		Pillar->NotifyElementGeometryChanged(bFinished);
		if (bFinished)
		{
			Pillar->MarkPackageDirty();
		}
	};

	MovePillar(StartPillar);
	if (EndPillar && EndPillar != StartPillar)
	{
		MovePillar(EndPillar);
	}

	if (!StartPillar)
	{
		Wall->LocalStart = RoundBuildingLocalCoordinates(Wall->LocalStart + LocalDelta);
	}
	if (!EndPillar)
	{
		Wall->LocalEnd = RoundBuildingLocalCoordinates(Wall->LocalEnd + LocalDelta);
	}

	RefreshSelectedWallMoveNeighborhood(Wall, bFinished);
	return true;
}

void FEasyHouseEditorMode::RefreshSelectedWallMoveNeighborhood(AEHB_Wall* Wall, bool bFinished) const
{
	if (!Wall)
	{
		return;
	}

	AEHBBuildingActorBase* Building = Wall->OwningBuilding;
	TArray<AEHB_Wall*> AffectedWalls;
	TSet<AEHB_Wall*> UniqueWalls;
	TArray<AEHB_Pillar*> AffectedPillars;
	TSet<AEHB_Pillar*> UniquePillars;

	auto AddWall = [&AffectedWalls, &UniqueWalls](AEHB_Wall* CandidateWall)
	{
		if (CandidateWall && !CandidateWall->IsActorBeingDestroyed() && !UniqueWalls.Contains(CandidateWall))
		{
			UniqueWalls.Add(CandidateWall);
			AffectedWalls.Add(CandidateWall);
		}
	};

	auto AddPillar = [&AffectedPillars, &UniquePillars](AEHB_Pillar* CandidatePillar)
	{
		if (CandidatePillar && !CandidatePillar->IsActorBeingDestroyed() && !UniquePillars.Contains(CandidatePillar))
		{
			UniquePillars.Add(CandidatePillar);
			AffectedPillars.Add(CandidatePillar);
		}
	};

	auto FindElement = [Building](const FGuid& ElementGuid) -> AEHBElementActorBase*
	{
		return Building && ElementGuid.IsValid()
			? Building->FindElementActorByGuid(ElementGuid)
			: nullptr;
	};

	auto AddWallByGuid = [&AddWall, &FindElement](const FGuid& WallGuid)
	{
		AddWall(Cast<AEHB_Wall>(FindElement(WallGuid)));
	};

	auto AddPillarByGuid = [&AddPillar, &FindElement](const FGuid& PillarGuid)
	{
		AddPillar(Cast<AEHB_Pillar>(FindElement(PillarGuid)));
	};

	AddWall(Wall);
	AddPillarByGuid(Wall->StartPillarGuid);
	AddPillarByGuid(Wall->EndPillarGuid);

	const TArray<AEHB_Pillar*> SeedPillars = AffectedPillars;
	for (AEHB_Pillar* Pillar : SeedPillars)
	{
		if (!Pillar)
		{
			continue;
		}

		for (const FGuid& ConnectedWallGuid : Pillar->ConnectedWallGuids)
		{
			AddWallByGuid(ConnectedWallGuid);
		}
	}

	const TArray<AEHB_Wall*> SeedWalls = AffectedWalls;
	for (AEHB_Wall* AffectedWall : SeedWalls)
	{
		if (!AffectedWall)
		{
			continue;
		}

		AddPillarByGuid(AffectedWall->StartPillarGuid);
		AddPillarByGuid(AffectedWall->EndPillarGuid);
	}

	if (Building)
	{
		Building->Modify();
	}
	for (AEHB_Pillar* Pillar : AffectedPillars)
	{
		if (Pillar)
		{
			Pillar->Modify();
		}
	}
	for (AEHB_Wall* AffectedWall : AffectedWalls)
	{
		if (AffectedWall)
		{
			AffectedWall->Modify();
			AffectedWall->RefreshFromConnectedPillars(bFinished);
			if (bFinished)
			{
				AffectedWall->MarkPackageDirty();
			}
		}
	}

	if (bFinished && Building)
	{
		Building->RebuildClosedLoops();
		Building->MarkPackageDirty();
	}
}

void FEasyHouseEditorMode::DrawWallMeasurementHud(FEditorViewportClient* /*ViewportClient*/, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) const
{
	if (!Viewport || !View || !Canvas)
	{
		return;
	}

	if (bWallCreationToolActive && bWallCreationDragging)
	{
		const AEHBBuildingActorBase* Building = ActiveBuilding.Get();
		const FTransform BuildingTransform = Building ? Building->GetActorTransform() : FTransform::Identity;
		FVector LocalStart = BuildingTransform.InverseTransformPosition(WallCreationStartLocation);
		FVector LocalEnd = BuildingTransform.InverseTransformPosition(WallCreationMouseLocation);
		LocalEnd.Z = LocalStart.Z;

		if (Building && IsWallCreationRectangleModeActive(Viewport))
		{
			const TArray<FVector> LocalCorners = {
				FVector(LocalStart.X, LocalStart.Y, LocalStart.Z),
				FVector(LocalEnd.X, LocalStart.Y, LocalStart.Z),
				FVector(LocalEnd.X, LocalEnd.Y, LocalStart.Z),
				FVector(LocalStart.X, LocalEnd.Y, LocalStart.Z)
			};

			for (int32 EdgeIndex = 0; EdgeIndex < LocalCorners.Num(); ++EdgeIndex)
			{
				const FVector& EdgeStart = LocalCorners[EdgeIndex];
				const FVector& EdgeEnd = LocalCorners[(EdgeIndex + 1) % LocalCorners.Num()];
				DrawWallMeasurementLabel(
					Viewport,
					View,
					Canvas,
					EdgeStart,
					EdgeEnd,
					BuildingTransform.TransformPosition(EdgeStart),
					BuildingTransform.TransformPosition(EdgeEnd),
					false);
			}
			return;
		}

		DrawWallMeasurementLabel(
			Viewport,
			View,
			Canvas,
			LocalStart,
			LocalEnd,
			WallCreationStartLocation,
			WallCreationMouseLocation,
			false);
		return;
	}

	if (!bWallPillarMeasurementTracking)
	{
		return;
	}

	TArray<const AEHB_Wall*> MeasurementWalls;
	TSet<const AEHB_Wall*> UniqueWalls;
	auto AddMeasurementWall = [&MeasurementWalls, &UniqueWalls](const AEHB_Wall* Wall)
	{
		if (Wall && !Wall->IsActorBeingDestroyed() && !UniqueWalls.Contains(Wall))
		{
			UniqueWalls.Add(Wall);
			MeasurementWalls.Add(Wall);
		}
	};

	auto AddWallsConnectedToPillar = [&AddMeasurementWall](const AEHB_Pillar* Pillar)
	{
		if (!Pillar || !Pillar->OwningBuilding)
		{
			return;
		}

		for (const FGuid& WallGuid : Pillar->ConnectedWallGuids)
		{
			AddMeasurementWall(Cast<AEHB_Wall>(Pillar->OwningBuilding->FindElementActorByGuid(WallGuid)));
		}
	};

	auto AddWallAndEndpointConnections = [&AddMeasurementWall, &AddWallsConnectedToPillar](const AEHB_Wall* Wall)
	{
		if (!Wall)
		{
			return;
		}

		AddMeasurementWall(Wall);
		const AEHBBuildingActorBase* Building = Wall->OwningBuilding;
		if (!Building)
		{
			return;
		}

		AddWallsConnectedToPillar(Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->StartPillarGuid)));
		AddWallsConnectedToPillar(Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->EndPillarGuid)));
	};

	if (const AEHB_Wall* Wall = SelectedWall.Get())
	{
		AddWallAndEndpointConnections(Wall);
	}

	if (GEditor && GEditor->GetSelectedActors())
	{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
		for (AActor* Actor : SelectedActors)
		{
			if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
			{
				AddWallAndEndpointConnections(Wall);
				continue;
			}

			const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor);
			if (!Pillar || !Pillar->OwningBuilding)
			{
				continue;
			}

			AddWallsConnectedToPillar(Pillar);
		}
	}

	TMap<FGuid, int32> AngleLabelCountsByPillar;
	auto ConsumeAngleLabelIndex = [&AngleLabelCountsByPillar](const FGuid& PillarGuid) -> int32
	{
		if (!PillarGuid.IsValid())
		{
			return 0;
		}

		int32& Count = AngleLabelCountsByPillar.FindOrAdd(PillarGuid);
		const int32 Result = Count;
		++Count;
		return Result;
	};

	for (const AEHB_Wall* Wall : MeasurementWalls)
	{
		if (!Wall)
		{
			continue;
		}

		const FTransform BuildingTransform = Wall->OwningBuilding
			? Wall->OwningBuilding->GetActorTransform()
			: FTransform::Identity;
		DrawWallMeasurementLabel(
			Viewport,
			View,
			Canvas,
			Wall->LocalStart,
			Wall->LocalEnd,
			BuildingTransform.TransformPosition(Wall->LocalStart),
			BuildingTransform.TransformPosition(Wall->LocalEnd),
			true,
			ConsumeAngleLabelIndex(Wall->StartPillarGuid),
			ConsumeAngleLabelIndex(Wall->EndPillarGuid));
	}
}

void FEasyHouseEditorMode::DrawWallMeasurementLabel(
	FViewport* Viewport,
	const FSceneView* View,
	FCanvas* Canvas,
	const FVector& LocalStart,
	const FVector& LocalEnd,
	const FVector& WorldStart,
	const FVector& WorldEnd,
	bool bDrawAngleAtPillars,
	int32 StartAngleLabelIndex,
	int32 EndAngleLabelIndex) const
{
	if (!Viewport || !View || !Canvas)
	{
		return;
	}

	const FVector LocalDelta = LocalEnd - LocalStart;
	const float Length = LocalDelta.Size2D();
	const FVector LocalDirection = LocalDelta.GetSafeNormal2D();
	if (Length <= 1.0f || LocalDirection.IsNearlyZero())
	{
		return;
	}

	DrawBuildingHudWorldLabel(
		Viewport,
		View,
		Canvas,
		(WorldStart + WorldEnd) * 0.5f + FVector(0.0f, 0.0f, 72.0f),
		FString::Printf(TEXT("L %.1fcm"), Length),
		FLinearColor(0.0f, 0.95f, 0.35f, 0.98f));

	if (!bDrawAngleAtPillars)
	{
		return;
	}

	const float Angle = NormalizeMeasurementAngle(FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y, LocalDirection.X)));
	FVector WorldDirection = (WorldEnd - WorldStart).GetSafeNormal2D();
	if (WorldDirection.IsNearlyZero())
	{
		WorldDirection = FVector::ForwardVector;
	}

	const FString AngleLabel = FString::Printf(TEXT("A %.1f\u00B0"), Angle);
	auto MakeAngleWorldLocation = [](const FVector& Endpoint, const FVector& AlongDirection, int32 LabelIndex) -> FVector
	{
		const int32 SafeIndex = FMath::Max(0, LabelIndex);
		FVector SideDirection(-AlongDirection.Y, AlongDirection.X, 0.0f);
		if (SideDirection.IsNearlyZero())
		{
			SideDirection = FVector::RightVector;
		}

		const float SideSign = (SafeIndex % 2) == 0 ? 1.0f : -1.0f;
		const float AlongDistance = 34.0f + static_cast<float>(SafeIndex) * 14.0f;
		const float SideDistance = 18.0f + static_cast<float>(SafeIndex / 2) * 8.0f;
		const float Height = 90.0f + static_cast<float>(SafeIndex / 2) * 8.0f;
		return Endpoint + AlongDirection * AlongDistance + SideDirection * SideDistance * SideSign + FVector(0.0f, 0.0f, Height);
	};

	auto MakeAnglePixelOffset = [](int32 LabelIndex) -> FVector2D
	{
		const int32 SafeIndex = FMath::Max(0, LabelIndex);
		const int32 Lane = SafeIndex % 4;
		const int32 Row = SafeIndex / 4;
		static const FVector2D LaneOffsets[4] = {
			FVector2D(-108.0f, -34.0f),
			FVector2D(8.0f, -34.0f),
			FVector2D(-108.0f, -64.0f),
			FVector2D(8.0f, -64.0f)
		};
		return LaneOffsets[Lane] + FVector2D(0.0f, static_cast<float>(Row) * -32.0f);
	};

	DrawBuildingHudWorldLabel(
		Viewport,
		View,
		Canvas,
		MakeAngleWorldLocation(WorldStart, WorldDirection, StartAngleLabelIndex),
		AngleLabel,
		FLinearColor(1.0f, 0.72f, 0.0f, 0.98f),
		MakeAnglePixelOffset(StartAngleLabelIndex));
	DrawBuildingHudWorldLabel(
		Viewport,
		View,
		Canvas,
		MakeAngleWorldLocation(WorldEnd, -WorldDirection, EndAngleLabelIndex),
		AngleLabel,
		FLinearColor(1.0f, 0.72f, 0.0f, 0.98f),
		MakeAnglePixelOffset(EndAngleLabelIndex));
}

bool FEasyHouseEditorMode::HasSelectedWallOrPillarForMeasurement() const
{
	if (SelectedWall.IsValid())
	{
		return true;
	}

	if (!GEditor || !GEditor->GetSelectedActors())
	{
		return false;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
	for (AActor* Actor : SelectedActors)
	{
		if (Cast<AEHB_Wall>(Actor) || Cast<AEHB_Pillar>(Actor))
		{
			return true;
		}
	}

	return false;
}

AEHB_DoorWindow* FEasyHouseEditorMode::CreateDoorWindowPreviewActor(AEHBBuildingActorBase* Building, const FEHBDoorWindowMeshData& RowData) const
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<AEHB_DoorWindow> DoorWindowClass = AEHB_DoorWindow::StaticClass();
	if (!RowData.DoorWindowClass.IsNull())
	{
		if (UClass* LoadedDoorWindowClass = RowData.DoorWindowClass.LoadSynchronous())
		{
			if (LoadedDoorWindowClass->IsChildOf(AEHB_DoorWindow::StaticClass()))
			{
				DoorWindowClass = LoadedDoorWindowClass;
			}
		}
	}
	else
	{
		const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
		if (ToolsetSettings && !ToolsetSettings->DoorWindowActorClass.IsNull())
		{
			if (UClass* LoadedDoorWindowClass = ToolsetSettings->DoorWindowActorClass.LoadSynchronous())
			{
				if (LoadedDoorWindowClass->IsChildOf(AEHB_DoorWindow::StaticClass()))
				{
					DoorWindowClass = LoadedDoorWindowClass;
				}
			}
		}
	}

	AEHB_DoorWindow* DoorWindow = SpawnDoorWindowPreviewActor(Building, DoorWindowClass);
	if (!DoorWindow)
	{
		return nullptr;
	}

	ApplyDoorWindowRowToActor(DoorWindow, RowData);

	return DoorWindow;
}

AEHB_DoorWindow* FEasyHouseEditorMode::CreateDefaultDoorWindowPreviewActor(AEHBBuildingActorBase* Building, EEHBDoorWindowElementKind Kind) const
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<AEHB_DoorWindow> DoorWindowClass = ResolveDefaultDoorWindowActorClass(Kind);
	AEHB_DoorWindow* DoorWindow = SpawnDoorWindowPreviewActor(Building, DoorWindowClass);
	if (!DoorWindow)
	{
		return nullptr;
	}

	ApplyDefaultDoorWindowToActor(DoorWindow, Kind, DoorWindowClass.Get());
	return DoorWindow;
}

AEHB_DoorWindow* FEasyHouseEditorMode::SpawnDoorWindowPreviewActor(AEHBBuildingActorBase* Building, TSubclassOf<AEHB_DoorWindow> DoorWindowClass) const
{
	if (!Building || !Building->GetWorld())
	{
		return nullptr;
	}

	if (!DoorWindowClass)
	{
		DoorWindowClass = AEHB_DoorWindow::StaticClass();
	}

	const FName ActorName = MakeUniqueObjectName(
		Building->GetLevel(),
		DoorWindowClass,
		TEXT("EHB_DoorWindow"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = Building;
	SpawnParams.OverrideLevel = Building->GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	AEHB_DoorWindow* DoorWindow = Building->GetWorld()->SpawnActor<AEHB_DoorWindow>(
		DoorWindowClass,
		Building->GetActorTransform(),
		SpawnParams);
	if (!DoorWindow)
	{
		return nullptr;
	}

	DoorWindow->SetFlags(RF_Transactional);
	DoorWindow->Modify();
	DoorWindow->ElementName = ActorName;
	DoorWindow->AttachToBuilding(Building, FTransform::Identity);

	if (DoorWindow->DoorWindowMeshComponent)
	{
		DoorWindow->DoorWindowMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

#if WITH_EDITOR
	DoorWindow->SetActorLabel(ActorName.ToString());
#endif

	return DoorWindow;
}

TSubclassOf<AEHB_DoorWindow> FEasyHouseEditorMode::ResolveDefaultDoorWindowActorClass(EEHBDoorWindowElementKind Kind) const
{
	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	auto LoadDoorWindowClass = [](const TSoftClassPtr<AEHB_DoorWindow>& SoftClass) -> UClass*
	{
		if (SoftClass.IsNull())
		{
			return nullptr;
		}

		UClass* LoadedClass = SoftClass.LoadSynchronous();
		return LoadedClass && LoadedClass->IsChildOf(AEHB_DoorWindow::StaticClass())
			? LoadedClass
			: nullptr;
	};

	if (ToolsetSettings)
	{
		const TSoftClassPtr<AEHB_DoorWindow>& PreferredClass =
			Kind == EEHBDoorWindowElementKind::Door
				? ToolsetSettings->DefaultDoorActorClass
				: ToolsetSettings->DefaultWindowActorClass;
		if (UClass* LoadedPreferredClass = LoadDoorWindowClass(PreferredClass))
		{
			return LoadedPreferredClass;
		}

		if (UClass* LoadedFallbackClass = LoadDoorWindowClass(ToolsetSettings->DoorWindowActorClass))
		{
			return LoadedFallbackClass;
		}
	}

	return AEHB_DoorWindow::StaticClass();
}

void FEasyHouseEditorMode::ApplyDoorWindowRowToActor(AEHB_DoorWindow* DoorWindow, const FEHBDoorWindowMeshData& RowData) const
{
	if (!DoorWindow)
	{
		return;
	}

	DoorWindow->Modify();
	DoorWindow->Kind = RowData.Kind;
	if (!RowData.SourceStaticMesh.IsNull())
	{
		DoorWindow->SourceStaticMesh = RowData.SourceStaticMesh;
	}
	else if (DoorWindow->SourceStaticMesh.IsNull() && DoorWindow->DoorWindowMeshComponent)
	{
		DoorWindow->SourceStaticMesh = DoorWindow->DoorWindowMeshComponent->GetStaticMesh();
	}
	DoorWindow->OpeningWidth = FMath::Max(1.0f, RowData.OpeningWidth);
	DoorWindow->OpeningHeight = FMath::Max(1.0f, RowData.OpeningHeight);
	DoorWindow->OpeningThickness = FMath::Max(1.0f, RowData.OpeningThickness);
	DoorWindow->SillHeight = RowData.Kind == EEHBDoorWindowElementKind::Door ? 0.0f : FMath::Max(0.0f, RowData.SillHeight);
	DoorWindow->SourceBoundsMin = RowData.SourceBoundsMin;
	DoorWindow->SourceBoundsMax = RowData.SourceBoundsMax;
	DoorWindow->RebuildDoorWindow();
}

void FEasyHouseEditorMode::ApplyDefaultDoorWindowToActor(AEHB_DoorWindow* DoorWindow, EEHBDoorWindowElementKind Kind, UClass* DoorWindowClass) const
{
	if (!DoorWindow)
	{
		return;
	}

	DoorWindow->Modify();
	const bool bPreserveConfiguredClassOpening =
		DoorWindowClass && DoorWindowClass != AEHB_DoorWindow::StaticClass();
	if (bPreserveConfiguredClassOpening)
	{
		DoorWindow->Kind = Kind;
		if (Kind == EEHBDoorWindowElementKind::Door)
		{
			DoorWindow->SillHeight = 0.0f;
		}
		DoorWindow->HandleOpeningSplineEdited();
		DoorWindow->RebuildDoorWindow();
		return;
	}

	if (Kind == EEHBDoorWindowElementKind::Door)
	{
		DoorWindow->InitializeDefaultDoorOpening();
	}
	else
	{
		DoorWindow->InitializeDefaultWindowOpening();
	}
	DoorWindow->RebuildDoorWindow();
}

AEHB_Wall* FEasyHouseEditorMode::ResolveWallFromHit(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (AEHB_Wall* Wall = Cast<AEHB_Wall>(HitActor))
	{
		return Wall;
	}

	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(HitComponent->GetOwner()))
		{
			return Wall;
		}
	}

	return HitActor ? Cast<AEHB_Wall>(HitActor->GetAttachParentActor()) : nullptr;
}

AEHB_DoorWindow* FEasyHouseEditorMode::ResolveDoorWindowFromHit(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(HitActor))
	{
		return DoorWindow;
	}

	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		if (AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(HitComponent->GetOwner()))
		{
			return DoorWindow;
		}
	}

	return HitActor ? Cast<AEHB_DoorWindow>(HitActor->GetAttachParentActor()) : nullptr;
}

AEHB_DoorWindow* FEasyHouseEditorMode::FindDoorWindowUnderCursor(const FHitResult& FirstHitResult) const
{
	if (AEHB_DoorWindow* DirectDoorWindow = ResolveDoorWindowFromHit(FirstHitResult))
	{
		return DirectDoorWindow;
	}

	FVector RayStart = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!GetViewportDropRay(RayStart, RayDirection, nullptr)
		|| !GCurrentLevelEditingViewportClient
		|| !GCurrentLevelEditingViewportClient->GetWorld())
	{
		return nullptr;
	}

	const FVector RayEnd = RayStart + RayDirection * HALF_WORLD_MAX;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_DoorWindowSelectionTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;
	if (AActor* PreviewActor = PreviewDoorWindowActor.Get())
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}

	TArray<FHitResult> HitResults;
	GCurrentLevelEditingViewportClient->GetWorld()->LineTraceMultiByChannel(
		HitResults,
		RayStart,
		RayEnd,
		ECC_Visibility,
		QueryParams);

	const AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	for (const FHitResult& HitResult : HitResults)
	{
		AEHB_DoorWindow* DoorWindow = ResolveDoorWindowFromHit(HitResult);
		if (!DoorWindow || DoorWindow->IsActorBeingDestroyed())
		{
			continue;
		}

		if (Building
			&& DoorWindow->OwningBuilding != Building
			&& DoorWindow->GetOwner() != Building
			&& DoorWindow->GetAttachParentActor() != Building)
		{
			continue;
		}

		return DoorWindow;
	}

	return nullptr;
}

AEHB_Pillar* FEasyHouseEditorMode::ResolvePillarFromHit(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(HitActor))
	{
		return Pillar;
	}

	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(HitComponent->GetOwner()))
		{
			return Pillar;
		}
	}

	return HitActor ? Cast<AEHB_Pillar>(HitActor->GetAttachParentActor()) : nullptr;
}

AEHB_Railing* FEasyHouseEditorMode::ResolveRailingFromHit(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (AEHB_Railing* Railing = Cast<AEHB_Railing>(HitActor))
	{
		return Railing;
	}

	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		if (AEHB_Railing* Railing = Cast<AEHB_Railing>(HitComponent->GetOwner()))
		{
			return Railing;
		}
	}

	return HitActor ? Cast<AEHB_Railing>(HitActor->GetAttachParentActor()) : nullptr;
}

AEHB_Stair* FEasyHouseEditorMode::ResolveStairFromHit(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (AEHB_Stair* Stair = Cast<AEHB_Stair>(HitActor))
	{
		return Stair;
	}

	if (UPrimitiveComponent* HitComponent = HitResult.GetComponent())
	{
		if (AEHB_Stair* Stair = Cast<AEHB_Stair>(HitComponent->GetOwner()))
		{
			return Stair;
		}
	}

	return HitActor ? Cast<AEHB_Stair>(HitActor->GetAttachParentActor()) : nullptr;
}

bool FEasyHouseEditorMode::UpdateDoorWindowPlacement(const FPointerEvent* MouseEvent)
{
	if (!bDoorWindowPlacementActive)
	{
		return false;
	}

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	AEHB_DoorWindow* DoorWindow = PreviewDoorWindowActor.Get();
	if (!Building || !DoorWindow)
	{
		return false;
	}

	FVector TargetLocation = DoorWindowPlacementWorldLocation;
	FRotator TargetRotation = DoorWindow->GetActorRotation();
	AEHB_Wall* HitWall = nullptr;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, MouseEvent) && HitResult.bBlockingHit)
	{
		TargetLocation = HitResult.ImpactPoint;
		HitWall = ResolveWallFromHit(HitResult);
	}
	else
	{
		FVector RayStart = FVector::ZeroVector;
		FVector RayDirection = FVector::ForwardVector;
		if (!GetViewportDropRay(RayStart, RayDirection, MouseEvent))
		{
			return false;
		}

		const double PlaneZ = Building->GetActorLocation().Z;
		if (FMath::Abs(RayDirection.Z) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const double DistanceAlongRay = (PlaneZ - RayStart.Z) / RayDirection.Z;
		if (DistanceAlongRay < 0.0)
		{
			return false;
		}

		TargetLocation = RayStart + RayDirection * DistanceAlongRay;
	}

	AEHB_Wall* PreviousHoveredWall = HoveredDoorWindowWall.Get();
	if (PreviousHoveredWall && HitWall != PreviousHoveredWall)
	{
		FVector RayStart = FVector::ZeroVector;
		FVector RayDirection = FVector::ForwardVector;
		FVector RetainedWallIntersection = FVector::ZeroVector;
		if (GetViewportDropRay(RayStart, RayDirection, MouseEvent)
			&& IntersectRayWithWallCenterPlane(
				PreviousHoveredWall,
				RayStart,
				RayDirection,
				RetainedWallIntersection))
		{
			const float RetainedWallDistance =
				FVector::DotProduct(RetainedWallIntersection - RayStart, RayDirection);
			const float TraceHitDistance =
				HitResult.bBlockingHit ? HitResult.Distance : TNumericLimits<float>::Max();

			// Once the preview hole exists, the visibility trace can pass through it and hit a wall
			// or floor behind it. Keep the current wall only when its original plane is still the
			// nearest valid wall location under the cursor.
			if (RetainedWallDistance <= TraceHitDistance + 0.1f)
			{
				HitWall = PreviousHoveredWall;
				TargetLocation = RetainedWallIntersection;
			}
		}
	}

	if (PreviousHoveredWall && PreviousHoveredWall != HitWall)
	{
		PreviousHoveredWall->ClearPreviewDoorWindowOpening();
	}

	if (HitWall)
	{
		TargetRotation = HitWall->GetActorRotation();
		TargetLocation = HitWall->ProjectWorldLocationToCenterAxis(TargetLocation, DoorWindow->GetOpeningBottomHeight());
	}
	else
	{
		TargetLocation.Z = Building->GetActorLocation().Z + DoorWindow->GetOpeningBottomHeight();
	}

	const FTransform WorldTransform(TargetRotation, TargetLocation);
	const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Building->GetActorTransform());
	DoorWindow->SetElementLocalTransform(LocalTransform, false);

	if (HitWall)
	{
		// The preview connection stores actor-to-wall transform, so apply snapping before rebuilding.
		DoorWindow->RequestPreviewWallOpening(HitWall);
	}

	DoorWindowPlacementWorldLocation = TargetLocation;
	HoveredDoorWindowWall = HitWall;

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

bool FEasyHouseEditorMode::CommitDoorWindowPlacement()
{
	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	AEHB_DoorWindow* DoorWindow = PreviewDoorWindowActor.Get();
	AEHB_Wall* Wall = HoveredDoorWindowWall.Get();
	if (!Building || !DoorWindow || !Wall)
	{
		return false;
	}

	if (!bDoorWindowPlacementUsesDefaultClass)
	{
		UDataTable* Table = DoorWindowPlacementTable.Get();
		if (!Table || DoorWindowPlacementRowName.IsNone() || Table->GetRowStruct() != FEHBDoorWindowMeshData::StaticStruct())
		{
			return false;
		}

		const FEHBDoorWindowMeshData* RowData = Table->FindRow<FEHBDoorWindowMeshData>(
			DoorWindowPlacementRowName,
			TEXT("FEasyHouseEditorMode::CommitDoorWindowPlacement"),
			false);
		if (!RowData)
		{
			return false;
		}

		const FEHBMeshSampleValidationResult ValidationResult = FEHBMeshSampleValidation::ValidateDoorWindowForTarget(*RowData, Wall);
		LogSampleValidationResult(TEXT("CommitDoorWindowPlacement"), ValidationResult);
		if (ValidationResult.HasErrors())
		{
			return false;
		}
	}

	const float DistanceFromStart = Wall->CalculateDistanceFromStartForWorldLocation(DoorWindow->GetActorLocation());

	const FScopedTransaction Transaction(LOCTEXT("PlaceDoorWindowOnWallTransaction", "放置门窗到墙体"));
	Building->Modify();
	Wall->Modify();
	DoorWindow->Modify();

	AEHB_DoorWindow* DoorWindowToReplace = Wall->FindOverlappingDoorWindow(DoorWindow, DistanceFromStart);

	// Replace the transient preview with one saved connection, then rebuild only once.
	Wall->ClearPreviewDoorWindowOpening(false);
	const bool bReplacedExistingDoorWindow =
		DoorWindowToReplace && AEHB_DoorWindow::ReplaceDoorWindow(DoorWindow, DoorWindowToReplace);
	if (!bReplacedExistingDoorWindow)
	{
		DoorWindow->BindToWall(Wall, DistanceFromStart);
		Wall->AddOrUpdateDoorWindowConnection(DoorWindow, DistanceFromStart);
		Wall->RebuildWallMesh();
	}

	if (DoorWindow->DoorWindowMeshComponent)
	{
		DoorWindow->DoorWindowMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		DoorWindow->DoorWindowMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
		DoorWindow->DoorWindowMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	}

	Building->MarkPackageDirty();
	Wall->MarkPackageDirty();
	DoorWindow->MarkPackageDirty();

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(DoorWindow, true, true, true);
	}

	return true;
}

bool FEasyHouseEditorMode::UpdateWallSurfacePlacement(const FPointerEvent* MouseEvent)
{
	if (!bWallSurfacePlacementActive)
	{
		return false;
	}

	AEHB_Wall* HitWall = nullptr;
	bool bHitLeftSide = true;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, MouseEvent) && HitResult.bBlockingHit)
	{
		HitWall = ResolveWallFromHit(HitResult);
		if (const UPrimitiveComponent* HitComponent = HitResult.GetComponent())
		{
			if (HitComponent->ComponentTags.Contains(TEXT("EHB_RightWall")))
			{
				bHitLeftSide = false;
			}
			else if (HitComponent->ComponentTags.Contains(TEXT("EHB_LeftWall")))
			{
				bHitLeftSide = true;
			}
		}
	}

	const bool bChanged = HoveredWallSurfaceWall.Get() != HitWall || bHoveredWallSurfaceLeftSide != bHitLeftSide;
	HoveredWallSurfaceWall = HitWall;
	bHoveredWallSurfaceLeftSide = bHitLeftSide;

	if (bChanged && GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return bChanged;
}

bool FEasyHouseEditorMode::CommitWallSurfacePlacement()
{
	AEHB_Wall* Wall = HoveredWallSurfaceWall.Get();
	UDataTable* Table = WallSurfacePlacementTable.Get();
	if (!Wall || !Table || WallSurfacePlacementRowName.IsNone() || Table->GetRowStruct() != FEHBWallMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBWallMeshData* RowData = Table->FindRow<FEHBWallMeshData>(
		WallSurfacePlacementRowName,
		TEXT("FEasyHouseEditorMode::CommitWallSurfacePlacement"),
		false);
	if (!RowData)
	{
		return false;
	}

	const FEHBMeshSampleValidationResult ValidationResult = FEHBMeshSampleValidation::ValidateWallSurfaceForTarget(*RowData, Wall, bWallSurfacePlacementCoverBothSides);
	LogSampleValidationResult(TEXT("CommitWallSurfacePlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyWallSurfaceSampleTransaction", "\u8986\u76d6\u5899\u9762\u91c7\u6837"));
	Wall->Modify();

	FDataTableRowHandle RowHandle;
	RowHandle.DataTable = Table;
	RowHandle.RowName = WallSurfacePlacementRowName;

	auto ApplySurfaceStyle = [&RowHandle, this](FEHBWallSurfaceStyle& SurfaceStyle, EEHBWallMeshSampleSide SampleSide)
	{
		SurfaceStyle.SourceType = EEHBWallSurfaceSourceType::SampledMesh;
		SurfaceStyle.SampledWallRow = RowHandle;
		SurfaceStyle.SampleSide = SampleSide;
		SurfaceStyle.bFlipSampleSide = bWallSurfacePlacementFlipSampleSides;
		SurfaceStyle.OverrideMaterial.Reset();
	};

	const EEHBWallMeshSampleSide LeftSampleSide = bWallSurfacePlacementFlipSampleSides
		? EEHBWallMeshSampleSide::Front
		: EEHBWallMeshSampleSide::Back;
	const EEHBWallMeshSampleSide RightSampleSide = bWallSurfacePlacementFlipSampleSides
		? EEHBWallMeshSampleSide::Back
		: EEHBWallMeshSampleSide::Front;

	if (bWallSurfacePlacementCoverBothSides)
	{
		ApplySurfaceStyle(Wall->LeftSurfaceStyle, LeftSampleSide);
		ApplySurfaceStyle(Wall->RightSurfaceStyle, RightSampleSide);
	}
	else if (bHoveredWallSurfaceLeftSide)
	{
		ApplySurfaceStyle(Wall->LeftSurfaceStyle, LeftSampleSide);
	}
	else
	{
		ApplySurfaceStyle(Wall->RightSurfaceStyle, RightSampleSide);
	}

	Wall->RebuildWallMesh();
	Wall->RebuildConnectedPillarMeshes();
	Wall->MarkPackageDirty();

	if (AEHBBuildingActorBase* Building = ActiveBuilding.Get())
	{
		Building->Modify();
		Building->MarkPackageDirty();
	}

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Wall, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

bool FEasyHouseEditorMode::UpdatePillarMeshPlacement(const FPointerEvent* MouseEvent)
{
	if (!bPillarMeshPlacementActive)
	{
		return false;
	}

	AEHB_Pillar* HitPillar = nullptr;

	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, MouseEvent) && HitResult.bBlockingHit)
	{
		HitPillar = ResolvePillarFromHit(HitResult);
	}

	const bool bChanged = HoveredPillarMeshPillar.Get() != HitPillar;
	HoveredPillarMeshPillar = HitPillar;

	if (bChanged && GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return bChanged;
}

bool FEasyHouseEditorMode::CommitPillarMeshPlacement()
{
	AEHB_Pillar* Pillar = HoveredPillarMeshPillar.Get();
	UDataTable* Table = PillarMeshPlacementTable.Get();
	if (!Pillar || !Table || PillarMeshPlacementRowName.IsNone() || Table->GetRowStruct() != FEHBPillarMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBPillarMeshData* RowData = Table->FindRow<FEHBPillarMeshData>(
		PillarMeshPlacementRowName,
		TEXT("FEasyHouseEditorMode::CommitPillarMeshPlacement"),
		false);
	if (!RowData)
	{
		return false;
	}

	const FEHBMeshSampleValidationResult ValidationResult = FEHBMeshSampleValidation::ValidatePillarForTarget(*RowData, Pillar);
	LogSampleValidationResult(TEXT("CommitPillarMeshPlacement"), ValidationResult);
	if (ValidationResult.HasErrors())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyPillarMeshSampleTransaction", "Apply Pillar Mesh Sample"));
	Pillar->Modify();
	if (!Pillar->ConfigureFromSampledPillarRow(Table, PillarMeshPlacementRowName, true))
	{
		return false;
	}

	if (AEHBBuildingActorBase* Building = Pillar->OwningBuilding)
	{
		Building->Modify();
		Building->MarkPackageDirty();
	}

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Pillar, true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

bool FEasyHouseEditorMode::UpdateRailingMeshPlacement(const FPointerEvent* MouseEvent)
{
	if (!bRailingMeshPlacementActive)
	{
		return false;
	}

	AEHB_Railing* HitRailing = nullptr;
	AEHB_Stair* HitStair = nullptr;
	EEHBRailingSide HitStairSide = EEHBRailingSide::Left;
	FGuid HitPostGuid;
	FHitResult HitResult;
	if (GetViewportDropHitResult(HitResult, MouseEvent) && HitResult.bBlockingHit)
	{
		HitRailing = ResolveRailingFromHit(HitResult);
		if (HitRailing
			&& (HitRailing->OwningBuilding != ActiveBuilding.Get()
				|| HitRailing->IsActorBeingDestroyed()))
		{
			HitRailing = nullptr;
		}

		if (HitRailing && bRailingMeshPlacementApplyToSinglePost)
		{
			if (HitResult.GetComponent() == HitRailing->PostMeshComponent.Get())
			{
				HitRailing->GetPostGuidForInstanceIndex(HitResult.Item, HitPostGuid);
			}
			else
			{
				HitRailing->GetPostGuidForOverrideComponent(HitResult.GetComponent(), HitPostGuid);
			}

			if (!HitPostGuid.IsValid())
			{
				HitRailing = nullptr;
			}
		}

		if (!HitRailing)
		{
			HitStair = ResolveStairFromHit(HitResult);
			if (HitStair
				&& (HitStair->OwningBuilding != ActiveBuilding.Get()
					|| HitStair->IsActorBeingDestroyed()))
			{
				HitStair = nullptr;
			}

			if (HitStair)
			{
				UPrimitiveComponent* HitComponent = HitResult.GetComponent();
				bool bHitEmbeddedRailing = false;
				if (HitComponent == HitStair->LeftRailingPostMeshComponent.Get()
					|| HitComponent == HitStair->LeftRailingRailMeshComponent.Get())
				{
					HitStairSide = EEHBRailingSide::Left;
					bHitEmbeddedRailing = true;
				}
				else if (HitComponent == HitStair->RightRailingPostMeshComponent.Get()
					|| HitComponent == HitStair->RightRailingRailMeshComponent.Get())
				{
					HitStairSide = EEHBRailingSide::Right;
					bHitEmbeddedRailing = true;
				}
				else if (HitStair->GetEmbeddedRailingPostGuidForOverrideComponent(HitComponent, HitPostGuid, HitStairSide))
				{
					bHitEmbeddedRailing = true;
				}

				if (!bHitEmbeddedRailing)
				{
					HitStair = nullptr;
				}
				else if (bRailingMeshPlacementApplyToSinglePost)
				{
					if (HitComponent == HitStair->LeftRailingPostMeshComponent.Get()
						|| HitComponent == HitStair->RightRailingPostMeshComponent.Get())
					{
						HitStair->GetEmbeddedRailingPostGuidForInstanceIndex(HitStairSide, HitResult.Item, HitPostGuid);
					}

					if (!HitPostGuid.IsValid())
					{
						const float SnapDistance = FMath::Max(30.0f, RailingCreationThickness * 4.0f);
						float BestDistanceSquared = FMath::Square(SnapDistance);
						for (const FEHBRailingPost& CandidatePost : HitStair->GetEmbeddedRailingPosts(HitStairSide))
						{
							if (CandidatePost.bSuppressInstance || !CandidatePost.PostGuid.IsValid())
							{
								continue;
							}

							const FVector CandidateWorldLocation = HitStair->GetActorTransform().TransformPosition(CandidatePost.LocalBaseLocation);
							const float DistanceSquared = FVector::DistSquared2D(CandidateWorldLocation, HitResult.ImpactPoint);
							if (DistanceSquared <= BestDistanceSquared)
							{
								BestDistanceSquared = DistanceSquared;
								HitPostGuid = CandidatePost.PostGuid;
							}
						}
					}

					if (!HitPostGuid.IsValid())
					{
						HitStair = nullptr;
					}
				}
			}
		}
	}

	const bool bChanged = HoveredRailingMeshRailing.Get() != HitRailing
		|| HoveredRailingMeshStair.Get() != HitStair
		|| HoveredRailingMeshStairSide != HitStairSide
		|| HoveredRailingMeshPostGuid != HitPostGuid;
	HoveredRailingMeshRailing = HitRailing;
	HoveredRailingMeshStair = HitStair;
	HoveredRailingMeshStairSide = HitStairSide;
	HoveredRailingMeshPostGuid = HitPostGuid;
	if (bChanged && GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return bChanged;
}

bool FEasyHouseEditorMode::CommitRailingMeshPlacement()
{
	AEHB_Railing* Railing = HoveredRailingMeshRailing.Get();
	AEHB_Stair* Stair = HoveredRailingMeshStair.Get();
	UDataTable* Table = RailingMeshPlacementTable.Get();
	if ((!Railing && !Stair) || !Table || RailingMeshPlacementRowName.IsNone() || Table->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBRailingMeshData* RowData = Table->FindRow<FEHBRailingMeshData>(
		RailingMeshPlacementRowName,
		TEXT("FEasyHouseEditorMode::CommitRailingMeshPlacement"),
		false);
	if (!RowData)
	{
		return false;
	}

	if (Railing)
	{
		const FEHBMeshSampleValidationResult ValidationResult = FEHBMeshSampleValidation::ValidateRailingForTarget(*RowData, Railing);
		LogSampleValidationResult(TEXT("CommitRailingMeshPlacement"), ValidationResult);
		if (ValidationResult.HasErrors())
		{
			return false;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyRailingMeshSampleTransaction", "应用扶手采样"));
	if (Railing)
	{
		Railing->Modify();
	}
	if (Stair)
	{
		Stair->Modify();
	}
	if (bRailingMeshPlacementApplyToSinglePost
		&& (!HoveredRailingMeshPostGuid.IsValid() || RowData->PostMesh.SourceStaticMesh.IsNull()))
	{
		return false;
	}
	const bool bApplied = Railing
		? (bRailingMeshPlacementApplyToSinglePost
			? Railing->ApplyPostMeshSampleToPost(HoveredRailingMeshPostGuid, Table, RailingMeshPlacementRowName, true)
			: Railing->ConfigureFromSampledRailingRow(Table, RailingMeshPlacementRowName, true))
		: (bRailingMeshPlacementApplyToSinglePost
			? Stair->ApplyRailingMeshSampleToEmbeddedPost(HoveredRailingMeshPostGuid, Table, RailingMeshPlacementRowName, true)
			: Stair->ApplyRailingMeshSampleToEmbeddedRailings(Table, RailingMeshPlacementRowName, true));
	if (!bApplied)
	{
		return false;
	}

	if (AEHBBuildingActorBase* Building = Railing ? Railing->OwningBuilding : Stair->OwningBuilding)
	{
		Building->Modify();
		Building->MarkPackageDirty();
	}

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Railing ? Cast<AActor>(Railing) : Cast<AActor>(Stair), true, true, true);
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

void FEasyHouseEditorMode::DrawPreviewPillar(FPrimitiveDrawInterface* PDI, const FVector& Center, const FVector& Forward, const FVector& Right) const
{
	if (!PDI)
	{
		return;
	}

	const FLinearColor PreviewColor(0.0f, 1.0f, 0.25f, 0.65f);
	const float HalfSize = FMath::Max(1.0f, WallCreationThickness) * 0.5f;
	const FVector HeightOffset(0.0f, 0.0f, FMath::Max(1.0f, WallCreationHeight));
	const FVector SafeForward = Forward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
	const FVector SafeRight = Right.GetSafeNormal2D().IsNearlyZero() ? FVector::RightVector : Right.GetSafeNormal2D();
	const FVector HalfForward = SafeForward * HalfSize;
	const FVector HalfRight = SafeRight * HalfSize;
	const TArray<FVector> BottomCorners = {
		Center - HalfForward - HalfRight,
		Center - HalfForward + HalfRight,
		Center + HalfForward + HalfRight,
		Center + HalfForward - HalfRight
	};

	for (int32 Index = 0; Index < BottomCorners.Num(); ++Index)
	{
		const FVector A = BottomCorners[Index];
		const FVector B = BottomCorners[(Index + 1) % BottomCorners.Num()];
		PDI->DrawLine(A, B, PreviewColor, SDPG_Foreground, 2.0f);
		PDI->DrawLine(A + HeightOffset, B + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
		PDI->DrawLine(A, A + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	}
}

void FEasyHouseEditorMode::DrawExistingPillarPreview(FPrimitiveDrawInterface* PDI, const AEHB_Pillar* Pillar, const FLinearColor& Color) const
{
	if (!PDI || !Pillar)
	{
		return;
	}

	TArray<FVector> LocalFootprint;
	Pillar->GetPillarFootprintLocalPoints(LocalFootprint);
	if (LocalFootprint.Num() < 3)
	{
		return;
	}

	const FTransform PillarTransform = Pillar->GetActorTransform();
	const float Height = FMath::Max(1.0f, Pillar->Height);
	for (int32 PointIndex = 0; PointIndex < LocalFootprint.Num(); ++PointIndex)
	{
		FVector LocalA = LocalFootprint[PointIndex];
		FVector LocalB = LocalFootprint[(PointIndex + 1) % LocalFootprint.Num()];
		LocalA.Z = 0.0f;
		LocalB.Z = 0.0f;

		const FVector BottomA = PillarTransform.TransformPosition(LocalA);
		const FVector BottomB = PillarTransform.TransformPosition(LocalB);
		LocalA.Z = Height;
		LocalB.Z = Height;
		const FVector TopA = PillarTransform.TransformPosition(LocalA);
		const FVector TopB = PillarTransform.TransformPosition(LocalB);

		PDI->DrawLine(BottomA, BottomB, Color, SDPG_Foreground, 3.0f);
		PDI->DrawLine(TopA, TopB, Color, SDPG_Foreground, 3.0f);
		PDI->DrawLine(BottomA, TopA, Color, SDPG_Foreground, 2.5f);
	}
}

bool FEasyHouseEditorMode::DrawWallCreationPathPreview(FPrimitiveDrawInterface* PDI,
 const AEHBBuildingActorBase* Building, const TArray<FWallCreationEndpointSnap>& Endpoints, bool bClosed) const
{
 if (!Building || !PDI) return false;
 const bool bAnchored = Endpoints.ContainsByPredicate([](const auto& E) { return E.Wall != nullptr; });
 if (Building->WallNodeAuthority.Version != 2 && bAnchored) return false;
 TArray<FEHBWallCreationEndpoint> Requests;
 for (const auto& E : Endpoints)
 {
  auto& R = Requests.AddDefaulted_GetRef(); R.LocalLocation=E.LocalLocation; R.WorldLocation=E.WorldLocation;
  R.Pillar=E.Pillar; R.Wall=E.Wall; R.FloorIndex=E.FloorIndex; R.WallDistance=E.WallDistance;
  R.NodeGuid=E.NodeGuid; R.ExpectedNodeRevision=E.ExpectedNodeRevision;
 }
 FEHBWallCreationOptions Options;
 // Mouse resolution already applies host/grid/drawing assistance. Preserve that exact preview.
 Options.bSnapToIntegerBuildingCoordinates=false;
 Options.bCreatePhysicalColumns=ShouldCreateWallColumns();
 Options.WallThickness=Options.PillarWidth=Options.PillarDepth=WallCreationThickness;
 Options.WallHeight=Options.PillarHeight=WallCreationHeight;
 Options.FloorIndex=FMath::Max(1,WallCreationStartPillarFloorIndex);
 if (Building->WallNodeAuthority.Version == 2)
 {
  FEHBWallPathPreview Preview;
  const auto Result=bAnchored
   ? EHBWallCreationCommand::CommitAnchoredPath(const_cast<AEHBBuildingActorBase*>(Building),Requests,bClosed,Options,true,&Preview)
   : EHBWallCreationCommand::Commit(const_cast<AEHBBuildingActorBase*>(Building),Requests,bClosed,Options,true,&Preview);
  if(!Result.bSucceeded)
  {
   for(int32 I=0;I<(bClosed?Endpoints.Num():Endpoints.Num()-1);++I)
    PDI->DrawLine(Endpoints[I].WorldLocation,Endpoints[(I+1)%Endpoints.Num()].WorldLocation,FLinearColor::Red,SDPG_Foreground,3);
   return true;
  }
  const FTransform Transform=Building->GetActorTransform();const FLinearColor Color(0,1,0.25f,0.65f);
  auto DrawPrism=[&](const TArray<FVector>& Local,const FVector& Height)
  {
   for(int32 I=0;I<Local.Num();++I)
   {
    auto Line=[&](const FVector& A,const FVector& B){PDI->DrawLine(Transform.TransformPosition(A),Transform.TransformPosition(B),Color,SDPG_Foreground,2);};
    Line(Local[I],Local[(I+1)%Local.Num()]);Line(Local[I]+Height,Local[(I+1)%Local.Num()]+Height);Line(Local[I],Local[I]+Height);
   }
  };
  TSet<FGuid> DrawnNodes;
  for(const auto& Side:Preview.Sides)if(Preview.WallGuids.Contains(Side.WallGuid))
  {
   const auto* Wall=Preview.Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Side.WallGuid;});if(!Wall)continue;
   // The shared side solver returns TOP boundary points, while DrawPrism takes a base.
   const FVector Height(0,0,FMath::Max(1.0f,Wall->Height));
   DrawPrism({Side.StartLeft-Height,Side.EndLeft-Height,Side.EndRight-Height,Side.StartRight-Height},Height);
   DrawnNodes.Add(Wall->StartNodeGuid);DrawnNodes.Add(Wall->EndNodeGuid);
  }
  for(const auto& Node:Preview.Model.Nodes)if(DrawnNodes.Contains(Node.NodeGuid))
  {
   if(Preview.NewPhysicalNodeGuids.Contains(Node.NodeGuid))
   {
    const FVector Half=Node.JunctionDimensions*0.5;
    TArray<FVector> Corners;for(const FVector& Corner:{FVector(-Half.X,-Half.Y,0),FVector(Half.X,-Half.Y,0),FVector(Half.X,Half.Y,0),FVector(-Half.X,Half.Y,0)})Corners.Add(Node.LocalTransform.TransformPosition(Corner));
    DrawPrism(Corners,FVector(0,0,Node.JunctionDimensions.Z));
   }
   else if(!Preview.Model.PillarBindings.ContainsByPredicate([&](const auto& P){return P.NodeGuid==Node.NodeGuid;}))
    PDI->DrawPoint(Transform.TransformPosition(Node.LocalTransform.GetLocation()+FVector(0,0,Node.JunctionDimensions.Z)),FLinearColor(0,1,1),10,SDPG_Foreground);
  }
  return true;
 }
 const auto Plan = FEHBWallPathPlanning::BuildForBuilding(Building, Requests, bClosed, Options);
 const auto Transform=Building->GetActorTransform();
 if (!Plan.bSucceeded)
 {
  // Invalid route is visible, but never presented as a successful green wall.
  for(int32 I=0;I<(bClosed?Endpoints.Num():Endpoints.Num()-1);++I)
   PDI->DrawLine(Endpoints[I].WorldLocation,Endpoints[(I+1)%Endpoints.Num()].WorldLocation,FLinearColor::Red,SDPG_Foreground,3);
  return true;
 }
 const FLinearColor Color(0,1,0.25f,0.65f);
 const float Half=FMath::Max(1.0f,WallCreationThickness)*0.5f;
 const FVector Height=Transform.TransformVector(FVector(0,0,FMath::Max(1.0f,WallCreationHeight)));
 TSet<int32> DrawnPoints;
 for(const auto& Edge:Plan.Segments)
 {
  const auto A=Plan.Points[Edge.X],B=Plan.Points[Edge.Y];
  const FVector Direction=(B.LocalPosition-A.LocalPosition).GetSafeNormal2D();
  FVector Start=A.LocalPosition+Direction*Half,End=B.LocalPosition-Direction*Half;
  const auto* StartPillar=Cast<AEHB_Pillar>(Building->FindElementActorByGuid(A.ExistingPillarGuid));
  const auto* EndPillar=Cast<AEHB_Pillar>(Building->FindElementActorByGuid(B.ExistingPillarGuid));
  if(StartPillar)StartPillar->ResolveWallConnectionPointToward(B.LocalPosition,WallCreationThickness,Start);
  if(EndPillar)EndPillar->ResolveWallConnectionPointToward(A.LocalPosition,WallCreationThickness,End);
  const FVector Right=FVector::CrossProduct(FVector::UpVector,Direction)*Half;
  const FVector Corners[]={Transform.TransformPosition(Start+Right),Transform.TransformPosition(End+Right),Transform.TransformPosition(End-Right),Transform.TransformPosition(Start-Right)};
  for(int32 I=0;I<4;++I)
  {
   PDI->DrawLine(Corners[I],Corners[(I+1)%4],Color,SDPG_Foreground,2);
   PDI->DrawLine(Corners[I]+Height,Corners[(I+1)%4]+Height,Color,SDPG_Foreground,2);
   PDI->DrawLine(Corners[I],Corners[I]+Height,Color,SDPG_Foreground,2);
  }
  for(int32 Index:{Edge.X,Edge.Y})
  {
   if(DrawnPoints.Contains(Index))continue; DrawnPoints.Add(Index);
   const auto& Point=Plan.Points[Index];
   if(const auto* Pillar=Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Point.ExistingPillarGuid)))
    DrawExistingPillarPreview(PDI,Pillar,FLinearColor(0.1f,1,0.45f,0.95f));
   else
    DrawPreviewPillar(PDI,Transform.TransformPosition(Point.LocalPosition),Transform.TransformVectorNoScale(Direction),Transform.TransformVectorNoScale(FVector::CrossProduct(FVector::UpVector,Direction)));
  }
 }
 return true;
}

void FEasyHouseEditorMode::DrawWallCreationPreview(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bWallCreationToolActive)
	{
		return;
	}

	const FLinearColor PreviewColor(0.0f, 1.0f, 0.25f, 0.65f);
	const FLinearColor SnappedPillarColor(0.1f, 1.0f, 0.45f, 0.95f);
	if (!bWallCreationDragging)
	{
		if (HoveredWallCreationNode.IsValid())
		{
			const auto* Building=ActiveBuilding.Get();
			const auto* Node=Building?Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==HoveredWallCreationNode;}):nullptr;
			if(Node)PDI->DrawPoint(Building->GetActorTransform().TransformPosition(Node->LocalTransform.GetLocation()+FVector(0,0,Node->JunctionDimensions.Z)),FLinearColor(0,1,1),10,SDPG_Foreground);
			return;
		}
		if (const AEHB_Pillar* HoveredPillar = HoveredWallCreationPillar.Get())
		{
			DrawExistingPillarPreview(PDI, HoveredPillar, SnappedPillarColor);
			return;
		}
		if(ShouldCreateWallColumns())DrawPreviewPillar(PDI, WallCreationMouseLocation, FVector::ForwardVector, FVector::RightVector);
		else PDI->DrawPoint(WallCreationMouseLocation+FVector(0,0,WallCreationHeight),FLinearColor(0,1,1),10,SDPG_Foreground);
		return;
	}

	if (IsWallCreationRectangleModeActive())
	{
		const AEHBBuildingActorBase* Building = ActiveBuilding.Get();
		if (Building)
		{
			const FTransform BuildingTransform = Building->GetActorTransform();
			FVector LocalStart = BuildingTransform.InverseTransformPosition(WallCreationStartLocation);
			FVector LocalEnd = BuildingTransform.InverseTransformPosition(WallCreationMouseLocation);
			LocalEnd.Z = LocalStart.Z;

			const float Width = FMath::Abs(LocalEnd.X - LocalStart.X);
			const float Depth = FMath::Abs(LocalEnd.Y - LocalStart.Y);
			const float MinRectangleSide = FMath::Max(10.0f, WallCreationThickness + 1.0f);
			if (Width > MinRectangleSide && Depth > MinRectangleSide)
			{
				const TArray<FVector> LocalCorners = {
					FVector(LocalStart.X, LocalStart.Y, LocalStart.Z),
					FVector(LocalEnd.X, LocalStart.Y, LocalStart.Z),
					FVector(LocalEnd.X, LocalEnd.Y, LocalStart.Z),
					FVector(LocalStart.X, LocalEnd.Y, LocalStart.Z)
				};

				TArray<FWallCreationEndpointSnap> CornerSnaps;
				BuildWallCreationRectangleEndpoints(Building, LocalCorners, CornerSnaps);

				if (DrawWallCreationPathPreview(PDI, Building, CornerSnaps, true)) return;

				FVector WorldX = BuildingTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal2D();
				FVector WorldY = BuildingTransform.TransformVectorNoScale(FVector::RightVector).GetSafeNormal2D();
				if (WorldX.IsNearlyZero())
				{
					WorldX = FVector::ForwardVector;
				}
				if (WorldY.IsNearlyZero())
				{
					WorldY = FVector::RightVector;
				}

				for (const FWallCreationEndpointSnap& CornerSnap : CornerSnaps)
				{
					if (CornerSnap.Pillar)
					{
						DrawExistingPillarPreview(PDI, CornerSnap.Pillar, SnappedPillarColor);
					}
					else
					{
						DrawPreviewPillar(PDI, CornerSnap.WorldLocation, WorldX, WorldY);
					}
				}

				const float HalfThickness = FMath::Max(1.0f, WallCreationThickness) * 0.5f;
				const FVector HeightOffset(0.0f, 0.0f, FMath::Max(1.0f, WallCreationHeight));
				auto DrawRectangleWallSegment = [&](const FVector& SegmentStartCenter, const FVector& SegmentEndCenter)
				{
					FVector WallPreviewStart = FVector::ZeroVector;
					FVector WallPreviewEnd = FVector::ZeroVector;
					if (!GetWallSegmentBetweenPillarSides(SegmentStartCenter, SegmentEndCenter, WallCreationThickness, WallPreviewStart, WallPreviewEnd))
					{
						return;
					}

					const FVector SegmentDirection = (SegmentEndCenter - SegmentStartCenter).GetSafeNormal2D();
					if (SegmentDirection.IsNearlyZero())
					{
						return;
					}

					const FVector SegmentRight = FVector::CrossProduct(FVector::UpVector, SegmentDirection).GetSafeNormal();
					const FVector StartLeft = WallPreviewStart + SegmentRight * HalfThickness;
					const FVector StartRight = WallPreviewStart - SegmentRight * HalfThickness;
					const FVector EndLeft = WallPreviewEnd + SegmentRight * HalfThickness;
					const FVector EndRight = WallPreviewEnd - SegmentRight * HalfThickness;

					PDI->DrawLine(StartLeft, EndLeft, PreviewColor, SDPG_Foreground, 3.0f);
					PDI->DrawLine(StartRight, EndRight, PreviewColor, SDPG_Foreground, 3.0f);
					PDI->DrawLine(StartLeft + HeightOffset, EndLeft + HeightOffset, PreviewColor, SDPG_Foreground, 3.0f);
					PDI->DrawLine(StartRight + HeightOffset, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 3.0f);
					PDI->DrawLine(StartLeft, StartLeft + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(StartRight, StartRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(EndLeft, EndLeft + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(EndRight, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(StartLeft, StartRight, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(EndLeft, EndRight, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(StartLeft + HeightOffset, StartRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
					PDI->DrawLine(EndLeft + HeightOffset, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
				};

				for (int32 EdgeIndex = 0; EdgeIndex < CornerSnaps.Num(); ++EdgeIndex)
				{
					DrawRectangleWallSegment(CornerSnaps[EdgeIndex].WorldLocation, CornerSnaps[(EdgeIndex + 1) % CornerSnaps.Num()].WorldLocation);
				}
				return;
			}
		}
	}

	const FVector Direction = (WallCreationMouseLocation - WallCreationStartLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	const AEHB_Pillar* StartPillar = WallCreationStartPillar.Get();
	const AEHB_Pillar* EndPillar = HoveredWallCreationPillar.Get();
	if (EndPillar == StartPillar)
	{
		EndPillar = nullptr;
	}

 if (const auto* Active = ActiveBuilding.Get(); Active && (Active->WallNodeAuthority.Version==2 || (!WallCreationStartWall.IsValid() && !HoveredWallCreationWall.IsValid())))
 {
  TArray<FWallCreationEndpointSnap> Endpoints; Endpoints.SetNum(2);
  Endpoints[0].Pillar=const_cast<AEHB_Pillar*>(StartPillar); Endpoints[1].Pillar=const_cast<AEHB_Pillar*>(EndPillar);
  Endpoints[0].WorldLocation=StartPillar?StartPillar->GetActorLocation():WallCreationStartLocation;
  Endpoints[1].WorldLocation=EndPillar?EndPillar->GetActorLocation():WallCreationMouseLocation;
  Endpoints[0].FloorIndex=WallCreationStartPillarFloorIndex; Endpoints[1].FloorIndex=HoveredWallCreationPillarFloorIndex;
  Endpoints[0].Wall=WallCreationStartWall.Get();Endpoints[1].Wall=HoveredWallCreationWall.Get();
  Endpoints[0].WallDistance=WallCreationStartWallDistance;Endpoints[1].WallDistance=HoveredWallCreationWallDistance;
  Endpoints[0].NodeGuid=WallCreationStartNode;Endpoints[1].NodeGuid=HoveredWallCreationNode;
  Endpoints[0].ExpectedNodeRevision=WallCreationStartNodeRevision;Endpoints[1].ExpectedNodeRevision=HoveredWallCreationNodeRevision;
  for(auto& E:Endpoints)E.LocalLocation=Active->GetActorTransform().InverseTransformPosition(E.WorldLocation);
  if(DrawWallCreationPathPreview(PDI,Active,Endpoints,false))return;
 }

	if (StartPillar)
	{
		DrawExistingPillarPreview(PDI, StartPillar, SnappedPillarColor);
	}
	else
	{
		DrawPreviewPillar(PDI, WallCreationStartLocation, Direction, Right);
	}

	if (EndPillar)
	{
		DrawExistingPillarPreview(PDI, EndPillar, SnappedPillarColor);
	}
	else
	{
		DrawPreviewPillar(PDI, WallCreationMouseLocation, Direction, Right);
	}

	FVector WallPreviewStart = FVector::ZeroVector;
	FVector WallPreviewEnd = FVector::ZeroVector;
	const AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (Building)
	{
		FVector LocalStart = StartPillar
			? StartPillar->GetElementLocalTransform().GetLocation()
			: Building->GetActorTransform().InverseTransformPosition(WallCreationStartLocation);
		FVector LocalEnd = EndPillar
			? EndPillar->GetElementLocalTransform().GetLocation()
			: Building->GetActorTransform().InverseTransformPosition(WallCreationMouseLocation);
		const FVector LocalDirection = (LocalEnd - LocalStart).GetSafeNormal2D();
		if (LocalDirection.IsNearlyZero())
		{
			return;
		}

		FVector LocalWallStart = LocalStart + LocalDirection * (FMath::Max(1.0f, WallCreationThickness) * 0.5f);
		FVector LocalWallEnd = LocalEnd - LocalDirection * (FMath::Max(1.0f, WallCreationThickness) * 0.5f);
		if (StartPillar)
		{
			StartPillar->ResolveWallConnectionPointToward(LocalEnd, WallCreationThickness, LocalWallStart);
		}
		if (EndPillar)
		{
			EndPillar->ResolveWallConnectionPointToward(LocalStart, WallCreationThickness, LocalWallEnd);
		}

		if (FVector::Dist2D(LocalWallStart, LocalWallEnd) <= 1.0f)
		{
			return;
		}

		WallPreviewStart = Building->GetActorTransform().TransformPosition(LocalWallStart);
		WallPreviewEnd = Building->GetActorTransform().TransformPosition(LocalWallEnd);
	}
	else if (!GetWallSegmentBetweenPillarSides(WallCreationStartLocation, WallCreationMouseLocation, WallCreationThickness, WallPreviewStart, WallPreviewEnd))
	{
		return;
	}

	const float HalfThickness = FMath::Max(1.0f, WallCreationThickness) * 0.5f;
	const FVector HeightOffset(0.0f, 0.0f, FMath::Max(1.0f, WallCreationHeight));

	const FVector StartLeft = WallPreviewStart + Right * HalfThickness;
	const FVector StartRight = WallPreviewStart - Right * HalfThickness;
	const FVector EndLeft = WallPreviewEnd + Right * HalfThickness;
	const FVector EndRight = WallPreviewEnd - Right * HalfThickness;

	PDI->DrawLine(StartLeft, EndLeft, PreviewColor, SDPG_Foreground, 3.0f);
	PDI->DrawLine(StartRight, EndRight, PreviewColor, SDPG_Foreground, 3.0f);
	PDI->DrawLine(StartLeft + HeightOffset, EndLeft + HeightOffset, PreviewColor, SDPG_Foreground, 3.0f);
	PDI->DrawLine(StartRight + HeightOffset, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 3.0f);
	PDI->DrawLine(StartLeft, StartLeft + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(StartRight, StartRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(EndLeft, EndLeft + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(EndRight, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(StartLeft, StartRight, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(EndLeft, EndRight, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(StartLeft + HeightOffset, StartRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(EndLeft + HeightOffset, EndRight + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
}

void FEasyHouseEditorMode::DrawRailingCreationPostPreview(
	FPrimitiveDrawInterface* PDI,
	const FVector& Center,
	const FVector& Forward,
	const FVector& Right) const
{
	if (!PDI)
	{
		return;
	}

	const FLinearColor PreviewColor(0.0f, 0.8f, 1.0f, 0.75f);
	const float HalfSize = FMath::Max(0.1f, RailingCreationThickness) * 0.5f;
	const FVector HeightOffset(0.0f, 0.0f, FMath::Max(1.0f, RailingCreationHeight));
	const FVector SafeForward = Forward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
	const FVector SafeRight = Right.GetSafeNormal2D().IsNearlyZero() ? FVector::RightVector : Right.GetSafeNormal2D();
	const FVector HalfForward = SafeForward * HalfSize;
	const FVector HalfRight = SafeRight * HalfSize;
	const TArray<FVector> BottomCorners = {
		Center - HalfForward - HalfRight,
		Center - HalfForward + HalfRight,
		Center + HalfForward + HalfRight,
		Center + HalfForward - HalfRight
	};

	for (int32 Index = 0; Index < BottomCorners.Num(); ++Index)
	{
		const FVector A = BottomCorners[Index];
		const FVector B = BottomCorners[(Index + 1) % BottomCorners.Num()];
		PDI->DrawLine(A, B, PreviewColor, SDPG_Foreground, 2.0f);
		PDI->DrawLine(A + HeightOffset, B + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
		PDI->DrawLine(A, A + HeightOffset, PreviewColor, SDPG_Foreground, 2.0f);
	}
}

void FEasyHouseEditorMode::DrawRailingCreationPostSnapIndicator(FPrimitiveDrawInterface* PDI, const FVector& Center) const
{
	if (!PDI)
	{
		return;
	}

	const FLinearColor IndicatorColor(1.0f, 0.72f, 0.0f, 0.95f);
	const float HalfSize = FMath::Max(12.0f, FMath::Max(0.1f, RailingCreationThickness) * 2.0f);
	const float Height = FMath::Max(1.0f, RailingCreationHeight);
	const FVector X(HalfSize, 0.0f, 0.0f);
	const FVector Y(0.0f, HalfSize, 0.0f);
	const FVector Z(0.0f, 0.0f, Height);
	const FVector BaseCorners[4] = {
		Center + X,
		Center + Y,
		Center - X,
		Center - Y
	};

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector A = BaseCorners[Index];
		const FVector B = BaseCorners[(Index + 1) % 4];
		PDI->DrawLine(A, B, IndicatorColor, SDPG_Foreground, 3.0f);
		PDI->DrawLine(A + Z, B + Z, IndicatorColor, SDPG_Foreground, 2.0f);
	}

	PDI->DrawLine(Center - X, Center + X, IndicatorColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(Center - Y, Center + Y, IndicatorColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(Center, Center + Z, IndicatorColor, SDPG_Foreground, 3.0f);
}

void FEasyHouseEditorMode::DrawRailingCreationPreview(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bRailingCreationToolActive)
	{
		return;
	}

	const auto& WallRequest = bRailingCreationDragging ? RailingWallDrag : HoveredRailingWall;
	if (WallRequest.IsSet())
	{
		const FLinearColor Color = bRailingWallReady ? FLinearColor(0, 0.9f, 0.8f) : FLinearColor::Red;
		const FVector Forward = WallRequest.BuildingTransform.TransformVectorNoScale((WallRequest.End - WallRequest.Start).GetSafeNormal2D());
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector X = Forward * WallRequest.Thickness * 0.5f, Y = Right * WallRequest.Thickness * 0.5f;
		const FVector Z(0, 0, WallRequest.Height);
		const FVector Corners[] = {WallRequest.WorldStart-X-Y, WallRequest.WorldStart+X-Y, WallRequest.WorldStart+X+Y, WallRequest.WorldStart-X+Y};
		for (int32 I = 0; I < 4; ++I)
		{
			PDI->DrawLine(Corners[I], Corners[(I+1)%4], Color, SDPG_Foreground, 2);
			PDI->DrawLine(Corners[I]+Z, Corners[(I+1)%4]+Z, Color, SDPG_Foreground, 2);
			PDI->DrawLine(Corners[I], Corners[I]+Z, Color, SDPG_Foreground, 2);
		}
		if (bRailingCreationDragging)
		{
			const FVector RailZ(0, 0, RailingCreationHeight);
			FVector RailStart = WallRequest.WorldStart;
			const FVector RailDirection = (RailingCreationMouseLocation-RailStart).GetSafeNormal2D();
			const double AcrossWall = FMath::Abs(FVector::CrossProduct(RailDirection, Forward).Z);
			if (bRailingWallReady && AcrossWall > 0.001 && !EHBWallRailingJunction::ClearsRetainedWall(
				RailDirection, Forward, WallRequest.Thickness, WallRequest.Thickness, RailingCreationThickness))
				RailStart += RailDirection * (WallRequest.Thickness * 0.5 / AcrossWall);
			PDI->DrawLine(RailStart+RailZ, RailingCreationMouseLocation+RailZ, Color, SDPG_Foreground, 3);
			const FVector Delta = RailingCreationMouseLocation - WallRequest.WorldStart;
			const int32 Segments = FMath::Max(1, FMath::CeilToInt(FMath::Min(2048.0, Delta.Size() / FMath::Max(1.0f, RailingCreationPostSpacing))));
			for (int32 Index = 1; Index <= Segments; ++Index)
			{
				const FVector Base = WallRequest.WorldStart + Delta * (static_cast<double>(Index) / Segments);
				const FVector Along = Delta.GetSafeNormal2D() * RailingCreationThickness * 0.5f;
				const FVector Across = FVector::CrossProduct(FVector::UpVector, Along);
				const FVector PostCorners[] = {Base-Along-Across, Base+Along-Across, Base+Along+Across, Base-Along+Across};
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					PDI->DrawLine(PostCorners[Corner], PostCorners[(Corner+1)%4], Color, SDPG_Foreground, 1);
					PDI->DrawLine(PostCorners[Corner]+RailZ, PostCorners[(Corner+1)%4]+RailZ, Color, SDPG_Foreground, 1);
					PDI->DrawLine(PostCorners[Corner], PostCorners[Corner]+RailZ, Color, SDPG_Foreground, 1);
				}
			}
		}
		return;
	}
	const FLinearColor PreviewColor(0.0f, 0.8f, 1.0f, 0.75f);
	const FLinearColor SnappedPillarColor(0.0f, 0.95f, 1.0f, 0.95f);
	if (!bRailingCreationDragging)
	{
		if (const AEHB_Pillar* HoveredPillar = HoveredRailingCreationPillar.Get())
		{
			DrawExistingPillarPreview(PDI, HoveredPillar, SnappedPillarColor);
			return;
		}
		if (HoveredRailingCreationRailing.IsValid() && HoveredRailingCreationPostGuid.IsValid())
		{
			DrawRailingCreationPostSnapIndicator(PDI, RailingCreationMouseLocation);
			return;
		}
		if (HoveredRailingCreationStair.IsValid() && HoveredRailingCreationPostGuid.IsValid())
		{
			DrawRailingCreationPostSnapIndicator(PDI, RailingCreationMouseLocation);
			return;
		}
		DrawRailingCreationPostPreview(PDI, RailingCreationMouseLocation, FVector::ForwardVector, FVector::RightVector);
		return;
	}

	const FVector Direction = (RailingCreationMouseLocation - RailingCreationStartLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	const float Length = FVector::Distance(RailingCreationStartLocation, RailingCreationMouseLocation);
	const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(Length / FMath::Max(1.0f, RailingCreationPostSpacing)));
	const FVector HeightOffset(0.0f, 0.0f, FMath::Max(1.0f, RailingCreationHeight));
	const bool bOmitPreviewStartPost = RailingCreationStartPillar.IsValid()
		|| (RailingCreationStartRailing.IsValid() && RailingCreationStartPostGuid.IsValid())
		|| (RailingCreationStartStair.IsValid() && RailingCreationStartPostGuid.IsValid());
	const bool bOmitPreviewEndPost = HoveredRailingCreationPillar.IsValid()
		|| (HoveredRailingCreationRailing.IsValid() && HoveredRailingCreationPostGuid.IsValid())
		|| (HoveredRailingCreationStair.IsValid() && HoveredRailingCreationPostGuid.IsValid());

	for (int32 Index = 0; Index <= SegmentCount; ++Index)
	{
		if ((Index == 0 && bOmitPreviewStartPost) || (Index == SegmentCount && bOmitPreviewEndPost))
		{
			continue;
		}

		const float Alpha = static_cast<float>(Index) / static_cast<float>(SegmentCount);
		const FVector PostCenter = FMath::Lerp(RailingCreationStartLocation, RailingCreationMouseLocation, Alpha);
		DrawRailingCreationPostPreview(PDI, PostCenter, Direction, Right);
	}

	if (const AEHB_Pillar* StartPillar = RailingCreationStartPillar.Get())
	{
		DrawExistingPillarPreview(PDI, StartPillar, SnappedPillarColor);
	}
	if (const AEHB_Pillar* EndPillar = HoveredRailingCreationPillar.Get())
	{
		if (EndPillar != RailingCreationStartPillar.Get())
		{
			DrawExistingPillarPreview(PDI, EndPillar, SnappedPillarColor);
		}
	}
	if (RailingCreationStartRailing.IsValid() && RailingCreationStartPostGuid.IsValid())
	{
		DrawRailingCreationPostSnapIndicator(PDI, RailingCreationStartLocation);
	}
	if (RailingCreationStartStair.IsValid() && RailingCreationStartPostGuid.IsValid())
	{
		DrawRailingCreationPostSnapIndicator(PDI, RailingCreationStartLocation);
	}
	if (HoveredRailingCreationRailing.IsValid() && HoveredRailingCreationPostGuid.IsValid())
	{
		DrawRailingCreationPostSnapIndicator(PDI, RailingCreationMouseLocation);
	}
	if (HoveredRailingCreationStair.IsValid() && HoveredRailingCreationPostGuid.IsValid())
	{
		DrawRailingCreationPostSnapIndicator(PDI, RailingCreationMouseLocation);
	}

	PDI->DrawLine(RailingCreationStartLocation, RailingCreationMouseLocation, PreviewColor, SDPG_Foreground, 2.0f);
	PDI->DrawLine(RailingCreationStartLocation + HeightOffset, RailingCreationMouseLocation + HeightOffset, PreviewColor, SDPG_Foreground, 3.0f);
}

void FEasyHouseEditorMode::DrawWallSurfacePlacementPreview(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bWallSurfacePlacementActive)
	{
		return;
	}

	const AEHB_Wall* Wall = HoveredWallSurfaceWall.Get();
	if (!Wall)
	{
		return;
	}

	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	const float SafeHeight = FMath::Max(1.0f, Wall->Height);
	const float HalfThickness = FMath::Max(1.0f, Wall->Thickness) * 0.5f;
	if (WallLength <= UE_SMALL_NUMBER)
	{
		return;
	}

	const FMaterialRenderProxy* ParentMaterialProxy = GEngine && GEngine->GeomMaterial
		? GEngine->GeomMaterial->GetRenderProxy()
		: nullptr;
	if (!ParentMaterialProxy)
	{
		return;
	}

	const FLinearColor FillColor(0.0f, 1.0f, 0.12f, 0.35f);
	FDynamicColoredMaterialRenderProxy* FillMaterialProxy = new FDynamicColoredMaterialRenderProxy(ParentMaterialProxy, FillColor);
	PDI->RegisterDynamicResource(FillMaterialProxy);
	const FTransform WallTransform = Wall->GetActorTransform();
	constexpr float SurfaceOffset = 0.75f;

	auto DrawSurfaceQuad = [&](bool bLeftSide)
	{
		const float LocalY = bLeftSide ? HalfThickness : -HalfThickness;
		const FVector LocalNormal = bLeftSide ? FVector(0.0f, 1.0f, 0.0f) : FVector(0.0f, -1.0f, 0.0f);
		const FVector WorldNormal = WallTransform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
		const float HalfLength = WallLength * 0.5f;

		const FVector LocalBottomStart(-HalfLength, LocalY, 0.0f);
		const FVector LocalTopStart(-HalfLength, LocalY, SafeHeight);
		const FVector LocalTopEnd(HalfLength, LocalY, SafeHeight);
		const FVector LocalBottomEnd(HalfLength, LocalY, 0.0f);

		const FVector WorldBottomStart = WallTransform.TransformPosition(LocalBottomStart) + WorldNormal * SurfaceOffset;
		const FVector WorldTopStart = WallTransform.TransformPosition(LocalTopStart) + WorldNormal * SurfaceOffset;
		const FVector WorldTopEnd = WallTransform.TransformPosition(LocalTopEnd) + WorldNormal * SurfaceOffset;
		const FVector WorldBottomEnd = WallTransform.TransformPosition(LocalBottomEnd) + WorldNormal * SurfaceOffset;

		const FVector WorldTangent = (WorldBottomEnd - WorldBottomStart).GetSafeNormal();
		const FVector3f Tangent = FVector3f(WorldTangent);
		const FVector3f Normal = FVector3f(WorldNormal);

		FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
		MeshBuilder.AddVertex(FDynamicMeshVertex(FVector3f(WorldBottomStart), Tangent, Normal, FVector2f(0.0f, 1.0f), FColor::White));
		MeshBuilder.AddVertex(FDynamicMeshVertex(FVector3f(WorldTopStart), Tangent, Normal, FVector2f(0.0f, 0.0f), FColor::White));
		MeshBuilder.AddVertex(FDynamicMeshVertex(FVector3f(WorldTopEnd), Tangent, Normal, FVector2f(1.0f, 0.0f), FColor::White));
		MeshBuilder.AddVertex(FDynamicMeshVertex(FVector3f(WorldBottomEnd), Tangent, Normal, FVector2f(1.0f, 1.0f), FColor::White));
		MeshBuilder.AddTriangle(0, 1, 2);
		MeshBuilder.AddTriangle(0, 2, 3);
		MeshBuilder.Draw(PDI, FMatrix::Identity, FillMaterialProxy, SDPG_Foreground, true, false);

		const FLinearColor EdgeColor(0.0f, 1.0f, 0.12f, 0.9f);
		PDI->DrawLine(WorldBottomStart, WorldTopStart, EdgeColor, SDPG_Foreground, 1.5f);
		PDI->DrawLine(WorldTopStart, WorldTopEnd, EdgeColor, SDPG_Foreground, 1.5f);
		PDI->DrawLine(WorldTopEnd, WorldBottomEnd, EdgeColor, SDPG_Foreground, 1.5f);
		PDI->DrawLine(WorldBottomEnd, WorldBottomStart, EdgeColor, SDPG_Foreground, 1.5f);
	};

	if (bWallSurfacePlacementCoverBothSides)
	{
		DrawSurfaceQuad(true);
		DrawSurfaceQuad(false);
	}
	else
	{
		DrawSurfaceQuad(bHoveredWallSurfaceLeftSide);
	}
}

void FEasyHouseEditorMode::DrawPillarMeshPlacementPreview(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bPillarMeshPlacementActive)
	{
		return;
	}

	const AEHB_Pillar* Pillar = HoveredPillarMeshPillar.Get();
	if (!Pillar)
	{
		return;
	}

	const FBox Bounds = Pillar->GetComponentsBoundingBox(true).ExpandBy(4.0f);
	if (!Bounds.IsValid)
	{
		return;
	}

	const FVector Min = Bounds.Min;
	const FVector Max = Bounds.Max;
	const FVector Corners[8] = {
		FVector(Min.X, Min.Y, Min.Z),
		FVector(Max.X, Min.Y, Min.Z),
		FVector(Max.X, Max.Y, Min.Z),
		FVector(Min.X, Max.Y, Min.Z),
		FVector(Min.X, Min.Y, Max.Z),
		FVector(Max.X, Min.Y, Max.Z),
		FVector(Max.X, Max.Y, Max.Z),
		FVector(Min.X, Max.Y, Max.Z)
	};

	const FLinearColor PreviewColor(0.0f, 1.0f, 0.12f, 0.9f);
	constexpr float LineThickness = 3.0f;
	const int32 Edges[12][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	for (const auto& Edge : Edges)
	{
		PDI->DrawLine(Corners[Edge[0]], Corners[Edge[1]], PreviewColor, SDPG_Foreground, LineThickness);
	}
}

void FEasyHouseEditorMode::DrawRailingMeshPlacementPreview(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bRailingMeshPlacementActive)
	{
		return;
	}

	const AEHB_Railing* Railing = HoveredRailingMeshRailing.Get();
	const AEHB_Stair* Stair = HoveredRailingMeshStair.Get();
	if (!Railing && !Stair)
	{
		return;
	}

	constexpr float LineThickness = 3.0f;
	const FLinearColor PreviewColor(0.0f, 1.0f, 0.12f, 0.9f);
	const int32 Edges[12][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	auto DrawBox = [PDI, &Edges, &PreviewColor](const FBox& Bounds)
	{
		if (!Bounds.IsValid)
		{
			return;
		}

		constexpr float LocalLineThickness = 3.0f;
		const FVector Min = Bounds.Min;
		const FVector Max = Bounds.Max;
		const FVector Corners[8] = {
			FVector(Min.X, Min.Y, Min.Z),
			FVector(Max.X, Min.Y, Min.Z),
			FVector(Max.X, Max.Y, Min.Z),
			FVector(Min.X, Max.Y, Min.Z),
			FVector(Min.X, Min.Y, Max.Z),
			FVector(Max.X, Min.Y, Max.Z),
			FVector(Max.X, Max.Y, Max.Z),
			FVector(Min.X, Max.Y, Max.Z)
		};

		for (const auto& Edge : Edges)
		{
			PDI->DrawLine(Corners[Edge[0]], Corners[Edge[1]], PreviewColor, SDPG_Foreground, LocalLineThickness);
		}
	};

	if (bRailingMeshPlacementApplyToSinglePost && HoveredRailingMeshPostGuid.IsValid())
	{
		FEHBRailingPost Post;
		if (Railing)
		{
			if (!Railing->FindPostByGuid(HoveredRailingMeshPostGuid, Post))
			{
				return;
			}
		}
		else if (Stair)
		{
			if (!Stair->FindEmbeddedRailingPostByGuid(HoveredRailingMeshPostGuid, Post))
			{
				return;
			}
		}
		else
		{
			return;
		}

		const float PostWidth = Railing ? Railing->PostWidth : Stair->StairData.RailingPostWidth;
		const float PostHeight = Railing ? Railing->PostHeight : Stair->StairData.RailingPostHeight;
		const float HalfWidth = FMath::Max(1.0f, PostWidth * 0.5f) + 4.0f;
		const float Height = FMath::Max(1.0f, PostHeight) + 8.0f;
		const FVector LocalCorners[8] = {
			FVector(-HalfWidth, -HalfWidth, -4.0f),
			FVector(HalfWidth, -HalfWidth, -4.0f),
			FVector(HalfWidth, HalfWidth, -4.0f),
			FVector(-HalfWidth, HalfWidth, -4.0f),
			FVector(-HalfWidth, -HalfWidth, Height),
			FVector(HalfWidth, -HalfWidth, Height),
			FVector(HalfWidth, HalfWidth, Height),
			FVector(-HalfWidth, HalfWidth, Height)
		};

		const FTransform PostLocalTransform(Post.LocalRotation, Post.LocalBaseLocation);
		const FTransform PostToWorld = PostLocalTransform * (Railing ? Railing->GetActorTransform() : Stair->GetActorTransform());
		FVector Corners[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Corners[Index] = PostToWorld.TransformPosition(LocalCorners[Index]);
		}
		for (const auto& Edge : Edges)
		{
			PDI->DrawLine(Corners[Edge[0]], Corners[Edge[1]], PreviewColor, SDPG_Foreground, LineThickness);
		}
		return;
	}

	if (Railing)
	{
		DrawBox(Railing->GetComponentsBoundingBox(true).ExpandBy(4.0f));
		return;
	}

	FBox StairRailingBounds(ForceInit);
	auto AddComponentBounds = [&StairRailingBounds](const UPrimitiveComponent* Component)
	{
		if (Component && Component->IsVisible())
		{
			StairRailingBounds += Component->Bounds.GetBox();
		}
	};

	if (HoveredRailingMeshStairSide == EEHBRailingSide::Left)
	{
		AddComponentBounds(Stair->LeftRailingPostMeshComponent.Get());
		AddComponentBounds(Stair->LeftRailingRailMeshComponent.Get());
		for (const TObjectPtr<UStaticMeshComponent>& Component : Stair->LeftRailingPostOverrideMeshComponents)
		{
			AddComponentBounds(Component.Get());
		}
	}
	else
	{
		AddComponentBounds(Stair->RightRailingPostMeshComponent.Get());
		AddComponentBounds(Stair->RightRailingRailMeshComponent.Get());
		for (const TObjectPtr<UStaticMeshComponent>& Component : Stair->RightRailingPostOverrideMeshComponents)
		{
			AddComponentBounds(Component.Get());
		}
	}
	DrawBox(StairRailingBounds.ExpandBy(4.0f));
}

#undef LOCTEXT_NAMESPACE


bool FEasyHouseEditorMode::HasSelectedUnboundWallNode() const
{
 const auto* B=NodeHandleBuilding.Get();return B&&B==GetActiveBuilding()&&B->WallNodeAuthority.Version==2&&!bWallCreationToolActive&&!bRailingCreationToolActive
  &&GEditor&&GEditor->GetSelectedActors()->IsSelected(B)&&!B->FindPhysicalPillarForNode(SelectedWallNode).IsValid()
  &&B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==SelectedWallNode;});
}
bool FEasyHouseEditorMode::SelectUnboundWallNode(AEHBBuildingActorBase* B,FGuid Id)
{
 if(!GEditor||!IsValid(B)||B->WallNodeAuthority.Version!=2||B->FindPhysicalPillarForNode(Id).IsValid()||!B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==Id;}))return false;
 CancelWallCreation();CancelRailingCreation();SetActiveBuilding(B);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 NodeHandleBuilding=B;SelectedWallNode=Id;NodeHandleDrag={};GEditor->RedrawLevelEditingViewports();return true;
}

void FEasyHouseEditorMode::DrawUnboundWallNodeControls(FPrimitiveDrawInterface* PDI) const
{
	if(auto* B=GetActiveBuilding();PDI&&B&&B->WallNodeAuthority.Version==2&&GEditor&&GEditor->GetSelectedActors()->IsSelected(B)&&!bWallCreationToolActive&&!bRailingCreationToolActive)
	{
		for(const auto& N:B->WallNodeAuthority.Nodes)if(!B->FindPhysicalPillarForNode(N.NodeGuid).IsValid())
		{
			FVector Local=N.LocalTransform.GetLocation();if(NodeHandleDrag.bCaptured&&!NodeHandleDrag.bCancelled&&NodeHandleDrag.NodeGuid==N.NodeGuid)Local=NodeHandleDrag.Target;
			const FVector Top=B->GetActorTransform().TransformPosition(Local+FVector(0,0,N.JunctionDimensions.Z));
			PDI->SetHitProxy(new HEHBWallNodeProxy(B,N.NodeGuid));PDI->DrawPoint(Top,N.NodeGuid==SelectedWallNode?FLinearColor::Yellow:FLinearColor(0,1,1),14,SDPG_Foreground);PDI->SetHitProxy(nullptr);
		}
	}
}

void FEasyHouseEditorMode::DrawNodeEditPreview(FPrimitiveDrawInterface* PDI,AEHBBuildingActorBase* B,const FEHBWallNodeModelEditDraft& Draft) const
{
	if(!PDI||!B||!Draft.bSucceeded||!Draft.bWouldChange)return;
	FName Reason;
	FEHBWallNodeModel Source;EHBRoomFinishMove::FNodeEditPlan Finishes;
	if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Source).bSucceeded||!Finishes.Prepare(B,Source,Draft.Definitions,B->QueryElements(FEHBElementQuery()),{},Reason))return;
	const auto& Sides=Finishes.GetCandidateSides().GetWallSides();
	auto Line=[&](FVector A,FVector Z){PDI->DrawLine(B->GetActorTransform().TransformPosition(A),B->GetActorTransform().TransformPosition(Z),FLinearColor(0,1,1),SDPG_Foreground,1.5f);};
	for(const auto& Side:Sides)if(Draft.UpdatePlan.WallGuids.Contains(Side.WallGuid))
	{
		const auto* W=Draft.Definitions.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Side.WallGuid;});if(!W)continue;const FVector Height(0,0,W->Height);
		Line(Side.StartLeft-Height,Side.EndLeft-Height);Line(Side.StartRight-Height,Side.EndRight-Height);Line(Side.StartLeft,Side.EndLeft);Line(Side.StartRight,Side.EndRight);Line(Side.StartLeft-Height,Side.StartLeft);Line(Side.EndRight-Height,Side.EndRight);
	}

	for(const auto& F:Finishes.Floors)for(const auto& Region:F.Regions)for(int32 I=0;I<Region.OuterPolygon.Num();++I)Line(Region.OuterPolygon[I],Region.OuterPolygon[(I+1)%Region.OuterPolygon.Num()]);
	for(const auto& S:Finishes.Slabs)for(int32 I=0;I<S.Polygon.Num();++I)Line(S.Actor->GetElementLocalTransform().TransformPosition(S.Polygon[I]),S.Actor->GetElementLocalTransform().TransformPosition(S.Polygon[(I+1)%S.Polygon.Num()]));
}
