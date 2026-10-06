#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapPlayerState.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "SubSystem/PlayerSubSystem/PlayerSubsystem.h"
#include "MTS_SubMapSubsystem.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameMainMap, Log, All);

AGameMainMapGameMode::AGameMainMapGameMode()
{
    PlayerControllerClass = AGameMainMapPlayerController::StaticClass();
    GameStateClass = AGameMainMapGameState::StaticClass();
    PlayerStateClass = AGameMainMapPlayerState::StaticClass();
    DefaultPawnClass = APlayerCameraPawn::StaticClass();
}

UClass* AGameMainMapGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    const auto* Player = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPlayerSubsystem>() : nullptr;
    return Player ? Player->GetPlayerPawnClass().Get() : Super::GetDefaultPawnClassForController_Implementation(InController);
}

bool AGameMainMapGameMode::ActivateLoadedMap(FName MapID, const FTransform& EntryTransform, EGameMainMapType MapType)
{
    auto* State = GetGameState<AGameMainMapGameState>();
    // Validate before querying the streamed instance so an omitted type always produces a useful error.
    if (MapType != EGameMainMapType::Base && MapType != EGameMainMapType::Battle)
    {
        const FText Error = NSLOCTEXT("GameMainMap", "ExplicitMapTypeRequired", "激活地图时必须明确指定地图类型为基地或战斗，不能使用未指定类型。");
        UE_LOG(LogGameMainMap, Error, TEXT("ActivateLoadedMap: MapType must be Base or Battle; MapID=%s. %s"), *MapID.ToString(), *Error.ToString());
        if (State)
        {
            State->LastError = Error;
            State->OnMapSwitchFailed.Broadcast(Error);
        }
        return false;
    }
    FMTS_SubMapInfo Map;
    if (!State || State->bSwitchingMap || !EntryTransform.IsValid()
        || !GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>()->GetSubMapByKey(MapID, Map)
        || Map.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed || !Map.bIsVisible) return false;

    TGuardValue<bool> SwitchingGuard(State->bSwitchingMap, true);
    State->LastError = FText::GetEmpty();
    const FName Previous = State->ActiveMapID;
    State->ActiveMapID = MapID;
    State->SetCurrentMapType(MapType);
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (auto* Controller = Cast<AGameMainMapPlayerController>(It->Get()))
        {
            Controller->EnterMap(MapID, EntryTransform);
            if (Controller->IsLocalController()) Controller->SwitchMapUI(MapType);
        }
    }
    OnMapActivated(Previous, MapID);
    State->OnActiveMapChanged.Broadcast(Previous, MapID);
    return true;
}
