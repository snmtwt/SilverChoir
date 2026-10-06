#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleVehiclePanelWidget.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBox.h"

#include "Components/ScrollBox.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace BattleModeListStyle
{
void Apply(UScrollBox* ScrollBox, const FBattleListScrollStyle& Settings, EOrientation Orientation, bool bAlwaysShow = false)
{
    if (!ScrollBox) return;
    const float RequestedThickness = Orientation == Orient_Vertical
        ? Settings.VerticalThickness : Settings.HorizontalThickness;
    const float Thickness = FMath::IsFinite(RequestedThickness)
        ? FMath::Clamp(RequestedThickness, 2.f, 8.f) : 4.f;
    const FVector2f ImageSize(Thickness, Thickness);
    const float Radius = Thickness * .5f;
    const FSlateRoundedBoxBrush Track(Settings.TrackColor, Radius, ImageSize);
    FScrollBarStyle BarStyle;
    BarStyle.SetHorizontalBackgroundImage(Track)
        .SetVerticalBackgroundImage(Track)
        .SetHorizontalTopSlotImage(Track)
        .SetVerticalTopSlotImage(Track)
        .SetHorizontalBottomSlotImage(Track)
        .SetVerticalBottomSlotImage(Track)
        .SetNormalThumbImage(FSlateRoundedBoxBrush(Settings.ThumbColor, Radius, ImageSize))
        .SetHoveredThumbImage(FSlateRoundedBoxBrush(Settings.HoverColor, Radius, ImageSize))
        .SetDraggedThumbImage(FSlateRoundedBoxBrush(Settings.DragColor, Radius, ImageSize))
        .SetThickness(Thickness);
    ScrollBox->SetWidgetBarStyle(BarStyle);

    // Remove the stock wide black fades; the surrounding card frames already
    // mark clipping edges. Zero content padding preserves the authored layout.
    FScrollBoxStyle BoxStyle = ScrollBox->GetWidgetStyle();
    const FSlateNoResource NoShadow;
    BoxStyle.SetTopShadowBrush(NoShadow).SetBottomShadowBrush(NoShadow)
        .SetLeftShadowBrush(NoShadow).SetRightShadowBrush(NoShadow)
        .SetHorizontalScrolledContentPadding(FMargin(0.f))
        .SetVerticalScrolledContentPadding(FMargin(0.f))
        .SetBarThickness(Thickness);
    ScrollBox->SetWidgetStyle(BoxStyle);
    ScrollBox->SetOrientation(Orientation);
    ScrollBox->SetScrollbarThickness(FVector2D(Thickness, Thickness));
    ScrollBox->SetScrollbarPadding(Orientation == Orient_Vertical
        ? FMargin(2.f, 0.f, 0.f, 0.f) : FMargin(0.f, 2.f, 0.f, 0.f));
    ScrollBox->SetAlwaysShowScrollbar(bAlwaysShow);
    ScrollBox->SetAlwaysShowScrollbarTrack(bAlwaysShow);
    ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
    ScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    ScrollBox->SetAnimateWheelScrolling(true);
    ScrollBox->SetAllowOverscroll(false);
}
}

UBattleMemberModeWidget::UBattleMemberModeWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    ListStyle.HorizontalThickness = 2.f;
}

void UBattleMemberModeWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    RefreshListStyle();
}

void UBattleMemberModeWidget::NativeConstruct()
{
    bEndingPresentation=false;
    bPresentationActive=true;
    const uint64 Revision=++PanelRevision;
    RefreshListStyle();
    if(VehicleButton)VehicleButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleVehicleClicked);
    if(UBattleVehiclePanelWidget* AuthoredPanel=VehiclePanel;IsValid(AuthoredPanel))
    {
        TGuardValue<bool> Guard(bChangingPanel,true);
        BindVehiclePanel(AuthoredPanel,bOwnsVehiclePanel&&VehiclePanel==AuthoredPanel);
        AuthoredPanel->ClosePanel();
        if(!bPresentationActive||Revision!=PanelRevision||VehiclePanel!=AuthoredPanel)return;
        AuthoredPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    Super::NativeConstruct();
    if(!bPresentationActive||!IsValid(MemberList))return;
    // Designer-authored cards and cards added by Blueprint Construct use the same native routing as a rebuilt roster.
    for(UWidget* Child:MemberList->GetAllChildren())
        if(auto* Card=Cast<UBattlePersonnelCardWidget>(Child))RegisterPersonnelCard(Card);
}

