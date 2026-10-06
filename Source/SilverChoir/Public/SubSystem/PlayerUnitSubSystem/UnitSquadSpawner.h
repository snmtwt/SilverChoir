#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitSquadSpawner.generated.h"

class AUnitSpawner;
class AUnitPawnBase;

/** 围绕自身按居中行列创建单位生成器；小队成员数据从玩家小队管理器读取。 */
UCLASS(Blueprintable, meta=(DisplayName="小队生成器"))
class SILVERCHOIR_API AUnitSquadSpawner : public AActor
{
    GENERATED_BODY()
public:
    AUnitSquadSpawner();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队生成", meta=(DisplayName="单位生成器类"))
    TSubclassOf<AUnitSpawner> UnitSpawnerClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队生成", meta=(DisplayName="每行人数", ClampMin="1", ClampMax="64"))
    int32 UnitsPerRow = 3;
    /** X 为前后排距，Y 为左右间距；间距不随生成器缩放。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队生成", meta=(DisplayName="单位间距", Units="cm"))
    FVector2D Spacing = FVector2D(160., 160.);
    /** 相对小队生成器旋转的局部偏移；Z 应包含 Pawn 胶囊中心高度。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队生成", meta=(DisplayName="生成位置偏移", Units="cm"))
    FVector LocalSpawnOffset = FVector(0., 0., 92.);

    /** 全队成功才替换当前队伍；无效 ID、空队、缺少实体类或中途失败保留旧队。 */
    UFUNCTION(BlueprintCallable, Category="玩家单位|小队生成", meta=(DisplayName="根据小队ID生成小队"))
    bool SpawnSquad(FGuid SquadId, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="玩家单位|小队生成", meta=(DisplayName="清空生成小队"))
    void ClearSpawnedSquad();
    UFUNCTION(BlueprintPure, Category="玩家单位|小队生成", meta=(DisplayName="获取生成小队ID"))
    FGuid GetSpawnedSquadId() const { return SpawnedSquadId; }
    UFUNCTION(BlueprintPure, Category="玩家单位|小队生成", meta=(DisplayName="获取小队单位生成器"))
    TArray<AUnitSpawner*> GetUnitSpawners() const;
    UFUNCTION(BlueprintPure, Category="玩家单位|小队生成", meta=(DisplayName="获取生成小队单位"))
    TArray<AUnitPawnBase*> GetSpawnedUnits() const;
    /** 无副作用的预览/布局查询，顺序与 MemberUnitIds 相同，包含生成器旋转与位置。 */
    UFUNCTION(BlueprintPure, Category="玩家单位|小队生成", meta=(DisplayName="计算小队生成位置"))
    TArray<FTransform> GetFormationTransforms(int32 MemberCount) const;
    UFUNCTION(BlueprintImplementableEvent, Category="玩家单位|小队生成", meta=(DisplayName="小队生成完成"))
    void OnSquadSpawned(FGuid SquadId, const TArray<AUnitPawnBase*>& Units);
    UFUNCTION(BlueprintImplementableEvent, Category="玩家单位|小队生成", meta=(DisplayName="生成小队已清空"))
    void OnSpawnedSquadCleared(FGuid SquadId);

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    static TArray<FVector> BuildFormationOffsets(int32 MemberCount, int32 InUnitsPerRow, FVector2D InSpacing);

private:
    UPROPERTY(Transient) TArray<TObjectPtr<AUnitSpawner>> UnitSpawners;
    UPROPERTY(Transient) TArray<TObjectPtr<AUnitSpawner>> PendingSpawners;
    UPROPERTY(Transient) FGuid SpawnedSquadId;
    bool bChangingSquad = false;
    static void DestroySpawners(TArray<TObjectPtr<AUnitSpawner>>& Spawners);
};
