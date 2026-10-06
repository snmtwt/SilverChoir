#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlayerSettings.generated.h"

class UPlayerManagerBase;
class APawn;

/** 静态项目配置，保存到 DefaultGame.ini；不承载玩家存档或运行时状态。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "玩家配置"))
class SILVERCHOIR_API UPlayerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPlayerSettings();

	/** 留空使用原生处理类；显式配置无效时由子系统报告错误。 */
	UPROPERTY(Config, EditAnywhere, Category = "玩家", meta = (DisplayName = "玩家处理类", AllowAbstract = "false"))
	TSoftClassPtr<UPlayerManagerBase> ManagerClass;
	/** GameMainMap 生成的玩家 Pawn；留空使用带背包组件的 APlayerCameraPawn。 */
	UPROPERTY(Config, EditAnywhere, Category="玩家", meta=(DisplayName="玩家 Pawn 类", AllowAbstract="false"))
	TSoftClassPtr<APawn> PlayerPawnClass;
};
