// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/SmartObjectTasks/HMS_PlayAnimFromBestCostTask.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "HMS_AnimInstance.h"
#include "IObjectChooser.h"
#include "MotionWarpingComponent.h"
#include "MoveLibrary/PlayMoverMontageCallbackProxy.h"
#include "MoverComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "PlayMontageCallbackProxy.h"
#include "ProxyAsset.h"
#include "ProxyTable.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_PlayAnimFromBestCostTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSPlayAnimFromBestCostTask, Log, All);

namespace UE::HMS::PlayAnimFromBestCostTask::Private
{
	constexpr double DestinationErrorTolerance = 10.0;
	const FName SmartObjectWarpTargetName(TEXT("SmartObject"));

	AActor* ResolveActor(const FStateTreeExecutionContext& Context, AActor* ExplicitActor)
	{
		if (IsValid(ExplicitActor))
		{
			return ExplicitActor;
		}

		UObject* Owner = Context.GetOwner();
		if (const AController* Controller = Cast<AController>(Owner))
		{
			return Controller->GetPawn() ? Cast<AActor>(Controller->GetPawn()) : const_cast<AController*>(Controller);
		}
		if (AActor* ActorOwner = Cast<AActor>(Owner))
		{
			return ActorOwner;
		}
		if (const UActorComponent* ComponentOwner = Cast<UActorComponent>(Owner))
		{
			return ComponentOwner->GetOwner();
		}
		return nullptr;
	}

	double ResolveRemainingPathDistance(const AActor& Actor, const FVector Destination)
	{
		const APawn* Pawn = Cast<APawn>(&Actor);
		const AAIController* AIController = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		if (AIController)
		{
			if (const UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent())
			{
				const FNavPathSharedPtr CurrentPath = PathFollowing->GetPath();
				if (CurrentPath.IsValid())
				{
					const double PathLength = CurrentPath->GetLength();
					if (FMath::IsFinite(PathLength) && PathLength >= 0.0)
					{
						return PathLength;
					}
				}
			}
		}

		return FVector::Dist(Actor.GetActorLocation(), Destination);
	}

	double ResolveApproachAngle(const AActor& Actor, const FVector Destination)
	{
		const FVector ToDestination = (Destination - Actor.GetActorLocation()).GetSafeNormal2D();
		if (ToDestination.IsNearlyZero())
		{
			return 0.0;
		}

		return FMath::FindDeltaAngleDegrees(
			Actor.GetActorForwardVector().Rotation().Yaw,
			ToDestination.Rotation().Yaw);
	}

	UAnimMontage* EvaluateAnimation(
		FHMS_PlayAnimFromBestCostTaskInstanceData& InstanceData,
		const double RemainingDistance,
		const double ApproachAngle,
		FHMS_SmartObjectSelectionOutputs& OutOutputs)
	{
		if (!IsValid(InstanceData.AnimationProxyTable))
		{
			return nullptr;
		}

		// 样例节点不暴露 Proxy Asset。通常每个 Smart Object 动画 Proxy Table
		// 只包含一个入口，因此从表的 RuntimeValues 中自动取得它。
		const UProxyAsset* AnimationProxyAsset = nullptr;
		for (const FRuntimeProxyValue& RuntimeValue : InstanceData.AnimationProxyTable->RuntimeValues)
		{
			if (IsValid(RuntimeValue.ProxyAsset)
				&& (!IsValid(RuntimeValue.ProxyAsset->Type)
					|| RuntimeValue.ProxyAsset->Type->IsChildOf(UAnimMontage::StaticClass())))
			{
				AnimationProxyAsset = RuntimeValue.ProxyAsset;
				break;
			}
		}

		if (!IsValid(AnimationProxyAsset))
		{
			return nullptr;
		}

		FHMS_SmartObjectSelectionInputs Inputs;
		UHMS_AnimInstance* HMSAnimInstance = nullptr;
		if (IsValid(InstanceData.Actor))
		{
			if (USkeletalMeshComponent* Mesh =
				InstanceData.Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				HMSAnimInstance = Cast<UHMS_AnimInstance>(Mesh->GetAnimInstance());
				if (IsValid(HMSAnimInstance))
				{
					Inputs.PoseHistoryNode = HMSAnimInstance->GetHMSPoseHistory();
				}
			}
		}
		if (!IsValid(HMSAnimInstance))
		{
			return nullptr;
		}
		Inputs.TargetDistance = RemainingDistance;
		Inputs.TargetAngle = ApproachAngle;
		OutOutputs = FHMS_SmartObjectSelectionOutputs();

		FChooserEvaluationContext EvaluationContext;
		// CHT_HMS_SmartObject_BenchAnim 的第一个 Context Data 是 UHMS_AnimInstance。
		// Pawn 只负责提供移动/交互上下文，Chooser 与 Pose Search 需要动画实例本身。
		EvaluationContext.AddObjectParam(HMSAnimInstance);
		EvaluationContext.AddStructParam(Inputs);
		EvaluationContext.AddStructParam(OutOutputs);

		UObject* Result = InstanceData.AnimationProxyTable->FindProxyObject(
			AnimationProxyAsset->Guid, EvaluationContext);
		return Cast<UAnimMontage>(Result);
	}

