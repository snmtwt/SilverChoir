#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/SIS_ItemObjectInterface.h"
#include "ItemBase.generated.h"

class USIS_ItemInventoryComponent;

/** 物品实体的抽象基类，根组件及具体外观由派生类提供。 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="物品基类"))
class SILVERCHOIR_API AItemBase : public AActor, public ISIS_ItemObjectInterface
{
    GENERATED_BODY()

public:
    AItemBase();

    /** 物品库存及容器数据，由 SIS 管理；子蓝图可配置组件并重载 SIS 接口事件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="物品|组件")
    TObjectPtr<USIS_ItemInventoryComponent> ItemInventoryComponent;
};
