#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSOCoverTypes.h"
#include "CSOCoverSubsystem.generated.h"

class ACSOCoverVolume;
class USmartObjectDefinition;
class USmartObjectSubsystem;

/** Private lease identity carried by the engine slot, separate from the querying actor identity. */
USTRUCT()
struct FCSOCoverClaimUserData : public FSmartObjectActorUserData
{
    GENERATED_BODY()
    FCSOCoverClaimUserData() = default;
    FCSOCoverClaimUserData(const AActor* Actor, const FGuid& Token)
        : FSmartObjectActorUserData(Actor), ReservationToken(Token) {}
    UPROPERTY() FGuid ReservationToken;
};

/** Runtime records never spawn one Actor or Component per cover. */
struct COVERSMARTOBJECTS_API FCSORuntimeCover
{
    int64 Id = 0;
    FCSOBakedCover Data;
    FCSOAgentProfile Profile;
    TWeakObjectPtr<ACSOCoverVolume> Volume;
    FSmartObjectHandle SmartObjectHandle;
    FSmartObjectSlotHandle SlotHandle;
};

struct FCSOCoverReservation
{
    TWeakObjectPtr<AActor> User;
    FSmartObjectClaimHandle Claim;
    FGuid Token;
    FIntVector Cell = FIntVector::ZeroValue;
    double ExpiresAt = 0.0;
};

/** Authority-only cover registration, spatial indexing and expiring Smart Object reservations. */
UCLASS(BlueprintType)
class COVERSMARTOBJECTS_API UCSOCoverSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableInEditor() const override { return false; }

    /** Called by the volume after the Smart Object subsystem has begun play. */
    void RegisterVolume(ACSOCoverVolume* Volume);
    void UnregisterVolume(ACSOCoverVolume* Volume);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    FCSOCoverQueryResult FindCover(const FCSOCoverQuery& Query);

    /** Finds and reserves synchronously on the game thread. Lease must be renewed during use. */
    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects", meta=(AdvancedDisplay="LeaseSeconds"))
    FCSOCoverQueryResult FindAndClaimCover(const FCSOCoverQuery& Query, float LeaseSeconds = 10.f);

    /** Rechecks moving enemies, changed collision and navigation immediately before entering/peeking. */
    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    FCSOCoverQueryResult ValidateCover(int64 CoverId, const FCSOCoverQuery& Query);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    FSmartObjectClaimHandle ClaimCover(int64 CoverId, AActor* User, float LeaseSeconds = 10.f);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    bool OccupyCover(const FSmartObjectClaimHandle& ClaimHandle);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    bool RenewCoverLease(const FSmartObjectClaimHandle& ClaimHandle, float LeaseSeconds = 10.f);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    bool ReleaseCover(const FSmartObjectClaimHandle& ClaimHandle);

    /** Removes covers affected by destruction; claims are invalidated by Smart Objects. */
    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    int32 InvalidateCoversInBounds(const FBox& Bounds);

    /** Re-registers saved bake data. Re-bake geometry first when static obstacles have changed. */
    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects")
    void RefreshVolume(ACSOCoverVolume* Volume);

    UFUNCTION(BlueprintPure, Category="Cover Smart Objects")
    int32 GetRegisteredCoverCount() const { return Covers.Num(); }

    UFUNCTION(BlueprintPure, Category="Cover Smart Objects")
    int32 GetPartitionCount() const { return Cells.Num(); }

    UFUNCTION(BlueprintPure, Category="Cover Smart Objects")
    int32 GetReservationCount() const { return Reservations.Num(); }

    static constexpr double CellSize = 1000.0;
    static constexpr int32 MaxEnemies = 32;
    USmartObjectSubsystem* GetSmartObjects() const;
    bool IsCoverAvailable(const FCSORuntimeCover& Cover, const AActor* User) const;

private:
    void RemoveCover(int64 CoverId);
    void CleanupReservations();
    void DrawRuntimeDebug();
    bool IsReservationValid(const FCSOCoverReservation& Reservation) const;
    bool IsReservationOwnerMatches(const FCSOCoverReservation& Reservation, bool bRequireEnabled = false) const;
    bool HasReservationOverlap(const FCSORuntimeCover& Cover) const;
    int64 FindReservation(const FSmartObjectClaimHandle& ClaimHandle) const;
    FSmartObjectClaimHandle ForgetReservation(int64 CoverId);

    UPROPERTY(Transient) TObjectPtr<USmartObjectDefinition> DefaultDefinition;
    TMap<int64, FCSORuntimeCover> Covers;
    TMap<FIntVector, TArray<int64>> Cells;
    TMap<int64, FCSOCoverReservation> Reservations;
    TMap<FSmartObjectSlotHandle, int64> ReservedSlots;
    TMap<FIntVector, TArray<int64>> ReservedCells;
    TMap<TWeakObjectPtr<ACSOCoverVolume>, TArray<int64>> VolumeCovers;
    int64 NextId = 1;
    double MaxRegisteredRadius = 0.0;
    double MaxRegisteredHeight = 0.0;
    float MaintenanceTime = 0.f;
    float DebugTime = 0.f;
};
