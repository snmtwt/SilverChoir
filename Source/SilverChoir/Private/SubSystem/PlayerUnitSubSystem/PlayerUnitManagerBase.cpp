#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "GameFramework/Actor.h"
#include "UObject/GCObject.h"
#include "Data/Vehicles/VehicleDataLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSettings.h"

namespace
{
// Shared struct memory is invisible to UObject reflection. Keep its UObject fields traced
// for the entire shared allocation lifetime, even if a consumer outlives the manager.
struct FSharedPlayerUnitData final : public FUnitData, public FGCObject
{
    explicit FSharedPlayerUnitData(const FUnitData& Source) : FUnitData(Source) {}
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddPropertyReferencesWithStructARO(FUnitData::StaticStruct(),static_cast<FUnitData*>(this));
    }
    virtual FString GetReferencerName() const override { return TEXT("SharedPlayerUnitData"); }
};
struct FSharedPlayerVehicleData final : public FVehicleData, public FGCObject
{
    explicit FSharedPlayerVehicleData(const FVehicleData& Source) : FVehicleData(Source) {}
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddPropertyReferencesWithStructARO(FVehicleData::StaticStruct(), static_cast<FVehicleData*>(this));
    }
    virtual FString GetReferencerName() const override { return TEXT("SharedPlayerVehicleData"); }
};
}

TSharedPtr<FVehicleData> UPlayerUnitManagerBase::GetVehicleDataShared(FGuid ID) const
{
    const auto* Found = VehicleDataStore.Find(ID);
    return Found ? *Found : nullptr;
}
bool UPlayerUnitManagerBase::LoadVehicleData(const TArray<FVehicleData>& Data, FText& OutError)
{
    check(IsInGameThread());
    OutError = FText::GetEmpty();
    if (bUpdatingVehicleData || bUpdatingUnitData)
    { OutError = FText::FromString(TEXT("正在更新玩家数据，请等待本次通知完成。")); return false; }
    TSet<FGuid> Seen;
    TArray<FGuid> ChangedIds;
    ChangedIds.Reserve(Data.Num());
    for (const FVehicleData& Item : Data)
    {
        if (!Item.VehicleId.IsValid() || Seen.Contains(Item.VehicleId) || Item.Attributes.PassengerCapacity < 0)
        { OutError = FText::FromString(TEXT("车辆数组包含无效或重复 ID，或负数座位，未加载任何数据。")); return false; }
        Seen.Add(Item.VehicleId);
        ChangedIds.Add(Item.VehicleId);
    }
    TGuardValue<bool> Guard(bUpdatingVehicleData, true);
    for (const FVehicleData& Item : Data)
    {
        if (auto* Existing = VehicleDataStore.Find(Item.VehicleId)) **Existing = Item;
        else
        {
            auto Record = MakeShared<FSharedPlayerVehicleData>(Item);
            Record->CanModify.BindUObject(this, &ThisClass::CanModifyVehicleData);
            Record->BeginNotify.BindUObject(this, &ThisClass::BeginVehicleDataNotify);
            Record->EndNotify.BindUObject(this, &ThisClass::EndVehicleDataNotify);
            VehicleDataStore.Add(Item.VehicleId, Record);
        }
    }
    // 先修复整批关联，外部观察者看到的小队容量/占用与全部车辆数据一致。
    if (!ChangedIds.IsEmpty()) OnVehicleDataReconcile.Broadcast(ChangedIds);
    for (const FGuid ID : ChangedIds) VehicleDataStore.FindChecked(ID)->NotifyDataChanged();
    return true;
}
bool UPlayerUnitManagerBase::LoadVehicleTemplates(UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError)
{
    OutVehicleIds.Reset();
    OutError = FText::GetEmpty();
    if (bUpdatingVehicleData || bUpdatingUnitData)
    { OutError = FText::FromString(TEXT("正在更新玩家数据，请等待本次通知完成。")); return false; }
    if (TileId.IsNone())
    { OutError = FText::FromString(TEXT("加载车辆模板时必须指定当前所在瓦片 ID。")); return false; }
    if (!Table) Table = GetDefault<UPlayerUnitSettings>()->VehicleTemplateTable.LoadSynchronous();
    if (!IsValid(Table) || Table->GetRowStruct() != FVehicleTemplate::StaticStruct())
    { OutError = FText::FromString(TEXT("车辆模板表未配置，或行结构不是 FVehicleTemplate。")); return false; }
    TArray<FName> Rows = Table->GetRowNames();
    Rows.Sort(FNameLexicalLess());
    TArray<FVehicleData> Created;
    if (!UVehicleDataLibrary::CreateVehicleDataFromTable(Table, Rows, Created, OutError, TileId)
        || !LoadVehicleData(Created, OutError)) return false;
    for (const FVehicleData& Item : Created) OutVehicleIds.Add(Item.VehicleId);
    return true;
}
bool UPlayerUnitManagerBase::GetVehicleDataSnapshot(FGuid ID, FVehicleData& OutData) const
{
    const auto Record = GetVehicleDataShared(ID);
    OutData = Record ? *Record : FVehicleData();
    return Record.IsValid();
}
bool UPlayerUnitManagerBase::UpdateVehicleData(FGuid ID, const FVehicleData& Data)
{
    if (ID != Data.VehicleId || !VehicleDataStore.Contains(ID)) return false;
    FText Error;
    return LoadVehicleData({Data}, Error);
}
bool UPlayerUnitManagerBase::RemoveVehicleData(FGuid ID, FText& OutError)
{
    check(IsInGameThread());
    OutError = FText::GetEmpty();
    if (bUpdatingVehicleData || bUpdatingUnitData)
    { OutError = FText::FromString(TEXT("正在更新玩家数据，请等待本次通知完成。")); return false; }
    auto Record = GetVehicleDataShared(ID);
    if (!Record)
    { OutError = FText::FromString(TEXT("车辆不存在。")); return false; }
    TGuardValue<bool> Guard(bUpdatingVehicleData, true);
    Record->CanModify.Unbind();
    Record->BeginNotify.Unbind();
    Record->EndNotify.Unbind();
    VehicleDataStore.Remove(ID);
    OnVehicleDataReconcile.Broadcast({ID});
    OnVehicleDataChanged.Broadcast(ID);
    // 消费者须重新查询管理类，不把已移除的外部共享引用当作有效玩家车辆。
    Record->NotifyDataChanged();
    return true;
}
TArray<FGuid> UPlayerUnitManagerBase::GetVehicleDataIDs() const
{
    TArray<FGuid> IDs;
    VehicleDataStore.GetKeys(IDs);
    IDs.Sort();
    return IDs;
}
TArray<FVehicleData> UPlayerUnitManagerBase::GetVehicleDataAtTile(FName TileId) const
{
    TArray<FVehicleData> Result;
    if (TileId.IsNone()) return Result;
    for (const FGuid ID : GetVehicleDataIDs())
    {
        const auto& Record = VehicleDataStore.FindChecked(ID);
        if (Record->RuntimeData.TileId == TileId) Result.Add(*Record);
    }
    return Result;
}
bool UPlayerUnitManagerBase::BeginVehicleDataNotify(FGuid ID)
{
    const bool bPreviousUpdating = bUpdatingVehicleData;
    bUpdatingVehicleData = true;
    OnVehicleDataReconcile.Broadcast({ID});
    OnVehicleDataChanged.Broadcast(ID);
    return bPreviousUpdating;
}

