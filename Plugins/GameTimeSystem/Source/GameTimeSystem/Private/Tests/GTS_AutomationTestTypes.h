#pragma once

#include "CoreMinimal.h"
#include "GTS_TimeTask.h"
#include "GTS_AutomationTestTypes.generated.h"

/** Native-only fixture; never configured as a gameplay task or saved into assets. */
UCLASS(NotBlueprintable, Transient)
class UGTS_AutomationTimeTask final : public UGTS_TimeTask
{
    GENERATED_BODY()
public:
    int32 CallCount = 0;
    FDateTime LastScheduledTime;
    FDateTime LastCurrentTime;
    TFunction<void(UGTS_AutomationTimeTask*, FDateTime, FDateTime)> OnInvoked;

    virtual void OnTimeReached_Implementation(FDateTime ScheduledTime, FDateTime CurrentTime) override;
};

UCLASS(NotBlueprintable, Transient)
class UGTS_AutomationTimeObserver final : public UObject
{
    GENERATED_BODY()
public:
    TArray<FDateTime> TimeEvents;
    TArray<double> ScaleEvents;
    TArray<bool> PauseEvents;
    TFunction<void(FDateTime)> OnTimeEvent;

    UFUNCTION() void HandleTime(FDateTime Time);
    UFUNCTION() void HandleScale(double Scale) { ScaleEvents.Add(Scale); }
    UFUNCTION() void HandlePaused(bool bPaused) { PauseEvents.Add(bPaused); }
};
