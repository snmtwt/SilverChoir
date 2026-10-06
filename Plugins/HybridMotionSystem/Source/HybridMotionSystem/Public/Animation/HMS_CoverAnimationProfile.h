#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveVector.h"
#include "HMS_CoverAnimationProfile.generated.h"

class UAnimMontage;
class UAnimSequence;
class UHMS_WeaponAimProfile;

/** One authored cover side/stance and equipment family. Side names are relative to facing the cover,
 * not the anatomical left/right of a character whose back is against it. */
UCLASS(BlueprintType)
class HYBRIDMOTIONSYSTEM_API UHMS_CoverAnimationProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(Categories="HMS.Cover")) FGameplayTag SlotTag;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(Categories="HMS.Weapon")) FGameplayTag RequiredWeaponType;
 /** Empty permits any behavior with this equipment; an explicit tag restricts the profile. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(Categories="HMS.Behavior")) FGameplayTag RequiredBehaviorState;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimSequence> ConcealedIdle;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimSequence> PeekIdle;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimMontage> EnterPeekMontage;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimMontage> ExitPeekMontage;
 /** Enable only for a time-reversed pair. Otherwise opposite input is queued until the current clip finishes. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") bool bExitReversesEnter = true;
	/** Use when this slot has no unarmed/other-equipment holding pose. Retract first, then release the claim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection") bool bLeaveCoverWhenProfileUnavailable = false;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") TObjectPtr<UHMS_WeaponAimProfile> AimProfile;
 /** Allow the visual AimOffset while the peek montage is still moving. Gameplay readiness stays separate. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Peek Transition") bool bAimDuringPeek = true;
 /** Normalized montage phase at which the normal AimProfile blend-in may begin. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Peek Transition", meta=(ClampMin="0", ClampMax="1", EditCondition="bAimDuringPeek"))
 float PeekAimStartFraction = .2f;
 /** Preserve target aiming while retracting instead of first returning to the neutral peek pose. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Peek Transition") bool bAimDuringRetract = true;
 /** Normalized retract phase at which the AimProfile blend-out begins. Gameplay readiness ends immediately. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Peek Transition", meta=(ClampMin="0", ClampMax="1", EditCondition="bAimDuringRetract"))
 float RetractAimEndFraction = .65f;
 /** Authored zero-offset weapon direction relative to the capsule, sampled over normalized enter time.
  * X = unwrapped yaw, Y = pitch. Reverse pairs reuse this curve backwards; empty uses AimReferenceRotation.
  * Rebuild after editing the peek motion, weapon socket or mesh orientation. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Peek Transition") FRuntimeVectorCurve PeekAimReferenceCurve;
 /** Authored zero-offset weapon direction relative to the pawn after the peek turn, in degrees.
  * Side-on aiming poses rarely point along the capsule's forward axis. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") FRotator AimReferenceRotation = FRotator::ZeroRotator;
 /** Head/eyes now; may be a calibrated weapon/muzzle socket after weapon integration. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") FName AimOriginSocket = TEXT("head");
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim", meta=(Units="cm")) FVector AimOriginOffset = FVector::ZeroVector;
};
