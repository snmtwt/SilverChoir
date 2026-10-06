#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlayerSquadSettings.generated.h"

class UPlayerSquadManagerBase;
class UTexture2D;
class UDataTable;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "玩家小队配置"))
class SILVERCHOIR_API UPlayerSquadSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPlayerSquadSettings();

    /** 留空使用原生处理类，也可以指定蓝图子类。 */
    UPROPERTY(Config, EditAnywhere, Category = "玩家小队", meta = (DisplayName = "玩家小队处理类", AllowAbstract = "false"))
    TSoftClassPtr<UPlayerSquadManagerBase> ManagerClass;

    /** 图标选择器使用的预设队徽，按此数组顺序显示；队长头像是独立的动态选项。 */
    UPROPERTY(Config, EditAnywhere, Category = "玩家小队|图标", meta = (DisplayName = "预设小队图标"))
    TArray<TSoftObjectPtr<UTexture2D>> PresetSquadIcons;

    /** 新建小队默认名称。按表中的 SortOrder 排序，跳过已使用名称；预览/取消不会消耗条目。 */
    UPROPERTY(Config, EditAnywhere, Category = "玩家小队|名称", meta = (DisplayName = "小队名称数据表", RequiredAssetDataTags="RowStructure=/Script/SilverChoir.SquadNameTemplate"))
    TSoftObjectPtr<UDataTable> SquadNameTable;
};
