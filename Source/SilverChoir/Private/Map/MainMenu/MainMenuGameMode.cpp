#include "Map/MainMenu/MainMenuGameMode.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "Map/MainMenu/MainMenuPlayerController.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	PlayerControllerClass = AMainMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}

void AMainMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// A menu has no pawn. Allow an explicit subclass configuration to opt in later.
	if (DefaultPawnClass) { Super::HandleStartingNewPlayer_Implementation(NewPlayer); }
}

void AMainMenuGameMode::InitializeHUDForPlayer_Implementation(APlayerController* NewPlayer)
{
	if (HUDClass) { Super::InitializeHUDForPlayer_Implementation(NewPlayer); }
}
