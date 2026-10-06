#include "MovementMode/HMS_WalkingMode.h"

#include "Math/SpringMath.h"
#include "MoveLibrary/RollbackBlackboardLibrary.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/Settings/StanceSettings.h"

const FName UHMS_WalkingMode::DidGenerateMoveEntry = TEXT("HMS.DidGenerateWalkMove");

namespace HMSSmoothWalkingStateTolerance
{
	constexpr float Velocity = 10.0f;
	constexpr float AngularVelocity = 10.0f;
	constexpr float Acceleration = 50.0f;
	constexpr float FacingDegrees = 10.0f;
}

UScriptStruct* FHMS_SmoothWalkingState::GetScriptStruct() const
{
	return StaticStruct();
}

FMoverDataStructBase* FHMS_SmoothWalkingState::Clone() const
{
	return new FHMS_SmoothWalkingState(*this);
}

bool FHMS_SmoothWalkingState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	const bool bResult = Super::NetSerialize(Ar, Map, bOutSuccess);
	Ar << SpringVelocity;
	Ar << SpringAcceleration;
	Ar << IntermediateVelocity;
	Ar << IntermediateFacing;
	Ar << IntermediateAngularVelocity;
	return bResult;
}

void FHMS_SmoothWalkingState::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);
	Out.Appendf("HMS SpringVelocity=%s SpringAcceleration=%s IntermediateVelocity=%s\n",
		*SpringVelocity.ToCompactString(), *SpringAcceleration.ToCompactString(),
		*IntermediateVelocity.ToCompactString());
}

bool FHMS_SmoothWalkingState::ShouldReconcile(const FMoverDataStructBase& AuthorityState) const
{
	const FHMS_SmoothWalkingState& Authority = static_cast<const FHMS_SmoothWalkingState&>(AuthorityState);
	return !(SpringVelocity - Authority.SpringVelocity).IsNearlyZero(HMSSmoothWalkingStateTolerance::Velocity)
		|| !(SpringAcceleration - Authority.SpringAcceleration).IsNearlyZero(HMSSmoothWalkingStateTolerance::Acceleration)
		|| !(IntermediateVelocity - Authority.IntermediateVelocity).IsNearlyZero(HMSSmoothWalkingStateTolerance::Velocity)
		|| FMath::RadiansToDegrees(IntermediateFacing.AngularDistance(Authority.IntermediateFacing)) > HMSSmoothWalkingStateTolerance::FacingDegrees
		|| !(IntermediateAngularVelocity - Authority.IntermediateAngularVelocity).IsNearlyZero(HMSSmoothWalkingStateTolerance::AngularVelocity);
}

void FHMS_SmoothWalkingState::Interpolate(
	const FMoverDataStructBase& From, const FMoverDataStructBase& To, const float Pct)
{
	const FHMS_SmoothWalkingState& FromState = static_cast<const FHMS_SmoothWalkingState&>(From);
	const FHMS_SmoothWalkingState& ToState = static_cast<const FHMS_SmoothWalkingState&>(To);
	SpringVelocity = FMath::Lerp(FromState.SpringVelocity, ToState.SpringVelocity, Pct);
	SpringAcceleration = FMath::Lerp(FromState.SpringAcceleration, ToState.SpringAcceleration, Pct);
	IntermediateVelocity = FMath::Lerp(FromState.IntermediateVelocity, ToState.IntermediateVelocity, Pct);
	IntermediateFacing = FQuat::Slerp(FromState.IntermediateFacing, ToState.IntermediateFacing, Pct);
	IntermediateAngularVelocity = FMath::Lerp(FromState.IntermediateAngularVelocity, ToState.IntermediateAngularVelocity, Pct);
}

UHMS_WalkingMode::UHMS_WalkingMode()
{
	// The UE 5.8 sample walking mode requests stance settings in addition to the
	// common legacy settings inherited from UWalkingMode.
	SharedSettingsClasses.AddUnique(UStanceSettings::StaticClass());

	WalkSettings.MaxSpeed = 165.0f;
	WalkSettings.Acceleration = 500.0f;
	WalkSettings.BrakingDeceleration = 1500.0f;
	WalkSettings.GaitChangeDeceleration = 300.0f;
	WalkSettings.FacingSmoothingTime = 0.4f;
	WalkSettings.TurningStrength = 8.0f;

	RunSettings.MaxSpeed = 375.0f;
	RunSettings.Acceleration = 800.0f;
	RunSettings.BrakingDeceleration = 1500.0f;
	RunSettings.GaitChangeDeceleration = 300.0f;
	RunSettings.FacingSmoothingTime = 0.4f;
	RunSettings.TurningStrength = 8.0f;

	SprintSettings.MaxSpeed = 585.0f;
	SprintSettings.Acceleration = 300.0f;
	SprintSettings.BrakingDeceleration = 1500.0f;
	SprintSettings.GaitChangeDeceleration = 300.0f;
	SprintSettings.FacingSmoothingTime = 0.8f;
	SprintSettings.TurningStrength = 4.0f;
}

