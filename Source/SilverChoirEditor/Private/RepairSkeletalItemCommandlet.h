#pragma once

#include "Commandlets/Commandlet.h"
#include "RepairSkeletalItemCommandlet.generated.h"

/** Repairs a stale inherited mesh reference after reparenting an item Blueprint. */
UCLASS()
class URepairSkeletalItemCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
