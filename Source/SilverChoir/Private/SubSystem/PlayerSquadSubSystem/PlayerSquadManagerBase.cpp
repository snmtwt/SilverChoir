#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"

#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSettings.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "UObject/Class.h"

namespace
{
bool SquadError(FText& OutError, const TCHAR* Message)
{
    OutError = FText::FromString(Message);
    return false;
}

void EnsureCaptain(FSquadData& Squad)
{
    if (!Squad.MemberUnitIds.Contains(Squad.CaptainUnitId))
        Squad.CaptainUnitId = Squad.MemberUnitIds.IsEmpty() ? FGuid() : Squad.MemberUnitIds[0];
}

bool ValidateAssignedVehicle(const FSquadData& Squad, FText& OutError)
{
    if (!Squad.HasAssignedVehicle()) return true;
    if (Squad.AssignedVehicle.Attributes.PassengerCapacity < 0)
        return SquadError(OutError, TEXT("车辆载员上限不能为负数。"));
    if (Squad.TileId.IsNone() || Squad.AssignedVehicle.RuntimeData.TileId != Squad.TileId)
        return SquadError(OutError, TEXT("车辆与小队必须位于同一有效战略瓦片。"));
    return true;
}
}

void UPlayerSquadManagerBase::Initialize(UPlayerUnitManagerBase* InUnitManager)
{
    check(IsInGameThread());
    if (bChangingSquads) return;
    Shutdown();
    UnitManager = InUnitManager;
    if (IsValid(UnitManager))
    {
        UnitManager->OnUnitDataChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitDataChanged);
        UnitManager->OnVehicleDataReconcile.AddUObject(this, &ThisClass::HandleVehicleDataChanged);
    }
}

void UPlayerSquadManagerBase::Shutdown()
{
    check(IsInGameThread());
    if (bChangingSquads) return;
    if (IsValid(UnitManager))
    {
        UnitManager->OnUnitDataChanged.RemoveDynamic(this, &ThisClass::HandleUnitDataChanged);
        UnitManager->OnVehicleDataReconcile.RemoveAll(this);
    }
    Squads.Reset();
    UnitSquadIndex.Reset();
    VehicleSquadIndex.Reset();
    UnitManager = nullptr;
    OnSquadChanged.Clear();
    OnUnitSquadChanged.Clear();
    OnVehicleSquadChanged.Clear();
}

void UPlayerSquadManagerBase::BeginDestroy()
{
    Shutdown();
    Super::BeginDestroy();
}

UWorld* UPlayerSquadManagerBase::GetWorld() const
{
    return !IsTemplate() && IsValid(GetOuter()) ? GetOuter()->GetWorld() : nullptr;
}

bool UPlayerSquadManagerBase::IsReady() const { return IsValid(UnitManager); }

FText UPlayerSquadManagerBase::GetNextSquadName() const
{
    // 名称选择仅在创建草稿时执行；不维护会被取消操作消耗的全局游标。
    TSet<FString> UsedNames;
    for (const auto& Pair : Squads)
        UsedNames.Add(Pair.Value.SquadName.ToString().TrimStartAndEnd().ToLower());

    UDataTable* Table = GetDefault<UPlayerSquadSettings>()->SquadNameTable.LoadSynchronous();
    TArray<FName> Rows;
    if (IsValid(Table) && Table->GetRowStruct() == FSquadNameTemplate::StaticStruct())
    {
        Rows = Table->GetRowNames();
        Rows.Sort([Table](FName A, FName B)
        {
            const auto* Left = Table->FindRow<FSquadNameTemplate>(A, TEXT("SquadNameOrder"), false);
            const auto* Right = Table->FindRow<FSquadNameTemplate>(B, TEXT("SquadNameOrder"), false);
            if (Left && Right && Left->SortOrder != Right->SortOrder) return Left->SortOrder < Right->SortOrder;
            return A.LexicalLess(B);
        });
    }

    TSet<FString> ConfiguredNames;
    for (FName RowName : Rows)
    {
        const auto* Row = Table->FindRow<FSquadNameTemplate>(RowName, TEXT("NextSquadName"), false);
        if (!Row) continue;
        const FString Name = Row->SquadName.ToString().TrimStartAndEnd();
        const FString Key = Name.ToLower();
        if (Name.IsEmpty() || ConfiguredNames.Contains(Key)) continue;
        ConfiguredNames.Add(Key);
        if (!UsedNames.Contains(Key)) return FText::TrimPrecedingAndTrailing(Row->SquadName);
    }

    // 有效且不重复的配置名用完后接续编号；例如 24 个配置名之后从“第25小队”开始。
    // 无有效配置时从 1 开始，查询不预占编号，并跳过正式小队已经使用的名称。
    for (int64 Index = static_cast<int64>(ConfiguredNames.Num()) + 1; ; ++Index)
    {
        const FString Candidate = FString::Printf(TEXT("第%lld小队"), Index);
        if (!UsedNames.Contains(Candidate.ToLower())) return FText::FromString(Candidate);
    }
}

