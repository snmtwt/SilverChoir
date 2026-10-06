#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/BattlePanelInteractionTestObserver.h"
#include "Map/BattleMap/BattleModeScrollBox.h"
#include "Map/BattleMap/BattleSquadEntryWidget.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeExit.h"
#include "MTS_SubMapSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace BattlePanelInteractionTest
{
class FRuntime final : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 45.)
        {
            Test->AddError(TEXT("Panel interaction test timed out waiting for a real game world"));
            return Finish();
        }
        if (!bInitialized)
        {
            if (!GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return false;
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController());
            if (!PC.IsValid()) return false;
            const auto* Maps = PC->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
            if ((Maps && Maps->IsSubMapTransitionInProgress()) || (PC->BattleWidget && PC->BattleWidget->bTargeting)) return false;
            if (!Initialize()) return Finish();
            ReadyAt = FPlatformTime::Seconds() + .15;
            return false;
        }
        if (FPlatformTime::Seconds() < ReadyAt) return false;
        if (!ValidateAuthoredCardRegistration()) return Finish();
        UI->bRosterInitialized = true;
        if (!Test->TestTrue(TEXT("Native HUD constructs the test roster"), UI->SetBattleSquads(Squads))) return Finish();
        AuthoredCard.Reset();
        if (!AttachCards()) return Finish();
        ValidateRepeatedClickAndExclusivity();
        ValidateTargetingSelection();
        ValidatePreservedMultiSelection();
        ValidateModeAndRosterChanges();
        ValidateReentrancy();
        if (!Test->HasAnyErrors())
            Test->AddInfo(TEXT("BATTLE_PANEL_INTERACTION_OK selected-card clicks preserve multiple canonical units; unselected-card clicks select only that unit; authored late binding, targeting cancellation, panel close/open, personnel/vehicle exclusivity and reentrant cancellation verified"));
        return Finish();
    }

