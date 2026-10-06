#pragma once

#include "Blueprint/UserWidget.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "UIBasic/PanelAnimationTick.h"
#include "BattleVehiclePanelWidget.generated.h"

class UBattleVehiclePanelWidget;
DECLARE_MULTICAST_DELEGATE_OneParam(FBattleVehiclePanelReleased, UBattleVehiclePanelWidget*);
DECLARE_MULTICAST_DELEGATE_OneParam(FBattleVehiclePanelAnimationFinished, UBattleVehiclePanelWidget*);

/** The member-mode panel owns the instance; a child Blueprint supplies its layout and business behavior. */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="战斗车辆面板"))
class SILVERCHOIR_API UBattleVehiclePanelWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|车辆") FGuid SquadId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|车辆") FBattleVehicleView Vehicle;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|车辆") bool bPanelOpen=false;
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|车辆", meta=(DisplayName="打开车辆面板"))
    void OnPanelOpened(FGuid InSquadId, FGuid VehicleId);
    /** Default closes immediately; a Blueprint override may finish its close animation before collapsing. */
    UFUNCTION(BlueprintNativeEvent, Category="战斗UI|车辆", meta=(DisplayName="关闭车辆面板"))
    void OnPanelClosed(FGuid InSquadId, FGuid VehicleId);
    virtual void OnPanelClosed_Implementation(FGuid InSquadId, FGuid VehicleId);
    void OpenPanel(FGuid InSquadId, const FBattleVehicleView& InVehicle);
    void ClosePanel();
    FBattleVehiclePanelReleased OnPresentationReleased;
    /** Native layout notification; Blueprint remains responsible for playing its animations. */
    FBattleVehiclePanelAnimationFinished OnLayoutAnimationFinished;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    bool bEndingPresentation=false;
    bool bFlushingPanelAnimations=false;
    uint64 PanelTransitionRevision=0;
    FPanelAnimationTick PanelAnimationTick;
    void HandleLayoutAnimationFinished(FWidgetAnimationState& State);
};