void UPlayerSquadManagerBase::HandleUnitDataChanged(FGuid UnitId)
{
    // Our own transactions broadcast once after every member/index has been committed.
    if (bChangingSquads || !IsReady()) return;
    const FGuid SquadId = UnitSquadIndex.FindRef(UnitId);
    if (!Squads.Contains(SquadId)) return;
    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    OnSquadChanged.Broadcast(SquadId);
}

bool UPlayerSquadManagerBase::CanWrite(FText& OutError) const
{
    check(IsInGameThread());
    OutError = FText::GetEmpty();
    if (!IsReady()) return SquadError(OutError, TEXT("玩家小队处理类尚未初始化。"));
    if (bChangingSquads || UnitManager->IsUpdatingUnitData() || UnitManager->IsUpdatingVehicleData())
        return SquadError(OutError, TEXT("正在更新或通知玩家数据，请在本次通知完成后再修改小队。"));
    return true;
}

bool UPlayerSquadManagerBase::ResolveAssignedVehicle(FSquadData& Squad, FText& OutError) const
{
    if (!Squad.HasAssignedVehicle())
    {
        Squad.AssignedVehicle = FVehicleData();
        return true;
    }
    const auto Vehicle = UnitManager->GetVehicleDataShared(Squad.AssignedVehicle.VehicleId);
    if (!Vehicle || Vehicle->VehicleId != Squad.AssignedVehicle.VehicleId)
        return SquadError(OutError, TEXT("车辆不存在，请先将车辆数据加载到玩家单位子系统。"));
    if (Vehicle->IsNotifyingDataChanged())
        return SquadError(OutError, TEXT("车辆正在发送数据修改通知，请在通知完成后再修改小队。"));
    // VehicleId is the only input from a draft/save. Never write stale or forged payloads back.
    Squad.AssignedVehicle = *Vehicle;
    return ValidateAssignedVehicle(Squad, OutError);
}

void UPlayerSquadManagerBase::HandleVehicleDataChanged(const TArray<FGuid>& VehicleIds)
{
    if (bChangingSquads || !IsReady()) return;
    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    TMap<FGuid, FGuid> RemovedUnits;
    TMap<FGuid, FGuid> RemovedVehicles;
    TSet<FGuid> ChangedSquads;
    for (const FGuid VehicleId : VehicleIds)
    {
        const FGuid SquadId = VehicleSquadIndex.FindRef(VehicleId);
        FSquadData* Squad = Squads.Find(SquadId);
        if (!Squad) continue;
        const auto Vehicle = UnitManager->GetVehicleDataShared(VehicleId);
        const bool bRetain = Vehicle && Vehicle->VehicleId == VehicleId
            && !Squad->TileId.IsNone() && Vehicle->RuntimeData.TileId == Squad->TileId
            && Vehicle->Attributes.PassengerCapacity >= 0;
        if (bRetain && FVehicleData::StaticStruct()->CompareScriptStruct(&Squad->AssignedVehicle, Vehicle.Get(), 0)) continue;
        if (bRetain) Squad->AssignedVehicle = *Vehicle;
        else
        {
            // Explicitly moving/removing a vehicle does not teleport its squad. Release it instead.
            Squad->AssignedVehicle = FVehicleData();
            VehicleSquadIndex.Remove(VehicleId);
            RemovedVehicles.Add(VehicleId, SquadId);
        }
        for (const FGuid UnitId : Squad->TrimMembersToCapacity())
        {
            UnitSquadIndex.Remove(UnitId);
            RemovedUnits.Add(UnitId, SquadId);
        }
        ChangedSquads.Add(SquadId);
    }
    // Reconcile the entire batch before the first observer sees any changed record.
    for (const auto& Pair : RemovedUnits)
    {
        if (const auto Unit = UnitManager->GetUnitDataShared(Pair.Key)) Unit->NotifyDataChanged();
        OnUnitSquadChanged.Broadcast(Pair.Key, Pair.Value, FGuid());
    }
    for (const auto& Pair : RemovedVehicles) OnVehicleSquadChanged.Broadcast(Pair.Key, Pair.Value, FGuid());
    for (const FGuid SquadId : ChangedSquads) OnSquadChanged.Broadcast(SquadId);
}

