#pragma once

#include "CoreMinimal.h"
#include "GSMTileMenuTypes.generated.h"

class AGSMMap3D;
class AGSMTile3D;
class APlayerController;

/**
 * 地图瓦片右键菜单创建时传递给菜单和按钮的上下文。
 *
 * 右键菜单和菜单按钮都会收到这份数据，蓝图中可以直接读取触发瓦片、所属地图和点击位置，
 * 不需要再额外从世界中查找。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMTileMenuContext
{
	GENERATED_BODY()

	/** 打开菜单时使用的玩家控制器。 */
	UPROPERTY(BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "玩家控制器", ToolTip = "打开菜单时使用的玩家控制器。"))
	TObjectPtr<APlayerController> PlayerController = nullptr;

	/** 触发菜单的瓦片对象。 */
	UPROPERTY(BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "触发瓦片", ToolTip = "打开菜单时右键点击的瓦片对象。"))
	TObjectPtr<AGSMTile3D> TileActor = nullptr;

	/** 触发瓦片所属的地图对象。 */
	UPROPERTY(BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "所属地图", ToolTip = "触发瓦片所属的地图对象。"))
	TObjectPtr<AGSMMap3D> OwningMap = nullptr;

	/** 鼠标命中的世界位置。 */
	UPROPERTY(BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "世界命中位置", ToolTip = "打开菜单时鼠标命中的世界位置。"))
	FVector WorldHitLocation = FVector::ZeroVector;

	/** 鼠标所在屏幕位置。 */
	UPROPERTY(BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "屏幕位置", ToolTip = "打开菜单时鼠标所在的屏幕坐标。"))
	FVector2D ScreenPosition = FVector2D::ZeroVector;
};
