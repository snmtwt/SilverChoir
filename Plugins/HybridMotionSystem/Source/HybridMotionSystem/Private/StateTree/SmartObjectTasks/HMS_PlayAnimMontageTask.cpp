// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/SmartObjectTasks/HMS_PlayAnimMontageTask.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "MotionWarpingComponent.h"
#include "MoveLibrary/PlayMoverMontageCallbackProxy.h"
#include "MoverComponent.h"
#include "PlayMontageCallbackProxy.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_PlayAnimMontageTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSPlayAnimMontageTask, Log, All);

namespace UE::HMS::PlayAnimMontageTask::Private
{
	const FName SmartObjectWarpTargetName(TEXT("SmartObject"));

	AActor* ResolveActor(const FStateTreeExecutionContext& Context, AActor* ExplicitActor)
	{
		if (IsValid(ExplicitActor))
		{
			return ExplicitActor;
		}
		if (const AController* Controller = Cast<AController>(Context.GetOwner()))
		{
			return Controller->GetPawn() ? Cast<AActor>(Controller->GetPawn()) : const_cast<AController*>(Controller);
		}
		if (AActor* OwnerActor = Cast<AActor>(Context.GetOwner()))
		{
			return OwnerActor;
		}
		if (const UActorComponent* Component = Cast<UActorComponent>(Context.GetOwner()))
		{
			return Component->GetOwner();
		}
		return nullptr;
	}

	AActor* ResolveSmartObjectActor(UWorld& World, const FSmartObjectSlotHandle SlotHandle)
	{
		if (!SlotHandle.IsValid())
		{
			return nullptr;
		}
		for (TObjectIterator<USmartObjectComponent> It; It; ++It)
		{
			if (It->GetWorld() == &World
				&& It->GetRegisteredHandle() == SlotHandle.GetSmartObjectHandle())
			{
				return It->GetOwner();
			}
		}
		return nullptr;
	}

	void RestoreCollision(FHMS_PlayAnimMontageTaskInstanceData& Data)
	{
		if (IsValid(Data.SmartObjectActor))
		{
			for (UPrimitiveComponent* Component : Data.CollisionComponents)
			{
				if (IsValid(Component))
				{
					Component->IgnoreActorWhenMoving(Data.SmartObjectActor, false);
				}
			}
		}
		Data.CollisionComponents.Reset();
		Data.SmartObjectActor = nullptr;
	}

	bool StartPlayback(FHMS_PlayAnimMontageTaskInstanceData& Data, const float StartTime)
	{
		USkeletalMeshComponent* Mesh = IsValid(Data.Actor)
			? Data.Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		if (!IsValid(Mesh) || !IsValid(Data.MontageToPlay))
		{
			return false;
		}

		if (UMoverComponent* Mover = Data.Actor->FindComponentByClass<UMoverComponent>())
		{
			Data.PlaybackProxy = UPlayMoverMontageCallbackProxy::CreateProxyObjectForPlayMoverMontage(
				Mover, Data.MontageToPlay, Data.PlayRate, StartTime, NAME_None, true, -1.0f);
		}
		else
		{
			Data.PlaybackProxy = UPlayMontageCallbackProxy::CreateProxyObjectForPlayMontage(
				Mesh, Data.MontageToPlay, Data.PlayRate, StartTime, NAME_None, true);
		}

		return IsValid(Data.PlaybackProxy) && Mesh->GetAnimInstance()
			&& Mesh->GetAnimInstance()->Montage_IsPlaying(Data.MontageToPlay);
	}

	void StopPlayback(FHMS_PlayAnimMontageTaskInstanceData& Data)
	{
		if (IsValid(Data.Actor) && IsValid(Data.MontageToPlay))
		{
			if (USkeletalMeshComponent* Mesh = Data.Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
				{
					AnimInstance->Montage_Stop(0.2f, Data.MontageToPlay);
				}
			}
		}
		Data.PlaybackProxy = nullptr;
		Data.bPlaybackActive = false;
	}
}

