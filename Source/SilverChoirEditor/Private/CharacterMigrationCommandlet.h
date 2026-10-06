#pragma once
#include "Commandlets/Commandlet.h"
#include "CharacterMigrationCommandlet.generated.h"

UCLASS()
class UCharacterMigrationCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCharacterMigrationCommandlet();
    virtual int32 Main(const FString& Params) override;
    UFUNCTION(BlueprintCallable, Category="Character Migration")
    static bool SetupBattleNavigation();
};
