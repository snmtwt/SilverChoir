#include "UIBasic/RaisedImageButtonWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/PixelAlignedButtonFrame.h"

namespace
{
	float SafeValue(float Value, float Fallback, float Min, float Max)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : Fallback, Min, Max);
	}

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}
}

URaisedImageButtonWidget::URaisedImageButtonWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ContentMode = EBasicButtonContent::IconOnly;
	ButtonText = FText::GetEmpty();
	MinimumSize = FVector2D(44.0, 44.0);
	IconSize = FVector2D(20.0, 20.0);
	IconBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	BackgroundColor = FLinearColor::Transparent;
	BorderColor = FLinearColor(FColor::FromHex(TEXT("507D94")));
	AccentColor = FLinearColor(FColor::FromHex(TEXT("24DDEB")));
	ButtonForegroundColor = NormalIconColor;
}

TSharedRef<SWidget> URaisedImageButtonWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
		WidgetTree->RootWidget = ButtonSize;
		ButtonSize->SetVisibility(ESlateVisibility::HitTestInvisible);
		FaceContent = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FaceContent"));
		FaceContent->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBoxSlot* FaceSlot = CastChecked<USizeBoxSlot>(ButtonSize->AddChild(FaceContent));
		FaceSlot->SetHorizontalAlignment(HAlign_Fill);
		FaceSlot->SetVerticalAlignment(VAlign_Fill);
		IconSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconSizeBox"));
		IconSizeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* CenteredIconSlot = FaceContent->AddChildToOverlay(IconSizeBox);
		CenteredIconSlot->SetHorizontalAlignment(HAlign_Center);
		CenteredIconSlot->SetVerticalAlignment(VAlign_Center);
		IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconSizeBox->AddChild(IconImage);
	}
	return Super::RebuildWidget();
}

void URaisedImageButtonWidget::SetButtonImage(UTexture2D* Texture)
{
	FSlateBrush ImageBrush = IconBrush;
	ImageBrush.SetResourceObject(Texture);
	ImageBrush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	ImageBrush.ImageSize = Texture ? FVector2D(Texture->GetSizeX(), Texture->GetSizeY()) : FVector2D::ZeroVector;
	ImageBrush.TintColor = FSlateColor(FLinearColor::White);
	SetIconBrush(ImageBrush);
	ApplyFlatContent();
}

void URaisedImageButtonWidget::SetIconTintColors(FLinearColor Normal, FLinearColor Hovered,
	FLinearColor Selected, FLinearColor Pressed)
{
	NormalIconColor = Normal;
	HoverIconColor = Hovered;
	SelectedIconColor = Selected;
	PressedIconColor = Pressed;
	ApplyFlatContent();
}

void URaisedImageButtonWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ApplyFlatContent();
}

void URaisedImageButtonWidget::SetIsEnabled(bool bInIsEnabled)
{
	if (!bInIsEnabled) { bPointerInside = false; bScanVisible = false; HoverScanPhase = 0.0f; }
	Super::SetIsEnabled(bInIsEnabled);
	ApplyFlatContent();
}

void URaisedImageButtonWidget::SetVisibility(ESlateVisibility InVisibility)
{
	if (InVisibility != ESlateVisibility::Visible)
	{
		bPointerInside = false;
		bScanVisible = false;
		HoverScanPhase = 0.0f;
	}
	Super::SetVisibility(InVisibility);
	ApplyFlatContent();
}

void URaisedImageButtonWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyFlatContent();
}

void URaisedImageButtonWidget::NativeConstruct()
{
	bVisualsActive = true;
	bPointerInside = false;
	bScanVisible = false;
	HoverScanPhase = 0.0f;
	LastFrameState = 255;
	const uint32 ConstructGeneration = ++VisualGeneration;
	const TWeakObjectPtr<URaisedImageButtonWidget> WeakThis(this);
	Super::NativeConstruct();
	if (WeakThis.IsValid() && bVisualsActive && VisualGeneration == ConstructGeneration)
	{
		ApplyFlatContent();
	}
}

void URaisedImageButtonWidget::NativeDestruct()
{
	bVisualsActive = false;
	bPointerInside = false;
	bScanVisible = false;
	HoverScanPhase = 0.0f;
	++VisualGeneration;
	Super::NativeDestruct();
}

void URaisedImageButtonWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	const TWeakObjectPtr<URaisedImageButtonWidget> WeakThis(this);
	const uint32 TickGeneration = VisualGeneration;
	// Input and sound timing remain entirely in the shared button implementation.
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!WeakThis.IsValid() || !bVisualsActive || VisualGeneration != TickGeneration) { return; }
	const bool bPreviouslyScanning = bScanVisible;
	bScanVisible = bEnableHoverScan && GetIsEnabled() && !IsPreviewDisabled()
		&& GetVisibility() == ESlateVisibility::Visible && bPointerInside;
	if (bScanVisible)
	{
		const float Delta = SafeValue(InDeltaTime, 0.0f, 0.0f, 1.0f);
		const float Duration = SafeValue(ScanDuration, 1.8f, 0.25f, 10.0f);
		HoverScanPhase = FMath::Fmod(HoverScanPhase + Delta / Duration, 1.0f);
		if (Delta > 0.0f) { InvalidateFrame(); }
	}
	else
	{
		HoverScanPhase = 0.0f;
	}
	if (bPreviouslyScanning != bScanVisible) { InvalidateFrame(); }
	// Super already applied the correct content tint (including disabled ancestors).
	ApplyFlatContent(false);
}

void URaisedImageButtonWidget::NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	bPointerInside = GetIsEnabled() && GetVisibility() == ESlateVisibility::Visible;
	HoverScanPhase = 0.0f;
	bScanVisible = bPointerInside && bEnableHoverScan;
	InvalidateFrame();
	// OnHovered may remove/rebuild the widget, so no access after dispatch.
	Super::NativeOnMouseEnter(Geometry, Event);
}

void URaisedImageButtonWidget::NativeOnMouseLeave(const FPointerEvent& Event)
{
	bPointerInside = false;
	bScanVisible = false;
	HoverScanPhase = 0.0f;
	InvalidateFrame();
	Super::NativeOnMouseLeave(Event);
}

bool URaisedImageButtonWidget::IsPreviewDisabled() const
{
	return IsDesignTime() && DesignerPreviewState == ERaisedImageButtonPreviewState::Disabled;
}

bool URaisedImageButtonWidget::IsFacePressed() const
{
	if (!GetIsEnabled() || IsPreviewDisabled()) { return false; }
	return IsDesignTime() ? DesignerPreviewState == ERaisedImageButtonPreviewState::Pressed
		|| DesignerPreviewState == ERaisedImageButtonPreviewState::SelectedPressed : IsButtonVisuallyPressed();
}

bool URaisedImageButtonWidget::IsFaceSelected() const
{
	return IsDesignTime() ? DesignerPreviewState == ERaisedImageButtonPreviewState::Selected
		|| DesignerPreviewState == ERaisedImageButtonPreviewState::SelectedPressed : IsButtonSelected();
}

bool URaisedImageButtonWidget::IsFaceHovered() const
{
	if (!GetIsEnabled() || IsPreviewDisabled()) { return false; }
	return IsDesignTime() ? DesignerPreviewState == ERaisedImageButtonPreviewState::Hovered
		: bPointerInside || HasKeyboardFocus();
}

FLinearColor URaisedImageButtonWidget::GetButtonContentTint(bool bEnabled) const
{
	FLinearColor Tint = IsFaceSelected() ? SelectedIconColor : NormalIconColor;
	if (bEnabled && !IsPreviewDisabled())
	{
		if (IsFaceHovered()) { Tint = HoverIconColor; }
		if (IsFacePressed()) { Tint = PressedIconColor; }
	}
	else
	{
		Tint *= FLinearColor(0.4f, 0.4f, 0.4f, 0.65f);
	}
	return Tint;
}

void URaisedImageButtonWidget::InvalidateFrame() const
{
	if (const auto Widget = GetCachedWidget()) { Widget->Invalidate(EInvalidateWidgetReason::Paint); }
}

uint32 URaisedImageButtonWidget::GetFrameStyleHash() const
{
	uint32 Hash = GetTypeHash(BorderColor);
	Hash = HashCombineFast(Hash, GetTypeHash(AccentColor));
	Hash = HashCombineFast(Hash, GetTypeHash(BorderThickness));
	Hash = HashCombineFast(Hash, GetTypeHash(CornerLength));
	Hash = HashCombineFast(Hash, GetTypeHash(PressInset));
	Hash = HashCombineFast(Hash, GetTypeHash(SelectionFillOpacity));
	Hash = HashCombineFast(Hash, GetTypeHash(ScanOpacity));
	return HashCombineFast(Hash, GetTypeHash(bEnableHoverScan));
}

