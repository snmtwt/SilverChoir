#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "HMS_SmartObjectInteractionProfile.generated.h"

class UAnimMontage;
class UChooserTable;

/** Context 1 for entry Choosers. Side is taken from the claimed slot, never guessed from the pawn's current facing. */
USTRUCT(BlueprintType)
struct HYBRIDMOTIONSYSTEM_API FHMS_SmartObjectEntryContext
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction") FGameplayTagContainer SlotTags;
};

/** Shared entry settings; a StateTree queries EntryDatabaseChooser, while the legacy API uses fixed animation fields. */
UCLASS(BlueprintType)
class HYBRIDMOTIONSYSTEM_API UHMS_SmartObjectInteractionProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Chooser receives the user's AnimInstance (context 0), including Blueprint-owned BehaviorState,
	 * EquippedWeaponType and Stance. Returns Pose Search databases; their entries determine montage and start time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry")
	TObjectPtr<UChooserTable> EntryDatabaseChooser;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry")
	FName PoseHistoryName = TEXT("PoseHistory");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry", meta = (ClampMin="0"))
	float MaximumPoseCost = 5.0f;
	/** Extend blend-in when handing over from movement. Zero uses the montage settings.
	 * Runtime clamps the blend to finish before contact, without changing the shared montage asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entry Transition", meta=(ClampMin="0", ClampMax="2", Units="s", DisplayName="移动进入混合时长"))
	float MovingEntryBlendTime = 0.4f;
 /** If pose matching fails nearby, choose a valid entry time by target geometry instead of walking to an animation start. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entry Fallback", meta=(DisplayName="允许近距离强制进入"))
 bool bAllowForcedEntry = false;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entry Fallback", meta=(EditCondition="bAllowForcedEntry", ClampMin="20", Units="cm", DisplayName="强制进入距离"))
 float MaximumForcedEntryDistance = 250.f;
 /** Inside this radius, also search the end of the warp window and favor the current facing.
  * This explicitly permits late starts beyond the ordinary Pose Search sampling range. Zero disables it. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entry Fallback", meta=(EditCondition="bAllowForcedEntry", ClampMin="0", Units="cm", DisplayName="近距离贴靠选段范围"))
 float CloseEntryDistance = 80.f;
 /** Slow short selected tails so endpoint correction has time to blend. Never speeds an animation up. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entry Fallback", meta=(EditCondition="bAllowForcedEntry", ClampMin="0.2", ClampMax="2", Units="s", DisplayName="最短贴靠时长"))
 float MinimumCloseEntryDuration = 0.45f;
	/** Maximum proportional change in authored entry travel. Lower values reduce handoff speed changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry", meta = (ClampMin="0.01", ClampMax="0.25"))
	float MaximumWarpScaleError = 0.25f;
	/** Only hand over to the queried animation inside this radius (cm). Author its searchable window accordingly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry", meta = (ClampMin="20", Units="cm"))
	float MaximumEntryDistance = 400.0f;
	/** Facing tolerance for aligned close-entry candidates and the final interaction transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matched Entry", meta = (ClampMin="1", ClampMax="30", Units="deg"))
	float MaximumEntryFacingError = 15.0f;
	/** Keep the claim until the user leaves. The property name is retained for asset compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Hold", meta=(DisplayName="保持交互占用"))
	bool bHoldFinalPose = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Hold", meta = (EditCondition="bHoldFinalPose", ClampMin="0", Units="s", DisplayName="进入待机时刻"))
	float HoldPoseTime = 3.1f;
	/** Resize through Character Mover after entry root motion has finished; restore prior standing intent on exit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction Hold", meta=(EditCondition="bHoldFinalPose", DisplayName="贴靠后保持蹲伏"))
	bool bCrouchOnHold = false;
	/** Use the selected clip's contact/warp end instead of a shared absolute animation time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction Hold", meta=(EditCondition="bHoldFinalPose", DisplayName="按所选动画接触时刻进入待机"))
	bool bHoldAtEntryContact = false;
	/** If set, release the entry montage only after the HMS Blend Stack evaluates this looping state.
	 * Empty preserves the generic frozen-pose interaction behavior for existing chairs/objects. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction Hold", meta=(EditCondition="bHoldFinalPose", Categories="SM.State", DisplayName="待机动画状态"))
	FGameplayTag HoldAnimationState;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction Hold", meta=(EditCondition="bHoldFinalPose", ClampMin="0.05", ClampMax="2", Units="s", DisplayName="进入到待机混合时长"))
	float HoldBlendOutTime = 0.35f;
	/** Missing/invalid/non-looping Chooser results fail cleanly instead of leaving a frozen montage forever. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction Hold", meta=(EditCondition="bHoldFinalPose", ClampMin="0.25", ClampMax="10", Units="s", DisplayName="待机接管超时"))
	float HoldHandoffTimeout = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Legacy Fixed Entry")
	TObjectPtr<UAnimMontage> EntryMontage;

	/** Montage time in seconds; preserves the authored approach/entry instead of looping it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Legacy Fixed Entry", meta = (ClampMin = "0"))
	float EntryStartTime = 0.0f;

	/** End of the authored displacement. The remaining pose may settle without further warping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Legacy Fixed Entry", meta = (ClampMin = "0"))
	float WarpEndTime = 1.0f;

	/** Root/capsule destination relative to the slot. X is the entry approach axis; the authored pose determines final body facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FTransform TargetOffset = FTransform::Identity;

	/** Navigation ends this far behind TargetOffset, in its local forward axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment", meta = (ClampMin = "0"))
	float ApproachDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment", meta = (ClampMin = "5"))
	float ApproachTolerance = 65.0f;

	/** Reject unreachable or excessively displaced projections rather than playing across an obstacle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment", meta = (ClampMin = "0"))
	float MaximumNavigationProjection = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FName WarpTargetName = TEXT("HMSInteraction");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "1"))
	float ApproachTimeout = 30.0f;

	/** Optional Actor tag to restrict this override to an explicitly designated test object. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	FName RequiredActorTag;

	/** Only the claimed object's movement collision is ignored, and restored after playback/abort. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bIgnoreObjectCollisionDuringEntry = true;
};
