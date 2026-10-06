#include "SH5UI_View.h"

#include "H5UI_BrowserBridge.h"
#include "H5UI_BrowserModule.h"
#include "H5UI_Interfaces.h"
#include "H5UI_Module.h"
#include "H5UI_View.h"
#include "H5UI_Input.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SNullWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UIBrowserSubview, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogH5UIPointerTrace, Log, All);

void SH5UI_View::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._OwnerWidget;
	DefaultSize = InArgs._DefaultSize;
	SetCanTick(true);
	SetClipping(EWidgetClipping::ClipToBounds);

	RuntimeView = MakeShared<FH5UI_RuntimeView>(
		[WeakOwner = OwnerWidget]()
		{
			if (UH5UI_View* Owner = WeakOwner.Get())
			{
				Owner->NotifyReady();
			}
		},
		[WeakOwner = OwnerWidget](const FString& Error)
		{
			if (UH5UI_View* Owner = WeakOwner.Get())
			{
				Owner->NotifyFailed(Error);
			}
		},
		[WeakOwner = OwnerWidget](const FH5UI_Event& Event)
		{
			if (UH5UI_View* Owner = WeakOwner.Get())
			{
				Owner->NotifyUIEvent(Event);
			}
		},
		[WeakOwner = OwnerWidget](const FString& Error)
		{
			if (UH5UI_View* Owner = WeakOwner.Get())
			{
				Owner->NotifyJavaScriptError(Error);
			}
		});
}

bool SH5UI_View::LoadURL(const FString& URL)
{
	return RuntimeView.IsValid() && RuntimeView->LoadURL(URL);
}

bool SH5UI_View::LoadString(const FString& Html, const FString& BaseURL)
{
	return RuntimeView.IsValid() && RuntimeView->LoadString(Html, BaseURL);
}

bool SH5UI_View::Reload()
{
	return RuntimeView.IsValid() && RuntimeView->Reload();
}

void SH5UI_View::Close()
{
	ReleaseNativePointerSession();
	DestroyBrowserSubview();
	if (RuntimeView)
	{
		RuntimeView->Close();
	}
}

bool SH5UI_View::FocusElementById(const FString& ElementId)
{
	return RuntimeView.IsValid() && RuntimeView->FocusElementById(ElementId);
}

bool SH5UI_View::ExecuteJavaScript(const FString& Script, FString& Result, FString& Error)
{
	return RuntimeView.IsValid() && RuntimeView->ExecuteJavaScript(Script, Result, Error);
}

bool SH5UI_View::DispatchHtmlEvent(const FString& EventName, const FString& Detail)
{
	return RuntimeView.IsValid() && RuntimeView->DispatchHtmlEvent(EventName, Detail);
}

bool SH5UI_View::DispatchIFrameEvent(const FString& EventName, const FString& Detail)
{
	if (!bEnableBrowserSubviews || !BrowserWidget || EventName.IsEmpty())
	{
		return false;
	}

	IH5UI_BrowserModule* BrowserModule = FModuleManager::GetModulePtr<IH5UI_BrowserModule>(TEXT("H5UIPluginCEF"));
	if (!BrowserModule)
	{
		return false;
	}

	TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetStringField(TEXT("name"), EventName);
	Envelope->SetStringField(TEXT("detail"), Detail);
	FString EnvelopeJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EnvelopeJson);
	if (!FJsonSerializer::Serialize(Envelope, Writer))
	{
		return false;
	}

	const FString Script = FString::Printf(
		TEXT("(function(e){var d=e.detail;try{d=JSON.parse(d);}catch(x){}window.dispatchEvent(new CustomEvent(e.name,{detail:d}));})(%s);"),
		*EnvelopeJson);
	const bool bDispatched = BrowserModule->ExecuteJavaScript(BrowserWidget, Script);
	if (bDispatched)
	{
		UE_LOG(LogH5UIBrowserSubview, Log, TEXT("Dispatched iframe event '%s': %s"), *EventName, *Detail);
	}
	return bDispatched;
}

bool SH5UI_View::IsPointerInteractingAt(const FVector2D& LocalPosition)
{
	return RuntimeView.IsValid() && RuntimeView->IsPointerInteractingAt(LocalPosition);
}

