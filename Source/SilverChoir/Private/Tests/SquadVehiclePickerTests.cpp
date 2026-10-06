#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Data/Vehicles/VehicleDataLibrary.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadMemberCardWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadVehicleEntryWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"

namespace SquadVehiclePickerTests
{
    const FName LocalTile(TEXT("SC_VEHICLE_PICKER_TEST_LOCAL"));
    const FName RemoteTile(TEXT("SC_VEHICLE_PICKER_TEST_REMOTE"));

    bool IsDisplayed(const UWidget* Widget)
    {
        return Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed
            && Widget->GetVisibility() != ESlateVisibility::Hidden;
    }

    bool SameVehicle(const FVehicleData& A, const FVehicleData& B)
    {
        return FVehicleData::StaticStruct()->CompareScriptStruct(&A, &B, 0);
    }

    bool SameVehicles(const TArray<FVehicleData>& A, const TArray<FVehicleData>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (!SameVehicle(A[Index], B[Index])) return false;
        return true;
    }

    // No test units are inserted into the canonical store. Existing capacity tests cover
    // member trimming; this fixture only creates removable squads and disposable vehicle data.
    struct FFixture
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<UPlayerSquadManagerBase> Squads;
        TWeakObjectPtr<UPlayerUnitManagerBase> Units;
        TStrongObjectPtr<UBaseMapWidget> Shell;
        TStrongObjectPtr<USquadMeetingRoomWidget> Room;
        TArray<FVehicleData> Vehicles;
        TArray<FGuid> CreatedSquads;
        TArray<FGuid> CreatedVehicles;
        FGuid Alpha;
        FGuid OccupiedSquad;
        FVehicleData RemoteVehicle;
        FVehicleData OccupiedVehicle;
        FText Error;

