#include "Object/Items/SkeletalMeshItem.h"

#include "Components/SkeletalMeshComponent.h"

ASkeletalMeshItem::ASkeletalMeshItem()
{
    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
    SetRootComponent(MeshComponent);
}