FVector2D SH5UI_View::LocalToDocumentPosition(const FVector2D& LocalPosition) const
{
	return RuntimeView.IsValid()
		? RuntimeView->LocalToDocumentPosition(LocalPosition)
		: LocalPosition;
}

void SH5UI_View::SetData(const FString& Name, const FString& Value)
{
	if (RuntimeView)
	{
		RuntimeView->SetData(Name, Value);
	}
}

void SH5UI_View::SynchronizeModels()
{
	if (RuntimeView)
	{
		RuntimeView->SynchronizeModels();
	}
}

void SH5UI_View::SetRenderScale(float InRenderScale)
{
	RenderScale = FMath::Clamp(InRenderScale, 0.5f, 4.0f);
}

void SH5UI_View::Configure(
	int32 TargetFrameRate,
	bool bInReceiveInput,
	bool bInConsumeInput,
	bool bInPassThroughTransparentMouseInput,
	bool bInPassThroughKeyboardWithoutEditableFocus,
	bool bInEnableJavaScript,
	float InRenderScale,
	bool bInEnableBrowserSubviews)
{
	bReceiveInput = bInReceiveInput;
	bConsumeInput = bInConsumeInput;
	bPassThroughTransparentMouseInput = bInPassThroughTransparentMouseInput;
	bPassThroughKeyboardWithoutEditableFocus = bInPassThroughKeyboardWithoutEditableFocus;
	bEnableJavaScript = bInEnableJavaScript;
	SetRenderScale(InRenderScale);
	if (bEnableBrowserSubviews != bInEnableBrowserSubviews)
	{
		bEnableBrowserSubviews = bInEnableBrowserSubviews;
		if (bEnableBrowserSubviews)
		{
			ChildSlot [ SAssignNew(BrowserCanvas, SConstraintCanvas) ];
		}
		else
		{
			DestroyBrowserSubview();
			ChildSlot [ SNullWidget::NullWidget ];
			BrowserCanvas.Reset();
		}
	}
	if (RuntimeView)
	{
		RuntimeView->SetTargetFrameRate(TargetFrameRate);
		RuntimeView->SetJavaScriptEnabled(bEnableJavaScript);
	}
}

FH5UI_PerformanceStats SH5UI_View::GetPerformanceStats() const
{
	return RuntimeView.IsValid() ? RuntimeView->GetPerformanceStats() : FH5UI_PerformanceStats();
}

FVector2D SH5UI_View::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return DefaultSize;
}

void SH5UI_View::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	PumpCapturedPointer(AllottedGeometry);
	const FSlateRenderTransform& ScreenTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FVector2f ScreenOrigin = TransformPoint(ScreenTransform, FVector2f::ZeroVector);
	const float ScreenScaleX = (TransformPoint(ScreenTransform, FVector2f(1.0f, 0.0f)) - ScreenOrigin).Size();
	const float ScreenScaleY = (TransformPoint(ScreenTransform, FVector2f(0.0f, 1.0f)) - ScreenOrigin).Size();
	const float ScreenPixelScale = FMath::Max(ScreenScaleX, ScreenScaleY);
	if (RuntimeView && RuntimeView->Update(AllottedGeometry.GetLocalSize(), InCurrentTime, ScreenPixelScale, RenderScale))
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (bEnableBrowserSubviews) { RefreshBrowserSubview(); }
}

int32 SH5UI_View::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 NativeLayerId = RuntimeView.IsValid()
		? RuntimeView->Paint(AllottedGeometry, OutDrawElements, LayerId)
		: LayerId;
	if (!bEnableBrowserSubviews) { return NativeLayerId; }
	const int32 BrowserLayerId = SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		NativeLayerId + 1,
		InWidgetStyle,
		bParentEnabled);
	return FMath::Max(NativeLayerId, BrowserLayerId);
}

bool SH5UI_View::SupportsKeyboardFocus() const
{
	return bReceiveInput;
}

FReply SH5UI_View::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	return bReceiveInput ? FReply::Handled() : FReply::Unhandled();
}

