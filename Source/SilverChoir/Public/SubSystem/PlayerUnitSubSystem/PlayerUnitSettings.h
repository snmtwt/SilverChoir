#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlayerUnitSettings.generated.h"
class UPlayerUnitManagerBase;
class UDataTable;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="玩家单位配置"))
class SILVERCHOIR_API UPlayerUnitSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPlayerUnitSettings();
    /** 留空使用原生处理类；可指定蓝图子类。 */
    UPROPERTY(Config, EditAnywhere, Category="玩家单位", meta=(DisplayName="玩家单位处理类", AllowAbstract="false"))
    TSoftClassPtr<UPlayerUnitManagerBase> ManagerClass;
    UPROPERTY(Config, EditAnywhere, Category="玩家单位", meta=(DisplayName="单位模板数据表", RequiredAssetDataTags="RowStructure=/Script/SilverChoir.UnitTemplate"))
    TSoftObjectPtr<UDataTable> UnitTemplateTable;
    /** 只用于显式调用车辆模板加载节点，不在初始化时自动生成车辆。 */
    UPROPERTY(Config, EditAnywhere, Category="玩家车辆", meta=(DisplayName="车辆模板数据表", RequiredAssetDataTags="RowStructure=/Script/SilverChoir.VehicleTemplate"))
    TSoftObjectPtr<UDataTable> VehicleTemplateTable;
};
