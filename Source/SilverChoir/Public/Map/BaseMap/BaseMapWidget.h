#pragma once

#include "CoreMinimal.h"
#include "UIBasic/MapWidgetBase.h"
#include "GameplayTagContainer.h"
#include "BaseMapWidget.generated.h"

class UBaseSceneWidget;
class UTextBlock;
class UBasicButtonWidget;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBaseUIBackRequested);

/** 基地 UI 基类；布局在子蓝图 Designer 中制作，继承 HUDLayer、ModalLayer 和 InitializeMapUI。 */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UBaseMapWidget : public UMapWidgetBase
{
	GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="基地UI|场景", meta=(Categories="GameScene"))
    TMap<FGameplayTag, TSubclassOf<UBaseSceneWidget>> SceneUIClasses;
    /** Returns the new instance. When replacing a room it stays pending until the old room notifies unload completion. */
    UFUNCTION(BlueprintCallable, Category="基地UI|场景", meta=(DisplayName="根据场景Tag创建子UI"))
    UBaseSceneWidget* CreateSceneUIByTag(FGameplayTag SceneTag);
    UFUNCTION(BlueprintPure, Category="基地UI|场景", meta=(DisplayName="获取当前子场景UI"))
    UBaseSceneWidget* GetCurrentSceneUI() const { return CurrentSceneUI; }
    /** Requests unloading; removal waits for the child's NotifyUnloadCompleted. Cancels a pending replacement. */
    UFUNCTION(BlueprintCallable, Category="基地UI|场景", meta=(DisplayName="卸载当前子场景UI"))
    void UnloadCurrentSceneUI();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="基地UI") FText HeaderTitle = NSLOCTEXT("BaseUI", "Title", "基地控制中心");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="基地UI") FText FooterStatus = NSLOCTEXT("BaseUI", "Status", "基地在线");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="基地UI", meta=(ClampMin="0")) int64 CurrencyAmount = 0;
    UPROPERTY(BlueprintReadOnly, Transient, Category="基地UI") TObjectPtr<UBaseSceneWidget> CurrentSceneUI;
    UPROPERTY(BlueprintAssignable, Category="基地UI") FBaseUIBackRequested OnBackRequested;
    /** Controls the button across widget construction/reconstruction; hidden buttons take no layout space. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="基地UI|返回基地", meta=(DisplayName="显示返回基地按钮"))
    bool bBackButtonVisible = true;
    UFUNCTION(BlueprintCallable, Category="基地UI|返回基地", meta=(DisplayName="设置返回基地按钮显示"))
    void SetBackButtonVisible(bool bVisible);
    /** Override in the base UI Blueprint to handle return navigation. Existing OnBackRequested listeners are still notified. */
    UFUNCTION(BlueprintNativeEvent, Category="基地UI|返回基地", meta=(DisplayName="返回基地"))
    void OnReturnToBase();
    virtual void OnReturnToBase_Implementation();
    UFUNCTION(BlueprintCallable, Category="基地UI") void SetHeaderTitle(FText Title);
    UFUNCTION(BlueprintCallable, Category="基地UI") void SetCurrencyAmount(int64 Amount);
    UFUNCTION(BlueprintCallable, Category="基地UI") void SetFooterStatus(FText Status);
    /** Replaces only the center content. Does not switch the gameplay scene itself. */
    UFUNCTION(BlueprintCallable, Category="基地UI") UBaseSceneWidget* ShowSceneUI(TSubclassOf<UBaseSceneWidget> WidgetClass);
    UFUNCTION(BlueprintCallable, Category="基地UI") void ClearSceneUI();
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="基地UI") TObjectPtr<UPanelWidget> SceneContent;
protected:
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="基地UI") TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="基地UI") TObjectPtr<UTextBlock> CurrencyText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="基地UI") TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="基地UI") TObjectPtr<UBasicButtonWidget> BackButton;
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    friend class UBaseSceneWidget;
    UPROPERTY(Transient) TObjectPtr<UBaseSceneWidget> PendingSceneUI;
    bool bSwitchingSceneUI = false;
    bool bEndingSceneUI = false;
    UBaseSceneWidget* MountSceneUI(TSubclassOf<UBaseSceneWidget> WidgetClass, FGameplayTag Tag);
    void RemoveCurrentSceneUI();
    void AttachPendingSceneUI();
    void FinishSceneUIUnload(UBaseSceneWidget* Finished);
    UFUNCTION() void HandleBackRequested();
};
