// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "Animation/HMS_AnimationQueryLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "SmartObject/HMS_SmartObjectInteractionProfile.h"

#include "AIController.h"
#include "Algo/RandomShuffle.h"
#include "GameFramework/Pawn.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"

DEFINE_LOG_CATEGORY_STATIC(LogHMSSmartObjectInteraction, Log, All);

UHMS_SmartObjectInteractionComponent::UHMS_SmartObjectInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UHMS_SmartObjectInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAddDefaultNPCUserTag && UserTags.IsEmpty())
	{
		const FGameplayTag NPCUserTag = FGameplayTag::RequestGameplayTag(
			TEXT("SmartObject.ObjectType.NPC"), false);
		if (NPCUserTag.IsValid())
		{
			UserTags.AddTag(NPCUserTag);
		}
		else
		{
			UE_LOG(LogHMSSmartObjectInteraction, Warning,
				TEXT("%s could not resolve gameplay tag SmartObject.ObjectType.NPC."),
				*GetNameSafe(GetOwner()));
		}
	}

	const UWorld* World = GetWorld();
	NextAutomaticSearchTime = World ? World->GetTimeSeconds() + InitialSearchDelay : 0.0;
}

void UHMS_SmartObjectInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupProfileInteraction();
	if (bInteractionContextActive)
	{
		GameplayInteractionContext.SetAbortContext(
			FGameplayInteractionAbortContext(EGameplayInteractionAbortReason::ExternalAbort));
		GameplayInteractionContext.Deactivate();
		bInteractionContextActive = false;
	}
	ReleaseClaim(true);
	SetInteractionState(EHMS_SmartObjectInteractionState::Idle);
	Super::EndPlay(EndPlayReason);
}

void UHMS_SmartObjectInteractionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bInteractionContextActive)
	{
		if (!GameplayInteractionContext.Tick(DeltaTime))
		{
			FinishInteraction(GameplayInteractionContext.GetLastRunStatus() == EStateTreeRunStatus::Succeeded);
		}
		return;
	}
	if (ActiveProfile)
	{
		TickProfileInteraction(DeltaTime);
		return;
	}

	if (InteractionState == EHMS_SmartObjectInteractionState::Interacting)
	{
		if (!bInteractionContextActive || !GameplayInteractionContext.Tick(DeltaTime))
		{
			const bool bSucceeded = bInteractionContextActive
				&& GameplayInteractionContext.GetLastRunStatus() == EStateTreeRunStatus::Succeeded;
			FinishInteraction(bSucceeded);
		}
		return;
	}

	if (!bAutoStart || (!bAutoRepeat && bSingleAutomaticCycleFinished))
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() < NextAutomaticSearchTime)
	{
		return;
	}

	if (InteractionState == EHMS_SmartObjectInteractionState::Cooldown
		|| InteractionState == EHMS_SmartObjectInteractionState::Failed)
	{
		SetInteractionState(EHMS_SmartObjectInteractionState::Idle);
	}

	if (InteractionState == EHMS_SmartObjectInteractionState::Idle)
	{
		FindAndUseSmartObject();
	}
}

bool UHMS_SmartObjectInteractionComponent::FindAndUseSmartObject()
{
	if (IsBusy())
	{
		return false;
	}
	bLastInteractionSucceeded = false;
	LastFailureReason = FText::GetEmpty();

	AActor* UserActor = ResolveUserActor();
	USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	if (!IsValid(UserActor) || !IsValid(Subsystem))
	{
		FailAndScheduleRetry(TEXT("missing user actor or SmartObjectSubsystem"));
		return false;
	}

	if (bRequireAIController && !ResolveAIController())
	{
		FailAndScheduleRetry(TEXT("the selected behavior requires an AIController"));
		return false;
	}

	SetInteractionState(EHMS_SmartObjectInteractionState::Searching);
	if (!ClaimBestCandidate(*Subsystem, *UserActor))
	{
		FailAndScheduleRetry(TEXT("no claimable Gameplay Interaction slot was found"));
		return false;
	}

	SetInteractionState(EHMS_SmartObjectInteractionState::Claimed);
	if (!StartClaimedInteraction(*Subsystem, *UserActor))
	{
		FailAndScheduleRetry(TEXT("the claimed slot could not start its Gameplay Interaction"));
		return false;
	}

	if (!bQueriedEntry) { SetInteractionState(EHMS_SmartObjectInteractionState::Interacting); }
	return true;
}

