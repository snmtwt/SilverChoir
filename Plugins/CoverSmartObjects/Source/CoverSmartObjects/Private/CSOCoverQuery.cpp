#include "CSOCoverSubsystem.h"

#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SmartObjectSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#endif

namespace CSOQuery
{
    static TAutoConsoleVariable<int32> DebugQuery(TEXT("cso.DebugQuery"), 0,
        TEXT("Draw live cover query protection/peek rays and rejection reasons (0/1)."));
    constexpr int32 MaxVisitedCells = 4096;
    constexpr int32 MaxGatheredCovers = 4096;
    constexpr int32 HardMaxCandidates = 512;
    constexpr int32 HardMaxTraces = 16384;
    constexpr int32 HardMaxPathTests = 64;

    enum class ETest : uint8 { Passed, Rejected, BudgetExceeded };
    enum class ESightTrace : uint8 { Clear, Blocked, BudgetExceeded };

    struct FContext
    {
        UWorld& World;
        const FCSOCoverQuery& Query;
        FVector TargetEye;
        TArray<FVector> OtherEyes;
        FCollisionQueryParams Collision;
        FCollisionQueryParams ClearanceCollision;
        FCollisionResponseParams SightResponse;
        float RequestedRadius = 0.f;
        float RequestedHeight = 0.f;
        int32 Traces = 0;
        int32 Candidates = 0;
        int32 Paths = 0;
        bool bTruncated = false;
        bool bDraw = false;

        FContext(UWorld& InWorld, const FCSOCoverQuery& InQuery)
            : World(InWorld), Query(InQuery), Collision(SCENE_QUERY_STAT(CSOCoverQuery), InQuery.bTraceComplex),
              ClearanceCollision(SCENE_QUERY_STAT(CSOCoverClearance), false)
        {
            bDraw = Query.bDrawDebug || DebugQuery.GetValueOnGameThread() != 0;
            const FVector EyeOffset(0., 0., Query.bEnemyPositionsAreEyes ? 0. : Query.EnemyEyeHeight);
            TargetEye = Query.TargetEnemy + EyeOffset;
            for (const FVector& Enemy : Query.Enemies)
            {
                const FVector Eye = Enemy + EyeOffset;
                if (Eye.Equals(TargetEye, 0.1) || OtherEyes.ContainsByPredicate(
                    [&Eye](const FVector& Existing) { return Eye.Equals(Existing, 0.1); })) { continue; }
                OtherEyes.Add(Eye);
            }
            for (AActor* Actor : Query.IgnoredActors) { if (IsValid(Actor)) { Collision.AddIgnoredActor(Actor); } }
            if (IsValid(Query.User))
            {
                Collision.AddIgnoredActor(Query.User);
                ClearanceCollision.AddIgnoredActor(Query.User);
                if (const AController* Controller = Cast<AController>(Query.User))
                {
                    if (Controller->GetPawn())
                    {
                        Collision.AddIgnoredActor(Controller->GetPawn());
                        ClearanceCollision.AddIgnoredActor(Controller->GetPawn());
                    }
                }
            }
            Collision.bFindInitialOverlaps = true;
            // Pawn-channel shapes are filtered by the physics query, without scanning the world's pawns.
            // Custom-channel pawn shapes are skipped lazily by TraceSight, charging every retry to the budget.
            // ClearanceCollision retains other pawns, so physically occupied cover is still rejected.
            SightResponse.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
            const APawn* Pawn = Cast<APawn>(Query.User);
            if (const AController* Controller = Cast<AController>(Query.User)) { Pawn = Controller->GetPawn(); }
            if (Pawn)
            {
                const FNavAgentProperties& Agent = Pawn->GetNavAgentPropertiesRef();
                RequestedRadius = Agent.AgentRadius;
                RequestedHeight = Agent.AgentHeight;
                if (!FMath::IsFinite(RequestedRadius) || !FMath::IsFinite(RequestedHeight)
                    || RequestedRadius <= 0.f || RequestedHeight <= 0.f)
                {
                    float HalfHeight = 0.f;
                    Pawn->GetSimpleCollisionCylinder(RequestedRadius, HalfHeight);
                    RequestedHeight = HalfHeight * 2.f;
                }
                if (!FMath::IsFinite(RequestedRadius) || !FMath::IsFinite(RequestedHeight)
                    || RequestedRadius <= 0.f || RequestedHeight <= 0.f)
                {
                    RequestedRadius = RequestedHeight = 0.f;
                }
            }
        }

