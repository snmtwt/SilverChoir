#pragma once
#include "Commandlets/Commandlet.h"
#include "RegionTextMaterialCommandlet.generated.h"
UCLASS()
class URegionTextMaterialCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	URegionTextMaterialCommandlet() { IsEditor=true; IsClient=false; LogToConsole=true; }
	virtual int32 Main(const FString& Params) override;
};
