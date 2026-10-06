#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GTS_TimeLibrary.generated.h"

class UGTS_TimeSubsystem;
class UGTS_TimeManager;
class UGTS_TimeTask;
class UTextBlock;

UCLASS(meta=(DisplayName="游戏时间函数库"))
class GAMETIMESYSTEM_API UGTS_TimeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category="游戏时间", meta=(WorldContext="WorldContextObject", DisplayName="获取游戏时间子系统"))
    static UGTS_TimeSubsystem* GetTimeSubsystem(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="游戏时间", meta=(WorldContext="WorldContextObject", DisplayName="获取游戏时间管理器"))
    static UGTS_TimeManager* GetTimeManager(const UObject* WorldContextObject);

    /** 非法日期返回 false，不会进入 FDateTime 的断言构造。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|日期", meta=(DisplayName="构造有效游戏日期", AdvancedDisplay="Hour,Minute,Second"))
    static bool MakeGameDateTime(int32 Year, int32 Month, int32 Day, FDateTime& DateTime,
        int32 Hour = 0, int32 Minute = 0, int32 Second = 0);

    UFUNCTION(BlueprintCallable, Category="游戏时间|日期", meta=(WorldContext="WorldContextObject", DisplayName="设置游戏时间（年月日时分秒）"))
    static bool SetCurrentTimeFromParts(const UObject* WorldContextObject, int32 Year, int32 Month,
        int32 Day, int32 Hour = 0, int32 Minute = 0, int32 Second = 0);

    /** 只修改日历；到期任务在下一次未暂停的时钟更新执行，不会在此节点内同步回调。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|日期", meta=(WorldContext="WorldContextObject", DisplayName="设置游戏时间"))
    static bool SetCurrentTime(const UObject* WorldContextObject, FDateTime NewTime);

    /** 可用 UE 的 Break Date Time 节点拆分年月日时分秒。 */
    UFUNCTION(BlueprintPure, Category="游戏时间|日期", meta=(WorldContext="WorldContextObject", DisplayName="获取当前游戏时间"))
    static FDateTime GetCurrentTime(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="游戏时间|流速", meta=(WorldContext="WorldContextObject", DisplayName="设置游戏时间流速"))
    static bool SetTimeScale(const UObject* WorldContextObject, double TimeScale);

    UFUNCTION(BlueprintPure, Category="游戏时间|流速", meta=(WorldContext="WorldContextObject", DisplayName="获取游戏时间流速"))
    static double GetTimeScale(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="游戏时间|流速", meta=(WorldContext="WorldContextObject", DisplayName="设置游戏时间暂停"))
    static void SetTimePaused(const UObject* WorldContextObject, bool bPaused);

    UFUNCTION(BlueprintPure, Category="游戏时间|流速", meta=(WorldContext="WorldContextObject", DisplayName="游戏时间是否暂停"))
    static bool IsTimePaused(const UObject* WorldContextObject);

    /** 先创建并设置任务参数，再注册；返回具体子类，可直接设置自定义蓝图变量。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|任务", meta=(WorldContext="WorldContextObject", DeterminesOutputType="TaskClass", DisplayName="创建游戏时间回调任务"))
    static UGTS_TimeTask* CreateTimeTask(const UObject* WorldContextObject, TSubclassOf<UGTS_TimeTask> TaskClass);

    /** 成功返回任务ID；无效ID表示注册失败。同一对象同时只允许一个预约。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|任务", meta=(WorldContext="WorldContextObject", DisplayName="注册游戏时间回调任务"))
    static FGuid RegisterTimeTask(const UObject* WorldContextObject, UGTS_TimeTask* Task, FDateTime ScheduledTime);

    UFUNCTION(BlueprintCallable, Category="游戏时间|任务", meta=(WorldContext="WorldContextObject", DisplayName="取消游戏时间回调任务"))
    static bool CancelTimeTask(const UObject* WorldContextObject, FGuid TaskId);

    UFUNCTION(BlueprintPure, Category="游戏时间|任务", meta=(WorldContext="WorldContextObject", DisplayName="游戏时间任务是否已注册"))
    static bool IsTimeTaskRegistered(const UObject* WorldContextObject, FGuid TaskId);

    UFUNCTION(BlueprintPure, Category="游戏时间|任务", meta=(WorldContext="WorldContextObject", DisplayName="获取待执行游戏时间任务数"))
    static int32 GetPendingTaskCount(const UObject* WorldContextObject);

    /** 注册后立即显示并随日历更新。重复注册同一控件只替换格式，不增加重复绑定。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|文本显示", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm="Format", DisplayName="注册游戏时间文本控件", CPP_Default_Format="{Year}-{Month}-{Day} {Hour}:{Minute}:{Second}"))
    static bool RegisterTimeTextBlock(const UObject* WorldContextObject, UTextBlock* TextBlock, const FText& Format);

    /** 停止自动更新，保留控件最后一次显示的文本。推荐在 Widget Destruct 时调用。 */
    UFUNCTION(BlueprintCallable, Category="游戏时间|文本显示", meta=(WorldContext="WorldContextObject", DisplayName="取消注册游戏时间文本控件"))
    static bool UnregisterTimeTextBlock(const UObject* WorldContextObject, UTextBlock* TextBlock);

    UFUNCTION(BlueprintPure, Category="游戏时间|文本显示", meta=(WorldContext="WorldContextObject", DisplayName="游戏时间文本控件是否已注册"))
    static bool IsTimeTextBlockRegistered(const UObject* WorldContextObject, UTextBlock* TextBlock);

    /** 不注册控件，仅按相同规则格式化当前日期。支持 Year/Month/Day/Hour/Minute/Second/Weekday。 */
    UFUNCTION(BlueprintPure, Category="游戏时间|文本显示", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm="Format", DisplayName="格式化当前游戏时间", CPP_Default_Format="{Year}-{Month}-{Day} {Hour}:{Minute}:{Second}"))
    static FText FormatCurrentTime(const UObject* WorldContextObject, const FText& Format);
};
