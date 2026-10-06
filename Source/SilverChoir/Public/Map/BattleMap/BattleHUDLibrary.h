#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "GameplayTagContainer.h"
#include "BattleHUDLibrary.generated.h"
class USIS_UnitInventoryComponent;

UCLASS()
class SILVERCHOIR_API UBattleHUDLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="战斗UI|算法") static float SanitizeRatio(float Value);
    UFUNCTION(BlueprintPure, Category="战斗UI|算法") static FBattleDockLayout CalculateDockLayout(float Width, int32 Members, int32 SelectedIndex, EBattleDrawer Drawer, float Expansion, float MemberDrawerWidth = 180.f, float VehicleDrawerWidth = 290.f);
    UFUNCTION(BlueprintPure, Category="战斗UI|装备") static bool AreHandsLinked(FGuid LeftItemId, FGuid RightItemId);
    /** Query SIS occupancy rather than guessing from weapon type. Slot tags are authored in Blueprint. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|装备") static bool ReadHandEquipment(USIS_UnitInventoryComponent* Inventory, FGameplayTag LeftSlot, FGameplayTag RightSlot, FGuid& LeftItemId, FGuid& RightItemId, bool& bLinked);
};
