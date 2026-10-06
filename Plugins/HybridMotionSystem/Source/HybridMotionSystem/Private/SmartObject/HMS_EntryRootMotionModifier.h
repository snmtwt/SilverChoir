#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier.h"
#include "HMS_EntryRootMotionModifier.generated.h"

/** Authored root path with smooth endpoint and heading correction. */
UCLASS()
class UHMS_EntryRootMotionModifier : public URootMotionModifier
{
	GENERATED_BODY()
public:
	/** Incoming velocity minus authored velocity in world space. */
 FVector InitialVelocityCorrection = FVector::ZeroVector;
	FVector WorldDisplacement = FVector::ZeroVector;
	FQuat InitialMeshRotation = FQuat::Identity;
 float RotationCorrectionDegrees = 0.f;
	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
};
