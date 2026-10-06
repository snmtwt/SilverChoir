#pragma once

#include "CoreMinimal.h"
#include "Input/Events.h"
#include "RmlUi/Core/EventListener.h"
#include "H5UI_Interfaces.h"
#include "H5UI_Types.h"

struct FGeometry;
class FSlateWindowElementList;
class FH5UI_ScriptRuntime;

namespace Rml
{
	class Context;
	class Element;
	class ElementDocument;
}

struct FH5UI_BrowserSubview
{
	FString Source;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

class FH5UI_RuntimeView final
{
public:
	using FReadyCallback = TFunction<void()>;
	using FFailedCallback = TFunction<void(const FString&)>;
	using FEventCallback = TFunction<void(const FH5UI_Event&)>;
	using FJavaScriptErrorCallback = TFunction<void(const FString&)>;

	FH5UI_RuntimeView(
		FReadyCallback InReady,
		FFailedCallback InFailed,
		FEventCallback InEvent,
		FJavaScriptErrorCallback InJavaScriptError = FJavaScriptErrorCallback());
	~FH5UI_RuntimeView();

	bool LoadURL(const FString& URL);
	bool LoadString(const FString& Html, const FString& BaseURL);
	bool Reload();
	void Close();
	bool FocusElementById(const FString& ElementId);
	bool ExecuteJavaScript(const FString& Script, FString& OutResult, FString& OutError);
	bool DispatchHtmlEvent(const FString& EventName, const FString& Detail);
	bool HasEditableFocus() const;
	void BlurFocusedElement();

	void SetTargetFrameRate(int32 InFrameRate);
	void SetJavaScriptEnabled(bool bEnabled);
	void SetData(const FString& Name, const FString& Value);
	void SynchronizeModels();

	bool Update(
		const FVector2D& LocalSize,
		double CurrentTime,
		float ScreenPixelScale = 1.0f,
		float RenderScale = 1.0f);
	int32 Paint(const FGeometry& Geometry, FSlateWindowElementList& ElementList, int32 LayerId);
	bool GetBrowserSubview(FH5UI_BrowserSubview& OutSubview) const;

	bool ProcessMouseMove(const FVector2D& LocalPosition, const FPointerEvent& Event);
	bool ProcessMouseButtonDown(const FVector2D& LocalPosition, const FPointerEvent& Event);
	bool ProcessMouseButtonUp(const FVector2D& LocalPosition, const FPointerEvent& Event);
	bool ProcessMouseWheel(const FVector2D& LocalPosition, const FPointerEvent& Event);
	/** Pumps a captured pointer from Slate's current cursor position even when no routed mouse-move arrived. */
	bool ProcessCapturedMouseMove(const FVector2D& LocalPosition, int32 Modifiers = 0);
	/** Releases a button whose Slate capture was interrupted before a routed mouse-up arrived. */
	bool ReleaseCapturedMouseButton(
		const FVector2D& LocalPosition,
		int32 Button,
		int32 Modifiers = 0);
	bool IsPointerInteractingAt(const FVector2D& LocalPosition);
	/** Converts Slate-local coordinates to browser document/CSS pixel coordinates. */
	FVector2D LocalToDocumentPosition(const FVector2D& LocalPosition) const;
	void ProcessMouseLeave();
	bool ProcessKeyDown(const FKeyEvent& Event);
	bool ProcessKeyUp(const FKeyEvent& Event);
	bool ProcessKeyChar(const FCharacterEvent& Event);

