#include "MTS_MapTransitionBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_SubMapSubsystem.h"
#include "Engine/World.h"

bool UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(const UObject* WorldContextObject, FName MapID, FVector& OutLocation)
{
	OutLocation = FVector::ZeroVector;
	UWorld* World = GEngine && IsValid(WorldContextObject)
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	auto* Subsystem = World ? World->GetSubsystem<UMTS_SubMapSubsystem>() : nullptr;
	FMTS_SubMapInfo Map;
	if (MapID.IsNone() || !Subsystem || !Subsystem->GetSubMapByKey(MapID, Map)) return false;
	OutLocation = Map.Transform.GetLocation();
	return true;
}

UMTS_MapTransitionHandler* UMTS_MapTransitionBlueprintLibrary::CreateMapTransitionHandler(
	const UObject* WorldContextObject,
	TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		return TransitionSubsystem->CreateTransitionHandler(TransitionHandlerClass);
	}

	return nullptr;
}

UMTS_MapTransitionSubsystem* UMTS_MapTransitionBlueprintLibrary::GetMapTransitionSubsystem(
	const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UMTS_MapTransitionSubsystem>() : nullptr;
}

bool UMTS_MapTransitionBlueprintLibrary::SwitchMap(
	const UObject* WorldContextObject,
	const FMTS_MapTransitionRequest& Request)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		return TransitionSubsystem->SwitchMap(Request);
	}

	return false;
}

bool UMTS_MapTransitionBlueprintLibrary::SwitchMapByObject(
	const UObject* WorldContextObject,
	TSoftObjectPtr<UWorld> TargetMap,
	TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass,
	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
	float LoadingCompletionDelaySeconds,
	bool bRequireManualMapReadyNotification,
	float AutomaticProgressMax)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		return TransitionSubsystem->SwitchMapByObject(
			TargetMap,
			TransitionHandlerClass,
			LoadingWidgetClass,
			LoadingCompletionDelaySeconds,
			bRequireManualMapReadyNotification,
			AutomaticProgressMax
		);
	}

	return false;
}

bool UMTS_MapTransitionBlueprintLibrary::SwitchMapByHandlerObject(
	const UObject* WorldContextObject,
	TSoftObjectPtr<UWorld> TargetMap,
	UMTS_MapTransitionHandler* TransitionHandler,
	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
	float LoadingCompletionDelaySeconds,
	bool bRequireManualMapReadyNotification,
	float AutomaticProgressMax)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		return TransitionSubsystem->SwitchMapByHandlerObject(
			TargetMap,
			TransitionHandler,
			LoadingWidgetClass,
			LoadingCompletionDelaySeconds,
			bRequireManualMapReadyNotification,
			AutomaticProgressMax
		);
	}

	return false;
}

void UMTS_MapTransitionBlueprintLibrary::MarkMapReady(
	const UObject* WorldContextObject,
	FName ReadySource)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		TransitionSubsystem->MarkMapReady(ReadySource);
	}
}

void UMTS_MapTransitionBlueprintLibrary::NotifyMapLoadCompleted(
	const UObject* WorldContextObject,
	FName ReadySource)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		TransitionSubsystem->NotifyMapLoadCompleted(ReadySource);
	}
}

void UMTS_MapTransitionBlueprintLibrary::SetTransitionProgress(
	const UObject* WorldContextObject,
	float Progress,
	const FText& LoadingContent)
{
	if (UMTS_MapTransitionSubsystem* TransitionSubsystem = GetMapTransitionSubsystem(WorldContextObject))
	{
		TransitionSubsystem->SetTransitionProgress(Progress, LoadingContent);
	}
}
