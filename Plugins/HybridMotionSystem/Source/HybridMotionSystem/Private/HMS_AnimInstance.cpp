#include "HMS_AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/HMS_AnimationQueryLibrary.h"
#include "Chooser.h"

#include "Components/HMS_AnimationDataComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "Components/HMS_NavMoverComponent.h"
#include "Interface/HMS_CharacterAnimationInterface.h"
#include "MoverPoseSearchTrajectoryPredictor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/BlueprintSpringMathLibrary.h"
#include "AnimationWarpingLibrary.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendProfile.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstanceProxy.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "ChooserFunctionLibrary.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchSchema.h"
#if WITH_EDITOR
#include "Engine/World.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#endif
#include "Misc/MemStack.h"
#include "NativeGameplayTags.h"
#include "UObject/UObjectHash.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_Stance_Crouch, "SM.Stance.Crouch");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_State_LocomotionLoop, "SM.State.Locomotion Loop");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_State_LocomotionTransition, "SM.State.Locomotion Transition");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_State_IdleTransition, "SM.State.Idle Transition");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_State_IdleLoop, "SM.State.Idle Loop");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HMS_State_IdleBreak, "SM.State.Idle Break");

namespace
{
	// External weapon branches own independent Pose Search data. Walking only
	// the outer root silently misses their editor warm-up and legacy recovery.
	void GatherChooserSearchObjects(UChooserTable* Root, TArray<UObject*>& Objects)
	{
		TSet<UChooserTable*> Visited;
		TArray<UChooserTable*> Pending = {Root};
		while (!Pending.IsEmpty())
		{
			UChooserTable* Table = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Table) || Visited.Contains(Table)) { continue; }
			Visited.Add(Table);
			TArray<UObject*> Owned;
			GetObjectsWithOuter(Table, Owned, EGetObjectsFlags::IncludeNestedObjects);
			for (UObject* Object : Owned)
			{
				if (Object->IsA<UPoseSearchDatabase>()) { Objects.AddUnique(Object); }
				if (auto* Child = Cast<UChooserTable>(Object)) { Pending.Add(Child); }
			}
			const TArray<FInstancedStruct>* Results = &Table->CookedResults;
#if WITH_EDITORONLY_DATA
			if (!Table->IsCookedData()) { Results = &Table->ResultsStructs; }
#endif
			const auto AddReference = [&](const FInstancedStruct& Result)
			{
				if (const auto* External = Result.GetPtr<FEvaluateChooser>()) { Pending.Add(External->Chooser); }
				if (const auto* Nested = Result.GetPtr<FNestedChooser>()) { Pending.Add(Nested->Chooser); }
			};
			for (const FInstancedStruct& Result : *Results) { AddReference(Result); }
			AddReference(Table->FallbackResult);
		}
	}
}

UHMS_AnimInstance::UHMS_AnimInstance()
{
}

FPoseHistoryReference UHMS_AnimInstance::GetHMSPoseHistory() const
{
	if (const FAnimNode_PoseSearchHistoryCollector_Base* PoseHistoryNode =
		UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), this))
	{
		if (PoseHistoryNode->GetPoseHistoryPtr() != nullptr)
		{
			return PoseHistoryNode->GetPoseHistoryReference();
		}
	}
	return FPoseHistoryReference();
}

void UHMS_AnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	EvaluatedBlendStackState.Reset();
	bEvaluatedBlendStackLoopValid = false;
	EvaluatedBlendStackFrame = 0;

	CacheOwningPawn();
	CacheMoverComponent();
	CacheNavMoverComponent();
	CacheAnimationDataComponent();
	bComponentCacheInitialized = true;
	InitializeMoverPredictor();

#if WITH_EDITOR
	// Chooser-owned databases can defer indexing until their first query, which
	// runs on the animation worker thread. Queue them on the game thread before
	// locomotion starts so the first stop does not fall back to idle while its
	// index is built. ContinueRequest reuses existing/shared work without waiting.
	// Cooked builds already contain the search indexes.
	if (IsInGameThread() && IsValid(ChooserTable) && GetWorld() && GetWorld()->IsGameWorld())
	{
		TArray<UObject*> ChooserSubObjects;
		GatherChooserSearchObjects(ChooserTable, ChooserSubObjects);
		for (UObject* SubObject : ChooserSubObjects)
		{
			if (const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(SubObject))
			{
				UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
					Database, UE::PoseSearch::ERequestAsyncBuildFlag::ContinueRequest);
			}
		}
	}
#endif
}

void UHMS_AnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	FBlendStackAnimNodeReference Stack;
	bool bValid = false;
	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(GetStateMachineBlendStackNodeReference(), Stack, bValid);
	EvaluatedBlendStackState = StateMachineState;
	EvaluatedBlendStackFrame = GFrameCounter;
	bEvaluatedBlendStackLoopValid = bValid && !NoValidAnim && !bChooserRetryPending
		&& BlendStackInputs.bLoop && IsValid(BlendStackInputs.Anim)
		&& BlendStackInputs.Anim->GetSkeleton() && GetSkelMeshComponent()
		&& BlendStackInputs.Anim->GetSkeleton()->IsCompatibleMesh(GetSkelMeshComponent()->GetSkeletalMeshAsset())
		&& CompletionTrackedState == StateMachineState
		&& UBlendStackAnimNodeLibrary::GetCurrentAsset(Stack) == BlendStackInputs.Anim;
}

bool UHMS_AnimInstance::HasEvaluatedBlendStackState(FGameplayTag ExpectedState, uint64 NotBeforeFrame) const
{
	return ExpectedState.IsValid() && bEvaluatedBlendStackLoopValid
		&& EvaluatedBlendStackFrame >= NotBeforeFrame
		&& EvaluatedBlendStackState.HasTagExact(ExpectedState);
}

void UHMS_AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Keep a state-local playback clock as a robust fallback for projects whose
	// Animation Blueprint has not supplied a valid Blend Stack node reference.
	// DeltaSeconds (instead of world time) correctly respects pause and animation ticking.
	if (CompletionTrackedAnimation.IsValid())
	{
		CompletionTrackedElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
	}

	if (!CachedOwnerActor.IsValid())
	{
		bComponentCacheInitialized = false;
		CacheOwningPawn();
	}

	if (!bComponentCacheInitialized)
	{
		CacheMoverComponent();
		CacheNavMoverComponent();
		CacheAnimationDataComponent();
		bComponentCacheInitialized = true;
	}
	else
	{
		if (CachedMoverComponent && !IsValid(CachedMoverComponent))
		{
			CacheMoverComponent();
		}
		if (CachedNavMoverComponent && !IsValid(CachedNavMoverComponent))
		{
			CacheNavMoverComponent();
		}
		if (CachedAnimationDataComponent && !IsValid(CachedAnimationDataComponent))
		{
			CacheAnimationDataComponent();
		}
	}

	if (!IsValid(Predictor) && IsValid(CachedMoverComponent))
	{
		InitializeMoverPredictor();
	}

	const auto* HMSNav = Cast<UHMS_NavMoverComponent>(CachedNavMoverComponent);
	bNavigationBraking = HMSNav && HMSNav->IsNavigationBraking();
	Update_PropertiesFromCharacter();
	Update_Logic();

	// Loops and cosmetic idle breaks must respond to semantic changes without
	// waiting for state re-entry. Starts/stops retain their footfall and pick up
	// the new family at hand-off; idle fidgets can safely blend immediately.
	// A failed query can retain the last asset while clearing its looping flag.
	// The cache-valid bit is only an optimization, not authority over semantic
	// changes. Recognize loop states too so a later valid family can recover.
	const bool bCanRefreshAnimationFamily = BlendStackInputs.bLoop
		|| StateMachineState.HasTagExact(TAG_HMS_State_IdleLoop)
		|| StateMachineState.HasTagExact(TAG_HMS_State_LocomotionLoop)
		|| StateMachineState.HasTagExact(TAG_HMS_State_IdleBreak)
		|| StateMachineState.HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("SM.State.Cover"), false));
	if (IsValid(BlendStackInputs.Anim) && !StateMachineState.IsEmpty() && bCanRefreshAnimationFamily
		&& (CachedChooserWeaponType != UHMS_AnimationQueryLibrary::GetEquippedWeaponType(this)
			|| CachedChooserBehaviorState != UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(this)))
	{
		SetBlendStackAnimFromChooser(StateMachineState, true);
	}

	if (bTurnInPlaceReselectRequested)
	{
		const AActor* Owner = CachedOwnerActor.Get();
		const float ExplicitTargetDelta = Owner && !TurnInPlaceReselectFacingDirection.IsNearlyZero()
			? FMath::FindDeltaAngleDegrees(
				Owner->GetActorRotation().Yaw,
				TurnInPlaceReselectFacingDirection.ToOrientationRotator().Yaw)
			: FutureFacingDelta;
		// On the press frame Mover's predictor still contains the previous target.
		// Publish the explicit mouse target delta so the transition graph does not
		// eject TurnInPlace to Idle before the regenerated trajectory arrives.
		FutureFacingDelta_LastFrame = FutureFacingDelta;
		FutureFacingDelta = ExplicitTargetDelta;
		if (TurnInPlaceReselectDelayFrames > 0)
		{
			--TurnInPlaceReselectDelayFrames;
		}
		else
		{
			bTurnInPlaceReselectRequested = false;
		}
	}

	// A UE5.8 Pose Search database can legitimately return no result while its
	// editor-only async index build is still in progress.  Retry the exact state
	// entry request without letting NoValidAnim eject the state after one frame.
	if (bChooserRetryPending)
	{
		ChooserRetryElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
		const FGameplayTagContainer RetryState = ChooserRetryState;
		const bool bRetryForceBlend = bChooserRetryForceBlend;
		bChooserRetryPending = false;
		bProcessingChooserRetry = true;
		SetBlendStackAnimFromChooser(RetryState, bRetryForceBlend);
		bProcessingChooserRetry = false;
	}

}

void UHMS_AnimInstance::RequestTurnInPlaceReselect(
	const FVector& RequestedWorldFacingDirection,
	const bool bForceReselect)
{
	const FVector RequestedDirection = RequestedWorldFacingDirection.GetSafeNormal2D();
	if (RequestedDirection.IsNearlyZero())
	{
		return;
	}

	LastTurnInPlaceRequestFrame = GFrameCounter;

	const bool bCurrentAnimationIsTurn = IsValid(BlendStackInputs.Anim)
		&& BlendStackInputs.Anim->GetName().Contains(TEXT("Turn"), ESearchCase::IgnoreCase);
	const bool bCurrentTagsContainTurn = BlendStackInputs.Tags.ContainsByPredicate(
		[](const FName& Tag)
		{
			return Tag.ToString().Contains(TEXT("Turn"), ESearchCase::IgnoreCase)
				|| Tag.ToString().Contains(TEXT("Spin"), ESearchCase::IgnoreCase);
		});
	// A first click from Idle is handled by the normal Idle->Turn transition.
	// Publish its explicit target for one animation frame; Mover may otherwise
	// consume enough of a small turn before the transition graph evaluates it.
	if (!bCurrentAnimationIsTurn && !bCurrentTagsContainTurn)
	{
		LastTurnInPlaceAnimationFacingDirection = RequestedDirection;
		bTurnInPlaceReselectRequested = true;
		TurnInPlaceReselectDelayFrames = 0;
		TurnInPlaceReselectFacingDirection = RequestedDirection;
		return;
	}
	if (bTurnInPlaceReselectRequested)
	{
		// Held-mouse updates keep the queued request pointed at the newest cursor
		// target without postponing the already scheduled query.
		TurnInPlaceReselectFacingDirection = RequestedDirection;
		return;
	}

	constexpr float HeldTargetReselectThresholdDegrees = 20.0f;
	const float TargetChangeDegrees = LastTurnInPlaceAnimationFacingDirection.IsNearlyZero()
		? 180.0f
		: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(LastTurnInPlaceAnimationFacingDirection, RequestedDirection),
			-1.0f,
			1.0f)));
	if (!bForceReselect && TargetChangeDegrees < HeldTargetReselectThresholdDegrees)
	{
		return;
	}

	bTurnInPlaceReselectRequested = true;
	bRestartTurnInPlaceFromBeginningOnNextSelection = true;
	TurnInPlaceReselectDelayFrames = 0;
	TurnInPlaceReselectFacingDirection = RequestedDirection;
	LastTurnInPlaceAnimationFacingDirection = RequestedDirection;
	bHasCachedChooserContext = false;
}

void UHMS_AnimInstance::CacheOwningPawn()
{
	CachedOwnerActor = GetOwningActor();
	CachedPawn = TryGetPawnOwner();
}

void UHMS_AnimInstance::CacheMoverComponent()
{
	CachedMoverComponent = nullptr;

	if (const AActor* Owner = CachedOwnerActor.Get())
	{
		CachedMoverComponent = Owner->FindComponentByClass<UMoverComponent>();
	}
}

void UHMS_AnimInstance::CacheNavMoverComponent()
{
	CachedNavMoverComponent = nullptr;

	if (const AActor* Owner = CachedOwnerActor.Get())
	{
		CachedNavMoverComponent = Owner->FindComponentByClass<UNavMoverComponent>();
	}
}

void UHMS_AnimInstance::CacheAnimationDataComponent()
{
	CachedAnimationDataComponent = nullptr;
	if (const AActor* Owner = CachedOwnerActor.Get())
	{
		CachedAnimationDataComponent = Owner->FindComponentByClass<UHMS_AnimationDataComponent>();
	}
}

void UHMS_AnimInstance::InitializeMoverPredictor()
{
	Predictor = nullptr;

	if (!IsValid(CachedMoverComponent))
	{
		return;
	}

	Predictor = NewObject<UMoverTrajectoryPredictor>(this);
	if (!IsValid(Predictor))
	{
		return;
	}

	Predictor->Setup(CachedMoverComponent);
}

APawn* UHMS_AnimInstance::GetCachedPawn() const
{
	return CachedPawn.Get();
}

UMoverComponent* UHMS_AnimInstance::GetCachedMoverComponent() const
{
	return CachedMoverComponent;
}

UNavMoverComponent* UHMS_AnimInstance::GetCachedNavMoverComponent() const
{
	return CachedNavMoverComponent;
}

UMoverTrajectoryPredictor* UHMS_AnimInstance::GetMoverTrajectoryPredictor() const
{
	return Predictor;
}

