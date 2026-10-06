#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"

#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

DEFINE_LOG_CATEGORY_STATIC(LogGSMSubsystem, Log, All);

namespace
{
	bool ParseGuidName(FName Name, FGuid& OutGuid)
	{
		OutGuid.Invalidate();
		return !Name.IsNone() && FGuid::Parse(Name.ToString(), OutGuid);
	}

	UGSMMapData* ResolveMapDataFromName(const UGSMMapSubsystem* Subsystem, FName MapId)
	{
		if (!Subsystem)
		{
			return nullptr;
		}
		FGuid MapGuid;
		if (ParseGuidName(MapId, MapGuid))
		{
			if (UGSMMapData* MapData = Subsystem->GetMapDataByGuid(MapGuid))
			{
				return MapData;
			}
		}
		AGSMMap3D* Map3D = Subsystem->GetGridStrategyMapById(MapId);
		return Map3D ? Map3D->GetMapData() : nullptr;
	}
}

void UGSMMapSubsystem::Deinitialize()
{
	ClearAllMapData();
	OnActiveMapChanged.Clear();
	OnMapTileClicked.Clear();
	Super::Deinitialize();
}

FGuid UGSMMapSubsystem::CreateMapData(
	UGSMMapDataAsset* MapDataAsset,
	bool bSetAsDefaultMap,
	UGSMMapData*& OutMapData)
{
	return CreateMapDataInternal(MapDataAsset, bSetAsDefaultMap, true, OutMapData);
}

FGuid UGSMMapSubsystem::CreateIndependentMapData(UGSMMapDataAsset* MapDataAsset, UGSMMapData*& OutMapData)
{
	return CreateMapDataInternal(MapDataAsset, false, false, OutMapData);
}

FGuid UGSMMapSubsystem::CreateMapDataInternal(
	UGSMMapDataAsset* MapDataAsset,
	bool bSetAsDefaultMap,
	bool bPromoteWhenDefaultMissing,
	UGSMMapData*& OutMapData)
{
	OutMapData = nullptr;
	if (!IsValid(MapDataAsset))
	{
		UE_LOG(LogGSMSubsystem, Warning, TEXT("CreateMapData failed: MapDataAsset is invalid."));
		return FGuid();
	}

	const FGuid MapGuid = FGuid::NewGuid();
	UClass* MapDataClass = MapDataAsset->MapDataClass
		? MapDataAsset->MapDataClass.Get()
		: UGSMMapData::StaticClass();
	UGSMMapData* NewMapData = NewObject<UGSMMapData>(this, MapDataClass);
	if (!NewMapData || !NewMapData->InitializeMap(MapGuid, MapDataAsset))
	{
		UE_LOG(LogGSMSubsystem, Error, TEXT("CreateMapData failed while initializing map %s."), *MapGuid.ToString());
		return FGuid();
	}

	const bool bUseDefaultSlot = bSetAsDefaultMap || (bPromoteWhenDefaultMissing && !IsValid(DefaultMapData));
	if (bUseDefaultSlot)
	{
		if (bSetAsDefaultMap && IsValid(DefaultMapData))
		{
			UE_LOG(LogGSMSubsystem, Warning,
				TEXT("Replacing default map data %s with %s. The previous default map will be deeply cleared."),
				*DefaultMapData->GetMapGuid().ToString(), *MapGuid.ToString());
			ClearDefaultMapData();
		}
		else if (!bSetAsDefaultMap)
		{
			UE_LOG(LogGSMSubsystem, Log,
				TEXT("No default map existed; map %s was promoted to default as a fallback."), *MapGuid.ToString());
		}

		DefaultMapData = NewMapData;
	}
	else
	{
		MapDataByGuid.Add(MapGuid, NewMapData);
	}

	OutMapData = NewMapData;
	return MapGuid;
}

UGSMMapData* UGSMMapSubsystem::GetMapDataByGuid(const FGuid& MapGuid) const
{
	if (!MapGuid.IsValid())
	{
		return nullptr;
	}
	if (IsValid(DefaultMapData) && DefaultMapData->GetMapGuid() == MapGuid)
	{
		return DefaultMapData;
	}
	const TObjectPtr<UGSMMapData>* Found = MapDataByGuid.Find(MapGuid);
	return Found ? Found->Get() : nullptr;
}

