#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace SquadTransferUITests
{
const FName LocalTile(TEXT("SC_TRANSFER_UI_LOCAL"));
const FName RemoteTile(TEXT("SC_TRANSFER_UI_REMOTE"));

bool IsDisplayed(const UWidget* Widget)
{
    return Widget && Widget->GetVisibility() != ESlateVisibility::Hidden
        && Widget->GetVisibility() != ESlateVisibility::Collapsed;
}

struct FFixture
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UPlayerUnitSubsystem> UnitSystem;
    TWeakObjectPtr<UPlayerSquadSubsystem> SquadSystem;
    FObjectPropertyBase* UnitManagerProperty = nullptr;
    FObjectPropertyBase* SquadManagerProperty = nullptr;
    bool bManagersSwapped = false;
    TStrongObjectPtr<UPlayerUnitManagerBase> OriginalUnits;
    TStrongObjectPtr<UPlayerSquadManagerBase> OriginalSquads;
    TStrongObjectPtr<UPlayerUnitManagerBase> Units;
    TStrongObjectPtr<UPlayerSquadManagerBase> Squads;
    TStrongObjectPtr<UBaseMapWidget> Shell;
    TStrongObjectPtr<USquadMeetingRoomWidget> Room;
    TArray<FGuid> UnitIds;
    FGuid Source;
    FGuid Target;
    FGuid Third;
    FText Error;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
        if (!Test.TestNotNull(TEXT("Transfer UI test has a game world"), World.Get())) return false;
        UnitSystem = UPlayerUnitLibrary::GetPlayerUnitSubsystem(World.Get());
        SquadSystem = UPlayerSquadLibrary::GetPlayerSquadSubsystem(World.Get());
        if (!Test.TestNotNull(TEXT("Player unit subsystem exists"), UnitSystem.Get())
            || !Test.TestNotNull(TEXT("Player squad subsystem exists"), SquadSystem.Get())) return false;
        UnitManagerProperty = FindFProperty<FObjectPropertyBase>(UnitSystem->GetClass(), TEXT("Manager"));
        SquadManagerProperty = FindFProperty<FObjectPropertyBase>(SquadSystem->GetClass(), TEXT("Manager"));
        if (!Test.TestNotNull(TEXT("Unit manager reflection property exists"), UnitManagerProperty)
            || !Test.TestNotNull(TEXT("Squad manager reflection property exists"), SquadManagerProperty)) return false;
        OriginalUnits.Reset(UnitSystem->GetManager());
        OriginalSquads.Reset(SquadSystem->GetManager());
        // Keep the real world/subsystem lookup and real UMG assets, but isolate the data.
        // Unit data intentionally has no removal API; swapping temporary managers avoids
        // permanently adding fixture personnel to later tests in this same game process.
        Units.Reset(NewObject<UPlayerUnitManagerBase>(UnitSystem.Get()));
        Squads.Reset(NewObject<UPlayerSquadManagerBase>(SquadSystem.Get()));
        Squads->Initialize(Units.Get());
        UnitManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), Units.Get());
        SquadManagerProperty->SetObjectPropertyValue_InContainer(SquadSystem.Get(), Squads.Get());
        bManagersSwapped = true;
        TArray<FUnitData> Data;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FUnitData Unit;
            Unit.UnitId = FGuid::NewGuid();
            Unit.Profile.CodeName = FText::FromString(FString::Printf(TEXT("转队测试 %d"), Index + 1));
            Unit.RuntimeData.TileId = LocalTile;
            UnitIds.Add(Unit.UnitId);
            Data.Add(Unit);
        }
        if (!Test.TestTrue(TEXT("Create isolated canonical unit records"), Units->LoadUnitData(Data, Error))) return false;
        if (!Test.TestTrue(TEXT("Create source squad"), Squads->CreateSquad(FText::FromString(TEXT("原属银翼小队")), nullptr, LocalTile, Source, Error))
            || !Test.TestTrue(TEXT("Create target squad"), Squads->CreateSquad(FText::FromString(TEXT("目标守望小队")), nullptr, LocalTile, Target, Error))
            || !Test.TestTrue(TEXT("Create third squad"), Squads->CreateSquad(FText::FromString(TEXT("第三暮鸦小队")), nullptr, LocalTile, Third, Error))) return false;
        if (!Test.TestTrue(TEXT("Source captain belongs to source squad"), Squads->AddUnitToSquad(UnitIds[0], Source, Error))
            || !Test.TestTrue(TEXT("Source second member belongs to source squad"), Squads->AddUnitToSquad(UnitIds[1], Source, Error))) return false;
        UClass* ShellClass = LoadClass<UBaseMapWidget>(nullptr, TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C"));
        UClass* RoomClass = LoadClass<USquadMeetingRoomWidget>(nullptr,
            TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom.WBP_SquadMeetingRoom_C"));
        if (!Test.TestNotNull(TEXT("Base shell Blueprint is available"), ShellClass)
            || !Test.TestNotNull(TEXT("Meeting room Blueprint is available"), RoomClass)) return false;
        Shell.Reset(CreateWidget<UBaseMapWidget>(World->GetFirstPlayerController(), ShellClass));
        if (!Test.TestNotNull(TEXT("Create actual base shell"), Shell.Get())) return false;
        Shell->AddToViewport(10000);
        {
            TGuardValue<float> Duration(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->TransitionDuration, 0.f);
            Room.Reset(Cast<USquadMeetingRoomWidget>(Shell->CreateSceneUIByTag(FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom")))));
        }
        return Test.TestNotNull(TEXT("Create actual meeting room"), Room.Get())
            && Test.TestTrue(TEXT("Load local squad list"), Room->LoadSquadList(LocalTile));
    }

    UPersonnelListEntryWidget* FindUnit(FGuid Id) const
    {
        if (const auto* List = Room.IsValid() ? Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("PersonnelList"))) : nullptr)
            for (UWidget* Child : List->GetAllChildren())
                if (auto* Row = Cast<UPersonnelListEntryWidget>(Child); Row && Row->GetUnitData() && Row->GetUnitData()->UnitId == Id) return Row;
        return nullptr;
    }
    ~FFixture()
    {
        if (Shell.IsValid()) Shell->RemoveFromParent();
        Room.Reset(); Shell.Reset();
        if (Squads.IsValid()) Squads->Shutdown();
        if (bManagersSwapped && UnitSystem.IsValid() && UnitManagerProperty)
            UnitManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), OriginalUnits.Get());
        if (bManagersSwapped && SquadSystem.IsValid() && SquadManagerProperty)
            SquadManagerProperty->SetObjectPropertyValue_InContainer(SquadSystem.Get(), OriginalSquads.Get());
    }
};

