#pragma once

#include "Map/BattleMap/BattleHUDWidgets.h"
#include "UIBasic/PanelAnimationTick.h"
#include "BattlePersonnelCardWidget.generated.h"

class UECGWidget;
class USIS_MirrorSlotContainer;
class UBattlePersonnelCardWidget;
struct FUnitData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBattlePersonnelPanelRequested, UBattlePersonnelCardWidget*, Card, FGuid, UnitId);
DECLARE_MULTICAST_DELEGATE_OneParam(FBattlePersonnelCardReleased, UBattlePersonnelCardWidget*);

/** Presentation for WBP_人员卡片. Real units share selection with their data; inventory business remains in Blueprint. */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="战斗人员卡片"))
class SILVERCHOIR_API UBattlePersonnelCardWidget : public UBattleMemberCardWidget
{
    GENERATED_BODY()
public:
    UBattlePersonnelCardWidget(const FObjectInitializer& Initializer);
    virtual void SetMember(const FBattleMemberView& Data) override;
    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category="战斗UI|人员", meta=(DisplayName="更新人员生命状态"))
    void SetMemberVitals(float Health, float Stamina, float Morale);
    UFUNCTION(BlueprintPure, Category="战斗UI|人员", meta=(DisplayName="是否选中人员"))
    bool IsMemberSelected() const { return IsButtonSelected(); }
    /** Bind to the canonical unit if available and apply its current selection without changing unit data. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|人员", meta=(DisplayName="刷新单位共享选中状态"))
    bool RefreshUnitSelection();
    UFUNCTION(BlueprintPure, Category="战斗UI|人员", meta=(DisplayName="使用单位共享选中状态"))
    bool UsesSharedUnitSelection() const { return SelectionUnitData.IsValid(); }
    /** A click notification, including clicks on an already selected unit. */
    UPROPERTY(BlueprintAssignable, Category="战斗UI|控制面板", meta=(DisplayName="人员控制面板点击请求"))
    FBattlePersonnelPanelRequested OnControlPanelRequested;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制面板") bool bControlPanelOpen=false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制面板") FGuid ControlPanelSquadId;
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|控制面板", meta=(DisplayName="打开控制面板"))
    void OnControlPanelOpened(FGuid UnitId, FGuid SquadId);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|控制面板", meta=(DisplayName="关闭控制面板"))
    void OnControlPanelClosed(FGuid UnitId, FGuid SquadId);
    // The owning member-mode panel controls this lifetime; Blueprint implements only the presentation events.
    void OpenControlPanel(FGuid SquadId);
    void CloseControlPanel();
    bool IsPresentationConstructed() const { return bPresentationConstructed; }
    FBattlePersonnelCardReleased OnPresentationReleased;
    /** Rebinding closes the old context, while the card remains registered with its owner. */
    FBattlePersonnelCardReleased OnMemberContextChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|样式")
    FLinearColor StaminaColor=FLinearColor(FColor(58,177,245));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|样式")
    FLinearColor MoraleColor=FLinearColor(FColor(71,211,151));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|样式", meta=(ClampMin="0",ClampMax="300"))
    float DisplayHeartRateBPM=72.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|设计预览")
    bool bPreviewSelected=false;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定")
    TObjectPtr<UECGWidget> ECGMonitor;
    /** Existing StrategyInventorySystem mirror-slot widget, kept interactive and editable in Designer. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定")
    TObjectPtr<USIS_MirrorSlotContainer> HandEquipment;

    UFUNCTION(BlueprintCallable, Category="战斗UI|样式", meta=(DisplayName="刷新人员卡片样式"))
    void RefreshCardStyle();
    /** Bind the real unit's inventory or other game systems here. Native clicks already select the unit. */
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务", meta=(DisplayName="人员数据更新后"))
    void OnMemberDataApplied(const FBattleMemberView& Data);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务", meta=(DisplayName="请求操作此人员"))
    void OnMemberInvoked(FGuid UnitId);

protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual int32 PaintButtonFrame(const FGeometry& Geometry,const FSlateRect& Culling,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    bool bHasMemberData=false;
    bool bPresentationConstructed=false;
    uint64 PresentationRevision=0;
    uint64 PanelTransitionRevision=0;
    bool bFlushingPanelAnimations=false;
    FPanelAnimationTick PanelAnimationTick;
    bool bInvokingMember=false;
    TSharedPtr<FUnitData> SelectionUnitData;
    FDelegateHandle UnitSelectedHandle;
    FDelegateHandle UnitDeselectedHandle;
    void ReleaseUnitSelection();
    void HandleUnitSelectionChanged(FGuid UnitId);
    void RefreshVitals();
    UFUNCTION() void HandleMemberInvoked();
};
