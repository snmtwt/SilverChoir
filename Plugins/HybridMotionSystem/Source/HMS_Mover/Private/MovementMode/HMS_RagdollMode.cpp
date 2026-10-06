#include "MovementMode/HMS_RagdollMode.h"

#include "MoveLibrary/MovementUtils.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "Structs/HMS_MoverStructs.h"

UHMS_RagdollMode::UHMS_RagdollMode()
{
	// 蓝图模式读取物理/场景数据，因此样例并不把该段放到异步工作线程。
	bSupportsAsync = false;
}

void UHMS_RagdollMode::SimulationTick_Implementation(
	const FSimulationTickParams& Params,
	FMoverTickEndData& OutputState)
{
	const FMoverDefaultSyncState* StartingSync =
		Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!StartingSync || !Params.MovingComps.UpdatedComponent.IsValid())
	{
		return;
	}

	const FHMS_MoverInput* HMSInput =
		Params.StartState.InputCmd.InputCollection.FindDataByType<FHMS_MoverInput>();
	const FCharacterDefaultInputs* DefaultInputs =
		Params.StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const FTransform StartTransform = StartingSync->GetTransform_WorldSpace();
	const FTransform TargetTransform = HMSInput && HMSInput->bHasRagdollInput
		? HMSInput->RagdollTransform
		: StartTransform;
	const FVector OrientationIntent = DefaultInputs
		? DefaultInputs->OrientationIntent.GetSafeNormal2D()
		: FVector::ZeroVector;
	const FQuat TargetRotation = !OrientationIntent.IsNearlyZero()
		? OrientationIntent.ToOrientationQuat()
		: StartTransform.GetRotation();

	const float DeltaSeconds = FMath::Max(Params.TimeStep.StepMs * 0.001f, UE_SMALL_NUMBER);
	const FVector Delta = TargetTransform.GetLocation() - StartTransform.GetLocation();
	FHitResult MoveHit;

	// ===== 样例蓝图：Try Safe Move And Slide（Sweep=false，Teleport=None） =====
	// 物理骨骼已经完成世界碰撞；胶囊在这里仅同步到身体位置，不能再次阻挡骨骼。
	// NewRotation 来自 CharacterDefaultInputs.OrientationIntent，而不是
	// RagdollTransform.Rotation；这样胶囊不会把物理骨骼的瞬时旋转再次反馈给骨骼。
	UMovementUtils::TrySafeMoveUpdatedComponentNoMovementRecord(
		Params.MovingComps,
		Delta,
		TargetRotation,
		false,
		MoveHit,
		ETeleportType::None);

	const FTransform FinalTransform = Params.MovingComps.UpdatedComponent->GetComponentTransform();
	const FVector LinearVelocity =
		(FinalTransform.GetLocation() - StartTransform.GetLocation()) / DeltaSeconds;
	const FVector AngularVelocity = UMovementUtils::ComputeAngularVelocityDegrees(
		StartTransform.Rotator(), FinalTransform.Rotator(), DeltaSeconds);

	// ===== 样例蓝图：Make Mover Default Sync State / 写回同步状态 =====
	FMoverDefaultSyncState& OutputSync =
		OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	OutputSync.SetTransforms_WorldSpace(
		FinalTransform.GetLocation(), FinalTransform.Rotator(), LinearVelocity, AngularVelocity, nullptr);
	OutputSync.MoveDirectionIntent = FVector::ZeroVector;
	Params.MovingComps.UpdatedComponent->ComponentVelocity = LinearVelocity;
}
