#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GSMBlueprintLibrary.generated.h"

class AGSMPiece3D;
class AGSMMap3D;
class AGSMTile3D;
class APlayerController;
class UGSMNavigationMoveData;
class UGSMMapSubsystem;
class UGSMMapData;
class UGSMMapDataAsset;
class UGSMPieceData;
class UGSMTileData;

/**
 * 网格策略地图蓝图函数库。
 *
 * 提供常用的子系统获取、地图瓦片查询、路径导航状态查询和导航移动数据对象创建节点。
 */
UCLASS(meta = (DisplayName = "网格策略地图蓝图函数库"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "根据配置创建地图数据"))
	static FGuid CreateMapData(
		const UObject* WorldContextObject,
		UGSMMapDataAsset* MapDataAsset,
		bool bSetAsDefaultMap,
		UGSMMapData*& OutMapData
	);

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取默认地图数据"))
	static UGSMMapData* GetDefaultMapData(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID获取地图数据"))
	static UGSMMapData* GetMapDataByGuid(const UObject* WorldContextObject, const FGuid& MapGuid);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "默认地图增量缩放"))
	static bool AddDefaultMap3DZoomAtWorldLocation(const UObject* WorldContextObject, const FVector& WorldPivotLocation, float ZoomDelta, float& OutMapScale);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID地图增量缩放"))
	static bool AddMap3DZoomAtWorldLocationByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& WorldPivotLocation, float ZoomDelta, float& OutMapScale);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "设置默认地图缩放"))
	static bool SetDefaultMap3DScaleAtWorldLocation(const UObject* WorldContextObject, const FVector& WorldPivotLocation, float NewMapScale, float& OutMapScale);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID设置地图缩放"))
	static bool SetMap3DScaleAtWorldLocationByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& WorldPivotLocation, float NewMapScale, float& OutMapScale);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取默认地图缩放"))
	static bool GetDefaultMap3DScale(const UObject* WorldContextObject, float& OutMapScale);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID获取地图缩放"))
	static bool GetMap3DScaleByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, float& OutMapScale);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "解析默认地图缩放中心"))
	static bool ResolveDefaultMap3DZoomPivotWorldLocation(const UObject* WorldContextObject, const FVector& DesiredWorldPivotLocation, FVector& OutWorldPivotLocation);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|缩放",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID解析地图缩放中心"))
	static bool ResolveMap3DZoomPivotWorldLocationByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& DesiredWorldPivotLocation, FVector& OutWorldPivotLocation);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "开始拖拽默认地图"))
	static bool BeginDefaultMap3DDragAtWorldLocation(const UObject* WorldContextObject, const FVector& WorldDragStartLocation);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID开始拖拽地图"))
	static bool BeginMap3DDragAtWorldLocationByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& WorldDragStartLocation);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "拖拽默认地图到世界位置"))
	static bool DragDefaultMap3DToWorldLocation(const UObject* WorldContextObject, const FVector& CurrentWorldDragLocation, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID拖拽地图到世界位置"))
	static bool DragMap3DToWorldLocationByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& CurrentWorldDragLocation, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按世界偏移平移默认地图"))
	static bool PanDefaultMap3DByWorldDelta(const UObject* WorldContextObject, const FVector& WorldDelta, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID和世界偏移平移地图"))
	static bool PanMap3DByWorldDeltaByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, const FVector& WorldDelta, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "结束拖拽默认地图"))
	static bool EndDefaultMap3DDrag(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID结束拖拽地图"))
	static bool EndMap3DDragByGuid(const UObject* WorldContextObject, const FGuid& MapGuid);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取默认地图拖拽状态"))
	static bool GetDefaultMap3DDragState(const UObject* WorldContextObject, bool& bOutIsDragging);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID获取地图拖拽状态"))
	static bool GetMap3DDragStateByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, bool& bOutIsDragging);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按鼠标开始拖拽默认地图"))
	static bool BeginDefaultMap3DDragWithMouse(const UObject* WorldContextObject, APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID和鼠标开始拖拽地图"))
	static bool BeginMap3DDragWithMouseByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, APlayerController* PlayerController);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取鼠标在默认地图拖拽平面的位置", ToolTip = "仅当鼠标射线与拖拽平面相交且交点位于默认3D地图凹槽内时返回成功。"))
	static bool GetMouseLocationOnDefaultMap3DDragPlane(const UObject* WorldContextObject, APlayerController* PlayerController, FVector& OutWorldLocation);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID获取鼠标在地图拖拽平面的位置", ToolTip = "仅当鼠标射线与拖拽平面相交且交点位于指定3D地图凹槽内时返回成功。"))
	static bool GetMouseLocationOnMap3DDragPlaneByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, APlayerController* PlayerController, FVector& OutWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "拖拽默认地图到鼠标位置"))
	static bool DragDefaultMap3DToMousePosition(const UObject* WorldContextObject, APlayerController* PlayerController, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|拖拽",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID拖拽地图到鼠标位置"))
	static bool DragMap3DToMousePositionByGuid(const UObject* WorldContextObject, const FGuid& MapGuid, APlayerController* PlayerController, FVector& OutMapContentCenter);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按GUID删除地图数据"))
	static bool RemoveMapData(const UObject* WorldContextObject, const FGuid& MapGuid);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按对象删除地图数据"))
	static bool RemoveMapDataObject(const UObject* WorldContextObject, UGSMMapData* MapData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "清空默认地图数据"))
	static bool ClearDefaultMapData(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|清理",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "清空全部地图数据"))
	static int32 ClearAllMapData(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "GSM|地图数据|瓦片",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取瓦片数据"))
	static UGSMTileData* GetTileData(const UObject* WorldContextObject, const FGuid& MapGuid, FName TileId);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|棋子",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "添加棋子数据"))
	static FGuid AddPieceData(
		const UObject* WorldContextObject,
		const FGuid& MapGuid,
		FName TargetTileId,
		FGuid RequestedPieceGuid,
		TSubclassOf<UGSMPieceData> PieceDataClass,
		TSubclassOf<AGSMPiece3D> Piece3DClass,
		const FGSMPiecePlacement& Placement,
		UGSMPieceData*& OutPieceData
	);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图数据|棋子",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "移动棋子数据"))
	static bool MovePieceData(
		const UObject* WorldContextObject,
		const FGuid& MapGuid,
		const FGuid& PieceGuid,
		FName TargetTileId,
		const FGSMPiecePlacement& Placement,
		UGSMPieceData*& OutPieceData
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DeterminesOutputType = "NavigationMoveDataClass", DynamicOutputParam = "OutNavigationMoveData", DisplayName = "创建导航移动数据对象"))
	static bool CreateGSMNavigationMoveData(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "导航移动数据类") TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
		UPARAM(DisplayName = "导航移动数据") UGSMNavigationMoveData*& OutNavigationMoveData
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取网格策略地图子系统"))
	static UGSMMapSubsystem* GetGSMMapSubsystem(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按地图ID和瓦片ID获取地图瓦片"))
	static AGSMTile3D* GetGridStrategyMapTileById(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "瓦片标识") FName TileId
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|瓦片", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "活动地图按瓦片ID获取地图瓦片"))
	static AGSMTile3D* GetActiveGridStrategyMapTileById(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "瓦片标识") FName TileId
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "移动棋子到瓦片"))
	static bool MoveGridStrategyMapPieceToTile(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "棋子ID") FName PieceId,
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "原瓦片ID") FName SourceTileId,
		UPARAM(DisplayName = "目标瓦片ID") FName TargetTileId,
		UPARAM(DisplayName = "移动参数") const FGSMPieceMoveOptions& MoveOptions,
		UPARAM(DisplayName = "输出棋子") AGSMPiece3D*& OutPiece
	);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|导航", meta = (DisplayName = "开始3D地图导航"))
	static bool BeginMap3DNavigation(
		UPARAM(DisplayName = "3D地图") AGSMMap3D* Map3D,
		UPARAM(DisplayName = "起点瓦片") AGSMTile3D* StartTile,
		UPARAM(DisplayName = "终点瓦片") AGSMTile3D* GoalTile,
		UPARAM(DisplayName = "路径瓦片数据") TArray<UGSMTileData*>& OutPathTiles,
		UPARAM(DisplayName = "导航模式") EGSMNavigationMode NavigationMode = EGSMNavigationMode::WalkingOnly
	);

	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|导航", meta = (DisplayName = "是否正在3D地图导航"))
	static bool IsMap3DNavigationActive(
		UPARAM(DisplayName = "3D地图") const AGSMMap3D* Map3D
	);

	UFUNCTION(BlueprintCallable, Category = "GSM|3D地图|导航", meta = (DisplayName = "结束3D地图导航"))
	static bool EndMap3DNavigation(
		UPARAM(DisplayName = "3D地图") AGSMMap3D* Map3D,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "指定地图是否正在鼠标路径导航"))
	static bool IsGridStrategyMapTilePathNavigationActive(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "地图ID") FName MapId
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "活动地图是否正在鼠标路径导航"))
	static bool IsActiveGridStrategyMapTilePathNavigationActive(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "结束指定地图瓦片路径导航并获取移动数据"))
	static UGSMNavigationMoveData* EndGridStrategyMapTilePathNavigationAndGetMoveData(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DeterminesOutputType = "NavigationMoveDataClass", DynamicOutputParam = "OutNavigationMoveData", DisplayName = "结束指定地图瓦片路径导航并获取指定类型移动数据"))
	static bool EndGridStrategyMapTilePathNavigationAndGetMoveDataAs(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "地图ID") FName MapId,
		UPARAM(DisplayName = "导航移动数据类") TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
		UPARAM(DisplayName = "导航移动数据") UGSMNavigationMoveData*& OutNavigationMoveData,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "结束活动地图瓦片路径导航并获取移动数据"))
	static UGSMNavigationMoveData* EndActiveGridStrategyMapTilePathNavigationAndGetMoveData(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DeterminesOutputType = "NavigationMoveDataClass", DynamicOutputParam = "OutNavigationMoveData", DisplayName = "结束活动地图瓦片路径导航并获取指定类型移动数据"))
	static bool EndActiveGridStrategyMapTilePathNavigationAndGetMoveDataAs(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "导航移动数据类") TSubclassOf<UGSMNavigationMoveData> NavigationMoveDataClass,
		UPARAM(DisplayName = "导航移动数据") UGSMNavigationMoveData*& OutNavigationMoveData,
		UPARAM(DisplayName = "清空路径箭头") bool bClearPathArrow = true
	);
};
