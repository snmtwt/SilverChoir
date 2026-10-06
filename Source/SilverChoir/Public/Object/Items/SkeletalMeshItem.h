#pragma once

#include "CoreMinimal.h"
#include "Object/Items/ItemBase.h"
#include "SkeletalMeshItem.generated.h"

class USkeletalMeshComponent;

/** 使用骨骼网格体显示的物品实体，可在蓝图中配置动画。 */
UCLASS(Blueprintable, meta=(DisplayName="骨骼网格体物品"))
class SILVERCHOIR_API ASkeletalMeshItem : public AItemBase
{
    GENERATED_BODY()

public:
    ASkeletalMeshItem();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="物品|组件")
    TObjectPtr<USkeletalMeshComponent> MeshComponent;
};
