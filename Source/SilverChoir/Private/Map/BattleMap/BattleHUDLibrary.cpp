#include "Map/BattleMap/BattleHUDLibrary.h"
#include "Components/SIS_UnitInventoryComponent.h"

float UBattleHUDLibrary::SanitizeRatio(float Value) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 0.f; }
bool UBattleHUDLibrary::AreHandsLinked(FGuid A, FGuid B) { return A.IsValid() && A == B; }
FBattleDockLayout UBattleHUDLibrary::CalculateDockLayout(float Width, int32 Members, int32 SelectedIndex, EBattleDrawer Drawer, float Expansion, float MemberDrawerWidth, float VehicleDrawerWidth)
{
    FBattleDockLayout L;
    Width = FMath::IsFinite(Width) ? FMath::Max(200.f, Width) : 200.f;
    Members = FMath::Clamp(Members, 0, 128);
    L.ModeButtonsX = Width - 40.f;
    L.VehicleButtonX = L.ModeButtonsX - 40.f;
    const bool bMember = Drawer == EBattleDrawer::Member && SelectedIndex >= 0 && SelectedIndex < Members;
    const bool bVehicle = Drawer == EBattleDrawer::Vehicle;
    const float Desired = bMember ? MemberDrawerWidth : bVehicle ? VehicleDrawerWidth : 0.f;
    L.DrawerWidth = FMath::Clamp(FMath::IsFinite(Desired) ? Desired : 0.f, 0.f, Width * .35f) * SanitizeRatio(Expansion);
    const float Gap = 6.f;
    const float Area = FMath::Max(0.f, L.VehicleButtonX - 8.f - 48.f);
    const float DrawerGap = L.DrawerWidth > 0.f ? Gap : 0.f;
    L.MemberWidth = Members ? FMath::Max(0.f, (Area - L.DrawerWidth - DrawerGap - Gap * (Members - 1)) / Members) : 0.f;
    float X = 48.f;
    for (int32 I = 0; I < Members; ++I)
    {
        L.MemberX.Add(X); X += L.MemberWidth;
        if (bMember && I == SelectedIndex) { X += DrawerGap; L.DrawerX = X; X += L.DrawerWidth; }
        if (I + 1 < Members) X += Gap;
    }
    if (bVehicle) L.DrawerX = L.VehicleButtonX - 8.f - L.DrawerWidth;
    return L;
}
bool UBattleHUDLibrary::ReadHandEquipment(USIS_UnitInventoryComponent* Inventory, FGameplayTag LeftSlot, FGameplayTag RightSlot, FGuid& LeftItemId, FGuid& RightItemId, bool& bLinked)
{
    LeftItemId.Invalidate(); RightItemId.Invalidate(); bLinked = false;
    if (!IsValid(Inventory) || !LeftSlot.IsValid() || !RightSlot.IsValid() || LeftSlot == RightSlot) return false;
    FSIS_ItemData L, R; AActor* Actor = nullptr;
    if (Inventory->GetItemByEquipSlot(LeftSlot, 0, L, Actor)) LeftItemId = L.ItemId;
    if (Inventory->GetItemByEquipSlot(RightSlot, 0, R, Actor)) RightItemId = R.ItemId;
    bLinked = AreHandsLinked(LeftItemId, RightItemId);
    return true;
}
