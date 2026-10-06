#include "Components/HMS_PathFollowingComponent.h"

void UHMS_PathFollowingComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	if (GetStatus() == EPathFollowingStatus::Moving)
	{
		// UE normally refreshes this only on path/segment changes. Its speed cache
		// makes this cheap, and updating before Super lets this frame brake correctly.
		UpdateDecelerationData();
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
