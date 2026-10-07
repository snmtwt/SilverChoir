#include "CSOCoverVolume.h"

#include "CSOCoverSubsystem.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
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
    constexpr int32 CSOGenerationVersion = 2;
    TAutoConsoleVariable<int32> CVarCSODebugBake(TEXT("cso.DebugBake"), 0,
        TEXT("Log bounded cover bake candidate, collision, silhouette and peek diagnostics. 0=off, 1=on."));
    TAutoConsoleVariable<int32> CVarCSODebugBakeMaxLines(TEXT("cso.DebugBakeMaxLines"), 10000,
        TEXT("Maximum detailed diagnostic lines per bake when cso.DebugBake is enabled (1..200000)."));

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
        bool bDebugBake = false;
        int32 DebugLineLimit = 0;
        mutable int32 DebugLines = 0;

        FCSOBakeContext(UWorld& InWorld, const ACSOCoverVolume& InVolume, UNavigationSystemV1& InNavigation, const ARecastNavMesh& InNavMesh)
            : World(InWorld), Volume(InVolume), Navigation(InNavigation), NavMesh(InNavMesh)
            , Collision(SCENE_QUERY_STAT(CSOCoverBake), InVolume.bTraceComplex, &InVolume)
        {
            Collision.bReturnPhysicalMaterial = false;
            Collision.bReturnFaceIndex = false;
            Collision.bIgnoreTouches = true;
            bDebugBake = CVarCSODebugBake.GetValueOnGameThread() != 0;
            DebugLineLimit = FMath::Clamp(CVarCSODebugBakeMaxLines.GetValueOnGameThread(), 1, 200000);
            // Authored preview NPCs must not become permanent cover or invalidate otherwise usable space.
            for (TActorIterator<APawn> It(&World); It; ++It) { Collision.AddIgnoredActor(*It); }
        }

        void Debug(const TCHAR* Stage, const FVector& Point, const FString& Detail = FString()) const
        {
            if (!bDebugBake || DebugLines >= DebugLineLimit) { return; }
            ++DebugLines;
            UE_LOG(LogCSOCoverBake, Display, TEXT("[CSO Bake] %s P=(%.3f,%.3f,%.3f) %s"), Stage, Point.X, Point.Y, Point.Z, *Detail);
            if (DebugLines == DebugLineLimit) { UE_LOG(LogCSOCoverBake, Display, TEXT("[CSO Bake] Diagnostic line limit reached.")); }
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
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Profile.Radius + Profile.Clearance, PaddedHalfHeight);
            const bool bFits = !World.OverlapBlockingTestByChannel(Center, FQuat::Identity, Volume.MovementTraceChannel, Shape, MovementCollision);
            if (!bFits && bDebugBake)
            {
                Debug(TEXT("Reject/Capsule"), Feet, FString::Printf(TEXT("R=%.2f HH=%.2f CenterZ=%.2f"), Shape.GetCapsuleRadius(), PaddedHalfHeight, Center.Z));
                TArray<FOverlapResult> Overlaps;
                World.OverlapMultiByChannel(Overlaps, Center, FQuat::Identity, Volume.MovementTraceChannel, Shape, MovementCollision);
                for (const FOverlapResult& Overlap : Overlaps)
                {
                    if (Overlap.bBlockingHit) { Debug(TEXT("CapsuleBlocker"), Feet, GetPathNameSafe(Overlap.GetComponent())); }
                }
            }
            return bFits;
        }

        bool FindGroundedFeet(const FVector& Point, FVector& OutFeet, bool bKeepHorizontalPosition = false) const
        {
            FNavLocation Projected;
            if (!Navigation.ProjectPointToNavigation(Point, Projected, FVector(20, 20, 100), &NavMesh))
            {
                Debug(TEXT("Reject/NavProjection"), Point);
                return false;
            }
            if (bKeepHorizontalPosition)
            {
                // A fixed corner anchor must itself lie on navigation. Projection may supply its height,
                // but must not slide the feet along the wall and silently change the animation distance.
                if (FVector::DistSquared2D(Point, Projected.Location) > FMath::Square(0.25))
                {
                    Debug(TEXT("Reject/CornerOffNavigation"), Point, Projected.Location.ToString());
                    return false;
                }
                Projected.Location.X = Point.X;
                Projected.Location.Y = Point.Y;
            }
            FCollisionQueryParams MovementCollision(Collision);
            MovementCollision.bTraceComplex = false;
            FHitResult Floor;
            if (!World.LineTraceSingleByChannel(Floor, Projected.Location + FVector(0, 0, 40), Projected.Location - FVector(0, 0, 150), Volume.MovementTraceChannel, MovementCollision))
            {
                Debug(TEXT("Reject/NoFloor"), Projected.Location);
                return false;
            }
            if (Floor.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(Volume.MaxFloorSlopeDegrees)))
            {
                Debug(TEXT("Reject/FloorSlope"), Floor.ImpactPoint, Floor.ImpactNormal.ToString());
                return false;
            }
            OutFeet = Floor.ImpactPoint + FVector(0, 0, 2);
            const FVector LocalFeet = Volume.GenerationBounds->GetComponentTransform().InverseTransformPosition(OutFeet);
            if (!FBox(-Volume.GenerationBounds->GetUnscaledBoxExtent(), Volume.GenerationBounds->GetUnscaledBoxExtent()).IsInsideOrOn(LocalFeet))
            {
                Debug(TEXT("Reject/FeetOutsideVolume"), OutFeet);
                return false;
            }
            if (FMath::Abs(OutFeet.Z - Projected.Location.Z) > 45.f)
            {
                Debug(TEXT("Reject/FloorHeightMismatch"), OutFeet, Projected.Location.ToString());
                return false;
            }
            if (!CapsuleFits(OutFeet, Volume.AgentProfile.CrouchHalfHeight)) { return false; }
            Debug(TEXT("GroundedFeet"), OutFeet);
            return true;
        }

        bool ProvidesBodyCover(const FCSOBakedCover& Cover) const
        {
            const FVector& Feet = Cover.Position;
            const FVector& Direction = Cover.WallDirection;
            const float BodyHeight = Cover.GetBodyHeight(Volume.AgentProfile);
            const float EyeHeight = Cover.GetEyeHeight(Volume.AgentProfile);
            const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction);
            const float Width = Volume.AgentProfile.Radius * 0.8f;
            // Silhouette samples include shoulders and head. Do not infer safety from just a single eye ray.
            // The optional low pose changes this animated silhouette, never the physical movement capsule.
            const float Heights[] = { FMath::Min(20.f, BodyHeight * 0.25f), BodyHeight * 0.5f, EyeHeight, BodyHeight - 2.f };
            const float Offsets[] = { 0.f, -Width, Width };
            for (const float Height : Heights)
            {
                for (const float Side : Offsets)
                {
                    const FVector Start = Feet + FVector(0, 0, Height) + Right * Side;
                    if (!Blocked(Start, Start + Direction * Volume.WallSearchDistance))
                    {
                        if (bDebugBake) { Debug(TEXT("Reject/BodyRayOpen"), Start, FString::Printf(TEXT("Pose=%d Height=%.2f Side=%.2f Dir=%s"), int32(Cover.GetStance()), Height, Side, *Direction.ToString())); }
                        return false;
                    }
                }
            }
            return true;
        }

        bool FindSideEdge(const FCSOBakedCover& Cover, ECSOPeek Peek, float& OutEdgeDistance) const
        {
            const FVector Eye = Cover.GetEye(Volume.AgentProfile);
            const FVector Side = Cover.GetRight() * (Peek == ECSOPeek::Left ? -1.f : 1.f);
            const float MaxDistance = Volume.MaxSidePeekDistance - Volume.AgentProfile.LeanDistance;
            if (MaxDistance <= 0.f || !Blocked(Eye, Eye + Cover.WallDirection * Volume.PeekProbeDistance)) { return false; }
            float LastBlocked = 0.f;
            const int32 Steps = FMath::CeilToInt(MaxDistance / Volume.SideEdgeSearchStep);
            for (int32 Step = 1; Step <= Steps; ++Step)
            {
                const float Distance = FMath::Min(MaxDistance, Step * Volume.SideEdgeSearchStep);
                const FVector Probe = Eye + Side * Distance;
                if (Blocked(Probe, Probe + Cover.WallDirection * Volume.PeekProbeDistance))
                {
                    LastBlocked = Distance;
                    continue;
                }
                // Keep the open side of the bracket so the final firing point is conservatively beyond
                // the actual silhouette. Coarse sample spacing never becomes an animation offset.
                float FirstOpen = Distance;
                while (FirstOpen - LastBlocked > 0.125f)
                {
                    const float Middle = (LastBlocked + FirstOpen) * 0.5f;
                    const FVector MiddleProbe = Eye + Side * Middle;
                    if (Blocked(MiddleProbe, MiddleProbe + Cover.WallDirection * Volume.PeekProbeDistance)) { LastBlocked = Middle; }
                    else { FirstOpen = Middle; }
                }
                OutEdgeDistance = FirstOpen;
                return true;
            }
            if (bDebugBake) { Debug(TEXT("Reject/NoSideEdge"), Eye, FString::Printf(TEXT("Peek=%d Max=%.2f Dir=%s"), int32(Peek), MaxDistance, *Cover.WallDirection.ToString())); }
            return false;
        }

        bool IsPeekClear(const FCSOBakedCover& Cover, ECSOPeek Peek) const
        {
            const FVector Eye = Cover.GetEye(Volume.AgentProfile);
            const FVector PeekEye = Cover.GetPeekEye(Volume.AgentProfile, Peek);
            const bool bHeadPathClear = SweepClear(Eye, PeekEye, true);
            const bool bAimClear = bHeadPathClear && SweepClear(PeekEye, PeekEye + Cover.WallDirection * Volume.PeekProbeDistance);
            if (!bAimClear && bDebugBake)
            {
                Debug(bHeadPathClear ? TEXT("Reject/PeekForward") : TEXT("Reject/HeadPath"), PeekEye, FString::Printf(TEXT("Peek=%d From=%s"), int32(Peek), *Eye.ToString()));
            }
            return bAimClear;
        }

        bool HasStandPeek(const FCSOBakedCover& Cover) const
        {
            return Cover.bCrouched && CapsuleFits(Cover.Position, Volume.AgentProfile.StandHalfHeight) && IsPeekClear(Cover, ECSOPeek::Stand);
        }

        bool MakeCornerAnchor(const FCSOBakedCover& Seed, ECSOPeek Peek, FCSOBakedCover& OutCover) const
        {
            const float FiringDistance = Volume.CornerInsetDistance + Volume.AgentProfile.LeanDistance;
            if (FiringDistance > Volume.MaxSidePeekDistance) { return false; }
            const FVector Side = Seed.GetRight() * (Peek == ECSOPeek::Left ? -1.f : 1.f);
            // Only wall-normal clearance may vary. Every attempted normal offset measures the actual
            // edge again: a pillar, angled return wall, or sloping floor can change its silhouette.
            constexpr int32 MaxNormalAttempts = 9;
            for (int32 NormalAttempt = 0; NormalAttempt < MaxNormalAttempts; ++NormalAttempt)
            {
                FCSOBakedCover ProbeCover = Seed;
                const FVector NormalPoint = Seed.Position - Seed.WallDirection * (NormalAttempt * 8.f);
                if (!FindGroundedFeet(NormalPoint, ProbeCover.Position, true)) { continue; }
                float EdgeDistance = 0.f;
                if (!FindSideEdge(ProbeCover, Peek, EdgeDistance)) { continue; }
                FVector AnchorPoint = ProbeCover.Position + Side * (EdgeDistance - Volume.CornerInsetDistance);
                // Re-grounding may change the eye height on a slope. Re-lock to that final silhouette,
                // with a strict bounded correction count; never accept a drifting fallback point.
                for (int32 Correction = 0; Correction < 3; ++Correction)
                {
                    FCSOBakedCover Anchor = Seed;
                    if (!FindGroundedFeet(AnchorPoint, Anchor.Position, true) || !FindSideEdge(Anchor, Peek, EdgeDistance)) { break; }
                    const float Error = EdgeDistance - Volume.CornerInsetDistance;
                    if (FMath::Abs(Error) > 0.25f)
                    {
                        AnchorPoint = Anchor.Position + Side * Error;
                        continue;
                    }
                    if ((!Anchor.bCrouched && !CapsuleFits(Anchor.Position, Volume.AgentProfile.StandHalfHeight)) || !ProvidesBodyCover(Anchor)) { break; }
                    Anchor.LeftPeekDistance = Peek == ECSOPeek::Left ? FiringDistance : 0.f;
                    Anchor.RightPeekDistance = Peek == ECSOPeek::Right ? FiringDistance : 0.f;
                    Anchor.PeekMask = int32(Peek);
                    if (!IsPeekClear(Anchor, Peek)) { break; }
                    if (HasStandPeek(Anchor)) { Anchor.PeekMask |= int32(ECSOPeek::Stand); }
                    if (bDebugBake) { Debug(TEXT("CornerAnchor"), Anchor.Position, FString::Printf(TEXT("Peek=%d Inset=%.3f Measured=%.3f NormalOffset=%.2f"), int32(Peek), Volume.CornerInsetDistance, EdgeDistance, NormalAttempt * 8.f)); }
                    OutCover = Anchor;
                    return true;
                }
            }
            return false;
        }

        void AcceptCover(const FCSOBakedCover& Cover)
        {
            if (Accepted.Num() >= Volume.MaxCoverPoints) { return; }
            if (IsDuplicate(Cover)) { Debug(TEXT("Reject/Duplicate"), Cover.Position); return; }
            if (bDebugBake) { Debug(TEXT("Accepted"), Cover.Position, FString::Printf(TEXT("Pose=%d Mask=%d Dir=%s"), int32(Cover.GetStance()), Cover.PeekMask, *Cover.WallDirection.ToString())); }
            const int32 Index = Accepted.Add(Cover);
            AcceptedCells.FindOrAdd(CSOCover::CellFor(Cover.Position, FMath::Max(5.f, Volume.MinCoverSpacing))).Add(Index);
        }

        int32 GenerateAnchors(const FCSOBakedCover& Seed, int32 AllowedSides = int32(ECSOPeek::Left) | int32(ECSOPeek::Right))
        {
            int32 UsablePeeks = 0;
            for (const ECSOPeek Peek : { ECSOPeek::Left, ECSOPeek::Right })
            {
                if (!(AllowedSides & int32(Peek))) { continue; }
                FCSOBakedCover Anchor;
                if (MakeCornerAnchor(Seed, Peek, Anchor))
                {
                    UsablePeeks |= Anchor.PeekMask;
                    AcceptCover(Anchor);
                }
            }
            // Interior low-wall points remain useful for standing to fire. They deliberately contain
            // no lateral direction unless a separately validated fixed corner anchor was generated.
            if (HasStandPeek(Seed))
            {
                FCSOBakedCover StandingPeek = Seed;
                StandingPeek.LeftPeekDistance = 0.f;
                StandingPeek.RightPeekDistance = 0.f;
                StandingPeek.PeekMask = int32(ECSOPeek::Stand);
                AcceptCover(StandingPeek);
                UsablePeeks |= int32(ECSOPeek::Stand);
            }
            return UsablePeeks;
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
                            Existing.bCrouched == Cover.bCrouched && Existing.bLowCrouched == Cover.bLowCrouched &&
                            (Existing.PeekMask & Cover.PeekMask) == Cover.PeekMask)
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
            if (bDebugBake) { Debug(TEXT("Sample"), EdgePoint, EdgeDirection.ToString()); }
            const FVector Local = Volume.GenerationBounds->GetComponentTransform().InverseTransformPosition(EdgePoint);
            if (!FBox(-Volume.GenerationBounds->GetUnscaledBoxExtent(), Volume.GenerationBounds->GetUnscaledBoxExtent()).IsInsideOrOn(Local))
            {
                return;
            }

            const FVector Perpendicular = FVector::CrossProduct(FVector::UpVector, EdgeDirection).GetSafeNormal2D();
            FVector Feet;
            if (!FindGroundedFeet(EdgePoint, Feet))
            {
                // Recast's simplified boundary may slope inside the required capsule clearance near a wall end.
                // A single safety-margin offset can miss every otherwise usable candidate on one wall face.
                // Search both normals at each increasing distance, retaining nav/floor/capsule validation.
                // Eight attempts per normal bound editor work; runtime queries never perform this search.
                const float FirstInset = FMath::Max(0.f, Volume.AgentProfile.Radius - NavMesh.GetConfig().AgentRadius) + Volume.AgentProfile.Clearance + 5.f;
                constexpr int32 MaxInsetAttempts = 8;
                constexpr float InsetStep = 8.f;
                bool bFoundClearance = false;
                for (int32 Attempt = 0; Attempt < MaxInsetAttempts; ++Attempt)
                {
                    const float Inset = FirstInset + Attempt * InsetStep;
                    if (FindGroundedFeet(EdgePoint + Perpendicular * Inset, Feet) || FindGroundedFeet(EdgePoint - Perpendicular * Inset, Feet))
                    {
                        if (bDebugBake) { Debug(TEXT("Recovered/BoundaryClearance"), Feet, FString::Printf(TEXT("Inset=%.2f Edge=%s"), Inset, *EdgePoint.ToString())); }
                        bFoundClearance = true;
                        break;
                    }
                }
                if (!bFoundClearance)
                {
                    return;
                }
            }
            for (const float Sign : { 1.f, -1.f })
            {
                const FCSOAgentProfile& Profile = Volume.AgentProfile;
                const float ProbeHeights[] = { Profile.CrouchEyeHeight, Profile.LowCrouchEyeHeight };
                const int32 ProbeCount = Profile.bEnableLowCrouch ? 2 : 1;
                for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
                {
                    FHitResult Wall;
                    const FVector ProbeStart = Feet + FVector(0, 0, ProbeHeights[ProbeIndex]);
                    if (!World.LineTraceSingleByChannel(Wall, ProbeStart, ProbeStart + Perpendicular * Sign * Volume.WallSearchDistance, Volume.GeometryTraceChannel, Collision) ||
                        FMath::Abs(Wall.ImpactNormal.Z) > 0.5f)
                    {
                        if (bDebugBake) { Debug(TEXT("Reject/NoWall"), ProbeStart, (Perpendicular * Sign).ToString()); }
                        continue;
                    }
                    if (bDebugBake) { Debug(TEXT("WallHit"), Wall.ImpactPoint, FString::Printf(TEXT("Normal=%s Component=%s"), *Wall.ImpactNormal.ToString(), *GetPathNameSafe(Wall.GetComponent()))); }
                    FVector Direction = (-Wall.ImpactNormal).GetSafeNormal2D();
                    // Double-sided procedural triangles may return a geometric normal with either winding.
                    // Orient the frame toward the actual hit, so the opposite wall face is never probed away from the obstacle.
                    if (FVector::DotProduct(Direction, Wall.ImpactPoint - ProbeStart) < 0.f) { Direction = -Direction; }
                    if (Direction.IsNearlyZero()) { continue; }
                    FCSOBakedCover Cover;
                    Cover.Position = Feet;
                    Cover.WallDirection = Direction;
                    Cover.bCrouched = true;
                    const bool bNormalCrouchHidden = ProvidesBodyCover(Cover);
                    if (!bNormalCrouchHidden)
                    {
                        if (!Profile.bEnableLowCrouch) { continue; }
                        Cover.bLowCrouched = true;
                        if (!ProvidesBodyCover(Cover)) { continue; }
                    }
                    const bool bStandingFits = CapsuleFits(Feet, Profile.StandHalfHeight);
                    if (bNormalCrouchHidden && bStandingFits)
                    {
                        Cover.bCrouched = false;
                        if (!ProvidesBodyCover(Cover)) { Cover.bCrouched = true; }
                    }
                    int32 UsablePeeks = GenerateAnchors(Cover);
                    const int32 SidePeeks = int32(ECSOPeek::Left) | int32(ECSOPeek::Right);
                    // An opening can exist only at crouched height on one end. A valid standing anchor
                    // on the opposite end must not suppress this direction's independent fallback.
                    if (!Cover.bCrouched && (UsablePeeks & SidePeeks) != SidePeeks)
                    {
                        Cover.bCrouched = true;
                        // Stance changes the eye-height silhouette, so regenerate and re-anchor the corner.
                        UsablePeeks |= GenerateAnchors(Cover, SidePeeks & ~UsablePeeks);
                    }
                    if (UsablePeeks == 0) { Debug(TEXT("Reject/NoPeek"), Feet); continue; }
                    if (Accepted.Num() >= Volume.MaxCoverPoints) { return; }
                    break; // A valid stance for this wall direction already exists; the second probe adds no distinct support.
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
    return bHasBake && BakedGenerationVersion == CSOGenerationVersion && IsBakeTransformValid() && AgentProfile.IsValid() && ProfilesEqual(AgentProfile, BakedProfile) &&
        GetActorTransform().Equals(BakeTransform, 0.01) &&
        GenerationBounds->GetComponentTransform().Equals(BakeBoundsTransform, 0.01) &&
        GenerationBounds->GetUnscaledBoxExtent().Equals(BakeBoundsExtent, 0.01) &&
        GeometryTraceChannel == BakedTraceChannel && MovementTraceChannel == BakedMovementTraceChannel && bTraceComplex == bBakedTraceComplex &&
        MaxSidePeekDistance == BakedMaxSidePeekDistance && SideEdgeSearchStep == BakedSideEdgeSearchStep && CornerInsetDistance == BakedCornerInsetDistance;
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
        !FMath::IsFinite(CornerInsetDistance) || CornerInsetDistance < 0.f || CornerInsetDistance > 500.f ||
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
        if (Context.bDebugBake) { Context.Debug(TEXT("NavEdge"), Start, FString::Printf(TEXT("End=%s"), *End.ToString())); }
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
    BakedCornerInsetDistance = CornerInsetDistance;
    BakedGenerationVersion = CSOGenerationVersion;
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
    UWorld* World = GetWorld();
    if (!World) { return; }
    // Editor worlds have no cover runtime subsystem or player controller. Preview
    // serialized bake data here so the same console switch works before Play.
    static const IConsoleVariable* GlobalDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("cso.Debug"));
    const bool bGlobalEditorPreview = World->WorldType == EWorldType::Editor && GlobalDebug && GlobalDebug->GetInt() != 0;
    if (!bDrawDebug && !bGlobalEditorPreview) { return; }
    const bool bValid = IsBakeDataValid();
    int32 Limit = FMath::Clamp(MaxDebugPoints, 1, 10000);
    if (bGlobalEditorPreview)
    {
        static const IConsoleVariable* GlobalLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("cso.DebugMaxPoints"));
        if (GlobalLimit) Limit = FMath::Min(Limit, FMath::Clamp(GlobalLimit->GetInt(), 1, 2048));
    }
    const int32 Count = FMath::Min(BakedCovers.Num(), Limit);
    // Baked points deliberately sit behind walls: editor inspection should show
    // them through the occluder, even when looking from the enemy-facing side.
    const uint8 Depth = World->WorldType == EWorldType::Editor ? SDPG_Foreground : SDPG_World;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FCSOBakedCover Cover = GetWorldCover(BakedCovers[Index]);
        const FColor Color = !bValid ? FColor::Red : (Cover.bLowCrouched ? FColor(160, 80, 255) : (Cover.bCrouched ? FColor::Cyan : FColor::Yellow));
        const FVector Eye = Cover.GetEye(AgentProfile);
        DrawDebugPoint(World, Cover.Position, 9.f, Color, false, 0.f, Depth);
        DrawDebugLine(World, Cover.Position, Eye, Color, false, 0.f, Depth, 1.5f);
        DrawDebugDirectionalArrow(World, Cover.Position + FVector(0, 0, 8), Cover.Position + FVector(0, 0, 8) + Cover.WallDirection * 45.f, 12.f, Color, false, 0.f, Depth, 1.5f);
        for (const ECSOPeek Peek : { ECSOPeek::Stand, ECSOPeek::Left, ECSOPeek::Right })
        {
            if ((Cover.PeekMask & static_cast<int32>(Peek)) == 0) { continue; }
            const FVector PeekEye = Cover.GetPeekEye(AgentProfile, Peek);
            DrawDebugLine(World, Eye, PeekEye, FColor::Green, false, 0.f, Depth, 2.f);
            DrawDebugDirectionalArrow(World, PeekEye, PeekEye + Cover.WallDirection * 70.f, 12.f, FColor::Green, false, 0.f, Depth, 2.f);
        }
    }
}
