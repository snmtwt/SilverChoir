#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "UnitAIController.generated.h"

/** One path follower per unit; the player's controller remains attached to the camera. */
UCLASS(Blueprintable)
class SILVERCHOIR_API AUnitAIController : public AAIController
{
    GENERATED_BODY()
public:
    AUnitAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
