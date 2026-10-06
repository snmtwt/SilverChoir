#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSubsystem.h"

#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSettings.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerSquadSubsystem, Log, All);

void UPlayerSquadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    InitializationError = FText::GetEmpty();

    UPlayerUnitSubsystem* Units = Collection.InitializeDependency<UPlayerUnitSubsystem>();
    if (!Units || !Units->IsReady())
    {
        InitializationError = NSLOCTEXT("PlayerSquad", "UnitSystemNotReady", "玩家单位子系统尚未就绪，无法初始化玩家小队子系统。");
        UE_LOG(LogPlayerSquadSubsystem, Error, TEXT("%s"), *InitializationError.ToString());
        return;
    }

    const TSoftClassPtr<UPlayerSquadManagerBase>& Configured = GetDefault<UPlayerSquadSettings>()->ManagerClass;
    UClass* ManagerClass = Configured.IsNull() ? UPlayerSquadManagerBase::StaticClass() : Configured.LoadSynchronous();
    if (!ManagerClass || !ManagerClass->IsChildOf(UPlayerSquadManagerBase::StaticClass())
        || ManagerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        InitializationError = NSLOCTEXT("PlayerSquad", "InvalidManager", "玩家小队处理类配置无效：无法加载、类型不匹配或不可实例化。");
        UE_LOG(LogPlayerSquadSubsystem, Error, TEXT("%s Class: %s"), *InitializationError.ToString(), *Configured.ToString());
        return;
    }

    Manager = NewObject<UPlayerSquadManagerBase>(this, ManagerClass);
    Manager->Initialize(Units->GetManager());
    if (!Manager->IsReady())
    {
        InitializationError = NSLOCTEXT("PlayerSquad", "ManagerNotReady", "玩家小队处理类初始化失败。");
        UE_LOG(LogPlayerSquadSubsystem, Error, TEXT("%s"), *InitializationError.ToString());
        Manager->Shutdown();
        Manager = nullptr;
    }
}

void UPlayerSquadSubsystem::Deinitialize()
{
    if (Manager)
    {
        Manager->Shutdown();
    }
    Manager = nullptr;
    InitializationError = FText::GetEmpty();
    Super::Deinitialize();
}

bool UPlayerSquadSubsystem::IsReady() const
{
    return IsValid(Manager) && Manager->IsReady();
}

UPlayerSquadManagerBase* UPlayerSquadSubsystem::GetManager() const
{
    return IsReady() ? Manager.Get() : nullptr;
}
