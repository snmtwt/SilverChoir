#include "GridStrategyMapSystem/Data/GSMBlueprintLibrary.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMNavigationMoveData.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

namespace
{
	AGSMMap3D* ResolveMap3DForBlueprintInteraction(
		const UObject* WorldContextObject,
		const FGuid& MapGuid,
		bool bUseDefaultMapData
	)
	{
		UGSMMapData* MapData = bUseDefaultMapData
			? UGSMBlueprintLibrary::GetDefaultMapData(WorldContextObject)
			: UGSMBlueprintLibrary::GetMapDataByGuid(WorldContextObject, MapGuid);
		if (!IsValid(MapData))
		{
			return nullptr;
		}

		AGSMMap3D* Map3D = MapData->GetMap3D();
		return IsValid(Map3D) ? Map3D : nullptr;
	}

	bool AssignNavigationMoveDataAs(
		UGSMNavigationMoveData* NavigationMoveData,
		TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
		UGSMNavigationMoveData*& OutNavigationMoveData
	)
	{
		OutNavigationMoveData = nullptr;

		UClass* ResolvedClass = NavigationMoveDataClass
			? NavigationMoveDataClass.Get()
			: UGSMNavigationMoveData::StaticClass();
		if (!IsValid(NavigationMoveData) || !ResolvedClass || !NavigationMoveData->IsA(ResolvedClass))
		{
			return false;
		}

		OutNavigationMoveData = NavigationMoveData;
		return true;
	}
}

FGuid UGSMBlueprintLibrary::CreateMapData(
	const UObject* WorldContextObject,
	UGSMMapDataAsset* MapDataAsset,
	bool bSetAsDefaultMap,
	UGSMMapData*& OutMapData)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	OutMapData = nullptr;
	return Subsystem ? Subsystem->CreateMapData(MapDataAsset, bSetAsDefaultMap, OutMapData) : FGuid();
}

UGSMMapData* UGSMBlueprintLibrary::GetDefaultMapData(const UObject* WorldContextObject)
{
	const UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetDefaultMapData() : nullptr;
}

UGSMMapData* UGSMBlueprintLibrary::GetMapDataByGuid(const UObject* WorldContextObject, const FGuid& MapGuid)
{
	const UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetMapDataByGuid(MapGuid) : nullptr;
}

