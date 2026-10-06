#pragma once
#include "Commandlets/Commandlet.h"
#include "OperationsRefinementCommandlet.generated.h"
UCLASS()
class UOperationsRefinementCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsRefinementCommandlet();
    virtual int32 Main(const FString& Params) override;
};
