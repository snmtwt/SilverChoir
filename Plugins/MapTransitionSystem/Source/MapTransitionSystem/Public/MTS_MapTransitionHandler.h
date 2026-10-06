#pragma once

#include "CoreMinimal.h"
#include "MTS_MapTransitionTypes.h"
#include "UObject/Object.h"
#include "MTS_MapTransitionHandler.generated.h"

class UMTS_MapTransitionSubsystem;

/**
 * 地图资源处理类。
 *
 * 子蓝图会在切图前收到 PreloadMapResources，可把跨地图数据存到这个对象上；
 * 目标地图明确 MarkMapReady 后，会收到 OnMapLoaded，可以从对象中的数据恢复地图资源。
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, meta = (DisplayName = "地图资源处理类"))
class MAPTRANSITIONSYSTEM_API UMTS_MapTransitionHandler : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="地图切换", meta=(DisplayName="由此处理类初始化子地图"))
	bool bInitializeSubMapsAfterMainMap = false;
	/** 统一进度计划全部完成；是否通知地图就绪由蓝图决定。 */
	UFUNCTION(BlueprintNativeEvent, Category="地图切换", meta=(DisplayName="全部子地图加载完成"))
	void OnSubMapsLoaded(UMTS_MapTransitionSubsystem* TransitionSubsystem);
	virtual void OnSubMapsLoaded_Implementation(UMTS_MapTransitionSubsystem* TransitionSubsystem) {}
	/** 主地图已打开，但可能仍需加载子地图；早于手动 MarkMapReady。 */
	UFUNCTION(BlueprintNativeEvent, Category="地图切换", meta=(DisplayName="主地图加载完成"))
	void OnMainMapLoaded(UMTS_MapTransitionSubsystem* TransitionSubsystem, const FMTS_MapTransitionRequest& Request);
	virtual void OnMainMapLoaded_Implementation(UMTS_MapTransitionSubsystem* TransitionSubsystem, const FMTS_MapTransitionRequest& Request) {}

	void InitializeTransitionHandler(UGameInstance* InGameInstance, const FMTS_MapTransitionRequest& InRequest);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "地图切换", meta = (DisplayName = "预加载地图资源"))
	bool PreloadMapResources(
		UPARAM(DisplayName = "地图切换子系统") UMTS_MapTransitionSubsystem* TransitionSubsystem,
		UPARAM(DisplayName = "切换请求") const FMTS_MapTransitionRequest& Request
	);

	/**
	 * 地图加载完成。
	 *
	 * 目标地图调用 MarkMapReady 后，子系统会在下一帧调用这里。
	 * 蓝图中适合在这里读取 PreloadMapResources 保存的数据，并恢复地图资源、新游戏数据或读档数据。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "地图切换", meta = (DisplayName = "地图加载完成"))
	void OnMapLoaded(
		UPARAM(DisplayName = "地图切换子系统") UMTS_MapTransitionSubsystem* TransitionSubsystem,
		UPARAM(DisplayName = "切换请求") const FMTS_MapTransitionRequest& Request
	);

	/** 地图切换失败时调用。蓝图可在这里回滚临时数据或显示错误提示。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "地图切换", meta = (DisplayName = "地图切换失败"))
	void OnMapTransitionFailed(
		UPARAM(DisplayName = "地图切换子系统") UMTS_MapTransitionSubsystem* TransitionSubsystem,
		UPARAM(DisplayName = "切换请求") const FMTS_MapTransitionRequest& Request,
		UPARAM(DisplayName = "失败原因") const FText& FailureReason
	);

	UFUNCTION(BlueprintPure, Category = "地图切换", meta = (DisplayName = "获取切换请求"))
	FMTS_MapTransitionRequest GetTransitionRequest() const { return TransitionRequest; }

protected:
	virtual bool PreloadMapResources_Implementation(UMTS_MapTransitionSubsystem* TransitionSubsystem, const FMTS_MapTransitionRequest& Request);
	virtual void OnMapLoaded_Implementation(UMTS_MapTransitionSubsystem* TransitionSubsystem, const FMTS_MapTransitionRequest& Request);
	virtual void OnMapTransitionFailed_Implementation(UMTS_MapTransitionSubsystem* TransitionSubsystem, const FMTS_MapTransitionRequest& Request, const FText& FailureReason);

protected:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "所属GameInstance"))
	TObjectPtr<UGameInstance> OwningGameInstance;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "地图切换", meta = (DisplayName = "切换请求"))
	FMTS_MapTransitionRequest TransitionRequest;
};
