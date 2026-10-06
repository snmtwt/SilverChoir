#include "CSOCoverVolume.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemConfig.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && WITH_RECAST
namespace
{
    struct FCSOBakeTestWorld
    {
        UWorld* World = nullptr;

        FCSOBakeTestWorld()
        {
            const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
                .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(true)
                .SetTransactional(false).CreateFXSystem(false);
            World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
        }

        ~FCSOBakeTestWorld()
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }

        UBoxComponent* AddBox(AActor* Actor, const FVector& Location, const FVector& Extent, bool bGeometry)
        {
            UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
            Actor->AddInstanceComponent(Box);
            if (Actor->GetRootComponent()) { Box->SetupAttachment(Actor->GetRootComponent()); }
            else { Actor->SetRootComponent(Box); }
            Box->SetMobility(EComponentMobility::Static);
            Box->SetBoxExtent(Extent);
            Box->SetWorldLocation(Location);
            Box->SetCollisionEnabled(bGeometry ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->SetCanEverAffectNavigation(bGeometry);
            Box->RegisterComponent();
            return Box;
        }

        void GeometryBox(const FVector& Location, const FVector& Extent)
        {
            AddBox(World->SpawnActor<AActor>(), Location, Extent, true);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSORealNavBakeTest, "CoverSmartObjects.Generation.NavBoundaryBake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSORealNavBakeTest::RunTest(const FString& Parameters)
{
    // Every object belongs to an unsaved isolated world. No authored map, asset, or seeded cover data is used.
    FCSOBakeTestWorld Fixture;
    Fixture.GeometryBox(FVector(0, 0, -20), FVector(1000, 1000, 20));
    Fixture.GeometryBox(FVector(-300, 0, 65), FVector(20, 200, 65));
    Fixture.GeometryBox(FVector(300, 0, 200), FVector(20, 200, 200));

    ANavMeshBoundsVolume* NavBounds = Fixture.World->SpawnActor<ANavMeshBoundsVolume>();
    Fixture.AddBox(NavBounds, FVector(0, 0, 200), FVector(900, 900, 350), false);
    UNavigationSystemConfig* Config = NewObject<UNavigationSystemConfig>(Fixture.World);
    Config->NavigationSystemClass = FSoftClassPath(UNavigationSystemV1::StaticClass());
    FNavigationSystem::AddNavigationSystemToWorld(*Fixture.World, FNavigationSystemRunMode::EditorMode, Config);
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Fixture.World);
    if (!TestNotNull(TEXT("Isolated navigation system created"), Navigation)) { return false; }
    Navigation->OnNavigationBoundsUpdated(NavBounds);
    Navigation->ReleaseInitialBuildingLock();
    // This fixture has no asynchronously loaded assets or ticking editor world. Release its editor startup
    // async-load lock explicitly; the normal editor tick that releases it never runs inside this test.
    Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Navigation->Build(); // Public editor Build synchronously waits for each nav data generator to complete.
    ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
    if (!TestNotNull(TEXT("Recast navigation data generated"), NavMesh)) { return false; }
    if (!TestTrue(TEXT("Real Recast tiles are populated"), NavMesh->HasValidNavmesh() && NavMesh->GetNumActiveTiles() > 0)) { return false; }

    ACSOCoverVolume* Volume = Fixture.World->SpawnActor<ACSOCoverVolume>();
    Volume->SetActorLocation(FVector(0, 0, 200));
    Volume->GenerationBounds->SetBoxExtent(FVector(800, 800, 300));
    Volume->MaxSamples = 20000;
    Volume->MaxCoverPoints = 1000;
    Volume->BakeCover();
    AddInfo(Volume->LastBakeReport);
    TestTrue(TEXT("Actual geometry bake has valid metadata"), Volume->IsBakeDataValid());
    TestTrue(TEXT("Actual geometry bake finishes within configured budgets"), Volume->bBakeComplete);
    if (!TestTrue(TEXT("Nav boundaries generated nonempty cover data"), !Volume->BakedCovers.IsEmpty())) { return false; }

    int32 LowWallStandPeeks = 0;
    int32 FullWallSidePeeks = 0;
    for (const FCSOBakedCover& Local : Volume->BakedCovers)
    {
        const FCSOBakedCover Cover = Volume->GetWorldCover(Local);
        if (Cover.Position.X < 0 && Cover.bCrouched && (Cover.PeekMask & int32(ECSOPeek::Stand)))
        {
            ++LowWallStandPeeks;
        }
        if (Cover.Position.X > 100 && !Cover.bCrouched && FMath::Abs(Cover.WallDirection.X) > 0.9)
        {
            for (const ECSOPeek Peek : { ECSOPeek::Left, ECSOPeek::Right })
            {
                if (!(Cover.PeekMask & int32(Peek))) { continue; }
                ++FullWallSidePeeks;
                const FVector Eye = Cover.GetPeekEye(Volume->AgentProfile, Peek);
                TestTrue(TEXT("Full-wall shot eye reaches 20cm beyond actual 200cm side edge"), FMath::Abs(Eye.Y) >= 220.f - KINDA_SMALL_NUMBER);
                TestTrue(TEXT("Edge approximation overshoot stays within its 5cm sampling step"), FMath::Abs(Eye.Y) <= 225.f + KINDA_SMALL_NUMBER);
                TestTrue(TEXT("Stored side distance includes displacement to the wall edge"),
                    FVector::Dist(Eye, Cover.GetEye(Volume->AgentProfile)) > Volume->AgentProfile.LeanDistance);
            }
        }
    }
    TestTrue(TEXT("Low wall supports standing to fire"), LowWallStandPeeks > 0);
    TestTrue(TEXT("Full wall supports edge plus 20cm side firing"), FullWallSidePeeks > 0);

    Volume->MovementTraceChannel = ECC_Camera;
    TestFalse(TEXT("Changing movement collision invalidates baked data"), Volume->IsBakeDataValid());
    Volume->MovementTraceChannel = ECC_Pawn;
    TestTrue(TEXT("Restoring movement collision restores metadata agreement"), Volume->IsBakeDataValid());
    Volume->SideEdgeSearchStep += 1.f;
    TestFalse(TEXT("Changing edge search quality invalidates baked data"), Volume->IsBakeDataValid());
    return true;
}
#endif
