#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MTS_MapTransitionTypes.h"
#include "MTS_MapTransitionBlueprintLibrary.generated.h"

class UMTS_MapLoadingWidget;
class UMTS_MapTransitionHandler;
class UMTS_MapTransitionSubsystem;

/**
 * 地图切换蓝图函数库。
 *
 * 这些节点只负责从当前世界自动找到地图切换子系统，真实状态仍由 UMTS_MapTransitionSubsystem 管理。
 */
UCLASS(meta = (DisplayName = "地图切换函数库"))
class MAPTRANSITIONSYSTEM_API UMTS_MapTransitionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads the registered instance's requested loading location, including loading/hidden maps.
	 * Does not load a map or assert readiness. Unknown ID/world returns false and zero location. */
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据MapID获取地图加载位置", ReturnDisplayName="找到地图"))
	static bool GetMapLoadingLocation(const UObject* WorldContextObject, FName MapID,
		UPARAM(DisplayName="加载位置") FVector& OutLocation);

	/**
	 * 创建一个可在切图前由外部填充数据的地图资源处理对象。
	 *
	 * 对象由地图切换子系统持有为 Outer，因此可以安全跨越 OpenLevel。
	 * 传给“按处理对象切换地图”后，该对象只用于本次切图，并会在流程结束后回收。
	 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "创建地图资源处理对象", DeterminesOutputType = "TransitionHandlerClass"))
	static UMTS_MapTransitionHandler* CreateMapTransitionHandler(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "地图资源处理类") TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass
	);

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "获取地图切换子系统"))
	static UMTS_MapTransitionSubsystem* GetMapTransitionSubsystem(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject
	);

	/**
	 * 按完整切换请求切换地图。
	 *
	 * 切换请求中的“需要主动通知加载完成”为 false 时，主地图加载完成后会自动调用地图处理类 OnMapLoaded。
	 * 为 true 时，插件会等待目标地图主动调用“通知地图加载完成”。
	 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "按切换请求切换地图"))
	static bool SwitchMap(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "切换请求") const FMTS_MapTransitionRequest& Request
	);

	/**
	 * 按地图对象切换地图。
	 *
	 * - “需要主动通知加载完成”为 false 时：目标主地图加载完成后，插件自动调用地图处理类 OnMapLoaded 并关闭加载 UI。
	 * - “需要主动通知加载完成”为 true 时：目标地图需要在自己的 BeginPlay 或其它初始化流程结束后，调用“通知地图加载完成”。
	 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "按地图对象切换地图"))
	static bool SwitchMapByObject(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "目标地图") TSoftObjectPtr<UWorld> TargetMap,
		UPARAM(DisplayName = "地图资源处理类") TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass,
		UPARAM(DisplayName = "加载界面类") TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
		UPARAM(DisplayName = "加载完成后延迟关闭秒数") float LoadingCompletionDelaySeconds = 0.0f,
		UPARAM(DisplayName = "需要主动通知加载完成") bool bRequireManualMapReadyNotification = false,
		UPARAM(DisplayName = "自动加载进度上限") float AutomaticProgressMax = 1.0f
	);

	/** 使用已经创建并填充好数据的地图资源处理对象切换地图。对象在本次流程完成或失败后回收。 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "按地图与处理对象切换地图"))
	static bool SwitchMapByHandlerObject(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "目标地图") TSoftObjectPtr<UWorld> TargetMap,
		UPARAM(DisplayName = "地图资源处理对象") UMTS_MapTransitionHandler* TransitionHandler,
		UPARAM(DisplayName = "加载界面类") TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
		UPARAM(DisplayName = "加载完成后延迟关闭秒数") float LoadingCompletionDelaySeconds = 0.0f,
		UPARAM(DisplayName = "需要主动通知加载完成") bool bRequireManualMapReadyNotification = false,
		UPARAM(DisplayName = "自动加载进度上限") float AutomaticProgressMax = 1.0f
	);

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "通知地图已准备好"))
	static void MarkMapReady(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "就绪来源") FName ReadySource
	);

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "通知地图加载完成"))
	static void NotifyMapLoadCompleted(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "完成来源") FName ReadySource
	);

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (WorldContext = "WorldContextObject", DisplayName = "设置切换进度"))
	static void SetTransitionProgress(
		UPARAM(DisplayName = "世界上下文") const UObject* WorldContextObject,
		UPARAM(DisplayName = "进度") float Progress,
		UPARAM(DisplayName = "加载内容") const FText& LoadingContent
	);
};
