#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MTS_MapTransitionTypes.h"
#include "MTS_MapLoadingWidget.generated.h"

/**
 * 地图加载界面基类。
 *
 * 插件只创建传入的加载控件子类，并向它推送切图进度；
 * 具体表现、动画、文字布局都在蓝图子类中实现。
 */
UCLASS(Blueprintable, BlueprintType, Abstract, meta = (DisplayName = "地图加载控件"))
class MAPTRANSITIONSYSTEM_API UMTS_MapLoadingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "应用地图切换状态"))
	void ApplyTransitionPayload(
		UPARAM(DisplayName = "切换状态") const FMTS_MapTransitionPayload& Payload
	);

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "获取当前切换状态"))
	FMTS_MapTransitionPayload GetCachedTransitionPayload() const { return CachedTransitionPayload; }

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "地图切换", meta = (DisplayName = "当地图切换状态变化"))
	void OnTransitionPayloadChanged(
		UPARAM(DisplayName = "切换状态") const FMTS_MapTransitionPayload& Payload
	);

	UFUNCTION(BlueprintImplementableEvent, Category = "地图切换", meta = (DisplayName = "当地图切换进度变化"))
	void OnTransitionProgressChanged(
		UPARAM(DisplayName = "进度") float Progress,
		UPARAM(DisplayName = "加载内容") const FText& LoadingContent
	);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "当前切换状态"))
	FMTS_MapTransitionPayload CachedTransitionPayload;
};
