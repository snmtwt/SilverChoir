#include "Components/HMS_AnimationDataComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/PoseSnapshot.h"
#include "Engine/SkeletalMesh.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameplayTagsManager.h"
#include "HMS_AnimInstance.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoveLibrary/PlayMoverMontageCallbackProxy.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Structs/HMS_MoverStructs.h"
#include "TimerManager.h"

UHMS_AnimationDataComponent::UHMS_AnimationDataComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UHMS_AnimationDataComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedMoverComponent = GetOwner() ? GetOwner()->FindComponentByClass<UMoverComponent>() : nullptr;
	CacheRagdollComponents();
	if (CachedMoverComponent)
	{
		CachedMoverComponent->OnBasedMovementApplied.AddDynamic(
			this, &UHMS_AnimationDataComponent::HandleBasedMovementApplied);
	}
	if (CachedMoverComponent && bAutoDetectMoverMovementMode)
	{
		CachedMoverComponent->OnMovementModeChanged.AddDynamic(
			this, &UHMS_AnimationDataComponent::HandleMoverMovementModeChanged);
		AnimationProperties.MovementMode = ResolveMoverMovementMode(CachedMoverComponent->GetMovementModeName());
	}

	if (UCharacterMoverComponent* CharacterMover = Cast<UCharacterMoverComponent>(CachedMoverComponent))
	{
		CharacterMover->OnStanceChanged.AddDynamic(this, &UHMS_AnimationDataComponent::HandleMoverStanceChanged);
		ApplyMoverStance(CharacterMover->IsCrouching() ? EStanceMode::Crouch : EStanceMode::Invalid);
	}
	else if (AnimationProperties.Stance.IsEmpty())
	{
		const FGameplayTag StandTag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("SM.Stance.Stand")), false);
		if (StandTag.IsValid())
		{
			AnimationProperties.Stance.AddTag(StandTag);
		}
	}

	if (const AActor* Owner = GetOwner())
	{
		const FVector Forward = Owner->GetActorForwardVector();
		if (!AnimationProperties.bUseExternalFacingDirection)
		{
			AnimationProperties.FacingDirection = Forward;
		}
		if (!AnimationProperties.bUseExternalAimingDirection)
		{
			AnimationProperties.AimingDirection = Forward;
		}
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FillReplicatedState();
	}
}

void UHMS_AnimationDataComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseGetUpPlayback();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RagdollRotationFreezeTimer);
	}
	if (CachedMoverComponent)
	{
		CachedMoverComponent->OnBasedMovementApplied.RemoveDynamic(
			this, &UHMS_AnimationDataComponent::HandleBasedMovementApplied);
		CachedMoverComponent->OnMovementModeChanged.RemoveDynamic(
			this, &UHMS_AnimationDataComponent::HandleMoverMovementModeChanged);
	}
	CachedMoverComponent = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UHMS_AnimationDataComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (RagdollState == EHMS_RagdollState::Ragdoll)
	{
		UpdateRagdollFromPhysics(DeltaTime);
	}
}

void UHMS_AnimationDataComponent::HandleBasedMovementApplied(
	const FTransform& TransformDelta,
	const FMoverTimeStep& TimeStep)
{
	// Mover guarantees this delegate runs on the game thread. AnimInstance copies
	// the value into its update snapshot before an any-thread graph reads it.
	AnimationProperties.BasedMovementDelta = TransformDelta;
	(void)TimeStep;
}

EHMS_MovementMode UHMS_AnimationDataComponent::ResolveMoverMovementMode(const FName MoverModeName)
{
	if (MoverModeName == DefaultModeNames::Falling)
	{
		return EHMS_MovementMode::InAir;
	}
	if (MoverModeName == DefaultModeNames::Flying)
	{
		return EHMS_MovementMode::Flying;
	}
	if (MoverModeName == DefaultModeNames::Walking || MoverModeName.IsNone())
	{
		return EHMS_MovementMode::OnGround;
	}

	const FString ModeString = MoverModeName.ToString();
	if (ModeString.Contains(TEXT("Ragdoll"), ESearchCase::IgnoreCase))
	{
		return EHMS_MovementMode::Ragdoll;
	}
	if (ModeString.Contains(TEXT("Slide"), ESearchCase::IgnoreCase))
	{
		return EHMS_MovementMode::Sliding;
	}
	if (ModeString.Contains(TEXT("Traversal"), ESearchCase::IgnoreCase)
		|| ModeString.Contains(TEXT("Mantle"), ESearchCase::IgnoreCase)
		|| ModeString.Contains(TEXT("Vault"), ESearchCase::IgnoreCase))
	{
		return EHMS_MovementMode::Traversing;
	}
	if (ModeString.Contains(TEXT("Fly"), ESearchCase::IgnoreCase))
	{
		return EHMS_MovementMode::Flying;
	}
	return EHMS_MovementMode::OnGround;
}

void UHMS_AnimationDataComponent::HandleMoverMovementModeChanged(
	const FName& PreviousMovementModeName,
	const FName& NewMovementModeName)
{
	// ===== 样例蓝图：On_MovementModeChanged_PostFinalize =====
	// 样例不是在 ExitRagdoll 事件里直接关物理，而是在确认离开 Ragdoll 模式后
	// 执行 On_RagdollMode_Exit，保证 Mover 同步状态先接管胶囊。
	if (PreviousMovementModeName == HMSModeNames::Ragdoll
		&& NewMovementModeName != HMSModeNames::Ragdoll
		&& RagdollState == EHMS_RagdollState::Ragdoll)
	{
		ApplyRagdollModeExit();
	}

	if (bAutoDetectMoverMovementMode && RagdollState == EHMS_RagdollState::Animated)
	{
		SetMovementMode(ResolveMoverMovementMode(NewMovementModeName));
	}
}

