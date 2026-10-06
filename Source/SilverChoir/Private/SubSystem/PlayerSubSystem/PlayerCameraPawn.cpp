#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "Components/SIS_UnitInventoryComponent.h"

APlayerCameraPawn::APlayerCameraPawn()
{
    // Mouse rotation is owned by BP_GameMainMapPlayerController's enhanced input.
    bEnableDefaultMouseRotationBindings = false;
    UnitInventoryComponent=CreateDefaultSubobject<USIS_UnitInventoryComponent>(TEXT("UnitInventoryComponent"));
}
