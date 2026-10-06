#include "CSOCoverVolume.h"

#include "CSOCoverSubsystem.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "SmartObjectDefinition.h"
#include "UObject/Class.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCSOCoverBake, Log, All);

namespace
{
    bool ProfilesEqual(const FCSOAgentProfile& A, const FCSOAgentProfile& B)
    {
        return FCSOAgentProfile::StaticStruct()->CompareScriptStruct(&A, &B, 0);
    }

#if WITH_EDITOR && WITH_RECAST
    // Clip in box-local space so samples outside a small bake volume do not consume its work budget.
    bool ClipSegmentToBounds(const UBoxComponent& Bounds, FVector& Start, FVector& End)
    {
        const FTransform Transform = Bounds.GetComponentTransform();
        const FVector A = Transform.InverseTransformPosition(Start);
        const FVector B = Transform.InverseTransformPosition(End);
        const FVector Delta = B - A;
        const FVector Extent = Bounds.GetUnscaledBoxExtent();
        double First = 0.0;
        double Last = 1.0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::Abs(Delta[Axis]) < UE_DOUBLE_SMALL_NUMBER)
            {
                if (A[Axis] < -Extent[Axis] || A[Axis] > Extent[Axis]) { return false; }
                continue;
            }
            double Enter = (-Extent[Axis] - A[Axis]) / Delta[Axis];
            double Leave = (Extent[Axis] - A[Axis]) / Delta[Axis];
            if (Enter > Leave) { Swap(Enter, Leave); }
            First = FMath::Max(First, Enter);
            Last = FMath::Min(Last, Leave);
            if (First > Last) { return false; }
        }
        const FVector WorldDelta = End - Start;
        End = Start + WorldDelta * Last;
        Start += WorldDelta * First;
        return true;
    }

    struct FCSOBakeContext
    {
        UWorld& World;
        const ACSOCoverVolume& Volume;
        UNavigationSystemV1& Navigation;
        const ARecastNavMesh& NavMesh;
        FCollisionQueryParams Collision;
        TMap<FIntVector, TArray<int32>> AcceptedCells;
        TArray<FCSOBakedCover> Accepted;

        FCSOBakeContext(UWorld& InWorld, const ACSOCoverVolume& InVolume, UNavigationSystemV1& InNavigation, const ARecastNavMesh& InNavMesh)
            : World(InWorld), Volume(InVolume), Navigation(InNavigation), NavMesh(InNavMesh)
            , Collision(SCENE_QUERY_STAT(CSOCoverBake), InVolume.bTraceComplex, &InVolume)
        {
            Collision.bReturnPhysicalMaterial = false;
            Collision.bReturnFaceIndex = false;
            Collision.bIgnoreTouches = true;
            // Authored preview NPCs must not become permanent cover or invalidate otherwise usable space.
            for (TActorIterator<APawn> It(&World); It; ++It) { Collision.AddIgnoredActor(*It); }
        }

        bool Blocked(const FVector& Start, const FVector& End) const
        {
            return World.LineTraceTestByChannel(Start, End, Volume.GeometryTraceChannel, Collision);
        }

        bool SweepClear(const FVector& Start, const FVector& End, bool bMovement = false) const
        {
            const float Radius = Volume.AgentProfile.EyeSweepRadius;
            const ECollisionChannel Channel = bMovement ? Volume.MovementTraceChannel.GetValue() : Volume.GeometryTraceChannel.GetValue();
            FCollisionQueryParams SweepParams(Collision);
            if (bMovement) { SweepParams.bTraceComplex = false; }
            return Radius > KINDA_SMALL_NUMBER
                ? !World.SweepTestByChannel(Start, End, FQuat::Identity, Channel, FCollisionShape::MakeSphere(Radius), SweepParams)
                : !World.LineTraceTestByChannel(Start, End, Channel, SweepParams);
        }

        bool CapsuleFits(const FVector& Feet, float HalfHeight) const
        {
            const FCSOAgentProfile& Profile = Volume.AgentProfile;
            const float PaddedHalfHeight = HalfHeight + Profile.Clearance;
            const FVector Center = Feet + FVector(0, 0, PaddedHalfHeight);
            FCollisionQueryParams MovementCollision(Collision);
            MovementCollision.bTraceComplex = false;
            return !World.OverlapBlockingTestByChannel(Center, FQuat::Identity, Volume.MovementTraceChannel,
                FCollisionShape::MakeCapsule(Profile.Radius + Profile.Clearance, PaddedHalfHeight), MovementCollision);
        }

        bool FindGroundedFeet(const FVector& Point, FVector& OutFeet) const
        {
            FNavLocation Projected;
            if (!Navigation.ProjectPointToNavigation(Point, Projected, FVector(20, 20, 100), &NavMesh)) { return false; }
            FCollisionQueryParams MovementCollision(Collision);
            MovementCollision.bTraceComplex = false;
            FHitResult Floor;
            if (!World.LineTraceSingleByChannel(Floor, Projected.Location + FVector(0, 0, 40), Projected.Location - FVector(0, 0, 150), Volume.MovementTraceChannel, MovementCollision) ||
                Floor.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(Volume.MaxFloorSlopeDegrees)))
            {
                return false;
            }
            OutFeet = Floor.ImpactPoint + FVector(0, 0, 2);
            const FVector LocalFeet = Volume.GenerationBounds->GetComponentTransform().InverseTransformPosition(OutFeet);
            return FBox(-Volume.GenerationBounds->GetUnscaledBoxExtent(), Volume.GenerationBounds->GetUnscaledBoxExtent()).IsInsideOrOn(LocalFeet) &&
                FMath::Abs(OutFeet.Z - Projected.Location.Z) <= 45.f && CapsuleFits(OutFeet, Volume.AgentProfile.CrouchHalfHeight);
        }

        bool ProvidesBodyCover(const FVector& Feet, const FVector& Direction, float HalfHeight, float EyeHeight) const
        {
            const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction);
            const float Width = Volume.AgentProfile.Radius * 0.8f;
            // Silhouette samples include shoulders and head. Do not infer safety from just a single eye ray.
            const float Heights[] = { FMath::Min(20.f, HalfHeight * 0.5f), HalfHeight, EyeHeight, 2.f * HalfHeight - 2.f };
            const float Offsets[] = { 0.f, -Width, Width };
            for (const float Height : Heights)
            {
                for (const float Side : Offsets)
                {
                    const FVector Start = Feet + FVector(0, 0, Height) + Right * Side;
                    if (!Blocked(Start, Start + Direction * Volume.WallSearchDistance))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        int32 FindPeeks(FCSOBakedCover& Cover, bool bStandingFits) const
        {
            const FCSOAgentProfile& Profile = Volume.AgentProfile;
            const FVector Eye = Cover.GetEye(Profile);
            Cover.LeftPeekDistance = 0.f;
            Cover.RightPeekDistance = 0.f;
            int32 Result = 0;
            for (const ECSOPeek Peek : { ECSOPeek::Left, ECSOPeek::Right, ECSOPeek::Stand })
            {
                if (Peek == ECSOPeek::Stand && (!Cover.bCrouched || !bStandingFits))
                {
                    continue;
                }
                if (Peek != ECSOPeek::Stand)
                {
                    const FVector Side = Cover.GetRight() * (Peek == ECSOPeek::Left ? -1.f : 1.f);
                    const float MaxEdgeDistance = Volume.MaxSidePeekDistance - Profile.LeanDistance;
                    bool bFoundEdge = false;
                    for (float EdgeDistance = 0.f; EdgeDistance <= MaxEdgeDistance + KINDA_SMALL_NUMBER; EdgeDistance += Volume.SideEdgeSearchStep)
                    {
                        const FVector EdgeEye = Eye + Side * EdgeDistance;
                        // Locate the end of the blocker with a line, then add the requested distance BEYOND that edge.
                        // Final sphere sweeps separately validate the actual head trajectory and firing clearance.
                        if (!Blocked(EdgeEye, EdgeEye + Cover.WallDirection * Volume.PeekProbeDistance))
                        {
                            const float PeekDistance = EdgeDistance + Profile.LeanDistance;
                            if (Peek == ECSOPeek::Left) { Cover.LeftPeekDistance = PeekDistance; }
                            else { Cover.RightPeekDistance = PeekDistance; }
                            bFoundEdge = true;
                            break;
                        }
                    }
                    if (!bFoundEdge) { continue; }
                }
                const FVector PeekEye = Cover.GetPeekEye(Profile, Peek);
                if (SweepClear(Eye, PeekEye, true) && SweepClear(PeekEye, PeekEye + Cover.WallDirection * Volume.PeekProbeDistance))
                {
                    Result |= static_cast<int32>(Peek);
                }
                else if (Peek == ECSOPeek::Left) { Cover.LeftPeekDistance = 0.f; }
                else if (Peek == ECSOPeek::Right) { Cover.RightPeekDistance = 0.f; }
            }
            return Result;
        }

        bool IsDuplicate(const FCSOBakedCover& Cover) const
        {
            const double Spacing = FMath::Max(5.f, Volume.MinCoverSpacing);
            const FIntVector Cell = CSOCover::CellFor(Cover.Position, Spacing);
            for (int32 Z = -1; Z <= 1; ++Z)
            for (int32 Y = -1; Y <= 1; ++Y)
            for (int32 X = -1; X <= 1; ++X)
            {
                if (const TArray<int32>* Indices = AcceptedCells.Find(Cell + FIntVector(X, Y, Z)))
                {
                    for (const int32 Index : *Indices)
                    {
                        const FCSOBakedCover& Existing = Accepted[Index];
                        if (FVector::DistSquared(Existing.Position, Cover.Position) < FMath::Square(Spacing) &&
                            FVector::DotProduct(Existing.WallDirection, Cover.WallDirection) > 0.9 &&
                            Existing.bCrouched == Cover.bCrouched && (Existing.PeekMask & Cover.PeekMask) == Cover.PeekMask)
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        void TestSample(const FVector& EdgePoint, const FVector& EdgeDirection)
        {
            const FVector Local = Volume.GenerationBounds->GetComponentTransform().InverseTransformPosition(EdgePoint);
            if (!FBox(-Volume.GenerationBounds->GetUnscaledBoxExtent(), Volume.GenerationBounds->GetUnscaledBoxExtent()).IsInsideOrOn(Local))
            {
                return;
            }

            const FVector Perpendicular = FVector::CrossProduct(FVector::UpVector, EdgeDirection).GetSafeNormal2D();
            FVector Feet;
            if (!FindGroundedFeet(EdgePoint, Feet))
            {
                // Recast erodes by its agent radius, while our physical profile also includes a safety margin.
                // Try a small displacement towards either side; reprojection prevents moving off the navmesh.
                const float Nudge = FMath::Max(0.f, Volume.AgentProfile.Radius - NavMesh.GetConfig().AgentRadius) + Volume.AgentProfile.Clearance + 5.f;
                if (!FindGroundedFeet(EdgePoint + Perpendicular * Nudge, Feet) && !FindGroundedFeet(EdgePoint - Perpendicular * Nudge, Feet))
                {
                    return;
                }
            }
            for (const float Sign : { 1.f, -1.f })
            {
                FHitResult Wall;
                const FVector ProbeStart = Feet + FVector(0, 0, Volume.AgentProfile.CrouchEyeHeight);
                if (!World.LineTraceSingleByChannel(Wall, ProbeStart, ProbeStart + Perpendicular * Sign * Volume.WallSearchDistance, Volume.GeometryTraceChannel, Collision) ||
                    FMath::Abs(Wall.ImpactNormal.Z) > 0.5f)
                {
                    continue;
                }
                const FVector Direction = (-Wall.ImpactNormal).GetSafeNormal2D();
                if (Direction.IsNearlyZero() || !ProvidesBodyCover(Feet, Direction, Volume.AgentProfile.CrouchHalfHeight, Volume.AgentProfile.CrouchEyeHeight))
                {
                    continue;
                }
                const bool bStandingFits = CapsuleFits(Feet, Volume.AgentProfile.StandHalfHeight);
                FCSOBakedCover Cover;
                Cover.Position = Feet;
                Cover.WallDirection = Direction;
                Cover.bCrouched = !(bStandingFits && ProvidesBodyCover(Feet, Direction, Volume.AgentProfile.StandHalfHeight, Volume.AgentProfile.StandEyeHeight));
                Cover.PeekMask = FindPeeks(Cover, bStandingFits);
                // A high obstacle can have an opening at crouched height but none at standing height.
                if (Cover.PeekMask == 0 && !Cover.bCrouched)
                {
                    Cover.bCrouched = true;
                    Cover.PeekMask = FindPeeks(Cover, bStandingFits);
                }
                if (Cover.PeekMask == 0 || IsDuplicate(Cover))
                {
                    continue;
                }
                const int32 Index = Accepted.Add(Cover);
                AcceptedCells.FindOrAdd(CSOCover::CellFor(Feet, FMath::Max(5.f, Volume.MinCoverSpacing))).Add(Index);
                if (Accepted.Num() >= Volume.MaxCoverPoints)
                {
                    return;
                }
            }
        }
    };
#endif
}

ACSOCoverVolume::ACSOCoverVolume()
{
    PrimaryActorTick.bCanEverTick = true;
    GenerationBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("GenerationBounds"));
    SetRootComponent(GenerationBounds);
    GenerationBounds->SetBoxExtent(FVector(1000, 1000, 300));
    GenerationBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GenerationBounds->SetCanEverAffectNavigation(false);
    GenerationBounds->SetGenerateOverlapEvents(false);
    GenerationBounds->SetHiddenInGame(true);
    GenerationBounds->ShapeColor = FColor(70, 190, 230);
}

bool ACSOCoverVolume::IsBakeTransformValid() const
{
    const FTransform Transform = GetActorTransform();
    return GenerationBounds && Transform.GetScale3D().Equals(FVector::OneVector, 0.001) &&
        Transform.GetUnitAxis(EAxis::Z).Equals(FVector::UpVector, 0.001) &&
        GenerationBounds->GetComponentScale().Equals(FVector::OneVector, 0.001) &&
        GenerationBounds->GetUpVector().Equals(FVector::UpVector, 0.001);
}

bool ACSOCoverVolume::IsBakeDataValid() const
{
    return bHasBake && IsBakeTransformValid() && AgentProfile.IsValid() && ProfilesEqual(AgentProfile, BakedProfile) &&
        GetActorTransform().Equals(BakeTransform, 0.01) &&
        GenerationBounds->GetComponentTransform().Equals(BakeBoundsTransform, 0.01) &&
        GenerationBounds->GetUnscaledBoxExtent().Equals(BakeBoundsExtent, 0.01) &&
        GeometryTraceChannel == BakedTraceChannel && MovementTraceChannel == BakedMovementTraceChannel && bTraceComplex == bBakedTraceComplex &&
        MaxSidePeekDistance == BakedMaxSidePeekDistance && SideEdgeSearchStep == BakedSideEdgeSearchStep;
}

FCSOBakedCover ACSOCoverVolume::GetWorldCover(const FCSOBakedCover& LocalCover) const
{
    FCSOBakedCover Result = LocalCover;
    Result.Position = GetActorTransform().TransformPosition(LocalCover.Position);
    Result.WallDirection = GetActorTransform().TransformVectorNoScale(LocalCover.WallDirection).GetSafeNormal2D();
    return Result;
}

void ACSOCoverVolume::BakeCover()
{
#if WITH_EDITOR && WITH_RECAST
    UWorld* World = GetWorld();
    if (!World || World->IsGameWorld())
    {
        UE_LOG(LogCSOCoverBake, Warning, TEXT("BakeCover is available in an editor world only."));
        return;
    }
    if (!AgentProfile.IsValid() || !IsBakeTransformValid() || !FMath::IsFinite(SampleSpacing) || SampleSpacing < 5.f ||
        !FMath::IsFinite(MinCoverSpacing) || MinCoverSpacing < 5.f || !FMath::IsFinite(WallSearchDistance) || WallSearchDistance < 10.f ||
        !FMath::IsFinite(PeekProbeDistance) || PeekProbeDistance < 10.f || !FMath::IsFinite(MaxFloorSlopeDegrees) ||
        !FMath::IsFinite(MaxSidePeekDistance) || MaxSidePeekDistance < 10.f || MaxSidePeekDistance > 500.f ||
        !FMath::IsFinite(SideEdgeSearchStep) || SideEdgeSearchStep < 1.f || SideEdgeSearchStep > 25.f ||
        MaxFloorSlopeDegrees < 0.f || MaxFloorSlopeDegrees >= 90.f || MaxSamples <= 0 || MaxCoverPoints <= 0)
    {
        LastBakeReport = TEXT("Invalid profile/settings or transform. Use scale (1,1,1), upright rotation, and edit Box Extent to resize; then bake again.");
        UE_LOG(LogCSOCoverBake, Error, TEXT("%s: %s"), *GetName(), *LastBakeReport);
        return;
    }
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    const ARecastNavMesh* NavMesh = Navigation ? Cast<ARecastNavMesh>(Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
    if (!NavMesh || !NavMesh->HasValidNavmesh())
    {
        LastBakeReport = TEXT("No built Recast navigation found. Add NavMeshBoundsVolume, build navigation, and load the required map cells.");
        UE_LOG(LogCSOCoverBake, Warning, TEXT("%s: %s"), *GetName(), *LastBakeReport);
        return;
    }

    FScopedSlowTask Progress(100.f, NSLOCTEXT("CoverSmartObjects", "BakingCover", "Baking cover Smart Objects from loaded navigation..."));
    Progress.MakeDialog(true);
    TArray<FNavigationWallEdge> Edges;
    TArray<FNavTileRef> Tiles;
    NavMesh->BeginBatchQuery();
    NavMesh->GetAllNavMeshTiles(Tiles);
    const FBox Bounds = GenerationBounds->Bounds.GetBox();
    for (const FNavTileRef Tile : Tiles)
    {
        // GetAllNavMeshTiles includes empty allocated pool entries in UE 5.8. Their bounds are invalid,
        // and GetEdgesInTile assumes a populated header. FBox::Intersect alone does not test IsValid.
        const FBox TileBounds = NavMesh->GetNavMeshTileBounds(Tile);
        if (TileBounds.IsValid && TileBounds.Intersect(Bounds))
        {
            TArray<FNavigationWallEdge> TileEdges;
            NavMesh->GetEdgesInTile(Tile, TileEdges);
            Edges.Append(TileEdges);
        }
    }
    NavMesh->FinishBatchQuery();
    Progress.EnterProgressFrame(5.f);

    if (Edges.IsEmpty())
    {
        LastBakeReport = TEXT("No navigation boundary edges intersect this volume. Existing baked data was preserved.");
        UE_LOG(LogCSOCoverBake, Warning, TEXT("%s: %s"), *GetName(), *LastBakeReport);
        return;
    }

    FCSOBakeContext Context(*World, *this, *Navigation, *NavMesh);
    int32 Samples = 0;
    bool bTruncated = false;
    bool bCancelled = false;
    for (const FNavigationWallEdge& Edge : Edges)
    {
        if (Progress.ShouldCancel()) { bCancelled = true; break; }
        Progress.EnterProgressFrame(95.f / Edges.Num());
        FVector Start = Edge.Start;
        FVector End = Edge.End;
        if (!ClipSegmentToBounds(*GenerationBounds, Start, End)) { continue; }
        const FVector Delta = End - Start;
        const double Length = Delta.Size();
        const FVector Direction = Delta.GetSafeNormal2D();
        if (Direction.IsNearlyZero() || Length < 1.0) { continue; }
        const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / SampleSpacing), 1, MaxSamples);
        // Endpoints matter: the sides of an obstacle are often its only useful firing positions.
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            if (Samples >= MaxSamples || Context.Accepted.Num() >= MaxCoverPoints) { bTruncated = true; break; }
            if ((Samples & 63) == 0 && Progress.ShouldCancel()) { bCancelled = true; break; }
            ++Samples;
            const int32 OrderedStep = Step == 0 ? 0 : (Step == 1 ? Steps : Step - 1);
            Context.TestSample(Start + Delta * (static_cast<double>(OrderedStep) / Steps), Direction);
        }
        if (bTruncated || bCancelled) { break; }
    }
    if (bCancelled)
    {
        LastBakeReport = TEXT("Bake cancelled. Existing baked data was preserved.");
        UE_LOG(LogCSOCoverBake, Display, TEXT("%s: %s"), *GetName(), *LastBakeReport);
        return;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("CoverSmartObjects", "BakeCoverTransaction", "Bake Cover Smart Objects"));
    Modify();
    BakedCovers = MoveTemp(Context.Accepted);
    for (FCSOBakedCover& Cover : BakedCovers)
    {
        Cover.Position = GetActorTransform().InverseTransformPosition(Cover.Position);
        Cover.WallDirection = GetActorTransform().InverseTransformVectorNoScale(Cover.WallDirection).GetSafeNormal2D();
    }
    BakedProfile = AgentProfile;
    BakeTransform = GetActorTransform();
    BakeBoundsTransform = GenerationBounds->GetComponentTransform();
    BakeBoundsExtent = GenerationBounds->GetUnscaledBoxExtent();
    BakedTraceChannel = GeometryTraceChannel;
    BakedMovementTraceChannel = MovementTraceChannel;
    bBakedTraceComplex = bTraceComplex;
    BakedMaxSidePeekDistance = MaxSidePeekDistance;
    BakedSideEdgeSearchStep = SideEdgeSearchStep;
    bHasBake = true;
    bBakeComplete = !bTruncated;
    LastBakeReport = FString::Printf(TEXT("%d cover points from %d samples / %d boundary edges. %s Loaded navigation only; save this level."),
        BakedCovers.Num(), Samples, Edges.Num(), bTruncated ? TEXT("LIMIT REACHED: partial bake; increase budgets or use smaller volumes.") : TEXT("Completed."));
    MarkPackageDirty();
    UE_LOG(LogCSOCoverBake, Display, TEXT("%s: %s"), *GetName(), *LastBakeReport);
#else
    UE_LOG(LogCSOCoverBake, Warning, TEXT("Cover baking requires an editor build with Recast navigation."));
#endif
}

void ACSOCoverVolume::ClearBakedCover()
{
    if (UWorld* World = GetWorld(); World && World->IsGameWorld())
    {
        UE_LOG(LogCSOCoverBake, Warning, TEXT("ClearBakedCover is available in an editor world only."));
        return;
    }
#if WITH_EDITOR
    const FScopedTransaction Transaction(NSLOCTEXT("CoverSmartObjects", "ClearCoverTransaction", "Clear Baked Cover Smart Objects"));
    Modify();
#endif
    BakedCovers.Reset();
    bHasBake = false;
    bBakeComplete = false;
    LastBakeReport = TEXT("Baked cover points cleared.");
    MarkPackageDirty();
}

void ACSOCoverVolume::BeginPlay()
{
    Super::BeginPlay();
    if (UCSOCoverSubsystem* Subsystem = GetWorld()->GetSubsystem<UCSOCoverSubsystem>())
    {
        Subsystem->RegisterVolume(this);
    }
}

void ACSOCoverVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UCSOCoverSubsystem* Subsystem = World->GetSubsystem<UCSOCoverSubsystem>())
        {
            Subsystem->UnregisterVolume(this);
        }
    }
    Super::EndPlay(EndPlayReason);
}

