#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "SmartObject/HMS_SmartObjectInteractionProfile.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AnimNotifyState_MotionWarping.h"
#include "RootMotionModifier.h"
#include "ChooserFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "MotionWarpingComponent.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "SmartObjectSubsystem.h"
#include "HMS_AnimInstance.h"
#include "PlayMontageCallbackProxy.h"

namespace
{
	bool MoveTo(APawn& Pawn, const FVector& Target, float MaximumProjection)
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn.GetWorld());
		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Target, Projected, FVector(100,100,200))
			|| FVector::Dist2D(Target, Projected.Location) > MaximumProjection) { return false; }
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(Pawn.GetWorld(), Pawn.GetNavAgentLocation(), Projected.Location, &Pawn);
		if (!Path || !Path->IsValid() || Path->IsPartial()) { return false; }
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(Pawn.GetController(), Projected.Location);
		if (auto* Following = Pawn.GetController()->FindComponentByClass<UPathFollowingComponent>()) { Following->SetAcceptanceRadius(5); }
		return true;
	}
	// This first reusable entry task supports a single, unscaled source sequence per montage.
	// Validate this instead of silently applying sequence times to an unrelated montage segment.
	bool FindEntryWindow(const UAnimMontage& Montage, FName Target, float Time, float& End)
	{
		if (Montage.SlotAnimTracks.Num()!=1 || Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1) { return false; }
		const FAnimSegment& Segment = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		if (!FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime)
			|| !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f) || Segment.LoopingCount!=1) { return false; }
		const UAnimSequenceBase* Source = Segment.GetAnimReference();
		if (!Source) { return false; }
		for (const FAnimNotifyEvent& Event : Source->Notifies)
		{
			const auto* Notify = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
			const auto* Warp = Notify ? Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier) : nullptr;
			if (Warp && Warp->WarpTargetName == Target && Time >= Event.GetTime()
				&& Time < Event.GetTime()+Event.GetDuration())
			{
				End = Event.GetTime()+Event.GetDuration();
				return End <= Montage.GetPlayLength();
			}
		}
		return false;
	}
}

bool UHMS_SmartObjectInteractionComponent::BeginQueriedEntry(UHMS_SmartObjectInteractionProfile* Profile)
{
	auto Fail = [this](const TCHAR* Reason) { LastFailureReason=FText::FromString(Reason); return false; };
	APawn* Pawn = Cast<APawn>(ResolveUserActor());
	auto* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	auto* Mover = Pawn ? Pawn->FindComponentByClass<UMoverComponent>() : nullptr;
	if (!Profile || !Profile->EntryDatabaseChooser || !Pawn || !Pawn->GetController() || !Mesh || !Mesh->GetAnimInstance()
		|| !Mover || !Pawn->FindComponentByClass<UMotionWarpingComponent>() || ActiveProfile)
	{ return Fail(TEXT("状态树进入配置、角色动画或 Mover 尚未就绪")); }
	if (Mover->IsBackendAsync()) { return Fail(TEXT("当前进入任务需要同步 Mover 后端")); }
	if (Profile->HoldAnimationState.IsValid() && (!Profile->bHoldFinalPose || !Cast<UHMS_AnimInstance>(Mesh->GetAnimInstance())))
	{ return Fail(TEXT("动画状态机待机需要启用保持交互占用，并使用 HMS 动画蓝图")); }
	if (Profile->bCrouchOnHold && (!Profile->bHoldFinalPose || !Cast<UCharacterMoverComponent>(Mover)
		|| !CastChecked<UCharacterMoverComponent>(Mover)->CanCrouch()))
	{ return Fail(TEXT("蹲伏贴靠需要支持蹲伏的 Character Mover，并启用保持交互占用")); }
	FTransform Slot;
	if (!GetClaimedSlotTransform(Slot)) { return Fail(TEXT("状态树进入任务没有有效槽位")); }
	ActiveProfile=Profile;
	bQueriedEntry=true;
	QueriedMontage=nullptr;
	QueriedElapsed=0;
	bEntryCompleted=bEntryInterrupted=false;
	bEntryAnimationReleased=false;
	HoldStartFrame=0;
	HoldWaitElapsed=0.f;
	EntryQueryCount=0;
 bUsedForcedEntry=false;
	SelectedEntryTime=SelectedEntryCost=EntryHandoffSpeed=0;
	SelectedEntryPlayRate=1.f;
	SelectedEntryBlendTime=0.f;
	ProfileTargetTransform=Profile->TargetOffset*Slot;
	// Query the current pose before issuing any navigation request.
	bRouteComplete=false;
 bEntryNavigationStarted=false;
	SetInteractionState(EHMS_SmartObjectInteractionState::Approaching);
	return true;
}

