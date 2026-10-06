#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleSquadEntryWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace BattleRosterUITest
{
/** Real subsystem lookups with isolated canonical storage, following SquadTransferUITests. */
struct FManagers
{
    TWeakObjectPtr<UPlayerUnitSubsystem> UnitSystem;
    TWeakObjectPtr<UPlayerSquadSubsystem> SquadSystem;
    FObjectPropertyBase* UnitProperty = nullptr;
    FObjectPropertyBase* SquadProperty = nullptr;
    TStrongObjectPtr<UPlayerUnitManagerBase> OriginalUnits, Units;
    TStrongObjectPtr<UPlayerSquadManagerBase> OriginalSquads, Squads;
    TArray<FGuid> OriginalUnitIds, OriginalSquadIds;
    TArray<FGuid> AlphaMembers, BravoMembers;
    FGuid Alpha, Bravo;
    bool bInstalled = false;

    ~FManagers() { Restore(); }

    bool Initialize(UWorld* World, FAutomationTestBase& Test)
    {
        UnitSystem = UPlayerUnitLibrary::GetPlayerUnitSubsystem(World);
        SquadSystem = UPlayerSquadLibrary::GetPlayerSquadSubsystem(World);
        if (!Test.TestNotNull(TEXT("Roster has a real unit subsystem"), UnitSystem.Get())
            || !Test.TestNotNull(TEXT("Roster has a real squad subsystem"), SquadSystem.Get())) return false;
        UnitProperty = FindFProperty<FObjectPropertyBase>(UnitSystem->GetClass(), TEXT("Manager"));
        SquadProperty = FindFProperty<FObjectPropertyBase>(SquadSystem->GetClass(), TEXT("Manager"));
        if (!Test.TestNotNull(TEXT("Unit manager can be isolated"), UnitProperty)
            || !Test.TestNotNull(TEXT("Squad manager can be isolated"), SquadProperty)) return false;
        OriginalUnits.Reset(Cast<UPlayerUnitManagerBase>(UnitProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get())));
        OriginalSquads.Reset(Cast<UPlayerSquadManagerBase>(SquadProperty->GetObjectPropertyValue_InContainer(SquadSystem.Get())));
        if (OriginalUnits.IsValid()) OriginalUnitIds = OriginalUnits->GetUnitDataIDs();
        if (OriginalSquads.IsValid()) OriginalSquadIds = OriginalSquads->GetSquadIds();
        Units.Reset(NewObject<UPlayerUnitManagerBase>(UnitSystem.Get()));
        Squads.Reset(NewObject<UPlayerSquadManagerBase>(SquadSystem.Get()));
        Squads->Initialize(Units.Get());
        UnitProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), Units.Get());
        SquadProperty->SetObjectPropertyValue_InContainer(SquadSystem.Get(), Squads.Get());
        bInstalled = true;

        const FName Tile(TEXT("SC_BATTLE_ROSTER_UI_TEST"));
        TArray<FUnitData> Data;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FUnitData Unit;
            Unit.UnitId = FGuid::NewGuid();
            Unit.Profile.CodeName = FText::FromString(FString::Printf(TEXT("战斗测试员 %d"), Index + 1));
            Unit.RuntimeData.TileId = Tile;
            Unit.RuntimeData.CurrentHealth = Index == 0 ? 62.f : 100.f;
            Unit.RuntimeData.CurrentStamina = 80.f - Index * 15.f;
            Data.Add(Unit);
            (Index < 2 ? AlphaMembers : BravoMembers).Add(Unit.UnitId);
        }
        FText Error;
        if (!Test.TestTrue(TEXT("Create isolated real unit records"), Units->LoadUnitData(Data, Error))
            || !Test.TestTrue(TEXT("Create Alpha squad"), Squads->CreateSquad(FText::FromString(TEXT("银翼小队")), nullptr, Tile, Alpha, Error))
            || !Test.TestTrue(TEXT("Create Bravo squad"), Squads->CreateSquad(FText::FromString(TEXT("守望小队")), nullptr, Tile, Bravo, Error))) return false;
        for (FGuid UnitId : AlphaMembers)
            if (!Test.TestTrue(TEXT("Alpha member joins through the real manager"), Squads->AddUnitToSquad(UnitId, Alpha, Error))) return false;
        for (FGuid UnitId : BravoMembers)
            if (!Test.TestTrue(TEXT("Bravo member joins through the real manager"), Squads->AddUnitToSquad(UnitId, Bravo, Error))) return false;
        return true;
    }

    void Restore(FAutomationTestBase* Test = nullptr)
    {
        if (!bInstalled) return;
        if (Squads.IsValid()) Squads->Shutdown();
        if (UnitSystem.IsValid()) UnitProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), OriginalUnits.Get());
        if (SquadSystem.IsValid()) SquadProperty->SetObjectPropertyValue_InContainer(SquadSystem.Get(), OriginalSquads.Get());
        bInstalled = false;
        if (Test)
        {
            if (UnitSystem.IsValid()) Test->TestTrue(TEXT("Original unit manager restored"),
                UnitProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get()) == OriginalUnits.Get());
            if (SquadSystem.IsValid()) Test->TestTrue(TEXT("Original squad manager restored"),
                SquadProperty->GetObjectPropertyValue_InContainer(SquadSystem.Get()) == OriginalSquads.Get());
            if (OriginalUnits.IsValid()) Test->TestTrue(TEXT("Original unit records remain unchanged"), OriginalUnits->GetUnitDataIDs() == OriginalUnitIds);
            if (OriginalSquads.IsValid()) Test->TestTrue(TEXT("Original squad records remain unchanged"), OriginalSquads->GetSquadIds() == OriginalSquadIds);
        }
    }
};

