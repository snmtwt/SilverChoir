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
    Fixture.GeometryBox(FVector(0, 450, 40), FVector(200, 20, 40));

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
    int32 LowCrouchPeeks = 0;
    uint8 FullWallSidesAndEnds = 0;
    for (const FCSOBakedCover& Local : Volume->BakedCovers)
    {
        const FCSOBakedCover Cover = Volume->GetWorldCover(Local);
        if (Cover.bLowCrouched)
        {
            ++LowCrouchPeeks;
            TestTrue(TEXT("Low crouch is an explicit crouched stance"), Cover.bCrouched && Cover.GetStance() == ECSOCoverStance::LowCrouch);
            TestEqual(TEXT("80cm cover uses a 72cm hidden silhouette"), Cover.GetBodyHeight(Volume->AgentProfile), 72.f);
            TestEqual(TEXT("Low crouch uses its 62cm eye origin"), Cover.GetEyeHeight(Volume->AgentProfile), 62.f);
            TestEqual(TEXT("Low crouch does not shrink the physical crouching capsule"), Volume->AgentProfile.CrouchHalfHeight, 60.f);
        }
        if (Cover.Position.X < 0 && Cover.bCrouched && !Cover.bLowCrouched && (Cover.PeekMask & int32(ECSOPeek::Stand)))
        {
            ++LowWallStandPeeks;
        }
        if (Cover.Position.X > 100 && !Cover.bCrouched && FMath::Abs(Cover.WallDirection.X) > 0.9)
        {
            for (const ECSOPeek Peek : { ECSOPeek::Left, ECSOPeek::Right })
            {
                if (!(Cover.PeekMask & int32(Peek))) { continue; }
                ++FullWallSidePeeks;
                const uint8 SideAndEnd = (Cover.Position.X < 300.f ? 0 : 2) + (Cover.Position.Y < 0.f ? 0 : 1);
                FullWallSidesAndEnds |= (1 << SideAndEnd);
                const FVector Eye = Cover.GetPeekEye(Volume->AgentProfile, Peek);
                TestTrue(TEXT("Full-wall body anchor is exactly 50cm inside the real corner"),
                    FMath::Abs((200.f - FMath::Abs(Cover.Position.Y)) - 50.f) <= 0.5f);
                TestTrue(TEXT("Full-wall shot eye reaches exactly 20cm beyond actual 200cm side edge"),
                    FMath::Abs(FMath::Abs(Eye.Y) - 220.f) <= 0.5f);
                TestTrue(TEXT("Stored side distance combines 50cm corner inset and 20cm exposure"),
                    FMath::Abs(FVector::Dist(Eye, Cover.GetEye(Volume->AgentProfile)) - 70.f) <= 0.5f);
            }
        }
    }
    TestTrue(TEXT("Low wall supports standing to fire"), LowWallStandPeeks > 0);
    TestTrue(TEXT("80cm wall generates explicit low-crouch firing points"), LowCrouchPeeks > 0);
    TestTrue(TEXT("Full wall supports edge plus 20cm side firing"), FullWallSidePeeks > 0);
    TestEqual(TEXT("Both wall faces support firing at both ends"), FullWallSidesAndEnds, uint8(15));

    Volume->MovementTraceChannel = ECC_Camera;
    TestFalse(TEXT("Changing movement collision invalidates baked data"), Volume->IsBakeDataValid());
    Volume->MovementTraceChannel = ECC_Pawn;
    TestTrue(TEXT("Restoring movement collision restores metadata agreement"), Volume->IsBakeDataValid());
    Volume->SideEdgeSearchStep += 1.f;
    TestFalse(TEXT("Changing edge search quality invalidates baked data"), Volume->IsBakeDataValid());
    Volume->SideEdgeSearchStep -= 1.f;
    Volume->AgentProfile.bEnableLowCrouch = false;
    Volume->BakeCover();
    TestFalse(TEXT("Disabling low crouch never silently emits a low-crouch point"),
        Volume->BakedCovers.ContainsByPredicate([](const FCSOBakedCover& Cover) { return Cover.bLowCrouched; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOQuantizedWallEndBakeTest, "CoverSmartObjects.Generation.QuantizedThinWallEnds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOQuantizedWallEndBakeTest::RunTest(const FString& Parameters)
{
    // Preserve the failing authored wall's position relative to the Recast voxel grid. A symmetric wall
    // placed at the origin does not reproduce the sloping, under-cleared contour on its east face.
    // Geometry is recreated in an unsaved world; this test never opens or modifies the authored map.
    FCSOBakeTestWorld Fixture;
    const FVector WallCenter(2059.723, 7139.23, 150);
    Fixture.GeometryBox(FVector(WallCenter.X, WallCenter.Y, -20), FVector(800, 1000, 20));
    Fixture.GeometryBox(WallCenter, FVector(10, 547.5, 150));
    Fixture.GeometryBox(FVector(WallCenter.X, 6581.73, 150), FVector(10, 10, 150));
    Fixture.GeometryBox(FVector(WallCenter.X, 7696.73, 150), FVector(10, 10, 150));

    ANavMeshBoundsVolume* NavBounds = Fixture.World->SpawnActor<ANavMeshBoundsVolume>();
    Fixture.AddBox(NavBounds, FVector(WallCenter.X, WallCenter.Y, 150), FVector(700, 900, 300), false);
    UNavigationSystemConfig* Config = NewObject<UNavigationSystemConfig>(Fixture.World);
    Config->NavigationSystemClass = FSoftClassPath(UNavigationSystemV1::StaticClass());
    FNavigationSystem::AddNavigationSystemToWorld(*Fixture.World, FNavigationSystemRunMode::EditorMode, Config);
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Fixture.World);
    if (!TestNotNull(TEXT("Quantized-wall navigation system created"), Navigation)) { return false; }
    Navigation->OnNavigationBoundsUpdated(NavBounds);
    ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create));
    if (!TestNotNull(TEXT("Quantized-wall Recast actor created"), NavMesh)) { return false; }
    FNavDataConfig NavConfig = NavMesh->GetConfig();
    NavConfig.AgentRadius = 35.f;
    NavConfig.AgentHeight = 144.f;
    NavMesh->SetConfig(NavConfig);
    NavMesh->SetCellSize(ENavigationDataResolution::Default, 19.f);
    NavMesh->SetCellHeight(ENavigationDataResolution::Default, 10.f);
    Navigation->ReleaseInitialBuildingLock();
    Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Navigation->Build();
    if (!TestTrue(TEXT("Quantized-wall fixture has real Recast tiles"), NavMesh->HasValidNavmesh())) { return false; }

    ACSOCoverVolume* Volume = Fixture.World->SpawnActor<ACSOCoverVolume>();
    Volume->SetActorLocation(FVector(2060, 7139, 0));
    Volume->GenerationBounds->SetBoxExtent(FVector(220, 700, 300));
    Volume->AgentProfile.bEnableLowCrouch = false;
    Volume->BakeCover();
    AddInfo(Volume->LastBakeReport);
    TestTrue(TEXT("Quantized-wall bake completes"), Volume->bBakeComplete && Volume->IsBakeDataValid());

    auto ValidateAnchors = [&](float ExpectedInset)
    {
        uint8 FacesAndEnds = 0;
        for (const FCSOBakedCover& Local : Volume->BakedCovers)
        {
            const FCSOBakedCover Cover = Volume->GetWorldCover(Local);
            if (Cover.bCrouched || FMath::Abs(Cover.WallDirection.X) < 0.9) { continue; }
            const bool bEast = Cover.Position.X > WallCenter.X;
            const bool bNorth = Cover.Position.Y > WallCenter.Y;
            const uint8 Bit = (bEast ? 2 : 0) + (bNorth ? 1 : 0);
            FacesAndEnds |= (1 << Bit);
            // The two end-cap boxes extend the central 1095cm wall by 20cm at each end.
            const double EndY = WallCenter.Y + (bNorth ? 567.5 : -567.5);
            const double ExpectedY = EndY + (bNorth ? -ExpectedInset : ExpectedInset);
            TestTrue(FString::Printf(TEXT("Inset %.0f: %s/%s body anchor is within 0.5cm of the requested corner inset"),
                ExpectedInset, bEast ? TEXT("east") : TEXT("west"), bNorth ? TEXT("north") : TEXT("south")),
                FMath::Abs(Cover.Position.Y - ExpectedY) <= 0.5);
            for (const ECSOPeek Peek : { ECSOPeek::Left, ECSOPeek::Right })
            {
                if (!(Cover.PeekMask & int32(Peek))) { continue; }
                const FVector Eye = Cover.GetPeekEye(Volume->AgentProfile, Peek);
                TestTrue(TEXT("Shot eye remains 20cm beyond the physical wall end after anchoring"),
                    FMath::Abs(Eye.Y - (EndY + (bNorth ? 20.0 : -20.0))) <= 0.5);
                TestTrue(TEXT("Baked peek displacement follows the configured inset, independent of the source sample"),
                    FMath::Abs(FVector::Dist(Eye, Cover.GetEye(Volume->AgentProfile)) - (ExpectedInset + 20.f)) <= 0.5);
            }
            FCollisionQueryParams Collision(SCENE_QUERY_STAT(CSOQuantizedWallTest));
            Collision.AddIgnoredActor(Volume);
            const FCSOAgentProfile& Profile = Volume->AgentProfile;
            TestFalse(TEXT("Recovered wall-end point retains full physical capsule clearance"), Fixture.World->OverlapBlockingTestByChannel(
                Cover.Position + FVector(0, 0, Profile.StandHalfHeight + Profile.Clearance), FQuat::Identity, ECC_Pawn,
                FCollisionShape::MakeCapsule(Profile.Radius + Profile.Clearance, Profile.StandHalfHeight + Profile.Clearance), Collision));
        }
        TestEqual(TEXT("Quantized thin wall supports east and west faces at its south end"), uint8(FacesAndEnds & 5), uint8(5));
        TestEqual(TEXT("Quantized thin wall supports both faces at both ends"), FacesAndEnds, uint8(15));
    };
    ValidateAnchors(50.f);

    // Use a different, non-divisor sampling interval on the already offset voxel grid.
    // Final corner anchors must not inherit whichever boundary samples happened to pass.
    Volume->SampleSpacing = 37.f;
    Volume->SideEdgeSearchStep = 7.f;
    Volume->BakeCover();
    TestTrue(TEXT("Different sampling and edge-probe spacing still complete a valid bake"), Volume->bBakeComplete && Volume->IsBakeDataValid());
    ValidateAnchors(50.f);

    Volume->CornerInsetDistance = 60.f;
    TestFalse(TEXT("Changing corner inset invalidates old anchors immediately"), Volume->IsBakeDataValid());
    Volume->BakeCover();
    TestTrue(TEXT("Rebaking stores the new corner inset"), Volume->bBakeComplete && Volume->IsBakeDataValid());
    ValidateAnchors(60.f);

    Volume->CornerInsetDistance = 10.f;
    Volume->BakeCover();
    TestTrue(TEXT("Too-shallow inset is a completed geometric rejection"), Volume->bBakeComplete);
    TestTrue(TEXT("Inset too small to hide the shoulders never produces unsafe corner slots"), Volume->BakedCovers.IsEmpty());

    Volume->CornerInsetDistance = 50.f;
    Volume->MaxSidePeekDistance = 65.f;
    Volume->BakeCover();
    TestTrue(TEXT("Insufficient side-peek displacement budget is handled safely"), Volume->bBakeComplete);
    TestTrue(TEXT("A 65cm side-peek budget cannot generate a 50cm inset plus 20cm exposure"), Volume->BakedCovers.IsEmpty());
    return true;
}
#endif