const FHMSGaitMovementSettings& UHMS_WalkingMode::GetSettingsByGait(const EHMS_Gait Gait) const
{
	switch (Gait)
	{
	case EHMS_Gait::Walk:
		return WalkSettings;
	case EHMS_Gait::Sprint:
		return SprintSettings;
	case EHMS_Gait::Run:
	default:
		return RunSettings;
	}
}

void UHMS_WalkingMode::OnRegistered(const FName ModeName, const FMoverSimContext& SimContext)
{
	Super::OnRegistered(ModeName, SimContext);

	URollbackBlackboard::EntrySettings Settings = URollbackBlackboardLibrary::MakeSingleFrameEntrySettings();
	Settings.PersistencePolicy = EBlackboardPersistencePolicy::ThroughNextFrame;
	SimContext.Blackboard.CreateEntry<bool>(DidGenerateMoveEntry, Settings);
}

void UHMS_WalkingMode::SimulationTick_Implementation(
	const FSimulationTickParams& Params,
	FMoverTickEndData& OutputState)
{
	Super::SimulationTick_Implementation(Params, OutputState);

	if (const FHMS_SmoothWalkingState* SmoothState =
		Params.StartState.SyncState.SyncStateCollection.FindDataByType<FHMS_SmoothWalkingState>())
	{
		OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FHMS_SmoothWalkingState>() = *SmoothState;
	}
}

void UHMS_WalkingMode::GenerateMove_Implementation(
	const FMoverSimContext& SimContext,
	const FMoverTickStartData& StartState,
	const FMoverTimeStep& TimeStep,
	FProposedMove& OutProposedMove) const
{
	const FMoverDefaultSyncState* SyncState =
		StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!SyncState)
	{
		return;
	}

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	if (DeltaSeconds <= UE_SMALL_NUMBER)
	{
		return;
	}

	const FCharacterDefaultInputs* DefaultInputs =
		StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const FHMS_MoverInput* HMSInput =
		StartState.InputCmd.InputCollection.FindDataByType<FHMS_MoverInput>();

	FVector RawMoveInput = DefaultInputs
		? DefaultInputs->GetMoveInput_WorldSpace()
		: SyncState->GetIntent_WorldSpace();
	FVector DesiredFacingDirection = DefaultInputs
		? DefaultInputs->GetOrientationIntentDir_WorldSpace()
		: SyncState->GetOrientation_WorldSpace().Quaternion().GetForwardVector();

	const UMoverComponent* Mover = GetMoverComponent();
	const FVector UpDirection = Mover ? Mover->GetUpDirection() : FVector::UpVector;
	RawMoveInput -= RawMoveInput.ProjectOnTo(UpDirection);
	DesiredFacingDirection -= DesiredFacingDirection.ProjectOnTo(UpDirection);

	const bool bHasMoveIntent = HMSInput
		? HMSInput->bHasNavigationInput && !RawMoveInput.IsNearlyZero()
		: !RawMoveInput.IsNearlyZero();
	const EHMS_Gait Gait = HMSInput ? HMSInput->Gait : EHMS_Gait::Run;
	const FHMSGaitMovementSettings& GaitSettings = GetSettingsByGait(Gait);

	float TargetSpeed = HMSInput && HMSInput->bWantsToCrouch
		? CrouchSpeed
		: GaitSettings.MaxSpeed;
	if (HMSInput && HMSInput->RequestedSpeed > UE_KINDA_SMALL_NUMBER)
	{
		TargetSpeed = FMath::Min(TargetSpeed, HMSInput->RequestedSpeed);
	}

	const FVector MoveDirection = RawMoveInput.GetSafeNormal();
	const FVector DesiredVelocity = bHasMoveIntent
		? MoveDirection * TargetSpeed
		: FVector::ZeroVector;

	OutProposedMove.DirectionIntent = bHasMoveIntent ? MoveDirection : FVector::ZeroVector;
	OutProposedMove.bHasDirIntent = bHasMoveIntent;
	OutProposedMove.LinearVelocity = SyncState->GetVelocity_WorldSpace();
	OutProposedMove.AngularVelocityDegrees = SyncState->GetAngularVelocityDegrees_WorldSpace();

	const FQuat CurrentFacing = SyncState->GetOrientation_WorldSpace().Quaternion();
	// Strafe is a movement mode contract: changing the move direction must not
	// rotate the capsule. Keep this invariant inside Mover so a producer that
	// supplies a different OrientationIntent cannot accidentally turn the actor.
	// Aim remains free to consume its externally supplied facing direction.
	if (HMSInput && HMSInput->RotationMode == EHMS_RotationMode::Strafe)
	{
		DesiredFacingDirection = CurrentFacing.GetForwardVector();
	}

	FQuat DesiredFacing = CurrentFacing;
	if (DesiredFacingDirection.Normalize())
	{
		DesiredFacing = FQuat::FindBetween(FVector::ForwardVector, DesiredFacingDirection);
	}

	const float CurrentSpeed = OutProposedMove.LinearVelocity.Size2D();
	const bool bRequestedSlowdown = HMSInput && HMSInput->RequestedSpeed > UE_KINDA_SMALL_NUMBER
		&& HMSInput->RequestedSpeed < GaitSettings.MaxSpeed * 0.99f;
	const float DecelerationAmount = !bHasMoveIntent || bRequestedSlowdown
		? StoppingDeceleration
		: (CurrentSpeed > TargetSpeed + UE_KINDA_SMALL_NUMBER
			? GaitChangeDeceleration
			: GaitSettings.BrakingDeceleration);
	const float FacingTime = bHasMoveIntent
		? GaitSettings.FacingSmoothingTime
		: IdleFacingSmoothingTime;

	GenerateSmoothWalkMove(
		const_cast<FMoverTickStartData&>(StartState), DeltaSeconds, SimContext,
		DesiredVelocity, DesiredFacing, CurrentFacing,
		GaitSettings.Acceleration, DecelerationAmount,
		GaitSettings.TurningStrength, FacingTime,
		OutProposedMove.AngularVelocityDegrees, OutProposedMove.LinearVelocity);

	SimContext.Blackboard.TrySet(DidGenerateMoveEntry, true);
}