bool UGSMMapSubsystem::RemoveMapData(const FGuid& MapGuid)
{
	if (!MapGuid.IsValid())
	{
		return false;
	}

	UGSMMapData* MapData = nullptr;
	if (IsValid(DefaultMapData) && DefaultMapData->GetMapGuid() == MapGuid)
	{
		MapData = DefaultMapData;
		DefaultMapData = nullptr;
	}
	else if (TObjectPtr<UGSMMapData>* Found = MapDataByGuid.Find(MapGuid))
	{
		MapData = Found->Get();
		MapDataByGuid.Remove(MapGuid);
	}

	if (!IsValid(MapData))
	{
		return false;
	}

	MapData->ClearAllData();
	UE_LOG(LogGSMSubsystem, Log, TEXT("Deeply cleared map data %s."), *MapGuid.ToString());
	return true;
}

bool UGSMMapSubsystem::RemoveMapDataObject(UGSMMapData* MapData)
{
	if (!IsValid(MapData))
	{
		return false;
	}
	if (DefaultMapData == MapData)
	{
		return ClearDefaultMapData();
	}

	for (const TPair<FGuid, TObjectPtr<UGSMMapData>>& Pair : MapDataByGuid)
	{
		if (Pair.Value == MapData)
		{
			return RemoveMapData(Pair.Key);
		}
	}
	return false;
}

bool UGSMMapSubsystem::ClearDefaultMapData()
{
	if (!IsValid(DefaultMapData))
	{
		DefaultMapData = nullptr;
		return false;
	}
	return RemoveMapData(DefaultMapData->GetMapGuid());
}

int32 UGSMMapSubsystem::ClearAllMapData()
{
	TSet<UGSMMapData*> ExistingMaps;
	if (IsValid(DefaultMapData))
	{
		ExistingMaps.Add(DefaultMapData);
	}
	for (const TPair<FGuid, TObjectPtr<UGSMMapData>>& Pair : MapDataByGuid)
	{
		if (IsValid(Pair.Value))
		{
			ExistingMaps.Add(Pair.Value);
		}
	}

	DefaultMapData = nullptr;
	MapDataByGuid.Reset();
	const bool bHadActiveMap = ActiveMap.IsValid();
	ActiveMap.Reset();
	RegisteredMaps.Reset();
	RegisteredMapsById.Reset();
	if (bHadActiveMap)
	{
		OnActiveMapChanged.Broadcast(nullptr);
	}

	for (UGSMMapData* MapData : ExistingMaps)
	{
		MapData->ClearAllData();
	}

	if (!ExistingMaps.IsEmpty())
	{
		UE_LOG(LogGSMSubsystem, Log, TEXT("Deeply cleared %d map data object(s)."), ExistingMaps.Num());
	}
	return ExistingMaps.Num();
}

UGSMTileData* UGSMMapSubsystem::GetTileData(const FGuid& MapGuid, FName TileId) const
{
	UGSMMapData* MapData = GetMapDataByGuid(MapGuid);
	return MapData ? MapData->GetTileById(TileId) : nullptr;
}

FGuid UGSMMapSubsystem::AddPieceData(
	const FGuid& MapGuid,
	FName TargetTileId,
	FGuid RequestedPieceGuid,
	TSubclassOf<UGSMPieceData> PieceDataClass,
	TSubclassOf<AGSMPiece3D> Piece3DClass,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	OutPieceData = nullptr;
	UGSMMapData* MapData = GetMapDataByGuid(MapGuid);
	return MapData
		? MapData->AddPieceToTileById(TargetTileId, RequestedPieceGuid, PieceDataClass, Piece3DClass, Placement, OutPieceData)
		: FGuid();
}

bool UGSMMapSubsystem::RemovePieceData(const FGuid& MapGuid, const FGuid& PieceGuid)
{
	UGSMMapData* MapData = GetMapDataByGuid(MapGuid);
	return MapData && MapData->RemovePieceByGuid(PieceGuid);
}

