#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HMS_MovementStruct.h"
#include "HMS_AnimationDataComponent.generated.h"

enum class EStanceMode : uint8;
class UMoverComponent;
class UAnimMontage;
class UPlayMoverMontageCallbackProxy;
class UPhysicsControlAsset;
class UPhysicsControlComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
struct FMoverTimeStep;
struct FHMS_MoverInput;

/** Network payload for animation semantics. Movement itself remains owned by Mover. */
USTRUCT()
struct FHMS_ReplicatedAnimationState
{
	GENERATED_BODY()

	UPROPERTY()
	EHMS_MovementMode MovementMode = EHMS_MovementMode::OnGround;

	UPROPERTY()
	EHMS_RotationMode RotationMode = EHMS_RotationMode::OrientToMovement;

	UPROPERTY()
	EHMS_Gait Gait = EHMS_Gait::Walk;

	UPROPERTY()
	EHMS_AccelerationSource AccelerationSource = EHMS_AccelerationSource::PlayerInput;

	UPROPERTY()
	FGameplayTagContainer Stance;

	UPROPERTY()
	FVector2D PlayerInputAcceleration = FVector2D::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal WorldSpaceMovementInput = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal FacingDirection = FVector::ForwardVector;

	UPROPERTY()
	FVector_NetQuantizeNormal AimingDirection = FVector::ForwardVector;

	UPROPERTY()
	uint8 bUseWorldSpaceMovementInput : 1 = false;

	UPROPERTY()
	uint8 bUseExternalFacingDirection : 1 = false;

	UPROPERTY()
	uint8 bUseExternalAimingDirection : 1 = false;
};

/**
 * Optional, non-invasive bridge between an Actor and UHMS_AnimInstance.
 * When absent, the original HMS character-interface workflow remains active.
 */
UCLASS(ClassGroup = (HybridMotionSystem), BlueprintType, Blueprintable,
	meta = (BlueprintSpawnableComponent, DisplayName = "HMS 动画数据组件"))