void UHMS_AnimationDataComponent::HandleMoverStanceChanged(
	const EStanceMode OldStance,
	const EStanceMode NewStance)
{
	(void)OldStance;
	ApplyMoverStance(NewStance);
}

void UHMS_AnimationDataComponent::ApplyMoverStance(const EStanceMode NewStance)
{
	const FName TagName = NewStance == EStanceMode::Crouch
		? FName(TEXT("SM.Stance.Crouch"))
		: FName(TEXT("SM.Stance.Stand"));
	const FGameplayTag StanceTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, false);
	if (!StanceTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer NewStanceContainer;
	NewStanceContainer.AddTag(StanceTag);
	SetStance(NewStanceContainer);
}

void UHMS_AnimationDataComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UHMS_AnimationDataComponent, ReplicatedState, COND_SimulatedOnly);
	DOREPLIFETIME(UHMS_AnimationDataComponent, bUseControllerData);
	DOREPLIFETIME(UHMS_AnimationDataComponent, ReplicatedRagdollInjuryState);
	DOREPLIFETIME(UHMS_AnimationDataComponent, RagdollState);
}

bool UHMS_AnimationDataComponent::GetAnimationProperties(FHMS_AnimationProperties& OutProperties) const
{
	OutProperties = AnimationProperties;
	if (bUseControllerData)
	{
		OutProperties.bUseExternalFacingDirection = false;
		OutProperties.bUseExternalAimingDirection = false;
	}
	else
	{
		OutProperties.bUseExternalFacingDirection = true;
		OutProperties.bUseExternalAimingDirection = true;
		OutProperties.FacingDirection = GetResolvedFacingDirection();
		OutProperties.AimingDirection = GetResolvedAimingDirection();
	}
	return true;
}

void UHMS_AnimationDataComponent::SetAnimationProperties(const FHMS_AnimationProperties& NewProperties)
{
	AnimationProperties = NewProperties;
	PublishSemanticState();
	TryPublishDirectionalState();
}

void UHMS_AnimationDataComponent::SetWorldSpaceMovementInput(const FVector& WorldDirectionAndMagnitude)
{
	AnimationProperties.bUseWorldSpaceMovementInput = true;
	AnimationProperties.WorldSpaceMovementInput = WorldDirectionAndMagnitude.GetClampedToMaxSize(1.0f);
	TryPublishDirectionalState();
}

void UHMS_AnimationDataComponent::SetPlayerInputAcceleration(const FVector2D& LocalInput)
{
	AnimationProperties.bUseWorldSpaceMovementInput = false;
	AnimationProperties.PlayerInputAcceleration = LocalInput.GetClampedToMaxSize(1.0f);
	TryPublishDirectionalState();
}

void UHMS_AnimationDataComponent::SetFacingAndAimingDirections(
	const FVector& WorldFacingDirection,
	const FVector& WorldAimingDirection)
{
	if (!WorldFacingDirection.IsNearlyZero())
	{
		AnimationProperties.FacingDirection = WorldFacingDirection.GetSafeNormal();
		AnimationProperties.bUseExternalFacingDirection = true;
	}
	if (!WorldAimingDirection.IsNearlyZero())
	{
		AnimationProperties.AimingDirection = WorldAimingDirection.GetSafeNormal();
		AnimationProperties.bUseExternalAimingDirection = true;
	}
	bUseControllerData = false;
	TryPublishDirectionalState();
}

void UHMS_AnimationDataComponent::SetMovementMode(const EHMS_MovementMode NewMovementMode)
{
	if (AnimationProperties.MovementMode != NewMovementMode)
	{
		AnimationProperties.MovementMode = NewMovementMode;
		PublishSemanticState();
	}
}

void UHMS_AnimationDataComponent::SetRagdollProperties(const FHMS_RagdollProperties& NewProperties)
{
	AnimationProperties.RagdollProperties = NewProperties;
}

// ============================================================================
// UE 5.8 SandboxCharacter_Mover_Ragdoll：TriggerRagdoll
// 蓝图顺序：停止 Montage -> 写 InjuryState -> SetPhysicsProfile(Ragdoll)
// -> 立即开启物理 -> QueueNextMovementMode(Ragdoll) -> 首 0.25 秒冻结胶囊旋转。
// ============================================================================
void UHMS_AnimationDataComponent::EnterRagdoll(const EHMS_RagdollInjuryState InjuryState)
{
	if (!GetOwner() || RagdollState == EHMS_RagdollState::Ragdoll)
	{
		return;
	}
	if (!GetOwner()->HasAuthority())
	{
		ServerEnterRagdoll(InjuryState);
		return;
	}
	ApplyEnterRagdoll(InjuryState);
}

void UHMS_AnimationDataComponent::ServerEnterRagdoll_Implementation(
	const EHMS_RagdollInjuryState InjuryState)
{
	if (RagdollState != EHMS_RagdollState::Ragdoll)
	{
		ApplyEnterRagdoll(InjuryState);
		GetOwner()->ForceNetUpdate();
	}
}