void UHMS_SmartObjectInteractionComponent::AbortInteraction()
{
	CleanupProfileInteraction();
	bLastInteractionSucceeded = false;
	if (bInteractionContextActive)
	{
		GameplayInteractionContext.SetAbortContext(
			FGameplayInteractionAbortContext(EGameplayInteractionAbortReason::ExternalAbort));
		GameplayInteractionContext.Deactivate();
		bInteractionContextActive = false;
	}

	const bool bHadClaim = ClaimedHandle.IsValid();
	AActor* PreviousSmartObject = ClaimedSmartObjectActor.Get();
	ReleaseClaim(true);
	SetInteractionState(EHMS_SmartObjectInteractionState::Idle);
	NextAutomaticSearchTime = GetWorld() ? GetWorld()->GetTimeSeconds() + FailureRetryDelay : 0.0;
	bSingleAutomaticCycleFinished = !bAutoRepeat;

	if (bHadClaim)
	{
		OnInteractionFinished.Broadcast(false, PreviousSmartObject);
	}
}

void UHMS_SmartObjectInteractionComponent::ResetInteraction()
{
	AbortInteraction();
	NextAutomaticSearchTime = 0.0;
	bSingleAutomaticCycleFinished = false;
}

bool UHMS_SmartObjectInteractionComponent::GetClaimedSlotTransform(FTransform& OutTransform) const
{
	const USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	return ClaimedHandle.IsValid() && Subsystem
		&& Subsystem->GetSlotTransform(ClaimedHandle, OutTransform);
}

FGameplayTagContainer UHMS_SmartObjectInteractionComponent::GetClaimedSlotTags() const
{
	const USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	return ClaimedHandle.IsValid() && Subsystem && Subsystem->IsSmartObjectSlotValid(ClaimedHandle.SlotHandle)
		? Subsystem->GetSlotTags(ClaimedHandle.SlotHandle)
		: FGameplayTagContainer();
}

bool UHMS_SmartObjectInteractionComponent::AddTaggedCooldown(
	const FGameplayTag CooldownTag, const float Duration)
{
	if (!CooldownTag.IsValid() || !GetWorld())
	{
		return false;
	}

	TaggedCooldownEndTimes.FindOrAdd(CooldownTag) =
		GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, Duration);
	return true;
}

bool UHMS_SmartObjectInteractionComponent::IsTaggedCooldownActive(
	const FGameplayTag CooldownTag) const
{
	if (!CooldownTag.IsValid() || !GetWorld())
	{
		return false;
	}

	const double* EndTime = TaggedCooldownEndTimes.Find(CooldownTag);
	return EndTime && *EndTime > GetWorld()->GetTimeSeconds();
}

float UHMS_SmartObjectInteractionComponent::GetTaggedCooldownRemaining(
	const FGameplayTag CooldownTag) const
{
	if (!CooldownTag.IsValid() || !GetWorld())
	{
		return 0.0f;
	}

	const double* EndTime = TaggedCooldownEndTimes.Find(CooldownTag);
	return EndTime
		? static_cast<float>(FMath::Max(0.0, *EndTime - GetWorld()->GetTimeSeconds()))
		: 0.0f;
}

AActor* UHMS_SmartObjectInteractionComponent::ResolveUserActor() const
{
	if (const AController* Controller = Cast<AController>(GetOwner()))
	{
		return Controller->GetPawn() ? Cast<AActor>(Controller->GetPawn()) : const_cast<AController*>(Controller);
	}

	return GetOwner();
}

