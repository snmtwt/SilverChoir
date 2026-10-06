#include "UIBasic/ContentFitBox.h"
#include "Widgets/SCompoundWidget.h"

class SContentFitBox : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SContentFitBox) {} SLATE_DEFAULT_SLOT(FArguments, Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { ChildSlot[Args._Content.Widget]; }
    virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override
    {
        const FVector2D Available=Geometry.GetLocalSize();
        const FVector2D Desired=ChildSlot.GetWidget()->GetDesiredSize();
        const float Scale=FMath::Clamp(FMath::Min(
            Desired.X>0 ? Available.X/Desired.X : 1.0,
            Desired.Y>0 ? Available.Y/Desired.Y : 1.0),0.001,1.0);
        const FGeometry VirtualGeometry=Geometry.MakeChild(Available/Scale,FSlateLayoutTransform(Scale));
        SCompoundWidget::OnArrangeChildren(VirtualGeometry,Children);
    }
};

TSharedRef<SWidget> UContentFitBox::RebuildWidget()
{
    return SNew(SContentFitBox)[Super::RebuildWidget()];
}