void UHMS_AnimationDataComponent::ApplyEnterRagdoll(
	const EHMS_RagdollInjuryState InjuryState)
{
	CacheRagdollComponents();
	if (!RagdollMesh || !RagdollCapsule || !CachedMoverComponent
		|| !RagdollMesh->GetPhysicsAsset()
		|| RagdollMesh->GetPhysicsAsset()->FindBodyIndex(RagdollPelvisBone) == INDEX_NONE
		|| RagdollMesh->GetBoneIndex(RagdollSpineBone) == INDEX_NONE)
	{
		return;
	}

	// Remove callbacks before stopping a previous get-up, so its interruption
	// cannot unlock this new ragdoll. Stop its movement as well as its animation.
	ReleaseGetUpPlayback();
	CachedMoverComponent->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage, false);
	if (UAnimInstance* AnimInstance = RagdollMesh->GetAnimInstance())
	{
		// 样例会用指定 BlendSettings 停止活跃 Montage；C++ 使用相同的短 BlendOut。
		AnimInstance->Montage_Stop(0.2f);
	}

	FHMS_RagdollProperties& Properties = AnimationProperties.RagdollProperties;
	Properties = FHMS_RagdollProperties();
	Properties.State = EHMS_RagdollState::Ragdoll;
	Properties.InjuryState = InjuryState;
	ReplicatedRagdollInjuryState = InjuryState;
	RagdollState = EHMS_RagdollState::Ragdoll;
	AnimationProperties.MovementMode = EHMS_MovementMode::Ragdoll;

	OriginalMeshRelativeTransform = RagdollMesh->GetRelativeTransform();
	ResolvedRagdollFacingAxis = RagdollChestFacingAxis.GetSafeNormal();
	if (bAutoDetectRagdollFacingAxis && RagdollMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& Ref = RagdollMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		FTransform RefSpine = FTransform::Identity;
		for (int32 Bone = Ref.FindBoneIndex(RagdollSpineBone); Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
		{
			RefSpine = RefSpine * Ref.GetRefBonePose()[Bone];
		}
		const FVector MeshForward = OriginalMeshRelativeTransform.GetRotation().UnrotateVector(FVector::ForwardVector);
		ResolvedRagdollFacingAxis = RefSpine.GetRotation().UnrotateVector(MeshForward).GetSafeNormal();
	}
	OriginalMeshCollisionProfile = RagdollMesh->GetCollisionProfileName();
	OriginalMeshCollisionEnabled = RagdollMesh->GetCollisionEnabled();
	OriginalCapsuleCollisionProfile = RagdollCapsule->GetCollisionProfileName();
	OriginalCapsuleCollisionEnabled = RagdollCapsule->GetCollisionEnabled();

	RagdollAttachmentCollision.Reset();
	if (bSuppressAttachmentPhysicsCollision)
	{
		TArray<UPrimitiveComponent*> Components;
		GetOwner()->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component != RagdollMesh && Component->IsAttachedTo(RagdollMesh)
				&& !Component->IsSimulatingPhysics() && CollisionEnabledHasPhysics(Component->GetCollisionEnabled()))
			{
				const ECollisionEnabled::Type Collision = Component->GetCollisionEnabled();
				RagdollAttachmentCollision.Add(Component, Collision);
				Component->SetCollisionEnabled(CollisionEnabledHasQuery(Collision)
					? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			}
		}
	}

	RagdollCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// Mover 必须继续模拟胶囊，但它的网络视觉平滑层不能再搬动物理 Mesh。
	// 这与“关闭 Mover”不同：Ragdoll 模式和同步状态仍正常逐帧运行。
	CachedMoverComponent->SetPrimaryVisualComponent(nullptr);
	// The capsule follows the simulated pelvis. Keep that parent movement from
	// being applied to the same rigid bodies again through the attachment tree.
	RagdollMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	RagdollMesh->SetCollisionProfileName(RagdollCollisionProfile);
	RagdollMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// 样例中特别强调：Physics Control Profile 在自己的 Tick 才完全应用，
	// 所以这里仍需立即 SetSimulatePhysics，避免同帧施加的冲量被丢失。
	InitializePhysicsControl();
	SetPhysicsProfile(RagdollPhysicsProfile);
	RagdollMesh->SetAllBodiesSimulatePhysics(true);
	RagdollMesh->SetSimulatePhysics(true);
	RagdollMesh->WakeAllRigidBodies();

	PreviousRagdollCenterOfMass = RagdollMesh->GetSkeletalCenterOfMass();
	bHasPreviousRagdollCenterOfMass = true;
	FrozenRagdollCapsuleRotation = GetOwner()->GetActorRotation();
	bFreezeRagdollCapsuleRotation = true;
	bool bFaceUp = true;
	CachedRagdollMoverTransform = CalculateRagdollCapsuleTransform(bFaceUp);
	Properties.bFaceUp = bFaceUp;

	CachedMoverComponent->QueueNextMode(HMSModeNames::Ragdoll);
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			RagdollRotationFreezeTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				bFreezeRagdollCapsuleRotation = false;
			}),
			0.25f,
			false);
	}
	PublishSemanticState();
}

// ============================================================================
// UE 5.8 SandboxCharacter_Mover_Ragdoll：ExitRagdoll
// 蓝图这里只排队 Walking。实际快照、Chooser、Physics Profile 恢复全部由
// HandleMoverMovementModeChanged -> ApplyRagdollModeExit 执行。
// ============================================================================
void UHMS_AnimationDataComponent::ExitRagdoll()
{
	if (!GetOwner() || RagdollState != EHMS_RagdollState::Ragdoll)
	{
		return;
	}
	if (!GetOwner()->HasAuthority())
	{
		ServerExitRagdoll();
		return;
	}
	if (CachedMoverComponent)
	{
		CachedMoverComponent->QueueNextMode(DefaultModeNames::Walking);
	}
}

void UHMS_AnimationDataComponent::ServerExitRagdoll_Implementation()
{
	ExitRagdoll();
}

void UHMS_AnimationDataComponent::ToggleRagdoll()
{
	IsRagdoll() ? ExitRagdoll() : EnterRagdoll(AnimationProperties.RagdollProperties.InjuryState);
}

