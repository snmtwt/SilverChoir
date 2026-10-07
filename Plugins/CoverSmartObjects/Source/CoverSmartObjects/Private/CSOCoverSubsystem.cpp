#include "CSOCoverSubsystem.h"

#include "CSOCoverBehaviorDefinition.h"
#include "CSOCoverVolume.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoverSmartObjects, Log, All);

namespace
{
    TAutoConsoleVariable<int32> CVarDebug(TEXT("cso.Debug"), 0, TEXT("Draw baked covers in realtime editor viewports and registered nearby covers during Play. 0=off, 1=on. Per-volume Draw Debug remains independent."));
    TAutoConsoleVariable<float> CVarDebugRadius(TEXT("cso.DebugRadius"), 5000.f, TEXT("Cover debug distance from the player camera in cm."));
    TAutoConsoleVariable<int32> CVarDebugMaxPoints(TEXT("cso.DebugMaxPoints"), 256, TEXT("Maximum visible cover points per runtime update, or per volume in editor preview."));
    TAutoConsoleVariable<int32> CVarDebugPartitions(TEXT("cso.DebugPartitions"), 0, TEXT("Draw spatial hash cells alongside cover points."));
    TAutoConsoleVariable<int32> CVarDebugLabels(TEXT("cso.DebugLabels"), 0, TEXT("Draw cover IDs, occupancy and remaining lease seconds."));

    double LeaseDuration(float Seconds)
    {
        return FMath::IsFinite(Seconds) ? FMath::Clamp(double(Seconds), 0.1, 3600.0) : 10.0;
    }
}

void UCSOCoverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<USmartObjectSubsystem>();
    DefaultDefinition = NewObject<USmartObjectDefinition>(this, NAME_None, RF_Transient);
    FSmartObjectSlotDefinition& Slot = DefaultDefinition->DebugAddSlot();
    Slot.BehaviorDefinitions.Add(NewObject<UCSOCoverBehaviorDefinition>(DefaultDefinition));
    DefaultDefinition->Validate();
}

void UCSOCoverSubsystem::Deinitialize()
{
    TArray<int64> Ids;
    Covers.GetKeys(Ids);
    for (const int64 Id : Ids) RemoveCover(Id);
    VolumeCovers.Reset();
    Reservations.Reset();
    ReservedSlots.Reset();
    ReservedCells.Reset();
    Cells.Reset();
    DefaultDefinition = nullptr;
    Super::Deinitialize();
}

bool UCSOCoverSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

USmartObjectSubsystem* UCSOCoverSubsystem::GetSmartObjects() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<USmartObjectSubsystem>() : nullptr;
}