TSharedPtr<FUnitData> UPlayerUnitManagerBase::GetUnitDataShared(FGuid ID) const
{
    const auto* Found=UnitDataStore.Find(ID); return Found?*Found:nullptr;
}
bool UPlayerUnitManagerBase::LoadUnitData(const TArray<FUnitData>& Data,FText& OutError)
{
    OutError=FText::GetEmpty();
    if (bUpdatingUnitData || bUpdatingVehicleData) { OutError=FText::FromString(TEXT("正在更新玩家数据，请等待本次通知完成。")); return false; }
    TSet<FGuid> Seen;
    for (const auto& Item:Data)
    {
        if (!Item.UnitId.IsValid() || Seen.Contains(Item.UnitId))
        {
            OutError=FText::FromString(TEXT("单位数组包含无效或重复的 UnitId，未加载任何数据。")); return false;
        }
        Seen.Add(Item.UnitId);
    }
    TGuardValue<bool> Guard(bUpdatingUnitData,true);
    for (const auto& Item:Data)
    {
        if (auto* Existing=UnitDataStore.Find(Item.UnitId)) **Existing=Item;
        else
        {
            auto Record=MakeShared<FSharedPlayerUnitData>(Item);
            Record->OnDataChanged.AddUObject(this,&ThisClass::ForwardUnitDataChanged);
            UnitDataStore.Add(Item.UnitId,Record);
        }
    }
    for (const auto& Item:Data) UnitDataStore.FindChecked(Item.UnitId)->NotifyDataChanged();
    return true;
}
bool UPlayerUnitManagerBase::GetUnitDataSnapshot(FGuid ID,FUnitData& OutData) const
{
    const auto Data=GetUnitDataShared(ID);
    OutData=Data?*Data:FUnitData(); return Data.IsValid();
}
bool UPlayerUnitManagerBase::UpdateUnitData(FGuid ID,const FUnitData& Data)
{
    if (bUpdatingUnitData || bUpdatingVehicleData || ID!=Data.UnitId || !UnitDataStore.Contains(ID)) return false;
    TGuardValue<bool> Guard(bUpdatingUnitData,true);
    *UnitDataStore.FindChecked(ID)=Data;
    UnitDataStore.FindChecked(ID)->NotifyDataChanged(); return true;
}
TArray<FGuid> UPlayerUnitManagerBase::GetUnitDataIDs() const
{
    TArray<FGuid> IDs; UnitDataStore.GetKeys(IDs); IDs.Sort(); return IDs;
}