void UHMS_AnimationDataComponent::SetRagdollRollInput(const FVector& WorldRollInput)
{
	RagdollRollInput = WorldRollInput.GetClampedToMaxSize(1.0f);
}

FVector UHMS_AnimationDataComponent::GetRagdollTargetOrientation() const
{
	// ===== 样例蓝图：Get_RagdollTargetOrientation =====
	// 1. 刚进入布娃娃时保持 Actor 原朝向，避免首帧物理姿势尚未稳定。
	if (bFreezeRagdollCapsuleRotation)
	{
		return GetOwner() ? GetOwner()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}

	// 2. Ragdoll_PlayRollingGetups：质心水平速度超过 150 时，沿滚动方向定向。
	const FVector CenterOfMassVelocity = AnimationProperties.RagdollProperties.CenterOfMassVelocity;
	if (CenterOfMassVelocity.Size2D() > 150.0f)
	{
		return CenterOfMassVelocity.GetSafeNormal2D();
	}

	// 3. 普通倒地时使用 Get_RagdollTransform 的 X 轴，但不直接把该旋转传给物理 Mesh。
	const FVector TransformForward = CachedRagdollMoverTransform.GetUnitAxis(EAxis::X).GetSafeNormal2D();
	return !TransformForward.IsNearlyZero()
		? TransformForward
		: (GetOwner() ? GetOwner()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector);
}

void UHMS_AnimationDataComponent::FillRagdollMoverInput(FHMS_MoverInput& OutInput) const
{
	OutInput.bHasRagdollInput = RagdollState == EHMS_RagdollState::Ragdoll;
	OutInput.RagdollTransform = CachedRagdollMoverTransform;
	OutInput.RagdollRollAmount = AnimationProperties.RagdollProperties.RollAmount;
}

// ============================================================================
// UE 5.8 SandboxCharacter_Mover_Ragdoll：On_RagdollMode_Exit
// 蓝图顺序：恢复胶囊碰撞 -> SavePoseSnapshot -> OverridePoseHistory
// -> Chooser 选择起身 Montage -> 播放 -> 下一帧恢复 Default Physics Profile。
// ============================================================================
void UHMS_AnimationDataComponent::ApplyRagdollModeExit()
{
	if (!RagdollMesh || !RagdollCapsule)
	{
		return;
	}

	RagdollState = EHMS_RagdollState::GettingUp;
	FHMS_RagdollProperties& Properties = AnimationProperties.RagdollProperties;
	Properties.State = EHMS_RagdollState::GettingUp;
	AnimationProperties.MovementMode = EHMS_MovementMode::OnGround;

	const FTransform SnapshotMeshTransform = RagdollMesh->GetComponentTransform();
	UAnimInstance* AnimInstance = RagdollMesh->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->SavePoseSnapshot(PoseSnapshotName);
		UPoseSearchLibrary::OverridePoseHistoryFromOwningMesh(AnimInstance, PoseHistoryName);
	}

	RagdollMesh->SetAllBodiesSimulatePhysics(false);
	RagdollMesh->SetSimulatePhysics(false);
	for (const auto& Attachment : RagdollAttachmentCollision)
	{
		if (UPrimitiveComponent* Component = Attachment.Key.Get())
		{
			Component->SetCollisionEnabled(Attachment.Value);
		}
	}
	RagdollAttachmentCollision.Reset();
	if (RagdollMesh->GetAttachParent() != RagdollCapsule)
	{
		RagdollMesh->AttachToComponent(RagdollCapsule, FAttachmentTransformRules::KeepWorldTransform);
	}
	RagdollMesh->SetCollisionProfileName(OriginalMeshCollisionProfile);
	RagdollMesh->SetCollisionEnabled(OriginalMeshCollisionEnabled);
	RagdollMesh->SetRelativeTransform(OriginalMeshRelativeTransform);
	if (AnimInstance)
	{
		// The detached physics mesh and restored animated mesh have different
		// component frames. Rebase the snapshot without changing its world pose.
		if (const FPoseSnapshot* Saved = AnimInstance->GetPoseSnapshot(PoseSnapshotName);
			Saved && Saved->bIsValid && !Saved->LocalTransforms.IsEmpty())
		{
			FPoseSnapshot Rebased = *Saved;
			Rebased.LocalTransforms[0] = (Rebased.LocalTransforms[0] * SnapshotMeshTransform)
				.GetRelativeTransform(RagdollMesh->GetComponentTransform());
			// Keep the snapshot root at the reference pose, like root-locked get-up
			// sequences. Carry its compensation into direct children instead, so
			// blending cannot swing the whole body around an offset root pivot.
			const FReferenceSkeleton& Ref = RagdollMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const FTransform RebasedRoot = Rebased.LocalTransforms[0];
			const FTransform ReferenceRoot = Ref.GetRefBonePose()[0];
			for (int32 Index = 1; Index < Rebased.BoneNames.Num(); ++Index)
			{
				const int32 BoneIndex = Ref.FindBoneIndex(Rebased.BoneNames[Index]);
				if (BoneIndex != INDEX_NONE && Ref.GetParentIndex(BoneIndex) == 0)
				{
					Rebased.LocalTransforms[Index] = (Rebased.LocalTransforms[Index] * RebasedRoot)
						.GetRelativeTransform(ReferenceRoot);
				}
			}
			Rebased.LocalTransforms[0] = ReferenceRoot;
			AnimInstance->AddPoseSnapshot(PoseSnapshotName) = MoveTemp(Rebased);
		}
	}
	if (CachedMoverComponent)
	{
		CachedMoverComponent->SetPrimaryVisualComponent(RagdollMesh);
	}
	RagdollCapsule->SetCollisionProfileName(
		OriginalCapsuleCollisionProfile.IsNone() ? CharacterCapsuleCollisionProfile : OriginalCapsuleCollisionProfile);
	RagdollCapsule->SetCollisionEnabled(OriginalCapsuleCollisionEnabled);

	StartGetUpMontage();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
			this, [this]() { SetPhysicsProfile(DefaultPhysicsProfile); }));
	}
	PublishSemanticState();
}

