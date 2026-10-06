#include "SubSystem/PlayerUnitSubSystem/UnitSquadSpawner.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSpawner.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

AUnitSquadSpawner::AUnitSquadSpawner()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    UnitSpawnerClass = AUnitSpawner::StaticClass();
}

TArray<FVector> AUnitSquadSpawner::BuildFormationOffsets(int32 MemberCount, int32 InUnitsPerRow, FVector2D InSpacing)
{
    TArray<FVector> Positions;
    if (MemberCount <= 0 || MemberCount > 4096 || InUnitsPerRow <= 0
        || !FMath::IsFinite(InSpacing.X) || !FMath::IsFinite(InSpacing.Y)
        || InSpacing.X <= 0. || InSpacing.Y <= 0.) return Positions;
    const int32 Columns = FMath::Min(InUnitsPerRow, MemberCount);
    FVector Center = FVector::ZeroVector;
    Positions.Reserve(MemberCount);
    for (int32 Index = 0; Index < MemberCount; ++Index)
    {
        const int32 Row = Index / Columns;
        const int32 InRow = FMath::Min(Columns, MemberCount - Row * Columns);
        const FVector Position(-Row * InSpacing.X, (Index % Columns - (InRow - 1) * .5) * InSpacing.Y, 0.);
        Positions.Add(Position);
        Center += Position;
    }
    // Center the occupied members too, including a partially filled final row.
    Center /= MemberCount;
    for (FVector& Position : Positions) Position -= Center;
    return Positions;
}

TArray<FTransform> AUnitSquadSpawner::GetFormationTransforms(int32 MemberCount) const
{
    TArray<FTransform> Transforms;
    if (LocalSpawnOffset.ContainsNaN() || !GetActorTransform().IsValid()) return Transforms;
    const TArray<FVector> Positions = BuildFormationOffsets(MemberCount, UnitsPerRow, Spacing);
    Transforms.Reserve(Positions.Num());
    for (const FVector& Position : Positions)
    {
        const FVector WorldPosition = GetActorLocation() + GetActorQuat().RotateVector(Position + LocalSpawnOffset);
        Transforms.Emplace(GetActorQuat(), WorldPosition, FVector::OneVector);
    }
    return Transforms;
}

void AUnitSquadSpawner::DestroySpawners(TArray<TObjectPtr<AUnitSpawner>>& Spawners)
{
    TArray<TObjectPtr<AUnitSpawner>> Previous = MoveTemp(Spawners);
    Spawners.Reset();
    for (AUnitSpawner* Spawner : Previous)
        if (IsValid(Spawner) && !Spawner->IsActorBeingDestroyed()) Spawner->Destroy();
}

