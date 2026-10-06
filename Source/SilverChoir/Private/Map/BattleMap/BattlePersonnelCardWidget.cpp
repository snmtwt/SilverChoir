#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleHUDLibrary.h"
#include "Map/BattleMap/BattleResourceBar.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "UIBasic/ECGWidget.h"
#include "Widget/SlotContainerByType/SIS_MirrorSlotContainer.h"
#include "Widget/SlotContainer/SIS_SlotContainer.h"
#include "UIBasic/PixelAlignedButtonFrame.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

UBattlePersonnelCardWidget::UBattlePersonnelCardWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    MinimumSize=FVector2D::ZeroVector;
    bUseTabStyle=false;
    BackgroundColor=FLinearColor(FColor(7,19,29,250));
    BorderColor=FLinearColor(FColor(39,77,95));
    AccentColor=FLinearColor(FColor(30,211,231));
    ButtonText=FText::GetEmpty();
}

void UBattlePersonnelCardWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    RefreshCardStyle();
}
void UBattlePersonnelCardWidget::NativeConstruct()
{
    ++PresentationRevision;
    bPresentationConstructed=true;
    OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMemberInvoked);
    Super::NativeConstruct();
    if(!bPresentationConstructed)return;
    RefreshCardStyle();RefreshVitals();
    const uint64 Revision=PresentationRevision;
    RefreshUnitSelection();
    if(bPresentationConstructed&&Revision==PresentationRevision&&bHasMemberData)OnMemberDataApplied(Member);
}
void UBattlePersonnelCardWidget::NativeDestruct()
{
    bPresentationConstructed=false;
    PanelAnimationTick.Stop();
    ++PresentationRevision;
    OnClicked.RemoveDynamic(this,&ThisClass::HandleMemberInvoked);
    OnPresentationReleased.Broadcast(this);
    CloseControlPanel();
    ReleaseUnitSelection();
    Super::NativeDestruct();
}
void UBattlePersonnelCardWidget::SetMember(const FBattleMemberView& Data)
{
    if(Member.UnitId!=Data.UnitId)
    {
        const uint64 Revision=++PresentationRevision;
        OnMemberContextChanged.Broadcast(this);
        if(Revision!=PresentationRevision)return;
        CloseControlPanel();
        if(Revision!=PresentationRevision)return;
    }
    FBattleMemberView Safe=Data;
    Safe.Health=UBattleHUDLibrary::SanitizeRatio(Data.Health);
    Safe.Stamina=UBattleHUDLibrary::SanitizeRatio(Data.Stamina);
    Safe.Morale=UBattleHUDLibrary::SanitizeRatio(Data.Morale);
    bHasMemberData=true;
    Super::SetMember(Safe);
    RefreshUnitSelection();
    RefreshVitals();
    // SetMember is commonly called before AddChild/TakeWidget. The inventory
    // mirror's child slots must exist before Blueprint attaches a real unit.
    if(bPresentationConstructed&&!IsDesignTime())OnMemberDataApplied(Member);
}
void UBattlePersonnelCardWidget::BeginDestroy()
{
    PanelAnimationTick.Stop();
    ReleaseUnitSelection();
    Super::BeginDestroy();
}
bool UBattlePersonnelCardWidget::RefreshUnitSelection()
{
    const auto Data=!IsDesignTime()&&Member.UnitId.IsValid()
        ? UPlayerUnitLibrary::GetUnitDataShared(this,Member.UnitId):nullptr;
    if(Data!=SelectionUnitData)
    {
        const bool bHadBinding=SelectionUnitData.IsValid();
        ReleaseUnitSelection();
        SelectionUnitData=Data;
        if(SelectionUnitData)
        {
            UnitSelectedHandle=SelectionUnitData->OnSelected.AddUObject(this,&ThisClass::HandleUnitSelectionChanged);
            UnitDeselectedHandle=SelectionUnitData->OnDeselected.AddUObject(this,&ThisClass::HandleUnitSelectionChanged);
        }
        else if(bHadBinding)SetSelected(false);
    }
    if(!SelectionUnitData)return false;
    SetSelected(SelectionUnitData->IsSelected());
    return true;
}
void UBattlePersonnelCardWidget::ReleaseUnitSelection()
{
    if(SelectionUnitData)
    {
        SelectionUnitData->OnSelected.Remove(UnitSelectedHandle);
        SelectionUnitData->OnDeselected.Remove(UnitDeselectedHandle);
    }
    UnitSelectedHandle.Reset();
    UnitDeselectedHandle.Reset();
    SelectionUnitData.Reset();
}
void UBattlePersonnelCardWidget::HandleUnitSelectionChanged(FGuid UnitId)
{
    if(SelectionUnitData&&UnitId==Member.UnitId)SetSelected(SelectionUnitData->IsSelected());
}
void UBattlePersonnelCardWidget::SetMemberVitals(float Health,float Stamina,float Morale)
{
    FBattleMemberView Updated=Member;
    Updated.Health=Health;Updated.Stamina=Stamina;Updated.Morale=Morale;
    SetMember(Updated);
}
void UBattlePersonnelCardWidget::RefreshCardStyle()
{
    auto StyleBar=[](UProgressBar* Bar,FLinearColor Color)
    {
        if(auto* Resource=Cast<UBattleResourceBar>(Bar))
        {
            Resource->SetFillColorAndOpacity(FLinearColor::White);
            Resource->SetGradientColors(FLinearColor(Color.R*.3f,Color.G*.3f,Color.B*.3f,1),Color,FMath::Lerp(Color,FLinearColor::White,.65f).CopyWithNewOpacity(.8f));
        }
        else if(Bar)Bar->SetFillColorAndOpacity(Color);
    };
    StyleBar(StaminaBar,StaminaColor);StyleBar(MoraleBar,MoraleColor);
    if(MemberName)MemberName->SetColorAndOpacity(FLinearColor(FColor(218,235,242)));
    // Theme only this mirror's generated children; the inventory plugin still
    // owns visibility, placement feedback, occupancy and drag/drop behavior.
    if(HandEquipment)for(auto* Container:HandEquipment->SlotContainerList)
    {
        if(!Container)continue;
        if(Container->ContainerName)Container->ContainerName->SetColorAndOpacity(FLinearColor(FColor(138,166,182)));
        for(const auto& Entry:Container->SlotMap)
        {
            const auto* EquipmentSlot=Entry.Value;
            if(!EquipmentSlot||!EquipmentSlot->SlotBorder)continue;
            auto Brush=EquipmentSlot->SlotBorder->Background;
            Brush.OutlineSettings.Color=FLinearColor(FColor(42,83,104));
            EquipmentSlot->SlotBorder->SetBrush(Brush);
            EquipmentSlot->SlotBorder->SetBrushColor(FLinearColor(FColor(42,83,104)));
        }
    }
    if(bHasMemberData)RefreshVitals();
    if(const auto Slate=GetCachedWidget())Slate->Invalidate(EInvalidateWidgetReason::Paint);
}
void UBattlePersonnelCardWidget::RefreshVitals()
{
    const float H=UBattleHUDLibrary::SanitizeRatio(Member.Health);
    const FColor StatusColor=H<=0?FColor(235,87,100):H<.3f?FColor(250,110,80):H<.7f?FColor(244,185,85):FColor(43,206,224);
    if(HealthText)
    {
        HealthText->SetText(H<=0?NSLOCTEXT("BattleCard","Down","失能"):H<.3f?NSLOCTEXT("BattleCard","Critical","危急"):H<.7f?NSLOCTEXT("BattleCard","Wounded","负伤"):NSLOCTEXT("BattleCard","Stable","稳定"));
        HealthText->SetColorAndOpacity(FLinearColor(StatusColor));
    }
    if(ECGMonitor)
    {
        const float Rate=FMath::IsFinite(DisplayHeartRateBPM)?FMath::Clamp(DisplayHeartRateBPM,0.f,300.f):72.f;
        ECGMonitor->SetECGParameters(FLinearColor(StatusColor),Rate,H<=0?0.f:FMath::Lerp(.28f,1.f,H));
    }
}
void UBattlePersonnelCardWidget::HandleMemberInvoked()
{
    if(!bPresentationConstructed||bInvokingMember||!Member.UnitId.IsValid())return;
    TGuardValue<bool> Guard(bInvokingMember,true);
    const uint64 Revision=PresentationRevision;
    const FGuid UnitId=Member.UnitId;
    if(auto* Player=Cast<AGameMainMapPlayerController>(GetOwningPlayer());Player&&IsValid(Player->BattleWidget))
        Player->BattleWidget->CancelTargeting();
    // CancelTargeting refreshes the HUD; its Blueprint callbacks may replace this card before selection.
    if(!bPresentationConstructed||Revision!=PresentationRevision||Member.UnitId!=UnitId)return;
    // Opening an already selected member must preserve the entire shared selection, including a box selection.
    const auto UnitData=UPlayerUnitLibrary::GetUnitDataShared(this,UnitId);
    if(!UnitData||!UnitData->IsSelected())
        if(auto* Player=GetOwningPlayer())
            if(auto* Selection=Player->FindComponentByClass<UBattleUnitSelectionComponent>())Selection->SelectUnitById(UnitId);
    // Selection listeners can rebuild the roster or change maps synchronously.
    if(!bPresentationConstructed||Revision!=PresentationRevision||Member.UnitId!=UnitId)return;
    OnControlPanelRequested.Broadcast(this,UnitId);
    if(!bPresentationConstructed||Revision!=PresentationRevision||Member.UnitId!=UnitId)return;
    OnMemberInvoked(UnitId);
}

