#pragma once

#include "CoreMinimal.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VehicleDataLibrary.generated.h"

/** 无世界上下文的车辆数据工厂。每次执行创建新 ID，因此创建节点刻意不声明 BlueprintPure。 */
UCLASS()
class SILVERCHOIR_API UVehicleDataLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 创建一辆新车；允许未配置图片/实体类，数值会转为非负有限值，耐久/燃料初始化为上限。 */
    UFUNCTION(BlueprintCallable, Category="车辆|数据创建", meta=(DisplayName="根据车辆模板创建车辆数据"))
    static FVehicleData CreateVehicleDataFromTemplate(const FVehicleTemplate& VehicleTemplate, FName TileId = NAME_None);

    /** 按同一模板创建 Count 辆独立车辆。Count=0 成功返回空数组；负数失败且清空输出。 */
    UFUNCTION(BlueprintCallable, Category="车辆|数据创建", meta=(DisplayName="根据车辆模板批量创建车辆数据"))
    static bool CreateVehicleDataBatch(const FVehicleTemplate& VehicleTemplate, int32 Count,
        TArray<FVehicleData>& OutData, FText& OutError, FName TileId = NAME_None);

    /**
     * 按显式行名顺序创建车辆，重复行名会创建不同 ID 的车辆；空列表不表示读取全表。
     * 表为空、行结构不匹配、行名为空或缺失时，整个操作失败并清空输出，不返回部分结果。
     */
    UFUNCTION(BlueprintCallable, Category="车辆|数据创建", meta=(DisplayName="根据车辆模板表行创建车辆数据数组"))
    static bool CreateVehicleDataFromTable(UDataTable* Table, const TArray<FName>& RowNames,
        TArray<FVehicleData>& OutData, FText& OutError, FName TileId = NAME_None);
};
