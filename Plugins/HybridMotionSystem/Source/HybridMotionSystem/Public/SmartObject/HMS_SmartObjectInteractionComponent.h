// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayInteractionContext.h"
#include "GameplayTagContainer.h"
#include "SmartObjectTypes.h"
#include "HMS_SmartObjectInteractionComponent.generated.h"

class AAIController;
class UAnimMontage;
class USmartObjectSubsystem;
class UHMS_SmartObjectInteractionProfile;
class UPlayMontageCallbackProxy;
class UPrimitiveComponent;
class URootMotionModifier;

/** How a usable Smart Object slot is selected from the search results. */
UENUM(BlueprintType)
enum class EHMS_SmartObjectSelectionMethod : uint8
{
	Closest,
	Farthest,
	Random
};

/** Runtime lifecycle of one HMS Smart Object interaction. */
UENUM(BlueprintType)
enum class EHMS_SmartObjectInteractionState : uint8
{
	Idle,
	Searching,
	Claimed,
	Interacting,
	Cooldown,
	Failed,
	Approaching,
	Aligning,
	Holding
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FHMS_SmartObjectInteractionStateChanged,
	EHMS_SmartObjectInteractionState, PreviousState,
	EHMS_SmartObjectInteractionState, NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FHMS_SmartObjectInteractionFinished,
	bool, bSucceeded,
	AActor*, SmartObjectActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FHMS_SmartObjectSlotClaimed,
	AActor*, SmartObjectActor,
	FTransform, SlotTransform);

/**
 * Componentized Smart Object search/claim/use pipeline for HMS characters.
 *
 * The component deliberately owns the claim and the FGameplayInteractionContext.
 * The selected Smart Object still owns its data-driven behavior (for example the
 * bench StateTree, Chooser table, motion-matched entry montage and Motion Warping
 * target from Game Animation Sample). This keeps concurrency and lifecycle code
 * in C++ without hard-coding a particular chair, skeleton, or animation set.
 *
 * Add the component to an AIController (preferred) or an AI-controlled Pawn.
 */
UCLASS(ClassGroup = (HMS), BlueprintType, Blueprintable,
	meta = (BlueprintSpawnableComponent, DisplayName = "HMS Smart Object Interaction"))
