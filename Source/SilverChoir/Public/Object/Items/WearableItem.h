#pragma once

#include "CoreMinimal.h"
#include "Object/Items/StaticMeshItem.h"
#include "WearableItem.generated.h"

class USkeletalMesh;

/** 衣服、手套、鞋等跟随单位身体骨骼的穿戴物品。
 * MeshComponent 是物品展示用的静态网格；WearMesh 是穿到单位身上的骨骼网格。
 * 装备事件使用 ItemId 管理单位上的外观组件。
 * SIS 可能复用同一事件对象，因此不在物品对象上缓存穿戴者。蓝图重载事件时需调用父实现。 */
UCLASS(Blueprintable, meta=(DisplayName="穿戴物品"))
class SILVERCHOIR_API AWearableItem : public AStaticMeshItem
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物品|组件")
    USkeletalMesh* WearMesh = nullptr;


    virtual bool OnAddToEquipmentSlot_Implementation(const FSIS_EquipmentSlotEventData& EventData) override;
    virtual void OnRemoveFromEquipmentSlot_Implementation(
        const FSIS_EquipmentSlotEventData& EventData, bool& bHandledItemActorEvent) override;
};
