#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "主菜单游戏模式"))
class SILVERCHOIR_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AMainMenuGameMode();
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void InitializeHUDForPlayer_Implementation(APlayerController* NewPlayer) override;
};
