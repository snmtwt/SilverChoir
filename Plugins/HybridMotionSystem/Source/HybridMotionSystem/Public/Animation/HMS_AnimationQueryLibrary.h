#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_AnimationQueryLibrary.generated.h"

class UAnimInstance;

/** Semantic selection inputs stay on the animation Blueprint, separate from stance, gait and aiming. */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UHMS_AnimationQueryLibrary : public UBlueprintFunctionLibrary
{

	GENERATED_BODY()
public:
	/** Set both Blueprint tags atomically on the game thread. Invalid tags or missing properties leave both unchanged. */
	UFUNCTION(BlueprintCallable, Category="HMS|Animation Query")
	static bool SetAnimationQueryContext(UAnimInstance* AnimationInstance,
		UPARAM(meta=(Categories="HMS.Behavior")) FGameplayTag BehaviorState,
		UPARAM(meta=(Categories="HMS.Weapon")) FGameplayTag EquippedWeaponType);

	UFUNCTION(BlueprintCallable, Category="HMS|Animation Query")
	static bool SetAnimationBehaviorState(UAnimInstance* AnimationInstance,
		UPARAM(meta=(Categories="HMS.Behavior")) FGameplayTag BehaviorState);

	UFUNCTION(BlueprintCallable, Category="HMS|Animation Query")
	static bool SetEquippedWeaponType(UAnimInstance* AnimationInstance,
		UPARAM(meta=(Categories="HMS.Weapon")) FGameplayTag EquippedWeaponType);

	UFUNCTION(BlueprintPure, Category="HMS|Animation Query")
	static FGameplayTag GetAnimationBehaviorState(const UAnimInstance* AnimationInstance);

	/** Reads EquippedWeaponType; supports old Blueprints containing only WeaponPose during migration. */
	UFUNCTION(BlueprintPure, Category="HMS|Animation Query")
	static FGameplayTag GetEquippedWeaponType(const UAnimInstance* AnimationInstance);
};
