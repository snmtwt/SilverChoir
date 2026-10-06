#pragma once
#include "Components/SizeBox.h"
#include "ContentFitBox.generated.h"

/** Fills available space at natural scale; uniformly shrinks only when content cannot fit.
 * Unlike ScaleBox, extra height remains available to Fill spacers inside the content. */
UCLASS(meta=(DisplayName="自适应内容容器"))
class SILVERCHOIR_API UContentFitBox : public USizeBox
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