namespace
{
bool AddMap3DZoomAtWorldLocationInternal(
	const UObject* WorldContextObject,
	const FVector& WorldPivotLocation,
	float ZoomDelta,
	float& OutMapScale,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapScale = 0.0f;
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutMapScale = Map3D->AddZoomAtWorldLocation(WorldPivotLocation, ZoomDelta);
	return true;
}

bool SetMap3DScaleAtWorldLocationInternal(
	const UObject* WorldContextObject,
	const FVector& WorldPivotLocation,
	float NewMapScale,
	float& OutMapScale,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapScale = 0.0f;
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutMapScale = Map3D->SetMapScaleAtWorldLocation(WorldPivotLocation, NewMapScale);
	return true;
}

bool GetMap3DScaleInternal(
	const UObject* WorldContextObject,
	float& OutMapScale,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapScale = 0.0f;
	const AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutMapScale = Map3D->GetCurrentMapScale();
	return true;
}

bool ResolveMap3DZoomPivotWorldLocationInternal(
	const UObject* WorldContextObject,
	const FVector& DesiredWorldPivotLocation,
	FVector& OutWorldPivotLocation,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutWorldPivotLocation = FVector::ZeroVector;
	const AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutWorldPivotLocation = Map3D->ResolveMapZoomPivotWorldLocation(DesiredWorldPivotLocation);
	return true;
}

bool BeginMap3DDragAtWorldLocationInternal(
	const UObject* WorldContextObject,
	const FVector& WorldDragStartLocation,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	Map3D->BeginDragMapAtWorldLocation(WorldDragStartLocation);
	return true;
}

bool DragMap3DToWorldLocationInternal(
	const UObject* WorldContextObject,
	const FVector& CurrentWorldDragLocation,
	FVector& OutMapContentCenter,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapContentCenter = FVector::ZeroVector;
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutMapContentCenter = Map3D->DragMapToWorldLocation(CurrentWorldDragLocation);
	return true;
}

bool PanMap3DByWorldDeltaInternal(
	const UObject* WorldContextObject,
	const FVector& WorldDelta,
	FVector& OutMapContentCenter,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapContentCenter = FVector::ZeroVector;
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	OutMapContentCenter = Map3D->PanMapByWorldDelta(WorldDelta);
	return true;
}

bool EndMap3DDragInternal(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	Map3D->EndDragMap();
	return true;
}

bool GetMap3DDragStateInternal(
	const UObject* WorldContextObject,
	bool& bOutIsDragging,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	bOutIsDragging = false;
	const AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	if (!Map3D)
	{
		return false;
	}

	bOutIsDragging = Map3D->IsDraggingMap();
	return true;
}

bool BeginMap3DDragWithMouseInternal(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	return Map3D && IsValid(PlayerController) && Map3D->BeginDragMapWithMouse(PlayerController);
}

bool GetMouseLocationOnMap3DDragPlaneInternal(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	FVector& OutWorldLocation,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutWorldLocation = FVector::ZeroVector;
	const AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	return Map3D && IsValid(PlayerController) && Map3D->GetMouseLocationOnDragPlane(PlayerController, OutWorldLocation);
}

bool DragMap3DToMousePositionInternal(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	FVector& OutMapContentCenter,
	const FGuid& MapGuid,
	bool bUseDefaultMapData)
{
	OutMapContentCenter = FVector::ZeroVector;
	AGSMMap3D* Map3D = ResolveMap3DForBlueprintInteraction(WorldContextObject, MapGuid, bUseDefaultMapData);
	return Map3D && IsValid(PlayerController) && Map3D->DragMapToMousePosition(PlayerController, OutMapContentCenter);
}
}

bool UGSMBlueprintLibrary::AddDefaultMap3DZoomAtWorldLocation(
	const UObject* WorldContextObject,
	const FVector& WorldPivotLocation,
	float ZoomDelta,
	float& OutMapScale)
{
	return AddMap3DZoomAtWorldLocationInternal(WorldContextObject, WorldPivotLocation, ZoomDelta, OutMapScale, FGuid(), true);
}

bool UGSMBlueprintLibrary::AddMap3DZoomAtWorldLocationByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& WorldPivotLocation,
	float ZoomDelta,
	float& OutMapScale)
{
	return AddMap3DZoomAtWorldLocationInternal(WorldContextObject, WorldPivotLocation, ZoomDelta, OutMapScale, MapGuid, false);
}

bool UGSMBlueprintLibrary::SetDefaultMap3DScaleAtWorldLocation(
	const UObject* WorldContextObject,
	const FVector& WorldPivotLocation,
	float NewMapScale,
	float& OutMapScale)
{
	return SetMap3DScaleAtWorldLocationInternal(WorldContextObject, WorldPivotLocation, NewMapScale, OutMapScale, FGuid(), true);
}

bool UGSMBlueprintLibrary::SetMap3DScaleAtWorldLocationByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& WorldPivotLocation,
	float NewMapScale,
	float& OutMapScale)
{
	return SetMap3DScaleAtWorldLocationInternal(WorldContextObject, WorldPivotLocation, NewMapScale, OutMapScale, MapGuid, false);
}

bool UGSMBlueprintLibrary::GetDefaultMap3DScale(const UObject* WorldContextObject, float& OutMapScale)
{
	return GetMap3DScaleInternal(WorldContextObject, OutMapScale, FGuid(), true);
}

bool UGSMBlueprintLibrary::GetMap3DScaleByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, float& OutMapScale)
{
	return GetMap3DScaleInternal(WorldContextObject, OutMapScale, MapGuid, false);
}

