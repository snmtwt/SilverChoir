#pragma once

#include "CoreMinimal.h"
#include "Components/ScrollBox.h"
#include "BattleModeScrollBox.generated.h"

/**
 * A program-controlled viewport for battle mode pages. The owner changes pages
 * with ScrollWidgetIntoView or SetScrollOffset; input cannot scroll this panel.
 * Children retain their normal input, including nested scrolling and inventory
 * drag/drop. Orientation, slots, styles and programmatic animation use the
 * inherited ScrollBox API. Input-related ScrollBox settings are not applicable.
 */
UCLASS(BlueprintType, meta=(DisplayName="战斗模式分页滚动框"))
class SILVERCHOIR_API UBattleModeScrollBox : public UScrollBox
{
    GENERATED_BODY()

public:
    UBattleModeScrollBox(const FObjectInitializer& ObjectInitializer);

    virtual void SynchronizeProperties() override;

#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override;
#endif

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    void ApplyPagingInputPolicy();
};
