#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Components/PanelWidget.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "GTS_TimeManager.h"
#include "GTS_TimeSubsystem.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "SMS_SceneBase.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "MTS_MapTransitionSubsystem.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SMS_SceneLibrary.h"
#include "SMS_SceneManager.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/StrongObjectPtr.h"

namespace OperationsBlueprintLifecycleTests
{
constexpr const TCHAR* RoomClassPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom.WBP_OperationsCommandRoom_C");
constexpr const TCHAR* OverviewClassPath = TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene.BP_基地全景_Scene_C");
constexpr const TCHAR* FixtureMount = TEXT("/OperationsTest/");
constexpr const TCHAR* FixtureClassPath = TEXT("/OperationsTest/WBP_OperationsCommandRoomDelayed.WBP_OperationsCommandRoomDelayed_C");

bool IsBlueprintImplementation(UClass* Class, FName Name)
{
    UFunction* Function = Class ? Class->FindFunctionByName(Name) : nullptr;
    if (!Function || Function->HasAnyFunctionFlags(FUNC_Native)
        || Function->GetOuterUClass() != Class) return false;
    bool bHasScript = Function->Script.Num() > 0;
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
    bHasScript |= Function->EventGraphFunction != nullptr;
#endif
    return bHasScript;
}

class FDelayedBlueprintCompletion final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UGameInstance> Instance;
    TWeakObjectPtr<AGameMainMapPlayerController> Controller;
    TWeakObjectPtr<USMS_SceneManager> Scenes;
    TWeakObjectPtr<AFCS_FreeCameraPawn> Camera;
    TWeakObjectPtr<UGTS_TimeSubsystem> Time;
    TStrongObjectPtr<UOperationsCommandRoomWidget> Room;
    TStrongObjectPtr<USMS_SceneBase> Scene;
    TWeakObjectPtr<UBaseMapWidget> FixtureHost;
    TStrongObjectPtr<UClass> OriginalWidgetClass;
    TStrongObjectPtr<UClass> DelayedWidgetClass;
    FString FixtureDirectory;
    bool bFixtureMounted = false;
    bool bClassMappingReplaced = false;
    FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.OperationsCommandRoom"));
    FGameplayTag OverviewTag;
    const double Started = FPlatformTime::Seconds();
    double StageStarted = Started;
    uint64 CameraStoppedFrame = 0;
    int32 InitialTimeBindings = 0;
    int32 Stage = 0;

    UBaseMapWidget* Shell() const { return Controller.IsValid() ? Controller->BaseWidget.Get() : nullptr; }
    void RestoreFixture()
    {
        if (bClassMappingReplaced && FixtureHost.IsValid())
            FixtureHost->SceneUIClasses.Add(RoomTag, OriginalWidgetClass.Get());
        bClassMappingReplaced = false;
        if (bFixtureMounted) FPackageName::UnRegisterMountPoint(FixtureMount, FixtureDirectory);
        bFixtureMounted = false;
    }
    void LogState(const TCHAR* Reason) const
    {
        UBaseSceneWidget* Mounted = Shell() ? Shell()->GetCurrentSceneUI() : nullptr;
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_BP_LIFECYCLE_STATE reason=%s stage=%d elapsed=%.2f state=%d current=%s pending=%s ui=%s loaded=%d camera=%s moving=%d"),
            Reason, Stage, FPlatformTime::Seconds() - StageStarted,
            Scenes.IsValid() ? int32(Scenes->GetTransitionState()) : -1,
            Scenes.IsValid() ? *Scenes->GetCurrentSceneTag().ToString() : TEXT("none"),
            Scenes.IsValid() ? *Scenes->GetPendingSceneTag().ToString() : TEXT("none"),
            *GetNameSafe(Mounted), Mounted && Mounted->IsSceneUILoaded(),
            *GetNameSafe(Camera.Get()), Camera.IsValid() && Camera->IsMovingToCameraState());
    }
    void NextStage(int32 Next, const TCHAR* Reason)
    {
        Stage = Next;
        StageStarted = FPlatformTime::Seconds();
        LogState(Reason);
    }
    bool IdleAt(FGameplayTag Tag) const
    {
        return Scenes.IsValid() && Scenes->GetTransitionState() == ESMS_SceneTransitionState::Idle
            && Scenes->GetCurrentSceneTag() == Tag;
    }
    bool CameraHasStoppedForTwoFrames()
    {
        if (!Camera.IsValid() || Camera->IsMovingToCameraState())
        {
            CameraStoppedFrame = 0;
            return false;
        }
        if (CameraStoppedFrame == 0) CameraStoppedFrame = GFrameCounter;
        return GFrameCounter >= CameraStoppedFrame + 2;
    }
    bool CheckHeldUI(ESMS_SceneTransitionState Expected, const TCHAR* Phase)
    {
        bool bOK = Test->TestTrue(FString(Phase) + TEXT(": manager still awaits UI completion"),
            Scenes->GetTransitionState() == Expected);
        bOK &= Test->TestTrue(FString(Phase) + TEXT(": old/current UI stays mounted"),
            Shell()->GetCurrentSceneUI() == Room.Get() && Room->GetParent() != nullptr);
        bOK &= Test->TestTrue(FString(Phase) + TEXT(": UI keeps its host until completion"), Room->BaseUI == Shell());
        bOK &= Test->TestFalse(FString(Phase) + TEXT(": return remains unavailable"), Shell()->bBackButtonVisible);
        bOK &= Test->TestFalse(FString(Phase) + TEXT(": C++ has not auto-completed UI loading"), Room->IsSceneUILoaded());
        return bOK;
    }

