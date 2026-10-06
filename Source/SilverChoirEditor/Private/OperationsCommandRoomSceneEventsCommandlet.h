#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OperationsCommandRoomSceneEventsCommandlet.generated.h"

/** Inspects or adds editable room lifecycle graphs without replacing authored graphs. */
UCLASS()
class UOperationsCommandRoomSceneEventsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsCommandRoomSceneEventsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
