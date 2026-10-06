#include "Object/Items/StaticMeshItem.h"

#include "Components/StaticMeshComponent.h"

AStaticMeshItem::AStaticMeshItem()
{
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    SetRootComponent(MeshComponent);
}
