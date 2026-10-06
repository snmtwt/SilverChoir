// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "HMS_SmartObjectAIController.generated.h"

class UHMS_SmartObjectInteractionComponent;

/** Minimal AIController host for the reusable HMS Smart Object interaction component. */
UCLASS(Blueprintable)
class HYBRIDMOTIONSYSTEM_API AHMS_SmartObjectAIController : public AAIController
{
	GENERATED_BODY()

public:
	AHMS_SmartObjectAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "HMS|Smart Object")
	UHMS_SmartObjectInteractionComponent* GetSmartObjectInteractionComponent() const
	{
		return SmartObjectInteractionComponent;
	}

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Smart Object")
	TObjectPtr<UHMS_SmartObjectInteractionComponent> SmartObjectInteractionComponent;
};
