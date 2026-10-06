#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "SmartObject/HMS_SmartObjectInteractionProfile.h"
#include "SmartObject/HMS_EntryRootMotionModifier.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "MotionWarpingComponent.h"
#include "MoveLibrary/PlayMoverMontageCallbackProxy.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "RootMotionModifier_SkewWarp.h"
#include "SmartObjectSubsystem.h"

bool UHMS_SmartObjectInteractionComponent::IsBusy() const
{
	return InteractionState == EHMS_SmartObjectInteractionState::Searching
		|| InteractionState == EHMS_SmartObjectInteractionState::Claimed
		|| InteractionState == EHMS_SmartObjectInteractionState::Approaching
		|| InteractionState == EHMS_SmartObjectInteractionState::Aligning
		|| InteractionState == EHMS_SmartObjectInteractionState::Interacting
		|| InteractionState == EHMS_SmartObjectInteractionState::Holding;
}

bool UHMS_SmartObjectInteractionComponent::IsPlayingProfileEntry() const
{
	return ActiveProfile && (InteractionState == EHMS_SmartObjectInteractionState::Interacting
		|| InteractionState == EHMS_SmartObjectInteractionState::Holding);
}

bool UHMS_SmartObjectInteractionComponent::IsAligningProfileEntry() const
{
	return ActiveProfile && InteractionState == EHMS_SmartObjectInteractionState::Aligning;
}

bool UHMS_SmartObjectInteractionComponent::FindAndUseSmartObjectWithProfile(
	UHMS_SmartObjectInteractionProfile* Profile)
{
	if (IsBusy())
	{
		return false;
	}
	bLastInteractionSucceeded = false;
	LastFailureReason = FText::GetEmpty();
	APawn* Pawn = Cast<APawn>(ResolveUserActor());
	USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	USkeletalMeshComponent* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!IsValid(Profile) || !IsValid(Profile->EntryMontage)
		|| !Pawn || !Pawn->GetController() || !Subsystem || !Mesh || !Mesh->GetAnimInstance()
		|| !Pawn->FindComponentByClass<UMoverComponent>()
		|| !Pawn->FindComponentByClass<UMotionWarpingComponent>())
	{
		FailAndScheduleRetry(TEXT("交互配置、动画、Mover 或控制器尚未就绪"));
		return false;
	}
	if (Pawn->FindComponentByClass<UMoverComponent>()->IsBackendAsync())
	{
		FailAndScheduleRetry(TEXT("固定进入交互目前需要同步 Mover 后端"));
		return false;
	}
	if (!Profile->EntryMontage->GetSkeleton()
		|| !Profile->EntryMontage->GetSkeleton()->IsCompatibleMesh(Mesh->GetSkeletalMeshAsset()))
	{
		FailAndScheduleRetry(TEXT("交互动画与角色骨架不兼容"));
		return false;
	}
	if (!Profile->EntryMontage->HasRootMotion() || Profile->EntryStartTime < 0.0f
		|| Profile->WarpEndTime <= Profile->EntryStartTime
		|| Profile->WarpEndTime > Profile->EntryMontage->GetPlayLength()
		|| Profile->WarpTargetName.IsNone())
	{
		FailAndScheduleRetry(TEXT("请检查进入动画的根运动和对齐时间范围"));
		return false;
	}
	ActiveProfile = Profile;
	SetInteractionState(EHMS_SmartObjectInteractionState::Searching);
	if (!ClaimBestCandidate(*Subsystem, *Pawn))
	{
		FailAndScheduleRetry(TEXT("附近没有符合配置且空闲的智能对象"));
		return false;
	}
	SetInteractionState(EHMS_SmartObjectInteractionState::Claimed);
	if (!BeginProfileApproach())
	{
		FailAndScheduleRetry(TEXT("无法找到通往交互入口的完整导航路径"));
		return false;
	}
	return true;
}

