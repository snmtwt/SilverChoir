#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSettings.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/UnitDisplayStand.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerUnitSubsystem,Log,All);
TSharedPtr<FVehicleData> UPlayerUnitSubsystem::GetVehicleDataShared(FGuid VehicleID) const
{
    return IsReady() ? Manager->GetVehicleDataShared(VehicleID) : nullptr;
}
bool UPlayerUnitSubsystem::LoadVehicleData(const TArray<FVehicleData>& Data, FText& OutError)
{
    if (IsReady()) return Manager->LoadVehicleData(Data, OutError);
    OutError = FText::FromString(TEXT("玩家单位系统尚未就绪。")); return false;
}
bool UPlayerUnitSubsystem::LoadVehicleTemplates(UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError)
{
    if (IsReady()) return Manager->LoadVehicleTemplates(Table, TileId, OutVehicleIds, OutError);
    OutVehicleIds.Reset();
    OutError = FText::FromString(TEXT("玩家单位系统尚未就绪。")); return false;
}
bool UPlayerUnitSubsystem::RegisterDisplayStand(AUnitDisplayStand* Stand)
{
    if (!IsValid(Stand) || Stand->IsActorBeingDestroyed() || Stand->GetGameInstance()!=GetGameInstance()) return false;
    if (AUnitDisplayStand* Existing=GetDisplayStand(); Existing && Existing!=Stand)
    {
        UE_LOG(LogPlayerUnitSubsystem, Warning, TEXT("Only one unit display stand may be registered. Keeping %s; ignoring %s."), *Existing->GetName(), *Stand->GetName());
        return false;
    }
    DisplayStand=Stand;
    return true;
}
void UPlayerUnitSubsystem::UnregisterDisplayStand(AUnitDisplayStand* Stand)
{
    // An old actor ending play must not unregister its replacement.
    if (DisplayStand.Get()==Stand) DisplayStand.Reset();
}
AUnitDisplayStand* UPlayerUnitSubsystem::GetDisplayStand() const
{
    AUnitDisplayStand* Stand=DisplayStand.Get();
    return Stand && !Stand->IsActorBeingDestroyed()?Stand:nullptr;
}
TSharedPtr<FUnitData> UPlayerUnitSubsystem::GetUnitDataShared(FGuid ID) const
{
    return IsReady()?Manager->GetUnitDataShared(ID):nullptr;
}
bool UPlayerUnitSubsystem::LoadUnitData(const TArray<FUnitData>& Data,FText& OutError)
{
    if (IsReady()) return Manager->LoadUnitData(Data,OutError);
    OutError=FText::FromString(TEXT("玩家单位系统尚未就绪。")); return false;
}
void UPlayerUnitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    InitializationError=FText::GetEmpty();
    const auto& Configured=GetDefault<UPlayerUnitSettings>()->ManagerClass;
    UClass* Class=Configured.IsNull()?UPlayerUnitManagerBase::StaticClass():Configured.LoadSynchronous();
    if (!Class || !Class->IsChildOf(UPlayerUnitManagerBase::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))
    {
        InitializationError=NSLOCTEXT("PlayerUnit","InvalidManager","玩家单位处理类配置无效：无法加载、类型不匹配或不可实例化。");
        UE_LOG(LogPlayerUnitSubsystem,Error,TEXT("%s Class: %s"),*InitializationError.ToString(),*Configured.ToString());
        return;
    }
    Manager=NewObject<UPlayerUnitManagerBase>(this,Class);
}
void UPlayerUnitSubsystem::Deinitialize()
{
    DisplayStand.Reset();
    if (Manager) Manager->ClearUnits();
    Manager=nullptr; InitializationError=FText::GetEmpty();
    Super::Deinitialize();
}
bool UPlayerUnitSubsystem::IsReady() const { return IsValid(Manager); }
UPlayerUnitManagerBase* UPlayerUnitSubsystem::GetManager() const { return IsReady()?Manager.Get():nullptr; }
