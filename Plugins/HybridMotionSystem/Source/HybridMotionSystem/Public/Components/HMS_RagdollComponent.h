#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HMS_RagdollComponent.generated.h"

/**
 * 旧蓝图资产兼容占位类型。
 *
 * UE 资产会序列化曾经存在的原生默认子对象类型；立即删除 UClass 会让旧 Pawn
 * 在加载时报告缺失类。该类不含任何布娃娃状态或行为，也不再由任何 Pawn 创建。
 * 所有 UE 5.8 布娃娃逻辑唯一位于 UHMS_AnimationDataComponent。
 */
UCLASS(NotBlueprintable, NotPlaceable,
	meta=(DeprecatedNode, DeprecationMessage="请使用 HMS AnimationDataComponent 中的布娃娃功能"))
class HYBRIDMOTIONSYSTEM_API UHMS_RagdollComponent final : public UActorComponent
{
	GENERATED_BODY()
};
