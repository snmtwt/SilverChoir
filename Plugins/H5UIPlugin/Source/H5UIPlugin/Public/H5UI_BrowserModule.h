#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SWidget;

/** Factory implemented by the optional, lazily loaded CEF module. */
class IH5UI_BrowserModule : public IModuleInterface
{
public:
	virtual TSharedRef<SWidget> CreateBrowserWidget(
		const FString& InitialURL,
		UObject* BridgeObject,
		int32 FrameRate) = 0;

	/** Executes JavaScript in a browser widget previously created by this module. */
	virtual bool ExecuteJavaScript(
		const TSharedPtr<SWidget>& BrowserWidget,
		const FString& Script) = 0;
};
