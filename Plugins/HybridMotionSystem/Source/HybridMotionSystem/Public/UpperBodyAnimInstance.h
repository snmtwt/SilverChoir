// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Chooser.h"
#include "GameplayTagContainer.h"
#include "HMS_MovementStruct.h"
#include "UpperBodyAnimInstance.generated.h"

/**
 * 上半身动画叠加层
 * 在人物动画蓝图中根据动画类型叠加上半身动画
 * 根据是否移动、移动速度、移动方向查询出对应的动画
 * 
 * 动画需要的参数
 * 是否瞄准
 * 
 * 
 * 未瞄准时，查询出未瞄准的上半身动画（左右手肩膀）
 * 
 * 根据曲线参数叠加动画
 * 瞄准时要通过IK控制器进行修正
 * 
 * 静止时腿部也要保持进行混合 因此需要是否移动参数
 * 需要有瞄准和非瞄准状态，因此需要 EHMS_RotationMode
 * 还需要一个瞄准偏移
 * 需要一个GameplayTag来区分姿势
 */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UUpperBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlayer", DisplayName = "动作查询列表")
	UChooserTable* ChooserTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlayer")
	FGameplayTagContainer AnimType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlayer")
	FGameplayTagContainer Stance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlayer", DisplayName = "是否移动")
	bool IsMoving;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlayer", DisplayName = "旋转模式(是否在瞄准)")
	EHMS_RotationMode RotationMode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlayer")
	TObjectPtr<UAnimationAsset> CurrentOverlayAnim = nullptr;

	// 更新OverlayerAnim
	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe), Category = "HMS|StateMachine")
	void UpdateOverlayerAnim();
};