void UCSOCoverSubsystem::RegisterVolume(ACSOCoverVolume* Volume)
{
    check(IsInGameThread());
    if (!IsValid(Volume) || Volume->GetWorld() != GetWorld() || !Volume->HasActorBegunPlay()) return;
    UnregisterVolume(Volume);
    USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    const IConsoleVariable* Disabled = IConsoleManager::Get().FindConsoleVariable(TEXT("ai.smartobject.DisableRuntime"));
    if (!SmartObjects || (Disabled && Disabled->GetInt() != 0)) return;
    if (!Volume->IsBakeDataValid() || !Volume->AgentProfile.IsValid())
    {
        UE_LOG(LogCoverSmartObjects, Warning, TEXT("Cover volume %s has stale or missing bake data; bake it again before play."), *GetNameSafe(Volume));
        return;
    }

    USmartObjectDefinition* Definition = Volume->SmartObjectDefinition ? Volume->SmartObjectDefinition.Get() : DefaultDefinition.Get();
    if (!Definition || !Definition->Validate() || Definition->GetSlots().Num() != 1
        || !FVector(Definition->GetSlot(0).Offset).IsNearlyZero()
        || !FRotator(Definition->GetSlot(0).Rotation).IsNearlyZero()
        || !Definition->GetUserTagFilter().IsEmpty() || Definition->GetPreconditions().IsValid()
        || !Definition->GetSlot(0).UserTagFilter.IsEmpty() || Definition->GetSlot(0).SelectionPreconditions.IsValid())
    {
        UE_LOG(LogCoverSmartObjects, Error, TEXT("Cover volume %s needs a valid unconditional one-slot Smart Object definition with zero slot offset/rotation and no user tag filters."), *GetNameSafe(Volume));
        return;
    }

    MaxRegisteredRadius = FMath::Max(MaxRegisteredRadius, double(Volume->AgentProfile.Radius + Volume->AgentProfile.Clearance));
    MaxRegisteredHeight = FMath::Max(MaxRegisteredHeight, double(Volume->AgentProfile.StandHalfHeight) * 2.0);

    TArray<int64>& VolumeIds = VolumeCovers.FindOrAdd(Volume);
    VolumeIds.Reserve(Volume->BakedCovers.Num());
    for (const FCSOBakedCover& LocalCover : Volume->BakedCovers)
    {
        FCSORuntimeCover Record;
        Record.Data = Volume->GetWorldCover(LocalCover);
        if (Record.Data.Position.ContainsNaN() || Record.Data.WallDirection.ContainsNaN()
            || Record.Data.Position.GetAbsMax() > 1.e9 || Record.Data.WallDirection.IsNearlyZero()
            || !FMath::IsFinite(Record.Data.LeftPeekDistance) || !FMath::IsFinite(Record.Data.RightPeekDistance)
            || Record.Data.LeftPeekDistance < 0.f || Record.Data.RightPeekDistance < 0.f
            || Record.Data.LeftPeekDistance > 10000.f || Record.Data.RightPeekDistance > 10000.f
            || (Record.Data.bLowCrouched && (!Record.Data.bCrouched || !Volume->AgentProfile.bEnableLowCrouch))) continue;
        Record.Data.WallDirection = Record.Data.WallDirection.GetSafeNormal2D();
        if (Record.Data.WallDirection.IsNearlyZero()) continue;
        Record.Profile = Volume->AgentProfile;
        Record.Volume = Volume;
        const FTransform Transform(Record.Data.WallDirection.Rotation(), Record.Data.Position);
        Record.SmartObjectHandle = SmartObjects->CreateSmartObject(*Definition, Transform, {});
        if (!Record.SmartObjectHandle.IsValid()) continue;
        TArray<FSmartObjectSlotHandle> Slots;
        SmartObjects->GetAllSlots(Record.SmartObjectHandle, Slots);
        if (Slots.Num() != 1)
        {
            SmartObjects->DestroySmartObject(Record.SmartObjectHandle);
            continue;
        }
        Record.SlotHandle = Slots[0];
        Record.Id = NextId++;
        VolumeIds.Add(Record.Id);
        Cells.FindOrAdd(CSOCover::CellFor(Record.Data.Position, CellSize)).Add(Record.Id);
        Covers.Add(Record.Id, MoveTemp(Record));
    }
    UE_LOG(LogCoverSmartObjects, Log, TEXT("Registered %d cover Smart Objects from %s (%d total, %d cells)."),
        VolumeIds.Num(), *GetNameSafe(Volume), Covers.Num(), Cells.Num());
}

void UCSOCoverSubsystem::UnregisterVolume(ACSOCoverVolume* Volume)
{
    check(IsInGameThread());
    TArray<int64> Ids;
    if (VolumeCovers.RemoveAndCopyValue(Volume, Ids))
    {
        for (const int64 Id : Ids) RemoveCover(Id);
    }
}

void UCSOCoverSubsystem::RefreshVolume(ACSOCoverVolume* Volume)
{
    RegisterVolume(Volume);
}

void UCSOCoverSubsystem::RemoveCover(int64 CoverId)
{
    FCSORuntimeCover Record;
    if (!Covers.RemoveAndCopyValue(CoverId, Record)) return;
    const FIntVector Cell = CSOCover::CellFor(Record.Data.Position, CellSize);
    if (TArray<int64>* Ids = Cells.Find(Cell))
    {
        Ids->RemoveSwap(CoverId);
        if (Ids->IsEmpty()) Cells.Remove(Cell);
    }
    ForgetReservation(CoverId);
    if (USmartObjectSubsystem* SmartObjects = GetSmartObjects())
    {
        // Destruction aborts active claims and dispatches the engine's invalidation callbacks.
        if (SmartObjects->IsSmartObjectSlotValid(Record.SlotHandle)) SmartObjects->DestroySmartObject(Record.SmartObjectHandle);
    }
    if (Covers.IsEmpty()) { MaxRegisteredRadius = 0.0; MaxRegisteredHeight = 0.0; }
}

