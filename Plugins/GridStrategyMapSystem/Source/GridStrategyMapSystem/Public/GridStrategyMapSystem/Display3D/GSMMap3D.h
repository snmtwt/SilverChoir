#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMMap3D.generated.h"

class AGSMTile3D;
class AGSMPathArrow3D;
class APlayerController;
class UGSMBoardMeshComponent;
class UGSMMapDataAsset;
class UGSMMapData;
class UGSMTileData;
class UGSMNavigationMoveData;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGSMTileClicked,
	AGSMTile3D*, TileActor,
	FVector, WorldHitLocation
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGSMSelectedTileChanged);

/**
 * 网格策略地图 Actor。
 *
 * 该 Actor 是运行时地图入口：从配置资产生成瓦片，统一处理范围、缩放、点击裁决和寻路。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "网格策略地图"))
class GRIDSTRATEGYMAPSYSTEM_API AGSMMap3D : public AActor
{
	GENERATED_BODY()

	friend class UGSMMapData;

public:
	AGSMMap3D();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintAssignable, Category = "网格策略地图", meta = (DisplayName = "当地图瓦片被点击"))
	FGSMTileClicked OnGridMapTileClicked;

	/** Read GetSelectedTile in the callback; another listener may change selection reentrantly. */
	UPROPERTY(BlueprintAssignable, Category = "网格策略地图|选择", meta = (DisplayName = "当选中瓦片变化"))
	FGSMSelectedTileChanged OnSelectedTileChanged;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "从配置加载地图"))
	bool LoadMapFromConfig();

	UFUNCTION(BlueprintCallable, Category = "GSM|3D展示", meta = (DisplayName = "从地图数据加载3D地图"))
	bool LoadMapFromData();

	UFUNCTION(BlueprintPure, Category = "GSM|3D展示")
	FGuid GetMapGuid() const { return MapGuid; }

	UFUNCTION(BlueprintPure, Category = "GSM|3D展示")
	UGSMMapData* GetMapData() const { return MapData; }

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "从配置重建地图"))
	void RebuildMapFromConfig();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "生成地图"))
	void GenerateMapFromEditorConfig();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|棋盘", meta = (DisplayName = "重新生成棋盘网格"))
	void RebuildBoardMesh();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|棋盘", meta = (DisplayName = "使用棋盘凹槽作为地图范围"))
	void ApplyBoardGrooveBoundsToMapBounds(
		UPARAM(DisplayName = "刷新瓦片隐藏") bool bRefreshTiles = true
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘", meta = (DisplayName = "获取棋盘凹槽范围"))
	FGSMQuadBounds GetBoardGrooveMapBounds() const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋盘", meta = (DisplayName = "鼠标是否命中地图凹槽"))
	bool GetMouseHitOnMapGroove(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "命中位置") FVector& OutHitLocation
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘", meta = (DisplayName = "获取棋盘网格组件"))
	UGSMBoardMeshComponent* GetBoardMeshComponent() const { return BoardMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图", meta = (DisplayName = "获取地图ID"))
	FName GetMapId() const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|地形", meta = (DisplayName = "获取地图地形网格组件"))
	UStaticMeshComponent* GetMapTerrainMeshComponent() const { return MapTerrainMeshComponent; }

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|地形", meta = (DisplayName = "刷新地图地形网格"))
	void RefreshMapTerrainMesh();

	void HandleBoardMeshChanged();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "清空地图瓦片"))
	void ClearMapTiles();

	/** 通知当前地图生成的全部有效 3D 瓦片执行其可重载更新入口。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|3D展示", meta = (DisplayName = "通知所有3D瓦片更新"))
	void NotifyAllTilesUpdate();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "设置地图配置"))
	void SetMapConfig(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* NewMapConfig,
		UPARAM(DisplayName = "重新加载地图") bool bReloadMap
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|范围", meta = (DisplayName = "设置地图范围"))
	void SetMapBounds(
		UPARAM(DisplayName = "地图范围") const FGSMQuadBounds& NewMapBounds,
		UPARAM(DisplayName = "启用范围") bool bEnableBounds,
		UPARAM(DisplayName = "刷新瓦片") bool bRefreshTiles
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|范围", meta = (DisplayName = "设置地图范围启用"))
	void SetMapBoundsEnabled(
		UPARAM(DisplayName = "启用范围") bool bNewUseMapBounds,
		UPARAM(DisplayName = "刷新瓦片") bool bRefreshTiles
	);

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|范围", meta = (DisplayName = "刷新范围隐藏"))
	void RefreshMapBoundsVisibility();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|缩放", meta = (DisplayName = "按位置增量缩放"))
	float AddZoomAtWorldLocation(
		UPARAM(DisplayName = "世界缩放中心") const FVector& WorldPivotLocation,
		UPARAM(DisplayName = "缩放输入") float ZoomDelta
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|缩放", meta = (DisplayName = "按位置设置缩放"))
	float SetMapScaleAtWorldLocation(
		UPARAM(DisplayName = "世界缩放中心") const FVector& WorldPivotLocation,
		UPARAM(DisplayName = "目标缩放") float NewMapScale
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|交互", meta = (DisplayName = "解析地图缩放中心"))
	FVector ResolveMapZoomPivotWorldLocation(
		UPARAM(DisplayName = "期望世界中心") const FVector& DesiredWorldPivotLocation
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "开始拖拽地图"))
	void BeginDragMapAtWorldLocation(
		UPARAM(DisplayName = "拖拽开始世界位置") const FVector& WorldDragStartLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "拖拽地图到世界位置"))
	FVector DragMapToWorldLocation(
		UPARAM(DisplayName = "当前拖拽世界位置") const FVector& CurrentWorldDragLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "按世界偏移平移地图"))
	FVector PanMapByWorldDelta(
		UPARAM(DisplayName = "世界偏移") const FVector& WorldDelta
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "结束拖拽地图"))
	void EndDragMap();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|拖拽", meta = (DisplayName = "是否正在拖拽地图"))
	bool IsDraggingMap() const { return bIsDraggingMap; }

	/** Called by a tile on mouse release, or by a controller that consumes the release itself. */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "消耗拖拽后的瓦片点击抑制"))
	bool ConsumeTileClickSuppressionAfterDrag();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "按鼠标开始拖拽地图"))
	bool BeginDragMapWithMouse(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController
	);

	/** Explicit map-only picking for a focused map view; unrelated scene geometry is ignored. */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|交互", meta = (DisplayName = "拾取鼠标下的本地图瓦片"))
	AGSMTile3D* GetTileUnderMouse(APlayerController* PlayerController, FVector& OutWorldHitLocation) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "获取鼠标在拖拽平面的位置", ToolTip = "仅当鼠标射线与拖拽平面相交且交点位于地图凹槽内时返回成功。"))
	bool GetMouseLocationOnDragPlane(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "世界位置") FVector& OutWorldLocation
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|拖拽", meta = (DisplayName = "拖拽地图到鼠标位置"))
	bool DragMapToMousePosition(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "地图内容中心") FVector& OutMapContentCenter
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|范围", meta = (DisplayName = "世界位置是否在地图范围内"))
	bool IsWorldLocationInsideMapBounds(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|范围", meta = (DisplayName = "本地位置是否在地图范围内"))
	bool IsLocalLocationInsideMapBounds(
		UPARAM(DisplayName = "本地位置") const FVector& LocalLocation
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图", meta = (DisplayName = "按标识获取瓦片"))
	AGSMTile3D* GetTileById(
		UPARAM(DisplayName = "瓦片标识") FName TileId
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|导航", meta = (DisplayName = "按格子坐标获取瓦片"))
	AGSMTile3D* GetTileByGridCoordinate(
		UPARAM(DisplayName = "格子坐标") FIntPoint GridCoordinate
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|导航", meta = (DisplayName = "获取最近瓦片"))
	AGSMTile3D* GetNearestTileToWorldLocation(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "要求可步行") bool bRequireWalkable,
		UPARAM(DisplayName = "尊重地图范围") bool bRespectMapBounds
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|导航", meta = (DisplayName = "按瓦片标识寻路"))
	bool FindPathByTileIds(
		UPARAM(DisplayName = "起点瓦片标识") FName StartTileId,
		UPARAM(DisplayName = "目标瓦片标识") FName GoalTileId,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "路径结果") FGSMPathResult& OutPath
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|导航", meta = (DisplayName = "按瓦片对象寻路"))
	bool FindPathByTiles(
		UPARAM(DisplayName = "起点瓦片") AGSMTile3D* StartTile,
		UPARAM(DisplayName = "目标瓦片") AGSMTile3D* GoalTile,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "路径结果") FGSMPathResult& OutPath
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|导航", meta = (DisplayName = "按世界位置寻路"))
	bool FindPathByWorldLocations(
		UPARAM(DisplayName = "起点世界位置") const FVector& StartWorldLocation,
		UPARAM(DisplayName = "目标世界位置") const FVector& GoalWorldLocation,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "路径结果") FGSMPathResult& OutPath
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "显示导航路径箭头"))
	AGSMPathArrow3D* ShowNavigationPath(
		UPARAM(DisplayName = "路径结果") const FGSMPathResult& PathResult
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "开始瓦片路径导航"))
	bool BeginTilePathNavigation(
		UPARAM(DisplayName = "起点瓦片") AGSMTile3D* StartTile,
		UPARAM(DisplayName = "路径箭头对象类") TSubclassOf<AGSMPathArrow3D> InPathArrowActorClass,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode = EGSMNavigationMode::WalkingOnly,
		UPARAM(DisplayName = "导航移动数据") UGSMNavigationMoveData* NavigationMoveData = nullptr
	);

	bool BeginTilePathNavigationWithCompletion(
		AGSMTile3D* StartTile,
		EGSMNavigationMode NavigationMode,
		const FGSMNavigationFinished& NavigationFinishedEvent
	);

	bool CompleteTilePathNavigation(AGSMTile3D* GoalTile);

	/**
	 * 使用固定的起点与终点开始 3D 导航。
	 * 路径由关联的 UGSMMapData 计算；本 Actor 只保存显示状态并生成路径箭头。
	 */
	bool BeginFixedTilePathNavigation(
		AGSMTile3D* StartTile,
		AGSMTile3D* GoalTile,
		EGSMNavigationMode NavigationMode,
		TArray<UGSMTileData*>& OutPathTiles
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "关闭瓦片路径导航"))
	bool EndTilePathNavigation(
		UPARAM(DisplayName = "导航路径瓦片数据") TArray<UGSMTileData*>& OutPathTiles,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "结束瓦片路径导航并获取移动数据"))
	UGSMNavigationMoveData* EndTilePathNavigationAndGetMoveData(
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "处理瓦片悬浮导航"))
	bool HandleTileHoverNavigation(
		UPARAM(DisplayName = "悬浮瓦片") AGSMTile3D* HoveredTile
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "是否正在瓦片路径导航"))
	bool IsTilePathNavigationActive() const { return bIsTilePathNavigationActive; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取导航起点瓦片"))
	AGSMTile3D* GetTilePathNavigationStartTile() const { return TilePathNavigationStartTile.Get(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取当前导航目标瓦片"))
	AGSMTile3D* GetTilePathNavigationTargetTile() const { return TilePathNavigationTargetTile.Get(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取当前导航路径结果"))
	FGSMPathResult GetCurrentTilePathNavigationResult() const { return CurrentTilePathNavigationResult; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取当前导航移动数据"))
	UGSMNavigationMoveData* GetTilePathNavigationMoveData() const { return TilePathNavigationMoveData.Get(); }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "清空导航路径箭头"))
	void ClearNavigationPath();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "刷新导航路径箭头"))
	bool RefreshNavigationPathVisual();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "获取或创建路径箭头对象"))
	AGSMPathArrow3D* GetOrCreatePathArrowActor();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径显示", meta = (DisplayName = "获取路径箭头对象"))
	AGSMPathArrow3D* GetPathArrowActor() const { return PathArrowActor.Get(); }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "获取瓦片导航路径点"))
	FVector GetNavigationPathPointWorldLocation(
		UPARAM(DisplayName = "瓦片") AGSMTile3D* TileActor
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径显示", meta = (DisplayName = "采样地图地形高度"))
	bool SampleMapTerrainWorldHeightAtWorldLocation(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "高度") float& OutWorldZ
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|地形", meta = (DisplayName = "获取地图地形表面世界位置", ToolTip = "根据当前地图地形网格体的实时缩放、旋转和位置，计算指定世界 XY 落在网格体表面上的世界位置。"))
	bool GetMapTerrainSurfaceWorldLocationAtWorldLocation(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "地形表面世界位置") FVector& OutSurfaceWorldLocation
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "处理瓦片点击请求"))
	bool HandleTileClickRequest(
		UPARAM(DisplayName = "瓦片") AGSMTile3D* TileActor,
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "地图是否接受瓦片点击"))
	bool CanAcceptTileClick(
		UPARAM(DisplayName = "瓦片") AGSMTile3D* TileActor,
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图", meta = (DisplayName = "当瓦片点击通过"))
	void OnTileClickAccepted(
		UPARAM(DisplayName = "瓦片") AGSMTile3D* TileActor,
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|选择", meta = (DisplayName = "切换选中瓦片"))
	bool SwitchSelectedTile(
		UPARAM(DisplayName = "新选中瓦片") AGSMTile3D* NewSelectedTile
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|选择", meta = (DisplayName = "清空选中瓦片"))
	void ClearSelectedTile();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|选择", meta = (DisplayName = "获取选中瓦片"))
	AGSMTile3D* GetSelectedTile() const { return SelectedTile.Get(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|缩放", meta = (DisplayName = "获取当前地图缩放"))
	float GetCurrentMapScale() const { return CurrentMapScale; }

	/** 获取地图统一配置的当前缩放档位索引（从 0 开始）。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|缩放", meta = (DisplayName = "获取当前地图缩放档位"))
	int32 GetCurrentMapScaleLevel() const;

	/**
	 * Resolves an arbitrary normalized map scale against the configured scale-level upper bounds.
	 * Each upper bound belongs to the preceding level.
	 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|缩放", meta = (DisplayName = "解析地图缩放档位"))
	int32 ResolveMapScaleLevel(
		UPARAM(DisplayName = "地图缩放") float MapScale
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|缩放", meta = (DisplayName = "获取有效最小地图缩放"))
	float GetEffectiveMinMapScale() const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|缩放", meta = (DisplayName = "获取全景最小地图缩放", ToolTip = "归一化缩放中，全景最小缩放固定为 1。"))
	float GetAutoFitMinMapScale() const;

protected:
	FName MakeRuntimeTileId(const FGSMTileEntry& TileEntry, int32 TileIndex) const;
	float GetEffectiveMaxMapScale() const;
	float CalculateMapFitScaleMultiplier() const;
	float GetEffectiveMapContentScale() const;
	float GetScaledRuntimeSquareTileSize() const;
	bool IsValidWalkingNeighborCoordinate(const FIntPoint& FromCoordinate, const FIntPoint& ToCoordinate) const;
	FVector2D CalculateUnscaledMapContentSizeFromConfig() const;
	bool GetMapScaleViewportBounds(FBox2D& OutBounds) const;
	FVector2D GetMapScaleViewportSize() const;
	FVector2D GetMapScaleViewportCenter() const;
	bool GetSpawnedTileContentBoundsLocal(FBox2D& OutBounds) const;
	bool GetMapTerrainTargetBoundsLocal(FBox2D& OutBounds) const;
	FVector GetSpawnedTileContentCenterWorldLocation() const;
	void GetRuntimeGridSize(int32& OutColumnCount, int32& OutRowCount) const;
	FVector2D CalculateConstrainedTileContentDeltaLocal(const FVector2D& DesiredDelta) const;
	FVector2D MoveSpawnedTilesByLocalDelta(const FVector2D& DesiredDelta);
	void ConstrainSpawnedTilesToScaleViewport();
	FTransform MakeScaledTileLocalTransform(const FTransform& BaseLocalTransform) const;
	void ApplyMapScaleToSpawnedTiles(float OldMapContentScale, const FVector& LocalPivot);
	void NotifySpawnedTilesMapScaleChanged(float PreviousMapScale);
	void ApplyMapScaleToTerrainMesh(float OldMapContentScale, const FVector& LocalPivot);
	void UpdateMapTerrainMeshTransform();
	void EnsureMapTerrainMaterialInstances();
	void GetMapMaterialBoundsMaskParameters(FVector& OutWorldCenter, FVector& OutWorldAxisX,
		FVector& OutWorldAxisY, FVector2D& OutWorldHalfSize) const;
	void RefreshSpawnedTilePieces() const;
	bool FindMapTerrainVisibleRegionMinZ(
		const FVector& TerrainRelativeLocation,
		const FVector& TerrainScale,
		const FRotator& TerrainRelativeRotation,
		float& OutMinZ
	) const;
	void ApplyMapTerrainMaterialBoundsMask(
		const FVector& WorldCenter,
		const FVector& WorldAxisX,
		const FVector& WorldAxisY,
		const FVector2D& WorldHalfSize,
		float Feather,
		bool bEnabled
	);
	void ApplyDecalReceiverStencilSettings();
	void ConfigurePlayerControllerClickEvents() const;
	void ApplyPathArrowClassFromMapConfig();
	void CacheTilePathNavigationResult(const FGSMPathResult& PathResult);
	void ResetTilePathNavigationState(bool bClearNavigationMoveDataPath = true);
	void RefreshCoordinateLabels();
	void ClearCoordinateLabels();
	UTextRenderComponent* GetOrCreateCoordinateLabelComponent(
		TArray<TObjectPtr<UTextRenderComponent>>& Components,
		int32 Index,
		const TCHAR* NamePrefix
	);
	void ConfigureCoordinateLabelComponent(
		UTextRenderComponent* LabelComponent,
		const FString& LabelText,
		const FVector& LocalLocation,
		const FRotator& LocalRotation
	) const;
	FString MakeColumnCoordinateLabel(int32 ColumnIndex) const;
	FString MakeRowCoordinateLabel(int32 RowIndex) const;
	void RegisterWithMapSubsystem();
	void UnregisterFromMapSubsystem();
	void HandleMapDataCleared(UGSMMapData* ClearedMapData);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "根组件"))
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "棋盘网格组件"))
	TObjectPtr<UGSMBoardMeshComponent> BoardMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "地图地形网格组件"))
	TObjectPtr<UStaticMeshComponent> MapTerrainMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径显示", meta = (DisplayName = "路径箭头对象类"))
	TSubclassOf<AGSMPathArrow3D> PathArrowActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径显示", meta = (DisplayName = "重载地图时清空路径箭头"))
	bool bClearPathVisualWhenMapReloads = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径显示", meta = (DisplayName = "路径点使用地形高度"))
	bool bUseTerrainHeightForNavigationPath = true;

	UPROPERTY(Transient)
	TObjectPtr<AGSMPathArrow3D> PathArrowActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MapTerrainMaterialInstances;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图", meta = (DisplayName = "地图配置"))
	TObjectPtr<UGSMMapDataAsset> MapConfig;

	/** 默认直接绑定子系统的默认地图数据；关闭后使用 MapGuid 查询。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GSM|3D展示", meta = (DisplayName = "使用默认地图数据"))
	bool bUseDefaultMapData = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GSM|3D展示", meta = (DisplayName = "地图GUID", EditCondition = "!bUseDefaultMapData"))
	FGuid MapGuid;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|3D展示")
	TObjectPtr<UGSMMapData> MapData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图", meta = (DisplayName = "地图ID", ToolTip = "用于子系统保存瓦片和棋子数据的稳定标识。切换场景后要继续使用同一张地图数据时，请填写固定值。"))
	FName MapId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图", meta = (DisplayName = "开始时加载地图"))
	bool bLoadOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|交互", meta = (DisplayName = "自动配置玩家点击事件"))
	bool bAutoConfigurePlayerClickEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|交互", meta = (DisplayName = "自动配置玩家悬浮事件"))
	bool bAutoConfigurePlayerHoverEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|交互", meta = (DisplayName = "启用瓦片右键点击"))
	bool bEnableRightMouseTileClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|范围", meta = (DisplayName = "启用地图范围"))
	bool bUseMapBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|范围", meta = (DisplayName = "地图范围"))
	FGSMQuadBounds MapBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|棋盘", meta = (DisplayName = "使用棋盘凹槽作为地图范围"))
	bool bUseBoardGrooveAsMapBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|材质裁切", meta = (DisplayName = "更新瓦片材质范围裁切"))
	bool bUpdateTileMaterialBoundsMask = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|材质裁切", meta = (DisplayName = "材质范围裁切羽化", ClampMin = "0.0"))
	float MaterialBoundsMaskFeather = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "启用CustomStencil贴花接收遮罩", ToolTip = "未设置地图配置资产时使用。设置配置资产后，以配置资产中的贴花遮罩设置为准。"))
	bool bUseCustomStencilForMapDecals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "贴花接收Stencil值", ClampMin = "0", ClampMax = "255", ToolTip = "未设置地图配置资产时使用。设置配置资产后，以配置资产中的贴花遮罩设置为准。"))
	int32 MapDecalReceiverStencilValue = 71;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "整体地形接收贴花Stencil", ToolTip = "未设置地图配置资产时使用。设置配置资产后，以配置资产中的贴花遮罩设置为准。"))
	bool bMarkWholeMapTerrainAsDecalReceiver = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "瓦片网格接收贴花Stencil", ToolTip = "未设置地图配置资产时使用。设置配置资产后，以配置资产中的贴花遮罩设置为准。"))
	bool bMarkTileMeshesAsDecalReceivers = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|地形", meta = (DisplayName = "地图地形高度偏移"))
	float MapTerrainHeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|地形", meta = (DisplayName = "地图地形水平角度偏移"))
	float MapTerrainYawOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "显示坐标标签"))
	bool bShowCoordinateLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "显示顶部列标签"))
	bool bShowTopCoordinateLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "显示底部列标签"))
	bool bShowBottomCoordinateLabels = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "显示左侧行标签"))
	bool bShowLeftCoordinateLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "显示右侧行标签"))
	bool bShowRightCoordinateLabels = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "坐标标签世界尺寸", ClampMin = "1.0"))
	float CoordinateLabelWorldSize = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "坐标标签偏移", ClampMin = "0.0"))
	float CoordinateLabelOffset = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "坐标标签高度偏移"))
	float CoordinateLabelZOffset = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "坐标标签颜色"))
	FLinearColor CoordinateLabelColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "顶部标签旋转"))
	FRotator TopCoordinateLabelRelativeRotation = FRotator(90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "底部标签旋转"))
	FRotator BottomCoordinateLabelRelativeRotation = FRotator(90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "左侧标签旋转"))
	FRotator LeftCoordinateLabelRelativeRotation = FRotator(90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "右侧标签旋转"))
	FRotator RightCoordinateLabelRelativeRotation = FRotator(90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|坐标标签", meta = (DisplayName = "从顶部开始编号行"))
	bool bNumberCoordinateRowsFromTop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "最小缩放", ClampMin = "1.0", ToolTip = "归一化缩放下 1 表示完整展示全部瓦片。"))
	float MinMapScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "自动计算全景最小缩放"))
	bool bUseAutoFitMinMapScale = true;

	/** 兼容旧资产保留；归一化缩放语义下不再额外引入全景留白。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", AdvancedDisplay, meta = (DisplayName = "旧版全景留白倍率（已停用）", ClampMin = "0.01", ClampMax = "1.0"))
	float AutoFitMinMapScalePadding = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "最小缩放时居中瓦片"))
	bool bCenterTilesAtMinMapScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "生成时使用最小缩放"))
	bool bStartGeneratedMapAtMinScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "最大缩放", ClampMin = "0.01"))
	float MaxMapScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "缩放步长", ClampMin = "0.01"))
	float ZoomStep = 0.1f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|缩放", meta = (DisplayName = "当前地图缩放", ToolTip = "归一化缩放：1 表示完整展示全部瓦片，大于 1 表示放大。"))
	float CurrentMapScale = 1.0f;

	/**
	 * 全部 3D 瓦片共用的缩放档位上限。运行时按从小到大解析。
	 * 默认 2、4 对应三个档位：<=2、(2,4]、>4。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|缩放", meta = (DisplayName = "缩放档位分界值", ClampMin = "1.0"))
	TArray<float> MapScaleLevelUpperBounds = { 2.0f, 4.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|拖拽", meta = (DisplayName = "拖拽时锁定Z轴"))
	bool bLockDragToOriginalZ = true;

	/** Accumulated local drag distance required to cancel the pending tile click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|拖拽", meta = (DisplayName = "取消点击的拖拽距离", ClampMin = "0.0"))
	float TileClickDragSuppressionDistance = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "瓦片生成碰撞处理"))
	ESpawnActorCollisionHandlingMethod TileSpawnCollisionHandling = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|导航", meta = (DisplayName = "按格子自动生成步行邻接"))
	bool bAutoGenerateWalkingLinksFromGrid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|导航", meta = (DisplayName = "允许经过隐藏瓦片"))
	bool bAllowNavigationThroughHiddenTiles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|导航", meta = (DisplayName = "最小导航代价", ClampMin = "0.01"))
	float MinimumNavigationCost = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|导航", meta = (DisplayName = "六边形邻接偏移"))
	TArray<FIntPoint> HexWalkingNeighborOffsets;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "正在瓦片路径导航"))
	bool bIsTilePathNavigationActive = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "导航起点瓦片"))
	TObjectPtr<AGSMTile3D> TilePathNavigationStartTile;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "导航目标瓦片"))
	TObjectPtr<AGSMTile3D> TilePathNavigationTargetTile;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "导航模式"))
	EGSMNavigationMode TilePathNavigationMode = EGSMNavigationMode::WalkingOnly;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "当前导航路径结果"))
	FGSMPathResult CurrentTilePathNavigationResult;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "当前导航移动数据"))
	TObjectPtr<UGSMNavigationMoveData> TilePathNavigationMoveData;

	UPROPERTY(Transient)
	FGSMNavigationFinished TilePathNavigationFinishedDelegate;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图", meta = (DisplayName = "已生成瓦片"))
	TArray<TObjectPtr<AGSMTile3D>> SpawnedTiles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|选择", meta = (DisplayName = "选中瓦片"))
	TObjectPtr<AGSMTile3D> SelectedTile;

	UFUNCTION()
	void HandleSelectedTileEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	UFUNCTION()
	void HandleSelectedTileDestroyed(AActor* Actor);

	uint64 SelectionChangeRevision = 0;
	bool bClearingOrEndingMap = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> ColumnCoordinateLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> BottomColumnCoordinateLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> RowCoordinateLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> RightRowCoordinateLabels;

	float RuntimeBaseSquareTileSize = 100.0f;
	FVector2D RuntimeUnscaledMapContentSize = FVector2D(100.0f, 100.0f);
	float RuntimeMapFitScaleMultiplier = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|拖拽", meta = (DisplayName = "正在拖拽地图"))
	bool bIsDraggingMap = false;

	FVector DragStartWorldLocation = FVector::ZeroVector;
	FVector DragPreviousWorldLocation = FVector::ZeroVector;
	float DragPlaneLocalZ = 0.0f;
	float AccumulatedDragDistance = 0.0f;
	bool bDragMovedBeyondClickTolerance = false;
	bool bDragClickSuppressionConsumed = false;
	bool bSuppressNextTileClickAfterDrag = false;

	TMap<FName, TWeakObjectPtr<AGSMTile3D>> SpawnedTilesById;
	TMap<FIntPoint, TWeakObjectPtr<AGSMTile3D>> SpawnedTilesByGridCoordinate;
};
