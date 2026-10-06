// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObject/HMS_SmartObjectAIController.h"

#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "Components/HMS_PathFollowingComponent.h"

AHMS_SmartObjectAIController::AHMS_SmartObjectAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHMS_PathFollowingComponent>(TEXT("PathFollowingComponent")))
{
	SmartObjectInteractionComponent = CreateDefaultSubobject<UHMS_SmartObjectInteractionComponent>(
		TEXT("HMSSmartObjectInteraction"));
}
