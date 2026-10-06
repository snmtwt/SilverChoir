#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GTS_TimeTask.generated.h"

class UGTS_TimeManager;

/** 由游戏时间管理器创建。注册期间由管理器持有；回调完成后可再次注册同一实例。 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta=(DisplayName="游戏时间回调任务"))
class GAMETIMESYSTEM_API UGTS_TimeTask : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;

	/** ScheduledTime 是登记的目标时间，CurrentTime 是触发时已经推进到的当前时间。 */
	UFUNCTION(BlueprintNativeEvent, Category="游戏时间|任务", meta=(DisplayName="时间回调任务"))
	void OnTimeReached(FDateTime ScheduledTime, FDateTime CurrentTime);
	virtual void OnTimeReached_Implementation(FDateTime ScheduledTime, FDateTime CurrentTime);

	UFUNCTION(BlueprintPure, Category="游戏时间|任务", meta=(DisplayName="获取任务时间管理器"))
	UGTS_TimeManager* GetTimeManager() const;

private:
	friend class UGTS_TimeManager;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGTS_TimeManager> Manager;

	FGuid PendingRegistrationId;
};
