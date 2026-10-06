// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SmartObjectRequestTypes.h"
#include "StateTreeTaskBase.h"
#include "HMS_FindSmartObjectTask.generated.h"

class AActor;
class USmartObjectBehaviorDefinition;

/** 多个候选插槽同时满足条件时，决定最终返回哪一个。 */
UENUM(BlueprintType)
enum class EHMS_FindSmartObjectSelectionMethod : uint8
{
	Closest UMETA(DisplayName = "距离查询中心最近"),
	Farthest UMETA(DisplayName = "距离查询中心最远"),
	Random UMETA(DisplayName = "随机")
};

/** Find Smart Object 状态树任务的可绑定输入和输出。 */
USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_FindSmartObjectTaskInstanceData
{
	GENERATED_BODY()

	/**
	 * 发起查询的角色。它会参与 Smart Object 条件计算。
	 * 未绑定时，任务会尝试使用 StateTree 所有者；若所有者是 Controller，则使用它控制的 Pawn。
	 */
	UPROPERTY(EditAnywhere, Category = "Context", meta = (Optional, DisplayName = "Actor"))
	TObjectPtr<AActor> UserActor = nullptr;

	/**
	 * 为 true：以查询角色的实时世界位置为中心。
	 * 为 false：以“查询位置”为中心，适合测试固定区域或替其他系统查询。
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "根据角色位置查询"))
	bool bUseCharacterLocation = true;

	/** 仅在不使用角色位置时显示；这是世界空间坐标。 */
	UPROPERTY(EditAnywhere, Category = "Parameter",
		meta = (DisplayName = "查询位置", EditCondition = "!bUseCharacterLocation", EditConditionHides))
	FVector QueryLocation = FVector::ZeroVector;

	/** 查询盒的半尺寸。例如 (1000,1000,500) 表示完整尺寸为 2000×2000×1000。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Search Box Extents"))
	FVector SearchBoxExtents = FVector(1000.0, 1000.0, 500.0);

	/** 样例中的 Closest Distance / Farthest Distance / Random。 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Search Type"))
	EHMS_FindSmartObjectSelectionMethod SelectionMethod = EHMS_FindSmartObjectSelectionMethod::Closest;

	/**
	 * 查询者向 Smart Object 声明的标签。
	 * 例如 SmartObject.ObjectType.NPC 可匹配“允许 NPC 使用”的椅子定义。
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Smart Object User Tags"))
	FGameplayTagContainer UserTags;

	/** 最终选中的 Smart Object Actor。 */
	UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Smart Object"))
	TObjectPtr<AActor> SmartObjectActor = nullptr;

	/** 最终选中的插槽句柄，可直接绑定给后续 Claim Smart Object 任务。 */
	UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Candidate Slot"))
	FSmartObjectSlotHandle CandidateSlot;
};

/**
 * 在指定盒形范围内查找满足标签和行为条件的 Smart Object 插槽。
 * 任务进入状态时只执行一次：找到候选返回 Succeeded，否则返回 Failed；本任务不会 Claim 插槽。
 */
USTRUCT(meta = (DisplayName = "HMS Find Smart Object", Category = "HMS|Smart Object"))
struct HYBRIDMOTIONSYSTEM_API FHMS_FindSmartObjectTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHMS_FindSmartObjectTaskInstanceData;

	FHMS_FindSmartObjectTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
