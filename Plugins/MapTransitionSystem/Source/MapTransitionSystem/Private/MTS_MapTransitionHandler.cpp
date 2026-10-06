#include "MTS_MapTransitionHandler.h"

#include "Engine/GameInstance.h"

UWorld* UMTS_MapTransitionHandler::GetWorld() const
{
	return OwningGameInstance ? OwningGameInstance->GetWorld() : nullptr;
}

void UMTS_MapTransitionHandler::InitializeTransitionHandler(
	UGameInstance* InGameInstance,
	const FMTS_MapTransitionRequest& InRequest)
{
	OwningGameInstance = InGameInstance;
	TransitionRequest = InRequest;
}

bool UMTS_MapTransitionHandler::PreloadMapResources_Implementation(
	UMTS_MapTransitionSubsystem* TransitionSubsystem,
	const FMTS_MapTransitionRequest& Request)
{
	return true;
}

void UMTS_MapTransitionHandler::OnMapLoaded_Implementation(
	UMTS_MapTransitionSubsystem* TransitionSubsystem,
	const FMTS_MapTransitionRequest& Request)
{
}

void UMTS_MapTransitionHandler::OnMapTransitionFailed_Implementation(
	UMTS_MapTransitionSubsystem* TransitionSubsystem,
	const FMTS_MapTransitionRequest& Request,
	const FText& FailureReason)
{
}
