#include "Components/HMS_CharacterMoverComponent.h"

#include "MovementMode/HMS_FallingMode.h"
#include "MovementMode/HMS_RagdollMode.h"
#include "MovementMode/HMS_WalkingMode.h"
#include "Structs/HMS_MoverStructs.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/Settings/StanceSettings.h"

UHMS_CharacterMoverComponent::UHMS_CharacterMoverComponent()
{
	MovementModes.Add(
		DefaultModeNames::Walking,
		CreateDefaultSubobject<UHMS_WalkingMode>(TEXT("HMSWalkingMode")));

	MovementModes.Add(
		DefaultModeNames::Falling,
		CreateDefaultSubobject<UHMS_FallingMode>(TEXT("HMSFallingMode")));

	MovementModes.Add(
		HMSModeNames::Ragdoll,
		CreateDefaultSubobject<UHMS_RagdollMode>(TEXT("HMSRagdollMode")));

	StartingMovementMode = DefaultModeNames::Falling;
}

void UHMS_CharacterMoverComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// Native CDOs and newly created Blueprint templates are validated before
	// OnRegister and never receive PostLoad. Populate their mode dependencies too.
	// Serialized objects must wait for PostLoad so their saved settings survive.
	if (!HasAnyFlags(RF_NeedLoad))
	{
		RefreshSharedSettings();
		ApplyGameAnimationSampleMovementDefaults();
	}
}

void UHMS_CharacterMoverComponent::PostLoad()
{
	Super::PostLoad();
	UpgradeDefaultMovementModes();
	RefreshSharedSettings();
	ApplyGameAnimationSampleMovementDefaults();
}

void UHMS_CharacterMoverComponent::OnRegister()
{
	// 蓝图资产可能在 HMS 组件加入以前保存过 MovementModes。注册前再做一次兼容升级，
	// 保证 PIE 和打包游戏均不会悄悄退回引擎默认模式。
	UpgradeDefaultMovementModes();
	RefreshSharedSettings();
	ApplyGameAnimationSampleMovementDefaults();
	Super::OnRegister();
}

void UHMS_CharacterMoverComponent::UpgradeDefaultMovementModes()
{
	const TObjectPtr<UBaseMovementMode>* WalkingEntry = MovementModes.Find(DefaultModeNames::Walking);
	if (!WalkingEntry || !WalkingEntry->Get() || WalkingEntry->Get()->GetClass() == UWalkingMode::StaticClass())
	{
		UHMS_WalkingMode* WalkingMode = FindObject<UHMS_WalkingMode>(this, TEXT("HMSWalkingMode"));
		if (!WalkingMode)
		{
			WalkingMode = NewObject<UHMS_WalkingMode>(this, TEXT("HMSWalkingMode"));
		}
		MovementModes.Add(DefaultModeNames::Walking, WalkingMode);
	}
	else if (UHMS_WalkingMode* WalkingMode = Cast<UHMS_WalkingMode>(WalkingEntry->Get()))
	{
		// Older serialized component templates predate the UE 5.8 stance-settings dependency.
		WalkingMode->SharedSettingsClasses.AddUnique(UStanceSettings::StaticClass());
	}

	const TObjectPtr<UBaseMovementMode>* FallingEntry = MovementModes.Find(DefaultModeNames::Falling);
	if (!FallingEntry || !FallingEntry->Get() || FallingEntry->Get()->GetClass() == UFallingMode::StaticClass())
	{
		UHMS_FallingMode* FallingMode = FindObject<UHMS_FallingMode>(this, TEXT("HMSFallingMode"));
		if (!FallingMode)
		{
			FallingMode = NewObject<UHMS_FallingMode>(this, TEXT("HMSFallingMode"));
		}
		MovementModes.Add(DefaultModeNames::Falling, FallingMode);
	}

	const TObjectPtr<UBaseMovementMode>* RagdollEntry = MovementModes.Find(HMSModeNames::Ragdoll);
	if (!RagdollEntry || !RagdollEntry->Get())
	{
		UHMS_RagdollMode* RagdollMode = FindObject<UHMS_RagdollMode>(this, TEXT("HMSRagdollMode"));
		if (!RagdollMode)
		{
			RagdollMode = NewObject<UHMS_RagdollMode>(this, TEXT("HMSRagdollMode"));
		}
		MovementModes.Add(HMSModeNames::Ragdoll, RagdollMode);
	}
}

void UHMS_CharacterMoverComponent::ApplyGameAnimationSampleMovementDefaults()
{
	if (!bUseGameAnimationSampleMovementDefaults)
	{
		return;
	}

	// Values read from UE 5.8's SandboxCharacter_Mover CharacterMover template.
	if (UCommonLegacyMovementSettings* Settings =
		FindSharedSettings_Mutable<UCommonLegacyMovementSettings>())
	{
		Settings->Acceleration = 500.0f;
		Settings->Deceleration = 0.0f;
		Settings->MaxSpeed = 300.0f;
		Settings->TurningBoost = 8.0f;
		Settings->TurningRate = 300.0f;
		Settings->GroundFriction = 0.0f;
		Settings->BrakingFriction = 8.0f;
		Settings->BrakingFrictionFactor = 0.0f;
		Settings->bUseSeparateBrakingFriction = false;
		Settings->bUseAccelerationForVelocityMove = true;
		Settings->MaxStepHeight = 30.0f;
		Settings->MaxWalkSlopeCosine = 0.71f;
		Settings->FloorSweepDistance = 40.0f;
		Settings->PerchRadiusThreshold = 20.0f;
		Settings->JumpUpwardsSpeed = 500.0f;
		Settings->bShouldRemainVertical = true;
		Settings->bIgnoreBaseRotation = false;
		Settings->bUseFlatBaseForFloorChecks = true;
		Settings->GroundMovementModeName = DefaultModeNames::Walking;
		Settings->AirMovementModeName = DefaultModeNames::Falling;
	}

	if (UStanceSettings* Settings = FindSharedSettings_Mutable<UStanceSettings>())
	{
		Settings->CrouchingMaxAcceleration = 2000.0f;
		Settings->CrouchingMaxSpeed = 200.0f;
		Settings->CrouchHalfHeight = 60.0f;
		Settings->CrouchedEyeHeight = 50.0f;
	}

	// Existing Blueprint component templates may have serialized the old HMS values,
	// so align the active HMS falling mode as well as its native constructor defaults.
	if (UHMS_FallingMode* FallingMode = FindMode_Mutable<UHMS_FallingMode>())
	{
		FallingMode->bCancelVerticalSpeedOnLanding = true;
		FallingMode->AirControlPercentage = 0.5f;
		FallingMode->FallingDeceleration = 0.0f;
		FallingMode->FallingLateralFriction = 0.0f;
		FallingMode->OverTerminalSpeedFallingDeceleration = 800.0f;
		FallingMode->TerminalMovementPlaneSpeed = 1500.0f;
		FallingMode->bShouldClampTerminalVerticalSpeed = true;
		FallingMode->VerticalFallingDeceleration = 4000.0f;
		FallingMode->TerminalVerticalSpeed = 2000.0f;
	}
}
