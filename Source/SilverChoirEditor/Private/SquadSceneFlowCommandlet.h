#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SquadSceneFlowCommandlet.generated.h"

/** Builds the editable squad-room scene Blueprint flow; does not run gameplay orchestration in C++. */
UCLASS()
class USquadSceneFlowCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    USquadSceneFlowCommandlet();
    virtual int32 Main(const FString& Params) override;
};
