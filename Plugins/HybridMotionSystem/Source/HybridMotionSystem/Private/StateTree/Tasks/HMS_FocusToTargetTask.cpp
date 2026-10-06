// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_FocusToTargetTask.h"

#include "AIController.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_FocusToTargetTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSFocusToTargetTask, Log, All);

FHMS_FocusToTargetTask::FHMS_FocusToTargetTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FHMS_FocusToTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!IsValid(InstanceData.Actor) || !IsValid(InstanceData.TargetToFocus))
	{
		UE_LOG(LogHMSFocusToTargetTask, Warning,
			TEXT("[FocusToTarget] 未设置焦点：AIController 或 Target to Focus 无效。Controller=%s，Target=%s。任务将保持运行。"),
			*GetNameSafe(InstanceData.Actor),
			*GetNameSafe(InstanceData.TargetToFocus));
		return EStateTreeRunStatus::Running;
	}

	InstanceData.Actor->SetFocus(InstanceData.TargetToFocus, EAIFocusPriority::Gameplay);

	// 原蓝图没有调用 FinishTask；它作为并行辅助任务保持到当前 State 结束。
	return EStateTreeRunStatus::Running;
}

