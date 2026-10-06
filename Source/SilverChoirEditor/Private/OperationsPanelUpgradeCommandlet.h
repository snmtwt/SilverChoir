#pragma once
#include "Commandlets/Commandlet.h"
#include "OperationsPanelUpgradeCommandlet.generated.h"
UCLASS()
class UOperationsPanelUpgradeCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsPanelUpgradeCommandlet();
    virtual int32 Main(const FString& Params) override;
};