int32 UCSOCoverSubsystem::InvalidateCoversInBounds(const FBox& Bounds)
{
    check(IsInGameThread());
    if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()) return 0;
    TArray<int64> Removed;
    // Invalidation is a rare geometry event, outside the frequently executed query path.
    for (const TPair<int64, FCSORuntimeCover>& Pair : Covers)
    {
        const FCSORuntimeCover& Cover = Pair.Value;
        const double WallReach = Cover.Volume.IsValid() ? double(Cover.Volume->WallSearchDistance) : 0.0;
        const double SideReach = FMath::Max(double(Cover.Profile.LeanDistance),
            FMath::Max(double(Cover.Data.LeftPeekDistance), double(Cover.Data.RightPeekDistance)));
        const double Padding = Cover.Profile.Radius + Cover.Profile.Clearance + FMath::Max(WallReach, SideReach);
        const FVector Min = Cover.Data.Position - FVector(Padding, Padding, Cover.Profile.Clearance);
        const FVector Max = Cover.Data.Position + FVector(Padding, Padding, Cover.Profile.StandHalfHeight * 2.f);
        if (Bounds.Intersect(FBox(Min, Max))) Removed.Add(Pair.Key);
    }
    for (const int64 Id : Removed)
    {
        if (const FCSORuntimeCover* Cover = Covers.Find(Id))
            if (TArray<int64>* Ids = VolumeCovers.Find(Cover->Volume)) Ids->RemoveSwap(Id);
        RemoveCover(Id);
    }
    return Removed.Num();
}

bool UCSOCoverSubsystem::IsReservationValid(const FCSOCoverReservation& Reservation) const
{
    return Reservation.User.IsValid() && GetWorld() && Reservation.ExpiresAt > GetWorld()->GetTimeSeconds()
        && IsReservationOwnerMatches(Reservation, true);
}

bool UCSOCoverSubsystem::IsReservationOwnerMatches(const FCSOCoverReservation& Reservation, bool bRequireEnabled) const
{
    const USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    if (!SmartObjects || !SmartObjects->IsSmartObjectSlotValid(Reservation.Claim.SlotHandle)) return false;
    bool bMatches = false;
    SmartObjects->ReadSlotData(Reservation.Claim.SlotHandle, [&Reservation, &bMatches, bRequireEnabled](FConstSmartObjectSlotView View)
    {
        const FCSOCoverClaimUserData* Data = View.GetUserData().GetPtr<const FCSOCoverClaimUserData>();
        bMatches = (!bRequireEnabled || View.IsEnabled())
            && (View.GetState() == ESmartObjectSlotState::Claimed || View.GetState() == ESmartObjectSlotState::Occupied)
            && Data && Data->ReservationToken == Reservation.Token;
    });
    return bMatches;
}

bool UCSOCoverSubsystem::IsCoverAvailable(const FCSORuntimeCover& Cover, const AActor* User) const
{
    const USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    if (!Cover.Volume.IsValid() || !Cover.Volume->IsBakeDataValid() || !SmartObjects || !SmartObjects->IsSmartObjectSlotValid(Cover.SlotHandle)
        || !SmartObjects->IsEnabled(Cover.SmartObjectHandle)) return false;
    if (const FCSOCoverReservation* Reservation = Reservations.Find(Cover.Id))
        return Reservation->User.Get() == User && IsReservationValid(*Reservation);
    // Do not steal another system's lower priority claim.
    return SmartObjects->GetSlotState(Cover.SlotHandle) == ESmartObjectSlotState::Free
        && SmartObjects->CanBeClaimed(Cover.SlotHandle, ESmartObjectClaimPriority::Normal)
        && !HasReservationOverlap(Cover);
}

