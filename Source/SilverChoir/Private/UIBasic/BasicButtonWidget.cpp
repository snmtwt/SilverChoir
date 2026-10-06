#include "UIBasic/BasicButtonWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "CoreGlobals.h"
#include "Engine/Font.h"
#include "UObject/ConstructorHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/DelayedButtonPress.h"
#include "UIBasic/PixelAlignedButtonFrame.h"
#include "UIBasic/SSilverChoirToolTip.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SSpacer.h"

/** Nonempty sentinel claims the tooltip path without opening a native window. */
class SBasicButtonWaitingToolTip : public SToolTip
{
public:
    SLATE_BEGIN_ARGS(SBasicButtonWaitingToolTip) {}
        SLATE_EVENT(FSimpleDelegate, OnClosed)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Closed = Args._OnClosed;
        SToolTip::Construct(SToolTip::FArguments().IsInteractive(false)[SNew(SSpacer)]);
    }
    virtual void OnClosed() override { SToolTip::OnClosed(); Closed.ExecuteIfBound(); }
private:
    FSimpleDelegate Closed;
};

/** Paint below the entire Designer tree. UUserWidget::NativePaint runs AFTER its children. */
class SBasicButtonFrame : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBasicButtonFrame) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UBasicButtonWidget>, Owner)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Owner=Args._Owner; ChildSlot[Args._Content.Widget]; }
    virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        SCompoundWidget::OnMouseEnter(Geometry,Event);
        bWaitingUnderPointer = true;
        LastPointerPosition = Event.GetScreenSpacePosition();
        LastPointerMotionTime = FPlatformTime::Seconds();
    }
    virtual void OnMouseLeave(const FPointerEvent& Event) override
    {
        bWaitingUnderPointer = false;
        LastPointerMotionTime = 0.;
        SCompoundWidget::OnMouseLeave(Event);
    }
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!Event.GetScreenSpacePosition().Equals(LastPointerPosition,.01f))
        {
            LastPointerPosition = Event.GetScreenSpacePosition();
            LastPointerMotionTime = FPlatformTime::Seconds();
        }
        return SCompoundWidget::OnMouseMove(Geometry,Event);
    }
    virtual bool OnVisualizeTooltip(const TSharedPtr<SWidget>& Content) override
    {
        // Claim the pending tooltip without opening any window. Returning an
        // empty tooltip instead would leak the outer UMG white tooltip through.
        if (!Content || (WaitingToolTip && Content == WaitingToolTip->AsWidget())) return true;
        return SCompoundWidget::OnVisualizeTooltip(Content);
    }
    virtual TSharedPtr<IToolTip> GetToolTip() override
    {
        // UMG's setters target the outer SObjectWidget. Intercept only its plain
        // text tooltip here, inside that wrapper, so future setters keep working.
        if (!Owner.IsValid() || !Owner->bUseGameToolTipStyle || Owner->GetToolTip()
            || Owner->ToolTipWidgetDelegate.IsBound()) return SCompoundWidget::GetToolTip();
        const auto Outer = Owner->GetCachedWidget();
        if (!Outer.IsValid() || Outer.Get() == this) return SCompoundWidget::GetToolTip();
        const auto Original = Outer->GetToolTip();
        if (!Original.IsValid() || Original->IsEmpty()) return SCompoundWidget::GetToolTip();
        const auto OriginalWidget = Original->AsWidget();
        if (OriginalWidget->GetType() != FName(TEXT("SToolTip"))) return SCompoundWidget::GetToolTip();
        const auto TextToolTip = StaticCastSharedRef<SToolTip>(OriginalWidget);
        if (TextToolTip->GetTextTooltip().IsEmpty()) return SCompoundWidget::GetToolTip();
        if (!GameToolTip.IsValid())
            GameToolTip = SNew(SSilverChoirToolTip).Owner(Owner).Source(TextToolTip)
                .OnClosed(this,&SBasicButtonFrame::HandleToolTipClosed,false);
        else GameToolTip->UpdateSource(TextToolTip);
        const float RequestedDelay = Owner->ToolTipStyle.HoverDelaySeconds;
        const double Delay = FMath::IsFinite(RequestedDelay) ? FMath::Clamp(RequestedDelay,0.f,10.f) : 1.5;
        if (Delay > 0.)
        {
            const double Now = FPlatformTime::Seconds();
            const FVector2D Position = FSlateApplication::Get().GetCursorPos();
            // Tooltip queries include disabled buttons, whose mouse enter/move
            // events are not routed. Begin their dwell here as well.
            if (!bWaitingUnderPointer || !Position.Equals(LastPointerPosition,.01f))
            {
                bWaitingUnderPointer = true;
                LastPointerPosition = Position;
                LastPointerMotionTime = Now;
            }
            if (Now-LastPointerMotionTime < Delay)
            {
                if (!WaitingToolTip)
                    WaitingToolTip = SNew(SBasicButtonWaitingToolTip)
                        .OnClosed(this,&SBasicButtonFrame::HandleToolTipClosed,true);
                bReturningWaitingToolTip = true;
                return WaitingToolTip;
            }
        }
        bReturningWaitingToolTip = false;
        return GameToolTip;
    }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const int32 ContentLayer=Owner.IsValid()?Owner->PaintButtonFrame(Geometry,CullingRect,Elements,LayerId,Style,bParentEnabled):LayerId;
        return SCompoundWidget::OnPaint(Args,Geometry,CullingRect,Elements,ContentLayer,Style,bParentEnabled);
    }
