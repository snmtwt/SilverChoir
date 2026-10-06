#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SmartObjectRuntime.h"
#include "CSOCoverTypes.generated.h"

UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class ECSOPeek : uint8
{
    None = 0, Stand = 1, Left = 2, Right = 4
};
ENUM_CLASS_FLAGS(ECSOPeek)

UENUM(BlueprintType)
enum class ECSOCoverRanking : uint8
{
    Balanced,
    SafestThenNearest,
    Nearest
};

UENUM(BlueprintType)
enum class ECSOCoverQueryStatus : uint8
{
    Success, NoCover, BudgetExceeded, InvalidRequest, Unavailable
};

/** All dimensions are centimeters, relative to feet; use a separate volume/profile for different agent sizes. */
USTRUCT(BlueprintType)
struct COVERSMARTOBJECTS_API FCSOAgentProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float Radius = 34.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float CrouchHalfHeight = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float StandHalfHeight = 90.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float CrouchEyeHeight = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float StandEyeHeight = 160.f;
    /** Extra exposure beyond the detected wall edge. The bake stores the full eye displacement per side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float LeanDistance = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float Clearance = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float EyeSweepRadius = 4.f;
    bool IsValid() const;
};

/** Serialized in a volume in local coordinates; at runtime the subsystem keeps a world-space copy. */
USTRUCT(BlueprintType)
struct COVERSMARTOBJECTS_API FCSOBakedCover
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") FVector Position = FVector::ZeroVector;
    /** Unit horizontal direction from agent towards the obstacle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") FVector WallDirection = FVector::ForwardVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") bool bCrouched = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(Bitmask, BitmaskEnum="/Script/CoverSmartObjects.ECSOPeek")) int32 PeekMask = 0;
    /** Full lateral eye displacement: measured distance to edge plus Profile.LeanDistance. Zero uses the profile value for manually authored points. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float LeftPeekDistance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float RightPeekDistance = 0.f;
    FVector GetRight() const { return FVector::CrossProduct(FVector::UpVector, WallDirection).GetSafeNormal(); }
    FVector GetEye(const FCSOAgentProfile& Profile) const;
    FVector GetPeekEye(const FCSOAgentProfile& Profile, ECSOPeek Peek) const;
};

USTRUCT(BlueprintType)
struct COVERSMARTOBJECTS_API FCSOCoverQuery
{
    GENERATED_BODY()
    /** Search and navigation origin at the feet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") FVector Origin = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") FVector TargetEnemy = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") TArray<FVector> Enemies;
    /** Default inputs are enemy feet; set true when passing actual eye/aim positions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") bool bEnemyPositionsAreEyes = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") float EnemyEyeHeight = 160.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) float SearchRadius = 2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float MaxVerticalDistance = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") ECSOCoverRanking Ranking = ECSOCoverRanking::Balanced;
    /** One additional exposed enemy costs this much distance in Balanced ranking. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0")) float ExposurePenalty = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) int32 MaxCandidates = 128;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) int32 MaxTraces = 2048;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="1")) int32 MaxPathTests = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") bool bRequireReachable = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
    /** Match the bake's sight geometry setting. Movement clearance always uses simple collision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") bool bTraceComplex = false;
    /** Character stance clearance uses movement collision, independently of sight/projectile collision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") TEnumAsByte<ECollisionChannel> MovementChannel = ECC_Pawn;
    /** Ignore the querying pawn AND enemy actors so their capsules do not masquerade as cover. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") TArray<TObjectPtr<AActor>> IgnoredActors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") TObjectPtr<AActor> User = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover") bool bDrawDebug = false;
};

USTRUCT(BlueprintType)
struct COVERSMARTOBJECTS_API FCSOCoverQueryResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Cover") ECSOCoverQueryStatus Status = ECSOCoverQueryStatus::NoCover;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int64 CoverId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FVector WallDirection = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FVector PeekLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Cover") ECSOPeek Peek = ECSOPeek::None;
    UPROPERTY(BlueprintReadOnly, Category="Cover") bool bCrouched = false;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FSmartObjectHandle SmartObjectHandle;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FSmartObjectSlotHandle SlotHandle;
    UPROPERTY(BlueprintReadOnly, Category="Cover") FSmartObjectClaimHandle ClaimHandle;
    UPROPERTY(BlueprintReadOnly, Category="Cover") float Distance = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Cover") float Score = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int32 ExposedEnemies = 0;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int32 TestedEnemies = 0;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int32 CandidatesTested = 0;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int32 TracesUsed = 0;
    UPROPERTY(BlueprintReadOnly, Category="Cover") int32 PathTestsUsed = 0;
    /** A valid result can still be the best only within the work budget. */
    UPROPERTY(BlueprintReadOnly, Category="Cover") bool bSearchTruncated = false;
    bool IsValid() const { return Status == ECSOCoverQueryStatus::Success && CoverId != 0; }
};

/** Non-UObject partition math, shared with tests. Uses floor to cover negative world coordinates. */
namespace CSOCover
{
    inline FIntVector CellFor(const FVector& Position, double CellSize)
    {
        return FIntVector(FMath::FloorToInt(Position.X / CellSize), FMath::FloorToInt(Position.Y / CellSize), FMath::FloorToInt(Position.Z / CellSize));
    }
    inline double Score(double Distance, int32 Exposed, double Penalty) { return Distance + Exposed * Penalty; }
}
