#include "Map/BattleMap/BattleResourceBar.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/PixelAlignedButtonFrame.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

namespace BattleResourceBar
{
float UnitValue(float Value)
{
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 0.f;
}

FLinearColor SafeColor(const FLinearColor& Color)
{
    return FLinearColor(UnitValue(Color.R), UnitValue(Color.G), UnitValue(Color.B), UnitValue(Color.A));
}

struct FPaintState
{
    float Percent = 0.f;
    FLinearColor Track = FLinearColor::Transparent;
    FLinearColor Frame = FLinearColor::Transparent;
    FLinearColor Bottom = FLinearColor::Transparent;
    FLinearColor Top = FLinearColor::Transparent;
    FLinearColor Highlight = FLinearColor::Transparent;
    FLinearColor Tick = FLinearColor::Transparent;
    int32 Divisions = 0;

    bool operator==(const FPaintState& Other) const
    {
        return Percent == Other.Percent && Track == Other.Track && Frame == Other.Frame
            && Bottom == Other.Bottom && Top == Other.Top && Highlight == Other.Highlight
            && Tick == Other.Tick && Divisions == Other.Divisions;
    }
};

FPaintState ReadState(const UBattleResourceBar* Owner)
{
    FPaintState State;
    if (Owner)
    {
        // Sanitize before attribute comparison as well as painting: NaN must not
        // invalidate an otherwise idle bar on every Slate attribute update.
        State.Percent = UnitValue(Owner->GetPercent());
        State.Track = SafeColor(Owner->TrackColor);
        State.Frame = SafeColor(Owner->FrameColor);
        const FLinearColor FillTint = SafeColor(Owner->GetFillColorAndOpacity());
        State.Bottom = SafeColor(Owner->BottomColor) * FillTint;
        State.Top = SafeColor(Owner->TopColor) * FillTint;
        State.Highlight = SafeColor(Owner->HighlightColor) * FillTint;
        State.Tick = SafeColor(Owner->TickColor);
        State.Divisions = FMath::Clamp(Owner->Divisions, 0, 8);
    }
    return State;
}
}

template <>
struct TWidgetTypeTraits<class SBattleResourceBar>
{
    static constexpr bool SupportsInvalidation() { return true; }
};

class SBattleResourceBar : public SLeafWidget
{
    SLATE_DECLARE_WIDGET(SBattleResourceBar, SLeafWidget)

public:
    SLATE_BEGIN_ARGS(SBattleResourceBar)
    {
        _Visibility = EVisibility::HitTestInvisible;
    }
        SLATE_ATTRIBUTE(BattleResourceBar::FPaintState, PaintState)
    SLATE_END_ARGS()

    SBattleResourceBar()
        : PaintState(*this, BattleResourceBar::FPaintState())
    {
        SetCanTick(false);
        bCanSupportFocus = false;
    }