void ACSOCoverVolume::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bDrawDebug || !GetWorld()) { return; }
    const bool bValid = IsBakeDataValid();
    const int32 Count = FMath::Min(BakedCovers.Num(), FMath::Max(1, MaxDebugPoints));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FCSOBakedCover Cover = GetWorldCover(BakedCovers[Index]);
        const FColor Color = !bValid ? FColor::Red : (Cover.bCrouched ? FColor::Cyan : FColor::Yellow);
        const FVector Eye = Cover.GetEye(AgentProfile);
        DrawDebugPoint(GetWorld(), Cover.Position, 9.f, Color, false, 0.f);
        DrawDebugLine(GetWorld(), Cover.Position, Eye, Color, false, 0.f, 0, 1.5f);
        DrawDebugDirectionalArrow(GetWorld(), Cover.Position + FVector(0, 0, 8), Cover.Position + FVector(0, 0, 8) + Cover.WallDirection * 45.f, 12.f, Color, false, 0.f, 0, 1.5f);
        for (const ECSOPeek Peek : { ECSOPeek::Stand, ECSOPeek::Left, ECSOPeek::Right })
        {
            if ((Cover.PeekMask & static_cast<int32>(Peek)) == 0) { continue; }
            const FVector PeekEye = Cover.GetPeekEye(AgentProfile, Peek);
            DrawDebugLine(GetWorld(), Eye, PeekEye, FColor::Green, false, 0.f, 0, 2.f);
            DrawDebugDirectionalArrow(GetWorld(), PeekEye, PeekEye + Cover.WallDirection * 70.f, 12.f, FColor::Green, false, 0.f, 0, 2.f);
        }
    }
}
