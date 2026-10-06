#pragma once
#include "Commandlets/Commandlet.h"
#include "MainMapInputCommandlet.generated.h"

UCLASS()
class UMainMapInputCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UMainMapInputCommandlet() { IsEditor=true; IsClient=false; LogToConsole=true; }
    virtual int32 Main(const FString& Params) override;
};