bool UCSOCoverSubsystem::HasReservationOverlap(const FCSORuntimeCover& Cover) const
{
    if (Reservations.IsEmpty()) return false;
    auto Overlaps = [this, &Cover](int64 OtherId)
    {
        if (OtherId == Cover.Id) return false;
        const FCSOCoverReservation* Reservation = Reservations.Find(OtherId);
        if (!Reservation) return false;
        const FCSORuntimeCover* Other = Covers.Find(OtherId);
        if (!Other) return false;
        const double Radius = Cover.Profile.Radius + Cover.Profile.Clearance + Other->Profile.Radius + Other->Profile.Clearance;
        const bool bHorizontal = FVector::DistSquared2D(Cover.Data.Position, Other->Data.Position) < FMath::Square(Radius);
        const double Bottom = FMath::Max(Cover.Data.Position.Z, Other->Data.Position.Z);
        const double Top = FMath::Min(Cover.Data.Position.Z + double(Cover.Profile.StandHalfHeight) * 2.0,
            Other->Data.Position.Z + double(Other->Profile.StandHalfHeight) * 2.0);
        return bHorizontal && Bottom < Top && IsReservationValid(*Reservation);
    };

    const double Radius = Cover.Profile.Radius + Cover.Profile.Clearance + MaxRegisteredRadius;
    const FVector Extent(Radius, Radius, MaxRegisteredHeight);
    // Extremely large authoring profiles must not cause unbounded cell traversal.
    if (Reservations.Num() < 32 || Radius > CellSize * 8.0 || MaxRegisteredHeight > CellSize * 8.0)
    {
        for (const auto& Pair : Reservations) if (Overlaps(Pair.Key)) return true;
        return false;
    }
    const FIntVector Min = CSOCover::CellFor(Cover.Data.Position - Extent, CellSize);
    const FIntVector Max = CSOCover::CellFor(Cover.Data.Position + Extent, CellSize);
    for (int32 X = Min.X; X <= Max.X; ++X)
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
    for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
        if (const TArray<int64>* Ids = ReservedCells.Find(FIntVector(X, Y, Z)))
            for (const int64 Id : *Ids) if (Overlaps(Id)) return true;
    return false;
}

FSmartObjectClaimHandle UCSOCoverSubsystem::ClaimCover(int64 CoverId, AActor* User, float LeaseSeconds)
{
    check(IsInGameThread());
    if (!IsValid(User) || User->GetWorld() != GetWorld()) return {};
    if (const FCSOCoverReservation* Previous = Reservations.Find(CoverId))
    {
        if (!IsReservationValid(*Previous))
        {
            const FSmartObjectClaimHandle PreviousClaim = Previous->Claim;
            ReleaseCover(PreviousClaim);
        }
    }
    const FCSORuntimeCover* Cover = Covers.Find(CoverId);
    USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    if (!Cover || !SmartObjects || !IsCoverAvailable(*Cover, User)) return {};
    if (FCSOCoverReservation* Existing = Reservations.Find(CoverId))
    {
        Existing->ExpiresAt = GetWorld()->GetTimeSeconds() + LeaseDuration(LeaseSeconds);
        return Existing->Claim;
    }
    const FGuid Token = FGuid::NewGuid();
    const FCSOCoverClaimUserData UserData(User, Token);
    const FSmartObjectClaimHandle Claim = SmartObjects->MarkSlotAsClaimed(Cover->SlotHandle,
        ESmartObjectClaimPriority::Normal, FConstStructView::Make(UserData));
    if (Claim.IsValid())
    {
        FCSOCoverReservation& Reservation = Reservations.Add(CoverId);
        Reservation.User = User;
        Reservation.Claim = Claim;
        Reservation.Token = Token;
        Reservation.Cell = CSOCover::CellFor(Cover->Data.Position, CellSize);
        Reservation.ExpiresAt = GetWorld()->GetTimeSeconds() + LeaseDuration(LeaseSeconds);
        ReservedSlots.Add(Claim.SlotHandle, CoverId);
        ReservedCells.FindOrAdd(Reservation.Cell).Add(CoverId);
    }
    return Claim;
}

FCSOCoverQueryResult UCSOCoverSubsystem::FindAndClaimCover(const FCSOCoverQuery& Query, float LeaseSeconds)
{
    check(IsInGameThread());
    if (!IsValid(Query.User) || Query.User->GetWorld() != GetWorld())
    {
        FCSOCoverQueryResult Result;
        Result.Status = ECSOCoverQueryStatus::InvalidRequest;
        return Result;
    }
    CleanupReservations();
    FCSOCoverQueryResult Result = FindCover(Query);
    if (Result.IsValid())
    {
        Result.ClaimHandle = ClaimCover(Result.CoverId, Query.User, LeaseSeconds);
        if (!Result.ClaimHandle.IsValid()) Result.Status = ECSOCoverQueryStatus::Unavailable;
    }
    return Result;
}

