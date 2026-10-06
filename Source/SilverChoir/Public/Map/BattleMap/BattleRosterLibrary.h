#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "BattleRosterLibrary.generated.h"

/** Read-only presentation snapshots of the existing player squad and unit records. */
UCLASS()
class SILVERCHOIR_API UBattleRosterLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Preserves the first occurrence of each squad ID and its member order.
     * Empty input succeeds with an empty result. Missing/invalid squads, members or
     * assigned vehicles fail the whole request and leave OutSquads empty.
     *
     * Health, stamina, vehicle condition and fuel are normalized from current/max.
     * Saved SIS Item.Hands slots 0/1 supply hand IDs and authored static icons;
     * bRequiresAllSlotsInGroup links both hands. No inventory or Pawn is created.
     * There is no authoritative morale, quick-slot binding or vehicle occupancy yet:
     * their display values are zero. Posture, stealth and minimap position retain
     * FBattleMemberView's presentation defaults until tactical state supplies them.
     */
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据", meta=(WorldContext="WorldContextObject", DisplayName="解析战斗小队展示数据"))
    static bool ResolveSquadViews(const UObject* WorldContextObject, const TArray<FGuid>& SquadIds,
        TArray<FBattleSquadView>& OutSquads, FText& OutError);
};
