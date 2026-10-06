#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/Common/StrategicMovementStructs.h"
#include "Structs/SIS_ItemStruct.h"

#include "UnitStructs.generated.h"

class UTexture2D;
class AUnitPawnBase;

/** 单位实体配置；模板创建实例时复制，后续实体相关参数统一扩展在此。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitEntityData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位实体", meta=(DisplayName="单位实体类"))
    TSubclassOf<AUnitPawnBase> UnitPawnClass;
};

UENUM(BlueprintType)
enum class ECharacterGender : uint8
{
	Unknown UMETA(DisplayName = "未知"),
	Male UMETA(DisplayName = "男性"),
	Female UMETA(DisplayName = "女性"),
	Other UMETA(DisplayName = "其他")
};


// 单位档案 记录单位的基础信息。
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitProfile
{
	GENERATED_BODY()


	/** 名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "名字"))
	FText FirstName;

	/** 姓 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "姓氏"))
	FText LastName;


	/** 角色代号/呼号，适合佣兵、特工等设定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "代号"))
	FText CodeName;


	/** 用于单位列表、编队界面等 UI 展示的头像纹理 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "头像纹理"))
	TObjectPtr<UTexture2D> PortraitTexture = nullptr;

	/** Optional authored square image; otherwise crop the full portrait around PortraitCropFocus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位档案|身份信息", meta=(DisplayName="方形头像（可选）"))
	TObjectPtr<UTexture2D> SquarePortraitTexture = nullptr;
	/** Normalized face position. Existing square portraits remain unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位档案|身份信息", meta=(DisplayName="方形头像裁切焦点"))
	FVector2D PortraitCropFocus = FVector2D(0.5, 0.28);

    /** 方形头像取原图短边的比例；小于 1 可聚焦脸部，完整头像不受影响。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位档案|头像", meta=(DisplayName="方形头像裁切范围", ClampMin="0.1", ClampMax="1.0"))
    float PortraitCropScale = 1.0f;

	/** 性别 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "性别"))
	ECharacterGender Gender = ECharacterGender::Unknown;

	/** 年龄 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|身份信息", meta = (DisplayName = "年龄", ClampMin = "0", UIMin = "0"))
	int32 Age = 18;


	// ==================== 背景信息 ====================

	/** 国籍 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|背景信息", meta = (DisplayName = "国籍"))
	FText Nationality;

	/** 出身地、来源地或所属地区 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|背景信息", meta = (DisplayName = "出身"))
	FText Origin;

	/** 人物背景简介，用于档案描述或剧情文本展示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位档案|背景信息", meta = (DisplayName = "人员简历", MultiLine = "true"))
	FText PersonnelResume;

};

// 单位运行时数据 需要频繁更改的数据。
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitRuntimeData
{
	GENERATED_BODY()

public:

	/** 单位当前所在的战略地图瓦片 ID；None 表示尚未分配位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位运行时数据|战略地图", meta=(DisplayName="当前所在瓦片ID"))
	FName TileId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位运行时数据|战略移动", meta = (DisplayName = "无法战略步行"))
    bool bCannotWalkStrategically = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位运行时数据|生命", meta = (DisplayName = "当前血量", ClampMin = "0.0"))
	float CurrentHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位运行时数据|体力", meta = (DisplayName = "当前体力", ClampMin = "0.0"))
	float CurrentStamina = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "单位运行时数据|负重", meta = (DisplayName = "当前负重", ClampMin = "0.0", Units = "kg"))
	float CurrentCarryWeight = 0.0f;


};

/** 基础属性，不包含装备临时加成；默认数值仅为占位，平衡规则确定后在模板中调整。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitAttributes
{
    GENERATED_BODY()


    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|生存", meta=(DisplayName="最大血量", ClampMin="0.0"))
    float MaxHealth = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|生存", meta=(DisplayName="最大体力", ClampMin="0.0"))
    float MaxStamina = 100.0f;
    /** 单位 kg；实际重量由背包系统同步至运行时数据，不在这里重复存放物品。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|生存", meta=(DisplayName="负重上限", ClampMin="0.0", Units="kg"))
    float MaxCarryWeight = 30.0f;
    /** 力量能力值，具体伤害/负重换算交由业务层实现。
        影响近战攻击力与负重能力
        可以降低使用重型武器时的惩罚
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|基础", meta=(DisplayName="力量", ClampMin="0"))
    int32 Strength = 10;

    /** 移动与反应能力。 
        影响移动速度与近战闪避
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|基础", meta=(DisplayName="敏捷", ClampMin="0"))
    int32 Agility = 10;
    /** 精细操作能力，与敏捷独立。 
        双手的灵巧度，影响持刀状态下的攻击力与飞刀准确度
        还可以降低双持手枪时的惩罚
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|基础", meta=(DisplayName="灵巧", ClampMin="0"))
    int32 Dexterity = 10;

    /** 学习与分析能力。 
        可以增加技能的学习速度
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|基础", meta=(DisplayName="智力", ClampMin="0"))
    int32 Intelligence = 10;
    /** 身体素质；生命和体力上限不在结构体内自动推导。
        影响生命和体力上限
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|基础", meta=(DisplayName="体质", ClampMin="0"))
    int32 Constitution = 10;
    /** 枪械操作熟练度，不直接等同于命中概率。
        影响枪械命中率
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|专业", meta=(DisplayName="枪法", ClampMin="0"))
    int32 Marksmanship = 0;

    /** 爆炸物操作能力。
        制作炸弹与拆弹能力，可以发现地雷，也能够降低爆炸伤害
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|专业", meta=(DisplayName="爆破", ClampMin="0"))
    int32 Demolition = 0;

    /** 电子入侵与系统操作能力。 
        可以控制电子系统
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|专业", meta=(DisplayName="黑客", ClampMin="0"))
    int32 Hacking = 0;

    /** 治疗与急救能力。
        数值低时影响急救速度，数值高时可以进行战地医疗工作。
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位属性|专业", meta=(DisplayName="医疗", ClampMin="0"))
    int32 Medicine = 0;
};

/** 管理者评价；Content 为空表示尚无评价。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitEvaluation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="人员评价", meta=(DisplayName="评价内容", MultiLine=true)) FText Content;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="人员评价", meta=(DisplayName="评价者")) FText Evaluator;
};

/** 由游戏时间和战斗结算维护，不使用现实时间自动累计。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitServiceRecord
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="服役记录", meta=(DisplayName="服役天数", ClampMin="0")) int32 ServiceDays=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="服役记录", meta=(DisplayName="参战次数", ClampMin="0")) int32 BattleCount=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="服役记录", meta=(DisplayName="负伤记录")) TArray<FText> InjuryRecords;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="服役记录", meta=(DisplayName="服役日志")) TArray<FText> Entries;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUnitDataChanged, FGuid);

/**
 * 单位数据规则（后续单位、实体和 UI 开发必须遵守）：
 * 1. 管理类 TMap<FGuid,TSharedPtr<FUnitData>> 保存唯一权威实例；用 ID 查询，禁止消费者另建权威副本。
 * 2. C++ 消费者持有同一个 TSharedPtr<FUnitData>，用 AddUObject + FDelegateHandle 订阅 OnDataChanged。
 *    换绑、NativeDestruct/EndPlay/BeginDestroy 时 Remove(handle)，随后 Reset 指针。
 * 3. 使用 Data->Modify([](FUnitData& D){ ... }); 修改。直接写字段后必须 NotifyDataChanged()。
 *    普通成员赋值不会自动通知！只在游戏线程修改，不在通知回调中再次修改；需要时延迟到下一帧。
 * 4. UnitId 是不可修改的身份。模板/存档数组和蓝图快照仅供传输；加载同 ID 原地更新，保持地址及订阅。
 * 5. 复制只复制业务字段，不复制事件订阅；事件不序列化。UObject 资源由共享存储的 GC 桥接追踪。
 * 6. 蓝图不能持有 TSharedPtr，使用 UUnitDataReference 桥接；其快照依旧是副本。
 *    跨 GameInstance 不复用引用，销毁消费者必须解绑，最后一个强引用释放才销毁数据。
 * 7. 小队只保存成员 UnitId；所属小队由 UPlayerSquadManagerBase 的反向索引查询，不重复保存 SquadId。
 *    入队/退队必须经小队管理类；战略移动用 SetSquadTileId 同步全队 TileId，战术 Pawn 位移不改战略位置。
 *    入队、退队或小队改名会触发本单位 OnDataChanged，UI 可重新查询所属小队。
 * 8. StrategicMovementData 保存战略瓦片之间的速度与消耗，RuntimeData.TileId 保存当前所在瓦片。
 *    行军业务经同一共享引用读取，修改仍须通知；复制/存档/模板生成时必须保留此字段。
 *    速度以瓦片/游戏小时计，不是战术 Pawn 的 cm/s；瓦片倍率与队伍出行方式由行军结算处理。
 * 9. 选中状态只在当前共享实例中有效，用 SetSelected 修改，订阅 OnSelected/OnDeselected。
 *    状态与订阅不进入模板、快照或存档；原地加载保留目标实例的选中状态。
 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitData
{
    GENERATED_BODY()

    FUnitData() = default;
    FUnitData(const FUnitData& Other) { *this=Other; }
    FUnitData& operator=(const FUnitData& Other)
    {
        if (this != &Other)
        {
            // 新增业务字段时同步维护这里；事件订阅和通知状态始终留在目标实例上。
            UnitId = Other.UnitId;
            Profile = Other.Profile;
            Attributes = Other.Attributes;
            RuntimeData = Other.RuntimeData;
            Evaluation = Other.Evaluation;
            ServiceRecord = Other.ServiceRecord;
            InventoryItems = Other.InventoryItems;
            EntityData = Other.EntityData;
            StrategicMovementData = Other.StrategicMovementData;
        }
        return *this;
    }
    FUnitDataChanged OnDataChanged;
    /** 运行时选择事件；实体与 UI 订阅同一权威数据，不依赖彼此引用。 */
    FUnitDataChanged OnSelected;
    FUnitDataChanged OnDeselected;
    bool IsSelected() const { return bSelected; }
    /** 仅状态改变时广播；通知中不允许递归切换，返回是否实际改变。 */
    bool SetSelected(bool bInSelected)
    {
        check(IsInGameThread());
        if (bNotifyingSelection || bSelected == bInSelected) return false;
        TGuardValue<bool> Guard(bNotifyingSelection, true);
        bSelected = bInSelected;
        (bSelected ? OnSelected : OnDeselected).Broadcast(UnitId);
        return true;
    }
    bool IsNotifyingDataChanged() const { return bNotifying; }
    void NotifyDataChanged()
    {
        check(IsInGameThread());
        if (bNotifying) return;
        TGuardValue<bool> Guard(bNotifying,true);
        OnDataChanged.Broadcast(UnitId);
    }
    template<typename T> void Modify(T&& Edit)
    {
        check(IsInGameThread());
        if (bNotifying) return;
        const FGuid OriginalId=UnitId;
        Edit(*this);
        UnitId=OriginalId;
        NotifyDataChanged();
    }
