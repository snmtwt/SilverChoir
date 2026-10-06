#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_AnimationQueryTools.generated.h"

class UAnimBlueprint;
class UChooserTable;

/** Author ordinary Chooser assets. Helpers support Undo and never save automatically. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_AnimationQueryTools : public UBlueprintFunctionLibrary
{

	GENERATED_BODY()
public:
	/** Inspect the selected behavior/equipment routing path for a live AnimInstance, stopping at the action/database table.
	 * Uses the actual Chooser name-column filter. Does not evaluate animations or modify assets. Empty means invalid routing. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation Query")
	static TArray<UChooserTable*> TraceAnimationQueryRoute(UChooserTable* Root, UObject* ContextObject);
	/** Add Blueprint-owned BehaviorState and migrate WeaponPose to EquippedWeaponType, retaining graph links/defaults. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation Query")
	static bool InstallAnimationQueryVariables(UAnimBlueprint* Blueprint);

	/** Replace an owned root with exact GameplayTag-name routes. Child signatures must match and must not form cycles.
	 * A null Fallback is an intentional unsupported route. Caller must preserve the old action table before replacing it. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation Query")
	static bool ConfigureAnimationTagRouter(UChooserTable* Root, FName Property,
		const TArray<FGameplayTag>& Values, const TArray<UChooserTable*>& Children, UChooserTable* Fallback);

	/** Migrate name-column property bindings in this asset's owned tables; never edits external tables. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation Query")
	static int32 RenameQueryTagBinding(UChooserTable* Root, FName OldName, FName NewName);

	/** Replace child table references in this asset only, including owned nested tables. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Animation Query")
	static bool ReplaceQueryChild(UChooserTable* Root, UChooserTable* OldChild, UChooserTable* NewChild);
};
