// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HMS_MovementStruct.h"
#include "Animation/TrajectoryTypes.h"
#include "AnimationWarpingTypes.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "Animation/AnimNodeReference.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "Chooser.h"
#include "GameplayTagContainer.h"
#include "HMS_AnimInstance.generated.h"

class APawn;
class AActor;
class AController;
class UMoverComponent;
class UNavMoverComponent;
class UMoverTrajectoryPredictor;
class UHMS_AnimationDataComponent;
class UAnimationAsset;
class UAnimMontage;
struct FPoseSearchTrajectory_WorldCollisionResults;

UCLASS(Blueprintable, BlueprintType)
class HYBRIDMOTIONSYSTEM_API UHMS_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UHMS_AnimInstance();

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	/** Game-thread handoff check, published only after an animation evaluation completes. */
	bool HasEvaluatedBlendStackState(FGameplayTag ExpectedState, uint64 NotBeforeFrame) const;

protected:
	UFUNCTION(BlueprintCallable, Category = "HMS|Initialization")
	virtual void CacheOwningPawn();

	UFUNCTION(BlueprintCallable, Category = "HMS|Initialization")
	virtual void CacheMoverComponent();

	UFUNCTION(BlueprintCallable, Category = "HMS|Initialization")
	virtual void CacheNavMoverComponent();

	UFUNCTION(BlueprintCallable, Category = "HMS|Initialization")
	virtual void CacheAnimationDataComponent();

	UFUNCTION(BlueprintCallable, Category = "HMS|Initialization")
	virtual void InitializeMoverPredictor();

public:
	UFUNCTION(BlueprintPure, Category = "HMS|References")
	APawn* GetCachedPawn() const;

	UFUNCTION(BlueprintPure, Category = "HMS|References")
	UMoverComponent* GetCachedMoverComponent() const;

	UFUNCTION(BlueprintPure, Category = "HMS|References")
	UNavMoverComponent* GetCachedNavMoverComponent() const;

	UFUNCTION(BlueprintPure, Category = "HMS|References")
	UMoverTrajectoryPredictor* GetMoverTrajectoryPredictor() const;

	UFUNCTION(BlueprintPure, Category = "HMS|References", meta = (DisplayName = "获取 HMS 动画数据组件"))
	UHMS_AnimationDataComponent* GetAnimationDataComponent() const { return CachedAnimationDataComponent; }

protected:
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> CachedPawn;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedOwnerActor;

	UPROPERTY(Transient)
	TObjectPtr<UMoverComponent> CachedMoverComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNavMoverComponent> CachedNavMoverComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMoverTrajectoryPredictor> Predictor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UHMS_AnimationDataComponent> CachedAnimationDataComponent = nullptr;

protected:
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "HMS|Animation")
	FAnimNodeReference GetOffsetRootNodeReference() const;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "HMS|Animation")
	FAnimNodeReference GetStateMachineBlendStackNodeReference() const;

