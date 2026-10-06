#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/Squads/SquadStructs.h"
#include "Data/Units/UnitStructs.h"
#include "PlayerSquadManagerBase.generated.h"

class UPlayerUnitManagerBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerSquadChanged, FGuid, SquadId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPlayerUnitSquadChanged, FGuid, UnitId, FGuid, PreviousSquadId, FGuid, CurrentSquadId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPlayerVehicleSquadChanged, FGuid, VehicleId, FGuid, PreviousSquadId, FGuid, CurrentSquadId);

/**
 * 玩家小队业务层。只保存小队和成员 ID，单位数据始终由玩家单位管理类唯一持有。
 * 所有写入在游戏线程进行，先校验后提交；通知时两侧索引及单位位置已经更新。
 * 通知回调中仅查询，后续写入应延迟到下一帧，避免嵌套操作破坏本次通知的一致性。
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="玩家小队处理类基类"))
class SILVERCHOIR_API UPlayerSquadManagerBase : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UPlayerUnitManagerBase* InUnitManager);
    void Shutdown();
    virtual UWorld* GetWorld() const override;
    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintPure, Category="玩家小队", meta=(DisplayName="玩家小队处理类是否就绪"))
    bool IsReady() const;

    /** 只查询不预占：按配置表顺序返回未使用名称；用完后接续“第X小队”，缺少有效表时从“第1小队”开始。 */
    UFUNCTION(BlueprintPure, Category="玩家小队|名称", meta=(DisplayName="获取下一个默认小队名称"))
    FText GetNextSquadName() const;

    UFUNCTION(BlueprintCallable, Category="玩家小队", meta=(DisplayName="创建玩家小队"))
    bool CreateSquad(FText SquadName, UTexture2D* SquadIcon, FName TileId, FGuid& OutSquadId, FText& OutError);

    /** 原子保存会议室草稿；验证容量/车辆占用，新队仅成功时分配 ID，已有队拒绝过期快照。草稿不负责移动战略位置。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队", meta=(DisplayName="保存小队草稿"))
    bool CommitSquadDraft(const FSquadData& Draft, const FSquadData& OriginalSquad, bool bCreateNew, FGuid& OutSquadId, FText& OutError);

    /** 原子分配同瓦片车辆并按载员上限裁剪；优先保留队长，成功才返回被移出成员，不删除单位数据。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|车辆", meta=(DisplayName="设置小队车辆"))
    bool SetSquadVehicle(FGuid SquadId, const FVehicleData& Vehicle, TArray<FGuid>& OutRemoved, FText& OutError);

    /** 清除车辆并恢复四人上限；超出人数按相同规则移出。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|车辆", meta=(DisplayName="清除小队车辆"))
    bool ClearSquadVehicle(FGuid SquadId, TArray<FGuid>& OutRemoved, FText& OutError);

    /** 不存在的小队返回零；有效小队无车四人，有车取含驾驶人员的载员上限。 */
    UFUNCTION(BlueprintPure, Category="玩家小队|成员", meta=(DisplayName="获取小队人数上限"))
    int32 GetSquadMaxMemberCount(FGuid SquadId) const;

    UFUNCTION(BlueprintCallable, Category="玩家小队", meta=(DisplayName="设置小队名称和图标"))
    bool UpdateSquadInfo(FGuid SquadId, FText SquadName, UTexture2D* SquadIcon, FText& OutError);

    /** 仅允许将本小队成员提升为队长；不自动将其他小队的单位转入。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|成员", meta=(DisplayName="设置小队队长"))
    bool SetSquadCaptain(FGuid SquadId, FGuid UnitId, FText& OutError);

    UFUNCTION(BlueprintCallable, Category="玩家小队", meta=(DisplayName="设置小队图标来源"))
    bool SetSquadIconSource(FGuid SquadId, ESquadIconSource IconSource, FText& OutError);

    /** 实时读取队长的共享单位数据；没有有效头像时使用预设小队图标。 */
    UFUNCTION(BlueprintPure, Category="玩家小队", meta=(DisplayName="获取小队显示图标"))
    UTexture2D* GetSquadIcon(FGuid SquadId) const;

    /** 解散小队并清除所有成员归属，不删除单位数据或单位实体。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队", meta=(DisplayName="解散玩家小队"))
    bool RemoveSquad(FGuid SquadId, FText& OutError);

    /** 必须与目标小队处于同一非空瓦片且目标未满员；全部校验成功后才从原队转入新队。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|成员", meta=(DisplayName="人员加入小队"))
    bool AddUnitToSquad(FGuid UnitId, FGuid SquadId, FText& OutError);

    UFUNCTION(BlueprintCallable, Category="玩家小队|成员", meta=(DisplayName="人员移出小队"))
    bool RemoveUnitFromSquad(FGuid UnitId, FText& OutError);

    /** 提交战略位置并同步所有成员及已分配车辆，未实现寻路、行程耗时、步行资格检查或 Pawn 位移。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|战略地图", meta=(DisplayName="设置小队当前瓦片位置"))
    bool SetSquadTileId(FGuid SquadId, FName TileId, FText& OutError);

    /** 全量恢复；先完整验证原输入，拒绝无效/重复成员、车辆或位置，再按容量裁剪历史超员并重建索引。 */
    UFUNCTION(BlueprintCallable, Category="玩家小队|数据", meta=(DisplayName="加载玩家小队数据"))
    bool LoadSquadData(const TArray<FSquadData>& Data, FText& OutError);

    UFUNCTION(BlueprintPure, Category="玩家小队|数据", meta=(DisplayName="根据ID获取小队"))
    bool GetSquad(FGuid SquadId, FSquadData& OutSquad) const;

    /** O(1) 查询单位归属；没有所属小队时返回 false 和空结构体。 */
    UFUNCTION(BlueprintPure, Category="玩家小队|成员", meta=(DisplayName="根据单位ID获取所属小队"))
    bool GetUnitSquad(FGuid UnitId, FSquadData& OutSquad) const;

    /** O(1) 查询正式车辆归属；未保存的会议室草稿不会占用车辆。 */
    UFUNCTION(BlueprintPure, Category="玩家小队|车辆", meta=(DisplayName="根据车辆ID获取所属小队"))
    bool GetVehicleSquad(FGuid VehicleId, FSquadData& OutSquad) const;

    /** 返回玩家单位管理类保存的原始车辆记录，不创建另一份可修改数据。 */
    TSharedPtr<FVehicleData> GetSquadVehicleShared(FGuid SquadId) const;

    UFUNCTION(BlueprintPure, Category="玩家小队|数据", meta=(DisplayName="获取玩家小队ID列表"))
    TArray<FGuid> GetSquadIds() const;

    UFUNCTION(BlueprintPure, Category="玩家小队|数据", meta=(DisplayName="获取玩家小队数据快照"))
    TArray<FSquadData> GetAllSquads() const;

    /** C++ 获取原始共享数据，顺序与 MemberUnitIds 一致；本类不创建/缓存第二份单位数据。 */
    TArray<TSharedPtr<FUnitData>> GetSquadUnitsShared(FGuid SquadId) const;

    /** 创建/修改/解散均通知；解散后 GetSquad 返回 false。 */
    UPROPERTY(BlueprintAssignable, Category="玩家小队|事件")
    FPlayerSquadChanged OnSquadChanged;

    /** 无归属使用无效 Guid；更换归属也会通知单位的 OnDataChanged，供人员 UI 刷新。 */
    UPROPERTY(BlueprintAssignable, Category="玩家小队|事件")
    FPlayerUnitSquadChanged OnUnitSquadChanged;

    UPROPERTY(BlueprintAssignable, Category="玩家小队|事件")
    FPlayerVehicleSquadChanged OnVehicleSquadChanged;

private:
    UPROPERTY(Transient) TObjectPtr<UPlayerUnitManagerBase> UnitManager;
    UPROPERTY(Transient) TMap<FGuid, FSquadData> Squads;
    TMap<FGuid, FGuid> UnitSquadIndex;
    TMap<FGuid, FGuid> VehicleSquadIndex;
    bool bChangingSquads = false;

    bool CanWrite(FText& OutError) const;
    bool ResolveUnitsForWrite(const TArray<FGuid>& Ids, TArray<TSharedPtr<FUnitData>>& OutUnits, FText& OutError) const;
    bool ResolveAssignedVehicle(FSquadData& Squad, FText& OutError) const;
    void HandleVehicleDataChanged(const TArray<FGuid>& VehicleIds);

    UFUNCTION()
    void HandleUnitDataChanged(FGuid UnitId);
};
