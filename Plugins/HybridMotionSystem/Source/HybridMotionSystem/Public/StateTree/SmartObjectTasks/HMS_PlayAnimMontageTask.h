// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmartObjectTypes.h"
#include "StateTreeTaskBase.h"
#include "HMS_PlayAnimMontageTask.generated.h"

class AActor;
class UAnimMontage;
class UPlayMontageCallbackProxy;
class UPrimitiveComponent;

USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_PlayAnimMontageTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Slot Handle"))
	FSmartObjectSlotHandle SlotHandle;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Montage to Play"))
	TObjectPtr<UAnimMontage> MontageToPlay = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Start Time", ClampMin = "0.0"))
	float StartTime = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Num Loops"))
	int32 NumLoops = 1;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Play Rate", ClampMin = "0.01"))
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Play Time", ClampMin = "0.0"))
	float PlayTime = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Play Time Variance", ClampMin = "0.0"))
	float RandomPlayTimeVariance = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Ignore Collision"))
	bool bIgnoreCollision = false;

	UPROPERTY(Transient)
	TObjectPtr<UPlayMontageCallbackProxy> PlaybackProxy = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> CollisionComponents;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SmartObjectActor = nullptr;

	UPROPERTY(Transient)
	float ElapsedTime = 0.0f;

	UPROPERTY(Transient)
	float TargetPlayDuration = 0.0f;

	UPROPERTY(Transient)
	bool bInfinitePlayback = false;

	UPROPERTY(Transient)
	bool bPlaybackActive = false;
};

/** 无样例 AC_SmartObjectAnimation 依赖的 Montage 播放任务。 */
USTRUCT(meta = (DisplayName = "HMS Play Anim Montage", Category = "HMS|Smart Object|Interaction"))
struct HYBRIDMOTIONSYSTEM_API FHMS_PlayAnimMontageTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_PlayAnimMontageTaskInstanceData;
	FHMS_PlayAnimMontageTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

