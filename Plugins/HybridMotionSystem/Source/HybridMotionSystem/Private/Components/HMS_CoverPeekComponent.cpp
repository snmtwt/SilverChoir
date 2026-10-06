#include "Components/HMS_CoverPeekComponent.h"
#include "Components/HMS_AnimationDataComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/HMS_WeaponAnimationLibrary.h"
#include "Animation/HMS_AnimationQueryLibrary.h"
#include "Animation/HMS_CoverAnimationProfile.h"
#include "Animation/HMS_WeaponAimProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "MoverComponent.h"
#include "MoveLibrary/PlayMoverMontageCallbackProxy.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"

UHMS_CoverPeekComponent::UHMS_CoverPeekComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	RequiredWeaponPose = FGameplayTag::RequestGameplayTag(TEXT("HMS.Weapon.Rifle"), false);
	const FGameplayTag Side = FGameplayTag::RequestGameplayTag(TEXT("HMS.Cover.RightStanding"), false);
	if (Side.IsValid()) { SupportedSlotTags.AddTag(Side); }
}

APawn* UHMS_CoverPeekComponent::ResolvePawn() const
{
	if (auto* Pawn = Cast<APawn>(GetOwner())) { return Pawn; }
	const auto* Controller = Cast<AController>(GetOwner());
	return Controller ? Controller->GetPawn() : nullptr;
}

bool UHMS_CoverPeekComponent::MatchesProfile(const UHMS_CoverAnimationProfile* Profile) const
{
	const APawn* Pawn = ResolvePawn();
	const auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	return Profile && Interaction.IsValid() && Profile->SlotTag.IsValid()
		&& Interaction->GetClaimedSlotTags().HasTagExact(Profile->SlotTag)
		&& Profile->RequiredWeaponType.IsValid()
		&& UHMS_AnimationQueryLibrary::GetEquippedWeaponType(Anim).MatchesTag(Profile->RequiredWeaponType)
		&& (!Profile->RequiredBehaviorState.IsValid()
			|| UHMS_AnimationQueryLibrary::GetAnimationBehaviorState(Anim).MatchesTag(Profile->RequiredBehaviorState));
}

UHMS_CoverAnimationProfile* UHMS_CoverPeekComponent::ResolveCoverProfile() const
{
	for (const auto& Profile : CoverProfiles) { if (MatchesProfile(Profile.Get())) { return Profile.Get(); } }
	return nullptr;
}

UHMS_CoverAnimationProfile* UHMS_CoverPeekComponent::GetCoverAnimationProfile() const
{
	if (!IsCoverLocked()) { return nullptr; }
	// Preserve the selected pair across reversal, retract and equipment changes.
	return PlayingProfile && (bPeeking || IsTransitioning()) ? PlayingProfile.Get() : ResolveCoverProfile();
}

UHMS_WeaponAimProfile* UHMS_CoverPeekComponent::GetCoverAimProfile() const
{
	const auto* Profile = GetCoverAnimationProfile();
	return Profile ? Profile->AimProfile.Get() : nullptr;
}

bool UHMS_CoverPeekComponent::SetAimTargetLocation(FVector WorldLocation)
{
	if (WorldLocation.ContainsNaN()) { return false; }
	AimTargetPoint = WorldLocation; bHasAimTargetPoint = true;
	bTrackAimActor = false; AimTargetActor.Reset(); return true;
}

bool UHMS_CoverPeekComponent::SetAimTargetActor(AActor* TargetActor, FVector LocalOffset)
{
	if (!IsValid(TargetActor) || TargetActor == ResolvePawn() || LocalOffset.ContainsNaN()) { return false; }
	AimTargetActor = TargetActor; TargetLocalOffset = LocalOffset;
	bTrackAimActor = true; bHasAimTargetPoint = false; return true;
}

void UHMS_CoverPeekComponent::ClearAimTarget()
{
	AimTargetActor.Reset(); bTrackAimActor = false; bHasAimTargetPoint = false;
}

bool UHMS_CoverPeekComponent::GetAimTargetLocation(FVector& WorldLocation) const
{
	WorldLocation = FVector::ZeroVector;
	if (bTrackAimActor)
	{
		if (!AimTargetActor.IsValid()) { return false; }
		WorldLocation = AimTargetActor->GetActorTransform().TransformPosition(TargetLocalOffset);
	}
	else if (bHasAimTargetPoint) { WorldLocation = AimTargetPoint; }
	else { return false; }
	return !WorldLocation.ContainsNaN();
}

