#pragma once

#include "CoreMinimal.h"
#include "MovementMode.h"
#include "HMS_RagdollMode.generated.h"

/**
 * UE 5.8 Game Animation Sample 的 BP_MovementMode_Ragdoll 的 C++ 对应实现。
 *
 * 该模式不计算角色运动意图，也不直接读取骨骼组件。AnimationDataComponent
 * 在输入生产阶段提供物理身体对应的目标胶囊变换；Mover 在预测模拟中唯一负责
 * 应用该变换并写回同步状态，从而避免“组件 Tick 移 Actor”与 Mover 后端互相覆盖。
 */
UCLASS(DisplayName = "HMS Ragdoll Movement Mode")
class HMS_MOVER_API UHMS_RagdollMode : public UBaseMovementMode
{
	GENERATED_BODY()

public:
	UHMS_RagdollMode();
	virtual void SimulationTick_Implementation(
		const FSimulationTickParams& Params,
		FMoverTickEndData& OutputState) override;
};
