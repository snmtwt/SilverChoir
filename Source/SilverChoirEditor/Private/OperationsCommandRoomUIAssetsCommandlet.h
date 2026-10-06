#pragma once

#include "Commandlets/Commandlet.h"
#include "OperationsCommandRoomUIAssetsCommandlet.generated.h"

/** Authors the command room's initial Designer layout without changing existing graphs. */
UCLASS()
class UOperationsCommandRoomUIAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsCommandRoomUIAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
