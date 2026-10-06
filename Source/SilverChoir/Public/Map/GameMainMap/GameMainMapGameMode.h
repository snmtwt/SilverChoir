#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "GameMainMapGameMode.generated.h"

/** 激活插件已加载的地图；加载、卸载和生命周期统一由地图切换插件管理。 */
UCLASS(Blueprintable)
class SILVERCHOIR_API AGameMainMapGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AGameMainMapGameMode();
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	/** 激活已就绪且显示的地图、安置玩家并切换 UI。必须显式指定 Base 或 Battle；None/非法值报错并返回 false，不推断类型。 */
	UFUNCTION(BlueprintCallable, Category="主地图", meta=(AutoCreateRefTerm="EntryTransform")) bool ActivateLoadedMap(FName MapID, const FTransform& EntryTransform, EGameMainMapType MapType = EGameMainMapType::None);
	/** 地图已就绪、控制器收到进入点后触发；用于基地 AI、音效等活动状态管理。 */
	UFUNCTION(BlueprintImplementableEvent, Category="主地图") void OnMapActivated(FName PreviousMapID, FName MapID);
};