UWorld* UPlayerUnitManagerBase::GetWorld() const
{
    return !IsTemplate() && IsValid(GetOuter()) ? GetOuter()->GetWorld() : nullptr;
}
bool UPlayerUnitManagerBase::IsCurrentUnit(AActor* Unit) const
{
    return IsValid(Unit) && !Unit->IsActorBeingDestroyed() && GetWorld() && Unit->GetWorld()==GetWorld();
}
bool UPlayerUnitManagerBase::RegisterUnit(FName UnitID,AActor* Unit)
{
    if (UnitID.IsNone() || !IsCurrentUnit(Unit)) return false;
    for (auto It=Units.CreateIterator();It;++It) if (!IsCurrentUnit(It.Value().Get())) It.RemoveCurrent();
    if (AActor* Existing=GetUnit(UnitID)) return Existing==Unit;
    // 一个 Actor 只允许使用一个 ID，防止枚举时重复。
    for (const auto& Entry:Units) if (Entry.Value.Get()==Unit) return false;
    Units.Add(UnitID,Unit); return true;
}
bool UPlayerUnitManagerBase::UnregisterUnit(FName UnitID) { return Units.Remove(UnitID)>0; }
AActor* UPlayerUnitManagerBase::GetUnit(FName UnitID) const
{
    const auto* Entry=Units.Find(UnitID);
    AActor* Unit=Entry?Entry->Get():nullptr;
    return IsCurrentUnit(Unit)?Unit:nullptr;
}
TArray<AActor*> UPlayerUnitManagerBase::GetUnits() const
{
    TArray<AActor*> Result;
    for (const auto& Entry:Units) if (IsCurrentUnit(Entry.Value.Get())) Result.Add(Entry.Value.Get());
    return Result;
}
void UPlayerUnitManagerBase::ClearUnits() { Units.Reset(); }

void UPlayerUnitManagerBase::ForwardUnitDataChanged(FGuid ID) { TGuardValue<bool> Guard(bUpdatingUnitData,true); OnUnitDataChanged.Broadcast(ID); }
void UPlayerUnitManagerBase::BeginDestroy()
{
    for (auto& Pair : VehicleDataStore)
    {
        Pair.Value->CanModify.Unbind();
        Pair.Value->BeginNotify.Unbind();
        Pair.Value->EndNotify.Unbind();
    }
    VehicleDataStore.Reset();
    OnVehicleDataReconcile.Clear();
    for (auto& Pair:UnitDataStore) Pair.Value->OnDataChanged.RemoveAll(this);
    UnitDataStore.Reset();
    Super::BeginDestroy();
}
void UUnitDataReference::Initialize(TSharedPtr<FUnitData> InData)
{
    if (Data == InData) return;
    ReleaseData();
    Data=MoveTemp(InData);
    if (Data)
    {
        ChangedHandle=Data->OnDataChanged.AddUObject(this,&ThisClass::HandleChanged);
        SelectedHandle=Data->OnSelected.AddUObject(this,&ThisClass::HandleSelected);
        DeselectedHandle=Data->OnDeselected.AddUObject(this,&ThisClass::HandleDeselected);
    }
}
bool UUnitDataReference::SetSelected(bool bSelected)
{
    // A Blueprint listener may rebind or release this reference during the notification.
    const TSharedPtr<FUnitData> CurrentData=Data;
    return CurrentData && CurrentData->SetSelected(bSelected);
}
void UUnitDataReference::ReleaseData()
{
    if (Data)
    {
        Data->OnDataChanged.Remove(ChangedHandle);
        Data->OnSelected.Remove(SelectedHandle);
        Data->OnDeselected.Remove(DeselectedHandle);
    }
    ChangedHandle.Reset(); SelectedHandle.Reset(); DeselectedHandle.Reset();
    Data.Reset();
}
void UUnitDataReference::BeginDestroy()
{
    ReleaseData();
    Super::BeginDestroy();
}
