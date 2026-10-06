#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadMemberCardWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Stack.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"

namespace SquadMeetingRoomTests
{
    const TCHAR* RoomClassPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom.WBP_SquadMeetingRoom_C");
    const TCHAR* ShellClassPath = TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C");
    const FName LocalTile(TEXT("SC_SQUAD_UI_TEST_BASE"));
    const FName RemoteTile(TEXT("SC_SQUAD_UI_TEST_REMOTE"));

    bool IsDisplayed(const UWidget* Widget)
    {
        return Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed
            && Widget->GetVisibility() != ESlateVisibility::Hidden;
    }

    bool SameSquadState(const FSquadData& A, const FSquadData& B)
    {
        return A.SquadId == B.SquadId && A.SquadName.EqualTo(B.SquadName)
            && A.SquadIcon == B.SquadIcon && A.IconSource == B.IconSource
            && A.TileId == B.TileId && A.CaptainUnitId == B.CaptainUnitId
            && A.MemberUnitIds == B.MemberUnitIds
            && FVehicleData::StaticStruct()->CompareScriptStruct(&A.AssignedVehicle, &B.AssignedVehicle, 0);
    }

    /** Intercept the real Blueprint event only for a synchronous save call; never edit the asset. */
    class FScopedSquadSavedHook
    {
        static FScopedSquadSavedHook* Active;
        USquadMeetingRoomWidget* Room;
        UFunction* Function;
        EFunctionFlags OriginalFlags;
        FNativeFuncPtr OriginalNative;
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
        UFunction* OriginalEventGraph;
#endif
        TFunction<void(FGuid)> Callback;

        static void Invoke(UObject* Context, FFrame& Stack, void* const Result)
        {
            const auto* IdProperty = FindFProperty<FStructProperty>(Stack.Node, TEXT("SquadId"));
            if (Active && Context == Active->Room && IdProperty)
                Active->Callback(*IdProperty->ContainerPtrToValuePtr<FGuid>(Stack.Locals));
        }
    public:
        FScopedSquadSavedHook(USquadMeetingRoomWidget* InRoom, TFunction<void(FGuid)> InCallback)
            : Room(InRoom), Function(InRoom->FindFunctionChecked(GET_FUNCTION_NAME_CHECKED(USquadMeetingRoomWidget, OnSquadSaved)))
            , OriginalFlags(Function->FunctionFlags), OriginalNative(Function->GetNativeFunc())
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
            , OriginalEventGraph(Function->EventGraphFunction)
#endif
            , Callback(MoveTemp(InCallback))
        {
            check(!Active);
            Active = this;
            Function->FunctionFlags |= FUNC_Native;
            Function->SetNativeFunc(&Invoke);
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
            Function->EventGraphFunction = nullptr;
#endif
        }
        ~FScopedSquadSavedHook()
        {
            Function->FunctionFlags = OriginalFlags;
            Function->SetNativeFunc(OriginalNative);
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
            Function->EventGraphFunction = OriginalEventGraph;
#endif
            Active = nullptr;
        }
    };
    FScopedSquadSavedHook* FScopedSquadSavedHook::Active = nullptr;

    struct FFixture
    {
        TWeakObjectPtr<UWorld> World;
        TStrongObjectPtr<UBaseMapWidget> Shell;
        TStrongObjectPtr<USquadMeetingRoomWidget> Room;
        TWeakObjectPtr<UPlayerUnitManagerBase> Units;
        TWeakObjectPtr<UPlayerSquadManagerBase> Squads;
        TArray<FGuid> UnitIds;
        TArray<FGuid> OverflowUnitIds;
        TArray<FGuid> CreatedSquads;
        TArray<FGuid> CreatedVehicles;
        FGuid Alpha;
        FGuid Bravo;
        FGuid Distant;
        FText Error;
        float Duration = 0.f;

        bool Initialize(FAutomationTestBase& Test, float InDuration, float AnimationSpeed = 1.f)
        {
            Duration = InDuration;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
            if (!Test.TestNotNull(TEXT("Dedicated game process has a world"), World.Get())) return false;
            Units = UPlayerUnitLibrary::GetPlayerUnitManager(World.Get());
            Squads = UPlayerSquadLibrary::GetPlayerSquadManager(World.Get());
            if (!Test.TestNotNull(TEXT("Canonical unit manager exists"), Units.Get())
                || !Test.TestNotNull(TEXT("Squad manager exists"), Squads.Get())) return false;

            // These reserved IDs only exist in the automation game's memory. Running both tests
            // reuses canonical records rather than accumulating duplicated fake personnel.
            const TCHAR* Codes[] = {TEXT("寒锋"), TEXT("脉冲"), TEXT("磐石"), TEXT("银针"), TEXT("夜莺"), TEXT("白露")};
            const TCHAR* Names[] = {TEXT("林霜"), TEXT("许璃"), TEXT("陆衡"), TEXT("顾宁"), TEXT("沈月"), TEXT("白芷")};
            TArray<FUnitData> InitialData;
            for (int32 Index = 0; Index < 6; ++Index)
            {
                FUnitData Unit;
                Unit.UnitId = FGuid(0x53515549, 0x9AC33711, 0xAD93CA55, Index + 1);
                Unit.Profile.CodeName = FText::FromString(Codes[Index]);
                Unit.Profile.FirstName = FText::FromString(Names[Index]);
                Unit.Profile.Age = 22 + Index;
                Unit.Profile.PortraitTexture = LoadObject<UTexture2D>(nullptr, *FString::Printf(
                    TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/Portraits/T_TestPortrait_%02d.T_TestPortrait_%02d"),
                    Index % 4 + 1, Index % 4 + 1));
                Unit.RuntimeData.TileId = Index == 5 ? RemoteTile : LocalTile;
                UnitIds.Add(Unit.UnitId);
                InitialData.Add(MoveTemp(Unit));
            }
            if (!Test.TestTrue(TEXT("Load temporary canonical test records"), Units->LoadUnitData(InitialData, Error))) return false;
            auto AddSquad = [&](const TCHAR* Name, FName Tile, FGuid& Id)
            {
                const bool bCreated = Squads->CreateSquad(FText::FromString(Name), nullptr, Tile, Id, Error);
                if (bCreated) CreatedSquads.Add(Id);
                return bCreated;
            };
            if (!Test.TestTrue(TEXT("Create three temporary squads"),
                AddSquad(TEXT("银翼小队"), LocalTile, Alpha)
                && AddSquad(TEXT("守望小队"), LocalTile, Bravo)
                && AddSquad(TEXT("远征小队"), RemoteTile, Distant))) return false;
            Test.TestTrue(TEXT("Assign first captain"), Squads->AddUnitToSquad(UnitIds[0], Alpha, Error));
            Test.TestTrue(TEXT("Assign source squad member"), Squads->AddUnitToSquad(UnitIds[2], Bravo, Error));
            Test.TestTrue(TEXT("Assign source squad replacement captain"), Squads->AddUnitToSquad(UnitIds[3], Bravo, Error));
            Test.TestTrue(TEXT("Assign distant member"), Squads->AddUnitToSquad(UnitIds[5], Distant, Error));
            const auto Icons = UPlayerSquadLibrary::GetPresetSquadIcons();
            if (!Icons.IsEmpty())
            {
                Squads->UpdateSquadInfo(Alpha, FText::FromString(TEXT("银翼小队")), Icons[0], Error);
                Squads->UpdateSquadInfo(Bravo, FText::FromString(TEXT("守望小队")), Icons[Icons.Num() > 1 ? 1 : 0], Error);
            }

            UClass* ShellClass = LoadClass<UBaseMapWidget>(nullptr, ShellClassPath);
            UClass* RoomClass = LoadClass<USquadMeetingRoomWidget>(nullptr, RoomClassPath);
            if (!Test.TestNotNull(TEXT("Real base shell Blueprint exists"), ShellClass)
                || !Test.TestNotNull(TEXT("Real meeting room Blueprint exists"), RoomClass)) return false;
            Shell.Reset(CreateWidget<UBaseMapWidget>(World->GetFirstPlayerController(), ShellClass));
            if (!Test.TestNotNull(TEXT("Base shell instance is created"), Shell.Get())) return false;
            Shell->SetCurrencyAmount(128450);
            Shell->AddToViewport(10000);
            const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom"));
            const auto* Configured = Shell->SceneUIClasses.Find(Tag);
            if (!Test.TestTrue(TEXT("Shell maps the scene tag to the real meeting room Blueprint"), Configured && Configured->Get() == RoomClass)) return false;
            {
                // A temporary CDO override affects only this instance. No package is dirtied/saved.
                TGuardValue<float> Override(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->TransitionDuration, Duration);
                TGuardValue<float> SpeedOverride(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->WidgetAnimationPlaybackSpeed, AnimationSpeed);
                TGuardValue<bool> AnimationOverride(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->bUseWidgetAnimations, true);
                Room.Reset(Cast<USquadMeetingRoomWidget>(Shell->CreateSceneUIByTag(Tag)));
            }
            if (!Test.TestNotNull(TEXT("CreateSceneUIByTag mounts meeting room"), Room.Get())) return false;
            Test.TestTrue(TEXT("Initial loading remains active until a tick, allowing completion-event binding"), Room->bTransitioning);
            Test.TestTrue(TEXT("Explicit tile can be supplied while entering"), Room->LoadSquadList(LocalTile));
            Test.TestEqual(TEXT("Current tile is recorded"), Room->CurrentTileId, LocalTile);
            Test.TestFalse(TEXT("Missing tile is rejected"), Room->LoadSquadList(NAME_None));
            Test.TestTrue(TEXT("Restore explicit local tile"), Room->LoadSquadList(LocalTile));
            return true;
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
            if (Units.IsValid())
                for (const FGuid Id : OverflowUnitIds)
                    if (const auto Unit = Units->GetUnitDataShared(Id))
                        Unit->Modify([](FUnitData& Data) { Data.RuntimeData.TileId = NAME_None; });
        }

        bool RegisterVehicle(const FVehicleData& Vehicle)
        {
            if (!Units->LoadVehicleData({Vehicle}, Error)) return false;
            CreatedVehicles.AddUnique(Vehicle.VehicleId);
            return true;
        }

        bool AddOverflowUnits()
        {
            TArray<FUnitData> Data;
            for (int32 Index = 0; Index < 16; ++Index)
            {
                FUnitData Unit;
                Unit.UnitId = FGuid(0x53515549, 0x9AC33711, 0xAD93CA55, 100 + Index);
                Unit.Profile.CodeName = FText::FromString(FString::Printf(TEXT("滚动验证 %02d"), Index + 1));
                Unit.RuntimeData.TileId = LocalTile;
                OverflowUnitIds.Add(Unit.UnitId);
                Data.Add(MoveTemp(Unit));
            }
            return Units.IsValid() && Units->LoadUnitData(Data, Error);
        }