bool UGSMBlueprintLibrary::ResolveDefaultMap3DZoomPivotWorldLocation(
	const UObject* WorldContextObject,
	const FVector& DesiredWorldPivotLocation,
	FVector& OutWorldPivotLocation)
{
	return ResolveMap3DZoomPivotWorldLocationInternal(WorldContextObject, DesiredWorldPivotLocation, OutWorldPivotLocation, FGuid(), true);
}

bool UGSMBlueprintLibrary::ResolveMap3DZoomPivotWorldLocationByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& DesiredWorldPivotLocation,
	FVector& OutWorldPivotLocation)
{
	return ResolveMap3DZoomPivotWorldLocationInternal(WorldContextObject, DesiredWorldPivotLocation, OutWorldPivotLocation, MapGuid, false);
}

bool UGSMBlueprintLibrary::BeginDefaultMap3DDragAtWorldLocation(
	const UObject* WorldContextObject,
	const FVector& WorldDragStartLocation)
{
	return BeginMap3DDragAtWorldLocationInternal(WorldContextObject, WorldDragStartLocation, FGuid(), true);
}

bool UGSMBlueprintLibrary::BeginMap3DDragAtWorldLocationByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& WorldDragStartLocation)
{
	return BeginMap3DDragAtWorldLocationInternal(WorldContextObject, WorldDragStartLocation, MapGuid, false);
}

bool UGSMBlueprintLibrary::DragDefaultMap3DToWorldLocation(
	const UObject* WorldContextObject,
	const FVector& CurrentWorldDragLocation,
	FVector& OutMapContentCenter)
{
	return DragMap3DToWorldLocationInternal(WorldContextObject, CurrentWorldDragLocation, OutMapContentCenter, FGuid(), true);
}

bool UGSMBlueprintLibrary::DragMap3DToWorldLocationByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& CurrentWorldDragLocation,
	FVector& OutMapContentCenter)
{
	return DragMap3DToWorldLocationInternal(WorldContextObject, CurrentWorldDragLocation, OutMapContentCenter, MapGuid, false);
}

bool UGSMBlueprintLibrary::PanDefaultMap3DByWorldDelta(
	const UObject* WorldContextObject,
	const FVector& WorldDelta,
	FVector& OutMapContentCenter)
{
	return PanMap3DByWorldDeltaInternal(WorldContextObject, WorldDelta, OutMapContentCenter, FGuid(), true);
}

bool UGSMBlueprintLibrary::PanMap3DByWorldDeltaByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FVector& WorldDelta,
	FVector& OutMapContentCenter)
{
	return PanMap3DByWorldDeltaInternal(WorldContextObject, WorldDelta, OutMapContentCenter, MapGuid, false);
}

bool UGSMBlueprintLibrary::EndDefaultMap3DDrag(const UObject* WorldContextObject)
{
	return EndMap3DDragInternal(WorldContextObject, FGuid(), true);
}

bool UGSMBlueprintLibrary::EndMap3DDragByGuid(const UObject* WorldContextObject, const FGuid& MapGuid)
{
	return EndMap3DDragInternal(WorldContextObject, MapGuid, false);
}

bool UGSMBlueprintLibrary::GetDefaultMap3DDragState(const UObject* WorldContextObject, bool& bOutIsDragging)
{
	return GetMap3DDragStateInternal(WorldContextObject, bOutIsDragging, FGuid(), true);
}

bool UGSMBlueprintLibrary::GetMap3DDragStateByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	bool& bOutIsDragging)
{
	return GetMap3DDragStateInternal(WorldContextObject, bOutIsDragging, MapGuid, false);
}

bool UGSMBlueprintLibrary::BeginDefaultMap3DDragWithMouse(
	const UObject* WorldContextObject,
	APlayerController* PlayerController)
{
	return BeginMap3DDragWithMouseInternal(WorldContextObject, PlayerController, FGuid(), true);
}

bool UGSMBlueprintLibrary::BeginMap3DDragWithMouseByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	APlayerController* PlayerController)
{
	return BeginMap3DDragWithMouseInternal(WorldContextObject, PlayerController, MapGuid, false);
}