void SH5UI_View::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	if (RuntimeView)
	{
		RuntimeView->BlurFocusedElement();
	}
	SCompoundWidget::OnFocusLost(InFocusEvent);
}

FReply SH5UI_View::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bReceiveInput || !RuntimeView)
	{
		return FReply::Unhandled();
	}
	RememberPointerPosition(MyGeometry, MouseEvent.GetScreenSpacePosition());
	TracePointerMove(TEXT("routed"));
	return ResolvePointerInputReply(
		RuntimeView->ProcessMouseMove(LastPointerLocalPosition, MouseEvent));
}

FReply SH5UI_View::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bReceiveInput || !RuntimeView)
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const bool bInteracting = RuntimeView->ProcessMouseButtonDown(LocalPosition, MouseEvent);
	if (bPassThroughTransparentMouseInput && !bInteracting)
	{
		return FReply::Unhandled();
	}
	if (bConsumeInput || bInteracting)
	{
		const int32 Button = H5UI_Input::GetMouseButtonIndex(MouseEvent.GetEffectingButton());
		if (Button != INDEX_NONE)
		{
			if (PressedMouseButtons.IsEmpty())
			{
				++PointerSessionSequence;
				PointerMoveCount = 0;
			}
			PressedMouseButtons.Add(Button);
		}
		RememberPointerPosition(MyGeometry, MouseEvent.GetScreenSpacePosition());
		UE_LOG(
			LogH5UIPointerTrace,
			Warning,
			TEXT("[SISH5UI PointerTrace][Slate] down Sequence=%llu Button=%d "
				"Screen=(%.1f,%.1f) Local=(%.1f,%.1f) Interacting=%s"),
			PointerSessionSequence,
			Button,
			LastPointerScreenPosition.X,
			LastPointerScreenPosition.Y,
			LastPointerLocalPosition.X,
			LastPointerLocalPosition.Y,
			bInteracting ? TEXT("true") : TEXT("false"));
		FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this));
		return RuntimeView->HasEditableFocus()
			? Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse)
			: Reply.ClearUserFocus(EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

FReply SH5UI_View::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate routes the second press here instead of through OnMouseButtonDown.
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SH5UI_View::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bReceiveInput || !RuntimeView)
	{
		return FReply::Unhandled();
	}

	const bool bHadMouseCapture = HasMouseCapture();
	RememberPointerPosition(MyGeometry, MouseEvent.GetScreenSpacePosition());
	const int32 Button = H5UI_Input::GetMouseButtonIndex(MouseEvent.GetEffectingButton());
	const bool bInteracting = RuntimeView->ProcessMouseButtonUp(
		LastPointerLocalPosition,
		MouseEvent);
	PressedMouseButtons.Remove(Button);
	UE_LOG(
		LogH5UIPointerTrace,
		Warning,
		TEXT("[SISH5UI PointerTrace][Slate] up Sequence=%llu Button=%d "
			"Moves=%llu Screen=(%.1f,%.1f) Local=(%.1f,%.1f) "
			"HadCapture=%s RemainingButtons=%d"),
		PointerSessionSequence,
		Button,
		PointerMoveCount,
		LastPointerScreenPosition.X,
		LastPointerScreenPosition.Y,
		LastPointerLocalPosition.X,
		LastPointerLocalPosition.Y,
		bHadMouseCapture ? TEXT("true") : TEXT("false"),
		PressedMouseButtons.Num());
	// Mouse capture belongs to the press lifecycle, not to the element currently
	// under the release position. RmlUi can legitimately report an unhandled
	// release after the pressed DOM node moved, was hidden, or the pointer left
	// its interactive area. Always release capture acquired by this view.
	if (bHadMouseCapture && PressedMouseButtons.IsEmpty())
	{
		return FReply::Handled().ReleaseMouseCapture();
	}
	FReply Reply = ResolvePointerInputReply(bInteracting);
	return Reply;
}

FReply SH5UI_View::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bReceiveInput || !RuntimeView)
	{
		return FReply::Unhandled();
	}
	return ResolvePointerInputReply(RuntimeView->ProcessMouseWheel(
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MouseEvent));
}

