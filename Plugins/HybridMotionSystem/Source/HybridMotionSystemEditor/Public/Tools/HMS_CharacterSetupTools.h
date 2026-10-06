#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_CharacterSetupTools.generated.h"
class UAnimBlueprint;
class USkeletalMesh;
class UPhysicsAsset;

UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_CharacterSetupTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Inspect node tags, references and bindings without changing the asset. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static FString InspectAnimationBlueprint(UAnimBlueprint* Blueprint);
	/** Describe state-machine transitions and their connected rule expressions. Read-only. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static FString InspectStateMachineRules(UAnimBlueprint* Blueprint);
	/** Repair migrated GASP idle aliases and route the circling exit through HMS. Does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static FString RepairLocomotionTransitions(UAnimBlueprint* Blueprint);
	/** Connect HMS blend-space, root rotation and orientation-warping inputs and repair node references. Does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static bool ConnectLocomotionInputs(UAnimBlueprint* Blueprint);
	/** Gate unbound Transform Bone corrections during physics/get-up. Existing alpha bindings are preserved. Does not save. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static int32 ConnectRagdollPoseCorrectionInputs(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static FString InspectRagdollRig(USkeletalMesh* Mesh);
	/** Creates a separate physics asset for a standard humanoid bone map, fitted to the supplied reference pose. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Character")
	static UPhysicsAsset* CreateHumanoidRagdollRig(USkeletalMesh* Mesh, const FString& PackagePath);
};
