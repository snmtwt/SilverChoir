#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMTile3D.generated.h"

class AGSMMap3D;
class APlayerController;
class UBoxComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UGSMTileContextMenu;
class UGSMTileMenuButton;
class UGSMPieceData;
class UGSMTileData;

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMTilePieceSpawnRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "棋子类"))
	TSubclassOf<AGSMPiece3D> PieceClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "棋子ID"))
	FName PieceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "相对瓦片XY"))
	FVector2D RelativeTileXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "相对瓦片Yaw"))
	float RelativeTileYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "默认缩放", ClampMin = "0.0001"))
	float DefaultScale = 1.0f;
};

/**
 * 网格策略地图瓦片 Actor。
 *
 * 瓦片对象拥有自己的网格、材质和贴花配置。
 * 地图配置资产只决定“生成哪个瓦片类”和“这个瓦片的数据是什么”。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "网格策略地图瓦片"))
class GRIDSTRATEGYMAPSYSTEM_API AGSMTile3D : public AActor
{
	GENERATED_BODY()

public:
	AGSMTile3D();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyActorOnReleased(FKey ButtonReleased) override;
	virtual void NotifyActorBeginCursorOver() override;

	UFUNCTION(BlueprintCallable, Category = "GSM|3D展示")
	void BindTileData(UGSMTileData* InTileData);

	UFUNCTION(BlueprintPure, Category = "GSM|3D展示")
	UGSMTileData* GetTileData() const { return TileData; }

	/** 获取所属 3D 地图当前的归一化缩放；1 表示展示全部瓦片。 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D展示|缩放", meta = (DisplayName = "获取当前3D地图缩放"))
	float GetCurrentMapScale() const;

	/**
	 * 获取当前缩放档位索引（从 0 开始）。
	 * 每个分界值属于它前面的档位；例如分界值为 2、4 时：<=2 为 0，(2,4] 为 1，>4 为 2。
	 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D展示|缩放", meta = (DisplayName = "获取当前3D地图缩放档位"))
	int32 GetCurrentMapScaleLevel() const;

	/**
	 * 3D 瓦片基础初始化完成，并且与瓦片数据对象成功建立双向绑定后调用。
	 * 此事件执行时 GetTileData 有效，且传入数据的 GetTile3D 会返回当前瓦片。
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示", meta = (DisplayName = "当3D瓦片初始化完成"))
	void OnTileInitialized(UGSMTileData* InitializedTileData);

	/** 统一更新入口。蓝图子类可重载，并可按需调用父实现刷新基础表现与棋子变换。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "GSM|3D展示", meta = (DisplayName = "更新3D瓦片"))
	void UpdateTile3D();

	/** 所属 3D 地图的归一化缩放发生变化，并完成瓦片变换刷新后调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示|缩放", meta = (DisplayName = "当3D地图缩放变化"))
	void OnMapScaleChanged(float PreviousMapScale, float NewMapScale);

	/** 仅当所属 3D 地图跨越缩放档位时调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示|缩放", meta = (DisplayName = "当3D地图缩放档位变化"))
	void OnMapScaleLevelChanged(
		int32 PreviousScaleLevel,
		int32 NewScaleLevel,
		float PreviousMapScale,
		float NewMapScale
	);

	/** 由所属地图调用；档位由地图统一计算后下发。 */
	void NotifyMapScaleChanged(
		float PreviousMapScale,
		float NewMapScale,
		int32 PreviousScaleLevel,
		int32 NewScaleLevel
	);

	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示|棋子", meta = (DisplayName = "当3D瓦片添加棋子数据"))
	void OnPieceDataAdded(UGSMPieceData* PieceData, FVector2D RelativeTileXY, float RelativeTileYaw);

	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示|棋子", meta = (DisplayName = "当3D瓦片更新棋子数据"))
	void OnPieceDataUpdated(UGSMPieceData* PieceData);

	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示|棋子", meta = (DisplayName = "当3D瓦片移除棋子数据"))
	void OnPieceDataRemoved(UGSMPieceData* PieceData);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "设置运行时地图缩放"))
	void SetRuntimeMapScale(
		UPARAM(DisplayName = "地图缩放") float NewMapScale
	);

	/** 当前瓦片实际占用的方形边长（已包含地图内容缩放，不包含瓦片 Actor 自身缩放）。 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D展示|瓦片", meta = (DisplayName = "获取运行时瓦片尺寸"))
	float GetRuntimeSquareTileSize() const { return RuntimeSquareTileSize; }

	/** 当前地图内容缩放；用于把相对瓦片坐标换算为当前显示尺寸。 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D展示|瓦片", meta = (DisplayName = "获取运行时地图缩放"))
	float GetRuntimeMapScale() const { return RuntimeMapScale; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "初始化地图瓦片"))
	void InitializeMapTile(
		UPARAM(DisplayName = "瓦片配置") const FGSMTileEntry& InTileEntry,
		UPARAM(DisplayName = "区域允许点击") bool bInRegionAllowsClick
	);

	/** 将瓦片自身配置的网格、材质和贴花参数应用到组件上。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "刷新瓦片表现"))
	void RefreshTileVisuals();

	void SetMaterialBoundsMask(
		const FVector& WorldCenter,
		const FVector& WorldAxisX,
		const FVector& WorldAxisY,
		const FVector2D& WorldHalfSize,
		float Feather,
		bool bEnabled
	);

	void SetDecalReceiverStencil(bool bEnabled, int32 StencilValue, bool bMarkTileMeshAsReceiver);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "设置运行时方形瓦片尺寸"))
	void SetRuntimeSquareTileSize(
		UPARAM(DisplayName = "瓦片尺寸") float NewTileSize
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|表现", meta = (DisplayName = "设置瓦片静态网格表现启用"))
	void SetTileStaticMeshVisualEnabled(
		UPARAM(DisplayName = "启用") bool bNewEnabled
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|点击", meta = (DisplayName = "请求地图瓦片点击"))
	bool RequestMapTileClick(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	/**
	 * 将当前瓦片设为鼠标导航起点。鼠标进入其他瓦片时预览路径，抬起左键时结束并执行回调。
	 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "设为导航起点"))
	bool SetAsNavigationStart(
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode,
		UPARAM(DisplayName = "导航结束事件") FGSMNavigationFinished NavigationFinishedEvent
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|悬浮", meta = (DisplayName = "请求地图瓦片悬浮"))
	bool RequestMapTileHover();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "请求右键点击瓦片"))
	bool RequestMapTileRightClick(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "显示瓦片右键菜单"))
	UGSMTileContextMenu* ShowMapTileContextMenu(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "显示瓦片右键菜单并添加按钮"))
	UGSMTileContextMenu* ShowMapTileContextMenuWithButtons(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation,
		UPARAM(DisplayName = "菜单按钮") const TArray<UGSMTileMenuButton*>& MenuButtons,
		UPARAM(DisplayName = "清空已有按钮") bool bClearExistingButtons
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|碰撞", meta = (DisplayName = "鼠标是否命中瓦片"))
	bool GetMouseHitOnTile(
		UPARAM(DisplayName = "玩家控制器") APlayerController* PlayerController,
		UPARAM(DisplayName = "命中位置") FVector& OutHitLocation
	) const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "通知瓦片点击已通过"))
	void NotifyMapTileClickAccepted(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "通知瓦片右键点击已通过"))
	void NotifyMapTileRightClickAccepted(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|范围", meta = (DisplayName = "设置范围隐藏状态"))
	void SetHiddenByMapBounds(
		UPARAM(DisplayName = "是否范围隐藏") bool bNewHiddenByMapBounds
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "设置瓦片点击启用"))
	void SetTileClickEnabled(
		UPARAM(DisplayName = "允许点击") bool bNewCanReceiveClick
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "设置边线贴花启用"))
	void SetEdgeDecalEnabled(
		UPARAM(DisplayName = "启用边线贴花") bool bNewEdgeDecalEnabled
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "是否范围隐藏"))
	bool IsHiddenByMapBounds() const { return bHiddenByMapBounds; }

	void SetNavigationInteractionOverride(bool bNewNavigationInteractionOverride);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "设置地图选中状态"))
	void SetSelectedByMap(
		UPARAM(DisplayName = "选中") bool bNewSelectedByMap
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "是否被地图选中"))
	bool IsSelectedByMap() const { return bSelectedByMap; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "是否可接收点击"))
	bool CanReceiveMapTileClick() const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "获取所属地图"))
	AGSMMap3D* GetOwningGridMap() const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "获取所属地图ID"))
	FName GetOwningGridMapId() const { return OwningMapId; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "获取瓦片标识"))
	FName GetTileId() const { return TileId; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "获取区域标识"))
	FName GetRegionId() const { return RegionId; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (DisplayName = "获取格子坐标"))
	FIntPoint GetGridCoordinate() const { return GridCoordinate; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "获取导航配置"))
	FGSMTileNavigationSettings GetNavigationSettings() const { return RuntimeNavigationSettings; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "是否允许步行通过"))
	bool CanWalkThrough() const { return RuntimeNavigationSettings.bCanWalkThrough; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "是否拥有公路连接"))
	bool HasRoadConnections() const { return !RuntimeNavigationSettings.RoadConnections.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "获取步行进入代价"))
	float GetWalkEnterCost() const { return RuntimeNavigationSettings.WalkEnterCost; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|导航", meta = (DisplayName = "获取公路进入代价"))
	float GetRoadEnterCost() const { return RuntimeNavigationSettings.RoadEnterCost; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "添加棋子"))
	AGSMPiece3D* AddMapPiece(
		UPARAM(DisplayName = "棋子类") TSubclassOf<AGSMPiece3D> PieceClass,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw = 0.0f,
		UPARAM(DisplayName = "默认缩放") float DefaultScale = 1.0f
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID添加棋子"))
	AGSMPiece3D* AddMapPieceWithId(
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "棋子类") TSubclassOf<AGSMPiece3D> PieceClass,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw = 0.0f,
		UPARAM(DisplayName = "默认缩放") float DefaultScale = 1.0f
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "添加已有棋子"))
	AGSMPiece3D* AddExistingMapPiece(
		UPARAM(DisplayName = "棋子") AGSMPiece3D* PieceActor,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw = 0.0f,
		UPARAM(DisplayName = "默认缩放") float DefaultScale = 1.0f
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "批量添加棋子"))
	bool BatchAddMapPieces(
		UPARAM(DisplayName = "棋子生成请求") const TArray<FGSMTilePieceSpawnRequest>& PieceRequests,
		UPARAM(DisplayName = "生成的棋子") TArray<AGSMPiece3D*>& OutPieces
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "删除棋子"))
	bool RemoveMapPiece(
		UPARAM(DisplayName = "棋子") AGSMPiece3D* PieceActor
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID删除棋子"))
	bool RemoveMapPieceById(
		UPARAM(DisplayName = "棋子ID") FName PieceId
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID移动棋子到瓦片"))
	bool MoveMapPieceByIdToTile(
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "目标瓦片") AGSMTile3D* TargetTile,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw,
		UPARAM(DisplayName = "默认缩放") float DefaultScale,
		UPARAM(DisplayName = "输出棋子") AGSMPiece3D*& OutPiece
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID移动棋子到瓦片ID"))
	bool MoveMapPieceByIdToTileId(
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "目标瓦片ID") FName TargetTileId,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D RelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float RelativeTileYaw,
		UPARAM(DisplayName = "默认缩放") float DefaultScale,
		UPARAM(DisplayName = "输出棋子") AGSMPiece3D*& OutPiece
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "删除全部棋子"))
	void ClearMapPieces();

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "设置棋子相对瓦片位置"))
	bool SetMapPieceRelativeTileXY(
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D NewRelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float NewRelativeTileYaw
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID调整棋子相对瓦片位置"))
	bool AdjustMapPieceRelativeTileXYById(
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D NewRelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float NewRelativeTileYaw
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按棋子对象调整相对瓦片位置"))
	bool AdjustMapPieceRelativeTileXYByActor(
		UPARAM(DisplayName = "棋子") AGSMPiece3D* PieceActor,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D NewRelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float NewRelativeTileYaw
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "计算朝向目标瓦片的边界位置和世界Yaw", ClampMin = "0.0"))
	bool CalculateEdgeRelativeTileXYAndWorldYawTowardTile(
		UPARAM(DisplayName = "下一个目标瓦片ID") FName NextTargetTileId,
		UPARAM(DisplayName = "边界相对瓦片XY") FVector2D& OutRelativeTileXY,
		UPARAM(DisplayName = "世界Yaw角") float& OutWorldYaw,
		UPARAM(DisplayName = "边界内缩") float EdgeInset = 0.0f
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "计算朝向目标瓦片的边界相对位置", DeprecatedFunction, DeprecationMessage = "请使用 CalculateEdgeRelativeTileXYAndWorldYawTowardTile。", ClampMin = "0.0"))
	bool CalculateEdgeRelativeTileXYTowardTile(
		UPARAM(DisplayName = "下一个目标瓦片ID") FName NextTargetTileId,
		UPARAM(DisplayName = "边界相对瓦片XY") FVector2D& OutRelativeTileXY,
		UPARAM(DisplayName = "边界内缩") float EdgeInset = 0.0f
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "计算朝向目标瓦片的世界Yaw角", DeprecatedFunction, DeprecationMessage = "请使用 CalculateEdgeRelativeTileXYAndWorldYawTowardTile。"))
	bool CalculateWorldYawTowardTile(
		UPARAM(DisplayName = "下一个目标瓦片ID") FName NextTargetTileId,
		UPARAM(DisplayName = "世界Yaw角") float& OutWorldYaw
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "按ID获取棋子"))
	AGSMPiece3D* GetMapPieceById(
		UPARAM(DisplayName = "棋子ID") FName PieceId
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "所属地图按ID获取棋子"))
	AGSMPiece3D* GetMapPieceByIdOnOwningMap(
		UPARAM(DisplayName = "棋子ID") FName PieceId
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "获取全部棋子"))
	TArray<AGSMPiece3D*> GetMapPieces() const;

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "刷新棋子变换"))
	void RefreshMapPieces();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "瓦片是否接受点击"))
	bool CanAcceptMapTileClick(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "瓦片是否接受右键点击"))
	bool CanAcceptMapTileRightClick(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片", meta = (DisplayName = "当瓦片被点击"))
	void OnMapTileClicked(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片|悬浮", meta = (DisplayName = "当瓦片被悬浮"))
	void OnMapTileHovered();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "当瓦片被右键点击"))
	void OnMapTileRightClicked(
		UPARAM(DisplayName = "世界命中位置") const FVector& WorldHitLocation
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "当瓦片被地图选中"))
	void OnMapTileSelected();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "当瓦片取消地图选中"))
	void OnMapTileDeselected();

	UFUNCTION(BlueprintImplementableEvent, Category = "网格策略地图|瓦片", meta = (DisplayName = "当范围隐藏状态变化"))
	void OnMapTileHiddenStateChanged(
		UPARAM(DisplayName = "是否范围隐藏") bool bNewHiddenByMapBounds
	);

protected:
	/** Native-only hook that always runs after the Blueprint initialization event. */
	virtual void HandleTileInitializedNative(UGSMTileData* InitializedTileData) {}

	/** Native-only hook that always runs after the Blueprint scale-level event. */
	virtual void HandleMapScaleLevelChangedNative(
		int32 PreviousScaleLevel,
		int32 NewScaleLevel,
		float PreviousMapScale,
		float NewMapScale) {}

	/** Native hooks always run after the matching Blueprint piece-data event. */
	virtual void HandlePieceDataAddedNative(UGSMPieceData* PieceData) {}
	virtual void HandlePieceDataUpdatedNative(UGSMPieceData* PieceData) {}
	virtual void HandlePieceDataRemovedNative(UGSMPieceData* PieceData) {}

	UFUNCTION()
	void HandlePieceDataAdded(UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement);

	UFUNCTION()
	void HandlePieceDataUpdated(UGSMPieceData* PieceData);

	UFUNCTION()
	void HandlePieceDataRemoved(UGSMPieceData* PieceData);

	void CacheMaterialInstances();
	void ApplyHiddenMaterialScalar();
	void ApplyEdgeDecalComponentColor();
	void ApplyEdgeDecalRuntimeScale();
	void ApplySelectionDecalColor();
	void RefreshCollisionForHiddenState();
	void ApplyRuntimeSquareTileSize();
	void ApplyMaterialBoundsMaskParameters();
	void ApplyDecalReceiverStencilSettings();
	void ApplyDecalReceiverStencilMaterialParameters();
	void ApplyMapPieceHiddenState(AGSMPiece3D* PieceActor) const;
	virtual void NotifyMapPieceDetachedFromTile(AGSMPiece3D* PieceActor);
	void UnregisterMapPiece(AGSMPiece3D* PieceActor);
	void RemoveInvalidMapPieces();
	FName MakeUniqueMapPieceId(TSubclassOf<AGSMPiece3D> PieceClass) const;
	bool DoesMapPieceIdExist(FName PieceId, const AGSMPiece3D* IgnoredPieceActor = nullptr) const;
	bool ResolveMapPieceWorldLocation(
		FVector2D RelativeTileXY,
		float RelativeTileZ,
		FVector& OutWorldLocation) const;
	bool RefreshMapPieceTransform(AGSMPiece3D* PieceActor);
	void TryNotifyTileInitialized();
	bool CanCreateRuntimeMaterialInstances() const;
	FVector GetBestClickLocationFromCursor() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "根组件"))
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "瓦片网格组件"))
	TObjectPtr<UStaticMeshComponent> TileMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "瓦片碰撞组件"))
	TObjectPtr<UBoxComponent> TileCollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "边线贴花组件"))
	TObjectPtr<UDecalComponent> EdgeDecalComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|网格", meta = (DisplayName = "启用瓦片静态网格表现"))
	bool bUseTileStaticMeshVisual = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|网格", meta = (DisplayName = "瓦片网格"))
	TObjectPtr<UStaticMesh> TileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|网格", meta = (DisplayName = "瓦片材质"))
	TObjectPtr<UMaterialInterface> TileMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|尺寸", meta = (DisplayName = "适配运行时方形尺寸"))
	bool bFitMeshToRuntimeSquareTileSize = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|碰撞", meta = (DisplayName = "使用独立瓦片碰撞"))
	bool bUseIndependentTileCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|碰撞", meta = (DisplayName = "瓦片碰撞相对位置"))
	FVector TileCollisionRelativeLocation = FVector::ZeroVector;

	/** 碰撞盒 XY 固定为 100x100，仅允许蓝图调整高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|碰撞", meta = (DisplayName = "瓦片碰撞高度", ClampMin = "1.0"))
	float TileCollisionHeight = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "启用边线贴花"))
	bool bUseEdgeDecal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "边线贴花材质"))
	TObjectPtr<UMaterialInterface> EdgeDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "默认边线贴花颜色"))
	FLinearColor DefaultEdgeDecalColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "选中边线贴花颜色"))
	FLinearColor SelectedEdgeDecalColor = FLinearColor(0.0f, 0.45f, 1.0f, 1.0f);

	/** 选中时在组件原有排序上增加优先级，取消选择后恢复。材质可通过 SelectedAmount 参数控制边宽和发光。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "选中边线排序增量", ClampMin = "0"))
	int32 SelectedEdgeDecalSortOrderOffset = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "边线贴花相对位置"))
	FVector EdgeDecalRelativeLocation = FVector(0.0, 0.0, 20.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "边线贴花相对旋转"))
	FRotator EdgeDecalRelativeRotation = FRotator(-90.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "默认边线贴花尺寸", ToolTip = "仅作为 C++ 默认初始值。蓝图中请优先调整边线贴花组件自身的贴花大小 Decal Size。"))
	FVector EdgeDecalSize = FVector(48.0, 128.0, 128.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "按运行时瓦片尺寸适配边线贴花"))
	bool bFitEdgeDecalToRuntimeSquareTileSize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "随地图缩放边线贴花"))
	bool bScaleEdgeDecalWithMapScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "同步地图缩放到贴花材质"))
	bool bApplyMapScaleToEdgeDecalMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "贴花尺寸同步到边框材质"))
	bool bSyncEdgeDecalSizeToBorderMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|边线贴花", meta = (DisplayName = "边框尺寸占贴花尺寸比例", ClampMin = "0.0"))
	float EdgeDecalBorderExtentScale = 0.48f;

	UPROPERTY(Transient)
	bool bUseDecalReceiverStencil = false;

	UPROPERTY(Transient)
	bool bMarkTileMeshAsDecalReceiver = false;

	UPROPERTY(Transient)
	int32 DecalReceiverStencilValue = 71;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|隐藏材质", meta = (DisplayName = "隐藏参数名"))
	FName HiddenScalarParameterName = TEXT("HiddenAmount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|隐藏材质", meta = (DisplayName = "可见参数值"))
	float VisibleScalarValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|隐藏材质", meta = (DisplayName = "隐藏参数值"))
	float HiddenScalarValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|隐藏行为", meta = (DisplayName = "范围外隐藏对象"))
	bool bSetActorHiddenWhenOutsideBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "瓦片表现|隐藏行为", meta = (DisplayName = "隐藏时禁用碰撞"))
	bool bDisableCollisionWhenHidden = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "右键菜单类"))
	TSubclassOf<UGSMTileContextMenu> TileContextMenuClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|右键菜单", meta = (DisplayName = "右键菜单层级"))
	int32 TileContextMenuZOrder = 999;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "所属地图"))
	TWeakObjectPtr<AGSMMap3D> OwningMap;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|3D展示")
	TObjectPtr<UGSMTileData> TileData;

	UPROPERTY(Transient)
	bool bMapTileInitialized = false;

	UPROPERTY(Transient)
	bool bTileInitializationNotified = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "所属地图ID"))
	FName OwningMapId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "瓦片标识"))
	FName TileId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "区域标识"))
	FName RegionId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "显示名称"))
	FText DisplayName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "格子坐标"))
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片|导航", meta = (DisplayName = "运行时导航配置"))
	FGSMTileNavigationSettings RuntimeNavigationSettings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "运行时方形瓦片尺寸"))
	float RuntimeSquareTileSize = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "运行时地图缩放"))
	float RuntimeMapScale = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "是否范围隐藏"))
	bool bHiddenByMapBounds = false;

	UPROPERTY(Transient)
	bool bNavigationInteractionOverride = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|瓦片|选择", meta = (DisplayName = "被地图选中"))
	bool bSelectedByMap = false;

	bool bSelectionDecalSortApplied = false;
	int32 UnselectedDecalSortOrder = 0;

	bool bMaterialBoundsMaskEnabled = false;
	FVector MaterialBoundsMaskCenter = FVector::ZeroVector;
	FVector MaterialBoundsMaskAxisX = FVector::ForwardVector;
	FVector MaterialBoundsMaskAxisY = FVector::RightVector;
	FVector2D MaterialBoundsMaskHalfSize = FVector2D::ZeroVector;
	float MaterialBoundsMaskFeather = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "瓦片允许点击"))
	bool bCanReceiveClick = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "区域允许点击"))
	bool bRegionAllowsClick = true;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TileMaterialInstances;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EdgeDecalMaterialInstance;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "地图瓦片|棋子", meta = (DisplayName = "棋子列表"))
	TArray<TObjectPtr<AGSMPiece3D>> MapPieceActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片|棋子", meta = (DisplayName = "瓦片隐藏时隐藏棋子"))
	bool bHideMapPiecesWhenTileHidden = true;
};
