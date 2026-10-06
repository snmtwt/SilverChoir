#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitSpawner.generated.h"

class AUnitPawnBase;
class USceneComponent;
class UUnitDataReference;
struct FUnitData;

/** 单位实体的统一生成入口。数据始终引用原始 FUnitData，不复制单位记录。 */
UCLASS(Blueprintable, meta=(DisplayName="单位生成器"))
class SILVERCHOIR_API AUnitSpawner : public AActor
{
    GENERATED_BODY()
public:
    AUnitSpawner();
    /** Pawn 根组件（胶囊中心）的生成位置，单位为厘米。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="单位生成")
    TObjectPtr<USceneComponent> SpawnAnchor;

    /** C++ 直接传入管理器持有的智能指针；验证失败保留当前实体。 */
    AUnitPawnBase* SpawnUnitShared(const TSharedPtr<FUnitData>& Data, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="玩家单位|生成", meta=(DisplayName="根据单位ID生成单位"))
    AUnitPawnBase* SpawnUnitById(FGuid UnitId, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="玩家单位|生成", meta=(DisplayName="根据单位数据引用生成单位"))
    AUnitPawnBase* SpawnUnitFromReference(UUnitDataReference* UnitData, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="玩家单位|生成", meta=(DisplayName="清空生成单位"))
    void ClearSpawnedUnit();
    UFUNCTION(BlueprintPure, Category="玩家单位|生成", meta=(DisplayName="获取生成单位"))
    AUnitPawnBase* GetSpawnedUnit() const;
    UFUNCTION(BlueprintImplementableEvent, Category="玩家单位|生成", meta=(DisplayName="单位生成完成"))
    void OnUnitSpawned(AUnitPawnBase* Unit);
    UFUNCTION(BlueprintImplementableEvent, Category="玩家单位|生成", meta=(DisplayName="生成单位已清空"))
    void OnSpawnedUnitCleared();

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    static bool ValidateUnitData(const TSharedPtr<FUnitData>& Data, FText& OutError);

protected:
    virtual FTransform GetUnitSpawnTransform() const;
    /** 在构造和 BeginPlay 之前配置预览/战斗差异；数据与库存初始化保持统一。 */
    virtual void ConfigureDeferredUnit(AUnitPawnBase* Unit);
    virtual void ConfigureFinishedUnit(AUnitPawnBase* Unit);

private:
    UPROPERTY(Transient) TObjectPtr<AUnitPawnBase> SpawnedUnit;
    UPROPERTY(Transient) TObjectPtr<AUnitPawnBase> PendingUnit;
    bool bChangingUnit = false;
};
