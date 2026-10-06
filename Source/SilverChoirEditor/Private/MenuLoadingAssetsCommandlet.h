#pragma once
#include "Commandlets/Commandlet.h"
#include "MenuLoadingAssetsCommandlet.generated.h"

UCLASS()
class UMenuLoadingAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMenuLoadingAssetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