protected:
	// =========================
	// Blueprint Config Inputs
	// =========================

	/** 默认姿势，避免第一帧为空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HMS|States")
	FGameplayTagContainer Stance;

	/** 姿势查询表（动作查询系统核心）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|StateMachine")
	TObjectPtr<UChooserTable> ChooserTable = nullptr;

	/**
	 * Pivot 建立姿势时的保护上限；实际窗口不超过混合时间加 0.05 秒。
	 * 同一目标的重复查询仍会被抑制，新目标在建姿结束后可打断当前动作。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|Pivot", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "折返动作最短锁定时间"))
	float PivotSelectionLockDuration = 0.6f;

	/**
	 * Start/Reface 建姿保护时间的上限，实际不超过混合时间加 0.05 秒。
	 * UE5.8 状态图的 Rotation Flipped 条件可能在 180 度起步期间连续数帧成立；不加保护会
	 * 每帧重置 BlendStack，并在首个起步动作建立姿势之前把它替换掉。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|PoseSearch", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "起步动作重选保护时间"))
	float StartSelectionLockDuration = 0.6f;

	/** Leave the authored re-facing motion visible when starting from idle or a stop. */
	UPROPERTY(EditDefaultsOnly, Category="HMS|PoseSearch", meta=(DisplayName="转身起步混合时间", ClampMin="0.05", ClampMax="0.3", Units="s"))
	float RefaceStartBlendTime = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category="HMS|PoseSearch", meta=(DisplayName="转身起步允许跳过的旋转比例", ClampMin="0", ClampMax="0.3"))
	float RefaceStartSkippedYawFraction = 0.1f;

	UPROPERTY(EditAnywhere, Category = "HMS|Ground")
	float GroundNormalInterpSpeed = 12.0f;

	/** 落地前一帧的垂直下落速度达到该值时，视为重落地。 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|Ground", meta = (DisplayName = "重落地下落速度阈值", ClampMin = "0.0", Units = "cm/s"))
	float HeavyLandingVerticalSpeedThreshold = 700.0f;

	/** 仅在空中执行的廉价单线检测，用于落地预测和下落 Chooser 条件。 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|Ground", meta = (DisplayName = "空中地面检测距离", ClampMin = "100.0", Units = "cm"))
	float AirGroundTraceDistance = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "HMS|AimOffset")
	float AimOffsetSmoothingTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "HMS|RootOffset")
	bool OffsetRootBoneEnabled = false;

	UPROPERTY(EditDefaultsOnly, Category = "HMS|RootOffset")
	float OffsetRootTranslationRadius = 0.0f;

	/** 输入方向与当前水平速度夹角超过此值时视为折返。 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|RootOffset", meta = (DisplayName = "折返判定角度", ClampMin = "90.0", ClampMax = "180.0", Units = "deg"))
	float SharpReversalAngleThreshold = 110.0f;

	/** 折返期间快速释放旧方向的根骨平移偏移。 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|RootOffset", meta = (DisplayName = "折返根骨释放半衰期", ClampMin = "0.0", Units = "s"))
	float SharpReversalTranslationHalfLife = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "HMS|MovementDirection")
	float MovementDirectionForwardHalfAngle = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HMS|MovementDirection")
	float MovementDirectionBackwardHalfAngle = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HMS|MovementDirection", meta = (DisplayName = "方向判定滞回角度", ClampMin = "0.0", ClampMax = "30.0"))
	float MovementDirectionHysteresisDegrees = 6.0f;

	/** NoCollision is the scalable default for Mover; Full preserves the former collision-adjusted path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HMS|Trajectory", meta = (DisplayName = "轨迹质量"))
	EHMS_TrajectoryQuality TrajectoryQuality = EHMS_TrajectoryQuality::NoCollision;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HMS|Trajectory", meta = (DisplayName = "轨迹更新间隔", ClampMin = "0.0", Units = "s"))
	float TrajectoryUpdateInterval = 0.033333f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HMS|Trajectory", meta = (DisplayName = "轻量轨迹起始 LOD", ClampMin = "0"))
	int32 LightweightTrajectoryLODThreshold = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HMS|StateMachine", meta = (DisplayName = "缓存重复动画查询"))
	bool bCacheRepeatedChooserQueries = true;

	/** 开始播放原地转身动画的最小剩余朝向差；退出阈值固定为 10 度以提供滞回。 */
	UPROPERTY(EditDefaultsOnly, Category = "HMS|TurnInPlace", meta = (ClampMin = "10.0", ClampMax = "90.0", Units = "deg"))
	float TurnInPlaceAngleThreshold = 50.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "HMS|Animation Data", meta = (DisplayName = "角色动画属性"))
	FHMS_AnimationProperties CharacterProperties;

public:
	const FHMS_AnimationProperties& GetCharacterProperties() const { return CharacterProperties; }
	float GetTurnInPlaceAngleThreshold() const { return TurnInPlaceAngleThreshold; }

	/**
	 * UE 5.8 Ragdoll 起身查询入口。动画蓝图应使用当前 PoseHistory 评估
	 * CHT_GetUpMontages，并同时返回选中的 Montage 与 Pose Match Start Time。
	 * 未实现或返回空 Montage 时，AnimationDataComponent 会使用固定的正/背面蒙太奇回退。
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category="HMS|Ragdoll",
		meta=(DisplayName="查询布娃娃起身动画"))
	UAnimMontage* ResolveRagdollGetUpAnimation(
		bool bFaceUp,
		bool bRollingGetUp,
		float& OutStartTime);

	/** 返回 AnimGraph 中名为 PoseHistory 的 Collector 引用，供 5.8 Chooser 上下文使用。 */
	UFUNCTION(BlueprintPure, Category="HMS|Ragdoll", meta=(DisplayName="获取 HMS 姿势历史"))
	FPoseHistoryReference GetHMSPoseHistory() const;