class FRuntime final : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60.)
        {
            Test->AddError(FString::Printf(TEXT("Battle roster UI timed out at stage %d"), Stage));
            return Finish();
        }
        if (Stage == 0)
        {
            if (!GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return false;
            APlayerController* PC = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Context.World()->GetFirstPlayerController();
            if (!PC) return false;
            if (!Managers.Initialize(PC->GetWorld(), *Test) || !CreatePreview(PC)) return Finish();
            Advance();
            return false;
        }
        if (FPlatformTime::Seconds() - StageStarted < .65) return false;
        FText Error;
        switch (Stage)
        {
        case 1:
            if (!ValidateBindings()) return Finish();
            Test->TestFalse(TEXT("Construction does not initialize a gameplay roster"), UI->bRosterInitialized);
            Test->TestEqual(TEXT("Member-mode authored squad fixtures survive before initialization"), UI->MemberModePanel->SquadList->GetChildrenCount(), 6);
            Test->TestEqual(TEXT("Squad-mode authored squad fixtures survive before initialization"), UI->SquadModePanel->SquadList->GetChildrenCount(), 6);
            Test->TestEqual(TEXT("Authored personnel cards survive before initialization"), UI->MemberModePanel->MemberList->GetChildrenCount(), 6);
            Test->TestNotNull(TEXT("Blueprint initialization event exists"), UI->FindFunction(TEXT("OnBattleUIInitialized")));
            Test->TestNotNull(TEXT("Blueprint squad-selection event exists"), UI->FindFunction(TEXT("OnBattleSquadSelected")));
            {
                const auto AuthoredCards = UI->MemberModePanel->MemberList->GetAllChildren();
                Test->TestFalse(TEXT("Invalid first initialization fails"), UI->InitializeBattleUI({FGuid::NewGuid()}, Error));
                Test->TestFalse(TEXT("Invalid first initialization leaves the roster uninitialized"), UI->bRosterInitialized);
                Test->TestTrue(TEXT("Invalid first initialization keeps authored personnel fixtures"),
                    UI->MemberModePanel->MemberList->GetAllChildren() == AuthoredCards);
            }
            if (!Test->TestTrue(TEXT("Initialize from real squad IDs with duplicates"),
                UI->InitializeBattleUI({Managers.Bravo, Managers.Alpha, Managers.Bravo}, Error))) return Finish();
            Test->TestTrue(TEXT("Successful initialization clears the output error"), Error.IsEmpty());
            Advance();
            return false;
        case 2:
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Bravo);
            ValidateMembers(Managers.BravoMembers);
            Capture(TEXT("01_Member_DefaultBravo.png"));
            Advance();
            return false;
        case 3:
            if (!CaptureFinished()) return false;
            if (!Click(FindRow(UI->MemberModePanel->SquadList, Managers.Alpha))) return Finish();
            Advance();
            return false;
        case 4:
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Alpha);
            ValidateMembers(Managers.AlphaMembers);
            SelectedMembers = UI->MemberModePanel->MemberList->GetAllChildren();
            if (!Click(FindRow(UI->MemberModePanel->SquadList, Managers.Alpha))) return Finish();
            Advance();
            return false;
        case 5:
            Test->TestTrue(TEXT("Clicking the selected squad keeps personnel widget instances"),
                UI->MemberModePanel->MemberList->GetAllChildren() == SelectedMembers);
            if (!Test->TestTrue(TEXT("Repeated initialization accepts the same ordered IDs"),
                UI->InitializeBattleUI({Managers.Bravo, Managers.Alpha}, Error))) return Finish();
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Alpha);
            ValidateMembers(Managers.AlphaMembers);
            ValidateRejectedInitialization();
            if (!Click(UI->SquadModeButton)) return Finish();
            Advance();
            return false;
        case 6:
            Test->TestEqual(TEXT("Real mode click opens the squad page"), UI->CurrentControlMode, EBattleControlMode::Squad);
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Alpha);
            ValidateMembers(Managers.AlphaMembers);
            if (!Click(FindRow(UI->SquadModePanel->SquadList, Managers.Bravo))) return Finish();
            Advance();
            return false;
        case 7:
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Bravo);
            ValidateMembers(Managers.BravoMembers);
            Capture(TEXT("02_Squad_SelectedBravo.png"));
            Advance();
            return false;
        case 8:
            if (!CaptureFinished()) return false;
            if (!Click(UI->MemberModeButton)) return Finish();
            Advance();
            return false;
        case 9:
            Test->TestEqual(TEXT("Returning to member mode keeps current squad"), UI->CurrentControlMode, EBattleControlMode::Member);
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Bravo);
            ValidateMembers(Managers.BravoMembers);
            if (!Test->TestTrue(TEXT("Reordered initialization succeeds"),
                UI->InitializeBattleUI({Managers.Alpha, Managers.Bravo, Managers.Alpha}, Error))) return Finish();
            ValidateRoster({Managers.Alpha, Managers.Bravo}, Managers.Bravo);
            if (!Test->TestTrue(TEXT("An empty ID list explicitly clears the roster"), UI->InitializeBattleUI({}, Error))) return Finish();
            ValidateRoster({}, FGuid());
            ValidateMembers({});
            Test->TestFalse(TEXT("Empty roster clears the selected member"), UI->SelectedMemberId.IsValid());
            Test->TestFalse(TEXT("A removed squad cannot be selected"), UI->SelectSquad(Managers.Bravo));
            Advance();
            return false;
        case 10:
            if (!Test->TestTrue(TEXT("Roster can be initialized again after clearing"), UI->InitializeBattleUI({Managers.Alpha}, Error))) return Finish();
            Advance();
            return false;
        case 11:
            ValidateRoster({Managers.Alpha}, Managers.Alpha);
            ValidateMembers(Managers.AlphaMembers);
            Capture(TEXT("03_Member_ReinitializedAlpha.png"));
            Advance();
            return false;
        case 12:
            if (!CaptureFinished()) return false;
            Cleanup();
            if (!Test->HasAnyErrors()) Test->AddInfo(TEXT("BATTLE_ROSTER_UI_OK real manager IDs populate both lists; actual Slate clicks synchronize selection and personnel cards; repeat, reorder, empty and invalid input covered; original managers restored"));
            return true;
        default:
            return Finish();
        }
    }

