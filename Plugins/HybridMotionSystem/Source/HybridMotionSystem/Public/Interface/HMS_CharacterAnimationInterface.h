#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HMS_MovementStruct.h"
#include "HMS_CharacterAnimationInterface.generated.h"

UINTERFACE(BlueprintType)
class HYBRIDMOTIONSYSTEM_API UHMS_CharacterAnimationInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 角色向动画系统提供高层语义动画状态的接口。
 *
 * 注意：
 * 这个接口不要求返回所有基础运动数据。
 * 基础数据（Transform / Velocity / Rotation 等）建议由 AnimInstance 自己补齐。
 */
class HYBRIDMOTIONSYSTEM_API IHMS_CharacterAnimationInterface
{
	GENERATED_BODY()

public:
	/**
	 * 获取角色提供给动画系统的高层语义属性。
	 *
	 * 返回值：
	 * - true: 成功
	 * - false: 失败，AnimInstance 应保留默认值或跳过
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "HMS|Animation")
	bool GetAnimationProperties(FHMS_AnimationProperties& OutProperties) const;
};