bool AUnitSquadSpawner::SpawnSquad(FGuid SquadId, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (bChangingSquad || !GetWorld() || IsActorBeingDestroyed())
    {
        OutError = FText::FromString(TEXT("小队生成器当前不可用或正在生成小队。"));
        return false;
    }
    UPlayerSquadManagerBase* Manager = UPlayerSquadLibrary::GetPlayerSquadManager(this);
    FSquadData Squad;
    if (!IsValid(Manager) || !Manager->GetSquad(SquadId, Squad) || Squad.MemberUnitIds.IsEmpty())
    {
        OutError = FText::FromString(TEXT("小队不存在或没有成员。"));
        return false;
    }
    UClass* SpawnerClass = UnitSpawnerClass.Get();
    if (!SpawnerClass || SpawnerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        OutError = FText::FromString(TEXT("未配置有效的单位生成器类。"));
        return false;
    }
    const TArray<TSharedPtr<FUnitData>> Data = Manager->GetSquadUnitsShared(SquadId);
    const TArray<FTransform> Transforms = GetFormationTransforms(Squad.MemberUnitIds.Num());
    if (Data.Num() != Squad.MemberUnitIds.Num() || Transforms.Num() != Data.Num())
    {
        OutError = FText::FromString(TEXT("小队成员数据缺失，或人数、间距、生成偏移配置无效。"));
        return false;
    }
    for (int32 Index = 0; Index < Data.Num(); ++Index)
    {
        if (!AUnitSpawner::ValidateUnitData(Data[Index], OutError)) return false;
        if (Data[Index]->UnitId != Squad.MemberUnitIds[Index])
        {
            OutError = FText::FromString(TEXT("小队成员数据顺序不一致。"));
            return false;
        }
    }

    TGuardValue<bool> Guard(bChangingSquad, true);
    auto Reject = [this, &OutError](const TCHAR* Fallback) -> bool
    {
        DestroySpawners(PendingSpawners);
        if (OutError.IsEmpty()) OutError = FText::FromString(Fallback);
        return false;
    };
    for (int32 Index = 0; Index < Data.Num(); ++Index)
    {
        FActorSpawnParameters Parameters;
        Parameters.Owner = this;
        Parameters.OverrideLevel = GetLevel();
        Parameters.bDeferConstruction = true;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AUnitSpawner* Spawner = GetWorld()->SpawnActor<AUnitSpawner>(SpawnerClass, Transforms[Index], Parameters);
        if (!IsValid(Spawner) || Spawner->IsActorBeingDestroyed()) return Reject(TEXT("创建小队成员生成器失败。"));
        PendingSpawners.Add(Spawner);
        Spawner->FinishSpawning(Transforms[Index]);
        if (!IsValid(Spawner) || Spawner->IsActorBeingDestroyed() || IsActorBeingDestroyed())
            return Reject(TEXT("小队生成器初始化被中止。"));
        if (!Spawner->SpawnUnitShared(Data[Index], OutError) || IsActorBeingDestroyed())
            return Reject(TEXT("生成小队成员失败。"));
    }
    // A member's Blueprint callback could remove an earlier member or alter the roster.
    FSquadData Current;
    if (!IsValid(Manager) || !Manager->GetSquad(SquadId, Current) || Current.MemberUnitIds != Squad.MemberUnitIds)
        return Reject(TEXT("生成过程中小队成员发生了变化。"));
    for (int32 Index = 0; Index < PendingSpawners.Num(); ++Index)
    {
        AUnitSpawner* Spawner = PendingSpawners[Index];
        AUnitPawnBase* Unit = IsValid(Spawner) ? Spawner->GetSpawnedUnit() : nullptr;
        if (!Unit || Unit->GetUnitDataShared() != Data[Index]) return Reject(TEXT("生成过程中有小队成员被移除或重新绑定。"));
    }
    TArray<TObjectPtr<AUnitSpawner>> Previous = MoveTemp(UnitSpawners);
    UnitSpawners = MoveTemp(PendingSpawners);
    SpawnedSquadId = SquadId;
    DestroySpawners(Previous);
    if (IsActorBeingDestroyed()) return Reject(TEXT("替换小队时生成器被销毁。"));
    OnSquadSpawned(SquadId, GetSpawnedUnits());
    return !IsActorBeingDestroyed();
}

void AUnitSquadSpawner::ClearSpawnedSquad()
{
    if (bChangingSquad) return;
    TGuardValue<bool> Guard(bChangingSquad, true);
    const FGuid Previous = SpawnedSquadId;
    SpawnedSquadId.Invalidate();
    DestroySpawners(UnitSpawners);
    if (Previous.IsValid() && !IsActorBeingDestroyed()) OnSpawnedSquadCleared(Previous);
}

TArray<AUnitSpawner*> AUnitSquadSpawner::GetUnitSpawners() const
{
    TArray<AUnitSpawner*> Result;
    for (AUnitSpawner* Spawner : UnitSpawners)
        if (IsValid(Spawner) && !Spawner->IsActorBeingDestroyed()) Result.Add(Spawner);
    return Result;
}

TArray<AUnitPawnBase*> AUnitSquadSpawner::GetSpawnedUnits() const
{
    TArray<AUnitPawnBase*> Result;
    for (AUnitSpawner* Spawner : GetUnitSpawners())
        if (AUnitPawnBase* Unit = Spawner->GetSpawnedUnit()) Result.Add(Unit);
    return Result;
}

void AUnitSquadSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SpawnedSquadId.Invalidate();
    DestroySpawners(PendingSpawners);
    DestroySpawners(UnitSpawners);
    Super::EndPlay(EndPlayReason);
}
