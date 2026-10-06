#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMMapWidget2D.generated.h"

class UGSMMapData;
class UGSMPieceData;
class UGSMTileData;

/** 2D 地图展示基类。当前保留数据绑定和刷新事件，具体瓦片绘制交给项目蓝图。 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "GSM 2D地图控件"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMMapWidget2D : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "GSM|2D展示")
	void SetMapData(UGSMMapData* InMapData);

	UFUNCTION(BlueprintPure, Category = "GSM|2D展示")
	UGSMMapData* GetMapData() const { return MapData; }

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示", meta = (DisplayName = "当2D地图数据加载"))
	void ReceiveMapDataLoaded(UGSMMapData* InMapData);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示", meta = (DisplayName = "刷新2D地图"))
	void RefreshMap2D();

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D地图添加棋子数据"))
	void ReceivePieceAdded2D(UGSMTileData* TileData, UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D地图更新棋子数据"))
	void ReceivePieceUpdated2D(UGSMTileData* TileData, UGSMPieceData* PieceData);

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D地图移除棋子数据"))
	void ReceivePieceRemoved2D(UGSMTileData* TileData, UGSMPieceData* PieceData);

protected:
	void BindMapEvents();
	void UnbindMapEvents();

	UFUNCTION()
	void HandlePieceAdded(UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement);

	UFUNCTION()
	void HandlePieceUpdated(UGSMPieceData* PieceData);

	UFUNCTION()
	void HandlePieceRemoved(UGSMPieceData* PieceData);

	UFUNCTION()
	void HandleMapDataCleared(UGSMMapData* ClearedMapData);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "GSM|2D展示")
	TObjectPtr<UGSMMapData> MapData;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UGSMTileData>> BoundTiles;
};