	USkeletalMeshComponent* ResolveMesh(AActor& Actor)
	{
		return Actor.FindComponentByClass<USkeletalMeshComponent>();
	}

	bool StartPlayback(FHMS_PlayAnimFromBestCostTaskInstanceData& InstanceData)
	{
		AActor* Actor = InstanceData.Actor;
		USkeletalMeshComponent* Mesh = IsValid(Actor) ? ResolveMesh(*Actor) : nullptr;
		if (!IsValid(Mesh) || !IsValid(InstanceData.SelectedMontage))
		{
			return false;
		}

		if (UMotionWarpingComponent* MotionWarping = Actor->FindComponentByClass<UMotionWarpingComponent>())
		{
			MotionWarping->AddOrUpdateWarpTargetFromTransform(
				SmartObjectWarpTargetName, InstanceData.Destination);
		}

		if (UMoverComponent* Mover = Actor->FindComponentByClass<UMoverComponent>())
		{
			InstanceData.PlaybackProxy = UPlayMoverMontageCallbackProxy::CreateProxyObjectForPlayMoverMontage(
				Mover,
				InstanceData.SelectedMontage,
				1.0f,
				static_cast<float>(FMath::Max(0.0, InstanceData.SelectedStartTime)),
				NAME_None,
				true,
				-1.0f);
		}
		else
		{
			InstanceData.PlaybackProxy = UPlayMontageCallbackProxy::CreateProxyObjectForPlayMontage(
				Mesh,
				InstanceData.SelectedMontage,
				1.0f,
				static_cast<float>(FMath::Max(0.0, InstanceData.SelectedStartTime)),
				NAME_None,
				true);
		}

		return IsValid(InstanceData.PlaybackProxy)
			&& Mesh->GetAnimInstance()
			&& Mesh->GetAnimInstance()->Montage_IsPlaying(InstanceData.SelectedMontage);
	}

	void StopPlayback(FHMS_PlayAnimFromBestCostTaskInstanceData& InstanceData)
	{
		if (!InstanceData.bPlaybackStarted || !IsValid(InstanceData.Actor)
			|| !IsValid(InstanceData.SelectedMontage))
		{
			return;
		}

		if (USkeletalMeshComponent* Mesh = ResolveMesh(*InstanceData.Actor))
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.2f, InstanceData.SelectedMontage);
			}
		}
	}
}