void UBattleMemberModeWidget::NativeDestruct()
{
    bEndingPresentation=true;
    bPresentationActive=false;
    ++PanelRevision;
    if(VehicleButton)
    {
        VehicleButton->CancelPendingClick();
        VehicleButton->OnClicked.RemoveDynamic(this,&ThisClass::HandleVehicleClicked);
    }
    ResetPersonnelCards();
    UBattleVehiclePanelWidget* Previous=VehiclePanel;
    const bool bRemovePrevious=bOwnsVehiclePanel;
    if(bRemovePrevious)
    {
        VehiclePanel=nullptr;
        bOwnsVehiclePanel=false;
    }
    if(IsValid(Previous))
    {
        Previous->OnPresentationReleased.RemoveAll(this);
        Previous->OnLayoutAnimationFinished.RemoveAll(this);
        // The Designer tree must survive removal/re-attachment of this same member-mode instance.
        if(bRemovePrevious)Previous->RemoveFromParent();
    }
    Super::NativeDestruct();
}

void UBattleMemberModeWidget::SetSquadContext(FGuid SquadId,const FBattleVehicleView& Vehicle)
{
    const bool bChanged=CurrentSquadId!=SquadId||CurrentVehicle.VehicleId!=Vehicle.VehicleId;
    const uint64 Revision=PanelRevision;
    if(bChanged)
    {
        CloseActivePanel();
        if(PanelRevision!=Revision+1)return;
    }
    CurrentSquadId=SquadId;
    CurrentVehicle=Vehicle;
    if(VehicleButton)VehicleButton->SetIsEnabled(SquadId.IsValid()&&Vehicle.VehicleId.IsValid());
}

void UBattleMemberModeWidget::RegisterPersonnelCard(UBattlePersonnelCardWidget* Card)
{
    if(bEndingPresentation||!IsValid(Card)||PersonnelCards.Contains(Card))return;
    PersonnelCards.Add(Card);
    Card->OnControlPanelRequested.AddUniqueDynamic(this,&ThisClass::HandlePersonnelPanelRequested);
    Card->OnPresentationReleased.AddUObject(this,&ThisClass::HandlePersonnelReleased);
    Card->OnMemberContextChanged.AddUObject(this,&ThisClass::HandlePersonnelContextChanged);
}

void UBattleMemberModeWidget::ResetPersonnelCards()
{
    // Unbind before calling close events: their Blueprint may replace the entire roster.
    TArray<TObjectPtr<UBattlePersonnelCardWidget>> Previous=MoveTemp(PersonnelCards);
    PersonnelCards.Reset();
    for(UBattlePersonnelCardWidget* Card:Previous)if(IsValid(Card))
    {
        Card->OnControlPanelRequested.RemoveDynamic(this,&ThisClass::HandlePersonnelPanelRequested);
        Card->OnPresentationReleased.RemoveAll(this);
        Card->OnMemberContextChanged.RemoveAll(this);
    }
    CloseActivePanel();
}

bool UBattleMemberModeWidget::CloseActivePanelInternal(uint64 Revision)
{
    UBattlePersonnelCardWidget* PreviousCard=ActivePersonnelCard;
    UBattleVehiclePanelWidget* PreviousVehicle=VehiclePanel;
    ActivePersonnelCard=nullptr;
    if(VehicleButton)VehicleButton->SetSelected(false);
    if(IsValid(PreviousCard))PreviousCard->CloseControlPanel();
    if(Revision!=PanelRevision)return false;
    if(IsValid(PreviousVehicle))
    {
        // A queued reveal belongs to the old open state, not to the next panel.
        if(bVehicleInMemberScrollBox&&MemberScrollBox)MemberScrollBox->ScrollWidgetIntoView(nullptr,false);
        PreviousVehicle->ClosePanel();
    }
    return Revision==PanelRevision;
}

void UBattleMemberModeWidget::CloseActivePanel()
{
    TGuardValue<bool> Guard(bChangingPanel,true);
    CloseActivePanelInternal(++PanelRevision);
}

bool UBattleMemberModeWidget::ShowPersonnelPanel(UBattlePersonnelCardWidget* Card)
{
    if(!bPresentationActive||bChangingPanel||!IsValid(Card)||!PersonnelCards.Contains(Card)
        ||Card->GetParent()!=MemberList||!Card->IsPresentationConstructed()||!Card->Member.UnitId.IsValid())return false;
    TGuardValue<bool> Guard(bChangingPanel,true);
    const uint64 Revision=++PanelRevision;
    const FGuid UnitId=Card->Member.UnitId, SquadId=CurrentSquadId;
    OnPanelOpening.Broadcast(UnitId);
    if(!bPresentationActive||Revision!=PanelRevision||!IsValid(Card)||!PersonnelCards.Contains(Card)
        ||Card->GetParent()!=MemberList||!Card->IsPresentationConstructed()
        ||Card->Member.UnitId!=UnitId||CurrentSquadId!=SquadId)return false;
    // Repeated click requests still run HUD bookkeeping, but must not restart opposing width animations.
    if(ActivePersonnelCard==Card&&Card->bControlPanelOpen&&Card->ControlPanelSquadId==SquadId
        &&(!IsValid(VehiclePanel)||!VehiclePanel->bPanelOpen))return true;
    if(!CloseActivePanelInternal(Revision)||!bPresentationActive||!IsValid(Card)
        ||!PersonnelCards.Contains(Card)||Card->GetParent()!=MemberList||!Card->IsPresentationConstructed()
        ||Card->Member.UnitId!=UnitId||CurrentSquadId!=SquadId)return false;
    ActivePersonnelCard=Card;
    Card->OpenControlPanel(SquadId);
    return bPresentationActive&&Revision==PanelRevision&&ActivePersonnelCard==Card&&Card->bControlPanelOpen;
}

