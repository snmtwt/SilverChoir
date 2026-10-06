#include "Animation/HMS_WeaponAnimationLibrary.h"
#include "Animation/HMS_AnimationQueryLibrary.h"
#include "Animation/AnimInstance.h"
#include "NativeGameplayTags.h"
#include "UObject/UnrealType.h"
#include "Components/HMS_CoverPeekComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Weapon_None, "HMS.Weapon.None");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Weapon_Rifle, "HMS.Weapon.Rifle");

UHMS_WeaponAimProfile* UHMS_WeaponAnimationLibrary::ReadCoverAimProfile(UAnimInstance* AnimationInstance)
{
	const APawn* Pawn = AnimationInstance ? AnimationInstance->TryGetPawnOwner() : nullptr;
	const auto* Cover = Pawn ? Pawn->FindComponentByClass<UHMS_CoverPeekComponent>() : nullptr;
	if (!Cover && Pawn && Pawn->GetController()) { Cover = Pawn->GetController()->FindComponentByClass<UHMS_CoverPeekComponent>(); }
	return Cover ? Cover->GetCoverAimProfile() : nullptr;
}

bool UHMS_WeaponAnimationLibrary::ReadCoverAimEnabled(UAnimInstance* AnimationInstance)
{
	const APawn* Pawn = AnimationInstance ? AnimationInstance->TryGetPawnOwner() : nullptr;
	const auto* Cover = Pawn ? Pawn->FindComponentByClass<UHMS_CoverPeekComponent>() : nullptr;
	if (!Cover && Pawn && Pawn->GetController()) { Cover = Pawn->GetController()->FindComponentByClass<UHMS_CoverPeekComponent>(); }
	return Cover && Cover->IsAimOffsetActive();
}

void UHMS_WeaponAnimationLibrary::ReadCoverPeekState(UAnimInstance* AnimationInstance, bool& Peeking, FVector2D& AimOffset, bool& AimReady)
{
	Peeking = false; AimOffset = FVector2D::ZeroVector; AimReady = false;
	const APawn* Pawn = AnimationInstance ? AnimationInstance->TryGetPawnOwner() : nullptr;
	const auto* Cover = Pawn ? Pawn->FindComponentByClass<UHMS_CoverPeekComponent>() : nullptr;
	if (!Cover && Pawn && Pawn->GetController()) { Cover = Pawn->GetController()->FindComponentByClass<UHMS_CoverPeekComponent>(); }
	if (Cover) { Peeking = Cover->IsPeeking() && Cover->CanPeek(); AimOffset = Cover->GetAimOffset(); AimReady = Cover->IsAimReady() && Cover->CanPeek(); }
}

void UHMS_WeaponAnimationLibrary::CalculateCoverPeekAimInputs(uint8 PeekState, const FGameplayTagContainer& StateTags,
	FVector2D AimOffset, bool AimReady, bool& Enabled, float& Yaw, float& Pitch)
{
	const FGameplayTag Cover = FGameplayTag::RequestGameplayTag(TEXT("SM.State.Cover"), false);
	// The visual gate may remain enabled during retract after PeekState is concealed.
	Enabled = AimReady && Cover.IsValid() && StateTags.HasTag(Cover);
	Yaw = FMath::IsFinite(AimOffset.X) ? FMath::Clamp(static_cast<float>(AimOffset.X), -65.f, 65.f) : 0.f;
	Pitch = FMath::IsFinite(AimOffset.Y) ? FMath::Clamp(static_cast<float>(AimOffset.Y), -45.f, 45.f) : 0.f;
}

void UHMS_WeaponAnimationLibrary::CalculateWeaponLayerInputs(FGameplayTag WeaponPose, FGameplayTag LayerPose,
	EHMS_RotationMode RotationMode, FVector2D AimOffset, float GroundSpeed,
	const FGameplayTagContainer& StateTags, bool& Enabled, bool& Aiming, float& Speed, float& Yaw, float& Pitch)
{
	const FGameplayTag Cover = FGameplayTag::RequestGameplayTag(TEXT("SM.State.Cover"), false);
	Enabled = LayerPose.IsValid() && WeaponPose.MatchesTag(LayerPose)
		&& !(Cover.IsValid() && StateTags.HasTag(Cover));
	Aiming = RotationMode == EHMS_RotationMode::Aim;
	Speed = FMath::IsFinite(GroundSpeed) ? FMath::Clamp(GroundSpeed, 0.f, 600.f) : 0.f;
	// Clamp instead of switching off at the limit: Mover/turn-in-place catches up with the aim.
	Yaw = FMath::IsFinite(AimOffset.X) ? FMath::Clamp(static_cast<float>(AimOffset.X), -75.f, 75.f) : 0.f;
	Pitch = FMath::IsFinite(AimOffset.Y) ? FMath::Clamp(static_cast<float>(AimOffset.Y), -60.f, 60.f) : 0.f;
}

bool UHMS_WeaponAnimationLibrary::SetAnimationWeaponPose(UAnimInstance* AnimationInstance,
	FGameplayTag WeaponPose, FName VariableName)
{
	if (VariableName == TEXT("WeaponPose") || VariableName == TEXT("EquippedWeaponType"))
	{ return UHMS_AnimationQueryLibrary::SetEquippedWeaponType(AnimationInstance, WeaponPose); }
	if (!IsInGameThread() || !IsValid(AnimationInstance)) { return false; }
	FStructProperty* Property = FindFProperty<FStructProperty>(AnimationInstance->GetClass(), VariableName);
	if (!Property || Property->Struct != FGameplayTag::StaticStruct()) { return false; }
	*Property->ContainerPtrToValuePtr<FGameplayTag>(AnimationInstance) = WeaponPose;
	return true;
}

FGameplayTag UHMS_WeaponAnimationLibrary::GetAnimationWeaponPose(const UAnimInstance* AnimationInstance, FName VariableName)
{
	if (VariableName == TEXT("WeaponPose") || VariableName == TEXT("EquippedWeaponType"))
	{ return UHMS_AnimationQueryLibrary::GetEquippedWeaponType(AnimationInstance); }
	if (!IsValid(AnimationInstance)) { return FGameplayTag(); }
	const FStructProperty* Property = FindFProperty<FStructProperty>(AnimationInstance->GetClass(), VariableName);
	return Property && Property->Struct == FGameplayTag::StaticStruct()
		? *Property->ContainerPtrToValuePtr<FGameplayTag>(AnimationInstance) : FGameplayTag();
}