bool UPlayerSquadManagerBase::ResolveUnitsForWrite(const TArray<FGuid>& Ids, TArray<TSharedPtr<FUnitData>>& OutUnits, FText& OutError) const
{
    OutUnits.Reset();
    OutUnits.Reserve(Ids.Num());
    for (const FGuid Id : Ids)
    {
        const auto Unit = UnitManager->GetUnitDataShared(Id);
        if (!Unit || Unit->UnitId != Id)
            return SquadError(OutError, TEXT("成员单位不存在，请先加载有效的玩家单位数据。"));
        if (Unit->IsNotifyingDataChanged())
            return SquadError(OutError, TEXT("成员单位正在发送数据修改通知，请在通知完成后再修改小队。"));
        OutUnits.Add(Unit);
    }
    return true;
}

bool UPlayerSquadManagerBase::CreateSquad(FText SquadName, UTexture2D* SquadIcon, FName TileId, FGuid& OutSquadId, FText& OutError)
{
    OutSquadId.Invalidate();
    if (!CanWrite(OutError)) return false;
    if (SquadName.ToString().TrimStartAndEnd().IsEmpty()) return SquadError(OutError, TEXT("小队名称不能为空。"));
    if (TileId.IsNone()) return SquadError(OutError, TEXT("创建小队需要有效的当前瓦片ID。"));

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    FSquadData Squad;
    do { Squad.SquadId = FGuid::NewGuid(); } while (Squads.Contains(Squad.SquadId));
    Squad.SquadName = SquadName;
    Squad.SquadIcon = SquadIcon;
    Squad.TileId = TileId;
    OutSquadId = Squad.SquadId;
    Squads.Add(Squad.SquadId, Squad);
    OnSquadChanged.Broadcast(OutSquadId);
    return true;
}

