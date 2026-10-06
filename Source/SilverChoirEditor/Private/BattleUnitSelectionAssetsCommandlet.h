#pragma once

#include "Commandlets/Commandlet.h"
#include "BattleUnitSelectionAssetsCommandlet.generated.h"

/** Adds editable battle-selection input routing while retaining every existing mouse branch. */
UCLASS()
class UBattleUnitSelectionAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBattleUnitSelectionAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
