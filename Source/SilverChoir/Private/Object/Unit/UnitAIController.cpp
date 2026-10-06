#include "Object/Unit/UnitAIController.h"
#include "Components/HMS_PathFollowingComponent.h"

AUnitAIController::AUnitAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UHMS_PathFollowingComponent>(TEXT("PathFollowingComponent")))
{
    bAllowStrafe = false;
}
