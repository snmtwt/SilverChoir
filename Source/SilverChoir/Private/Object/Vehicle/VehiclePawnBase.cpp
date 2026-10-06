#include "Object/Vehicle/VehiclePawnBase.h"

#include "Components/SceneComponent.h"

AVehiclePawnBase::AVehiclePawnBase()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
}

bool AVehiclePawnBase::InitializeVehicleId(FGuid InVehicleId)
{
    if (!InVehicleId.IsValid()) return false;
    VehicleId = InVehicleId;
    return true;
}