class FExerciseTransfer final : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FAutomationTestBase* Test;
    int32 Step = 0;
    double StepStarted = FPlatformTime::Seconds();
    TWeakObjectPtr<UPersonnelListEntryWidget> OriginalRow;
    FString SuggestedName;
    int32 SquadCount = 0;
    FString CaptureTag = TEXT("Transfer");
    void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
    void Capture(const TCHAR* Suffix)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SquadUI")
            / (CaptureTag + TEXT("_") + Suffix + TEXT(".png")), true, false);
    }
    bool Choose(FGuid UnitId)
    {
        auto* Row = F->FindUnit(UnitId);
        if (!Test->TestNotNull(TEXT("Mounted personnel row exists"), Row)) return false;
        Test->TestTrue(TEXT("Row keeps original canonical unit allocation"), Row->GetUnitData().Get() == F->Units->GetUnitDataShared(UnitId).Get());
        Row->OnSelectionRequested.Broadcast(Row->ChoiceID);
        return true;
    }
    void CheckOwner(FGuid UnitId, FGuid Expected)
    {
        FSquadData Owner;
        Test->TestTrue(TEXT("Unit has formal ownership"), F->Squads->GetUnitSquad(UnitId, Owner));
        Test->TestEqual(TEXT("Formal ownership changes only on successful save"), Owner.SquadId, Expected);
    }
    bool Click(const TCHAR* Name)
    {
        auto* Button = Cast<UBasicButtonWidget>(F->Room->GetWidgetFromName(Name));
        if (!Test->TestNotNull(Name, Button)) return false;
        Test->TestTrue(FString(Name) + TEXT(" is enabled"), Button->GetIsEnabled());
        Button->OnClicked.Broadcast();
        return true;
    }