bool UGSMMapSubsystem::MovePieceDataByGuid(
	const FGuid& MapGuid,
	const FGuid& PieceGuid,
	FName TargetTileId,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	OutPieceData = nullptr;
	UGSMMapData* MapData = GetMapDataByGuid(MapGuid);
	return MapData && MapData->MovePieceByGuidToTileId(PieceGuid, TargetTileId, Placement, OutPieceData);
}

bool UGSMMapSubsystem::FindPathByMapGuid(
	const FGuid& MapGuid,
	FName StartTileId,
	FName GoalTileId,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath) const
{
	UGSMMapData* MapData = GetMapDataByGuid(MapGuid);
	if (MapData)
	{
		return MapData->FindPathByTileIds(StartTileId, GoalTileId, NavigationMode, OutPath);
	}
	OutPath.Reset();
	OutPath.FailureReason = FText::FromString(TEXT("Map data was not found."));
	return false;
}

bool UGSMMapSubsystem::FindPathOnDefaultMap(
	FName StartTileId,
	FName GoalTileId,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath) const
{
	if (DefaultMapData)
	{
		return DefaultMapData->FindPathByTileIds(StartTileId, GoalTileId, NavigationMode, OutPath);
	}
	OutPath.Reset();
	OutPath.FailureReason = FText::FromString(TEXT("Default map data is invalid."));
	return false;
}

bool UGSMMapSubsystem::RegisterMap3D(
	AGSMMap3D* Map3D,
	bool bUseDefaultMapData,
	const FGuid& MapGuid,
	UGSMMapData*& OutMapData)
{
	OutMapData = bUseDefaultMapData ? DefaultMapData.Get() : GetMapDataByGuid(MapGuid);
	if (!IsValid(OutMapData))
	{
		UE_LOG(LogGSMSubsystem, Warning, TEXT("RegisterMap3D failed for %s: map data was not found."), *GetNameSafe(Map3D));
		return false;
	}
	return OutMapData->RegisterMap3D(Map3D);
}

void UGSMMapSubsystem::UnregisterMap3D(AGSMMap3D* Map3D, UGSMMapData* MapData)
{
	if (IsValid(MapData))
	{
		MapData->UnregisterMap3D(Map3D);
	}
}

void UGSMMapSubsystem::RegisterGridStrategyMap(AGSMMap3D* GridMap, bool bMakeActive)
{
	if (!IsValid(GridMap))
	{
		return;
	}

	RegisteredMaps.RemoveAll([](const TWeakObjectPtr<AGSMMap3D>& RegisteredMap)
	{
		return !RegisteredMap.IsValid();
	});

	RegisteredMaps.AddUnique(GridMap);
	RegisteredMapsById.Add(GridMap->GetMapId(), GridMap);

	if (bMakeActive || !ActiveMap.IsValid())
	{
		SetActiveGridStrategyMap(GridMap);
	}
}

void UGSMMapSubsystem::UnregisterGridStrategyMap(AGSMMap3D* GridMap)
{
	RegisteredMaps.Remove(GridMap);
	if (IsValid(GridMap))
	{
		const FName MapId = GridMap->GetMapId();
		if (const TWeakObjectPtr<AGSMMap3D>* RegisteredMap = RegisteredMapsById.Find(MapId))
		{
			if (RegisteredMap->Get() == GridMap)
			{
				RegisteredMapsById.Remove(MapId);
			}
		}
	}

	if (ActiveMap.Get() != GridMap)
	{
		return;
	}

	ActiveMap.Reset();

	for (int32 Index = RegisteredMaps.Num() - 1; Index >= 0; --Index)
	{
		if (RegisteredMaps[Index].IsValid())
		{
			SetActiveGridStrategyMap(RegisteredMaps[Index].Get());
			return;
		}
	}

	OnActiveMapChanged.Broadcast(nullptr);
}

void UGSMMapSubsystem::SetActiveGridStrategyMap(AGSMMap3D* GridMap)
{
	if (ActiveMap.Get() == GridMap)
	{
		return;
	}

	ActiveMap = GridMap;
	OnActiveMapChanged.Broadcast(GridMap);
}