void UBattleMemberModeWidget::BindVehiclePanel(UBattleVehiclePanelWidget* Panel,bool bOwned)
{
    if(IsValid(VehiclePanel)&&VehiclePanel!=Panel)
    {
        VehiclePanel->OnPresentationReleased.RemoveAll(this);
        VehiclePanel->OnLayoutAnimationFinished.RemoveAll(this);
    }
    VehiclePanel=Panel;
    bOwnsVehiclePanel=bOwned;
    if(IsValid(Panel))
    {
        // Reconstructing Slate or rediscovering an existing instance must not accumulate listeners.
        Panel->OnPresentationReleased.RemoveAll(this);
        Panel->OnPresentationReleased.AddUObject(this,&ThisClass::HandleVehicleReleased);
        Panel->OnLayoutAnimationFinished.RemoveAll(this);
        Panel->OnLayoutAnimationFinished.AddUObject(this,&ThisClass::HandleVehicleLayoutAnimationFinished);
    }
    CacheVehicleScrollContext();
}

void UBattleMemberModeWidget::CacheVehicleScrollContext()
{
    bVehicleInMemberScrollBox=false;
    if(!IsValid(VehiclePanel)||!MemberScrollBox)return;
    // Resolve ancestry only when binding/attaching, never on button clicks or Tick.
    for(UWidget* Parent=VehiclePanel->GetParent();Parent;Parent=Parent->GetParent())
        if(Parent==MemberScrollBox)
        {
            bVehicleInMemberScrollBox=true;
            break;
        }
}

bool UBattleMemberModeWidget::EnsureVehiclePanel(uint64 Revision)
{
    // BindWidget supplies the Designer instance directly, independent of its parent container.
    if(IsValid(VehiclePanel)&&IsValid(VehiclePanel->GetParent()))return true;
    // Compatibility for native-created test/game layouts without a Designer template.
    if(!IsValid(VehiclePanelContainer)||!VehiclePanelClass
        ||VehiclePanelClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))return false;
    if(IsValid(VehiclePanel))
    {
        UBattleVehiclePanelWidget* Previous=VehiclePanel;
        const bool bRemovePrevious=bOwnsVehiclePanel;
        VehiclePanel=nullptr;
        bOwnsVehiclePanel=false;
        Previous->OnPresentationReleased.RemoveAll(this);
        Previous->OnLayoutAnimationFinished.RemoveAll(this);
        Previous->ClosePanel();
        if(!bPresentationActive||Revision!=PanelRevision)return false;
        if(bRemovePrevious)Previous->RemoveFromParent();
    }
    auto* Created=CreateWidget<UBattleVehiclePanelWidget>(this,VehiclePanelClass);
    if(!Created)return false;
    Created->SetVisibility(ESlateVisibility::Collapsed);
    BindVehiclePanel(Created,true);
    VehiclePanelContainer->AddChild(Created);
    const bool bAttached=bPresentationActive&&Revision==PanelRevision&&VehiclePanel==Created&&Created->GetParent()==VehiclePanelContainer;
    if(bAttached)CacheVehicleScrollContext();
    return bAttached;
}

