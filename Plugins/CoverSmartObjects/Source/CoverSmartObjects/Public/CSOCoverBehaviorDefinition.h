#pragma once

#include "CoreMinimal.h"
#include "SmartObjectDefinition.h"
#include "CSOCoverBehaviorDefinition.generated.h"

/** Marks a slot as cover. Movement, stance and weapon animation remain controlled by the user. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class COVERSMARTOBJECTS_API UCSOCoverBehaviorDefinition : public USmartObjectBehaviorDefinition
{
    GENERATED_BODY()
};
