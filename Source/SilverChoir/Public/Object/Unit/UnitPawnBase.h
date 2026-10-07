#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Pawn.h"
#include "MoverSimulationTypes.h"
#include "HMS_MovementStruct.h"
#include "Data/Units/UnitStructs.h"
#include "UnitPawnBase.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UHMS_CharacterMoverComponent;
class USIS_UnitInventoryComponent;
class UDecalComponent;
class UHMS_NavMoverComponent;
class UHMS_AnimationDataComponent;
class USkeletalMesh;

/** 单位实体基类。数据由玩家单位管理类持有，实体只持有共享引用。 */
UCLASS(Blueprintable, meta=(DisplayName="单位 Pawn 基类"))
class SILVERCHOIR_API AUnitPawnBase : public APawn, public IMoverInputProducerInterface
{
    GENERATED_BODY()
public:
    AUnitPawnBase();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<UCapsuleComponent> CollisionComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<USkeletalMeshComponent> MeshComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<UHMS_CharacterMoverComponent> MoverComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<UHMS_NavMoverComponent> NavMoverComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<UHMS_AnimationDataComponent> AnimationDataComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件") TObjectPtr<USIS_UnitInventoryComponent> UnitInventoryComponent;
    /** 原生选中圈，子蓝图可调整贴花材质、尺寸和相对位置。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位|组件", meta=(DisplayName="选中贴花"))
    TObjectPtr<UDecalComponent> SelectionDecal;

    /** 创建穿戴组件并跟随 MeshComponent 的骨骼姿势。同 Key 替换旧组件；无效输入返回空并保留旧组件。
     * 网格需使用与身体兼容的骨骼名称和层级。仅管理外观，不修改库存或 UnitData。 */
    UFUNCTION(BlueprintCallable, Category="单位|穿戴", meta=(DisplayName="添加穿戴骨骼网格体"))
    USkeletalMeshComponent* AddWearableMesh(USkeletalMesh* SkeletalMesh, FGuid Key);

    /** 销毁该 Key 对应的穿戴组件；不存在时返回 false。 */
    UFUNCTION(BlueprintCallable, Category="单位|穿戴", meta=(DisplayName="删除穿戴骨骼网格体"))
    bool RemoveWearableMesh(FGuid Key);

#pragma region 动画
    /** 修改自身 Mesh 动画实例的行为标签；动画实例未就绪或标签无效时返回 false。 */
    UFUNCTION(BlueprintCallable, Category="单位|动画", meta=(DisplayName="修改行为状态"))
    bool SetAnimationBehaviorState(UPARAM(meta=(Categories="HMS.Behavior")) FGameplayTag BehaviorState);

    /** 修改动画使用的武器标签，不执行实际装备操作；动画实例未就绪或标签无效时返回 false。 */
    UFUNCTION(BlueprintCallable, Category="单位|动画", meta=(DisplayName="修改装备武器类型"))
    bool SetEquippedWeaponType(UPARAM(meta=(Categories="HMS.Weapon")) FGameplayTag EquippedWeaponType);
#pragma endregion

    /** 世界空间速度(cm/s)。持续生效，传零停止；可由控制器或蓝图调用。 */
    UFUNCTION(BlueprintCallable, Category="单位|移动", meta=(DisplayName="设置单位移动速度"))
    void SetUnitMoveVelocity(FVector WorldVelocity);
    UFUNCTION(BlueprintCallable, Category="单位|移动", meta=(DisplayName="命令单位寻路移动"))
    bool RequestMoveToLocation(FVector Destination, float AcceptanceRadius = 25.f);
    UFUNCTION(BlueprintCallable, Category="单位|移动", meta=(DisplayName="停止单位移动"))
    void StopUnitMovement();
    UFUNCTION(BlueprintCallable, Category="单位|移动", meta=(DisplayName="设置单位移动步态"))
    void SetMovementGait(EHMS_Gait Gait);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="单位|移动", meta=(ClampMin="1")) float WalkSpeed = 165.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="单位|移动", meta=(ClampMin="1")) float RunSpeed = 375.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="单位|移动", meta=(ClampMin="1")) float SprintSpeed = 585.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="单位|移动") EHMS_Gait MovementGait = EHMS_Gait::Run;
    virtual FVector GetNavAgentLocation() const override;
    virtual void UpdateNavigationRelevance() override;
    virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;
    virtual void PostInitializeComponents() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category="单位|数据", meta=(DisplayName="绑定单位数据")) bool BindUnitData(FGuid UnitId);
    /** 生成器直接传入管理器的原始智能指针；构造前绑定时延后通知到组件初始化完成。 */
    bool BindUnitDataShared(const TSharedPtr<FUnitData>& InData);
    UFUNCTION(BlueprintPure, Category="单位|数据", meta=(DisplayName="获取单位ID")) FGuid GetUnitId() const;
    TSharedPtr<FUnitData> GetUnitDataShared() const { return UnitData; }
    UFUNCTION(BlueprintImplementableEvent, Category="单位|数据", meta=(DisplayName="单位数据已更新")) void OnUnitDataUpdated(FGuid UnitId);

    /** 框选/点选入口的资格开关；关闭后仍允许取消既有选中。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位|选中", meta=(DisplayName="允许被选中"))
    bool bCanBeSelected=true;
    /** 有数据时修改同一共享选中状态；未绑定数据的测试实体使用本地状态。 */
    UFUNCTION(BlueprintCallable, Category="单位|选中", meta=(DisplayName="设置单位实体选中状态"))
    bool SetUnitSelected(bool bSelected);
    UFUNCTION(BlueprintPure, Category="单位|选中", meta=(DisplayName="单位实体是否选中"))
    bool IsUnitSelected() const;
    /** 原生贴花显隐已自动同步；子蓝图可增加材质、描边等表现。首次绑定也会同步调用。 */
    UFUNCTION(BlueprintImplementableEvent, Category="单位|选中", meta=(DisplayName="单位被选中"))
    void OnUnitSelected();
    UFUNCTION(BlueprintImplementableEvent, Category="单位|选中", meta=(DisplayName="单位取消选中"))
    void OnUnitDeselected();
private:
    UPROPERTY(Transient)
    TMap<FGuid, TObjectPtr<USkeletalMeshComponent>> WearableMeshes;
    float GetMovementSpeed() const;
    FVector MoveVelocity=FVector::ZeroVector;
    TSharedPtr<FUnitData> UnitData;
    FDelegateHandle DataChangedHandle;
    FDelegateHandle SelectedHandle;
    FDelegateHandle DeselectedHandle;
    bool bUnitSelected=false;
    bool bNotifyingSelection=false;
    bool bBindingUnitData=false;
    bool bPendingInitialDataNotification=false;
    void ReleaseUnitData();
    void HandleUnitDataChanged(FGuid UnitId);
    void HandleUnitSelected(FGuid UnitId);
    void HandleUnitDeselected(FGuid UnitId);
    void ApplySelectionState(bool bSelected, bool bForceNotify=false);
    void RefreshSelectionDecalVisibility();
};