void UHMS_AnimInstance::Update_PropertiesFromCharacter()
{
	AActor* Owner = CachedOwnerActor.Get();
	if (!IsValid(Owner))
	{
		return;
	}

	FHMS_AnimationProperties NewProperties;
	NewProperties.MovementMode = MovementMode;
	NewProperties.Stance = Stance;
	NewProperties.RotationMode = RotationMode;
	NewProperties.Gait = Gait;
	NewProperties.GroundNormal = GroundNormal;
	NewProperties.GroundLocation = GroundLocation;
	NewProperties.AccelerationSource = AccelerationSource;
	NewProperties.PlayerInputAcceleration.X = PlayerInputAcceleration.X;
	NewProperties.PlayerInputAcceleration.Y = PlayerInputAcceleration.Y;

	bool bPropertiesProvided = false;
	if (IsValid(CachedAnimationDataComponent))
	{
		bPropertiesProvided = CachedAnimationDataComponent->GetAnimationProperties(NewProperties);
	}

	if (!bPropertiesProvided && Owner->GetClass()->ImplementsInterface(UHMS_CharacterAnimationInterface::StaticClass()))
	{
		FHMS_AnimationProperties InterfaceProperties;
		const bool bInterfaceSuccess =
			IHMS_CharacterAnimationInterface::Execute_GetAnimationProperties(Owner, InterfaceProperties);

		if (bInterfaceSuccess)
		{
			NewProperties = InterfaceProperties;
			bPropertiesProvided = true;
		}
	}
	if (!bPropertiesProvided && IsValid(CachedMoverComponent))
	{
		// A plain Mover pawn does not need to implement the HMS interface just to
		// expose Walking/Falling/Flying to the animation state machine.
		NewProperties.MovementMode = UHMS_AnimationDataComponent::ResolveMoverMovementMode(
			CachedMoverComponent->GetMovementModeName());
	}

	if (NewProperties.Stance.IsEmpty())
	{
		NewProperties.Stance = Stance;
	}

	CharacterProperties = NewProperties;
	AimingRotation = CharacterProperties.bUseExternalAimingDirection
		&& !CharacterProperties.AimingDirection.IsNearlyZero()
		? CharacterProperties.AimingDirection.ToOrientationRotator()
		: GetControllerAimingRotation();
}

void UHMS_AnimInstance::Update_Logic()
{
	Update_Trajectory();
	Update_EssentialValues();
	//Update_MovementDirectionData();
	Update_States();
	Update_AimOffset();
	Update_AdditiveLean();
}

void UHMS_AnimInstance::Update_Trajectory()
{
	bUseTransientMovementDirectionOverride = false;
	const float FrameDeltaSeconds = FMath::Max(GetDeltaSeconds(), 0.0f);

	if (ShouldUseLightweightTrajectory() || !IsValid(Predictor))
	{
		bUsingLightweightTrajectory = true;
		UpdateLightweightTrajectory(FrameDeltaSeconds);
		return;
	}
	bUsingLightweightTrajectory = false;
	TrajectoryUpdateAccumulator += FrameDeltaSeconds;
	if (TrajectoryUpdateInterval > 0.0f && TrajectoryUpdateAccumulator < TrajectoryUpdateInterval)
	{
		return;
	}

	const float PredictionDeltaSeconds = FMath::Max(TrajectoryUpdateAccumulator, FrameDeltaSeconds);
	TrajectoryUpdateAccumulator = 0.0f;

	TScriptInterface<IPoseSearchTrajectoryPredictorInterface> PredictorInterface;
	PredictorInterface.SetObject(Predictor);
	PredictorInterface.SetInterface(Cast<IPoseSearchTrajectoryPredictorInterface>(Predictor));

	if (!PredictorInterface.GetInterface())
	{
		return;
	}

	FTransformTrajectory GeneratedTrajectory;
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectoryWithPredictor(
		PredictorInterface,
		PredictionDeltaSeconds,
		Trajectory,
		PreviousDesiredControllerYaw,
		GeneratedTrajectory,
		0.033f,
		15,
		0.10f,
		15
	);

	if (TrajectoryQuality == EHMS_TrajectoryQuality::Full)
	{
		FPoseSearchTrajectory_WorldCollisionResults CollisionResult;
		FTransformTrajectory CollisionAdjustedTrajectory;
		static const TArray<AActor*> ActorsToIgnore;

		// Kept only for the explicit Full compatibility mode; UE 5.8 recommends no collision pass for Mover.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UPoseSearchTrajectoryLibrary::HandleTransformTrajectoryWorldCollisions(
			this,
			this,
			GeneratedTrajectory,
			true,
			0.01f,
			CollisionAdjustedTrajectory,
			CollisionResult,
			ETraceTypeQuery::TraceTypeQuery1,
			false,
			ActorsToIgnore,
			EDrawDebugTrace::None,
			true,
			150.0f
		);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		TrajectoryCollision = CollisionResult;
		Trajectory = MoveTemp(CollisionAdjustedTrajectory);
	}
	else
	{
		Trajectory = MoveTemp(GeneratedTrajectory);
	}

	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
		Trajectory,
		-0.3f,
		-0.2f,
		Trj_PastVelocity,
		false
	);

	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
		Trajectory,
		0.1f,
		0.2f,
		Trj_NearFutureVelocity,
		false
	);

	Trj_PreviousFutureVelocity = Trj_FutureVelocity;

	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
		Trajectory,
		0.4f,
		0.5f,
		Trj_FutureVelocity,
		false
	);

	Trj_TurnAngle = Get_TrajectoryTurnAngle();

	{
		FTransformTrajectorySample FutureSample;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(
			Trajectory,
			1.5f,
			FutureSample,
			false
		);

		Trj_FutureFacing = UKismetMathLibrary::Quat_Rotator(FutureSample.Facing);
	}

	FutureFacingDelta_LastFrame = FutureFacingDelta;
	static const TArray<float> FacingSampleTimes = { 0.0f, 0.25f, 0.75f, 1.5f };
	FutureFacingDelta = Get_TotalFacingDelta(FacingSampleTimes);

	UpdateTransientMovementDirectionOverride();

	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(
		Trajectory,
		-0.4f,
		-0.3f,
		Trj_PastAngularVelocity,
		false
	);

	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(
		Trajectory,
		0.0f,
		0.1f,
		Trj_CurrentAngularVelocity,
		false
	);

	const bool bBothTurningLeft =
		(Trj_PastAngularVelocity.Z < -200.0f) &&
		(Trj_CurrentAngularVelocity.Z < -200.0f);

	const bool bBothTurningRight =
		(Trj_PastAngularVelocity.Z > 200.0f) &&
		(Trj_CurrentAngularVelocity.Z > 200.0f);

	Trj_IsCircling = bBothTurningLeft || bBothTurningRight;

	if (Trj_IsCircling)
	{
		Trj_CirclingTime += PredictionDeltaSeconds;
	}
	else
	{
		Trj_CirclingTime = 0.0f;
	}
}

bool UHMS_AnimInstance::ShouldUseLightweightTrajectory() const
{
    // Display previews keep their AnimInstance alive after detaching the Mover simulation.
    // Predicting against that backend produces invalid samples and a warning every frame.
    if (!IsValid(CachedMoverComponent) || !CachedMoverComponent->IsRegistered()
        || !CachedMoverComponent->HasBeenInitialized())
    {
        return true;
    }
	if (TrajectoryQuality == EHMS_TrajectoryQuality::Lightweight)
	{
		return true;
	}

	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	return Mesh && LightweightTrajectoryLODThreshold >= 0
		&& Mesh->GetPredictedLODLevel() >= LightweightTrajectoryLODThreshold;
}

void UHMS_AnimInstance::UpdateLightweightTrajectory(const float DeltaSeconds)
{
	const AActor* Owner = CachedOwnerActor.Get();
	const FVector CurrentVelocity = IsValid(CachedMoverComponent)
		? CachedMoverComponent->GetVelocity()
		: (Owner ? Owner->GetVelocity() : FVector::ZeroVector);

	FVector DesiredDirection = CharacterProperties.bUseWorldSpaceMovementInput
		? CharacterProperties.WorldSpaceMovementInput.GetSafeNormal2D()
		: CurrentVelocity.GetSafeNormal2D();
	if (DesiredDirection.IsNearlyZero() && !CharacterProperties.bUseWorldSpaceMovementInput)
	{
		DesiredDirection = CurrentVelocity.GetSafeNormal2D();
	}

	const float CurrentSpeed = CurrentVelocity.Size2D();
	Trj_PastVelocity = Trj_NearFutureVelocity;
	Trj_PreviousFutureVelocity = Trj_FutureVelocity;
	Trj_NearFutureVelocity = CurrentVelocity;
	Trj_FutureVelocity = DesiredDirection.IsNearlyZero()
		? FVector::ZeroVector
		: DesiredDirection * FMath::Max(CurrentSpeed, 100.0f);

	const FVector CurrentDirection = CurrentVelocity.GetSafeNormal2D();
	Trj_TurnAngle = (!CurrentDirection.IsNearlyZero() && !DesiredDirection.IsNearlyZero())
		? FMath::FindDeltaAngleDegrees(CurrentDirection.ToOrientationRotator().Yaw, DesiredDirection.ToOrientationRotator().Yaw)
		: 0.0f;

	// Body facing and aiming are independent. AimingDirection drives AimOffset,
	// never the trajectory used by TurnInPlace. This must match the full Mover
	// predictor path, especially when a sub-threshold mouse aim is requested.
	FVector FacingDirection = CharacterProperties.bUseExternalFacingDirection
		? CharacterProperties.FacingDirection.GetSafeNormal2D()
		: DesiredDirection;
	if (FacingDirection.IsNearlyZero())
	{
		FacingDirection = Owner ? Owner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}

	const float PreviousFutureYaw = Trj_FutureFacing.Yaw;
	Trj_FutureFacing = FacingDirection.ToOrientationRotator();
	FutureFacingDelta_LastFrame = FutureFacingDelta;
	const float ReferenceYaw = Owner
		? Owner->GetActorRotation().Yaw
		: RootTransform.Rotator().Yaw;
	FutureFacingDelta = FMath::FindDeltaAngleDegrees(ReferenceYaw, Trj_FutureFacing.Yaw);

	Trj_PastAngularVelocity = Trj_CurrentAngularVelocity;
	Trj_CurrentAngularVelocity = FVector(0.0f, 0.0f,
		FMath::FindDeltaAngleDegrees(PreviousFutureYaw, Trj_FutureFacing.Yaw)
		/ FMath::Max(DeltaSeconds, 0.001f));
	Trj_IsCircling = false;
	Trj_CirclingTime = 0.0f;
	UpdateTransientMovementDirectionOverride();
}

void UHMS_AnimInstance::Update_EssentialValues()
{
	CharacterTransform_LastFrame = CharacterTransform;

	if (const AActor* Owner = CachedOwnerActor.Get())
	{
		CharacterTransform = Owner->GetActorTransform();
	}
	else
	{
		CharacterTransform = FTransform::Identity;
	}

	const FAnimNodeReference OffsetRootNode = GetOffsetRootNodeReference();
	if (OffsetRootBoneEnabled && OffsetRootNode.GetAnimNodePtr<FAnimNode_OffsetRootBone>())
	{
		const FTransform OffsetRootTransform = UAnimationWarpingLibrary::GetOffsetRootTransform(OffsetRootNode);

		const FVector RootLocation = OffsetRootTransform.GetLocation();
		const FRotator RootRotation = OffsetRootTransform.Rotator();

		RootTransform = FTransform(
			FRotator(RootRotation.Pitch, RootRotation.Yaw + 90.0f, RootRotation.Roll),
			RootLocation,
			FVector::OneVector
		);
	}
	else
	{
		RootTransform = CharacterTransform;
	}

	AccelerationSource = CharacterProperties.AccelerationSource;

	// 玩家输入仍然允许由接口提供
	PlayerInputAcceleration.X = CharacterProperties.PlayerInputAcceleration.X;
	PlayerInputAcceleration.Y = CharacterProperties.PlayerInputAcceleration.Y;
	PlayerInputAcceleration.Z = 0.0f;
	PlayerInputAcceleration = PlayerInputAcceleration.GetSafeNormal2D();

	// Nav 输入不再依赖接口，直接从 NavMover 读取
	if (AccelerationSource == EHMS_AccelerationSource::NavMover)
	{
		NavInputAcceleration = ResolveNavMoverInputAcceleration();
	}
	else
	{
		NavInputAcceleration = FVector::ZeroVector;
	}

	SelectedInputAcceleration = GetSelectedInputAcceleration();

	Acceleration_LastFrame = Acceleration;
	Acceleration = SelectedInputAcceleration;
	AccelerationAmount = Acceleration.Size2D();
	// Input is normalized above; a tolerance of 1 classified every direction as zero.
	HasAcceleration = !Acceleration.IsNearlyZero(0.001f);

	Velocity_LastFrame = Velocity;
	Velocity = IsValid(CachedMoverComponent)
		? CachedMoverComponent->GetVelocity()
		: (CachedOwnerActor.IsValid() ? CachedOwnerActor->GetVelocity() : FVector::ZeroVector);
	Speed2D = Velocity.Size2D();
	HasVelocity = Speed2D > 5.0f;

	const double SafeDeltaSeconds = FMath::Max(static_cast<double>(GetDeltaSeconds()), 0.001);
	VelocityAcceleration = (Velocity - Velocity_LastFrame) / SafeDeltaSeconds;
	RelativeAcceleration = UKismetMathLibrary::LessLess_VectorRotator(
		VelocityAcceleration,
		RootTransform.Rotator()
	);

	if (HasVelocity)
	{
		LastNonZeroVelocity = Velocity;
	}

	GroundNormal_LastFrame = GroundNormal;
	GroundLocation = CharacterProperties.GroundLocation;
	BasedMovementDelta = CharacterProperties.BasedMovementDelta;

	if (!CharacterProperties.GroundNormal.IsNearlyZero(0.001f))
	{
		GroundNormal = CharacterProperties.GroundNormal.GetSafeNormal();
	}
	else
	{
		GroundNormal = FVector::UpVector;
	}

	SmoothedGroundNormal = FMath::VInterpTo(
		SmoothedGroundNormal,
		GroundNormal,
		GetDeltaSeconds(),
		GroundNormalInterpSpeed
	).GetSafeNormal();

	const FVector LocalGroundNormal = RootTransform.InverseTransformVectorNoScale(SmoothedGroundNormal);
	SlopeAngle.X = FMath::RadiansToDegrees(FMath::Atan2(-LocalGroundNormal.X, LocalGroundNormal.Z));
	SlopeAngle.Y = FMath::RadiansToDegrees(FMath::Atan2(LocalGroundNormal.Y, LocalGroundNormal.Z));

	bSharpDirectionReversal_LastFrame = bSharpDirectionReversal;
	bSharpDirectionReversal = false;
	bImmediateDirectionChangePivot_LastFrame = bImmediateDirectionChangePivot;
	bImmediateDirectionChangePivot = false;
	const FVector InputDirection = GetSelectedWorldInputDirection();
	const FVector VelocityDirection = Velocity.GetSafeNormal2D();
	float InputVelocityAngle = 0.0f;
	if (!InputDirection.IsNearlyZero() && !VelocityDirection.IsNearlyZero() && Speed2D > 50.0f)
	{
		const float DirectionDot = FMath::Clamp(FVector::DotProduct(InputDirection, VelocityDirection), -1.0f, 1.0f);
		InputVelocityAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
		bSharpDirectionReversal = InputVelocityAngle >= SharpReversalAngleThreshold;
		bImmediateDirectionChangePivot = InputVelocityAngle >= GetPivotTurnAngleThreshold();
	}

	if (bImmediateDirectionChangePivot)
	{
		// Keep Mover's braking arc and visual-component facing for Pose Search.
		// Input reversal is a transition signal, not a replacement prediction.
		if (!bImmediateDirectionChangePivot_LastFrame)
		{
			// 同一个 Locomotion Transition 状态可能已经缓存过起步/循环查询。
			// 大角度转向上升沿必须重新执行 Chooser/Pose Match。
			bHasCachedChooserContext = false;
		}
	}

}

