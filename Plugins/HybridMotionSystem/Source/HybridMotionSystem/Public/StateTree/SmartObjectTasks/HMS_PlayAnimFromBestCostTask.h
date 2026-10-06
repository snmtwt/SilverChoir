// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectTypes.h"
#include "SmartObject/HMS_SmartObjectSelectionTypes.h"
#include "StateTreeTaskBase.h"
#include "HMS_PlayAnimFromBestCostTask.generated.h"

class AActor;
class UAnimMontage;
class UPlayMontageCallbackProxy;
class UProxyTable;

/** “Play Anim From Best Cost”任务的输入与运行时数据。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_PlayAnimFromBestCostTaskInstanceData
{
	GENERATED_BODY()

	/** 执行动画的角色；未绑定时从 StateTree Owner 解析。 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> Actor = nullptr;

	/** Claim Smart Object Slot 输出的句柄。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Claimed Handle"))
	FSmartObjectClaimHandle ClaimedHandle;

	/** MoveTo 使用的交互入口世界变换，同时也是 Motion Warping 目标。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Destination"))
	FTransform Destination = FTransform::Identity;

	/** 将内部 Smart Object Proxy Asset 映射到具体 Chooser/动画的 Proxy Table。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Animation Proxy Table"))
	TObjectPtr<UProxyTable> AnimationProxyTable = nullptr;

	/** 评分低于该值时接受当前动画。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Cost Threshold", ClampMin = "0.0"))
	double CostThreshold = 1.0;

	/** 仅当导航路径剩余距离不大于该值时开始进行动画评分。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Maximum Distance Threshold", ClampMin = "0.0"))
	double MaximumDistanceThreshold = 300.0;

	/** 速度低于该值时暂缓 Motion Match；到达终点时仍会使用兜底结果。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Minimum Velocity Check", ClampMin = "0.0"))
	double MinimumVelocityCheck = 10.0;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> SelectedMontage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPlayMontageCallbackProxy> PlaybackProxy = nullptr;

	UPROPERTY(Transient)
	double SelectedCost = TNumericLimits<double>::Max();

	UPROPERTY(Transient)
	double SelectedStartTime = 0.0;

	UPROPERTY(Transient)
	bool bPlaybackStarted = false;
};

/**
 * 根据导航距离、接近角度和 Motion Match Cost 持续选择 Smart Object 进入动画。
 * 找到足够低的 Cost 或角色到达终点后，设置 Motion Warping、播放 Montage，
 * 并保持 Running 直到动画结束。
 */
USTRUCT(meta = (DisplayName = "HMS Evaluate and Play Motion Matched Montage", Category = "HMS|Smart Object|Interaction"))
struct HYBRIDMOTIONSYSTEM_API FHMS_PlayAnimFromBestCostTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_PlayAnimFromBestCostTaskInstanceData;

	FHMS_PlayAnimFromBestCostTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