bool UPlayerSquadManagerBase::CommitSquadDraft(const FSquadData& Draft, const FSquadData& OriginalSquad,
    bool bCreateNew, FGuid& OutSquadId, FText& OutError)
{
    OutSquadId.Invalidate();
    if (!CanWrite(OutError)) return false;

    const FSquadData* Current = nullptr;
    if (bCreateNew)
    {
        if (Draft.SquadId.IsValid())
            return SquadError(OutError, TEXT("新小队草稿不能预先指定小队ID。"));
    }
    else
    {
        if (!Draft.SquadId.IsValid() || Draft.SquadId != OriginalSquad.SquadId)
            return SquadError(OutError, TEXT("小队草稿与原始快照的ID不一致。"));
        Current = Squads.Find(Draft.SquadId);
        if (!Current) return SquadError(OutError, TEXT("正在编辑的小队已不存在，请重新选择小队。"));
        if (!Current->MatchesSnapshot(OriginalSquad))
            return SquadError(OutError, TEXT("小队已在其他操作中发生改变，请重新打开后编辑；本次草稿未保存。"));
        if (Draft.TileId != Current->TileId)
            return SquadError(OutError, TEXT("小队草稿不能修改战略位置，请重新打开当前瓦片中的小队。"));
    }

    if (Draft.SquadName.ToString().TrimStartAndEnd().IsEmpty())
        return SquadError(OutError, TEXT("小队名称不能为空。"));
    if (Draft.TileId.IsNone())
        return SquadError(OutError, TEXT("保存小队需要有效的当前瓦片ID。"));
    if (Draft.IconSource != ESquadIconSource::Preset && Draft.IconSource != ESquadIconSource::CaptainPortrait)
        return SquadError(OutError, TEXT("小队图标来源无效。"));
    FSquadData ResolvedDraft = Draft;
    if (!ResolveAssignedVehicle(ResolvedDraft, OutError)) return false;
    if (Draft.MemberUnitIds.Num() > ResolvedDraft.GetMaxMemberCount())
        return SquadError(OutError, TEXT("小队成员超过人数上限，请先移出超员成员。"));
    if (Draft.HasAssignedVehicle())
    {
        const FGuid OwnerId = VehicleSquadIndex.FindRef(Draft.AssignedVehicle.VehicleId);
        if (OwnerId.IsValid() && OwnerId != Draft.SquadId)
            return SquadError(OutError, TEXT("车辆已分配给另一小队，未保存任何修改。"));
    }

    TSet<FGuid> RequestedIds;
    TSet<FGuid> AffectedSquadIds;
    if (Current) AffectedSquadIds.Add(Current->SquadId);
    for (const FGuid UnitId : Draft.MemberUnitIds)
    {
        if (!UnitId.IsValid() || RequestedIds.Contains(UnitId))
            return SquadError(OutError, TEXT("小队草稿包含无效或重复的成员ID，未保存任何修改。"));
        RequestedIds.Add(UnitId);
        const FGuid PreviousId = UnitSquadIndex.FindRef(UnitId);
        if (PreviousId.IsValid())
        {
            const FSquadData* Previous = Squads.Find(PreviousId);
            if (!Previous || !Previous->MemberUnitIds.Contains(UnitId))
                return SquadError(OutError, TEXT("成员原小队索引无效，未保存任何修改。"));
            AffectedSquadIds.Add(PreviousId);
        }
    }
    if ((RequestedIds.IsEmpty() && Draft.CaptainUnitId.IsValid())
        || (!RequestedIds.IsEmpty() && (!Draft.CaptainUnitId.IsValid() || !RequestedIds.Contains(Draft.CaptainUnitId))))
        return SquadError(OutError, TEXT("非空小队必须从当前草稿成员中指定一名队长，空小队不能指定队长。"));

    // Include members who stay in donor squads: captain repairs affect their displayed data,
    // and a notifying or invalid canonical record must reject the entire transaction.
    TSet<FGuid> AffectedUnitIds = RequestedIds;
    for (const FGuid SquadId : AffectedSquadIds)
    {
        const FSquadData& Affected = Squads.FindChecked(SquadId);
        TSet<FGuid> Seen;
        for (const FGuid UnitId : Affected.MemberUnitIds)
        {
            if (!UnitId.IsValid() || Seen.Contains(UnitId) || UnitSquadIndex.FindRef(UnitId) != SquadId)
                return SquadError(OutError, TEXT("小队成员与归属索引不一致，未保存任何修改。"));
            Seen.Add(UnitId);
            AffectedUnitIds.Add(UnitId);
        }
    }
    TArray<FGuid> UnitIds = AffectedUnitIds.Array();
    UnitIds.Sort();
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(UnitIds, Units, OutError)) return false;
    TMap<FGuid, FGuid> PreviousMemberships;
    for (const auto& Unit : Units)
    {
        const FGuid PreviousId = UnitSquadIndex.FindRef(Unit->UnitId);
        PreviousMemberships.Add(Unit->UnitId, PreviousId);
        if ((RequestedIds.Contains(Unit->UnitId) && Unit->RuntimeData.TileId != Draft.TileId)
            || (PreviousId.IsValid() && Unit->RuntimeData.TileId != Squads.FindChecked(PreviousId).TileId))
            return SquadError(OutError, TEXT("小队与成员必须位于同一有效战略瓦片，未保存任何修改。"));
    }

    // Stage only squad records and IDs. FUnitData always remains in its canonical allocation.
    TMap<FGuid, FSquadData> UpdatedDonors;
    for (const FGuid SquadId : AffectedSquadIds)
    {
        if (SquadId == Draft.SquadId) continue;
        FSquadData Donor = Squads.FindChecked(SquadId);
        Donor.MemberUnitIds.RemoveAll([&RequestedIds](const FGuid& UnitId) { return RequestedIds.Contains(UnitId); });
        EnsureCaptain(Donor);
        UpdatedDonors.Add(SquadId, MoveTemp(Donor));
    }
    FSquadData Committed = MoveTemp(ResolvedDraft);
    const FGuid PreviousVehicleId = Current ? Current->AssignedVehicle.VehicleId : FGuid();
    const FGuid NewVehicleId = Committed.AssignedVehicle.VehicleId;

    // All validation is complete; allocate a new identity only inside the successful commit.
    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    if (bCreateNew)
    {
        do { Committed.SquadId = FGuid::NewGuid(); } while (Squads.Contains(Committed.SquadId));
    }
    if (Current)
    {
        for (const FGuid UnitId : Current->MemberUnitIds)
            if (!RequestedIds.Contains(UnitId)) UnitSquadIndex.Remove(UnitId);
    }
    for (auto& Pair : UpdatedDonors) Squads.FindChecked(Pair.Key) = MoveTemp(Pair.Value);
    OutSquadId = Committed.SquadId;
    Squads.Add(OutSquadId, MoveTemp(Committed));
    for (const FGuid UnitId : Draft.MemberUnitIds) UnitSquadIndex.Add(UnitId, OutSquadId);
    if (PreviousVehicleId != NewVehicleId) VehicleSquadIndex.Remove(PreviousVehicleId);
    if (NewVehicleId.IsValid()) VehicleSquadIndex.Add(NewVehicleId, OutSquadId);

    // Every observer can now query the final target, repaired donors, and reverse index.
    for (const auto& Unit : Units)
    {
        Unit->NotifyDataChanged();
        const FGuid PreviousId = PreviousMemberships.FindRef(Unit->UnitId);
        const FGuid CurrentId = UnitSquadIndex.FindRef(Unit->UnitId);
        if (PreviousId != CurrentId) OnUnitSquadChanged.Broadcast(Unit->UnitId, PreviousId, CurrentId);
    }
    if (PreviousVehicleId != NewVehicleId)
    {
        if (const auto Vehicle = UnitManager->GetVehicleDataShared(PreviousVehicleId))
        {
            Vehicle->NotifyDataChanged();
            OnVehicleSquadChanged.Broadcast(PreviousVehicleId, OutSquadId, FGuid());
        }
        if (const auto Vehicle = UnitManager->GetVehicleDataShared(NewVehicleId))
        {
            Vehicle->NotifyDataChanged();
            OnVehicleSquadChanged.Broadcast(NewVehicleId, FGuid(), OutSquadId);
        }
    }
    AffectedSquadIds.Add(OutSquadId);
    TArray<FGuid> SquadIds = AffectedSquadIds.Array();
    SquadIds.Sort();
    for (const FGuid SquadId : SquadIds) OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::SetSquadVehicle(FGuid SquadId, const FVehicleData& Vehicle,
    TArray<FGuid>& OutRemoved, FText& OutError)
{
    OutRemoved.Reset();
    if (!CanWrite(OutError)) return false;
    const FSquadData* Current = Squads.Find(SquadId);
    if (!Current) return SquadError(OutError, TEXT("小队不存在。"));
    if (!Vehicle.VehicleId.IsValid()) return SquadError(OutError, TEXT("分配车辆需要有效的车辆ID。"));
    const FSquadData Original = *Current;
    FSquadData Draft = Original;
    Draft.AssignedVehicle = Vehicle;
    if (!ResolveAssignedVehicle(Draft, OutError)) return false;
    TArray<FGuid> Removed = Draft.TrimMembersToCapacity();
    FGuid SavedId;
    if (!CommitSquadDraft(Draft, Original, false, SavedId, OutError)) return false;
    OutRemoved = MoveTemp(Removed);
    return true;
}

