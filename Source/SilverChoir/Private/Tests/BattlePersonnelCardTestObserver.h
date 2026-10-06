#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BattlePersonnelCardTestObserver.generated.h"

/** Records the card's dynamic selection delegate without deciding roster state. */
UCLASS(NotBlueprintable, Transient)
class UBattlePersonnelCardTestObserver : public UObject
{
    GENERATED_BODY()
public:
    int32 Requests = 0;
    FName LastChoice;

    UFUNCTION()
    void RecordSelection(FName Choice)
    {
        ++Requests;
        LastChoice = Choice;
    }
};
