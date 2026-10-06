#include "Map/BattleMap/BattleVehiclePanelWidget.h"

void UBattleVehiclePanelWidget::OpenPanel(FGuid InSquadId,const FBattleVehicleView& InVehicle)
{
    if(bEndingPresentation||bFlushingPanelAnimations)return;
    if(bPanelOpen&&SquadId==InSquadId&&Vehicle.VehicleId==InVehicle.VehicleId)return;
    const uint64 Revision=++PanelTransitionRevision;
    const FGuid RequestedVehicleId=InVehicle.VehicleId;
    SquadId=InSquadId;
    Vehicle=InVehicle;
    bPanelOpen=true;
    // A collapsed widget can retain a suspended closing animation. Remove its
    // evaluation before Blueprint starts the opposite width animation.
    if(IsAnyAnimationPlaying())
    {
        TGuardValue<bool> Guard(bFlushingPanelAnimations,true);
        StopAllAnimations();
        FlushAnimations();
    }
    if(Revision!=PanelTransitionRevision||bEndingPresentation||!bPanelOpen
        ||SquadId!=InSquadId||Vehicle.VehicleId!=RequestedVehicleId)return;
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    OnPanelOpened(SquadId,Vehicle.VehicleId);
    if(Revision==PanelTransitionRevision&&!bEndingPresentation)PanelAnimationTick.Start(this);
}

void UBattleVehiclePanelWidget::ClosePanel()
{
    if(!bPanelOpen)return;
    const uint64 Revision=++PanelTransitionRevision;
    const FGuid PreviousSquad=SquadId, PreviousVehicle=Vehicle.VehicleId;
    bPanelOpen=false;
    if(!bFlushingPanelAnimations&&IsAnyAnimationPlaying())
    {
        TGuardValue<bool> Guard(bFlushingPanelAnimations,true);
        StopAllAnimations();
        FlushAnimations();
    }
    if(Revision!=PanelTransitionRevision||bPanelOpen)return;
    OnPanelClosed(PreviousSquad,PreviousVehicle);
    if(Revision==PanelTransitionRevision&&!bEndingPresentation)PanelAnimationTick.Start(this);
}

void UBattleVehiclePanelWidget::OnPanelClosed_Implementation(FGuid InSquadId,FGuid VehicleId)
{
    SetVisibility(ESlateVisibility::Collapsed);
}

void UBattleVehiclePanelWidget::NativeConstruct()
{
    bEndingPresentation=false;
    OnAnimationFinishedPlaying().RemoveAll(this);
    OnAnimationFinishedPlaying().AddUObject(this,&ThisClass::HandleLayoutAnimationFinished);
    Super::NativeConstruct();
}

void UBattleVehiclePanelWidget::HandleLayoutAnimationFinished(FWidgetAnimationState& State)
{
    if(!bEndingPresentation&&!bFlushingPanelAnimations&&bPanelOpen)OnLayoutAnimationFinished.Broadcast(this);
}

void UBattleVehiclePanelWidget::NativeDestruct()
{
    bEndingPresentation=true;
    PanelAnimationTick.Stop();
    OnAnimationFinishedPlaying().RemoveAll(this);
    OnPresentationReleased.Broadcast(this);
    ClosePanel();
    Super::NativeDestruct();
}