bool UPlayerSquadManagerBase::ClearSquadVehicle(FGuid SquadId, TArray<FGuid>& OutRemoved, FText& OutError)
{
    OutRemoved.Reset();
    if (!CanWrite(OutError)) return false;
    const FSquadData* Current = Squads.Find(SquadId);
    if (!Current) return SquadError(OutError, TEXT("小队不存在。"));
    const FSquadData Original = *Current;
    FSquadData Draft = Original;
    Draft.AssignedVehicle = FVehicleData();
    TArray<FGuid> Removed = Draft.TrimMembersToCapacity();
    FGuid SavedId;
    if (!CommitSquadDraft(Draft, Original, false, SavedId, OutError)) return false;
    OutRemoved = MoveTemp(Removed);
    return true;
}

int32 UPlayerSquadManagerBase::GetSquadMaxMemberCount(FGuid SquadId) const
{
    const FSquadData* Squad = IsReady() ? Squads.Find(SquadId) : nullptr;
    return Squad ? Squad->GetMaxMemberCount() : 0;
}

bool UPlayerSquadManagerBase::UpdateSquadInfo(FGuid SquadId, FText SquadName, UTexture2D* SquadIcon, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    FSquadData* Squad = Squads.Find(SquadId);
    if (!Squad) return SquadError(OutError, TEXT("小队不存在。"));
    if (SquadName.ToString().TrimStartAndEnd().IsEmpty()) return SquadError(OutError, TEXT("小队名称不能为空。"));
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(Squad->MemberUnitIds, Units, OutError)) return false;

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    Squad->SquadName = SquadName;
    Squad->SquadIcon = SquadIcon;
    for (const auto& Unit : Units) Unit->NotifyDataChanged();
    OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::SetSquadCaptain(FGuid SquadId, FGuid UnitId, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    FSquadData* Squad = Squads.Find(SquadId);
    if (!Squad) return SquadError(OutError, TEXT("小队不存在。"));
    if (!UnitId.IsValid() || !Squad->MemberUnitIds.Contains(UnitId) || UnitSquadIndex.FindRef(UnitId) != SquadId)
        return SquadError(OutError, TEXT("队长必须是当前小队中的成员。"));
    if (Squad->CaptainUnitId == UnitId) return true;
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(Squad->MemberUnitIds, Units, OutError)) return false;

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    Squad->CaptainUnitId = UnitId;
    for (const auto& Unit : Units) Unit->NotifyDataChanged();
    OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::SetSquadIconSource(FGuid SquadId, ESquadIconSource IconSource, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    FSquadData* Squad = Squads.Find(SquadId);
    if (!Squad) return SquadError(OutError, TEXT("小队不存在。"));
    if (IconSource != ESquadIconSource::Preset && IconSource != ESquadIconSource::CaptainPortrait)
        return SquadError(OutError, TEXT("小队图标来源无效。"));
    if (Squad->IconSource == IconSource) return true;
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(Squad->MemberUnitIds, Units, OutError)) return false;

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    Squad->IconSource = IconSource;
    OnSquadChanged.Broadcast(SquadId);
    return true;
}