private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), ReadyAt = 0.;
    bool bInitialized = false, bManagerInstalled = false, bStateChanged = false;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<AGameMainMapGameState> State;
    TWeakObjectPtr<UPlayerUnitSubsystem> UnitSystem;
    FObjectPropertyBase* ManagerProperty = nullptr;
    TStrongObjectPtr<UPlayerUnitManagerBase> OriginalManager, Manager;
    TArray<TWeakObjectPtr<AUnitPawnBase>> OriginalSelectedPawns;
    TArray<FGuid> OriginalSelectedRecords;
    TArray<FGuid> OriginalRecordIds;
    EGameMainMapType OriginalMapType = EGameMainMapType::None;
    FName OriginalMapId;
    TStrongObjectPtr<UBattlePanelInteractionTestObserver> Observer;
    TStrongObjectPtr<UBattlePanelInteractionTestHUD> UI;
    TStrongObjectPtr<UBattlePanelInteractionTestCard> AuthoredCard;
    TStrongObjectPtr<UCanvasPanel> Root;
    TSharedPtr<SWidget> Preview;
    TArray<FBattleSquadView> Squads;
    TArray<TWeakObjectPtr<UBattlePanelInteractionTestCard>> ObservedCards;

    UBattleMemberModeWidget* Panel() const { return UI.IsValid() ? UI->MemberModePanel.Get() : nullptr; }
    UBattleUnitSelectionComponent* Selection() const { return PC.IsValid() ? PC->UnitSelection.Get() : nullptr; }

    bool Initialize()
    {
        State = PC->GetWorld()->GetGameState<AGameMainMapGameState>();
        UnitSystem = UPlayerUnitLibrary::GetPlayerUnitSubsystem(PC.Get());
        if (!Test->TestNotNull(TEXT("Real game state exists"), State.Get())
            || !Test->TestNotNull(TEXT("Real unit selection component exists"), Selection())
            || !Test->TestNotNull(TEXT("Real canonical unit subsystem exists"), UnitSystem.Get())) return false;
        ManagerProperty = FindFProperty<FObjectPropertyBase>(UnitSystem->GetClass(), TEXT("Manager"));
        if (!Test->TestNotNull(TEXT("Unit manager can be isolated"), ManagerProperty)) return false;
        OriginalManager.Reset(Cast<UPlayerUnitManagerBase>(ManagerProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get())));
        if (OriginalManager.IsValid())
        {
            OriginalRecordIds = OriginalManager->GetUnitDataIDs();
            for (FGuid Id : OriginalRecordIds)
                if (const auto Data = OriginalManager->GetUnitDataShared(Id); Data && Data->IsSelected()) OriginalSelectedRecords.Add(Id);
        }
        for (AUnitPawnBase* Pawn : Selection()->GetSelectedUnits()) OriginalSelectedPawns.Add(Pawn);
        OriginalMapType = State->GetCurrentMapType();
        OriginalMapId = State->ActiveMapID;
        bStateChanged = true;
        Selection()->CancelBoxSelection();
        Selection()->ClearUnitSelection();
        State->SetCurrentMapType(EGameMainMapType::Battle);
        State->ActiveMapID = NAME_None;
        Manager.Reset(NewObject<UPlayerUnitManagerBase>(UnitSystem.Get()));
        ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), Manager.Get());
        bManagerInstalled = true;
        Observer.Reset(NewObject<UBattlePanelInteractionTestObserver>());

        TArray<FUnitData> Records;
        for (int32 SquadIndex = 0; SquadIndex < 2; ++SquadIndex)
        {
            FBattleSquadView& Squad = Squads.AddDefaulted_GetRef();
            Squad.SquadId = FGuid::NewGuid();
            Squad.Name = FText::FromString(FString::Printf(TEXT("Panel fixture %d"), SquadIndex));
            Squad.Vehicle.VehicleId = FGuid::NewGuid();
            for (int32 Index = 0; Index < (SquadIndex == 0 ? 3 : 1); ++Index)
            {
                FUnitData& Record = Records.AddDefaulted_GetRef();
                Record.UnitId = FGuid::NewGuid();
                Record.Profile.CodeName = FText::FromString(FString::Printf(TEXT("Panel unit %d"), Records.Num()));
                FBattleMemberView& Member = Squad.Members.AddDefaulted_GetRef();
                Member.UnitId = Record.UnitId;
                Member.Profile = Record.Profile;
            }
        }
        FText Error;
        if (!Test->TestTrue(TEXT("Isolated canonical records load"), Manager->LoadUnitData(Records, Error)) || !CreatePreview()) return false;
        bInitialized = true;
        return true;
    }

    bool CreatePreview()
    {
        UI.Reset(CreateWidget<UBattlePanelInteractionTestHUD>(PC.Get()));
        if (!Test->TestNotNull(TEXT("HUD is created through CreateWidget with the real player"), UI.Get())) return false;
        auto* MemberMode = CreateWidget<UBattlePanelInteractionTestMemberMode>(PC.Get());
        auto* SquadMode = CreateWidget<UBattlePanelInteractionTestSquadMode>(PC.Get());
        if (!Test->TestTrue(TEXT("Concrete mode fixtures are created"), MemberMode && SquadMode && UI->WidgetTree && MemberMode->WidgetTree && SquadMode->WidgetTree)) return false;
        UI->MemberModePanel = MemberMode;
        UI->SquadModePanel = SquadMode;
        UI->MemberCardClass = UBattlePanelInteractionTestCard::StaticClass();
        UI->SquadEntryClass = UBattleSquadEntryWidget::StaticClass();
        UI->ModePages = UI->WidgetTree->ConstructWidget<UBattleModeScrollBox>();
        UI->WidgetTree->RootWidget = UI->ModePages;
        UI->ModePages->AddChild(MemberMode);
        UI->ModePages->AddChild(SquadMode);

        UVerticalBox* MemberRoot = MemberMode->WidgetTree->ConstructWidget<UVerticalBox>();
        MemberMode->WidgetTree->RootWidget = MemberRoot;
        MemberMode->SquadScrollBox = MemberMode->WidgetTree->ConstructWidget<UScrollBox>();
        MemberMode->SquadList = MemberMode->WidgetTree->ConstructWidget<UVerticalBox>();
        MemberMode->SquadScrollBox->AddChild(MemberMode->SquadList);
        MemberRoot->AddChild(MemberMode->SquadScrollBox);
        MemberMode->MemberScrollBox = MemberMode->WidgetTree->ConstructWidget<UScrollBox>();
        MemberMode->MemberList = MemberMode->WidgetTree->ConstructWidget<UHorizontalBox>();
        MemberMode->MemberScrollBox->AddChild(MemberMode->MemberList);
        MemberRoot->AddChild(MemberMode->MemberScrollBox);
        // Simulate a Designer-placed empty card before either mode/HUD NativeConstruct runs.
        // Deliberately do not call RegisterPersonnelCard: construction must discover it.
        AuthoredCard.Reset(CreateWidget<UBattlePanelInteractionTestCard>(PC.Get()));
        if (!Test->TestNotNull(TEXT("Authored empty personnel fixture exists"), AuthoredCard.Get())) return false;
        AuthoredCard->Observer = Observer.Get();
        AuthoredCard->OnControlPanelRequested.AddUniqueDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordRequest);
        ObservedCards.Add(AuthoredCard.Get());
        MemberMode->MemberList->AddChild(AuthoredCard.Get());
        MemberMode->VehicleButton = CreateWidget<UBattleHUDButton>(PC.Get());
        MemberRoot->AddChild(MemberMode->VehicleButton);
        MemberMode->VehiclePanelContainer = MemberMode->WidgetTree->ConstructWidget<UVerticalBox>();
        MemberRoot->AddChild(MemberMode->VehiclePanelContainer);
        MemberMode->VehiclePanelClass = UBattlePanelInteractionTestVehicle::StaticClass();
        // Inject only the observer fixture instance; production ShowVehiclePanel/OnClicked own its lifetime.
        auto* Vehicle = CreateWidget<UBattlePanelInteractionTestVehicle>(PC.Get());
        if (!Test->TestNotNull(TEXT("Vehicle is a real UserWidget"), Vehicle)) return false;
        Vehicle->Observer = Observer.Get();
        Vehicle->WidgetTree->RootWidget = Vehicle->WidgetTree->ConstructWidget<UVerticalBox>();
        Vehicle->SetVisibility(ESlateVisibility::Collapsed);
        MemberMode->VehiclePanel = Vehicle;
        MemberMode->VehiclePanelContainer->AddChild(Vehicle);

        UVerticalBox* SquadRoot = SquadMode->WidgetTree->ConstructWidget<UVerticalBox>();
        SquadMode->WidgetTree->RootWidget = SquadRoot;
        SquadMode->SquadScrollBox = SquadMode->WidgetTree->ConstructWidget<UScrollBox>();
        SquadMode->SquadList = SquadMode->WidgetTree->ConstructWidget<UVerticalBox>();
        SquadMode->SquadScrollBox->AddChild(SquadMode->SquadList);
        SquadRoot->AddChild(SquadMode->SquadScrollBox);
        SquadMode->SquadCardScrollBox = SquadMode->WidgetTree->ConstructWidget<UScrollBox>();
        SquadMode->SquadCardList = SquadMode->WidgetTree->ConstructWidget<UHorizontalBox>();
        SquadMode->SquadCardScrollBox->AddChild(SquadMode->SquadCardList);
        SquadRoot->AddChild(SquadMode->SquadCardScrollBox);

        Root.Reset(NewObject<UCanvasPanel>(PC.Get()));
        UCanvasPanelSlot* Slot = Root->AddChildToCanvas(UI.Get());
        Slot->SetAnchors(FAnchors(0, 0, 1, 1));
        Slot->SetOffsets(FMargin(0));
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 150);
        return true;
    }

    UBattlePanelInteractionTestCard* Card(int32 Index) const
    {
        return UI.IsValid() && UI->MemberCards.IsValidIndex(Index) ? Cast<UBattlePanelInteractionTestCard>(UI->MemberCards[Index]) : nullptr;
    }

    bool AttachCards()
    {
        bool bOK = Test->TestTrue(TEXT("Current squad has actual personnel widgets"), !UI->MemberCards.IsEmpty());
        for (UBattleMemberCardWidget* Entry : UI->MemberCards)
        {
            auto* Personnel = Cast<UBattlePanelInteractionTestCard>(Entry);
            if (!Test->TestNotNull(TEXT("HUD created observer subclasses using its real roster path"), Personnel)) return false;
            Personnel->Observer = Observer.Get();
            Personnel->OnControlPanelRequested.AddUniqueDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordRequest);
            ObservedCards.AddUnique(Personnel);
            bOK &= Test->TestTrue(TEXT("Personnel NativeConstruct has run through Slate"), Personnel->IsPresentationConstructed());
            bOK &= Test->TestTrue(TEXT("Personnel shares canonical unit selection"), Personnel->UsesSharedUnitSelection());
        }
        return bOK;
    }

    void Click(UBattlePanelInteractionTestCard* Personnel)
    {
        if (!Test->TestNotNull(TEXT("Personnel click target exists"), Personnel)) return;
        const int32 BeforeRequests = Observer->Requests, BeforeInvocations = Observer->Invocations;
        const FGuid UnitId = Personnel->Member.UnitId;
        Personnel->OnClicked.Broadcast(); // The same multicast emitted by the real Slate button.
        Test->TestEqual(TEXT("Every personnel click notifies the parent once"), Observer->Requests, BeforeRequests + 1);
        Test->TestEqual(TEXT("Every personnel click invokes the compatibility event once"), Observer->Invocations, BeforeInvocations + 1);
        Test->TestEqual(TEXT("Request carries the clicked unit"), Observer->LastRequestedUnit, UnitId);
        Test->TestTrue(TEXT("Native unit selection completed before the panel request"), Observer->bLastRequestWasSelected);
        const auto Data = Manager->GetUnitDataShared(UnitId);
        Test->TestTrue(TEXT("Native click selects the canonical unit record"), Data && Data->IsSelected());
    }

    using FExpectedEvent = TPair<UObject*, FName>;
    void ExpectEvents(const TCHAR* Scenario, const TArray<FExpectedEvent>& Expected)
    {
        if (!Test->TestEqual(FString(Scenario) + TEXT(" event count"), Observer->Events.Num(), Expected.Num())) return;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            const FBattlePanelTestEvent& Event = Observer->Events[Index];
            Test->TestTrue(FString(Scenario) + TEXT(" exact event owner/order"), Event.Source.Get() == Expected[Index].Key);
            Test->TestEqual(FString(Scenario) + TEXT(" exact event name/order"), Event.Name, Expected[Index].Value);
            const bool bOpening = Event.Name == TEXT("OnControlPanelOpened") || Event.Name == TEXT("OnPanelOpened");
            Test->TestEqual(FString(Scenario) + TEXT(" open state committed before Blueprint callback"), Event.bOpenAtCallback, bOpening);
            if (Event.Name == TEXT("OnControlPanelOpened"))
                Test->TestTrue(FString(Scenario) + TEXT(" unit selected before Blueprint open event"), Event.bUnitSelectedAtCallback);
        }
    }

    void ExpectActive(UBattlePersonnelCardWidget* Personnel, bool bVehicle)
    {
        Test->TestTrue(TEXT("Parent active personnel reference matches the open panel"), Panel()->ActivePersonnelCard == Personnel);
        Test->TestEqual(TEXT("Vehicle and personnel panel are mutually exclusive"), Panel()->VehiclePanel->bPanelOpen, bVehicle);
        for (const auto& Weak : ObservedCards)
            if (Weak.IsValid()) Test->TestEqual(TEXT("Only the active personnel card is open"), Weak->bControlPanelOpen, Weak.Get() == Personnel);
        Test->TestEqual(TEXT("Vehicle visibility matches its open state"), Panel()->VehiclePanel->GetVisibility(),
            bVehicle ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }

    bool ValidateAuthoredCardRegistration()
    {
        if (!Test->TestNotNull(TEXT("Authored card survives outer HUD construction"), AuthoredCard.Get())) return false;
        Test->TestFalse(TEXT("Authored registration regression runs before roster initialization"), UI->bRosterInitialized);
        Test->TestTrue(TEXT("Authored card remains in the real member list"), AuthoredCard->GetParent() == Panel()->MemberList);
        if (!Test->TestTrue(TEXT("Authored card NativeConstruct has run"), AuthoredCard->IsPresentationConstructed())) return false;
        Test->TestFalse(TEXT("Authored card starts without unit data"), AuthoredCard->Member.UnitId.IsValid());
        Panel()->SetSquadContext(Squads[0].SquadId, Squads[0].Vehicle);

        Observer->Events.Reset();
        AuthoredCard->SetMember(Squads[0].Members[0]);
        Test->TestTrue(TEXT("First SetMember binds canonical unit data"), AuthoredCard->UsesSharedUnitSelection());
        Click(AuthoredCard.Get());
        ExpectEvents(TEXT("First data assignment retains authored registration"), {{AuthoredCard.Get(), TEXT("OnControlPanelOpened")}});
        ExpectActive(AuthoredCard.Get(), false);

        const FGuid OriginalUnitId = AuthoredCard->Member.UnitId;
        Observer->Events.Reset();
        AuthoredCard->SetMember(Squads[0].Members[1]);
        ExpectEvents(TEXT("Changing authored unit closes its previous context"), {{AuthoredCard.Get(), TEXT("OnControlPanelClosed")}});
        if (!Observer->Events.IsEmpty())
        {
            Test->TestEqual(TEXT("Changed-context close reports the old unit ID"), Observer->Events[0].UnitId, OriginalUnitId);
            Test->TestEqual(TEXT("Changed-context close preserves the old squad ID"), Observer->Events[0].SquadId, Squads[0].SquadId);
        }
        ExpectActive(nullptr, false);
        Observer->Events.Reset();
        Click(AuthoredCard.Get());
        ExpectEvents(TEXT("Changed authored card opens without re-registering"), {{AuthoredCard.Get(), TEXT("OnControlPanelOpened")}});
        ExpectActive(AuthoredCard.Get(), false);
        Test->TestEqual(TEXT("Authored card now requests its replacement unit"), Observer->LastRequestedUnit, Squads[0].Members[1].UnitId);
        Panel()->CloseActivePanel();
        Selection()->ClearUnitSelection();
        Observer->Events.Reset();
        return true;
    }

    void ValidateRepeatedClickAndExclusivity()
    {
        auto* First = Card(0);
        auto* Second = Card(1);
        if (!First || !Second) { Test->AddError(TEXT("Alpha requires two personnel cards")); return; }
        auto* Vehicle = Panel()->VehiclePanel.Get();
        Observer->Events.Reset();
        Click(First);
        ExpectEvents(TEXT("First click"), {{First, TEXT("OnControlPanelOpened")}});
        ExpectActive(First, false);
        Test->TestEqual(TEXT("Personnel open passes the current squad"), First->ControlPanelSquadId, Squads[0].SquadId);
        Test->TestTrue(TEXT("Card shows canonical selected state"), First->IsMemberSelected());

        Observer->Events.Reset();
        const int32 BeforeSelectedClick = Observer->Requests;
        Click(First);
        ExpectEvents(TEXT("Already open card click does not restart its presentation"), {});
        Test->TestEqual(TEXT("Already selected card still issues its own request"), Observer->Requests, BeforeSelectedClick + 1);
        ExpectActive(First, false);

        Observer->Events.Reset();
        Panel()->CloseActivePanel();
        ExpectEvents(TEXT("Explicit close"), {{First, TEXT("OnControlPanelClosed")}});
        Test->TestTrue(TEXT("Closing a control panel does not deselect its unit"), First->IsMemberSelected());
        Observer->Events.Reset();
        Click(First);
        ExpectEvents(TEXT("Closed selected card reopens"), {{First, TEXT("OnControlPanelOpened")}});

        Observer->Events.Reset();
        Click(Second);
        ExpectEvents(TEXT("Personnel A to B"), {{First, TEXT("OnControlPanelClosed")}, {Second, TEXT("OnControlPanelOpened")}});
        ExpectActive(Second, false);
        Test->TestFalse(TEXT("New personnel selection deselects the previous canonical unit"), Manager->GetUnitDataShared(First->Member.UnitId)->IsSelected());

        Observer->Events.Reset();
        Panel()->VehicleButton->OnClicked.Broadcast();
        ExpectEvents(TEXT("Personnel to vehicle"), {{Second, TEXT("OnControlPanelClosed")}, {Vehicle, TEXT("OnPanelOpened")}});
        ExpectActive(nullptr, true);
        Test->TestEqual(TEXT("Vehicle open uses current squad"), Vehicle->SquadId, Squads[0].SquadId);
        Test->TestEqual(TEXT("Vehicle open uses current vehicle"), Vehicle->Vehicle.VehicleId, Squads[0].Vehicle.VehicleId);
        Observer->Events.Reset();
        int32 RepeatedVehicleRequests=0;
        const FDelegateHandle VehicleRequestHandle=Panel()->OnPanelOpening.AddLambda([&](FGuid UnitId){if(!UnitId.IsValid())++RepeatedVehicleRequests;});
        Test->TestTrue(TEXT("Explicitly showing an open vehicle succeeds"),Panel()->ShowVehiclePanel());
        Panel()->OnPanelOpening.Remove(VehicleRequestHandle);
        Test->TestEqual(TEXT("Repeated explicit vehicle show still notifies its parent"),RepeatedVehicleRequests,1);
        ExpectEvents(TEXT("Repeated explicit vehicle show does not restart its presentation"),{});
        ExpectActive(nullptr,true);

        // A visually selected card can be stale after an external selection update. Clicking it
        // must still execute SelectUnitById, rather than treating its selected styling as a no-op.
        First->SetSelected(true);
        Test->TestFalse(TEXT("Selected styling alone does not select canonical data"), Manager->GetUnitDataShared(First->Member.UnitId)->IsSelected());
        Observer->Events.Reset();
        Click(First);
        ExpectEvents(TEXT("Vehicle to personnel"), {{Vehicle, TEXT("OnPanelClosed")}, {First, TEXT("OnControlPanelOpened")}});
        ExpectActive(First, false);
    }

    void ValidateTargetingSelection()
    {
        auto* First = Card(0);
        auto* Second = Card(1);
        if (!First || !Second || !PC.IsValid()) return;
        TStrongObjectPtr<UBattleMapWidget> OriginalHUD(PC->BattleWidget.Get());
        const auto OriginalCursor = PC->CurrentMouseCursor;
        const TSharedPtr<SWidget> OriginalFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
        PC->BattleWidget = UI.Get();
        ON_SCOPE_EXIT
        {
            // Cancel while this fixture still owns PC->BattleWidget, then restore all temporary UI state.
            if (UI.IsValid()) UI->CancelTargeting();
            if (PC.IsValid())
            {
                PC->BattleWidget = OriginalHUD.Get();
                PC->CurrentMouseCursor = OriginalCursor;
                Test->TestTrue(TEXT("Targeting regression restores the player's original BattleWidget"), PC->BattleWidget == OriginalHUD.Get());
            }
            if (FSlateApplication::IsInitialized())
            {
                if (OriginalFocus.IsValid()) FSlateApplication::Get().SetKeyboardFocus(OriginalFocus, EFocusCause::SetDirectly);
                else FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
            }
        };

        Test->TestEqual(TEXT("Targeting starts from the first personnel selection"), UI->SelectedMemberId, First->Member.UnitId);
        UI->BeginTargeting(EBattleCommand::Interact);
        Test->TestTrue(TEXT("Real PC BattleWidget enters target interaction"), PC->BattleWidget == UI.Get() && PC->BattleWidget->bTargeting);
        Test->TestFalse(TEXT("Selection is initially gated by active targeting"), Selection()->SelectUnitById(Second->Member.UnitId));
        Test->TestTrue(TEXT("Targeting precondition keeps the old canonical unit selected"), Manager->GetUnitDataShared(First->Member.UnitId)->IsSelected());
        Test->TestFalse(TEXT("Targeting precondition leaves the new canonical unit unselected"), Manager->GetUnitDataShared(Second->Member.UnitId)->IsSelected());

        Observer->Events.Reset();
        Click(Second);
        ExpectEvents(TEXT("Targeting click switches personnel after cancelling targeting"), {
            {First, TEXT("OnControlPanelClosed")}, {Second, TEXT("OnControlPanelOpened")}});
        ExpectActive(Second, false);
        Test->TestFalse(TEXT("Personnel click leaves targeting before the new panel opens"), UI->bTargeting);
        Test->TestFalse(TEXT("Targeting click deselects the old canonical record"), Manager->GetUnitDataShared(First->Member.UnitId)->IsSelected());
        Test->TestTrue(TEXT("Targeting click selects the new canonical record"), Manager->GetUnitDataShared(Second->Member.UnitId)->IsSelected());
        Test->TestEqual(TEXT("Targeting click updates the HUD's selected member"), UI->SelectedMemberId, Second->Member.UnitId);
        Test->TestEqual(TEXT("Cancelling targeting restores the original cursor"), PC->CurrentMouseCursor, OriginalCursor);

        UI->BeginTargeting(EBattleCommand::Pickup);
        Test->TestTrue(TEXT("Already selected card can also be clicked during targeting"), UI->bTargeting);
        Observer->Events.Reset();
        Click(Second);
        ExpectEvents(TEXT("Targeting click on the open card preserves its presentation"), {});
        Test->TestFalse(TEXT("Repeated-card click also cancels targeting"), UI->bTargeting);
        Click(First); // Restore the starting personnel for the following mode-change scenario.
    }

    void ValidatePreservedMultiSelection()
    {
        auto* First = Card(0);
        auto* Second = Card(1);
        auto* Third = Card(2);
        if (!Test->TestTrue(TEXT("Multi-selection fixture has three actual personnel cards"), First && Second && Third)) return;
        const TArray<UBattlePanelInteractionTestCard*> Cards = {First, Second, Third};
        TArray<AUnitPawnBase*> Pawns;
        ON_SCOPE_EXIT
        {
            Selection()->OnSelectionChanged.RemoveDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordSelectionChange);
            Selection()->ClearUnitSelection();
            for (auto* Pawn : Pawns) if (IsValid(Pawn)) Pawn->Destroy();
            // Continue the remaining panel scenarios with their original data-only first selection.
            Selection()->SelectUnitById(First->Member.UnitId);
        };
        for (int32 Index = 0; Index < Cards.Num(); ++Index)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Pawn = PC->GetWorld()->SpawnActor<AUnitPawnBase>(FVector(900000., Index * 250., 900000.), FRotator::ZeroRotator, Params);
            if (!Test->TestNotNull(TEXT("Multi-selection fixture spawns a real selectable Pawn"), Pawn)) return;
            Pawns.Add(Pawn);
            TInlineComponentArray<UActorComponent*> Components(Pawn);
            for (auto* Component : Components) { Component->Deactivate(); Component->SetComponentTickEnabled(false); }
            Pawn->SetActorEnableCollision(false);
            if (!Test->TestTrue(TEXT("Multi-selection Pawn binds its card's canonical record"), Pawn->BindUnitData(Cards[Index]->Member.UnitId))) return;
        }
        Panel()->CloseActivePanel();
        Selection()->SetSelectedUnits({Pawns[0], Pawns[1]});
        Selection()->OnSelectionChanged.AddUniqueDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordSelectionChange);
        const int32 BeforeClicks = Observer->SelectionChanges;
        auto ExpectSelection = [&](const TCHAR* Scenario, bool bFirst, bool bSecond, bool bThird, int32 ExpectedChanges)
        {
            const bool Expected[] = {bFirst, bSecond, bThird};
            const TArray<AUnitPawnBase*> Selected = Selection()->GetSelectedUnits();
            Test->TestEqual(FString(Scenario) + TEXT(" selected Pawn count"), Selected.Num(), int32(bFirst) + int32(bSecond) + int32(bThird));
            for (int32 Index = 0; Index < Cards.Num(); ++Index)
            {
                const FString Label = FString::Printf(TEXT("%s member %d"), Scenario, Index);
                const auto Data = Manager->GetUnitDataShared(Cards[Index]->Member.UnitId);
                Test->TestEqual(Label + TEXT(" canonical selection"), Data && Data->IsSelected(), Expected[Index]);
                Test->TestEqual(Label + TEXT(" Pawn selection"), Pawns[Index]->IsUnitSelected(), Expected[Index]);
                Test->TestEqual(Label + TEXT(" selection component membership"), Selected.Contains(Pawns[Index]), Expected[Index]);
            }
            Test->TestEqual(FString(Scenario) + TEXT(" selection change notifications"), Observer->SelectionChanges, BeforeClicks + ExpectedChanges);
        };
        ExpectSelection(TEXT("Initial A/B multi-selection"), true, true, false, 0);

        // A stale display state cannot turn a canonical selected-card click into exclusive selection.
        First->SetSelected(false);
        Observer->Events.Reset();
        Click(First);
        ExpectEvents(TEXT("Selected A opens while preserving A/B"), {{First, TEXT("OnControlPanelOpened")}});
        ExpectActive(First, false);
        ExpectSelection(TEXT("Selected A click"), true, true, false, 0);

        Observer->Events.Reset();
        Click(Second);
        ExpectEvents(TEXT("Selected B replaces only the panel"), {{First, TEXT("OnControlPanelClosed")}, {Second, TEXT("OnControlPanelOpened")}});
        ExpectActive(Second, false);
        ExpectSelection(TEXT("Selected B click"), true, true, false, 0);

        Observer->Events.Reset();
        Click(Second);
        ExpectEvents(TEXT("Selected B repeat preserves its open presentation"), {});
        ExpectActive(Second, false);
        ExpectSelection(TEXT("Repeated selected B click"), true, true, false, 0);

        Observer->Events.Reset();
        Panel()->CloseActivePanel();
        ExpectEvents(TEXT("Closing a panel preserves A/B"), {{Second, TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);
        ExpectSelection(TEXT("Explicit panel close"), true, true, false, 0);

        Observer->Events.Reset();
        Click(First);
        ExpectEvents(TEXT("Selected A reopens after explicit close"), {{First, TEXT("OnControlPanelOpened")}});
        ExpectSelection(TEXT("Selected A reopen"), true, true, false, 0);

        // Conversely, a visually selected C must still replace A/B when its canonical state is unselected.
        Third->SetSelected(true);
        Observer->Events.Reset();
        Click(Third);
        ExpectEvents(TEXT("Unselected C becomes the exclusive selection and panel"), {{First, TEXT("OnControlPanelClosed")}, {Third, TEXT("OnControlPanelOpened")}});
        ExpectActive(Third, false);
        ExpectSelection(TEXT("Unselected C click"), false, false, true, 1);

        Observer->Events.Reset();
        Click(Third);
        ExpectEvents(TEXT("Now-selected C repeat preserves its open presentation"), {});
        ExpectSelection(TEXT("Repeated selected C click"), false, false, true, 1);
        Click(First); // Restore the open first-personnel panel for the following mode-change scenario.
    }

    void ValidateModeAndRosterChanges()
    {
        TStrongObjectPtr<UBattlePanelInteractionTestCard> Previous(Card(0));
        if (!Previous.IsValid()) return;
        Observer->Events.Reset();
        Test->TestTrue(TEXT("Switching HUD mode succeeds"), UI->SetControlMode(EBattleControlMode::Squad, false));
        ExpectEvents(TEXT("Mode switch"), {{Previous.Get(), TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);
        Observer->Events.Reset();
        Test->TestTrue(TEXT("Returning to member mode succeeds"), UI->SetControlMode(EBattleControlMode::Member, false));
        ExpectEvents(TEXT("Returning mode does not reopen stale panel"), {});

        Click(Previous.Get());
        Observer->Events.Reset();
        Test->TestTrue(TEXT("Selecting another squad rebuilds its cards"), UI->SelectSquad(Squads[1].SquadId));
        ExpectEvents(TEXT("Squad change closes old personnel"), {{Previous.Get(), TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);
        Test->TestNull(TEXT("Old card is removed from the roster container"), Previous->GetParent());
        Observer->Events.Reset();
        Previous->OnControlPanelRequested.Broadcast(Previous.Get(), Previous->Member.UnitId);
        ExpectEvents(TEXT("Removed card cannot reopen through its old delegate"), {});
        Test->TestFalse(TEXT("Parent refuses a stale card explicitly"), Panel()->ShowPersonnelPanel(Previous.Get()));
        if (!AttachCards()) return;
        TStrongObjectPtr<UBattlePanelInteractionTestCard> Bravo(Card(0));
        Click(Bravo.Get());
        Test->TestEqual(TEXT("Rebuilt card opens in the new squad"), Bravo->ControlPanelSquadId, Squads[1].SquadId);

        Observer->Events.Reset();
        Test->TestTrue(TEXT("Reinitializing the same roster succeeds"), UI->SetBattleSquads(Squads));
        ExpectEvents(TEXT("Roster rebuild closes stale personnel"), {{Bravo.Get(), TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);
        Test->TestTrue(TEXT("Roster rebuild creates a fresh card"), Card(0) != Bravo.Get());
        Test->TestFalse(TEXT("Rebuilt-away card cannot be reopened"), Panel()->ShowPersonnelPanel(Bravo.Get()));
        if (!AttachCards()) return;

        Panel()->VehicleButton->OnClicked.Broadcast();
        auto* Vehicle = Panel()->VehiclePanel.Get();
        const FGuid OldVehicleId = Vehicle->Vehicle.VehicleId;
        Observer->Events.Reset();
        Squads[1].Vehicle.VehicleId = FGuid::NewGuid();
        Test->TestTrue(TEXT("Vehicle-context rebuild succeeds"), UI->SetBattleSquads(Squads));
        ExpectEvents(TEXT("Roster rebuild closes old vehicle"), {{Vehicle, TEXT("OnPanelClosed")}});
        if (!Observer->Events.IsEmpty())
        {
            Test->TestEqual(TEXT("Vehicle close reports the previous vehicle, not replacement data"), Observer->Events[0].VehicleId, OldVehicleId);
            Test->TestEqual(TEXT("Vehicle close reports its original squad"), Observer->Events[0].SquadId, Squads[1].SquadId);
        }
        ExpectActive(nullptr, false);
        AttachCards();
    }

    void ValidateReentrancy()
    {
        if (!Test->TestTrue(TEXT("Restore Alpha for reentrancy scenarios"), UI->SelectSquad(Squads[0].SquadId)) || !AttachCards()) return;
        auto* First = Card(0);
        auto* Second = Card(1);
        if (!First || !Second) return;
        Click(First);
        bool bAttempted = false, bNestedShow = true;
        Observer->OnPanelEvent = [&](UObject* Source, FName Event)
        {
            if (Source == First && Event == TEXT("OnControlPanelClosed") && !bAttempted)
            {
                bAttempted = true;
                bNestedShow = Panel()->ShowVehiclePanel();
            }
        };
        Observer->Events.Reset();
        Click(Second);
        Observer->OnPanelEvent = nullptr;
        Test->TestTrue(TEXT("Blueprint close callback was exercised"), bAttempted);
        Test->TestFalse(TEXT("Nested show is rejected during an outer switch"), bNestedShow);
        ExpectEvents(TEXT("Reentrant show cannot interleave panels"), {{First, TEXT("OnControlPanelClosed")}, {Second, TEXT("OnControlPanelOpened")}});
        ExpectActive(Second, false);

        Click(First);
        bool bReset = false;
        Observer->OnPanelEvent = [&](UObject* Source, FName Event)
        {
            if (Source == First && Event == TEXT("OnControlPanelClosed") && !bReset)
            {
                bReset = true;
                Panel()->ResetPersonnelCards();
            }
        };
        Observer->Events.Reset();
        Click(Second);
        Observer->OnPanelEvent = nullptr;
        Test->TestTrue(TEXT("Blueprint callback reset the roster bindings"), bReset);
        ExpectEvents(TEXT("Reset during close cancels pending open"), {{First, TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);
        Test->TestFalse(TEXT("Reset card cannot open without registration"), Panel()->ShowPersonnelPanel(Second));
        Panel()->RegisterPersonnelCard(First);
        Panel()->RegisterPersonnelCard(Second);

        Click(First);
        bool bClosedOnOpen = false;
        Observer->OnPanelEvent = [&](UObject* Source, FName Event)
        {
            if (Source == Second && Event == TEXT("OnControlPanelOpened") && !bClosedOnOpen)
            {
                bClosedOnOpen = true;
                Panel()->CloseActivePanel();
            }
        };
        Observer->Events.Reset();
        Click(Second);
        Observer->OnPanelEvent = nullptr;
        Test->TestTrue(TEXT("Blueprint open callback can close its own panel"), bClosedOnOpen);
        ExpectEvents(TEXT("Close during open leaves no stale active panel"), {
            {First, TEXT("OnControlPanelClosed")}, {Second, TEXT("OnControlPanelOpened")}, {Second, TEXT("OnControlPanelClosed")}});
        ExpectActive(nullptr, false);

        // Selection itself may rebuild the roster before the card can broadcast its panel request.
        Selection()->ClearUnitSelection();
        TStrongObjectPtr<UBattlePanelInteractionTestCard> Retired(First);
        bool bRebuilt = false;
        Observer->OnSelectionChange = [&]()
        {
            if (!bRebuilt)
            {
                bRebuilt = true;
                Test->TestTrue(TEXT("Selection callback can synchronously rebuild the HUD"), UI->SetBattleSquads(Squads));
            }
        };
        Selection()->OnSelectionChanged.AddUniqueDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordSelectionChange);
        const int32 RequestsBeforeRebuild = Observer->Requests;
        const int32 InvocationsBeforeRebuild = Observer->Invocations;
        Observer->Events.Reset();
        Retired->OnClicked.Broadcast();
        Selection()->OnSelectionChanged.RemoveDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordSelectionChange);
        Observer->OnSelectionChange = nullptr;
        Test->TestTrue(TEXT("Selection callback actually rebuilt the roster during click"), bRebuilt);
        Test->TestEqual(TEXT("Destroyed presentation does not issue stale parent requests"), Observer->Requests, RequestsBeforeRebuild);
        Test->TestEqual(TEXT("Destroyed presentation does not issue stale compatibility events"), Observer->Invocations, InvocationsBeforeRebuild);
        ExpectEvents(TEXT("Rebuild during selection cannot open a retired card"), {});
        ExpectActive(nullptr, false);
        Test->TestFalse(TEXT("Retired card is rejected after selection-time rebuild"), Panel()->ShowPersonnelPanel(Retired.Get()));
    }

    bool Finish() { Cleanup(); return true; }
    void Cleanup()
    {
        if (Observer.IsValid())
        {
            Observer->OnPanelEvent = nullptr;
            Observer->OnSelectionChange = nullptr;
            if (Selection()) Selection()->OnSelectionChanged.RemoveDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordSelectionChange);
        }
        for (const auto& Weak : ObservedCards)
            if (Weak.IsValid())
            {
                Weak->CancelPendingClick();
                if (Observer.IsValid()) Weak->OnControlPanelRequested.RemoveDynamic(Observer.Get(), &UBattlePanelInteractionTestObserver::RecordRequest);
            }
        if (Panel())
        {
            Panel()->ResetPersonnelCards();
            if (Panel()->VehicleButton) Panel()->VehicleButton->CancelPendingClick();
        }
        if (Preview.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) Root->ClearChildren();
        UI.Reset();
        AuthoredCard.Reset();
        Root.Reset();
        ObservedCards.Reset();
        Observer.Reset();
        if (bStateChanged && Selection()) Selection()->ClearUnitSelection();
        if (bManagerInstalled && UnitSystem.IsValid())
        {
            ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), OriginalManager.Get());
            Test->TestTrue(TEXT("Original unit manager is restored"), ManagerProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get()) == OriginalManager.Get());
            if (OriginalManager.IsValid()) Test->TestTrue(TEXT("Original canonical records remain unchanged"), OriginalManager->GetUnitDataIDs() == OriginalRecordIds);
        }
        bManagerInstalled = false;
        if (bStateChanged && State.IsValid())
        {
            State->ActiveMapID = OriginalMapId;
            State->SetCurrentMapType(OriginalMapType);
        }
        if (bStateChanged)
        {
            for (const auto& Pawn : OriginalSelectedPawns) if (Pawn.IsValid()) Pawn->SetUnitSelected(true);
            if (OriginalManager.IsValid())
                for (FGuid Id : OriginalSelectedRecords)
                    if (const auto Data = OriginalManager->GetUnitDataShared(Id)) Data->SetSelected(true);
        }
        bStateChanged = false;
        bInitialized = false;
        OriginalSelectedPawns.Reset();
        OriginalSelectedRecords.Reset();
        Manager.Reset();
        OriginalManager.Reset();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattlePanelInteractionRuntimeTest,
    "SilverChoir.BattleUI.Panels.RuntimeInteraction", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattlePanelInteractionRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattlePanelInteractionTest::FRuntime(this));
    return true;
}
#endif
