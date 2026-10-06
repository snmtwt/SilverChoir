#pragma once

#include "CoreMinimal.h"
#include "SubSystem/GameMapTransitionSystem/GameMainMapSubMapHandler.h"
#include "GameMapSubMapHandler.generated.h"

class AUnitSquadSpawner;

/** Native game-map entry flow. Blueprint children provide each map's authored configuration. */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="游戏地图子地图处理类"))
class SILVERCHOIR_API UGameMapSubMapHandler : public UGameMainMapSubMapHandler
{
    GENERATED_BODY()
public:
    UGameMapSubMapHandler();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="游戏地图|配置", meta=(DisplayName="子地图加载请求"))
    FMTS_SubMapLoadRequest LoadRequest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="游戏地图|配置", meta=(DisplayName="局部进入变换"))
    FTransform LocalEntryTransform = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="游戏地图|配置", meta=(DisplayName="进入前场景标签"))
    FGameplayTag OverviewSceneTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="游戏地图|配置", meta=(DisplayName="地图类型"))
    EGameMainMapType MapType = EGameMainMapType::Battle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="游戏地图|小队", meta=(DisplayName="参战小队ID"))
    TArray<FGuid> SquadIds;

    /** Also used when revisiting a resident map. Preserves the existing Blueprint call signature. */
    UFUNCTION(BlueprintCallable, Category="游戏地图|进入", meta=(DisplayName="准备进入瓦片地图"))
    void PrepareTileEntry();

    /** Registration order defines the corresponding index in SpawnSquads' ID array. */
    UFUNCTION(BlueprintCallable, Category="游戏地图|小队", meta=(DisplayName="注册小队生成器"))
    bool RegisterSquadSpawner(AUnitSquadSpawner* SquadSpawner, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="游戏地图|小队", meta=(DisplayName="注销小队生成器"))
    bool UnregisterSquadSpawner(AUnitSquadSpawner* SquadSpawner, FText& OutError);
    /** Invalid registrations remain null entries so querying never silently changes index pairing. */
    UFUNCTION(BlueprintPure, Category="游戏地图|小队", meta=(DisplayName="获取已注册小队生成器"))
    TArray<AUnitSquadSpawner*> GetRegisteredSquadSpawners() const;
    /** Preflights the entire batch. Runtime Blueprint failures stop the batch; earlier successes remain. */
    UFUNCTION(BlueprintCallable, Category="游戏地图|小队", meta=(DisplayName="生成小队"))
    bool SpawnSquads(const TArray<FGuid>& InSquadIds, FText& OutError);

    virtual void OnEntrySceneReady_Implementation(FGameplayTag SceneTag) override;
    virtual void OnSubMapLoaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override;
    virtual void OnSubMapReady_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override;
    virtual void OnSubMapLoadFailed_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map, const FText& Error) override;

protected:
    virtual void OnReleaseResources() override;

private:
    UPROPERTY(Transient) TArray<TWeakObjectPtr<AUnitSquadSpawner>> RegisteredSquadSpawners;
    bool bSpawningSquads = false;
    bool bEnteringMap = false;

    bool ValidateActiveMap(FMTS_SubMapInfo& OutMap, FText& OutError) const;
    bool ValidateSquadSpawner(AUnitSquadSpawner* SquadSpawner, const FMTS_SubMapInfo& Map, FText& OutError) const;
    bool RefreshBattleUI(FText& OutError);
    bool ReportGameMapError(const FText& Error, FText& OutError);
};
