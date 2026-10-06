#pragma once

#include "CoreMinimal.h"
#include "StrategicMovementStructs.generated.h"

/**
 * 单位与车辆共用的战略瓦片移动基础参数。默认值仅为占位，在模板中配置实际平衡数值。
 * 速度单位为标准瓦片/游戏小时，消耗为每标准瓦片的资源点数；不受地图 Actor 缩放影响。
 * 一次普通相邻瓦片移动按一个标准瓦片计算，长距离公路连接的长度由行军业务定义。
 * GridStrategyMapSystem 的寻路 Cost 只用于选路，不可直接当作游戏时间或移动距离。
 * 后续行军结算读取瓦片的耗时/体力/燃料倍率：耗时 = 距离 / 速度 * 耗时倍率，
 * 消耗 = 距离 * 每瓦片消耗 * 消耗倍率；SpeedTilesPerHour 为零表示不能行军，禁止除零。
 * 这里只保存参数，不执行路径、扣资源、推进游戏时钟或改变 RuntimeData.TileId。
 * 单位数据仍经唯一 FUnitData 共享引用读取，修改使用 Modify/NotifyDataChanged 通知。
 */
USTRUCT(BlueprintType, meta=(DisplayName="战略移动数据"))
struct SILVERCHOIR_API FStrategicMovementData
{
    GENERATED_BODY()

    /** 基础战略行进速度；人员步行限制还须检查 RuntimeData.bCannotWalkStrategically。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="战略移动", meta=(DisplayName="战略移动速度（瓦片/游戏小时）", ClampMin="0.0"))
    float SpeedTilesPerHour = 1.f;

    /** 消耗单位与 FUnitRuntimeData.CurrentStamina 一致；无需消耗体力时配置为零。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="战略移动", meta=(DisplayName="每瓦片体力消耗", ClampMin="0.0"))
    float StaminaCostPerTile = 0.f;

    /** 消耗单位与 FVehicleRuntimeData.CurrentFuel 一致；人员或无燃料消耗载具配置为零。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="战略移动", meta=(DisplayName="每瓦片燃料消耗", ClampMin="0.0"))
    float FuelCostPerTile = 0.f;

    /** ClampMin 不约束 C++ 或导入数据；创建实例时清洗副本，保留模板原值。 */
    FStrategicMovementData GetSanitized() const
    {
        const auto NonNegative = [](float Value) { return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f; };
        FStrategicMovementData Result = *this;
        Result.SpeedTilesPerHour = NonNegative(SpeedTilesPerHour);
        Result.StaminaCostPerTile = NonNegative(StaminaCostPerTile);
        Result.FuelCostPerTile = NonNegative(FuelCostPerTile);
        return Result;
    }
};
