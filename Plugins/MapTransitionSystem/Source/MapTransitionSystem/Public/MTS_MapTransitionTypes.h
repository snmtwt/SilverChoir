#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "MTS_MapTransitionTypes.generated.h"

class UMTS_MapLoadingWidget;
class UMTS_MapTransitionHandler;

UENUM(BlueprintType)
enum class EMTS_MapTransitionPhase : uint8
{
	None UMETA(DisplayName = "无"),
	Preloading UMETA(DisplayName = "预加载"),
	OpeningMap UMETA(DisplayName = "打开地图"),
	WaitingForMapReady UMETA(DisplayName = "等待地图就绪"),
	ApplyingLoadedResources UMETA(DisplayName = "应用地图资源"),
	Completed UMETA(DisplayName = "完成"),
	Failed UMETA(DisplayName = "失败"),
	WaitingForManualProgress UMETA(DisplayName = "等待手动进度")
};

USTRUCT(BlueprintType)
struct MAPTRANSITIONSYSTEM_API FMTS_MapTransitionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "目标地图"))
	TSoftObjectPtr<UWorld> TargetMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "目标地图名"))
	FName TargetMapName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "地图资源处理类"))
	TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "加载界面类"))
	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "加载标题"))
	FText LoadingTitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "加载副标题"))
	FText LoadingSubtitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "加载提示"))
	FText LoadingTip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "加载完成后延迟关闭秒数", ClampMin = "0.0", UIMin = "0.0"))
	float LoadingCompletionDelaySeconds = 0.0f;

	/**
	 * 是否需要目标地图主动通知插件“地图已经加载完成”。
	 *
	 * false：主地图 OpenLevel 完成后，插件会在 PostLoadMapWithWorld 后自动进入地图处理类 OnMapLoaded，
	 *        然后继续关闭加载 UI 等后续流程。适合普通地图，没有额外异步初始化或流式子关卡。
	 * true：主地图加载完成后，插件会停在 WaitingForMapReady 阶段并保持加载 UI。
	 *       目标地图需要在 GameMode、关卡蓝图或任意对象中完成自己的异步初始化后，
	 *       主动调用“通知地图加载完成/MarkMapReady”，插件才会调用地图处理类 OnMapLoaded。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "需要主动通知加载完成"))
	bool bRequireManualMapReadyNotification = false;
	/** 项目可据此跳过 GameMode 自动初始化，由 OnMainMapLoaded 处理。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="地图切换", meta=(DisplayName="由处理类初始化子地图"))
	bool bInitializeMapFromHandler = false;

	/** 显示进度追赶目标进度的速度。数值越大，进度条越快接近目标；0 表示不平滑，直接跳到目标进度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "进度平滑速度", ClampMin = "0.0", UIMin = "0.0"))
	float ProgressSmoothSpeed = 1.5f;

	/** 自动阶段的进度上限，0.8 表示 80%。仅影响显示进度，不影响就绪通知、关闭延迟或完成条件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "自动加载进度上限", ClampMin = "0.0", ClampMax = "1.0"))
	float AutomaticProgressMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图切换", meta = (DisplayName = "绝对切图"))
	bool bAbsoluteTravel = true;

	bool HasValidTargetMap() const
	{
		return !TargetMap.IsNull() || !TargetMapName.IsNone();
	}
};

USTRUCT(BlueprintType)
struct MAPTRANSITIONSYSTEM_API FMTS_MapTransitionPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "阶段"))
	EMTS_MapTransitionPhase Phase = EMTS_MapTransitionPhase::None;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "进度"))
	float Progress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "加载内容"))
	FText LoadingContent;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "加载标题"))
	FText LoadingTitle;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "加载副标题"))
	FText LoadingSubtitle;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "加载提示"))
	FText LoadingTip;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "就绪来源"))
	FName ReadySource = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "是否阻塞输入"))
	bool bShouldBlockInput = false;
};
