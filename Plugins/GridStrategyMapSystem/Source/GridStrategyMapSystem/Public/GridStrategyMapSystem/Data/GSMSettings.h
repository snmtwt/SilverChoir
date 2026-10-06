#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GSMSettings.generated.h"

class AGSMTile3D;
class UGSMTileContextMenu;

/**
 * 网格策略地图插件级配置。
 */
UCLASS(config = GridStrategyMapSystem, defaultconfig, meta = (DisplayName = "网格策略地图设置"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGSMSettings();

	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("GridStrategyMapSystem"); }

	/** 地图配置资产和单个瓦片都未指定瓦片类时使用的默认地图瓦片 Actor 类。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "地图瓦片", meta = (DisplayName = "默认地图瓦片类"))
	TSubclassOf<AGSMTile3D> DefaultTileActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "地图瓦片菜单", meta = (DisplayName = "默认地图瓦片右键菜单类"))
	TSubclassOf<UGSMTileContextMenu> DefaultTileContextMenuClass;

	/** 已停用：飞行导航固定忽略瓦片的可步行状态。保留字段仅用于兼容旧配置。 */
	UPROPERTY(Config, BlueprintReadOnly, Category = "导航", meta = (DeprecatedProperty, DeprecationMessage = "飞行导航现在固定忽略可步行状态。"))
	bool bFlyingNavigationRequiresWalkableGoal = false;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|配置", meta = (DisplayName = "获取默认地图瓦片类"))
	static TSubclassOf<AGSMTile3D> GetDefaultTileActorClass();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|设置", meta = (DisplayName = "获取默认地图瓦片右键菜单类"))
	static TSubclassOf<UGSMTileContextMenu> GetDefaultTileContextMenuClass();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|设置", meta = (DeprecatedFunction, DeprecationMessage = "飞行导航现在固定忽略可步行状态。", DisplayName = "飞行导航终点是否要求可步行（已停用）"))
	static bool ShouldFlyingNavigationRequireWalkableGoal();
};
