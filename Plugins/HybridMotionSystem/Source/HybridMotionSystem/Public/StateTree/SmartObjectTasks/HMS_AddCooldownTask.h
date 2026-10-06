// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "HMS_AddCooldownTask.generated.h"

class AAIController;

USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_AddCooldownTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context", meta = (DisplayName = "Actor"))
	TObjectPtr<AAIController> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Cooldown Tag"))
	FGameplayTag CooldownTag;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Cooldown Time", ClampMin = "0.0"))
	float CooldownTime = 5.0f;
};

/** 把“标签 → 世界时间到期值”写入 HMS 交互组件，并立即成功。 */
USTRUCT(meta = (DisplayName = "HMS Add Cooldown", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_AddCooldownTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_AddCooldownTaskInstanceData;
	FHMS_AddCooldownTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

