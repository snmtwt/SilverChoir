#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CommandMapInputCommandlet.generated.h"

/** Adds the command-room gesture to the existing controller without rewriting other inputs. */
UCLASS()
class UCommandMapInputCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCommandMapInputCommandlet();
    virtual int32 Main(const FString& Params) override;
};
