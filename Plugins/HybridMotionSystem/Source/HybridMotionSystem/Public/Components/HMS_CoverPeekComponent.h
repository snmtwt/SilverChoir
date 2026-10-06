#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "HMS_CoverPeekComponent.generated.h"

class APawn;
class UAnimMontage;
class UPlayMoverMontageCallbackProxy;
class UHMS_CoverAnimationProfile;
class UHMS_WeaponAimProfile;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHMS_CoverPeekChanged, bool, bPeeking);

/** Optional cover input policy. Animation assets and the cover/peek enums remain in the Anim Blueprint. */
UCLASS(ClassGroup=(HMS), BlueprintType, meta=(BlueprintSpawnableComponent))
class HYBRIDMOTIONSYSTEM_API UHMS_CoverPeekComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UHMS_CoverPeekComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;

	/** Game/ability event entry point. False always retracts; true requires a held slot and the configured weapon/side. */
	UFUNCTION(BlueprintCallable, Category="HMS|Cover")
	bool RequestPeek(bool bEnable);
	/** Track a world-space target without changing the actor's facing. A location at world origin is valid. */
	UFUNCTION(BlueprintCallable, Category="HMS|Cover|Aim")
	bool SetAimTargetLocation(FVector WorldLocation);
	/** Track an actor and an optional point in that actor's local space. Destroyed targets are cleared safely. */
	UFUNCTION(BlueprintCallable, Category="HMS|Cover|Aim")
	bool SetAimTargetActor(AActor* TargetActor, FVector LocalOffset = FVector::ZeroVector);
	UFUNCTION(BlueprintCallable, Category="HMS|Cover|Aim")
	void ClearAimTarget();
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Aim")
	bool GetAimTargetLocation(FVector& WorldLocation) const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Aim")
	FVector GetAimOrigin() const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Aim")
	FVector GetResolvedAimDirection() const { return ResolvedAimDirection; }
	/** Game-thread snapshot for the Anim Blueprint; state enums remain Blueprint-owned. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Animation")
	UHMS_CoverAnimationProfile* GetCoverAnimationProfile() const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Animation")
	UHMS_WeaponAimProfile* GetCoverAimProfile() const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	bool CanPeek() const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	bool IsPeeking() const { return bPeeking; }
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	bool IsTransitioning() const { return PlaybackProxy != nullptr; }
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	bool IsAimReady() const { return bPeeking && !IsTransitioning(); }
	/** Visual layer gate. May become true before IsAimReady; never use this alone to permit firing. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover|Animation")
	bool IsAimOffsetActive() const;
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	bool IsCoverLocked() const;
	/** Feed this to the movement input producer as facing; keep world aim separate. Does not teleport/rotate an actor. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	FVector GetLockedFacingDirection() const { return LockedFacing; }
	UFUNCTION(BlueprintPure, Category="HMS|Cover")
	FVector2D GetAimOffset() const { return SmoothedAimOffset; }
	UPROPERTY(BlueprintAssignable, Category="HMS|Cover")
	FHMS_CoverPeekChanged OnPeekChanged;
	/** First matching slot/equipment/behavior profile wins. Empty retains the legacy montage properties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover|Animation")
	TArray<TObjectPtr<UHMS_CoverAnimationProfile>> CoverProfiles;
	/** Only enable sides for which the project's Chooser and Aim Offset are configured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover")
	FGameplayTagContainer SupportedSlotTags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover")
	FGameplayTag RequiredWeaponPose;
	/** Paired root-motion clips. Exit is the reverse path; both must have the same duration for interrupted reversal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover|Animation")
	TObjectPtr<UAnimMontage> EnterPeekMontage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover|Animation")
	TObjectPtr<UAnimMontage> ExitPeekMontage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover", meta=(ClampMin="0",ClampMax="90"))
	float MaximumAimYaw = 65.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover", meta=(ClampMin="0",ClampMax="89"))
	float MaximumAimPitch = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover", meta=(ClampMin="0"))
	float AimSmoothingTime = 0.12f;
	/** Bounds correction across the rear +/-180 yaw seam and interrupted direction changes. Degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Cover|Aim", meta=(ClampMin="1"))
	float MaximumAimOffsetSpeed = 720.f;
private:
	float GetTransitionPhase() const;
	int32 PlayingMontageInstanceId = INDEX_NONE;
	UHMS_CoverAnimationProfile* ResolveCoverProfile() const;
	bool MatchesProfile(const UHMS_CoverAnimationProfile* Profile) const;
	UPROPERTY(Transient)
	TObjectPtr<UHMS_CoverAnimationProfile> PlayingProfile;
	TWeakObjectPtr<AActor> AimTargetActor;
	FVector AimTargetPoint = FVector::ZeroVector;
	FVector TargetLocalOffset = FVector::ZeroVector;
	FVector ResolvedAimDirection = FVector::ForwardVector;
	bool bHasAimTargetPoint = false;
	bool bTrackAimActor = false;
	bool bHasQueuedPeek = false;
	bool bQueuedPeek = false;
	APawn* ResolvePawn() const;
	void RefreshInteraction();
	void ResetPeek();
	void StopTransition();
	UFUNCTION()
	void TransitionCompleted(FName NotifyName);
	UFUNCTION()
	void TransitionInterrupted(FName NotifyName);
	UFUNCTION()
	void InteractionChanged(EHMS_SmartObjectInteractionState Previous, EHMS_SmartObjectInteractionState Current);
	TWeakObjectPtr<UHMS_SmartObjectInteractionComponent> Interaction;
	FVector LockedFacing = FVector::ForwardVector;
	FVector2D SmoothedAimOffset = FVector2D::ZeroVector;
	FRotator SmoothedWorldAim = FRotator::ZeroRotator;
	bool bHasSmoothedWorldAim = false;
	bool bPeeking = false;
	UPROPERTY(Transient)
	TObjectPtr<UPlayMoverMontageCallbackProxy> PlaybackProxy;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PlayingMontage;
};
