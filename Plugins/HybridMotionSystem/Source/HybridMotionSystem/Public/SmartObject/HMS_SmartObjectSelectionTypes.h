// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "HMS_SmartObjectSelectionTypes.generated.h"

/**
 * HMS Smart Object 动画代理表的查询输入。
 * 对应样例工程的 SmartObjectSelectionInputs，但类型完全归 HMS 插件所有。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "HMS Smart Object Selection Inputs"))
struct HYBRIDMOTIONSYSTEM_API FHMS_SmartObjectSelectionInputs
{
	GENERATED_BODY()

	/** HMS 动画实例中 Pose History Collector 当前保存的姿势历史。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object", meta = (DisplayName = "Pose History Node"))
	FPoseHistoryReference PoseHistoryNode;

	/** 角色当前朝向与交互入口方向之间的有符号角度，单位为度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object", meta = (DisplayName = "Target Angle"))
	double TargetAngle = 0.0;

	/** 角色沿导航路径到交互入口的剩余距离，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object", meta = (DisplayName = "Target Distance"))
	double TargetDistance = 0.0;
};

/**
 * HMS Smart Object 动画代理表写回的选择结果。
 * 对应样例工程的 SmartObjectSelectionOutputs，但类型完全归 HMS 插件所有。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "HMS Smart Object Selection Outputs"))
struct HYBRIDMOTIONSYSTEM_API FHMS_SmartObjectSelectionOutputs
{
	GENERATED_BODY()

	/** Motion Match 代价；数值越小，动画越适合角色当前姿势。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object")
	double Cost = 0.0;

	/** 选中动画的建议播放起始时间，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Smart Object", meta = (DisplayName = "Start Time"))
	double StartTime = 0.0;
};