        bool SpendTrace()
        {
            if (Traces >= Query.MaxTraces) { bTruncated = true; return false; }
            ++Traces;
            return true;
        }

        ESightTrace TraceSight(const FVector& Start, const FVector& End, FHitResult& OutHit)
        {
            // Every retry uses the original segment, so skipping a pawn cannot skip protective geometry behind it.
            // The shared trace cap also bounds stacked or custom-channel pawns and any pathological repeat hit.
            while (SpendTrace())
            {
                OutHit = FHitResult();
                const bool bHit = World.LineTraceSingleByChannel(OutHit, Start, End, Query.TraceChannel, Collision, SightResponse);
                if (!bHit) { return ESightTrace::Clear; }
                if (const APawn* Pawn = Cast<APawn>(OutHit.GetActor()))
                {
                    Collision.AddIgnoredActor(Pawn);
                    continue;
                }
                return ESightTrace::Blocked;
            }
            return ESightTrace::BudgetExceeded;
        }

        void Reject(const FCSORuntimeCover& Cover, const TCHAR* Reason) const
        {
            if (bDraw) { DrawDebugString(&World, Cover.Data.Position + FVector(0, 0, 210), Reason, nullptr, FColor::Red, 2.f, true); }
        }

        void Counters(FCSOCoverQueryResult& Result) const
        {
            Result.TracesUsed = Traces;
            Result.CandidatesTested = Candidates;
            Result.PathTestsUsed = Paths;
            Result.bSearchTruncated = bTruncated;
        }
    };

    bool IsRequestValid(const FCSOCoverQuery& Q)
    {
        const auto ValidPosition = [](const FVector& V)
        {
            return !V.ContainsNaN() && V.GetAbsMax() <= 1.e12;
        };
        if (!ValidPosition(Q.Origin) || !ValidPosition(Q.TargetEnemy)
            || Q.Enemies.Num() > UCSOCoverSubsystem::MaxEnemies
            || Q.IgnoredActors.Num() > 64
            || !FMath::IsFinite(Q.SearchRadius) || Q.SearchRadius <= 0.f || Q.SearchRadius > 1.e7f
            || !FMath::IsFinite(Q.MaxVerticalDistance) || Q.MaxVerticalDistance < 0.f || Q.MaxVerticalDistance > 1.e7f
            || !FMath::IsFinite(Q.EnemyEyeHeight) || Q.EnemyEyeHeight < 0.f || Q.EnemyEyeHeight > 10000.f
            || !FMath::IsFinite(Q.ExposurePenalty) || Q.ExposurePenalty < 0.f || Q.ExposurePenalty > 1.e9f
            || Q.MaxCandidates < 1 || Q.MaxCandidates > HardMaxCandidates
            || Q.MaxTraces < 1 || Q.MaxTraces > HardMaxTraces
            || Q.MaxPathTests < 1 || Q.MaxPathTests > HardMaxPathTests
            || Q.TraceChannel.GetValue() >= ECC_MAX
            || Q.MovementChannel.GetValue() >= ECC_MAX
            || static_cast<uint8>(Q.Ranking) > static_cast<uint8>(ECSOCoverRanking::Nearest)) { return false; }
        for (const FVector& Enemy : Q.Enemies) { if (!ValidPosition(Enemy)) { return false; } }
        return true;
    }

    // Six samples cover crown, chest, both shoulders, pelvis and knees. All must be hidden.
    void BodySamples(const FCSORuntimeCover& Cover, TArray<FVector, TInlineAllocator<6>>& Out)
    {
        const auto& P = Cover.Profile;
        const auto& D = Cover.Data;
        const double Height = 2. * (D.bCrouched ? P.CrouchHalfHeight : P.StandHalfHeight);
        const FVector Up = FVector::UpVector;
        const FVector Shoulder = D.GetRight() * P.Radius * 0.9;
        Out.Add(D.Position + Up * (Height - 2.));
        Out.Add(D.Position + Up * Height * 0.7);
        Out.Add(D.Position + Up * Height * 0.8 + Shoulder);
        Out.Add(D.Position + Up * Height * 0.8 - Shoulder);
        Out.Add(D.Position + Up * Height * 0.45);
        Out.Add(D.Position + Up * Height * 0.2);
    }

