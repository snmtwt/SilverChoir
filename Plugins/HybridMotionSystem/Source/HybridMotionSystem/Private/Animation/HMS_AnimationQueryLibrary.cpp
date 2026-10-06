#include "Animation/HMS_AnimationQueryLibrary.h"
#include "Animation/AnimInstance.h"
#include "NativeGameplayTags.h"
#include "UObject/UnrealType.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Behavior_Relaxed, "HMS.Behavior.Relaxed");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Behavior_Alert, "HMS.Behavior.Alert");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Behavior_Combat, "HMS.Behavior.Combat");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Behavior_Stealth, "HMS.Behavior.Stealth");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Weapon_Pistol, "HMS.Weapon.Pistol");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Stance_Prone, "SM.Stance.Prone");

namespace
{
	const FStructProperty* TagProperty(const UAnimInstance* Anim, FName Name)
	{
		const auto* Property = IsValid(Anim) ? FindFProperty<FStructProperty>(Anim->GetClass(), Name) : nullptr;
		return Property && Property->Struct == FGameplayTag::StaticStruct() ? Property : nullptr;
	}
	const FStructProperty* WeaponProperty(const UAnimInstance* Anim)
	{
		const auto* Property = TagProperty(Anim, TEXT("EquippedWeaponType"));
		return Property ? Property : TagProperty(Anim, TEXT("WeaponPose"));
	}
	bool IsFamily(FGameplayTag Value, FName Family)
	{
		const auto Root = FGameplayTag::RequestGameplayTag(Family, false);
		return Value.IsValid() && Root.IsValid() && Value != Root && Value.MatchesTag(Root);
	}
	FGameplayTag Read(const UAnimInstance* Anim, const FStructProperty* Property, FGameplayTag Default)
	{
		if (!Property) { return Default; }
		const auto Value = *Property->ContainerPtrToValuePtr<FGameplayTag>(Anim);
		return Value.IsValid() ? Value : Default;
	}
}

bool UHMS_AnimationQueryLibrary::SetAnimationQueryContext(UAnimInstance* Anim, FGameplayTag Behavior, FGameplayTag Weapon)
{
	if (!IsInGameThread() || !IsFamily(Behavior, TEXT("HMS.Behavior")) || !IsFamily(Weapon, TEXT("HMS.Weapon"))) { return false; }
	const auto* B = TagProperty(Anim, TEXT("BehaviorState")); const auto* W = WeaponProperty(Anim);
	if (!B || !W) { return false; }
	*B->ContainerPtrToValuePtr<FGameplayTag>(Anim) = Behavior;
	*W->ContainerPtrToValuePtr<FGameplayTag>(Anim) = Weapon;
	return true;
}

bool UHMS_AnimationQueryLibrary::SetAnimationBehaviorState(UAnimInstance* Anim, FGameplayTag Behavior)
{
	if (!IsInGameThread() || !IsFamily(Behavior, TEXT("HMS.Behavior"))) { return false; }
	const auto* Property = TagProperty(Anim, TEXT("BehaviorState"));
	if (!Property) { return false; }
	*Property->ContainerPtrToValuePtr<FGameplayTag>(Anim) = Behavior; return true;
}

bool UHMS_AnimationQueryLibrary::SetEquippedWeaponType(UAnimInstance* Anim, FGameplayTag Weapon)
{
	if (!IsInGameThread() || !IsFamily(Weapon, TEXT("HMS.Weapon"))) { return false; }
	const auto* Property = WeaponProperty(Anim);
	if (!Property) { return false; }
	*Property->ContainerPtrToValuePtr<FGameplayTag>(Anim) = Weapon; return true;
}

FGameplayTag UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(const UAnimInstance* Anim)
{
	return Read(Anim, TagProperty(Anim, TEXT("BehaviorState")), TAG_HMS_Behavior_Relaxed);
}

FGameplayTag UHMS_AnimationQueryLibrary::GetEquippedWeaponType(const UAnimInstance* Anim)
{
	return Read(Anim, WeaponProperty(Anim), FGameplayTag::RequestGameplayTag(TEXT("HMS.Weapon.None"), false));
}