int64 UCSOCoverSubsystem::FindReservation(const FSmartObjectClaimHandle& ClaimHandle) const
{
    if (!ClaimHandle.IsValid()) return 0;
    if (const int64* Id = ReservedSlots.Find(ClaimHandle.SlotHandle))
        if (const FCSOCoverReservation* Reservation = Reservations.Find(*Id))
            if (Reservation->Claim == ClaimHandle) return *Id;
    return 0;
}

bool UCSOCoverSubsystem::OccupyCover(const FSmartObjectClaimHandle& ClaimHandle)
{
    check(IsInGameThread());
    const FCSOCoverReservation* Reservation = Reservations.Find(FindReservation(ClaimHandle));
    USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    if (!Reservation || !IsReservationValid(*Reservation) || !SmartObjects) return false;
    if (SmartObjects->GetSlotState(ClaimHandle.SlotHandle) == ESmartObjectSlotState::Occupied) return true;
    return SmartObjects->MarkSlotAsOccupied<USmartObjectBehaviorDefinition>(ClaimHandle) != nullptr;
}

bool UCSOCoverSubsystem::RenewCoverLease(const FSmartObjectClaimHandle& ClaimHandle, float LeaseSeconds)
{
    check(IsInGameThread());
    FCSOCoverReservation* Reservation = Reservations.Find(FindReservation(ClaimHandle));
    if (!Reservation || !IsReservationValid(*Reservation)) return false;
    Reservation->ExpiresAt = GetWorld()->GetTimeSeconds() + LeaseDuration(LeaseSeconds);
    return true;
}

bool UCSOCoverSubsystem::ReleaseCover(const FSmartObjectClaimHandle& ClaimHandle)
{
    check(IsInGameThread());
    const int64 Id = FindReservation(ClaimHandle);
    if (Id == 0) return false;
    const FCSOCoverReservation* Reservation = Reservations.Find(Id);
    const bool bStillOwned = Reservation && IsReservationOwnerMatches(*Reservation);
    ForgetReservation(Id);
    USmartObjectSubsystem* SmartObjects = GetSmartObjects();
    return bStillOwned && SmartObjects
        && SmartObjects->MarkSlotAsFree(ClaimHandle);
}

FSmartObjectClaimHandle UCSOCoverSubsystem::ForgetReservation(int64 CoverId)
{
    FCSOCoverReservation Reservation;
    if (!Reservations.RemoveAndCopyValue(CoverId, Reservation)) return {};
    ReservedSlots.Remove(Reservation.Claim.SlotHandle);
    if (TArray<int64>* Ids = ReservedCells.Find(Reservation.Cell))
    {
        Ids->RemoveSwap(CoverId);
        if (Ids->IsEmpty()) ReservedCells.Remove(Reservation.Cell);
    }
    return Reservation.Claim;
}

void UCSOCoverSubsystem::CleanupReservations()
{
    TArray<FSmartObjectClaimHandle> Expired;
    for (const auto& Pair : Reservations)
        if (!IsReservationValid(Pair.Value)) Expired.Add(Pair.Value.Claim);
    // Release checks the exact live token immediately before touching native state. A stale
    // record is forgotten without asking Smart Objects to release a newer owner's claim.
    for (const FSmartObjectClaimHandle& Claim : Expired) ReleaseCover(Claim);
}

void UCSOCoverSubsystem::Tick(float DeltaTime)
{
    MaintenanceTime += DeltaTime;
    if (MaintenanceTime >= 0.25f)
    {
        MaintenanceTime = 0.f;
        CleanupReservations();
        TArray<TWeakObjectPtr<ACSOCoverVolume>> StaleVolumes;
        for (const auto& Pair : VolumeCovers)
            if (!Pair.Key.IsValid() || !Pair.Key->IsBakeDataValid()) StaleVolumes.Add(Pair.Key);
        for (const TWeakObjectPtr<ACSOCoverVolume>& Key : StaleVolumes)
        {
            TArray<int64> Ids;
            VolumeCovers.RemoveAndCopyValue(Key, Ids);
            for (const int64 Id : Ids) RemoveCover(Id);
        }
    }
    DebugTime += DeltaTime;
    if (CVarDebug.GetValueOnGameThread() && DebugTime >= 0.15f)
    {
        DebugTime = 0.f;
        DrawRuntimeDebug();
    }
}

