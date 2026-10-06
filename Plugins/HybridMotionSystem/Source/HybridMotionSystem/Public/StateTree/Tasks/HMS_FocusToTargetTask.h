// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "HMS_FocusToTargetTask.generated.h"

class AActor;
class AAIController;

/** 与样例 STT_FocusToTarget 对应的输入数据。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_FocusToTargetTaskInstanceData
{
	GENERATED_BODY()

	/** 负责控制角色朝向的 AI Controller。 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (DisplayName = "Actor"))
	TObjectPtr<AAIController> Actor = nullptr;

	/** 需要注视的 Actor，例如正在交互的长椅。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Target to Focus"))
	TObjectPtr<AActor> TargetToFocus = nullptr;
};

/**
 * 让 AI Controller 注视指定 Actor。
 *
 * 与原蓝图相同，本任务进入后保持 Running，并且不会在退出时自动 ClearFocus；
 * 后续的 SetFocus、ClearFocus 或 AI Controller 销毁会覆盖/清除这个焦点。
 */
USTRUCT(meta = (DisplayName = "HMS Focus On Target", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_FocusToTargetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_FocusToTargetTaskInstanceData;

	FHMS_FocusToTargetTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

