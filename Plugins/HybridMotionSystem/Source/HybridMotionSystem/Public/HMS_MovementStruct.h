#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Structs/HMS_MoverStructs.h"
#include "HMS_MovementStruct.generated.h"

class UAnimationAsset;
class UBlendProfile;

UENUM(BlueprintType)
enum class EHMS_MovementState : uint8
{
	Idle    UMETA(DisplayName = "Idle"),
	Moving  UMETA(DisplayName = "Moving")
};

UENUM(BlueprintType)
enum class EHMS_MovementDirection : uint8
{
	F  UMETA(DisplayName = "Forward"),
	B  UMETA(DisplayName = "Backward"),
	LL UMETA(DisplayName = "Left"),
	LR UMETA(DisplayName = "Left Rear"),
	RL UMETA(DisplayName = "Right"),
	RR UMETA(DisplayName = "Right Rear")
};


UENUM(BlueprintType)
enum class EHMS_MovementMode : uint8
{
	OnGround   UMETA(DisplayName = "On Ground"),
	InAir      UMETA(DisplayName = "In Air"),
	Sliding    UMETA(DisplayName = "Sliding"),
	Traversing UMETA(DisplayName = "Traversing"),
	Ragdoll    UMETA(DisplayName = "Ragdoll"),
	Flying     UMETA(DisplayName = "Flying")
};

UENUM(BlueprintType)
enum class EHMS_RagdollState : uint8
{
	Animated  UMETA(DisplayName = "正常动画"),
	Ragdoll   UMETA(DisplayName = "布娃娃"),
	GettingUp UMETA(DisplayName = "正在起身")
};

/** UE 5.8 Game Animation Sample 使用的受伤布娃娃姿态分类。枚举顺序必须与样例保持一致。 */
UENUM(BlueprintType)
enum class EHMS_RagdollInjuryState : uint8
{
	None      UMETA(DisplayName = "None"),
	Limp      UMETA(DisplayName = "Limp"),
	Stunned   UMETA(DisplayName = "Stunned"),
	HeadFace  UMETA(DisplayName = "Head Face"),
	HeadBack  UMETA(DisplayName = "Head Back"),
	BodyFront UMETA(DisplayName = "Body Front"),
	Groin     UMETA(DisplayName = "Groin")
};

UENUM(BlueprintType)
enum class EHMS_AccelerationSource : uint8
{
	PlayerInput UMETA(DisplayName = "Player Input"),
	NavMover    UMETA(DisplayName = "Nav Mover")
};

UENUM(BlueprintType)
enum class EHMS_TrajectoryQuality : uint8
{
	Full        UMETA(DisplayName = "完整（含碰撞）"),
	NoCollision UMETA(DisplayName = "预测（无碰撞）"),
	Lightweight UMETA(DisplayName = "轻量级")
};

/** 与 GASP RagdollProperties 对应的动画侧只读数据。 */
USTRUCT(BlueprintType)
struct FHMS_RagdollProperties
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "布娃娃状态"))
	EHMS_RagdollState State = EHMS_RagdollState::Animated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "脊柱速度"))
	FVector SpineVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "质心速度"))
	FVector CenterOfMassVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "布娃娃速度"))
	float Speed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "预计碰撞时间"))
	float TimeToImpact = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "碰撞方向"))
	FVector2D ImpactDirection = FVector2D::ZeroVector;

	/** 对应 5.8 样例 S_RagdollProperties.Ragdoll_InjuryState。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Ragdoll", meta = (DisplayName = "布娃娃受伤状态"))
	EHMS_RagdollInjuryState InjuryState = EHMS_RagdollInjuryState::None;

	/** 对应 5.8 样例 S_RagdollProperties.Ragdoll_RollAmount，供地面/翻滚姿态混合。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "布娃娃翻滚量"))
	float RollAmount = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "重力倍率"))
	float GravityMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "面朝上"))
	bool bFaceUp = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|Ragdoll", meta = (DisplayName = "接触地面"))
	bool bOnGround = false;
};

/**
 * 外部传给动画实例的必要语义输入。
 * 约定：
 * 1. 当前速度由 Mover 组件在 AnimInstance 内部获取
 * 2. 瞄准朝向由控制器朝向在 AnimInstance 内部获取
 * 3. MovementDirection 由 AnimInstance 内部瞬时计算
 */
USTRUCT(BlueprintType)
struct FHMS_AnimationProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Semantic")
	EHMS_MovementMode MovementMode = EHMS_MovementMode::OnGround;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Semantic")
	FGameplayTagContainer Stance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Semantic")
	EHMS_RotationMode RotationMode = EHMS_RotationMode::OrientToMovement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Semantic")
	EHMS_Gait Gait = EHMS_Gait::Walk;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Ground")
	FVector GroundNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Ground")
	FVector GroundLocation = FVector::ZeroVector;

	/** Mover 在移动平台/旋转基座上产生的本帧世界空间 Transform 增量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Ground", meta = (DisplayName = "基座移动增量"))
	FTransform BasedMovementDelta = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Ragdoll")
	FHMS_RagdollProperties RagdollProperties;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Input")
	EHMS_AccelerationSource AccelerationSource = EHMS_AccelerationSource::PlayerInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Input")
	FVector2D PlayerInputAcceleration = FVector2D::ZeroVector;

	/** AI/Mass may provide world-space intent directly, bypassing controller-relative WASD conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Input", meta = (DisplayName = "使用世界空间移动输入"))
	bool bUseWorldSpaceMovementInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Input", meta = (DisplayName = "世界空间移动输入"))
	FVector WorldSpaceMovementInput = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Direction", meta = (DisplayName = "使用外部朝向"))
	bool bUseExternalFacingDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Direction", meta = (DisplayName = "外部朝向"))
	FVector FacingDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Direction", meta = (DisplayName = "使用外部瞄准方向"))
	bool bUseExternalAimingDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Direction", meta = (DisplayName = "外部瞄准方向"))
	FVector AimingDirection = FVector::ForwardVector;

};

USTRUCT(BlueprintType)
struct FHMS_ChooserOutputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser")
	double StartTime = 0.0;

	/** Optional playback cap applied after Pose Match. -1 keeps the matched time; 0 plays from the beginning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser", meta = (ClampMin = "-1", Units = "s", DisplayName = "最晚播放起点"))
	double MaximumStartTime = -1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser")
	double BlendTime = 0.3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser")
	FName BlendProfile = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Chooser")
	TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct FHMS_BlendStackInputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	TObjectPtr<UAnimationAsset> Anim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	double StartTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	double BlendTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|BlendStack")
	TArray<FName> Tags;
};

/**
 * 兼容保留。
 * 如果后续确认完全没有地方引用这个 UObject 类型，可以删掉。
 */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UHMS_MovementStruct : public UObject
{
	GENERATED_BODY()
};
