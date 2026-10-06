#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_CameraConfigDataAsset.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "MTS_MapTransitionHandler.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_MapTransitionBlueprintLibrary.h"
#include "MTS_SubMapDataAsset.h"
#include "SMS_SceneLibrary.h"
#include "SMS_SceneBase.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace CameraMapOffsets
{
const FVector TestOffset(400, -900, -20000);

// Exercise the real new-game Blueprint without changing its saved load configuration.
struct FLoadOverride
{
    TStrongObjectPtr<UObject> Defaults;
    TArray<FMTS_SubMapLoadConfig>* Configs = nullptr;
    TArray<FMTS_SubMapLoadConfig> Saved;

    bool Initialize(AMainMenuPlayerController* Menu)
    {
        if (!Menu->NewGameTransitionHandlerClass) return false;
        Defaults.Reset(Menu->NewGameTransitionHandlerClass->GetDefaultObject());
        auto* Property = FindFProperty<FArrayProperty>(Defaults->GetClass(), TEXT("SubMapConfigs"));
        auto* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
        if (!Inner || Inner->Struct != FMTS_SubMapLoadConfig::StaticStruct()) return false;
        Configs = Property->ContainerPtrToValuePtr<TArray<FMTS_SubMapLoadConfig>>(Defaults.Get());
        Saved = *Configs;
        for (auto& Config : *Configs)
        {
            if (Config.MapID != TEXT("Base")) continue;
            Config.Location = TestOffset;
            Config.Rotation = FRotator::ZeroRotator;
            return true;
        }
        return false;
    }
    ~FLoadOverride() { if (Configs) *Configs = Saved; }
};

class FSceneFlow final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UGameInstance> GI;
    TSharedRef<FLoadOverride> LoadOverride;
    TWeakObjectPtr<AFCS_FreeCameraPawn> TestCamera;
    bool bSavedEdgeScroll = false;
    int32 Stage = 0;
    double Started = FPlatformTime::Seconds();
    FFCS_CameraState FirstOperationsState;
    const FGameplayTag Overview = FGameplayTag::RequestGameplayTag(TEXT("GameScene.BaseOverview"));
    const FGameplayTag Squad = FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom"));
    const FGameplayTag Personnel = FGameplayTag::RequestGameplayTag(TEXT("GameScene.PersonnelPreparationRoom"));
    const FGameplayTag Operations = FGameplayTag::RequestGameplayTag(TEXT("GameScene.OperationsCommandRoom"));

    bool CheckMember(USMS_SceneBase* Scene, AFCS_FreeCameraPawn* Camera, const TCHAR* Member)
    {
        auto* Field = FindFProperty<FStructProperty>(Scene->GetClass(), Member);
        if (!Test->TestTrue(TEXT("Authored camera state exists"), Field && Field->Struct == FFCS_CameraState::StaticStruct())) return false;
        const auto Local = *Field->ContainerPtrToValuePtr<FFCS_CameraState>(Scene);
        const auto Expected = UPlayerLibrary::AddCameraStateLocationOffset(Local, TestOffset);
        const auto Actual = Camera->GetCurrentCameraState();
        Test->TestTrue(FString::Printf(TEXT("%s uses map-local camera position plus actual instance offset; actual=%s expected=%s"), *Scene->GetName(), *Actual.Location.ToString(), *Expected.Location.ToString()), Actual.Location.Equals(Expected.Location, 1));
        Test->TestTrue(TEXT("Offset preserves authored yaw"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Actual.Yaw, Expected.Yaw), 0.f, .1f));
        Test->TestTrue(TEXT("Offset preserves authored pitch"), FMath::IsNearlyEqual(Actual.Pitch, Expected.Pitch, .1f));
        Test->TestTrue(TEXT("Offset preserves authored arm length"), FMath::IsNearlyEqual(Actual.TargetArmLength, Expected.TargetArmLength, 1.f));
        Test->TestTrue(TEXT("Scene does not write world coordinates back to its camera variable"), Local.Location.Equals(Field->ContainerPtrToValuePtr<FFCS_CameraState>(Scene->GetClass()->GetDefaultObject())->Location));
        return true;
    }

