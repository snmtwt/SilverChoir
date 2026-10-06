#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/Units/UnitStructs.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "PlayerUnitManagerBase.generated.h"
class AActor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerUnitDataChanged, FGuid, UnitID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerVehicleDataChanged, FGuid, VehicleID);
DECLARE_MULTICAST_DELEGATE_OneParam(FPlayerVehicleDataReconcile, const TArray<FGuid>&);

/** 玩家单位/车辆权威数据业务层；Actor 登记独立于持久记录，不负责实体生成或磁盘存档。 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="玩家单位处理类基类"))
class SILVERCHOIR_API UPlayerUnitManagerBase : public UObject
{
    GENERATED_BODY()
public:
    /** 车辆与人员均由本处理类唯一持有；只在游戏线程读取/修改，快照不得作为权威副本。 */
    TSharedPtr<FVehicleData> GetVehicleDataShared(FGuid VehicleID) const;
    bool IsUpdatingVehicleData() const { return bUpdatingVehicleData; }
    /** 小队协调层在公开通知前修复车辆关联，不能在此阶段重入写入。 */
    FPlayerVehicleDataReconcile OnVehicleDataReconcile;
    UPROPERTY(BlueprintAssignable, Category="玩家单位|车辆") FPlayerVehicleDataChanged OnVehicleDataChanged;
    /** 按 ID 合并，已有记录原地更新，未包含的记录保留。整批验证失败不写入。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="加载玩家车辆数据数组"))
    bool LoadVehicleData(const TArray<FVehicleData>& Data, FText& OutError);
    /** 全表每行创建一辆新车并追加；重复执行会再次生成新车辆，不是恢复存档节点。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="从模板表加载玩家车辆"))
    bool LoadVehicleTemplates(UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError);
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(DisplayName="根据ID获取车辆数据快照"))
    bool GetVehicleDataSnapshot(FGuid VehicleID, FVehicleData& OutData) const;
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="更新玩家车辆数据"))
    bool UpdateVehicleData(FGuid VehicleID, const FVehicleData& Data);
    /** 删除权威记录并解除小队分配；外部旧共享引用不再代表玩家拥有的车辆。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="移除玩家车辆数据"))
    bool RemoveVehicleData(FGuid VehicleID, FText& OutError);
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(DisplayName="获取玩家车辆ID列表"))
    TArray<FGuid> GetVehicleDataIDs() const;
    /** 精确匹配战略瓦片；None 返回空，不表示全部。 */
    UFUNCTION(BlueprintPure, Category="玩家单位|车辆", meta=(DisplayName="获取当前瓦片车辆数据"))
    TArray<FVehicleData> GetVehicleDataAtTile(FName TileId) const;
    /** Shared canonical record. Modify through FUnitData::Modify; never change UnitId. Game thread only. */
    TSharedPtr<FUnitData> GetUnitDataShared(FGuid UnitID) const;
    /** 供关联数据管理类拒绝单位批量加载/通知中的重入写入。 */
    bool IsUpdatingUnitData() const { return bUpdatingUnitData; }
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="加载玩家单位数据数组"))
    bool LoadUnitData(const TArray<FUnitData>& Data, FText& OutError);
    /** Snapshot only. Changes must be submitted through UpdateUnitData. */
    UFUNCTION(BlueprintPure, Category="玩家单位|数据", meta=(DisplayName="获取单位数据快照"))
    bool GetUnitDataSnapshot(FGuid UnitID, FUnitData& OutData) const;
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="更新玩家单位数据"))
    bool UpdateUnitData(FGuid UnitID, const FUnitData& Data);
    UFUNCTION(BlueprintPure, Category="玩家单位|数据", meta=(DisplayName="获取玩家单位ID列表"))
    TArray<FGuid> GetUnitDataIDs() const;
    UPROPERTY(BlueprintAssignable, Category="玩家单位|数据") FPlayerUnitDataChanged OnUnitDataChanged;
    virtual UWorld* GetWorld() const override;
    virtual void BeginDestroy() override;
    /** ID 必须唯一且非空；仅接受当前世界中的有效 Actor。相同 ID/Actor 重复注册视为成功。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(DisplayName="注册玩家单位"))
    bool RegisterUnit(FName UnitID, AActor* Unit);
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(DisplayName="移除玩家单位登记"))
    bool UnregisterUnit(FName UnitID);
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(DisplayName="根据ID获取玩家单位"))
    AActor* GetUnit(FName UnitID) const;
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(DisplayName="获取所有玩家单位"))
    TArray<AActor*> GetUnits() const;
    UFUNCTION(BlueprintCallable, Category="玩家单位", meta=(DisplayName="清空玩家单位登记"))
    void ClearUnits();
private:
    // 小队事务需在提交两侧关联与发送通知期间锁住车辆写入。
    friend class UPlayerSquadManagerBase;
    bool CanModifyVehicleData() const { return !bUpdatingVehicleData && !bUpdatingUnitData; }
    bool BeginVehicleDataNotify(FGuid ID);
    void EndVehicleDataNotify(bool bPreviousUpdating) { bUpdatingVehicleData = bPreviousUpdating; }
    TMap<FGuid, TSharedPtr<FVehicleData>> VehicleDataStore;
    bool bUpdatingVehicleData = false;
    void ForwardUnitDataChanged(FGuid ID);
    TMap<FGuid,TSharedPtr<FUnitData>> UnitDataStore;
    bool bUpdatingUnitData = false;
    // GameInstance 生命周期长于地图；弱引用与世界检查避免返回旧地图 Actor。
    UPROPERTY(Transient) TMap<FName,TWeakObjectPtr<AActor>> Units;
    bool IsCurrentUnit(AActor* Unit) const;
};