template<typename TState>
bool UHMS_AnimInstance::AreStatesEqual(const TState& A, const TState& B)
{
	return A == B;
}

template<typename TState>
void UHMS_AnimInstance::UpdateStateValues(
	const TState& NewState,
	TState& CurrentState,
	TState& LastFrameState,
	TState& RecentState,
	float& TimeInState,
	float& LastStateTime,
	float RecentTimeLimit
)
{
	LastFrameState = CurrentState;
	CurrentState = NewState;

	if (!AreStatesEqual(CurrentState, LastFrameState))
	{
		LastStateTime = TimeInState;
		TimeInState = 0.0f;
	}
	else
	{
		TimeInState += GetDeltaSeconds();
	}

	if (TimeInState < RecentTimeLimit)
	{
		RecentState = CurrentState;
	}
}

void UHMS_AnimInstance::Update_States()
{
	UpdateStateValues<EHMS_MovementMode>(
		CharacterProperties.MovementMode,
		MovementMode,
		MovementMode_LastFrame,
		MovementMode_Recent,
		MovementMode_Time,
		MovementMode_LastStateTime,
		0.2f
	);

	JustLanded_Heavy = MovementMode == EHMS_MovementMode::OnGround
		&& MovementMode_LastFrame == EHMS_MovementMode::InAir
		&& Velocity_LastFrame.Z <= -HeavyLandingVerticalSpeedThreshold;
	JustTraversed = MovementMode != EHMS_MovementMode::Traversing
		&& MovementMode_LastFrame == EHMS_MovementMode::Traversing;

	bJustBecameAirborne = MovementMode == EHMS_MovementMode::InAir
		&& MovementMode_LastFrame != EHMS_MovementMode::InAir;
	bJustLanded = MovementMode == EHMS_MovementMode::OnGround
		&& MovementMode_LastFrame == EHMS_MovementMode::InAir;
	bIsFalling = MovementMode == EHMS_MovementMode::InAir && Velocity.Z < 0.0f;
	VerticalVelocity = Velocity.Z;
	RagdollProperties = CharacterProperties.RagdollProperties;
	RetargetCorrectionAlpha = RagdollProperties.State == EHMS_RagdollState::Animated
		? FMath::FInterpConstantTo(RetargetCorrectionAlpha, 1.f, GetDeltaSeconds(), 4.f) : 0.f;

	if (bJustBecameAirborne)
	{
		AirTime = 0.0f;
		GroundDistance = AirGroundTraceDistance;
	}
	else if (MovementMode == EHMS_MovementMode::InAir)
	{
		AirTime += FMath::Max(GetDeltaSeconds(), 0.0f);
	}
	if (bJustLanded)
	{
		LandingVelocity = Velocity_LastFrame;
		AirTime = 0.0f;
		GroundDistance = 0.0f;
		PredictedTimeToLand = 0.0f;
	}

	const float GravityZ = IsValid(CachedMoverComponent)
		? CachedMoverComponent->GetGravityAcceleration().Z
		: -980.0f;
	const float GravityMagnitude = FMath::Max(FMath::Abs(GravityZ), 1.0f);
	TimeToApex = MovementMode == EHMS_MovementMode::InAir && VerticalVelocity > 0.0f
		? VerticalVelocity / GravityMagnitude
		: 0.0f;

	if (MovementMode == EHMS_MovementMode::InAir && CachedOwnerActor.IsValid() && GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HMSAirGround), false, CachedOwnerActor.Get());
		const FVector Start = CachedOwnerActor->GetActorLocation();
		const FVector End = Start - FVector(0.0f, 0.0f, AirGroundTraceDistance);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			GroundDistance = FMath::Max(Start.Z - Hit.ImpactPoint.Z, 0.0f);
			const float Discriminant = VerticalVelocity * VerticalVelocity
				+ 2.0f * GravityMagnitude * GroundDistance;
			PredictedTimeToLand = (VerticalVelocity + FMath::Sqrt(FMath::Max(Discriminant, 0.0f)))
				/ GravityMagnitude;
		}
		else
		{
			GroundDistance = AirGroundTraceDistance;
			PredictedTimeToLand = 0.0f;
		}
	}

	UpdateStateValues<EHMS_RotationMode>(
		CharacterProperties.RotationMode,
		RotationMode,
		RotationMode_LastFrame,
		RotationMode_Recent,
		RotationMode_Time,
		RotationMode_LastStateTime,
		0.1f
	);

	const EHMS_MovementState NewMovementState =
		IsMoving() ? EHMS_MovementState::Moving : EHMS_MovementState::Idle;

	UpdateStateValues<EHMS_MovementState>(
		NewMovementState,
		MovementState,
		MovementState_LastFrame,
		MovementState_Recent,
		MovementState_Time,
		MovementState_LastStateTime,
		0.1f
	);

	EHMS_Gait NewGait = CharacterProperties.Gait;
	// A turning pivot can satisfy the circling detector while it is still
	// finishing the previous turn. Do not synthesize a Walk request in the
	// middle of that Run pivot. Explicit Walk/Sprint input still passes through.
	if (NewGait == EHMS_Gait::Run && Trj_IsCircling
		&& !IsPivotSelectionActive() && !IsStartSelectionActive())
	{
		NewGait = EHMS_Gait::Walk;
	}
	// Pivot 会主动消耗速度，速度阈值驱动的外部 Gait 很容易在动作中途
	// Run -> Walk -> Run。保持进入 Pivot 时的步态，避免 Chooser 连续用
	// Walk/Run Start 覆盖正在播放的折返动画。
	if (IsPivotSelectionLocked())
	{
		NewGait = Gait;
	}

	UpdateStateValues<EHMS_Gait>(
		NewGait,
		Gait,
		Gait_LastFrame,
		Gait_Recent,
		Gait_Time,
		Gait_LastStateTime,
		0.1f
	);

	const FGameplayTagContainer NewStance = CharacterProperties.Stance.IsEmpty() ? Stance : CharacterProperties.Stance;
	UpdateStateValues<FGameplayTagContainer>(
		NewStance,
		Stance,
		Stance_LastFrame,
		Stance_Recent,
		Stance_Time,
		Stance_LastStateTime,
		0.1f
	);

	const EHMS_MovementDirection EffectiveMovementDirection = GetEffectiveMovementDirection();
	UpdateStateValues<EHMS_MovementDirection>(
		EffectiveMovementDirection,
		MovementDirection,
		MovementDirection_LastFrame,
		MovementDirection_Recent,
		MovementDirection_Time,
		MovementDirection_LastStateTime,
		0.1f
	);
}

void UHMS_AnimInstance::Update_AimOffset()
{
	const FRotator AimTarget = AimingRotation;

	UBlueprintSpringMathLibrary::CriticalSpringDampRotator(
		SmoothedAimTarget,
		InOutAngularVelocity,
		AimTarget,
		GetDeltaSeconds(),
		AimOffsetSmoothingTime
	);

	Previous_AO = AO;

	const FRotator DeltaAimRot = UKismetMathLibrary::NormalizedDeltaRotator(
		SmoothedAimTarget,
		RootTransform.Rotator()
	);

	AO = FVector2D(DeltaAimRot.Yaw, DeltaAimRot.Pitch);

	const double MaxAllowedYaw =
		(MovementState == EHMS_MovementState::Idle) ? 110.0 : 180.0;

	const bool bYawWithinLimit = FMath::Abs(DeltaAimRot.Yaw) <= MaxAllowedYaw;

	const bool bRotationModeAllowsAO =
		(RotationMode == EHMS_RotationMode::Strafe) ||
		(RotationMode == EHMS_RotationMode::Aim);

	const bool bSlotWeightAllowsAO =
		Blueprint_GetSlotMontageLocalWeight(FName(TEXT("DefaultSlot"))) < 0.5f;

	const bool bAODeltaIsStable =
		FMath::Abs(AO.X - Previous_AO.X) < 135.0f;

	EnableAO =
		bYawWithinLimit &&
		bRotationModeAllowsAO &&
		bSlotWeightAllowsAO &&
		bAODeltaIsStable;

	if (!EnableAO)
	{
		SmoothedAimTarget = AimTarget;
	}
}

bool UHMS_AnimInstance::IsMoving() const
{
	if (bNavigationBraking) { return false; }
	// A fresh movement request cancels Stop without waiting for its remaining
	// playback or a throttled trajectory prediction to reach moving speed.
	if ((StateMachineState.HasTagExact(TAG_HMS_State_IdleTransition)
		|| BlendStackInputs.Tags.Contains(FName(TEXT("Interaction"))))
		&& CharacterProperties.bUseWorldSpaceMovementInput
		&& !CharacterProperties.WorldSpaceMovementInput.IsNearlyZero(0.01f))
	{
		return true;
	}
	// GASP uses predicted movement, so Stop can play during physical deceleration.
	if (!ShouldUseLightweightTrajectory())
	{
		FVector FutureVelocity;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
			Trajectory, 0.9f, 1.0f, FutureVelocity, true);
		return !FutureVelocity.IsNearlyZero(10.f);
	}
	return !Trj_FutureVelocity.IsNearlyZero(10.f);
}

FRotator UHMS_AnimInstance::GetControllerAimingRotation() const
{
	if (CharacterProperties.bUseExternalAimingDirection
		&& !CharacterProperties.AimingDirection.IsNearlyZero())
	{
		return CharacterProperties.AimingDirection.ToOrientationRotator();
	}

	const APawn* OwningPawn = CachedPawn.Get();
	if (!IsValid(OwningPawn))
	{
		return FRotator::ZeroRotator;
	}

	if (const AController* Controller = OwningPawn->GetController())
	{
		return Controller->GetControlRotation();
	}

	return OwningPawn->GetBaseAimRotation();
}

FVector UHMS_AnimInstance::GetSelectedInputAcceleration() const
{
	if (CharacterProperties.bUseWorldSpaceMovementInput)
	{
		return CharacterProperties.WorldSpaceMovementInput.GetSafeNormal2D();
	}

	if (AccelerationSource == EHMS_AccelerationSource::NavMover)
	{
		return NavInputAcceleration;
	}

	return PlayerInputAcceleration;
}

FVector UHMS_AnimInstance::GetSelectedWorldInputDirection() const
{
	if (CharacterProperties.bUseWorldSpaceMovementInput
		|| AccelerationSource == EHMS_AccelerationSource::NavMover)
	{
		return SelectedInputAcceleration.GetSafeNormal2D();
	}

	FVector LocalInput = SelectedInputAcceleration;
	LocalInput.Z = 0.0f;
	if (LocalInput.IsNearlyZero(0.001f))
	{
		return FVector::ZeroVector;
	}

	LocalInput.Normalize();
	const FRotator InputReferenceRotation = GetControllerAimingRotation();
	const FVector WorldInput = FRotationMatrix(InputReferenceRotation).TransformVector(
		FVector(LocalInput.Y, LocalInput.X, 0.0f));
	return WorldInput.GetSafeNormal2D();
}

float UHMS_AnimInstance::GetPivotTurnAngleThreshold() const
{
	if (Gait == EHMS_Gait::Sprint)
	{
		return 60.0f;
	}

	switch (MovementMode_Recent)
	{
	case EHMS_MovementMode::InAir:
	case EHMS_MovementMode::Sliding:
		return 100.0f;
	case EHMS_MovementMode::OnGround:
	case EHMS_MovementMode::Traversing:
	default:
		return 75.0f;
	}
}

EHMS_MovementDirection UHMS_AnimInstance::CalculateMovementDirectionFromVectors(
	const FVector& InInputAcceleration
) const
{
	if (CharacterProperties.RotationMode == EHMS_RotationMode::OrientToMovement)
	{
		return EHMS_MovementDirection::F;
	}

	if (CharacterProperties.Gait == EHMS_Gait::Sprint)
	{
		return EHMS_MovementDirection::F;
	}

	FVector WorldAccelerationDirection;
	if (CharacterProperties.bUseWorldSpaceMovementInput)
	{
		WorldAccelerationDirection = InInputAcceleration.GetSafeNormal2D();
	}
	else
	{
		FVector LocalInput = InInputAcceleration;
		LocalInput.Z = 0.0f;
		if (!LocalInput.IsNearlyZero(0.001f))
		{
			LocalInput.Normalize();
			const FRotator InputReferenceRotation = GetControllerAimingRotation();
			const FVector TransformedInput = FRotationMatrix(InputReferenceRotation).TransformVector(
				FVector(LocalInput.Y, LocalInput.X, 0.0f));
			WorldAccelerationDirection = TransformedInput.GetSafeNormal2D();
		}
	}

	if (WorldAccelerationDirection.IsNearlyZero(0.001f))
	{
		return EHMS_MovementDirection::F;
	}

	const FRotator FacingRotation = CharacterProperties.bUseExternalFacingDirection
		&& !CharacterProperties.FacingDirection.IsNearlyZero()
		? CharacterProperties.FacingDirection.ToOrientationRotator()
		: RootTransform.Rotator();
	const FVector Forward = FRotationMatrix(FacingRotation).GetUnitAxis(EAxis::X).GetSafeNormal2D();
	const FVector Right = FRotationMatrix(FacingRotation).GetUnitAxis(EAxis::Y).GetSafeNormal2D();
	const float SignedAngle = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::DotProduct(WorldAccelerationDirection, Right),
		FVector::DotProduct(WorldAccelerationDirection, Forward)));
	const float AbsoluteAngle = FMath::Abs(SignedAngle);

	EHMS_MovementDirection Result = EHMS_MovementDirection::F;

	// Keep the previous sector slightly longer to avoid repeated Chooser reselection near boundaries.
	if (MovementDirection == EHMS_MovementDirection::F
		&& AbsoluteAngle <= MovementDirectionForwardHalfAngle + MovementDirectionHysteresisDegrees)
	{
		Result = EHMS_MovementDirection::F;
	}
	else if (MovementDirection == EHMS_MovementDirection::B
		&& AbsoluteAngle >= MovementDirectionBackwardHalfAngle - MovementDirectionHysteresisDegrees)
	{
		Result = EHMS_MovementDirection::B;
	}
	else
	{
		const bool bWasLeft = MovementDirection == EHMS_MovementDirection::LL || MovementDirection == EHMS_MovementDirection::LR;
		const bool bWasRight = MovementDirection == EHMS_MovementDirection::RL || MovementDirection == EHMS_MovementDirection::RR;
		const bool bInputIsRight = SignedAngle >= 0.0f;
		const bool bInputStayedOnPreviousSide =
			(bWasRight && bInputIsRight) || (bWasLeft && !bInputIsRight);
		// Hysteresis may keep a lateral sector only while the input remains on
		// that same side. Using AbsoluteAngle alone made +90 (right) and -90
		// (left) indistinguishable, permanently latching the first side entered.
		if (bInputStayedOnPreviousSide
			&& AbsoluteAngle >= MovementDirectionForwardHalfAngle - MovementDirectionHysteresisDegrees
			&& AbsoluteAngle <= MovementDirectionBackwardHalfAngle + MovementDirectionHysteresisDegrees)
		{
			Result = bWasRight ? EHMS_MovementDirection::RL : EHMS_MovementDirection::LL;
		}
		else if (AbsoluteAngle <= MovementDirectionForwardHalfAngle)
		{
			Result = EHMS_MovementDirection::F;
		}
		else if (AbsoluteAngle >= MovementDirectionBackwardHalfAngle)
		{
			Result = EHMS_MovementDirection::B;
		}
		else
		{
			Result = SignedAngle >= 0.0f
				? EHMS_MovementDirection::RL
				: EHMS_MovementDirection::LL;
		}
	}

	return Result;
}

