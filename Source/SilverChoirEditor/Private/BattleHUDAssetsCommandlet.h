#pragma once
#include "Commandlets/Commandlet.h"
#include "BattleHUDAssetsCommandlet.generated.h"
UCLASS()
class UBattleHUDAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBattleHUDAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
