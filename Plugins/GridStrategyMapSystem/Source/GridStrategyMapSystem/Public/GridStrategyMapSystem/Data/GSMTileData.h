#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "UObject/Object.h"
#include "GSMTileData.generated.h"

class AGSMTile3D;
class UGSMMapData;
class UGSMPieceData;
class UGSMTileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGSMTilePieceAdded,
	UGSMPieceData*, PieceData,
	const FGSMPiecePlacement&, Placement
);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGSMTilePieceChanged, UGSMPieceData*, PieceData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGSMTileDataUpdated, UGSMTileData*, TileData);

/** 单个瓦片的权威运行时数据。该对象可在蓝图中继承并增加游戏字段。 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "GSM瓦片数据"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMTileData : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	bool IsTileDataValid() const { return MapGuid.IsValid() && TileEntry.TileId != NAME_None && MapData.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	FGuid GetMapGuid() const { return MapGuid; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	FName GetTileId() const { return TileEntry.TileId; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	FGSMTileEntry GetTileEntry() const { return TileEntry; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	FIntPoint GetGridCoordinate() const { return TileEntry.GridCoordinate; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	FTransform GetLocalTransform() const { return TileEntry.LocalTransform; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片|导航")
	FGSMTileNavigationSettings GetNavigationSettings() const { return TileEntry.Navigation; }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片")
	UGSMMapData* GetMapData() const { return MapData.Get(); }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片|展示")
	AGSMTile3D* GetTile3D() const { return Tile3D.Get(); }

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片|棋子")
	UGSMPieceData* GetPieceByGuid(const FGuid& PieceGuid) const;

	UFUNCTION(BlueprintPure, Category = "GSM|瓦片|棋子")
	TArray<UGSMPieceData*> GetPieces() const;

	UPROPERTY(BlueprintAssignable, Category = "GSM|瓦片|棋子")
	FGSMTilePieceAdded OnPieceAdded;

	UPROPERTY(BlueprintAssignable, Category = "GSM|瓦片|棋子")
	FGSMTilePieceChanged OnPieceUpdated;

	UPROPERTY(BlueprintAssignable, Category = "GSM|瓦片|棋子")
	FGSMTilePieceChanged OnPieceRemoved;

	/** Unified notification for any data change that affects this tile. */
	UPROPERTY(BlueprintAssignable, Category = "GSM|瓦片")
	FGSMTileDataUpdated OnTileDataUpdated;

	/** Call after a derived tile-data class changes fields not represented by piece data. */
	UFUNCTION(BlueprintCallable, Category = "GSM|瓦片", meta = (BlueprintProtected, DisplayName = "通知瓦片数据已更新"))
	void NotifyTileDataUpdated();

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|瓦片|棋子", meta = (DisplayName = "当棋子添加到瓦片"))
	void ReceivePieceAdded(UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|瓦片|棋子", meta = (DisplayName = "当瓦片棋子更新"))
	void ReceivePieceUpdated(UGSMPieceData* PieceData);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|瓦片|棋子", meta = (DisplayName = "当棋子从瓦片移除"))
	void ReceivePieceRemoved(UGSMPieceData* PieceData);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|瓦片", meta = (DisplayName = "当瓦片数据更新"))
	void ReceiveTileDataUpdated(UGSMTileData* TileData);

private:
	friend class UGSMMapData;
	friend class AGSMMap3D;
	friend class AGSMTile3D;

	void InitializeTile(UGSMMapData* InMapData, const FGuid& InMapGuid, const FGSMTileEntry& InTileEntry);
	bool AttachPiece(UGSMPieceData* PieceData);
	bool DetachPiece(UGSMPieceData* PieceData);
	void RegisterTile3D(AGSMTile3D* InTile3D);
	void UnregisterTile3D(AGSMTile3D* InTile3D);
	void InvalidateTileData();

	UFUNCTION()
	void HandlePieceDataChanged(UGSMPieceData* PieceData);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|瓦片", meta = (AllowPrivateAccess = "true"))
	FGuid MapGuid;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|瓦片", meta = (AllowPrivateAccess = "true"))
	FGSMTileEntry TileEntry;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGSMMapData> MapData;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UGSMPieceData>> PiecesByGuid;

	UPROPERTY(Transient)
	TWeakObjectPtr<AGSMTile3D> Tile3D;

	/** 预留给后续 2D 瓦片控件，当前只保留弱引用槽位和数据事件。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> Tile2D;
};