void UHMS_AnimationDataComponent::StartGetUpMontage()
{
	if (!RagdollMesh || RagdollState != EHMS_RagdollState::GettingUp)
	{
		return;
	}
	UAnimInstance* AnimInstance = RagdollMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		FinishGetUp();
		return;
	}

	float StartTime = 0.0f;
	UAnimMontage* Montage = nullptr;
	const bool bRolling = AnimationProperties.RagdollProperties.CenterOfMassVelocity.Size2D() > 150.0f;
	if (UHMS_AnimInstance* HMSAnimInstance = Cast<UHMS_AnimInstance>(AnimInstance))
	{
		// ===== 样例蓝图：Evaluate Chooser CHT_GetUpMontages（PoseHistory 为上下文） =====
		Montage = HMSAnimInstance->ResolveRagdollGetUpAnimation(
			AnimationProperties.RagdollProperties.bFaceUp, bRolling, StartTime);
	}
	auto IsCompatibleMontage = [this](const UAnimMontage* Candidate)
	{
		return Candidate && Candidate->GetSkeleton()
			&& Candidate->GetSkeleton()->IsCompatibleMesh(RagdollMesh->GetSkeletalMeshAsset());
	};
	if (!IsCompatibleMontage(Montage))
	{
		Montage = AnimationProperties.RagdollProperties.bFaceUp
			? FaceUpGetUpMontage : FaceDownGetUpMontage;
		StartTime = 0.0f;
	}

	ReleaseGetUpPlayback();
	if (IsCompatibleMontage(Montage) && CachedMoverComponent)
	{
		StartTime = FMath::Clamp(StartTime, 0.0f,
			FMath::Max(0.0f, Montage->GetPlayLength() - UE_SMALL_NUMBER));
		GetUpPlaybackProxy = UPlayMoverMontageCallbackProxy::CreateProxyObjectForPlayMoverMontage(
			CachedMoverComponent, Montage, 1.f, StartTime);
		if (GetUpPlaybackProxy && AnimInstance->Montage_IsPlaying(Montage))
		{
			GetUpPlaybackProxy->OnCompleted.AddDynamic(this, &UHMS_AnimationDataComponent::HandleGetUpPlaybackEnded);
			GetUpPlaybackProxy->OnInterrupted.AddDynamic(this, &UHMS_AnimationDataComponent::HandleGetUpPlaybackEnded);
			return;
		}
	}
	// Failed playback must never leave movement locked in GettingUp.
	FinishGetUp();
}

void UHMS_AnimationDataComponent::ReleaseGetUpPlayback()
{
	if (GetUpPlaybackProxy)
	{
		GetUpPlaybackProxy->OnCompleted.RemoveDynamic(this, &UHMS_AnimationDataComponent::HandleGetUpPlaybackEnded);
		GetUpPlaybackProxy->OnInterrupted.RemoveDynamic(this, &UHMS_AnimationDataComponent::HandleGetUpPlaybackEnded);
		GetUpPlaybackProxy = nullptr;
	}
}

void UHMS_AnimationDataComponent::HandleGetUpPlaybackEnded(FName NotifyName)
{
	FinishGetUp();
}

void UHMS_AnimationDataComponent::FinishGetUp()
{
	ReleaseGetUpPlayback();
	if (RagdollState != EHMS_RagdollState::GettingUp) { return; }
	RagdollState = EHMS_RagdollState::Animated;
	AnimationProperties.RagdollProperties.State = EHMS_RagdollState::Animated;
	AnimationProperties.MovementMode = CachedMoverComponent
		? ResolveMoverMovementMode(CachedMoverComponent->GetMovementModeName())
		: EHMS_MovementMode::OnGround;
	RagdollRollInput = FVector::ZeroVector;
	bHasPreviousRagdollCenterOfMass = false;
	PublishSemanticState();
}