AAIController* UHMS_SmartObjectInteractionComponent::ResolveAIController() const
{
	if (AAIController* Controller = Cast<AAIController>(GetOwner()))
	{
		return Controller;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
}

USmartObjectSubsystem* UHMS_SmartObjectInteractionComponent::ResolveSubsystem() const
{
	return GetWorld() ? USmartObjectSubsystem::GetCurrent(GetWorld()) : nullptr;
}

bool UHMS_SmartObjectInteractionComponent::ClaimBestCandidate(
	USmartObjectSubsystem& Subsystem,
	AActor& UserActor)
{
	FSmartObjectRequestFilter Filter;
	Filter.UserTags = UserTags;
	if (bIncludeAnimationContextInUserTags)
	{
		const auto* Mesh = UserActor.FindComponentByClass<USkeletalMeshComponent>();
		const auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		for (FGameplayTag Tag : {UHMS_AnimationQueryLibrary::GetEquippedWeaponType(Anim),
			UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(Anim)})
		{ if (Tag.IsValid()) { Filter.UserTags.AddTag(Tag); } }
	}
	Filter.ClaimPriority = ClaimPriority;
	Filter.BehaviorDefinitionClasses.Add(
		UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass());
	Filter.bShouldEvaluateConditions = true;
	Filter.bShouldIncludeClaimedSlots = false;
	Filter.bShouldIncludeDisabledSlots = false;

	const FVector SafeExtents(
		FMath::Abs(SearchBoxExtents.X),
		FMath::Abs(SearchBoxExtents.Y),
		FMath::Abs(SearchBoxExtents.Z));
	const FSmartObjectRequest Request(
		FBox::BuildAABB(UserActor.GetActorLocation(), SafeExtents), Filter);

	const FSmartObjectActorUserData UserData(&UserActor);
	TArray<FSmartObjectRequestResult> Candidates;
	if (!Subsystem.FindSmartObjects(Request, Candidates, FConstStructView::Make(UserData)))
	{
		return false;
	}

	const FVector UserLocation = UserActor.GetActorLocation();
	Candidates.RemoveAll([&Subsystem, this](const FSmartObjectRequestResult& Candidate)
	{
		const FName ActorTag = ActiveProfile ? ActiveProfile->RequiredActorTag : RequiredActorTag;
		if (!ActorTag.IsNone())
		{
			const USmartObjectComponent* Component = Subsystem.GetSmartObjectComponentByRequestResult(Candidate);
			if (!Component || !Component->GetOwner()->ActorHasTag(ActorTag))
			{
				return true;
			}
		}
		return !Candidate.IsValid()
			|| !Subsystem.GetBehaviorDefinition<UGameplayInteractionSmartObjectBehaviorDefinition>(Candidate);
	});

	if (Candidates.IsEmpty())
	{
		return false;
	}

	if (SelectionMethod == EHMS_SmartObjectSelectionMethod::Random)
	{
		Algo::RandomShuffle(Candidates);
	}
	else
	{
		Candidates.Sort([&Subsystem, UserLocation, this](
			const FSmartObjectRequestResult& Left,
			const FSmartObjectRequestResult& Right)
		{
			const TOptional<FVector> LeftLocation = Subsystem.GetSlotLocation(Left);
			const TOptional<FVector> RightLocation = Subsystem.GetSlotLocation(Right);
			const double LeftDistance = LeftLocation.IsSet()
				? FVector::DistSquared(UserLocation, LeftLocation.GetValue())
				: TNumericLimits<double>::Max();
			const double RightDistance = RightLocation.IsSet()
				? FVector::DistSquared(UserLocation, RightLocation.GetValue())
				: TNumericLimits<double>::Max();
			return SelectionMethod == EHMS_SmartObjectSelectionMethod::Closest
				? LeftDistance < RightDistance
				: LeftDistance > RightDistance;
		});
	}

	for (const FSmartObjectRequestResult& Candidate : Candidates)
	{
		ClaimedHandle = Subsystem.MarkSlotAsClaimed(
			Candidate.SlotHandle, ClaimPriority, FConstStructView::Make(UserData));
		if (!ClaimedHandle.IsValid())
		{
			continue;
		}

		if (USmartObjectComponent* SmartObjectComponent = Subsystem.GetSmartObjectComponent(ClaimedHandle))
		{
			ClaimedSmartObjectActor = SmartObjectComponent->GetOwner();
		}

		Subsystem.RegisterSlotInvalidationCallback(
			ClaimedHandle,
			FOnSlotInvalidated::CreateUObject(
				this, &UHMS_SmartObjectInteractionComponent::HandleSlotInvalidated));

		FTransform SlotTransform = FTransform::Identity;
		Subsystem.GetSlotTransform(ClaimedHandle, SlotTransform);
		OnSlotClaimed.Broadcast(ClaimedSmartObjectActor.Get(), SlotTransform);
		return true;
	}

	return false;
}

bool UHMS_SmartObjectInteractionComponent::StartClaimedInteraction(
	USmartObjectSubsystem& Subsystem,
	AActor& UserActor)
{
	if (!ClaimedHandle.IsValid())
	{
		return false;
	}

	const UGameplayInteractionSmartObjectBehaviorDefinition* BehaviorDefinition =
		Subsystem.MarkSlotAsOccupied<UGameplayInteractionSmartObjectBehaviorDefinition>(ClaimedHandle);
	if (!BehaviorDefinition)
	{
		ReleaseClaim(true);
		return false;
	}

	if (!ClaimedSmartObjectActor.IsValid())
	{
		if (const USmartObjectComponent* SmartObjectComponent = Subsystem.GetSmartObjectComponent(ClaimedHandle))
		{
			ClaimedSmartObjectActor = SmartObjectComponent->GetOwner();
		}
	}

	if (!ClaimedSmartObjectActor.IsValid())
	{
		ReleaseClaim(true);
		return false;
	}

	GameplayInteractionContext.SetContextActor(&UserActor);
	GameplayInteractionContext.SetSmartObjectActor(ClaimedSmartObjectActor.Get());
	GameplayInteractionContext.SetClaimedHandle(ClaimedHandle);
	GameplayInteractionContext.SetAbortContext(FGameplayInteractionAbortContext());
	bInteractionContextActive = GameplayInteractionContext.Activate(*BehaviorDefinition);
	return bInteractionContextActive;
}

void UHMS_SmartObjectInteractionComponent::FinishInteraction(const bool bSucceeded)
{
	CleanupProfileInteraction();
	bLastInteractionSucceeded = bSucceeded;
	AActor* PreviousSmartObject = ClaimedSmartObjectActor.Get();
	if (bInteractionContextActive)
	{
		GameplayInteractionContext.Deactivate();
		bInteractionContextActive = false;
	}

	ReleaseClaim(true);
	OnInteractionFinished.Broadcast(bSucceeded, PreviousSmartObject);

	if (bAutoRepeat)
	{
		EnterCooldown(bSucceeded ? SuccessCooldown : FailureRetryDelay);
	}
	else
	{
		bSingleAutomaticCycleFinished = true;
		SetInteractionState(bSucceeded
			? EHMS_SmartObjectInteractionState::Idle
			: EHMS_SmartObjectInteractionState::Failed);
	}
}

void UHMS_SmartObjectInteractionComponent::ReleaseClaim(const bool bMarkSlotFree)
{
	if (!ClaimedHandle.IsValid())
	{
		ClaimedSmartObjectActor.Reset();
		return;
	}

	if (USmartObjectSubsystem* Subsystem = ResolveSubsystem())
	{
		Subsystem->UnregisterSlotInvalidationCallback(ClaimedHandle);
		if (bMarkSlotFree && Subsystem->IsClaimedSmartObjectValid(ClaimedHandle))
		{
			Subsystem->MarkSlotAsFree(ClaimedHandle);
		}
	}

	ClaimedHandle.Invalidate();
	ClaimedSmartObjectActor.Reset();
}

void UHMS_SmartObjectInteractionComponent::EnterCooldown(const float Duration)
{
	SetInteractionState(EHMS_SmartObjectInteractionState::Cooldown);
	NextAutomaticSearchTime = GetWorld() ? GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, Duration) : 0.0;
}

