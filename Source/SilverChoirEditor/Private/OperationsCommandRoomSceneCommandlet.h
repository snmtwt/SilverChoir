#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OperationsCommandRoomSceneCommandlet.generated.h"

UCLASS()
class UOperationsCommandRoomSceneCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UOperationsCommandRoomSceneCommandlet();
    virtual int32 Main(const FString& Params) override;
};
