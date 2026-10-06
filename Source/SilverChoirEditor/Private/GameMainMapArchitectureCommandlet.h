#pragma once
#include "Commandlets/Commandlet.h"
#include "GameMainMapArchitectureCommandlet.generated.h"

UCLASS()
class UGameMainMapArchitectureCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UGameMainMapArchitectureCommandlet();
    virtual int32 Main(const FString& Params) override;
};
