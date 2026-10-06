#pragma once

#include "CoreMinimal.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleVehiclePanelWidget.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "UObject/UnrealType.h"
#include "BattlePanelInteractionTestObserver.generated.h"

class AUnitPawnBase;

struct FBattlePanelTestEvent
{
    FName Name;
    TWeakObjectPtr<UObject> Source;
    FGuid UnitId;
    FGuid SquadId;
    FGuid VehicleId;
    bool bOpenAtCallback = false;
    bool bUnitSelectedAtCallback = false;
};

/** Records real Blueprint event dispatch. Optional callbacks simulate Blueprint reentrancy. */
UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestObserver : public UObject
{
    GENERATED_BODY()
public:
    TArray<FBattlePanelTestEvent> Events;
    int32 Requests = 0;
    int32 Invocations = 0;
    int32 SelectionChanges = 0;
    FGuid LastRequestedUnit;
    bool bLastRequestWasSelected = false;
    TFunction<void(UObject*, FName)> OnPanelEvent;
    TFunction<void()> OnSelectionChange;

    static FGuid GuidParameter(UFunction* Function, void* Parameters, const TCHAR* Name)
    {
        const FStructProperty* Property = FindFProperty<FStructProperty>(Function, Name);
        return Property && Property->Struct == TBaseStructure<FGuid>::Get() && Parameters
            ? *Property->ContainerPtrToValuePtr<FGuid>(Parameters) : FGuid();
    }

    void Record(UObject* Source, UFunction* Function, void* Parameters)
    {
        if (!Function) return;
        const FName Name = Function->GetFName();
        if (Name == TEXT("OnMemberInvoked")) { ++Invocations; return; }
        if (Name != TEXT("OnControlPanelOpened") && Name != TEXT("OnControlPanelClosed")
            && Name != TEXT("OnPanelOpened") && Name != TEXT("OnPanelClosed")) return;
        FBattlePanelTestEvent Event;
        Event.Name = Name;
        Event.Source = Source;
        Event.UnitId = GuidParameter(Function, Parameters, TEXT("UnitId"));
        Event.SquadId = GuidParameter(Function, Parameters, TEXT("SquadId"));
        if (!Event.SquadId.IsValid()) Event.SquadId = GuidParameter(Function, Parameters, TEXT("InSquadId"));
        Event.VehicleId = GuidParameter(Function, Parameters, TEXT("VehicleId"));
        if (const auto* Card = Cast<UBattlePersonnelCardWidget>(Source))
        {
            Event.bOpenAtCallback = Card->bControlPanelOpen;
            const auto Data = UPlayerUnitLibrary::GetUnitDataShared(Card, Event.UnitId);
            Event.bUnitSelectedAtCallback = Data && Data->IsSelected();
        }
        if (const auto* Vehicle = Cast<UBattleVehiclePanelWidget>(Source)) Event.bOpenAtCallback = Vehicle->bPanelOpen;
        Events.Add(Event);
        if (OnPanelEvent) OnPanelEvent(Source, Name);
    }

    void RecordNativeVehicleClose(UBattleVehiclePanelWidget* Source, FGuid InSquadId, FGuid VehicleId)
    {
        FBattlePanelTestEvent Event;
        Event.Name = TEXT("OnPanelClosed");
        Event.Source = Source;
        Event.SquadId = InSquadId;
        Event.VehicleId = VehicleId;
        Event.bOpenAtCallback = Source->bPanelOpen;
        Events.Add(Event);
        if (OnPanelEvent) OnPanelEvent(Source, Event.Name);
    }

    UFUNCTION() void RecordRequest(UBattlePersonnelCardWidget* Card, FGuid UnitId)
    {
        ++Requests;
        LastRequestedUnit = UnitId;
        const auto Data = UPlayerUnitLibrary::GetUnitDataShared(Card, UnitId);
        bLastRequestWasSelected = Data && Data->IsSelected();
    }

    UFUNCTION() void RecordSelectionChange(const TArray<AUnitPawnBase*>& Units)
    {
        ++SelectionChanges;
        if (OnSelectionChange) OnSelectionChange();
    }
};

UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestCard : public UBattlePersonnelCardWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UBattlePanelInteractionTestObserver> Observer;
    virtual void ProcessEvent(UFunction* Function, void* Parameters) override
    {
        if (Observer) Observer->Record(this, Function, Parameters);
        Super::ProcessEvent(Function, Parameters);
    }
};

UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestVehicle : public UBattleVehiclePanelWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UBattlePanelInteractionTestObserver> Observer;
    virtual void ProcessEvent(UFunction* Function, void* Parameters) override
    {
        const bool bCloseEvent = Function && Function->GetFName() == TEXT("OnPanelClosed");
        TGuardValue<bool> Guard(bCloseObservedViaProcessEvent, bCloseObservedViaProcessEvent || bCloseEvent);
        if (Observer) Observer->Record(this, Function, Parameters);
        Super::ProcessEvent(Function, Parameters);
    }
    virtual void OnPanelClosed_Implementation(FGuid InSquadId, FGuid VehicleId) override
    {
        // UE 5.8's generated BlueprintNativeEvent wrapper calls the implementation directly
        // when the resolved owner is native. Observe that real dispatch path as well, without
        // double-counting an event whose ProcessEvent thunk reaches this implementation.
        if (Observer && !bCloseObservedViaProcessEvent) Observer->RecordNativeVehicleClose(this, InSquadId, VehicleId);
        Super::OnPanelClosed_Implementation(InSquadId, VehicleId);
    }
private:
    bool bCloseObservedViaProcessEvent = false;
};

/** Concrete fixture types; all interaction behavior remains in the production base classes. */
UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestMemberMode : public UBattleMemberModeWidget
{
    GENERATED_BODY()
};

UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestSquadMode : public UBattleSquadModeWidget
{
    GENERATED_BODY()
};

UCLASS(NotBlueprintable, Transient)
class UBattlePanelInteractionTestHUD : public UBattleMapWidget
{
    GENERATED_BODY()
};