AGSMTile3D* UGSMMapSubsystem::GetTileByIdOnActiveMap(FName TileId) const
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	return IsValid(GridMap) ? GridMap->GetTileById(TileId) : nullptr;
}

AGSMMap3D* UGSMMapSubsystem::GetGridStrategyMapById(FName MapId) const
{
	if (MapId.IsNone())
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<AGSMMap3D>* GridMap = RegisteredMapsById.Find(MapId))
	{
		return GridMap->Get();
	}

	for (const TWeakObjectPtr<AGSMMap3D>& RegisteredMap : RegisteredMaps)
	{
		AGSMMap3D* GridMap = RegisteredMap.Get();
		if (IsValid(GridMap) && GridMap->GetMapId() == MapId)
		{
			return GridMap;
		}
	}

	return nullptr;
}

bool UGSMMapSubsystem::SetActiveGridStrategyMapById(FName MapId)
{
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	if (!IsValid(GridMap))
	{
		return false;
	}

	SetActiveGridStrategyMap(GridMap);
	return true;
}

AGSMTile3D* UGSMMapSubsystem::GetTileByIdOnMap(FName MapId, FName TileId) const
{
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	return IsValid(GridMap) ? GridMap->GetTileById(TileId) : nullptr;
}

void UGSMMapSubsystem::SaveMapTileData(FName MapId, const TArray<FGSMTileEntry>& TileEntries)
{
	// Runtime tile data is created once from UGSMMapDataAsset. This compatibility entry point no longer stores a second copy.
}

bool UGSMMapSubsystem::GetMapRuntimeData(FName MapId, FGSMRuntimeData& OutMapData) const
{
	OutMapData = FGSMRuntimeData();
	UGSMMapData* MapData = ResolveMapDataFromName(this, MapId);
	if (!MapData)
	{
		return false;
	}

	OutMapData.MapId = FName(*MapData->GetMapGuid().ToString(EGuidFormats::DigitsWithHyphens));
	for (UGSMTileData* TileData : MapData->GetTiles())
	{
		if (TileData)
		{
			OutMapData.TileEntries.Add(TileData->GetTileEntry());
		}
	}
	for (UGSMPieceData* PieceData : MapData->GetPieces())
	{
		if (!PieceData)
		{
			continue;
		}
		const FGSMPiecePlacement Placement = PieceData->GetPlacement();
		FGSMLegacyPieceRecord& Record = OutMapData.Pieces.AddDefaulted_GetRef();
		Record.PieceId = FName(*PieceData->GetPieceGuid().ToString(EGuidFormats::DigitsWithHyphens));
		Record.TileId = PieceData->GetTileId();
		Record.PieceClass = PieceData->GetPiece3DClass();
		Record.RelativeTileXY = Placement.RelativeTileXY;
		Record.RelativeTileYaw = Placement.RelativeTileYaw;
		Record.DefaultScale = Placement.DefaultScale;
	}
	return true;
}

bool UGSMMapSubsystem::AddOrUpdateMapPieceOnTile(
	FName MapId,
	FName TileId,
	FName PieceId,
	TSubclassOf<AGSMPiece3D> PieceClass,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale,
	AGSMPiece3D*& OutPiece)
{
	OutPiece = nullptr;
	UGSMMapData* MapData = ResolveMapDataFromName(this, MapId);
	FGuid PieceGuid;
	if (!MapData || TileId.IsNone() || !ParseGuidName(PieceId, PieceGuid))
	{
		return false;
	}

	FGSMPiecePlacement Placement;
	Placement.RelativeTileXY = RelativeTileXY;
	Placement.RelativeTileYaw = RelativeTileYaw;
	Placement.DefaultScale = FMath::Max(DefaultScale, KINDA_SMALL_NUMBER);

	UGSMPieceData* PieceData = MapData->GetPieceByGuid(PieceGuid);
	bool bSucceeded = false;
	if (PieceData)
	{
		if (PieceClass)
		{
			PieceData->SetPiece3DClass(PieceClass);
		}
		bSucceeded = MapData->MovePieceDataToTileId(PieceData, TileId, Placement);
	}
	else
	{
		MapData->AddPieceToTileById(TileId, PieceGuid, nullptr, PieceClass, Placement, PieceData);
		bSucceeded = PieceData != nullptr;
	}
	OutPiece = PieceData ? PieceData->GetPiece3D() : nullptr;
	return bSucceeded;
}

