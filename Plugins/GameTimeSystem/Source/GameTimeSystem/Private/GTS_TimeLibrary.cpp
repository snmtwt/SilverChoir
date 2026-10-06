#include "GTS_TimeLibrary.h"

#include "GTS_TimeManager.h"
#include "GTS_TimeSubsystem.h"
#include "GTS_TimeTask.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UGTS_TimeSubsystem* UGTS_TimeLibrary::GetTimeSubsystem(const UObject* WorldContextObject)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    return Instance ? Instance->GetSubsystem<UGTS_TimeSubsystem>() : nullptr;
}

UGTS_TimeManager* UGTS_TimeLibrary::GetTimeManager(const UObject* WorldContextObject)
{
    UGTS_TimeSubsystem* Subsystem = GetTimeSubsystem(WorldContextObject);
    return Subsystem ? Subsystem->GetManager() : nullptr;
}

bool UGTS_TimeLibrary::MakeGameDateTime(int32 Year, int32 Month, int32 Day, FDateTime& DateTime,
    int32 Hour, int32 Minute, int32 Second)
{
    DateTime = FDateTime::MinValue();
    if (!FDateTime::Validate(Year, Month, Day, Hour, Minute, Second, 0)) return false;
    DateTime = FDateTime(Year, Month, Day, Hour, Minute, Second);
    return true;
}

bool UGTS_TimeLibrary::SetCurrentTimeFromParts(const UObject* WorldContextObject, int32 Year, int32 Month,
    int32 Day, int32 Hour, int32 Minute, int32 Second)
{
    FDateTime NewTime;
    return MakeGameDateTime(Year, Month, Day, NewTime, Hour, Minute, Second) && SetCurrentTime(WorldContextObject, NewTime);
}

bool UGTS_TimeLibrary::SetCurrentTime(const UObject* WorldContextObject, FDateTime NewTime)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager && Manager->SetCurrentTime(NewTime);
}

FDateTime UGTS_TimeLibrary::GetCurrentTime(const UObject* WorldContextObject)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager ? Manager->GetCurrentTime() : FDateTime::MinValue();
}

bool UGTS_TimeLibrary::SetTimeScale(const UObject* WorldContextObject, double TimeScale)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager && Manager->SetTimeScale(TimeScale);
}

double UGTS_TimeLibrary::GetTimeScale(const UObject* WorldContextObject)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager ? Manager->GetTimeScale() : 0.0;
}

void UGTS_TimeLibrary::SetTimePaused(const UObject* WorldContextObject, bool bPaused)
{
    if (UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject)) Manager->SetTimePaused(bPaused);
}

bool UGTS_TimeLibrary::IsTimePaused(const UObject* WorldContextObject)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return !Manager || Manager->IsTimePaused();
}

UGTS_TimeTask* UGTS_TimeLibrary::CreateTimeTask(const UObject* WorldContextObject, TSubclassOf<UGTS_TimeTask> TaskClass)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager ? Manager->CreateTimeTask(TaskClass) : nullptr;
}

FGuid UGTS_TimeLibrary::RegisterTimeTask(const UObject* WorldContextObject, UGTS_TimeTask* Task, FDateTime ScheduledTime)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager ? Manager->RegisterTimeTask(Task, ScheduledTime) : FGuid();
}

bool UGTS_TimeLibrary::CancelTimeTask(const UObject* WorldContextObject, FGuid TaskId)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager && Manager->CancelTimeTask(TaskId);
}

bool UGTS_TimeLibrary::IsTimeTaskRegistered(const UObject* WorldContextObject, FGuid TaskId)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager && Manager->IsTimeTaskRegistered(TaskId);
}

int32 UGTS_TimeLibrary::GetPendingTaskCount(const UObject* WorldContextObject)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager ? Manager->GetPendingTaskCount() : 0;
}

bool UGTS_TimeLibrary::RegisterTimeTextBlock(const UObject* WorldContextObject, UTextBlock* TextBlock, const FText& Format)
{
    UGTS_TimeSubsystem* Subsystem = GetTimeSubsystem(WorldContextObject);
    return Subsystem && Subsystem->RegisterTimeTextBlock(TextBlock, Format);
}

bool UGTS_TimeLibrary::UnregisterTimeTextBlock(const UObject* WorldContextObject, UTextBlock* TextBlock)
{
    UGTS_TimeSubsystem* Subsystem = GetTimeSubsystem(WorldContextObject);
    return Subsystem && Subsystem->UnregisterTimeTextBlock(TextBlock);
}

bool UGTS_TimeLibrary::IsTimeTextBlockRegistered(const UObject* WorldContextObject, UTextBlock* TextBlock)
{
    UGTS_TimeSubsystem* Subsystem = GetTimeSubsystem(WorldContextObject);
    return Subsystem && Subsystem->IsTimeTextBlockRegistered(TextBlock);
}

FText UGTS_TimeLibrary::FormatCurrentTime(const UObject* WorldContextObject, const FText& Format)
{
    UGTS_TimeManager* Manager = GetTimeManager(WorldContextObject);
    return Manager && Manager->IsInitialized()
        ? UGTS_TimeSubsystem::FormatTimeText(Manager->GetCurrentTime(), Format) : FText::GetEmpty();
}
