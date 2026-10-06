#pragma once

#include "CoreMinimal.h"

class UUserWidget;

/** Keeps finite panel layout animations eligible for UMG ticking while clipped by a ScrollBox.
 * Does not advance animation time itself, and unregisters as soon as the animations finish. */
class SILVERCHOIR_API FPanelAnimationTick
{
public:
    ~FPanelAnimationTick();
    void Start(UUserWidget* Widget);
    void Stop();

private:
    TWeakObjectPtr<UUserWidget> Owner;
    FDelegateHandle PostTickHandle;
    void KeepAnimationEligible(float DeltaSeconds);
};
