#pragma once
#include "Commandlets/Commandlet.h"
#include "PersonnelUIAssetsCommandlet.generated.h"
UCLASS()
class UPersonnelUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UPersonnelUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
