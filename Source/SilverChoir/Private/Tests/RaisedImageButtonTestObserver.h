#pragma once

#include "CoreMinimal.h"
#include "CoreGlobals.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/Object.h"
#include "RaisedImageButtonTestObserver.generated.h"

/** Counts the public dynamic delegate, including accidental duplicate dispatch. */
UCLASS(NotBlueprintable, Transient)
class URaisedImageButtonTestObserver : public UObject
{
    GENERATED_BODY()
public:
    int32 Clicks = 0;
    int32 PressStarts = 0;
    uint64 LastPressStartFrame = 0;
    bool bPressedVisualAtStart = false;
    TWeakObjectPtr<UBasicButtonWidget> Button;
    UFUNCTION() void RecordClick() { ++Clicks; }
    UFUNCTION() void RecordPressStart()
    {
        ++PressStarts;
        LastPressStartFrame = GFrameCounter;
        bPressedVisualAtStart = Button.IsValid() && Button->IsButtonVisuallyPressed();
    }
};