EStateTreeRunStatus UHMS_SmartObjectInteractionComponent::TickQueriedEntry(float DeltaTime)
{
	auto Fail=[this](const TCHAR* Reason) { LastFailureReason=FText::FromString(Reason); return EStateTreeRunStatus::Failed; };
	APawn* Pawn=Cast<APawn>(ResolveUserActor());
	if (!bQueriedEntry || !ActiveProfile || !Pawn || !Pawn->GetController() || !ResolveSubsystem()
		|| !ResolveSubsystem()->IsClaimedSmartObjectValid(ClaimedHandle)) { return Fail(TEXT("交互目标、占用或角色已失效")); }
	QueriedElapsed+=DeltaTime;
	auto IsFacingTarget=[&]()
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw,
			ProfileTargetTransform.Rotator().Yaw))<=ActiveProfile->MaximumEntryFacingError;
	};
	if (IsPlayingProfileEntry())
	{
		if (bEntryInterrupted) { return Fail(TEXT("进入动画被打断")); }
		USkeletalMeshComponent* PlayingMesh=Pawn->FindComponentByClass<USkeletalMeshComponent>();
		UAnimInstance* PlayingAnim=PlayingMesh ? PlayingMesh->GetAnimInstance() : nullptr;
		if (InteractionState==EHMS_SmartObjectInteractionState::Holding)
		{
			if (!PlayingAnim) { return Fail(TEXT("交互待机动画实例已失效")); }
			if (ActiveProfile->bCrouchOnHold)
			{
				if (auto* CharacterMover = Pawn->FindComponentByClass<UCharacterMoverComponent>(); CharacterMover && !CharacterMover->GetCrouchIntent())
				{ CharacterMover->Crouch(); }
			}
			if (!ActiveProfile->HoldAnimationState.IsValid())
			{
				return QueriedMontage && PlayingAnim->Montage_IsActive(QueriedMontage)
					? EStateTreeRunStatus::Running : Fail(TEXT("保持动画已失效"));
			}
			const auto* HMSAnim=Cast<UHMS_AnimInstance>(PlayingAnim);
			if (bEntryAnimationReleased)
			{
				// Blueprint pose changes must not silently leave an old pose on screen when
				// the replacement Chooser row is missing, non-looping or incompatible.
				const bool bIdleValid=HMSAnim && HMSAnim->HasEvaluatedBlendStackState(ActiveProfile->HoldAnimationState,HoldStartFrame+1);
				HoldWaitElapsed=bIdleValid ? 0.f : HoldWaitElapsed+FMath::Max(DeltaTime,0.f);
				return HoldWaitElapsed<FMath::Clamp(ActiveProfile->HoldHandoffTimeout,0.25f,10.f)
					? EStateTreeRunStatus::Running : Fail(TEXT("交互待机已失效：请检查当前姿态的 Chooser 动画、循环设置与骨架"));
			}
			HoldWaitElapsed+=FMath::Max(DeltaTime,0.f);
			if (HMSAnim && HMSAnim->HasEvaluatedBlendStackState(ActiveProfile->HoldAnimationState,HoldStartFrame+1))
			{
				// This is a deliberate animation ownership transfer, not an interrupted interaction.
				// Detach callbacks before stopping; keep the profile, StateTree and claim alive.
				if (EntryPlaybackProxy)
				{
					EntryPlaybackProxy->OnCompleted.RemoveDynamic(this,&UHMS_SmartObjectInteractionComponent::HandleEntryCompleted);
					EntryPlaybackProxy->OnInterrupted.RemoveDynamic(this,&UHMS_SmartObjectInteractionComponent::HandleEntryInterrupted);
				}
				bEntryAnimationReleased=true;
				bEntryCompleted=true;
				HoldWaitElapsed=0.f;
				PlayingAnim->Montage_Stop(FMath::Clamp(ActiveProfile->HoldBlendOutTime,0.05f,2.f),QueriedMontage);
				EntryPlaybackProxy=nullptr;
				return EStateTreeRunStatus::Running;
			}
			if (HoldWaitElapsed>=FMath::Clamp(ActiveProfile->HoldHandoffTimeout,0.25f,10.f))
			{ return Fail(TEXT("待机状态未接管：请检查状态机入口、Chooser 状态/姿态行、循环动画及 DefaultSlot 的 Always Update Source Pose")); }
			return EStateTreeRunStatus::Running;
		}
		if (!PlayingAnim || !QueriedMontage || (!bEntryCompleted && !PlayingAnim->Montage_IsActive(QueriedMontage)))
		{ return Fail(TEXT("进入或保持动画已失效")); }
		if (!bEntryCompleted && ActiveProfile->bHoldFinalPose && PlayingAnim->Montage_GetPosition(QueriedMontage)>=SelectedEntryHoldTime)
		{
			if (FVector::Dist2D(Pawn->GetNavAgentLocation(),ProfileTargetTransform.GetLocation())>15)
			{ return Fail(TEXT("进入结束，但角色未对齐接触位置")); }
			if (!IsFacingTarget()) { return Fail(TEXT("进入结束，但角色未对齐交互朝向")); }
			PlayingAnim->Montage_SetPosition(QueriedMontage,SelectedEntryHoldTime);
			PlayingAnim->Montage_Pause(QueriedMontage);
			// The root-motion layer has its own clock. Stop it explicitly while retaining the skeletal pose.
			Pawn->FindComponentByClass<UMoverComponent>()->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage,false);
			HoldStartFrame=GFrameCounter;
			HoldWaitElapsed=0.f;
			SetInteractionState(EHMS_SmartObjectInteractionState::Holding);
			return EStateTreeRunStatus::Running;
		}
		if (bEntryCompleted)
		{
			if (!IsFacingTarget()) { return Fail(TEXT("进入结束，但角色未对齐交互朝向")); }
			return FVector::Dist2D(Pawn->GetNavAgentLocation(),ProfileTargetTransform.GetLocation())<=15
				? EStateTreeRunStatus::Succeeded : Fail(TEXT("进入结束，但角色未对齐接触位置"));
		}
		if (QueriedElapsed>ActiveProfile->ApproachTimeout+10) { return Fail(TEXT("进入动画超时")); }
		return EStateTreeRunStatus::Running;
	}
	if (QueriedElapsed>ActiveProfile->ApproachTimeout) { return Fail(TEXT("没有找到可连续接入的姿态，请换一个接近位置")); }
	const FVector Feet=Pawn->GetNavAgentLocation();
	const float Distance=FVector::Dist2D(Feet,ProfileTargetTransform.GetLocation());
 auto* Following=Pawn->GetController()->FindComponentByClass<UPathFollowingComponent>();
	auto* Mesh=Pawn->FindComponentByClass<USkeletalMeshComponent>();
	UAnimInstance* Anim=Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !UPoseSearchLibrary::FindPoseHistoryNode(ActiveProfile->PoseHistoryName,Anim))
	{ return Fail(TEXT("动画蓝图缺少匹配的 Pose History 节点")); }
	FHMS_SmartObjectEntryContext EntryContext;
	EntryContext.SlotTags=GetClaimedSlotTags();
	FChooserEvaluationContext ChooserContext;
	ChooserContext.AddObjectParam(Anim);
	ChooserContext.AddStructParam(EntryContext);
	TArray<UObject*> Databases=UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(ChooserContext,
		UChooserFunctionLibrary::MakeEvaluateChooser(ActiveProfile->EntryDatabaseChooser),UPoseSearchDatabase::StaticClass());
	// The StateTree owns entry selection. Never navigate indefinitely or ignore
	// equipment/behavior filters when the configured route has no database.
	Databases.RemoveAll([](const UObject* Object) { return !IsValid(Object) || !Object->IsA<UPoseSearchDatabase>(); });
	if (Databases.IsEmpty()) { return Fail(TEXT("当前行为状态、装备武器类型或身体姿态没有配置进入动画数据库，请检查智能对象的进入查询表")); }
 auto ContinueApproach=[&]()
 {
  // Navigation approaches the interaction target, never a sampled animation start.
  if (!bEntryNavigationStarted)
  {
   if (!MoveTo(*Pawn,ProfileTargetTransform.GetLocation(),ActiveProfile->MaximumNavigationProjection))
    { return Fail(TEXT("没有通往交互目标的完整路径")); }
   bEntryNavigationStarted=true;
  }
  else if (Following && Following->GetStatus()==EPathFollowingStatus::Idle)
   { return Fail(TEXT("已抵达导航终点，但没有有效进入动画；请检查查询窗口或开启近距离强制进入")); }
  return EStateTreeRunStatus::Running;
 };
 const float QueryRadius=ActiveProfile->bAllowForcedEntry ? FMath::Max(ActiveProfile->MaximumEntryDistance,ActiveProfile->MaximumForcedEntryDistance) : ActiveProfile->MaximumEntryDistance;
 if (Distance>QueryRadius) { return ContinueApproach(); }
	FPoseSearchBlueprintResult Result;
	FPoseSearchContinuingProperties Continuing;
	Continuing.InterruptMode=EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose;
	UPoseSearchLibrary::MotionMatch(Anim,Databases,ActiveProfile->PoseHistoryName,Continuing,FPoseSearchFutureProperties(),Result);
	++EntryQueryCount;
 auto ValidEntry=[&](UAnimMontage* Clip,float Time,float& Contact)
 {
  return Clip && Clip->HasRootMotion() && Clip->GetSkeleton()
   && Clip->GetSkeleton()->IsCompatibleMesh(Mesh->GetSkeletalMeshAsset())
   && FMath::IsFinite(Time) && FindEntryWindow(*Clip,ActiveProfile->WarpTargetName,Time,Contact)
   && Contact-Time>=FMath::Max(0.2f,Clip->BlendIn.GetBlendTime()+1.f/30.f)
   && (!ActiveProfile->bHoldFinalPose || ((ActiveProfile->bHoldAtEntryContact ? Contact : ActiveProfile->HoldPoseTime)>=Contact
    && (ActiveProfile->bHoldAtEntryContact ? Contact : ActiveProfile->HoldPoseTime)<Clip->GetPlayLength() && !Clip->bEnableAutoBlendOut));
 };
 auto* Montage=Cast<UAnimMontage>(Result.SelectedAnim);
 float WarpEnd=0;
 bool bMatched=!Result.bIsMirrored && FMath::IsFinite(Result.SearchCost)
  && Result.SearchCost<=ActiveProfile->MaximumPoseCost && ValidEntry(Montage,Result.SelectedTime,WarpEnd);
 if(bMatched)
 {
  const float Travel=UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage,Result.SelectedTime,WarpEnd).GetTranslation().Size2D();
  const float ScaleError=FMath::Clamp(ActiveProfile->MaximumWarpScaleError,0.01f,0.25f);
  bMatched=Travel>=5 && Distance>=Travel*(1-ScaleError) && Distance<=Travel*(1+ScaleError);
 }
 const bool bCanForce=ActiveProfile->bAllowForcedEntry && Distance<=ActiveProfile->MaximumForcedEntryDistance;
 const bool bCloseEntry=bCanForce && ActiveProfile->CloseEntryDistance>0 && Distance<=ActiveProfile->CloseEntryDistance;
 if((!bMatched || bCloseEntry) && bCanForce)
 {
  // Nearby, an already aligned pawn should blend into the contact pose instead of
  // repeating the approach turn. Search the remaining authored warp window too;
  // the ordinary database range intentionally excludes these nearly stationary poses.
  // Keep skeleton, window, blend-in and holding validation even for forced entry.
  const FQuat FinalMeshRotation=(Pawn->FindComponentByClass<UMoverComponent>()->GetBaseVisualComponentTransform()*ProfileTargetTransform).GetRotation();
  const FTransform FinalMesh(FinalMeshRotation,ProfileTargetTransform.GetLocation());
  float Best=MAX_flt;
  const bool bPreserveFacing=bCloseEntry && FMath::Abs(FMath::FindDeltaAngleDegrees(
   Pawn->GetActorRotation().Yaw,ProfileTargetTransform.Rotator().Yaw))<=45.f;
  bool bBestPreservesFacing=false;
  for(UObject* Object:Databases)
  {
   const auto* Database=Cast<UPoseSearchDatabase>(Object); if(!Database) { continue; }
   for(int32 I=0;const auto* Entry=Database->GetDatabaseAnimationAsset(I);++I)
   {
    auto* Clip=Cast<UAnimMontage>(Entry->AnimAsset);
    if(!Clip || !Entry->IsEnabled() || Entry->GetMirrorOption()==EPoseSearchMirrorOption::MirroredOnly) { continue; }
    const float LastSample=bCloseEntry ? Clip->GetPlayLength() : Entry->SamplingRange.Max;
    for(int32 Frame=FMath::CeilToInt(FMath::Max(0.f,Entry->SamplingRange.Min)*30.f);Frame/30.f<=LastSample;++Frame)
    {
     const float T=Frame/30.f;
     float Contact=0; if(!ValidEntry(Clip,T,Contact)) { continue; }
     const FTransform Remaining=UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Clip,T,Contact);
     const float Travel=Remaining.GetTranslation().Size2D(); if(!bCloseEntry && Travel<5) { continue; }
     const FVector AuthoredPoint=(Remaining.Inverse()*FinalMesh).GetLocation();
     const float Denominator=FMath::Max(Distance,30.f);
     const float Heading=FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw,
      ProfileTargetTransform.Rotator().Yaw-Remaining.Rotator().Yaw));
     const bool bPreservesFacing=bPreserveFacing && Heading<=ActiveProfile->MaximumEntryFacingError;
     const float Score=FVector::Dist2D(AuthoredPoint,Feet)/Denominator
      +3.f*FMath::Abs(Travel-Distance)/Denominator+(bCloseEntry ? 3.f : 0.15f)*Heading/180.f;
     if((bPreservesFacing && !bBestPreservesFacing)
      || (bPreservesFacing==bBestPreservesFacing && Score<Best))
     {
      bBestPreservesFacing=bPreservesFacing;
      Best=Score; Montage=Clip; Result.SelectedTime=T; WarpEnd=Contact;
      Result.SearchCost=-1.f; bMatched=true; bUsedForcedEntry=true;
     }
    }
   }
  }
 }
 if(!bMatched) { return ContinueApproach(); }
	QueriedMontage=Montage;
	QueriedWarpEnd=WarpEnd;
	SelectedEntryTime=Result.SelectedTime;
	SelectedEntryHoldTime=ActiveProfile->bHoldAtEntryContact ? WarpEnd : ActiveProfile->HoldPoseTime;
	if (bCloseEntry && bUsedForcedEntry)
	{
		SelectedEntryPlayRate=FMath::Clamp((WarpEnd-SelectedEntryTime)
			/FMath::Clamp(ActiveProfile->MinimumCloseEntryDuration,0.2f,2.f),0.1f,1.f);
	}
	SelectedEntryCost=Result.SearchCost;
	EntryHandoffSpeed=Pawn->GetVelocity().Size2D();
	// Stop only navigation ownership; preserve velocity at the root-motion handoff.
	if (Following) Following->AbortMove(*this,FPathFollowingResultFlags::ForcedScript,FAIRequestID::CurrentRequest,EPathFollowingVelocityMode::Keep);
	if (!PlayProfileEntry()) { return Fail(TEXT("查询已成功，但进入动画未能开始")); }
	return EStateTreeRunStatus::Running;
}

void UHMS_SmartObjectInteractionComponent::EndQueriedEntry()
{
	CleanupProfileInteraction();
	bQueriedEntry=false;
}