// ============================================================================
// UE 5.8 SandboxCharacter_Mover_Ragdoll：Ragdoll_UpdateProperties
// ============================================================================
void UHMS_AnimationDataComponent::UpdateRagdollFromPhysics(const float DeltaTime)
{
	if (!RagdollMesh)
	{
		return;
	}
	FHMS_RagdollProperties& Properties = AnimationProperties.RagdollProperties;
	Properties.State = EHMS_RagdollState::Ragdoll;
	Properties.SpineVelocity = RagdollMesh->GetPhysicsLinearVelocity(RagdollSpineBone);
	Properties.Speed = Properties.SpineVelocity.Size();

	const FVector CenterOfMass = RagdollMesh->GetSkeletalCenterOfMass();
	Properties.CenterOfMassVelocity = bHasPreviousRagdollCenterOfMass && DeltaTime > UE_SMALL_NUMBER
		? (CenterOfMass - PreviousRagdollCenterOfMass) / DeltaTime
		: Properties.SpineVelocity;
	PreviousRagdollCenterOfMass = CenterOfMass;
	bHasPreviousRagdollCenterOfMass = true;

	bool bFaceUp = true;
	CachedRagdollMoverTransform = CalculateRagdollCapsuleTransform(bFaceUp);
	Properties.bFaceUp = bFaceUp;
	Properties.RollAmount = RagdollRollInput.Size();

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HMSRagdollGround), false, GetOwner());
	QueryParams.AddIgnoredComponent(static_cast<const UPrimitiveComponent*>(RagdollMesh.Get()));
	const FVector PelvisLocation = RagdollMesh->GetBoneLocation(RagdollPelvisBone);
	Properties.bOnGround = GetWorld() && GetWorld()->SweepSingleByChannel(
		GroundHit,
		PelvisLocation,
		PelvisLocation - FVector(0.0f, 0.0f, RagdollGroundTraceDistance),
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(RagdollGroundSphereRadius),
		QueryParams);

	// ===== 样例蓝图：Ragdoll_TimeToImpact / ImpactDirection =====
	Properties.TimeToImpact = RagdollImpactPredictionTime;
	if (Properties.bOnGround)
	{
		Properties.TimeToImpact = 0.0f;
	}
	else if (Properties.SpineVelocity.Z < -UE_SMALL_NUMBER)
	{
		Properties.TimeToImpact = FMath::Clamp(
			(PelvisLocation.Z - CachedRagdollMoverTransform.GetLocation().Z)
			/ -Properties.SpineVelocity.Z,
			0.0f,
			RagdollImpactPredictionTime);
	}
	const FVector LocalImpact = RagdollMesh->GetComponentTransform().InverseTransformVectorNoScale(
		Properties.SpineVelocity.GetSafeNormal());
	Properties.ImpactDirection = FVector2D(LocalImpact.X, LocalImpact.Y).GetSafeNormal();

	if (!RagdollRollInput.IsNearlyZero())
	{
		const FVector TorqueAxis = FVector::CrossProduct(
			FVector::UpVector, RagdollRollInput.GetSafeNormal());
		RagdollMesh->AddTorqueInDegrees(
			TorqueAxis * 100000.0f * RagdollRollInput.Size(), RagdollSpineBone, true);
	}
	UpdateRagdollPhysicsStrengthsFromCurves();
}

// ============================================================================
// UE 5.8 样例：Ragdoll_UpdatePhysicsStrengthsFromCurves
// 受伤状态在 AnimGraph 中选择目标姿势；四条曲线只缩放对应控制集的角强度。
// ============================================================================
void UHMS_AnimationDataComponent::UpdateRagdollPhysicsStrengthsFromCurves()
{
	if (!PhysicsControl || !bPhysicsControlInitialized || AppliedPhysicsProfile != RagdollPhysicsProfile
		|| !RagdollMesh)
	{
		return;
	}
	const UAnimInstance* AnimInstance = RagdollMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}
	const TPair<FName, FName> CurveToSet[] = {
		{TEXT("Ragdoll_Strength_Head"), TEXT("ParentSpace_Head")},
		{TEXT("Ragdoll_Strength_Torso"), TEXT("ParentSpace_Torso")},
		{TEXT("Ragdoll_Strength_Arms"), TEXT("ParentSpace_Arms")},
		{TEXT("Ragdoll_Strength_Legs"), TEXT("ParentSpace_Legs")}
	};
	for (const TPair<FName, FName>& Pair : CurveToSet)
	{
		FPhysicsControlMultiplier Multiplier;
		Multiplier.AngularStrengthMultiplier = FMath::Max(AnimInstance->GetCurveValue(Pair.Key), 0.0f);
		PhysicsControl->SetControlMultipliersInSet(Pair.Value, Multiplier, false);
	}
}

// ============================================================================
// UE 5.8 样例：BP_MovementMode_Ragdoll 的 TargetCapsuleTransform 计算输入。
// 物理骨骼决定 XY/朝向，向下球扫决定胶囊中心高度；首 0.25 秒保持进入朝向。
// ============================================================================
FTransform UHMS_AnimationDataComponent::CalculateRagdollCapsuleTransform(bool& bOutFaceUp) const
{
	bOutFaceUp = true;
	if (!RagdollMesh || !GetOwner())
	{
		return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	}

	FTransform Pelvis = RagdollMesh->GetBoneTransform(RagdollMesh->GetBoneIndex(RagdollPelvisBone));
	FTransform Spine = RagdollMesh->GetBoneTransform(RagdollMesh->GetBoneIndex(RagdollSpineBone));
	if (const FBodyInstance* Body = RagdollMesh->GetBodyInstance(RagdollPelvisBone))
	{
		Pelvis = Body->GetUnrealWorldTransform(false, true);
	}
	if (const FBodyInstance* Body = RagdollMesh->GetBodyInstance(RagdollSpineBone))
	{
		Spine = Body->GetUnrealWorldTransform(false, true);
	}
	const float ChestUp = Spine.TransformVectorNoScale(ResolvedRagdollFacingAxis).Z;
	// Preserve the last classification near a side-on pose, rather than flipping every frame.
	bOutFaceUp = FMath::Abs(ChestUp) < 0.1f
		? AnimationProperties.RagdollProperties.bFaceUp : ChestUp > 0.f;

	FVector Location = Pelvis.GetLocation();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HMSRagdollCapsuleTarget), false, GetOwner());
	QueryParams.AddIgnoredComponent(static_cast<const UPrimitiveComponent*>(RagdollMesh.Get()));
	FHitResult Hit;
	if (GetWorld() && GetWorld()->SweepSingleByChannel(
		Hit,
		Location + FVector(0.0f, 0.0f, RagdollGroundSphereRadius),
		Location - FVector(0.0f, 0.0f, RagdollGroundTraceDistance),
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(RagdollGroundSphereRadius),
		QueryParams))
	{
		const float HalfHeight = Cast<UCapsuleComponent>(RagdollCapsule)
			? CastChecked<UCapsuleComponent>(RagdollCapsule)->GetScaledCapsuleHalfHeight()
			: 88.0f;
		Location.Z = Hit.ImpactPoint.Z + HalfHeight;
	}

	// ===== 样例蓝图：Get_RagdollTransform =====
	// 样例并非始终使用 -(Spine-Pelvis)。它根据 Spine RightVector.Z 在
	// 正向/反向身体轴之间选择，再以 MakeRotFromZX 构造只含水平朝向的变换。
	const FVector PelvisToSpine = Spine.GetLocation() - Pelvis.GetLocation();
	const FVector SelectedBodyAxis = bOutFaceUp ? -PelvisToSpine : PelvisToSpine;
	FRotator Rotation = FrozenRagdollCapsuleRotation;
	if (SelectedBodyAxis.SizeSquared2D() > 1.f)
	{
		Rotation = FRotationMatrix::MakeFromZX(FVector::UpVector, SelectedBodyAxis).Rotator();
	}
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	return FTransform(Rotation, Location, GetOwner()->GetActorScale3D());
}

