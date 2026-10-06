#include "SubSystem/PlayerSubSystem/PlayerSubsystem.h"

#include "Templates/UnrealTemplate.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SubSystem/PlayerSubSystem/PlayerManagerBase.h"
#include "SubSystem/PlayerSubSystem/PlayerSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerSubsystem, Log, All);

void UPlayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializationError = FText::GetEmpty();

	const UPlayerSettings* Settings = GetDefault<UPlayerSettings>();
	UClass* ManagerClass = Settings->ManagerClass.IsNull()
		? UPlayerManagerBase::StaticClass()
		: Settings->ManagerClass.LoadSynchronous();

	if (!ManagerClass || !ManagerClass->IsChildOf(UPlayerManagerBase::StaticClass())
		|| ManagerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		InitializationError = NSLOCTEXT("SilverChoirPlayer", "InvalidManagerClass",
			"玩家处理类配置无效：无法加载、类型不匹配或不可实例化。");
		UE_LOG(LogPlayerSubsystem, Error, TEXT("%s Class: %s"),
			*InitializationError.ToString(), *Settings->ManagerClass.ToSoftObjectPath().ToString());
		return;
	}

	Manager = NewObject<UPlayerManagerBase>(this, ManagerClass);
}

void UPlayerSubsystem::Deinitialize()
{
	Manager = nullptr;
	bExecutingGameOperation = false;
	InitializationError = FText::GetEmpty();
	Super::Deinitialize();
}

bool UPlayerSubsystem::IsReady() const
{
	return IsValid(Manager.Get());
}

UPlayerManagerBase* UPlayerSubsystem::GetManager() const
{
	return IsReady() ? Manager.Get() : nullptr;
}

bool UPlayerSubsystem::InitializeGame()
{
	if (!IsReady() || bExecutingGameOperation)
	{
		return false;
	}

	TGuardValue<bool> Guard(bExecutingGameOperation, true);
	return Manager->InitializeGame();
}

TSubclassOf<APawn> UPlayerSubsystem::GetPlayerPawnClass() const
{
	const auto& ConfiguredClass = GetDefault<UPlayerSettings>()->PlayerPawnClass;
	UClass* PawnClass = ConfiguredClass.IsNull() ? APlayerCameraPawn::StaticClass() : ConfiguredClass.LoadSynchronous();
	if (!PawnClass || !PawnClass->IsChildOf(APawn::StaticClass()) || PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		UE_LOG(LogPlayerSubsystem, Error, TEXT("Invalid PlayerPawnClass: %s"), *ConfiguredClass.ToString());
		return nullptr;
	}
	return PawnClass;
}

APawn* UPlayerSubsystem::GetPlayerPawn() const
{
	const auto* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return Controller ? Controller->GetPawn() : nullptr;
}

APlayerCameraPawn* UPlayerSubsystem::GetPlayerCamera() const
{
	APawn* Pawn=GetPlayerPawn();
	return IsValid(Pawn)?Cast<APlayerCameraPawn>(Pawn):nullptr;
}

bool UPlayerSubsystem::LoadGame(const FString& SlotName, int32 UserIndex)
{
	if (!IsReady() || bExecutingGameOperation || SlotName.TrimStartAndEnd().IsEmpty() || UserIndex < 0)
	{
		return false;
	}

	TGuardValue<bool> Guard(bExecutingGameOperation, true);
	return Manager->LoadGame(SlotName, UserIndex);
}

bool UPlayerSubsystem::SaveGame(const FString& SlotName, int32 UserIndex)
{
	if (!IsReady() || bExecutingGameOperation || SlotName.TrimStartAndEnd().IsEmpty() || UserIndex < 0)
	{
		return false;
	}

	TGuardValue<bool> Guard(bExecutingGameOperation, true);
	return Manager->SaveGame(SlotName, UserIndex);
}

void UPlayerSubsystem::LoadNewGameData()
{

	Manager->LoadNewGameData();
}
