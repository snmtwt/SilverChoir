#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GTS_TimeManager.generated.h"

class UGameInstance;
class UGTS_TimeTask;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGTS_TimeChanged, FDateTime, CurrentTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGTS_TimeScaleChanged, double, TimeScale);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGTS_TimePausedChanged, bool, bPaused);

/** 内部登记记录。UPROPERTY 确保蓝图任务在等待过程中不会被垃圾回收。 */
USTRUCT()
struct FGTS_PendingTimeTask
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UGTS_TimeTask> Task = nullptr;

	FDateTime ScheduledTime;
	int64 QueueTicks = 0;
	uint64 Sequence = 0;
};

/** 不持有 UObject。取消后留下的堆条目会被忽略，并按阈值压缩。 */
struct FGTS_TimeTaskHeapEntry
{
	FGuid RegistrationId;
	int64 ScheduledTicks = 0;
	uint64 Sequence = 0;
};

/** 独立日历时钟；不改变引擎 Time Dilation。所有操作在游戏线程执行。 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="游戏时间管理器"))
class GAMETIMESYSTEM_API UGTS_TimeManager : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;
	/** 非法日期或非有限/负时间倍率会使 IsInitialized() 保持 false。 */
	void Initialize(UGameInstance* InGameInstance, FDateTime InitialTime, double InitialTimeScale,
		bool bInitiallyPaused, int32 InMaxCallbacksPerTick);
	void Shutdown();

	/** 子系统传入真实经过秒数。用于确定性测试的入口；0 秒仍会处理已到期任务。 */
	void AdvanceTime(double RealDeltaSeconds);
	bool SetCurrentTime(FDateTime NewTime);
	bool SetTimeScale(double NewTimeScale);
	void SetTimePaused(bool bNewPaused);

	FDateTime GetCurrentTime() const { return CurrentTime; }
	double GetTimeScale() const { return TimeScale; }
	bool IsTimePaused() const { return bTimePaused; }
	bool IsInitialized() const { return bInitialized; }

	UGTS_TimeTask* CreateTimeTask(TSubclassOf<UGTS_TimeTask> TaskClass);
	/** 回调中新登记的任务最早下一次推进才派发；过去时间按当前时刻排队，防止反复插队。 */
	FGuid RegisterTimeTask(UGTS_TimeTask* Task, FDateTime ScheduledTime);
	bool CancelTimeTask(FGuid RegistrationId);
	bool IsTimeTaskRegistered(FGuid RegistrationId) const;
	int32 GetPendingTaskCount() const { return PendingTasks.Num(); }

	/** 自动推进仅在跨越整秒时广播；显式改变时间时立即广播。 */
	UPROPERTY(BlueprintAssignable, Category="游戏时间", meta=(DisplayName="游戏时间已改变"))
	FGTS_TimeChanged OnTimeChanged;

	UPROPERTY(BlueprintAssignable, Category="游戏时间", meta=(DisplayName="时间流速已改变"))
	FGTS_TimeScaleChanged OnTimeScaleChanged;

	UPROPERTY(BlueprintAssignable, Category="游戏时间", meta=(DisplayName="时间暂停状态已改变"))
	FGTS_TimePausedChanged OnTimePausedChanged;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UGameInstance> GameInstance;

	UPROPERTY(Transient)
	TMap<FGuid, FGTS_PendingTimeTask> PendingTasks;

	TArray<FGTS_TimeTaskHeapEntry> TaskHeap;
	TArray<FGTS_TimeTaskHeapEntry> DeferredHeapEntries;
	FDateTime CurrentTime;
	double TimeScale = 1.0;
	double FractionalTicks = 0.0;
	uint64 NextSequence = 0;
	uint64 LifecycleGeneration = 0;
	int32 MaxCallbacksPerTick = 128;
	bool bTimePaused = false;
	bool bInitialized = false;
	bool bAdvancing = false;
	bool bDispatching = false;
	bool bNotifyingTime = false;
	bool bPauseWhenCalendarLimitDrains = false;

	void CompactTaskHeapIfNeeded();
	void BroadcastTimeChanged();
	static bool IsSupportedTime(FDateTime Time);
};
