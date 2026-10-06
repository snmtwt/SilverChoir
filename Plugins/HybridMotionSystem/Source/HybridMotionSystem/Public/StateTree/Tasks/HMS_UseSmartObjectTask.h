// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayInteractionContext.h"
#include "SmartObjectTypes.h"
#include "StateTreeTaskBase.h"
#include "HMS_UseSmartObjectTask.generated.h"

class AActor;

/** Use Smart Object 状态树任务的输入与运行时数据。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_UseSmartObjectTaskInstanceData
{
	GENERATED_BODY()

	/** 执行交互的角色；未绑定时会从 StateTree Owner 解析。 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> UserActor = nullptr;

	/** Claim Smart Object Slot 输出的占用句柄。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Claimed Handle"))
	FSmartObjectClaimHandle ClaimedHandle;

	/** 从 Claimed Handle 反查得到，不作为 StateTree 输入暴露。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> SmartObjectActor = nullptr;

	/** 驱动 Smart Object 自身 Gameplay Interaction StateTree 的运行时上下文。 */
	UPROPERTY(Transient)
	FGameplayInteractionContext InteractionContext;

	/** 防止任务正常结束后 ExitState 再次中止或释放。 */
	UPROPERTY(Transient)
	bool bInteractionActive = false;
};

/**
 * 使用已经 Claim 的 Smart Object 插槽。
 *
 * 进入任务时将插槽切换为 Occupied，并启动 Smart Object 行为定义中的
 * Gameplay Interaction StateTree；任务保持 Running，直到内部 StateTree
 * 成功或失败。无论正常结束还是外部中断，任务都会释放插槽。
 */
USTRUCT(meta = (DisplayName = "HMS Use Smart Object", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_UseSmartObjectTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_UseSmartObjectTaskInstanceData;

	FHMS_UseSmartObjectTask();

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