public:
    FDelayedBlueprintCompletion(FAutomationTestBase* InTest, UGameInstance* InInstance)
        : Test(InTest), Instance(InInstance) {}
    ~FDelayedBlueprintCompletion() override { RestoreFixture(); }

    bool Update() override
    {
        if (!Instance.IsValid())
        {
            Test->AddError(TEXT("Blueprint lifecycle regression lost the real NewGame instance."));
            return true;
        }
        if (FPlatformTime::Seconds() - Started > 180.
            || (Stage != 0 && FPlatformTime::Seconds() - StageStarted > 10.))
        {
            LogState(TEXT("timeout"));
            Test->AddError(FString::Printf(TEXT("Delayed Blueprint lifecycle timed out at stage %d; state=%d error=%s"),
                Stage, Scenes.IsValid() ? int32(Scenes->GetTransitionState()) : -1,
                Scenes.IsValid() ? *Scenes->GetLastError().ToString() : TEXT("no scene manager")));
            return true;
        }
        UWorld* World = Instance->GetWorld();
        switch (Stage)
        {
        case 0:
        {
            auto* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
            auto* Transition = Instance->GetSubsystem<UMTS_MapTransitionSubsystem>();
            if (!State || !State->IsBaseMap() || State->ActiveMapID != TEXT("Base")
                || !Transition || Transition->IsTransitionInProgress()) return false;
            Controller = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
            Scenes = USMS_SceneLibrary::GetSceneManager(World);
            Time = Instance->GetSubsystem<UGTS_TimeSubsystem>();
            Camera = UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World);
            if (!Test->TestNotNull(TEXT("Real base shell exists"), Shell())
                || !Test->TestNotNull(TEXT("Real scene manager exists"), Scenes.Get())
                || !Test->TestNotNull(TEXT("Real free camera exists"), Camera.Get())
                || !Test->TestNotNull(TEXT("Real time subsystem exists"), Time.Get())) return true;
            if (Scenes->GetTransitionState() != ESMS_SceneTransitionState::Idle) return false;
            UClass* RoomClass = LoadClass<UOperationsCommandRoomWidget>(nullptr, RoomClassPath);
            UClass* OverviewClass = LoadClass<USMS_SceneBase>(nullptr, OverviewClassPath);
            if (!Test->TestNotNull(TEXT("Saved operations Widget Blueprint exists"), RoomClass)
                || !Test->TestNotNull(TEXT("Saved overview Scene Blueprint exists"), OverviewClass)) return true;
            if (!Test->TestTrue(TEXT("LoadSceneUI is implemented by the saved Widget Blueprint"),
                    IsBlueprintImplementation(RoomClass, GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, LoadSceneUI)))
                || !Test->TestTrue(TEXT("UnloadSceneUI is implemented by the saved Widget Blueprint"),
                    IsBlueprintImplementation(RoomClass, GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, UnloadSceneUI)))) return true;
            OverviewTag = OverviewClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            UE_LOG(LogTemp, Display, TEXT("OPERATIONS_BP_LIFECYCLE_TAGS expected=%s widgetDefault=%s overview=%s"),
                *RoomTag.ToString(), *RoomClass->GetDefaultObject<UOperationsCommandRoomWidget>()->SceneTag.ToString(), *OverviewTag.ToString());
            LogState(TEXT("base ready before fixture normalization"));
            // Always begin from a completed overview. A previously mounted room
            // would otherwise make SwitchScene a no-op and never invoke a new load.
            if (!IdleAt(OverviewTag))
            {
                if (!Test->TestTrue(TEXT("Fixture returns to overview through the real scene manager"),
                    Scenes->SwitchScene(OverviewTag))) return true;
            }
            else if (Shell()->GetCurrentSceneUI())
            {
                Shell()->UnloadCurrentSceneUI();
            }
            NextStage(6, TEXT("await clean overview before installing delayed Blueprint fixture"));
            return false;
        }
        case 6:
        {
            if (!IdleAt(OverviewTag) || Shell()->GetCurrentSceneUI()) return false;
            UClass* RoomClass = LoadClass<UOperationsCommandRoomWidget>(nullptr, RoomClassPath);
            if (!Test->TestNotNull(TEXT("Saved operations class remains available"), RoomClass)) return true;
            const TSubclassOf<UBaseSceneWidget>* RegisteredClass = Shell()->SceneUIClasses.Find(RoomTag);
            if (!Test->TestTrue(TEXT("Real host registers the saved operations Blueprint before fixture substitution"),
                RegisteredClass && RegisteredClass->Get() == RoomClass)) return true;
            if (!Test->TestFalse(TEXT("Fixture mount is not owned by another test"), FPackageName::MountPointExists(FixtureMount))) return true;
            FixtureDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OperationsCommandRoomBlueprintEvents/Fixtures/"));
            FPackageName::RegisterMountPoint(FixtureMount, FixtureDirectory);
            bFixtureMounted = true;
            DelayedWidgetClass.Reset(LoadClass<UOperationsCommandRoomWidget>(nullptr, FixtureClassPath));
            if (!Test->TestNotNull(TEXT("CreateLifecycleFixture produced the delayed Blueprint copy in Saved/OperationsCommandRoomBlueprintEvents/Fixtures"),
                DelayedWidgetClass.Get())) return true;
            if (!Test->TestTrue(TEXT("Delayed fixture retains the real Blueprint LoadSceneUI graph"),
                    IsBlueprintImplementation(DelayedWidgetClass.Get(), GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, LoadSceneUI)))
                || !Test->TestTrue(TEXT("Delayed fixture retains the real Blueprint UnloadSceneUI graph"),
                    IsBlueprintImplementation(DelayedWidgetClass.Get(), GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, UnloadSceneUI)))) return true;
            // The fixture is the saved widget's graph/Designer copy with only its
            // two completion execution inputs disconnected. Production assets and
            // the real scene manager/scene Blueprint are never modified here.
            FixtureHost = Shell();
            OriginalWidgetClass.Reset(RegisteredClass->Get());
            FixtureHost->SceneUIClasses.Add(RoomTag, DelayedWidgetClass.Get());
            bClassMappingReplaced = true;
            InitialTimeBindings = Time->GetRegisteredTimeTextBlockCount();
            if (!Test->TestTrue(TEXT("Real manager accepts operations entry with delayed Blueprint load"),
                Scenes->SwitchScene(RoomTag))) return true;
            NextStage(1, TEXT("operations entry requested with load completion withheld"));
            return false;
        }
        case 1:
        {
            if (!Shell()->GetCurrentSceneUI()) return false;
            Room.Reset(Cast<UOperationsCommandRoomWidget>(Shell()->GetCurrentSceneUI()));
            Scene.Reset(Scenes->GetCurrentScene());
            if (!Test->TestNotNull(TEXT("Real host mounted the delayed Blueprint fixture"), Room.Get())
                || !Test->TestNotNull(TEXT("Actual operations scene remains current during entry"), Scene.Get())) return true;
            if (!Test->TestTrue(TEXT("Mounted widget uses the generated delayed Blueprint copy"),
                Room->GetClass() == DelayedWidgetClass.Get())) return true;
            if (!Test->TestTrue(TEXT("Scene entry executes its saved Blueprint implementation"),
                    IsBlueprintImplementation(Scene->GetClass(), GET_FUNCTION_NAME_CHECKED(USMS_SceneBase, EnterScene)))
                || !Test->TestTrue(TEXT("Scene exit executes its saved Blueprint implementation"),
                    IsBlueprintImplementation(Scene->GetClass(), GET_FUNCTION_NAME_CHECKED(USMS_SceneBase, LeaveScene)))) return true;
            Test->TestEqual(TEXT("Delayed load still prepares real date/time subscriptions"),
                Time->GetRegisteredTimeTextBlockCount(), InitialTimeBindings + 2);
            Test->TestTrue(TEXT("Scene subscribes to Blueprint-driven load completion"),
                Room->OnLoadCompleted.GetAllObjects().Contains(Scene.Get()));
            Test->TestTrue(TEXT("Scene subscribes to Blueprint-driven unload completion"),
                Room->OnUnloadCompleted.GetAllObjects().Contains(Scene.Get()));
            if (!CheckHeldUI(ESMS_SceneTransitionState::Entering, TEXT("Load withheld"))) return true;
            CameraStoppedFrame = 0;
            NextStage(2, TEXT("Blueprint load completion withheld; await camera entry"));
            return false;
        }
        case 2:
            if (!CameraHasStoppedForTwoFrames()) return false;
            if (!CheckHeldUI(ESMS_SceneTransitionState::Entering, TEXT("Camera entered, load still withheld"))) return true;
            Room->NotifyUnloadCompleted();
            Test->TestTrue(TEXT("Premature unload notification cannot complete entry"),
                Scenes->GetTransitionState() == ESMS_SceneTransitionState::Entering && Shell()->GetCurrentSceneUI() == Room.Get());
            Room->NotifyLoadCompleted();
            Room->NotifyLoadCompleted();
            NextStage(3, TEXT("explicit load completion delivered"));
            return false;
        case 3:
        {
            if (!IdleAt(RoomTag)) return false;
            Test->TestTrue(TEXT("Explicit Blueprint completion unlocks entered UI"), Room->IsSceneUILoaded());
            Test->TestTrue(TEXT("Return is shown only after camera and UI finish"), Shell()->bBackButtonVisible);
            auto* Back = Cast<UBasicButtonWidget>(Shell()->GetWidgetFromName(TEXT("BackButton")));
            if (!Test->TestNotNull(TEXT("Real shell return button exists"), Back)) return true;
            Back->OnClicked.Broadcast();
            if (!Test->TestTrue(TEXT("Real return starts the asynchronous Blueprint leave"),
                Scenes->GetTransitionState() == ESMS_SceneTransitionState::Leaving
                && Scenes->GetPendingSceneTag() == OverviewTag)) return true;
            CameraStoppedFrame = 0;
            NextStage(4, TEXT("real return requested; await camera return while Blueprint unload is incomplete"));
            return false;
        }
        case 4:
        {
            if (!CameraHasStoppedForTwoFrames()) return false;
            if (!CheckHeldUI(ESMS_SceneTransitionState::Leaving, TEXT("Camera returned, unload still withheld"))) return true;
            const auto* ExitProperty = FindFProperty<FStructProperty>(Scene->GetClass(), TEXT("ExitOverheadCameraState"));
            const auto* EntryProperty = FindFProperty<FStructProperty>(Scene->GetClass(), TEXT("OverheadCameraState"));
            if (!Test->TestNotNull(TEXT("Exit has a separate Blueprint camera state"), ExitProperty) || !Test->TestNotNull(TEXT("Entry overhead is Blueprint editable"),EntryProperty)) return true;
            const auto& ExitState = *ExitProperty->ContainerPtrToValuePtr<FFCS_CameraState>(Scene.Get());
            const auto& EntryState = *EntryProperty->ContainerPtrToValuePtr<FFCS_CameraState>(Scene.Get());
            const auto Actual = Camera->GetCurrentCameraState();
            Test->TestTrue(TEXT("Leave stops above this room, without restoring prior position"),Actual.Location.Equals(ExitState.Location,1));
            Test->TestTrue(TEXT("Leave reaches configured overhead arm"),FMath::IsNearlyEqual(Actual.TargetArmLength,ExitState.TargetArmLength,1.f));
            Test->TestTrue(TEXT("Separate exit state has faster zoom"),ExitState.ZoomSpeed>EntryState.ZoomSpeed);
            Test->TestNull(TEXT("Obsolete return position cache removed"),FindFProperty<FProperty>(Scene->GetClass(),TEXT("ReturnCameraState")));
            Test->TestEqual(TEXT("Unload prepare releases date/time subscriptions before animation completion"),
                Time->GetRegisteredTimeTextBlockCount(), InitialTimeBindings);
            Room->NotifyLoadCompleted();
            Test->TestFalse(TEXT("Late load completion is ignored during unload"), Room->IsSceneUILoaded());
            Test->TestTrue(TEXT("Late load completion cannot finish scene exit"),
                Scenes->GetTransitionState() == ESMS_SceneTransitionState::Leaving);
            Room->NotifyUnloadCompleted();
            Test->TestTrue(TEXT("Scene completion is deferred beyond the UI unload call stack"),
                Scenes->GetTransitionState() == ESMS_SceneTransitionState::Leaving
                && Scenes->GetCurrentScene() == Scene.Get());
            Test->TestNull(TEXT("Host releases old UI before deferred scene continuation"), Shell()->GetCurrentSceneUI());
            Test->TestNull(TEXT("Finished unload releases the widget host"), Room->BaseUI.Get());
            Test->TestNull(TEXT("Finished unload detaches the widget"), Room->GetParent());
            Room->NotifyUnloadCompleted();
            Test->TestTrue(TEXT("Duplicate unload notification cannot bypass deferred completion"),
                Scenes->GetTransitionState() == ESMS_SceneTransitionState::Leaving);
            NextStage(5, TEXT("explicit unload completion delivered; await deferred overview"));
            return false;
        }
        case 5:
            if (!IdleAt(OverviewTag)) return false;
            Test->TestNull(TEXT("Overview contains no stale operations UI"), Shell()->GetCurrentSceneUI());
            Test->TestFalse(TEXT("Finished scene releases its widget load delegate"),
                Room->OnLoadCompleted.GetAllObjects().Contains(Scene.Get()));
            Test->TestFalse(TEXT("Finished scene releases its widget unload delegate"),
                Room->OnUnloadCompleted.GetAllObjects().Contains(Scene.Get()));
            Test->TestEqual(TEXT("No time text subscriptions leaked"), Time->GetRegisteredTimeTextBlockCount(), InitialTimeBindings);
            Test->TestFalse(TEXT("Detached scene cannot complete a newer scene entry"), Scene->NotifyEnterCompleted());
            RestoreFixture();
            Test->TestTrue(TEXT("Real host restores its original operations widget class"),
                Shell()->SceneUIClasses.FindRef(RoomTag).Get() == OriginalWidgetClass.Get());
            Test->TestFalse(TEXT("Temporary fixture mount is released"), FPackageName::MountPointExists(FixtureMount));
            Test->TestTrue(TEXT("Original Load Blueprint implementation remains unchanged"),
                IsBlueprintImplementation(OriginalWidgetClass.Get(), GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, LoadSceneUI)));
            Test->TestTrue(TEXT("Original Unload Blueprint implementation remains unchanged"),
                IsBlueprintImplementation(OriginalWidgetClass.Get(), GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, UnloadSceneUI)));
            Test->AddInfo(TEXT("OPERATIONS_BLUEPRINT_LIFECYCLE_OK real NewGame/scene Blueprint, copied widget with deferred load/unload, camera gates, next-tick exit and fixture restoration"));
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOperationsCommandRoomBlueprintLifecycleTest,
    "SilverChoir.BaseUI.OperationsCommandRoom.BlueprintLifecycle",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOperationsCommandRoomBlueprintLifecycleTest::RunTest(const FString& Parameters)
{
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType != EWorldType::Game || !Context.World()) continue;
        auto* Menu = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController());
        if (!Menu) continue;
        UGameInstance* Instance = Context.World()->GetGameInstance();
        Menu->HandleMenuAction(EMainMenuAction::NewGame);
        ADD_LATENT_AUTOMATION_COMMAND(OperationsBlueprintLifecycleTests::FDelayedBlueprintCompletion(this, Instance));
        return true;
    }
    AddError(TEXT("Run OperationsCommandRoom.BlueprintLifecycle alone from MainMenu -game, after OperationsCommandRoomWidgetEvents -CreateLifecycleFixture."));
    return false;
}
#endif
