#pragma once

#include "CoreMinimal.h"
#include "Data/Common/StrategicMovementStructs.h"
#include "Engine/DataTable.h"
#include "VehicleStructs.generated.h"

class AVehiclePawnBase;
class UTexture2D;

DECLARE_MULTICAST_DELEGATE_OneParam(FVehicleDataChanged, FGuid);
DECLARE_DELEGATE_RetVal(bool, FVehicleDataCanModify);
DECLARE_DELEGATE_RetVal_OneParam(bool, FVehicleDataBeginNotify, FGuid);
DECLARE_DELEGATE_OneParam(FVehicleDataEndNotify, bool);

/** 车辆档案。资源引用由模板复制到实例，预览图仅用于界面展示。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆档案", meta=(DisplayName="车辆名称"))
    FText VehicleName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆档案", meta=(DisplayName="车辆描述", MultiLine="true"))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆档案", meta=(DisplayName="车辆预览图"))
    TObjectPtr<UTexture2D> PreviewImage = nullptr;
};

/** 车辆基础属性；默认数值为占位值，实际平衡数据在模板中配置。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleAttributes
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆属性", meta=(DisplayName="最大耐久", ClampMin="0.0"))
    float MaxDurability = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆属性", meta=(DisplayName="最大燃料", ClampMin="0.0"))
    float MaxFuel = 100.f;

    /** 总座位数包含驾驶席；选为小队车辆时直接作为人数上限，不与默认四人相加。0 表示不能载员。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆属性", meta=(DisplayName="座位数（含驾驶席）", ClampMin="0"))
    int32 PassengerCapacity = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆属性", meta=(DisplayName="载货重量上限", ClampMin="0.0", Units="kg"))
    float MaxCargoWeight = 100.f;

    /** ClampMin 仅约束编辑器输入；创建实例时清洗 C++ / 导入数据，不修改原模板。 */
    FVehicleAttributes GetSanitized() const
    {
        const auto NonNegative = [](float Value) { return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f; };
        FVehicleAttributes Result = *this;
        Result.MaxDurability = NonNegative(MaxDurability);
        Result.MaxFuel = NonNegative(MaxFuel);
        Result.PassengerCapacity = FMath::Max(0, PassengerCapacity);
        Result.MaxCargoWeight = NonNegative(MaxCargoWeight);
        return Result;
    }
};

