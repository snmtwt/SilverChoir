#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Display2D/GSMTileMenuTypes.h"
#include "GSMTileMenuButton.generated.h"

class AGSMMap3D;
class AGSMTile3D;
class APlayerController;
class UGSMTileContextMenu;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FGSMTileMenuButtonClicked,
	UGSMTileMenuButton*, Button,
	FGSMTileMenuContext, MenuContext,
	UGSMTileContextMenu*, OwningMenu
);

/**
 * 地图瓦片右键菜单中的按钮基类。
 *
 * C++ 不创建具体按钮控件，蓝图子类可以自由实现外观和点击行为。
 * 当按钮被注册到菜单时，系统会自动把触发瓦片、所属地图、命中位置、屏幕位置等上下文写入按钮。
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class GRIDSTRATEGYMAPSYSTEM_API UGSMTileMenuButton : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 设置按钮上下文。菜单注册按钮时会自动调用。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "设置菜单上下文", ToolTip = "把当前菜单上下文和所属菜单写入按钮。"))
	void SetMenuContext(
		UPARAM(DisplayName = "菜单上下文") const FGSMTileMenuContext& InMenuContext,
		UPARAM(DisplayName = "所属菜单") UGSMTileContextMenu* InOwningMenu
	);

	/** 由蓝图按钮点击事件调用，用来触发统一的按钮点击流程。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "触发菜单按钮点击", ToolTip = "蓝图按钮控件被点击时调用该函数，触发按钮事件和委托。"))
	void TriggerOnClickEvent();

	/** 蓝图可重写事件：当按钮收到菜单上下文时触发。适合刷新按钮文字、图标或可用状态。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "当菜单上下文设置", ToolTip = "按钮收到菜单上下文后触发，可在蓝图中刷新显示或状态。"))
	void OnMenuContextAssigned(
		UPARAM(DisplayName = "菜单上下文") const FGSMTileMenuContext& InMenuContext,
		UPARAM(DisplayName = "所属菜单") UGSMTileContextMenu* InOwningMenu
	);

	void OnMenuContextAssigned_Implementation(
		const FGSMTileMenuContext& InMenuContext,
		UGSMTileContextMenu* InOwningMenu
	);

	/** 蓝图可重写事件：当按钮被点击时触发。设置 bHandled 可阻止默认广播委托。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "当菜单按钮点击", ToolTip = "按钮点击时触发。蓝图可设置已处理来阻止默认点击委托。"))
	void OnClickMenuButton(
		UPARAM(DisplayName = "菜单上下文") const FGSMTileMenuContext& InMenuContext,
		UPARAM(DisplayName = "所属菜单") UGSMTileContextMenu* InOwningMenu,
		UPARAM(DisplayName = "已处理") bool& bHandled
	);

	void OnClickMenuButton_Implementation(
		const FGSMTileMenuContext& InMenuContext,
		UGSMTileContextMenu* InOwningMenu,
		bool& bHandled
	);

	/** 默认点击委托。蓝图事件未处理点击时广播。 */
	UPROPERTY(BlueprintAssignable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "点击事件", ToolTip = "按钮被点击且蓝图事件未处理时广播。"))
	FGSMTileMenuButtonClicked OnClicked;

	/** 完整菜单上下文。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "菜单上下文", ToolTip = "按钮当前持有的完整菜单上下文。"))
	FGSMTileMenuContext MenuContext;

	/** 打开菜单时使用的玩家控制器。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "玩家控制器", ToolTip = "打开菜单时使用的玩家控制器。"))
	TObjectPtr<APlayerController> PlayerController = nullptr;

	/** 触发菜单的瓦片对象。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "触发瓦片", ToolTip = "打开菜单时右键点击的瓦片对象。"))
	TObjectPtr<AGSMTile3D> TileActor = nullptr;

	/** 触发瓦片所属的地图对象。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "所属地图", ToolTip = "触发瓦片所属的地图对象。"))
	TObjectPtr<AGSMMap3D> OwningMap = nullptr;

	/** 鼠标右键命中的世界位置。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "世界命中位置", ToolTip = "打开菜单时鼠标命中的世界位置。"))
	FVector WorldHitLocation = FVector::ZeroVector;

	/** 打开菜单时的屏幕位置。菜单通常会显示在这个位置附近。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "屏幕位置", ToolTip = "打开菜单时鼠标所在的屏幕坐标。"))
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** 当前按钮注册到的菜单对象。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "所属菜单", ToolTip = "当前按钮所属的右键菜单对象。"))
	TObjectPtr<UGSMTileContextMenu> OwningMenu = nullptr;
};
