#include "Map/BattleMap/BattleRosterLibrary.h"

#include "Engine/Texture2D.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"

namespace
{
    float ResourceRatio(float Current, float Maximum)
    {
        if (!FMath::IsFinite(Current) || !FMath::IsFinite(Maximum) || Maximum <= 0.f)
            return 0.f;
        // Clamp before dividing so a tiny positive maximum cannot overflow.
        return FMath::Clamp(Current, 0.f, Maximum) / Maximum;
    }

    void ReadSavedHandEquipment(const FUnitData& Unit, FBattleMemberView& View)
    {
        // Config/DefaultSIS_Setting.ini defines Item.Hands as a two-slot group.
        // Read its serialized placement, never infer held equipment from item type.
        const FName HandsTag(TEXT("Item.Hands"));
        for (const FSIS_ItemData& Item : Unit.InventoryItems)
        {
            const FSIS_ItemLocationInfo& Location = Item.ItemLocationInfo;
            if (!Item.ItemId.IsValid() || Location.SlotType != ESIS_SlotType::EquipmentSlot
                || Location.EquipmentSlotType.GetTagName() != HandsTag)
                continue;

            const bool bBothHands = Item.ItemTemplate.BasicProperty.bRequiresAllSlotsInGroup;
            const FSIS_ImageConfig& ImageConfig = Item.ItemTemplate.ImageConfig;
            UTexture2D* Image = ImageConfig.bUseSceneCapture ? nullptr : ImageConfig.StaticIcon;
            if ((bBothHands || Location.BelongSlotContainerIndex == 0) && !View.LeftItemId.IsValid())
            {
                View.LeftItemId = Item.ItemId;
                View.LeftItemImage = Image;
            }
            if ((bBothHands || Location.BelongSlotContainerIndex == 1) && !View.RightItemId.IsValid())
            {
                View.RightItemId = Item.ItemId;
                View.RightItemImage = Image;
            }
        }
    }
}

bool UBattleRosterLibrary::ResolveSquadViews(const UObject* WorldContextObject, const TArray<FGuid>& SquadIds,
    TArray<FBattleSquadView>& OutSquads, FText& OutError)
{
    OutSquads.Reset();
    OutError = FText::GetEmpty();
    if (SquadIds.IsEmpty()) return true;
    if (!IsInGameThread())
    {
        OutError = NSLOCTEXT("BattleRoster", "GameThreadRequired", "战斗小队数据必须在游戏线程读取。");
        return false;
    }

    const UPlayerSquadManagerBase* SquadManager = UPlayerSquadLibrary::GetPlayerSquadManager(WorldContextObject);
    const UPlayerUnitManagerBase* UnitManager = UPlayerUnitLibrary::GetPlayerUnitManager(WorldContextObject);
    if (!IsValid(SquadManager) || !SquadManager->IsReady() || !IsValid(UnitManager))
    {
        OutError = NSLOCTEXT("BattleRoster", "ManagersNotReady", "玩家小队或玩家单位系统尚未就绪。");
        return false;
    }

    TSet<FGuid> SeenSquads;
    TArray<FBattleSquadView> Resolved;
    Resolved.Reserve(SquadIds.Num());
    for (const FGuid SquadId : SquadIds)
    {
        if (!SquadId.IsValid())
        {
            OutError = NSLOCTEXT("BattleRoster", "InvalidSquadId", "小队 ID 列表包含无效 ID。");
            return false;
        }
        if (SeenSquads.Contains(SquadId)) continue;
        SeenSquads.Add(SquadId);

        FSquadData Squad;
        if (!SquadManager->GetSquad(SquadId, Squad))
        {
            OutError = FText::Format(NSLOCTEXT("BattleRoster", "MissingSquad", "玩家小队不存在：{0}"),
                FText::FromString(SquadId.ToString()));
            return false;
        }

        FBattleSquadView View;
        View.SquadId = SquadId;
        View.Name = Squad.SquadName;
        View.Icon = SquadManager->GetSquadIcon(SquadId);
        View.Members.Reserve(Squad.MemberUnitIds.Num());
        for (const FGuid UnitId : Squad.MemberUnitIds)
        {
            const TSharedPtr<const FUnitData> Unit = UnitManager->GetUnitDataShared(UnitId);
            if (!UnitId.IsValid() || !Unit.IsValid() || Unit->UnitId != UnitId)
            {
                OutError = FText::Format(NSLOCTEXT("BattleRoster", "MissingMember", "小队 {0} 的玩家成员不存在或 ID 无效：{1}"),
                    FText::FromString(SquadId.ToString()), FText::FromString(UnitId.ToString()));
                return false;
            }

            FBattleMemberView Member;
            Member.UnitId = UnitId;
            Member.Profile = Unit->Profile;
            Member.Health = ResourceRatio(Unit->RuntimeData.CurrentHealth, Unit->Attributes.MaxHealth);
            Member.Stamina = ResourceRatio(Unit->RuntimeData.CurrentStamina, Unit->Attributes.MaxStamina);
            // No morale/quick-slot authority exists. Clear the view's demo values.
            Member.Morale = 0.f;
            Member.QuickItemCounts.Init(0, 4);
            Member.LeftFallback = EBattleGlyph::EmptyHand;
            Member.RightFallback = EBattleGlyph::EmptyHand;
            ReadSavedHandEquipment(*Unit, Member);
            View.Members.Add(MoveTemp(Member));
        }

        // A squad assignment does not establish who has boarded the vehicle.
        View.Vehicle.Occupants = 0;
        View.Vehicle.Seats = 0;
        View.Vehicle.Condition = 0.f;
        View.Vehicle.Fuel = 0.f;
        if (Squad.HasAssignedVehicle())
        {
            const TSharedPtr<const FVehicleData> Vehicle = SquadManager->GetSquadVehicleShared(SquadId);
            if (!Vehicle.IsValid() || Vehicle->VehicleId != Squad.AssignedVehicle.VehicleId)
            {
                OutError = FText::Format(NSLOCTEXT("BattleRoster", "MissingVehicle", "小队 {0} 分配的玩家车辆不存在：{1}"),
                    FText::FromString(SquadId.ToString()), FText::FromString(Squad.AssignedVehicle.VehicleId.ToString()));
                return false;
            }
            View.Vehicle.VehicleId = Vehicle->VehicleId;
            View.Vehicle.Name = Vehicle->Profile.VehicleName;
            View.Vehicle.Image = Vehicle->Profile.PreviewImage;
            View.Vehicle.Condition = ResourceRatio(Vehicle->RuntimeData.CurrentDurability, Vehicle->Attributes.MaxDurability);
            View.Vehicle.Fuel = ResourceRatio(Vehicle->RuntimeData.CurrentFuel, Vehicle->Attributes.MaxFuel);
            View.Vehicle.Seats = FMath::Max(0, Vehicle->Attributes.PassengerCapacity);
        }
        Resolved.Add(MoveTemp(View));
    }

    OutSquads = MoveTemp(Resolved);
    return true;
}