bool UBattleMemberModeWidget::ShowVehiclePanel()
{
    if(!bPresentationActive||bChangingPanel||!CurrentSquadId.IsValid()||!CurrentVehicle.VehicleId.IsValid())return false;
    TGuardValue<bool> Guard(bChangingPanel,true);
    const uint64 Revision=++PanelRevision;
    const FGuid SquadId=CurrentSquadId, VehicleId=CurrentVehicle.VehicleId;
    if(!EnsureVehiclePanel(Revision))return false;
    UBattleVehiclePanelWidget* Panel=VehiclePanel;
    OnPanelOpening.Broadcast(FGuid());
    if(!bPresentationActive||Revision!=PanelRevision||!IsValid(Panel)||VehiclePanel!=Panel
        ||!IsValid(Panel->GetParent())||CurrentSquadId!=SquadId||CurrentVehicle.VehicleId!=VehicleId)return false;
    const bool bAlreadyOpen=ActivePersonnelCard==nullptr&&Panel->bPanelOpen
        &&Panel->SquadId==SquadId&&Panel->Vehicle.VehicleId==VehicleId;
    if(!bAlreadyOpen)
    {
        if(!CloseActivePanelInternal(Revision)||!bPresentationActive||!IsValid(Panel)||VehiclePanel!=Panel
            ||!IsValid(Panel->GetParent())||CurrentSquadId!=SquadId||CurrentVehicle.VehicleId!=VehicleId)return false;
        if(VehicleButton)VehicleButton->SetSelected(true);
        Panel->OpenPanel(SquadId,CurrentVehicle);
    }
    else if(VehicleButton)VehicleButton->SetSelected(true);
    const bool bOpened=bPresentationActive&&Revision==PanelRevision&&IsValid(Panel)&&VehiclePanel==Panel&&Panel->bPanelOpen;
    if(bOpened&&bVehicleInMemberScrollBox&&MemberScrollBox)
        MemberScrollBox->ScrollWidgetIntoView(Panel,true,EDescendantScrollDestination::IntoView);
    return bOpened;
}

void UBattleMemberModeWidget::HandlePersonnelPanelRequested(UBattlePersonnelCardWidget* Card,FGuid UnitId)
{
    if(IsValid(Card)&&Card->Member.UnitId==UnitId)ShowPersonnelPanel(Card);
}

void UBattleMemberModeWidget::HandlePersonnelReleased(UBattlePersonnelCardWidget* Card)
{
    PersonnelCards.Remove(Card);
    Card->OnControlPanelRequested.RemoveDynamic(this,&ThisClass::HandlePersonnelPanelRequested);
    Card->OnPresentationReleased.RemoveAll(this);
    Card->OnMemberContextChanged.RemoveAll(this);
    if(ActivePersonnelCard==Card)CloseActivePanel();
}

void UBattleMemberModeWidget::HandlePersonnelContextChanged(UBattlePersonnelCardWidget* Card)
{
    if(ActivePersonnelCard==Card)CloseActivePanel();
}

void UBattleMemberModeWidget::HandleVehicleReleased(UBattleVehiclePanelWidget* Panel)
{
    Panel->OnPresentationReleased.RemoveAll(this);
    Panel->OnLayoutAnimationFinished.RemoveAll(this);
    if(VehiclePanel!=Panel)return;
    // Slate teardown does not remove a Designer child from its UMG parent. Retain both its identity
    // and the ownership flag until our own teardown decides whether a dynamic child needs removal.
    ++PanelRevision;
    if(VehicleButton)VehicleButton->SetSelected(false);
}

void UBattleMemberModeWidget::HandleVehicleLayoutAnimationFinished(UBattleVehiclePanelWidget* Panel)
{
    if(!bPresentationActive||bChangingPanel||Panel!=VehiclePanel||!IsValid(Panel)||!Panel->bPanelOpen
        ||!bVehicleInMemberScrollBox||!MemberScrollBox)return;
    // Width animations change the scroll extent after the initial click. Slate handles this
    // request on its next layout/tick using the finished size, without polling or a fixed delay.
    MemberScrollBox->ScrollWidgetIntoView(Panel,true,EDescendantScrollDestination::IntoView);
}

void UBattleMemberModeWidget::HandleVehicleClicked()
{
    if(!bPresentationActive||bChangingPanel)return;
    if(IsValid(VehiclePanel)&&VehiclePanel->bPanelOpen&&IsValid(VehiclePanel->GetParent()))
        CloseActivePanel();
    else ShowVehiclePanel();
}

void UBattleMemberModeWidget::RefreshListStyle()
{
    BattleModeListStyle::Apply(SquadScrollBox, ListStyle, Orient_Vertical);
    BattleModeListStyle::Apply(MemberScrollBox, ListStyle, Orient_Horizontal, true);
    // Inventory slot labels can opt out of ordinary ancestor clipping. Keep
    // every member-list child inside this viewport, including those labels.
    if(MemberScrollBox)MemberScrollBox->SetClipping(EWidgetClipping::ClipToBoundsAlways);
}

void UBattleSquadModeWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    RefreshListStyle();
}

void UBattleSquadModeWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RefreshListStyle();
}

void UBattleSquadModeWidget::RefreshListStyle()
{
    BattleModeListStyle::Apply(SquadScrollBox, ListStyle, Orient_Vertical);
    BattleModeListStyle::Apply(SquadCardScrollBox, ListStyle, Orient_Horizontal);
}
