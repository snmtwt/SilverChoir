#pragma once

#include "Commandlets/Commandlet.h"
#include "PersonnelSceneReturnRepairCommandlet.generated.h"

/** Applies a targeted, editable Blueprint patch without rebuilding the room's user-authored flow. */
UCLASS()
class UPersonnelSceneReturnRepairCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UPersonnelSceneReturnRepairCommandlet();
    virtual int32 Main(const FString& Params) override;
};