public:
    FExerciseTransfer(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest) : F(InFixture), Test(InTest)
    {
        FParse::Value(FCommandLine::Get(), TEXT("SquadTransferCaptureTag="), CaptureTag);
        CaptureTag = FPaths::MakeValidFileName(CaptureTag);
    }
    bool Update() override
    {
        auto* Room = F->Room.Get();
        if (!Room || !F->World.IsValid()) { Test->AddError(TEXT("Lost transfer test world/widget.")); return true; }
        const double Elapsed = FPlatformTime::Seconds() - StepStarted;
        if (Elapsed > 12.) { Test->AddError(FString::Printf(TEXT("Transfer test timed out at phase %d."), Step)); return true; }
        if (Room->bTransitioning || Elapsed < .12) return false;
        FSquadData Draft;
        switch (Step)
        {
        case 0:
            if (!Test->TestTrue(TEXT("Open target squad draft"), Room->SelectSquad(F->Target))) return true;
            Next(); return false;
        case 1:
        {
            auto* Row = F->FindUnit(F->UnitIds[0]);
            if (!Test->TestNotNull(TEXT("Foreign member is listed"), Row)) return true;
            OriginalRow = Row;
            Test->TestTrue(TEXT("Foreign member subtitle displays original squad"), Row->ButtonSubtitle.ToString().Contains(TEXT("原属银翼小队")));
            auto* Detail = Cast<UTextBlock>(Row->GetWidgetFromName(TEXT("DetailLabel")));
            if (Test->TestNotNull(TEXT("Foreign squad subtitle has text widget"), Detail))
            {
                const FLinearColor Color = Detail->GetColorAndOpacity().GetSpecifiedColor();
                Test->TestTrue(TEXT("Foreign squad subtitle uses dark gold, not default blue"), Color.R > Color.G && Color.G > Color.B && Color.R < .95f);
            }
            if (!Choose(F->UnitIds[0])) return true;
            Test->TestTrue(TEXT("Clicking foreign member opens confirmation"), Room->IsUnitTransferPending());
            Test->TestEqual(TEXT("Pending request retains exact unit identity"), Room->GetPendingUnitTransferId(), F->UnitIds[0]);
            Test->TestTrue(TEXT("Actual confirmation panel is visible"), IsDisplayed(Room->GetWidgetFromName(TEXT("TransferConfirmPanel"))));
            auto* Message = Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("TransferConfirmText")));
            if (Test->TestNotNull(TEXT("Confirmation text is bound"), Message))
                Test->TestTrue(TEXT("Confirmation names original squad"), Message->GetText().ToString().Contains(TEXT("原属银翼小队")));
            Room->GetSelectedSquad(Draft);
            Test->TestFalse(TEXT("Unconfirmed member is not in draft"), Draft.MemberUnitIds.Contains(F->UnitIds[0]));
            CheckOwner(F->UnitIds[0], F->Source);
            Test->TestFalse(TEXT("Modal blocks rename"), Room->RenameSelectedSquad(FText::FromString(TEXT("Blocked name"))));
            Test->TestFalse(TEXT("Modal blocks save"), Room->SaveSelectedSquad());
            Test->TestFalse(TEXT("Modal blocks programmatic addition of another member"), Room->AddUnitToSelectedSquad(F->UnitIds[2]));
            if (!Click(TEXT("CancelUnitTransferButton"))) return true;
            Test->TestFalse(TEXT("Cancel button clears pending request"), Room->IsUnitTransferPending());
            Test->TestFalse(TEXT("Cancel hides modal"), IsDisplayed(Room->GetWidgetFromName(TEXT("TransferConfirmPanel"))));
            Room->GetSelectedSquad(Draft);
            Test->TestTrue(TEXT("Cancel leaves draft member list unchanged"), Draft.MemberUnitIds.IsEmpty());
            CheckOwner(F->UnitIds[0], F->Source);
            if (!Choose(F->UnitIds[0])) return true;
            Test->TestTrue(TEXT("Source may be renamed while prompt is open"), F->Squads->UpdateSquadInfo(F->Source, FText::FromString(TEXT("更名后的原小队")), nullptr, F->Error));
            Capture(TEXT("01_Confirm"));
            Next(); return false;
        }
        case 2:
        {
            auto* Message = Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("TransferConfirmText")));
            if (Test->TestNotNull(TEXT("Rename keeps confirmation label"), Message))
                Test->TestTrue(TEXT("Prompt refreshes renamed source squad"), Message->GetText().ToString().Contains(TEXT("更名后的原小队")));
            if (!Click(TEXT("ConfirmUnitTransferButton"))) return true;
            Test->TestFalse(TEXT("Confirm closes prompt"), Room->IsUnitTransferPending());
            Room->GetSelectedSquad(Draft);
            Test->TestTrue(TEXT("Confirm adds member to draft"), Draft.MemberUnitIds.Contains(F->UnitIds[0]));
            CheckOwner(F->UnitIds[0], F->Source);
            Capture(TEXT("02_Draft"));
            Next(); return false;
        }
        case 3:
            if (auto* Row = F->FindUnit(F->UnitIds[0]))
            {
                Test->TestTrue(TEXT("Confirmed unsaved member still displays original squad"), Row->ButtonSubtitle.ToString().Contains(TEXT("更名后的原小队")));
                if (auto* Detail = Cast<UTextBlock>(Row->GetWidgetFromName(TEXT("DetailLabel"))))
                {
                    const FLinearColor Color = Detail->GetColorAndOpacity().GetSpecifiedColor();
                    Test->TestTrue(TEXT("Confirmed unsaved source remains dark gold until save"), Color.R > Color.G && Color.G > Color.B);
                }
            }
            Test->TestTrue(TEXT("Confirmation preserves mounted row rather than flashing list"), OriginalRow.Get() == F->FindUnit(F->UnitIds[0]));
            if (!Test->TestTrue(TEXT("Saving commits approved transfer"), Room->SaveSelectedSquad())) return true;
            CheckOwner(F->UnitIds[0], F->Target);
            Next(); return false;
        case 4:
            Test->TestEqual(TEXT("Save returns to squad list"), Room->CurrentLayer, ESquadRoomLayer::SquadSelection);
            Test->TestTrue(TEXT("Reopen target"), Room->SelectSquad(F->Target));
            Next(); return false;
        case 5:
            if (auto* SavedRow = F->FindUnit(F->UnitIds[0]))
                if (auto* Detail = Cast<UTextBlock>(SavedRow->GetWidgetFromName(TEXT("DetailLabel"))))
                {
                    const FLinearColor Color = Detail->GetColorAndOpacity().GetSpecifiedColor();
                    Test->TestFalse(TEXT("After save current-squad member no longer has foreign-squad gold"), Color.R > Color.G && Color.G > Color.B);
                }
            if (!Choose(F->UnitIds[1])) return true;
            Test->TestTrue(TEXT("Second source member requests confirmation"), Room->IsUnitTransferPending());
            Test->TestTrue(TEXT("Another system transfers source before confirmation"), F->Squads->AddUnitToSquad(F->UnitIds[1], F->Third, F->Error));
            Test->TestFalse(TEXT("Stale source ownership cannot be confirmed"), Room->ConfirmPendingUnitTransfer());
            Test->TestFalse(TEXT("Failed confirmation cancels stale request"), Room->IsUnitTransferPending());
            Room->GetSelectedSquad(Draft);
            Test->TestFalse(TEXT("Stale confirmation never adds unit"), Draft.MemberUnitIds.Contains(F->UnitIds[1]));
            CheckOwner(F->UnitIds[1], F->Third);
            Next(); return false;
        case 6:
            if (!Choose(F->UnitIds[1])) return true;
            Test->TestTrue(TEXT("Confirm current source identity"), Room->ConfirmPendingUnitTransfer());
            Test->TestTrue(TEXT("Source changes after confirmation but before save"), F->Squads->AddUnitToSquad(F->UnitIds[1], F->Source, F->Error));
            Test->TestFalse(TEXT("Save refuses transfer from a newly unconfirmed source"), Room->SaveSelectedSquad());
            CheckOwner(F->UnitIds[1], F->Source);
            Room->ReturnToSquadList();
            Next(); return false;
        case 7:
        {
            SquadCount = F->Squads->GetSquadIds().Num();
            SuggestedName = F->Squads->GetNextSquadName().ToString();
            FGuid CreatedId = FGuid::NewGuid();
            Test->TestTrue(TEXT("Empty draft name uses suggestion"), Room->CreateSquadAtTile(LocalTile, FText::GetEmpty(), CreatedId));
            Test->TestFalse(TEXT("New unsaved draft has no official ID"), CreatedId.IsValid());
            Room->GetSelectedSquad(Draft);
            Test->TestEqual(TEXT("Draft uses current suggested name"), Draft.SquadName.ToString(), SuggestedName);
            Test->TestEqual(TEXT("Unsaved draft creates no official squad"), F->Squads->GetSquadIds().Num(), SquadCount);
            Next(); return false;
        }
        case 8:
            Room->ReturnToSquadList();
            Next(); return false;
        case 9:
            Test->TestEqual(TEXT("Cancel never consumes suggested name"), F->Squads->GetNextSquadName().ToString(), SuggestedName);
            Test->TestEqual(TEXT("Canceled draft never creates a squad"), F->Squads->GetSquadIds().Num(), SquadCount);
            Test->TestTrue(TEXT("Reopen target for pending-location check"), Room->SelectSquad(F->Target));
            Next(); return false;
        case 10:
            if (!Choose(F->UnitIds[1])) return true;
            Test->TestTrue(TEXT("Source squad can move while prompt is open"), F->Squads->SetSquadTileId(F->Source, RemoteTile, F->Error));
            Test->TestFalse(TEXT("Out-of-tile unit cannot be confirmed"), Room->ConfirmPendingUnitTransfer());
            Test->TestFalse(TEXT("Location failure clears request"), Room->IsUnitTransferPending());
            Test->TestTrue(TEXT("Restore source to local tile"), F->Squads->SetSquadTileId(F->Source, LocalTile, F->Error));
            Next(); return false;
        case 11:
            if (!Choose(F->UnitIds[1])) return true;
            Test->TestTrue(TEXT("Open prompt before room unload"), Room->IsUnitTransferPending());
            Room->UnloadSceneUI();
            Test->TestFalse(TEXT("Unload cancels pending transfer"), Room->IsUnitTransferPending());
            Test->TestFalse(TEXT("Closed room rejects late confirmation"), Room->ConfirmPendingUnitTransfer());
            CheckOwner(F->UnitIds[1], F->Source);
            return true;
        }
        Test->AddError(TEXT("Unknown transfer test phase."));
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadTransferUITest, "SilverChoir.BaseUI.SquadMeetingRoom.TransferConfirmation",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSquadTransferUITest::RunTest(const FString& Parameters)
{
    const TSharedRef<SquadTransferUITests::FFixture> Fixture = MakeShared<SquadTransferUITests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadTransferUITests::FExerciseTransfer(Fixture, this));
    return true;
}
#endif