FVector UHMS_CoverPeekComponent::GetAimOrigin() const
{
	const APawn* Pawn = ResolvePawn();
	const auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const auto* Profile = GetCoverAnimationProfile();
	const FName Socket = Profile ? Profile->AimOriginSocket : FName(TEXT("head"));
	if (Mesh && Mesh->DoesSocketExist(Socket))
	{
		return Mesh->GetSocketTransform(Socket).TransformPosition(Profile ? Profile->AimOriginOffset : FVector::ZeroVector);
	}
	FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector; FRotator Rotation;
	if (Pawn) { Pawn->GetActorEyesViewPoint(Location, Rotation); }
	return Location;
}

void UHMS_CoverPeekComponent::BeginPlay()
{
	Super::BeginPlay(); RefreshInteraction();
	if (const APawn* Pawn = ResolvePawn())
	{
		if (auto* Mesh = Pawn->FindComponentByClass<USkeletalMeshComponent>()) { Mesh->AddTickPrerequisiteComponent(this); }
	}
}

void UHMS_CoverPeekComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ResetPeek();
	if (Interaction.IsValid()) { Interaction->OnInteractionStateChanged.RemoveDynamic(this, &ThisClass::InteractionChanged); }
	Interaction.Reset(); Super::EndPlay(Reason);
}

void UHMS_CoverPeekComponent::RefreshInteraction()
{
	APawn* Pawn = ResolvePawn();
	auto* Found = Pawn ? Pawn->FindComponentByClass<UHMS_SmartObjectInteractionComponent>() : nullptr;
	if (!Found && Pawn && Pawn->GetController()) { Found = Pawn->GetController()->FindComponentByClass<UHMS_SmartObjectInteractionComponent>(); }
	if (Interaction.Get() == Found) { return; }
	if (Interaction.IsValid()) { Interaction->OnInteractionStateChanged.RemoveDynamic(this, &ThisClass::InteractionChanged); }
	ResetPeek(); Interaction = Found;
	if (Found)
	{
		Found->OnInteractionStateChanged.AddDynamic(this, &ThisClass::InteractionChanged);
		InteractionChanged(EHMS_SmartObjectInteractionState::Idle, Found->GetInteractionState());
	}
}

bool UHMS_CoverPeekComponent::IsCoverLocked() const
{
	const FGameplayTag Cover = FGameplayTag::RequestGameplayTag(TEXT("HMS.Cover"), false);
	return Interaction.IsValid() && Interaction->GetInteractionState() == EHMS_SmartObjectInteractionState::Holding
		&& Cover.IsValid() && Interaction->GetClaimedSlotTags().HasTag(Cover);
}

bool UHMS_CoverPeekComponent::CanPeek() const
{
	const APawn* Pawn = ResolvePawn();
	const auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const auto* Data = Pawn ? Pawn->FindComponentByClass<UHMS_AnimationDataComponent>() : nullptr;
	const auto* Profile = GetCoverAnimationProfile();
	const bool Configured = CoverProfiles.IsEmpty() ? EnterPeekMontage && ExitPeekMontage
		: Profile && MatchesProfile(Profile) && Profile->EnterPeekMontage && Profile->ExitPeekMontage;
	return IsCoverLocked() && Interaction->IsHoldingAnimationReady() && Mesh && Configured
		&& !(Data && (Data->IsRagdoll() || Data->IsGettingUpFromRagdoll()))
		&& (!CoverProfiles.IsEmpty() || (Interaction->GetClaimedSlotTags().HasAnyExact(SupportedSlotTags)
			&& RequiredWeaponPose.IsValid()
			&& UHMS_WeaponAnimationLibrary::GetAnimationWeaponPose(Mesh->GetAnimInstance()).MatchesTag(RequiredWeaponPose)));
}

float UHMS_CoverPeekComponent::GetTransitionPhase() const
{
	const auto* Pawn = ResolvePawn();
	const auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	// Montage_GetPosition only finds active instances. Auto blend-out removes that
	// entry before OnCompleted fires; query the exact instance throughout its tail.
	const auto* Instance = Anim ? Anim->GetMontageInstanceForID(PlayingMontageInstanceId) : nullptr;
	return Instance && PlayingMontage && Instance->Montage == PlayingMontage
		? FMath::Clamp(Instance->GetPosition() / FMath::Max(PlayingMontage->GetPlayLength(), UE_SMALL_NUMBER), 0.f, 1.f) : 1.f;
}

