#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/Units/UnitStructs.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "PlayerUnitLibrary.generated.h"
class UPlayerUnitSubsystem;
class UPlayerUnitManagerBase;
class AActor;
class AUnitDisplayStand;
class USIS_UnitInventoryComponent;

UCLASS(meta=(DisplayName="玩家单位蓝图函数库"))
class SILVERCHOIR_API UPlayerUnitLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    static TSharedPtr<FVehicleData> GetVehicleDataShared(const UObject* WorldContextObject, FGuid VehicleID);
    /** 显式初始化车辆：整表每行创建一辆并追加，全部设置到 TileId；重复执行会生成新的车辆。Table 为空使用项目配置。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="从模板表加载玩家车辆"))
    static bool LoadPlayerVehiclesFromTemplateTable(const UObject* WorldContextObject, UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError);
    /** 恢复存档或接收工厂输出：按 ID 合并，保持已有共享地址；不在此分配小队。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="加载玩家车辆数据数组"))
    static bool LoadPlayerVehicleData(const UObject* WorldContextObject, const TArray<FVehicleData>& Data, FText& OutError);
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="根据ID获取车辆数据快照"))
    static bool GetPlayerVehicleDataSnapshot(const UObject* WorldContextObject, FGuid VehicleID, FVehicleData& OutData);
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家车辆ID列表"))
    static TArray<FGuid> GetPlayerVehicleDataIDs(const UObject* WorldContextObject);
    /** 精确匹配当前位置；None 返回空列表，不显示其他瓦片车辆。 */
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="获取当前瓦片车辆数据"))
    static TArray<FVehicleData> GetPlayerVehicleDataAtTile(const UObject* WorldContextObject, FName TileId);
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="更新玩家车辆数据"))
    static bool UpdatePlayerVehicleData(const UObject* WorldContextObject, FGuid VehicleID, const FVehicleData& Data);
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(WorldContext="WorldContextObject", DisplayName="移除玩家车辆数据"))
    static bool RemovePlayerVehicleData(const UObject* WorldContextObject, FGuid VehicleID, FText& OutError);
    /** 展示台 BeginPlay 后可用；未注册或已卸载时返回空。 */
    UFUNCTION(BlueprintPure, Category="玩家单位|展示", meta=(WorldContext="WorldContextObject", DisplayName="获取单位展示台"))
    static AUnitDisplayStand* GetUnitDisplayStand(const UObject* WorldContextObject);
    static TSharedPtr<FUnitData> GetUnitDataShared(const UObject* WorldContextObject, FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(WorldContext="WorldContextObject", DisplayName="根据ID获取单位数据引用"))
    static UUnitDataReference* GetUnitDataReference(const UObject* WorldContextObject, FGuid UnitId);
    /** 仅创建数据，不生成 Actor；每次调用生成新的唯一 ID。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="根据模板创建单位数据"))
    static FUnitData CreateUnitDataFromTemplate(const FUnitTemplate& UnitTemplate);
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="根据模板批量创建单位数据"))
    static TArray<FUnitData> CreateUnitDataBatch(const FUnitTemplate& UnitTemplate, int32 Count);
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="根据模板数组创建单位数据"))
    static TArray<FUnitData> CreateUnitDataFromTemplates(const TArray<FUnitTemplate>& Templates);
    /** Null table uses the configured unit template table. Missing rows fail without partial output. */
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="根据模板表行创建单位数据数组"))
    static bool CreateUnitDataFromTable(UDataTable* Table, const TArray<FName>& RowNames, TArray<FUnitData>& OutData, FText& OutError, FName TileId = NAME_None);
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(WorldContext="WorldContextObject", DisplayName="加载玩家单位数据数组"))
    static bool LoadPlayerUnitData(const UObject* WorldContextObject, const TArray<FUnitData>& Data, FText& OutError);
    UFUNCTION(BlueprintPure, Category="玩家单位|数据", meta=(WorldContext="WorldContextObject", DisplayName="获取单位数据快照"))
    static bool GetPlayerUnitDataSnapshot(const UObject* WorldContextObject, FGuid UnitID, FUnitData& OutData);
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(WorldContext="WorldContextObject", DisplayName="更新玩家单位数据"))
    static bool UpdatePlayerUnitData(const UObject* WorldContextObject, FGuid UnitID, const FUnitData& Data);
    /** 导出装备及其背包/附件/技能物品到权威 UnitData.InventoryItems，并通知订阅者。
     * 空库存会清空旧记录；保留物品 ID 和槽位关系；这里只更新单位数据，不写磁盘存档。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|装备", meta=(WorldContext="WorldContextObject", DisplayName="保存装备数据到UnitData"))
    static bool SaveEquipmentToUnitData(const UObject* WorldContextObject, FGuid UnitID,
        USIS_UnitInventoryComponent* InventoryComponent, FText& OutError);
    UFUNCTION(BlueprintPure, Category="玩家单位|数据", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家单位ID列表"))
    static TArray<FGuid> GetPlayerUnitDataIDs(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家单位子系统"))
    static UPlayerUnitSubsystem* GetPlayerUnitSubsystem(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家单位处理类"))
    static UPlayerUnitManagerBase* GetPlayerUnitManager(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="玩家单位系统是否就绪"))
    static bool IsPlayerUnitSystemReady(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="注册玩家单位"))
    static bool RegisterPlayerUnit(const UObject* WorldContextObject, FName UnitID, AActor* Unit);
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="移除玩家单位登记"))
    static bool UnregisterPlayerUnit(const UObject* WorldContextObject, FName UnitID);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="根据ID获取玩家单位"))
    static AActor* GetPlayerUnit(const UObject* WorldContextObject, FName UnitID);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="获取所有玩家单位"))
    static TArray<AActor*> GetPlayerUnits(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(WorldContext="WorldContextObject", DisplayName="清空玩家单位登记"))
    static void ClearPlayerUnits(const UObject* WorldContextObject);
};
