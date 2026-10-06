#include "Object/Items/WearableItem.h"

#include "Object/Unit/UnitPawnBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SIS_InventoryContainerComponent.h"

namespace
{
AUnitPawnBase* ResolveWearer(const FSIS_EquipmentSlotEventData& EventData)
{
    if (auto* Unit = Cast<AUnitPawnBase>(EventData.TargetUserActor.Get())) return Unit;
    const auto* Inventory = EventData.TargetInventory.Get();
    return IsValid(Inventory) ? Cast<AUnitPawnBase>(Inventory->GetOwner()) : nullptr;
}
}

bool AWearableItem::OnAddToEquipmentSlot_Implementation(const FSIS_EquipmentSlotEventData& EventData)
{
    AUnitPawnBase* Unit = ResolveWearer(EventData);
    if (!IsValid(Unit) || !IsValid(WearMesh)) return false;

    USkeletalMeshComponent* Clothing = Unit->AddWearableMesh(
        WearMesh, EventData.ItemData.ItemId);
    if (!Clothing) return false;

    // The static display mesh and its materials are independent from the wearable mesh.
    return true;
}

void AWearableItem::OnRemoveFromEquipmentSlot_Implementation(
    const FSIS_EquipmentSlotEventData& EventData, bool& bHandledItemActorEvent)
{
    // We own only the unit's clothing component. SIS still owns any bound world-item actor.
    bHandledItemActorEvent = false;
    if (AUnitPawnBase* Unit = ResolveWearer(EventData); IsValid(Unit))
        Unit->RemoveWearableMesh(EventData.ItemData.ItemId);
}
