#pragma once

#include "CoreMinimal.h"
#include "Object/Items/ItemBase.h"
#include "StaticMeshItem.generated.h"

class UStaticMeshComponent;

/** 使用静态网格体显示的物品实体。 */
UCLASS(Blueprintable, meta=(DisplayName="静态网格体物品"))
class SILVERCHOIR_API AStaticMeshItem : public AItemBase
{
    GENERATED_BODY()

public:
    AStaticMeshItem();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="物品|组件")
    TObjectPtr<UStaticMeshComponent> MeshComponent;
};
