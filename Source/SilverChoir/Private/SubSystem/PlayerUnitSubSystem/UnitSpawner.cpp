#include "SubSystem/PlayerUnitSubSystem/UnitSpawner.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Components/SceneComponent.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Tools/SIS_InventorySystemBFL.h"
#include "Engine/World.h"

AUnitSpawner::AUnitSpawner()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SpawnAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnAnchor"));
    SpawnAnchor->SetupAttachment(RootComponent);
}

bool AUnitSpawner::ValidateUnitData(const TSharedPtr<FUnitData>& Data, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (!Data || !Data->UnitId.IsValid())
    {
        OutError = FText::FromString(TEXT("单位数据或单位ID无效。"));
        return false;
    }
    UClass* PawnClass = Data->EntityData.UnitPawnClass.Get();
    if (!PawnClass || PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        OutError = FText::FromString(TEXT("单位未配置有效的 UnitPawnBase 实体类。"));
        return false;
    }
    return true;
}

AUnitPawnBase* AUnitSpawner::SpawnUnitById(FGuid UnitId, FText& OutError)
{
    return SpawnUnitShared(UPlayerUnitLibrary::GetUnitDataShared(this, UnitId), OutError);
}

AUnitPawnBase* AUnitSpawner::SpawnUnitFromReference(UUnitDataReference* UnitData, FText& OutError)
{
    return SpawnUnitShared(IsValid(UnitData) ? UnitData->GetSharedData() : nullptr, OutError);
}

AUnitPawnBase* AUnitSpawner::SpawnUnitShared(const TSharedPtr<FUnitData>& Data, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (bChangingUnit || !GetWorld() || IsActorBeingDestroyed())
    {
        OutError = FText::FromString(TEXT("单位生成器当前不可用或正在生成单位。"));
        return nullptr;
    }
    if (!ValidateUnitData(Data, OutError)) return nullptr;
    const TSharedPtr<FUnitData> SharedData = Data;
    const FTransform Transform = GetUnitSpawnTransform();
    if (!Transform.IsValid())
    {
        OutError = FText::FromString(TEXT("单位生成位置无效。"));
        return nullptr;
    }
    TGuardValue<bool> Guard(bChangingUnit, true);
    FActorSpawnParameters Parameters;
    Parameters.Owner = this;
    Parameters.OverrideLevel = GetLevel();
    Parameters.bDeferConstruction = true;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AUnitPawnBase* Unit = GetWorld()->SpawnActor<AUnitPawnBase>(SharedData->EntityData.UnitPawnClass, Transform, Parameters);
    if (!IsValid(Unit))
    {
        OutError = FText::FromString(TEXT("创建单位实体失败。"));
        return nullptr;
    }
    PendingUnit = Unit;
    auto Reject = [this, Unit, &OutError](const TCHAR* Message) -> AUnitPawnBase*
    {
        PendingUnit = nullptr;
        if (IsValid(Unit) && !Unit->IsActorBeingDestroyed()) Unit->Destroy();
        OutError = FText::FromString(Message);
        return nullptr;
    };
    ConfigureDeferredUnit(Unit);
    if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || IsActorBeingDestroyed())
        return Reject(TEXT("单位初始化被中止。"));
    if (!IsValid(Unit->UnitInventoryComponent)) return Reject(TEXT("单位缺少库存组件。"));
    Unit->UnitInventoryComponent->bInitializeFromPreset = false;
    // Data is already available to construction, initial notifications and Blueprint BeginPlay.
    if (!Unit->BindUnitDataShared(SharedData)) return Reject(TEXT("绑定共享单位数据失败。"));
    Unit->FinishSpawning(Transform);
    if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || IsActorBeingDestroyed() || Unit->GetUnitDataShared() != SharedData)
        return Reject(TEXT("单位在初始化时被销毁。"));
    ConfigureFinishedUnit(Unit);
    if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || IsActorBeingDestroyed()
        || !IsValid(Unit->UnitInventoryComponent)) return Reject(TEXT("单位初始化未完成。"));
    // Restore the existing item identities and equipment relationships for every type of spawner.
    USIS_InventorySystemBFL::LoadItemDatasToInventoryComponent(Unit->UnitInventoryComponent, SharedData->InventoryItems, false);
    if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || IsActorBeingDestroyed() || Unit->GetUnitDataShared() != SharedData)
        return Reject(TEXT("单位库存初始化被中止。"));

    AUnitPawnBase* Previous = GetSpawnedUnit();
    SpawnedUnit = Unit;
    PendingUnit = nullptr;
    if (Previous) Previous->Destroy();
    if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || IsActorBeingDestroyed())
        return Reject(TEXT("替换单位时生成被中止。"));
    OnUnitSpawned(Unit);
    return IsValid(Unit) && !Unit->IsActorBeingDestroyed() && !IsActorBeingDestroyed()
        ? Unit : Reject(TEXT("单位生成完成事件中止了生成。"));
}

FTransform AUnitSpawner::GetUnitSpawnTransform() const
{
    return IsValid(SpawnAnchor) ? SpawnAnchor->GetComponentTransform() : GetActorTransform();
}
void AUnitSpawner::ConfigureDeferredUnit(AUnitPawnBase* Unit) {}
void AUnitSpawner::ConfigureFinishedUnit(AUnitPawnBase* Unit) {}

void AUnitSpawner::ClearSpawnedUnit()
{
    if (bChangingUnit) return;
    TGuardValue<bool> Guard(bChangingUnit, true);
    AUnitPawnBase* Previous = GetSpawnedUnit();
    SpawnedUnit = nullptr;
    if (Previous) Previous->Destroy();
    if (Previous && !IsActorBeingDestroyed()) OnSpawnedUnitCleared();
}

AUnitPawnBase* AUnitSpawner::GetSpawnedUnit() const
{
    return IsValid(SpawnedUnit) && !SpawnedUnit->IsActorBeingDestroyed() ? SpawnedUnit.Get() : nullptr;
}

void AUnitSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AUnitPawnBase* Previous = SpawnedUnit;
    AUnitPawnBase* Pending = PendingUnit;
    SpawnedUnit = nullptr;
    PendingUnit = nullptr;
    if (IsValid(Pending) && !Pending->IsActorBeingDestroyed()) Pending->Destroy();
    if (IsValid(Previous) && !Previous->IsActorBeingDestroyed()) Previous->Destroy();
    Super::EndPlay(EndPlayReason);
}