void UHMS_AnimationDataComponent::SetPhysicsProfile(const FName ProfileName)
{
	// ===== 样例蓝图：Set Controls Profile =====
	if (PhysicsControl && bPhysicsControlInitialized && !ProfileName.IsNone())
	{
		PhysicsControl->InvokeControlProfile(ProfileName);
		AppliedPhysicsProfile = ProfileName;
	}
}

bool UHMS_AnimationDataComponent::InitializePhysicsControl()
{
	// ===== 样例蓝图：Create Controls And Body Modifiers From Physics Control Asset =====
	if (bPhysicsControlInitialized)
	{
		return true;
	}
	if (!PhysicsControl || PhysicsControlAsset.IsNull() || !RagdollMesh)
	{
		return false;
	}
	PhysicsControl->PhysicsControlAsset = PhysicsControlAsset;
	bPhysicsControlInitialized = PhysicsControl->CreateControlsAndBodyModifiersFromPhysicsControlAsset(
		RagdollMesh, RagdollCapsule, NAME_None);
	return bPhysicsControlInitialized;
}

void UHMS_AnimationDataComponent::CacheRagdollComponents()
{
	// ===== 样例蓝图：缓存 Mesh、胶囊与 PhysicsControl 引用 =====
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (!RagdollMesh)
	{
		RagdollMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	}
	if (!RagdollCapsule)
	{
		RagdollCapsule = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	}
	if (!PhysicsControl)
	{
		PhysicsControl = Owner->FindComponentByClass<UPhysicsControlComponent>();
	}
	if (!PhysicsControl && !PhysicsControlAsset.IsNull())
	{
		PhysicsControl = NewObject<UPhysicsControlComponent>(Owner, TEXT("HMSPhysicsControlRuntime"));
		Owner->AddInstanceComponent(PhysicsControl);
		PhysicsControl->RegisterComponent();
	}
	if (RagdollMesh && RagdollMesh->GetBoneIndex(RagdollSpineBone) == INDEX_NONE)
	{
		RagdollSpineBone = RagdollMesh->GetBoneIndex(TEXT("spine_03")) != INDEX_NONE
			? TEXT("spine_03") : RagdollPelvisBone;
	}
}

void UHMS_AnimationDataComponent::OnRep_RagdollState(const EHMS_RagdollState PreviousState)
{
	// ===== 样例蓝图的网络入口：远端也执行相同 TriggerRagdoll 物理初始化 =====
	if (RagdollState == EHMS_RagdollState::Ragdoll && PreviousState != EHMS_RagdollState::Ragdoll)
	{
		ApplyEnterRagdoll(ReplicatedRagdollInjuryState);
	}
}

void UHMS_AnimationDataComponent::SetRotationMode(const EHMS_RotationMode NewRotationMode)
{
	if (AnimationProperties.RotationMode != NewRotationMode)
	{
		AnimationProperties.RotationMode = NewRotationMode;
		PublishSemanticState();
	}
}

void UHMS_AnimationDataComponent::SetGait(const EHMS_Gait NewGait)
{
	if (AnimationProperties.Gait != NewGait)
	{
		AnimationProperties.Gait = NewGait;
		PublishSemanticState();
	}
}

void UHMS_AnimationDataComponent::SetStance(const FGameplayTagContainer& NewStance)
{
	if (AnimationProperties.Stance != NewStance)
	{
		AnimationProperties.Stance = NewStance;
		PublishSemanticState();
	}
}

FVector UHMS_AnimationDataComponent::GetResolvedFacingDirection() const
{
	if (bUseControllerData)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (const AController* Controller = Pawn ? Pawn->GetController() : nullptr)
		{
			return Controller->GetControlRotation().Vector().GetSafeNormal();
		}
	}
	return AnimationProperties.FacingDirection.GetSafeNormal();
}

FVector UHMS_AnimationDataComponent::GetResolvedAimingDirection() const
{
	if (bUseControllerData)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (const AController* Controller = Pawn ? Pawn->GetController() : nullptr)
		{
			return Controller->GetControlRotation().Vector().GetSafeNormal();
		}
	}
	return AnimationProperties.AimingDirection.GetSafeNormal();
}

void UHMS_AnimationDataComponent::PublishSemanticState()
{
	AActor* Owner = GetOwner();
	if (!bEnableNetworkSync || !Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		FillReplicatedState();
		Owner->ForceNetUpdate();
	}
	else if (IsLocallyControlledOwner())
	{
		ServerSetSemanticState(AnimationProperties.MovementMode, AnimationProperties.RotationMode,
			AnimationProperties.Gait, AnimationProperties.AccelerationSource, AnimationProperties.Stance);
	}
}

