#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GTS_TimeSettings.generated.h"

class UGTS_TimeManager;

/** 游戏日历默认配置；不改变世界 TimeDilation。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="游戏时间系统"))
class GAMETIMESYSTEM_API UGTS_TimeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UGTS_TimeSettings();

    UPROPERTY(Config, EditAnywhere, Category="游戏时间", meta=(DisplayName="时间管理类", AllowAbstract="false"))
    TSoftClassPtr<UGTS_TimeManager> ManagerClass;

    UPROPERTY(Config, EditAnywhere, Category="游戏时间", meta=(DisplayName="初始游戏时间"))
    FDateTime InitialTime = FDateTime(2049, 1, 1);

    /** 1 表示现实一秒推进游戏一秒，60 表示现实一秒推进游戏一分钟。 */
    UPROPERTY(Config, EditAnywhere, Category="游戏时间", meta=(DisplayName="初始时间流速", ClampMin="0", UIMax="3600"))
    double InitialTimeScale = 1.0;

    UPROPERTY(Config, EditAnywhere, Category="游戏时间", meta=(DisplayName="初始暂停"))
    bool bInitiallyPaused = false;

    /** UE SetGamePaused 时一并停止日历；本插件暂停开关只暂停日历。 */
    UPROPERTY(Config, EditAnywhere, Category="游戏时间", meta=(DisplayName="随游戏暂停"))
    bool bPauseWithGame = true;

    /** 加速或跳时积累大量任务时分帧派发，防止单帧卡死。 */
    UPROPERTY(Config, EditAnywhere, Category="游戏时间|任务", meta=(DisplayName="每帧最多回调任务数", ClampMin="1", ClampMax="10000"))
    int32 MaxCallbacksPerTick = 128;
};
