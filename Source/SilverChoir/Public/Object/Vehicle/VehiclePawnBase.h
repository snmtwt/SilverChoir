#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VehiclePawnBase.generated.h"

class USceneComponent;

/**
 * 车辆实体基类，仅提供根组件与车辆 ID 关联。
 * 不复制 FVehicleData；模型、碰撞、实际车辆驱动由子蓝图/后续实现提供。
 * 不使用单位的角色 Mover，也不自动创建库存、驾驶或小队成员系统。
 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="车辆 Pawn 基类"))
class SILVERCHOIR_API AVehiclePawnBase : public APawn
{
    GENERATED_BODY()

public:
    AVehiclePawnBase();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="车辆|组件", meta=(DisplayName="车辆根组件"))
    TObjectPtr<USceneComponent> SceneRoot;

    /** 只关联有效 ID，不查询/复制车辆数据。传入无效 ID 返回 false 并保留已有关联。 */
    UFUNCTION(BlueprintCallable, Category="车辆|数据", meta=(DisplayName="初始化车辆ID"))
    bool InitializeVehicleId(FGuid InVehicleId);

    /** 尚未初始化时返回无效 GUID；调用方通过 ID 访问其持有的车辆记录。 */
    UFUNCTION(BlueprintPure, Category="车辆|数据", meta=(DisplayName="获取车辆ID"))
    FGuid GetVehicleId() const { return VehicleId; }

private:
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="车辆|数据", meta=(DisplayName="车辆唯一ID", AllowPrivateAccess="true"))
    FGuid VehicleId;
};
