#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "CSOCoverTypes.h"
#include "CSOFindCoverAsync.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOAsyncCoverCompleted, const FCSOCoverQueryResult&, Result);

/** Queues bounded queries across frames. World collision and Smart Object operations stay on the game thread. */
UCLASS()
class COVERSMARTOBJECTS_API UCSOFindCoverAsync : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, Category="Cover Smart Objects") FCSOAsyncCoverCompleted Completed;

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects", meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"))
    static UCSOFindCoverAsync* FindCoverQueued(UObject* WorldContextObject, const FCSOCoverQuery& Query, bool bClaim = true, float LeaseSeconds = 10.f);

    UFUNCTION(BlueprintCallable, Category="Cover Smart Objects") void Cancel();
    virtual void Activate() override;
    static void ShutdownQueue();

private:
    void Execute();
    static bool PumpQueue(float DeltaTime);
    UPROPERTY() FCSOCoverQuery Request;
    TWeakObjectPtr<UWorld> QueryWorld;
    bool bReserve = true;
    bool bCancelled = false;
    bool bActivated = false;
    bool bInputRejected = false;
    float Lease = 10.f;
};