void UHMS_AnimInstance::UpdateTransientMovementDirectionOverride()
{
	bUseTransientMovementDirectionOverride = false;
	TransientMovementDirectionOverride = EHMS_MovementDirection::F;

	if (CharacterProperties.RotationMode == EHMS_RotationMode::OrientToMovement)
	{
		return;
	}

	// Compare wrapped angles. A transition from +179 to -179 is two degrees,
	// not a 358-degree facing reversal.
	const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(
		FutureFacingDelta_LastFrame,
		FutureFacingDelta));
	if (Delta > 200.0f)
	{
		bUseTransientMovementDirectionOverride = true;
		TransientMovementDirectionOverride = EHMS_MovementDirection::B;
	}
}

EHMS_MovementDirection UHMS_AnimInstance::GetEffectiveMovementDirection() const
{
	if (bUseTransientMovementDirectionOverride)
	{
		return TransientMovementDirectionOverride;
	}
	// 获取加速度方向

	return CalculateMovementDirectionFromVectors(SelectedInputAcceleration);
}

float UHMS_AnimInstance::Get_TotalFacingDelta(const TArray<float>& Times) const
{
	if (Times.IsEmpty())
	{
		return 0.0f;
	}

	TArray<FRotator> Rotations;
	Rotations.Reserve(Times.Num());

	for (const float Time : Times)
	{
		FTransformTrajectorySample Sample;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(
			Trajectory,
			Time,
			Sample,
			false
		);

		Rotations.Add(Sample.Facing.Rotator());
	}

	if (Rotations.IsEmpty())
	{
		return 0.0f;
	}

	const FAnimNodeReference OffsetRootNode = GetOffsetRootNodeReference();
	const bool bOffsetRootNodeValid = OffsetRootNode.GetAnimNodePtr<FAnimNode_OffsetRootBone>() != nullptr;
	// The node can still exist in the AnimGraph while HMS root offset is disabled.
	// Reading it in that configuration compares the Mover trajectory against a
	// stale animation-driven offset and repeatedly enters Idle Transition.  The
	// UE5.8 Mover predictor's facing is expressed in the visual component's world
	// basis (the StreamerGirl mesh is ActorYaw - 90), so the component transform is
	// the correct non-offset reference -- not the owning actor transform.
	const bool bUseOffsetRootTransform = OffsetRootBoneEnabled && bOffsetRootNodeValid;
	const FTransform RootWorldTransform = bUseOffsetRootTransform
		? UAnimationWarpingLibrary::GetOffsetRootTransform(OffsetRootNode)
		: (GetSkelMeshComponent()
			? GetSkelMeshComponent()->GetComponentTransform()
			: CharacterTransform);

	// Match GASP: start from the current simulated root world rotation, then
	// accumulate each trajectory sample's relative rotation. When the Offset Root
	// feature is disabled or its node reference is unavailable, use the primary
	// visual component's world transform, which is also the predictor's basis.
	double Angle = UKismetMathLibrary::NormalizedDeltaRotator(
		Rotations[0], RootWorldTransform.Rotator()).Yaw;
	for (int32 Index = 0; Index + 1 < Rotations.Num(); ++Index)
	{
		Angle += UKismetMathLibrary::NormalizedDeltaRotator(
			Rotations[Index + 1], Rotations[Index]).Yaw;
	}

	return static_cast<float>(Angle);
}

float UHMS_AnimInstance::Get_TrajectoryTurnAngle() const
{
	const FRotator FutureRot = UKismetMathLibrary::Conv_VectorToRotator(Trj_FutureVelocity);
	const FRotator CurrentRot = UKismetMathLibrary::Conv_VectorToRotator(Velocity);
	const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(FutureRot, CurrentRot);
	return Delta.Yaw;
}

void UHMS_AnimInstance::Update_AdditiveLean()
{
	if (Velocity.IsNearlyZero(1.0f))
	{
		LateralAccelerationAmount = 0.0f;
		LeanAmount = FVector2D::ZeroVector;
		return;
	}

	const FRotator VelocityRot = Velocity.ToOrientationRotator();
	const FVector LocalAccel = VelocityAcceleration.RotateAngleAxis(-VelocityRot.Yaw, FVector::UpVector);
	const double Lateral = LocalAccel.Y;

	const double SpeedScale = FMath::GetMappedRangeValueClamped(
		FVector2D(200.0, 320.0),
		FVector2D(500.0, 800.0),
		Speed2D
	);

	double Value = 0.0;
	if (SpeedScale > KINDA_SMALL_NUMBER)
	{
		Value = Lateral / SpeedScale;
	}

	LateralAccelerationAmount = FMath::Clamp(Value, -1.0, 1.0);

	switch (MovementDirection)
	{
	case EHMS_MovementDirection::F:
		LeanAmount = FVector2D(+LateralAccelerationAmount, 0.0f);
		break;

	case EHMS_MovementDirection::B:
		LeanAmount = FVector2D(-LateralAccelerationAmount, 0.0f);
		break;

	case EHMS_MovementDirection::LL:
	case EHMS_MovementDirection::LR:
		LeanAmount = FVector2D(0.0f, +LateralAccelerationAmount);
		break;

	case EHMS_MovementDirection::RL:
	case EHMS_MovementDirection::RR:
		LeanAmount = FVector2D(0.0f, -LateralAccelerationAmount);
		break;

	default:
		LeanAmount = FVector2D::ZeroVector;
		break;
	}
}