UTexture2D* UPlayerSquadManagerBase::GetSquadIcon(FGuid SquadId) const
{
    const FSquadData* Squad = IsReady() ? Squads.Find(SquadId) : nullptr;
    if (!Squad) return nullptr;
    if (Squad->IconSource == ESquadIconSource::CaptainPortrait)
    {
        const auto Captain = UnitManager->GetUnitDataShared(Squad->CaptainUnitId);
        if (Captain && IsValid(Captain->Profile.PortraitTexture))
            return Captain->Profile.PortraitTexture;
    }
    return Squad->SquadIcon;
}

bool UPlayerSquadManagerBase::RemoveSquad(FGuid SquadId, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    const FSquadData* Squad = Squads.Find(SquadId);
    if (!Squad) return SquadError(OutError, TEXT("小队不存在。"));
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(Squad->MemberUnitIds, Units, OutError)) return false;
    const FGuid VehicleId = Squad->AssignedVehicle.VehicleId;
    const auto Vehicle = UnitManager->GetVehicleDataShared(VehicleId);

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    for (const auto& Unit : Units) UnitSquadIndex.Remove(Unit->UnitId);
    VehicleSquadIndex.Remove(VehicleId);
    Squads.Remove(SquadId);
    for (const auto& Unit : Units)
    {
        Unit->NotifyDataChanged();
        OnUnitSquadChanged.Broadcast(Unit->UnitId, SquadId, FGuid());
    }
    if (Vehicle)
    {
        Vehicle->NotifyDataChanged();
        OnVehicleSquadChanged.Broadcast(VehicleId, SquadId, FGuid());
    }
    OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::AddUnitToSquad(FGuid UnitId, FGuid SquadId, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    FSquadData* Destination = Squads.Find(SquadId);
    if (!Destination) return SquadError(OutError, TEXT("目标小队不存在。"));
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite({UnitId}, Units, OutError)) return false;
    const auto& Unit = Units[0];
    if (Destination->TileId.IsNone() || Unit->RuntimeData.TileId.IsNone() || Destination->TileId != Unit->RuntimeData.TileId)
        return SquadError(OutError, TEXT("单位与目标小队必须位于同一有效战略瓦片，原有归属未改变。"));
    const FGuid PreviousId = UnitSquadIndex.FindRef(UnitId);
    if (PreviousId == SquadId) return true;
    if (Destination->MemberUnitIds.Num() >= Destination->GetMaxMemberCount())
        return SquadError(OutError, TEXT("目标小队已满员，原有归属未改变。"));
    FSquadData* Previous = PreviousId.IsValid() ? Squads.Find(PreviousId) : nullptr;
    if (PreviousId.IsValid() && !Previous) return SquadError(OutError, TEXT("单位原小队索引无效，未修改归属。"));

    // Validate first; no callbacks occur until both sides of the transfer are committed.
    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    if (Previous)
    {
        Previous->MemberUnitIds.Remove(UnitId);
        EnsureCaptain(*Previous);
    }
    Destination->MemberUnitIds.Add(UnitId);
    EnsureCaptain(*Destination);
    UnitSquadIndex.Add(UnitId, SquadId);
    Unit->NotifyDataChanged();
    OnUnitSquadChanged.Broadcast(UnitId, PreviousId, SquadId);
    if (Previous) OnSquadChanged.Broadcast(PreviousId);
    OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::RemoveUnitFromSquad(FGuid UnitId, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite({UnitId}, Units, OutError)) return false;
    const FGuid PreviousId = UnitSquadIndex.FindRef(UnitId);
    if (!PreviousId.IsValid()) return true;
    FSquadData* Previous = Squads.Find(PreviousId);
    if (!Previous) return SquadError(OutError, TEXT("单位原小队索引无效，未修改归属。"));

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    Previous->MemberUnitIds.Remove(UnitId);
    EnsureCaptain(*Previous);
    UnitSquadIndex.Remove(UnitId);
    Units[0]->NotifyDataChanged();
    OnUnitSquadChanged.Broadcast(UnitId, PreviousId, FGuid());
    OnSquadChanged.Broadcast(PreviousId);
    return true;
}