bool UGSMBlueprintLibrary::GetMouseLocationOnDefaultMap3DDragPlane(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	FVector& OutWorldLocation)
{
	return GetMouseLocationOnMap3DDragPlaneInternal(WorldContextObject, PlayerController, OutWorldLocation, FGuid(), true);
}

bool UGSMBlueprintLibrary::GetMouseLocationOnMap3DDragPlaneByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	APlayerController* PlayerController,
	FVector& OutWorldLocation)
{
	return GetMouseLocationOnMap3DDragPlaneInternal(WorldContextObject, PlayerController, OutWorldLocation, MapGuid, false);
}

bool UGSMBlueprintLibrary::DragDefaultMap3DToMousePosition(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	FVector& OutMapContentCenter)
{
	return DragMap3DToMousePositionInternal(WorldContextObject, PlayerController, OutMapContentCenter, FGuid(), true);
}

bool UGSMBlueprintLibrary::DragMap3DToMousePositionByGuid(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	APlayerController* PlayerController,
	FVector& OutMapContentCenter)
{
	return DragMap3DToMousePositionInternal(WorldContextObject, PlayerController, OutMapContentCenter, MapGuid, false);
}

bool UGSMBlueprintLibrary::RemoveMapData(const UObject* WorldContextObject, const FGuid& MapGuid)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem && Subsystem->RemoveMapData(MapGuid);
}

bool UGSMBlueprintLibrary::RemoveMapDataObject(const UObject* WorldContextObject, UGSMMapData* MapData)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem && Subsystem->RemoveMapDataObject(MapData);
}

bool UGSMBlueprintLibrary::ClearDefaultMapData(const UObject* WorldContextObject)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem && Subsystem->ClearDefaultMapData();
}

int32 UGSMBlueprintLibrary::ClearAllMapData(const UObject* WorldContextObject)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->ClearAllMapData() : 0;
}

UGSMTileData* UGSMBlueprintLibrary::GetTileData(const UObject* WorldContextObject, const FGuid& MapGuid, FName TileId)
{
	const UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetTileData(MapGuid, TileId) : nullptr;
}

FGuid UGSMBlueprintLibrary::AddPieceData(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	FName TargetTileId,
	FGuid RequestedPieceGuid,
	TSubclassOf<UGSMPieceData> PieceDataClass,
	TSubclassOf<AGSMPiece3D> Piece3DClass,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	OutPieceData = nullptr;
	return Subsystem
		? Subsystem->AddPieceData(MapGuid, TargetTileId, RequestedPieceGuid, PieceDataClass, Piece3DClass, Placement, OutPieceData)
		: FGuid();
}

bool UGSMBlueprintLibrary::MovePieceData(
	const UObject* WorldContextObject,
	const FGuid& MapGuid,
	const FGuid& PieceGuid,
	FName TargetTileId,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	UGSMMapSubsystem* Subsystem = GetGSMMapSubsystem(WorldContextObject);
	OutPieceData = nullptr;
	return Subsystem && Subsystem->MovePieceDataByGuid(MapGuid, PieceGuid, TargetTileId, Placement, OutPieceData);
}

bool UGSMBlueprintLibrary::CreateGSMNavigationMoveData(
	const UObject* WorldContextObject,
	TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
	UGSMNavigationMoveData*& OutNavigationMoveData
)
{
	OutNavigationMoveData = nullptr;

	UClass* ResolvedClass = NavigationMoveDataClass
		? NavigationMoveDataClass.Get()
		: UGSMNavigationMoveData::StaticClass();
	if (!ResolvedClass || ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}

	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UObject* Outer = GameInstance ? static_cast<UObject*>(GameInstance) : GetTransientPackage();

	OutNavigationMoveData = NewObject<UGSMNavigationMoveData>(Outer, ResolvedClass);
	return IsValid(OutNavigationMoveData);
}