void URaisedImageButtonWidget::ApplyFlatContent(bool bRefreshTint)
{
	const bool bPressed = IsFacePressed();
	const uint8 FrameState = (GetIsEnabled() && !IsPreviewDisabled() ? 1 : 0)
		| (bPressed ? 2 : 0) | (IsFaceSelected() ? 4 : 0) | (IsFaceHovered() ? 8 : 0);
	const uint32 FrameStyleHash = GetFrameStyleHash();
	if (FrameStyleHash != LastFrameStyleHash || FrameState != LastFrameState)
	{
		LastFrameStyleHash = FrameStyleHash;
		LastFrameState = FrameState;
		InvalidateFrame();
	}
	// A fixed outer hit area and pure render scaling keep surrounding HUD layout stationary.
	if (FaceContent && !FaceContent->GetRenderTransform().Translation.IsNearlyZero())
	{
		FaceContent->SetRenderTranslation(FVector2D::ZeroVector);
	}
	if (IconSizeBox)
	{
		if (!IconSizeBox->GetRenderTransform().Translation.IsNearlyZero())
		{
			IconSizeBox->SetRenderTranslation(FVector2D::ZeroVector);
		}
		if (!IconSizeBox->GetRenderTransformPivot().Equals(FVector2D(0.5, 0.5)))
		{
			IconSizeBox->SetRenderTransformPivot(FVector2D(0.5, 0.5));
		}
		const float Scale = bPressed ? SafeValue(PressedIconScale, 0.9f, 0.6f, 1.0f) : 1.0f;
		if (!IconSizeBox->GetRenderTransform().Scale.Equals(FVector2D(Scale, Scale)))
		{
			IconSizeBox->SetRenderScale(FVector2D(Scale, Scale));
		}
		FVector2D FittedSize(SafeValue(IconSize.X, 20.0f, 1.0f, 4096.0f), SafeValue(IconSize.Y, 20.0f, 1.0f, 4096.0f));
		if (bPreserveImageAspectRatio)
		{
			FVector2D SourceSize(IconBrush.ImageSize);
			if (const UTexture2D* Texture = Cast<UTexture2D>(IconBrush.GetResourceObject()))
			{
				SourceSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			}
			if (FMath::IsFinite(SourceSize.X) && FMath::IsFinite(SourceSize.Y) && SourceSize.X > 0.0 && SourceSize.Y > 0.0)
			{
				FittedSize = SourceSize * FMath::Min(FittedSize.X / SourceSize.X, FittedSize.Y / SourceSize.Y);
			}
		}
		if (!IconSizeBox->IsWidthOverride() || !FMath::IsNearlyEqual(IconSizeBox->GetWidthOverride(), static_cast<float>(FittedSize.X)))
		{
			IconSizeBox->SetWidthOverride(FittedSize.X);
		}
		if (!IconSizeBox->IsHeightOverride() || !FMath::IsNearlyEqual(IconSizeBox->GetHeightOverride(), static_cast<float>(FittedSize.Y)))
		{
			IconSizeBox->SetHeightOverride(FittedSize.Y);
		}
	}
	if (bRefreshTint && IconImage)
	{
		const FLinearColor Tint = GetButtonContentTint(GetIsEnabled());
		if (IconImage->GetColorAndOpacity() != Tint) { IconImage->SetColorAndOpacity(Tint); }
	}
}

