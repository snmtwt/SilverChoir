#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "HMS_FallingMode.generated.h"

/** GASP Mover falling defaults packaged as a reusable native mode. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "HMS 下落模式"))
class HMS_MOVER_API UHMS_FallingMode : public UFallingMode
{
	GENERATED_BODY()

public:
	UHMS_FallingMode();
};
