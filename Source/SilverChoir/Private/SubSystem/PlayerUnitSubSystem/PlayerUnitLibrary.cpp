#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSettings.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Object/Unit/UnitPawnBase.h"

TSharedPtr<FVehicleData> UPlayerUnitLibrary::GetVehicleDataShared(const UObject* Context, FGuid VehicleID)
{
    auto* Manager = GetPlayerUnitManager(Context);
    return Manager ? Manager->GetVehicleDataShared(VehicleID) : nullptr;
}
bool UPlayerUnitLibrary::LoadPlayerVehiclesFromTemplateTable(const UObject* Context, UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError)
{
    if (auto* System = GetPlayerUnitSubsystem(Context)) return System->LoadVehicleTemplates(Table, TileId, OutVehicleIds, OutError);
    OutVehicleIds.Reset();
    OutError = FText::FromString(TEXT("无法获取玩家单位子系统。")); return false;
}
bool UPlayerUnitLibrary::LoadPlayerVehicleData(const UObject* Context, const TArray<FVehicleData>& Data, FText& OutError)
{
    if (auto* System = GetPlayerUnitSubsystem(Context)) return System->LoadVehicleData(Data, OutError);
    OutError = FText::FromString(TEXT("无法获取玩家单位子系统。")); return false;
}
bool UPlayerUnitLibrary::GetPlayerVehicleDataSnapshot(const UObject* Context, FGuid VehicleID, FVehicleData& OutData)
{
    if (auto* Manager = GetPlayerUnitManager(Context)) return Manager->GetVehicleDataSnapshot(VehicleID, OutData);
    OutData = FVehicleData(); return false;
}
TArray<FGuid> UPlayerUnitLibrary::GetPlayerVehicleDataIDs(const UObject* Context)
{
    auto* Manager = GetPlayerUnitManager(Context);
    return Manager ? Manager->GetVehicleDataIDs() : TArray<FGuid>();
}
TArray<FVehicleData> UPlayerUnitLibrary::GetPlayerVehicleDataAtTile(const UObject* Context, FName TileId)
{
    auto* Manager = GetPlayerUnitManager(Context);
    return Manager ? Manager->GetVehicleDataAtTile(TileId) : TArray<FVehicleData>();
}
bool UPlayerUnitLibrary::UpdatePlayerVehicleData(const UObject* Context, FGuid VehicleID, const FVehicleData& Data)
{
    auto* Manager = GetPlayerUnitManager(Context);
    return Manager && Manager->UpdateVehicleData(VehicleID, Data);
}
bool UPlayerUnitLibrary::RemovePlayerVehicleData(const UObject* Context, FGuid VehicleID, FText& OutError)
{
    if (auto* Manager = GetPlayerUnitManager(Context)) return Manager->RemoveVehicleData(VehicleID, OutError);
    OutError = FText::FromString(TEXT("无法获取玩家单位子系统。")); return false;
}

AUnitDisplayStand* UPlayerUnitLibrary::GetUnitDisplayStand(const UObject* WorldContextObject)
{
    auto* System=GetPlayerUnitSubsystem(WorldContextObject);
    return System?System->GetDisplayStand():nullptr;
}