bool UGSMMapSubsystem::RemoveMapPieceFromTile(FName MapId, FName TileId, FName PieceId)
{
	UGSMMapData* MapData = ResolveMapDataFromName(this, MapId);
	FGuid PieceGuid;
	if (!MapData || !ParseGuidName(PieceId, PieceGuid))
	{
		return false;
	}
	UGSMPieceData* PieceData = MapData->GetPieceByGuid(PieceGuid);
	return PieceData && (TileId.IsNone() || PieceData->GetTileId() == TileId) && MapData->RemovePieceByGuid(PieceGuid);
}

bool UGSMMapSubsystem::MoveMapPieceToTile(
	FName MapId,
	FName PieceId,
	FName SourceTileId,
	FName TargetTileId,
	TSubclassOf<AGSMPiece3D> PieceClass,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale,
	AGSMPiece3D*& OutPiece)
{
	OutPiece = nullptr;
	UGSMMapData* MapData = ResolveMapDataFromName(this, MapId);
	FGuid PieceGuid;
	if (!MapData || TargetTileId.IsNone() || !ParseGuidName(PieceId, PieceGuid))
	{
		return false;
	}

	UGSMPieceData* PieceData = MapData->GetPieceByGuid(PieceGuid);
	if (!PieceData)
	{
		return false;
	}
	if (PieceClass)
	{
		PieceData->SetPiece3DClass(PieceClass);
	}
	FGSMPiecePlacement Placement = PieceData->GetPlacement();
	Placement.RelativeTileXY = RelativeTileXY;
	Placement.RelativeTileYaw = RelativeTileYaw;
	if (DefaultScale > 0.0f)
	{
		Placement.DefaultScale = DefaultScale;
	}
	const bool bMoved = MapData->MovePieceDataToTileId(PieceData, TargetTileId, Placement);
	OutPiece = PieceData->GetPiece3D();
	return bMoved;
}

bool UGSMMapSubsystem::LoadConfigOnActiveMap(UGSMMapDataAsset* MapConfig)
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		return false;
	}

	GridMap->SetMapConfig(MapConfig, true);
	return true;
}

float UGSMMapSubsystem::AddZoomOnActiveMapAtWorldLocation(const FVector& WorldPivotLocation, float ZoomDelta)
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		return 1.0f;
	}

	return GridMap->AddZoomAtWorldLocation(WorldPivotLocation, ZoomDelta);
}

float UGSMMapSubsystem::SetActiveMapScaleAtWorldLocation(const FVector& WorldPivotLocation, float NewMapScale)
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		return 1.0f;
	}

	return GridMap->SetMapScaleAtWorldLocation(WorldPivotLocation, NewMapScale);
}

void UGSMMapSubsystem::SetActiveMapBounds(
	const FGSMQuadBounds& MapBounds,
	bool bEnableBounds,
	bool bRefreshTiles
)
{
	if (AGSMMap3D* GridMap = ActiveMap.Get())
	{
		GridMap->SetMapBounds(MapBounds, bEnableBounds, bRefreshTiles);
	}
}

bool UGSMMapSubsystem::GetMouseHitOnMapGroove(
	FName MapId,
	APlayerController* PlayerController,
	FVector& OutHitLocation
) const
{
	OutHitLocation = FVector::ZeroVector;
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	return IsValid(GridMap) && GridMap->GetMouseHitOnMapGroove(PlayerController, OutHitLocation);
}

bool UGSMMapSubsystem::GetMouseHitOnActiveMapGroove(APlayerController* PlayerController, FVector& OutHitLocation) const
{
	OutHitLocation = FVector::ZeroVector;

	AGSMMap3D* GridMap = ActiveMap.Get();
	return IsValid(GridMap) && GridMap->GetMouseHitOnMapGroove(PlayerController, OutHitLocation);
}

