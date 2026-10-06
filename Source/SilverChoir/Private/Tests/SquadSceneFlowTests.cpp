#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_FreeCameraSubsystem.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "SMS_SceneBase.h"
#include "SMS_SceneManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace SquadSceneFlowTests
{
    const TCHAR* SquadScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene.BP_小队会议室_Scene_C");
    const TCHAR* PersonnelScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_人员整备室_Scene.BP_人员整备室_Scene_C");
    const TCHAR* OverviewScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene.BP_基地全景_Scene_C");
    const TCHAR* ShellPath = TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C");
    const FName TestTile(TEXT("SC_SQUAD_SCENE_FLOW_TEST"));

    template<typename TProperty>
    TProperty* Property(const UObject* Object, const TCHAR* Name)
    {
        return Object ? FindFProperty<TProperty>(Object->GetClass(), Name) : nullptr;
    }

    int32 Stage(const UObject* Object)
    {
        const auto* Value = Property<FIntProperty>(Object, TEXT("FlowStage"));
        return Value ? Value->GetPropertyValue_InContainer(Object) : INDEX_NONE;
    }

    bool CameraFinished(const UObject* Object)
    {
        const auto* Value = Property<FBoolProperty>(Object, TEXT("CameraFinish"));
        return Value && Value->GetPropertyValue_InContainer(Object);
    }

    UObject* ObjectValue(const UObject* Object, const TCHAR* Name)
    {
        const auto* Value = Property<FObjectPropertyBase>(Object, Name);
        return Value ? Value->GetObjectPropertyValue_InContainer(Object) : nullptr;
    }

    bool HasFlowError(const UObject* Object)
    {
        if (const auto* Value = Property<FStrProperty>(Object, TEXT("LastFlowError")))
            return !Value->GetPropertyValue_InContainer(Object).IsEmpty();
        if (const auto* Value = Property<FTextProperty>(Object, TEXT("LastFlowError")))
            return !Value->GetPropertyValue_InContainer(Object).IsEmpty();
        return false;
    }

    struct FCameraDefaultOverride
    {
        UObject* Defaults = nullptr;
        FStructProperty* Field = nullptr;
        FFCS_CameraState Saved;

        bool Set(UObject* InDefaults, const TCHAR* Name, const FFCS_CameraState& Value)
        {
            Defaults = InDefaults;
            Field = Property<FStructProperty>(Defaults, Name);
            if (!Field || Field->Struct != FFCS_CameraState::StaticStruct())
            {
                Field = nullptr;
                return false;
            }
            auto* State = Field->ContainerPtrToValuePtr<FFCS_CameraState>(Defaults);
            Saved = *State;
            *State = Value;
            return true;
        }
        void Restore() const
        {
            if (Defaults && Field)
                *Field->ContainerPtrToValuePtr<FFCS_CameraState>(Defaults) = Saved;
        }
    };

    struct FFixture
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameMainMapPlayerController> Controller;
        TWeakObjectPtr<AFCS_FreeCameraPawn> Camera;
        TWeakObjectPtr<UFCS_FreeCameraSubsystem> CameraSubsystem;
        TStrongObjectPtr<UBaseMapWidget> PreviousShell;
        TStrongObjectPtr<UBaseMapWidget> Shell;
        TStrongObjectPtr<USMS_SceneManager> Manager;
        TStrongObjectPtr<USMS_SceneBase> OldScene;
        TStrongObjectPtr<USquadMeetingRoomWidget> OldRoom;
        FCameraDefaultOverride Overhead;
        FCameraDefaultOverride Interior;
        FCameraDefaultOverride Overview;
        UObject* SquadDefaults = nullptr;
        FNameProperty* TileProperty = nullptr;
        FName SavedTile;
        FGameplayTag SquadTag;
        FGameplayTag PersonnelTag;
        FGameplayTag OverviewTag;

        bool Initialize(FAutomationTestBase& Test)
        {
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
            if (!Test.TestNotNull(TEXT("Scene flow uses a real game world"), World.Get())) return false;
            Controller = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
            CameraSubsystem = World->GetSubsystem<UFCS_FreeCameraSubsystem>();
            Camera = UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World.Get());
            if (!Test.TestNotNull(TEXT("GameMain controller exists"), Controller.Get())
                || !Test.TestNotNull(TEXT("Registered free camera exists"), Camera.Get())
                || !Test.TestNotNull(TEXT("Camera subsystem exists"), CameraSubsystem.Get())) return false;

            UClass* SquadClass = LoadClass<USMS_SceneBase>(nullptr, SquadScenePath);
            UClass* PersonnelClass = LoadClass<USMS_SceneBase>(nullptr, PersonnelScenePath);
            UClass* OverviewClass = LoadClass<USMS_SceneBase>(nullptr, OverviewScenePath);
            UClass* ShellClass = LoadClass<UBaseMapWidget>(nullptr, ShellPath);
            if (!Test.TestNotNull(TEXT("Saved squad scene Blueprint exists"), SquadClass)
                || !Test.TestNotNull(TEXT("Reference personnel scene Blueprint exists"), PersonnelClass)
                || !Test.TestNotNull(TEXT("Base overview scene Blueprint exists"), OverviewClass)
                || !Test.TestNotNull(TEXT("Base shell Blueprint exists"), ShellClass)) return false;

            FFCS_CameraState NearState;
            UFCS_FreeCameraBlueprintLibrary::GetFreeCameraState(World.Get(), NearState);
            NearState.MoveSpeed = NearState.RotationSpeed = NearState.ZoomSpeed = 1000000.f;
            SquadDefaults = SquadClass->GetDefaultObject();
            if (!Test.TestTrue(TEXT("Squad overhead target is editable"), Overhead.Set(SquadDefaults, TEXT("OverheadCameraState"), NearState))
                || !Test.TestTrue(TEXT("Squad interior target is editable"), Interior.Set(SquadDefaults, TEXT("InteriorCameraState"), NearState))) return false;
            // Only modify in-memory CDO values; never dirty or save these Blueprint packages.
            Overview.Set(OverviewClass->GetDefaultObject(), TEXT("移动到区域上方"), NearState);
            TileProperty = Property<FNameProperty>(SquadDefaults, TEXT("CurrentTileId"));
            if (!Test.TestNotNull(TEXT("Squad scene exposes its configured tile"), TileProperty)) return false;
            SavedTile = TileProperty->GetPropertyValue_InContainer(SquadDefaults);
            TileProperty->SetPropertyValue_InContainer(SquadDefaults, TestTile);
            SquadTag = SquadClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            PersonnelTag = PersonnelClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            OverviewTag = OverviewClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;

            PreviousShell.Reset(Controller->BaseWidget);
            Shell.Reset(CreateWidget<UBaseMapWidget>(Controller.Get(), ShellClass));
            if (!Test.TestNotNull(TEXT("Independent test shell is created"), Shell.Get())) return false;
            Shell->AddToViewport(10000);
            Controller->BaseWidget = Shell.Get();
            Manager.Reset(NewObject<USMS_SceneManager>(World.Get()));
            Manager->SceneClasses = {SquadClass, PersonnelClass, OverviewClass};
            return Test.TestTrue(TEXT("Isolated scene manager accepts real scene classes"), Manager->InitializeManager());
        }

        ~FFixture()
        {
            if (Camera.IsValid()) Camera->CancelCameraStateMove();
            if (CameraSubsystem.IsValid() && Camera.IsValid()) CameraSubsystem->RegisterFreeCamera(Camera.Get());
            if (Manager.IsValid()) Manager->ShutdownManager();
            if (Controller.IsValid()) Controller->BaseWidget = PreviousShell.Get();
            if (Shell.IsValid()) Shell->RemoveFromParent();
            Overhead.Restore();
            Interior.Restore();
            Overview.Restore();
            if (SquadDefaults && TileProperty) TileProperty->SetPropertyValue_InContainer(SquadDefaults, SavedTile);
        }

        bool IsIdleAt(FGameplayTag Tag) const
        {
            return Manager->GetTransitionState() == ESMS_SceneTransitionState::Idle
                && Manager->GetCurrentSceneTag() == Tag;
        }

        bool CheckEntered(FAutomationTestBase& Test)
        {
            auto* Scene = Manager->GetCurrentScene();
            auto* Room = Cast<USquadMeetingRoomWidget>(Shell->GetCurrentSceneUI());
            Test.TestEqual(TEXT("Entry reaches active flow stage"), Stage(Scene), 2);
            Test.TestTrue(TEXT("Back button is available after both entry branches complete"), Shell->bBackButtonVisible);
            if (!Test.TestNotNull(TEXT("Scene mounted real meeting-room widget"), Room)) return false;
            Test.TestEqual(TEXT("Scene passes configured tile to the squad list"), Room->CurrentTileId, TestTile);
            Test.TestFalse(TEXT("UI entrance animation has completed before SMS becomes idle"), Room->bTransitioning);
            Test.TestTrue(TEXT("Scene keeps its own room reference"), ObjectValue(Scene, TEXT("RoomUI")) == Room);
            Test.TestTrue(TEXT("Scene keeps its own shell reference"), ObjectValue(Scene, TEXT("BaseUI")) == Shell.Get());
            Test.TestTrue(TEXT("Load callback is bound to this scene instance"), Room->OnLoadCompleted.GetAllObjects().Contains(Scene));
            Test.TestTrue(TEXT("Unload callback is bound to this scene instance"), Room->OnUnloadCompleted.GetAllObjects().Contains(Scene));
            OldScene.Reset(Scene);
            OldRoom.Reset(Room);
            return true;
        }

        void CheckDetached(FAutomationTestBase& Test)
        {
            Test.TestEqual(TEXT("Departed scene reaches finished flow stage"), Stage(OldScene.Get()), 4);
            Test.TestNull(TEXT("Departed scene releases its room"), ObjectValue(OldScene.Get(), TEXT("RoomUI")));
            Test.TestNull(TEXT("Departed scene releases its shell"), ObjectValue(OldScene.Get(), TEXT("BaseUI")));
            Test.TestFalse(TEXT("Old room no longer retains the scene through load callback"), OldRoom->OnLoadCompleted.GetAllObjects().Contains(OldScene.Get()));
            Test.TestFalse(TEXT("Old room no longer retains the scene through unload callback"), OldRoom->OnUnloadCompleted.GetAllObjects().Contains(OldScene.Get()));
            Test.TestNull(TEXT("Unloaded room is detached from the shell"), OldRoom->BaseUI.Get());
            Test.TestFalse(TEXT("Stale scene cannot notify completion of another scene"), OldScene->NotifyEnterCompleted());
        }
    };

    class FNormalFlow final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        TWeakObjectPtr<USquadMeetingRoomWidget> PreloadedRoom;
        int32 Step = -1;
        double Started = FPlatformTime::Seconds();
        void Next() { ++Step; Started = FPlatformTime::Seconds(); }
    public:
        FNormalFlow(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest) : Fixture(InFixture), Test(InTest) {}
        bool Update() override
        {
            if (FPlatformTime::Seconds() - Started > 20.)
            {
                Test->AddError(FString::Printf(TEXT("Actual scene Blueprint flow timed out at step %d; SMS state %d, error %s"),
                    Step, int32(Fixture->Manager->GetTransitionState()), *Fixture->Manager->GetLastError().ToString()));
                return true;
            }
            auto* Manager = Fixture->Manager.Get();
            switch (Step)
            {
            case -1:
                // Another Blueprint may create this same-tag UI before SMS enters the room.
                // The host reuses it; its load event has already fired when the scene binds.
                PreloadedRoom = Cast<USquadMeetingRoomWidget>(Fixture->Shell->CreateSceneUIByTag(Fixture->SquadTag));
                if (!Test->TestNotNull(TEXT("Preload the actual meeting-room UI before scene entry"), PreloadedRoom.Get())) return true;
                Next();
                return false;
            case 0:
                if (!Test->TestNotNull(TEXT("Preloaded room remains alive while its animation finishes"), PreloadedRoom.Get())) return true;
                if (!PreloadedRoom->IsSceneUILoaded()) return false;
                Test->TestFalse(TEXT("Preloaded UI completed its entrance before SMS binds callbacks"), PreloadedRoom->bTransitioning);
                Test->TestTrue(TEXT("Initial squad entry accepted"), Manager->SwitchScene(Fixture->SquadTag));
                Test->TestFalse(TEXT("Back button hidden during entry"), Fixture->Shell->bBackButtonVisible);
                Next();
                return false;
            case 1:
                if (!Fixture->IsIdleAt(Fixture->SquadTag)) return false;
                Test->TestTrue(TEXT("Scene reuses the already-loaded same-tag UI without waiting for a second load event"),
                    Fixture->Shell->GetCurrentSceneUI() == PreloadedRoom.Get());
                if (!Fixture->CheckEntered(*Test)) return true;
                // Slow only this exit, so the camera is guaranteed to finish first. Explicit UI
                // completion below exercises the host's guarded unload broadcast deterministically.
                Fixture->OldRoom->WidgetAnimationPlaybackSpeed = .05f;
                Test->TestTrue(TEXT("Switch directly from squad to personnel accepted"), Manager->SwitchScene(Fixture->PersonnelTag));
                Test->TestFalse(TEXT("Back button hidden during exit"), Fixture->Shell->bBackButtonVisible);
                Test->TestTrue(TEXT("Animated room remains mounted while unloading"), Fixture->Shell->GetCurrentSceneUI() == Fixture->OldRoom.Get());
                Next();
                return false;
            case 2:
                if (!CameraFinished(Fixture->OldScene.Get())) return false;
                Test->TestTrue(TEXT("Completed camera alone cannot finish scene exit"), Manager->GetTransitionState() == ESMS_SceneTransitionState::Leaving);
                Fixture->OldRoom->NotifyUnloadCompleted();
                Test->TestTrue(TEXT("Final scene notification is deferred beyond the UI unload broadcast"), Manager->GetTransitionState() == ESMS_SceneTransitionState::Leaving);
                Test->TestNull(TEXT("Host releases old UI before next scene starts"), Fixture->Shell->GetCurrentSceneUI());
                Next();
                return false;
            case 3:
                if (!Fixture->IsIdleAt(Fixture->PersonnelTag)) return false;
                Fixture->CheckDetached(*Test);
                Test->TestNotNull(TEXT("Next scene can mount personnel UI after guarded unload"), Cast<UPersonnelPreparationRoomWidget>(Fixture->Shell->GetCurrentSceneUI()));
                Test->TestTrue(TEXT("Meeting room can be entered again"), Manager->SwitchScene(Fixture->SquadTag));
                Next();
                return false;
            case 4:
            {
                if (!Fixture->IsIdleAt(Fixture->SquadTag)) return false;
                UObject* FirstScene = Fixture->OldScene.Get();
                UObject* FirstRoom = Fixture->OldRoom.Get();
                Test->TestTrue(TEXT("Re-entry creates a fresh scene instance"), Manager->GetCurrentScene() != FirstScene);
                Test->TestTrue(TEXT("Re-entry creates a fresh room instance"), Fixture->Shell->GetCurrentSceneUI() != FirstRoom);
                if (!Fixture->CheckEntered(*Test)) return true;
                Test->TestTrue(TEXT("Return to base overview accepted"), Manager->SwitchScene(Fixture->OverviewTag));
                Test->TestFalse(TEXT("Rapid duplicate switch is rejected during exit"), Manager->SwitchScene(Fixture->PersonnelTag));
                Next();
                return false;
            }
            case 5:
                if (!Fixture->IsIdleAt(Fixture->OverviewTag)) return false;
                Fixture->CheckDetached(*Test);
                Test->TestNull(TEXT("Natural exit animation removes the room"), Fixture->Shell->GetCurrentSceneUI());
                Test->TestFalse(TEXT("Overview leaves return button hidden"), Fixture->Shell->bBackButtonVisible);
                return true;
            }
            return false;
        }
    };

    class FMissingDependencies final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double Started = FPlatformTime::Seconds();
        void Next() { ++Step; Started = FPlatformTime::Seconds(); }
    public:
        FMissingDependencies(TSharedRef<FFixture> InFixture, FAutomationTestBase* InTest) : Fixture(InFixture), Test(InTest) {}
        bool Update() override
        {
            if (FPlatformTime::Seconds() - Started > 12.)
            {
                Test->AddError(FString::Printf(TEXT("Missing dependency left the squad scene busy at step %d."), Step));
                return true;
            }
            auto* Manager = Fixture->Manager.Get();
            switch (Step)
            {
            case 0:
                Fixture->Camera->CancelCameraStateMove();
                Test->TestTrue(TEXT("Temporarily unregister camera"), Fixture->CameraSubsystem->UnregisterFreeCamera(Fixture->Camera.Get()));
                Test->TestTrue(TEXT("Scene request remains accepted without a camera"), Manager->SwitchScene(Fixture->SquadTag));
                Next();
                return false;
            case 1:
                if (!Fixture->IsIdleAt(Fixture->SquadTag)) return false;
                if (!Fixture->CheckEntered(*Test)) return true;
                Test->TestTrue(TEXT("Failed camera request produces a diagnosable scene error"), HasFlowError(Manager->GetCurrentScene()));
                Fixture->CameraSubsystem->RegisterFreeCamera(Fixture->Camera.Get());
                Test->TestTrue(TEXT("Scene still supports exit after camera failure"), Manager->SwitchScene(Fixture->OverviewTag));
                Next();
                return false;
            case 2:
                if (!Fixture->IsIdleAt(Fixture->OverviewTag)) return false;
                Fixture->CheckDetached(*Test);
                Fixture->Controller->BaseWidget = nullptr;
                Test->TestTrue(TEXT("Scene request remains accepted without base UI"), Manager->SwitchScene(Fixture->SquadTag));
                Next();
                return false;
            case 3:
                if (!Fixture->IsIdleAt(Fixture->SquadTag)) return false;
                Test->TestTrue(TEXT("Missing shell produces a diagnosable scene error"), HasFlowError(Manager->GetCurrentScene()));
                Test->TestEqual(TEXT("Missing UI completes entry instead of blocking SMS"), Stage(Manager->GetCurrentScene()), 2);
                Test->TestNull(TEXT("Missing shell does not create an unhosted room"), ObjectValue(Manager->GetCurrentScene(), TEXT("RoomUI")));
                Fixture->Controller->BaseWidget = Fixture->Shell.Get();
                Test->TestTrue(TEXT("Incomplete presentation can still be left"), Manager->SwitchScene(Fixture->OverviewTag));
                Next();
                return false;
            case 4:
                if (!Fixture->IsIdleAt(Fixture->OverviewTag)) return false;
                Test->TestNull(TEXT("Missing UI branch leaves no dangling room in the host"), Fixture->Shell->GetCurrentSceneUI());
                return true;
            }
            return false;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadSceneNormalFlowTest, "SilverChoir.BaseUI.SquadSceneFlow.NormalLifecycle",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadSceneNormalFlowTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadSceneFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadSceneFlowTests::FNormalFlow(Fixture, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadSceneMissingDependenciesTest, "SilverChoir.BaseUI.SquadSceneFlow.MissingDependencies",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSquadSceneMissingDependenciesTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SquadSceneFlowTests::FFixture>();
    if (!Fixture->Initialize(*this)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(SquadSceneFlowTests::FMissingDependencies(Fixture, this));
    return true;
}
#endif
