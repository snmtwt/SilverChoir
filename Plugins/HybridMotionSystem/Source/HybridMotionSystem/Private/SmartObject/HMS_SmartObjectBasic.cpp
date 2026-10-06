// Fill out your copyright notice in the Description page of Project Settings.

#include "SmartObject/HMS_SmartObjectBasic.h"
#include "Structs/HMS_MoverStructs.h"

AHMS_SmartObjectBasic::AHMS_SmartObjectBasic()
{
	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	PrimaryActorTick.bCanEverTick = false;
}

void AHMS_SmartObjectBasic::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void AHMS_SmartObjectBasic::BeginPlay()
{
	Super::BeginPlay();
	InitializeSmartObjectDefinition();

	if (bAutoActivateOnBeginPlay)
	{
		SetSmartObjectActive(true);
	}
}

void AHMS_SmartObjectBasic::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetSmartObjectActive(false);
	Super::EndPlay(EndPlayReason);
}

void AHMS_SmartObjectBasic::SetSmartObjectActive(bool bNewActive)
{
	if (!IsValid(SmartObjectComponent) || bNewActive == bIsActivated)
		return;

	bIsActivated = bNewActive;

	if (bNewActive)
		RegisterToSubsystem();
	else
		UnregisterFromSubsystem();
}

void AHMS_SmartObjectBasic::RegisterToSubsystem()
{
	if (USmartObjectSubsystem* Subsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>())
	{
		Subsystem->RegisterSmartObject(SmartObjectComponent);

	}
}

void AHMS_SmartObjectBasic::UnregisterFromSubsystem()
{
	if (USmartObjectSubsystem* Subsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>())
	{
		Subsystem->UnregisterSmartObject(SmartObjectComponent);

	}
}

void AHMS_SmartObjectBasic::InitializeSmartObjectDefinition_Implementation()
{
	if (IsValid(SmartObjectDefinition) && IsValid(SmartObjectComponent))
	{
		SmartObjectComponent->SetDefinition(SmartObjectDefinition);

	}
}

void AHMS_SmartObjectBasic::BroadcastUsageToHMS(const FGameplayTagContainer& UsageTags, EHMS_Gait SuggestedGait, EHMS_RotationMode SuggestedRotation)
{

}