UGSMMapSubsystem* UGSMBlueprintLibrary::GetGSMMapSubsystem(
	const UObject* WorldContextObject
)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UGSMMapSubsystem>() : nullptr;
}

AGSMTile3D* UGSMBlueprintLibrary::GetGridStrategyMapTileById(
	const UObject* WorldContextObject,
	FName MapId,
	FName TileId
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	return GSMMapSubsystem ? GSMMapSubsystem->GetTileByIdOnMap(MapId, TileId) : nullptr;
}

AGSMTile3D* UGSMBlueprintLibrary::GetActiveGridStrategyMapTileById(
	const UObject* WorldContextObject,
	FName TileId
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	return GSMMapSubsystem ? GSMMapSubsystem->GetTileByIdOnActiveMap(TileId) : nullptr;
}

bool UGSMBlueprintLibrary::MoveGridStrategyMapPieceToTile(
	const UObject* WorldContextObject,
	FName PieceId,
	FName MapId,
	FName SourceTileId,
	FName TargetTileId,
	const FGSMPieceMoveOptions& MoveOptions,
	AGSMPiece3D*& OutPiece
)
{
	OutPiece = nullptr;

	UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	if (!GSMMapSubsystem)
	{
		return false;
	}

	FVector2D ResolvedRelativeTileXY = MoveOptions.TargetRelativeTileXY;
	float ResolvedRelativeTileYaw = MoveOptions.TargetRelativeTileYaw;
	TSubclassOf<AGSMPiece3D> ResolvedPieceClass;
	FName ExistingPieceTileId = NAME_None;

	FGSMRuntimeData MapData;
	if (GSMMapSubsystem->GetMapRuntimeData(MapId, MapData))
	{
		for (const FGSMLegacyPieceRecord& PieceData : MapData.Pieces)
		{
			if (PieceData.PieceId == PieceId)
			{
				if (MoveOptions.bKeepExistingRelativeTileXY)
				{
					ResolvedRelativeTileXY = PieceData.RelativeTileXY;
				}
				if (MoveOptions.bKeepExistingRelativeTileYaw)
				{
					ResolvedRelativeTileYaw = PieceData.RelativeTileYaw;
				}
				ExistingPieceTileId = PieceData.TileId;
				ResolvedPieceClass = PieceData.PieceClass;
				break;
			}
		}
	}

	const FName ClassLookupSourceTileId = SourceTileId.IsNone() ? ExistingPieceTileId : SourceTileId;
	if (!ResolvedPieceClass && !ClassLookupSourceTileId.IsNone())
	{
		if (AGSMTile3D* SourceTile = GSMMapSubsystem->GetTileByIdOnMap(MapId, ClassLookupSourceTileId))
		{
			if (AGSMPiece3D* SourcePiece = SourceTile->GetMapPieceById(PieceId))
			{
				ResolvedPieceClass = SourcePiece->GetClass();
			}
		}
	}

	if (!ResolvedPieceClass && !TargetTileId.IsNone())
	{
		if (AGSMTile3D* TargetTile = GSMMapSubsystem->GetTileByIdOnMap(MapId, TargetTileId))
		{
			if (AGSMPiece3D* TargetPiece = TargetTile->GetMapPieceById(PieceId))
			{
				ResolvedPieceClass = TargetPiece->GetClass();
			}
		}
	}

	if (!ResolvedPieceClass)
	{
		ResolvedPieceClass = MoveOptions.PieceClass;
	}

	return GSMMapSubsystem->MoveMapPieceToTile(
		MapId,
		PieceId,
		SourceTileId,
		TargetTileId,
		ResolvedPieceClass,
		ResolvedRelativeTileXY,
		ResolvedRelativeTileYaw,
		MoveOptions.DefaultScale,
		OutPiece
	);
}

bool UGSMBlueprintLibrary::BeginMap3DNavigation(
	AGSMMap3D* Map3D,
	AGSMTile3D* StartTile,
	AGSMTile3D* GoalTile,
	TArray<UGSMTileData*>& OutPathTiles,
	EGSMNavigationMode NavigationMode)
{
	OutPathTiles.Reset();
	return IsValid(Map3D)
		&& Map3D->BeginFixedTilePathNavigation(StartTile, GoalTile, NavigationMode, OutPathTiles);
}

