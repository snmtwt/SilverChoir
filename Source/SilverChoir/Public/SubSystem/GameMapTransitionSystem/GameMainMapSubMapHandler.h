#pragma once

#include "CoreMinimal.h"
#include "MTS_SubMapHandler.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "GameMainMapSubMapHandler.generated.h"

class USMS_SceneManager;

/** Reusable game-specific operations; subclasses own entry flow and configuration. */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="主地图子地图处理类"))
class SILVERCHOIR_API UGameMainMapSubMapHandler : public UMTS_SubMapHandler
{
    GENERATED_BODY()
public:
    /** Completes initial loading; a resident instance is activated immediately instead.
     * OnSubMapReady should call ActivateThisMap for the initial visit. */
    UFUNCTION(BlueprintCallable, Category="主地图|子地图", meta=(DisplayName="提交子地图进入", AutoCreateRefTerm="LocalEntryTransform"))
    bool CommitMapEntry(const FTransform& LocalEntryTransform, EGameMainMapType MapType, FText& OutError);

    UFUNCTION(BlueprintCallable, Category="主地图|子地图", meta=(DisplayName="激活本子地图", AutoCreateRefTerm="LocalEntryTransform"))
    bool ActivateThisMap(const FTransform& LocalEntryTransform, EGameMainMapType MapType, FText& OutError);

    /** Switches to the Blueprint-selected scene and waits for its full lifecycle to finish.
     * If already there and idle, OnEntrySceneReady is dispatched synchronously. */
    UFUNCTION(BlueprintCallable, Category="主地图|子地图", meta=(DisplayName="切换并等待进入前场景"))
    bool WaitForEntryScene(FGameplayTag SceneTag, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="主地图|子地图", meta=(DisplayName="取消进入前场景等待"))
    void CancelEntrySceneWait();
    UFUNCTION(BlueprintPure, Category="主地图|子地图", meta=(DisplayName="是否正在等待进入前场景"))
    bool IsWaitingForEntryScene() const { return WaitingSceneManager.IsValid(); }

    /** Subclasses may finish deployment/initialization here, then call CommitMapEntry. */
    UFUNCTION(BlueprintNativeEvent, Category="主地图|子地图", meta=(DisplayName="进入前场景已就绪"))
    void OnEntrySceneReady(FGameplayTag SceneTag);
    virtual void OnEntrySceneReady_Implementation(FGameplayTag SceneTag) {}

protected:
    virtual void OnReleaseResources() override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<USMS_SceneManager> WaitingSceneManager;
    FGameplayTag WaitingSceneTag;
    UFUNCTION() void HandleEntrySceneChanged(FGameplayTag PreviousSceneTag, FGameplayTag CurrentSceneTag);
};
