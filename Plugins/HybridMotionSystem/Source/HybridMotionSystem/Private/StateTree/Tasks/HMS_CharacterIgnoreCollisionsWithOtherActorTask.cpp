// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_CharacterIgnoreCollisionsWithOtherActorTask.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_CharacterIgnoreCollisionsWithOtherActorTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSIgnoreActorCollisionTask, Log, All);

namespace UE::HMS::IgnoreActorCollisionTask::Private
{
	AActor* ResolveCharacterActor(const FStateTreeExecutionContext& Context, AActor* ExplicitActor)
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

}

FHMS_CharacterIgnoreCollisionsWithOtherActorTask::FHMS_CharacterIgnoreCollisionsWithOtherActorTask()
{
	// 与蓝图的 FinishTask(true) 相同：进入时执行一次，不需要 Tick。
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FHMS_CharacterIgnoreCollisionsWithOtherActorTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AActor* CharacterActor = UE::HMS::IgnoreActorCollisionTask::Private::ResolveCharacterActor(
		Context, InstanceData.CharacterActor);
	if (!IsValid(CharacterActor))
	{
		UE_LOG(LogHMSIgnoreActorCollisionTask, Warning,
			TEXT("[IgnoreActorCollision] 启动失败：无法取得有效的 Character Actor。StateTreeOwner=%s。"),
			*GetNameSafe(Context.GetOwner()));
		return EStateTreeRunStatus::Failed;
	}

	if (!IsValid(InstanceData.OtherActor) || InstanceData.OtherActor == CharacterActor)
	{
		UE_LOG(LogHMSIgnoreActorCollisionTask, Warning,
			TEXT("[IgnoreActorCollision] 启动失败：Other Actor 无效或与 Character Actor 相同。Character=%s，Other=%s。"),
			*GetNameSafe(CharacterActor),
			*GetNameSafe(InstanceData.OtherActor));
		return EStateTreeRunStatus::Failed;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(CharacterActor);
	int32 ChangedComponentCount = 0;
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (!IsValid(Component) || Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		const bool bAlreadyMatches = Component->GetMoveIgnoreActors().Contains(InstanceData.OtherActor)
			== InstanceData.bShouldIgnore;
		if (bAlreadyMatches)
		{
			continue;
		}

		Component->IgnoreActorWhenMoving(InstanceData.OtherActor, InstanceData.bShouldIgnore);
		++ChangedComponentCount;
	}

	if (PrimitiveComponents.IsEmpty())
	{
		UE_LOG(LogHMSIgnoreActorCollisionTask, Warning,
			TEXT("[IgnoreActorCollision] 启动失败：Character=%s 没有 PrimitiveComponent。"),
			*GetNameSafe(CharacterActor));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Succeeded;
}
