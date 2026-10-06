// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_ClearFocusTask.h"

#include "AIController.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_ClearFocusTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSClearFocusTask, Log, All);

FHMS_ClearFocusTask::FHMS_ClearFocusTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FHMS_ClearFocusTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (IsValid(InstanceData.Actor))
	{
		InstanceData.Actor->ClearFocus(EAIFocusPriority::Gameplay);
	}
	else
	{
		UE_LOG(LogHMSClearFocusTask, Warning,
			TEXT("[ClearFocus] AIController 无效，未执行清除。任务将保持运行。"));
	}

	return EStateTreeRunStatus::Running;
}

