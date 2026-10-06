#pragma once

#include "CoreMinimal.h"
#include "Widgets/SToolTip.h"

class UBasicButtonWidget;
class STextBlock;

/** Presentation for an existing text tooltip; the source retains UMG binding semantics. */
class SSilverChoirToolTip : public SToolTip
{
public:
    SLATE_BEGIN_ARGS(SSilverChoirToolTip) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UBasicButtonWidget>, Owner)
        SLATE_ARGUMENT(TSharedPtr<SToolTip>, Source)
        SLATE_EVENT(FSimpleDelegate, OnClosed)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void UpdateSource(TSharedPtr<SToolTip> InSource);
    FText GetDisplayedText() const;

    virtual bool IsEmpty() const override;
    virtual void OnOpening() override;
    virtual void OnClosed() override { SToolTip::OnClosed(); Closed.ExecuteIfBound(); }

private:
    void ApplyStyle();

    TWeakObjectPtr<UBasicButtonWidget> Owner;
    TSharedPtr<SToolTip> Source;
    TSharedPtr<STextBlock> TextBlock;
    FSimpleDelegate Closed;

    // Slate widgets retain brush pointers, so these must live as long as the tooltip.
    FSlateBrush BackgroundBrush;
    FSlateBrush AccentBrush;
};
