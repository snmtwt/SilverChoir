#include "UIBasic/SSilverChoirToolTip.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/BasicButtonWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SSilverChoirToolTip::Construct(const FArguments& InArgs)
{
    Owner = InArgs._Owner;
    Source = InArgs._Source;
    Closed = InArgs._OnClosed;

    SAssignNew(TextBlock, STextBlock)
        .Text(this, &SSilverChoirToolTip::GetDisplayedText)
        .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);

    // Custom content avoids SToolTip's hard-coded bright background and black text.
    SToolTip::Construct(SToolTip::FArguments()
        .TextMargin(FMargin(0.f))
        .BorderImage(nullptr)
        .IsInteractive(false)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SBorder)
                .BorderImage(&BackgroundBrush)
                .Padding(FMargin(11.f, 8.f))
                [
                    TextBlock.ToSharedRef()
                ]
            ]
            + SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Top)
            .Padding(FMargin(4.f, 10.f, 0.f, 0.f))
            [
                SNew(SBox)
                .WidthOverride(2.f)
                .HeightOverride(14.f)
                [
                    SNew(SImage).Image(&AccentBrush)
                ]
            ]
        ]);

    ApplyStyle();
}

void SSilverChoirToolTip::UpdateSource(TSharedPtr<SToolTip> InSource)
{
    if (Source == InSource) return;
    Source = MoveTemp(InSource);
    // Keep this tooltip's identity stable while text and native bindings change.
    TextBlock->Invalidate(EInvalidateWidgetReason::Layout);
    Invalidate(EInvalidateWidgetReason::Layout);
}

FText SSilverChoirToolTip::GetDisplayedText() const
{
    return Owner.IsValid() && Source ? Source->GetTextTooltip() : FText::GetEmpty();
}

bool SSilverChoirToolTip::IsEmpty() const
{
    // SToolTip itself treats any custom content as nonempty, even with empty text.
    return !Owner.IsValid() || !Source || Source->IsEmpty();
}

void SSilverChoirToolTip::OnOpening()
{
    SToolTip::OnOpening();
    ApplyStyle();
}

void SSilverChoirToolTip::ApplyStyle()
{
    const UBasicButtonWidget* Button = Owner.Get();
    const FGameToolTipStyle Style = Button ? Button->ToolTipStyle : FGameToolTipStyle();
    const float FontSize = FMath::IsFinite(Style.FontSize)
        ? FMath::Clamp(Style.FontSize, 8.f, 32.f) : 12.f;
    const float MaxWidth = FMath::IsFinite(Style.MaxWidth)
        ? FMath::Clamp(Style.MaxWidth, 96.f, 960.f) : 320.f;

    FSlateFontInfo TextFont = Button ? Button->Font : FCoreStyle::GetDefaultFontStyle("Regular", 12);
    if (!TextFont.HasValidFont()) TextFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
    TextFont.Size = FontSize;

    BackgroundBrush = FSlateRoundedBoxBrush(Style.BackgroundColor, 3.f, Style.BorderColor, 1.f);
    AccentBrush = FSlateRoundedBoxBrush(Style.AccentColor, 1.f);
    TextBlock->SetFont(TextFont);
    TextBlock->SetColorAndOpacity(Style.TextColor);
    TextBlock->SetWrapTextAt(MaxWidth - 22.f);
    Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}