void UHMS_AnimInstance::SetBlendStackAnimFromChooser(
	const FGameplayTagContainer InStateMachineState,
	const bool bForceBlend
)
{
	// GASP 5.8's OnStateEntry_IdleTransition always requests a forced blend.
	// Keep that contract in native code as well so older copied HMS Blueprint
	// entry graphs cannot silently retain their historical false pin value.
	const bool bEffectiveForceBlend = bForceBlend
		|| InStateMachineState.HasTagExact(TAG_HMS_State_IdleTransition);

	if (!bProcessingChooserRetry)
	{
		bChooserRetryPending = false;
		ChooserRetryElapsedSeconds = 0.0f;
	}

	// State-machine graphs copied from GASP can contain self transitions whose raw
	// trajectory conditions fire while a pivot is crossing zero speed.  Preserve
	// the already selected pivot for its playback range even when such a
	// graph requests a forced chooser evaluation for the same state.
	const bool bAllowOpposingPivotReselect = HasOpposingPivotRequest()
		|| (!IsPivotSelectionLocked() && HasLocomotionContextChanged());
	if (IsPivotSelectionActive()
		&& InStateMachineState == StateMachineState
		&& !bAllowOpposingPivotReselect)
	{
		return;
	}

	// Steering curves can end before Mover's offset root has caught up. The
	// legacy Spin rule then re-enters the same turn (notably 90R at 0.3 s).
	// Keep its playback and footfalls while still allowing a deliberate retarget.
	if (InStateMachineState == StateMachineState && IsTurnInPlaceSelectionActive()
		&& !HasNewTurnInPlaceTarget()
		&& BlendStackInputs.Anim->GetPlayLength() - GetSelectedAnimationTime() > 0.15f)
	{
		return;
	}

	// Rotation Flipped can remain true for several frames during a 90/180-degree
	// start. The copied state graph therefore calls this entry function again with
	// ForceBlend=true while the first Start pose is still being established. Pose
	// matching again at that point replaces the correct idle-origin query with a
	// query against a partially blended transition pose. Keep the first result for
	// its playback range; transitions to a different state are unaffected.
	const bool bAllowStartPivotInterrupt = !IsStartSelectionLocked()
		&& (HasNewLocomotionTarget() || HasLocomotionContextChanged());
	if (IsStartSelectionActive()
		&& InStateMachineState == StateMachineState
		&& !bAllowStartPivotInterrupt)
	{
		return;
	}

	const bool bEnteringNewState = StateMachineState != InStateMachineState;
	const bool bAnimationFamilyChanged = IsValid(BlendStackInputs.Anim)
		&& (CachedChooserWeaponType != UHMS_AnimationQueryLibrary::GetEquippedWeaponType(this)
			|| CachedChooserBehaviorState != UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(this));
	StateMachineState = InStateMachineState;
	NoValidAnim = false;
	NotifyTransition_ReTransition = false;
	NotifyTransition_ToLoop = false;

	if (bCacheRepeatedChooserQueries && !bEffectiveForceBlend && IsChooserContextCached(InStateMachineState)
		&& IsValid(BlendStackInputs.Anim))
	{
		return;
	}

	if (!IsValid(ChooserTable))
	{
		NoValidAnim = true;
		return;
	}

	FHMS_ChooserOutputs ChooserOutput;
	// Sentinels make the UE5.8 Pose Match output bindings observable. The regular
	// output-struct column writes StartTime, but only the Pose Match column writes
	// SearchCost. A non-negative cost therefore proves that Pose Search actually
	// ran for the winning row instead of merely returning its animation asset.
	FPoseHistoryReference PoseHistoryReference;
	bool bPoseHistoryValid = false;
	if (const FAnimNode_PoseSearchHistoryCollector_Base* PoseHistoryNode =
		UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), this))
	{
		if (PoseHistoryNode->GetPoseHistoryPtr() != nullptr)
		{
			PoseHistoryReference = PoseHistoryNode->GetPoseHistoryReference();
			bPoseHistoryValid = true;
		}
	}
	SearchCost = -1.0;

	UAnimationAsset* PreviousPlayingAnimation = nullptr;
	float PreviousPlayingTime = -1.0f;
	FBlendStackAnimNodeReference ActiveBlendStackNode;
	bool bActiveBlendStackNodeValid = false;
	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(
		GetStateMachineBlendStackNodeReference(), ActiveBlendStackNode, bActiveBlendStackNodeValid);
	if (bActiveBlendStackNodeValid)
	{
		PreviousPlayingAnimation = UBlendStackAnimNodeLibrary::GetCurrentAsset(ActiveBlendStackNode);
		PreviousPlayingTime = UBlendStackAnimNodeLibrary::GetCurrentAssetTime(ActiveBlendStackNode);
	}

	FChooserEvaluationContext Context;
	Context.AddObjectParam(this);
	Context.AddStructParam(ChooserOutput);
	// UE5.8 Pose Match columns consume the pose history as context parameter 2.
	Context.AddStructParam(PoseHistoryReference);

	const FInstancedStruct ChooserStruct = UChooserFunctionLibrary::MakeEvaluateChooser(ChooserTable);
	// Match UE 5.8's UK2Node_EvaluateChooser2 in FirstResult mode: the sample asks
	// the chooser for one strongly typed AnimationAsset, not an untyped result array.
	UObject* const FoundObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
		Context, ChooserStruct, UAnimationAsset::StaticClass());
	UAnimationAsset* ChosenAnimation = Cast<UAnimationAsset>(FoundObject);

	// The HMS table was originally authored with the UE5.7 locomotion hierarchy.
	// In that layout a legacy parent table can return the correct asset before the
	// UE5.8 Pose Search column on the leaf table is evaluated. The observable
	// symptom is a valid Loop/Start/Pivot asset with StartTime == 0 and SearchCost == -1.
	// Re-run the restricted search performed by UE5.8's FPoseSearchColumn. For a
	// pivot we must search every row which survived the ordinary chooser filters,
	// not the (possibly wrong) first row returned by the legacy table hierarchy.
	// Equal pivot row conditions are intentional in 5.8: Pose Search is the final
	// discriminator and selects both the asset and its matched start time.
	const bool bIsLoopPoseMatchCandidate =
		InStateMachineState.HasTagExact(TAG_HMS_State_LocomotionLoop);
	// The legacy HMS chooser can reach a Stop leaf without running that leaf's
	// Pose Match output bindings.  This is especially visible after the 5.8
	// schema migration because the dedicated Stops schema finds a useful pose,
	// while the stale Blueprint-struct StartTime binding still leaves the native
	// FHMS_ChooserOutputs value at zero.  Treat stops like the other pose-matched
	// locomotion results so the selected asset time is recovered below.
	const bool bIsStopPoseMatchCandidate = IsValid(ChosenAnimation)
		&& ChosenAnimation->GetName().Contains(TEXT("_Stop_"), ESearchCase::IgnoreCase);
	// A legacy leaf can return one stop foot before its Pose Match column runs.
	// Recover both feet within that exact stop family, retaining gait, direction
	// and reface-angle filtering, instead of searching only the first foot.
	FString StopFamilyPrefix;
	if (bIsStopPoseMatchCandidate && SearchCost < 0.0)
	{
		const FString StopName = ChosenAnimation->GetName();
		if (StopName.EndsWith(TEXT("_Lfoot")) || StopName.EndsWith(TEXT("_Rfoot")))
		{
			StopFamilyPrefix = StopName.LeftChop(6);
		}
	}
	const bool bRecoverStopCandidates = !StopFamilyPrefix.IsEmpty();
	const bool bIsTransitionPoseMatchCandidate =
		ChooserOutput.Tags.Contains(FName(TEXT("Start")))
		|| ChooserOutput.Tags.Contains(FName(TEXT("Pivot")))
		|| bIsStopPoseMatchCandidate;
	auto CanonicalDirection = [](EHMS_MovementDirection Direction)
	{
		if (Direction == EHMS_MovementDirection::LR)
		{
			return EHMS_MovementDirection::LL;
		}
		if (Direction == EHMS_MovementDirection::RR)
		{
			return EHMS_MovementDirection::RL;
		}
		return Direction;
	};
	const EHMS_MovementDirection TransitionSourceDirection =
		CanonicalDirection(CompletionTrackedAnimation.IsValid()
			? CachedChooserMovementDirection : MovementDirection_LastFrame);
	const EHMS_MovementDirection TransitionTargetDirection =
		CanonicalDirection(MovementDirection);
	const bool bSemanticOpposingDirections =
		(TransitionSourceDirection == EHMS_MovementDirection::F
			&& TransitionTargetDirection == EHMS_MovementDirection::B)
		|| (TransitionSourceDirection == EHMS_MovementDirection::B
			&& TransitionTargetDirection == EHMS_MovementDirection::F)
		|| (TransitionSourceDirection == EHMS_MovementDirection::LL
			&& TransitionTargetDirection == EHMS_MovementDirection::RL)
		|| (TransitionSourceDirection == EHMS_MovementDirection::RL
			&& TransitionTargetDirection == EHMS_MovementDirection::LL);
	// In fixed-facing movement a 75-110 degree change is represented by Box/Spin
	// assets, while a semantic opposite direction uses Pivot_X_Y. Recover both as
	// candidate sets: the legacy hierarchy can return a Turn row or no row at all.
	const bool bFixedFacing = CharacterProperties.RotationMode != EHMS_RotationMode::OrientToMovement;
	const bool bContinuingLocomotion =
		CompletionTrackedState.HasTagExact(TAG_HMS_State_LocomotionLoop)
		|| CompletionTrackedState.HasTagExact(TAG_HMS_State_LocomotionTransition);
	// A gradual heading change can cross a strafe sector without ever reaching
	// the sharp-turn angle. The chooser may return nothing in that case. Recover
	// the direction pair for a moving locomotion request even without a Pivot tag.
	const bool bMovingSectorChange = InStateMachineState.HasTagExact(TAG_HMS_State_LocomotionTransition)
		&& bContinuingLocomotion && IsMoving() && Speed2D > 10.0f;
	const bool bFixedFacingDirectionChange = bFixedFacing
		&& TransitionSourceDirection != TransitionTargetDirection
		&& (bImmediateDirectionChangePivot || ChooserOutput.Tags.Contains(FName(TEXT("Pivot"))) || bMovingSectorChange);
	// In strafe, a diagonal input can be >120 degrees from velocity while the
	// semantic transition is LL->F, not a lateral reversal. There is no Pivot_LL_F
	// asset: use the Box/Spin set for the actual source and target sectors.
	const bool bRecoverPivotCandidates = bFixedFacing
		? (bFixedFacingDirectionChange && bSemanticOpposingDirections)
		: (bImmediateDirectionChangePivot && (bSharpDirectionReversal || bSemanticOpposingDirections));
	const bool bRecoverCrossDirectionCandidates = bFixedFacingDirectionChange
		&& !bSemanticOpposingDirections;
	const bool bRecoverTransitionCandidateSet =
		bRecoverPivotCandidates || bRecoverCrossDirectionCandidates;
	if ((IsValid(ChosenAnimation) || bRecoverTransitionCandidateSet)
		&& (FMath::IsNearlyEqual(SearchCost, -1.0, UE_DOUBLE_SMALL_NUMBER)
			|| bRecoverTransitionCandidateSet)
		&& bPoseHistoryValid
		&& (bIsLoopPoseMatchCandidate || bIsTransitionPoseMatchCandidate || bRecoverTransitionCandidateSet))
	{
		TSet<const UAnimationAsset*> RepresentativeAssets;
		if (IsValid(ChosenAnimation))
		{
			RepresentativeAssets.Add(ChosenAnimation);
			if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(ChosenAnimation))
			{
				// Use authored sample references so replacement sets and custom names
				// cannot silently fall back to an unrelated sample sequence.
				for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
				{
					if (IsValid(Sample.Animation)) { RepresentativeAssets.Add(Sample.Animation); }
				}
			}
		}

		auto DirectionToToken = [](EHMS_MovementDirection Direction) -> const TCHAR*
		{
			switch (Direction)
			{
			case EHMS_MovementDirection::B: return TEXT("B");
			case EHMS_MovementDirection::LL:
			case EHMS_MovementDirection::LR: return TEXT("LL");
			case EHMS_MovementDirection::RL:
			case EHMS_MovementDirection::RR: return TEXT("RL");
			default: return TEXT("F");
			}
		};

		const FString PivotAssetToken = FString::Printf(TEXT("_Pivot_%s_%s_"),
			DirectionToToken(TransitionSourceDirection), DirectionToToken(TransitionTargetDirection));
		const FString BoxAssetToken = FString::Printf(TEXT("_Box_%s_%s_"),
			DirectionToToken(TransitionSourceDirection), DirectionToToken(TransitionTargetDirection));
		const FString SpinAssetToken = FString::Printf(TEXT("_Spin_%s_%s_"),
			DirectionToToken(TransitionSourceDirection), DirectionToToken(TransitionTargetDirection));
		// Direction alone is not equivalent to the set of rows surviving the 5.8
		// chooser. The transition database also contains crouch, walk and run assets
		// with the same Pivot_X_Y token. Preserve the chooser's stance/gait filters
		// before asking Pose Search to choose the foot and start time.
		const FString PivotLocomotionToken = Stance.HasTagExact(TAG_HMS_Stance_Crouch)
			? TEXT("_Crouch_")
			: (Gait == EHMS_Gait::Walk ? TEXT("_Walk_") : TEXT("_Run_"));

		TArray<UObject*> ChooserSubObjects;
		GatherChooserSearchObjects(ChooserTable, ChooserSubObjects);

		UPoseSearchDatabase* MatchedDatabase = nullptr;
		UObject* MatchedRepresentativeAsset = nullptr;
		TArray<int32, TInlineAllocator<8>> MatchedDatabaseAssetIndexes;
		const TCHAR* RequiredDatabaseName = !bRecoverTransitionCandidateSet && bIsLoopPoseMatchCandidate
			? TEXT("Locomotion Loops")
			: (bIsStopPoseMatchCandidate
				? TEXT("Locomotion Stops")
				: TEXT("Locomotion Transitions"));
		for (UObject* SubObject : ChooserSubObjects)
		{
			UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(SubObject);
			if (!IsValid(Database)
				|| !Database->GetName().Contains(RequiredDatabaseName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Identical Walk/Run/Pivot tokens exist in every weapon family. Anchor
			// recovery to the actual selected asset before widening to its siblings.
			bool bOwnsChosenFamily = false;
			for (int32 Index = 0; Index < Database->GetNumAnimationAssets(); ++Index)
			{
				bOwnsChosenFamily |= RepresentativeAssets.Contains(Cast<UAnimationAsset>(Database->GetAnimationAsset(Index)));
			}
			if (!bOwnsChosenFamily) { continue; }

			for (int32 AssetIndex = 0; AssetIndex < Database->GetNumAnimationAssets(); ++AssetIndex)
			{
				UObject* DatabaseAsset = Database->GetAnimationAsset(AssetIndex);
				const bool bMatchesLocomotionFamily = IsValid(DatabaseAsset)
					&& DatabaseAsset->GetName().Contains(PivotLocomotionToken, ESearchCase::IgnoreCase);
				const bool bMatchesPivotSet = bRecoverPivotCandidates
					&& bMatchesLocomotionFamily
					&& DatabaseAsset->GetName().Contains(PivotAssetToken, ESearchCase::IgnoreCase);
				const bool bMatchesCrossDirectionSet = bRecoverCrossDirectionCandidates
					&& bMatchesLocomotionFamily
					&& (DatabaseAsset->GetName().Contains(BoxAssetToken, ESearchCase::IgnoreCase)
						|| DatabaseAsset->GetName().Contains(SpinAssetToken, ESearchCase::IgnoreCase));
				const bool bMatchesChosenAsset = !bRecoverTransitionCandidateSet
					&& IsValid(DatabaseAsset)
					&& RepresentativeAssets.Contains(Cast<UAnimationAsset>(DatabaseAsset));
				const bool bMatchesStopSibling = bRecoverStopCandidates && IsValid(DatabaseAsset)
					&& (DatabaseAsset->GetName() == StopFamilyPrefix + TEXT("_Lfoot")
						|| DatabaseAsset->GetName() == StopFamilyPrefix + TEXT("_Rfoot"));
				if (bMatchesPivotSet || bMatchesCrossDirectionSet || bMatchesChosenAsset || bMatchesStopSibling)
				{
					MatchedDatabase = Database;
					if (!IsValid(MatchedRepresentativeAsset))
					{
						MatchedRepresentativeAsset = DatabaseAsset;
					}
					MatchedDatabaseAssetIndexes.Add(AssetIndex);
					if (!bRecoverTransitionCandidateSet && !bRecoverStopCandidates)
					{
						break;
					}
				}
			}

			if (MatchedDatabase)
			{
				break;
			}
		}

		double RecoveredAssetTimeSeconds = -1.0;
		if (MatchedDatabase
			&& IsValid(MatchedRepresentativeAsset)
			&& !MatchedDatabaseAssetIndexes.IsEmpty()
			&& IsValid(MatchedDatabase->Schema))
		{
			FMemMark MemMark(FMemStack::Get());
			UE::PoseSearch::FStackDatabaseToAssetIndexes AssetIndexesToConsiderPerDatabase;
			auto& RestrictedAssetIndexes = AssetIndexesToConsiderPerDatabase.FindOrAdd(MatchedDatabase);
			for (const int32 AssetIndex : MatchedDatabaseAssetIndexes)
			{
				RestrictedAssetIndexes.Add(AssetIndex);
			}

			const FFloatInterval PoseJumpThresholdTime(0.0f, 0.0f);
			UE::PoseSearch::FSearchContext SearchContext(
				0.0f, PoseJumpThresholdTime, FPoseSearchEvent());
			SearchContext.AddRole(
				MatchedDatabase->Schema->GetDefaultRole(),
				&Context,
				PoseHistoryReference.PoseHistory.Get());
			SearchContext.SetAssetIndexesToConsiderPerDatabase(&AssetIndexesToConsiderPerDatabase);

			FPoseSearchContinuingProperties ContinuingProperties;
			ContinuingProperties.PlayingAsset = PreviousPlayingAnimation;
			ContinuingProperties.PlayingAssetAccumulatedTime = PreviousPlayingTime;

			TArray<const UObject*, TInlineAllocator<1>> DatabasesToSearch;
			DatabasesToSearch.Add(MatchedDatabase);
			UE::PoseSearch::FSearchResults_Single SearchResults;
			UPoseSearchLibrary::MotionMatch(
				SearchContext, DatabasesToSearch, ContinuingProperties, SearchResults);

			const UE::PoseSearch::FSearchResult BestSearchResult = SearchResults.GetBestResult();
			if (BestSearchResult.IsValid() && BestSearchResult.IsAssetTimeValid())
			{
				RecoveredAssetTimeSeconds = BestSearchResult.GetAssetTime();
				if (bRecoverTransitionCandidateSet || bRecoverStopCandidates)
				{
					if (const UAnimationAsset* RecoveredAnimation = BestSearchResult.GetCurrentResultAnimationAsset())
					{
						ChosenAnimation = const_cast<UAnimationAsset*>(RecoveredAnimation);
						if (bRecoverTransitionCandidateSet)
						{
							ChooserOutput.Tags.Reset();
							ChooserOutput.Tags.Add(FName(TEXT("Pivot")));
							ChooserOutput.BlendTime = 0.4;
							ChooserOutput.BlendProfile = FName(TEXT("FastFeet_FastRoot"));
						}
					}
				}
				ChooserOutput.StartTime = RecoveredAssetTimeSeconds;
				// Blend Stack sequence players consume seconds, but BlendSpace players
				// consume normalized accumulated time in [0, 1]. The recovery search is
				// performed against the representative sequence, so convert its matched
				// seconds before forwarding the result to a BlendSpace output asset.
				if (Cast<UBlendSpace>(ChosenAnimation))
				{
					if (const UAnimSequenceBase* RepresentativeSequence =
						Cast<UAnimSequenceBase>(BestSearchResult.GetCurrentResultAnimationAsset()))
					{
						const double RepresentativeLength = RepresentativeSequence->GetPlayLength();
						if (RepresentativeLength > UE_DOUBLE_SMALL_NUMBER)
						{
							ChooserOutput.StartTime = FMath::Clamp(
								RecoveredAssetTimeSeconds / RepresentativeLength,
								0.0,
								1.0);
						}
					}
				}
				SearchCost = static_cast<float>(BestSearchResult.PoseCost);
			}
		}

	}

	if (!IsValid(ChosenAnimation))
	{
		if (bFixedFacing && InStateMachineState.HasTagExact(TAG_HMS_State_LocomotionTransition))
		{
			// No transition result is not proof that an index is rebuilding. Keeping
			// the previous loop for the 8-second retry window can visibly run backward
			// while the pawn moves forward. Resolve the CURRENT direction's loop now:
			// older state graphs can re-enter and clear NoValidAnim before taking
			// their fallback edge. A loop result also drives their existing Loop exit.
			bChooserRetryPending = false;
			ChooserRetryElapsedSeconds = 0.0f;
			NoValidAnim = true;
			// This recursion is bounded: the fallback request is a Loop, so it cannot
			// enter this Locomotion Transition-only branch a second time.
			SetBlendStackAnimFromChooser(FGameplayTagContainer(TAG_HMS_State_LocomotionLoop), false);
			return;
		}

		constexpr float ChooserAsyncBuildRetryTimeout = 8.0f;
		const bool bIsIdleTransitionRequest =
			InStateMachineState.HasTagExact(TAG_HMS_State_IdleTransition);

		// GASP 5.8 does not keep a locomotion clip alive when an Idle Transition
		// query has no result. It publishes NoValidAnim and lets the stock
		// "General - No Valid Anim" rule return to Idle Loop. Retaining the
		// previous Start/Loop here is unsafe: a failed or rebuilding idle database
		// otherwise leaves a running clip visible on a stationary capsule for the
		// whole retry timeout (the observed endless foot stomping).
		if (bIsIdleTransitionRequest)
		{
			bChooserRetryPending = false;
			ChooserRetryElapsedSeconds = 0.0f;
			BlendStackInputs.bLoop = false;
			NoValidAnim = true;
			return;
		}

		const bool bCanKeepCurrentAnimation = IsValid(BlendStackInputs.Anim);
		const bool bShouldRetry = bCanKeepCurrentAnimation
			&& ChooserRetryElapsedSeconds < ChooserAsyncBuildRetryTimeout;

		if (bShouldRetry)
		{
			// The UE5.8 "General - No Valid Anim" transition rule is:
			//     NoValidAnim OR BlendStackInputs.Loop
			// While Pose Search is building its async index, BlendStackInputs still
			// describes the animation from the previous state (normally Idle Loop).
			// Leaving that stale Loop bit set makes Locomotion Transition exit on its
			// very first frame, and the following Locomotion Loop request overwrites
			// the queued Start request. Keep the previous asset visible, but do not
			// publish its state-local loop metadata until the retry returns a real
			// UE5.8 Chooser result. If a looping result legitimately wins the motion
			// match, the success path below sets bLoop back to true and the stock rule
			// still enters the looping state as intended.
			BlendStackInputs.bLoop = false;
			NoValidAnim = false;
			bChooserRetryPending = true;
			bChooserRetryForceBlend = bEffectiveForceBlend;
			ChooserRetryState = InStateMachineState;

			return;
		}

		bChooserRetryPending = false;
		NoValidAnim = true;
		return;
	}

	bChooserRetryPending = false;
	ChooserRetryElapsedSeconds = 0.0f;

	// Limit a stationary reface by its actual root-turn window. A fixed 0.4 s
	// clamp plus a 0.3 s blend hid almost all rotation in the CC5 run starts,
	// even though their already-running tail continued for another two seconds.
	if (ChooserOutput.Tags.Contains(FName(TEXT("Start"))))
	{
		const bool bStationaryOrigin = Speed2D < 50.0f
			|| CompletionTrackedState.HasTagExact(TAG_HMS_State_IdleLoop)
			|| CompletionTrackedState.HasTagExact(TAG_HMS_State_IdleTransition)
			|| CompletionTrackedState.HasTagExact(TAG_HMS_State_IdleBreak);
		const bool bSignificantReface = FMath::Abs(FutureFacingDelta) >= 60.0f;
		const UAnimSequence* StartSequence = Cast<UAnimSequence>(ChosenAnimation);
		if (bStationaryOrigin && StartSequence)
		{
			constexpr double SampleStep = 1.0 / 60.0;
			const double ScanEnd = FMath::Min(StartSequence->GetPlayLength(), 1.5);
			TArray<TPair<double, double>> YawSamples;
			double AccumulatedYaw = 0.0;
			for (double Time = SampleStep; Time <= ScanEnd; Time += SampleStep)
			{
				const FTransform Delta = StartSequence->ExtractRootMotionFromRange(
					Time - SampleStep, Time, FAnimExtractContext(Time - SampleStep, true, {}, false));
				AccumulatedYaw += FMath::Abs(FMath::UnwindDegrees(Delta.Rotator().Yaw));
				YawSamples.Emplace(Time, AccumulatedYaw);
			}
			if (AccumulatedYaw >= 30.0)
			{
				const double AllowedYaw = AccumulatedYaw * FMath::Clamp(RefaceStartSkippedYawFraction, 0.f, 0.3f);
				double LatestStart = 0.0;
				for (const auto& Sample : YawSamples)
				{
					if (Sample.Value > AllowedYaw) { break; }
					LatestStart = Sample.Key;
				}
				if (bSignificantReface)
				{
					ChooserOutput.StartTime = FMath::Min(ChooserOutput.StartTime, LatestStart);
				}
				// A 45-degree request may match the middle of a 90-degree reface.
				// Keep that useful time match, but do not hide its remaining turn
				// behind the longer blend used for a straight acceleration clip.
				ChooserOutput.BlendTime = FMath::Min(ChooserOutput.BlendTime, static_cast<double>(RefaceStartBlendTime));
			}
		}
	}

	// In OrientToMovement, retargeted Turn clips contain an initial run-up before
	// their root starts rotating.  A match at zero therefore turns late, while a
	// match after the rotation has completed produces a capsule-only snap.  Move
	// only these Turn results to the earliest searchable point that leaves the
	// root-turn onset close to the blend window.  Strafe Pivot/Box clips keep the
	// exact Pose Search result because they intentionally preserve facing.
	const bool bMovingTurnResult =
		ChooserOutput.Tags.Contains(FName(TEXT("Pivot")))
		&& RotationMode == EHMS_RotationMode::OrientToMovement
		&& ChosenAnimation->GetName().Contains(TEXT("_Turn_"), ESearchCase::IgnoreCase);
	if (bMovingTurnResult)
	{
		if (const UAnimSequenceBase* TurnSequence = Cast<UAnimSequenceBase>(ChosenAnimation))
		{
			const double AnimationLength = TurnSequence->GetPlayLength();
			const double PoseMatchedStartTime = ChooserOutput.StartTime;
			constexpr double SampleStep = 1.0 / 120.0;
			constexpr double MaximumAcceptedYawOnset = 0.5;
			constexpr double TargetYawOnset = 0.35;
			constexpr double MinimumRemainingPlayback = 0.75;
			const double LatestAllowedStartTime = FMath::Max(
				AnimationLength - MinimumRemainingPlayback, 0.0);

			auto FindYawOnset = [TurnSequence, AnimationLength](const double StartTime)
			{
				constexpr double LocalSampleStep = 1.0 / 120.0;
				for (double Elapsed = LocalSampleStep;
					StartTime + Elapsed <= AnimationLength;
					Elapsed += LocalSampleStep)
				{
					FAnimExtractContext ExtractionContext(StartTime, true, {}, false);
					const FTransform RootMotion = TurnSequence->ExtractRootMotionFromRange(
						StartTime, StartTime + Elapsed, ExtractionContext);
					if (FMath::Abs(FMath::UnwindDegrees(RootMotion.Rotator().Yaw)) >= 5.0)
					{
						return Elapsed;
					}
				}
				return -1.0;
			};

			double ExclusionEndTime = 0.0;
			for (const FAnimNotifyEvent& NotifyEvent : TurnSequence->Notifies)
			{
				if (NotifyEvent.NotifyName == FName(TEXT("PoseSearchExcludeFromDatabase")))
				{
					ExclusionEndTime = FMath::Max(
						ExclusionEndTime,
						static_cast<double>(NotifyEvent.GetTriggerTime() + NotifyEvent.GetDuration()));
				}
			}

			const double OriginalYawOnset = FindYawOnset(PoseMatchedStartTime);
			if (OriginalYawOnset < 0.0)
			{
				// The selected point is already past all meaningful root rotation.
				ChooserOutput.StartTime = FMath::Clamp(
					ExclusionEndTime + SampleStep, 0.0, LatestAllowedStartTime);
			}
			else if (OriginalYawOnset > MaximumAcceptedYawOnset)
			{
				ChooserOutput.StartTime = FMath::Clamp(
					PoseMatchedStartTime + OriginalYawOnset - TargetYawOnset,
					ExclusionEndTime + SampleStep,
					LatestAllowedStartTime);
			}

		}
	}

	// Pose Search can return an accumulated continuing time.  BlendStack's
	// StartTime, however, is an asset-local time.  GASP normally avoids exposing
	// this distinction by re-entering through its Spin transition; an explicit
	// mouse interruption can make the accumulated value visible.  Normalize it
	// into the selected non-looping clip instead of arbitrarily jumping to a
	// fixed "0.9 seconds remaining" point.
	const bool bTurnInPlaceResult = Speed2D < 50.0f
		&& ChooserOutput.Tags.ContainsByPredicate(
			[](const FName& Tag)
			{
				return Tag.ToString().Contains(TEXT("Spin"), ESearchCase::IgnoreCase)
					|| Tag.ToString().Contains(TEXT("Turn"), ESearchCase::IgnoreCase);
			});
	if (bTurnInPlaceResult)
	{
		TurnInPlaceFacingOnSelection = GetTurnInPlaceTargetDirection();
		const double AnimationLength = ChosenAnimation->GetPlayLength();
		if (AnimationLength > UE_DOUBLE_SMALL_NUMBER
			&& (ChooserOutput.StartTime < 0.0
				|| ChooserOutput.StartTime >= AnimationLength))
		{
			const double PoseMatchedStartTime = ChooserOutput.StartTime;
			ChooserOutput.StartTime = PoseMatchedStartTime >= 0.0
				? FMath::Fmod(PoseMatchedStartTime, AnimationLength)
				: 0.0;
		}

		if (bRestartTurnInPlaceFromBeginningOnNextSelection
			&& InStateMachineState.HasTagExact(TAG_HMS_State_IdleTransition))
		{
			const double PoseMatchedStartTime = ChooserOutput.StartTime;
			ChooserOutput.StartTime = 0.0;
			bRestartTurnInPlaceFromBeginningOnNextSelection = false;
		}
	}

	// Keep pose matching available across the asset, but allow an authored row
	// to retain its entry motion. Some turns finish root rotation before the
	// best matching pose; starting at that pose would play only their settling tail.
	if (FMath::IsFinite(ChooserOutput.MaximumStartTime) && ChooserOutput.MaximumStartTime >= 0.0)
	{
		ChooserOutput.StartTime = FMath::Clamp(ChooserOutput.StartTime, 0.0, ChooserOutput.MaximumStartTime);
	}

	// AddStructParam refers to ChooserOutput directly, just like the 5.8 Blueprint
	// node's output struct pin. No Context memory reinterpretation is required.
	const FHMS_ChooserOutputs& Output = ChooserOutput;
	BlendStackInputs.Anim = ChosenAnimation;
	bool bIsAssetLooping = false;
	UPoseSearchLibrary::IsAnimationAssetLooping(BlendStackInputs.Anim, bIsAssetLooping);
	BlendStackInputs.bLoop = bIsAssetLooping;
	BlendStackInputs.StartTime = Output.StartTime;
	BlendStackInputs.BlendTime = Output.BlendTime;
	if (bAnimationFamilyChanged) { BlendStackInputs.BlendTime = FMath::Max(BlendStackInputs.BlendTime, 0.3); }
	BlendStackInputs.BlendCurve = Output.BlendCurve;
	BlendStackInputs.Tags = Output.Tags;
	// The full-weight entry montage supplies the visible blend. Prepare its destination
	// directly underneath it instead of revealing an old locomotion-to-idle blend as well.
	if (bEnteringNewState && Output.Tags.Contains(FName(TEXT("Interaction")))
		&& Blueprint_GetSlotMontageLocalWeight(FName(TEXT("DefaultSlot"))) >= 0.999f)
	{
		BlendStackInputs.BlendTime = 0.0;
		ForceBlendStackNextUpdate();
	}
	if (BlendStackInputs.Tags.Contains(FName(TEXT("Pivot")))
		|| BlendStackInputs.Tags.Contains(FName(TEXT("Start"))))
	{
		// In strafe, facing stays fixed and therefore cannot identify an opposing
		// movement request. Store the selected movement direction instead.
		const FVector SelectedPivotDirection = GetSelectedWorldInputDirection();
		PivotFacingOnSelection = SelectedPivotDirection.IsNearlyZero()
			? Trj_FutureFacing
			: SelectedPivotDirection.ToOrientationRotator();
		bHasPivotFacingOnSelection = true;
	}
	else
	{
		bHasPivotFacingOnSelection = false;
	}
	// UE5.8's sample forwards the Pose Match output verbatim. Retargeted stationary
	// re-facing starts and excluded Pivot intervals are the guarded exceptions above.

	if (const UBlendProfile* BlendProfile = GetBlendProfileByName(Output.BlendProfile))
	{
		BlendStackInputs.BlendProfile = const_cast<UBlendProfile*>(BlendProfile);
	}
	else
	{
		BlendStackInputs.BlendProfile = nullptr;
	}

	// Every accepted non-looping result is forced into the Blend Stack below,
	// including the SAME asset at the SAME start time. That is a new playback:
	// its Start/Pivot protection and fallback clock must start from zero as well.
	// Asset identity alone misses repeated pivots and leaves them unprotected.
	const bool bRestartsPlayback = !BlendStackInputs.bLoop;
	if (bRestartsPlayback || CompletionTrackedAnimation.Get() != BlendStackInputs.Anim
		|| CompletionTrackedState != InStateMachineState)
	{
		CompletionTrackedAnimation = BlendStackInputs.Anim;
		CompletionTrackedState = InStateMachineState;
		CompletionTrackedElapsedSeconds = 0.0f;
	}

	CacheChooserContext(InStateMachineState);

	// UE 5.8's SetBlendStackAnimFromChooser graph forces the BlendStack update for
	// every non-looping result. Loop results keep their pose-matched StartTime and
	// blend naturally from the transition clip.
	if (!BlendStackInputs.bLoop)
	{
		ForceBlendStackNextUpdate();
	}
}

