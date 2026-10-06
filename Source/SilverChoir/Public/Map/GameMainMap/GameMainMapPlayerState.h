#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GameMainMapPlayerState.generated.h"

/** 基地/战斗切换不会销毁此 PlayerState；跨 OpenLevel 的存档应放入玩家子系统。 */
UCLASS(Blueprintable)
class SILVERCHOIR_API AGameMainMapPlayerState : public APlayerState
{
	GENERATED_BODY()
};
