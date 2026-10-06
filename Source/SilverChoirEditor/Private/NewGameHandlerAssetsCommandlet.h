#pragma once
#include "Commandlets/Commandlet.h"
#include "NewGameHandlerAssetsCommandlet.generated.h"
UCLASS()
class UNewGameHandlerAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UNewGameHandlerAssetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