bool UHMS_SmartObjectInteractionComponent::BeginProfileApproach()
{
	APawn* Pawn = Cast<APawn>(ResolveUserActor());
	FTransform SlotTransform;
	if (!Pawn || !Pawn->GetController() || !GetClaimedSlotTransform(SlotTransform))
	{
		return false;
	}
	ProfileTargetTransform = ActiveProfile->TargetOffset * SlotTransform;
	const FVector Entrance = ProfileTargetTransform.TransformPositionNoScale(
		FVector(-ActiveProfile->ApproachDistance, 0.0f, 0.0f));
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Navigation || !Navigation->ProjectPointToNavigation(
		Entrance, Projected, FVector(100.0f, 100.0f, 200.0f))
		|| FVector::Dist2D(Entrance, Projected.Location) > ActiveProfile->MaximumNavigationProjection)
	{
		return false;
	}
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
		GetWorld(), Pawn->GetNavAgentLocation(), Projected.Location, Pawn);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return false;
	}
	ProfileApproachLocation = Projected.Location;
	ProfileElapsedTime = 0.0f;
	bEntryCompleted = false;
	bEntryInterrupted = false;
	SetInteractionState(EHMS_SmartObjectInteractionState::Approaching);
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(Pawn->GetController(), ProfileApproachLocation);
	if (UPathFollowingComponent* Following = Pawn->GetController()->FindComponentByClass<UPathFollowingComponent>())
	{
		Following->SetAcceptanceRadius(5.0f);
	}
	return true;
}

void UHMS_SmartObjectInteractionComponent::TickProfileInteraction(const float DeltaTime)
{
	APawn* Pawn = Cast<APawn>(ResolveUserActor());
	USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	if (!Pawn || !Pawn->GetController() || !ClaimedSmartObjectActor.IsValid()
		|| !Subsystem || !Subsystem->IsClaimedSmartObjectValid(ClaimedHandle))
	{
		FailAndScheduleRetry(TEXT("交互目标或控制器已失效"));
		return;
	}
	ProfileElapsedTime += DeltaTime;
	if (InteractionState == EHMS_SmartObjectInteractionState::Approaching)
	{
		const FVector Feet = Pawn->GetNavAgentLocation();
		if (FVector::Dist2D(Feet, ProfileApproachLocation) <= ActiveProfile->ApproachTolerance
			&& FMath::Abs(Feet.Z - ProfileApproachLocation.Z) <= 100.0f)
		{
			Pawn->GetController()->StopMovement();
			ProfileElapsedTime = 0.0f;
			SetInteractionState(EHMS_SmartObjectInteractionState::Aligning);
			return;
		}
		const UPathFollowingComponent* Following = Pawn->GetController()->FindComponentByClass<UPathFollowingComponent>();
		if (ProfileElapsedTime > ActiveProfile->ApproachTimeout
			|| (ProfileElapsedTime > 0.5f && (!Following || Following->GetStatus() == EPathFollowingStatus::Idle)))
		{
			FailAndScheduleRetry(TEXT("无法抵达交互入口，可换个位置重试"));
		}
		return;
	}
	if (IsAligningProfileEntry())
	{
		const float FacingError = FMath::Abs(FMath::FindDeltaAngleDegrees(
			Pawn->GetActorRotation().Yaw, ProfileTargetTransform.Rotator().Yaw));
		if (FacingError <= 0.1f && Pawn->GetVelocity().Size2D() < 1.0f)
		{
			if (!PlayProfileEntry())
			{
				FailAndScheduleRetry(TEXT("进入动画未能开始播放"));
			}
		}
		else if (ProfileElapsedTime > 3.0f)
		{
			FailAndScheduleRetry(TEXT("角色未能面向交互目标，请检查 Mover 朝向输入"));
		}
		return;
	}
	if (IsPlayingProfileEntry())
	{
		if (bEntryInterrupted)
		{
			FailAndScheduleRetry(TEXT("进入动画被其他动作打断"));
		}
		else if (bEntryCompleted)
		{
			if (FVector::Dist2D(Pawn->GetNavAgentLocation(), ProfileTargetTransform.GetLocation()) > 15.0f)
			{
				FailAndScheduleRetry(TEXT("进入动画已结束，但目标对齐被阻挡"));
			}
			else
			{
				FinishInteraction(true);
			}
		}
		else if (ProfileElapsedTime > ActiveProfile->EntryMontage->GetPlayLength() + 2.0f)
		{
			FailAndScheduleRetry(TEXT("进入动画播放超时"));
		}
	}
}

