// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "HMS_ClearFocusTask.generated.h"

class AAIController;

USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_ClearFocusTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context", meta = (DisplayName = "Actor"))
	TObjectPtr<AAIController> Actor = nullptr;
};

/** 清除 AIController 的 Gameplay 焦点；与样例相同，任务本身保持 Running。 */
USTRUCT(meta = (DisplayName = "HMS Clear Focus", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_ClearFocusTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_ClearFocusTaskInstanceData;
	FHMS_ClearFocusTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

