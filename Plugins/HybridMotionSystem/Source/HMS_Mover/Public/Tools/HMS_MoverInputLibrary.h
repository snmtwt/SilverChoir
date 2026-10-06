// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MoverSimulationTypes.h"
#include "Structs/HMS_MoverStructs.h"
#include "HMS_MoverInputLibrary.generated.h"

class UNavMoverComponent;

/**
 * 
 */
UCLASS()
class HMS_MOVER_API UHMS_MoverInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

    /**
     * @brief 玩家输入专用输入生产器（第三人称 WASD + 鼠标/控制器旋转）
     *
     * 专为玩家控制单位设计。
     * - RotationMode 决定最终朝向：
     *     - OrientToMovement → 面向移动方向
     *     - Strafe         → 面向 Controller 前向（侧移）
     *     - Aim            → 面向 Controller 前向（瞄准模式）
     */
    UFUNCTION(BlueprintCallable, Category = "HMS|Mover|Input",
        meta = (ToolTip = "玩家输入专用：WASD + Controller旋转。推荐用于玩家小队单位。",
            DisplayName = "Produce Player HMS Input",
            Keywords = "Mover, Player, Input, Strafe, Aim, Controller"))
    static void ProducePlayerHMSInput(
        AController* Controller,
        APawn* OwningPawn,
        FVector2D PlayerMoveInput,
        EHMS_RotationMode RotationMode,
        EHMS_Gait Gait,
        float AimTurnThresholdDegrees,
        FMoverInputCmdContext& OutInputCmd
    );

    /**
     * @brief AI导航输入专用输入生产器（NavMover + Controller朝向）
     *
     * 专为AI控制单位设计。
     * - RotationMode 决定最终朝向（逻辑与玩家版本完全一致）
     */
    UFUNCTION(BlueprintCallable, Category = "HMS|Mover|Input",
        meta = (ToolTip = "AI导航专用：NavMover + Controller旋转。推荐用于AI小队单位。",
            DisplayName = "Produce Nav HMS Input",
            Keywords = "Mover, Nav, AI, Navigation, Strafe, Aim"))
    static void ProduceNavHMSInput(
        AController* Controller,
        APawn* OwningPawn,
        UNavMoverComponent* NavMover,
        EHMS_RotationMode RotationMode,
        EHMS_Gait Gait,
        float AimTurnThresholdDegrees,
        FMoverInputCmdContext& OutInputCmd
    );
};
