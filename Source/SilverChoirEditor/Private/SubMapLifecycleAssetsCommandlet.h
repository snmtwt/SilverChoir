#pragma once
#include "Commandlets/Commandlet.h"
#include "SubMapLifecycleAssetsCommandlet.generated.h"
UCLASS()
class USubMapLifecycleAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USubMapLifecycleAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
