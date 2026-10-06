#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "HMS_WeaponAnimationTools.generated.h"
class UAnimBlueprint;
class UAnimSequence;
class UBlendSpace;
class USkeleton;
class UChooserTable;
class UAnimationAsset;
class UUserDefinedEnum;

/** Builds ordinary, editable Unreal animation nodes; runtime assets have no editor dependency. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_WeaponAnimationTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
	static bool ConfigureCoverPeekChooser(UChooserTable* Root, UChooserTable* Concealed,
		UAnimSequence* PeekIdle, UAnimSequence* ConcealedIdle, UUserDefinedEnum* PeekEnum, UUserDefinedEnum* CoverEnum);
	/** Add a dedicated cover Aim Offset after the weapon layer and before ragdoll/slots. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Cover")
	static bool InstallCoverPeekAimOffset(UAnimBlueprint* Blueprint, UBlendSpace* AimOffset);
	/** Replace animation references throughout an owned Chooser tree, including BlendSpaces and Pose Search rows/databases.
	 * External child assets are not modified. Preserves filters and outputs; supports Undo and does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static int32 RemapWeaponChooser(UChooserTable* Chooser, const TArray<UAnimationAsset*>& Sources,
		const TArray<UAnimationAsset*>& Replacements);

	/** Turn a root table into two mutually exclusive weapon routes. Duplicate its original contents first.
	 * The Blueprint-owned GameplayTag is read through its TagName field; no native weapon variable is added. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static bool ConfigureWeaponChooserRouter(UChooserTable* Root, UChooserTable* Unarmed,
		UChooserTable* Weapon, FGameplayTag WeaponTag, FName WeaponProperty = TEXT("EquippedWeaponType"));

	/** Use the full-body Chooser result as the ready pose, retaining the existing aimed hold and Aim Offset. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static bool UseFullBodyWeaponReadyPose(UAnimBlueprint* Blueprint);

	/** Create a speed blend space or a mesh-space Aim Offset. Refuses to overwrite existing assets; does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static UBlendSpace* CreateWeaponBlendSpace(const FString& PackagePath, USkeleton* Skeleton,
		const TArray<UAnimSequence*>& Animations, const TArray<FVector>& SamplePositions, bool AimOffset);
	/** Validate and rebuild triangulation after editing samples from an editor script. Does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static bool RefreshWeaponBlendSpace(UBlendSpace* BlendSpace);

	/** Inserts a Blueprint-owned EquippedWeaponType tag and an editable upper-body layer before the selected saved pose.
	 * Put it before ragdoll and full-body slots. Calling again leaves the existing layer intact. Does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Weapon")
	static FString InstallWeaponLayer(UAnimBlueprint* Blueprint, FName BeforeCachedPose,
		UBlendSpace* Ready, UBlendSpace* Aim, UBlendSpace* AimOffset, FName UpperBodyBone, FGameplayTag LayerPose);
};
