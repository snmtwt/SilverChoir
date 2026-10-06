#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "BaseSceneWidget.generated.h"

class UBaseMapWidget;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBaseSceneUILifecycleEvent);

/** A functional room's content only. The base-map shell owns the header and footer. */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UBaseSceneWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="基地场景") FGameplayTag SceneTag;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="基地场景") FText SceneTitle;
    UPROPERTY(BlueprintReadOnly, Transient, Category="基地场景") TObjectPtr<UBaseMapWidget> BaseUI;
    /** Start initialization/animation; call NotifyLoadCompleted when finished. */
    UFUNCTION(BlueprintNativeEvent, Category="基地场景", meta=(DisplayName="加载场景UI"))
    void LoadSceneUI();
    virtual void LoadSceneUI_Implementation();
    /** Start the exit animation; the host keeps this widget until NotifyUnloadCompleted. */
    UFUNCTION(BlueprintNativeEvent, Category="基地场景", meta=(DisplayName="卸载场景UI"))
    void UnloadSceneUI();
    virtual void UnloadSceneUI_Implementation();
    UPROPERTY(BlueprintAssignable, Category="基地场景", meta=(DisplayName="加载完成"))
    FBaseSceneUILifecycleEvent OnLoadCompleted;
    UPROPERTY(BlueprintAssignable, Category="基地场景", meta=(DisplayName="卸载完成"))
    FBaseSceneUILifecycleEvent OnUnloadCompleted;
    UFUNCTION(BlueprintCallable, Category="基地场景", meta=(DisplayName="通知加载完成"))
    void NotifyLoadCompleted();
    UFUNCTION(BlueprintCallable, Category="基地场景", meta=(DisplayName="通知卸载完成"))
    void NotifyUnloadCompleted();
    /** Completion delegates do not replay when the host reuses an already loaded UI. */
    UFUNCTION(BlueprintPure, Category="基地场景", meta=(DisplayName="场景UI是否已加载完成"))
    bool IsSceneUILoaded() const { return Lifecycle == ELifecycle::Loaded; }
    UFUNCTION(BlueprintImplementableEvent, Category="基地场景") void OnSceneUIOpened();
    UFUNCTION(BlueprintImplementableEvent, Category="基地场景") void OnSceneUIClosed();
private:
    friend class UBaseMapWidget;
    enum class ELifecycle : uint8 { Detached, Loading, Loaded, Unloading };
    ELifecycle Lifecycle = ELifecycle::Detached;
};
