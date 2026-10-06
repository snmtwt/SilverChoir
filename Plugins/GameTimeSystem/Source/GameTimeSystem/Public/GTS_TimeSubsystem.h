#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GTS_TimeSubsystem.generated.h"

class UGTS_TimeManager;
class UTextBlock;

/** 每个 GameInstance 一份日历，切换主地图不重置，结束游戏时释放所有未执行任务。 */
UCLASS(BlueprintType, meta=(DisplayName="游戏时间子系统"))
class GAMETIMESYSTEM_API UGTS_TimeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category="游戏时间", meta=(DisplayName="获取时间管理器"))
    UGTS_TimeManager* GetManager() const { return Manager; }

    UFUNCTION(BlueprintPure, Category="游戏时间", meta=(DisplayName="游戏时间系统是否就绪"))
    bool IsReady() const;

    UFUNCTION(BlueprintPure, Category="游戏时间", meta=(DisplayName="获取游戏时间初始化错误"))
    FText GetInitializationError() const { return InitializationError; }

    /** 弱引用绑定；立即刷新。重复注册更新格式，空格式合法并清空文字。 */
    bool RegisterTimeTextBlock(UTextBlock* TextBlock, const FText& Format);
    /** 取消绑定后保留控件当前显示的文字。 */
    bool UnregisterTimeTextBlock(UTextBlock* TextBlock);
    bool IsTimeTextBlockRegistered(UTextBlock* TextBlock) const;
    int32 GetRegisteredTimeTextBlockCount() const;

    /** 支持 {Year}/{Month}/{Day}/{Hour}/{Minute}/{Second}/{Weekday}；无效日期返回空文本。 */
    static FText FormatTimeText(FDateTime Time, const FText& Format);

private:
    bool TickClock(float RealDeltaSeconds);

    UFUNCTION()
    void HandleTimeChanged(FDateTime CurrentTime);

    void PruneTimeTextBindings();
    void RefreshTimeTextBlock(UTextBlock* TextBlock, FDateTime Time, const FText& Format);

    UPROPERTY(Transient)
    TObjectPtr<UGTS_TimeManager> Manager;

    UPROPERTY(Transient)
    FText InitializationError;

    FTSTicker::FDelegateHandle TickerHandle;
    bool bPauseWithGame = true;

    // 不延长 UserWidget 或 TextBlock 的生命周期；UI 可在 Destruct 中主动取消绑定。
    TMap<TWeakObjectPtr<UTextBlock>, FText> TimeTextBindings;
    TSet<TWeakObjectPtr<UTextBlock>> UpdatingTimeTextBlocks;
    float TimeTextPruneElapsed = 0.0f;
};