    ETest IsHidden(FContext& Context, const FVector& EnemyEye,
        const TArray<FVector, TInlineAllocator<6>>& Samples)
    {
        for (const FVector& Sample : Samples)
        {
            FHitResult Hit;
            const ESightTrace Trace = Context.TraceSight(EnemyEye, Sample, Hit);
            if (Trace == ESightTrace::BudgetExceeded) { return ETest::BudgetExceeded; }
            // An enemy eye embedded in world geometry must never count as protective cover.
            const bool bProtected = Trace == ESightTrace::Blocked && !Hit.bStartPenetrating;
            if (Context.bDraw)
            {
                DrawDebugLine(&Context.World, EnemyEye, Sample, bProtected ? FColor::Green : FColor::Red, false, 2.f, 0, 0.8f);
            }
            if (!bProtected) { return ETest::Rejected; }
        }
        return ETest::Passed;
    }

    ETest HasCapsuleRoom(FContext& Context, const FCSORuntimeCover& Cover, bool bStanding)
    {
        if (!Context.SpendTrace()) { return ETest::BudgetExceeded; }
        const FCSOAgentProfile& P = Cover.Profile;
        const float Half = (bStanding ? P.StandHalfHeight : P.CrouchHalfHeight) + P.Clearance;
        const FVector Center = Cover.Data.Position + FVector(0., 0., Half + 1.);
        const bool bBlocked = Context.World.OverlapBlockingTestByChannel(Center, FQuat::Identity,
            Context.Query.MovementChannel, FCollisionShape::MakeCapsule(P.Radius + P.Clearance, Half), Context.ClearanceCollision);
        return bBlocked ? ETest::Rejected : ETest::Passed;
    }

    ETest CanPeek(FContext& Context, const FCSORuntimeCover& Cover, ECSOPeek Peek, FVector& OutEye)
    {
        if (Peek == ECSOPeek::Stand)
        {
            // Standing clearance is independent of the crouched geometry cached by the bake.
            const ETest Room = HasCapsuleRoom(Context, Cover, true);
            if (Room != ETest::Passed) { return Room; }
        }
        const FVector CoveredEye = Cover.Data.GetEye(Cover.Profile);
        OutEye = Cover.Data.GetPeekEye(Cover.Profile, Peek);
        if (!Context.SpendTrace()) { return ETest::BudgetExceeded; }
        FHitResult SweepHit;
        // Head travel is physical movement: sight-transparent obstacles and other pawns still block it.
        if (Context.World.SweepSingleByChannel(SweepHit, CoveredEye, OutEye, FQuat::Identity,
            Context.Query.MovementChannel, FCollisionShape::MakeSphere(FMath::Max(0.1f, Cover.Profile.EyeSweepRadius)),
            Context.ClearanceCollision)) { return ETest::Rejected; }
        FHitResult ShotHit;
        const ESightTrace Shot = Context.TraceSight(OutEye, Context.TargetEye, ShotHit);
        if (Shot == ESightTrace::BudgetExceeded) { return ETest::BudgetExceeded; }
        const bool bShotBlocked = Shot == ESightTrace::Blocked;
        if (Context.bDraw)
        {
            DrawDebugDirectionalArrow(&Context.World, CoveredEye, OutEye, 6.f, FColor::Cyan, false, 2.f, 0, 2.f);
            DrawDebugLine(&Context.World, OutEye, Context.TargetEye, bShotBlocked ? FColor::Orange : FColor::Cyan, false, 2.f, 0, 1.5f);
        }
        return bShotBlocked ? ETest::Rejected : ETest::Passed;
    }

