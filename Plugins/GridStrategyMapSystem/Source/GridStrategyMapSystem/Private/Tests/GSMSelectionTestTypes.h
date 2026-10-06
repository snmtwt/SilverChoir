#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "GSMSelectionTestTypes.generated.h"

/** Native hooks exercise the same reentrant path as tile Blueprint events. */
UCLASS(Transient, NotBlueprintable)
class AGSMSelectionTestTile : public AGSMTile3D
{
    GENERATED_BODY()
public:
    TFunction<void()> SelectedAction;
    TFunction<void()> DeselectedAction;
    virtual void OnMapTileSelected_Implementation() override
    {
        Super::OnMapTileSelected_Implementation();
        if (SelectedAction) SelectedAction();
    }
    virtual void OnMapTileDeselected_Implementation() override
    {
        Super::OnMapTileDeselected_Implementation();
        if (DeselectedAction) DeselectedAction();
    }
};

UCLASS(Transient, NotBlueprintable)
class UGSMSelectionTestObserver : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TWeakObjectPtr<AGSMMap3D> Map;
    UPROPERTY() TWeakObjectPtr<AGSMTile3D> LastObservedTile;
    int32 Notifications = 0;
    TFunction<void()> Action;

    UFUNCTION()
    void HandleSelectionChanged()
    {
        ++Notifications;
        LastObservedTile = Map.IsValid() ? Map->GetSelectedTile() : nullptr;
        if (Action) Action();
    }
};