class HYBRIDMOTIONSYSTEM_API UHMS_SmartObjectInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHMS_SmartObjectInteractionComponent();
	/** Called by the Smart Object's StateTree task; does not search or claim a second slot. */
	bool BeginQueriedEntry(UHMS_SmartObjectInteractionProfile* Profile);
	EStateTreeRunStatus TickQueriedEntry(float DeltaTime);
	void EndQueriedEntry();
	UFUNCTION(BlueprintPure, Category="HMS|Smart Object")
	bool IsStateTreeDriven() const { return bInteractionContextActive; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	float SelectedEntryTime = 0.0f;
	float SelectedEntryHoldTime = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	float SelectedEntryPlayRate = 1.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	float SelectedEntryBlendTime = 0.0f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Smart Object|Diagnostics")
 bool bUsedForcedEntry = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	float SelectedEntryCost = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	float EntryHandoffSpeed = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="HMS|Smart Object|Query")
	int32 EntryQueryCount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Smart Object|Search")
	FName RequiredActorTag;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Immediately searches, claims, and starts the best matching interaction. */
	UFUNCTION(BlueprintCallable, Category = "HMS|Smart Object")
	bool FindAndUseSmartObject();

	/** Manual fixed-animation override. Works with player- or AI-controlled Mover Pawns. */
	UFUNCTION(BlueprintCallable, Category = "HMS|Smart Object")
	bool FindAndUseSmartObjectWithProfile(UHMS_SmartObjectInteractionProfile* Profile);

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool IsBusy() const;

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool IsPlayingProfileEntry() const;

	/** Hosts should stop movement and submit ProfileTargetTransform's facing through Mover inputs. */
	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool IsAligningProfileEntry() const;

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	FText GetLastFailureReason() const { return LastFailureReason; }

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool WasLastInteractionSuccessful() const { return bLastInteractionSucceeded; }

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	FTransform GetProfileTargetTransform() const { return ProfileTargetTransform; }

	/** Aborts the active interaction and releases its claim. */
	UFUNCTION(BlueprintCallable, Category = "HMS|Smart Object")
	void AbortInteraction();

	/** Cancels cooldown and permits an immediate automatic/manual retry. */
	UFUNCTION(BlueprintCallable, Category = "HMS|Smart Object")
	void ResetInteraction();

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	EHMS_SmartObjectInteractionState GetInteractionState() const { return InteractionState; }
	/** The entry montage has handed the pose to the animation state machine; follow-up montages may now take over safely. */
	UFUNCTION(BlueprintPure, Category="HMS|Smart Object")
	bool IsHoldingAnimationReady() const { return InteractionState == EHMS_SmartObjectInteractionState::Holding && bEntryAnimationReleased; }

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool IsInteracting() const { return InteractionState == EHMS_SmartObjectInteractionState::Interacting; }

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	AActor* GetClaimedSmartObjectActor() const { return ClaimedSmartObjectActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	bool GetClaimedSlotTransform(FTransform& OutTransform) const;

	/** Runtime tags on the claimed slot. Returns empty after release or invalidation.
	 * Blueprint animation logic can map these tags to its own interaction-state enum. */
	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	FGameplayTagContainer GetClaimedSlotTags() const;

	/** 添加或刷新一个带标签的绝对时间冷却。 */
	UFUNCTION(BlueprintCallable, Category = "HMS|Smart Object|Cooldown")
	bool AddTaggedCooldown(FGameplayTag CooldownTag, float Duration);

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object|Cooldown")
	bool IsTaggedCooldownActive(FGameplayTag CooldownTag) const;

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object|Cooldown")
	float GetTaggedCooldownRemaining(FGameplayTag CooldownTag) const;

	/** Starts searching automatically after InitialSearchDelay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Automation")
	bool bAutoStart = true;

	/** Repeats the ambient interaction cycle after success or failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Automation")
	bool bAutoRepeat = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Automation", meta = (ClampMin = "0.0"))
	float InitialSearchDelay = 2.0f;

	/** Matches the sample's five-second Sit cooldown by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Automation", meta = (ClampMin = "0.0"))
	float SuccessCooldown = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Automation", meta = (ClampMin = "0.1"))
	float FailureRetryDelay = 2.0f;

	/** Half extents of the search box around the user actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Search")
	FVector SearchBoxExtents = FVector(2000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Search")
	EHMS_SmartObjectSelectionMethod SelectionMethod = EHMS_SmartObjectSelectionMethod::Closest;

	/** Tags presented by the user to the Smart Object definition filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Search")
	FGameplayTagContainer UserTags;
	/** Append Blueprint-owned equipment/behavior tags to each search, allowing unsupported slots to be filtered before claiming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Smart Object|Search")
	bool bIncludeAnimationContextInUserTags = true;

	/** Adds SmartObject.ObjectType.NPC when UserTags is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Search")
	bool bAddDefaultNPCUserTag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Search")
	ESmartObjectClaimPriority ClaimPriority = ESmartObjectClaimPriority::Normal;

	/** The sample bench behavior requires an AAIController for its MoveTo tasks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object|Validation")
	bool bRequireAIController = true;

	UPROPERTY(BlueprintAssignable, Category = "HMS|Smart Object|Events")
	FHMS_SmartObjectInteractionStateChanged OnInteractionStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "HMS|Smart Object|Events")
	FHMS_SmartObjectSlotClaimed OnSlotClaimed;

	UPROPERTY(BlueprintAssignable, Category = "HMS|Smart Object|Events")
	FHMS_SmartObjectInteractionFinished OnInteractionFinished;

private:
	bool bEntryAnimationReleased = false;
	bool bRestoreStandingAfterHold = false;
	uint64 HoldStartFrame = 0;
	float HoldWaitElapsed = 0.0f;
	bool bQueriedEntry = false;
	bool bRouteComplete = false;
 bool bEntryNavigationStarted = false;
	bool bRestoreWindowSearch = false;
	bool bPreviousWindowSearch = false;
	float QueriedWarpEnd = 0.0f;
	float QueriedElapsed = 0.0f;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> QueriedMontage;
	bool BeginProfileApproach();
	void TickProfileInteraction(float DeltaTime);
	bool PlayProfileEntry();
	void CleanupProfileInteraction();
	UFUNCTION()
	void HandleEntryCompleted(FName NotifyName);
	UFUNCTION()
	void HandleEntryInterrupted(FName NotifyName);

	UPROPERTY(Transient)
	TObjectPtr<UHMS_SmartObjectInteractionProfile> ActiveProfile;
	UPROPERTY(Transient)
	TObjectPtr<UPlayMontageCallbackProxy> EntryPlaybackProxy;
	UPROPERTY(Transient)
	TObjectPtr<URootMotionModifier> EntryWarpModifier;
	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> EntryCollisionComponent;
	UPROPERTY(Transient)
	FText LastFailureReason;
	FTransform ProfileTargetTransform = FTransform::Identity;
	FVector ProfileApproachLocation = FVector::ZeroVector;
	float ProfileElapsedTime = 0.0f;
	bool bEntryCompleted = false;
	bool bEntryInterrupted = false;
	bool bAddedObjectCollisionIgnore = false;
	bool bLastInteractionSucceeded = false;

	AActor* ResolveUserActor() const;
	AAIController* ResolveAIController() const;
	USmartObjectSubsystem* ResolveSubsystem() const;
	bool ClaimBestCandidate(USmartObjectSubsystem& Subsystem, AActor& UserActor);
	bool StartClaimedInteraction(USmartObjectSubsystem& Subsystem, AActor& UserActor);
	void FinishInteraction(bool bSucceeded);
	void ReleaseClaim(bool bMarkSlotFree);
	void EnterCooldown(float Duration);
	void SetInteractionState(EHMS_SmartObjectInteractionState NewState);
	void FailAndScheduleRetry(const TCHAR* Reason);
	void HandleSlotInvalidated(const FSmartObjectClaimHandle& InvalidatedHandle, ESmartObjectSlotState SlotState);

	UPROPERTY(Transient)
	FGameplayInteractionContext GameplayInteractionContext;

	UPROPERTY(Transient)
	FSmartObjectClaimHandle ClaimedHandle;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ClaimedSmartObjectActor;

	UPROPERTY(VisibleInstanceOnly, Category = "HMS|Smart Object|Runtime")
	EHMS_SmartObjectInteractionState InteractionState = EHMS_SmartObjectInteractionState::Idle;

	double NextAutomaticSearchTime = 0.0;
	bool bInteractionContextActive = false;
	bool bSingleAutomaticCycleFinished = false;

	/** 与样例 AIController.Cooldowns Map 对应，但由 HMS 组件持有。 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, double> TaggedCooldownEndTimes;
};
