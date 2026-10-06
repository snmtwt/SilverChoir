// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "Structs/HMS_MoverStructs.h"
#include "HMS_WalkingMode.generated.h"

/**
 * HMS 自有的平滑行走预测状态。
 *
 * Mover 自带的 FSmoothWalkingState 位于引擎模块的 Private 目录，外部插件不能
 * 安全依赖。保留等价状态也可以让起步/停步弹簧参与回滚、网络校正和插值。
 */
USTRUCT()
struct HMS_MOVER_API FHMS_SmoothWalkingState : public FMoverDataStructBase
{
	GENERATED_BODY()

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override;
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override;

	FVector SpringVelocity = FVector::ZeroVector;
	FVector SpringAcceleration = FVector::ZeroVector;
	FVector IntermediateVelocity = FVector::ZeroVector;
	FQuat IntermediateFacing = FQuat::Identity;
	FVector IntermediateAngularVelocity = FVector::ZeroVector;
};


/**
 * @brief Gait 移动参数设置
 *
 * 为不同移动状态（Walk / Run / Sprint）提供独立的物理参数。
 * 这些参数直接影响 Mover 生成的 ProposedMove，进而影响上层动画查询系统 (Chooser + PoseSearch) 的运动匹配质量。
 *
 * 设计原则（UE5.7 Mover2.0+ 动画插件开发）：
 * - 每个 Gait 独立调参，便于美术/策划快速迭代不同角色的移动手感。
 * - 所有数值单位统一为 cm / s / s²，符合 UE 物理单位。
 */
USTRUCT(BlueprintType)
struct FHMSGaitMovementSettings
{
	GENERATED_BODY()

	/** 最大移动速度 (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "最大移动速度", ToolTip = "该 Gait 下的理论最高速度。角色会逐渐加速到此值。"))
	float MaxSpeed = 300.f;

	/** 加速度 (cm/s²) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "加速度", ToolTip = "角色向目标速度加速的加速度。值越大起步越快，建议 200~800。"))
	float Acceleration = 100.f;

	/** 无输入时的刹车减速度 (cm/s²) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "停止减速度", ToolTip = "没有移动输入时使用的减速度。值越大停得越干脆。"))
	float BrakingDeceleration = 1400.f;

	/** 超速时的回收减速度 (cm/s²) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "状态切换减速度", ToolTip = "当前速度高于 MaxSpeed 时用于回收的减速度。"))
	float GaitChangeDeceleration = 1600.f;

	/** 反向移动时的额外刹车倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "反向刹车倍率", ToolTip = "反向移动时 BrakingDeceleration 的倍率。建议 1.5~3.0，避免瞬间折返。"))
	float ReverseBrakeMultiplier = 1.8f;

	/** 角色朝向旋转速度 (度/秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "角色旋转速度", ToolTip = "逻辑层朝向变化的最大角速度。使用 RootOffset 时可设很高 (3000~10000)。"))
	float RotationRateDegrees = 3600.f;

	/** 速度方向旋转速度 (度/秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "速度方向旋转速度"))
	float VelocityTurnRateDegrees = 360.f;

	/** 朝向平滑时间 (秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "朝向平滑时间", ToolTip = "越小越跟手，越大越平滑。使用 RootOffset 时可设很小 (0.001~0.02)。"))
	float FacingSmoothingTime = 0.01f;

	/** 样例 SmoothWalkingMode 使用的转向强度，而不是硬旋转速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (DisplayName = "速度转向强度", ClampMin = "0.0"))
	float TurningStrength = 8.0f;
};


/**
 * @brief 自定义 WalkingMode（HMS 核心移动逻辑）
 *
 * 继承自 UWalkingMode，负责为 Mover2.0 生成高质量的 FProposedMove。
 *
 * 在 UE5.7 + Mover2.0 + 动画查询插件开发中的定位：
 * - 为 HMS_AnimInstance 提供准确的 Velocity、Acceleration、Facing 等数据。
 * - 支持玩家输入 + NavMover 双输入源。
 * - 通过 Gait + RotationMode 驱动上层 Chooser 查询和 PoseSearch 运动匹配。
 *
 * 优化亮点：
 * - 模块化拆分（输入处理 / 线速度 / 角速度）
 * - 更清晰的运动学分支逻辑 + 详细中文注释
 * - 轻微性能优化（减少重复计算、const 正确性）
 */
UCLASS()
class HMS_MOVER_API UHMS_WalkingMode : public UWalkingMode
{
	GENERATED_BODY()

public:
	UHMS_WalkingMode();

	virtual void OnRegistered(const FName ModeName, const FMoverSimContext& SimContext) override;
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;
	virtual void GenerateMove_Implementation(
		const FMoverSimContext& SimContext,
		const FMoverTickStartData& StartState,
		const FMoverTimeStep& TimeStep,
		FProposedMove& OutProposedMove) const override;

protected:
	// ==================== Gait 设置 ====================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Gait")
	FHMSGaitMovementSettings WalkSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Gait")
	FHMSGaitMovementSettings RunSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Gait")
	FHMSGaitMovementSettings SprintSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|General")
	float StoppingDeceleration = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|General")
	float GaitChangeDeceleration = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|General")
	float CrouchSpeed = 165.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|General")
	float IdleFacingSmoothingTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|General")
	bool bUseSlopeRelativeVelocity = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float DirectionalAccelerationFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float AccelerationSmoothingTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float DecelerationSmoothingTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AccelerationSmoothingCompensation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DecelerationSmoothingCompensation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float OutsideInfluenceSmoothingTime = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	bool bSmoothFacingWithDoubleSpring = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float VelocityDeadzoneThreshold = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float AccelerationDeadzoneThreshold = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float FacingDeadzoneThreshold = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing", meta = (ClampMin = "0.0"))
	float AngularVelocityDeadzoneThreshold = 0.01f;

protected:
	/** 根据 Gait 返回对应参数 */
	const FHMSGaitMovementSettings& GetSettingsByGait(EHMS_Gait Gait) const;

	void GenerateSmoothWalkMove(
		FMoverTickStartData& StartState,
		float DeltaSeconds,
		const FMoverSimContext& SimContext,
		const FVector& DesiredVelocity,
		const FQuat& DesiredFacing,
		const FQuat& CurrentFacing,
		float AccelerationAmount,
		float DecelerationAmount,
		float TurningStrengthAmount,
		float FacingSmoothingTimeAmount,
		FVector& InOutAngularVelocityDegrees,
		FVector& InOutVelocity) const;

	static const FName DidGenerateMoveEntry;

};