TArray<FUnitData> UPlayerUnitLibrary::CreateUnitDataBatch(const FUnitTemplate& Template,int32 Count)
{
    TArray<FUnitData> Result;
    for (int32 I=0; I<Count; ++I) Result.Add(CreateUnitDataFromTemplate(Template));
    return Result;
}
TArray<FUnitData> UPlayerUnitLibrary::CreateUnitDataFromTemplates(const TArray<FUnitTemplate>& Templates)
{
    TArray<FUnitData> Result; Result.Reserve(Templates.Num());
    for (const auto& Template:Templates) Result.Add(CreateUnitDataFromTemplate(Template));
    return Result;
}
bool UPlayerUnitLibrary::CreateUnitDataFromTable(UDataTable* Table,const TArray<FName>& RowNames,TArray<FUnitData>& OutData,FText& OutError,FName TileId)
{
    OutData.Reset(); OutError=FText::GetEmpty();
    if (!Table) Table=GetDefault<UPlayerUnitSettings>()->UnitTemplateTable.LoadSynchronous();
    if (!Table || Table->GetRowStruct()!=FUnitTemplate::StaticStruct())
    { OutError=FText::FromString(TEXT("单位模板表未配置或行结构不是 FUnitTemplate。")); return false; }
    TArray<FUnitTemplate> Templates; Templates.Reserve(RowNames.Num());
    for (FName Name:RowNames)
    {
        const auto* Row=Table->FindRow<FUnitTemplate>(Name,TEXT("CreateUnitDataFromTable"),false);
        if (!Row) { OutError=FText::Format(NSLOCTEXT("PlayerUnit","MissingRow","单位模板行不存在：{0}"),FText::FromName(Name)); return false; }
        Templates.Add(*Row);
    }
    OutData=CreateUnitDataFromTemplates(Templates);
    for (auto& Unit:OutData) Unit.RuntimeData.TileId=TileId;
    return true;
}
bool UPlayerUnitLibrary::LoadPlayerUnitData(const UObject* Context,const TArray<FUnitData>& Data,FText& OutError)
{
    if (auto* System=GetPlayerUnitSubsystem(Context)) return System->LoadUnitData(Data,OutError);
    OutError=FText::FromString(TEXT("无法获取玩家单位子系统。")); return false;
}
bool UPlayerUnitLibrary::GetPlayerUnitDataSnapshot(const UObject* Context,FGuid ID,FUnitData& OutData)
{
    if (auto* Manager=GetPlayerUnitManager(Context)) return Manager->GetUnitDataSnapshot(ID,OutData);
    OutData=FUnitData(); return false;
}
bool UPlayerUnitLibrary::UpdatePlayerUnitData(const UObject* Context,FGuid ID,const FUnitData& Data)
{
    auto* Manager=GetPlayerUnitManager(Context); return Manager && Manager->UpdateUnitData(ID,Data);
}
bool UPlayerUnitLibrary::SaveEquipmentToUnitData(const UObject* Context,FGuid ID,
    USIS_UnitInventoryComponent* InventoryComponent,FText& OutError)
{
    OutError=FText::GetEmpty();
    if (!IsValid(InventoryComponent))
    { OutError=FText::FromString(TEXT("库存组件无效，无法保存单位装备。")); return false; }
    auto* Manager=GetPlayerUnitManager(Context);
    if (!Manager)
    { OutError=FText::FromString(TEXT("无法获取玩家单位处理类。")); return false; }
    if (InventoryComponent->GetWorld()!=Manager->GetWorld())
    { OutError=FText::FromString(TEXT("库存组件与单位数据不属于同一个世界。")); return false; }
    const auto Data=Manager->GetUnitDataShared(ID);
    if (!Data)
    { OutError=FText::FromString(TEXT("单位ID无效或尚未加载该单位数据。")); return false; }
    if (const auto* Unit=Cast<AUnitPawnBase>(InventoryComponent->GetOwner()))
    {
        const auto BoundData=Unit->GetUnitDataShared();
        if (BoundData && BoundData!=Data)
        { OutError=FText::FromString(TEXT("库存组件绑定的单位与传入的单位ID不一致。")); return false; }
    }
    if (Data->IsNotifyingDataChanged())
    { OutError=FText::FromString(TEXT("请勿在单位数据更新通知中再次保存装备，可延迟到下一帧调用。")); return false; }
    TArray<FSIS_ItemData> Items;
    // SIS returns false for an empty inventory: this is a valid snapshot which
    // must replace stale equipment rather than accidentally resurrecting it.
    InventoryComponent->GetAllItemDataForSerialization(true,Items);
    Data->Modify([&Items](FUnitData& Unit){Unit.InventoryItems=MoveTemp(Items);});
    return true;
}
TArray<FGuid> UPlayerUnitLibrary::GetPlayerUnitDataIDs(const UObject* Context)
{
    auto* Manager=GetPlayerUnitManager(Context); return Manager?Manager->GetUnitDataIDs():TArray<FGuid>();
}