public:
	// ==========
	// Update Flow
	// ==========

	UFUNCTION(BlueprintCallable, Category = "HMS|Update")
	virtual void Update_PropertiesFromCharacter();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_Logic();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_Trajectory();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_EssentialValues();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_MovementDirectionData();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_States();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_AimOffset();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	virtual void Update_AdditiveLean();

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	bool IsMoving() const;

protected:
	template<typename TState>
	static bool AreStatesEqual(const TState& A, const TState& B);

	template<typename TState>
	void UpdateStateValues(
		const TState& NewState,
		TState& CurrentState,
		TState& LastFrameState,
		TState& RecentState,
		float& TimeInState,
		float& LastStateTime,
		float RecentTimeLimit
	);

	virtual FRotator GetControllerAimingRotation() const;

	virtual FVector GetSelectedInputAcceleration() const;

	virtual EHMS_MovementDirection CalculateMovementDirectionFromVectors(
		const FVector& InInputAcceleration
	) const;

	virtual void UpdateTransientMovementDirectionOverride();

	virtual EHMS_MovementDirection GetEffectiveMovementDirection() const;

public:
	// ==========
	// Trajectory
	// ==========

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	FTransformTrajectory Trajectory;

	UPROPERTY(Transient)
	FVector Trj_FutureVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	float Trj_TurnAngle = 0.f;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	float FutureFacingDelta = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	float FutureFacingDelta_LastFrame = 0.f;

	UPROPERTY(Transient)
	float PreviousDesiredControllerYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	float Trj_CirclingTime = 0.f;

	UPROPERTY(Transient)
	FPoseSearchTrajectory_WorldCollisionResults TrajectoryCollision;

	UPROPERTY(Transient)
	FVector Trj_PastVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector Trj_NearFutureVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector Trj_PreviousFutureVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	FRotator Trj_FutureFacing = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "HMS|Trajectory")
	FRotator FutureFacingOnTransitionStart = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	FVector Trj_PastAngularVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector Trj_CurrentAngularVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Trajectory")
	bool Trj_IsCircling = false;

protected:
	UPROPERTY(Transient)
	bool bUseTransientMovementDirectionOverride = false;

	UPROPERTY(Transient)
	EHMS_MovementDirection TransientMovementDirectionOverride = EHMS_MovementDirection::F;

public:
	// ===============
	// Essential Values
	// ===============

protected:
	UPROPERTY(Transient)
	FVector GroundNormal = FVector::UpVector;

	UPROPERTY(Transient)
	FVector GroundNormal_LastFrame = FVector::UpVector;

	UPROPERTY(Transient)
	FVector SmoothedGroundNormal = FVector::UpVector;

	UPROPERTY(Transient)
	FVector GroundLocation = FVector::ZeroVector;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Movement")
	float Speed2D = 0.0f;

	UPROPERTY(Transient)
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector Velocity_LastFrame = FVector::ZeroVector;

	UPROPERTY(Transient)
	bool HasVelocity = false;

	UPROPERTY(Transient)
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector Acceleration_LastFrame = FVector::ZeroVector;

	UPROPERTY(Transient)
	float AccelerationAmount = 0.0f;

	UPROPERTY(Transient)
	bool HasAcceleration = false;

	UPROPERTY(Transient)
	FVector VelocityAcceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector RelativeAcceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector LastNonZeroVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	EHMS_AccelerationSource AccelerationSource = EHMS_AccelerationSource::PlayerInput;

	UPROPERTY(Transient)
	FVector PlayerInputAcceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector NavInputAcceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector SelectedInputAcceleration = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator AimingRotation = FRotator::ZeroRotator;

