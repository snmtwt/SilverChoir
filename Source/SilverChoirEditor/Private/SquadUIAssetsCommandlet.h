#pragma once

#include "Commandlets/Commandlet.h"
#include "SquadUIAssetsCommandlet.generated.h"

/** Authors the editable squad-room Designer tree without replacing Blueprint graphs. */
UCLASS()
class USquadUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USquadUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
