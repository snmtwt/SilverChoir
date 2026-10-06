#include "Map/GameMainMap/GameMainMapLibrary.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapPlayerState.h"
#include "FCS_FreeCameraPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
UWorld* MainMapWorld(const UObject* Context)
{
    return GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
}
}
void UGameMainMapLibrary::GetGameMainMapObjects(const UObject* Context, AGameMainMapGameMode*& GameMode,
    AGameMainMapGameState*& GameState, AGameMainMapPlayerController*& PlayerController,
    AGameMainMapPlayerState*& PlayerState, APawn*& PlayerPawn, int32 PlayerIndex)
{
    GameMode = GetGameMainMapGameMode(Context);
    GameState = GetGameMainMapGameState(Context);
    PlayerController = GetGameMainMapPlayerController(Context, PlayerIndex);
    PlayerState = PlayerController ? PlayerController->GetPlayerState<AGameMainMapPlayerState>() : nullptr;
    PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
}
AGameMainMapGameMode* UGameMainMapLibrary::GetGameMainMapGameMode(const UObject* Context)
{
    UWorld* World = MainMapWorld(Context);
    return World ? World->GetAuthGameMode<AGameMainMapGameMode>() : nullptr;
}
AGameMainMapGameState* UGameMainMapLibrary::GetGameMainMapGameState(const UObject* Context)
{
    UWorld* World = MainMapWorld(Context);
    return World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
}
AGameMainMapPlayerController* UGameMainMapLibrary::GetGameMainMapPlayerController(const UObject* Context, int32 Index)
{
    UWorld* World = MainMapWorld(Context);
    return World && Index >= 0 ? Cast<AGameMainMapPlayerController>(UGameplayStatics::GetPlayerController(World, Index)) : nullptr;
}
AGameMainMapPlayerState* UGameMainMapLibrary::GetGameMainMapPlayerState(const UObject* Context, int32 Index)
{
    auto* PC = GetGameMainMapPlayerController(Context, Index);
    return PC ? PC->GetPlayerState<AGameMainMapPlayerState>() : nullptr;
}
APawn* UGameMainMapLibrary::GetGameMainMapPlayerPawn(const UObject* Context, int32 Index)
{
    auto* PC = GetGameMainMapPlayerController(Context, Index);
    return PC ? PC->GetPawn() : nullptr;
}
AFCS_FreeCameraPawn* UGameMainMapLibrary::GetGameMainMapPlayerCamera(const UObject* Context, int32 Index)
{
    return Cast<AFCS_FreeCameraPawn>(GetGameMainMapPlayerPawn(Context, Index));
}
EGameMainMapType UGameMainMapLibrary::GetCurrentMapType(const UObject* Context)
{
    auto* State = GetGameMainMapGameState(Context);
    return State ? State->GetCurrentMapType() : EGameMainMapType::None;
}
