#pragma once
#include "Commandlets/Commandlet.h"
#include "MainMenuAssetsCommandlet.generated.h"

/** Explicit authoring utility, never runs automatically when opening the editor. */
UCLASS()
class UMainMenuAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMainMenuAssetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
