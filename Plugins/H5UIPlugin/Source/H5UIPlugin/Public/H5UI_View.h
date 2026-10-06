#pragma once

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "H5UI_Types.h"
#include "H5UI_View.generated.h"

class SH5UI_View;
class UH5UI_View;
class UH5UI_EventHandler;

/** Native, type-agnostic event stream used by optional integration plugins. */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FH5UI_NativeUIEventDelegate,
	UH5UI_View*,
	const FH5UI_Event&);

/** Native lifecycle notification for integrations that hold weak view registrations. */
DECLARE_MULTICAST_DELEGATE_OneParam(FH5UI_ViewReleasedDelegate, UH5UI_View*);

UCLASS(meta = (DisplayName = "H5 UI View"))
class H5UIPLUGIN_API UH5UI_View final : public UWidget
{
	GENERATED_BODY()

public:
	UH5UI_View(const FObjectInitializer& ObjectInitializer);

	/** Gameface-compatible resource URL, for example coui://uiresources/hud.html. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin")
	FString URL;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin")
	bool bAutoLoad;

	/** Optional CEF iframe support. Off by default: no browser creation or per-frame iframe scanning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Browser", meta = (DisplayName = "Enable CEF IFrames"))
	bool bEnableBrowserSubviews = false;

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Browser", meta = (DisplayName = "Set CEF IFrames Enabled"))
	void SetBrowserSubviewsEnabled(bool bEnabled);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Input")
	bool bReceiveInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Input")
	bool bConsumeInput;

	/** Let pointer events continue to the game when RCSS pointer-events excludes the hovered page area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Input")
	bool bPassThroughTransparentMouseInput;

	/** Let keyboard events continue to the game unless an editable HTML control has focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Input")
	bool bPassThroughKeyboardWithoutEditableFocus;

	/** Execute standard script tags and expose the native DOM and UE bridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|JavaScript", meta = (DisplayName = "Enable JavaScript"))
	bool bEnableJavaScript;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Performance", meta = (ClampMin = "1", ClampMax = "240"))
	int32 TargetFrameRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Layout")
	FVector2D DefaultSize;

	/**
	 * Additional raster-resolution multiplier applied after the automatic Slate/UMG screen pixel scale.
	 * A value of 1.0 renders at the native screen density. Values above 1.0 supersample rasterized UI resources,
	 * while values below 1.0 can reduce their cost on high-DPI displays. CSS layout dimensions are unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Rendering",
		meta = (ClampMin = "0.5", ClampMax = "4.0", UIMin = "0.5", UIMax = "4.0"))
	float RenderScale;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "H5 UI Plugin")
	EH5UI_ViewState ViewState;

	UPROPERTY(BlueprintAssignable, Category = "H5 UI Plugin|Events")
	FH5UI_ViewReadyDelegate OnReadyForBindings;

	UPROPERTY(BlueprintAssignable, Category = "H5 UI Plugin|Events")
	FH5UI_ViewFailedDelegate OnLoadFailed;

	UPROPERTY(BlueprintAssignable, Category = "H5 UI Plugin|Events")
	FH5UI_UIEventDelegate OnUIEvent;

	/**
	 * Per-view typed HTML-to-Unreal routes. Each entry creates one handler when
	 * this view's document becomes ready and overrides the project-level route
	 * for the same event type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "H5 UI Plugin|Events",
		meta = (DisplayName = "Event Handler Classes"))
	TMap<FName, TSoftClassPtr<UH5UI_EventHandler>> EventHandlerClasses;

	UPROPERTY(BlueprintAssignable, Category = "H5 UI Plugin|JavaScript")
	FH5UI_JavaScriptErrorDelegate OnJavaScriptError;

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin", meta = (DisplayName = "Load URL"))
	bool LoadURL(const FString& NewURL);

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin", meta = (DisplayName = "Load HTML String"))
	bool LoadString(const FString& Html, const FString& BaseURL = TEXT("coui://uiresources/"));

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin")
	bool Reload();

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin")
	void Close();

	/** Changes the additional raster-resolution multiplier without rebuilding the widget. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Rendering", meta = (DisplayName = "Set HTML Render Scale"))
	void SetHtmlRenderScale(float NewRenderScale);

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Input")
	bool FocusElementById(const FString& ElementId);

	/** Execute JavaScript in this view and return its string or JSON result. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|JavaScript", meta = (DisplayName = "Execute JavaScript"))
	bool ExecuteJavaScript(const FString& Script, FString& Result, FString& Error);

	/** Dispatches a custom event from Unreal to the page document/window. Detail may contain plain text or JSON. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Events", meta = (DisplayName = "Dispatch HTML Event"))
	bool DispatchHtmlEvent(FName EventName, const FString& Detail = TEXT(""));

	/** Dispatches a custom event from Unreal to the currently visible CEF iframe. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Events", meta = (DisplayName = "Dispatch IFrame Event"))
	bool DispatchIFrameEvent(FName EventName, const FString& Detail = TEXT(""));

	/** Sends sample JSON to CommanderOS; the iframe displays it and acknowledges receipt back to Unreal. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Events", meta = (DisplayName = "Send IFrame Test Data"))
	bool SendIFrameTestData(const FString& Message = TEXT("Hello from Unreal"));

	/**
	 * Registers an existing event-handler object for this view. The object receives this view automatically and
	 * takes precedence over a project-settings handler configured with the same event-type key.
	 */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Events", meta = (DisplayName = "Register H5UI Event Handler"))
	bool RegisterEventHandler(FName EventType, UH5UI_EventHandler* EventHandler);