private:

	FTransform CharacterTransform = FTransform::Identity;
	FTransform CharacterTransform_LastFrame = FTransform::Identity;
	FTransform RootTransform = FTransform::Identity;

protected:
	// ======
	// States
	// ======

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementMode MovementMode = EHMS_MovementMode::OnGround;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementMode MovementMode_LastFrame = EHMS_MovementMode::OnGround;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementMode MovementMode_Recent = EHMS_MovementMode::OnGround;

	/** 仅在从空中落地的首帧为真，供 Chooser 选择重落地动画。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States", meta = (DisplayName = "刚刚重落地"))
	bool JustLanded_Heavy = false;

	/** Chooser 兼容字段：离开 Traversing 后的短窗口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States", meta = (DisplayName = "刚刚完成穿越"))
	bool JustTraversed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	float MovementMode_Time = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	float MovementMode_LastStateTime = 0.0f;

	/** GASP 空中状态图所需的基础数据；由 C++ 统一计算，蓝图只负责表现。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "正在下落"))
	bool bIsFalling = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "刚刚离地"))
	bool bJustBecameAirborne = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "刚刚落地"))
	bool bJustLanded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "垂直速度", Units = "cm/s"))
	float VerticalVelocity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "滞空时间", Units = "s"))
	float AirTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "到达最高点时间", Units = "s"))
	float TimeToApex = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "距地高度", Units = "cm"))
	float GroundDistance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "预计落地时间", Units = "s"))
	float PredictedTimeToLand = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Air", meta = (DisplayName = "落地速度"))
	FVector LandingVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ground", meta = (DisplayName = "坡度角"))
	FVector2D SlopeAngle = FVector2D::ZeroVector;

	/** 来自 Mover OnBasedMovementApplied，供 Offset Root Bone OnUpdate 使用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|RootOffset", meta = (DisplayName = "基座移动增量"))
	FTransform BasedMovementDelta = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|RootOffset", meta = (DisplayName = "正在折返"))
	bool bSharpDirectionReversal = false;

	UPROPERTY(Transient)
	bool bSharpDirectionReversal_LastFrame = false;

	/** 输入与速度达到 Pivot 阈值，但不一定构成 110° 以上的前后折返。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Pivot", meta = (DisplayName = "正在即时转向"))
	bool bImmediateDirectionChangePivot = false;

	UPROPERTY(Transient)
	bool bImmediateDirectionChangePivot_LastFrame = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "布娃娃属性"))
	FHMS_RagdollProperties RagdollProperties;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_RotationMode RotationMode = EHMS_RotationMode::OrientToMovement;

	UPROPERTY(Transient)
	EHMS_RotationMode RotationMode_LastFrame = EHMS_RotationMode::OrientToMovement;

	UPROPERTY(Transient)
	EHMS_RotationMode RotationMode_Recent = EHMS_RotationMode::OrientToMovement;

	UPROPERTY(Transient)
	float RotationMode_Time = 0.0f;

	UPROPERTY(Transient)
	float RotationMode_LastStateTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementState MovementState = EHMS_MovementState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementState MovementState_LastFrame = EHMS_MovementState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementState MovementState_Recent = EHMS_MovementState::Idle;

	UPROPERTY(Transient)
	float MovementState_Time = 0.0f;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	float MovementState_LastStateTime = 0.0f;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_Gait Gait = EHMS_Gait::Walk;

	/** 样例状态过渡会比较当前步态与上一帧步态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States", meta = (DisplayName = "上一帧步态"))
	EHMS_Gait Gait_LastFrame = EHMS_Gait::Walk;

	UPROPERTY(Transient)
	EHMS_Gait Gait_Recent = EHMS_Gait::Walk;

	UPROPERTY(Transient)
	float Gait_Time = 0.0f;

	UPROPERTY(Transient)
	float Gait_LastStateTime = 0.0f;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	FGameplayTagContainer Stance_LastFrame;

	UPROPERTY(Transient)
	FGameplayTagContainer Stance_Recent;

	UPROPERTY(Transient)
	float Stance_Time = 0.0f;

	UPROPERTY(Transient)
	float Stance_LastStateTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementDirection MovementDirection = EHMS_MovementDirection::F;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementDirection MovementDirection_LastFrame = EHMS_MovementDirection::F;

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|States")
	EHMS_MovementDirection MovementDirection_Recent = EHMS_MovementDirection::F;

	UPROPERTY(Transient)
	float MovementDirection_Time = 0.0f;

	UPROPERTY(Transient)
	float MovementDirection_LastStateTime = 0.0f;

public:
	// =========
	// AimOffset
	// =========

	/** 蓝图需要读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|AimOffset")
	FVector2D AO = FVector2D::ZeroVector;

protected:
	UPROPERTY(Transient)
	FVector2D Previous_AO = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FRotator SmoothedAimTarget = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	FVector InOutAngularVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|AimOffset")
	bool EnableAO = false;

public:
	// ============
	// Additive Lean
	// ============

protected:
	UPROPERTY(Transient)
	float LateralAccelerationAmount = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|AdditiveLean")
	FVector2D LeanAmount = FVector2D::ZeroVector;

public:
	// =========================
	// StateMachine / BlendStack
	// =========================

protected:
	/** 蓝图需要读取。 */
	// Published on the game thread; interaction components never inspect worker-thread animation nodes.
	FGameplayTagContainer EvaluatedBlendStackState;
	uint64 EvaluatedBlendStackFrame = 0;
	bool bEvaluatedBlendStackLoopValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|StateMachine")
	FGameplayTagContainer StateMachineState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|StateMachine")
	bool NoValidAnim = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|StateMachine")
	FHMS_BlendStackInputs BlendStackInputs;

	/** 由 UE5.8 Pose Match Chooser 列写入，便于调试当前查询质量。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "HMS|PoseSearch", meta = (DisplayName = "查询成本"))
	double SearchCost = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|StateMachine")
	bool NotifyTransition_ReTransition = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|StateMachine")
	bool NotifyTransition_ToLoop = false;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|StateMachine")
	void SetBlendStackAnimFromChooser(
		FGameplayTagContainer InStateMachineState,
		bool bForceBlend
	);

public:
	// =====
	// Tools
	// =====

	/** 请求正在播放的原地转身响应新的外部朝向，并在下一动画帧重新查询动画。 */
	UFUNCTION(BlueprintCallable, Category = "HMS|TurnInPlace")
	void RequestTurnInPlaceReselect(
		const FVector& RequestedWorldFacingDirection,
		bool bForceReselect = true);

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Trajectory")
	float Get_TotalFacingDelta(const TArray<float>& Times) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Trajectory")
	float Get_TrajectoryTurnAngle() const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|StateMachine")
	float Get_DynamicPlayRate(const FAnimNodeReference& BlendStackInput) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Steering")
	bool EnableSteering(const FAnimNodeReference& Node) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Steering")
	FQuat Get_DesiredFacing(
		const FAnimNodeReference& Node,
		FName SteeringTargetTimeCurveName = TEXT("SteeringTargetTime")
	) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Steering")
	float Get_ProceduralTargetTime(
		const FAnimNodeReference& Node,
		FName CurveName = TEXT("SteeringTargetTime")
	) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Procedural")
	float Get_StrideWarpAlpha(
		const FAnimNodeReference& Node,
		FName EnableWarpingCurveName = TEXT("Enable_Warping"),
		FName EnableStrideWarpingCurveName = TEXT("Enable_StideWarping")
	) const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Procedural")
	float Get_StrafeWarpAlpha(
		const FAnimNodeReference& Node,
		FName EnableWarpingCurveName = TEXT("Enable_Warping"),
		FName EnableStrafeWarpingCurveName = TEXT("Enable_StrafeWarping")
	) const;

	/**
	 * Matches the UE 5.8 Game Animation Sample orientation-warping binding.
	 * Root-bone space is only valid while Offset Root Bone is enabled; otherwise
	 * graph-mode orientation warping must evaluate in component space.
	 */
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Procedural")
	EOrientationWarpingSpace Get_OrientationWarpingWarpingSpace() const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|RootOffset")
	EOffsetRootBoneMode Get_OffsetRootTranslationMode() const;

	/** Physics snapshots and DefaultSlot montages own the root; do not retain locomotion offsets. */
	UFUNCTION(BlueprintPure, Category="HMS|Animation", meta=(BlueprintThreadSafe))
	bool ShouldResetOffsetRoot() const;

	/** Avoid applying standing retarget offsets a second time to a physics snapshot. */
	UFUNCTION(BlueprintPure, Category="HMS|Animation", meta=(BlueprintThreadSafe))
	float Get_RetargetCorrectionAlpha() const { return RetargetCorrectionAlpha; }

	UPROPERTY(Transient)
	float RetargetCorrectionAlpha = 1.0f;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|RootOffset")
	EOffsetRootBoneMode Get_OffsetRootRotationMode() const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|RootOffset")
	float Get_OffsetRootTranslationHalfLife() const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|RootOffset")
	float Get_OffsetRootTranslationRadius() const;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|Warp")
	FVector Get_StrafeWarpDirection() const;

	/** 与 UE 5.8 GASP Get_BlendSpaceInputs 一致：为带 BS_Slope 标签的动画输出坡度 Blend Space 输入。 */
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, DisplayName = "获取混合空间输入"), Category = "HMS|BlendSpace")
	FVector Get_BlendSpaceInputs() const;
	/** Read node values in one call; animation-node references must not be retained across calls. */
	UFUNCTION(BlueprintPure, Category="HMS|Animation")
	UAnimationAsset* GetLocomotionPlayback(float& OutTime, FVector& OutBlendParameters, FTransform& OutRootTransform, bool& bOutRootValid) const;
	/** Native read-only snapshot for opt-in diagnostic recorders. */
	void GetLocomotionSelection(FHMS_BlendStackInputs& OutInputs, FGameplayTagContainer& OutState,
		EHMS_Gait& OutGait, EHMS_RotationMode& OutRotationMode) const
	{
		OutInputs = BlendStackInputs; OutState = StateMachineState;
		OutGait = Gait; OutRotationMode = RotationMode;
	}
	/** Default uses GASP's BS_Slope convention. Enable to drive custom blend spaces explicitly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|BlendSpace")
	bool bOverrideBlendSpaceInputs = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|BlendSpace", meta=(EditCondition="bOverrideBlendSpaceInputs"))
	FVector BlendSpaceInputOverride = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="HMS|BlendSpace", meta=(ClampMin="0", Units="deg"))
	float SlopeBlendDeadZone = 15.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="HMS|BlendSpace", meta=(ClampMin="1", Units="deg"))
	float SlopeBlendFullAngle = 30.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="HMS|Navigation")
	bool bNavigationBraking = false;

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|StateMachine")
	bool IsAnimationAlmostComplete();

	UFUNCTION(BlueprintPure, meta = (AnimGetter = "true", GetterContext = "Transition", BlueprintThreadSafe), Category = "HMS|Transition")
	bool ShouldTransition_LocomotionReselect();

	/**
	 * 兼容旧 GASP 过渡图的统一闸门。UE 5.8 中同一状态的多条 Re-Enter
	 * 规则仍可能在查询重试、Start 或 Pivot 播放期间同时成立；旧蓝图的
	 * LogDebug 只是原样返回布尔值，因而会反复重置 BlendStack。
	 */
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Transition")
	bool FilterLegacyTransitionRule(const FString& RuleName, bool bRawResult) const;

	// 是否进行原地旋转（Strafe/Aim Idle 时触发）
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Transition")
	bool ShouldTransition_TurnInPlaceToIdleLoop();

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	bool ShouldTurnInPlace();

	/** Re-enter a turn only for a new target or after the current clip finishes. */
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Transition")
	bool ShouldReselectTurnInPlace();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "HMS|Movement", meta = (BlueprintThreadSafe))
	bool IsPivoting() const;

	/** 当前是否仍处于已选 Pivot 动画的短暂建姿保护窗口。 */
	UFUNCTION(BlueprintPure, Category = "HMS|Movement", meta = (BlueprintThreadSafe, DisplayName = "折返动作是否锁定"))
	bool IsPivotSelectionLocked() const;
	bool IsStartSelectionLocked() const;

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	bool ShouldTransitionToLocomotionLoop(float StateTime);

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	bool ShouldBreakRotationAnimation(float StateTime, float EarlyStateTime = 0.5f);

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	bool ShouldTriggerSharpTurnTransition(int32 MachineIndex);

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	bool ShouldExitStartPivotByCircling(int32 MachineIndex);

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "HMS|Movement")
	FVector ResolveNavMoverInputAcceleration();

