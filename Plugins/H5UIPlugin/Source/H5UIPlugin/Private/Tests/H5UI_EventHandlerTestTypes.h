#pragma once

#include "CoreMinimal.h"
#include "H5UI_EventHandler.h"
#include "H5UI_EventHandlerTestTypes.generated.h"

USTRUCT()
struct FH5UI_TestStructArgument
{
	GENERATED_BODY()

	UPROPERTY()
	FString SlotId;

	UPROPERTY()
	FString ElementId;
};

UCLASS()
class UH5UI_StructArrayEventTestHandler : public UH5UI_EventHandler
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void ReceiveStructArray(const TArray<FH5UI_TestStructArgument>& Values);

	TArray<FH5UI_TestStructArgument> ReceivedValues;
};