bool UGSMBlueprintLibrary::IsMap3DNavigationActive(const AGSMMap3D* Map3D)
{
	return IsValid(Map3D) && Map3D->IsTilePathNavigationActive();
}

bool UGSMBlueprintLibrary::EndMap3DNavigation(AGSMMap3D* Map3D, bool bClearPathArrow)
{
	if (!IsValid(Map3D) || !Map3D->IsTilePathNavigationActive())
	{
		return false;
	}

	Map3D->EndTilePathNavigationAndGetMoveData(bClearPathArrow);
	return true;
}

bool UGSMBlueprintLibrary::IsGridStrategyMapTilePathNavigationActive(
	const UObject* WorldContextObject,
	FName MapId
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	const AGSMMap3D* GridMap = GSMMapSubsystem
		? GSMMapSubsystem->GetGridStrategyMapById(MapId)
		: nullptr;
	return IsValid(GridMap) && GridMap->IsTilePathNavigationActive();
}

bool UGSMBlueprintLibrary::IsActiveGridStrategyMapTilePathNavigationActive(
	const UObject* WorldContextObject
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	const AGSMMap3D* ActiveMap = GSMMapSubsystem
		? GSMMapSubsystem->GetActiveGridStrategyMap()
		: nullptr;
	return IsValid(ActiveMap) && ActiveMap->IsTilePathNavigationActive();
}

UGSMNavigationMoveData* UGSMBlueprintLibrary::EndGridStrategyMapTilePathNavigationAndGetMoveData(
	const UObject* WorldContextObject,
	FName MapId,
	bool bClearPathArrow
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	AGSMMap3D* GridMap = GSMMapSubsystem
		? GSMMapSubsystem->GetGridStrategyMapById(MapId)
		: nullptr;
	return IsValid(GridMap)
		? GridMap->EndTilePathNavigationAndGetMoveData(bClearPathArrow)
		: nullptr;
}

bool UGSMBlueprintLibrary::EndGridStrategyMapTilePathNavigationAndGetMoveDataAs(
	const UObject* WorldContextObject,
	FName MapId,
	TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
	UGSMNavigationMoveData*& OutNavigationMoveData,
	bool bClearPathArrow
)
{
	UGSMNavigationMoveData* NavigationMoveData = EndGridStrategyMapTilePathNavigationAndGetMoveData(
		WorldContextObject,
		MapId,
		bClearPathArrow
	);
	return AssignNavigationMoveDataAs(NavigationMoveData, NavigationMoveDataClass, OutNavigationMoveData);
}

UGSMNavigationMoveData* UGSMBlueprintLibrary::EndActiveGridStrategyMapTilePathNavigationAndGetMoveData(
	const UObject* WorldContextObject,
	bool bClearPathArrow
)
{
	const UGSMMapSubsystem* GSMMapSubsystem = GetGSMMapSubsystem(WorldContextObject);
	AGSMMap3D* ActiveMap = GSMMapSubsystem
		? GSMMapSubsystem->GetActiveGridStrategyMap()
		: nullptr;
	return IsValid(ActiveMap)
		? ActiveMap->EndTilePathNavigationAndGetMoveData(bClearPathArrow)
		: nullptr;
}

bool UGSMBlueprintLibrary::EndActiveGridStrategyMapTilePathNavigationAndGetMoveDataAs(
	const UObject* WorldContextObject,
	TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
	UGSMNavigationMoveData*& OutNavigationMoveData,
	bool bClearPathArrow
)
{
	UGSMNavigationMoveData* NavigationMoveData = EndActiveGridStrategyMapTilePathNavigationAndGetMoveData(
		WorldContextObject,
		bClearPathArrow
	);
	return AssignNavigationMoveDataAs(NavigationMoveData, NavigationMoveDataClass, OutNavigationMoveData);
}