void UBattlePersonnelCardWidget::OpenControlPanel(FGuid SquadId)
{
    if(!bPresentationConstructed||bFlushingPanelAnimations||!Member.UnitId.IsValid())return;
    if(bControlPanelOpen&&ControlPanelSquadId==SquadId)return;
    const uint64 Revision=++PanelTransitionRevision;
    const FGuid UnitId=Member.UnitId;
    bControlPanelOpen=true;
    ControlPanelSquadId=SquadId;
    // Stop only this card's animations; the ECG is a separate child widget.
    // Flush the old width track before the business event starts its replacement.
    if(IsAnyAnimationPlaying())
    {
        TGuardValue<bool> Guard(bFlushingPanelAnimations,true);
        StopAllAnimations();
        FlushAnimations();
    }
    if(Revision!=PanelTransitionRevision||!bPresentationConstructed||!bControlPanelOpen
        ||Member.UnitId!=UnitId||ControlPanelSquadId!=SquadId)return;
    OnControlPanelOpened(UnitId,SquadId);
    if(Revision==PanelTransitionRevision&&bPresentationConstructed)PanelAnimationTick.Start(this);
}
void UBattlePersonnelCardWidget::CloseControlPanel()
{
    if(!bControlPanelOpen)return;
    const uint64 Revision=++PanelTransitionRevision, Presentation=PresentationRevision;
    const FGuid UnitId=Member.UnitId, SquadId=ControlPanelSquadId;
    bControlPanelOpen=false;
    ControlPanelSquadId.Invalidate();
    if(!bFlushingPanelAnimations&&IsAnyAnimationPlaying())
    {
        TGuardValue<bool> Guard(bFlushingPanelAnimations,true);
        StopAllAnimations();
        FlushAnimations();
    }
    if(Revision!=PanelTransitionRevision||Presentation!=PresentationRevision||bControlPanelOpen)return;
    OnControlPanelClosed(UnitId,SquadId);
    if(Revision==PanelTransitionRevision&&bPresentationConstructed)PanelAnimationTick.Start(this);
}

