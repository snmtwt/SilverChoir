#include "CSOCoverVolume.h"
#include "CSOCoverSubsystem.h"
#include "Components/LineBatchComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && ENABLE_DRAW_DEBUG
namespace
{
    struct FCSOScopedDebugCVar
    {
        IConsoleVariable* Variable;
        FString PreviousValue;
        EConsoleVariableFlags Priority;

        FCSOScopedDebugCVar(const TCHAR* Name, int32 Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
            , PreviousValue(Variable ? Variable->GetString() : FString())
            , Priority(Variable ? EConsoleVariableFlags(Variable->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode)
        {
            if (Variable) { Variable->Set(Value, Priority); }
        }

        ~FCSOScopedDebugCVar()
        {
            if (Variable) { Variable->Set(*PreviousValue, Priority); }
        }
    };

    struct FCSODebugTestWorld
    {
        UWorld* World = nullptr;

        FCSODebugTestWorld()
        {
            const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
                .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(true)
                .SetTransactional(false).CreateFXSystem(false);
            World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            if (World) { GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World); }
        }

        ~FCSODebugTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOEditorDebugPreviewTest, "CoverSmartObjects.Debug.EditorConsolePreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCSOEditorDebugPreviewTest::RunTest(const FString& Parameters)
{
    // Keep each variable's existing priority: ECVF_SetByTemp would lose to a
    // user's Console override. Scopes restore their prior values on every exit.
    FCSOScopedDebugCVar Helpers(TEXT("r.EnableDrawDebugHelpers"), 1);
    FCSOScopedDebugCVar DebugOff(TEXT("cso.Debug"), 0);
    FCSOScopedDebugCVar DebugLimit(TEXT("cso.DebugMaxPoints"), 2);
    if (!TestNotNull(TEXT("Global cover debug switch is registered"), DebugOff.Variable)
        || !TestNotNull(TEXT("Global cover debug cap is registered"), DebugLimit.Variable)
        || !TestNotNull(TEXT("Draw-debug helper switch is registered"), Helpers.Variable)) { return false; }
    FCSODebugTestWorld Fixture;
    if (!TestNotNull(TEXT("Isolated editor world exists"), Fixture.World)) { return false; }
    TestTrue(TEXT("Editor preview needs no player controller"), Fixture.World->GetFirstPlayerController() == nullptr);
    TestTrue(TEXT("Editor preview needs no runtime cover subsystem"), Fixture.World->GetSubsystem<UCSOCoverSubsystem>() == nullptr);

    ACSOCoverVolume* Volume = Fixture.World->SpawnActor<ACSOCoverVolume>();
    if (!TestNotNull(TEXT("Preview volume exists"), Volume)) { return false; }
    Volume->SetActorLocation(FVector(100, 200, 0));
    Volume->bDrawDebug = false;
    Volume->MaxDebugPoints = 3;
    TestTrue(TEXT("Preview can tick before Play"), Volume->ShouldTickIfViewportsOnly());
    // Seed the data being visualized directly: this regression concerns preview
    // routing, independent of navigation generation or runtime registration.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FCSOBakedCover& Cover = Volume->BakedCovers.AddDefaulted_GetRef();
        Cover.Position = FVector(Index * 100, 0, 0);
        Cover.PeekMask = int32(ECSOPeek::Stand) | int32(ECSOPeek::Left) | int32(ECSOPeek::Right);
        Cover.LeftPeekDistance = 65.f;
        Cover.RightPeekDistance = 80.f;
    }

    ULineBatchComponent* Foreground = Fixture.World->GetLineBatcher(UWorld::ELineBatcherType::Foreground);
    if (!TestNotNull(TEXT("Editor foreground line batcher exists"), Foreground)) { return false; }
    auto DrawFrame = [&]()
    {
        Foreground->Flush();
        Volume->Tick(1.f / 60.f);
    };

    DrawFrame();
    TestEqual(TEXT("Global off and local off emit no cover points"), Foreground->BatchedPoints.Num(), 0);
    TestEqual(TEXT("Global off and local off emit no direction lines"), Foreground->BatchedLines.Num(), 0);

    {
        FCSOScopedDebugCVar DebugOn(TEXT("cso.Debug"), 1);
        DrawFrame();
        TestEqual(TEXT("Global switch alone enables preview and respects the global point cap"), Foreground->BatchedPoints.Num(), 2);
        TestFalse(TEXT("Global preview emits direction lines"), Foreground->BatchedLines.IsEmpty());
        const FCSOBakedCover Cover = Volume->GetWorldCover(Volume->BakedCovers[0]);
        TestTrue(TEXT("Preview points use the volume world transform"), Foreground->BatchedPoints.ContainsByPredicate(
            [&Cover](const FBatchedPoint& Point) { return Point.Position.Equals(Cover.Position) && Point.DepthPriority == SDPG_Foreground; }));
        for (const ECSOPeek Peek : { ECSOPeek::Stand, ECSOPeek::Left, ECSOPeek::Right })
        {
            const FVector CoveredEye = Cover.GetEye(Volume->AgentProfile);
            const FVector PeekEye = Cover.GetPeekEye(Volume->AgentProfile, Peek);
            TestTrue(FString::Printf(TEXT("Baked peek %d is drawn through occluding geometry"), int32(Peek)),
                Foreground->BatchedLines.ContainsByPredicate([&](const FBatchedLine& Line)
                {
                    return Line.Start.Equals(CoveredEye) && Line.End.Equals(PeekEye) && Line.DepthPriority == SDPG_Foreground;
                }));
        }
        Volume->MaxDebugPoints = 1;
        DrawFrame();
        TestEqual(TEXT("Volume cap also constrains global preview"), Foreground->BatchedPoints.Num(), 1);
    }

    DrawFrame();
    TestEqual(TEXT("Turning global preview off stops new points"), Foreground->BatchedPoints.Num(), 0);
    TestEqual(TEXT("Turning global preview off stops new lines"), Foreground->BatchedLines.Num(), 0);

    Volume->bDrawDebug = true;
    Volume->MaxDebugPoints = 3;
    DrawFrame();
    TestEqual(TEXT("Local preview works independently of the global switch and its cap"), Foreground->BatchedPoints.Num(), 3);
    TestFalse(TEXT("Local preview also emits peek directions"), Foreground->BatchedLines.IsEmpty());
    Volume->bDrawDebug = false;
    DrawFrame();
    TestEqual(TEXT("Turning local preview off stops new points"), Foreground->BatchedPoints.Num(), 0);
    TestEqual(TEXT("Turning local preview off stops new lines"), Foreground->BatchedLines.Num(), 0);
    return true;
}
#endif
