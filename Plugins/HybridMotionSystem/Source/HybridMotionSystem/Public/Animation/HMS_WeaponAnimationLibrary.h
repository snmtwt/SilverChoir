#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "HMS_MovementStruct.h"
#include "HMS_WeaponAnimationLibrary.generated.h"

class UAnimInstance;
class UHMS_WeaponAimProfile;

/** Stateless weapon layers. Equipment and cover state belong to the animation Blueprint. */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UHMS_WeaponAnimationLibrary : public UBlueprintFunctionLibrary
{

	GENERATED_BODY()
public:
	/** Game-thread snapshot for Blueprint Update Animation; the Blueprint owns its peek enum and cached angles. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover", meta=(DefaultToSelf="AnimationInstance"))
	static void ReadCoverPeekState(UAnimInstance* AnimationInstance, bool& Peeking, FVector2D& AimOffset, bool& AimReady);
	/** Cache the visual gate separately from ReadCoverPeekState's completed/ready flag. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover", meta=(DefaultToSelf="AnimationInstance"))
	static bool ReadCoverAimEnabled(UAnimInstance* AnimationInstance);
	/** Cache on the game thread alongside ReadCoverPeekState; the AnimGraph only reads the cached asset. */
	UFUNCTION(BlueprintPure, Category="HMS|Cover", meta=(DefaultToSelf="AnimationInstance"))
	static UHMS_WeaponAimProfile* ReadCoverAimProfile(UAnimInstance* AnimationInstance);
	UFUNCTION(BlueprintPure, Category="HMS|Cover", meta=(BlueprintThreadSafe))
	static void CalculateCoverPeekAimInputs(uint8 PeekState, const FGameplayTagContainer& StateTags,
		FVector2D AimOffset, bool AimReady, bool& Enabled, float& Yaw, float& Pitch);
	/** Uses the existing rotation mode: Aim raises the weapon; other modes use the ready pose.
	 * Dedicated interaction poses retain ownership of the upper body. Root motion and legs are unchanged. */
	UFUNCTION(BlueprintPure, Category="HMS|Weapon", meta=(BlueprintThreadSafe))
	static void CalculateWeaponLayerInputs(FGameplayTag WeaponPose, FGameplayTag LayerPose,
		EHMS_RotationMode RotationMode, FVector2D AimOffset, float GroundSpeed,
		const FGameplayTagContainer& StateTags, bool& Enabled, bool& Aiming,
		float& Speed, float& Yaw, float& Pitch);

	/** Compatibility API: default WeaponPose resolves the Blueprint-owned EquippedWeaponType (or legacy WeaponPose).
	 * New integrations should use HMS Animation Query Library. Explicit custom property names remain supported. */
	UFUNCTION(BlueprintCallable, Category="HMS|Weapon")
	static bool SetAnimationWeaponPose(UAnimInstance* AnimationInstance, FGameplayTag WeaponPose,
		FName VariableName = TEXT("WeaponPose"));

	UFUNCTION(BlueprintPure, Category="HMS|Weapon")
	static FGameplayTag GetAnimationWeaponPose(const UAnimInstance* AnimationInstance,
		FName VariableName = TEXT("WeaponPose"));
};
