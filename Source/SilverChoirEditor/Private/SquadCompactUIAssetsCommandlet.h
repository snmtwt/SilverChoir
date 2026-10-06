#pragma once

#include "Commandlets/Commandlet.h"
#include "SquadCompactUIAssetsCommandlet.generated.h"

/** Updates the existing squad icon picker and member cards without rebuilding Blueprint graphs or animations. */
UCLASS()
class USquadCompactUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    USquadCompactUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
