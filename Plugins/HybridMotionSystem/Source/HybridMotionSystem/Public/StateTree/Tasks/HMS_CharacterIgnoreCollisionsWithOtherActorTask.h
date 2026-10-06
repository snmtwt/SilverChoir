// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "HMS_CharacterIgnoreCollisionsWithOtherActorTask.generated.h"

class AActor;
/** HMS 角色与另一个 Actor 设置移动碰撞忽略关系的输入数据。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_CharacterIgnoreCollisionsWithOtherActorTaskInstanceData
{
	GENERATED_BODY()

	/** 要修改移动碰撞的角色；未绑定时会从 StateTree Owner 解析。 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> CharacterActor = nullptr;

	/** 交互期间需要临时忽略的 Actor，例如当前使用的长椅。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "Other Actor"))
	TObjectPtr<AActor> OtherActor = nullptr;

	/** true 表示忽略碰撞；false 表示恢复碰撞。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Should Ignore"))
	bool bShouldIgnore = true;
};

/**
 * 设置角色所有启用碰撞的 PrimitiveComponent 是否忽略另一个 Actor。
 * 与原蓝图相同，设置完成后任务立即成功；需要恢复时再次执行并将 Should Ignore 设为 false。
 */
USTRUCT(meta = (DisplayName = "HMS Character Ignore Collisions With Other Actor", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_CharacterIgnoreCollisionsWithOtherActorTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_CharacterIgnoreCollisionsWithOtherActorTaskInstanceData;

	FHMS_CharacterIgnoreCollisionsWithOtherActorTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