	EH5UI_ViewState GetState() const;
	const FH5UI_PerformanceStats& GetPerformanceStats() const;

#if WITH_DEV_AUTOMATION_TESTS
	FVector2D GetElementBorderSizeForTesting(const FString& ElementId) const;
	FVector2D GetElementBorderPositionForTesting(const FString& ElementId) const;
	bool HasElementFontFaceForTesting(const FString& ElementId) const;
	float GetElementFontRasterizationScaleForTesting(const FString& ElementId) const;
	bool HasElementTransformForTesting(const FString& ElementId) const;
	float GetElementTopLeftBorderRadiusForTesting(const FString& ElementId) const;
	FVector2D GetDescendantBorderSizeForTesting(const FString& ElementId, const FString& TagName) const;
	FVector2D GetDescendantBorderPositionForTesting(const FString& ElementId, const FString& TagName) const;
	FVector2D GetDirectTextVisualCenterForTesting(const FString& ElementId) const;
	int32 GetDirectTextLineCountForTesting(const FString& ElementId) const;
	FString GetDescendantInnerRmlForTesting(const FString& ElementId, const FString& TagName) const;
	bool HasElementAttributeForTesting(const FString& ElementId, const FString& AttributeName) const;
	FString GetElementAttributeForTesting(const FString& ElementId, const FString& AttributeName) const;
	FString GetElementValueForTesting(const FString& ElementId) const;
	FString GetElementInnerRmlForTesting(const FString& ElementId) const;
	bool IsDescendantInsideElementForTesting(const FString& ElementId, const FString& TagName) const;
	bool IsPointerInteractingAtForTesting(const FVector2D& LocalPosition);
	bool ProcessMouseMoveForTesting(const FVector2D& LocalPosition);
	bool ProcessMouseButtonDownForTesting(const FVector2D& LocalPosition);
	bool ProcessMouseButtonUpForTesting(const FVector2D& LocalPosition);
	bool ProcessMouseWheelForTesting(const FVector2D& LocalPosition, float WheelDelta);
	bool ClickElementForTesting(const FString& ElementId);
	bool HasGeneratedPseudoElementForTesting(const FString& ElementId, bool bBefore) const;
#endif

private:
	friend class FSilverChoirBackgroundAnimationTest;
	friend class FH5UI_BindingSynchronizationPerformanceTest;
	friend class FH5UI_DynamicPerformanceBindingsTest;
	class FH5UI_BridgeEventListener final : public Rml::EventListener
	{
	public:
		explicit FH5UI_BridgeEventListener(FH5UI_RuntimeView& InOwner);
		virtual void ProcessEvent(Rml::Event& Event) override;

	private:
		FH5UI_RuntimeView& Owner;
	};

	bool LoadDocumentText(const FString& DocumentText, const FString& SourceURL);
	void AttachBridgeListeners(Rml::Element* Element);
	bool ApplyGeneratedPseudoElements();
	void SynchronizeElement(Rml::Element* Element);
	void SynchronizeElementBindings(Rml::Element* Element);
	void SynchronizePeriodicBindings(Rml::Element* Element, bool& bPerformanceValuesPublished);
	void UpdatePerformanceModelValues();
	void PublishPerformanceStats(double CurrentTime);
	void HandleBridgeEvent(Rml::Event& Event);
	void EmitJavaScriptEvent(const FString& EventType, const FString& Name, const FString& Payload, const FString& ElementId);
	FString GetJavaScriptData(const FString& Name) const;
	void SetJavaScriptData(const FString& Name, const FString& Value);
	void ReportJavaScriptError(const FString& Error) const;
	void MarkActive();
	void Fail(const FString& Error);

	FString ContextName;
	Rml::Context* Context = nullptr;
	Rml::ElementDocument* Document = nullptr;
	TUniquePtr<FH5UI_BridgeEventListener> BridgeEventListener;
	TUniquePtr<FH5UI_ScriptRuntime> ScriptRuntime;
	TArray<FH5UI_GeneratedPseudoSelector> GeneratedPseudoSelectors;
	TMap<FString, FString> ModelValues;
	bool bSynchronizingModels = false;
#if WITH_DEV_AUTOMATION_TESTS
	uint64 BindingContentWriteCount = 0;
	uint64 PeriodicBindingElementSyncCount = 0;
	uint64 PerformanceModelPublishCount = 0;
#endif
	bool bJavaScriptEnabled = true;
	bool bStrategyControlGeometryLogged = false;
	/**
	 * Input and native HTML events can change paint-only state or the contents
	 * of an external texture without making RmlUi report a layout update.
	 */
	bool bPaintRequested = true;
	bool bLastUpdateRequestedPaintOnly = false;
	uint64 PaintOnlyUpdateCount = 0;
	uint64 PaintOnlySubmissionCount = 0;
	FString CurrentURL;
	EH5UI_ViewState State = EH5UI_ViewState::Unloaded;
	FIntPoint ViewSize = FIntPoint(1, 1);
	FIntPoint RenderSize = FIntPoint(1, 1);
	float ScreenPixelScale = 1.0f;
	float LayoutViewportScale = 1.0f;
	float RenderScale = 1.0f;
	float FontRasterizationScale = 1.0f;
	int32 TargetFrameRate = 60;
	double NextUpdateTime = 0.0;
	double NextPerformancePublishTime = 0.0;
	double LastActivityTime = 0.0;
	float LastUpdateMilliseconds = 0.0f;
	FH5UI_PerformanceStats PerformanceStats;
	FReadyCallback ReadyCallback;
	FFailedCallback FailedCallback;
	FEventCallback EventCallback;
	FJavaScriptErrorCallback JavaScriptErrorCallback;
};
