#pragma once

#include "Commandlets/Commandlet.h"
#include "OperationsCommandRoomWidgetEventsCommandlet.generated.h"

/** Adds editable command-room Widget Blueprint event flows while preserving authored content. */
UCLASS()
class UOperationsCommandRoomWidgetEventsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsCommandRoomWidgetEventsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