TStatId UCSOCoverSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UCSOCoverSubsystem, STATGROUP_Tickables);
}

void UCSOCoverSubsystem::DrawRuntimeDebug()
{
#if ENABLE_DRAW_DEBUG
    UWorld* World = GetWorld();
    APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    if (!Player) return;
    FVector Camera;
    FRotator Rotation;
    Player->GetPlayerViewPoint(Camera, Rotation);
    const float Radius = FMath::Clamp(CVarDebugRadius.GetValueOnGameThread(), 100.f, 20000.f);
    const int32 Limit = FMath::Clamp(CVarDebugMaxPoints.GetValueOnGameThread(), 1, 2048);
    const float Life = 0.17f;
    const FIntVector Min = CSOCover::CellFor(Camera - FVector(Radius), CellSize);
    const FIntVector Max = CSOCover::CellFor(Camera + FVector(Radius), CellSize);
    int32 Drawn = 0;
    for (int32 X = Min.X; X <= Max.X && Drawn < Limit; ++X)
    for (int32 Y = Min.Y; Y <= Max.Y && Drawn < Limit; ++Y)
    for (int32 Z = Min.Z; Z <= Max.Z && Drawn < Limit; ++Z)
    {
        const FIntVector Key(X, Y, Z);
        const TArray<int64>* Ids = Cells.Find(Key);
        if (!Ids) continue;
        if (CVarDebugPartitions.GetValueOnGameThread())
            DrawDebugBox(World, FVector(Key.X, Key.Y, Key.Z) * CellSize + FVector(CellSize * 0.5), FVector(CellSize * 0.5), FColor(70, 70, 70), false, Life);
        for (const int64 Id : *Ids)
        {
            const FCSORuntimeCover* Cover = Covers.Find(Id);
            if (!Cover || FVector::DistSquared(Cover->Data.Position, Camera) > FMath::Square(Radius)) continue;
            const FCSOCoverReservation* Reservation = Reservations.Find(Id);
            const ESmartObjectSlotState State = GetSmartObjects()->GetSlotState(Cover->SlotHandle);
            const FColor Color = State == ESmartObjectSlotState::Occupied ? FColor::Red
                : (State == ESmartObjectSlotState::Claimed ? FColor::Orange
                : (Cover->Data.bLowCrouched ? FColor(180, 80, 255) : (Cover->Data.bCrouched ? FColor::Cyan : FColor::Blue)));
            DrawDebugPoint(World, Cover->Data.Position, 9.f, Color, false, Life);
            const FVector Eye = Cover->Data.GetEye(Cover->Profile);
            DrawDebugLine(World, Cover->Data.Position, Eye, Color, false, Life, 0, 1.5f);
            DrawDebugDirectionalArrow(World, Eye, Eye + Cover->Data.WallDirection * 35.f, 8.f, Color, false, Life);
            for (const ECSOPeek Peek : { ECSOPeek::Stand, ECSOPeek::Left, ECSOPeek::Right })
                if ((Cover->Data.PeekMask & int32(Peek)) != 0)
                    DrawDebugDirectionalArrow(World, Eye, Cover->Data.GetPeekEye(Cover->Profile, Peek), 8.f, FColor::Green, false, Life, 0, 2.f);
            if (CVarDebugLabels.GetValueOnGameThread())
            {
                const double Remaining = Reservation ? FMath::Max(0.0, Reservation->ExpiresAt - World->GetTimeSeconds()) : 0.0;
                const FString Label = FString::Printf(TEXT("%lld %s%s %.1fs"), Id,
                    Cover->Data.bLowCrouched ? TEXT("LowCrouch ") : (Cover->Data.bCrouched ? TEXT("Crouch ") : TEXT("Stand ")),
                    State == ESmartObjectSlotState::Occupied ? TEXT("Occupied") : (State == ESmartObjectSlotState::Claimed ? TEXT("Claimed") : TEXT("Free")), Remaining);
                DrawDebugString(World, Eye + FVector(0, 0, 25), Label, nullptr, Color, Life, false, 0.8f);
            }
            if (++Drawn >= Limit) break;
        }
    }
#endif
}
