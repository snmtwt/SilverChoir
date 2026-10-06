#pragma once

#include "CoreMinimal.h"
#include "Data/Squads/SquadStructs.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlayerSquadLibrary.generated.h"

class UPlayerSquadSubsystem;
class UPlayerSquadManagerBase;
class UTexture2D;
class UUnitDataReference;
struct FUnitData;

/** 小队节点入口。单位查询返回共享引用，不创建或保留 FUnitData 副本。 */
UCLASS(meta = (DisplayName = "玩家小队蓝图函数库"))
class SILVERCHOIR_API UPlayerSquadLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家小队子系统"))
    static UPlayerSquadSubsystem* GetPlayerSquadSubsystem(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家小队处理类"))
    static UPlayerSquadManagerBase* GetPlayerSquadManager(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (WorldContext = "WorldContextObject", DisplayName = "玩家小队系统是否就绪"))
    static bool IsPlayerSquadSystemReady(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "玩家小队|名称", meta = (WorldContext = "WorldContextObject", DisplayName = "获取下一个默认小队名称"))
    static FText GetNextSquadName(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "玩家小队|数据", meta = (WorldContext = "WorldContextObject", DisplayName = "创建玩家小队"))
    static bool CreateSquad(const UObject* WorldContextObject, FText SquadName, UTexture2D* SquadIcon, FName TileId, FGuid& OutSquadId, FText& OutError);

    /** 校验成功后一次性提交名称、图标、车辆、成员及队长；新小队保存前不占用 ID、车辆或成员。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|数据", meta = (WorldContext = "WorldContextObject", DisplayName = "保存小队草稿"))
    static bool CommitSquadDraft(const UObject* WorldContextObject, const FSquadData& Draft, const FSquadData& OriginalSquad, bool bCreateNew, FGuid& OutSquadId, FText& OutError);

    /** 即时分配已在玩家单位子系统中的同瓦片车辆；只读取传入的 ID，座位等取权威记录。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|车辆", meta = (WorldContext = "WorldContextObject", DisplayName = "设置小队车辆"))
    static bool SetSquadVehicle(const UObject* WorldContextObject, FGuid SquadId, const FVehicleData& Vehicle, TArray<FGuid>& OutRemoved, FText& OutError);

    /** 即时清除车辆并恢复四人上限，优先保留队长；不删除被移出单位的数据。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|车辆", meta = (WorldContext = "WorldContextObject", DisplayName = "清除小队车辆"))
    static bool ClearSquadVehicle(const UObject* WorldContextObject, FGuid SquadId, TArray<FGuid>& OutRemoved, FText& OutError);

    /** 不存在的小队返回零。 */
    UFUNCTION(BlueprintPure, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "获取小队人数上限"))
    static int32 GetSquadMaxMemberCount(const UObject* WorldContextObject, FGuid SquadId);

    UFUNCTION(BlueprintCallable, Category = "玩家小队|数据", meta = (WorldContext = "WorldContextObject", DisplayName = "设置小队名称和图标"))
    static bool UpdateSquadInfo(const UObject* WorldContextObject, FGuid SquadId, FText SquadName, UTexture2D* SquadIcon, FText& OutError);

    UFUNCTION(BlueprintCallable, Category = "玩家小队|成员", meta = (WorldContext = "WorldContextObject", DisplayName = "设置小队队长"))
    static bool SetSquadCaptain(const UObject* WorldContextObject, FGuid SquadId, FGuid UnitId, FText& OutError);

    UFUNCTION(BlueprintCallable, Category = "玩家小队|图标", meta = (WorldContext = "WorldContextObject", DisplayName = "设置小队图标来源"))
    static bool SetSquadIconSource(const UObject* WorldContextObject, FGuid SquadId, ESquadIconSource IconSource, FText& OutError);

    /** 使用队长头像时实时查询权威单位数据，没有头像时回退到预设图标。 */
    UFUNCTION(BlueprintPure, Category = "玩家小队|图标", meta = (WorldContext = "WorldContextObject", DisplayName = "获取小队显示图标"))
    static UTexture2D* GetSquadIcon(const UObject* WorldContextObject, FGuid SquadId);

    /** 按项目设置顺序加载队徽；无效路径跳过，队长头像选项由界面单独添加。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|图标", meta = (DisplayName = "获取预设小队图标"))
    static TArray<UTexture2D*> GetPresetSquadIcons();

    /** 删除小队并清除成员归属，单位核心数据仍保存在玩家单位子系统中。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|数据", meta = (WorldContext = "WorldContextObject", DisplayName = "解散玩家小队"))
    static bool RemoveSquad(const UObject* WorldContextObject, FGuid SquadId, FText& OutError);

    /** 先验证目标队未满员且与单位瓦片相同；成功时自动移出原队并加入新队。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|成员", meta = (WorldContext = "WorldContextObject", DisplayName = "人员加入小队"))
    static bool AddUnitToSquad(const UObject* WorldContextObject, FGuid UnitId, FGuid SquadId, FText& OutError);

    UFUNCTION(BlueprintCallable, Category = "玩家小队|成员", meta = (WorldContext = "WorldContextObject", DisplayName = "人员移出小队"))
    static bool RemoveUnitFromSquad(const UObject* WorldContextObject, FGuid UnitId, FText& OutError);

    /** 提交小队、全部成员及已分配车辆的战略瓦片位置；不负责寻路、Actor 移动或步行资格检查。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|战略位置", meta = (WorldContext = "WorldContextObject", DisplayName = "设置小队当前瓦片位置"))
    static bool SetSquadTileId(const UObject* WorldContextObject, FGuid SquadId, FName TileId, FText& OutError);

    /** 返回小队信息快照；成员仅有 ID，通过共享数据查询节点取得单位数据。 */
    UFUNCTION(BlueprintPure, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "根据ID获取小队"))
    static bool GetSquad(const UObject* WorldContextObject, FGuid SquadId, FSquadData& OutSquad);

    UFUNCTION(BlueprintPure, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "根据单位ID获取所属小队"))
    static bool GetUnitSquad(const UObject* WorldContextObject, FGuid UnitId, FSquadData& OutSquad);

    /** O(1) 查询车辆正式归属。未保存的会议室草稿不占用车辆。 */
    UFUNCTION(BlueprintPure, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "根据车辆ID获取所属小队"))
    static bool GetVehicleSquad(const UObject* WorldContextObject, FGuid VehicleId, FSquadData& OutSquad);

    /** C++ 直接获取玩家单位子系统中的权威车辆智能指针。 */
    static TSharedPtr<FVehicleData> GetSquadVehicleShared(const UObject* WorldContextObject, FGuid SquadId);

    UFUNCTION(BlueprintPure, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家小队ID列表"))
    static TArray<FGuid> GetSquadIds(const UObject* WorldContextObject);

    /** C++ 使用：数组中的每个指针直接指向玩家单位处理类持有的权威数据。 */
    static TArray<TSharedPtr<FUnitData>> GetSquadUnitsShared(const UObject* WorldContextObject, FGuid SquadId);

    /** 蓝图共享引用桥接。需要持续监听时请保存返回对象并绑定其数据修改事件。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|查询", meta = (WorldContext = "WorldContextObject", DisplayName = "获取小队单位数据引用"))
    static TArray<UUnitDataReference*> GetSquadUnitReferences(const UObject* WorldContextObject, FGuid SquadId);

    /** 先加载单位和车辆，再全量加载小队；车辆 ID 查询权威数据，完整验证后才裁剪历史超员。 */
    UFUNCTION(BlueprintCallable, Category = "玩家小队|存档", meta = (WorldContext = "WorldContextObject", DisplayName = "加载玩家小队数据"))
    static bool LoadSquadData(const UObject* WorldContextObject, const TArray<FSquadData>& Data, FText& OutError);

    /** 小队存档快照，只包含成员 ID，不包含单位核心数据的副本。 */
    UFUNCTION(BlueprintPure, Category = "玩家小队|存档", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家小队数据快照"))
    static TArray<FSquadData> GetAllSquads(const UObject* WorldContextObject);
};
