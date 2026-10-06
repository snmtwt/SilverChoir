#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SubSystem/PlayerSubSystem/PlayerSubsystem.h"

FFCS_CameraState UPlayerLibrary::AddCameraStateLocationOffset(const FFCS_CameraState& CameraState, FVector LocationOffset)
{
	FFCS_CameraState Result = CameraState;
	Result.Location += LocationOffset;
	return Result;
}

APlayerCameraPawn* UPlayerLibrary::GetPlayerCamera(const UObject* WorldContextObject)
{
	const auto* Subsystem=GetPlayerSubsystem(WorldContextObject);
	return Subsystem?Subsystem->GetPlayerCamera():nullptr;
}

UPlayerSubsystem* UPlayerLibrary::GetPlayerSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine && IsValid(WorldContextObject)
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPlayerSubsystem>() : nullptr;
}

UPlayerManagerBase* UPlayerLibrary::GetPlayerManager(const UObject* WorldContextObject)
{
	const UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetManager() : nullptr;
}

bool UPlayerLibrary::IsPlayerSystemReady(const UObject* WorldContextObject)
{
	const UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem && Subsystem->IsReady();
}

APawn* UPlayerLibrary::GetPlayerPawn(const UObject* WorldContextObject)
{
	const auto* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetPlayerPawn() : nullptr;
}

TSubclassOf<APawn> UPlayerLibrary::GetPlayerPawnClass(const UObject* WorldContextObject)
{
	const auto* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetPlayerPawnClass() : nullptr;
}

bool UPlayerLibrary::InitializeGame(const UObject* WorldContextObject)
{
	UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem && Subsystem->InitializeGame();
}

bool UPlayerLibrary::LoadGame(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex)
{
	UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem && Subsystem->LoadGame(SlotName, UserIndex);
}

bool UPlayerLibrary::SaveGame(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex)
{
	UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	return Subsystem && Subsystem->SaveGame(SlotName, UserIndex);
}

void UPlayerLibrary::LoadNewGameData(const UObject* WorldContextObject)
{
	UPlayerSubsystem* Subsystem = GetPlayerSubsystem(WorldContextObject);
	Subsystem->LoadNewGameData();
}