/** 车辆实体配置；尚未制作实体时允许为空，实际生成 Pawn 的业务须检查类是否可生成。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleEntityData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆实体", meta=(DisplayName="车辆实体类"))
    TSubclassOf<AVehiclePawnBase> VehiclePawnClass;
};

/** 车辆运行时状态；TileId 是战略位置，战术地图上的 Pawn 位移不会自动改变此值。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleRuntimeData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆运行时数据|战略地图", meta=(DisplayName="当前所在瓦片ID"))
    FName TileId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆运行时数据", meta=(DisplayName="当前耐久", ClampMin="0.0"))
    float CurrentDurability = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆运行时数据", meta=(DisplayName="当前燃料", ClampMin="0.0"))
    float CurrentFuel = 0.f;
};

/** 车辆模板表行，不包含实例 ID 或运行时状态；同一行可以创建多辆独立车辆。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleTemplate : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆模板", meta=(DisplayName="车辆档案"))
    FVehicleProfile Profile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆模板", meta=(DisplayName="车辆属性"))
    FVehicleAttributes Attributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆模板", meta=(DisplayName="车辆实体配置"))
    FVehicleEntityData EntityData;

    /** 战略瓦片之间的行军速度与基础消耗；不用于地图内车辆 Pawn 驾驶。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆模板", meta=(DisplayName="战略移动参数"))
    FStrategicMovementData StrategicMovementData;
};

/**
 * 单辆车辆的数据记录。默认构造不生成 ID，只有工厂创建新车辆时分配 GUID。
 * 工厂复制模板配置、清洗数值、填满耐久及燃料，并设置战略瓦片位置；不修改模板表。
 * SourceTemplateRow 仅记录来源，不在模板修改后自动同步已有车辆。
 * 权威记录只在 UPlayerUnitManagerBase 的 VehicleDataStore 中保存一份，C++ 消费者获取同一 TSharedPtr。
 * 修改使用 Modify/NotifyDataChanged；VehicleId 不可修改，消费者销毁/换绑时移除事件订阅。
 * 同 ID 加载原地更新，保留指针和订阅；复制/蓝图返回值仅为快照，不复制事件，不自动写回。
 * 共享记录中的 UObject 资源由管理类分配的 GC 桥接追踪，外部共享引用可延长资源生命周期。
 * 小队只按 VehicleId 关联权威车辆，AssignedVehicle 是兼容已有蓝图/存档的展示及草稿快照。
 * 所属小队由 UPlayerSquadManagerBase 的反向索引查询，不在此重复保存 SquadId。
 * 绑定车辆的战略移动应使用 SetSquadTileId；直接移走车辆会解除原小队的车辆分配。
 * 实体 AVehiclePawnBase 只关联 VehicleId，不持有第二份可修改副本。具体驾驶和座位占用尚未实现。
 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FVehicleData
{
    GENERATED_BODY()

    FVehicleData() = default;
    FVehicleData(const FVehicleData& Other) { *this = Other; }
    FVehicleData& operator=(const FVehicleData& Other)
    {
        if (this != &Other)
        {
            // 新增业务字段时同步维护；事件订阅与通知状态留在目标实例上。
            VehicleId = Other.VehicleId;
            SourceTemplateRow = Other.SourceTemplateRow;
            Profile = Other.Profile;
            Attributes = Other.Attributes;
            EntityData = Other.EntityData;
            StrategicMovementData = Other.StrategicMovementData;
            RuntimeData = Other.RuntimeData;
        }
        return *this;
    }
    FVehicleDataChanged OnDataChanged;
    bool IsNotifyingDataChanged() const { return bNotifying; }
    void NotifyDataChanged()
    {
        check(IsInGameThread());
        if (bNotifying) return;
        TGuardValue<bool> Guard(bNotifying, true);
        // 管理类先修复关联并锁住写入，不能依赖 multicast 的回调注册顺序。
        const bool bPreviousUpdating = BeginNotify.IsBound() ? BeginNotify.Execute(VehicleId) : false;
        OnDataChanged.Broadcast(VehicleId);
        if (EndNotify.IsBound()) EndNotify.Execute(bPreviousUpdating);
    }
    template<typename T> void Modify(T&& Edit)
    {
        check(IsInGameThread());
        if (bNotifying || (CanModify.IsBound() && !CanModify.Execute())) return;
        const FGuid OriginalId = VehicleId;
        Edit(*this);
        VehicleId = OriginalId;
        NotifyDataChanged();
    }
private:
    friend class UPlayerUnitManagerBase;
    // 不复制这些所有者回调；本记录上的消费者和所有者关系始终保持不变。
    FVehicleDataCanModify CanModify;
    FVehicleDataBeginNotify BeginNotify;
    FVehicleDataEndNotify EndNotify;
    bool bNotifying = false;
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="车辆数据", meta=(DisplayName="车辆唯一ID"))
    FGuid VehicleId;

    /** 表工厂填写来源行名；直接从结构体模板创建时为 None。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="车辆数据", meta=(DisplayName="来源模板行"))
    FName SourceTemplateRow = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆数据", meta=(DisplayName="车辆档案"))
    FVehicleProfile Profile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆数据", meta=(DisplayName="车辆属性"))
    FVehicleAttributes Attributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆数据", meta=(DisplayName="车辆实体配置"))
    FVehicleEntityData EntityData;

    /** 战略瓦片之间的移动配置；具体路线、移动模式和执行进度由出行流程管理。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆数据", meta=(DisplayName="战略移动参数"))
    FStrategicMovementData StrategicMovementData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="车辆数据", meta=(DisplayName="车辆运行时数据"))
    FVehicleRuntimeData RuntimeData;
};
