#include "GTS_TimeManager.h"

#include "Engine/GameInstance.h"
#include "GTS_TimeTask.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FGTS_EarlierTask
	{
		bool operator()(const FGTS_TimeTaskHeapEntry& A, const FGTS_TimeTaskHeapEntry& B) const
		{
			return A.ScheduledTicks != B.ScheduledTicks
				? A.ScheduledTicks < B.ScheduledTicks
				: A.Sequence < B.Sequence;
		}
	};
}

UWorld* UGTS_TimeManager::GetWorld() const
{
	const UGameInstance* Instance = GameInstance.Get();
	return Instance ? Instance->GetWorld() : nullptr;
}

bool UGTS_TimeManager::IsSupportedTime(FDateTime Time)
{
	return Time.GetTicks() >= FDateTime::MinValue().GetTicks()
		&& Time.GetTicks() <= FDateTime::MaxValue().GetTicks();
}

void UGTS_TimeManager::Initialize(UGameInstance* InGameInstance, FDateTime InitialTime,
	double InitialTimeScale, bool bInitiallyPaused, int32 InMaxCallbacksPerTick)
{
	check(IsInGameThread());
	Shutdown();
	if (!IsSupportedTime(InitialTime) || !FMath::IsFinite(InitialTimeScale) || InitialTimeScale < 0.0)
	{
		return;
	}
	GameInstance = InGameInstance;
	CurrentTime = InitialTime;
	TimeScale = InitialTimeScale;
	bTimePaused = bInitiallyPaused;
	MaxCallbacksPerTick = FMath::Max(1, InMaxCallbacksPerTick);
	FractionalTicks = 0.0;
	NextSequence = 0;
	bPauseWhenCalendarLimitDrains = false;
	bInitialized = true;
}

void UGTS_TimeManager::Shutdown()
{
	check(IsInGameThread());
	++LifecycleGeneration;
	bInitialized = false;
	for (const TPair<FGuid, FGTS_PendingTimeTask>& Pair : PendingTasks)
	{
		if (IsValid(Pair.Value.Task) && Pair.Value.Task->PendingRegistrationId == Pair.Key)
		{
			Pair.Value.Task->PendingRegistrationId.Invalidate();
		}
	}
	PendingTasks.Empty();
	TaskHeap.Empty();
	DeferredHeapEntries.Empty();
	GameInstance.Reset();
	FractionalTicks = 0.0;
	bPauseWhenCalendarLimitDrains = false;
	OnTimeChanged.Clear();
	OnTimeScaleChanged.Clear();
	OnTimePausedChanged.Clear();
}

bool UGTS_TimeManager::SetCurrentTime(FDateTime NewTime)
{
	check(IsInGameThread());
	if (!bInitialized || bAdvancing || bNotifyingTime || !IsSupportedTime(NewTime))
	{
		return false;
	}
	FractionalTicks = 0.0;
	bPauseWhenCalendarLimitDrains = false;
	if (CurrentTime != NewTime)
	{
		CurrentTime = NewTime;
		BroadcastTimeChanged();
	}
	return true;
}

bool UGTS_TimeManager::SetTimeScale(double NewTimeScale)
{
	check(IsInGameThread());
	if (!bInitialized || !FMath::IsFinite(NewTimeScale) || NewTimeScale < 0.0)
	{
		return false;
	}
	if (TimeScale != NewTimeScale)
	{
		TimeScale = NewTimeScale;
		const TStrongObjectPtr<UGTS_TimeManager> KeepAlive(this);
		OnTimeScaleChanged.Broadcast(TimeScale);
	}
	return true;
}

void UGTS_TimeManager::SetTimePaused(bool bNewPaused)
{
	check(IsInGameThread());
	if (bInitialized && bTimePaused != bNewPaused)
	{
		bTimePaused = bNewPaused;
		const TStrongObjectPtr<UGTS_TimeManager> KeepAlive(this);
		OnTimePausedChanged.Broadcast(bTimePaused);
	}
}

