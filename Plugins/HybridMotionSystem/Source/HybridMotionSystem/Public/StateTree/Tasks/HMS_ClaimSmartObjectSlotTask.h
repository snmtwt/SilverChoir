// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectTypes.h"
#include "StateTreeTaskBase.h"
#include "HMS_ClaimSmartObjectSlotTask.generated.h"

class AActor;

/** Claim Smart Object Slot 状态树任务的输入和输出。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_ClaimSmartObjectSlotTaskInstanceData
{
	GENERATED_BODY()

	/**
	 * 使用插槽的角色，同时会作为 Smart Object 条件计算及 Claim 的 UserData。
	 * 未绑定时会尝试使用 StateTree 所有者；若所有者是 Controller，则使用其 Pawn。
	 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> UserActor = nullptr;

	/** Find Smart Object 输出的 Smart Object Actor，用于校验归属和搜索备用插槽。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Smart Object"))
	TObjectPtr<AActor> SmartObject = nullptr;

	/** Find Smart Object 输出的候选插槽。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Slot To Be Claimed"))
	FSmartObjectSlotHandle SlotToBeClaimed;

	/** 成功占用后的句柄，绑定给后续移动或 Use Smart Object 任务。 */
	UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Claimed Handle"))
	FSmartObjectClaimHandle ClaimedHandle;
};

/**
 * 以 Normal 优先级占用候选 Smart Object 插槽。
 * 若候选已被抢占，会尝试同一个 Smart Object 上的其他可用 Gameplay Interaction 插槽。
 */
USTRUCT(meta = (DisplayName = "HMS Claim Smart Object Slot", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_ClaimSmartObjectSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_ClaimSmartObjectSlotTaskInstanceData;

	FHMS_ClaimSmartObjectSlotTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