FHMS_PlayAnimFromBestCostTask::FHMS_PlayAnimFromBestCostTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FHMS_PlayAnimFromBestCostTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.Actor = UE::HMS::PlayAnimFromBestCostTask::Private::ResolveActor(Context, InstanceData.Actor);
	InstanceData.SelectedMontage = nullptr;
	InstanceData.PlaybackProxy = nullptr;
	InstanceData.SelectedCost = TNumericLimits<double>::Max();
	InstanceData.SelectedStartTime = 0.0;
	InstanceData.bPlaybackStarted = false;

	if (!IsValid(InstanceData.Actor) || !InstanceData.ClaimedHandle.IsValid()
		|| !IsValid(InstanceData.AnimationProxyTable))
	{
		UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
			TEXT("[PlayAnimFromBestCost] 启动失败：输入无效。Actor=%s，ProxyTable=%s，ClaimedHandle=%s。"),
			*GetNameSafe(InstanceData.Actor),
			*GetNameSafe(InstanceData.AnimationProxyTable),
			*LexToString(InstanceData.ClaimedHandle));
		return EStateTreeRunStatus::Failed;
	}

	USkeletalMeshComponent* Mesh =
		InstanceData.Actor->FindComponentByClass<USkeletalMeshComponent>();
	if (!IsValid(Mesh) || !IsValid(Cast<UHMS_AnimInstance>(Mesh->GetAnimInstance())))
	{
		UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
			TEXT("[PlayAnimFromBestCost] 启动失败：Actor=%s 的 Skeletal Mesh 没有使用 UHMS_AnimInstance。Mesh=%s，AnimInstance=%s。"),
			*GetNameSafe(InstanceData.Actor),
			*GetNameSafe(Mesh),
			*GetNameSafe(IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr));
		return EStateTreeRunStatus::Failed;
	}

	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!IsValid(Subsystem) || !Subsystem->IsClaimedSmartObjectValid(InstanceData.ClaimedHandle))
	{
		UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
			TEXT("[PlayAnimFromBestCost] 启动失败：Claimed Handle 已失效。"));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHMS_PlayAnimFromBestCostTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!IsValid(InstanceData.Actor))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.bPlaybackStarted)
	{
		USkeletalMeshComponent* Mesh =
			UE::HMS::PlayAnimFromBestCostTask::Private::ResolveMesh(*InstanceData.Actor);
		UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
		if (!IsValid(AnimInstance))
		{
			UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
				TEXT("[PlayAnimFromBestCost] 播放失败：动画期间 AnimInstance 失效。"));
			return EStateTreeRunStatus::Failed;
		}

		if (AnimInstance->Montage_IsPlaying(InstanceData.SelectedMontage))
		{
			return EStateTreeRunStatus::Running;
		}

		InstanceData.PlaybackProxy = nullptr;
		InstanceData.bPlaybackStarted = false;
		return EStateTreeRunStatus::Succeeded;
	}

	const FVector DestinationLocation = InstanceData.Destination.GetLocation();
	const double RemainingDistance =
		UE::HMS::PlayAnimFromBestCostTask::Private::ResolveRemainingPathDistance(
			*InstanceData.Actor, DestinationLocation);
	const double Speed = InstanceData.Actor->GetVelocity().Size2D();
	const bool bReachedDestination = RemainingDistance
		<= UE::HMS::PlayAnimFromBestCostTask::Private::DestinationErrorTolerance;
	if (RemainingDistance > InstanceData.MaximumDistanceThreshold)
	{
		return EStateTreeRunStatus::Running;
	}

	if (Speed < InstanceData.MinimumVelocityCheck && !bReachedDestination)
	{
		return EStateTreeRunStatus::Running;
	}

	const double ApproachAngle =
		UE::HMS::PlayAnimFromBestCostTask::Private::ResolveApproachAngle(
			*InstanceData.Actor, DestinationLocation);
	FHMS_SmartObjectSelectionOutputs Outputs;
	UAnimMontage* Candidate = UE::HMS::PlayAnimFromBestCostTask::Private::EvaluateAnimation(
		InstanceData, RemainingDistance, ApproachAngle, Outputs);
	if (IsValid(Candidate) && Outputs.Cost < InstanceData.SelectedCost)
	{
		InstanceData.SelectedMontage = Candidate;
		InstanceData.SelectedCost = Outputs.Cost;
		InstanceData.SelectedStartTime = Outputs.StartTime;
	}

	const bool bDedicatedServer = IsRunningDedicatedServer();
	const bool bCostAccepted = IsValid(InstanceData.SelectedMontage)
		&& InstanceData.SelectedCost <= InstanceData.CostThreshold;
	if (!IsValid(InstanceData.SelectedMontage)
		|| (!bDedicatedServer && !bCostAccepted && !bReachedDestination))
	{
		if (bReachedDestination && !IsValid(InstanceData.SelectedMontage))
		{
			UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
				TEXT("[PlayAnimFromBestCost] 评分失败：角色已到达 Destination，但 Proxy Table 没有返回 Montage。"));
			return EStateTreeRunStatus::Failed;
		}
		return EStateTreeRunStatus::Running;
	}

	if (!UE::HMS::PlayAnimFromBestCostTask::Private::StartPlayback(InstanceData))
	{
		UE_LOG(LogHMSPlayAnimFromBestCostTask, Warning,
			TEXT("[PlayAnimFromBestCost] 播放失败：无法启动 Montage=%s。请检查 Mesh、AnimInstance、Mover 和 Montage Slot。"),
			*GetNameSafe(InstanceData.SelectedMontage));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bPlaybackStarted = true;
	return EStateTreeRunStatus::Running;
}

void FHMS_PlayAnimFromBestCostTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.bPlaybackStarted)
	{
		UE::HMS::PlayAnimFromBestCostTask::Private::StopPlayback(InstanceData);
	}

	InstanceData.PlaybackProxy = nullptr;
	InstanceData.bPlaybackStarted = false;
}
