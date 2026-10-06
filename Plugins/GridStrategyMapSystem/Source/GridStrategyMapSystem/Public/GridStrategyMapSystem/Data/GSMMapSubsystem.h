#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMMapSubsystem.generated.h"

class AGSMMap3D;
class AGSMPiece3D;
class AGSMTile3D;
class APlayerController;
class UGSMMapDataAsset;
class UGSMMapData;
class UGSMPieceData;
class UGSMTileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGridStrategyActiveMapChanged,
	AGSMMap3D*, ActiveMap
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGridStrategySubsystemTileClicked,
	AGSMTile3D*, TileActor,
	FVector, WorldHitLocation
);

/**
 * 网格策略地图子系统。
 *
 * 提供一个 GameInstance 级统一入口，让 UI、控制器或其他系统不必直接持有地图 Actor 引用。
 */
UCLASS(meta = (DisplayName = "网格策略地图子系统"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMMapSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	friend class AGSMMap3D;

	virtual void Deinitialize() override;

	/** 根据配置创建权威地图数据，返回稳定 GUID。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据", meta = (DisplayName = "创建地图数据"))
	FGuid CreateMapData(UGSMMapDataAsset* MapDataAsset, bool bSetAsDefaultMap, UGSMMapData*& OutMapData);

	/** 创建独立地图数据，始终按 GUID 保存，不占用或替换默认地图。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据", meta = (DisplayName = "创建独立地图数据"))
	FGuid CreateIndependentMapData(UGSMMapDataAsset* MapDataAsset, UGSMMapData*& OutMapData);

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据", meta = (DisplayName = "获取默认地图数据"))
	UGSMMapData* GetDefaultMapData() const { return DefaultMapData; }

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据", meta = (DisplayName = "按GUID获取地图数据"))
	UGSMMapData* GetMapDataByGuid(const FGuid& MapGuid) const;

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据", meta = (DisplayName = "移除地图数据"))
	bool RemoveMapData(const FGuid& MapGuid);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理", meta = (DisplayName = "按对象删除地图数据"))
	bool RemoveMapDataObject(UGSMMapData* MapData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理", meta = (DisplayName = "清空默认地图数据"))
	bool ClearDefaultMapData();

	/** 读档前调用。返回实际深度清空的地图数量。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理", meta = (DisplayName = "清空全部地图数据"))
	int32 ClearAllMapData();

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据")
	int32 GetMapDataCount() const { return MapDataByGuid.Num() + (DefaultMapData.Get() != nullptr ? 1 : 0); }

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据|瓦片")
	UGSMTileData* GetTileData(const FGuid& MapGuid, FName TileId) const;

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|棋子")
	FGuid AddPieceData(const FGuid& MapGuid, FName TargetTileId, FGuid RequestedPieceGuid,
		TSubclassOf<UGSMPieceData> PieceDataClass, TSubclassOf<AGSMPiece3D> Piece3DClass,
		const FGSMPiecePlacement& Placement, UGSMPieceData*& OutPieceData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|棋子")
	bool RemovePieceData(const FGuid& MapGuid, const FGuid& PieceGuid);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|棋子")
	bool MovePieceDataByGuid(const FGuid& MapGuid, const FGuid& PieceGuid, FName TargetTileId,
		const FGSMPiecePlacement& Placement, UGSMPieceData*& OutPieceData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|导航")
	bool FindPathByMapGuid(const FGuid& MapGuid, FName StartTileId, FName GoalTileId,
		EGSMNavigationMode NavigationMode, FGSMPathResult& OutPath) const;

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|导航")
	bool FindPathOnDefaultMap(FName StartTileId, FName GoalTileId,
		EGSMNavigationMode NavigationMode, FGSMPathResult& OutPath) const;

	UPROPERTY(BlueprintAssignable, Category = "网格策略地图", meta = (DisplayName = "当活动地图变化"))
	FGridStrategyActiveMapChanged OnActiveMapChanged;

	UPROPERTY(BlueprintAssignable, Category = "网格策略地图", meta = (DisplayName = "当地图瓦片被点击"))
	FGridStrategySubsystemTileClicked OnMapTileClicked;

	UFUNCTION(BlueprintPure, Category = "网格策略地图", meta = (DisplayName = "获取活动地图"))
	AGSMMap3D* GetActiveGridStrategyMap() const { return ActiveMap.Get(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图", meta = (DisplayName = "按地图ID获取地图"))
	AGSMMap3D* GetGridStrategyMapById(
		UPARAM(DisplayName = "地图ID") FName MapId
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "按地图ID设置活动地图"))
	bool SetActiveGridStrategyMapById(
		UPARAM(DisplayName = "地图ID") FName MapId
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "活动地图按瓦片ID获取瓦片"))
	AGSMTile3D* GetTileByIdOnActiveMap(
		UPARAM(DisplayName = "瓦片标识") FName TileId
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "按地图ID和瓦片ID获取瓦片"))
	AGSMTile3D* GetTileByIdOnMap(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "瓦片标识") FName TileId
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|数据", meta = (DisplayName = "保存地图瓦片数据"))
	void SaveMapTileData(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "瓦片数据") const TArray<FGSMTileEntry>& TileEntries
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|数据", meta = (DisplayName = "获取地图数据"))
	bool GetMapRuntimeData(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "地图数据") FGSMRuntimeData& OutMapData
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "添加或更新瓦片棋子"))
	bool AddOrUpdateMapPieceOnTile(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "瓦片ID") FName TileId,
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "棋子类") TSubclassOf<AGSMPiece3D> PieceClass,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw,
		UPARAM(DisplayName = "默认缩放") float DefaultScale,
		UPARAM(DisplayName = "输出棋子") AGSMPiece3D*& OutPiece
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "从瓦片移除棋子"))
	bool RemoveMapPieceFromTile(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "瓦片ID") FName TileId,
		UPARAM(DisplayName = "棋子ID") FName PieceId
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "移动棋子到瓦片"))
	bool MoveMapPieceToTile(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "源瓦片ID") FName SourceTileId,
		UPARAM(DisplayName = "目标瓦片ID") FName TargetTileId,
		UPARAM(DisplayName = "棋子类") TSubclassOf<AGSMPiece3D> PieceClass,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw,
		UPARAM(DisplayName = "默认缩放") float DefaultScale,
		UPARAM(DisplayName = "输出棋子") AGSMPiece3D*& OutPiece
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "在活动地图加载配置"))
	bool LoadConfigOnActiveMap(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* MapConfig
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|缩放", meta = (DisplayName = "活动地图按位置增量缩放"))
	float AddZoomOnActiveMapAtWorldLocation(
		UPARAM(DisplayName = "世界缩放中心") const FVector& WorldPivotLocation,
		UPARAM(DisplayName = "缩放输入") float ZoomDelta
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|缩放", meta = (DisplayName = "活动地图按位置设置缩放"))
	float SetActiveMapScaleAtWorldLocation(
		UPARAM(DisplayName = "世界缩放中心") const FVector& WorldPivotLocation,
		UPARAM(DisplayName = "目标缩放") float NewMapScale
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|范围", meta = (DisplayName = "设置活动地图范围"))
	void SetActiveMapBounds(
		UPARAM(DisplayName = "地图范围") const FGSMQuadBounds& MapBounds,
		UPARAM(DisplayName = "启用范围") bool bEnableBounds,
		UPARAM(DisplayName = "刷新瓦片") bool bRefreshTiles
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "请求点击地图瓦片"))
	bool RequestTileClick(
		UPARAM(DisplayName = "瓦片") AGSMTile3D* TileActor,
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|导航", meta = (DisplayName = "活动地图获取最近瓦片"))
	AGSMTile3D* GetNearestTileOnActiveMap(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "要求可步行") bool bRequireWalkable,
		UPARAM(DisplayName = "尊重地图范围") bool bRespectMapBounds
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|导航", meta = (DisplayName = "活动地图按瓦片ID寻路"))
	bool FindPathOnActiveMapByTileIds(
		UPARAM(DisplayName = "起点瓦片标识") FName StartTileId,
		UPARAM(DisplayName = "目标瓦片标识") FName GoalTileId,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "路径结果") FGSMPathResult& OutPath
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|导航", meta = (DisplayName = "活动地图按世界位置寻路"))
	bool FindPathOnActiveMapByWorldLocations(
		UPARAM(DisplayName = "起点世界位置") const FVector& StartWorldLocation,
		UPARAM(DisplayName = "目标世界位置") const FVector& GoalWorldLocation,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "路径结果") FGSMPathResult& OutPath
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|交互", meta = (DisplayName = "鼠标是否命中指定地图凹槽"))
	bool GetMouseHitOnMapGroove(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "命中位置") FVector& OutHitLocation
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|交互", meta = (DisplayName = "鼠标是否命中活动地图凹槽"))
	bool GetMouseHitOnActiveMapGroove(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "命中位置") FVector& OutHitLocation
	) const;

	/** 使用鼠标开始拖拽指定地图。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "按鼠标开始拖拽指定地图", ToolTip = "使用当前鼠标位置开始拖拽指定地图ID对应的地图。"))
	bool BeginDragMapWithMouse(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController
	) const;

	/** 将指定地图拖拽到当前鼠标位置，并返回地图内容中心。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "拖拽指定地图到鼠标位置", ToolTip = "根据当前鼠标位置更新指定地图ID对应地图的平移，并输出地图内容中心。"))
	bool DragMapToMousePosition(
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "地图内容中心") FVector& OutMapContentCenter
	) const;

	/** 结束指定地图的拖拽状态。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "结束拖拽指定地图", ToolTip = "清除指定地图ID对应地图的拖拽缓存状态。"))
	void EndDragMap(
		UPARAM(DisplayName = "地图ID") FName MapId
	) const;

	/** 使用鼠标开始拖拽当前活动地图。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "按鼠标开始拖拽活动地图", ToolTip = "使用当前鼠标位置开始拖拽子系统记录的活动地图。"))
	bool BeginDragActiveMapWithMouse(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController
	) const;

	/** 将当前活动地图拖拽到鼠标位置，并返回地图内容中心。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "拖拽活动地图到鼠标位置", ToolTip = "根据当前鼠标位置更新活动地图的平移，并输出地图内容中心。"))
	bool DragActiveMapToMousePosition(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "地图内容中心") FVector& OutMapContentCenter
	) const;

	/** 结束当前活动地图的拖拽状态。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "结束拖拽活动地图", ToolTip = "清除活动地图的拖拽缓存状态。"))
	void EndDragActiveMap() const;

	void BroadcastTileClicked(AGSMTile3D* TileActor, const FVector& WorldHitLocation);
	void RefreshMapPiecesForMap(FName MapId);

protected:
	bool RegisterMap3D(AGSMMap3D* Map3D, bool bUseDefaultMapData, const FGuid& MapGuid, UGSMMapData*& OutMapData);
	void UnregisterMap3D(AGSMMap3D* Map3D, UGSMMapData* MapData);

	void RegisterGridStrategyMap(AGSMMap3D* GridMap, bool bMakeActive);
	void UnregisterGridStrategyMap(AGSMMap3D* GridMap);
	void SetActiveGridStrategyMap(AGSMMap3D* GridMap);

	UPROPERTY(Transient)
	TWeakObjectPtr<AGSMMap3D> ActiveMap;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AGSMMap3D>> RegisteredMaps;

	UPROPERTY(Transient)
	TMap<FName, TWeakObjectPtr<AGSMMap3D>> RegisteredMapsById;

	/** 默认地图走直接指针，避免高频 TMap 查询。 */
	UPROPERTY(Transient)
	TObjectPtr<UGSMMapData> DefaultMapData;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UGSMMapData>> MapDataByGuid;

private:
	FGuid CreateMapDataInternal(UGSMMapDataAsset* MapDataAsset, bool bSetAsDefaultMap,
		bool bPromoteWhenDefaultMissing, UGSMMapData*& OutMapData);
};