bool UHMS_CoverPeekComponent::IsAimOffsetActive() const
{
	if (!CanPeek()) { return false; }
	if (!IsTransitioning()) { return bPeeking; }
	const auto* Profile = GetCoverAnimationProfile();
	if (!Profile || !PlayingMontage) { return false; }
	if (!bPeeking)
	{
		// Keep the visual layer through the first part of the retract. The gameplay
		// peek flag is already false, so firing cannot remain enabled by this gate.
		const float End = FMath::IsFinite(Profile->RetractAimEndFraction)
			? FMath::Clamp(Profile->RetractAimEndFraction, 0.f, 1.f) : .65f;
		return Profile->bAimDuringRetract && GetTransitionPhase() < End;
	}
	const float Start = FMath::IsFinite(Profile->PeekAimStartFraction) ? FMath::Clamp(Profile->PeekAimStartFraction, 0.f, 1.f) : .2f;
	return Profile->bAimDuringPeek && GetTransitionPhase() >= Start;
}

bool UHMS_CoverPeekComponent::RequestPeek(bool bEnable)
{
	if (bEnable && !CanPeek()) { return false; }
	if (bPeeking == bEnable) { bHasQueuedPeek = false; return true; }
	if (!IsCoverLocked()) { ResetPeek(); return !bEnable; }
	APawn* Pawn = ResolvePawn();
	auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	auto* Mover = Pawn ? Pawn->FindComponentByClass<UMoverComponent>() : nullptr;
	UHMS_CoverAnimationProfile* Profile = PlayingProfile && (bPeeking || IsTransitioning()) ? PlayingProfile.Get() : ResolveCoverProfile();
	UAnimMontage* Enter = Profile ? Profile->EnterPeekMontage.Get() : EnterPeekMontage.Get();
	UAnimMontage* Exit = Profile ? Profile->ExitPeekMontage.Get() : ExitPeekMontage.Get();
	const bool ReversePair = !Profile || Profile->bExitReversesEnter;
	UAnimMontage* Next = bEnable ? Enter : Exit;
	if (!Anim || !Mover || Mover->IsBackendAsync() || !Next || !Enter || !Exit
		|| !Next->HasRootMotion() || !Mesh->GetSkeletalMeshAsset() || Next->GetSkeleton() != Mesh->GetSkeletalMeshAsset()->GetSkeleton()
		|| (ReversePair && !FMath::IsNearlyEqual(Enter->GetPlayLength(), Exit->GetPlayLength(), .001f))) { return false; }
	if (IsTransitioning() && !ReversePair) { bHasQueuedPeek = true; bQueuedPeek = bEnable; return true; }
	// Mirrored time on the paired reverse clip allows a second button press to
	// retract halfway through the turn without snapping to either endpoint.
	float Start = 0.f;
	if (PlayingMontage && IsTransitioning())
	{ Start = FMath::Clamp(Next->GetPlayLength() * (1.f - GetTransitionPhase()), 0.f, Next->GetPlayLength() - .001f); }
	StopTransition();
	PlayingProfile = Profile; bHasQueuedPeek = false;
	PlayingMontage = Next;
	PlaybackProxy = UPlayMoverMontageCallbackProxy::CreateProxyObjectForPlayMoverMontage(Mover, Next, 1.f, Start, NAME_None, true, -1.f);
	if (!PlaybackProxy || !Anim->Montage_IsPlaying(Next)) { PlayingMontage = nullptr; PlaybackProxy = nullptr; return false; }
	if (const auto* Instance = Anim->GetActiveInstanceForMontage(Next)) { PlayingMontageInstanceId = Instance->GetInstanceID(); }
	PlaybackProxy->OnCompleted.AddDynamic(this, &ThisClass::TransitionCompleted);
	PlaybackProxy->OnInterrupted.AddDynamic(this, &ThisClass::TransitionInterrupted);
	bPeeking = bEnable; OnPeekChanged.Broadcast(bPeeking);
	return true;
}

