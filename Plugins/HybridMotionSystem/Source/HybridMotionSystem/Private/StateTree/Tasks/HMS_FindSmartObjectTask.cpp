// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_FindSmartObjectTask.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"
#include "StructUtils/StructView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_FindSmartObjectTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSFindSmartObjectTask, Log, All);

namespace UE::HMS::FindSmartObjectTask::Private
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
}

FHMS_FindSmartObjectTask::FHMS_FindSmartObjectTask()
{
	// 这是一次性查询任务，不需要 Tick，也不需要在退出状态前重复复制绑定属性。
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FHMS_FindSmartObjectTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// 先清空上一次运行的输出，避免失败时下游误用旧句柄。
	InstanceData.SmartObjectActor = nullptr;
	InstanceData.CandidateSlot = FSmartObjectSlotHandle();

	AActor* UserActor = UE::HMS::FindSmartObjectTask::Private::ResolveUserActor(Context, InstanceData.UserActor);
	if (InstanceData.bUseCharacterLocation && !IsValid(UserActor))
	{
		UE_LOG(LogHMSFindSmartObjectTask, Warning,
			TEXT("[FindSmartObject] 查询失败：角色位置模式需要有效的 UserActor。StateTreeOwner=%s"),
			*GetNameSafe(Context.GetOwner()));
		return EStateTreeRunStatus::Failed;
	}

	const FVector SearchCenter = InstanceData.bUseCharacterLocation
		? UserActor->GetActorLocation()
		: InstanceData.QueryLocation;

	const FVector SafeExtents(
		FMath::Abs(InstanceData.SearchBoxExtents.X),
		FMath::Abs(InstanceData.SearchBoxExtents.Y),
		FMath::Abs(InstanceData.SearchBoxExtents.Z));

	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!IsValid(Subsystem))
	{
		UE_LOG(LogHMSFindSmartObjectTask, Warning,
			TEXT("[FindSmartObject] 查询失败：当前 World=%s 无法取得 SmartObjectSubsystem。"),
			*GetNameSafe(World));
		return EStateTreeRunStatus::Failed;
	}

	FSmartObjectRequestFilter Filter;
	Filter.UserTags = InstanceData.UserTags;
	Filter.ClaimPriority = ESmartObjectClaimPriority::Normal;
	Filter.BehaviorDefinitionClasses.Add(
		UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass());
	Filter.bShouldEvaluateConditions = true;
	Filter.bShouldIncludeClaimedSlots = false;
	Filter.bShouldIncludeDisabledSlots = false;

	const FSmartObjectRequest Request(FBox::BuildAABB(SearchCenter, SafeExtents), Filter);

	TArray<FSmartObjectRequestResult> Candidates;
	const FSmartObjectActorUserData UserData(UserActor);
	if (!Subsystem->FindSmartObjects(Request, Candidates, FConstStructView::Make(UserData)))
	{
		return EStateTreeRunStatus::Failed;
	}

	// 无法取得插槽变换的结果不能参与距离排序，也无法为后续移动提供可靠目标。
	Candidates.RemoveAll([Subsystem](const FSmartObjectRequestResult& Candidate)
	{
		return !Candidate.IsValid() || !Subsystem->GetSlotTransform(Candidate).IsSet();
	});

	if (Candidates.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	int32 SelectedIndex = 0;
	if (InstanceData.SelectionMethod == EHMS_FindSmartObjectSelectionMethod::Random)
	{
		SelectedIndex = FMath::RandRange(0, Candidates.Num() - 1);
	}
	else
	{
		const bool bSelectClosest =
			InstanceData.SelectionMethod == EHMS_FindSmartObjectSelectionMethod::Closest;
		double SelectedDistanceSquared = bSelectClosest
			? TNumericLimits<double>::Max()
			: -1.0;

		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			const FVector SlotLocation = Subsystem->GetSlotTransform(Candidates[CandidateIndex]).GetValue().GetLocation();
			const double DistanceSquared = FVector::DistSquared(SearchCenter, SlotLocation);
			if ((bSelectClosest && DistanceSquared < SelectedDistanceSquared)
				|| (!bSelectClosest && DistanceSquared > SelectedDistanceSquared))
			{
				SelectedIndex = CandidateIndex;
				SelectedDistanceSquared = DistanceSquared;
			}
		}
	}

	const FSmartObjectRequestResult& SearchResult = Candidates[SelectedIndex];
	InstanceData.CandidateSlot = SearchResult.SlotHandle;

	if (USmartObjectComponent* SmartObjectComponent =
		Subsystem->GetSmartObjectComponentByRequestResult(SearchResult))
	{
		InstanceData.SmartObjectActor = SmartObjectComponent->GetOwner();
	}

	return EStateTreeRunStatus::Succeeded;
}
