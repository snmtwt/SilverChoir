#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_WeaponAimTools.generated.h"
class UAnimBlueprint;
class UHMS_WeaponAimProfile;

UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_WeaponAimTools : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Stabilize the authored free-aim grip after AimOffset interpolation. Preserves wrist rotation and finger curls.
  * Uses a standard Two Bone IK with the current elbow as its pole. Idempotent; does not save. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Aim")
 static FString InstallFreeAimGrip(UAnimBlueprint* Blueprint, FName LeftHand = TEXT("hand_l"), FName RightHand = TEXT("hand_r"));
 /** Upgrade the HMS weapon layer. Preserves the full-body Chooser, root motion, slots and ragdoll.
  * Adds Blueprint-owned profile variables and ordinary editable graph nodes. Does not save.
  * Repeated calls only update profile defaults. Refuses unknown destination routes. */
 UFUNCTION(BlueprintCallable, Category="HMS|Editor|Aim")
 static FString InstallAimProfiles(UAnimBlueprint* Blueprint, UHMS_WeaponAimProfile* FreeAim,
  UHMS_WeaponAimProfile* CoverAim, FName UpperBodyBone = TEXT("spine_03"));
};