void UHMS_CoverPeekComponent::StopTransition()
{
	if (PlaybackProxy)
	{
		PlaybackProxy->OnCompleted.RemoveDynamic(this, &ThisClass::TransitionCompleted);
		PlaybackProxy->OnInterrupted.RemoveDynamic(this, &ThisClass::TransitionInterrupted);
	}
	APawn* Pawn = ResolvePawn(); auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (PlayingMontage && Anim && Anim->GetMontageInstanceForID(PlayingMontageInstanceId))
	{
		Anim->Montage_Stop(.12f, PlayingMontage);
		if (auto* Mover = Pawn->FindComponentByClass<UMoverComponent>()) { Mover->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage, false); }
	}
	PlaybackProxy = nullptr; PlayingMontage = nullptr; PlayingMontageInstanceId = INDEX_NONE;
}

void UHMS_CoverPeekComponent::ResetPeek()
{
	StopTransition(); SmoothedAimOffset = FVector2D::ZeroVector;
	bHasSmoothedWorldAim = false;
	PlayingProfile = nullptr; bHasQueuedPeek = false;
	if (bPeeking) { bPeeking = false; OnPeekChanged.Broadcast(false); }
}

void UHMS_CoverPeekComponent::TransitionCompleted(FName NotifyName)
{
	PlaybackProxy = nullptr; PlayingMontage = nullptr; PlayingMontageInstanceId = INDEX_NONE;
	if (const APawn* Pawn = ResolvePawn()) { LockedFacing = Pawn->GetActorForwardVector().GetSafeNormal2D(); }
	// Keep the accumulated aim when handing the montage to PeekIdle or reversing.
	if (bHasQueuedPeek)
	{
		const bool Next = bQueuedPeek; bHasQueuedPeek = false; RequestPeek(Next);
	}
}

void UHMS_CoverPeekComponent::TransitionInterrupted(FName NotifyName)
{
	// An unrelated montage took control. Release the cover claim rather than
	// retaining a half-turned pose as a valid cover idle.
	PlaybackProxy = nullptr; PlayingMontage = nullptr; PlayingMontageInstanceId = INDEX_NONE;
	if (Interaction.IsValid() && IsCoverLocked()) { Interaction->AbortInteraction(); }
	ResetPeek();
}

void UHMS_CoverPeekComponent::InteractionChanged(EHMS_SmartObjectInteractionState Previous, EHMS_SmartObjectInteractionState Current)
{
	if (Current == EHMS_SmartObjectInteractionState::Holding && ResolvePawn())
	{
		PlayingProfile = ResolveCoverProfile();
		LockedFacing = ResolvePawn()->GetActorForwardVector().GetSafeNormal2D();
		SmoothedAimOffset = FVector2D::ZeroVector;
		bHasSmoothedWorldAim = false;
	}
	else
	{
		ResetPeek();
		if (Current == EHMS_SmartObjectInteractionState::Idle || Current == EHMS_SmartObjectInteractionState::Failed
			|| Current == EHMS_SmartObjectInteractionState::Cooldown) { ClearAimTarget(); }
	}
}

