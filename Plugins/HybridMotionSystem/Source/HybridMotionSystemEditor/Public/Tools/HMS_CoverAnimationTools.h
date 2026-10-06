#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "HMS_CoverAnimationTools.generated.h"
class UChooserTable;
class UPoseSearchDatabase;
class UUserDefinedEnum;
class UHMS_CoverAnimationProfile;
class USmartObjectDefinition;
class UAnimBlueprint;

/** Author ordinary, editable Chooser tables. Operations are undoable and never save automatically. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_CoverAnimationTools : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Upgrade the HMS cover AimOffset route to run after its full-body montage slot. Undoable; does not save. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
 static bool RouteCoverAimAfterMontage(UAnimBlueprint* Blueprint, FName SlotName = TEXT("DefaultSlot"));
 /** Bake sampled capsule-relative weapon yaw/pitch over normalized peek time. Undoable; does not save. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
 static bool ConfigurePeekAimReference(UHMS_CoverAnimationProfile* Profile,
  const TArray<float>& Times, const TArray<FVector2D>& Angles);
 /** Set the user-tag requirement on one named slot; an invalid tag clears that requirement. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
 static bool ConfigureSlotRequiredTag(USmartObjectDefinition* Definition, FName SlotName, FGameplayTag RequiredTag);
 /** Rebuild a leaf with stance and claimed-slot filters. Behavior/equipment parent routers stay unchanged. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
 static bool ConfigureEntrySideChooser(UChooserTable* Table, FGameplayTag Stance,
  const TArray<FGameplayTag>& SlotTags, const TArray<UPoseSearchDatabase*>& Databases);
 /** Rebuild a leaf with Blueprint-owned CoverState / CoverPeekState and equipment filters.
  * One concealed/peek pair per profile; preserves the table's fallback and context/output structures. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
 static bool ConfigureCoverIdleChooser(UChooserTable* Table, UUserDefinedEnum* CoverEnum, UUserDefinedEnum* PeekEnum,
  const TArray<uint8>& CoverStates, const TArray<UHMS_CoverAnimationProfile*>& Profiles);
};