int32 URaisedImageButtonWidget::PaintButtonFrame(const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FPixelAlignedButtonFrame Frame = FPixelAlignedButtonFrame::Make(Geometry, SafeValue(BorderThickness, 1.0f, 1.0f, 8.0f));
	const FVector2f Size = Frame.Max - Frame.Min;
	if (Size.X <= 2.0f || Size.Y <= 2.0f) { return LayerId; }
	const bool bEnabled = bParentEnabled && GetIsEnabled() && !IsPreviewDisabled();
	const bool bPressed = bEnabled && IsFacePressed();
	const bool bFaceSelected = IsFaceSelected();
	const bool bHovered = bEnabled && IsFaceHovered();
	const FVector2f Stroke(FMath::Min(Frame.Thickness.X, Size.X * 0.1f), FMath::Min(Frame.Thickness.Y, Size.Y * 0.1f));
	const FLinearColor Tint = Style.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, bEnabled ? 1.0f : 0.4f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	auto Box = [&](int32 Layer, FVector2f Position, FVector2f Extent, const FLinearColor& Color)
	{
		if (Extent.X <= 0.0f || Extent.Y <= 0.0f || Color.A <= 0.0f) { return; }
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)),
			White, ESlateDrawEffect::NoPixelSnapping, Color * Tint);
	};

	// Selection has a persistent fill and marker. Hover/press never imitate that marker.
	if (bFaceSelected)
	{
		Box(LayerId, Frame.Min + Stroke, Size - 2.0f * Stroke,
			WithAlpha(AccentColor, SafeValue(SelectionFillOpacity, 0.08f, 0.0f, 1.0f)));
	}
	else if (bHovered)
	{
		Box(LayerId, Frame.Min + Stroke, Size - 2.0f * Stroke, WithAlpha(AccentColor, 0.025f));
	}

	// A restrained scan travels upward. Its short fading wake follows below it.
	const bool bDrawScan = bEnabled && bEnableHoverScan
		&& (IsDesignTime() ? DesignerPreviewState == ERaisedImageButtonPreviewState::Hovered : bScanVisible);
	if (bDrawScan)
	{
		const float Phase = IsDesignTime() ? 0.45f : HoverScanPhase;
		const float Bottom = Frame.Max.Y - 4.0f * Stroke.Y;
		const float Top = Frame.Min.Y + 3.0f * Stroke.Y;
		const float ScanY = FMath::Lerp(Bottom, Top, Phase);
		const float Fade = FMath::Clamp(Phase * 8.0f, 0.0f, 1.0f) * FMath::Clamp((1.0f - Phase) * 8.0f, 0.0f, 1.0f);
		const float Opacity = SafeValue(ScanOpacity, 0.24f, 0.0f, 1.0f) * Fade;
		const FVector2f ScanSize(FMath::Max(0.0f, Size.X - 6.0f * Stroke.X), Stroke.Y);
		for (int32 Trail = 3; Trail >= 0; --Trail)
		{
			const float Y = ScanY + Trail * Stroke.Y;
			if (Y + Stroke.Y <= Frame.Max.Y - 2.0f * Stroke.Y)
			{
				Box(LayerId + 1, FVector2f(Frame.Min.X + 3.0f * Stroke.X, Y), ScanSize,
					WithAlpha(AccentColor, Opacity * (Trail == 0 ? 1.0f : 0.24f / Trail)));
			}
		}
	}

	// Four open corner brackets: no full outline, bevel, physical cap, or displaced layout.
	const float Squeeze = bPressed ? FMath::Min(SafeValue(PressInset, 2.0f, 0.0f, 8.0f), FMath::Min(Size.X, Size.Y) * 0.15f) : 0.0f;
	const FVector2f CornerMin = Frame.Min + FVector2f(Squeeze, Squeeze);
	const FVector2f CornerMax = Frame.Max - FVector2f(Squeeze, Squeeze);
	const FVector2f CornerSize = CornerMax - CornerMin;
	const float Length = FMath::Min(SafeValue(CornerLength, 7.0f, 2.0f, 24.0f), FMath::Min(CornerSize.X, CornerSize.Y) * 0.35f);
	FLinearColor Edge = bFaceSelected || bHovered || bPressed ? AccentColor : BorderColor;
	if (bPressed) { Edge = FMath::Lerp(Edge, FLinearColor::White, 0.25f); }
	for (int32 SideY = 0; SideY < 2; ++SideY)
	{
		for (int32 SideX = 0; SideX < 2; ++SideX)
		{
			const float X = SideX == 0 ? CornerMin.X : CornerMax.X - Length;
			const float Y = SideY == 0 ? CornerMin.Y : CornerMax.Y - Stroke.Y;
			Box(LayerId + 2, FVector2f(X, Y), FVector2f(Length, Stroke.Y), Edge);
			Box(LayerId + 2, FVector2f(SideX == 0 ? CornerMin.X : CornerMax.X - Stroke.X,
				SideY == 0 ? CornerMin.Y + Stroke.Y : CornerMax.Y - Length), FVector2f(Stroke.X, Length - Stroke.Y), Edge);
		}
	}
	if (bFaceSelected)
	{
		const float MarkerWidth = FMath::Min(14.0f, Size.X * 0.34f);
		Box(LayerId + 3, FVector2f(Frame.Min.X + (Size.X - MarkerWidth) * 0.5f, Frame.Max.Y - 3.0f * Stroke.Y),
			FVector2f(MarkerWidth, 2.0f * Stroke.Y), AccentColor);
	}
	return LayerId + 4;
}