void UHMS_AnimationDataComponent::TryPublishDirectionalState()
{
	AActor* Owner = GetOwner();
	if (!bEnableNetworkSync || !Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		FillReplicatedState();
		return;
	}
	if (!IsLocallyControlledOwner() || !GetWorld())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double MinimumInterval = 1.0 / FMath::Max(DirectionNetUpdateRate, 1.0f);
	if (LastDirectionalSendTime >= 0.0 && Now - LastDirectionalSendTime < MinimumInterval)
	{
		return;
	}
	LastDirectionalSendTime = Now;
	ServerSetDirectionalState(AnimationProperties.PlayerInputAcceleration,
		AnimationProperties.WorldSpaceMovementInput.GetSafeNormal(),
		AnimationProperties.FacingDirection.GetSafeNormal(),
		AnimationProperties.AimingDirection.GetSafeNormal(),
		AnimationProperties.bUseWorldSpaceMovementInput,
		AnimationProperties.bUseExternalFacingDirection,
		AnimationProperties.bUseExternalAimingDirection,
		bUseControllerData);
}

void UHMS_AnimationDataComponent::FillReplicatedState()
{
	ReplicatedState.MovementMode = AnimationProperties.MovementMode;
	ReplicatedState.RotationMode = AnimationProperties.RotationMode;
	ReplicatedState.Gait = AnimationProperties.Gait;
	ReplicatedState.AccelerationSource = AnimationProperties.AccelerationSource;
	ReplicatedState.Stance = AnimationProperties.Stance;
	ReplicatedState.PlayerInputAcceleration = AnimationProperties.PlayerInputAcceleration;
	ReplicatedState.WorldSpaceMovementInput = AnimationProperties.WorldSpaceMovementInput.GetSafeNormal();
	ReplicatedState.FacingDirection = AnimationProperties.FacingDirection.GetSafeNormal();
	ReplicatedState.AimingDirection = AnimationProperties.AimingDirection.GetSafeNormal();
	ReplicatedState.bUseWorldSpaceMovementInput = AnimationProperties.bUseWorldSpaceMovementInput;
	ReplicatedState.bUseExternalFacingDirection = AnimationProperties.bUseExternalFacingDirection;
	ReplicatedState.bUseExternalAimingDirection = AnimationProperties.bUseExternalAimingDirection;
}

void UHMS_AnimationDataComponent::ApplyReplicatedState()
{
	AnimationProperties.MovementMode = ReplicatedState.MovementMode;
	AnimationProperties.RotationMode = ReplicatedState.RotationMode;
	AnimationProperties.Gait = ReplicatedState.Gait;
	AnimationProperties.AccelerationSource = ReplicatedState.AccelerationSource;
	AnimationProperties.Stance = ReplicatedState.Stance;
	AnimationProperties.PlayerInputAcceleration = ReplicatedState.PlayerInputAcceleration;
	AnimationProperties.WorldSpaceMovementInput = ReplicatedState.WorldSpaceMovementInput;
	AnimationProperties.FacingDirection = ReplicatedState.FacingDirection;
	AnimationProperties.AimingDirection = ReplicatedState.AimingDirection;
	AnimationProperties.bUseWorldSpaceMovementInput = ReplicatedState.bUseWorldSpaceMovementInput;
	AnimationProperties.bUseExternalFacingDirection = ReplicatedState.bUseExternalFacingDirection;
	AnimationProperties.bUseExternalAimingDirection = ReplicatedState.bUseExternalAimingDirection;
}

bool UHMS_AnimationDataComponent::IsLocallyControlledOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

void UHMS_AnimationDataComponent::OnRep_ReplicatedState()
{
	ApplyReplicatedState();
}

void UHMS_AnimationDataComponent::ServerSetSemanticState_Implementation(
	const EHMS_MovementMode NewMovementMode,
	const EHMS_RotationMode NewRotationMode,
	const EHMS_Gait NewGait,
	const EHMS_AccelerationSource NewAccelerationSource,
	const FGameplayTagContainer& NewStance)
{
	AnimationProperties.MovementMode = NewMovementMode;
	AnimationProperties.RotationMode = NewRotationMode;
	AnimationProperties.Gait = NewGait;
	AnimationProperties.AccelerationSource = NewAccelerationSource;
	AnimationProperties.Stance = NewStance;
	FillReplicatedState();
}

void UHMS_AnimationDataComponent::ServerSetDirectionalState_Implementation(
	const FVector2D NewPlayerInput,
	const FVector_NetQuantizeNormal NewWorldInput,
	const FVector_NetQuantizeNormal NewFacingDirection,
	const FVector_NetQuantizeNormal NewAimingDirection,
	const bool bNewUseWorldInput,
	const bool bNewUseExternalFacing,
	const bool bNewUseExternalAiming,
	const bool bNewUseControllerData)
{
	AnimationProperties.PlayerInputAcceleration = NewPlayerInput;
	AnimationProperties.WorldSpaceMovementInput = FVector(NewWorldInput).GetSafeNormal();
	AnimationProperties.FacingDirection = FVector(NewFacingDirection).GetSafeNormal();
	AnimationProperties.AimingDirection = FVector(NewAimingDirection).GetSafeNormal();
	AnimationProperties.bUseWorldSpaceMovementInput = bNewUseWorldInput;
	AnimationProperties.bUseExternalFacingDirection = bNewUseExternalFacing;
	AnimationProperties.bUseExternalAimingDirection = bNewUseExternalAiming;
	bUseControllerData = bNewUseControllerData;
	FillReplicatedState();
}
