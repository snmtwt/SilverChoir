// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "EHBDoorWindowOpeningSplineComponent.generated.h"

/**
 * Door/window opening outline constrained to the owner's local XZ plane.
 * The spline remains editable in Blueprint and level viewports.
 */
UCLASS(ClassGroup = (EasyHouse), meta = (BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBDoorWindowOpeningSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UEHBDoorWindowOpeningSplineComponent();

	void ConstrainToOpeningPlane();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
#endif

private:
	bool bApplyingOpeningPlaneConstraint = false;
};
