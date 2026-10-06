#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Units/UnitStructs.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "PlayerUnitSubsystem.generated.h"
class UPlayerUnitManagerBase;
class AUnitDisplayStand;

/** 玩家单位协调层，持有人员与车辆数据处理类；Actor 登记独立于数据记录。 */
UCLASS(BlueprintType, NotBlueprintable, meta=(DisplayName="玩家单位管理子系统"))
class SILVERCHOIR_API UPlayerUnitSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    TSharedPtr<FVehicleData> GetVehicleDataShared(FGuid VehicleID) const;
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="加载玩家车辆数据数组"))
    bool LoadVehicleData(const TArray<FVehicleData>& Data, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="玩家单位|车辆", meta=(DisplayName="从模板表加载玩家车辆"))
    bool LoadVehicleTemplates(UDataTable* Table, FName TileId, TArray<FGuid>& OutVehicleIds, FText& OutError);
    bool RegisterDisplayStand(AUnitDisplayStand* Stand);
    void UnregisterDisplayStand(AUnitDisplayStand* Stand);
    AUnitDisplayStand* GetDisplayStand() const;
    TSharedPtr<FUnitData> GetUnitDataShared(FGuid UnitID) const;
    UFUNCTION(BlueprintCallable, Category="玩家单位|数据", meta=(DisplayName="加载玩家单位数据数组"))
    bool LoadUnitData(const TArray<FUnitData>& Data, FText& OutError);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(DisplayName="玩家单位系统是否就绪"))
    bool IsReady() const;
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(DisplayName="获取玩家单位处理类"))
    UPlayerUnitManagerBase* GetManager() const;
    UFUNCTION(BlueprintPure, Category="玩家单位", meta=(DisplayName="获取玩家单位系统初始化错误"))
    FText GetInitializationError() const { return InitializationError; }
private:
    UPROPERTY(Transient) TWeakObjectPtr<AUnitDisplayStand> DisplayStand;
    UPROPERTY(Transient) TObjectPtr<UPlayerUnitManagerBase> Manager;
    UPROPERTY(Transient) FText InitializationError;
};
