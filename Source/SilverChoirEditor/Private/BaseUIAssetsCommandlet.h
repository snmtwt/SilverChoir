#pragma once
#include "Commandlets/Commandlet.h"
#include "BaseUIAssetsCommandlet.generated.h"
UCLASS()
class UBaseUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBaseUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
