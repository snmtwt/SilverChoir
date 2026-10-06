#include "GTS_TimeTask.h"

#include "GTS_TimeManager.h"

UWorld* UGTS_TimeTask::GetWorld() const
{
	const UGTS_TimeManager* TimeManager = Manager.Get();
	return TimeManager ? TimeManager->GetWorld() : nullptr;
}

UGTS_TimeManager* UGTS_TimeTask::GetTimeManager() const
{
	return Manager.Get();
}

void UGTS_TimeTask::OnTimeReached_Implementation(FDateTime ScheduledTime, FDateTime CurrentTime)
{
}
