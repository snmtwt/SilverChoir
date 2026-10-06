#pragma once

#include "CoreMinimal.h"
#include "H5UI_BrowserBridge.h"
#include "H5UI_RuntimeView.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UH5UI_View;
class SConstraintCanvas;

class SH5UI_View final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SH5UI_View)
		: _OwnerWidget(nullptr)
		, _DefaultSize(640.0f, 360.0f)
	{
	}
		SLATE_ARGUMENT(UH5UI_View*, OwnerWidget)
		SLATE_ARGUMENT(FVector2D, DefaultSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool LoadURL(const FString& URL);
	bool LoadString(const FString& Html, const FString& BaseURL);
	bool Reload();
	void Close();
	bool FocusElementById(const FString& ElementId);
	bool ExecuteJavaScript(const FString& Script, FString& Result, FString& Error);
	bool DispatchHtmlEvent(const FString& EventName, const FString& Detail);
	bool DispatchIFrameEvent(const FString& EventName, const FString& Detail);
	bool IsPointerInteractingAt(const FVector2D& LocalPosition);
	FVector2D LocalToDocumentPosition(const FVector2D& LocalPosition) const;
	void SetData(const FString& Name, const FString& Value);
	void SynchronizeModels();
	void SetRenderScale(float InRenderScale);
	void Configure(
		int32 TargetFrameRate,
		bool bReceiveInput,
		bool bConsumeInput,
		bool bPassThroughTransparentMouseInput,
		bool bPassThroughKeyboardWithoutEditableFocus,
		bool bEnableJavaScript,
		float InRenderScale,
		bool bInEnableBrowserSubviews = false);
	FH5UI_PerformanceStats GetPerformanceStats() const;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;

private:
	FReply ResolveInputReply(bool bInteracting) const;
	FReply ResolvePointerInputReply(bool bInteracting) const;
	void RememberPointerPosition(const FGeometry& Geometry, const FVector2D& ScreenPosition);
	void PumpCapturedPointer(const FGeometry& Geometry);
	void ReleaseNativePointerSession();
	void TracePointerMove(const TCHAR* Source);
	void RefreshBrowserSubview();
	void DestroyBrowserSubview();
	FMargin GetBrowserOffset() const;
	FString ResolveBrowserSource(const FString& Source) const;

	TWeakObjectPtr<UH5UI_View> OwnerWidget;
	TSharedPtr<FH5UI_RuntimeView> RuntimeView;
	TSharedPtr<SConstraintCanvas> BrowserCanvas;
	TSharedPtr<SWidget> BrowserWidget;
	TStrongObjectPtr<UH5UI_BrowserBridge> BrowserBridge;
	FString LoadedBrowserSource;
	FVector2D BrowserPosition = FVector2D::ZeroVector;
	FVector2D BrowserSize = FVector2D::ZeroVector;
	FVector2D DefaultSize = FVector2D(640.0f, 360.0f);
	float RenderScale = 1.0f;
	TSet<int32> PressedMouseButtons;
	FVector2D LastPointerScreenPosition = FVector2D::ZeroVector;
	FVector2D LastPointerLocalPosition = FVector2D::ZeroVector;
	bool bHasPointerPosition = false;
	uint64 PointerSessionSequence = 0;
	uint64 PointerMoveCount = 0;
	bool bReceiveInput = true;
	bool bConsumeInput = true;
	bool bPassThroughTransparentMouseInput = true;
	bool bPassThroughKeyboardWithoutEditableFocus = true;
	bool bEnableJavaScript = true;
	bool bEnableBrowserSubviews = false;
};
