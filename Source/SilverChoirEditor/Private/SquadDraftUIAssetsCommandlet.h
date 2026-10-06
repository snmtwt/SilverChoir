#pragma once

#include "Commandlets/Commandlet.h"
#include "SquadDraftUIAssetsCommandlet.generated.h"

/** Surgically updates the squad management Designer tree while preserving authored Blueprint logic and animations. */
UCLASS()
class USquadDraftUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USquadDraftUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
