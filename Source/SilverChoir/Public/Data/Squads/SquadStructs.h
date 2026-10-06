#pragma once

#include "CoreMinimal.h"
#include "Data/Vehicles/VehicleStructs.h"

#include "SquadStructs.generated.h"

class UTexture2D;

/** 独立小队名称表行。按排序值、行名依次选取尚未使用的非空名称；不保存小队实例或预占状态。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FSquadNameTemplate : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队名称", meta=(DisplayName="排序"))
    int32 SortOrder = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队名称", meta=(DisplayName="小队名称"))
    FText SquadName;
};

UENUM(BlueprintType)
enum class ESquadIconSource : uint8
{
    Preset UMETA(DisplayName="预设队徽"),
    CaptainPortrait UMETA(DisplayName="队长头像")
};

/**
 * 战略小队数据。权威记录只由 UPlayerSquadManagerBase 保存。
 * 成员仅保存 UnitId，不保存 FUnitData 副本；使用 GetSquadUnitsShared 获取玩家单位管理类的原始智能指针。
 * 单位归属反向索引由小队管理类统一维护，不在 FUnitData 中重复保存 SquadId。
 * 返回蓝图的本结构体是查询/存档快照，也可作为界面编辑草稿；修改副本不会更改权威记录。
 * 草稿通过 CommitSquadDraft 校验并统一提交（新建草稿无 SquadId，保存成功才分配）。恢复存档先加载单位、车辆再加载小队。
 * TileId 为战略位置，战术场景中的 Pawn 位移不改变此值。
 * 非空小队始终有一名成员担任队长；队长离队时按加入顺序递补。队长头像实时读取单位权威数据。
 * 未分配车辆时最多四人；分配车辆后载员上限包含驾驶人员。车辆权威记录同样由玩家单位管理类唯一持有。
 * AssignedVehicle 保留为界面草稿/存档兼容快照，VehicleId 是关联键；提交时重新查询权威记录，忽略快照内的车辆修改。
 * 车辆归属由小队管理类反向索引维护；GetSquadVehicleShared 返回原始车辆智能指针。修改车辆须通过其 Modify/NotifyDataChanged。
 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FSquadData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队", meta=(DisplayName="小队唯一ID"))
    FGuid SquadId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队", meta=(DisplayName="小队名称"))
    FText SquadName;

    /** 预设队徽，也是队长不存在或没有头像时的回退图标；不缓存队长头像。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队", meta=(DisplayName="预设小队图标"))
    TObjectPtr<UTexture2D> SquadIcon = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队", meta=(DisplayName="小队图标来源"))
    ESquadIconSource IconSource = ESquadIconSource::Preset;

    /** 有效 ID 必须同时出现在 MemberUnitIds 中；空队使用无效 ID。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队|成员", meta=(DisplayName="队长单位ID"))
    FGuid CaptainUnitId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队|战略地图", meta=(DisplayName="当前所在瓦片ID"))
    FName TileId = NAME_None;

    /** 兼容快照，不是第二份权威车辆。VehicleId 无效表示未分配；有效 ID 必须已在玩家单位管理类中，同瓦片且独占。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队|车辆", meta=(DisplayName="分配车辆"))
    FVehicleData AssignedVehicle;

    /** 保持加入顺序。禁止外部直接增删，用 AddUnitToSquad / RemoveUnitFromSquad 维护双向索引。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="小队|成员", meta=(DisplayName="成员单位ID"))
    TArray<FGuid> MemberUnitIds;

    bool HasAssignedVehicle() const { return AssignedVehicle.VehicleId.IsValid(); }
    int32 GetMaxMemberCount() const { return HasAssignedVehicle() ? FMath::Max(0, AssignedVehicle.Attributes.PassengerCapacity) : 4; }

    /** 只裁剪本快照中的 ID；优先保留队长，其余保留加入顺序，从末尾移出并返回移出 ID。容量为零时清空队长。 */
    TArray<FGuid> TrimMembersToCapacity();

    /** 完整比较编辑快照，包括车辆所有反射字段，供管理类和 UI 拒绝过期草稿。 */
    bool MatchesSnapshot(const FSquadData& Other) const;
};