        bool Initialize(FAutomationTestBase& Test)
        {
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
            if (!Test.TestNotNull(TEXT("Vehicle picker runs in a dedicated game world"), World.Get())) return false;
            Squads = UPlayerSquadLibrary::GetPlayerSquadManager(World.Get());
            Units = UPlayerUnitLibrary::GetPlayerUnitManager(World.Get());
            if (!Test.TestNotNull(TEXT("Player squad manager is available"), Squads.Get())) return false;
            if (!Test.TestNotNull(TEXT("Player vehicle store is available"), Units.Get())) return false;

            auto* Table = LoadObject<UDataTable>(nullptr,
                TEXT("/Game/System/Data/Vehicles/DT_VehicleTemplates.DT_VehicleTemplates"));
            if (!Test.TestNotNull(TEXT("Vehicle template table exists for test-only data generation"), Table)) return false;
            const TArray<FName> Rows = {TEXT("SportsCar_2Seat"), TEXT("Sedan_4Seat"), TEXT("SUV_6Seat")};
            if (!Test.TestTrue(TEXT("Fixture explicitly generates three choices through the table factory"),
                UVehicleDataLibrary::CreateVehicleDataFromTable(Table, Rows, Vehicles, Error, LocalTile))) return false;
            if (!Test.TestEqual(TEXT("Factory generated all requested choices"), Vehicles.Num(), 3)) return false;
            for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
            {
                Test.TestEqual(TEXT("Fixture uses two, four and six seat templates"), Vehicles[Index].Attributes.PassengerCapacity, 2 + Index * 2);
                Test.TestNotNull(TEXT("Each supplied vehicle includes its real 16:9 preview texture"), Vehicles[Index].Profile.PreviewImage.Get());
            }
            RemoteVehicle = Vehicles[0];
            RemoteVehicle.VehicleId = FGuid::NewGuid();
            RemoteVehicle.Profile.VehicleName = FText::FromString(TEXT("异地车辆（隐藏）"));
            RemoteVehicle.RuntimeData.TileId = RemoteTile;
            OccupiedVehicle = Vehicles[1];
            OccupiedVehicle.VehicleId = FGuid::NewGuid();
            OccupiedVehicle.Profile.VehicleName = FText::FromString(TEXT("已分配车辆（禁用）"));

            auto AddSquad = [&](const TCHAR* Name, FGuid& OutId)
            {
                const bool bCreated = Squads->CreateSquad(FText::FromString(Name), nullptr, LocalTile, OutId, Error);
                if (bCreated) CreatedSquads.Add(OutId);
                return bCreated;
            };
            if (!Test.TestTrue(TEXT("Create disposable vehicle-picker squads"),
                AddSquad(TEXT("车辆选择测试小队"), Alpha) && AddSquad(TEXT("车辆占用测试小队"), OccupiedSquad))) return false;
            UClass* ShellClass = LoadClass<UBaseMapWidget>(nullptr,
                TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C"));
            UClass* RoomClass = LoadClass<USquadMeetingRoomWidget>(nullptr,
                TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom.WBP_SquadMeetingRoom_C"));
            if (!Test.TestNotNull(TEXT("Real base shell exists"), ShellClass)
                || !Test.TestNotNull(TEXT("Real squad meeting room exists"), RoomClass)) return false;
            Shell.Reset(CreateWidget<UBaseMapWidget>(World->GetFirstPlayerController(), ShellClass));
            if (!Test.TestNotNull(TEXT("Base shell can be mounted"), Shell.Get())) return false;
            Shell->SetCurrencyAmount(128450);
            Shell->AddToViewport(10000);
            {
                // Zero duration makes the test deterministic without modifying or saving a Blueprint.
                TGuardValue<float> DurationOverride(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->TransitionDuration, 0.f);
                Room.Reset(Cast<USquadMeetingRoomWidget>(Shell->CreateSceneUIByTag(
                    FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom")))));
            }
            if (!Test.TestNotNull(TEXT("Shell creates the real meeting room"), Room.Get())) return false;
            return Test.TestTrue(TEXT("Test room accepts an explicit local strategic tile"), Room->LoadSquadList(LocalTile));
        }

        TArray<FVehicleData> AllChoices() const
        {
            TArray<FVehicleData> Result = Vehicles;
            Result.Add(RemoteVehicle);
            Result.Add(OccupiedVehicle);
            return Result;
        }

        bool StoreVehicles(const TArray<FVehicleData>& Data)
        {
            if (!Units->LoadVehicleData(Data, Error)) return false;
            for (const FVehicleData& Vehicle : Data) CreatedVehicles.AddUnique(Vehicle.VehicleId);
            return true;
        }

        USquadVehicleEntryWidget* FindVehicle(FGuid VehicleId) const
        {
            if (const auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("VehicleOptions"))))
                for (UWidget* Child : List->GetAllChildren())
                    if (auto* Entry = Cast<USquadVehicleEntryWidget>(Child); Entry && Entry->GetVehicleId() == VehicleId)
                        return Entry;
            return nullptr;
        }

        ~FFixture()
        {
            if (Shell.IsValid()) Shell->RemoveFromParent();
            Room.Reset();
            Shell.Reset();
            if (Squads.IsValid())
                for (const FGuid Id : CreatedSquads) Squads->RemoveSquad(Id, Error);
            if (Units.IsValid())
                for (const FGuid Id : CreatedVehicles) Units->RemoveVehicleData(Id, Error);
        }
    };

    class FExercisePicker final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();
        FString CaptureTag = TEXT("VehiclePicker");
        TStrongObjectPtr<USquadVehicleEntryWidget> StaleRow;
        TWeakObjectPtr<USquadVehicleEntryWidget> RetainedRow;
        FVehicleData SavedVehicle;
        int32 SquadCountBeforeDraft = 0;
        bool bPreviewCaptured = false;
        int32 LiveMovePhase = 0;

        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
        bool Click(const TCHAR* Name)
        {
            auto* Button = Cast<UBasicButtonWidget>(Fixture->Room->GetWidgetFromName(Name));
            if (!Test->TestNotNull(Name, Button)
                || !Test->TestTrue(FString(Name) + TEXT(" is enabled"), Button->GetIsEnabled())) return false;
            Button->OnClicked.Broadcast();
            return true;
        }
        void Capture(const TCHAR* Suffix)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SquadUI")
                / (CaptureTag + TEXT("_") + Suffix + TEXT(".png")), true, false);
        }
        void CheckDraftCapacity(int32 Expected)
        {
            auto* Room = Fixture->Room.Get();
            Test->TestEqual(TEXT("Draft capacity follows selected vehicle"), Room->GetSelectedSquadCapacity(), Expected);
            auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList")));
            if (!Test->TestNotNull(TEXT("Vehicle choice updates the real member strip"), List)) return;
            Test->TestEqual(TEXT("Every available vehicle seat has one mounted slot"), List->GetChildrenCount(), Expected);
            for (UWidget* Child : List->GetAllChildren())
                if (auto* Card = Cast<USquadMemberCardWidget>(Child))
                    Test->TestTrue(TEXT("Test has empty seats without adding duplicate unit records"), Card->IsEmptySlot());
                else Test->AddError(TEXT("Member strip contains a non-seat widget."));
        }
        void CheckCanonicalVehicle(const FVehicleData& Expected)
        {
            FSquadData Official;
            if (Test->TestTrue(TEXT("Canonical squad remains accessible"), Fixture->Squads->GetSquad(Fixture->Alpha, Official)))
                Test->TestTrue(TEXT("Only a successful save changes the canonical assigned vehicle"), SameVehicle(Official.AssignedVehicle, Expected));
        }
        void CheckPopupGeometry()
        {
            auto* Room = Fixture->Room.Get();
            UWidget* Card = Room->GetWidgetFromName(TEXT("VehiclePickerFit"));
            if (!Card) Card = Room->GetWidgetFromName(TEXT("VehiclePickerCard"));
            if (!Card) Card = Room->GetWidgetFromName(TEXT("VehiclePickerPanel"));
            auto* Options = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("VehicleOptions")));
            if (!Test->TestNotNull(TEXT("Vehicle picker has a positioned container"), Card)
                || !Test->TestNotNull(TEXT("Vehicle choices use a scrolling container"), Options)) return;
            const FGeometry& Root = Room->GetCachedGeometry();
            const FGeometry& Popup = Card->GetCachedGeometry();
            const FVector2D Min = Root.AbsoluteToLocal(Popup.LocalToAbsolute(FVector2D::ZeroVector));
            const FVector2D Max = Root.AbsoluteToLocal(Popup.LocalToAbsolute(Popup.GetLocalSize()));
            const FVector2D Size = Root.GetLocalSize();
            Test->TestTrue(TEXT("Vehicle picker receives nonzero real layout geometry"), Max.X > Min.X && Max.Y > Min.Y);
            Test->TestTrue(TEXT("Vehicle picker remains inside the room at the current viewport size"),
                Min.X >= -1. && Min.Y >= -1. && Max.X <= Size.X + 1. && Max.Y <= Size.Y + 1.);
            Test->TestTrue(TEXT("Vehicle list retains positive scroll viewport height"), Options->GetCachedGeometry().GetLocalSize().Y > 40.);
            if (auto* First = Fixture->FindVehicle(Fixture->Vehicles[0].VehicleId))
            {
                auto* Image = Cast<UImage>(First->GetWidgetFromName(TEXT("VehicleImage")));
                if (Test->TestNotNull(TEXT("Vehicle card uses the image binding"), Image))
                {
                    Test->TestTrue(TEXT("Vehicle card displays the supplied image resource"),
                        Image->GetBrush().GetResourceObject() == Fixture->Vehicles[0].Profile.PreviewImage.Get());
                    const FVector2D ImageSize = Image->GetCachedGeometry().GetLocalSize();
                    Test->TestTrue(TEXT("Vehicle card preview preserves a 16:9 frame"),
                        ImageSize.Y > 0. && FMath::IsNearlyEqual(ImageSize.X / ImageSize.Y, 16. / 9., 0.04));
                }
            }
        }

    public:
        FExercisePicker(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest)
        {
            FParse::Value(FCommandLine::Get(), TEXT("VehiclePickerCaptureTag="), CaptureTag);
            CaptureTag = FPaths::MakeValidFileName(CaptureTag);
        }

        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            if (!Room || !Fixture->World.IsValid()) { Test->AddError(TEXT("Vehicle-picker fixture was lost.")); return true; }
            const double Elapsed = FPlatformTime::Seconds() - StepStarted;
            if (Elapsed > 12.) { Test->AddError(FString::Printf(TEXT("Vehicle picker timed out at step %d."), Step)); return true; }
            if (Room->bTransitioning || Elapsed < 0.15) return false;
            FSquadData Draft;
            TArray<FGuid> Removed;
            switch (Step)
            {
            case 0:
                Test->TestTrue(TEXT("Production widget starts without any automatically generated vehicles"), Room->GetAvailableVehicles().IsEmpty());
                if (!Test->TestTrue(TEXT("Open an existing local squad"), Room->SelectSquad(Fixture->Alpha))) return true;
                Next(); return false;
            case 1:
            {
                CheckDraftCapacity(4);
                if (!Click(TEXT("ChooseVehicleButton"))) return true;
                Test->TestTrue(TEXT("Choose vehicle opens the built-in picker"), Room->IsVehiclePickerVisible());
                auto* Empty = Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("VehicleEmptyText")));
                Test->TestTrue(TEXT("An empty supplied source has a visible explanatory message"), IsDisplayed(Empty) && !Empty->GetText().IsEmpty());
                if (auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("VehicleOptions"))))
                    Test->TestEqual(TEXT("Empty source creates no implicit template-derived vehicles"), List->GetChildrenCount(), 0);
                Test->TestTrue(TEXT("Fixture registers vehicles in the authoritative subsystem"), Fixture->StoreVehicles(Fixture->AllChoices()));
                Test->TestTrue(TEXT("Another squad owns one local vehicle"),
                    Fixture->Squads->SetSquadVehicle(Fixture->OccupiedSquad, Fixture->OccupiedVehicle, Removed, Fixture->Error));
                Test->TestTrue(TEXT("Blueprint-facing node reads vehicles using the explicit tile"), Room->LoadAvailableVehicles(LocalTile));
                const auto Before = Room->GetAvailableVehicles();
                auto Invalid = Before;
                Invalid.Last().VehicleId.Invalidate();
                Test->TestFalse(TEXT("An invalid ID rejects the complete replacement batch"), Fixture->StoreVehicles(Invalid));
                Test->TestTrue(TEXT("Invalid replacement leaves the previous source unchanged"), SameVehicles(Room->GetAvailableVehicles(), Before));
                Invalid = Before;
                Invalid.Last().VehicleId = Invalid[0].VehicleId;
                Test->TestFalse(TEXT("Duplicate IDs reject the complete replacement batch"), Fixture->StoreVehicles(Invalid));
                Test->TestTrue(TEXT("Duplicate replacement leaves the previous source unchanged"), SameVehicles(Room->GetAvailableVehicles(), Before));
                Test->TestFalse(TEXT("A missing choice cannot be selected by the Blueprint node"), Room->SelectAvailableVehicle(FGuid::NewGuid()));
                Test->TestFalse(TEXT("A vehicle at another strategic tile cannot be selected"), Room->SelectAvailableVehicle(Fixture->RemoteVehicle.VehicleId));
                Test->TestFalse(TEXT("A vehicle owned by another squad cannot be selected"), Room->SelectAvailableVehicle(Fixture->OccupiedVehicle.VehicleId));
                Test->TestNull(TEXT("Foreign-tile vehicles are absent from the rendered list"), Fixture->FindVehicle(Fixture->RemoteVehicle.VehicleId));
                if (auto* Row = Fixture->FindVehicle(Fixture->OccupiedVehicle.VehicleId))
                    Test->TestFalse(TEXT("Other squads' local vehicles remain visibly disabled"), Row->GetIsEnabled());
                else Test->AddError(TEXT("Occupied local vehicle should remain visible as a disabled row."));
                Test->TestEqual(TEXT("Only the four local records are returned"), Before.Num(), 4);
                Room->LoadAvailableVehicles(LocalTile); // Clears error for the visual capture.
                Next(); return false;
            }
            case 2:
                if (LiveMovePhase == 0)
                {
                    CheckPopupGeometry();
                    Capture(TEXT("01_Choices"));
                    if (auto Shared = Fixture->Units->GetVehicleDataShared(Fixture->RemoteVehicle.VehicleId))
                        Shared->Modify([](FVehicleData& Data) { Data.RuntimeData.TileId = LocalTile; });
                    LiveMovePhase = 1;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                if (LiveMovePhase == 1)
                {
                    Test->TestNotNull(TEXT("Vehicle notification adds newly arrived local vehicles without reopening"),
                        Fixture->FindVehicle(Fixture->RemoteVehicle.VehicleId));
                    if (auto Shared = Fixture->Units->GetVehicleDataShared(Fixture->RemoteVehicle.VehicleId))
                        Shared->Modify([](FVehicleData& Data) { Data.RuntimeData.TileId = RemoteTile; });
                    LiveMovePhase = 2;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                Test->TestNull(TEXT("Vehicle notification hides departed vehicles without reopening"),
                    Fixture->FindVehicle(Fixture->RemoteVehicle.VehicleId));
                Next(); return false;
            case 3:
            {
                Room->SetIconPickerVisible(true);
                Test->TestFalse(TEXT("Opening the emblem picker closes the vehicle picker"), Room->IsVehiclePickerVisible());
                Test->TestTrue(TEXT("Emblem picker is visible"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Room->SetVehiclePickerVisible(true);
                Test->TestFalse(TEXT("Opening the vehicle picker closes the emblem picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                StaleRow.Reset(Fixture->FindVehicle(Fixture->Vehicles[0].VehicleId));
                RetainedRow = Fixture->FindVehicle(Fixture->Vehicles[1].VehicleId);
                if (!Test->TestNotNull(TEXT("A supplied vehicle has a real interactive row"), StaleRow.Get())) return true;
                const FKeyEvent Press(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
                const TSharedRef<SWidget> SlateRow = StaleRow->TakeWidget();
                Test->TestTrue(TEXT("Vehicle row accepts delayed keyboard activation"), SlateRow->OnKeyDown(StaleRow->GetCachedGeometry(), Press).IsEventHandled());
                SlateRow->OnKeyUp(StaleRow->GetCachedGeometry(), Press);
                Test->TestTrue(TEXT("Vehicle activation is pending before source replacement"), StaleRow->IsPressPending());
                Test->TestTrue(TEXT("Store can remove an unassigned available vehicle"),
                    Fixture->Units->RemoveVehicleData(Fixture->Vehicles[0].VehicleId, Fixture->Error));
                Test->TestFalse(TEXT("Source replacement immediately cancels pending vehicle activation"), StaleRow->IsPressPending());
                Room->LoadAvailableVehicles(LocalTile);
                Test->TestTrue(TEXT("Unchanged choices reuse their mounted row objects"),
                    Fixture->FindVehicle(Fixture->Vehicles[1].VehicleId) == RetainedRow.Get());
                StaleRow->OnSelectionRequested.Broadcast(StaleRow->ChoiceID);
                Test->TestTrue(TEXT("Late callbacks from removed source rows cannot assign a vehicle"),
                    Room->GetSelectedSquad(Draft) && !Draft.AssignedVehicle.VehicleId.IsValid());
                Test->TestFalse(TEXT("Removed IDs are rejected by programmatic selection too"), Room->SelectAvailableVehicle(Fixture->Vehicles[0].VehicleId));
                Test->TestTrue(TEXT("Restore fixture vehicle to canonical store"), Fixture->StoreVehicles({Fixture->Vehicles[0]}));
                Room->LoadAvailableVehicles(LocalTile);
                Test->TestTrue(TEXT("Select the two-seat vehicle through the public node"), Room->SelectAvailableVehicle(Fixture->Vehicles[0].VehicleId));
                Test->TestFalse(TEXT("Successful selection closes the popup"), Room->IsVehiclePickerVisible());
                CheckCanonicalVehicle(FVehicleData());
                Next(); return false;
            }
            case 4:
                if (bPreviewCaptured)
                {
                    Test->TestTrue(TEXT("Switch to the four-seat choice"), Room->SelectAvailableVehicle(Fixture->Vehicles[1].VehicleId));
                    Next(); return false;
                }
                CheckDraftCapacity(2);
                Test->TestTrue(TEXT("Selection updates the draft vehicle record"),
                    Room->GetSelectedSquad(Draft) && SameVehicle(Draft.AssignedVehicle, Fixture->Vehicles[0]));
                if (auto* Preview = Cast<UImage>(Room->GetWidgetFromName(TEXT("VehiclePreviewImage"))))
                {
                    Test->TestTrue(TEXT("Management preview displays the selected vehicle texture"),
                        IsDisplayed(Preview) && Preview->GetBrush().GetResourceObject() == Fixture->Vehicles[0].Profile.PreviewImage.Get());
                    const FVector2D Size = Preview->GetCachedGeometry().GetLocalSize();
                    Test->TestTrue(TEXT("Management vehicle image is also 16:9"), Size.Y > 0 && FMath::IsNearlyEqual(Size.X / Size.Y, 16. / 9., .01));
                }
                else Test->AddError(TEXT("Management panel is missing VehiclePreviewImage."));
                Capture(TEXT("02_SelectedPreview"));
                bPreviewCaptured = true;
                return false; // Let the requested screenshot render before changing selection.
            case 5:
                CheckDraftCapacity(4);
                Room->SetVehiclePickerVisible(true);
                Test->TestTrue(TEXT("Another squad may claim a previously available vehicle while the picker is open"),
                    Fixture->Squads->SetSquadVehicle(Fixture->OccupiedSquad, Fixture->Vehicles[2], Removed, Fixture->Error));
                Test->TestFalse(TEXT("Selection revalidates live ownership before the deferred view refresh"), Room->SelectAvailableVehicle(Fixture->Vehicles[2].VehicleId));
                Test->TestTrue(TEXT("Rejected ownership race preserves the current draft choice"),
                    Room->GetSelectedSquad(Draft) && Draft.AssignedVehicle.VehicleId == Fixture->Vehicles[1].VehicleId);
                Test->TestTrue(TEXT("Restore the other squad's original vehicle"),
                    Fixture->Squads->SetSquadVehicle(Fixture->OccupiedSquad, Fixture->OccupiedVehicle, Removed, Fixture->Error));
                Test->TestTrue(TEXT("Select the released six-seat vehicle"), Room->SelectAvailableVehicle(Fixture->Vehicles[2].VehicleId));
                Next(); return false;
            case 6:
            {
                CheckDraftCapacity(6);
                CheckCanonicalVehicle(FVehicleData());
                auto Updated = Fixture->Vehicles;
                Updated[2].Profile.VehicleName = FText::FromString(TEXT("远征越野车 · 更新资料"));
                Updated[2].RuntimeData.CurrentFuel = 42.f;
                Test->TestTrue(TEXT("Same-ID vehicle payloads can be refreshed in the manager"), Fixture->StoreVehicles(Updated));
                Test->TestTrue(TEXT("Re-selecting the same ID applies its latest supplied payload"), Room->SelectAvailableVehicle(Updated[2].VehicleId));
                Test->TestTrue(TEXT("Updated source payload appears in the editable draft"),
                    Room->GetSelectedSquad(Draft) && SameVehicle(Draft.AssignedVehicle, Updated[2]));
                SavedVehicle = Updated[2];
                Test->TestTrue(TEXT("Loading a tile without vehicles clears displayed choices"), Room->LoadAvailableVehicles(TEXT("SC_VEHICLE_PICKER_EMPTY")));
                Test->TestTrue(TEXT("Changing the visible tile preserves the already selected draft vehicle"),
                    Room->GetSelectedSquad(Draft) && SameVehicle(Draft.AssignedVehicle, SavedVehicle));
                Room->SetVehiclePickerVisible(true);
                Test->TestTrue(TEXT("Empty list does not prevent opening the vehicle picker"), Room->IsVehiclePickerVisible());
                Test->TestTrue(TEXT("Empty source displays its message again"), IsDisplayed(Room->GetWidgetFromName(TEXT("VehicleEmptyText"))));
                if (!Test->TestTrue(TEXT("Saving commits the selected vehicle even when the choice source has been cleared"), Room->SaveSelectedSquad())) return true;
                Test->TestFalse(TEXT("Save closes the vehicle picker before returning"), Room->IsVehiclePickerVisible());
                Next(); return false;
            }
            case 7:
                Test->TestTrue(TEXT("Successful save returns to squad selection"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                CheckCanonicalVehicle(SavedVehicle);
                Test->TestEqual(TEXT("Saved vehicle updates canonical capacity"), Fixture->Squads->GetSquadMaxMemberCount(Fixture->Alpha), 6);
                Test->TestTrue(TEXT("Reopen saved squad for cancel regression"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 8:
                Test->TestTrue(TEXT("Restore current-tile choices for the next editing session"), Room->LoadAvailableVehicles(LocalTile));
                Test->TestTrue(TEXT("Changing saved squad to a smaller vehicle remains a draft"), Room->SelectAvailableVehicle(Fixture->Vehicles[0].VehicleId));
                Room->SetVehiclePickerVisible(true);
                Room->ReturnToSquadList();
                Test->TestFalse(TEXT("Returning without save closes the popup"), Room->IsVehiclePickerVisible());
                Next(); return false;
            case 9:
                CheckCanonicalVehicle(SavedVehicle);
                Test->TestTrue(TEXT("Cancelled editing returns to the squad list"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                Test->TestTrue(TEXT("Reopen to clear the vehicle explicitly"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 10:
                Room->SetVehiclePickerVisible(true);
                if (!Click(TEXT("CloseVehiclePickerButton"))) return true;
                if (!Click(TEXT("ClearVehicleButton"))) return true;
                Test->TestTrue(TEXT("Explicit no-vehicle selection only clears the draft"),
                    Room->GetSelectedSquad(Draft) && !Draft.AssignedVehicle.VehicleId.IsValid());
                CheckCanonicalVehicle(SavedVehicle);
                Next(); return false;
            case 11:
                CheckDraftCapacity(4);
                Room->ReturnToSquadList();
                Next(); return false;
            case 12:
            {
                CheckCanonicalVehicle(SavedVehicle);
                SquadCountBeforeDraft = Fixture->Squads->GetSquadIds().Num();
                FGuid NewId;
                Test->TestTrue(TEXT("Create a disposable new squad draft"),
                    Room->CreateSquadAtTile(LocalTile, FText::FromString(TEXT("未保存的车辆小队")), NewId));
                Test->TestFalse(TEXT("New squad draft has no canonical identity"), NewId.IsValid());
                Next(); return false;
            }
            case 13:
                Test->TestTrue(TEXT("New drafts can choose a supplied vehicle"), Room->SelectAvailableVehicle(Fixture->Vehicles[0].VehicleId));
                Test->TestEqual(TEXT("Selecting a vehicle does not create an unsaved new squad"), Fixture->Squads->GetSquadIds().Num(), SquadCountBeforeDraft);
                Room->ReturnToSquadList();
                Next(); return false;
            case 14:
                Test->TestEqual(TEXT("Cancelling a new vehicle draft creates no squad"), Fixture->Squads->GetSquadIds().Num(), SquadCountBeforeDraft);
                Test->TestTrue(TEXT("Reopen before testing close during selection"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 15:
                Room->SetVehiclePickerVisible(true);
                if (!Click(TEXT("CloseVehiclePickerButton"))) return true;
                Test->TestFalse(TEXT("Dedicated close button dismisses the popup"), Room->IsVehiclePickerVisible());
                Room->SetVehiclePickerVisible(true);
                Fixture->Shell->UnloadCurrentSceneUI();
                Test->TestFalse(TEXT("Unloading the room immediately hides its vehicle popup"), Room->IsVehiclePickerVisible());
                Test->TestFalse(TEXT("Unloading room rejects late programmatic vehicle choices"), Room->SelectAvailableVehicle(Fixture->Vehicles[0].VehicleId));
                Next(); return false;
            case 16:
                if (Fixture->Shell->GetCurrentSceneUI()) return false;
                Test->TestNull(TEXT("Room exit releases its host after vehicle editing"), Room->BaseUI.Get());
                Test->TestFalse(TEXT("Room exit removes squad subscriptions"), Fixture->Squads->OnSquadChanged.GetAllObjects().Contains(Room));
                Test->TestFalse(TEXT("Room exit removes vehicle subscriptions"), Fixture->Units->OnVehicleDataChanged.GetAllObjects().Contains(Room));
                CheckCanonicalVehicle(SavedVehicle);
                return true;
            }
            return false;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadVehiclePickerTest, "SilverChoir.BaseUI.SquadMeetingRoom.VehiclePicker",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadVehiclePickerTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadVehiclePickerTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadVehiclePickerTests::FExercisePicker(Fixture, this));
    return true;
}
#endif
