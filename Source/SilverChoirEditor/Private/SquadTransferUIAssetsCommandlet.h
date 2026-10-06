#pragma once

#include "Commandlets/Commandlet.h"
#include "SquadTransferUIAssetsCommandlet.generated.h"

/** Adds the editable squad transfer dialog and initial name table without replacing authored assets. */
UCLASS()
class USquadTransferUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USquadTransferUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
