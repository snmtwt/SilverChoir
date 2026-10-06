#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "H5UI_Settings.generated.h"

class UH5UI_EventHandler;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "H5 UI Plugin"))
class H5UIPLUGIN_API UH5UI_Settings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UH5UI_Settings();

	virtual FName GetCategoryName() const override;

	/** Directory below the project Content folder used by coui://uiresources/. */
	UPROPERTY(Config, EditAnywhere, Category = "Resources")
	FString ResourceDirectory;

	/** Additional font files loaded as fallback faces. Paths may be project-relative or absolute. */
	UPROPERTY(Config, EditAnywhere, Category = "Resources", meta = (FilePathFilter = "Font files (*.ttf;*.otf)|*.ttf;*.otf"))
	TArray<FFilePath> FallbackFonts;

	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "240"))
	int32 ActiveUpdateRate;

	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "60"))
	int32 IdleUpdateRate;

	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "0.0"))
	float IdleAfterSeconds;

	/** Default memory limit for each independent JavaScript view context. */
	UPROPERTY(Config, EditAnywhere, Category = "JavaScript", meta = (ClampMin = "1", ClampMax = "256"))
	int32 JavaScriptMemoryLimitMB;

	/** Maximum uninterrupted execution time for one script or callback on the game thread. */
	UPROPERTY(Config, EditAnywhere, Category = "JavaScript", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float JavaScriptExecutionTimeLimitMilliseconds;

	/** Frame budget used to drain Promise/microtask jobs. Vue component updates commonly need more time than a small event callback. */
	UPROPERTY(Config, EditAnywhere, Category = "JavaScript", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float JavaScriptPromiseJobTimeLimitMilliseconds;

	/** One-time budget for evaluating each page script, allowing bundled frameworks to initialise. */
	UPROPERTY(Config, EditAnywhere, Category = "JavaScript", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float JavaScriptInitialExecutionTimeLimitMilliseconds;

	/** Maximum timer and promise callbacks processed by one view during a frame. */
	UPROPERTY(Config, EditAnywhere, Category = "JavaScript", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 JavaScriptMaxCallbacksPerFrame;

	UPROPERTY(Config, EditAnywhere, Category = "Security")
	bool bAllowAbsoluteFilePaths;

	/**
	 * Typed HTML-to-Unreal routes. Each view creates one instance for every entry when its page becomes ready.
	 * HTML selects a route with H5UI.emit(EventType, FunctionName, ...Arguments).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Events")
	TMap<FName, TSoftClassPtr<UH5UI_EventHandler>> EventHandlerClasses;
};