void SH5UI_View::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// A captured pointer is allowed to travel outside the H5 view. Sending
	// ProcessMouseLeave here would terminate RmlUi's move stream even though
	// Slate still owns the gesture. Tick() keeps pumping the absolute cursor.
	if (RuntimeView && PressedMouseButtons.IsEmpty())
	{
		RuntimeView->ProcessMouseLeave();
	}
	SCompoundWidget::OnMouseLeave(MouseEvent);
}

void SH5UI_View::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	UE_LOG(
		LogH5UIPointerTrace,
		Warning,
		TEXT("[SISH5UI PointerTrace][Slate] capture-lost Sequence=%llu "
			"Moves=%llu Buttons=%d"),
		PointerSessionSequence,
		PointerMoveCount,
		PressedMouseButtons.Num());
	ReleaseNativePointerSession();
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FReply SH5UI_View::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bReceiveInput || !RuntimeView ||
		(bPassThroughKeyboardWithoutEditableFocus && !RuntimeView->HasEditableFocus()))
	{
		return FReply::Unhandled();
	}
	return ResolveInputReply(RuntimeView->ProcessKeyDown(InKeyEvent));
}

FReply SH5UI_View::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bReceiveInput || !RuntimeView ||
		(bPassThroughKeyboardWithoutEditableFocus && !RuntimeView->HasEditableFocus()))
	{
		return FReply::Unhandled();
	}
	return ResolveInputReply(RuntimeView->ProcessKeyUp(InKeyEvent));
}

FReply SH5UI_View::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (!bReceiveInput || !RuntimeView ||
		(bPassThroughKeyboardWithoutEditableFocus && !RuntimeView->HasEditableFocus()))
	{
		return FReply::Unhandled();
	}
	return ResolveInputReply(RuntimeView->ProcessKeyChar(InCharacterEvent));
}

FReply SH5UI_View::ResolveInputReply(bool bInteracting) const
{
	return bConsumeInput || bInteracting ? FReply::Handled() : FReply::Unhandled();
}

FReply SH5UI_View::ResolvePointerInputReply(bool bInteracting) const
{
	return bPassThroughTransparentMouseInput && !bInteracting
		? FReply::Unhandled()
		: ResolveInputReply(bInteracting);
}

void SH5UI_View::RememberPointerPosition(
	const FGeometry& Geometry,
	const FVector2D& ScreenPosition)
{
	LastPointerScreenPosition = ScreenPosition;
	LastPointerLocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
	bHasPointerPosition = true;
}