        UPersonnelListEntryWidget* FindPersonnel(FGuid Id) const
        {
            auto* List = Room.IsValid() ? Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("PersonnelList"))) : nullptr;
            if (List)
                for (UWidget* Child : List->GetAllChildren())
                    if (auto* Row = Cast<UPersonnelListEntryWidget>(Child))
                        if (Row->GetUnitData() && Row->GetUnitData()->UnitId == Id) return Row;
            return nullptr;
        }

        USquadMemberCardWidget* FindMember(FGuid Id) const
        {
            auto* List = Room.IsValid() ? Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList"))) : nullptr;
            if (List)
                for (UWidget* Child : List->GetAllChildren())
                    if (auto* Card = Cast<USquadMemberCardWidget>(Child))
                        if (Card->GetUnitDataShared() && Card->GetUnitDataShared()->UnitId == Id) return Card;
            return nullptr;
        }

        USelectionButtonWidget* FindSquad(FGuid Id) const
        {
            auto* List = Room.IsValid() ? Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("SquadList"))) : nullptr;
            if (List)
                for (UWidget* Child : List->GetAllChildren())
                    if (auto* Row = Cast<USelectionButtonWidget>(Child))
                    {
                        FGuid Parsed;
                        if (FGuid::Parse(Row->ChoiceID.ToString(), Parsed) && Parsed == Id) return Row;
                    }
            return nullptr;
        }
    };

    class FExerciseRoom final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();
        bool bCapture = false;
        FGuid NewSquad;
        FString CaptureTag;
        FSquadData OriginalAlpha;
        FSquadData SavedAlpha;
        FSquadData SavedNewSquad;
        int32 OriginalSquadCount = 0;
        bool bReopenedSavedAlpha = false;
        bool bCapturedSavedManagement = false;
        bool bReopenedSavedNewSquad = false;
        bool bLoadedOverflowUnits = false;
        TArray<TWeakObjectPtr<UWidget>> StableRows[3];
        float StableOffsets[3] = {};

        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
        void Capture(const TCHAR* Name)
        {
            if (bCapture)
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SquadUI")
                    / (CaptureTag + TEXT("_") + Name + TEXT(".png")), true, false);
        }
        bool Click(UUserWidget* Owner, const TCHAR* Name)
        {
            auto* Button = Owner ? Cast<UBasicButtonWidget>(Owner->GetWidgetFromName(Name)) : nullptr;
            if (!Test->TestNotNull(Name, Button)) return false;
            if (!Test->TestTrue(FString(Name) + TEXT(" is enabled"), Button->GetIsEnabled())) return false;
            Button->OnClicked.Broadcast();
            return true;
        }
        void CheckManagement(bool bExpected)
        {
            auto* Room = Fixture->Room.Get();
            Test->TestTrue(TEXT("Right management panel visibility matches layer"),
                IsDisplayed(Room->GetWidgetFromName(TEXT("ManagementPanel"))) == bExpected);
            Test->TestTrue(TEXT("Bottom member strip visibility matches layer"),
                IsDisplayed(Room->GetWidgetFromName(TEXT("MemberStripPanel"))) == bExpected);
            auto* Switcher = Cast<UWidgetSwitcher>(Room->GetWidgetFromName(TEXT("LeftPages")));
            if (Test->TestNotNull(TEXT("Left content switcher is bound"), Switcher))
                Test->TestEqual(TEXT("Left content page matches layer"), Switcher->GetActiveWidgetIndex(), bExpected ? 1 : 0);
        }

        void RememberListState(bool bExerciseScrolling = false)
        {
            const TCHAR* Names[] = {TEXT("SquadList"), TEXT("PersonnelList"), TEXT("MemberList")};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                StableRows[Index].Reset();
                auto* List = Cast<UScrollBox>(Fixture->Room->GetWidgetFromName(Names[Index]));
                if (!Test->TestNotNull(Names[Index], List)) continue;
                if (bExerciseScrolling && Index > 0)
                {
                    Test->TestTrue(FString(Names[Index]) + TEXT(" has overflow for a meaningful scroll regression"),
                        List->GetScrollOffsetOfEnd() > 48.f);
                    List->SetScrollOffset(FMath::Min(48.f, List->GetScrollOffsetOfEnd()));
                }
                StableOffsets[Index] = List->GetScrollOffset();
                for (UWidget* Child : List->GetAllChildren()) StableRows[Index].Add(Child);
            }
        }

        void CheckListState()
        {
            const TCHAR* Names[] = {TEXT("SquadList"), TEXT("PersonnelList"), TEXT("MemberList")};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                auto* List = Cast<UScrollBox>(Fixture->Room->GetWidgetFromName(Names[Index]));
                if (!List) continue;
                Test->TestEqual(FString(Names[Index]) + TEXT(" keeps its row count after metadata edits"),
                    List->GetChildrenCount(), StableRows[Index].Num());
                for (int32 RowIndex = 0; RowIndex < StableRows[Index].Num(); ++RowIndex)
                    Test->TestTrue(FString(Names[Index]) + TEXT(" retains the mounted row object and order"),
                        StableRows[Index][RowIndex].IsValid() && List->GetChildAt(RowIndex) == StableRows[Index][RowIndex].Get());
                Test->TestTrue(FString(Names[Index]) + TEXT(" retains scroll offset after name, icon, and captain edits"),
                    FMath::IsNearlyEqual(List->GetScrollOffset(), StableOffsets[Index], 0.5f));
            }
        }

    public:
        FExerciseRoom(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest, bool bInCapture)
            : Fixture(InFixture), Test(InTest), bCapture(bInCapture)
        {
            CaptureTag = TEXT("SquadRoom");
            FParse::Value(FCommandLine::Get(), TEXT("SquadCaptureTag="), CaptureTag);
            CaptureTag = FPaths::MakeValidFileName(CaptureTag);
        }

        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            auto* Shell = Fixture->Shell.Get();
            if (!Room || !Shell || !Fixture->World.IsValid())
            {
                Test->AddError(TEXT("Meeting room fixture disappeared before verification finished."));
                return true;
            }
            const double Elapsed = FPlatformTime::Seconds() - StepStarted;
            if (Elapsed > 10.)
            {
                Test->AddError(FString::Printf(TEXT("Meeting room lifecycle/transition timed out at step %d."), Step));
                return true;
            }
            FSquadData Data;
            switch (Step)
            {
            case 0:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                OriginalSquadCount = Fixture->Squads->GetSquadIds().Num();
                Fixture->Squads->GetSquad(Fixture->Alpha, OriginalAlpha);
                Test->TestTrue(TEXT("Entrance finishes at squad selection"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                CheckManagement(false);
                if (auto* CreateButton = Cast<UBasicButtonWidget>(Room->GetWidgetFromName(TEXT("CreateSquadButton"))))
                    Test->TestTrue(TEXT("New squad button is wired"), CreateButton->OnClicked.IsBound());
                else Test->AddError(TEXT("Squad selection has no new squad button."));
                Test->TestNotNull(TEXT("Local squad appears in standby list"), Fixture->FindSquad(Fixture->Alpha));
                Test->TestNull(TEXT("Distant squad is excluded by standby"), Fixture->FindSquad(Fixture->Distant));
                Room->SetShowAll(true);
                Next();
                return false;
            case 1:
            {
                if (Elapsed < 0.1) return false;
                if (auto* Remote = Fixture->FindSquad(Fixture->Distant))
                    Test->TestFalse(TEXT("Distant squad is visibly disabled in All"), Remote->GetIsEnabled());
                else Test->AddError(TEXT("All squad list omitted the distant squad."));
                Test->TestFalse(TEXT("Distant squad cannot enter local management"), Room->SelectSquad(Fixture->Distant));
                FGuid Rejected;
                Test->TestFalse(TEXT("Creating a squad on another tile is rejected"), Room->CreateSquadAtTile(RemoteTile, FText::FromString(TEXT("Wrong Tile")), Rejected));
                Room->SetShowAll(false);
                Capture(TEXT("01_Selection"));
                Next();
                return false;
            }
            case 2:
                if (Elapsed < 0.3) return false;
                Test->TestTrue(TEXT("Select local squad"), Room->SelectSquad(Fixture->Alpha));
                if (Fixture->Duration > 0)
                {
                    Test->TestTrue(TEXT("Layer animation starts"), Room->bTransitioning);
                    auto* Switcher = Cast<UWidgetSwitcher>(Room->GetWidgetFromName(TEXT("LeftPages")));
                    if (Switcher) Test->TestEqual(TEXT("Old squad page remains until its slide-out finishes"), Switcher->GetActiveWidgetIndex(), 0);
                    Test->TestFalse(TEXT("Rapid second squad selection is rejected"), Room->SelectSquad(Fixture->Bravo));
                    Test->TestFalse(TEXT("Member edits are rejected during transition"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[1]));
                    FGuid Rejected;
                    Test->TestFalse(TEXT("New squad requests cannot interrupt transition"), Room->CreateSquadAtTile(LocalTile, FText::FromString(TEXT("Rapid")), Rejected));
                }
                Next();
                return false;
            case 3:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                Test->TestTrue(TEXT("Completed layer is management"), Room->CurrentLayer == ESquadRoomLayer::SquadManagement);
                Test->TestEqual(TEXT("Requested squad remains selected"), Room->SelectedSquadId, Fixture->Alpha);
                CheckManagement(true);
                Test->TestTrue(TEXT("Selecting an existing squad begins an unchanged draft"), Room->bEditingDraft && !Room->bNewSquadDraft && !Room->HasUnsavedSquadChanges());
                for (const TCHAR* Name : {TEXT("SaveSquadButton"), TEXT("ChooseSquadIconButton"), TEXT("CloseIconPickerButton"), TEXT("IconPickerPanel"), TEXT("DraftStatusText")})
                    Test->TestNotNull(FString(Name) + TEXT(" is bound in the real Blueprint"), Room->GetWidgetFromName(Name));
                Test->TestFalse(TEXT("Icon picker starts closed"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Room->SetShowAll(true);
                Next();
                return false;
            case 4:
                if (Elapsed < 0.15) return false;
                if (auto* Remote = Fixture->FindPersonnel(Fixture->UnitIds[5]))
                    Test->TestFalse(TEXT("Distant personnel are disabled in All"), Remote->GetIsEnabled());
                else Test->AddError(TEXT("All personnel list omitted distant personnel."));
                Test->TestFalse(TEXT("Distant unit cannot be added through API"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[5]));
                if (auto* Local = Fixture->FindPersonnel(Fixture->UnitIds[1]))
                {
                    Test->TestTrue(TEXT("Personnel row holds the original canonical pointer"), Local->GetUnitData().Get() == Fixture->Units->GetUnitDataShared(Fixture->UnitIds[1]).Get());
                    Local->OnClicked.Broadcast();
                }
                else Test->AddError(TEXT("Local personnel row is missing."));
                Test->TestTrue(TEXT("Selecting personnel adds them to the draft"), Room->GetSelectedSquad(Data) && Data.MemberUnitIds.Contains(Fixture->UnitIds[1]));
                Test->TestFalse(TEXT("Draft selection does not assign a free canonical unit"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[1], Data));
                Test->TestTrue(TEXT("UI can draft colocated personnel from another squad"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[2]));
                Fixture->Squads->GetSquad(Fixture->Bravo, Data);
                Test->TestEqual(TEXT("Draft transfer leaves the source captain unchanged"), Data.CaptainUnitId, Fixture->UnitIds[2]);
                Test->TestTrue(TEXT("Draft transfer leaves the source roster unchanged"), Data.MemberUnitIds.Contains(Fixture->UnitIds[2]));
                Room->SetShowAll(false);
                Next();
                return false;
            case 5:
                if (Elapsed < 0.15) return false;
                for (int32 Index : {0, 1, 2})
                {
                    auto* Card = Fixture->FindMember(Fixture->UnitIds[Index]);
                    if (Test->TestNotNull(TEXT("Each member gets a bottom card"), Card))
                        Test->TestTrue(TEXT("Member card holds the original canonical pointer"), Card->GetUnitDataShared().Get() == Fixture->Units->GetUnitDataShared(Fixture->UnitIds[Index]).Get());
                }
                RememberListState();
                Click(Fixture->FindMember(Fixture->UnitIds[1]), TEXT("PromoteButton"));
                Room->GetSelectedSquad(Data);
                Test->TestEqual(TEXT("Member card promotion edits the draft captain"), Data.CaptainUnitId, Fixture->UnitIds[1]);
                Test->TestTrue(TEXT("An incomplete name remains editable in the draft"), Room->RenameSelectedSquad(FText::FromString(TEXT("   "))));
                Test->TestFalse(TEXT("Saving rejects whitespace-only names"), Room->SaveSelectedSquad());
                Test->TestTrue(TEXT("Failed save stays in the editable management layer"),
                    Room->CurrentLayer == ESquadRoomLayer::SquadManagement && Room->bEditingDraft && !Room->bTransitioning);
                Test->TestEqual(TEXT("Failed save keeps the selected draft identity"), Room->SelectedSquadId, Fixture->Alpha);
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Failed save preserves all canonical squad fields"), SameSquadState(Data, OriginalAlpha));
                if (auto* Name = Cast<UEditableTextBox>(Room->GetWidgetFromName(TEXT("SquadNameInput"))))
                {
                    Name->SetText(FText::FromString(TEXT("银翼先锋小队")));
                    Name->OnTextCommitted.Broadcast(Name->GetText(), ETextCommit::OnEnter);
                }
                else Test->AddError(TEXT("Editable squad name is not bound."));
                Room->GetSelectedSquad(Data);
                Test->TestEqual(TEXT("Name input commits only to the draft"), Data.SquadName.ToString(), FString(TEXT("银翼先锋小队")));
                Test->TestFalse(TEXT("Invalid preset indices are rejected"), Room->SelectPresetIcon(-1));
                Click(Room, TEXT("ChooseSquadIconButton"));
                Test->TestTrue(TEXT("Choose icon opens the modal picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Test->TestTrue(TEXT("Generated preset icon is selectable"), Room->SelectPresetIcon(0));
                Test->TestFalse(TEXT("Choosing a preset closes the picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Click(Room, TEXT("ChooseSquadIconButton"));
                Click(Room, TEXT("UseCaptainPortraitButton"));
                Test->TestFalse(TEXT("Choosing the captain portrait closes the picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Next();
                return false;
            case 6:
                if (Elapsed < 0.2) return false;
                CheckListState();
                Room->GetSelectedSquad(Data);
                Test->TestTrue(TEXT("Captain portrait button changes icon mode"), Data.IconSource == ESquadIconSource::CaptainPortrait);
                if (auto* Icon = Cast<UImage>(Room->GetWidgetFromName(TEXT("SquadIconImage"))))
                    Test->TestTrue(TEXT("Management icon displays the current captain portrait"), Icon->GetBrush().GetResourceObject() == Fixture->Units->GetUnitDataShared(Fixture->UnitIds[1])->Profile.PortraitTexture.Get());
                else Test->AddError(TEXT("Squad icon display is not bound."));
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Name, icon, roster and captain stay canonical until Save Squad"), SameSquadState(Data, OriginalAlpha));
                Test->TestTrue(TEXT("Edited draft is marked unsaved"), Room->HasUnsavedSquadChanges());
                Room->GetSelectedSquad(SavedAlpha);
                Click(Room, TEXT("SaveSquadButton"));
                Test->TestFalse(TEXT("Save Squad clears the dirty state"), Room->HasUnsavedSquadChanges());
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Save Squad commits the complete draft"), SameSquadState(Data, SavedAlpha));
                Test->TestEqual(TEXT("Save Squad commits the name"), SavedAlpha.SquadName.ToString(), FString(TEXT("银翼先锋小队")));
                Test->TestEqual(TEXT("Save Squad commits the promoted captain"), SavedAlpha.CaptainUnitId, Fixture->UnitIds[1]);
                Test->TestTrue(TEXT("Free personnel are assigned only after save"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[1], Data) && Data.SquadId == Fixture->Alpha);
                Test->TestTrue(TEXT("Cross-squad transfer happens only after save"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[2], Data) && Data.SquadId == Fixture->Alpha);
                Fixture->Squads->GetSquad(Fixture->Bravo, Data);
                Test->TestEqual(TEXT("Source captain is repaired on save"), Data.CaptainUnitId, Fixture->UnitIds[3]);
                if (Fixture->Duration > 0)
                    Test->TestTrue(TEXT("Successful save starts the existing animated return"), Room->bTransitioning);
                Next();
                return false;
            case 7:
                if (!bReopenedSavedAlpha)
                {
                    if (Room->bTransitioning) return false;
                    Test->TestTrue(TEXT("Successful save returns to squad selection"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                    Test->TestFalse(TEXT("Successful return releases the saved editing draft"), Room->bEditingDraft || Room->SelectedSquadId.IsValid());
                    Test->TestNotNull(TEXT("Saved squad remains listed after automatic return"), Fixture->FindSquad(Fixture->Alpha));
                    Test->TestTrue(TEXT("Saved squad can be reopened for further edits"), Room->SelectSquad(Fixture->Alpha));
                    bReopenedSavedAlpha = true;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                if (!bCapturedSavedManagement)
                {
                    Fixture->Units->GetUnitDataShared(Fixture->UnitIds[1])->Modify([&](FUnitData& Unit)
                    {
                        Unit.Profile.CodeName = FText::FromString(TEXT("流光"));
                        Unit.Profile.PortraitTexture = Fixture->Units->GetUnitDataShared(Fixture->UnitIds[0])->Profile.PortraitTexture;
                    });
                    Capture(TEXT("02_Management"));
                    bCapturedSavedManagement = true;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                if (Elapsed < 0.3) return false;
                if (auto* Icon = Cast<UImage>(Room->GetWidgetFromName(TEXT("SquadIconImage"))))
                    Test->TestTrue(TEXT("Shared captain edits refresh the displayed squad icon"), Icon->GetBrush().GetResourceObject() == Fixture->Units->GetUnitDataShared(Fixture->UnitIds[1])->Profile.PortraitTexture.Get());
                if (auto* Card = Fixture->FindMember(Fixture->UnitIds[1]))
                    if (auto* Name = Cast<UTextBlock>(Card->GetWidgetFromName(TEXT("MemberName"))))
                        Test->TestEqual(TEXT("Shared unit notification refreshes bottom member name"), Name->GetText().ToString(), FString(TEXT("流光")));
                Click(Room, TEXT("ChooseSquadIconButton"));
                Test->TestTrue(TEXT("Picker can reopen after saving"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Capture(TEXT("02_IconPicker"));
                Next();
                return false;
            case 8:
                if (Elapsed < 0.3) return false;
                Click(Room, TEXT("CloseIconPickerButton"));
                Test->TestFalse(TEXT("Close picker button dismisses the popup"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Click(Fixture->FindMember(Fixture->UnitIds[2]), TEXT("RemoveButton"));
                Test->TestTrue(TEXT("Remove button changes only the draft roster"), Room->GetSelectedSquad(Data) && !Data.MemberUnitIds.Contains(Fixture->UnitIds[2]));
                Test->TestTrue(TEXT("Unsaved removal preserves canonical membership"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[2], Data) && Data.SquadId == Fixture->Alpha);
                Test->TestTrue(TEXT("Further unsaved captain change is allowed"), Room->PromoteMemberToCaptain(Fixture->UnitIds[0]));
                Test->TestTrue(TEXT("Further unsaved icon change is allowed"), Room->SelectPresetIcon(0));
                Test->TestTrue(TEXT("Further unsaved rename is allowed"), Room->RenameSelectedSquad(FText::FromString(TEXT("将放弃的名称"))));
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Further draft edits preserve the last saved squad"), SameSquadState(Data, SavedAlpha));
                Click(Room, TEXT("BackToSquadsButton"));
                Next();
                return false;
            case 9:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                Test->TestTrue(TEXT("Back navigation reaches squad selection"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                CheckManagement(false);
                Test->TestFalse(TEXT("Returning discards the existing draft"), Room->GetSelectedSquad(Data));
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Returning discards unsaved name, icon, captain and roster changes"), SameSquadState(Data, SavedAlpha));
                Test->TestTrue(TEXT("Create new squad accepts explicit current tile"), Room->CreateSquadAtTile(LocalTile, FText::FromString(TEXT("曙光小队")), NewSquad));
                Test->TestFalse(TEXT("New draft has no allocated squad identity"), NewSquad.IsValid());
                Test->TestFalse(TEXT("New draft has no selected canonical identity"), Room->SelectedSquadId.IsValid());
                Test->TestEqual(TEXT("Creating a draft does not create a manager record"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount);
                Test->TestTrue(TEXT("New draft remains available for display"), Room->GetSelectedSquad(Data));
                Test->TestEqual(TEXT("New squad records current tile"), Data.TileId, LocalTile);
                Next();
                return false;
            case 10:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                Test->TestTrue(TEXT("Creation opens management for an unsaved new draft"), Room->bNewSquadDraft && Room->bEditingDraft);
                CheckManagement(true);
                Test->TestTrue(TEXT("New squad starts empty"), Room->GetSelectedSquad(Data) && Data.MemberUnitIds.IsEmpty());
                Capture(TEXT("03_NewSquad"));
                Next();
                return false;
            case 11:
                if (Elapsed < 0.3) return false;
                Test->TestTrue(TEXT("Personnel can be added to a new draft"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[4]));
                Test->TestFalse(TEXT("Unsaved new draft does not reserve personnel"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[4], Data));
                Click(Room, TEXT("BackToSquadsButton"));
                Next();
                return false;
            case 12:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                Test->TestEqual(TEXT("Cancelling a new draft creates no ghost squad"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount);
                Test->TestFalse(TEXT("Cancelling a new draft leaves personnel unassigned"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[4], Data));
                Test->TestFalse(TEXT("Cancelled draft is released"), Room->bEditingDraft);
                Test->TestTrue(TEXT("Another new draft can be opened"), Room->CreateSquadAtTile(LocalTile, FText::FromString(TEXT("已保存的新小队")), NewSquad));
                Next();
                return false;
            case 13:
                if (Room->bTransitioning || Elapsed < 0.15) return false;
                Test->TestTrue(TEXT("New draft can select its first member"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[4]));
                Test->TestTrue(TEXT("Save creates the new canonical squad"), Room->SaveSelectedSquad());
                Test->TestTrue(TEXT("New squad can be resolved after the editing selection is cleared"),
                    Fixture->Squads->GetUnitSquad(Fixture->UnitIds[4], Data));
                NewSquad = Data.SquadId;
                if (NewSquad.IsValid()) Fixture->CreatedSquads.AddUnique(NewSquad);
                Test->TestTrue(TEXT("Saving allocates a valid new identity"), NewSquad.IsValid());
                Test->TestEqual(TEXT("Only successful save adds one manager record"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount + 1);
                Test->TestTrue(TEXT("Saved new squad is queryable"), Fixture->Squads->GetSquad(NewSquad, SavedNewSquad));
                Test->TestTrue(TEXT("New squad membership commits on save"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[4], Data) && Data.SquadId == NewSquad);
                Test->TestFalse(TEXT("Saved draft no longer reports new or unsaved state"), Room->bNewSquadDraft || Room->HasUnsavedSquadChanges());
                Next();
                return false;
            case 14:
                if (!bReopenedSavedNewSquad)
                {
                    if (Room->bTransitioning) return false;
                    Test->TestTrue(TEXT("Saving a new squad also returns to the squad list"), Room->CurrentLayer == ESquadRoomLayer::SquadSelection);
                    Test->TestFalse(TEXT("New-squad return clears its editing selection"), Room->bEditingDraft || Room->SelectedSquadId.IsValid());
                    Test->TestNotNull(TEXT("The newly saved squad is listed"), Fixture->FindSquad(NewSquad));
                    Test->TestTrue(TEXT("The newly saved squad can be reopened"), Room->SelectSquad(NewSquad));
                    bReopenedSavedNewSquad = true;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                if (Room->bTransitioning) return false;
                if (!bLoadedOverflowUnits)
                {
                    Test->TestTrue(TEXT("Load canonical overflow units for scroll regression"), Fixture->AddOverflowUnits());
                    FVehicleData ScrollVehicle;
                    ScrollVehicle.VehicleId = FGuid::NewGuid();
                    ScrollVehicle.Attributes.PassengerCapacity = Fixture->OverflowUnitIds.Num() + 1;
                    ScrollVehicle.RuntimeData.TileId = LocalTile;
                    Test->TestTrue(TEXT("Scroll test vehicle is registered in the player store"), Fixture->RegisterVehicle(ScrollVehicle));
                    TArray<FGuid> Removed;
                    Test->TestTrue(TEXT("Scroll regression supplies enough vehicle seats"), Room->SetSelectedSquadVehicle(ScrollVehicle, Removed));
                    for (const FGuid Id : Fixture->OverflowUnitIds)
                        Test->TestTrue(TEXT("Overflow units can be selected in the draft"), Room->AddUnitToSelectedSquad(Id));
                    bLoadedOverflowUnits = true;
                    StepStarted = FPlatformTime::Seconds();
                    return false;
                }
                if (Elapsed < 0.2) return false;
                RememberListState(true);
                Test->TestTrue(TEXT("Changing a scrolled draft name succeeds"), Room->RenameSelectedSquad(FText::FromString(TEXT("关闭时放弃的草稿"))));
                Test->TestTrue(TEXT("Changing a scrolled draft icon succeeds"), Room->SelectPresetIcon(0));
                if (Test->TestFalse(TEXT("Scroll regression has member IDs"), Fixture->OverflowUnitIds.IsEmpty()))
                    Test->TestTrue(TEXT("Changing a scrolled draft captain succeeds"), Room->PromoteMemberToCaptain(Fixture->OverflowUnitIds[0]));
                Next();
                return false;
            case 15:
                if (Elapsed < 0.2) return false;
                CheckListState();
                for (const FGuid Id : Fixture->OverflowUnitIds)
                {
                    if (auto* Row = Fixture->FindPersonnel(Id))
                        Test->TestTrue(TEXT("Reused personnel row retains canonical unit storage"), Row->GetUnitData().Get() == Fixture->Units->GetUnitDataShared(Id).Get());
                    if (auto* Card = Fixture->FindMember(Id))
                        Test->TestTrue(TEXT("Reused member card retains canonical unit storage"), Card->GetUnitDataShared().Get() == Fixture->Units->GetUnitDataShared(Id).Get());
                    Test->TestFalse(TEXT("Draft overflow members remain globally unassigned"), Fixture->Squads->GetUnitSquad(Id, Data));
                }
                Fixture->Squads->GetSquad(NewSquad, Data);
                Test->TestTrue(TEXT("Scrolled draft editing leaves the saved squad unchanged"), SameSquadState(Data, SavedNewSquad));
                Click(Room, TEXT("ChooseSquadIconButton"));
                Shell->UnloadCurrentSceneUI();
                Test->TestTrue(TEXT("Unload retains child until completion tick"), Shell->GetCurrentSceneUI() == Room);
                Test->TestFalse(TEXT("Closing room rejects late additions"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[4]));
                Next();
                return false;
            case 16:
                if (Shell->GetCurrentSceneUI()) return false;
                Test->TestNull(TEXT("Native exit animation notifies host and removes child"), Shell->GetCurrentSceneUI());
                Test->TestNull(TEXT("Removed child releases host"), Room->BaseUI.Get());
                Test->TestFalse(TEXT("Unloaded room unbinds squad updates"), Fixture->Squads->OnSquadChanged.GetAllObjects().Contains(Room));
                Test->TestFalse(TEXT("Unloaded room unbinds unit updates"), Fixture->Units->OnUnitDataChanged.GetAllObjects().Contains(Room));
                Test->TestFalse(TEXT("Unloading discards the pending draft"), Room->GetSelectedSquad(Data));
                Test->TestFalse(TEXT("Unloading closes the icon popup"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Fixture->Squads->GetSquad(NewSquad, Data);
                Test->TestTrue(TEXT("Unloading preserves the last saved squad exactly"), SameSquadState(Data, SavedNewSquad));
                Test->TestEqual(TEXT("Unload creates no additional squad"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount + 1);
                Shell->RemoveFromParent();
                return true;
            }
            return false;
        }
    };

    class FExerciseVehicleCapacity final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double Started = FPlatformTime::Seconds();
        FSquadData OriginalAlpha;
        FSquadData OriginalBravo;
        FSquadData SavedExpanded;
        FSquadData SavedDefault;
        TArray<TSharedPtr<FUnitData>> Canonical;
        TArray<TWeakObjectPtr<UWidget>> OriginalSlots;
        TArray<FGuid> Removed;
        FVehicleData SixSeat;
        int32 OriginalSquadCount = 0;
        void Next() { ++Step; Started = FPlatformTime::Seconds(); }
        FVehicleData Vehicle(int32 Capacity) const
        {
            FVehicleData Result;
            Result.VehicleId = FGuid::NewGuid();
            Result.Profile.VehicleName = FText::FromString(FString::Printf(TEXT("测试车辆 · %d 座"), Capacity));
            Result.Attributes.PassengerCapacity = Capacity;
            Result.RuntimeData.TileId = LocalTile;
            Test->TestTrue(TEXT("Capacity test vehicle is registered in the player store"), Fixture->RegisterVehicle(Result));
            return Result;
        }
        void Capture(const TCHAR* Name) const
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SquadUI")
                / (FString(TEXT("Capacity_")) + Name + TEXT(".png")), true, false);
        }
        void CheckSlots(int32 Capacity, int32 Members)
        {
            auto* Room = Fixture->Room.Get();
            auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList")));
            if (!Test->TestNotNull(TEXT("Capacity test has a member strip"), List)) return;
            Test->TestEqual(TEXT("UI capacity query matches selected draft"), Room->GetSelectedSquadCapacity(), Capacity);
            Test->TestEqual(TEXT("One mounted card is displayed for every seat"), List->GetChildrenCount(), Capacity);
            FSquadData Draft;
            if (!Test->TestTrue(TEXT("Selected draft is available for seat verification"), Room->GetSelectedSquad(Draft))) return;
            Test->TestEqual(TEXT("Draft member count matches seat contents"), Draft.MemberUnitIds.Num(), Members);
            for (int32 Index = 0; Index < List->GetChildrenCount(); ++Index)
            {
                auto* Card = Cast<USquadMemberCardWidget>(List->GetChildAt(Index));
                if (!Test->TestNotNull(TEXT("Each seat uses the member card widget"), Card)) continue;
                Test->TestEqual(TEXT("Card records its mounted seat index"), Card->GetSlotIndex(), Index);
                Test->TestTrue(TEXT("Empty seats follow actual members"), Card->IsEmptySlot() == (Index >= Members));
                if (Index < Members && Draft.MemberUnitIds.IsValidIndex(Index))
                {
                    Test->TestEqual(TEXT("Member card follows roster order"), Card->GetUnitId(), Draft.MemberUnitIds[Index]);
                    Test->TestTrue(TEXT("Occupied seat holds the canonical shared allocation"),
                        Card->GetUnitDataShared().Get() == Fixture->Units->GetUnitDataShared(Draft.MemberUnitIds[Index]).Get());
                }
                else
                {
                    Test->TestFalse(TEXT("Empty seat has no unit identity"), Card->GetUnitId().IsValid());
                    Test->TestFalse(TEXT("Empty seat holds no canonical unit"), Card->GetUnitDataShared().IsValid());
                    for (const TCHAR* ButtonName : {TEXT("RemoveButton"), TEXT("PromoteButton")})
                        if (auto* Button = Card->GetWidgetFromName(ButtonName))
                            Test->TestFalse(TEXT("Empty seat actions are disabled"), Button->GetIsEnabled());
                }
            }
        }
    public:
        FExerciseVehicleCapacity(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest), SixSeat(Vehicle(6)) {}
        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            if (!Room || !Fixture->World.IsValid()) { Test->AddError(TEXT("Vehicle capacity UI fixture was lost.")); return true; }
            const double Elapsed = FPlatformTime::Seconds() - Started;
            if (Elapsed > 10.) { Test->AddError(FString::Printf(TEXT("Vehicle capacity UI test timed out at step %d."), Step)); return true; }
            if (Room->bTransitioning || Elapsed < 0.15) return false;
            FSquadData Data;
            switch (Step)
            {
            case 0:
                OriginalSquadCount = Fixture->Squads->GetSquadIds().Num();
                Fixture->Squads->GetSquad(Fixture->Alpha, OriginalAlpha);
                Fixture->Squads->GetSquad(Fixture->Bravo, OriginalBravo);
                for (int32 Index = 0; Index < 5; ++Index) Canonical.Add(Fixture->Units->GetUnitDataShared(Fixture->UnitIds[Index]));
                Test->TestEqual(TEXT("No selected draft exposes zero capacity"), Room->GetSelectedSquadCapacity(), 0);
                Test->TestTrue(TEXT("Open default-capacity squad"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 1:
                CheckSlots(4, 1);
                if (auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList"))))
                    for (UWidget* Child : List->GetAllChildren()) OriginalSlots.Add(Child);
                Capture(TEXT("01_Default4"));
                Next(); return false;
            case 2:
                Test->TestTrue(TEXT("Add second member to the local draft"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[1]));
                Test->TestTrue(TEXT("Assign six-seat vehicle only to the draft"), Room->SetSelectedSquadVehicle(SixSeat, Removed));
                Test->TestTrue(TEXT("Capacity expansion evicts no draft members"), Removed.IsEmpty());
                Next(); return false;
            case 3:
                CheckSlots(6, 2);
                if (auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList"))))
                    for (int32 Index = 0; Index < OriginalSlots.Num(); ++Index)
                        Test->TestTrue(TEXT("Expanding capacity reuses existing seat widgets"), List->GetChildAt(Index) == OriginalSlots[Index].Get());
                Capture(TEXT("02_SixSeats_TwoMembers"));
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Unsaved vehicle selection does not mutate the canonical squad"), SameSquadState(Data, OriginalAlpha));
                Next(); return false;
            case 4:
                for (int32 Index : {2, 3, 4}) Test->TestTrue(TEXT("Six-seat draft accepts further local members"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[Index]));
                Test->TestTrue(TEXT("Last selected member can become draft captain"), Room->PromoteMemberToCaptain(Fixture->UnitIds[4]));
                Test->TestTrue(TEXT("Selecting a smaller vehicle trims the draft"), Room->SetSelectedSquadVehicle(Vehicle(3), Removed));
                Test->TestTrue(TEXT("Draft shrink reports surplus members"), Removed.Num() == 2 && Removed.Contains(Fixture->UnitIds[2]) && Removed.Contains(Fixture->UnitIds[3]));
                Next(); return false;
            case 5:
                CheckSlots(3, 3);
                Room->GetSelectedSquad(Data);
                Test->TestTrue(TEXT("Draft shrink retains earliest members and captain"), Data.MemberUnitIds == TArray<FGuid>({Fixture->UnitIds[0], Fixture->UnitIds[1], Fixture->UnitIds[4]}));
                Test->TestEqual(TEXT("Draft shrink preserves its captain"), Data.CaptainUnitId, Fixture->UnitIds[4]);
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Unsaved shrink preserves original squad"), SameSquadState(Data, OriginalAlpha));
                Fixture->Squads->GetSquad(Fixture->Bravo, Data);
                Test->TestTrue(TEXT("Unsaved draft shrink preserves source memberships"), SameSquadState(Data, OriginalBravo));
                Room->ReturnToSquadList();
                Next(); return false;
            case 6:
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Cancelling vehicle changes preserves canonical state"), SameSquadState(Data, OriginalAlpha));
                Test->TestEqual(TEXT("Cancel clears selected draft capacity"), Room->GetSelectedSquadCapacity(), 0);
                Test->TestTrue(TEXT("Reopen squad for a committed vehicle change"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 7:
                Test->TestTrue(TEXT("Select six-seat vehicle for saving"), Room->SetSelectedSquadVehicle(SixSeat, Removed));
                for (int32 Index : {1, 2, 3, 4}) Test->TestTrue(TEXT("Select five members before saving"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[Index]));
                Test->TestTrue(TEXT("Select captain before saving"), Room->PromoteMemberToCaptain(Fixture->UnitIds[4]));
                Room->GetSelectedSquad(SavedExpanded);
                Test->TestTrue(TEXT("Save vehicle and expanded membership together"), Room->SaveSelectedSquad());
                Next(); return false;
            case 8:
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Saved vehicle and roster match the entire draft"), SameSquadState(Data, SavedExpanded));
                Test->TestTrue(TEXT("Reopen saved vehicle squad"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 9:
                CheckSlots(6, 5);
                Test->TestTrue(TEXT("Clearing draft vehicle restores default capacity"), Room->ClearSelectedSquadVehicle(Removed));
                Test->TestTrue(TEXT("Clearing vehicle reports newest non-captain eviction"), Removed == TArray<FGuid>({Fixture->UnitIds[3]}));
                Test->TestTrue(TEXT("Unsaved eviction retains canonical membership"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[3], Data) && Data.SquadId == Fixture->Alpha);
                Room->GetSelectedSquad(SavedDefault);
                Test->TestTrue(TEXT("Save default-capacity shrink"), Room->SaveSelectedSquad());
                Next(); return false;
            case 10:
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Saving shrink commits the vehicle clear and roster together"), SameSquadState(Data, SavedDefault));
                Test->TestFalse(TEXT("Saving shrink actually clears evicted unit membership"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[3], Data));
                for (int32 Index = 0; Index < Canonical.Num(); ++Index)
                    Test->TestTrue(TEXT("Vehicle edits preserve every canonical unit allocation"), Fixture->Units->GetUnitDataShared(Fixture->UnitIds[Index]).Get() == Canonical[Index].Get());
                Test->TestTrue(TEXT("Evicted unit remains assignable to another squad"), Fixture->Squads->AddUnitToSquad(Fixture->UnitIds[3], Fixture->Bravo, Fixture->Error));
                Test->TestTrue(TEXT("Reopen a full four-seat squad"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 11:
                CheckSlots(4, 4);
                Room->SetShowAll(true);
                Test->TestFalse(TEXT("UI rejects transfer into a full draft"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[3]));
                Test->TestTrue(TEXT("Rejected full-draft transfer retains source membership"), Fixture->Squads->GetUnitSquad(Fixture->UnitIds[3], Data) && Data.SquadId == Fixture->Bravo);
                Next(); return false;
            case 12:
                if (auto* Row = Fixture->FindPersonnel(Fixture->UnitIds[3]))
                {
                    Test->TestFalse(TEXT("Full draft visibly disables nonmember personnel"), Row->GetIsEnabled());
                    const auto Shared = Row->GetUnitData();
                    const float Health = Shared->RuntimeData.CurrentHealth;
                    Shared->Modify([](FUnitData& Unit) { Unit.RuntimeData.CurrentHealth += 1.f; });
                    Test->TestFalse(TEXT("Unit refresh cannot re-enable a nonmember row while full"), Row->GetIsEnabled());
                    Shared->Modify([Health](FUnitData& Unit) { Unit.RuntimeData.CurrentHealth = Health; });
                }
                else Test->AddError(TEXT("Full-draft nonmember row is missing."));
                Test->TestTrue(TEXT("Zero-seat vehicle can be selected"), Room->SetSelectedSquadVehicle(Vehicle(0), Removed));
                Test->TestEqual(TEXT("Zero-seat draft reports all four evictions"), Removed.Num(), 4);
                Next(); return false;
            case 13:
                CheckSlots(0, 0);
                Test->TestFalse(TEXT("Zero-seat draft cannot add members"), Room->AddUnitToSelectedSquad(Fixture->UnitIds[0]));
                Test->TestTrue(TEXT("Clearing zero-seat vehicle restores empty default slots"), Room->ClearSelectedSquadVehicle(Removed));
                Test->TestTrue(TEXT("Clearing an empty draft evicts no additional members"), Removed.IsEmpty());
                Next(); return false;
            case 14:
                CheckSlots(4, 0);
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Unsaved zero-seat edits do not remove canonical members"), SameSquadState(Data, SavedDefault));
                Room->ReturnToSquadList();
                Next(); return false;
            case 15:
            {
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Cancelling zero-seat edits restores the saved squad"), SameSquadState(Data, SavedDefault));
                FGuid NewId;
                Test->TestTrue(TEXT("Open an identity-free new squad draft"), Room->CreateSquadAtTile(LocalTile, FText::FromString(TEXT("Vehicle-only draft")), NewId));
                Test->TestFalse(TEXT("New draft has no canonical identity"), NewId.IsValid());
                Next(); return false;
            }
            case 16:
                Test->TestTrue(TEXT("New draft can select a vehicle before an identity exists"), Room->SetSelectedSquadVehicle(Vehicle(6), Removed));
                Next(); return false;
            case 17:
                CheckSlots(6, 0);
                Test->TestEqual(TEXT("Vehicle-only draft creates no manager record"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount);
                Room->ReturnToSquadList();
                Next(); return false;
            case 18:
                Test->TestEqual(TEXT("Cancelling vehicle-only draft leaves no ghost squad"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount);
                return true;
            }
            return false;
        }
    };

    class FExerciseSaveNavigation final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();
        int32 SavedCallbacks = 0;
        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
    public:
        FExerciseSaveNavigation(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest) {}
        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            auto* Shell = Fixture->Shell.Get();
            if (!Room || !Shell || !Fixture->World.IsValid()) return true;
            if (FPlatformTime::Seconds() - StepStarted > 10.)
            {
                Test->AddError(FString::Printf(TEXT("Save navigation timed out at step %d."), Step));
                return true;
            }
            switch (Step)
            {
            case 0:
                if (Room->bTransitioning) return false;
                if (!Test->TestTrue(TEXT("Open an existing draft for save navigation"), Room->SelectSquad(Fixture->Alpha))) return true;
                Next(); return false;
            case 1:
            {
                if (Room->bTransitioning) return false;
                Room->RenameSelectedSquad(FText::FromString(TEXT("   ")));
                {
                    FScopedSquadSavedHook Hook(Room, [&](FGuid) { ++SavedCallbacks; });
                    Test->TestFalse(TEXT("Invalid draft save fails"), Room->SaveSelectedSquad());
                }
                Test->TestEqual(TEXT("Failed saves never emit OnSquadSaved"), SavedCallbacks, 0);
                FSquadData Draft;
                Test->TestTrue(TEXT("Failed save retains the invalid name for correction"), Room->GetSelectedSquad(Draft)
                    && Draft.SquadName.ToString().TrimStartAndEnd().IsEmpty());
                Test->TestTrue(TEXT("Failed save retains interactive management"), Room->CurrentLayer == ESquadRoomLayer::SquadManagement
                    && Room->bEditingDraft && !Room->bTransitioning && Room->SelectedSquadId == Fixture->Alpha);
                auto* Row = Fixture->FindPersonnel(Fixture->UnitIds[1]);
                if (!Test->TestNotNull(TEXT("A free personnel row can queue a draft action"), Row)) return true;
                const FKeyEvent Press(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
                const TSharedRef<SWidget> SlateRow = Row->TakeWidget();
                Test->TestTrue(TEXT("Personnel row accepts a delayed key press"), SlateRow->OnKeyDown(Row->GetCachedGeometry(), Press).IsEventHandled());
                SlateRow->OnKeyUp(Row->GetCachedGeometry(), Press);
                Test->TestTrue(TEXT("Personnel action is pending before changing drafts"), Row->IsPressPending());
                Test->TestTrue(TEXT("Change drafts before the personnel action ticks"), Room->SelectSquad(Fixture->Bravo));
                Test->TestFalse(TEXT("Changing drafts cancels the prior row action immediately"), Row->IsPressPending());
                Next(); return false;
            }
            case 2:
            {
                if (Room->bTransitioning || FPlatformTime::Seconds() - StepStarted < 0.15) return false;
                FSquadData Draft;
                Test->TestTrue(TEXT("Deferred action from Alpha cannot add personnel to Bravo"), Room->GetSelectedSquad(Draft)
                    && Draft.SquadId == Fixture->Bravo && !Draft.MemberUnitIds.Contains(Fixture->UnitIds[1]));
                Test->TestTrue(TEXT("Reopen Alpha for a synchronous commit callback"), Room->SelectSquad(Fixture->Alpha));
                Room->RenameSelectedSquad(FText::FromString(TEXT("Saved before unit notification")));
                const auto Canonical = Fixture->Units->GetUnitDataShared(Fixture->UnitIds[0]);
                int32 UnitCallbacks = 0;
                const FDelegateHandle Handle = Canonical->OnDataChanged.AddLambda([&](FGuid)
                {
                    ++UnitCallbacks;
                    Test->TestTrue(TEXT("Unit notification can select Bravo during Alpha commit"), Room->SelectSquad(Fixture->Bravo));
                    Room->RenameSelectedSquad(FText::FromString(TEXT("New Bravo draft must survive")));
                });
                {
                    FScopedSquadSavedHook Hook(Room, [&](FGuid SavedId)
                    {
                        ++SavedCallbacks;
                        Test->TestEqual(TEXT("Commit still reports Alpha after unit callback navigation"), SavedId, Fixture->Alpha);
                    });
                    Test->TestTrue(TEXT("Save succeeds when a canonical unit notification opens another draft"), Room->SaveSelectedSquad());
                }
                Canonical->OnDataChanged.Remove(Handle);
                Test->TestEqual(TEXT("Canonical unit notification was exercised once"), UnitCallbacks, 1);
                Test->TestEqual(TEXT("Commit emits one saved callback after unit navigation"), SavedCallbacks, 1);
                Test->TestTrue(TEXT("Old save tail preserves the new Bravo draft and edits"), Room->GetSelectedSquad(Draft)
                    && Draft.SquadId == Fixture->Bravo && Draft.SquadName.ToString() == TEXT("New Bravo draft must survive")
                    && !Room->bTransitioning && Room->CurrentLayer == ESquadRoomLayer::SquadManagement);
                Test->TestTrue(TEXT("Reopen Alpha for the Blueprint save callback"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            }
            case 3:
            {
                if (Room->bTransitioning) return false;
                Room->RenameSelectedSquad(FText::FromString(TEXT("Saved before callback navigation")));
                {
                    FScopedSquadSavedHook Hook(Room, [&](FGuid SavedId)
                    {
                        ++SavedCallbacks;
                        Test->TestEqual(TEXT("Save callback receives the committed identity"), SavedId, Fixture->Alpha);
                        Test->TestTrue(TEXT("Return animation is started before the save callback"), Room->bTransitioning);
                        FSquadData Saved;
                        Test->TestTrue(TEXT("Canonical commit precedes the save callback"), Fixture->Squads->GetSquad(SavedId, Saved)
                            && Saved.SquadName.ToString() == TEXT("Saved before callback navigation"));
                        Test->TestTrue(TEXT("Callback can explicitly replace the current list context"), Room->LoadSquadList(LocalTile));
                        Test->TestTrue(TEXT("Callback can open a different squad"), Room->SelectSquad(Fixture->Bravo));
                    });
                    Test->TestTrue(TEXT("Save succeeds when its notification changes selection"), Room->SaveSelectedSquad());
                }
                Test->TestEqual(TEXT("Second successful save emits one more callback"), SavedCallbacks, 2);
                Test->TestEqual(TEXT("Save tail does not overwrite callback selection"), Room->SelectedSquadId, Fixture->Bravo);
                Next(); return false;
            }
            case 4:
            {
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Callback-selected management survives the old return"),
                    Room->CurrentLayer == ESquadRoomLayer::SquadManagement && Room->SelectedSquadId == Fixture->Bravo);
                Room->RenameSelectedSquad(FText::FromString(TEXT("Saved before callback close")));
                {
                    FScopedSquadSavedHook Hook(Room, [&](FGuid SavedId)
                    {
                        ++SavedCallbacks;
                        Test->TestEqual(TEXT("Closing callback receives the second committed identity"), SavedId, Fixture->Bravo);
                        Test->TestTrue(TEXT("Second save also starts return before notifying"), Room->bTransitioning);
                        Shell->UnloadCurrentSceneUI();
                    });
                    Test->TestTrue(TEXT("Save succeeds when its notification closes the room"), Room->SaveSelectedSquad());
                }
                Test->TestEqual(TEXT("Closing callback fires exactly once"), SavedCallbacks, 3);
                Test->TestFalse(TEXT("Closing callback leaves no editing draft to reopen"), Room->bEditingDraft);
                Test->TestFalse(TEXT("Closing callback keeps late edits blocked"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            }
            case 5:
                if (Shell->GetCurrentSceneUI()) return false;
                Test->TestNull(TEXT("Callback close completes without a later save return reopening the child"), Room->BaseUI.Get());
                Test->TestFalse(TEXT("Callback close removes squad subscriptions"), Fixture->Squads->OnSquadChanged.GetAllObjects().Contains(Room));
                Shell->RemoveFromParent();
                return true;
            }
            return false;
        }
    };

    /** Check the compact card and nearby picker using the real Blueprint geometry. */
    class FExerciseCompactMembersAndPicker final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();
        FString CaptureTag = TEXT("CompactSquadRoom");
        FSquadData OriginalAlpha;
        TSharedPtr<FUnitData> RemoteUnit;
        FName OriginalRemoteTile;
        bool bMovedRemoteUnit = false;
        int32 OriginalSquadCount = 0;

        struct FBounds { FVector2D Min; FVector2D Max; };
        TMap<FGuid, FBounds> MemberContentBounds;
        TSet<FGuid> GeometryCheckedMembers;
        bool bCheckingScrollEnd = false;
        int32 ScrollLayoutWaitTicks = 0;
        static FBounds BoundsIn(const UWidget* Child, const UWidget* Parent)
        {
            const FGeometry& ChildGeometry = Child->GetCachedGeometry();
            const FGeometry& ParentGeometry = Parent->GetCachedGeometry();
            return {ParentGeometry.AbsoluteToLocal(ChildGeometry.LocalToAbsolute(FVector2D::ZeroVector)),
                ParentGeometry.AbsoluteToLocal(ChildGeometry.LocalToAbsolute(ChildGeometry.GetLocalSize()))};
        }
        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
        void Capture(const TCHAR* Name)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SquadUI")
                / (CaptureTag + TEXT("_") + Name + TEXT(".png")), true, false);
        }
        bool Click(const TCHAR* Name)
        {
            auto* Button = Cast<UBasicButtonWidget>(Fixture->Room->GetWidgetFromName(Name));
            if (!Test->TestNotNull(Name, Button) || !Test->TestTrue(FString(Name) + TEXT(" is enabled"), Button->GetIsEnabled())) return false;
            Button->OnClicked.Broadcast();
            return true;
        }
        void RestoreRemoteUnit()
        {
            if (!bMovedRemoteUnit || !RemoteUnit) return;
            RemoteUnit->Modify([this](FUnitData& Unit) { Unit.RuntimeData.TileId = OriginalRemoteTile; });
            bMovedRemoteUnit = false;
        }
        void CheckCards(bool bRequireLastMemberVisible = false)
        {
            auto* Room = Fixture->Room.Get();
            auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList")));
            if (!Test->TestNotNull(TEXT("Compact members use the existing member list"), List)) return;
            Test->TestEqual(TEXT("Six selected members have six cards"), List->GetChildrenCount(), 6);
            int32 ViewWidth = 0, ViewHeight = 0;
            Room->GetOwningPlayer()->GetViewportSize(ViewWidth, ViewHeight);
            const bool bWideViewport = ViewWidth >= 1900 && float(ViewWidth) / FMath::Max(ViewHeight, 1) >= 1.7f;
            const FVector2D ViewportSize = List->GetCachedGeometry().GetLocalSize();
            Test->TestTrue(TEXT("Member list has a real viewport"), ViewportSize.X > 0 && ViewportSize.Y > 0);
            if (bWideViewport)
                Test->TestTrue(TEXT("Six members need no horizontal scrolling at 1920x1080"),
                    FMath::IsNearlyZero(List->GetScrollOffset(), 0.5f) && List->GetScrollOffsetOfEnd() <= 1.f);

            const FLinearColor CaptainGold = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("B69B61")));
            int32 FullyVisibleCards = 0;
            bool bLastMemberFullyVisible = false;
            for (int32 Index = 0; Index < Fixture->UnitIds.Num(); ++Index)
            {
                auto* Card = Fixture->FindMember(Fixture->UnitIds[Index]);
                if (!Test->TestNotNull(TEXT("Every selected unit has a compact member card"), Card)) continue;
                Test->TestTrue(TEXT("Compact cards retain canonical unit pointers"),
                    Card->GetUnitDataShared().Get() == Fixture->Units->GetUnitDataShared(Fixture->UnitIds[Index]).Get());
                const FVector2D Size = Card->GetCachedGeometry().GetLocalSize();
                Test->TestTrue(TEXT("Compact card width is 122 layout units"), FMath::IsNearlyEqual(Size.X, 122., 2.));
                Test->TestTrue(TEXT("Compact card height stays within 204 layout units"), Size.Y > 0 && Size.Y <= 206.);
                const FBounds CardBounds = BoundsIn(Card, List);
                FBounds* ContentBounds = MemberContentBounds.Find(Fixture->UnitIds[Index]);
                if (!ContentBounds)
                {
                    FBounds InitialContentBounds = CardBounds;
                    InitialContentBounds.Min.X += List->GetScrollOffset();
                    InitialContentBounds.Max.X += List->GetScrollOffset();
                    ContentBounds = &MemberContentBounds.Add(Fixture->UnitIds[Index], InitialContentBounds);
                }
                // Offscreen Slate descendants may retain old cached geometry. Determine which
                // cards should be visible from their initial content position and current scroll,
                // then require fresh geometry only for fully visible cards after layout settles.
                const bool bFullyVisible = ContentBounds->Min.X - List->GetScrollOffset() >= -1.
                    && ContentBounds->Max.X - List->GetScrollOffset() <= ViewportSize.X + 1.
                    && ContentBounds->Min.Y >= -1. && ContentBounds->Max.Y <= ViewportSize.Y + 1.;
                FullyVisibleCards += bFullyVisible ? 1 : 0;
                if (Index == Fixture->UnitIds.Num() - 1) bLastMemberFullyVisible = bFullyVisible;
                if (bWideViewport)
                    Test->TestTrue(TEXT("Every member card is fully inside the wide member viewport"), bFullyVisible);
                if (bFullyVisible)
                {
                    Test->TestTrue(TEXT("Fully visible cards have refreshed viewport geometry"),
                        CardBounds.Min.X >= -1. && CardBounds.Min.Y >= -1.
                        && CardBounds.Max.X <= ViewportSize.X + 1. && CardBounds.Max.Y <= ViewportSize.Y + 1.);
                }
                auto* Name = Cast<UTextBlock>(Card->GetWidgetFromName(TEXT("MemberName")));
                auto* Role = Cast<UTextBlock>(Card->GetWidgetFromName(TEXT("CaptainLabel")));
                auto* Portrait = Card->GetWidgetFromName(TEXT("MemberPortrait"));
                auto* Promote = Card->GetWidgetFromName(TEXT("PromoteButton"));
                auto* Remove = Card->GetWidgetFromName(TEXT("RemoveButton"));
                if (!Test->TestNotNull(TEXT("Compact card has a name header"), Name)
                    || !Test->TestNotNull(TEXT("Compact card has a role label"), Role)
                    || !Test->TestNotNull(TEXT("Compact card has an avatar"), Portrait)
                    || !Test->TestNotNull(TEXT("Compact card has a promote button"), Promote)
                    || !Test->TestNotNull(TEXT("Compact card has a remove button"), Remove)) continue;
                Test->TestFalse(TEXT("Compact member name remains on one line"), Name->GetAutoWrapText());
                Test->TestTrue(TEXT("Compact name and role use readable small fonts"),
                    Name->GetFont().Size >= 10 && Name->GetFont().Size <= 16
                    && Role->GetFont().Size >= 10 && Role->GetFont().Size <= 16);
                if (bFullyVisible)
                {
                    const FBounds NameBounds = BoundsIn(Name, Card);
                    const FBounds PortraitBounds = BoundsIn(Portrait, Card);
                    const FBounds RoleBounds = BoundsIn(Role, Card);
                    const FBounds PromoteBounds = BoundsIn(Promote, Card);
                    const FBounds RemoveBounds = BoundsIn(Remove, Card);
                    Test->TestTrue(TEXT("Name header sits above the avatar"), NameBounds.Max.Y <= PortraitBounds.Min.Y + 1.);
                    Test->TestTrue(TEXT("Role appears beside the avatar"), RoleBounds.Min.X >= PortraitBounds.Max.X - 1.
                        && RoleBounds.Min.Y < PortraitBounds.Max.Y && RoleBounds.Max.Y > PortraitBounds.Min.Y);
                    Test->TestTrue(TEXT("Promotion is below the avatar and role"),
                        PromoteBounds.Min.Y >= FMath::Max(PortraitBounds.Max.Y, RoleBounds.Max.Y) - 1.);
                    Test->TestTrue(TEXT("Removal is stacked below promotion"), RemoveBounds.Min.Y >= PromoteBounds.Max.Y - 1.);
                    GeometryCheckedMembers.Add(Fixture->UnitIds[Index]);
                }
                const FLinearColor RoleColor = Role->GetColorAndOpacity().GetSpecifiedColor();
                if (Index == 0)
                {
                    Test->TestEqual(TEXT("First member remains the draft captain"), Role->GetText().ToString(), FString(TEXT("队长")));
                    Test->TestTrue(TEXT("Captain role uses dark gold"), RoleColor.Equals(CaptainGold, 0.01f));
                    if (auto* Accent = Cast<UImage>(Card->GetWidgetFromName(TEXT("CaptainAccent"))))
                        Test->TestTrue(TEXT("Captain accent matches the dark gold role"), Accent->GetColorAndOpacity().Equals(CaptainGold, 0.01f));
                }
                else
                {
                    Test->TestEqual(TEXT("Other compact cards show member role"), Role->GetText().ToString(), FString(TEXT("队员")));
                    Test->TestTrue(TEXT("Member role retains its blue style"), RoleColor.B > RoleColor.R && RoleColor.A > 0.9f);
                }
            }
            Test->TestTrue(TEXT("The member viewport contains complete, inspectable cards"), FullyVisibleCards > 0);
            if (bRequireLastMemberVisible)
                Test->TestTrue(TEXT("Scrolling to the end makes the sixth member fully visible"), bLastMemberFullyVisible);
        }
        void CheckPicker()
        {
            auto* Room = Fixture->Room.Get();
            if (auto* Panel = Room->GetWidgetFromName(TEXT("IconPickerPanel")))
                Test->TestTrue(TEXT("Picker overlay does not consume pointer input across the full screen"),
                    Panel->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
            UWidget* Picker = Room->GetWidgetFromName(TEXT("IconPickerFit"));
            if (!Picker) Picker = Room->GetWidgetFromName(TEXT("IconPickerCard"));
            auto* Emblem = Room->GetWidgetFromName(TEXT("ChooseSquadIconButton"));
            if (!Test->TestNotNull(TEXT("Picker has a compact positioned container"), Picker)
                || !Test->TestNotNull(TEXT("Picker has an emblem button anchor"), Emblem)) return;
            const FBounds Popup = BoundsIn(Picker, Room);
            const FBounds Anchor = BoundsIn(Emblem, Room);
            const FVector2D RootSize = Room->GetCachedGeometry().GetLocalSize();
            Test->TestTrue(TEXT("Picker stays contained in the room at either viewport size"),
                Popup.Min.X >= -1. && Popup.Min.Y >= -1. && Popup.Max.X <= RootSize.X + 1. && Popup.Max.Y <= RootSize.Y + 1.);
            Test->TestTrue(TEXT("Picker appears immediately left of the emblem instead of centered in the room"),
                Popup.Max.X <= Anchor.Min.X + 1. && Anchor.Min.X - Popup.Max.X <= 40.);
            Test->TestTrue(TEXT("Picker stays vertically near its emblem"), Popup.Min.Y <= Anchor.Max.Y + 24. && Popup.Max.Y >= Anchor.Min.Y - 24.);
            Test->TestTrue(TEXT("Picker uses a compact pop-over footprint"),
                Popup.Max.X - Popup.Min.X <= 282. && Popup.Max.Y - Popup.Min.Y <= 302.);
        }

    public:
        FExerciseCompactMembersAndPicker(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest)
        {
            if (!FParse::Value(FCommandLine::Get(), TEXT("CompactCaptureTag="), CaptureTag))
                FParse::Value(FCommandLine::Get(), TEXT("SquadCaptureTag="), CaptureTag);
            CaptureTag = FPaths::MakeValidFileName(CaptureTag);
        }
        virtual ~FExerciseCompactMembersAndPicker() override { RestoreRemoteUnit(); }

        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            auto* Shell = Fixture->Shell.Get();
            if (!Room || !Shell || !Fixture->World.IsValid()) return true;
            const double Elapsed = FPlatformTime::Seconds() - StepStarted;
            if (Elapsed > 10.)
            {
                Test->AddError(FString::Printf(TEXT("Compact member/picker test timed out at step %d."), Step));
                return true;
            }
            FSquadData Data;
            switch (Step)
            {
            case 0:
                if (Room->bTransitioning) return false;
                OriginalSquadCount = Fixture->Squads->GetSquadIds().Num();
                Fixture->Squads->GetSquad(Fixture->Alpha, OriginalAlpha);
                RemoteUnit = Fixture->Units->GetUnitDataShared(Fixture->UnitIds[5]);
                if (!Test->TestTrue(TEXT("Sixth canonical fixture unit exists"), RemoteUnit.IsValid())) return true;
                OriginalRemoteTile = RemoteUnit->RuntimeData.TileId;
                bMovedRemoteUnit = true;
                RemoteUnit->Modify([](FUnitData& Unit) { Unit.RuntimeData.TileId = LocalTile; });
                Test->TestTrue(TEXT("Open Alpha as a compact-layout draft"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 1:
            {
                if (Room->bTransitioning) return false;
                FVehicleData SixSeatVehicle;
                SixSeatVehicle.VehicleId = FGuid::NewGuid();
                SixSeatVehicle.Attributes.PassengerCapacity = 6;
                SixSeatVehicle.RuntimeData.TileId = LocalTile;
                Test->TestTrue(TEXT("Layout test vehicle is registered in the player store"), Fixture->RegisterVehicle(SixSeatVehicle));
                TArray<FGuid> Removed;
                Test->TestTrue(TEXT("Six-member layout draft has a six-seat vehicle"), Room->SetSelectedSquadVehicle(SixSeatVehicle, Removed));
                for (const FGuid Id : Fixture->UnitIds)
                    Test->TestTrue(TEXT("All six colocated units can join the unsaved draft"), Room->AddUnitToSelectedSquad(Id));
                Next(); return false;
            }
            case 2:
                if (Elapsed < 0.25) return false;
                CheckCards();
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Six-card preview does not change the canonical roster"), SameSquadState(Data, OriginalAlpha));
                Capture(TEXT("Compact_01_Members"));
                Next(); return false;
            case 3:
            {
                if (Elapsed < 0.3) return false;
                auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList")));
                bCheckingScrollEnd = List && List->GetScrollOffsetOfEnd() > 1.f;
                if (bCheckingScrollEnd) List->ScrollToEnd();
                ScrollLayoutWaitTicks = 0;
                Next(); return false;
            }
            case 4:
            {
                if (++ScrollLayoutWaitTicks < 3 || Elapsed < 0.15) return false;
                auto* List = Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("MemberList")));
                if (bCheckingScrollEnd && List)
                {
                    Test->TestTrue(TEXT("Narrow member list reaches its scrollable end"), List->GetScrollOffset() > 0.f
                        && FMath::IsNearlyEqual(List->GetScrollOffset(), List->GetScrollOffsetOfEnd(), 1.f));
                    CheckCards(true);
                    List->ScrollToStart();
                }
                Test->TestEqual(TEXT("Every member's internal layout was checked while fully visible"), GeometryCheckedMembers.Num(), 6);
                Next(); return false;
            }
            case 5:
                if (Elapsed < 0.15) return false;
                Click(TEXT("ChooseSquadIconButton"));
                Test->TestTrue(TEXT("Emblem button opens the nearby picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Next(); return false;
            case 6:
                if (Elapsed < 0.2) return false;
                CheckPicker();
                Capture(TEXT("Compact_02_Picker"));
                Next(); return false;
            case 7:
                if (Elapsed < 0.3) return false;
                Click(TEXT("CloseIconPickerButton"));
                Test->TestFalse(TEXT("Close button dismisses the nearby picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Click(TEXT("ChooseSquadIconButton"));
                Click(TEXT("UseCaptainPortraitButton"));
                Test->TestFalse(TEXT("Picking an icon dismisses the nearby picker"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Test->TestTrue(TEXT("Picker selection updates the draft icon"), Room->GetSelectedSquad(Data) && Data.IconSource == ESquadIconSource::CaptainPortrait);
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Nearby picker still edits only the unsaved draft"), SameSquadState(Data, OriginalAlpha));
                Room->ReturnToSquadList();
                Next(); return false;
            case 8:
                if (Room->bTransitioning) return false;
                RestoreRemoteUnit();
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Returning discards all compact-preview edits"), SameSquadState(Data, OriginalAlpha));
                Test->TestEqual(TEXT("Preview and picker create no squads"), Fixture->Squads->GetSquadIds().Num(), OriginalSquadCount);
                Test->TestEqual(TEXT("Sixth unit is restored to its remote tile"), RemoteUnit->RuntimeData.TileId, OriginalRemoteTile);
                Test->TestTrue(TEXT("Existing squad can be reopened after preview cancellation"), Room->SelectSquad(Fixture->Alpha));
                Next(); return false;
            case 9:
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Icon can be edited after cancelling the six-member preview"), Room->UseCaptainPortrait());
                Test->TestTrue(TEXT("Explicit Save Squad still commits an icon edit"), Room->SaveSelectedSquad());
                Fixture->Squads->GetSquad(Fixture->Alpha, Data);
                Test->TestTrue(TEXT("Saved icon mode is canonical"), Data.IconSource == ESquadIconSource::CaptainPortrait);
                Test->TestTrue(TEXT("Saving the later icon edit does not commit cancelled members"), Data.MemberUnitIds == OriginalAlpha.MemberUnitIds);
                Next(); return false;
            case 10:
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Saving an icon edit returns to selection after the return animation"),
                    Room->CurrentLayer == ESquadRoomLayer::SquadSelection && !Room->bEditingDraft);
                Shell->UnloadCurrentSceneUI();
                Next(); return false;
            case 11:
                if (Shell->GetCurrentSceneUI()) return false;
                Test->TestFalse(TEXT("Closing releases the compact-layout draft"), Room->bEditingDraft);
                Test->TestFalse(TEXT("Closing leaves the nearby picker hidden"), IsDisplayed(Room->GetWidgetFromName(TEXT("IconPickerPanel"))));
                Shell->RemoveFromParent();
                return true;
            }
            return false;
        }
    };

    /** Exercise the animations saved in the real WBP, rather than the native fallback clock. */
    class FExerciseBlueprintAnimations final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();
        UWidgetAnimation* SquadEntrance = nullptr;
        FVector2D FirstTranslation = FVector2D::ZeroVector;
        float FirstOpacity = 0.f;
        bool bSawEntrance = false;
        bool bSawTranslationChange = false;
        bool bSawOpacityChange = false;
        bool bSawExtendedEntrance = false;
        bool bSawSquadExit = false;
        bool bSawPersonnelEntrance = false;
        bool bSawManagementEntrance = false;
        bool bSawMembersEntrance = false;
        bool bSawDelayedTail = false;
        bool bExpectDelayedTail = false;
        double RemainingManagementPlayback = 0.;
        bool bSawPersonnelExit = false;
        bool bSawManagementExit = false;
        bool bSawMembersExit = false;
        bool bSawReturnEntrance = false;
        bool bSawClosingAnimation = false;

        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
        bool IsPlaying(ESquadRoomPanel Panel, bool bEntering) const
        {
            auto* Room = Fixture->Room.Get();
            auto* Animation = Room->GetPanelTransitionAnimation(Panel, bEntering);
            return Animation && Room->IsAnimationPlaying(Animation);
        }
        void CheckInputLocked(bool bAllPanels)
        {
            auto* Room = Fixture->Room.Get();
            Test->TestTrue(TEXT("Animation keeps the room transition locked"), Room->bTransitioning);
            const TCHAR* Names[] = {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("MemberStripPanel")};
            for (int32 Index = 0; Index < (bAllPanels ? 3 : 1); ++Index)
                if (auto* Panel = Room->GetWidgetFromName(Names[Index]))
                    Test->TestFalse(FString(Names[Index]) + TEXT(" does not accept input until animations finish"), Panel->GetIsEnabled());
        }
        void MeasureManagementEntranceTiming()
        {
            auto* Room = Fixture->Room.Get();
            const double Speed = FMath::Max(0.01f, Room->WidgetAnimationPlaybackSpeed);
            double LeftDuration = 0.;
            double OtherDuration = 0.;
            for (const ESquadRoomPanel Panel : {ESquadRoomPanel::LeftList, ESquadRoomPanel::Management, ESquadRoomPanel::Members})
            {
                const UWidgetAnimation* Animation = Room->GetPanelTransitionAnimation(Panel, true);
                if (!Test->TestNotNull(TEXT("Every management panel has an entrance animation"), Animation)) continue;
                const double Duration = FMath::Max(0.f, Animation->GetEndTime() - Animation->GetStartTime());
                if (Panel == ESquadRoomPanel::LeftList) LeftDuration = Duration / Speed;
                else OtherDuration = FMath::Max(OtherDuration, Duration / Speed);
                // The layer may have started on the preceding UI tick. Account for playback
                // already observed, rather than treating this latent-command step as time zero.
                const double Remaining = FMath::Max(0., Duration - Room->GetAnimationCurrentTime(Animation)) / Speed;
                RemainingManagementPlayback = FMath::Max(RemainingManagementPlayback, Remaining);
            }
            // Equal-duration user-authored animations legitimately finish together. A delayed
            // tail is expected only when the saved right/bottom range extends beyond the left.
            bExpectDelayedTail = OtherDuration > LeftDuration + 0.01;
        }
        void CheckAnimationAssets()
        {
            const auto* Class = Cast<UWidgetBlueprintGeneratedClass>(Fixture->Room->GetClass());
            if (!Test->TestNotNull(TEXT("Meeting room is a generated widget Blueprint"), Class)) return;
            const TCHAR* Expected[] = {
                TEXT("小队列表滑入"), TEXT("小队列表滑出"), TEXT("人员列表滑入"), TEXT("人员列表滑出"),
                TEXT("管理面板滑入"), TEXT("管理面板滑出"), TEXT("成员名单滑入"), TEXT("成员名单滑出")};
            for (const TCHAR* Label : Expected)
            {
                const UWidgetAnimation* Found = nullptr;
                for (const auto& Animation : Class->Animations)
                {
                    if (!Animation) continue;
                    bool bMatches = Animation->GetName().Contains(Label);
#if WITH_EDITOR
                    bMatches |= Animation->GetDisplayLabel() == Label;
#endif
                    if (bMatches) { Found = Animation; break; }
                }
                if (Test->TestNotNull(FString(TEXT("Real Blueprint animation exists: ")) + Label, Found))
                    Test->TestTrue(FString(Label) + TEXT(" contains a nonzero playback range"), Found->GetEndTime() > Found->GetStartTime());
            }
        }

    public:
        FExerciseBlueprintAnimations(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest)
        {
            CheckAnimationAssets();
            SquadEntrance = Fixture->Room->GetPanelTransitionAnimation(ESquadRoomPanel::LeftList, true);
            Test->TestNotNull(TEXT("Squad layer resolves its bound entrance animation"), SquadEntrance);
        }

        bool Update() override
        {
            auto* Room = Fixture->Room.Get();
            auto* Shell = Fixture->Shell.Get();
            if (!Room || !Shell || !Fixture->World.IsValid())
            {
                Test->AddError(TEXT("Blueprint-animation fixture disappeared."));
                return true;
            }
            const double Elapsed = FPlatformTime::Seconds() - StepStarted;
            if (Elapsed > 10.)
            {
                Test->AddError(FString::Printf(TEXT("Blueprint animation did not finish at step %d."), Step));
                return true;
            }
            switch (Step)
            {
            case 0:
            {
                auto* Left = Room->GetWidgetFromName(TEXT("LeftPanel"));
                if (!Left || !SquadEntrance) return true;
                if (Room->IsAnimationPlaying(SquadEntrance))
                {
                    const FVector2D Translation = Left->GetRenderTransform().Translation;
                    const float Opacity = Left->GetRenderOpacity();
                    if (!bSawEntrance)
                    {
                        FirstTranslation = Translation;
                        FirstOpacity = Opacity;
                        bSawEntrance = true;
                    }
                    else
                    {
                        bSawTranslationChange |= !Translation.Equals(FirstTranslation, 0.1);
                        bSawOpacityChange |= !FMath::IsNearlyEqual(Opacity, FirstOpacity, 0.005f);
                    }
                    // At half speed this position requires > 0.26 seconds of playback,
                    // beyond the old fixed 0.22-second native completion timer.
                    bSawExtendedEntrance |= Room->GetAnimationCurrentTime(SquadEntrance) > 0.13f;
                    CheckInputLocked(false);
                }
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("The saved UMG entrance actually played"), bSawEntrance);
                Test->TestTrue(TEXT("UMG entrance animates the left panel translation"), bSawTranslationChange);
                Test->TestTrue(TEXT("UMG entrance animates the left panel opacity"), bSawOpacityChange);
                Test->TestTrue(TEXT("Slower UMG playback outlasts the fallback duration without premature completion"), bSawExtendedEntrance);
                Test->TestFalse(TEXT("Entrance completion has no animation still running"), Room->IsAnyAnimationPlaying());
                Test->TestTrue(TEXT("Entrance settles at the final left-panel position"), Left->GetRenderTransform().Translation.IsNearlyZero(0.1));
                Test->TestTrue(TEXT("Entrance restores full opacity"), FMath::IsNearlyEqual(Left->GetRenderOpacity(), 1.f));
                if (!Test->TestTrue(TEXT("Begin animated squad selection"), Room->SelectSquad(Fixture->Alpha))) return true;
                Next();
                return false;
            }
            case 1:
                if (Room->CurrentLayer == ESquadRoomLayer::SquadSelection)
                {
                    bSawSquadExit |= IsPlaying(ESquadRoomPanel::LeftList, false);
                    CheckInputLocked(false);
                    return false;
                }
                Test->TestTrue(TEXT("Old squad list plays its own UMG exit before replacement"), bSawSquadExit);
                Test->TestTrue(TEXT("Personnel layer resolves a different entrance animation"),
                    Room->GetPanelTransitionAnimation(ESquadRoomPanel::LeftList, true) != SquadEntrance);
                MeasureManagementEntranceTiming();
                Next();
                return false;
            case 2:
            {
                const bool bLeftPlaying = IsPlaying(ESquadRoomPanel::LeftList, true);
                const bool bRightPlaying = IsPlaying(ESquadRoomPanel::Management, true);
                const bool bBottomPlaying = IsPlaying(ESquadRoomPanel::Members, true);
                bSawPersonnelEntrance |= bLeftPlaying;
                bSawManagementEntrance |= bRightPlaying;
                bSawMembersEntrance |= bBottomPlaying;
                bSawDelayedTail |= !bLeftPlaying && (bRightPlaying || bBottomPlaying);
                if (bLeftPlaying || bRightPlaying || bBottomPlaying) CheckInputLocked(true);
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Personnel list plays its UMG entrance"), bSawPersonnelEntrance);
                Test->TestTrue(TEXT("Right management panel plays its UMG entrance"), bSawManagementEntrance);
                Test->TestTrue(TEXT("Bottom member strip plays its UMG entrance"), bSawMembersEntrance);
                if (bExpectDelayedTail)
                    Test->TestTrue(TEXT("Longer saved right/bottom entrances keep interaction locked after the left finishes"), bSawDelayedTail);
                const double TickTolerance = FMath::Max(0.02, 2. * Fixture->World->GetDeltaSeconds());
                Test->TestTrue(TEXT("Management completion waits for the actual longest saved animation at the selected playback speed"),
                    Elapsed + TickTolerance >= RemainingManagementPlayback);
                Test->TestFalse(TEXT("Management becomes interactive only after every animation ends"), Room->IsAnyAnimationPlaying());
                for (const TCHAR* Name : {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("MemberStripPanel")})
                    if (auto* Panel = Room->GetWidgetFromName(Name))
                        Test->TestTrue(FString(Name) + TEXT(" becomes enabled at the completed layer"), Panel->GetIsEnabled());
                Room->ReturnToSquadList();
                Next();
                return false;
            }
            case 3:
                if (Room->CurrentLayer == ESquadRoomLayer::SquadManagement)
                {
                    bSawPersonnelExit |= IsPlaying(ESquadRoomPanel::LeftList, false);
                    bSawManagementExit |= IsPlaying(ESquadRoomPanel::Management, false);
                    bSawMembersExit |= IsPlaying(ESquadRoomPanel::Members, false);
                    CheckInputLocked(true);
                    return false;
                }
                bSawReturnEntrance |= IsPlaying(ESquadRoomPanel::LeftList, true);
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Return plays personnel-list exit"), bSawPersonnelExit);
                Test->TestTrue(TEXT("Return plays management-panel exit"), bSawManagementExit);
                Test->TestTrue(TEXT("Return plays member-strip exit"), bSawMembersExit);
                Test->TestTrue(TEXT("Return plays squad-list entrance after the outgoing panels finish"), bSawReturnEntrance);
                Test->TestFalse(TEXT("Return collapses the right panel"), IsDisplayed(Room->GetWidgetFromName(TEXT("ManagementPanel"))));
                Test->TestFalse(TEXT("Return collapses the member strip"), IsDisplayed(Room->GetWidgetFromName(TEXT("MemberStripPanel"))));
                if (!Test->TestTrue(TEXT("Reopen management before unloading"), Room->SelectSquad(Fixture->Bravo))) return true;
                Next();
                return false;
            case 4:
                if (Room->bTransitioning) return false;
                Shell->UnloadCurrentSceneUI();
                Test->TestTrue(TEXT("Normal unload retains the widget while exit animations play"), Shell->GetCurrentSceneUI() == Room);
                Next();
                return false;
            case 5:
                if (Shell->GetCurrentSceneUI())
                {
                    const bool bPlaying = IsPlaying(ESquadRoomPanel::LeftList, false)
                        || IsPlaying(ESquadRoomPanel::Management, false) || IsPlaying(ESquadRoomPanel::Members, false);
                    bSawClosingAnimation |= bPlaying;
                    if (bPlaying) CheckInputLocked(true);
                    return false;
                }
                Test->TestTrue(TEXT("Normal unload used real UMG exit animations"), bSawClosingAnimation);
                Test->TestFalse(TEXT("Unloaded widget has no running animations"), Room->IsAnyAnimationPlaying());
                Test->TestNull(TEXT("Animation completion releases the base shell"), Room->BaseUI.Get());
                Test->TestFalse(TEXT("Animation completion removes squad subscriptions"), Fixture->Squads->OnSquadChanged.GetAllObjects().Contains(Room));
                Test->TestFalse(TEXT("Animation completion removes unit subscriptions"), Fixture->Units->OnUnitDataChanged.GetAllObjects().Contains(Room));
                Shell->RemoveFromParent();
                return true;
            }
            return false;
        }
    };

    class FInterruptRoomTransitions final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double StepStarted = FPlatformTime::Seconds();

        void Next() { ++Step; StepStarted = FPlatformTime::Seconds(); }
        void CheckDetached(USquadMeetingRoomWidget* Room)
        {
            Test->TestNull(TEXT("Interrupted unload removes the host's current child"), Fixture->Shell->GetCurrentSceneUI());
            Test->TestNull(TEXT("Interrupted unload releases child's host"), Room->BaseUI.Get());
            Test->TestFalse(TEXT("Interrupted unload removes squad subscriptions"), Fixture->Squads->OnSquadChanged.GetAllObjects().Contains(Room));
            Test->TestFalse(TEXT("Interrupted unload removes unit subscriptions"), Fixture->Units->OnUnitDataChanged.GetAllObjects().Contains(Room));
        }
        void InterruptWithoutPoseJump(USquadMeetingRoomWidget* Room)
        {
            const TCHAR* Names[] = {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("MemberStripPanel")};
            FVector2D BeforeTranslation[3];
            float BeforeOpacity[3];
            for (int32 Index = 0; Index < 3; ++Index)
            {
                UWidget* Panel = Room->GetWidgetFromName(Names[Index]);
                BeforeTranslation[Index] = Panel ? Panel->GetRenderTransform().Translation : FVector2D::ZeroVector;
                BeforeOpacity[Index] = Panel ? Panel->GetRenderOpacity() : 0.f;
            }
            Fixture->Shell->UnloadCurrentSceneUI();
            for (int32 Index = 0; Index < 3; ++Index)
                if (UWidget* Panel = Room->GetWidgetFromName(Names[Index]))
                {
                    Test->TestTrue(FString(Names[Index]) + TEXT(" retains its current position when interrupted"),
                        Panel->GetRenderTransform().Translation.Equals(BeforeTranslation[Index], 0.1));
                    Test->TestTrue(FString(Names[Index]) + TEXT(" retains its current opacity when interrupted"),
                        FMath::IsNearlyEqual(Panel->GetRenderOpacity(), BeforeOpacity[Index], 0.005f));
                }
        }

    public:
        FInterruptRoomTransitions(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest) {}

        bool Update() override
        {
            auto* Shell = Fixture->Shell.Get();
            auto* Room = Fixture->Room.Get();
            if (!Shell || !Room || !Fixture->World.IsValid())
            {
                Test->AddError(TEXT("Interrupted-transition fixture disappeared."));
                return true;
            }
            const double Elapsed = FPlatformTime::Seconds() - StepStarted;
            // A normal two-phase transition is 0.44 s. This bound also catches a stale
            // management page that no longer has a squad but stopped scheduling a return.
            if (Elapsed > (Step == 0 ? 10. : 2.))
            {
                Test->AddError(FString::Printf(TEXT("Interrupted transition did not settle within two seconds (step %d, layer %d, transitioning %d)."),
                    Step, int32(Room->CurrentLayer), Room->bTransitioning));
                return true;
            }
            switch (Step)
            {
            case 0:
                if (Room->bTransitioning) return false;
                if (!Test->TestTrue(TEXT("Select squad to begin the management animation"), Room->SelectSquad(Fixture->Alpha))) return true;
                Test->TestTrue(TEXT("Selection animation is active before external deletion"), Room->bTransitioning);
                if (!Test->TestTrue(TEXT("External subsystem can disband the selected squad during animation"),
                    Fixture->Squads->RemoveSquad(Fixture->Alpha, Fixture->Error))) return true;
                Fixture->CreatedSquads.Remove(Fixture->Alpha);
                Next();
                return false;
            case 1:
                if (Room->bTransitioning || Room->CurrentLayer != ESquadRoomLayer::SquadSelection) return false;
                Test->TestFalse(TEXT("External disband clears the selected squad after returning"), Room->SelectedSquadId.IsValid());
                Test->TestFalse(TEXT("No stale management panel remains after disband"), IsDisplayed(Room->GetWidgetFromName(TEXT("ManagementPanel"))));
                Test->TestFalse(TEXT("No stale member strip remains after disband"), IsDisplayed(Room->GetWidgetFromName(TEXT("MemberStripPanel"))));
                if (auto* Pages = Cast<UWidgetSwitcher>(Room->GetWidgetFromName(TEXT("LeftPages"))))
                    Test->TestEqual(TEXT("External disband returns the left side to squad selection"), Pages->GetActiveWidgetIndex(), 0);
                else Test->AddError(TEXT("Meeting room is missing LeftPages."));
                if (!Test->TestTrue(TEXT("Another squad remains selectable after recovery"), Room->SelectSquad(Fixture->Bravo))) return true;
                Next();
                return false;
            case 2:
                if (Room->bTransitioning) return false;
                Test->TestTrue(TEXT("Recovered room can enter management again"), Room->CurrentLayer == ESquadRoomLayer::SquadManagement);
                Room->ReturnToSquadList();
                Test->TestTrue(TEXT("Return-to-list animation is active"), Room->bTransitioning);
                Test->TestTrue(TEXT("Management is still the mounted layer while it slides out"), Room->CurrentLayer == ESquadRoomLayer::SquadManagement);
                Next();
                return false;
            case 3:
                if (Elapsed < 0.075) return false;
                Test->TestTrue(TEXT("Interrupt return after animation has advanced"), Room->bTransitioning);
                InterruptWithoutPoseJump(Room);
                Test->TestTrue(TEXT("Unload interrupts return animation without immediate removal"), Shell->GetCurrentSceneUI() == Room);
                Test->TestFalse(TEXT("Interrupted closing room rejects late selection"), Room->SelectSquad(Fixture->Bravo));
                Next();
                return false;
            case 4:
            {
                if (Shell->GetCurrentSceneUI()) return false;
                CheckDetached(Room);
                UClass* RoomClass = LoadClass<USquadMeetingRoomWidget>(nullptr, RoomClassPath);
                if (!Test->TestNotNull(TEXT("Room class can be reopened"), RoomClass)) return true;
                {
                    TGuardValue<float> Override(RoomClass->GetDefaultObject<USquadMeetingRoomWidget>()->TransitionDuration, Fixture->Duration);
                    Fixture->Room.Reset(Cast<USquadMeetingRoomWidget>(Shell->CreateSceneUIByTag(
                        FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom")))));
                }
                Room = Fixture->Room.Get();
                if (!Test->TestNotNull(TEXT("Room can be recreated after interrupted exit"), Room)) return true;
                Test->TestTrue(TEXT("Recreated room begins its entry animation"), Room->bTransitioning);
                Test->TestTrue(TEXT("Recreated room accepts tile while entering"), Room->LoadSquadList(LocalTile));
                InterruptWithoutPoseJump(Room);
                Test->TestTrue(TEXT("Immediate entry cancellation retains widget until exit completes"), Shell->GetCurrentSceneUI() == Room);
                Test->TestFalse(TEXT("Cancelled entry rejects squad selection"), Room->SelectSquad(Fixture->Bravo));
                Next();
                return false;
            }
            case 5:
                if (Shell->GetCurrentSceneUI()) return false;
                CheckDetached(Room);
                Shell->RemoveFromParent();
                return true;
            }
            return false;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomControlsTest, "SilverChoir.BaseUI.SquadMeetingRoom.DataAndControls",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomControlsTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseRoom(Fixture, this, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomTransitionTest, "SilverChoir.BaseUI.SquadMeetingRoom.TransitionsAndCapture",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomTransitionTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.22f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseRoom(Fixture, this, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomInterruptedTransitionsTest, "SilverChoir.BaseUI.SquadMeetingRoom.InterruptedTransitions",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomInterruptedTransitionsTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.22f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FInterruptRoomTransitions(Fixture, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomBlueprintAnimationsTest, "SilverChoir.BaseUI.SquadMeetingRoom.BlueprintAnimations",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomBlueprintAnimationsTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.22f, 0.5f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseBlueprintAnimations(Fixture, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomCompactMembersAndPickerTest, "SilverChoir.BaseUI.SquadMeetingRoom.CompactMembersAndPicker",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomCompactMembersAndPickerTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseCompactMembersAndPicker(Fixture, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomVehicleCapacityTest, "SilverChoir.BaseUI.SquadMeetingRoom.VehicleCapacity",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomVehicleCapacityTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseVehicleCapacity(Fixture, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadRoomSaveNavigationTest, "SilverChoir.BaseUI.SquadMeetingRoom.SaveNavigation",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadRoomSaveNavigationTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadMeetingRoomTests::FFixture>();
    if (!Fixture->Initialize(*this, 0.22f)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadMeetingRoomTests::FExerciseSaveNavigation(Fixture, this));
    return true;
}
#endif