bool UHMS_AnimInstance::IsChooserContextCached(const FGameplayTagContainer& InStateMachineState) const
{
	return bHasCachedChooserContext
		&& CachedChooserTable.Get() == ChooserTable
		&& CachedChooserState == InStateMachineState
		&& CachedChooserStance == Stance
		&& CachedChooserWeaponType == UHMS_AnimationQueryLibrary::GetEquippedWeaponType(this)
		&& CachedChooserBehaviorState == UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(this)
		&& CachedChooserMovementDirection == MovementDirection
		&& CachedChooserGait == Gait
		&& CachedChooserRotationMode == RotationMode
		&& CachedChooserMovementMode == MovementMode;
}

void UHMS_AnimInstance::CacheChooserContext(const FGameplayTagContainer& InStateMachineState)
{
	bHasCachedChooserContext = true;
	CachedChooserTable = ChooserTable;
	CachedChooserState = InStateMachineState;
	CachedChooserStance = Stance;
	CachedChooserWeaponType = UHMS_AnimationQueryLibrary::GetEquippedWeaponType(this);
	CachedChooserBehaviorState = UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(this);
	CachedChooserMovementDirection = MovementDirection;
	CachedChooserGait = Gait;
	CachedChooserRotationMode = RotationMode;
	CachedChooserMovementMode = MovementMode;
}

void UHMS_AnimInstance::ForceBlendStackNextUpdate() const
{
	FBlendStackAnimNodeReference BlendStackNode;
	bool bValid = false;
	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(
		GetStateMachineBlendStackNodeReference(), BlendStackNode, bValid);
	if (bValid)
	{
		UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(BlendStackNode);
	}
}

float UHMS_AnimInstance::Get_DynamicPlayRate(const FAnimNodeReference& BlendStackInput) const
{
	UAnimationAsset* CurrentAsset =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(BlendStackInput);

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CurrentAsset);
	if (!IsValid(AnimSequence))
	{
		return 1.0f;
	}

	const float AnimTime =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(BlendStackInput);

	float AlphaCurve = 0.0f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		FName(TEXT("Enable_Warping")),
		AnimTime,
		AlphaCurve))
	{
		return 1.0f;
	}

	float SpeedCurve = 0.0f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		FName(TEXT("MoveData_Speed")),
		AnimTime,
		SpeedCurve))
	{
		return 1.0f;
	}

	float MaxDynamicPlayRate = 1.25f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		FName(TEXT("MaxDynamicPlayRate")),
		AnimTime,
		MaxDynamicPlayRate))
	{
		MaxDynamicPlayRate = 1.25f;
	}

	float MinDynamicPlayRate = 1.0f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		FName(TEXT("MinDynamicPlayRate")),
		AnimTime,
		MinDynamicPlayRate))
	{
		MinDynamicPlayRate = 1.0f;
	}

	const double ClampedSpeedCurve = FMath::Clamp(
		static_cast<double>(SpeedCurve),
		0.1,
		999.0
	);

	const double RawRate = UKismetMathLibrary::SafeDivide(
		static_cast<double>(Speed2D),
		ClampedSpeedCurve
	);

	const double ClampedRate = FMath::Clamp(
		RawRate,
		static_cast<double>(MinDynamicPlayRate),
		static_cast<double>(MaxDynamicPlayRate)
	);

	const double BaseDynamicRate = UKismetMathLibrary::Lerp(
		1.0,
		ClampedRate,
		static_cast<double>(AlphaCurve)
	);

	const double AngularVelocityAbs = FMath::Abs(
		static_cast<double>(Trj_CurrentAngularVelocity.Z)
	);

	const double AngularScale = UKismetMathLibrary::MapRangeClamped(
		AngularVelocityAbs,
		100.0,
		400.0,
		1.0,
		1.2
	);

	const double CirclingAlpha = UKismetMathLibrary::MapRangeClamped(
		static_cast<double>(Trj_CirclingTime),
		0.0,
		0.5,
		0.0,
		1.0
	);

	const double CirclingRateScale = UKismetMathLibrary::Lerp(
		1.0,
		AngularScale,
		CirclingAlpha
	);

	return static_cast<float>(BaseDynamicRate * CirclingRateScale);
}

bool UHMS_AnimInstance::EnableSteering(const FAnimNodeReference& Node) const
{
	const bool bCurrentBlendStackAnimIsActive =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimIsActive(Node);

	const bool bLoopingAnim = BlendStackInputs.bLoop;
	const bool bCanSteerWhileMoving = (bCurrentBlendStackAnimIsActive || bLoopingAnim) && IsMoving();

	const bool bMovementModeMatchesA =
		MovementMode == EHMS_MovementMode::Sliding;

	const bool bMovementModeMatchesB =
		MovementMode == EHMS_MovementMode::OnGround;

	return bCanSteerWhileMoving || bMovementModeMatchesA || bMovementModeMatchesB;
}