    ETest Evaluate(FContext& Context, const FCSORuntimeCover& Cover, FCSOCoverQueryResult& Result)
    {
        ++Context.Candidates;
        if (!Cover.Profile.IsValid() || Cover.Data.WallDirection.IsNearlyZero()) { return ETest::Rejected; }
        if (Cover.Profile.Radius < Context.RequestedRadius || Cover.Profile.StandHalfHeight * 2.f < Context.RequestedHeight)
        {
            Context.Reject(Cover, TEXT("Agent exceeds baked profile"));
            return ETest::Rejected;
        }
        const ETest Room = HasCapsuleRoom(Context, Cover, !Cover.Data.bCrouched);
        if (Room != ETest::Passed) { Context.Reject(Cover, TEXT("Stance blocked")); return Room; }
        TArray<FVector, TInlineAllocator<6>> Samples;
        BodySamples(Cover, Samples);
        const ETest Hidden = IsHidden(Context, Context.TargetEye, Samples);
        if (Hidden != ETest::Passed) { Context.Reject(Cover, TEXT("Target sees body / trace budget")); return Hidden; }

        ECSOPeek Chosen = ECSOPeek::None;
        FVector PeekEye = FVector::ZeroVector;
        for (const ECSOPeek Peek : {ECSOPeek::Stand, ECSOPeek::Left, ECSOPeek::Right})
        {
            if ((Cover.Data.PeekMask & static_cast<int32>(Peek)) == 0) { continue; }
            // A full-height cover uses lateral leaning; standing never counts as an exit from it.
            if (Peek == ECSOPeek::Stand && !Cover.Data.bCrouched) { continue; }
            const ETest PeekTest = CanPeek(Context, Cover, Peek, PeekEye);
            if (PeekTest == ETest::BudgetExceeded) { return PeekTest; }
            if (PeekTest == ETest::Passed) { Chosen = Peek; break; }
        }
        if (Chosen == ECSOPeek::None) { Context.Reject(Cover, TEXT("No clear target peek")); return ETest::Rejected; }

        int32 Exposed = 0;
        for (const FVector& Enemy : Context.OtherEyes)
        {
            const ETest OtherHidden = IsHidden(Context, Enemy, Samples);
            if (OtherHidden == ETest::BudgetExceeded) { return OtherHidden; }
            if (OtherHidden == ETest::Rejected) { ++Exposed; }
        }
        Result.Status = ECSOCoverQueryStatus::Success;
        Result.CoverId = Cover.Id;
        Result.Location = Cover.Data.Position;
        Result.WallDirection = Cover.Data.WallDirection;
        Result.bCrouched = Cover.Data.bCrouched;
        Result.SmartObjectHandle = Cover.SmartObjectHandle;
        Result.SlotHandle = Cover.SlotHandle;
        Result.Peek = Chosen;
        Result.PeekLocation = PeekEye;
        Result.ExposedEnemies = Exposed;
        Result.TestedEnemies = Context.OtherEyes.Num() + 1;
        Result.Distance = FVector::Distance(Context.Query.Origin, Cover.Data.Position);
        Result.Score = static_cast<float>(CSOCover::Score(Result.Distance, Exposed, Context.Query.ExposurePenalty));
        return ETest::Passed;
    }

    enum class EPathTest : uint8 { Reachable, Unreachable, BudgetExceeded, Unavailable };