void UHMS_CoverPeekComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, TickType, Function); RefreshInteraction();
	const APawn* Pawn = ResolvePawn();
	const auto* Data = Pawn ? Pawn->FindComponentByClass<UHMS_AnimationDataComponent>() : nullptr;
	if (IsCoverLocked() && Data && (Data->IsRagdoll() || Data->IsGettingUpFromRagdoll()))
	{ Interaction->AbortInteraction(); ResetPeek(); return; }
	if (bPeeking && !CanPeek() && !RequestPeek(false))
	{ if (Interaction.IsValid()) { Interaction->AbortInteraction(); } ResetPeek(); }
	if (IsCoverLocked() && PlayingProfile && PlayingProfile->bLeaveCoverWhenProfileUnavailable
		&& !MatchesProfile(PlayingProfile) && !bPeeking && !IsTransitioning())
	{ Interaction->AbortInteraction(); ResetPeek(); return; }
	if (!IsCoverLocked()) { SmoothedAimOffset = FVector2D::ZeroVector; bHasSmoothedWorldAim = false; return; }
	if (IsTransitioning())
	{
		LockedFacing = Pawn->GetActorForwardVector().GetSafeNormal2D();
		// Root motion controls facing; aim continues to track independently.
	}
	if (bTrackAimActor && !AimTargetActor.IsValid()) { ClearAimTarget(); }
	FVector TargetLocation;
	FVector Aim = GetAimTargetLocation(TargetLocation) ? (TargetLocation - GetAimOrigin()).GetSafeNormal()
		: Data ? Data->GetResolvedAimingDirection() : LockedFacing;
	if (Aim.ContainsNaN() || Aim.IsNearlyZero()) { return; }
	ResolvedAimDirection = Aim;
	// Smooth target intent in world space, not the changing animation reference.
	// Otherwise a fast authored turn adds a second lag that persists after the peek.
	const float Alpha = FMath::IsFinite(AimSmoothingTime) && AimSmoothingTime > UE_SMALL_NUMBER
		? 1.f - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / AimSmoothingTime) : 1.f;
	const FRotator WorldAim = Aim.Rotation();
	if (!bHasSmoothedWorldAim) { SmoothedWorldAim = WorldAim; bHasSmoothedWorldAim = true; }
	else
	{
		SmoothedWorldAim.Yaw += FMath::FindDeltaAngleDegrees(SmoothedWorldAim.Yaw, WorldAim.Yaw) * Alpha;
		SmoothedWorldAim.Pitch += FMath::FindDeltaAngleDegrees(SmoothedWorldAim.Pitch, WorldAim.Pitch) * Alpha;
		SmoothedWorldAim.Normalize();
	}
	FRotator AimBasis = LockedFacing.Rotation();
	if (const auto* Profile = GetCoverAnimationProfile())
	{
		FRotator Reference = Profile->AimReferenceRotation;
		if (PlayingMontage && (bPeeking || Profile->bExitReversesEnter)
			&& Profile->PeekAimReferenceCurve.GetRichCurveConst(0)->GetNumKeys() > 0
			&& Profile->PeekAimReferenceCurve.GetRichCurveConst(1)->GetNumKeys() > 0)
		{
			float Phase = GetTransitionPhase();
			if (!bPeeking) { Phase = 1.f - Phase; }
			const FVector Sample = Profile->PeekAimReferenceCurve.GetValue(Phase);
			if (!Sample.ContainsNaN()) { Reference = FRotator(Sample.Y, Sample.X, 0.f); }
		}
		if (!Reference.ContainsNaN())
		{
			AimBasis.Yaw += Reference.Yaw;
			AimBasis.Pitch += Reference.Pitch;
		}
	}
	if (IsTransitioning() && !bPeeking)
	{
		// Retract from the current additive pose, carried by the authored turn.
		// Chasing the target behind the body can cross +/-180 and flip the yaw
		// clamp. Preserve the visible aim instead, then let the visual gate fade it.
		// Keep the world smoother on this visible direction so an interrupted
		// retract blends back toward the target without jumping the aim angles.
		SmoothedWorldAim = (AimBasis + FRotator(SmoothedAimOffset.Y, SmoothedAimOffset.X, 0.f)).GetNormalized();
		bHasSmoothedWorldAim = true;
		return;
	}
	const FRotator Delta = (SmoothedWorldAim - AimBasis).GetNormalized();
	const auto* AimProfile = GetCoverAimProfile();
	const float YawSetting = AimProfile ? AimProfile->MaxYaw : MaximumAimYaw;
	const float PitchSetting = AimProfile ? AimProfile->MaxPitch : MaximumAimPitch;
	const float YawLimit = FMath::IsFinite(YawSetting) ? FMath::Clamp(YawSetting, 0.f, 90.f) : 65.f;
	const float PitchLimit = FMath::IsFinite(PitchSetting) ? FMath::Clamp(PitchSetting, 0.f, 89.f) : 45.f;
	const FVector2D Target(FMath::Clamp(Delta.Yaw, -YawLimit, YawLimit), FMath::Clamp(Delta.Pitch, -PitchLimit, PitchLimit));
	// A target behind a turning body crosses the +/-180 seam. After clamping,
	// that can jump directly between opposite aim limits; bound the correction
	// so re-peeking from an interrupted retract stays continuous as well.
	const float CorrectionSpeed = FMath::IsFinite(MaximumAimOffsetSpeed) ? FMath::Max(1.f, MaximumAimOffsetSpeed) : 720.f;
	const double MaxStep = CorrectionSpeed * FMath::Max(DeltaTime, 0.f);
	SmoothedAimOffset.X += FMath::Clamp(Target.X - SmoothedAimOffset.X, -MaxStep, MaxStep);
	SmoothedAimOffset.Y += FMath::Clamp(Target.Y - SmoothedAimOffset.Y, -MaxStep, MaxStep);
}