FQuat UHMS_AnimInstance::Get_DesiredFacing(
	const FAnimNodeReference& Node,
	const FName SteeringTargetTimeCurveName
) const
{
	if (bUsingLightweightTrajectory)
	{
		// Lightweight prediction stores actor-facing orientation, while Steering
		// compares against the visual root. The full Mover predictor below already
		// outputs visual-component facing and must not receive this offset twice.
		const FQuat MeshFacingOffset = GetProxyOnAnyThread<FAnimInstanceProxy>()
			.GetComponentRelativeTransform().GetRotation();
		return (Trj_FutureFacing.Quaternion() * MeshFacingOffset).GetNormalized();
	}

	UAnimationAsset* CurrentAsset =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node);

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CurrentAsset);
	if (!IsValid(AnimSequence))
	{
		return Trj_FutureFacing.Quaternion();
	}

	const float AnimTime =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node);

	float SteeringTargetTimeNormalized = 0.0f;
	const bool bHasCurve = UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		SteeringTargetTimeCurveName,
		AnimTime,
		SteeringTargetTimeNormalized
	);

	const float SafeNormalizedTime = bHasCurve ? SteeringTargetTimeNormalized : 0.0f;

	const float TrajectorySampleTime = static_cast<float>(
		UKismetMathLibrary::MapRangeClamped(
			static_cast<double>(SafeNormalizedTime),
			0.0,
			1.0,
			0.1,
			1.5
		)
		);

	FTransformTrajectorySample Sample;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(
		Trajectory,
		TrajectorySampleTime,
		Sample,
		false
	);

	return Sample.Facing;
}

float UHMS_AnimInstance::Get_ProceduralTargetTime(
	const FAnimNodeReference& Node,
	const FName CurveName
) const
{
	UAnimationAsset* CurrentAsset =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node);

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CurrentAsset);
	if (!IsValid(AnimSequence))
	{
		return 0.4f;
	}

	const float AnimTime =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node);

	float CurveValue = 0.0f;
	UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		CurveName,
		AnimTime,
		CurveValue
	);

	return static_cast<float>(
		UKismetMathLibrary::MapRangeClamped(
			static_cast<double>(CurveValue),
			0.0,
			1.0,
			0.1,
			0.3
		)
		);
}

float UHMS_AnimInstance::Get_StrideWarpAlpha(
	const FAnimNodeReference& Node,
	const FName EnableWarpingCurveName,
	const FName EnableStrideWarpingCurveName
) const
{
	// UE 5.8 uses the common Enable_Warping curve for both stride and
	// orientation warping. Keep the legacy parameter in the public signature so
	// existing Anim Blueprint bindings remain compatible, but do not combine it
	// with the common curve.
	(void)EnableStrideWarpingCurveName;

	UAnimationAsset* CurrentAsset =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node);

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CurrentAsset);
	if (!IsValid(AnimSequence))
	{
		// Blend Spaces are the normal locomotion-loop result in the UE 5.8
		// sample. Their evaluated curves live on the AnimInstance rather than on
		// a single source sequence.
		return FMath::Clamp(GetCurveValue(EnableWarpingCurveName), 0.0f, 1.0f);
	}

	const float AnimTime =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node);

	float EnableWarpingValue = 0.0f;
	UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		EnableWarpingCurveName,
		AnimTime,
		EnableWarpingValue
	);

	return FMath::Clamp(EnableWarpingValue, 0.0f, 1.0f);
}

float UHMS_AnimInstance::Get_StrafeWarpAlpha(
	const FAnimNodeReference& Node,
	const FName EnableWarpingCurveName,
	const FName EnableStrafeWarpingCurveName
) const
{
	// See Get_StrideWarpAlpha: UE 5.8 drives both nodes from Enable_Warping.
	(void)EnableStrafeWarpingCurveName;

	UAnimationAsset* CurrentAsset =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node);

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CurrentAsset);
	if (!IsValid(AnimSequence))
	{
		return FMath::Clamp(GetCurveValue(EnableWarpingCurveName), 0.0f, 1.0f);
	}

	const float AnimTime =
		UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node);

	float EnableWarpingValue = 0.0f;
	UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		AnimSequence,
		EnableWarpingCurveName,
		AnimTime,
		EnableWarpingValue
	);

	return FMath::Clamp(EnableWarpingValue, 0.0f, 1.0f);
}

EOrientationWarpingSpace UHMS_AnimInstance::Get_OrientationWarpingWarpingSpace() const
{
	return OffsetRootBoneEnabled
		? EOrientationWarpingSpace::RootBoneTransform
		: EOrientationWarpingSpace::ComponentTransform;
}

bool UHMS_AnimInstance::ShouldResetOffsetRoot() const
{
	// Mover owns montage displacement and facing. Release alone can retain a
	// rotation offset forever when clamped to root velocity in a paused hold pose.
	return !OffsetRootBoneEnabled || RagdollProperties.State != EHMS_RagdollState::Animated
		|| IsSlotActive(FName(TEXT("DefaultSlot")))
		|| BlendStackInputs.Tags.Contains(FName(TEXT("Interaction")));
}

EOffsetRootBoneMode UHMS_AnimInstance::Get_OffsetRootTranslationMode() const
{
	if (ShouldResetOffsetRoot() || IsSlotActive(FName(TEXT("DefaultSlot"))))
	{
		return EOffsetRootBoneMode::Release;
	}

	switch (MovementMode)
	{
	case EHMS_MovementMode::OnGround:
		return IsMoving() ? EOffsetRootBoneMode::Interpolate : EOffsetRootBoneMode::Release;
		//return EOffsetRootBoneMode::Release;

	case EHMS_MovementMode::InAir:
		return EOffsetRootBoneMode::Release;

	default:
		break;
	}

	return EOffsetRootBoneMode::Accumulate;
}

EOffsetRootBoneMode UHMS_AnimInstance::Get_OffsetRootRotationMode() const
{
	if (ShouldResetOffsetRoot() || IsSlotActive(FName(TEXT("DefaultSlot"))))
	{
		return EOffsetRootBoneMode::Release;
	}
	const FString StateString = StateMachineState.ToStringSimple();
	if (StateString.Contains(TEXT("Transition")))
	{
		// 转身/折返动画播放期间保持当前视觉朝向，让动画本身完成旋转。
		return EOffsetRootBoneMode::Accumulate;
	}
	if (StateString.Contains(TEXT("Loop")))
	{
		// Mover owns facing during sustained locomotion. Release keeps the
		// half-life blend back to component facing without accumulating more
		// rotation from the outgoing turn or the looping animation stream.
		// Accumulate above still lets starts/pivots complete their authored turn.
		return EOffsetRootBoneMode::Release;
	}

	return IsMoving() ? EOffsetRootBoneMode::Interpolate : EOffsetRootBoneMode::Accumulate;
}

float UHMS_AnimInstance::Get_OffsetRootTranslationHalfLife() const
{
	if (bSharpDirectionReversal)
	{
		return SharpReversalTranslationHalfLife;
	}
	return (MovementState == EHMS_MovementState::Idle) ? 0.0f : 0.3f;
}

float UHMS_AnimInstance::Get_OffsetRootTranslationRadius() const
{
	return OffsetRootTranslationRadius;
}

FVector UHMS_AnimInstance::Get_StrafeWarpDirection() const
{
	const double AngularVelocityZAbs = FMath::Abs(
		static_cast<double>(Trj_CurrentAngularVelocity.Z)
	);
	const double Alpha = UKismetMathLibrary::MapRangeClamped(
		AngularVelocityZAbs,
		20.0,
		100.0,
		0.0,
		1.0
	);

	return UKismetMathLibrary::VLerp(
		LastNonZeroVelocity,
		Trj_NearFutureVelocity,
		static_cast<float>(Alpha)
	);
}

UAnimationAsset* UHMS_AnimInstance::GetLocomotionPlayback(float& OutTime, FVector& OutBlendParameters, FTransform& OutRootTransform, bool& bOutRootValid) const
{
	OutTime = 0; OutBlendParameters = FVector::ZeroVector; OutRootTransform = FTransform::Identity;
	const FAnimNodeReference RootRef = GetOffsetRootNodeReference();
	bOutRootValid = RootRef.GetAnimNodePtr<FAnimNode_OffsetRootBone>() != nullptr;
	if (bOutRootValid) { OutRootTransform = UAnimationWarpingLibrary::GetOffsetRootTransform(RootRef); }
	FBlendStackAnimNodeReference Stack; bool bValid = false;
	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(GetStateMachineBlendStackNodeReference(), Stack, bValid);
	if (!bValid) { return nullptr; }
	OutTime = UBlendStackAnimNodeLibrary::GetCurrentAssetTime(Stack);
	if (const auto* Node = Stack.GetAnimNodePtr<FAnimNode_BlendStack>()) { OutBlendParameters = Node->GetBlendParameters(); }
	return UBlendStackAnimNodeLibrary::GetCurrentAsset(Stack);
}

FVector UHMS_AnimInstance::Get_BlendSpaceInputs() const
{
	if (bOverrideBlendSpaceInputs) { return BlendSpaceInputOverride; }
	if (!BlendStackInputs.Tags.Contains(FName(TEXT("BS_Slope"))))
	{
		return FVector::ZeroVector;
	}

	const float SideSlopeAngle = SlopeAngle.Y;
	if (FMath::Abs(SideSlopeAngle) < SlopeBlendDeadZone)
	{
		return FVector::ZeroVector;
	}

	const float SlopeBlendX = FMath::GetMappedRangeValueClamped(
		FVector2D(-FMath::Max(SlopeBlendFullAngle, 1.f), FMath::Max(SlopeBlendFullAngle, 1.f)),
		FVector2D(-1.0f, 1.0f),
		SideSlopeAngle);
	return FVector(SlopeBlendX, 0.0f, 0.0f);
}

bool UHMS_AnimInstance::IsAnimationAlmostComplete()
{
	// A late pose match must still establish its start/turn pose before a generic
	// completion rule can replace it. Re-entry rules use the same protection window.
	if (IsStartSelectionLocked() || IsPivotSelectionLocked())
	{
		return false;
	}
	// Locomotion Transition has a lower-priority self-transition for a new Pivot,
	// but its generic "almost complete" exit has higher state-machine priority.
	// Without this guard a second reversal is evaluated as:
	// Pivot A -> Loop for one frame -> Pivot B. Keep the transition state active so
	// the dedicated Pivot re-entry can win directly on this update.
	if (HasNewLocomotionTarget())
	{
		return false;
	}

	FBlendStackAnimNodeReference BlendStackNode;
	bool bValid = false;

	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(
		GetStateMachineBlendStackNodeReference(),
		BlendStackNode,
		bValid
	);

	if (!bValid)
	{
		const UAnimationAsset* TrackedAnimation = CompletionTrackedAnimation.Get();
		const bool bCanUseTimedFallback = IsValid(TrackedAnimation)
			&& TrackedAnimation == BlendStackInputs.Anim
			&& CompletionTrackedState == StateMachineState
			&& !BlendStackInputs.bLoop;
		const float PlayLength = bCanUseTimedFallback ? TrackedAnimation->GetPlayLength() : 0.0f;
		const float CurrentTime = bCanUseTimedFallback
			? FMath::Clamp(BlendStackInputs.StartTime + CompletionTrackedElapsedSeconds, 0.0f, PlayLength)
			: 0.0f;
		const float TimeRemaining = bCanUseTimedFallback ? FMath::Max(PlayLength - CurrentTime, 0.0f) : 0.0f;
		const bool bFallbackResult = bCanUseTimedFallback && PlayLength > 0.0f && TimeRemaining <= 0.75f;

		return bFallbackResult;
	}

	const bool bIsLooping = UBlendStackAnimNodeLibrary::IsCurrentAssetLooping(BlendStackNode);
	const float TimeRemaining = UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(BlendStackNode);
	return !bIsLooping && TimeRemaining <= 0.75f;
}

bool UHMS_AnimInstance::ShouldTransition_LocomotionReselect()
{
	if (IsPivotSelectionLocked() || IsStartSelectionLocked())
	{
		return false;
	}

	const bool bStanceChanged = Stance != Stance_LastFrame;
	const bool bMovementDirectionChanged = MovementDirection != MovementDirection_LastFrame;
	const bool bGaitChanged = Gait != Gait_LastFrame;
	const bool bImmediatePivotStarted = bImmediateDirectionChangePivot
		&& !bImmediateDirectionChangePivot_LastFrame;

	const bool bAnyStateChanged =
		bStanceChanged ||
		bMovementDirectionChanged ||
		bGaitChanged ||
		bImmediatePivotStarted || HasLocomotionContextChanged() || HasNewLocomotionTarget();

	return bAnyStateChanged;
}

bool UHMS_AnimInstance::FilterLegacyTransitionRule(const FString& RuleName, bool bRawResult) const
{
	const bool bIsPivotRule = RuleName.Contains(TEXT("Pivot"), ESearchCase::IgnoreCase);
	const bool bIsStateChangedRule = RuleName.Contains(TEXT("State Changed"), ESearchCase::IgnoreCase);
	const bool bNewTarget = HasNewLocomotionTarget();
	const bool bContextChanged = HasLocomotionContextChanged();
	// The old graph compares only the last two frames. Re-evaluate against the
	// successful selection so changes made during the blend window are not lost.
	const bool bPendingRequest = (bIsStateChangedRule && bContextChanged)
		|| (bIsPivotRule && bNewTarget);
	if (!bRawResult && !bPendingRequest)
	{
		return false;
	}

	// These are the labels used by the extracted GASP Re-Enter transition rules.
	// In the original Blueprint LogDebug only forwarded the raw result.  That is
	// unsafe once Chooser evaluation is asynchronous (UE 5.8), because the state
	// can re-enter every frame while its first result is still being built.
	const bool bIsProtectedReentryRule =
		RuleName.Contains(TEXT("State Changed"), ESearchCase::IgnoreCase)
		|| RuleName.Contains(TEXT("Pivot"), ESearchCase::IgnoreCase)
		|| RuleName.Contains(TEXT("Broke"), ESearchCase::IgnoreCase)
		|| RuleName.Contains(TEXT("Rotation Flipped"), ESearchCase::IgnoreCase);

	if (!bIsProtectedReentryRule)
	{
		return true;
	}

	const bool bRetryBlocked = bChooserRetryPending || bProcessingChooserRetry;
	const bool bSelectionChanged = bNewTarget || bContextChanged;
	const bool bPivotBlocked = IsPivotSelectionLocked()
		|| (IsPivotSelectionActive() && !bSelectionChanged);
	// A genuine direction reversal must be able to interrupt a Start animation.
	// The old combined guard treated StartLock as a global transition lock, so a
	// 180-degree input made during Start was delayed until the Start clip ended.
	// Keep StartLock for duplicate same-state rules (Rotation Flipped / State
	// Changed / Broke), while the dedicated Pivot rule is guarded only against
	// reselecting an already active pivot.
	const bool bStartBlocked = IsStartSelectionLocked()
		|| (IsStartSelectionActive() && !bSelectionChanged);
	const bool bBlocked = bRetryBlocked || bPivotBlocked || bStartBlocked;

	return !bBlocked;
}

bool UHMS_AnimInstance::ShouldTransition_TurnInPlaceToIdleLoop()
{
	const float TurnInPlaceSteeringAlpha =
		GetCurveValue(FName(TEXT("Enable_TurnInPlaceSteering")));
	const bool bSteeringWindowEnded = TurnInPlaceSteeringAlpha < 0.1f;
	const bool bFacingAligned = FMath::Abs(FutureFacingDelta) <= 10.0f;
	const bool bAnimationAlmostComplete = IsAnimationAlmostComplete();
	const bool bResult = bFacingAligned || (bSteeringWindowEnded && bAnimationAlmostComplete);

	return bResult;
}

