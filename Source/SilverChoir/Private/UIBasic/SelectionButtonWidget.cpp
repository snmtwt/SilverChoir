#include "UIBasic/SelectionButtonWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/PixelAlignedButtonFrame.h"

int32 USelectionButtonWidget::PaintButtonFrame(const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool bParentEnabled) const
{
    if (!bUseTabStyle) return Super::PaintButtonFrame(Geometry,CullingRect,Elements,LayerId,Style,bParentEnabled);
    const auto Frame=FPixelAlignedButtonFrame::Make(Geometry,1.f);
    const FVector2f Size=Frame.Max-Frame.Min;
    const float Active=GetButtonVisualStrength()*(IsButtonSelected()?1.f:.45f);
    const FLinearColor Tint=Style.GetColorAndOpacityTint()*FLinearColor(1,1,1,bParentEnabled&&GetIsEnabled()?1.f:.4f);
    FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Frame.Min)),
        FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::NoPixelSnapping,BackgroundColor*Tint);
    TArray<FSlateGradientStop> Stops;
    Stops.Add(FSlateGradientStop(FVector2f(0,0),FLinearColor(AccentColor.R,AccentColor.G,AccentColor.B,.025f*Active)*Tint));
    Stops.Add(FSlateGradientStop(FVector2f(0,Size.Y*.55f),FLinearColor(AccentColor.R,AccentColor.G,AccentColor.B,.055f*Active)*Tint));
    Stops.Add(FSlateGradientStop(FVector2f(0,Size.Y),FLinearColor(AccentColor.R,AccentColor.G,AccentColor.B,.015f+.24f*Active)*Tint));
    // Slate names the orientation of the stop lines, so horizontal stops fade along Y.
    FSlateDrawElement::MakeGradient(Elements,LayerId+1,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Frame.Min)),Stops,Orient_Horizontal,ESlateDrawEffect::NoPixelSnapping);
    const FLinearColor Outline=FMath::Lerp(BorderColor*.65f,AccentColor*.45f,Active)*Tint;
    const auto* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
    auto Line=[&](FVector2f Position,FVector2f Extent,FLinearColor Color)
    {
        FSlateDrawElement::MakeBox(Elements,LayerId+2,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Position)),Brush,ESlateDrawEffect::NoPixelSnapping,Color);
    };
    Line(Frame.Min,FVector2f(Size.X,Frame.Thickness.Y),Outline);
    Line(Frame.Min,FVector2f(Frame.Thickness.X,Size.Y),Outline);
    Line(FVector2f(Frame.Max.X-Frame.Thickness.X,Frame.Min.Y),FVector2f(Frame.Thickness.X,Size.Y),Outline);
    const FLinearColor Edge=FMath::Lerp(BorderColor*.65f,FLinearColor(FColor::FromHex(TEXT("35C9F2"))),Active)*Tint;
    const float Underline=Frame.Thickness.Y*(IsButtonSelected()?2.f:1.f);
    FSlateDrawElement::MakeBox(Elements,LayerId+2,
        Geometry.ToPaintGeometry(FVector2f(Size.X,Underline),FSlateLayoutTransform(FVector2f(Frame.Min.X,Frame.Max.Y-Underline))),
        FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::NoPixelSnapping,Edge);
    return LayerId+3;
}
void USelectionButtonWidget::NativeConstruct()
{
    Super::NativeConstruct();
    OnClicked.AddUniqueDynamic(this, &ThisClass::ForwardSelection);
}
void USelectionButtonWidget::NativeDestruct()
{
    OnClicked.RemoveDynamic(this, &ThisClass::ForwardSelection);
    Super::NativeDestruct();
}
void USelectionButtonWidget::ForwardSelection() { OnSelectionRequested.Broadcast(ChoiceID); }