private:
    bool bNotifying=false;
    bool bSelected=false;
    bool bNotifyingSelection=false;
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="单位唯一ID"))
    FGuid UnitId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="单位档案"))
    FUnitProfile Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位实体", meta=(DisplayName="单位实体配置"))
    FUnitEntityData EntityData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="基础属性"))
    FUnitAttributes Attributes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="战略移动数据"))
    FStrategicMovementData StrategicMovementData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="运行时数据"))
    FUnitRuntimeData RuntimeData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="人员评价")) FUnitEvaluation Evaluation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据", meta=(DisplayName="服役记录")) FUnitServiceRecord ServiceRecord;
    /** 单位持有的物品数据；修改后通过 Modify/NotifyDataChanged 通知。
     * 与 SIS 背包组件的导入导出需由业务层调用；SaveGame 过滤归档还需处理插件嵌套字段。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位数据|背包", meta=(DisplayName="单位物品数据"))
    TArray<FSIS_ItemData> InventoryItems;
};

/** DataTable 行结构。模板不保存实例 ID 或当前血量，创建实例时按属性上限初始化。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FUnitTemplate : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位模板", meta=(DisplayName="单位档案"))
    FUnitProfile Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="单位实体", meta=(DisplayName="单位实体配置"))
    FUnitEntityData EntityData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位模板", meta=(DisplayName="基础属性"))
    FUnitAttributes Attributes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位模板", meta=(DisplayName="战略移动数据"))
    FStrategicMovementData StrategicMovementData;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUnitReferenceChanged, FGuid, UnitId);

/** 蓝图共享引用桥接；内部持有管理类中的同一个单位数据，不持有权威副本。 */
UCLASS(BlueprintType)
class SILVERCHOIR_API UUnitDataReference : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(TSharedPtr<FUnitData> InData);
    virtual void BeginDestroy() override;
    TSharedPtr<FUnitData> GetSharedData() const { return Data; }
    UFUNCTION(BlueprintPure, Category="玩家单位|数据") FUnitData GetSnapshot() const { return Data?*Data:FUnitData(); }
    UFUNCTION(BlueprintPure, Category="玩家单位|数据") FGuid GetUnitId() const { return Data?Data->UnitId:FGuid(); }
    UPROPERTY(BlueprintAssignable, Category="玩家单位|数据") FUnitReferenceChanged OnDataChanged;
    /** 蓝图通过共享引用绑定，GetSnapshot 返回的副本不携带选中状态。 */
    UFUNCTION(BlueprintPure, Category="玩家单位|选中", meta=(DisplayName="单位是否选中"))
    bool IsSelected() const { return Data && Data->IsSelected(); }
    UFUNCTION(BlueprintCallable, Category="玩家单位|选中", meta=(DisplayName="设置单位选中状态"))
    bool SetSelected(bool bSelected);
    UPROPERTY(BlueprintAssignable, Category="玩家单位|选中", meta=(DisplayName="单位被选中"))
    FUnitReferenceChanged OnSelected;
    UPROPERTY(BlueprintAssignable, Category="玩家单位|选中", meta=(DisplayName="单位取消选中"))
    FUnitReferenceChanged OnDeselected;
private:
    TSharedPtr<FUnitData> Data;
    FDelegateHandle ChangedHandle;
    FDelegateHandle SelectedHandle;
    FDelegateHandle DeselectedHandle;
    void ReleaseData();
    void HandleChanged(FGuid Id) { OnDataChanged.Broadcast(Id); }
    void HandleSelected(FGuid Id) { OnSelected.Broadcast(Id); }
    void HandleDeselected(FGuid Id) { OnDeselected.Broadcast(Id); }
};
