#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_InteractionStateTools.generated.h"

class UAnimBlueprint;
class UAnimSequence;
class USkeleton;
class UChooserTable;
class UUserDefinedEnum;
class UEdGraph;
class UK2Node_CallFunction;

/** Editor authoring helpers for Blueprint-owned interaction states. Changes support Undo; save explicitly. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_InteractionStateTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Add a call by local function name or native function path, without the editor action-menu cache. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UK2Node_CallFunction* AddFunctionCall(UAnimBlueprint* Blueprint, UEdGraph* Graph, FName FunctionName);

	/** Add a logical state to an HMS Blend Stack state machine. Existing states are never replaced.
	 * Predicate and animation callback functions must already exist in the animation Blueprint. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static FString AddLogicalInteractionState(UAnimBlueprint* Blueprint, FName MachineName, FName StateName,
		const TArray<FName>& EntryStates, FName MovingState, FName IdleState, FName EnterPredicate,
		FName ExitMovingPredicate, FName ExitIdlePredicate, FName EntryCallback, FName UpdateCallback);

	/** Create an enum-filtered idle table and add its state route to a tag-only root Chooser.
	 * Enum value zero means no interaction; one row is reserved for each remaining visible enumerator.
	 * Null animations are explicit unconfigured rows. Existing packages/routes are never replaced. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UChooserTable* CreateInteractionIdleChooser(UChooserTable* RootChooser, const FString& PackagePath,
		FGameplayTag StateTag, FName EnumProperty, UUserDefinedEnum* StateEnum,
		const TArray<UAnimSequence*>& IdleAnimations, float BlendTime = 0.3f);

	UFUNCTION(BlueprintPure, Category="HMS|Editor|Interaction")
	static FString DescribeInteractionChooser(UChooserTable* Chooser);

	/** Replace animation results in this table and update matching Pose Match rows together.
	 * Preserves filters, outputs and sampling settings; refreshes search data. Does not recurse or save.
	 * Returns changed row count, or -1 for invalid/mismatched inputs. Supports Undo. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation")
	static int32 ReplaceChooserAnimations(UChooserTable* Chooser,
		const TArray<UAnimSequence*>& SourceAnimations, const TArray<UAnimSequence*>& ReplacementAnimations);

	/** Update animation references in search databases owned by this Chooser's root.
	 * Keeps sampling, mirroring and search settings. Includes legacy recovery databases.
	 * Returns changed entry count, or -1 for invalid input; supports Undo and does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation")
	static int32 ReplaceChooserDatabaseAnimations(UChooserTable* Chooser,
		const TArray<UAnimSequence*>& SourceAnimations, const TArray<UAnimSequence*>& ReplacementAnimations);

	/** Set an optional playback entry limit on matching HMS output rows; -1 disables the limit.
	 * Preserves pose sampling, database references and all other rows. Returns changed cell count, or -1 on invalid input. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation")
	static int32 SetChooserAnimationStartTimeLimit(UChooserTable* Chooser, UAnimSequence* Animation,
		float MaximumStartTime);

	/** Copy a missing named blend profile from a source skeleton, mapping entries by bone name.
	 * Existing target profiles are preserved. Missing target bones are skipped.
	 * Returns copied entry count, 0 if already present, or -1 for invalid input.
	 * Supports Undo and marks the target skeleton dirty; does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation")
	static int32 CopyMissingSkeletonBlendProfile(USkeleton* TargetSkeleton, USkeleton* SourceSkeleton,
		FName ProfileName);
};
