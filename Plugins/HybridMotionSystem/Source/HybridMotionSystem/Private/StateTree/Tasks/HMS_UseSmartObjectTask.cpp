// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_UseSmartObjectTask.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_UseSmartObjectTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSUseSmartObjectTask, Log, All);

namespace UE::HMS::UseSmartObjectTask::Private
{
	AActor* ResolveUserActor(const FStateTreeExecutionContext& Context, AActor* ExplicitActor)
	{
		if (IsValid(ExplicitActor))
		{
			return ExplicitActor;
		}

		UObject* ContextOwner = Context.GetOwner();
		if (const AController* Controller = Cast<AController>(ContextOwner))
		{
			return Controller->GetPawn() ? Cast<AActor>(Controller->GetPawn()) : const_cast<AController*>(Controller);
		}

		if (AActor* OwnerActor = Cast<AActor>(ContextOwner))
		{
			return OwnerActor;
		}

		if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(ContextOwner))
		{
			return OwnerComponent->GetOwner();
		}

		return nullptr;
	}

	void ReleaseClaim(USmartObjectSubsystem* Subsystem, const FSmartObjectClaimHandle& ClaimedHandle)
	{
		if (IsValid(Subsystem) && ClaimedHandle.IsValid()
			&& Subsystem->IsClaimedSmartObjectValid(ClaimedHandle))
		{
			Subsystem->MarkSlotAsFree(ClaimedHandle);
		}
	}

	void AbortInteraction(FHMS_UseSmartObjectTaskInstanceData& InstanceData)
	{
		if (!InstanceData.bInteractionActive)
		{
			return;
		}

		InstanceData.InteractionContext.SetAbortContext(
			FGameplayInteractionAbortContext(EGameplayInteractionAbortReason::ExternalAbort));
		InstanceData.InteractionContext.Deactivate();
		InstanceData.bInteractionActive = false;
	}
}

FHMS_UseSmartObjectTask::FHMS_UseSmartObjectTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FHMS_UseSmartObjectTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bInteractionActive = false;
	InstanceData.SmartObjectActor = nullptr;

	AActor* UserActor = UE::HMS::UseSmartObjectTask::Private::ResolveUserActor(
		Context, InstanceData.UserActor);
	if (!IsValid(UserActor))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：无法取得有效的 UserActor。StateTreeOwner=%s。"),
			*GetNameSafe(Context.GetOwner()));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.ClaimedHandle.IsValid())
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：Claimed Handle 无效。请绑定 Claim Smart Object Slot 的输出。"));
		return EStateTreeRunStatus::Failed;
	}

	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!IsValid(Subsystem))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：当前 World=%s 无法取得 SmartObjectSubsystem。"),
			*GetNameSafe(World));
		return EStateTreeRunStatus::Failed;
	}

	if (!Subsystem->IsClaimedSmartObjectValid(InstanceData.ClaimedHandle))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：ClaimedHandle=%s 已失效或不再属于当前用户。"),
			*LexToString(InstanceData.ClaimedHandle));
		return EStateTreeRunStatus::Failed;
	}

	USmartObjectComponent* HandleComponent = Subsystem->GetSmartObjectComponent(InstanceData.ClaimedHandle);
	InstanceData.SmartObjectActor = IsValid(HandleComponent) ? HandleComponent->GetOwner() : nullptr;
	if (!IsValid(InstanceData.SmartObjectActor))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：无法通过 ClaimedHandle=%s 解析 Smart Object Actor。"),
			*LexToString(InstanceData.ClaimedHandle));
		UE::HMS::UseSmartObjectTask::Private::ReleaseClaim(Subsystem, InstanceData.ClaimedHandle);
		return EStateTreeRunStatus::Failed;
	}

	const UGameplayInteractionSmartObjectBehaviorDefinition* BehaviorDefinition =
		Subsystem->MarkSlotAsOccupied<UGameplayInteractionSmartObjectBehaviorDefinition>(
			InstanceData.ClaimedHandle);
	if (!IsValid(BehaviorDefinition))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：无法把插槽切换为 Occupied，或插槽没有 Gameplay Interaction Behavior Definition。"));
		UE::HMS::UseSmartObjectTask::Private::ReleaseClaim(Subsystem, InstanceData.ClaimedHandle);
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.InteractionContext.SetContextActor(UserActor);
	InstanceData.InteractionContext.SetSmartObjectActor(InstanceData.SmartObjectActor);
	InstanceData.InteractionContext.SetClaimedHandle(InstanceData.ClaimedHandle);
	InstanceData.InteractionContext.SetAbortContext(FGameplayInteractionAbortContext());
	InstanceData.bInteractionActive = InstanceData.InteractionContext.Activate(*BehaviorDefinition);
	if (!InstanceData.bInteractionActive)
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 启动失败：Gameplay Interaction Context 无法激活。请检查长椅 Behavior Definition 的 StateTree Schema 和上下文要求。"));
		UE::HMS::UseSmartObjectTask::Private::ReleaseClaim(Subsystem, InstanceData.ClaimedHandle);
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHMS_UseSmartObjectTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bInteractionActive)
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 运行失败：交互上下文未处于激活状态。"));
		return EStateTreeRunStatus::Failed;
	}

	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!IsValid(Subsystem) || !Subsystem->IsClaimedSmartObjectValid(InstanceData.ClaimedHandle))
	{
		UE_LOG(LogHMSUseSmartObjectTask, Warning,
			TEXT("[UseSmartObject] 运行失败：交互期间 Smart Object 或占用句柄失效。ClaimedHandle=%s。"),
			*LexToString(InstanceData.ClaimedHandle));
		UE::HMS::UseSmartObjectTask::Private::AbortInteraction(InstanceData);
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.InteractionContext.Tick(DeltaTime))
	{
		return EStateTreeRunStatus::Running;
	}

	const EStateTreeRunStatus InteractionStatus = InstanceData.InteractionContext.GetLastRunStatus();
	InstanceData.InteractionContext.Deactivate();
	InstanceData.bInteractionActive = false;
	UE::HMS::UseSmartObjectTask::Private::ReleaseClaim(Subsystem, InstanceData.ClaimedHandle);

	const bool bSucceeded = InteractionStatus == EStateTreeRunStatus::Succeeded;

	return bSucceeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

void FHMS_UseSmartObjectTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bInteractionActive)
	{
		return;
	}

	UE::HMS::UseSmartObjectTask::Private::AbortInteraction(InstanceData);
	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	UE::HMS::UseSmartObjectTask::Private::ReleaseClaim(Subsystem, InstanceData.ClaimedHandle);
}