bool UHMS_SmartObjectInteractionComponent::PlayProfileEntry()
{
	APawn* Pawn = Cast<APawn>(ResolveUserActor());
	USkeletalMeshComponent* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	UMoverComponent* Mover = Pawn ? Pawn->FindComponentByClass<UMoverComponent>() : nullptr;
	UMotionWarpingComponent* Warping = Pawn ? Pawn->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	USmartObjectSubsystem* Subsystem = ResolveSubsystem();
	UAnimMontage* Montage = bQueriedEntry ? QueriedMontage.Get() : ActiveProfile->EntryMontage.Get();
	const float StartTime = bQueriedEntry ? SelectedEntryTime : ActiveProfile->EntryStartTime;
	const float EndTime = bQueriedEntry ? QueriedWarpEnd : ActiveProfile->WarpEndTime;
	const float PlayRate = bQueriedEntry ? SelectedEntryPlayRate : 1.f;
	if (!Montage || !Mesh || !Mesh->GetAnimInstance() || !Mover || !Warping || !Subsystem
		|| (!bQueriedEntry && !Subsystem->MarkSlotAsOccupied<UGameplayInteractionSmartObjectBehaviorDefinition>(ClaimedHandle)))
	{
		return false;
	}
	if (!bQueriedEntry) { Pawn->GetController()->StopMovement(); }
	else
	{
		// The selected source window is implemented by our explicit modifier below.
		// Avoid applying both the source SkewWarp and the entry modifier to the same movement.
		bPreviousWindowSearch = Warping->bSearchForWindowsInAnimsWithinMontages;
		bRestoreWindowSearch = true;
		Warping->bSearchForWindowsInAnimsWithinMontages = false;
	}
	Warping->AddOrUpdateWarpTargetFromTransform(ActiveProfile->WarpTargetName, ProfileTargetTransform);
	UHMS_EntryRootMotionModifier* Modifier = NewObject<UHMS_EntryRootMotionModifier>(Warping);
	Modifier->Animation = Montage;
	Modifier->StartTime = StartTime;
	Modifier->EndTime = EndTime;
	Modifier->WorldDisplacement = ProfileTargetTransform.GetLocation() - Pawn->GetNavAgentLocation();
	Modifier->WorldDisplacement.Z = 0.0f;
	Modifier->InitialMeshRotation = (Mover->GetBaseVisualComponentTransform() * Pawn->GetActorTransform()).GetRotation();
 if (bQueriedEntry)
 {
  const float Turn=UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage,StartTime,EndTime).Rotator().Yaw;
  Modifier->RotationCorrectionDegrees=FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw+Turn,ProfileTargetTransform.Rotator().Yaw);
 }
 if(bQueriedEntry)
 {
  const float DT=FMath::Min(1.f/60.f,EndTime-StartTime);
  const FVector AuthoredVelocity=Modifier->InitialMeshRotation.RotateVector(UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage,StartTime,StartTime+DT).GetTranslation()/DT);
  // The modifier integrates in animation seconds, while velocity is in world seconds.
  Modifier->InitialVelocityCorrection=Pawn->GetVelocity()/PlayRate-AuthoredVelocity;
  Modifier->InitialVelocityCorrection.Z=0;
 }
	Warping->AddModifier(Modifier);
	EntryWarpModifier = Modifier;
	if (ActiveProfile->bIgnoreObjectCollisionDuringEntry)
	{
		EntryCollisionComponent = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
		if (UPrimitiveComponent* Collision = EntryCollisionComponent.Get())
		{
			bAddedObjectCollisionIgnore = !Collision->GetMoveIgnoreActors().Contains(ClaimedSmartObjectActor.Get());
			Collision->IgnoreActorWhenMoving(ClaimedSmartObjectActor.Get(), true);
		}
	}
	EntryPlaybackProxy = UPlayMoverMontageCallbackProxy::CreateProxyObjectForPlayMoverMontage(
		Mover, Montage, PlayRate, StartTime, NAME_None, true, -1.0f);
	if (!EntryPlaybackProxy || !Mesh->GetAnimInstance()->Montage_IsPlaying(Montage))
	{
		return false;
	}
	SelectedEntryBlendTime = Montage->GetDefaultBlendInTime();
	if (Pawn->GetVelocity().SizeSquared2D() > FMath::Square(10.f) && ActiveProfile->MovingEntryBlendTime > 0.f)
	{
		if (FAnimMontageInstance* Instance = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(Montage))
		{
			const float SecondsUntilContact = (EndTime - StartTime)
				/ FMath::Max(FMath::Abs(PlayRate * Montage->RateScale), UE_KINDA_SMALL_NUMBER);
			SelectedEntryBlendTime = FMath::Min(
				FMath::Max(SelectedEntryBlendTime, FMath::Clamp(ActiveProfile->MovingEntryBlendTime, 0.f, 2.f)),
				FMath::Max(0.f, SecondsUntilContact - 1.f / 30.f));
			FMontageBlendSettings BlendSettings;
			BlendSettings.Blend = Montage->GetBlendInArgs();
			BlendSettings.Blend.BlendTime = SelectedEntryBlendTime;
			BlendSettings.BlendMode = Montage->BlendModeIn;
			BlendSettings.BlendProfile = Montage->BlendProfileIn;
			// The proxy just created this instance; no animation tick has run yet.
			// Reconfigure its blend without replaying, rewinding or queuing a second root-motion move.
			Instance->Play(PlayRate, BlendSettings);
		}
	}
	EntryPlaybackProxy->OnCompleted.AddDynamic(this, &UHMS_SmartObjectInteractionComponent::HandleEntryCompleted);
	EntryPlaybackProxy->OnInterrupted.AddDynamic(this, &UHMS_SmartObjectInteractionComponent::HandleEntryInterrupted);
	ProfileElapsedTime = 0.0f;
	SetInteractionState(EHMS_SmartObjectInteractionState::Interacting);
	return true;
}