private:
    void HandleToolTipClosed(bool bWasWaiting)
    {
        // Slate closes the old tooltip when switching sentinel <-> real. Only
        // reset when the currently returned tooltip itself loses its path.
        if (bWasWaiting == bReturningWaitingToolTip)
        {
            bWaitingUnderPointer = false;
            LastPointerMotionTime = 0.;
        }
    }
    TWeakObjectPtr<UBasicButtonWidget> Owner;
    TSharedPtr<SSilverChoirToolTip> GameToolTip;
    TSharedPtr<SToolTip> WaitingToolTip;
    FVector2D LastPointerPosition = FVector2D::ZeroVector;
    double LastPointerMotionTime = 0.;
    bool bWaitingUnderPointer = false;
    bool bReturningWaitingToolTip = false;
};

UBasicButtonWidget::UBasicButtonWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), Press(MakePimpl<FDelayedButtonPress>())
{
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::Visible);
	ButtonText = NSLOCTEXT("SilverChoirUI", "Button", "按钮");
	static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	Font = FSlateFontInfo(DefaultFont.Object, 20, TEXT("Regular"));
	BackgroundColor = FLinearColor(FColor::FromHex(TEXT("06101A")));
	BorderColor = FLinearColor(FColor::FromHex(TEXT("173E59")));
	AccentColor = FLinearColor(FColor::FromHex(TEXT("3B9BD3")));
	ButtonForegroundColor = FLinearColor(FColor::FromHex(TEXT("9CB4C5")));
}

UBasicButtonWidget::~UBasicButtonWidget() = default;

TSharedRef<SWidget> UBasicButtonWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
		WidgetTree->RootWidget = ButtonSize;
		UBorder* ContentBorder = WidgetTree->ConstructWidget<UBorder>();
		ContentBorder->SetBrushColor(FLinearColor::Transparent);
		ContentBorder->SetPadding(FMargin(22.0f, 10.0f));
		ContentBorder->SetHorizontalAlignment(HAlign_Center);
		ContentBorder->SetVerticalAlignment(VAlign_Center);
		// All visual children are non-interactive. The user widget owns pointer capture and focus.
		ButtonSize->SetVisibility(ESlateVisibility::HitTestInvisible);
		ButtonSize->AddChild(ContentBorder);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		ContentBorder->AddChild(Row);
		IconSizeBox = WidgetTree->ConstructWidget<USizeBox>();
		IconImage = WidgetTree->ConstructWidget<UImage>();
		IconSizeBox->AddChild(IconImage);
		IconSlot = Row->AddChildToHorizontalBox(IconSizeBox);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		Label = WidgetTree->ConstructWidget<UTextBlock>();
		Row->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
	}
    return SNew(SBasicButtonFrame).Owner(this)[Super::RebuildWidget()];
}

void UBasicButtonWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyContent();
}

void UBasicButtonWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ApplyContent();
}

void UBasicButtonWidget::ApplyContent()
{
	if (ButtonSize)
	{
		ButtonSize->SetMinDesiredWidth(FMath::Max(0.0, MinimumSize.X));
		ButtonSize->SetMinDesiredHeight(FMath::Max(0.0, MinimumSize.Y));
	}
	if (Label)
	{
		Label->SetText(ButtonText);
		Label->SetFont(Font);
		Label->SetColorAndOpacity(ButtonForegroundColor);
		Label->SetVisibility(ContentMode == EBasicButtonContent::IconOnly ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IconImage)
	{
		IconImage->SetBrush(IconBrush);
		IconImage->SetColorAndOpacity(ButtonForegroundColor);
		IconImage->SetVisibility(ContentMode == EBasicButtonContent::TextOnly ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	IconSlot = IconSizeBox ? Cast<UHorizontalBoxSlot>(IconSizeBox->Slot) : nullptr;
	if (IconSizeBox)
	{
		IconSizeBox->SetWidthOverride(FMath::Max(1.0, IconSize.X));
		IconSizeBox->SetHeightOverride(FMath::Max(1.0, IconSize.Y));
		IconSizeBox->SetVisibility(ContentMode == EBasicButtonContent::TextOnly ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IconSlot) { IconSlot->SetPadding(FMargin(0.0f, 0.0f, ContentMode == EBasicButtonContent::IconAndText ? 12.0f : 0.0f, 0.0f)); }
	if (DetailLabel)
	{
		DetailLabel->SetText(ButtonSubtitle);
		DetailLabel->SetVisibility(ButtonSubtitle.IsEmpty() || ContentMode == EBasicButtonContent::IconOnly
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IndexLabel)
	{
		IndexLabel->SetText(ButtonIndex);
		IndexLabel->SetVisibility(ButtonIndex.IsEmpty() || ContentMode == EBasicButtonContent::IconOnly
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UBasicButtonWidget::SetButtonText(const FText& InText) { ButtonText = InText; ApplyContent(); }
void UBasicButtonWidget::SetIconBrush(const FSlateBrush& InBrush) { IconBrush = InBrush; ApplyContent(); }
void UBasicButtonWidget::SetContentMode(EBasicButtonContent InMode) { ContentMode = InMode; ApplyContent(); }
void UBasicButtonWidget::SetSelected(bool bInSelected)
{
    if (bSelected == bInSelected) { return; }
    bSelected = bInSelected;
    if (const auto Widget = GetCachedWidget()) { Widget->Invalidate(EInvalidateWidgetReason::Paint); }
}
bool UBasicButtonWidget::IsPressPending() const { return Press->bActive; }
bool UBasicButtonWidget::IsButtonVisuallyPressed() const { return Press->bPressedVisual; }

bool UBasicButtonWidget::IsInputAllowed() const
{
	if (!GetIsEnabled() || GetVisibility() != ESlateVisibility::Visible) { return false; }
	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget) { return false; }
	for (; SlateWidget; SlateWidget = SlateWidget->GetParentWidget())
	{
		if (!SlateWidget->IsEnabled() || !SlateWidget->GetVisibility().IsVisible()) { return false; }
	}
	return true;
}

bool UBasicButtonWidget::BeginPress(const FKey& Key)
{
	if (!IsInputAllowed() || !Press->Begin(GFrameCounter)) { return false; }
	ActiveKey = Key;
	if (PressSound && GetWorld()) { UGameplayStatics::PlaySound2D(this, PressSound); }
	OnPressStarted.Broadcast();
	return Press->bActive && IsInputAllowed();
}

void UBasicButtonWidget::CancelPendingClick()
{
	// Once cancelled, disabled/hidden buttons need no further Slate capture queries.
	// Keep ActiveKey in this check: releasing outside can clear Press before capture cleanup.
	if (!Press->bActive && !ActiveKey.IsValid()) { return; }
	Press->Cancel();
	ActiveKey = FKey();
	if (FSlateApplication::IsInitialized())
	{
#if WITH_DEV_AUTOMATION_TESTS
		++CaptureQueryCount;
#endif
		const TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetUser(ActivePointerUser);
		if (User && GetCachedWidget() && User->DoesWidgetHaveCapture(GetCachedWidget(), ActivePointerIndex))
		{
			User->ReleaseCapture(ActivePointerIndex);
		}
	}
}

void UBasicButtonWidget::SetIsEnabled(bool bInIsEnabled)
{
	if (!bInIsEnabled) { CancelPendingClick(); bPointerHovered = false; }
	Super::SetIsEnabled(bInIsEnabled);
}

void UBasicButtonWidget::SetVisibility(ESlateVisibility InVisibility)
{
	if (InVisibility != ESlateVisibility::Visible) { CancelPendingClick(); bPointerHovered = false; }
	Super::SetVisibility(InVisibility);
}

void UBasicButtonWidget::NativeDestruct()
{
	CancelPendingClick();
	bPointerHovered = false;
	HoverAmount = 0.0f;
	Super::NativeDestruct();
}

void UBasicButtonWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const bool bEnabled = IsInputAllowed();
	if (!bEnabled) { CancelPendingClick(); }
	const bool bClick = bEnabled && Press->Advance(GFrameCounter);
	const float PreviousHoverAmount = HoverAmount;
	HoverAmount = FMath::FInterpTo(HoverAmount,
		bEnabled && (bPointerHovered || HasKeyboardFocus() || bSelected) ? 1.0f : 0.0f, InDeltaTime, 10.0f);
    // The custom frame is outside the Designer tree. Updating a label alone does
    // not invalidate its cached paint under Slate invalidation.
    if (PreviousHoverAmount != HoverAmount || Press->bActive || bClick)
    {
        if (const auto Widget = GetCachedWidget()) { Widget->Invalidate(EInvalidateWidgetReason::Paint); }
    }
	const FLinearColor Tint = GetButtonContentTint(bEnabled);
	// Compare the live widgets instead of caching Tint: Blueprint synchronization or
	// a direct child style edit must still be reflected on the following tick.
	if (Label && Label->GetColorAndOpacity() != FSlateColor(Tint))
	{
		Label->SetColorAndOpacity(Tint);
#if WITH_DEV_AUTOMATION_TESTS
		++FeedbackColorWriteCount;
#endif
	}
	if (IconImage && IconImage->GetColorAndOpacity() != Tint)
	{
		IconImage->SetColorAndOpacity(Tint);
#if WITH_DEV_AUTOMATION_TESTS
		++FeedbackColorWriteCount;
#endif
	}
	if (bClick)
	{
		ActiveKey = FKey();
		OnClicked.Broadcast(); // Last operation: a listener may remove this widget.
	}
}

float UBasicButtonWidget::GetButtonVisualStrength() const { return Press->bPressedVisual?1.f:HoverAmount; }
FLinearColor UBasicButtonWidget::GetButtonContentTint(bool bEnabled) const
{
	return !bEnabled ? ButtonForegroundColor * FLinearColor(0.4f, 0.4f, 0.4f, 1.0f)
		: FMath::Lerp(ButtonForegroundColor, FLinearColor::White, Press->bPressedVisual ? 1.0f : HoverAmount);
}
int32 UBasicButtonWidget::PaintButtonFrame(const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FPixelAlignedButtonFrame Frame = FPixelAlignedButtonFrame::Make(Geometry, BorderThickness);
	const FVector2f Size = Frame.Max - Frame.Min;
	const FVector2f Stroke = Frame.Thickness;
	const bool bEnabled = bParentEnabled && GetIsEnabled();
	const float Active = Press->bPressedVisual ? 1.0f : HoverAmount;
	const FLinearColor Tint = Style.GetColorAndOpacityTint()
		* FLinearColor(1.0f, 1.0f, 1.0f, bEnabled ? 1.0f : 0.4f);
	const FLinearColor Edge = FMath::Lerp(BorderColor, AccentColor, Active);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	auto Box = [&](int32 Layer, FVector2f Position, FVector2f Extent, const FLinearColor& Color)
	{
		if (Extent.X > 0.0f && Extent.Y > 0.0f)
		{
			FSlateDrawElement::MakeBox(Elements, Layer,
				Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)), White,
				ESlateDrawEffect::NoPixelSnapping, Color * Tint);
		}
	};
	Box(LayerId, Frame.Min, Size, BackgroundColor);
	Box(LayerId + 1, Frame.Min + Stroke, FVector2f((Size.X - 2.0f * Stroke.X) * Active, Size.Y - 2.0f * Stroke.Y),
		FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, Press->bPressedVisual ? 0.32f : 0.10f));
	Box(LayerId + 2, Frame.Min, FVector2f(Size.X, Stroke.Y), Edge);
	Box(LayerId + 2, FVector2f(Frame.Min.X, Frame.Max.Y - Stroke.Y), FVector2f(Size.X, Stroke.Y), Edge);
	Box(LayerId + 2, Frame.Min, FVector2f(Stroke.X, Size.Y), Edge);
	Box(LayerId + 2, FVector2f(Frame.Max.X - Stroke.X, Frame.Min.Y), FVector2f(Stroke.X, Size.Y), Edge);
	// The inner accent rail is separate from the uniform outer frame.
	Box(LayerId + 2, Frame.Min + Stroke, FVector2f(Stroke.X * (2.0f + FMath::RoundToFloat(Active * 3.0f)), Size.Y - 2.0f * Stroke.Y), Edge);
    return LayerId + 3;
}

void UBasicButtonWidget::NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Super::NativeOnMouseEnter(Geometry, Event);
	if (IsInputAllowed() && !bPointerHovered)
	{
		bPointerHovered = true;
		if (HoverSound && GetWorld()) { UGameplayStatics::PlaySound2D(this, HoverSound); }
		OnHovered.Broadcast(); // Last operation: a listener may remove this widget.
	}
}

void UBasicButtonWidget::NativeOnMouseLeave(const FPointerEvent& Event)
{
	Super::NativeOnMouseLeave(Event);
	bPointerHovered = false;
	OnUnhovered.Broadcast();
}

FReply UBasicButtonWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	if (Press->bActive) { return FReply::Handled(); }
	ActivePointerUser = Event.GetUserIndex();
	ActivePointerIndex = Event.GetPointerIndex();
	if (!BeginPress(EKeys::LeftMouseButton)) { return FReply::Handled(); }
	return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply UBasicButtonWidget::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return NativeOnMouseButtonDown(Geometry, Event);
}

FReply UBasicButtonWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || ActiveKey != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	Press->Release(IsInputAllowed() && Geometry.IsUnderLocation(Event.GetScreenSpacePosition()));
	return FReply::Handled().ReleaseMouseCapture();
}

void UBasicButtonWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if (ActiveKey == EKeys::LeftMouseButton && !Press->bReleased) { CancelPendingClick(); }
	Super::NativeOnMouseCaptureLost(Event);
}

FReply UBasicButtonWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Escape) { CancelPendingClick(); return FReply::Handled(); }
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!Event.IsRepeat()) { BeginPress(Key); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}

FReply UBasicButtonWidget::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == ActiveKey)
	{
		Press->Release(IsInputAllowed());
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(Geometry, Event);
}

void UBasicButtonWidget::NativeOnFocusLost(const FFocusEvent& Event)
{
	CancelPendingClick();
	Super::NativeOnFocusLost(Event);
}