void UHMS_WalkingMode::GenerateSmoothWalkMove(
	FMoverTickStartData& StartState,
	const float DeltaSeconds,
	const FMoverSimContext& SimContext,
	const FVector& DesiredVelocity,
	const FQuat& DesiredFacing,
	const FQuat& CurrentFacing,
	const float AccelerationAmount,
	const float DecelerationAmount,
	const float TurningStrengthAmount,
	const float FacingSmoothingTimeAmount,
	FVector& InOutAngularVelocityDegrees,
	FVector& InOutVelocity) const
{
	bool bStateIsNew = false;
	FHMS_SmoothWalkingState& SpringState =
		StartState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FHMS_SmoothWalkingState>(bStateIsNew);

	bool bGeneratedLastFrame = false;
	SimContext.Blackboard.TryGet(DidGenerateMoveEntry, bGeneratedLastFrame);
	if (bStateIsNew || !bGeneratedLastFrame)
	{
		SpringState.SpringVelocity = InOutVelocity;
		SpringState.SpringAcceleration = FVector::ZeroVector;
		SpringState.IntermediateVelocity = InOutVelocity;
		SpringState.IntermediateFacing = CurrentFacing;
		SpringState.IntermediateAngularVelocity = FVector::ZeroVector;
	}

	const float VelocityMatch = FMath::Clamp(
		SpringState.SpringVelocity.Dot(InOutVelocity) /
		FMath::Max(InOutVelocity.Length() * SpringState.SpringVelocity.Length(), UE_SMALL_NUMBER),
		0.0f, 1.0f);
	FMath::ExponentialSmoothingApprox(
		SpringState.IntermediateVelocity, InOutVelocity, DeltaSeconds,
		(OutsideInfluenceSmoothingTime + UE_KINDA_SMALL_NUMBER) / (1.0f - VelocityMatch));
	SpringState.SpringVelocity = InOutVelocity;

	if (TurningStrengthAmount > 0.0f && !DesiredVelocity.IsNearlyZero())
	{
		FMath::ExponentialSmoothingApprox(
			SpringState.IntermediateVelocity,
			DesiredVelocity.GetSafeNormal() * SpringState.IntermediateVelocity.Length(),
			DeltaSeconds,
			SpringMath::StrengthToSmoothingTime(TurningStrengthAmount));
	}

	const bool bAccelerating =
		(1.01f * DesiredVelocity.SquaredLength()) > SpringState.SpringVelocity.SquaredLength();
	const float LateralAccelerationMagnitude = bAccelerating
		? (1.0f - DirectionalAccelerationFactor) * AccelerationAmount
		: DecelerationAmount;
	const float DirectionalAccelerationMagnitude = bAccelerating
		? DirectionalAccelerationFactor * AccelerationAmount
		: 0.0f;
	const float PreviousVelocityLength = SpringState.IntermediateVelocity.Length();
	const FVector VelocityDifference = DesiredVelocity - SpringState.IntermediateVelocity;
	const FVector LateralAcceleration = VelocityDifference.GetSafeNormal() * FMath::Min(
		LateralAccelerationMagnitude,
		VelocityDifference.Length() / FMath::Max(DeltaSeconds, UE_SMALL_NUMBER));
	const FVector DirectionalAcceleration =
		DesiredVelocity.GetSafeNormal() * DirectionalAccelerationMagnitude;
	const FVector DesiredAcceleration = LateralAcceleration + DirectionalAcceleration;

	FVector NextVelocity = VelocityDifference.Dot(DesiredAcceleration * DeltaSeconds) < VelocityDifference.SquaredLength()
		? SpringState.IntermediateVelocity + DesiredAcceleration * DeltaSeconds
		: DesiredVelocity;
	NextVelocity = NextVelocity.GetClampedToMaxSize(
		FMath::Max(PreviousVelocityLength, DesiredVelocity.Length()));

	const float SmoothingTime = bAccelerating
		? AccelerationSmoothingTime
		: DecelerationSmoothingTime;
	const float SmoothingCompensation = bAccelerating
		? AccelerationSmoothingCompensation
		: DecelerationSmoothingCompensation;
	const float LagSeconds = DeltaSeconds + SmoothingCompensation * SmoothingTime;
	FVector TrackVelocity = VelocityDifference.Dot(DesiredAcceleration * LagSeconds) < VelocityDifference.SquaredLength()
		? SpringState.IntermediateVelocity + DesiredAcceleration * LagSeconds
		: DesiredVelocity;
	TrackVelocity = TrackVelocity.GetClampedToMaxSize(
		FMath::Max(PreviousVelocityLength, DesiredVelocity.Length()));

	SpringMath::CriticalSpringDamper(
		SpringState.SpringVelocity, SpringState.SpringAcceleration,
		TrackVelocity, SmoothingTime, DeltaSeconds);
	if ((DesiredVelocity - SpringState.SpringVelocity).SquaredLength() < FMath::Square(VelocityDeadzoneThreshold))
	{
		SpringState.SpringVelocity = DesiredVelocity;
		if (SpringState.SpringAcceleration.SquaredLength() < FMath::Square(AccelerationDeadzoneThreshold))
		{
			SpringState.SpringAcceleration = FVector::ZeroVector;
		}
	}

	InOutVelocity = SpringState.SpringVelocity;
	SpringState.IntermediateVelocity = NextVelocity;

	FVector AngularVelocityRadians = FMath::DegreesToRadians(InOutAngularVelocityDegrees);
	FQuat UpdatedFacing = CurrentFacing;
	if (bSmoothFacingWithDoubleSpring)
	{
		SpringMath::CriticalSpringDamperQuat(
			SpringState.IntermediateFacing, SpringState.IntermediateAngularVelocity,
			DesiredFacing, FacingSmoothingTimeAmount / 2.0f, DeltaSeconds);
		SpringMath::CriticalSpringDamperQuat(
			UpdatedFacing, AngularVelocityRadians, SpringState.IntermediateFacing,
			FacingSmoothingTimeAmount / 2.0f, DeltaSeconds);
	}
	else
	{
		SpringState.IntermediateFacing = DesiredFacing;
		SpringState.IntermediateAngularVelocity = AngularVelocityRadians;
		SpringMath::CriticalSpringDamperQuat(
			UpdatedFacing, AngularVelocityRadians, DesiredFacing,
			FacingSmoothingTimeAmount, DeltaSeconds);
	}

	if (DesiredFacing.AngularDistance(UpdatedFacing) < FMath::DegreesToRadians(FacingDeadzoneThreshold))
	{
		AngularVelocityRadians = ((CurrentFacing.Inverse() * UpdatedFacing)
			.GetShortestArcWith(FQuat::Identity)).ToRotationVector() / DeltaSeconds;
		SpringState.IntermediateFacing = DesiredFacing;
		if (AngularVelocityRadians.SquaredLength() <
			FMath::Square(FMath::DegreesToRadians(AngularVelocityDeadzoneThreshold)))
		{
			SpringState.IntermediateAngularVelocity = FVector::ZeroVector;
		}
	}

	InOutAngularVelocityDegrees = FMath::RadiansToDegrees(AngularVelocityRadians);
}
