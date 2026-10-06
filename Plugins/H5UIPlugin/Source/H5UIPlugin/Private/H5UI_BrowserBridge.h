#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "H5UI_BrowserBridge.generated.h"

class UH5UI_View;

/** Restricted bridge exposed only to CEF pages hosted by an H5UI iframe. */
UCLASS()
class UH5UI_BrowserBridge final : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UH5UI_View* InOwner);

	UFUNCTION()
	void Emit(
		const FString& EventType,
		const FString& FunctionName,
		const FString& ArgumentsJson,
		const FString& ElementId);

private:
	TWeakObjectPtr<UH5UI_View> Owner;
};
