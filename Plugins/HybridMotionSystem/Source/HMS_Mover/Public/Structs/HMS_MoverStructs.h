// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MoverTypes.h"
#include "HMS_MoverStructs.generated.h"


UENUM(BlueprintType)
enum class EHMS_RotationMode : uint8
{
	OrientToMovement UMETA(DisplayName = "Orient To Movement"),
	Strafe           UMETA(DisplayName = "Strafe"),
	Aim              UMETA(DisplayName = "Aim")
};

UENUM(BlueprintType)
enum class EHMS_Gait : uint8
{
	Walk   UMETA(DisplayName = "Walk"),
	Run    UMETA(DisplayName = "Run"),
	Sprint UMETA(DisplayName = "Sprint")
};

/** HMS 自定义移动模式名称。与 UE 5.8 样例蓝图中的模式名保持一致。 */
namespace HMSModeNames
{
	inline const FName Ragdoll(TEXT("Ragdoll"));
}

USTRUCT(BlueprintType)
struct FHMS_MoverInput : public FMoverDataStructBase
{

	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	bool bHasNavigationInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	FVector WorldMoveDirection = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	FVector RequestedVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	float RequestedSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	bool bOrientToMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	EHMS_RotationMode RotationMode = EHMS_RotationMode::OrientToMovement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	EHMS_Gait Gait = EHMS_Gait::Run;

	/** 作为输入命令参与网络预测，避免在模拟线程直接读取 Pawn/组件状态。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	bool bWantsToCrouch = false;

	// ===== Aim =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	bool bHasAimTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	FVector AimTargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	FVector AimDirection = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover")
	float AimTurnThresholdDegrees = 75.f;

	// ===== UE 5.8 样例：S_MoverCustomInputs_Ragdoll =====
	/** 当前帧是否携带布娃娃移动输入。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover|Ragdoll")
	bool bHasRagdollInput = false;

	/** 由物理骨骼计算出的目标胶囊世界变换。Ragdoll 模式只消费该值，不读取外部组件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover|Ragdoll")
	FTransform RagdollTransform = FTransform::Identity;

	/** 对应样例 S_MoverCustomInputs_Ragdoll.RollAmount。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mover|Ragdoll")
	float RagdollRollAmount = 0.0f;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FMoverDataStructBase* Clone() const override
	{
		return new FHMS_MoverInput(*this);
	}
};


/**
 * 
 */
UCLASS()
class HMS_MOVER_API UHMS_MoverStructs : public UObject
{
	GENERATED_BODY()
	
};