    void Construct(const FArguments& InArgs)
    {
        PaintState.Assign(*this, InArgs._PaintState);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        // Do not import SProgressBar's marquee-brush minimum size into the card.
        return FVector2D::ZeroVector;
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
        const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const auto Frame = FPixelAlignedButtonFrame::Make(Geometry, 1.f);
        const FVector2f Size = Frame.Max - Frame.Min;
        if (Size.X <= 0.f || Size.Y <= 0.f)
        {
            return Layer;
        }

        const auto& State = PaintState.Get();
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled)
            ? ESlateDrawEffect::NoPixelSnapping
            : ESlateDrawEffect::NoPixelSnapping | ESlateDrawEffect::DisabledEffect;
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        auto Box = [&](int32 BoxLayer, FVector2f Position, FVector2f Extent, FLinearColor Color)
        {
            if (Extent.X > 0.f && Extent.Y > 0.f)
            {
                FSlateDrawElement::MakeBox(Elements, BoxLayer,
                    Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)),
                    Brush, Effects, Color * Tint);
            }
        };

        // One physical pixel where space permits; very thin allocations retain
        // an interior instead of letting the two frame edges consume the fill.
        const FVector2f Edge(FMath::Min(Frame.Thickness.X, Size.X * .25f),
            FMath::Min(Frame.Thickness.Y, Size.Y * .25f));
        const FVector2f InnerMin = Frame.Min + Edge;
        const FVector2f InnerSize = Size - Edge * 2.f;
        Box(Layer, Frame.Min, Size, State.Frame);
        Box(Layer + 1, InnerMin, InnerSize, State.Track);

        const float FillHeight = InnerSize.Y * State.Percent;
        const FVector2f FillMin(InnerMin.X, InnerMin.Y + InnerSize.Y - FillHeight);
        if (FillHeight > 0.f)
        {
            TArray<FSlateGradientStop> Stops;
            Stops.Emplace(FVector2f::ZeroVector, State.Top * Tint);
            Stops.Emplace(FVector2f(0.f, FillHeight), State.Bottom * Tint);
            // Horizontal stop lines produce a gradient along the vertical axis.
            FSlateDrawElement::MakeGradient(Elements, Layer + 2,
                Geometry.ToPaintGeometry(FVector2f(InnerSize.X, FillHeight), FSlateLayoutTransform(FillMin)),
                MoveTemp(Stops), Orient_Horizontal, Effects);

            const float ShineWidth = FMath::Min(Frame.Thickness.X * .6f, InnerSize.X * .3f);
            FLinearColor SideHighlight = State.Highlight;
            SideHighlight.A *= .28f;
            Box(Layer + 3, FillMin, FVector2f(ShineWidth, FillHeight), SideHighlight);
        }

        // At least eight physical pixels between marks avoids a noisy ladder on
        // short bars. Marks sit inside the track, never over its outer border.
        const int32 VisibleDivisions = FMath::Min(State.Divisions,
            FMath::FloorToInt(InnerSize.Y / (Frame.Thickness.Y * 8.f)));
        const float TickHeight = FMath::Min(Frame.Thickness.Y * .65f, InnerSize.Y);
        const float TickWidth = FMath::Min(Frame.Thickness.X * 2.f, InnerSize.X * .65f);
        for (int32 Index = 1; Index < VisibleDivisions; ++Index)
        {
            const float Y = InnerMin.Y + InnerSize.Y * float(Index) / float(VisibleDivisions);
            Box(Layer + 4, FVector2f(InnerMin.X + InnerSize.X - TickWidth, Y - TickHeight * .5f),
                FVector2f(TickWidth, TickHeight), State.Tick);
        }

        if (FillHeight > 0.f)
        {
            // The cap is part of the fill, including at tiny nonzero values.
            // A zero value produces neither a luminous line nor a residual cap.
            Box(Layer + 5, FillMin,
                FVector2f(InnerSize.X, FMath::Min(Frame.Thickness.Y * .8f, FillHeight)), State.Highlight);
        }
        return Layer + 5;
    }

private:
    TSlateAttribute<BattleResourceBar::FPaintState> PaintState;
};

SLATE_IMPLEMENT_WIDGET(SBattleResourceBar)
void SBattleResourceBar::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
    SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, PaintState, EInvalidateWidgetReason::Paint);
}

UBattleResourceBar::UBattleResourceBar(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetBarFillType(EProgressBarFillType::BottomToTop);
    SetBarFillStyle(EProgressBarFillStyle::Scale);
    SetBorderPadding(FVector2D::ZeroVector);
    SetFillColorAndOpacity(FLinearColor::White);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UBattleResourceBar::RebuildWidget()
{
    // Keep the inherited storage and nonvirtual setters intact. A registered
    // Slate attribute observes GetPercent even when called via UProgressBar*;
    // the stock Slate progress bar and its optional marquee timer are not built.
    MyProgressBar.Reset();
    const TWeakObjectPtr<UBattleResourceBar> WeakThis(this);
    return SNew(SBattleResourceBar)
        .PaintState_Lambda([WeakThis]() { return BattleResourceBar::ReadState(WeakThis.Get()); });
}

void UBattleResourceBar::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (const auto Widget = GetCachedWidget())
    {
        Widget->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void UBattleResourceBar::SetGradientColors(FLinearColor InBottomColor, FLinearColor InTopColor,
    FLinearColor InHighlightColor)
{
    BottomColor = BattleResourceBar::SafeColor(InBottomColor);
    TopColor = BattleResourceBar::SafeColor(InTopColor);
    HighlightColor = BattleResourceBar::SafeColor(InHighlightColor);
}

#if WITH_EDITOR
const FText UBattleResourceBar::GetPaletteCategory()
{
    return NSLOCTEXT("SilverChoir", "BattleResourceBarCategory", "战斗 UI");
}

void UBattleResourceBar::OnCreationFromPalette()
{
    // UProgressBar's palette hook applies a blue tint. Keep the custom gradient
    // colors independent, and give a newly placed bar a readable preview value.
    SetFillColorAndOpacity(FLinearColor::White);
    SetPercent(.75f);
}
#endif
