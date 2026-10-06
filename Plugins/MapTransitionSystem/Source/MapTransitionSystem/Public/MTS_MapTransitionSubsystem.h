#pragma once

#include "CoreMinimal.h"
#include "MTS_MapTransitionTypes.h"
#include "MTS_SubMapDataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTS_MapTransitionSubsystem.generated.h"

class UMTS_MapLoadingWidget;
class UMTS_MapTransitionHandler;
class UMTS_SubMapSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTS_MapTransitionPayloadChanged, const FMTS_MapTransitionPayload&, Payload);

/**
 * 统一地图切换子系统。
 *
 * 所有跨地图流程都从这里进入：先调用处理器预加载，再显示加载界面并打开地图。
 * 如果切换请求不需要主动通知加载完成，目标主地图加载完成后会自动调用处理器加载完成事件。
 * 如果切换请求需要主动通知加载完成，目标地图必须调用 MarkMapReady 或 NotifyMapLoadCompleted 后，才会继续完成流程。
 */
UCLASS(meta = (DisplayName = "地图切换子系统"))
class MAPTRANSITIONSYSTEM_API UMTS_MapTransitionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	friend class FMTSProgressLimitTest;
	friend class FMTSBatchTest;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "地图切换", meta = (DisplayName = "当地图切换状态变化"))
	FMTS_MapTransitionPayloadChanged OnTransitionPayloadChanged;

	/** 创建一个以本子系统为 Outer、可安全跨越 OpenLevel 的地图资源处理对象。 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "创建地图资源处理对象", DeterminesOutputType = "TransitionHandlerClass"))
	UMTS_MapTransitionHandler* CreateTransitionHandler(
		UPARAM(DisplayName = "地图资源处理类") TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass
	);

	/**
	 * 按地图对象切换地图。
	 *
	 * - “需要主动通知加载完成”为 false 时：插件在目标主地图加载完成后，会自动调用地图资源处理类 OnMapLoaded，
	 *   然后执行加载完成延迟、关闭加载界面等后续流程。适合没有额外子关卡/异步初始化的普通地图。
	 * - “需要主动通知加载完成”为 true 时：插件只负责打开目标主地图，并在主地图加载后停留在等待状态。
	 *   你需要在目标地图 BeginPlay、GameMode 或其他对象中完成流式子关卡/异步初始化，
	 *   然后调用“通知地图加载完成”，插件才会调用地图资源处理类 OnMapLoaded 并关闭加载界面。
	 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "按地图对象切换地图"))
	bool SwitchMapByObject(
		UPARAM(DisplayName = "目标地图") TSoftObjectPtr<UWorld> TargetMap,
		UPARAM(DisplayName = "地图资源处理类") TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass,
		UPARAM(DisplayName = "加载界面类") TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
		UPARAM(DisplayName = "加载完成后延迟关闭秒数") float LoadingCompletionDelaySeconds = 0.0f,
		UPARAM(DisplayName = "需要主动通知加载完成") bool bRequireManualMapReadyNotification = false,
		UPARAM(DisplayName = "自动加载进度上限") float AutomaticProgressMax = 1.0f
	);

	/** 使用外部预先创建并填充好的处理对象执行一次地图切换。 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "按地图与处理对象切换地图"))
	bool SwitchMapByHandlerObject(
		UPARAM(DisplayName = "目标地图") TSoftObjectPtr<UWorld> TargetMap,
		UPARAM(DisplayName = "地图资源处理对象") UMTS_MapTransitionHandler* TransitionHandler,
		UPARAM(DisplayName = "加载界面类") TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
		UPARAM(DisplayName = "加载完成后延迟关闭秒数") float LoadingCompletionDelaySeconds = 0.0f,
		UPARAM(DisplayName = "需要主动通知加载完成") bool bRequireManualMapReadyNotification = false,
		UPARAM(DisplayName = "自动加载进度上限") float AutomaticProgressMax = 1.0f
	);

	/**
	 * 按完整切换请求切换地图。
	 *
	 * Request.bRequireManualMapReadyNotification 控制目标主地图加载完成后的行为：
	 * false 表示主地图加载完成后自动进入地图处理类 OnMapLoaded；
	 * true 表示等待目标地图主动调用“通知地图加载完成/MarkMapReady”，再进入 OnMapLoaded。
	 */
	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "按切换请求切换地图"))
	bool SwitchMap(
		UPARAM(DisplayName = "切换请求") const FMTS_MapTransitionRequest& Request
	);

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "通知地图已准备好"))
	void MarkMapReady(
		UPARAM(DisplayName = "就绪来源") FName ReadySource
	);

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "通知地图加载完成"))
	void NotifyMapLoadCompleted(
		UPARAM(DisplayName = "完成来源") FName ReadySource
	);
	/** 异步地图初始化失败时终止本次切换并关闭加载界面。 */
	UFUNCTION(BlueprintCallable, Category="地图切换", meta=(DisplayName="报告地图初始化失败", AutoCreateRefTerm="Error"))
	void ReportMapInitializationFailure(const FText& Error);
	/** 在加载首个子地图前登记全部 ID/权重，串行或并行加载共享剩余进度。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|统一进度", meta=(DisplayName="配置子地图统一进度"))
	bool ConfigureSubMapProgressPlan(const TMap<FName, float>& MapWeights);
	UFUNCTION(BlueprintPure, Category="地图切换|统一进度", meta=(DisplayName="是否已配置子地图统一进度"))
	bool HasSubMapProgressPlan() const { return !SubMapWeights.IsEmpty(); }
	/** 主地图就绪后一次提交整组配置。并行加载，自动登记统一进度；完成仍需蓝图主动通知。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="按配置数组加载子地图", AutoCreateRefTerm="Configs"))
	bool LoadSubMaps(const TArray<FMTS_SubMapLoadConfig>& Configs, FText& OutError);

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "是否正在切换地图"))
	bool IsTransitionInProgress() const { return bTransitionInProgress; }

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "获取当前切换请求"))
	FMTS_MapTransitionRequest GetActiveTransitionRequest() const { return ActiveRequest; }

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "获取当前切换状态"))
	FMTS_MapTransitionPayload GetCurrentPayload() const { return CurrentPayload; }

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "获取当前地图资源处理器"))
	UMTS_MapTransitionHandler* GetActiveTransitionHandler() const { return ActiveHandler; }

	UFUNCTION(BlueprintCallable, Category = "地图切换", meta = (DisplayName = "设置切换进度"))
	void SetTransitionProgress(
		UPARAM(DisplayName = "进度") float Progress,
		UPARAM(DisplayName = "加载内容") const FText& LoadingContent
	);

protected:
	bool StartTransition(const FMTS_MapTransitionRequest& Request, UMTS_MapTransitionHandler* SuppliedHandler);
	void ReleaseActiveHandler();
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMapWithWorld(UWorld* LoadedWorld);
	void FinalizeMapReady();
	void FailTransition(const FText& FailureReason);
	void CompleteTransition();
	void SetTransitionPhase(EMTS_MapTransitionPhase NewPhase, float Progress, const FText& LoadingContent);
	void SetTargetProgress(float Progress);
	void StartProgressSmoothing();
	void StopProgressSmoothing();
	void TickProgressSmoothing();
	void BroadcastPayload();
	void ShowLoadingScreen();
	void HideLoadingScreen();
	void ClearSubMapProgressPlan();
	UFUNCTION() void HandleSubMapProgress(FName MapID, const FMTS_MapTransitionPayload& Payload);
	FName ResolveMapName(const FMTS_MapTransitionRequest& Request) const;

protected:
	UPROPERTY(Transient)
	bool bTransitionInProgress = false;

	UPROPERTY(Transient)
	bool bMapReadyReceived = false;

	UPROPERTY(Transient)
	FMTS_MapTransitionRequest ActiveRequest;

	UPROPERTY(Transient)
	FMTS_MapTransitionPayload CurrentPayload;

	UPROPERTY(Transient)
	TObjectPtr<UMTS_MapTransitionHandler> ActiveHandler;

	UPROPERTY(Transient)
	TObjectPtr<UMTS_MapLoadingWidget> ActiveLoadingWidget;

	UPROPERTY(Transient)
	float TargetProgress = 0.0f;

	FTimerHandle FinalizeMapReadyTimerHandle;

	FTimerHandle CompletionDelayTimerHandle;

	FTimerHandle ProgressSmoothingTimerHandle;
	UPROPERTY(Transient) TObjectPtr<UMTS_SubMapSubsystem> ProgressSubMaps;
	TMap<FName, float> SubMapWeights;
	TMap<FName, float> SubMapProgress;
	TSet<FName> CompletedSubMaps;
	FName DeferredReadySource;
	bool bSubMapsReadyNotified = false;
	TArray<FName> BatchMapIDs;
};
