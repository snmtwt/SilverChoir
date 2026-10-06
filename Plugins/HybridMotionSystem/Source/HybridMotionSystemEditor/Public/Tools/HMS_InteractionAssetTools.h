#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_InteractionAssetTools.generated.h"
class UAnimMontage;
class UChooserTable;
class UStateTree;
class UPoseSearchDatabase;
class UHMS_SmartObjectInteractionProfile;
class UUserDefinedEnum;

/** Creates editable entry assets. Existing packages are never overwritten; saving is left to the caller. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_InteractionAssetTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Creates an editable Blueprint enum with caller-supplied states; never overwrites an existing asset. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UUserDefinedEnum* CreateInteractionStateEnum(const FString& PackagePath, const TArray<FText>& StateNames);
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
 static FString DescribeEntrySamples(UPoseSearchDatabase* Database);
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
 static bool AddEntryMontage(UPoseSearchDatabase* Database, UAnimMontage* Montage);
	/** Re-read source entry markers, restrict indexed start times, and rebuild the database. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static bool RefreshEntryDatabase(UPoseSearchDatabase* Database);
	/** Set the searchable part of one montage without changing its animation or notifies. Times must remain inside HMS.EntryWarp. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static bool SetEntryQueryRange(UPoseSearchDatabase* Database, UAnimMontage* Montage, float SearchStart, float SearchEnd);
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UChooserTable* CreateEntryQueryAssets(UAnimMontage* Montage, const FString& Folder);
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UStateTree* CreateEntryStateTree(UHMS_SmartObjectInteractionProfile* Profile, const FString& PackagePath);
};
