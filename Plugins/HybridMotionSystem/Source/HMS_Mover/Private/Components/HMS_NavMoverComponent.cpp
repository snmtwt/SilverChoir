#include "Components/HMS_NavMoverComponent.h"

UHMS_NavMoverComponent::UHMS_NavMoverComponent()
{
	NavMovementProperties.bUseAccelerationForPaths = true;
}

void UHMS_NavMoverComponent::RequestPathMove(const FVector& MoveInput)
{
	// UE's base implementation normalizes this vector, discarding the path follower's slowdown.
	GameFrameNavMovementRequested = GFrameCounter;
	CachedNavMoveInputIntent = MoveInput.GetSafeNormal2D() * FMath::Clamp(MoveInput.Size(), 0., 1.);
	CachedNavMoveInputVelocity = FVector::ZeroVector;
	bRequestedBraking = MoveInput.SizeSquared() < FMath::Square(0.99f);
}

void UHMS_NavMoverComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	// Also accept zero: the base class ignores it and can retain a previous movement command.
	GameFrameNavMovementRequested = GFrameCounter;
	CachedNavMoveInputIntent = FVector::ZeroVector;
	CachedNavMoveInputVelocity = FVector(MoveVelocity.X, MoveVelocity.Y, 0);
	bRequestedBraking = !bForceMaxSpeed && MoveVelocity.Size2D() < RequestedMaxSpeed * 0.99f;
}

float UHMS_NavMoverComponent::GetPathFollowingBrakingDistance(float MaxSpeed) const
{
	// Overlap-based MoveTo can finish a capsule radius before the goal. Start
	// braking outside that reach region, especially for slow walking speeds.
	return FMath::Max(0.f, GetNavAgentPropertiesRef().AgentRadius)
		+ FMath::Max(10.f, FMath::Square(MaxSpeed) / (2.f * FMath::Max(BrakingDeceleration, 1.f))
		+ MaxSpeed * FMath::Max(BrakingLeadTime, 0.f));
}

bool UHMS_NavMoverComponent::IsNavigationBraking() const
{
	return bRequestedBraking && GFrameCounter <= GameFrameNavMovementRequested + 1;
}