FHMS_PlayAnimMontageTask::FHMS_PlayAnimMontageTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FHMS_PlayAnimMontageTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Actor = UE::HMS::PlayAnimMontageTask::Private::ResolveActor(Context, Data.Actor);
	Data.ElapsedTime = 0.0f;
	Data.TargetPlayDuration = 0.0f;
	Data.bInfinitePlayback = Data.NumLoops < 0;
	Data.bPlaybackActive = false;
	Data.CollisionComponents.Reset();
	Data.SmartObjectActor = nullptr;

	if (!IsValid(Data.Actor) || !IsValid(Data.MontageToPlay) || Data.PlayRate <= 0.0f)
	{
		UE_LOG(LogHMSPlayAnimMontageTask, Warning,
			TEXT("[PlayAnimMontage] 启动失败：Actor、Montage 或 PlayRate 无效。Actor=%s Montage=%s Rate=%.2f。"),
			*GetNameSafe(Data.Actor), *GetNameSafe(Data.MontageToPlay), Data.PlayRate);
		return EStateTreeRunStatus::Failed;
	}

	if (UWorld* World = Data.Actor->GetWorld())
	{
		if (USmartObjectSubsystem* Subsystem = World->GetSubsystem<USmartObjectSubsystem>())
		{
			const TOptional<FTransform> SlotTransform = Subsystem->GetSlotTransform(Data.SlotHandle);
			if (SlotTransform.IsSet())
			{
				if (UMotionWarpingComponent* MotionWarping =
					Data.Actor->FindComponentByClass<UMotionWarpingComponent>())
				{
					MotionWarping->AddOrUpdateWarpTargetFromTransform(
						UE::HMS::PlayAnimMontageTask::Private::SmartObjectWarpTargetName,
						SlotTransform.GetValue());
				}
			}
		}

		if (Data.bIgnoreCollision)
		{
			Data.SmartObjectActor =
				UE::HMS::PlayAnimMontageTask::Private::ResolveSmartObjectActor(*World, Data.SlotHandle);
			if (IsValid(Data.SmartObjectActor))
			{
				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Data.Actor);
				for (UPrimitiveComponent* Component : PrimitiveComponents)
				{
					if (IsValid(Component) && Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
						&& !Component->GetMoveIgnoreActors().Contains(Data.SmartObjectActor))
					{
						Component->IgnoreActorWhenMoving(Data.SmartObjectActor, true);
						Data.CollisionComponents.Add(Component);
					}
				}
			}
		}
	}

	if (Data.PlayTime > 0.0f)
	{
		Data.TargetPlayDuration = FMath::Max(0.01f, Data.PlayTime
			+ FMath::FRandRange(-Data.RandomPlayTimeVariance, Data.RandomPlayTimeVariance));
	}
	else if (!Data.bInfinitePlayback)
	{
		const float SingleDuration =
			FMath::Max(0.01f, Data.MontageToPlay->GetPlayLength() - Data.StartTime) / Data.PlayRate;
		Data.TargetPlayDuration = SingleDuration * FMath::Max(1, Data.NumLoops);
	}

	if (!UE::HMS::PlayAnimMontageTask::Private::StartPlayback(Data, Data.StartTime))
	{
		UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
		UE_LOG(LogHMSPlayAnimMontageTask, Warning,
			TEXT("[PlayAnimMontage] 启动失败：无法播放 Montage=%s。"), *GetNameSafe(Data.MontageToPlay));
		return EStateTreeRunStatus::Failed;
	}

	Data.bPlaybackActive = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHMS_PlayAnimMontageTask::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.bPlaybackActive || !IsValid(Data.Actor) || !IsValid(Data.MontageToPlay))
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.ElapsedTime += DeltaTime;
	if (!Data.bInfinitePlayback && Data.TargetPlayDuration > 0.0f
		&& Data.ElapsedTime >= Data.TargetPlayDuration)
	{
		UE::HMS::PlayAnimMontageTask::Private::StopPlayback(Data);
		UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
		return EStateTreeRunStatus::Succeeded;
	}

	USkeletalMeshComponent* Mesh = Data.Actor->FindComponentByClass<USkeletalMeshComponent>();
	UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInstance))
	{
		UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
		return EStateTreeRunStatus::Failed;
	}

	if (!AnimInstance->Montage_IsPlaying(Data.MontageToPlay))
	{
		if (Data.bInfinitePlayback || Data.ElapsedTime < Data.TargetPlayDuration)
		{
			if (!UE::HMS::PlayAnimMontageTask::Private::StartPlayback(Data, 0.0f))
			{
				UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
				return EStateTreeRunStatus::Failed;
			}
		}
		else
		{
			Data.bPlaybackActive = false;
			UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
			return EStateTreeRunStatus::Succeeded;
		}
	}

	return EStateTreeRunStatus::Running;
}

void FHMS_PlayAnimMontageTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.bPlaybackActive)
	{
		UE::HMS::PlayAnimMontageTask::Private::StopPlayback(Data);
	}
	UE::HMS::PlayAnimMontageTask::Private::RestoreCollision(Data);
}
