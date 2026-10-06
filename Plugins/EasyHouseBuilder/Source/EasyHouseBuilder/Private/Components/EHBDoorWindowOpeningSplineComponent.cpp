// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/EHBDoorWindowOpeningSplineComponent.h"

#include "Actors/EHB_DoorWindow.h"

UEHBDoorWindowOpeningSplineComponent::UEHBDoorWindowOpeningSplineComponent()
{
	SetMobility(EComponentMobility::Movable);
	bEditableWhenInherited = true;
	bInputSplinePointsToConstructionScript = false;
	SetClosedLoop(true, false);
	SetDrawDebug(true);
	SetUnselectedSplineSegmentColor(FLinearColor(1.0f, 0.35f, 0.05f));
	SetSelectedSplineSegmentColor(FLinearColor(1.0f, 0.8f, 0.1f));
}

void UEHBDoorWindowOpeningSplineComponent::ConstrainToOpeningPlane()
{
	if (bApplyingOpeningPlaneConstraint)
	{
		return;
	}

	TGuardValue<bool> ConstraintGuard(bApplyingOpeningPlaneConstraint, true);
	bool bChanged = false;

	if (!GetRelativeTransform().Equals(FTransform::Identity))
	{
		SetRelativeTransform(FTransform::Identity);
		bChanged = true;
	}

	const int32 PointCount = GetNumberOfSplinePoints();
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		FVector PointLocation = GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local);
		if (!FMath::IsNearlyZero(PointLocation.Y))
		{
			PointLocation.Y = 0.0f;
			SetLocationAtSplinePoint(PointIndex, PointLocation, ESplineCoordinateSpace::Local, false);
			bChanged = true;
		}

		if (GetSplinePointType(PointIndex) != ESplinePointType::Linear)
		{
			SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
			bChanged = true;
		}
	}

	const bool bShouldBeClosed = PointCount >= 3;
	if (IsClosedLoop() != bShouldBeClosed)
	{
		SetClosedLoop(bShouldBeClosed, false);
		bChanged = true;
	}

	if (bChanged)
	{
		UpdateSpline();
	}
}

#if WITH_EDITOR
void UEHBDoorWindowOpeningSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bApplyingOpeningPlaneConstraint)
	{
		return;
	}

	ConstrainToOpeningPlane();
	if (AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(GetOwner()))
	{
		DoorWindow->HandleOpeningSplineEdited();
	}
}

void UEHBDoorWindowOpeningSplineComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

	ConstrainToOpeningPlane();
	if (bFinished)
	{
		if (AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(GetOwner()))
		{
			DoorWindow->HandleOpeningSplineEdited();
		}
	}
}
#endif