public:
    FSceneFlow(FAutomationTestBase* InTest, UGameInstance* Instance, TSharedRef<FLoadOverride> InOverride)
        : Test(InTest), GI(Instance), LoadOverride(InOverride) {}
    ~FSceneFlow() { if (TestCamera.IsValid()) TestCamera->SetEdgeScrollEnabled(bSavedEdgeScroll); }

    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 150.)
        {
            Test->AddError(FString::Printf(TEXT("Negative-height scene flow timed out at stage %d"), Stage));
            return true;
        }
        auto* World = GI.IsValid() ? GI->GetWorld() : nullptr;
        auto* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
        auto* Scenes = World ? USMS_SceneLibrary::GetSceneManager(World) : nullptr;
        auto* Camera = World ? UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World) : nullptr;
        if (Camera)
        {
            if (!TestCamera.IsValid())
            {
                TestCamera = Camera;
                bSavedEdgeScroll = Camera->GetCameraConfig() && Camera->GetCameraConfig()->bEnableEdgeScroll;
            }
            // The offscreen test viewport must not pan toward the desktop cursor between transitions.
            Camera->SetEdgeScrollEnabled(false);
        }
        if (!State || !Scenes || !Camera || !State->IsBaseMap()
            || GI->GetSubsystem<UMTS_MapTransitionSubsystem>()->IsTransitionInProgress()
            || Scenes->GetTransitionState() != ESMS_SceneTransitionState::Idle
            || Camera->IsMovingToCameraState()) return false;

        FVector Location;
        Test->TestTrue(TEXT("Base instance is queryable from the new Blueprint node"), UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(World, TEXT("Base"), Location));
        Test->TestTrue(TEXT("Query returns the real handler loading location"), Location.Equals(TestOffset));
        auto* Scene = Scenes->GetCurrentScene();
        if (!Test->TestNotNull(TEXT("Current scene remains valid"), Scene)) return true;
        const FGameplayTag Tags[] = {Overview, Squad, Overview, Personnel, Overview, Operations, Overview, Operations, Overview};
        Test->TestEqual(TEXT("Requested scene transition completed"), Scenes->GetCurrentSceneTag(), Tags[Stage]);

        if (Stage == 3)
        {
            // This legacy Blueprint authors its state as a function-local variable.
            const FVector Local(-1457.037335, 1802.329790, 100);
            Test->TestTrue(TEXT("Personnel room local-variable camera receives the same offset"), Camera->GetActorLocation().Equals(Local + TestOffset, 1));
        }
        else
        {
            // The current overview Blueprint authors its own final view on every return.
            // Preserve that graph, rather than imposing an older version's exit-view behavior.
            if (!CheckMember(Scene, Camera, Stage == 1 || Stage == 5 || Stage == 7 ? TEXT("InteriorCameraState") : TEXT("移动到区域上方"))) return true;
            if (Stage == 5) FirstOperationsState = Camera->GetCurrentCameraState();
            if (Stage == 7) Test->TestTrue(TEXT("Repeated entry never accumulates the loading offset"), Camera->GetCurrentCameraState().Location.Equals(FirstOperationsState.Location, .1));
        }
        Test->AddInfo(FString::Printf(TEXT("CAMERA_OFFSET_STAGE %d %s location=%s"), Stage, *Scene->GetName(), *Camera->GetActorLocation().ToString()));
        if (Stage == 8)
        {
            Test->AddInfo(TEXT("CAMERA_MAP_OFFSETS_OK real negative-height load, four scene Blueprints, return view, repeated entry"));
            return true;
        }
        ++Stage;
        if (!Test->TestTrue(TEXT("Next scene transition accepted"), Scenes->SwitchScene(Tags[Stage]))) return true;
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraMapOffsetSceneTest, "SilverChoir.Camera.MapOffsets.RuntimeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCameraMapOffsetSceneTest::RunTest(const FString&)
{
    // Verify the math helper with deliberately non-default settings before running saved assets.
    FFCS_CameraState Local;
    Local.Location = FVector(10, 20, 30);
    Local.Yaw = 21; Local.Pitch = -77; Local.TargetArmLength = 1234;
    Local.MoveSpeed = 567; Local.RotationSpeed = 89; Local.ZoomSpeed = 4321;
    const auto Offset = UPlayerLibrary::AddCameraStateLocationOffset(Local, CameraMapOffsets::TestOffset);
    TestTrue(TEXT("Helper translates only position"), Offset.Location.Equals(FVector(410, -880, -19970)));
    TestTrue(TEXT("Helper preserves angles, arm and speeds"), Offset.Yaw == Local.Yaw && Offset.Pitch == Local.Pitch && Offset.TargetArmLength == Local.TargetArmLength && Offset.MoveSpeed == Local.MoveSpeed && Offset.RotationSpeed == Local.RotationSpeed && Offset.ZoomSpeed == Local.ZoomSpeed);
    TestTrue(TEXT("Helper leaves source untouched"), Local.Location.Equals(FVector(10, 20, 30)));
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World())
            if (auto* Menu = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController()))
            {
                auto Fixture = MakeShared<CameraMapOffsets::FLoadOverride>();
                if (!TestTrue(TEXT("Override only the real new-game Base load request in memory"), Fixture->Initialize(Menu))) return false;
                auto* Instance = Context.World()->GetGameInstance();
                Menu->HandleMenuAction(EMainMenuAction::NewGame);
                ADD_LATENT_AUTOMATION_COMMAND(CameraMapOffsets::FSceneFlow(this, Instance, Fixture));
                return true;
            }
    AddError(TEXT("Run alone from MainMenu -game"));
    return false;
}
#endif