bool UPlayerSquadManagerBase::SetSquadTileId(FGuid SquadId, FName TileId, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    FSquadData* Squad = Squads.Find(SquadId);
    if (!Squad) return SquadError(OutError, TEXT("小队不存在。"));
    if (TileId.IsNone()) return SquadError(OutError, TEXT("小队当前瓦片ID不能为空。"));
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(Squad->MemberUnitIds, Units, OutError)) return false;
    const auto Vehicle = GetSquadVehicleShared(SquadId);
    if (Squad->HasAssignedVehicle() && (!Vehicle || Vehicle->IsNotifyingDataChanged()))
        return SquadError(OutError, TEXT("小队车辆不存在或正在发送数据通知，未移动小队。"));

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    bool bChanged = Squad->TileId != TileId;
    Squad->TileId = TileId;
    const bool bVehicleChanged = Vehicle && Vehicle->RuntimeData.TileId != TileId;
    if (bVehicleChanged)
    {
        Vehicle->RuntimeData.TileId = TileId;
        Squad->AssignedVehicle = *Vehicle;
        bChanged = true;
    }
    TArray<TSharedPtr<FUnitData>> ChangedUnits;
    for (const auto& Unit : Units)
    {
        if (Unit->RuntimeData.TileId == TileId) continue;
        Unit->RuntimeData.TileId = TileId;
        ChangedUnits.Add(Unit);
        bChanged = true;
    }
    // All shared records are updated before the first UI/actor receives a notification.
    for (const auto& Unit : ChangedUnits) Unit->NotifyDataChanged();
    if (bVehicleChanged) Vehicle->NotifyDataChanged();
    if (bChanged) OnSquadChanged.Broadcast(SquadId);
    return true;
}

bool UPlayerSquadManagerBase::LoadSquadData(const TArray<FSquadData>& Data, FText& OutError)
{
    if (!CanWrite(OutError)) return false;
    TMap<FGuid, FSquadData> NewSquads;
    TMap<FGuid, FGuid> NewIndex;
    TMap<FGuid, FGuid> NewVehicleIndex;
    TSet<FGuid> AffectedIds;
    TSet<FGuid> AffectedSquads;
    for (const FSquadData& Squad : Data)
    {
        if (!Squad.SquadId.IsValid() || NewSquads.Contains(Squad.SquadId))
            return SquadError(OutError, TEXT("小队ID无效或重复，未加载任何小队。"));
        if (Squad.TileId.IsNone() || Squad.SquadName.ToString().TrimStartAndEnd().IsEmpty())
            return SquadError(OutError, TEXT("小队名称和当前瓦片ID不能为空，未加载任何小队。"));
        if (Squad.IconSource != ESquadIconSource::Preset && Squad.IconSource != ESquadIconSource::CaptainPortrait)
            return SquadError(OutError, TEXT("小队图标来源无效，未加载任何小队。"));
        if (Squad.CaptainUnitId.IsValid() && !Squad.MemberUnitIds.Contains(Squad.CaptainUnitId))
            return SquadError(OutError, TEXT("队长必须是当前小队中的成员，未加载任何小队。"));
        FSquadData Resolved = Squad;
        if (!ResolveAssignedVehicle(Resolved, OutError)) return false;
        if (Squad.HasAssignedVehicle())
        {
            if (NewVehicleIndex.Contains(Squad.AssignedVehicle.VehicleId))
                return SquadError(OutError, TEXT("一辆车只能分配给一个小队，车辆ID重复，未加载任何小队。"));
            NewVehicleIndex.Add(Squad.AssignedVehicle.VehicleId, Squad.SquadId);
        }
        TArray<TSharedPtr<FUnitData>> Units;
        if (!ResolveUnitsForWrite(Squad.MemberUnitIds, Units, OutError)) return false;
        for (const auto& Unit : Units)
        {
            if (NewIndex.Contains(Unit->UnitId)) return SquadError(OutError, TEXT("一个单位只能加入一个小队，成员ID重复，未加载任何小队。"));
            if (Unit->RuntimeData.TileId != Squad.TileId) return SquadError(OutError, TEXT("小队与成员的瓦片位置不一致，未加载任何小队。"));
            NewIndex.Add(Unit->UnitId, Squad.SquadId);
            AffectedIds.Add(Unit->UnitId);
        }
        // Keep the complete input until every original member and vehicle has been validated.
        // Otherwise trimming could hide a duplicate, missing, or displaced trailing member.
        NewSquads.Add(Squad.SquadId, MoveTemp(Resolved));
        AffectedSquads.Add(Squad.SquadId);
    }
    for (const auto& Pair : UnitSquadIndex) AffectedIds.Add(Pair.Key);
    for (const auto& Pair : Squads) AffectedSquads.Add(Pair.Key);
    TArray<FGuid> UnitIds = AffectedIds.Array();
    UnitIds.Sort();
    TArray<TSharedPtr<FUnitData>> Units;
    if (!ResolveUnitsForWrite(UnitIds, Units, OutError)) return false;

    // Older saves have no captain/vehicle fields. Repair the captain before capacity trimming,
    // then rebuild the reverse index from retained IDs; removed units stay in canonical storage.
    NewIndex.Reset();
    for (auto& Pair : NewSquads)
    {
        EnsureCaptain(Pair.Value);
        Pair.Value.TrimMembersToCapacity();
        for (const FGuid UnitId : Pair.Value.MemberUnitIds) NewIndex.Add(UnitId, Pair.Key);
    }

    TGuardValue<bool> Guard(bChangingSquads, true);
    TGuardValue<bool> VehicleGuard(UnitManager->bUpdatingVehicleData, true);
    const TMap<FGuid, FGuid> PreviousIndex = MoveTemp(UnitSquadIndex);
    const TMap<FGuid, FGuid> PreviousVehicleIndex = MoveTemp(VehicleSquadIndex);
    Squads = MoveTemp(NewSquads);
    UnitSquadIndex = MoveTemp(NewIndex);
    VehicleSquadIndex = MoveTemp(NewVehicleIndex);
    for (const auto& Unit : Units)
    {
        Unit->NotifyDataChanged();
        const FGuid PreviousId = PreviousIndex.FindRef(Unit->UnitId);
        const FGuid CurrentId = UnitSquadIndex.FindRef(Unit->UnitId);
        if (PreviousId != CurrentId) OnUnitSquadChanged.Broadcast(Unit->UnitId, PreviousId, CurrentId);
    }
    TSet<FGuid> VehicleIds;
    for (const auto& Pair : PreviousVehicleIndex) VehicleIds.Add(Pair.Key);
    for (const auto& Pair : VehicleSquadIndex) VehicleIds.Add(Pair.Key);
    for (const FGuid VehicleId : VehicleIds)
    {
        const FGuid PreviousId = PreviousVehicleIndex.FindRef(VehicleId);
        const FGuid CurrentId = VehicleSquadIndex.FindRef(VehicleId);
        if (PreviousId == CurrentId) continue;
        if (const auto Vehicle = UnitManager->GetVehicleDataShared(VehicleId)) Vehicle->NotifyDataChanged();
        OnVehicleSquadChanged.Broadcast(VehicleId, PreviousId, CurrentId);
    }
    TArray<FGuid> SquadIds = AffectedSquads.Array();
    SquadIds.Sort();
    for (const FGuid Id : SquadIds) OnSquadChanged.Broadcast(Id);
    return true;
}