void UGTS_TimeManager::BroadcastTimeChanged()
{
	const TStrongObjectPtr<UGTS_TimeManager> KeepAlive(this);
	const TGuardValue<bool> NotificationGuard(bNotifyingTime, true);
	OnTimeChanged.Broadcast(CurrentTime);
}

UGTS_TimeTask* UGTS_TimeManager::CreateTimeTask(TSubclassOf<UGTS_TimeTask> TaskClass)
{
	check(IsInGameThread());
	if (!bInitialized || !TaskClass
		|| TaskClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	UGTS_TimeTask* Task = NewObject<UGTS_TimeTask>(this, TaskClass);
	Task->Manager = this;
	return Task;
}

FGuid UGTS_TimeManager::RegisterTimeTask(UGTS_TimeTask* Task, FDateTime ScheduledTime)
{
	check(IsInGameThread());
	if (!bInitialized || !IsValid(Task) || Task->IsTemplate()
		|| Task->Manager.Get() != this || Task->GetOuter() != this
		|| Task->PendingRegistrationId.IsValid() || !IsSupportedTime(ScheduledTime)
		|| NextSequence == MAX_uint64)
	{
		return FGuid();
	}

	FGuid RegistrationId;
	do
	{
		RegistrationId = FGuid::NewGuid();
	}
	while (!RegistrationId.IsValid() || PendingTasks.Contains(RegistrationId));

	FGTS_PendingTimeTask Pending;
	Pending.Task = Task;
	Pending.ScheduledTime = ScheduledTime;
	// 回调中重新登记过去时间时，以当前时刻排队，避免反复插队饿死已经到期的任务。
	// 向蓝图传递的 ScheduledTime 仍然是调用者登记的原始时间。
	Pending.QueueTicks = bAdvancing
		? FMath::Max(ScheduledTime.GetTicks(), CurrentTime.GetTicks())
		: ScheduledTime.GetTicks();
	Pending.Sequence = NextSequence++;
	PendingTasks.Add(RegistrationId, Pending);
	Task->PendingRegistrationId = RegistrationId;

	const FGTS_TimeTaskHeapEntry Entry{RegistrationId, Pending.QueueTicks, Pending.Sequence};
	if (bAdvancing)
	{
		DeferredHeapEntries.Add(Entry);
	}
	else
	{
		TaskHeap.HeapPush(Entry, FGTS_EarlierTask());
	}
	return RegistrationId;
}

bool UGTS_TimeManager::CancelTimeTask(FGuid RegistrationId)
{
	check(IsInGameThread());
	FGTS_PendingTimeTask RemovedTask;
	if (!bInitialized || !PendingTasks.RemoveAndCopyValue(RegistrationId, RemovedTask))
	{
		return false;
	}
	if (IsValid(RemovedTask.Task) && RemovedTask.Task->PendingRegistrationId == RegistrationId)
	{
		RemovedTask.Task->PendingRegistrationId.Invalidate();
	}
	CompactTaskHeapIfNeeded();
	return true;
}

bool UGTS_TimeManager::IsTimeTaskRegistered(FGuid RegistrationId) const
{
	check(IsInGameThread());
	return bInitialized && PendingTasks.Contains(RegistrationId);
}

void UGTS_TimeManager::CompactTaskHeapIfNeeded()
{
	// 不在广播中重建，否则本次回调中新登记的任务会过早进入可派发的堆。
	if (bAdvancing || int64(TaskHeap.Num()) <= FMath::Max<int64>(64, int64(PendingTasks.Num()) * 2))
	{
		return;
	}
	TaskHeap.Reset(PendingTasks.Num());
	for (const TPair<FGuid, FGTS_PendingTimeTask>& Pair : PendingTasks)
	{
		TaskHeap.Add({Pair.Key, Pair.Value.QueueTicks, Pair.Value.Sequence});
	}
	TaskHeap.Heapify(FGTS_EarlierTask());
}

void UGTS_TimeManager::AdvanceTime(double RealDeltaSeconds)
{
	check(IsInGameThread());
	if (!bInitialized || bAdvancing || bNotifyingTime || bTimePaused
		|| !FMath::IsFinite(RealDeltaSeconds) || RealDeltaSeconds < 0.0)
	{
		return;
	}

	const TStrongObjectPtr<UGTS_TimeManager> KeepAlive(this);
	const uint64 AdvanceGeneration = LifecycleGeneration;
	const TGuardValue<bool> AdvanceGuard(bAdvancing, true);
	ON_SCOPE_EXIT
	{
		if (bInitialized && LifecycleGeneration == AdvanceGeneration)
		{
			for (const FGTS_TimeTaskHeapEntry& Entry : DeferredHeapEntries)
			{
				const FGTS_PendingTimeTask* Pending = PendingTasks.Find(Entry.RegistrationId);
				if (Pending && Pending->Sequence == Entry.Sequence)
				{
					TaskHeap.HeapPush(Entry, FGTS_EarlierTask());
				}
			}
			DeferredHeapEntries.Reset();
			// 此刻不再派发，可安全压缩取消产生的空条目。
			const TGuardValue<bool> CompactGuard(bAdvancing, false);
			CompactTaskHeapIfNeeded();
		}
	};

	const int64 OldTicks = CurrentTime.GetTicks();
	if (RealDeltaSeconds > 0.0 && TimeScale > 0.0)
	{
		const double AdvanceTicks = RealDeltaSeconds * TimeScale * double(ETimespan::TicksPerSecond) + FractionalTicks;
		const int64 RemainingTicks = FDateTime::MaxValue().GetTicks() - OldTicks;
		if (!FMath::IsFinite(AdvanceTicks) || AdvanceTicks >= double(RemainingTicks))
		{
			CurrentTime = FDateTime::MaxValue();
			FractionalTicks = 0.0;
			bPauseWhenCalendarLimitDrains = true;
		}
		else
		{
			const int64 WholeTicks = int64(AdvanceTicks);
			FractionalTicks = AdvanceTicks - double(WholeTicks);
			CurrentTime = FDateTime(OldTicks + WholeTicks);
		}
	}

	if (CurrentTime.GetTicks() / ETimespan::TicksPerSecond != OldTicks / ETimespan::TicksPerSecond)
	{
		BroadcastTimeChanged();
	}

	if (!bInitialized || LifecycleGeneration != AdvanceGeneration)
	{
		return;
	}

	const TGuardValue<bool> DispatchGuard(bDispatching, true);
	int32 CallbacksDispatched = 0;
	while (!bTimePaused && CallbacksDispatched < MaxCallbacksPerTick && !TaskHeap.IsEmpty())
	{
		if (TaskHeap[0].ScheduledTicks > CurrentTime.GetTicks())
		{
			break;
		}
		FGTS_TimeTaskHeapEntry Entry;
		TaskHeap.HeapPop(Entry, FGTS_EarlierTask(), EAllowShrinking::No);
		const FGTS_PendingTimeTask* Found = PendingTasks.Find(Entry.RegistrationId);
		if (!Found || Found->Sequence != Entry.Sequence)
		{
			continue;
		}
		// 回调可以主动 GC、取消其他任务、关闭管理器或重新登记自身；先保活并移除旧登记。
		const TStrongObjectPtr<UGTS_TimeTask> Task(Found->Task.Get());
		const FDateTime ScheduledTime = Found->ScheduledTime;
		PendingTasks.Remove(Entry.RegistrationId);
		if (!IsValid(Task.Get()))
		{
			continue;
		}
		Task->PendingRegistrationId.Invalidate();
		++CallbacksDispatched;
		Task->OnTimeReached(ScheduledTime, CurrentTime);
		if (!bInitialized || LifecycleGeneration != AdvanceGeneration)
		{
			return;
		}
	}

	// 达到日历上限时保留帧预算，分帧清空到期任务后再自动暂停，避免尾部任务永久滞留。
	if (bPauseWhenCalendarLimitDrains && PendingTasks.IsEmpty() && !bTimePaused)
	{
		SetTimePaused(true);
	}
}