bool UGSMMapSubsystem::BeginDragMapWithMouse(
	FName MapId,
	APlayerController* PlayerController
) const
{
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	return IsValid(GridMap) && GridMap->BeginDragMapWithMouse(PlayerController);
}

bool UGSMMapSubsystem::DragMapToMousePosition(
	FName MapId,
	APlayerController* PlayerController,
	FVector& OutMapContentCenter
) const
{
	OutMapContentCenter = FVector::ZeroVector;
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	return IsValid(GridMap) && GridMap->DragMapToMousePosition(PlayerController, OutMapContentCenter);
}

void UGSMMapSubsystem::EndDragMap(FName MapId) const
{
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	if (IsValid(GridMap))
	{
		GridMap->EndDragMap();
	}
}

bool UGSMMapSubsystem::BeginDragActiveMapWithMouse(APlayerController* PlayerController) const
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	return IsValid(GridMap) && GridMap->BeginDragMapWithMouse(PlayerController);
}

bool UGSMMapSubsystem::DragActiveMapToMousePosition(
	APlayerController* PlayerController,
	FVector& OutMapContentCenter
) const
{
	OutMapContentCenter = FVector::ZeroVector;

	AGSMMap3D* GridMap = ActiveMap.Get();
	return IsValid(GridMap) && GridMap->DragMapToMousePosition(PlayerController, OutMapContentCenter);
}

void UGSMMapSubsystem::EndDragActiveMap() const
{
	if (AGSMMap3D* GridMap = ActiveMap.Get())
	{
		GridMap->EndDragMap();
	}
}

bool UGSMMapSubsystem::RequestTileClick(AGSMTile3D* TileActor, const FVector& WorldHitLocation)
{
	if (!IsValid(TileActor))
	{
		return false;
	}

	return TileActor->RequestMapTileClick(WorldHitLocation);
}

AGSMTile3D* UGSMMapSubsystem::GetNearestTileOnActiveMap(
	const FVector& WorldLocation,
	bool bRequireWalkable,
	bool bRespectMapBounds
) const
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		return nullptr;
	}

	return GridMap->GetNearestTileToWorldLocation(WorldLocation, bRequireWalkable, bRespectMapBounds);
}

bool UGSMMapSubsystem::FindPathOnActiveMapByTileIds(
	FName StartTileId,
	FName GoalTileId,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath
) const
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		OutPath.Reset();
		OutPath.FailureReason = FText::FromString(TEXT("No active grid strategy map."));
		return false;
	}

	return GridMap->FindPathByTileIds(StartTileId, GoalTileId, NavigationMode, OutPath);
}

bool UGSMMapSubsystem::FindPathOnActiveMapByWorldLocations(
	const FVector& StartWorldLocation,
	const FVector& GoalWorldLocation,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath
) const
{
	AGSMMap3D* GridMap = ActiveMap.Get();
	if (!IsValid(GridMap))
	{
		OutPath.Reset();
		OutPath.FailureReason = FText::FromString(TEXT("No active grid strategy map."));
		return false;
	}

	return GridMap->FindPathByWorldLocations(StartWorldLocation, GoalWorldLocation, NavigationMode, OutPath);
}

void UGSMMapSubsystem::BroadcastTileClicked(AGSMTile3D* TileActor, const FVector& WorldHitLocation)
{
	OnMapTileClicked.Broadcast(TileActor, WorldHitLocation);
}

void UGSMMapSubsystem::RefreshMapPiecesForMap(FName MapId)
{
	AGSMMap3D* GridMap = GetGridStrategyMapById(MapId);
	UGSMMapData* MapData = ResolveMapDataFromName(this, MapId);
	if (!IsValid(GridMap) || !MapData)
	{
		return;
	}

	for (UGSMTileData* TileData : MapData->GetTiles())
	{
		AGSMTile3D* Tile3D = TileData ? GridMap->GetTileById(TileData->GetTileId()) : nullptr;
		if (Tile3D)
		{
			Tile3D->BindTileData(TileData);
		}
	}
}