bool UHMS_AnimInstance::ShouldTurnInPlace()
{
	if (!CachedPawn.IsValid())
	{
		return false;
	}

	const float AbsFacingDelta = FMath::Abs(FutureFacingDelta);
	const bool bFacingLargeEnough = AbsFacingDelta >= TurnInPlaceAngleThreshold;
	const bool bIsNearlyIdle = Speed2D < 50.0f;
	const bool bIsIdleState = MovementState == EHMS_MovementState::Idle;

	return bFacingLargeEnough && bIsNearlyIdle && bIsIdleState;
}

bool UHMS_AnimInstance::IsTurnInPlaceSelectionActive() const
{
	return StateMachineState.HasTagExact(TAG_HMS_State_IdleTransition)
		&& IsValid(BlendStackInputs.Anim) && !BlendStackInputs.bLoop
		&& (BlendStackInputs.Tags.Contains(FName(TEXT("Spin_L")))
			|| BlendStackInputs.Tags.Contains(FName(TEXT("Spin_R"))))
		&& BlendStackInputs.Anim->GetPlayLength() - GetSelectedAnimationTime() > 0.05f;
}

FVector UHMS_AnimInstance::GetTurnInPlaceTargetDirection() const
{
	// Compare stable actor-facing targets. Full Mover prediction contains the
	// mesh-facing offset and evolves during the turn; comparing it directly to
	// input-facing incorrectly treats one held target as several new requests.
	if (LastTurnInPlaceRequestFrame != 0
		&& GFrameCounter - LastTurnInPlaceRequestFrame <= 2
		&& !TurnInPlaceReselectFacingDirection.IsNearlyZero())
	{
		return TurnInPlaceReselectFacingDirection;
	}
	if (CharacterProperties.bUseExternalFacingDirection
		&& !CharacterProperties.FacingDirection.IsNearlyZero())
	{
		return CharacterProperties.FacingDirection.GetSafeNormal2D();
	}
	if (bUsingLightweightTrajectory)
	{
		return Trj_FutureFacing.Vector().GetSafeNormal2D();
	}
	const FQuat MeshFacingOffset = GetProxyOnAnyThread<FAnimInstanceProxy>()
		.GetComponentRelativeTransform().GetRotation();
	return (Trj_FutureFacing.Quaternion() * MeshFacingOffset.Inverse())
		.GetForwardVector().GetSafeNormal2D();
}

bool UHMS_AnimInstance::HasNewTurnInPlaceTarget() const
{
	if (bRestartTurnInPlaceFromBeginningOnNextSelection
		|| CachedChooserStance != Stance || CachedChooserRotationMode != RotationMode
		|| CachedChooserWeaponType != UHMS_AnimationQueryLibrary::GetEquippedWeaponType(this)
		|| CachedChooserBehaviorState != UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(this))
	{
		return true;
	}
	const FVector Target = GetTurnInPlaceTargetDirection();
	return !TurnInPlaceFacingOnSelection.IsNearlyZero() && !Target.IsNearlyZero()
		&& FVector::DotProduct(TurnInPlaceFacingOnSelection, Target)
			< FMath::Cos(FMath::DegreesToRadians(20.0f));
}

bool UHMS_AnimInstance::ShouldReselectTurnInPlace()
{
	if (!ShouldTurnInPlace()) { return false; }
	return !IsTurnInPlaceSelectionActive() || HasNewTurnInPlaceTarget()
		|| BlendStackInputs.Anim->GetPlayLength() - GetSelectedAnimationTime() <= 0.15f;
}

bool UHMS_AnimInstance::IsPivoting_Implementation() const
{
	// The instantaneous speed/trajectory test necessarily becomes false while a
	// reversal decelerates through zero.  Keep the semantic state true until the
	// selected pivot has had enough time to establish its pose; otherwise the raw
	// GASP LocomotionTransition -> Loop rule can cut it off at the zero crossing.
	if (IsPivotSelectionLocked())
	{
		return true;
	}

	// The stock 5.8 Pivot self-transition calls IsPivoting. During the zero-speed
	// phase of an active Pivot, the normal gait speed window can be false even when
	// the player has requested the opposite direction. Preserve that deliberate
	// interrupt so the self-transition can re-run the Chooser immediately.
	if (HasOpposingPivotRequest())
	{
		return true;
	}

	if (MovementState != EHMS_MovementState::Moving)
	{
		return false;
	}

	// UE 5.8 GASP 的 State Machine Pivot 条件：只判断已经转换到角色局部语义的
	// 轨迹转角。即时输入/速度夹角只负责及时修正轨迹，不能直接替代该条件，
	// 否则 Offset Root Bone 回正和角色减速期间会重复触发 Pivot。
	if (FMath::Abs(Trj_TurnAngle) < GetPivotTurnAngleThreshold())
	{
		return false;
	}

	// A confirmed reversal remains a Pivot while Mover brakes through the gait's
	// normal speed window. Dropping Run below 175 cm/s used to turn IsPivoting off
	// mid-query and let the same transition select a 180-degree Start/Reface clip.
	if (bImmediateDirectionChangePivot)
	{
		return true;
	}

	// 与样例 SM Pivot Conditions 的速度窗口逐项一致。
	if (Stance.HasTagExact(TAG_HMS_Stance_Crouch))
	{
		return FMath::IsWithinInclusive(Speed2D, 50.0f, 200.0f);
	}

	switch (Gait)
	{
	case EHMS_Gait::Walk:
		return FMath::IsWithinInclusive(Speed2D, 50.0f, 600.0f);
	case EHMS_Gait::Run:
		return FMath::IsWithinInclusive(Speed2D, 175.0f, 600.0f);
	case EHMS_Gait::Sprint:
		return FMath::IsWithinInclusive(Speed2D, 200.0f, 700.0f);
	default:
		return false;
	}
}

bool UHMS_AnimInstance::IsPivotSelectionActive() const
{
	const UAnimationAsset* PivotAnimation = CompletionTrackedAnimation.Get();
	if (!IsValid(PivotAnimation)
		|| PivotAnimation != BlendStackInputs.Anim
		|| !BlendStackInputs.Tags.Contains(FName(TEXT("Pivot")))
		|| BlendStackInputs.bLoop)
	{
		return false;
	}

	const float CurrentAssetTime = GetSelectedAnimationTime();
	const float RemainingTime = FMath::Max(PivotAnimation->GetPlayLength() - CurrentAssetTime, 0.0f);
	constexpr float PivotEndTolerance = 0.05f;
	return RemainingTime > PivotEndTolerance;
}

bool UHMS_AnimInstance::IsPivotSelectionLocked() const
{
	// This is deliberately only the pose-establishment window. Keeping it true for
	// the entire clip made IsPivoting() hold the transition state until animation
	// completion, delaying the turn-to-loop handoff. Same-state duplicate queries
	// are independently suppressed by IsPivotSelectionActive().
	return IsPivotSelectionActive()
		&& CompletionTrackedElapsedSeconds < FMath::Min(FMath::Max(PivotSelectionLockDuration, 0.0f),
			FMath::Max(static_cast<float>(BlendStackInputs.BlendTime), 0.0f) + 0.05f);
}

bool UHMS_AnimInstance::HasOpposingPivotRequest() const
{
	return IsPivotSelectionActive() && HasNewLocomotionTarget();
}

bool UHMS_AnimInstance::HasNewLocomotionTarget() const
{
	if (!bHasPivotFacingOnSelection
		|| (!IsPivotSelectionActive() && !IsStartSelectionActive())
		|| IsPivotSelectionLocked() || IsStartSelectionLocked())
	{
		return false;
	}

	// Compare against the selected world-space request, not against velocity.
	// A second turn made while braking through zero must survive until unlock.
	const FVector CurrentPivotDirection = GetSelectedWorldInputDirection();
	if (CurrentPivotDirection.IsNearlyZero())
	{
		return false;
	}

	const float FacingDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
		PivotFacingOnSelection.Yaw,
		CurrentPivotDirection.ToOrientationRotator().Yaw));
	return FacingDelta >= 60.0f;
}

bool UHMS_AnimInstance::HasLocomotionContextChanged() const
{
	return CompletionTrackedAnimation.IsValid() && IsMoving()
		&& (CachedChooserMovementDirection != MovementDirection
			|| CachedChooserStance != Stance || CachedChooserGait != Gait
			|| CachedChooserRotationMode != RotationMode);
}

float UHMS_AnimInstance::GetSelectedAnimationTime() const
{
	FBlendStackAnimNodeReference Node;
	bool bValid = false;
	UBlendStackAnimNodeLibrary::ConvertToBlendStackNodePure(GetStateMachineBlendStackNodeReference(), Node, bValid);
	if (bValid && BlendStackInputs.Anim && UBlendStackAnimNodeLibrary::GetCurrentAsset(Node) == BlendStackInputs.Anim)
	{
		return UBlendStackAnimNodeLibrary::GetCurrentAssetTime(Node);
	}
	return FMath::Max(static_cast<float>(BlendStackInputs.StartTime) + CompletionTrackedElapsedSeconds, 0.0f);
}

bool UHMS_AnimInstance::IsStartSelectionActive() const
{
	const UAnimationAsset* StartAnimation = CompletionTrackedAnimation.Get();
	if (!IsValid(StartAnimation)
		|| StartAnimation != BlendStackInputs.Anim
		|| CompletionTrackedState != StateMachineState
		|| !BlendStackInputs.Tags.Contains(FName(TEXT("Start")))
		|| BlendStackInputs.bLoop)
	{
		return false;
	}

	const float CurrentAssetTime = GetSelectedAnimationTime();
	const float RemainingTime = FMath::Max(StartAnimation->GetPlayLength() - CurrentAssetTime, 0.0f);
	return RemainingTime > 0.05f;
}

bool UHMS_AnimInstance::IsStartSelectionLocked() const
{
	return IsStartSelectionActive()
		&& CompletionTrackedElapsedSeconds < FMath::Min(FMath::Max(StartSelectionLockDuration, 0.0f),
			FMath::Max(static_cast<float>(BlendStackInputs.BlendTime), 0.0f) + 0.05f);
}

bool UHMS_AnimInstance::ShouldTransitionToLocomotionLoop(float StateTime)
{
	const bool bIsPivotingNow = IsPivoting();
	const bool bPivotSelectionLocked = IsPivotSelectionLocked();
	// 不使用上一状态遗留的 bLoop 将阈值降到 0。状态机必须先真正进入起步状态，
	// 完成 Chooser 查询并得到非循环动画，才允许走这条兜底过渡。
	const bool bHasValidStartAnimation = IsValid(BlendStackInputs.Anim) && !BlendStackInputs.bLoop;
	const bool bTimeValid = StateTime > 0.5f;
	const bool bNotCircling = !Trj_IsCircling;
	const bool bIsOnGround = MovementMode_Recent == EHMS_MovementMode::OnGround;

	const bool bMoving = IsMoving();
	const bool bResult = bHasValidStartAnimation && !bIsPivotingNow && !bPivotSelectionLocked && bTimeValid &&
		bNotCircling && bIsOnGround && bMoving;
	return bResult;
}

bool UHMS_AnimInstance::ShouldBreakRotationAnimation(float StateTime, float EarlyStateTime)
{
	if (IsPivotSelectionLocked() || IsStartSelectionLocked())
	{
		return false;
	}

	const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(
		Trj_FutureFacing,
		FutureFacingOnTransitionStart
	);

	const double DeltaYaw = FMath::Abs(DeltaRot.Yaw);
	const bool bAngleExceeded = DeltaYaw > 60.0;
	//const float StateTime = GetInstanceCurrentStateElapsedTime(MachineIndex);
	const bool bIsEarlyState = StateTime < FMath::Max(EarlyStateTime, 0.0f);
	const bool bIsNotLooping = !BlendStackInputs.bLoop;
	const bool bNotCircling = !Trj_IsCircling;

	const bool bResult = bAngleExceeded && bIsEarlyState && bIsNotLooping && bNotCircling;
	return bResult;
}

bool UHMS_AnimInstance::ShouldTriggerSharpTurnTransition(int32 MachineIndex)
{
	if (IsPivotSelectionLocked() || IsStartSelectionLocked())
	{
		return false;
	}

	const double RawDelta = FutureFacingDelta - FutureFacingDelta_LastFrame;
	// Crossing the -180/+180 representation boundary produces a raw delta close
	// to 360 degrees even when the physical facing changed by only a few degrees.
	// The legacy Rotation Flipped graph used that discontinuity as its signal. In
	// the Mover path it may oscillate at the boundary for hundreds of frames, so a
	// re-entry is valid only when the trajectory also reports a real sharp turn.
	const bool bCrossedAngleWrap = FMath::Abs(RawDelta) > 200.0;
	const bool bHasMeaningfulTrajectoryTurn =
		FMath::Abs(Trj_TurnAngle) >= GetPivotTurnAngleThreshold()
		|| bImmediateDirectionChangePivot
		|| bSharpDirectionReversal;
	const bool bSharpTurn = bCrossedAngleWrap && bHasMeaningfulTrajectoryTurn;
	const float StateTime = GetInstanceCurrentStateElapsedTime(MachineIndex);
	const bool bTimeValid = StateTime > 0.1f;

	const bool bResult = bSharpTurn && bTimeValid;
	return bResult;
}

bool UHMS_AnimInstance::ShouldExitStartPivotByCircling(int32 MachineIndex)
{
	if (IsStartSelectionLocked() || IsPivotSelectionLocked()
		|| CompletionTrackedElapsedSeconds < 0.5f)
	{
		return false;
	}
	const bool bCirclingLongEnough = Trj_CirclingTime > 0.3f;
	const bool bIsStart = BlendStackInputs.Tags.Contains(FName(TEXT("Start")));
	const bool bIsPivot = BlendStackInputs.Tags.Contains(FName(TEXT("Pivot")));

	// A stationary reface briefly curves as it accelerates. Let its selected
	// turn play; a new input direction can still use the dedicated reselect rule.
	const bool bResult = bCirclingLongEnough && !IsStartSelectionActive() && (bIsStart || bIsPivot);
	(void)MachineIndex;
	return bResult;
}

FVector UHMS_AnimInstance::ResolveNavMoverInputAcceleration()
{
	if (!IsValid(CachedNavMoverComponent))
	{
		return FVector::ZeroVector;
	}

	FVector MoveInputIntent = FVector::ZeroVector;
	FVector MoveInputVelocity = FVector::ZeroVector;
	CachedNavMoverComponent->ConsumeNavMovementData(MoveInputIntent, MoveInputVelocity);

	if (!MoveInputIntent.IsNearlyZero(0.001f))
	{
		return MoveInputIntent.GetSafeNormal2D();
	}

	if (!MoveInputVelocity.IsNearlyZero(0.001f))
	{
		return MoveInputVelocity.GetSafeNormal2D();
	}

	return FVector::ZeroVector;
}

void UHMS_AnimInstance::Update_MovementDirectionData()
{
	MovementDirection = CalculateMovementDirectionFromVectors(SelectedInputAcceleration);
}
