#include "Object/Items/ItemBase.h"

#include "Components/SIS_ItemInventoryComponent.h"

AItemBase::AItemBase()
{
    PrimaryActorTick.bCanEverTick = false;

    ItemInventoryComponent = CreateDefaultSubobject<USIS_ItemInventoryComponent>(TEXT("ItemInventoryComponent"));
}
