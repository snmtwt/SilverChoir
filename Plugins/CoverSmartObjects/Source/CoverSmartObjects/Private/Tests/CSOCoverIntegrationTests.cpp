#include "CSOCoverSubsystem.h"
#include "CSOCoverVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_SMARTOBJECT_DEBUG
namespace
{
    struct FCSOTestWorld
    {
        UWorld* World = nullptr;
        ACSOCoverVolume* Volume = nullptr;
        UCSOCoverSubsystem* Covers = nullptr;
        UBoxComponent* Wall = nullptr;
        AActor* User = nullptr;

        UBoxComponent* Box(FVector Location, FVector Extent)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
            Actor->AddInstanceComponent(Shape);
            Actor->SetRootComponent(Shape);
            Shape->SetBoxExtent(Extent);
            Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Shape->SetCollisionObjectType(ECC_WorldStatic);
            Shape->SetCollisionResponseToAllChannels(ECR_Block);
            Shape->RegisterComponent();
            Actor->SetActorLocation(Location);
            return Shape;
        }

        void SeedBake(ACSOCoverVolume* Target, FVector Position)
        {
            FCSOBakedCover Point;
            Point.Position = Position;
            Point.PeekMask = int32(ECSOPeek::Stand);
            Target->BakedCovers.Add(Point);
            // Seed exactly the serialized bake metadata, without adding a test-only runtime API.
            auto CopyStruct = [Target](const TCHAR* Name, const void* Source)
            {
                FStructProperty* Property = FindFProperty<FStructProperty>(Target->GetClass(), Name);
                Property->Struct->CopyScriptStruct(Property->ContainerPtrToValuePtr<void>(Target), Source);
            };
            CopyStruct(TEXT("BakedProfile"), &Target->AgentProfile);
            const FTransform Transform = Target->GetActorTransform();
            CopyStruct(TEXT("BakeTransform"), &Transform);
            const FTransform BoundsTransform = Target->GenerationBounds->GetComponentTransform();
            CopyStruct(TEXT("BakeBoundsTransform"), &BoundsTransform);
            const FVector Extent = Target->GenerationBounds->GetUnscaledBoxExtent();
            CopyStruct(TEXT("BakeBoundsExtent"), &Extent);
            FindFProperty<FBoolProperty>(Target->GetClass(), TEXT("bHasBake"))->SetPropertyValue_InContainer(Target, true);
        }

        FCSOTestWorld()
        {
            const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
                .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(true)
                .SetTransactional(false).CreateFXSystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Box(FVector(0, 0, -10), FVector(2000, 2000, 10));
            Wall = Box(FVector(100, 0, 65), FVector(20, 200, 65));
            User = World->SpawnActor<AActor>();
            Volume = World->SpawnActor<ACSOCoverVolume>();
            SeedBake(Volume, FVector::ZeroVector);
            Covers = World->GetSubsystem<UCSOCoverSubsystem>();
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            Volume->DispatchBeginPlay();
        }

        ~FCSOTestWorld()
        {
            Covers->UnregisterVolume(Volume);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }

        FCSOCoverQuery Query() const
        {
            FCSOCoverQuery Q;
            Q.Origin = FVector(-150, 0, 0);
            Q.TargetEnemy = FVector(1000, 0, 0);
            Q.bRequireReachable = false;
            Q.User = User;
            return Q;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOLowWallTest, "CoverSmartObjects.Integration.LowWallAndDynamicOcclusion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOLowWallTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    TestEqual(TEXT("One real Smart Object registered"), Test.Covers->GetRegisteredCoverCount(), 1);
    FCSOCoverQuery Q = Test.Query();
    FCSOCoverQueryResult R = Test.Covers->FindCover(Q);
    TestTrue(TEXT("Low wall hides body and permits standing shot"), R.IsValid());
    TestTrue(TEXT("Native Smart Object handle exists"), R.SmartObjectHandle.IsValid());
    TestTrue(TEXT("Native slot handle exists"), R.SlotHandle.IsValid());
    TestEqual(TEXT("Standing peek selected"), R.Peek, ECSOPeek::Stand);
    Q.Enemies = {Q.TargetEnemy, FVector(-1000, 0, 0), FVector(-1000, 0, 0)};
    R = Test.Covers->FindCover(Q);
    TestTrue(TEXT("Extra visible enemy is a soft cost, not rejection"), R.IsValid());
    TestEqual(TEXT("Duplicates and target not counted twice"), R.TestedEnemies, 2);
    TestEqual(TEXT("Rear enemy is exposed"), R.ExposedEnemies, 1);
    Test.Wall->SetBoxExtent(FVector(20, 200, 200), true);
    R = Test.Covers->FindCover(Q);
    TestFalse(TEXT("New tall geometry blocks old baked standing peek"), R.IsValid());
    Test.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    R = Test.Covers->FindCover(Q);
    TestFalse(TEXT("Removed wall cannot be trusted from bake"), R.IsValid());
    Q.MaxTraces = 1;
    R = Test.Covers->FindCover(Q);
    TestEqual(TEXT("Incomplete evaluation reports budget, not no-cover"), R.Status, ECSOCoverQueryStatus::BudgetExceeded);
    TestTrue(TEXT("Budget flag returned"), R.bSearchTruncated);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOReservationTest, "CoverSmartObjects.Integration.ReservationAndInvalidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOReservationTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    FCSOCoverQuery Q = Test.Query();
    FCSOCoverQueryResult First = Test.Covers->FindAndClaimCover(Q, 10.f);
    TestTrue(TEXT("Claim succeeds"), First.IsValid() && First.ClaimHandle.IsValid());
    TestTrue(TEXT("Occupied transition uses native SmartObject behavior"), Test.Covers->OccupyCover(First.ClaimHandle));
    TestTrue(TEXT("Lease can be renewed"), Test.Covers->RenewCoverLease(First.ClaimHandle, 20.f));
    Q.User = Test.World->SpawnActor<AActor>();
    TestFalse(TEXT("Other user cannot claim same cover"), Test.Covers->FindAndClaimCover(Q, 10.f).IsValid());
    TestTrue(TEXT("Release succeeds"), Test.Covers->ReleaseCover(First.ClaimHandle));
    TestFalse(TEXT("Stale release is harmless"), Test.Covers->ReleaseCover(First.ClaimHandle));
    FCSOCoverQueryResult Second = Test.Covers->FindAndClaimCover(Q, 10.f);
    TestTrue(TEXT("Other user can claim after release"), Second.IsValid());
    TestEqual(TEXT("Bounds invalidation removes cover"), Test.Covers->InvalidateCoversInBounds(FBox(FVector(-50, -50, -10), FVector(150, 50, 200))), 1);
    TestEqual(TEXT("No stale spatial record remains"), Test.Covers->GetRegisteredCoverCount(), 0);
    TestFalse(TEXT("Destroyed instance cannot renew"), Test.Covers->RenewCoverLease(Second.ClaimHandle));
    TestFalse(TEXT("Invalidated result no longer validates"), Test.Covers->ValidateCover(Second.CoverId, Q).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOExternalClaimTest, "CoverSmartObjects.Integration.StaleLeaseDoesNotReleaseExternalOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOExternalClaimTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    const FCSOCoverQueryResult Owned = Test.Covers->FindAndClaimCover(Test.Query(), 10.f);
    USmartObjectSubsystem* SmartObjects = Test.World->GetSubsystem<USmartObjectSubsystem>();
    TestTrue(TEXT("Initial claim exists"), Owned.ClaimHandle.IsValid());
    TestTrue(TEXT("External release succeeds"), SmartObjects->MarkSlotAsFree(Owned.ClaimHandle));
    AActor* NewUser = Test.World->SpawnActor<AActor>();
    const FSmartObjectActorUserData Data(NewUser);
    const FSmartObjectClaimHandle NewClaim = SmartObjects->MarkSlotAsClaimed(Owned.SlotHandle, ESmartObjectClaimPriority::Normal, FConstStructView::Make(Data));
    TestTrue(TEXT("External new claim exists"), NewClaim.IsValid());
    TestFalse(TEXT("Old lease cannot renew"), Test.Covers->RenewCoverLease(Owned.ClaimHandle));
    TestFalse(TEXT("Old lease cannot occupy new owner's slot"), Test.Covers->OccupyCover(Owned.ClaimHandle));
    Test.Covers->Tick(.3f);
    TestEqual(TEXT("Stale reservation removed"), Test.Covers->GetReservationCount(), 0);
    TestTrue(TEXT("Cleanup preserves native new owner"), SmartObjects->MarkSlotAsOccupied<USmartObjectBehaviorDefinition>(NewClaim) != nullptr);
    SmartObjects->MarkSlotAsFree(NewClaim);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOAdjacentCoverTest, "CoverSmartObjects.Integration.AdjacentSlotsCannotOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOAdjacentCoverTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    FCSOBakedCover Adjacent = Test.Volume->BakedCovers[0];
    Adjacent.Position.Y = 50.f;
    Test.Volume->BakedCovers.Add(Adjacent);
    Test.Covers->RefreshVolume(Test.Volume);
    TestEqual(TEXT("Two distinct native slots registered"), Test.Covers->GetRegisteredCoverCount(), 2);
    FCSOCoverQuery Q = Test.Query();
    const FCSOCoverQueryResult First = Test.Covers->FindAndClaimCover(Q, 10.f);
    TestTrue(TEXT("First user reserves a slot"), First.IsValid());
    Q.User = Test.World->SpawnActor<AActor>();
    TestFalse(TEXT("Neighboring overlapping slot cannot be reserved"), Test.Covers->FindAndClaimCover(Q, 10.f).IsValid());
    Test.Covers->ReleaseCover(First.ClaimHandle);
    TestTrue(TEXT("Neighbor reservation index clears on release"), Test.Covers->FindAndClaimCover(Q, 10.f).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOTallWallPeekTest, "CoverSmartObjects.Integration.TallWallLateralTwentyCentimeters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOTallWallPeekTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    Test.Wall->SetWorldLocation(FVector(100, 200, 200));
    Test.Wall->SetBoxExtent(FVector(20, 200, 200), true); // Straight tall wall, left boundary y=0.
    auto Configure = [&Test](float PositionY)
    {
        Test.Volume->BakedCovers.Empty();
        Test.SeedBake(Test.Volume, FVector(0, PositionY, 0));
        Test.Volume->BakedCovers[0].bCrouched = false;
        Test.Volume->BakedCovers[0].PeekMask = int32(ECSOPeek::Left) | int32(ECSOPeek::Right);
        Test.Volume->BakedCovers[0].LeftPeekDistance = PositionY + 20.f;
        Test.Covers->RefreshVolume(Test.Volume);
    };
    Configure(40.f); // Default 34 cm agent remains behind edge; eye travels 40+20 cm past it.
    FCSOCoverQuery Q = Test.Query();
    Q.TargetEnemy.Y = 40.f;
    FCSOCoverQueryResult R = Test.Covers->FindCover(Q);
    TestTrue(TEXT("Full-height body hidden with legal side peek"), R.IsValid());
    TestEqual(TEXT("Left side sees target outside the wall boundary"), R.Peek, ECSOPeek::Left);
    TestFalse(TEXT("Tall cover does not request crouch"), R.bCrouched);
    TestEqual(TEXT("Head clears the measured wall corner by twenty centimeters"), R.PeekLocation, FVector(0, -20, 160));
    TestEqual(TEXT("Actual movement includes distance from covered center to corner"), R.PeekLocation.Y - R.Location.Y, -60.);
    Configure(10.f);
    R = Test.Covers->FindCover(Q);
    TestFalse(TEXT("Exposed shoulder cannot be excused by a clear head ray"), R.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOPawnOcclusionTest, "CoverSmartObjects.Integration.PawnsAreNeitherCoverNorSightObstacles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOPawnOcclusionTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    const auto PawnBox = [&Test](FVector Location, FVector Extent)
    {
        APawn* Pawn = Test.World->SpawnActor<APawn>();
        UBoxComponent* Shape = NewObject<UBoxComponent>(Pawn);
        Pawn->AddInstanceComponent(Shape);
        Pawn->SetRootComponent(Shape);
        Shape->SetBoxExtent(Extent);
        Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Shape->SetCollisionObjectType(ECC_Pawn);
        Shape->SetCollisionResponseToAllChannels(ECR_Block);
        Shape->RegisterComponent();
        Pawn->SetActorLocation(Location);
        return Pawn;
    };
    const FCSOCoverQuery Q = Test.Query();
    APawn* TargetPawn = PawnBox(Q.TargetEnemy + FVector(0,0,90), FVector(40,40,90));
    const FCSOCoverQueryResult R = Test.Covers->FindCover(Q);
    TestTrue(TEXT("Position-only target inside pawn collision still permits shot"), R.IsValid());
    CastChecked<UBoxComponent>(TargetPawn->GetRootComponent())->SetCollisionObjectType(ECC_WorldDynamic);
    const FCSOCoverQueryResult CustomChannel = Test.Covers->FindCover(Q);
    TestTrue(TEXT("Pawn with custom object channel is skipped lazily"), CustomChannel.IsValid());
    TestTrue(TEXT("Custom-channel retry is charged to the trace budget"), CustomChannel.TracesUsed > R.TracesUsed);
    FCSOCoverQuery Limited = Q;
    Limited.MaxTraces = R.TracesUsed;
    TestEqual(TEXT("Skipping pawn cannot bypass total trace limit"), Test.Covers->FindCover(Limited).Status, ECSOCoverQueryStatus::BudgetExceeded);
    Test.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PawnBox(FVector(100,0,65), FVector(20,200,65));
    TestFalse(TEXT("Another pawn cannot impersonate a protective wall"), Test.Covers->FindCover(Q).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOMovementClearanceTest, "CoverSmartObjects.Integration.MovementOnlyCeilingBlocksStanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOMovementClearanceTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    const FCSOCoverQuery Q = Test.Query();
    TestTrue(TEXT("Standing shot initially clear"), Test.Covers->FindCover(Q).IsValid());
    UBoxComponent* Ceiling = Test.Box(FVector(0,0,160), FVector(60,60,5));
    Ceiling->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    TestFalse(TEXT("Ceiling invisible to sight still blocks standing capsule"), Test.Covers->FindCover(Q).IsValid());
    Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Removed movement obstacle is reevaluated immediately"), Test.Covers->FindCover(Q).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOSideMovementClearanceTest, "CoverSmartObjects.Integration.MovementOnlyObstacleBlocksLateralHead", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOSideMovementClearanceTest::RunTest(const FString& Parameters)
{
    FCSOTestWorld Test;
    Test.Wall->SetWorldLocation(FVector(100,200,200));
    Test.Wall->SetBoxExtent(FVector(20,200,200), true);
    Test.Volume->BakedCovers.Empty();
    Test.SeedBake(Test.Volume, FVector(0,40,0));
    Test.Volume->BakedCovers[0].bCrouched = false;
    Test.Volume->BakedCovers[0].PeekMask = int32(ECSOPeek::Left);
    Test.Volume->BakedCovers[0].LeftPeekDistance = 60.f;
    Test.Covers->RefreshVolume(Test.Volume);
    FCSOCoverQuery Q = Test.Query();
    Q.TargetEnemy.Y = 40.f;
    TestTrue(TEXT("Lateral peek initially clear"), Test.Covers->FindCover(Q).IsValid());
    // Outside the standing capsule, but directly inside the head's covered-to-peek trajectory.
    UBoxComponent* Blocker = Test.Box(FVector(0,-10,160), FVector(5,2,5));
    Blocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    TestFalse(TEXT("Sight-transparent physical obstacle prevents lateral head clipping"), Test.Covers->FindCover(Q).IsValid());
    Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Lateral head clearance updates when obstacle is removed"), Test.Covers->FindCover(Q).IsValid());
    return true;
}
#endif