    EPathTest IsReachable(FContext& Context, const FCSOCoverQueryResult& Result)
    {
        if (!Context.Query.bRequireReachable) { return EPathTest::Reachable; }
        if (Context.Paths >= Context.Query.MaxPathTests) { Context.bTruncated = true; return EPathTest::BudgetExceeded; }
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&Context.World);
        if (!Nav) { return EPathTest::Unavailable; }
        const APawn* Pawn = Cast<APawn>(Context.Query.User);
        if (const AController* Controller = Cast<AController>(Context.Query.User)) { Pawn = Controller->GetPawn(); }
        const ANavigationData* NavData = Pawn
            ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Context.Query.Origin)
            : Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
        if (!NavData) { return EPathTest::Unavailable; }
        FPathFindingQuery PathQuery(Context.Query.User.Get(), *NavData, Context.Query.Origin, Result.Location);
        PathQuery.SetAllowPartialPaths(false);
        ++Context.Paths;
        const FPathFindingResult Path = Pawn ? Nav->FindPathSync(Pawn->GetNavAgentPropertiesRef(), PathQuery) : Nav->FindPathSync(PathQuery);
        return Path.IsSuccessful() && Path.Path.IsValid() && !Path.Path->IsPartial()
            ? EPathTest::Reachable : EPathTest::Unreachable;
    }

    struct FCellCandidate { FIntVector Cell; double DistanceSquared = 0.; };
    struct FCellCloser
    {
        bool operator()(const FCellCandidate& A, const FCellCandidate& B) const { return A.DistanceSquared < B.DistanceSquared; }
    };

    double CellDistanceSquared(const FIntVector& Cell, const FVector& Origin)
    {
        const FVector Min = FVector(Cell) * UCSOCoverSubsystem::CellSize;
        return FBox(Min, Min + FVector(UCSOCoverSubsystem::CellSize)).ComputeSquaredDistanceToPoint(Origin);
    }

    bool Better(const FCSOCoverQueryResult& A, const FCSOCoverQueryResult& B, ECSOCoverRanking Ranking)
    {
        if (Ranking == ECSOCoverRanking::SafestThenNearest && A.ExposedEnemies != B.ExposedEnemies)
        { return A.ExposedEnemies < B.ExposedEnemies; }
        if (Ranking == ECSOCoverRanking::Balanced && A.Score != B.Score) { return A.Score < B.Score; }
        if (A.Distance != B.Distance) { return A.Distance < B.Distance; }
        return A.CoverId < B.CoverId;
    }
}

