// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/HMS_ClaimSmartObjectSlotTask.h"

#include "AIController.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "GameplayTagAssetInterface.h"
#include "SmartObjectBlueprintFunctionLibrary.h"
#include "SmartObjectComponent.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"
#include "StructUtils/StructView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HMS_ClaimSmartObjectSlotTask)

DEFINE_LOG_CATEGORY_STATIC(LogHMSClaimSmartObjectSlotTask, Log, All);

namespace UE::HMS::ClaimSmartObjectSlotTask::Private
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

	FGameplayTagContainer ResolveUserTags(const AActor& UserActor)
	{
		FGameplayTagContainer UserTags;
		if (const IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(&UserActor))
		{
			TagInterface->GetOwnedGameplayTags(UserTags);
		}

		// Game Animation Sample 的 NPC Smart Object 使用此标签作为用户条件。
		const FGameplayTag NPCUserTag = FGameplayTag::RequestGameplayTag(
			TEXT("SmartObject.ObjectType.NPC"), false);
		if (NPCUserTag.IsValid())
		{
			UserTags.AddTag(NPCUserTag);
		}

		return UserTags;
	}
}

FHMS_ClaimSmartObjectSlotTask::FHMS_ClaimSmartObjectSlotTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FHMS_ClaimSmartObjectSlotTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ClaimedHandle.Invalidate();

	AActor* UserActor = UE::HMS::ClaimSmartObjectSlotTask::Private::ResolveUserActor(
		Context, InstanceData.UserActor);
	if (!IsValid(UserActor))
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：无法取得有效的 UserActor。StateTreeOwner=%s"),
			*GetNameSafe(Context.GetOwner()));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.SlotToBeClaimed.IsValid())
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：Slot To Be Claimed 句柄无效。UserActor=%s"),
			*GetNameSafe(UserActor));
		return EStateTreeRunStatus::Failed;
	}

	if (!IsValid(InstanceData.SmartObject))
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：Smart Object Actor 无效。请绑定 Find Smart Object 的 Smart Object 输出。"));
		return EStateTreeRunStatus::Failed;
	}

	UWorld* World = Context.GetWorld();
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!IsValid(Subsystem))
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：当前 World=%s 无法取得 SmartObjectSubsystem。"),
			*GetNameSafe(World));
		return EStateTreeRunStatus::Failed;
	}

	USmartObjectComponent* SmartObjectComponent =
		InstanceData.SmartObject->FindComponentByClass<USmartObjectComponent>();
	if (!IsValid(SmartObjectComponent))
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：Smart Object Actor=%s 上没有 SmartObjectComponent。"),
			*GetNameSafe(InstanceData.SmartObject));
		return EStateTreeRunStatus::Failed;
	}

	const FSmartObjectRequestResult OriginalResult(
		InstanceData.SlotToBeClaimed.GetSmartObjectHandle(),
		InstanceData.SlotToBeClaimed);
	if (USmartObjectComponent* SlotOwnerComponent =
		Subsystem->GetSmartObjectComponentByRequestResult(OriginalResult);
		IsValid(SlotOwnerComponent) && SlotOwnerComponent != SmartObjectComponent)
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：Smart Object=%s 与 Slot=%s 不属于同一个 SmartObjectComponent。"),
			*GetNameSafe(InstanceData.SmartObject),
			*LexToString(InstanceData.SlotToBeClaimed));
		return EStateTreeRunStatus::Failed;
	}

	const FSmartObjectActorUserData UserData(UserActor);
	const FConstStructView UserDataView = FConstStructView::Make(UserData);
	constexpr ESmartObjectClaimPriority ClaimPriority = ESmartObjectClaimPriority::Normal;

	// Find 和 Claim 之间可能发生竞争，先尝试原始候选。
	InstanceData.ClaimedHandle = Subsystem->MarkSlotAsClaimed(
		InstanceData.SlotToBeClaimed, ClaimPriority, UserDataView);
	if (InstanceData.ClaimedHandle.IsValid())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	FSmartObjectRequestFilter Filter;
	Filter.UserTags = UE::HMS::ClaimSmartObjectSlotTask::Private::ResolveUserTags(*UserActor);
	Filter.ClaimPriority = ClaimPriority;
	Filter.BehaviorDefinitionClasses.Add(
		UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass());
	Filter.bShouldEvaluateConditions = true;
	Filter.bShouldIncludeClaimedSlots = false;
	Filter.bShouldIncludeDisabledSlots = false;

	TArray<FSmartObjectRequestResult> AlternativeCandidates;
	if (!USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInComponent(
		Filter, SmartObjectComponent, AlternativeCandidates, UserActor))
	{
		UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
			TEXT("[ClaimSmartObjectSlot] 占用失败：%s 上没有满足条件的备用插槽。"),
			*GetNameSafe(SmartObjectComponent->GetOwner()));
		return EStateTreeRunStatus::Failed;
	}

	for (const FSmartObjectRequestResult& Alternative : AlternativeCandidates)
	{
		if (!Alternative.IsValid() || Alternative.SlotHandle == InstanceData.SlotToBeClaimed)
		{
			continue;
		}

		InstanceData.ClaimedHandle = Subsystem->MarkSlotAsClaimed(
			Alternative.SlotHandle, ClaimPriority, UserDataView);
		if (InstanceData.ClaimedHandle.IsValid())
		{
			return EStateTreeRunStatus::Succeeded;
		}
	}

	UE_LOG(LogHMSClaimSmartObjectSlotTask, Warning,
		TEXT("[ClaimSmartObjectSlot] 占用失败：所有备用插槽也在竞争过程中变得不可用。"));
	return EStateTreeRunStatus::Failed;
}