	/** Removes a dynamically registered handler. Passing a different handler does not remove the active route. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Events", meta = (DisplayName = "Unregister H5UI Event Handler"))
	bool UnregisterEventHandler(FName EventType, UH5UI_EventHandler* EventHandler);

	/** Returns the dynamically registered handler first, then the project-settings handler for this event type. */
	UFUNCTION(BlueprintPure, Category = "H5 UI Plugin|Events")
	UH5UI_EventHandler* GetEventHandler(FName EventType) const;

	/** Returns whether this exact handler is registered dynamically on this view for the event type. */
	bool IsEventHandlerDynamicallyRegistered(FName EventType, const UH5UI_EventHandler* EventHandler) const;

	/**
	 * Native presentation adapters can use this to serialize a reflected structure and dispatch it
	 * as an event without replacing the view's persistent pageData model.
	 */
	bool DispatchHtmlEventFromStruct(
		FName EventName,
		const UScriptStruct* StructType,
		const void* StructData);

	/** Serializes a reflected structure using the same JSON and ueasset:// texture rules as page data. */
	bool SerializeStructToJsonString(
		const UScriptStruct* StructType,
		const void* StructData,
		FString& OutJson) const;

	/**
	 * Converts an absolute Slate cursor position to this document's CSS pixel coordinates
	 * and returns whether the page's pointer-events hit test considers that point interactive.
	 * The output uses the same coordinate space as DOM clientX/clientY and
	 * getBoundingClientRect(), including a down-scaled parent UMG transform.
	 */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Input")
	bool IsScreenPositionInteractive(const FVector2D& ScreenPosition, FVector2D& DocumentPosition);

	/** Returns the currently constructed H5 UI views without owning them. */
	static void GetLiveViews(TArray<UH5UI_View*>& OutViews);

	/** Global native event stream. H5UIPlugin remains unaware of subscriber payload types. */
	static FH5UI_NativeUIEventDelegate& OnAnyNativeUIEvent();

	/** Fires before an H5 UI view releases its Slate resources. */
	static FH5UI_ViewReleasedDelegate& OnAnyViewReleased();

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Data Binding")
	void SetDataString(FName Name, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Data Binding")
	void SetDataNumber(FName Name, double Value);

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Data Binding")
	void SetDataBoolean(FName Name, bool bValue);

	/**
	 * Serializes any Blueprint structure to JSON, stores it as pageData, and notifies an already loaded page.
	 * Pages can read the initial value with window.ue.getData('pageData') and listen for H5UIPageData.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "H5 UI Plugin|Data Binding",
		meta = (DisplayName = "Set Page Data (Struct)", CustomStructureParam = "PageData", AutoCreateRefTerm = "PageData"))
	bool SetPageData(const int32& PageData, FName EventName = TEXT("H5UIPageData"));
	DECLARE_FUNCTION(execSetPageData);

	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Data Binding")
	void SynchronizeModels();

	UFUNCTION(BlueprintPure, Category = "H5 UI Plugin|Performance")
	FH5UI_PerformanceStats GetPerformanceStats() const;

	virtual void BeginDestroy() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	friend class SH5UI_View;
	friend class UH5UI_BrowserBridge;

	bool SetPageDataFromStruct(const UScriptStruct* StructType, const void* StructData, FName EventName);

	void NotifyReady();
	void NotifyFailed(const FString& Error);
	void NotifyUIEvent(const FH5UI_Event& Event);
	void NotifyJavaScriptError(const FString& Error);
	void CreateEventHandlers();
	void DeactivateEventHandlers(bool bUnregisterDynamicHandlers);
	TSharedPtr<SH5UI_View> MyHtmlView;
	TMap<FString, FString> PendingModelValues;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UH5UI_EventHandler>> ConfiguredEventHandlers;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UH5UI_EventHandler>> RegisteredEventHandlers;
};
