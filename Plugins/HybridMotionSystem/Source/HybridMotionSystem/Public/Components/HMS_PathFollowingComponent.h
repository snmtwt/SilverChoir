#pragma once

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "HMS_PathFollowingComponent.generated.h"

/** Keeps navigation braking in sync when HMS changes gait during an active path. */
UCLASS(ClassGroup=(HMS), meta=(BlueprintSpawnableComponent))
class HYBRIDMOTIONSYSTEM_API UHMS_PathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()
public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
};
