#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "SMS_SceneBase.h"
#include "SMS_SceneLibrary.h"
#include "SMS_SceneManager.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace PersonnelReturnFlowTests
{
    template<typename TProperty>
    TProperty* Property(const UObject* Object, const TCHAR* Name)
    {
        return Object ? FindFProperty<TProperty>(Object->GetClass(), Name) : nullptr;
    }

    int32 BoolValue(const UObject* Object, const TCHAR* Name)
    {
        const auto* Field = Property<FBoolProperty>(Object, Name);
        return Field ? int32(Field->GetPropertyValue_InContainer(Object)) : -1;
    }

    struct FFixture
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameMainMapPlayerController> Controller;
        TWeakObjectPtr<AFCS_FreeCameraPawn> Camera;
        TStrongObjectPtr<UBaseMapWidget> PreviousShell;
        TStrongObjectPtr<UBaseMapWidget> Shell;
        TStrongObjectPtr<USMS_SceneManager> Manager;
        TStrongObjectPtr<UPersonnelPreparationRoomWidget> Room;
        FGameplayTag PersonnelTag;
        FGameplayTag OverviewTag;
        bool bSavedMovementDisabled = false;
        bool bSavedRotationDisabled = false;
        bool bOwnsGlobalFlow = false;

        bool Initialize(FAutomationTestBase& Test)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
            if (!Test.TestNotNull(TEXT("Return flow requires a real game world"), World.Get())) return false;
            Controller = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
            Camera = UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World.Get());
            Manager.Reset(USMS_SceneLibrary::GetSceneManager(World.Get()));
            if (!Test.TestNotNull(TEXT("GameMain player controller exists"), Controller.Get())
                || !Test.TestNotNull(TEXT("A registered free camera exists"), Camera.Get())
                || !Test.TestNotNull(TEXT("Blueprint and test use the same global scene manager"), Manager.Get())) return false;

            // Run in the standalone GameMainMap automation world. Never tear down an existing
            // user scene just to set up a fixture: its state cannot be restored synchronously.
            if (!Test.TestTrue(TEXT("Global scene manager is initialized"), Manager->IsReady())
                || !Test.TestTrue(TEXT("Start with an idle, empty scene manager"),
                    Manager->GetTransitionState() == ESMS_SceneTransitionState::Idle && !Manager->GetCurrentScene())) return false;
            bOwnsGlobalFlow = true;
            bSavedMovementDisabled = UFCS_FreeCameraBlueprintLibrary::IsFreeCameraMovementDisabled(World.Get());
            bSavedRotationDisabled = UFCS_FreeCameraBlueprintLibrary::IsFreeCameraRotationDisabled(World.Get());

            UClass* PersonnelClass = LoadClass<USMS_SceneBase>(nullptr,
                TEXT("/Game/System/Map/BaseMap/Scene/BP_人员整备室_Scene.BP_人员整备室_Scene_C"));
            UClass* OverviewClass = LoadClass<USMS_SceneBase>(nullptr,
                TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene.BP_基地全景_Scene_C"));
            UClass* ShellClass = LoadClass<UBaseMapWidget>(nullptr,
                TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C"));
            if (!Test.TestNotNull(TEXT("Personnel scene Blueprint exists"), PersonnelClass)
                || !Test.TestNotNull(TEXT("Base overview scene Blueprint exists"), OverviewClass)
                || !Test.TestNotNull(TEXT("Base shell Blueprint exists"), ShellClass)) return false;
            PersonnelTag = PersonnelClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            OverviewTag = OverviewClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            PreviousShell.Reset(Controller->BaseWidget);
            Shell.Reset(CreateWidget<UBaseMapWidget>(Controller.Get(), ShellClass));
            if (!Test.TestNotNull(TEXT("Actual base shell created"), Shell.Get())) return false;
            Shell->AddToViewport(10000);
            Controller->BaseWidget = Shell.Get();
            return Test.TestNotNull(TEXT("Actual inherited return button exists"), BackButton());
        }

        ~FFixture()
        {
            if (bOwnsGlobalFlow && Manager.IsValid())
            {
                // Restore the initially empty runtime registry without touching its configured
                // classes or saved Blueprint defaults. These tests run in a dedicated game process.
                Manager->ShutdownManager();
                Manager->InitializeManager();
            }
            if (Camera.IsValid()) Camera->CancelCameraStateMove();
            if (World.IsValid() && bOwnsGlobalFlow)
            {
                UFCS_FreeCameraBlueprintLibrary::SetFreeCameraMovementDisabled(World.Get(), bSavedMovementDisabled);
                UFCS_FreeCameraBlueprintLibrary::SetFreeCameraRotationDisabled(World.Get(), bSavedRotationDisabled);
            }
            if (Shell.IsValid())
            {
                if (Controller.IsValid()) Controller->BaseWidget = PreviousShell.Get();
                Shell->RemoveFromParent();
            }
        }

        UBasicButtonWidget* BackButton() const
        {
            const auto* Field = Property<FObjectPropertyBase>(Shell.Get(), TEXT("BackButton"));
            return Field ? Cast<UBasicButtonWidget>(Field->GetObjectPropertyValue_InContainer(Shell.Get())) : nullptr;
        }

        bool IsIdleAt(FGameplayTag Tag) const
        {
            return Manager->GetTransitionState() == ESMS_SceneTransitionState::Idle
                && Manager->GetCurrentSceneTag() == Tag;
        }

        FString Diagnostics(int32 Step) const
        {
            const auto* Scene = Manager->GetCurrentScene();
            const auto* CurrentRoom = Cast<UPersonnelPreparationRoomWidget>(Shell->GetCurrentSceneUI());
            const auto* Button = BackButton();
            return FString::Printf(TEXT("step=%d SMS=%d current=%s pending=%s UiFinish=%d CameraFinish=%d Room=%s Loaded=%d ListTransition=%d ListDisplay=%d BackVisible=%d ButtonVisibility=%d ButtonEnabled=%d ButtonBound=%d CameraMoving=%d MovementDisabled=%d RotationDisabled=%d SMSerror=%s"),
                Step, int32(Manager->GetTransitionState()), *Manager->GetCurrentSceneTag().ToString(), *Manager->GetPendingSceneTag().ToString(),
                BoolValue(Scene, TEXT("UiFinish")), BoolValue(Scene, TEXT("CameraFinish")), *GetNameSafe(CurrentRoom),
                CurrentRoom && CurrentRoom->IsSceneUILoaded(), CurrentRoom && CurrentRoom->bListTransitioning,
                CurrentRoom ? int32(CurrentRoom->CurrentListDisplay) : -1, Shell->bBackButtonVisible,
                Button ? int32(Button->GetVisibility()) : -1, Button && Button->GetIsEnabled(), Button && Button->OnClicked.IsBound(),
                Camera.IsValid() && Camera->IsMovingToCameraState(),
                UFCS_FreeCameraBlueprintLibrary::IsFreeCameraMovementDisabled(World.Get()),
                UFCS_FreeCameraBlueprintLibrary::IsFreeCameraRotationDisabled(World.Get()), *Manager->GetLastError().ToString());
        }
    };

    class FReturnFlow final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        const bool bWarehouse;
        const bool bPreloaded;
        const bool bEarlyEquipmentSwitch;
        bool bSwitchedEquipmentDuringEntry = false;
        int32 CompletedVisits = 0;
        TStrongObjectPtr<UPersonnelPreparationRoomWidget> PreviousVisitRoom;
        int32 Step = 0;
        double Started = FPlatformTime::Seconds();
        double LastReport = 0.;
        void Next() { ++Step; Started = FPlatformTime::Seconds(); LastReport = 0.; }

    public:
        FReturnFlow(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest, bool InWarehouse, bool InPreloaded, bool InEarlyEquipmentSwitch = false)
            : Fixture(InFixture), Test(InTest), bWarehouse(InWarehouse), bPreloaded(InPreloaded), bEarlyEquipmentSwitch(InEarlyEquipmentSwitch) {}

        bool Update() override
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - LastReport > 5.)
            {
                Test->AddInfo(Fixture->Diagnostics(Step));
                LastReport = Now;
            }
            if (Now - Started > 30.)
            {
                Test->AddError(TEXT("Personnel return-to-base flow timed out: ") + Fixture->Diagnostics(Step));
                return true;
            }
            auto* Manager = Fixture->Manager.Get();
            switch (Step)
            {
            case 0:
                if (bPreloaded)
                {
                    Fixture->Room.Reset(Cast<UPersonnelPreparationRoomWidget>(Fixture->Shell->CreateSceneUIByTag(Fixture->PersonnelTag)));
                    if (!Test->TestNotNull(TEXT("Preload real personnel room before scene entry"), Fixture->Room.Get())) return true;
                }
                Next();
                return false;
            case 1:
                if (bPreloaded && !Fixture->Room->IsSceneUILoaded()) return false;
                if (!Test->TestTrue(TEXT("Global manager accepts personnel scene entry"), Manager->SwitchScene(Fixture->PersonnelTag))) return true;
                if (Manager->GetTransitionState() == ESMS_SceneTransitionState::Entering)
                    Test->TestFalse(TEXT("Return button is hidden while scene entry is pending"), Fixture->Shell->bBackButtonVisible);
                Next();
                return false;
            case 2:
                if (bEarlyEquipmentSwitch && !bSwitchedEquipmentDuringEntry)
                {
                    auto* LoadingRoom = Cast<UPersonnelPreparationRoomWidget>(Fixture->Shell->GetCurrentSceneUI());
                    if (!LoadingRoom) return false;
                    if (!Test->TestFalse(TEXT("Early equipment switch occurs before the entrance animation completes"), LoadingRoom->IsSceneUILoaded())) return true;
                    Fixture->Room.Reset(LoadingRoom);
                    bSwitchedEquipmentDuringEntry = true;
                    Test->AddInfo(TEXT("Before changing page during entrance: ") + Fixture->Diagnostics(Step));
                    LoadingRoom->SetDetailPage(EPersonnelDetailPage::Equipment);
                    Test->AddInfo(TEXT("After changing page during entrance: ") + Fixture->Diagnostics(Step));
                }
                if (!Fixture->IsIdleAt(Fixture->PersonnelTag)) return false;
                if (bPreloaded) Test->TestTrue(TEXT("Scene can reuse the already-loaded UI"), Fixture->Shell->GetCurrentSceneUI() == Fixture->Room.Get());
                Fixture->Room.Reset(Cast<UPersonnelPreparationRoomWidget>(Fixture->Shell->GetCurrentSceneUI()));
                if (!Test->TestNotNull(TEXT("Personnel entry mounted real UI"), Fixture->Room.Get())) return true;
                if (PreviousVisitRoom.IsValid())
                    Test->TestTrue(TEXT("Re-entering personnel creates a fresh room instead of reviving the detached instance"), Fixture->Room.Get() != PreviousVisitRoom.Get());
                Test->TestTrue(TEXT("Entry completed UI animation before allowing navigation"), Fixture->Room->IsSceneUILoaded());
                Test->TestTrue(TEXT("Return button is offered after entry"), Fixture->Shell->bBackButtonVisible);
                if (bWarehouse) Fixture->Room->SetDetailPage(EPersonnelDetailPage::Equipment);
                Next();
                return false;
            case 3:
            {
                if (Fixture->Room->bListTransitioning) return false;
                if (bWarehouse) Test->TestTrue(TEXT("Equipment page exposes warehouse before exit"), Fixture->Room->CurrentListDisplay == EPersonnelListDisplay::Warehouse);
                UBasicButtonWidget* Button = Fixture->BackButton();
                if (!Test->TestNotNull(TEXT("Return button remains mounted"), Button)) return true;
                Test->TestTrue(TEXT("Return button is enabled"), Button->GetIsEnabled());
                Test->TestTrue(TEXT("Return button is visible"), Button->GetVisibility() == ESlateVisibility::Visible);
                Test->TestTrue(TEXT("Return button is bound to the base shell"), Button->OnClicked.GetAllObjects().Contains(Fixture->Shell.Get()));
                // Go through the real button delegate -> native shell handler -> Blueprint event
                // -> global SMS library. Calling SwitchScene directly would miss the broken route.
                Button->OnClicked.Broadcast();
                Test->AddInfo(TEXT("Immediately after actual button delegate: ") + Fixture->Diagnostics(Step));
                Test->TestFalse(TEXT("Return button hides immediately when its exit action starts"), Fixture->Shell->bBackButtonVisible);
                if (!Test->TestTrue(TEXT("Actual return-button action starts leaving the personnel scene"),
                    Manager->GetTransitionState() == ESMS_SceneTransitionState::Leaving
                    && Manager->GetPendingSceneTag() == Fixture->OverviewTag)) return true;
                Next();
                return false;
            }
            case 4:
                if (!Fixture->IsIdleAt(Fixture->OverviewTag)) return false;
                Test->TestNull(TEXT("Return-to-base removes the personnel UI"), Fixture->Shell->GetCurrentSceneUI());
                Test->TestNull(TEXT("Unloaded personnel room releases its host"), Fixture->Room->BaseUI.Get());
                Test->TestFalse(TEXT("Base overview hides the return button"), Fixture->Shell->bBackButtonVisible);
                Test->AddInfo(TEXT("PERSONNEL_RETURN_FLOW_OK: ") + Fixture->Diagnostics(Step));
                ++CompletedVisits;
                if (!bWarehouse && !bPreloaded && !bEarlyEquipmentSwitch && CompletedVisits == 1)
                {
                    // Exercise the same live shell and global manager across a complete second
                    // visit. Old animation bindings and detached UI must not stall re-entry.
                    PreviousVisitRoom.Reset(Fixture->Room.Get());
                    if (!Test->TestTrue(TEXT("Re-enter personnel from base overview"), Manager->SwitchScene(Fixture->PersonnelTag))) return true;
                    if (Manager->GetTransitionState() == ESMS_SceneTransitionState::Entering)
                        Test->TestFalse(TEXT("Return button stays hidden while re-entry is pending"), Fixture->Shell->bBackButtonVisible);
                    Step = 2;
                    Started = Now;
                    LastReport = 0.;
                    return false;
                }
                return true;
            }
            return false;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonnelProfileReturnTest, "SilverChoir.BaseUI.PersonnelReturnFlow.Profile",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPersonnelProfileReturnTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<PersonnelReturnFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(PersonnelReturnFlowTests::FReturnFlow(Fixture, this, false, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonnelWarehouseReturnTest, "SilverChoir.BaseUI.PersonnelReturnFlow.Warehouse",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPersonnelWarehouseReturnTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<PersonnelReturnFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(PersonnelReturnFlowTests::FReturnFlow(Fixture, this, true, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonnelPreloadedReturnTest, "SilverChoir.BaseUI.PersonnelReturnFlow.PreloadedUI",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPersonnelPreloadedReturnTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<PersonnelReturnFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(PersonnelReturnFlowTests::FReturnFlow(Fixture, this, false, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonnelEarlyEquipmentReturnTest, "SilverChoir.BaseUI.PersonnelReturnFlow.EarlyEquipmentSwitch",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPersonnelEarlyEquipmentReturnTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<PersonnelReturnFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(PersonnelReturnFlowTests::FReturnFlow(Fixture, this, true, false, true));
    return true;
}
#endif
