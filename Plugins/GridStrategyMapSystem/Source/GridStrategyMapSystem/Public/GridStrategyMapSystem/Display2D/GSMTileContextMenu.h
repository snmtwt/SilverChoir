#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Display2D/GSMTileMenuTypes.h"
#include "TimerManager.h"
#include "GSMTileContextMenu.generated.h"

class UGSMTileMenuButton;
class UVerticalBox;

/**
 * 地图瓦片右键菜单基类。
 *
 * C++ 只负责菜单生命周期、按钮注册和点击外部自动关闭逻辑。
 * 具体菜单外观必须由蓝图子类实现，并且蓝图根布局中必须提供一个名为 MenuButtonVerticalBox 的 VerticalBox，
 * 系统传入的按钮会被添加到这个容器里。
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class GRIDSTRATEGYMAPSYSTEM_API UGSMTileContextMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 初始化菜单上下文。右键触发瓦片、所属地图、命中位置和屏幕位置都会通过这里传入。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "初始化地图瓦片菜单", ToolTip = "设置菜单上下文并刷新菜单按钮。"))
	void InitializeMapTileMenu(
		UPARAM(DisplayName = "菜单上下文") const FGSMTileMenuContext& InMenuContext
	);

	/** 重新从蓝图事件中获取按钮并填充到菜单容器。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "刷新菜单按钮", ToolTip = "调用获取地图瓦片菜单按钮事件，并把返回按钮加入菜单容器。"))
	void RefreshMenuButtons();

	/** 批量添加已经创建好的按钮对象。可选择先清空现有按钮。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "批量添加菜单按钮", ToolTip = "将传入的按钮对象添加到 MenuButtonVerticalBox，可选择清空旧按钮。"))
	void BatchAddMapTileMenuButtons(
		UPARAM(DisplayName = "菜单按钮") const TArray<UGSMTileMenuButton*>& InMenuButtons,
		UPARAM(DisplayName = "清空已有按钮") bool bClearExistingButtons
	);

	/** 注册单个按钮，并把当前菜单上下文写入按钮。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "注册菜单按钮", ToolTip = "把按钮添加到菜单容器，并把菜单上下文同步到按钮内部。"))
	void RegisterMapTileMenuButton(
		UPARAM(DisplayName = "菜单按钮") UGSMTileMenuButton* InMenuButton
	);

	/** 蓝图可重写事件：根据上下文创建或返回菜单按钮。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "获取地图瓦片菜单按钮", ToolTip = "蓝图可重写该事件，根据瓦片上下文返回要显示的按钮对象。"))
	void GetMapTileMenuButtons(
		UPARAM(DisplayName = "菜单上下文") const FGSMTileMenuContext& InMenuContext,
		UPARAM(DisplayName = "菜单按钮") TArray<UGSMTileMenuButton*>& OutMenuButtons,
		UPARAM(DisplayName = "清空默认按钮") bool& bClearExistingButtons
	);

	void GetMapTileMenuButtons_Implementation(
		const FGSMTileMenuContext& InMenuContext,
		TArray<UGSMTileMenuButton*>& OutMenuButtons,
		bool& bClearExistingButtons
	);

	/** 延迟关闭自身。用于避免打开菜单那一帧的鼠标按下状态立刻触发关闭。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "延迟关闭菜单", ToolTip = "经过点击关闭延迟后移除菜单自身。"))
	void OnDelayCloseSelf();

public:
	/** 菜单当前使用的上下文，包含触发瓦片、所属地图和屏幕位置。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "菜单上下文", ToolTip = "菜单当前使用的瓦片上下文。"))
	FGSMTileMenuContext MenuContext;

	/** 点击其他位置后延迟关闭的时间。保留短延迟可避免刚打开菜单时立即关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|瓦片菜单", meta = (DisplayName = "点击后关闭延迟", ClampMin = "0.0", ToolTip = "检测到鼠标点击后延迟多少秒关闭菜单。"))
	float CloseDelayAfterMouseClick = 0.2f;

protected:
	/** 蓝图中必须绑定的按钮容器。系统创建或传入的菜单按钮会添加到这里。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UVerticalBox> MenuButtonVerticalBox = nullptr;

	bool bFirstUpdateAfterOpen = true;
	bool bConstructed = false;
	FTimerHandle CloseSelfTimerHandle;
};