FUnitData UPlayerUnitLibrary::CreateUnitDataFromTemplate(const FUnitTemplate& UnitTemplate)
{
    FUnitData Data;
    Data.UnitId=FGuid::NewGuid();
    Data.Profile=UnitTemplate.Profile;
    Data.EntityData=UnitTemplate.EntityData;
    Data.StrategicMovementData=UnitTemplate.StrategicMovementData.GetSanitized();
    Data.Profile.Age=FMath::Max(0,Data.Profile.Age);
    Data.Attributes=UnitTemplate.Attributes;
    // ClampMin is editor metadata; validate programmatically supplied templates as well.
    auto NonNegative=[](float Value) { return FMath::IsFinite(Value)?FMath::Max(0.f,Value):0.f; };
    Data.Attributes.MaxHealth=NonNegative(Data.Attributes.MaxHealth);
    Data.Attributes.MaxStamina=NonNegative(Data.Attributes.MaxStamina);
    Data.Attributes.MaxCarryWeight=NonNegative(Data.Attributes.MaxCarryWeight);
    for (int32* Value:{&Data.Attributes.Strength,&Data.Attributes.Agility,&Data.Attributes.Dexterity,
        &Data.Attributes.Intelligence,&Data.Attributes.Constitution,&Data.Attributes.Marksmanship,
        &Data.Attributes.Demolition,&Data.Attributes.Hacking,&Data.Attributes.Medicine}) *Value=FMath::Max(0,*Value);
    Data.RuntimeData.CurrentHealth=Data.Attributes.MaxHealth;
    Data.RuntimeData.CurrentStamina=Data.Attributes.MaxStamina;
    return Data;
}

UPlayerUnitSubsystem* UPlayerUnitLibrary::GetPlayerUnitSubsystem(const UObject* WorldContextObject)
{
    UWorld* World=GEngine && IsValid(WorldContextObject)?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::ReturnNull):nullptr;
    auto* Instance=World?World->GetGameInstance():nullptr;
    return Instance?Instance->GetSubsystem<UPlayerUnitSubsystem>():nullptr;
}
UPlayerUnitManagerBase* UPlayerUnitLibrary::GetPlayerUnitManager(const UObject* Context)
{
    const auto* System=GetPlayerUnitSubsystem(Context); return System?System->GetManager():nullptr;
}
bool UPlayerUnitLibrary::IsPlayerUnitSystemReady(const UObject* Context) { return GetPlayerUnitManager(Context)!=nullptr; }
bool UPlayerUnitLibrary::RegisterPlayerUnit(const UObject* Context,FName ID,AActor* Unit)
{
    auto* Manager=GetPlayerUnitManager(Context); return Manager && Manager->RegisterUnit(ID,Unit);
}
bool UPlayerUnitLibrary::UnregisterPlayerUnit(const UObject* Context,FName ID)
{
    auto* Manager=GetPlayerUnitManager(Context); return Manager && Manager->UnregisterUnit(ID);
}
AActor* UPlayerUnitLibrary::GetPlayerUnit(const UObject* Context,FName ID)
{
    const auto* Manager=GetPlayerUnitManager(Context); return Manager?Manager->GetUnit(ID):nullptr;
}
TArray<AActor*> UPlayerUnitLibrary::GetPlayerUnits(const UObject* Context)
{
    const auto* Manager=GetPlayerUnitManager(Context); return Manager?Manager->GetUnits():TArray<AActor*>();
}
void UPlayerUnitLibrary::ClearPlayerUnits(const UObject* Context)
{
    if (auto* Manager=GetPlayerUnitManager(Context)) Manager->ClearUnits();
}

TSharedPtr<FUnitData> UPlayerUnitLibrary::GetUnitDataShared(const UObject* Context,FGuid UnitId)
{
    auto* Manager=GetPlayerUnitManager(Context);
    return Manager?Manager->GetUnitDataShared(UnitId):nullptr;
}
UUnitDataReference* UPlayerUnitLibrary::GetUnitDataReference(const UObject* Context,FGuid UnitId)
{
    auto Data=GetUnitDataShared(Context,UnitId);
    if (!Data) return nullptr;
    auto* Reference=NewObject<UUnitDataReference>();
    Reference->Initialize(Data);
    return Reference;
}
