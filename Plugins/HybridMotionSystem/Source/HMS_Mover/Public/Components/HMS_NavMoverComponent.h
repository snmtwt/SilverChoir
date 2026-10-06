#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "HMS_NavMoverComponent.generated.h"

/** Preserves navigation's braking input for HMS movement and animation. */
UCLASS(ClassGroup=(HMS), meta=(BlueprintSpawnableComponent))
class HMS_MOVER_API UHMS_NavMoverComponent : public UNavMoverComponent
{

	GENERATED_BODY()
public:
	UHMS_NavMoverComponent();
	virtual void RequestPathMove(const FVector& MoveInput) override;
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	virtual float GetMaxSpeedForNavMovement() const override { return RequestedMaxSpeed; }
	virtual float GetPathFollowingBrakingDistance(float MaxSpeed) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Navigation", meta=(ClampMin="1", Units="cm/s"))
	float RequestedMaxSpeed = 375.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Navigation", meta=(ClampMin="1"))
	float BrakingDeceleration = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|Navigation", meta=(ClampMin="0", Units="s"))
	float BrakingLeadTime = 0.05f;

	/** Only current navigation requests count; a completed path cannot leave this latched. */
	UFUNCTION(BlueprintPure, Category="HMS|Navigation")
	bool IsNavigationBraking() const;
private:
	bool bRequestedBraking = false;
};