FCSOCoverQueryResult UCSOCoverSubsystem::FindCover(const FCSOCoverQuery& Query)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(CSO_FindCover);
    using namespace CSOQuery;
    FCSOCoverQueryResult Result;
    if (!IsInGameThread() || !IsRequestValid(Query)) { Result.Status = ECSOCoverQueryStatus::InvalidRequest; return Result; }
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client || !GetSmartObjects()) { Result.Status = ECSOCoverQueryStatus::Unavailable; return Result; }
    if (Query.User && (!IsValid(Query.User) || Query.User->GetWorld() != World)) { Result.Status = ECSOCoverQueryStatus::InvalidRequest; return Result; }
    FContext Context(*World, Query);
    if (Covers.IsEmpty()) { return Result; }
    TArray<const FCSORuntimeCover*> Nearby;
    TArray<FCellCandidate> Frontier;
    TSet<FIntVector> Visited;
    const double RadiusSquared = FMath::Square(static_cast<double>(Query.SearchRadius));
    const FIntVector OriginCell = CSOCover::CellFor(Query.Origin, CellSize);
    Frontier.HeapPush({OriginCell, 0.}, FCellCloser());
    Visited.Add(OriginCell);
    const FIntVector Directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int32 CellsTouched = 0;
    int32 RecordsTouched = 0;
    while (!Frontier.IsEmpty())
    {
        if (CellsTouched >= MaxVisitedCells || RecordsTouched >= MaxGatheredCovers) { Context.bTruncated = true; break; }
        FCellCandidate Cell;
        Frontier.HeapPop(Cell, FCellCloser(), EAllowShrinking::No);
        ++CellsTouched;
        if (const TArray<int64>* Ids = Cells.Find(Cell.Cell))
        {
            for (const int64 Id : *Ids)
            {
                if (RecordsTouched >= MaxGatheredCovers) { Context.bTruncated = true; break; }
                ++RecordsTouched;
                const FCSORuntimeCover* Cover = Covers.Find(Id);
                if (!Cover || !IsCoverAvailable(*Cover, Query.User)) { continue; }
                const FVector Delta = Cover->Data.Position - Query.Origin;
                if (FMath::Abs(Delta.Z) <= Query.MaxVerticalDistance && Delta.SizeSquared() <= RadiusSquared) { Nearby.Add(Cover); }
            }
        }
        for (const FIntVector& Direction : Directions)
        {
            const FIntVector Next = Cell.Cell + Direction;
            if (Visited.Contains(Next)) { continue; }
            const double MinZ = Next.Z * CellSize;
            if (MinZ > Query.Origin.Z + Query.MaxVerticalDistance || MinZ + CellSize < Query.Origin.Z - Query.MaxVerticalDistance) { continue; }
            const double DistSquared = CellDistanceSquared(Next, Query.Origin);
            if (DistSquared > RadiusSquared) { continue; }
            Visited.Add(Next);
            Frontier.HeapPush({Next, DistSquared}, FCellCloser());
        }
    }
    Nearby.Sort([&Query](const FCSORuntimeCover& A, const FCSORuntimeCover& B)
    {
        const double DistA = FVector::DistSquared(Query.Origin, A.Data.Position);
        const double DistB = FVector::DistSquared(Query.Origin, B.Data.Position);
        return DistA == DistB ? A.Id < B.Id : DistA < DistB;
    });
    if (Nearby.Num() > Query.MaxCandidates)
    {
        Nearby.SetNum(Query.MaxCandidates, EAllowShrinking::No);
        Context.bTruncated = true;
    }
    TArray<FCSOCoverQueryResult> Valid;
    for (const FCSORuntimeCover* Cover : Nearby)
    {
        FCSOCoverQueryResult Candidate;
        const ETest Test = Evaluate(Context, *Cover, Candidate);
        if (Test == ETest::BudgetExceeded) { break; }
        if (Test == ETest::Passed) { Valid.Add(Candidate); }
    }
    Valid.Sort([&Query](const FCSOCoverQueryResult& A, const FCSOCoverQueryResult& B) { return Better(A, B, Query.Ranking); });
    Result.Status = Context.bTruncated ? ECSOCoverQueryStatus::BudgetExceeded : ECSOCoverQueryStatus::NoCover;
    for (const FCSOCoverQueryResult& Candidate : Valid)
    {
        const EPathTest Path = IsReachable(Context, Candidate);
        if (Path == EPathTest::Reachable) { Result = Candidate; break; }
        if (Path == EPathTest::BudgetExceeded) { Result.Status = ECSOCoverQueryStatus::BudgetExceeded; break; }
        if (Path == EPathTest::Unavailable) { Result.Status = ECSOCoverQueryStatus::Unavailable; break; }
        if (Context.bDraw) { DrawDebugString(World, Candidate.Location + FVector(0,0,220), TEXT("No complete nav path"), nullptr, FColor::Red, 2.f, true); }
    }
    Context.Counters(Result);
    if (Context.bDraw && Result.IsValid())
    {
        DrawDebugSphere(World, Result.Location + FVector(0,0,20), 20.f, 12, FColor::Yellow, false, 2.f, 0, 2.f);
        DrawDebugString(World, Result.Location + FVector(0,0,240), FString::Printf(TEXT("Selected: exposed %d/%d, traces %d%s"),
            Result.ExposedEnemies, Result.TestedEnemies, Result.TracesUsed, Result.bSearchTruncated ? TEXT(" (budget limited)") : TEXT("")), nullptr, FColor::Yellow, 2.f, true);
    }
    return Result;
}