private:
    FAutomationTestBase* Test;
    FManagers Managers;
    TStrongObjectPtr<UCanvasPanel> Root;
    TStrongObjectPtr<UBattleMapWidget> UI;
    TSharedPtr<SWidget> Preview;
    TArray<UWidget*> SelectedMembers;
    double Started = FPlatformTime::Seconds(), StageStarted = Started;
    int32 Stage = 0;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;

    void Advance() { ++Stage; StageStarted = FPlatformTime::Seconds(); }
    bool Finish() { Cleanup(); return true; }
    void Cleanup()
    {
        if (UI.IsValid())
        {
            if (UI->MemberModeButton) UI->MemberModeButton->CancelPendingClick();
            if (UI->SquadModeButton) UI->SquadModeButton->CancelPendingClick();
            for (UVerticalBox* List : {UI->MemberModePanel ? UI->MemberModePanel->SquadList.Get() : nullptr,
                UI->SquadModePanel ? UI->SquadModePanel->SquadList.Get() : nullptr})
                if (List) for (UWidget* Child : List->GetAllChildren())
                    if (auto* Button = Cast<UBasicButtonWidget>(Child)) Button->CancelPendingClick();
        }
        if (Preview.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) Root->ClearChildren();
        UI.Reset(); Root.Reset();
        SelectedMembers.Reset();
        Managers.Restore(Test);
    }

    bool CreatePreview(APlayerController* PC)
    {
        UClass* Class = LoadClass<UBattleMapWidget>(nullptr, TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
        if (!Test->TestNotNull(TEXT("Actual battle HUD Blueprint exists"), Class)) return false;
        UI.Reset(CreateWidget<UBattleMapWidget>(PC, Class));
        if (!Test->TestNotNull(TEXT("Actual battle HUD can be instantiated"), UI.Get())) return false;
        Root.Reset(NewObject<UCanvasPanel>(PC));
        auto* Slot = Root->AddChildToCanvas(UI.Get());
        Slot->SetAnchors(FAnchors(0, 0, 1, 1));
        Slot->SetOffsets(FMargin(0));
        // Leave the Blueprint initialization entry uncalled until the test supplies real IDs.
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 100);
        return true;
    }

    bool ValidateBindings()
    {
        bool Valid = Test->TestNotNull(TEXT("Member mode panel exists"), UI->MemberModePanel.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode panel exists"), UI->SquadModePanel.Get());
        Valid &= Test->TestNotNull(TEXT("Member mode button exists"), UI->MemberModeButton.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode button exists"), UI->SquadModeButton.Get());
        Valid &= Test->TestNotNull(TEXT("Squad entry class is configured"), UI->SquadEntryClass.Get());
        if (!Valid) return false;
        Valid &= Test->TestNotNull(TEXT("Member mode has a squad list"), UI->MemberModePanel->SquadList.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode has a squad list"), UI->SquadModePanel->SquadList.Get());
        Valid &= Test->TestNotNull(TEXT("Member mode has a personnel list"), UI->MemberModePanel->MemberList.Get());
        return Valid;
    }

    UBattleSquadEntryWidget* FindRow(UVerticalBox* List, FGuid SquadId) const
    {
        if (List) for (UWidget* Child : List->GetAllChildren())
            if (auto* Row = Cast<UBattleSquadEntryWidget>(Child))
            {
                FGuid Id;
                if (FGuid::Parse(Row->ChoiceID.ToString(), Id) && Id == SquadId) return Row;
            }
        return nullptr;
    }

    void ValidateRoster(const TArray<FGuid>& Expected, FGuid Selected)
    {
        Test->TestTrue(TEXT("Explicit initialization marks the roster initialized"), UI->bRosterInitialized);
        Test->TestFalse(TEXT("Real manager initialization does not enter test-data mode"), UI->bUsingTestData);
        Test->TestEqual(TEXT("HUD selected squad matches the clicked row"), UI->SelectedSquadId, Selected);
        if (Test->TestEqual(TEXT("Resolved squad snapshot deduplicates IDs"), UI->Squads.Num(), Expected.Num()))
            for (int32 Index = 0; Index < Expected.Num(); ++Index)
                Test->TestEqual(TEXT("Resolved snapshots preserve caller order"), UI->Squads[Index].SquadId, Expected[Index]);
        for (UVerticalBox* List : {UI->MemberModePanel->SquadList.Get(), UI->SquadModePanel->SquadList.Get()})
        {
            if (!Test->TestEqual(TEXT("Both mode lists contain exactly the requested squads"), List->GetChildrenCount(), Expected.Num())) continue;
            int32 SelectedCount = 0;
            for (int32 Index = 0; Index < Expected.Num(); ++Index)
            {
                auto* Row = Cast<UBattleSquadEntryWidget>(List->GetChildAt(Index));
                if (!Test->TestNotNull(TEXT("Dynamic row uses the real squad-entry widget"), Row)) continue;
                Test->TestTrue(TEXT("Row uses the configured Designer class"), Row->GetClass() == UI->SquadEntryClass.Get());
                FGuid RowId;
                Test->TestTrue(TEXT("Selection delegate carries a parseable squad ID"), FGuid::Parse(Row->ChoiceID.ToString(), RowId));
                Test->TestEqual(TEXT("Squad list preserves caller order"), RowId, Expected[Index]);
                Test->TestEqual(TEXT("Selection is synchronized in both modes"), Row->IsSquadSelected(), RowId == Selected);
                SelectedCount += Row->IsSquadSelected() ? 1 : 0;
                Test->TestNotNull(TEXT("Squad row has a click sound"), Row->PressSound.Get());
                Test->TestNotNull(TEXT("Squad row has a hover sound"), Row->HoverSound.Get());
                Test->TestFalse(TEXT("Completed row has no pending click"), Row->IsPressPending());
            }
            Test->TestEqual(TEXT("Each nonempty list has exactly one selected row"), SelectedCount, Expected.IsEmpty() ? 0 : 1);
        }
    }

    void ValidateMembers(const TArray<FGuid>& Expected)
    {
        auto* List = UI->MemberModePanel->MemberList.Get();
        if (!Test->TestEqual(TEXT("Personnel list reflects only the selected squad"), List->GetChildrenCount(), Expected.Num())) return;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            auto* Card = Cast<UBattlePersonnelCardWidget>(List->GetChildAt(Index));
            if (!Test->TestNotNull(TEXT("Roster uses WBP personnel-card native type"), Card)) continue;
            Test->TestEqual(TEXT("Personnel card contains the actual canonical unit ID"), Card->Member.UnitId, Expected[Index]);
            const auto Unit = Managers.Units->GetUnitDataShared(Expected[Index]);
            Test->TestTrue(TEXT("Personnel card displays the canonical unit name"), Unit.IsValid() && Card->Member.Profile.CodeName.EqualTo(Unit->Profile.CodeName));
            Test->TestTrue(TEXT("Personnel card retains the authored Blueprint class"), Card->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
        }
    }

    void ValidateRejectedInitialization()
    {
        const auto MemberRows = UI->MemberModePanel->SquadList->GetAllChildren();
        const auto SquadRows = UI->SquadModePanel->SquadList->GetAllChildren();
        const auto Cards = UI->MemberModePanel->MemberList->GetAllChildren();
        const FGuid OldSquad = UI->SelectedSquadId, OldMember = UI->SelectedMemberId;
        for (const TArray<FGuid>& Invalid : {TArray<FGuid>{Managers.Bravo, FGuid::NewGuid()}, TArray<FGuid>{FGuid()}})
        {
            FText Error;
            Test->TestFalse(TEXT("Unknown or zero squad IDs reject the entire request"), UI->InitializeBattleUI(Invalid, Error));
            Test->TestFalse(TEXT("Invalid initialization returns a useful error"), Error.IsEmpty());
            Test->TestTrue(TEXT("Rejected input leaves both row sets and personnel instances untouched"),
                UI->MemberModePanel->SquadList->GetAllChildren() == MemberRows
                && UI->SquadModePanel->SquadList->GetAllChildren() == SquadRows
                && UI->MemberModePanel->MemberList->GetAllChildren() == Cards);
            Test->TestEqual(TEXT("Rejected input preserves squad selection"), UI->SelectedSquadId, OldSquad);
            Test->TestEqual(TEXT("Rejected input preserves member selection"), UI->SelectedMemberId, OldMember);
            ValidateRoster({Managers.Bravo, Managers.Alpha}, Managers.Alpha);
        }
    }

    bool Click(UBasicButtonWidget* Button)
    {
        if (!Test->TestNotNull(TEXT("Requested click target exists"), Button)) return false;
        const auto Slate = Button->TakeWidget();
        const auto Window = FSlateApplication::Get().FindWidgetWindow(Slate);
        if (!Test->TestTrue(TEXT("Click target is mounted in a Slate window"), Window.IsValid())) return false;
        const auto& Geometry = Button->GetCachedGeometry();
        const FVector2D Position(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
        const auto Path = Window->GetHittestGrid().GetBubblePath(Position, 0.f, false);
        if (!Test->TestTrue(TEXT("Row or mode button is reachable through the actual hit grid"),
            Path.ContainsByPredicate([&](const FWidgetAndPointer& Entry) { return Entry.Widget == Slate; }))) return false;
        TSet<FKey> Keys;
        Keys.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        const bool HandledDown = Slate->OnMouseButtonDown(Geometry, Down).IsEventHandled();
        Keys.Reset();
        const FPointerEvent Up(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        const bool HandledUp = Slate->OnMouseButtonUp(Geometry, Up).IsEventHandled();
        return Test->TestTrue(TEXT("Actual Slate button handles mouse press"), HandledDown)
            && Test->TestTrue(TEXT("Actual Slate button handles mouse release"), HandledUp);
    }

    void Capture(const TCHAR* Name)
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/BattleRosterUI");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = Directory / Name;
        PreviousScreenshotTime = IFileManager::Get().GetTimeStamp(*ScreenshotPath);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    }
    bool CaptureFinished() const
    {
        return !FScreenshotRequest::IsScreenshotRequested()
            && IFileManager::Get().FileSize(*ScreenshotPath) > 0
            && IFileManager::Get().GetTimeStamp(*ScreenshotPath) != PreviousScreenshotTime;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleRosterUIRuntimeTest,
    "SilverChoir.BattleUI.Roster.RuntimeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleRosterUIRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleRosterUITest::FRuntime(this));
    return true;
}
#endif