void SH5UI_View::PumpCapturedPointer(const FGeometry& Geometry)
{
	if (!RuntimeView || PressedMouseButtons.IsEmpty() ||
		!FSlateApplication::IsInitialized())
	{
		return;
	}

	const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
	if (bHasPointerPosition &&
		CursorPosition.Equals(LastPointerScreenPosition, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	RememberPointerPosition(Geometry, CursorPosition);
	TracePointerMove(TEXT("tick-pump"));
	RuntimeView->ProcessCapturedMouseMove(LastPointerLocalPosition);
}

void SH5UI_View::TracePointerMove(const TCHAR* Source)
{
	if (PressedMouseButtons.IsEmpty())
	{
		return;
	}
	++PointerMoveCount;
	if (PointerMoveCount <= 3 || PointerMoveCount % 15 == 0)
	{
		UE_LOG(
			LogH5UIPointerTrace,
			Warning,
			TEXT("[SISH5UI PointerTrace][Slate] move Sequence=%llu "
				"Count=%llu Source=%s Screen=(%.1f,%.1f) Local=(%.1f,%.1f)"),
			PointerSessionSequence,
			PointerMoveCount,
			Source,
			LastPointerScreenPosition.X,
			LastPointerScreenPosition.Y,
			LastPointerLocalPosition.X,
			LastPointerLocalPosition.Y);
	}
}

void SH5UI_View::ReleaseNativePointerSession()
{
	if (!RuntimeView || PressedMouseButtons.IsEmpty())
	{
		PressedMouseButtons.Reset();
		return;
	}

	const TSet<int32> ButtonsToRelease = MoveTemp(PressedMouseButtons);
	PressedMouseButtons.Reset();
	for (const int32 Button : ButtonsToRelease)
	{
		RuntimeView->ReleaseCapturedMouseButton(
			LastPointerLocalPosition,
			Button);
	}
}

void SH5UI_View::RefreshBrowserSubview()
{
	FH5UI_BrowserSubview Subview;
	if (!RuntimeView || !RuntimeView->GetBrowserSubview(Subview))
	{
		DestroyBrowserSubview();
		return;
	}

	const FString BrowserSource = ResolveBrowserSource(Subview.Source);
	if (BrowserSource.IsEmpty())
	{
		DestroyBrowserSubview();
		return;
	}

	if (!BrowserPosition.Equals(Subview.Position, 0.5) || !BrowserSize.Equals(Subview.Size, 0.5))
	{
		BrowserPosition = Subview.Position;
		BrowserSize = Subview.Size;
		if (BrowserCanvas)
		{
			BrowserCanvas->Invalidate(EInvalidateWidgetReason::Layout);
		}
	}

	if (BrowserWidget && BrowserSource == LoadedBrowserSource)
	{
		return;
	}

	DestroyBrowserSubview();
	if (!BrowserCanvas)
	{
		return;
	}

	UH5UI_View* Owner = OwnerWidget.Get();
	BrowserBridge = TStrongObjectPtr<UH5UI_BrowserBridge>(NewObject<UH5UI_BrowserBridge>(
		Owner ? static_cast<UObject*>(Owner) : GetTransientPackage()));
	BrowserBridge->Initialize(Owner);

	IH5UI_BrowserModule* BrowserModule = FModuleManager::LoadModulePtr<IH5UI_BrowserModule>(TEXT("H5UIPluginCEF"));
	if (!BrowserModule)
	{
		UE_LOG(LogH5UIBrowserSubview, Warning, TEXT("H5UIPluginCEF is unavailable on this platform."));
		BrowserBridge.Reset();
		return;
	}

	TSharedRef<SWidget> NewBrowser = BrowserModule->CreateBrowserWidget(BrowserSource, BrowserBridge.Get(), 30);

	BrowserWidget = NewBrowser;
	BrowserCanvas->AddSlot()
		.Offset(TAttribute<FMargin>::CreateSP(this, &SH5UI_View::GetBrowserOffset))
		.Anchors(FAnchors(0.0f, 0.0f))
		.Alignment(FVector2D::ZeroVector)
		.AutoSize(false)
		.ZOrder(1.0f)
	[
		NewBrowser
	];

	LoadedBrowserSource = BrowserSource;
	UE_LOG(LogH5UIBrowserSubview, Log, TEXT("Opened H5UI iframe: %s"), *BrowserSource);
}

void SH5UI_View::DestroyBrowserSubview()
{
	if (BrowserCanvas && BrowserWidget)
	{
		BrowserCanvas->RemoveSlot(BrowserWidget.ToSharedRef());
	}
	BrowserWidget.Reset();
	BrowserBridge.Reset();
	LoadedBrowserSource.Reset();
}

FMargin SH5UI_View::GetBrowserOffset() const
{
	return FMargin(BrowserPosition.X, BrowserPosition.Y, BrowserSize.X, BrowserSize.Y);
}

FString SH5UI_View::ResolveBrowserSource(const FString& Source) const
{
	if (Source.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) ||
		Source.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) ||
		Source.StartsWith(TEXT("about:"), ESearchCase::IgnoreCase))
	{
		return Source;
	}

	if (!FH5UI_Module::IsAvailable())
	{
		return FString();
	}

	FString LocalPath = FH5UI_Module::Get().GetFileInterface().ResolvePath(Source);
	if (LocalPath.IsEmpty() || !FPaths::FileExists(LocalPath))
	{
		UE_LOG(LogH5UIBrowserSubview, Warning, TEXT("Blocked or missing H5UI iframe source: %s"), *Source);
		return FString();
	}

	FPaths::NormalizeFilename(LocalPath);
	LocalPath.ReplaceInline(TEXT(" "), TEXT("%20"));
	LocalPath.ReplaceInline(TEXT("#"), TEXT("%23"));
	return FString(TEXT("file:///")) + LocalPath;
}