void UHMS_SmartObjectInteractionComponent::HandleEntryCompleted(FName NotifyName)
{
	bEntryCompleted = true;
}

void UHMS_SmartObjectInteractionComponent::HandleEntryInterrupted(FName NotifyName)
{
	bEntryInterrupted = true;
}

void UHMS_SmartObjectInteractionComponent::CleanupProfileInteraction()
{
	if (!ActiveProfile)
	{
		return;
	}
	UAnimMontage* Montage = bQueriedEntry ? QueriedMontage.Get() : ActiveProfile->EntryMontage.Get();
	if (EntryPlaybackProxy)
	{
		EntryPlaybackProxy->OnCompleted.RemoveDynamic(this, &UHMS_SmartObjectInteractionComponent::HandleEntryCompleted);
		EntryPlaybackProxy->OnInterrupted.RemoveDynamic(this, &UHMS_SmartObjectInteractionComponent::HandleEntryInterrupted);
	}
	if (EntryWarpModifier)
	{
		EntryWarpModifier->SetState(ERootMotionModifierState::Disabled);
		EntryWarpModifier = nullptr;
	}
	if (APawn* Pawn = Cast<APawn>(ResolveUserActor()))
	{
		if (Pawn->GetController())
		{
			Pawn->GetController()->StopMovement();
		}
		if (USkeletalMeshComponent* Mesh = Pawn->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance(); Anim && ((EntryPlaybackProxy && !bEntryCompleted) || bEntryAnimationReleased))
			{
				if (!Anim->GetCurrentActiveMontage() || Anim->GetCurrentActiveMontage() == Montage)
				{
					if (UMoverComponent* Mover = Pawn->FindComponentByClass<UMoverComponent>())
					{
						Mover->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage, false);
					}
				}
				if (Montage) { Anim->Montage_Stop(0.2f, Montage); }
			}
		}
		if (UMotionWarpingComponent* Warping = Pawn->FindComponentByClass<UMotionWarpingComponent>())
		{
			Warping->RemoveWarpTarget(ActiveProfile->WarpTargetName);
			if (bRestoreWindowSearch) { Warping->bSearchForWindowsInAnimsWithinMontages = bPreviousWindowSearch; }
		}
	}
	if (bAddedObjectCollisionIgnore && EntryCollisionComponent.IsValid() && ClaimedSmartObjectActor.IsValid())
	{
		EntryCollisionComponent->IgnoreActorWhenMoving(ClaimedSmartObjectActor.Get(), false);
	}
	bAddedObjectCollisionIgnore = false;
	EntryCollisionComponent.Reset();
	EntryPlaybackProxy = nullptr;
	bRestoreWindowSearch = false;
	QueriedMontage = nullptr;
	bEntryAnimationReleased = false;
	HoldStartFrame = 0;
	HoldWaitElapsed = 0.0f;
	if (bRestoreStandingAfterHold)
	{
		if (AActor* User = ResolveUserActor())
		{
			if (auto* Mover = User->FindComponentByClass<UCharacterMoverComponent>()) { Mover->UnCrouch(); }
		}
	}
	bRestoreStandingAfterHold = false;
	ActiveProfile = nullptr;
}