private:
	/** 播放范围尚未结束；仅用于禁止同状态重复查询，不阻止退出到 Loop/Idle。 */
	bool IsPivotSelectionActive() const;
	bool IsStartSelectionActive() const;
	bool IsTurnInPlaceSelectionActive() const;
	bool HasNewTurnInPlaceTarget() const;
	FVector GetTurnInPlaceTargetDirection() const;
	/** 当前输入是否要求打断正在播放的 Pivot，并改播相反方向的新 Pivot。 */
	bool HasOpposingPivotRequest() const;
	bool HasNewLocomotionTarget() const;
	bool HasLocomotionContextChanged() const;
	float GetSelectedAnimationTime() const;

	void UpdateLightweightTrajectory(float DeltaSeconds);
	bool ShouldUseLightweightTrajectory() const;
	FVector GetSelectedWorldInputDirection() const;
	float GetPivotTurnAngleThreshold() const;
	void ForceBlendStackNextUpdate() const;
	bool IsChooserContextCached(const FGameplayTagContainer& InStateMachineState) const;
	void CacheChooserContext(const FGameplayTagContainer& InStateMachineState);

	float TrajectoryUpdateAccumulator = 0.0f;
	bool bComponentCacheInitialized = false;
	bool bUsingLightweightTrajectory = false;
	TWeakObjectPtr<UAnimationAsset> CompletionTrackedAnimation;
	FGameplayTagContainer CompletionTrackedState;
	float CompletionTrackedElapsedSeconds = 0.0f;
	FRotator PivotFacingOnSelection = FRotator::ZeroRotator;
	bool bHasPivotFacingOnSelection = false;
	bool bHasCachedChooserContext = false;
	bool bTurnInPlaceReselectRequested = false;
	// Mouse targeting only publishes the explicit target angle. The 5.8 state
	// machine remains the sole owner of Chooser evaluation, including re-entry.
	bool bRestartTurnInPlaceFromBeginningOnNextSelection = false;
	int32 TurnInPlaceReselectDelayFrames = 0;
	FVector TurnInPlaceReselectFacingDirection = FVector::ZeroVector;
	FVector LastTurnInPlaceAnimationFacingDirection = FVector::ZeroVector;
	FVector TurnInPlaceFacingOnSelection = FVector::ZeroVector;
	uint64 LastTurnInPlaceRequestFrame = 0;
	/**
	 * UE5.8 的 Pose Search 数据库在编辑器中可能仍在异步构建索引。
	 * Chooser 此时会临时返回空结果；保存请求并在后续动画帧重试，
	 * 避免状态机把临时不可用误判成 NoValidAnim。
	 */
	bool bChooserRetryPending = false;
	bool bProcessingChooserRetry = false;
	bool bChooserRetryForceBlend = false;
	float ChooserRetryElapsedSeconds = 0.0f;
	FGameplayTagContainer ChooserRetryState;
	TWeakObjectPtr<UChooserTable> CachedChooserTable;
	FGameplayTagContainer CachedChooserState;
	FGameplayTagContainer CachedChooserStance;
	// Query cache only. Authoritative behavior and equipment tags remain Blueprint variables.
	FGameplayTag CachedChooserWeaponType;
	FGameplayTag CachedChooserBehaviorState;
	EHMS_MovementDirection CachedChooserMovementDirection = EHMS_MovementDirection::F;
	EHMS_Gait CachedChooserGait = EHMS_Gait::Walk;
	EHMS_RotationMode CachedChooserRotationMode = EHMS_RotationMode::OrientToMovement;
	EHMS_MovementMode CachedChooserMovementMode = EHMS_MovementMode::OnGround;

};
