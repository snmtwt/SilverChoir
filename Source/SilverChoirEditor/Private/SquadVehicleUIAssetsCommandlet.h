#pragma once

#include "Commandlets/Commandlet.h"
#include "SquadVehicleUIAssetsCommandlet.generated.h"

/** Adds the squad vehicle picker without replacing the authored room tree, graphs or animations. */
UCLASS()
class USquadVehicleUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USquadVehicleUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