void UHMS_SmartObjectInteractionComponent::SetInteractionState(
	const EHMS_SmartObjectInteractionState NewState)
{
	if (InteractionState == NewState)
	{
		return;
	}

	const EHMS_SmartObjectInteractionState PreviousState = InteractionState;
	InteractionState = NewState;
	if (NewState == EHMS_SmartObjectInteractionState::Holding && ActiveProfile && ActiveProfile->bCrouchOnHold)
	{
		if (AActor* User = ResolveUserActor())
		{
			if (auto* Mover = User->FindComponentByClass<UCharacterMoverComponent>())
			{
				bRestoreStandingAfterHold = !Mover->GetCrouchIntent();
				Mover->Crouch();
			}
		}
	}
	OnInteractionStateChanged.Broadcast(PreviousState, NewState);
}

void UHMS_SmartObjectInteractionComponent::FailAndScheduleRetry(const TCHAR* Reason)
{
	LastFailureReason = FText::FromString(Reason);
	bLastInteractionSucceeded = false;
	CleanupProfileInteraction();
	UE_LOG(LogHMSSmartObjectInteraction, Warning, TEXT("%s: %s"), *GetNameSafe(GetOwner()), Reason);

	if (bInteractionContextActive)
	{
		GameplayInteractionContext.SetAbortContext(
			FGameplayInteractionAbortContext(EGameplayInteractionAbortReason::InternalAbort));
		GameplayInteractionContext.Deactivate();
		bInteractionContextActive = false;
	}
	ReleaseClaim(true);
	SetInteractionState(EHMS_SmartObjectInteractionState::Failed);
	NextAutomaticSearchTime = GetWorld() ? GetWorld()->GetTimeSeconds() + FailureRetryDelay : 0.0;
	bSingleAutomaticCycleFinished = !bAutoRepeat;
}

void UHMS_SmartObjectInteractionComponent::HandleSlotInvalidated(
	const FSmartObjectClaimHandle& InvalidatedHandle,
	const ESmartObjectSlotState SlotState)
{
	if (InvalidatedHandle != ClaimedHandle)
	{
		return;
	}

	FailAndScheduleRetry(TEXT("the Smart Object slot was invalidated"));
}
