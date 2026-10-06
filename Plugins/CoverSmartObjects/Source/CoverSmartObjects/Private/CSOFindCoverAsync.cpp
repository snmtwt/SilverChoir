#include "CSOFindCoverAsync.h"
#include "CSOCoverSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

namespace
{
    TArray<TWeakObjectPtr<UCSOFindCoverAsync>> PendingQueries;
    FTSTicker::FDelegateHandle QueueTicker;
    TAutoConsoleVariable<int32> AsyncQueriesPerFrame(TEXT("cso.AsyncQueriesPerFrame"), 2, TEXT("Maximum queued cover queries per engine tick."));
    TAutoConsoleVariable<float> AsyncMilliseconds(TEXT("cso.AsyncMilliseconds"), 2.f, TEXT("Cooperative time budget; checked between complete bounded queries, not during a collision call."));
    constexpr int32 MaxQueueSize = 128;
}

UCSOFindCoverAsync* UCSOFindCoverAsync::FindCoverQueued(UObject* WorldContextObject, const FCSOCoverQuery& Query, bool bClaim, float LeaseSeconds)
{
    UCSOFindCoverAsync* Action = NewObject<UCSOFindCoverAsync>();
    Action->QueryWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    Action->bInputRejected = Query.Enemies.Num() > UCSOCoverSubsystem::MaxEnemies || Query.IgnoredActors.Num() > 64;
    if (!Action->bInputRejected) Action->Request = Query;
    Action->bReserve = bClaim;
    Action->Lease = LeaseSeconds;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void UCSOFindCoverAsync::Activate()
{
    if (bActivated || bCancelled) return;
    bActivated = true;
    if (bInputRejected)
    {
        FCSOCoverQueryResult Failure;
        Failure.Status = ECSOCoverQueryStatus::InvalidRequest;
        Completed.Broadcast(Failure);
        SetReadyToDestroy();
        return;
    }
    if (!QueryWorld.IsValid() || PendingQueries.Num() >= MaxQueueSize)
    {
        FCSOCoverQueryResult Failure;
        Failure.Status = QueryWorld.IsValid() ? ECSOCoverQueryStatus::BudgetExceeded : ECSOCoverQueryStatus::Unavailable;
        Failure.bSearchTruncated = QueryWorld.IsValid();
        Completed.Broadcast(Failure);
        SetReadyToDestroy();
        return;
    }
    PendingQueries.Add(this);
    if (!QueueTicker.IsValid()) QueueTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&UCSOFindCoverAsync::PumpQueue));
}

void UCSOFindCoverAsync::Cancel()
{
    bCancelled = true;
    PendingQueries.RemoveAll([this](const TWeakObjectPtr<UCSOFindCoverAsync>& Item) { return !Item.IsValid() || Item.Get() == this; });
    SetReadyToDestroy();
}

void UCSOFindCoverAsync::Execute()
{
    if (bCancelled) return;
    FCSOCoverQueryResult Result;
    Result.Status = ECSOCoverQueryStatus::Unavailable;
    if (UWorld* World = QueryWorld.Get(); World && !World->bIsTearingDown && !World->IsUnreachable())
    {
        if (UCSOCoverSubsystem* Subsystem = World->GetSubsystem<UCSOCoverSubsystem>())
        {
            Result = bReserve ? Subsystem->FindAndClaimCover(Request, Lease) : Subsystem->FindCover(Request);
        }
    }
    Completed.Broadcast(Result);
    SetReadyToDestroy();
}

bool UCSOFindCoverAsync::PumpQueue(float DeltaTime)
{
    const double Start = FPlatformTime::Seconds();
    const int32 Count = FMath::Clamp(AsyncQueriesPerFrame.GetValueOnGameThread(), 1, 16);
    const double Seconds = FMath::Max(0.1f, AsyncMilliseconds.GetValueOnGameThread()) * 0.001;
    for (int32 Index = 0; Index < Count && !PendingQueries.IsEmpty(); ++Index)
    {
        const TWeakObjectPtr<UCSOFindCoverAsync> Action = PendingQueries[0];
        PendingQueries.RemoveAt(0, 1, EAllowShrinking::No);
        if (Action.IsValid()) Action->Execute();
        if (FPlatformTime::Seconds() - Start >= Seconds) break;
    }
    if (PendingQueries.IsEmpty()) { QueueTicker.Reset(); return false; }
    return true;
}

void UCSOFindCoverAsync::ShutdownQueue()
{
    if (QueueTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(QueueTicker);
    QueueTicker.Reset();
    const TArray<TWeakObjectPtr<UCSOFindCoverAsync>> Remaining = MoveTemp(PendingQueries);
    PendingQueries.Reset();
    for (const TWeakObjectPtr<UCSOFindCoverAsync>& Action : Remaining) if (Action.IsValid()) Action->Cancel();
}