int32 UBattlePersonnelCardWidget::PaintButtonFrame(const FGeometry& G,const FSlateRect& Culling,
    FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    const auto Frame=FPixelAlignedButtonFrame::Make(G,1.f);
    const FVector2f Size=Frame.Max-Frame.Min;
    if(Size.X<=0||Size.Y<=0)return Layer;
    const bool Selected=IsButtonSelected()||(IsDesignTime()&&bPreviewSelected);
    const float Activity=Selected?1.f:GetButtonVisualStrength()*.45f;
    const FLinearColor Tint=Style.GetColorAndOpacityTint()*FLinearColor(1,1,1,Enabled&&GetIsEnabled()?1.f:.4f);
    const auto* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
    auto Box=[&](int32 L,FVector2f P,FVector2f S,FLinearColor Color)
    {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(S,FSlateLayoutTransform(Frame.Min+P)),Brush,ESlateDrawEffect::NoPixelSnapping,Color*Tint);};
    Box(Layer,{0,0},Size,BackgroundColor);
    TArray<FSlateGradientStop> Stops;
    Stops.Emplace(FVector2f(0,0),AccentColor.CopyWithNewOpacity(.025f+.11f*Activity)*Tint);
    Stops.Emplace(FVector2f(0,Size.Y),AccentColor.CopyWithNewOpacity(.005f+.025f*Activity)*Tint);
    FSlateDrawElement::MakeGradient(E,Layer+1,G.ToPaintGeometry(Size,FSlateLayoutTransform(Frame.Min)),Stops,Orient_Horizontal,ESlateDrawEffect::NoPixelSnapping);
    const FVector2f Stroke=Frame.Thickness;
    const FLinearColor Edge=FMath::Lerp(BorderColor,AccentColor,Activity);
    Box(Layer+2,{0,0},{Size.X,Stroke.Y},Edge);Box(Layer+2,{0,Size.Y-Stroke.Y},{Size.X,Stroke.Y},Edge);
    Box(Layer+2,{0,0},{Stroke.X,Size.Y},Edge);Box(Layer+2,{Size.X-Stroke.X,0},{Stroke.X,Size.Y},Edge);
    if(Selected)
    {
        // All accents stay inside the allotted rectangle; selection cannot change layout.
        const float Corner=FMath::Min(12.f,Size.X*.12f);
        Box(Layer+3,{1,1},{Corner,2},AccentColor);Box(Layer+3,{1,1},{2,Corner},AccentColor);
        Box(Layer+3,{Size.X-Corner-1,Size.Y-3},{Corner,2},AccentColor);
        Box(Layer+3,{Size.X-3,Size.Y-Corner-1},{2,Corner},AccentColor);
        Box(Layer+3,{4,3},{FMath::Max(0.f,Size.X-8),1},AccentColor.CopyWithNewOpacity(.16f));
    }
    return Layer+4;
}
