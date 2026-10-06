#pragma once

#include "CoreMinimal.h"
#include "SubSystem/GameMapTransitionSystem/GameMapSubMapHandler.h"
#include "GameMapHandlerTestProxy.generated.h"

/** Isolates registry/deployment tests from the base-room camera flow; real T5 is tested separately. */
UCLASS(NotBlueprintable, Transient)
class UGameMapHandlerTestProxy : public UGameMapSubMapHandler
{
    GENERATED_BODY()
public:
    virtual void OnSubMapLoaded_Implementation(UMTS_SubMapSubsystem*, const FMTS_SubMapInfo&) override { FinishLoading(); }
    virtual void OnSubMapReady_Implementation(UMTS_SubMapSubsystem*, const FMTS_SubMapInfo&) override {}
};
