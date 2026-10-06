#pragma once
#include "CoreMinimal.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSpawner.h"
#include "UnitDisplayStand.generated.h"

class AUnitPawnBase;
class USceneComponent;
class UStaticMeshComponent;
class APlayerController;

/** 放置在地图中的单位展示台。只生成展示实体，单位数据仍由玩家单位管理类唯一持有。 */
UCLASS(Blueprintable, meta=(DisplayName="单位展示台"))
class SILVERCHOIR_API AUnitDisplayStand : public AUnitSpawner
{
    GENERATED_BODY()
public:
    AUnitDisplayStand();
    virtual void BeginPlay() override;
    /** 地台底面的中心；人物脚底自动对齐地台顶面，不需要手动增加胶囊半高。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位展示")
    TObjectPtr<USceneComponent> DisplayAnchor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位展示")
    TObjectPtr<UStaticMeshComponent> PlatformMesh;

    /** 对齐地台顶面后的额外高度，单位厘米，用于特殊模型的脚底微调。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位展示", meta=(Units="cm"))
    float DisplayHeightOffset = 0.f;

    /** 每个鼠标水平位移单位对应的旋转角度；负值反转方向。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="单位展示|旋转")
    float MouseRotationSensitivity = 0.35f;

    /** 由蓝图在拖动开始时调用。不绑定鼠标按键，也不改变输入模式或光标。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|展示", meta=(DisplayName="开始鼠标旋转展示单位"))
    bool BeginMouseRotation(APlayerController* PlayerController,
        UPARAM(DisplayName="反转鼠标横向旋转") bool bInvertHorizontalRotation = false);

    UFUNCTION(BlueprintCallable, Category="玩家单位|展示", meta=(DisplayName="结束鼠标旋转展示单位"))
    void EndMouseRotation();

    UFUNCTION(BlueprintPure, Category="玩家单位|展示", meta=(DisplayName="正在鼠标旋转展示单位"))
    bool IsMouseRotating() const { return bMouseRotating; }

    /** 也可由蓝图的拖动事件传入水平位移，避免 UMG 捕获鼠标时丢失轴输入。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|展示", meta=(DisplayName="旋转展示单位"))
    void RotateDisplayedUnit(float MouseDeltaX);

    virtual void Tick(float DeltaSeconds) override;

    /** 成功后替换旧展示；ID 无效或未配置实体类时返回空，保留旧展示。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|展示", meta=(DisplayName="根据单位ID展示单位"))
    AUnitPawnBase* DisplayUnit(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="玩家单位|展示", meta=(DisplayName="清空展示单位"))
    void ClearDisplayedUnit();
    UFUNCTION(BlueprintPure, Category="玩家单位|展示", meta=(DisplayName="获取当前展示单位"))
    AUnitPawnBase* GetDisplayedUnit() const;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
protected:
    virtual FTransform GetUnitSpawnTransform() const override;
    virtual void ConfigureDeferredUnit(AUnitPawnBase* Unit) override;
    virtual void ConfigureFinishedUnit(AUnitPawnBase* Unit) override;
private:
    TWeakObjectPtr<APlayerController> RotationController;
    FVector2f PreviousMousePosition = FVector2f::ZeroVector;
    bool bHasMousePosition = false;
    bool bMouseRotating = false;
    bool bInvertMouseHorizontalRotation = false;
};
