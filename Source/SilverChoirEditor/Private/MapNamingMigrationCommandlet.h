#pragma once
#include "Commandlets/Commandlet.h"
#include "MapNamingMigrationCommandlet.generated.h"

/** One-off naming migration. Snapshot captures reflection before changing native names. */
UCLASS()
class UMapNamingMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMapNamingMigrationCommandlet() { IsEditor=true; IsClient=false; LogToConsole=true; }
	virtual int32 Main(const FString& Params) override;
};
