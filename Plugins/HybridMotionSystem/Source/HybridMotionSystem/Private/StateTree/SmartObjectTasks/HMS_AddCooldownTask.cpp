// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/SmartObjectTasks/HMS_AddCooldownTask.h"

#include "AIController.h"
#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_AddCooldownTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSAddCooldownTask, Log, All);

FHMS_AddCooldownTask::FHMS_AddCooldownTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FHMS_AddCooldownTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	UHMS_SmartObjectInteractionComponent* InteractionComponent = IsValid(InstanceData.Actor)
		? InstanceData.Actor->FindComponentByClass<UHMS_SmartObjectInteractionComponent>()
		: nullptr;
	if (!IsValid(InteractionComponent) && IsValid(InstanceData.Actor)
		&& IsValid(InstanceData.Actor->GetPawn()))
	{
		InteractionComponent = InstanceData.Actor->GetPawn()
			->FindComponentByClass<UHMS_SmartObjectInteractionComponent>();
	}

	if (!IsValid(InteractionComponent))
	{
		UE_LOG(LogHMSAddCooldownTask, Warning,
			TEXT("[AddCooldown] 添加失败：AIController/Pawn 上没有 HMS Smart Object Interaction Component。Actor=%s。"),
			*GetNameSafe(InstanceData.Actor));
		return EStateTreeRunStatus::Failed;
	}

	if (!InteractionComponent->AddTaggedCooldown(
		InstanceData.CooldownTag, FMath::Max(0.0f, InstanceData.CooldownTime)))
	{
		UE_LOG(LogHMSAddCooldownTask, Warning,
			TEXT("[AddCooldown] 添加失败：Cooldown Tag 无效。Actor=%s，Tag=%s。"),
			*GetNameSafe(InstanceData.Actor), *InstanceData.CooldownTag.ToString());
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Succeeded;
}

