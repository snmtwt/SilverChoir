#pragma once
#include "Commandlets/Commandlet.h"
#include "ECGAssetsCommandlet.generated.h"
UCLASS()
class UECGAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UECGAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
