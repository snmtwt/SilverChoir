#pragma once
#include "UIBasic/BasicButtonWidget.h"
#include "SelectionButtonWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSelectionRequested, FName, ChoiceID);

/** Reusable radio/list choice. The owning group controls the selected state. */
UCLASS(Blueprintable)
class SILVERCHOIR_API USelectionButtonWidget : public UBasicButtonWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="单选|样式") bool bUseTabStyle = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单选") FName ChoiceID;
    UPROPERTY(BlueprintAssignable, Category="单选") FSelectionRequested OnSelectionRequested;
protected:
    virtual int32 PaintButtonFrame(const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool bParentEnabled) const override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void ForwardSelection();
};
