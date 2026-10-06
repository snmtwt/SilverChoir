#include "Animation/HMS_WeaponAimProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Misc/DataValidation.h"

void UHMS_WeaponAimLibrary::ReadAimGrip(const UHMS_WeaponAimProfile* Profile, float& Alpha, FVector& Target)
{
 Alpha = Profile && Profile->Context == EHMS_AimContext::FreeAim && Profile->IsUsable()
  && Profile->bStabilizeLeftHand && !Profile->LeftHandGripOffset.ContainsNaN() ? 1.f : 0.f;
 Target = Alpha > 0.f ? Profile->LeftHandGripOffset : FVector::ZeroVector;
}

bool UHMS_WeaponAimProfile::IsUsable(USkeleton* ExpectedSkeleton) const
{
 if (!RequiredWeaponType.IsValid() || !AimOffset || !AimOffset->GetSkeleton() || AimOffset->GetBlendSamples().IsEmpty()
  || (ExpectedSkeleton && AimOffset->GetSkeleton() != ExpectedSkeleton)) { return false; }
 if (Context == EHMS_AimContext::FreeAim && (!HoldingPose || HoldingPose->GetBlendSamples().IsEmpty() || HoldingPose->IsValidAdditive()
  || HoldingPose->GetSkeleton() != AimOffset->GetSkeleton())) { return false; }
 return FMath::IsFinite(MaxYaw) && MaxYaw > 0.f && MaxYaw <= 180.f
  && FMath::IsFinite(MaxPitch) && MaxPitch > 0.f && MaxPitch <= 90.f
  && FMath::IsFinite(BlendInTime) && BlendInTime >= 0.f
  && FMath::IsFinite(BlendOutTime) && BlendOutTime >= 0.f;
}

#if WITH_EDITOR
EDataValidationResult UHMS_WeaponAimProfile::IsDataValid(FDataValidationContext& ValidationContext) const
{
 const EDataValidationResult Parent = Super::IsDataValid(ValidationContext);
 if (!IsUsable())
 {
  ValidationContext.AddError(NSLOCTEXT("HMS", "InvalidAimProfile", "Aim profile requires a weapon tag, compatible animation assets, positive finite limits and finite non-negative blend times."));
  return EDataValidationResult::Invalid;
 }
 return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif

namespace HMSAim
{
 bool IsCover(const FGameplayTagContainer& Tags)
 {
  const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("SM.State.Cover"), false);
  return Tag.IsValid() && Tags.HasTag(Tag);
 }
 float Finite(float Value) { return FMath::IsFinite(Value) ? Value : 0.f; }
 void Read(const UHMS_WeaponAimProfile* Profile, FVector2D Angles, float& Yaw, float& Pitch,
  UBlendSpace*& Offset, float& In, float& Out)
 {
  Offset = Profile ? Profile->AimOffset.Get() : nullptr;
  In = Profile ? FMath::Clamp(Finite(Profile->BlendInTime), 0.f, 10.f) : .25f;
  Out = Profile ? FMath::Clamp(Finite(Profile->BlendOutTime), 0.f, 10.f) : .2f;
  Yaw = Pitch = 0.f;
  if (!Offset || !Profile) { return; }
  // Respect both the designer's limit and the authored BlendSpace domain.
  const FBlendParameter& X = Offset->GetBlendParameter(0);
  const FBlendParameter& Y = Offset->GetBlendParameter(1);
  const float YawLimit = FMath::Clamp(Finite(Profile->MaxYaw), 0.f, 180.f);
  const float PitchLimit = FMath::Clamp(Finite(Profile->MaxPitch), 0.f, 90.f);
  Yaw = FMath::Clamp(FMath::Clamp(Finite(Angles.X), -YawLimit, YawLimit), X.Min, X.Max);
  Pitch = FMath::Clamp(FMath::Clamp(Finite(Angles.Y), -PitchLimit, PitchLimit), Y.Min, Y.Max);
 }
}

void UHMS_WeaponAimLibrary::CalculateFreeAimInputs(const UHMS_WeaponAimProfile* Profile,
 FGameplayTag EquippedWeaponType, EHMS_RotationMode RotationMode, FVector2D AimAngles, float GroundSpeed,
 const FGameplayTagContainer& StateTags, bool& Enabled, float& Speed, float& Yaw, float& Pitch,
 UBlendSpace*& HoldingPose, UBlendSpace*& OffsetAsset, float& BlendIn, float& BlendOut)
{
 HMSAim::Read(Profile, AimAngles, Yaw, Pitch, OffsetAsset, BlendIn, BlendOut);
 HoldingPose = Profile ? Profile->HoldingPose.Get() : nullptr;
 Speed = FMath::Max(0.f, HMSAim::Finite(GroundSpeed));
 if (HoldingPose) { const auto& Axis = HoldingPose->GetBlendParameter(0); Speed = FMath::Clamp(Speed, Axis.Min, Axis.Max); }
 Enabled = Profile && Profile->Context == EHMS_AimContext::FreeAim && Profile->IsUsable()
  && EquippedWeaponType.MatchesTag(Profile->RequiredWeaponType) && RotationMode == EHMS_RotationMode::Aim
  && !HMSAim::IsCover(StateTags);
 // An invalid replacement cannot supply the outgoing pose. Cut to the valid base pose
 // instead of blending a null asset's reference pose into the character for a few frames.
 if (!Profile || Profile->Context != EHMS_AimContext::FreeAim || !Profile->IsUsable()) { BlendOut = 0.f; }
}

void UHMS_WeaponAimLibrary::CalculateCoverAimInputs(const UHMS_WeaponAimProfile* Profile,
 FGameplayTag EquippedWeaponType, uint8 PeekState, bool AimReady, FVector2D AimAngles,
 const FGameplayTagContainer& StateTags, bool& Enabled, float& Yaw, float& Pitch,
 UBlendSpace*& OffsetAsset, float& BlendIn, float& BlendOut)
{
 HMSAim::Read(Profile, AimAngles, Yaw, Pitch, OffsetAsset, BlendIn, BlendOut);
 // The Blueprint supplies CoverAimEnabled here. PeekState is already concealed
 // during retract and must not cancel the outgoing visual aim layer.
 Enabled = Profile && Profile->Context == EHMS_AimContext::CoverPeek && Profile->IsUsable()
  && EquippedWeaponType.MatchesTag(Profile->RequiredWeaponType) && AimReady
  && HMSAim::IsCover(StateTags);
 if (!Profile || Profile->Context != EHMS_AimContext::CoverPeek || !Profile->IsUsable()) { BlendOut = 0.f; }
}