FCSOCoverQueryResult UCSOCoverSubsystem::ValidateCover(int64 CoverId, const FCSOCoverQuery& Query)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(CSO_ValidateCover);
    using namespace CSOQuery;
    FCSOCoverQueryResult Result;
    if (!IsInGameThread() || !IsRequestValid(Query)) { Result.Status = ECSOCoverQueryStatus::InvalidRequest; return Result; }
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client || !GetSmartObjects()) { Result.Status = ECSOCoverQueryStatus::Unavailable; return Result; }
    if (Query.User && (!IsValid(Query.User) || Query.User->GetWorld() != World)) { Result.Status = ECSOCoverQueryStatus::InvalidRequest; return Result; }
    const FCSORuntimeCover* Cover = Covers.Find(CoverId);
    if (!Cover || !IsCoverAvailable(*Cover, Query.User)) { return Result; }
    FContext Context(*World, Query);
    const ETest Test = Evaluate(Context, *Cover, Result);
    if (Test == ETest::BudgetExceeded) { Result.Status = ECSOCoverQueryStatus::BudgetExceeded; }
    else if (Test == ETest::Passed)
    {
        const EPathTest Path = IsReachable(Context, Result);
        if (Path != EPathTest::Reachable)
        {
            Result = FCSOCoverQueryResult();
            Result.Status = Path == EPathTest::Unavailable ? ECSOCoverQueryStatus::Unavailable
                : (Path == EPathTest::BudgetExceeded ? ECSOCoverQueryStatus::BudgetExceeded : ECSOCoverQueryStatus::NoCover);
        }
    }
    Context.Counters(Result);
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOQueryRankingTest, "CoverSmartObjects.Query.RankingTradeoffs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOQueryRankingTest::RunTest(const FString& Parameters)
{
    FCSOCoverQueryResult Near;
    Near.CoverId = 1; Near.Distance = 100.f; Near.ExposedEnemies = 2;
    Near.Score = CSOCover::Score(Near.Distance, Near.ExposedEnemies, 1000.);
    FCSOCoverQueryResult Safe;
    Safe.CoverId = 2; Safe.Distance = 1200.f; Safe.ExposedEnemies = 0;
    Safe.Score = CSOCover::Score(Safe.Distance, Safe.ExposedEnemies, 1000.);
    TestTrue(TEXT("Balanced prefers avoiding two enemies within distance penalty"), CSOQuery::Better(Safe, Near, ECSOCoverRanking::Balanced));
    TestTrue(TEXT("Nearest uses distance even when other enemies see it"), CSOQuery::Better(Near, Safe, ECSOCoverRanking::Nearest));
    Safe.Distance = 10000.f;
    TestTrue(TEXT("Safest is lexicographic, independent of distance"), CSOQuery::Better(Safe, Near, ECSOCoverRanking::SafestThenNearest));
    Safe = Near; Safe.CoverId = 2;
    TestTrue(TEXT("Equal quality uses deterministic ID tie break"), CSOQuery::Better(Near, Safe, ECSOCoverRanking::Balanced));
    TestFalse(TEXT("Tie comparison is asymmetric"), CSOQuery::Better(Safe, Near, ECSOCoverRanking::Balanced));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOQueryBoundsTest, "CoverSmartObjects.Query.WorkLimitsAndSpatialOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOQueryBoundsTest::RunTest(const FString& Parameters)
{
    FCSOCoverQuery Query;
    TestTrue(TEXT("Default request valid"), CSOQuery::IsRequestValid(Query));
    Query.Enemies.SetNum(33);
    TestFalse(TEXT("Too many enemies rejected rather than silently omitted"), CSOQuery::IsRequestValid(Query));
    Query.Enemies.Empty(); Query.IgnoredActors.SetNum(65);
    TestFalse(TEXT("Ignored actor input is bounded before collision setup"), CSOQuery::IsRequestValid(Query));
    Query.IgnoredActors.Empty();
    Query.Enemies.Empty(); Query.MaxTraces = 0;
    TestFalse(TEXT("Zero trace budget rejected"), CSOQuery::IsRequestValid(Query));
    Query.MaxTraces = 100000000;
    TestFalse(TEXT("Unbounded trace budget rejected"), CSOQuery::IsRequestValid(Query));
    Query = FCSOCoverQuery(); Query.ExposurePenalty = -1.f;
    TestFalse(TEXT("Negative exposure penalty cannot reward exposure"), CSOQuery::IsRequestValid(Query));
    Query = FCSOCoverQuery(); Query.SearchRadius = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Nonfinite radius rejected before cell math"), CSOQuery::IsRequestValid(Query));
    TArray<CSOQuery::FCellCandidate> Cells;
    Cells.HeapPush({FIntVector(9,0,0), 81000000.}, CSOQuery::FCellCloser());
    Cells.HeapPush({FIntVector(1,0,0), 1000000.}, CSOQuery::FCellCloser());
    Cells.HeapPush({FIntVector(4,0,0), 16000000.}, CSOQuery::FCellCloser());
    CSOQuery::FCellCandidate First;
    Cells.HeapPop(First, CSOQuery::FCellCloser(), EAllowShrinking::No);
    TestEqual(TEXT("Partition frontier expands smallest distance lower bound first"), First.Cell, FIntVector(1,0,0));
    TestEqual(TEXT("Cell lower bound respects negative coordinates"), CSOQuery::CellDistanceSquared(FIntVector(-2,0,0), FVector::ZeroVector), 1000000.);
    return true;
}
#endif
