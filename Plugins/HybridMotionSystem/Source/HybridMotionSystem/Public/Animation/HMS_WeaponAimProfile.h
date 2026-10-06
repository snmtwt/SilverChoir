#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "HMS_MovementStruct.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_WeaponAimProfile.generated.h"

class UBlendSpace;
class UAimOffsetBlendSpace;
class USkeleton;

/** Asset configuration only. Equipment, rotation mode and cover state remain Blueprint-owned. */
UENUM(BlueprintType)
enum class EHMS_AimContext : uint8 { FreeAim, CoverPeek };

UCLASS(BlueprintType)
class HYBRIDMOTIONSYSTEM_API UHMS_WeaponAimProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") EHMS_AimContext Context = EHMS_AimContext::FreeAim;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") FGameplayTag RequiredWeaponType;
 /** Non-additive upper-body hold. Cover uses its own full-body Chooser/transition pose instead. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim", meta=(EditCondition="Context == EHMS_AimContext::FreeAim", EditConditionHides))
 TObjectPtr<UBlendSpace> HoldingPose;
 /** Mesh-space additive samples must use the matching hold/peek idle as their reference. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim") TObjectPtr<UAimOffsetBlendSpace> AimOffset;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Limits", meta=(ClampMin="0", ClampMax="180", Units="Degrees")) float MaxYaw = 75.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Limits", meta=(ClampMin="0", ClampMax="90", Units="Degrees")) float MaxPitch = 60.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Transition", meta=(ClampMin="0", Units="s")) float BlendInTime = .25f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Transition", meta=(ClampMin="0", Units="s")) float BlendOutTime = .2f;
 /** Maintain the authored two-hand grip between AimOffset samples. No weapon actor is required. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Grip") bool bStabilizeLeftHand = false;
 /** Left wrist position in right-hand bone space, sampled from the reference pose. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim|Grip", meta=(EditCondition="bStabilizeLeftHand", Units="cm")) FVector LeftHandGripOffset = FVector::ZeroVector;
 bool IsUsable(USkeleton* ExpectedSkeleton = nullptr) const;
#if WITH_EDITOR
 virtual EDataValidationResult IsDataValid(class FDataValidationContext& ValidationContext) const override;
#endif
};

/** Pure immutable profile reads, safe for Animation Blueprint worker-thread evaluation. */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UHMS_WeaponAimLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintPure, Category="HMS|Aim", meta=(BlueprintThreadSafe))
 static void ReadAimGrip(const UHMS_WeaponAimProfile* Profile, float& Alpha, FVector& Target);
 UFUNCTION(BlueprintPure, Category="HMS|Aim", meta=(BlueprintThreadSafe))
 static void CalculateFreeAimInputs(const UHMS_WeaponAimProfile* Profile, FGameplayTag EquippedWeaponType,
  EHMS_RotationMode RotationMode, FVector2D AimAngles, float GroundSpeed, const FGameplayTagContainer& StateTags,
  bool& Enabled, float& Speed, float& Yaw, float& Pitch, UBlendSpace*& HoldingPose, UBlendSpace*& OffsetAsset,
  float& BlendIn, float& BlendOut);
 /** AimReady receives CoverAimEnabled, not the firing-ready flag.
  * PeekState remains for Blueprint compatibility and does not cancel the retract fade. */
 UFUNCTION(BlueprintPure, Category="HMS|Aim", meta=(BlueprintThreadSafe))
 static void CalculateCoverAimInputs(const UHMS_WeaponAimProfile* Profile, FGameplayTag EquippedWeaponType,
  uint8 PeekState, bool AimReady, FVector2D AimAngles, const FGameplayTagContainer& StateTags,
  bool& Enabled, float& Yaw, float& Pitch, UBlendSpace*& OffsetAsset, float& BlendIn, float& BlendOut);
};
