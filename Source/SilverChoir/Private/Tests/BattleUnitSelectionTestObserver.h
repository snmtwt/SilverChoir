#pragma once

#include "CoreMinimal.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Components/DecalComponent.h"
#include "UObject/Class.h"
#include "BattleUnitSelectionTestObserver.generated.h"

/** Runtime fixture verifies inherited selection events without shipping game behavior. */
UCLASS(NotBlueprintable, Transient)
class ABattleSelectionTestPawn : public AUnitPawnBase
{
    GENERATED_BODY()
public:
    int32 SelectedEvents = 0;
    int32 DeselectedEvents = 0;
    int32 IncorrectDecalEvents = 0;
    TWeakObjectPtr<UBattleUnitSelectionComponent> ClearOnNextSelected;
    virtual void ProcessEvent(UFunction* Function, void* Parameters) override
    {
        if (Function)
        {
            if (Function->GetFName() == TEXT("OnUnitSelected"))
            {
                ++SelectedEvents;
                if (!SelectionDecal || !SelectionDecal->IsVisible() || SelectionDecal->bHiddenInGame) ++IncorrectDecalEvents;
                if (auto* Selection=ClearOnNextSelected.Get())
                {
                    ClearOnNextSelected.Reset();
                    Selection->CancelBoxSelection();
                    Selection->ClearUnitSelection();
                }
            }
            if (Function->GetFName() == TEXT("OnUnitDeselected"))
            {
                ++DeselectedEvents;
                if (!SelectionDecal || SelectionDecal->IsVisible() || !SelectionDecal->bHiddenInGame) ++IncorrectDecalEvents;
            }
        }
        Super::ProcessEvent(Function, Parameters);
    }
};

/** Only replaces physical pointer/UI queries; every gesture uses production selection code. */
UCLASS(NotBlueprintable, Transient)
class UBattleSelectionTestComponent : public UBattleUnitSelectionComponent
{
    GENERATED_BODY()
public:
    FVector2D Pointer=FVector2D::ZeroVector;
    bool bPointerAvailable=true;
    bool bOverUI=false;
protected:
    virtual bool ReadSelectionPointer(FVector2D& OutPosition) const override
    { OutPosition=Pointer; return bPointerAvailable; }
    virtual bool IsPointerOverUI() const override { return bOverUI; }
};

UCLASS(NotBlueprintable, Transient)
class UBattleUnitSelectionTestObserver : public UObject
{
    GENERATED_BODY()
public:
    int32 Changes = 0;
    int32 LastCount = 0;
    TWeakObjectPtr<UBattleUnitSelectionComponent> ClearOnNextChange;
    UFUNCTION() void RecordSelection(const TArray<AUnitPawnBase*>& Units)
    {
        ++Changes; LastCount=Units.Num();
        if (auto* Selection=ClearOnNextChange.Get())
        {
            ClearOnNextChange.Reset();
            Selection->CancelBoxSelection();
            Selection->ClearUnitSelection();
        }
    }
};