bool UPlayerSquadManagerBase::GetSquad(FGuid SquadId, FSquadData& OutSquad) const
{
    const FSquadData* Found = IsReady() ? Squads.Find(SquadId) : nullptr;
    OutSquad = Found ? *Found : FSquadData();
    return Found != nullptr;
}

bool UPlayerSquadManagerBase::GetUnitSquad(FGuid UnitId, FSquadData& OutSquad) const
{
    return GetSquad(UnitSquadIndex.FindRef(UnitId), OutSquad);
}

bool UPlayerSquadManagerBase::GetVehicleSquad(FGuid VehicleId, FSquadData& OutSquad) const
{
    return GetSquad(VehicleSquadIndex.FindRef(VehicleId), OutSquad);
}

TSharedPtr<FVehicleData> UPlayerSquadManagerBase::GetSquadVehicleShared(FGuid SquadId) const
{
    const FSquadData* Squad = IsReady() ? Squads.Find(SquadId) : nullptr;
    return Squad && Squad->HasAssignedVehicle() ? UnitManager->GetVehicleDataShared(Squad->AssignedVehicle.VehicleId) : nullptr;
}

TArray<FGuid> UPlayerSquadManagerBase::GetSquadIds() const
{
    TArray<FGuid> Ids;
    if (IsReady()) Squads.GetKeys(Ids);
    Ids.Sort();
    return Ids;
}

TArray<FSquadData> UPlayerSquadManagerBase::GetAllSquads() const
{
    TArray<FSquadData> Result;
    Result.Reserve(Squads.Num());
    for (const FGuid Id : GetSquadIds()) Result.Add(Squads.FindChecked(Id));
    return Result;
}

TArray<TSharedPtr<FUnitData>> UPlayerSquadManagerBase::GetSquadUnitsShared(FGuid SquadId) const
{
    TArray<TSharedPtr<FUnitData>> Result;
    if (const FSquadData* Squad = IsReady() ? Squads.Find(SquadId) : nullptr)
    {
        Result.Reserve(Squad->MemberUnitIds.Num());
        for (const FGuid Id : Squad->MemberUnitIds)
        {
            if (auto Unit = UnitManager->GetUnitDataShared(Id)) Result.Add(MoveTemp(Unit));
        }
    }
    return Result;
}