class HYBRIDMOTIONSYSTEM_API UHMS_AnimationDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHMS_AnimationDataComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "HMS|动画数据", meta = (DisplayName = "获取动画属性"))
	bool GetAnimationProperties(FHMS_AnimationProperties& OutProperties) const;

	UFUNCTION(BlueprintCallable, Category = "HMS|动画数据", meta = (DisplayName = "设置动画属性"))
	void SetAnimationProperties(const FHMS_AnimationProperties& NewProperties);

	UFUNCTION(BlueprintCallable, Category = "HMS|输入", meta = (DisplayName = "设置世界空间移动输入"))
	void SetWorldSpaceMovementInput(const FVector& WorldDirectionAndMagnitude);

	UFUNCTION(BlueprintCallable, Category = "HMS|输入", meta = (DisplayName = "设置本地玩家输入"))
	void SetPlayerInputAcceleration(const FVector2D& LocalInput);

	UFUNCTION(BlueprintCallable, Category = "HMS|方向", meta = (DisplayName = "设置朝向与瞄准方向"))
	void SetFacingAndAimingDirections(const FVector& WorldFacingDirection, const FVector& WorldAimingDirection);

	UFUNCTION(BlueprintCallable, Category = "HMS|状态", meta = (DisplayName = "设置移动模式"))
	void SetMovementMode(EHMS_MovementMode NewMovementMode);

	UFUNCTION(BlueprintCallable, Category = "HMS|布娃娃", meta = (DisplayName = "设置布娃娃动画数据"))
	void SetRagdollProperties(const FHMS_RagdollProperties& NewProperties);

	// =====================================================================
	// UE 5.8 SandboxCharacter_Mover_Ragdoll：对外事件（蓝图一比一入口）
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category="HMS|布娃娃", meta=(DisplayName="进入受伤布娃娃"))
	void EnterRagdoll(EHMS_RagdollInjuryState InjuryState = EHMS_RagdollInjuryState::None);

	/** 与样例 ExitRagdoll 一致：这里只请求 Walking；清理在模式退出事件执行。 */
	UFUNCTION(BlueprintCallable, Category="HMS|布娃娃", meta=(DisplayName="退出布娃娃"))
	void ExitRagdoll();

	UFUNCTION(BlueprintCallable, Category="HMS|布娃娃", meta=(DisplayName="切换布娃娃"))
	void ToggleRagdoll();

	UFUNCTION(BlueprintCallable, Category="HMS|布娃娃", meta=(DisplayName="设置布娃娃翻滚输入"))
	void SetRagdollRollInput(const FVector& WorldRollInput);

	UFUNCTION(BlueprintPure, Category="HMS|布娃娃", meta=(DisplayName="处于布娃娃"))
	bool IsRagdoll() const { return RagdollState == EHMS_RagdollState::Ragdoll; }

	UFUNCTION(BlueprintPure, Category="HMS|布娃娃", meta=(DisplayName="正在起身"))
	bool IsGettingUpFromRagdoll() const { return RagdollState == EHMS_RagdollState::GettingUp; }

	/** 样例 Get_RagdollTargetOrientation：与物理姿势 Transform 分离的胶囊朝向意图。 */
	UFUNCTION(BlueprintPure, Category="HMS|布娃娃", meta=(DisplayName="获取布娃娃目标朝向"))
	FVector GetRagdollTargetOrientation() const;

	/** 输入生产器调用：把样例 S_MoverCustomInputs_Ragdoll 写进当前 Mover 帧。 */
	void FillRagdollMoverInput(FHMS_MoverInput& OutInput) const;

	UFUNCTION(BlueprintPure, Category = "HMS|状态", meta = (DisplayName = "将 Mover 模式转换为 HMS 模式"))
	static EHMS_MovementMode ResolveMoverMovementMode(FName MoverModeName);

	UFUNCTION(BlueprintCallable, Category = "HMS|状态", meta = (DisplayName = "设置旋转模式"))
	void SetRotationMode(EHMS_RotationMode NewRotationMode);

	UFUNCTION(BlueprintCallable, Category = "HMS|状态", meta = (DisplayName = "设置步态"))
	void SetGait(EHMS_Gait NewGait);

	UFUNCTION(BlueprintCallable, Category = "HMS|状态", meta = (DisplayName = "设置姿态标签"))
	void SetStance(const FGameplayTagContainer& NewStance);

	UFUNCTION(BlueprintPure, Category = "HMS|方向", meta = (DisplayName = "获取解析后朝向"))
	FVector GetResolvedFacingDirection() const;

	UFUNCTION(BlueprintPure, Category = "HMS|方向", meta = (DisplayName = "获取解析后瞄准方向"))
	FVector GetResolvedAimingDirection() const;

	/** True preserves the original HMS controller-driven behavior. Disable for AI/Mass supplied directions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "HMS|方向", meta = (DisplayName = "使用控制器方向数据"))
	bool bUseControllerData = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|网络", meta = (DisplayName = "启用动画语义网络同步"))
	bool bEnableNetworkSync = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|网络", meta = (DisplayName = "方向网络更新频率", ClampMin = "1.0", ClampMax = "60.0"))
	float DirectionNetUpdateRate = 20.0f;

	/** 自动监听 Mover 的 Walking/Falling/Flying 以及自定义 Slide/Traversal/Ragdoll 模式。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|状态", meta = (DisplayName = "自动同步 Mover 移动模式"))
	bool bAutoDetectMoverMovementMode = true;

	// =====================================================================
	// UE 5.8 SandboxCharacter_Mover_Ragdoll：资产与物理配置
	// =====================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|引用")
	TObjectPtr<USkeletalMeshComponent> RagdollMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|引用")
	TObjectPtr<UPrimitiveComponent> RagdollCapsule = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|引用")
	TObjectPtr<UPhysicsControlComponent> PhysicsControl = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|引用")
	TSoftObjectPtr<UPhysicsControlAsset> PhysicsControlAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|骨骼")
	FName RagdollPelvisBone = TEXT("pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|骨骼")
	FName RagdollSpineBone = TEXT("spine_03");

	/** Derive the chest-facing axis from this mesh's reference pose, instead of assuming mannequin +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|骨骼")
	bool bAutoDetectRagdollFacingAxis = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|骨骼", meta=(EditCondition="!bAutoDetectRagdollFacingAxis"))
	FVector RagdollChestFacingAxis = FVector(0,1,0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

	/** Keep non-simulated hair/clothing/accessories from pushing the body they follow. Queries remain enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置", meta=(DisplayName="布娃娃时禁用附属模型物理碰撞"))
	bool bSuppressAttachmentPhysicsCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName CharacterCapsuleCollisionProfile = TEXT("CharacterCapsule");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName RagdollPhysicsProfile = TEXT("Ragdoll");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName DefaultPhysicsProfile = TEXT("Kinematic");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName PoseSnapshotName = TEXT("Ragdoll");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|配置")
	FName PoseHistoryName = TEXT("PoseHistory");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|起身")
	TObjectPtr<UAnimMontage> FaceUpGetUpMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|起身")
	TObjectPtr<UAnimMontage> FaceDownGetUpMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|检测", meta=(Units="cm"))
	float RagdollGroundTraceDistance = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|检测", meta=(Units="cm"))
	float RagdollGroundSphereRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMS|布娃娃|检测", meta=(Units="s"))
	float RagdollImpactPredictionTime = 1.0f;

	/** 样例的 Ragdoll_InjuryState 必须先于状态一起复制，远端进入物理时才能选择同一受伤姿势。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="HMS|布娃娃")
	EHMS_RagdollInjuryState ReplicatedRagdollInjuryState = EHMS_RagdollInjuryState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_RagdollState,
		Category="HMS|布娃娃")
	EHMS_RagdollState RagdollState = EHMS_RagdollState::Animated;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HMS|动画数据", meta = (ShowOnlyInnerProperties))
	FHMS_AnimationProperties AnimationProperties;

private:
	UFUNCTION()
	void HandleMoverStanceChanged(EStanceMode OldStance, EStanceMode NewStance);

	UFUNCTION()
	void HandleMoverMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName);

	// ===== 样例蓝图：TriggerRagdoll =====
	void ApplyEnterRagdoll(EHMS_RagdollInjuryState InjuryState);
	// ===== 样例蓝图：On_RagdollMode_Exit =====
	void ApplyRagdollModeExit();
	// ===== 样例蓝图：Ragdoll_UpdateProperties / UpdatePhysicsStrengthsFromCurves =====
	void UpdateRagdollFromPhysics(float DeltaTime);
	void UpdateRagdollPhysicsStrengthsFromCurves();
	// ===== 样例蓝图：计算 S_MoverCustomInputs_Ragdoll.RagdollTransform =====
	FTransform CalculateRagdollCapsuleTransform(bool& bOutFaceUp) const;
	void SetPhysicsProfile(FName ProfileName);
	bool InitializePhysicsControl();
	void StartGetUpMontage();
	void FinishGetUp();
	void ReleaseGetUpPlayback();
	UFUNCTION()
	void HandleGetUpPlaybackEnded(FName NotifyName);
	void CacheRagdollComponents();

	UFUNCTION()
	void HandleBasedMovementApplied(const FTransform& TransformDelta, const FMoverTimeStep& TimeStep);

	void ApplyMoverStance(EStanceMode NewStance);

	void PublishSemanticState();
	void TryPublishDirectionalState();
	void FillReplicatedState();
	void ApplyReplicatedState();
	bool IsLocallyControlledOwner() const;

	UFUNCTION()
	void OnRep_ReplicatedState();

	UFUNCTION()
	void OnRep_RagdollState(EHMS_RagdollState PreviousState);

	UFUNCTION(Server, Reliable)
	void ServerEnterRagdoll(EHMS_RagdollInjuryState InjuryState);

	UFUNCTION(Server, Reliable)
	void ServerExitRagdoll();

	UFUNCTION(Server, Reliable)
	void ServerSetSemanticState(EHMS_MovementMode NewMovementMode, EHMS_RotationMode NewRotationMode,
		EHMS_Gait NewGait, EHMS_AccelerationSource NewAccelerationSource, const FGameplayTagContainer& NewStance);

	UFUNCTION(Server, Unreliable)
	void ServerSetDirectionalState(FVector2D NewPlayerInput, FVector_NetQuantizeNormal NewWorldInput,
		FVector_NetQuantizeNormal NewFacingDirection, FVector_NetQuantizeNormal NewAimingDirection,
		bool bNewUseWorldInput, bool bNewUseExternalFacing, bool bNewUseExternalAiming,
		bool bNewUseControllerData);

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedState)
	FHMS_ReplicatedAnimationState ReplicatedState;

	UPROPERTY(Transient)
	TObjectPtr<UMoverComponent> CachedMoverComponent = nullptr;

	FVector RagdollRollInput = FVector::ZeroVector;
	FVector PreviousRagdollCenterOfMass = FVector::ZeroVector;
	FVector ResolvedRagdollFacingAxis = FVector(0,1,0);
	bool bHasPreviousRagdollCenterOfMass = false;
	bool bPhysicsControlInitialized = false;
	bool bFreezeRagdollCapsuleRotation = false;
	FName AppliedPhysicsProfile = NAME_None;
	FRotator FrozenRagdollCapsuleRotation = FRotator::ZeroRotator;
	FTransform CachedRagdollMoverTransform = FTransform::Identity;
	FName OriginalMeshCollisionProfile = NAME_None;
	ECollisionEnabled::Type OriginalMeshCollisionEnabled = ECollisionEnabled::NoCollision;
	FName OriginalCapsuleCollisionProfile = NAME_None;
	ECollisionEnabled::Type OriginalCapsuleCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	FTransform OriginalMeshRelativeTransform = FTransform::Identity;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type> RagdollAttachmentCollision;
	FTimerHandle RagdollRotationFreezeTimer;
	UPROPERTY(Transient)
	TObjectPtr<UPlayMoverMontageCallbackProxy> GetUpPlaybackProxy = nullptr;

	double LastDirectionalSendTime = -1.0;
};
