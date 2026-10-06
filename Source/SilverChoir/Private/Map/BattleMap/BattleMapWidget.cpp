#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleHUDLibrary.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleVehiclePanelWidget.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattleModeScrollBox.h"
#include "Map/BattleMap/BattleRosterLibrary.h"
#include "Map/BattleMap/BattleSquadEntryWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace BattleHUD
{
FGuid Id(FName Name){FGuid R;FGuid::Parse(Name.ToString(),R);return R;}
void Place(UWidget* W, float X, float Y, float Width, float Height)
{if(W)if(auto* S=Cast<UCanvasPanelSlot>(W->Slot)){S->SetPosition({X,Y});S->SetSize({Width,Height});}}
FText Text(const FString& V){return FText::FromString(V);}
}
const FBattleSquadView* UBattleMapWidget::CurrentSquad() const
{return Squads.FindByPredicate([this](const auto& S){return S.SquadId==SelectedSquadId;});}
FBattleMemberView* UBattleMapWidget::MutableMember(FGuid Id)
{for(auto& S:Squads)for(auto& M:S.Members)if(M.UnitId==Id)return &M;return nullptr;}
bool UBattleMapWidget::GetMemberView(FGuid Id,FBattleMemberView& Out) const
{for(const auto& S:Squads)for(const auto& M:S.Members)if(M.UnitId==Id){Out=M;return true;}Out={};return false;}
bool UBattleMapWidget::InitializeTestHUD(UBattleHUDTestData* Data)
{return IsValid(Data) && SetBattleSquads(Data->Squads,true);}
bool UBattleMapWidget::InitializeBattleUI(const TArray<FGuid>& SquadIds,FText& OutError)
{
    OutError=FText::GetEmpty();
    if(bEndingPresentation||bInitializingRoster||bUpdatingRoster)
    {OutError=NSLOCTEXT("BattleUI","UpdatingRoster","正在更新战斗UI小队列表。");return false;}
    TGuardValue<bool> InitializingGuard(bInitializingRoster,true);
    TArray<FBattleSquadView> Resolved;
    if(!UBattleRosterLibrary::ResolveSquadViews(this,SquadIds,Resolved,OutError))return false;
    if(!MemberModePanel||!MemberModePanel->SquadList||!MemberModePanel->MemberList
        ||!SquadModePanel||!SquadModePanel->SquadList)
    {OutError=NSLOCTEXT("BattleUI","MissingRosterPanels","战斗UI未绑定成员、小队模式的列表容器。");return false;}
    if(!Resolved.IsEmpty()&&(!SquadEntryClass||!MemberCardClass
        ||SquadEntryClass->HasAnyClassFlags(CLASS_Abstract)||MemberCardClass->HasAnyClassFlags(CLASS_Abstract)))
    {OutError=NSLOCTEXT("BattleUI","MissingRosterClasses","请配置有效的小队项和人员卡片控件类。");return false;}
    const FGuid Previous=SelectedSquadId;
    const bool bWasInitialized=bRosterInitialized;
    bRosterInitialized=true;
    if(!SetBattleSquads(Resolved,false))
    {
        bRosterInitialized=bWasInitialized;
        OutError=NSLOCTEXT("BattleUI","InvalidRoster","小队展示数据无效，界面保留原有列表。");
        return false;
    }
    TArray<FGuid> AcceptedIds;
    for(const auto& Squad:Squads)AcceptedIds.Add(Squad.SquadId);
    // The full presentation is committed before Blueprint receives either event.
    TGuardValue<bool> UpdatingGuard(bUpdatingRoster,true);
    OnBattleUIInitialized(AcceptedIds);
    if(Previous!=SelectedSquadId)OnBattleSquadSelected(Previous,SelectedSquadId);
    return true;
}
bool UBattleMapWidget::SetBattleSquads(const TArray<FBattleSquadView>& Data,bool bTestData)
{
    if(bEndingPresentation||bUpdatingRoster)return false;
    TGuardValue<bool> UpdatingGuard(bUpdatingRoster,true);
    const uint64 Revision=PresentationRevision;
    TSet<FGuid> SquadIds,UnitIds;
    for(const auto& S:Data)
    {
        if(!S.SquadId.IsValid()||SquadIds.Contains(S.SquadId)||S.Members.Num()>128)return false;
        SquadIds.Add(S.SquadId);
        for(const auto& M:S.Members){if(!M.UnitId.IsValid()||UnitIds.Contains(M.UnitId))return false;UnitIds.Add(M.UnitId);}
    }
    CancelTargeting();CloseModal();Squads=Data;bUsingTestData=bTestData;
    for(auto& S:Squads)
    {
        S.Vehicle.Condition=UBattleHUDLibrary::SanitizeRatio(S.Vehicle.Condition);S.Vehicle.Fuel=UBattleHUDLibrary::SanitizeRatio(S.Vehicle.Fuel);
        S.Vehicle.Seats=FMath::Max(0,S.Vehicle.Seats);S.Vehicle.Occupants=FMath::Clamp(S.Vehicle.Occupants,0,S.Vehicle.Seats);
        for(auto& M:S.Members)
        {
            M.Health=UBattleHUDLibrary::SanitizeRatio(M.Health);M.Stamina=UBattleHUDLibrary::SanitizeRatio(M.Stamina);M.Morale=UBattleHUDLibrary::SanitizeRatio(M.Morale);
            if(!FMath::IsFinite(M.MapPosition.X)||!FMath::IsFinite(M.MapPosition.Y))M.MapPosition=FVector2D(.5,.5);
            M.QuickItemCounts.SetNum(4);for(auto& N:M.QuickItemCounts)N=FMath::Max(0,N);
        }
    }
    if(!SquadIds.Contains(SelectedSquadId))SelectedSquadId=Squads.IsEmpty()?FGuid():Squads[0].SquadId;
    SelectedMemberId.Invalidate();ActiveDrawer=EBattleDrawer::None;DrawerAmount=0;
    RebuildRoster();return !bEndingPresentation&&Revision==PresentationRevision;
}
void UBattleMapWidget::NativeConstruct()
{
    bEndingPresentation=false;
    const uint64 Revision=++PresentationRevision;
    Super::NativeConstruct();SetIsFocusable(true);
    if(bEndingPresentation||Revision!=PresentationRevision)return;
    CommandButtons.Reset();
    if(WidgetTree)WidgetTree->ForEachWidget([this](UWidget* W){if(auto* B=Cast<UBattleHUDButton>(W))
    {
        if(MemberModePanel&&B==MemberModePanel->VehicleButton)return;
        B->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleChoice);CommandButtons.Add(B);
    }});
    RebuildRoster();
    if(bEndingPresentation||Revision!=PresentationRevision)return;
    RefreshViews();ApplyControlMode(false);
}
void UBattleMapWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    ApplyControlMode(false);
}
void UBattleMapWidget::ApplyControlMode(bool bAnimate)
{
    const bool bMember=CurrentControlMode==EBattleControlMode::Member;
    if(MemberModeButton)MemberModeButton->SetSelected(bMember);
    if(SquadModeButton)SquadModeButton->SetSelected(!bMember);
    // Keep both pages in the layout, but prevent navigation/clicks from reaching
    // controls on the inactive page (including while the viewport is sliding).
    if(MemberModePanel)MemberModePanel->SetIsEnabled(bMember);
    if(SquadModePanel)SquadModePanel->SetIsEnabled(!bMember);
    UWidget* Page=bMember?static_cast<UWidget*>(MemberModePanel.Get()):static_cast<UWidget*>(SquadModePanel.Get());
    if(ModePages&&Page)
    {
        const float Speed=FMath::IsFinite(ModeSwitchInterpolationSpeed)?FMath::Clamp(ModeSwitchInterpolationSpeed,1.f,60.f):18.f;
        ModePages->SetScrollAnimationInterpolationSpeed(Speed);
        ModePages->ScrollWidgetIntoView(Page,bAnimate,EDescendantScrollDestination::TopOrLeft,0.f);
    }
}
bool UBattleMapWidget::SetControlMode(EBattleControlMode Mode,bool bAnimate)
{
    if(bEndingPresentation)return false;
    if((Mode!=EBattleControlMode::Member&&Mode!=EBattleControlMode::Squad)||!ModePages||!MemberModePanel||!SquadModePanel)return false;
    if(CurrentControlMode==Mode)return true;
    if(bSwitchingControlMode)return false;
    TGuardValue<bool> SwitchingGuard(bSwitchingControlMode,true);
    const uint64 Revision=PresentationRevision;
    if(MemberModePanel)MemberModePanel->CloseActivePanel();
    if(Revision!=PresentationRevision)return false;
    const auto Previous=CurrentControlMode;
    CurrentControlMode=Mode;
    CancelTargeting();CloseModal();SetDrawer(EBattleDrawer::None);
    ApplyControlMode(bAnimate);
    RefreshViews();
    if(bEndingPresentation||Revision!=PresentationRevision)return false;
    OnControlModeChanged(Previous,Mode);
    return true;
}
void UBattleMapWidget::NativeDestruct()
{
    bEndingPresentation=true;
    ++PresentationRevision;
    if(MemberModePanel)
    {
        MemberModePanel->OnPanelOpening.RemoveAll(this);
        MemberModePanel->ResetPersonnelCards();
    }
    CancelTargeting();CloseModal();
    for(auto B:CommandButtons)if(B){B->CancelPendingClick();B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleChoice);}
    for(auto B:SquadButtons)if(B){B->CancelPendingClick();B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleSquadChoice);}
    for(auto B:SquadEntries)if(B){B->CancelPendingClick();B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleSquadChoice);}
    for(auto C:MemberCards)if(C)
    {
        C->CancelPendingClick();C->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleMemberChoice);
    }
    CommandButtons.Reset();Super::NativeDestruct();
}
void UBattleMapWidget::RebuildRoster(bool bRebuildSquads)
{
    if(bEndingPresentation)return;
    TGuardValue<bool> UpdatingGuard(bUpdatingRoster,true);
    const uint64 Revision=PresentationRevision;
    if(MemberModePanel)
    {
        MemberModePanel->OnPanelOpening.RemoveAll(this);
        MemberModePanel->OnPanelOpening.AddUObject(this,&ThisClass::HandleControlPanelOpening);
        MemberModePanel->ResetPersonnelCards();
    }
    if(Revision!=PresentationRevision)return;
    for(auto C:MemberCards)if(C)
    {
        C->CancelPendingClick();C->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleMemberChoice);
        C->RemoveFromParent();
    }
    MemberCards.Reset();
    if(bRosterInitialized)
    {
        if(MemberModePanel)
        {
            const auto* Squad=CurrentSquad();
            MemberModePanel->SetSquadContext(SelectedSquadId,Squad?Squad->Vehicle:FBattleVehicleView());
            if(Revision!=PresentationRevision)return;
        }
        if(bRebuildSquads)
        {
            for(auto Entry:SquadEntries)if(Entry)
            {
                Entry->CancelPendingClick();
                Entry->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleSquadChoice);
                Entry->RemoveFromParent();
            }
            SquadEntries.Reset();
            for(UVerticalBox* List:{MemberModePanel?MemberModePanel->SquadList.Get():nullptr,
                SquadModePanel?SquadModePanel->SquadList.Get():nullptr})
            {
                if(!List)continue;
                List->ClearChildren();
                if(!SquadEntryClass)continue;
                for(int32 Index=0;Index<Squads.Num();++Index)
                {
                    auto* Entry=CreateWidget<UBattleSquadEntryWidget>(this,SquadEntryClass);
                    if(!Entry)continue;
                    Entry->SetSquad(Squads[Index],Index);
                    Entry->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleSquadChoice);
                    auto* EntrySlot=List->AddChildToVerticalBox(Entry);
                    EntrySlot->SetHorizontalAlignment(HAlign_Fill);
                    EntrySlot->SetPadding(FMargin(0,0,0,Index+1<Squads.Num()?3.f:0.f));
                    SquadEntries.Add(Entry);
                }
            }
        }
        if(MemberModePanel&&MemberModePanel->MemberList)
        {
            MemberModePanel->MemberList->ClearChildren();
            if(const auto* Squad=CurrentSquad();Squad&&MemberCardClass)
                for(const auto& Member:Squad->Members)
                {
                    auto* Card=CreateWidget<UBattleMemberCardWidget>(this,MemberCardClass);
                    if(!Card)continue;
                    Card->SetMember(Member);
                    if(auto* Personnel=Cast<UBattlePersonnelCardWidget>(Card))
                    {
                        MemberModePanel->RegisterPersonnelCard(Personnel);
                    }
                    else Card->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleMemberChoice);
                    auto* CardSlot=MemberModePanel->MemberList->AddChildToHorizontalBox(Card);
                    if(bEndingPresentation||Revision!=PresentationRevision)return;
                    if(!CardSlot)continue;
                    CardSlot->SetHorizontalAlignment(HAlign_Fill);
                    CardSlot->SetVerticalAlignment(VAlign_Fill);
                    MemberCards.Add(Card);
                }
            if(MemberModePanel->MemberScrollBox)MemberModePanel->MemberScrollBox->SetScrollOffset(0.f);
        }
        RefreshViews();
        return;
    }
    for(auto B:SquadButtons)if(B){B->CancelPendingClick();B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleSquadChoice);}SquadButtons.Reset();if(SquadRail)SquadRail->ClearChildren();
    if(SquadRail&&ButtonClass)for(int32 I=0;I<Squads.Num();++I)
    {
        auto* B=CreateWidget<UBattleHUDButton>(GetOwningPlayer(),ButtonClass);if(!B)continue;
        B->Glyph=EBattleGlyph::Squad;B->ChoiceID=FName(*Squads[I].SquadId.ToString());B->ButtonText=BattleHUD::Text(FString::Printf(TEXT("%02d"),I+1));B->SetToolTipText(Squads[I].Name);
        B->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleSquadChoice);
        auto* ChildSlot=SquadRail->AddChildToVerticalBox(B);ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ChildSlot->SetPadding(FMargin(0,0,0,I+1<Squads.Num()?4.f:0.f));SquadButtons.Add(B);
    }
    if(const auto* S=CurrentSquad();S&&DockPanel&&MemberCardClass)for(const auto& M:S->Members)
    {
        auto* C=CreateWidget<UBattleMemberCardWidget>(GetOwningPlayer(),MemberCardClass);if(!C)continue;
        C->SetMember(M);C->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleMemberChoice);DockPanel->AddChildToCanvas(C);MemberCards.Add(C);
    }
    // The legacy dock rebuild keeps Designer-authored member-mode cards in place.
    // Restore their routing after the common reset, including cards whose data will be assigned later.
    if(MemberModePanel&&MemberModePanel->MemberList)
        for(UWidget* Child:MemberModePanel->MemberList->GetAllChildren())
            if(auto* Card=Cast<UBattlePersonnelCardWidget>(Child))MemberModePanel->RegisterPersonnelCard(Card);
    LastWidth=0;RefreshViews();UpdateLayout(MaximumDockWidth);
}
void UBattleMapWidget::RefreshViews()
{
    if(bEndingPresentation||bRefreshingViews)return;
    TGuardValue<bool> RefreshGuard(bRefreshingViews,true);
    TGuardValue<bool> UpdatingGuard(bUpdatingRoster,true);
    const auto* S=CurrentSquad();
    if(SquadNameText)SquadNameText->SetText(S?S->Name:BattleHUD::Text(TEXT("暂无小队")));
    for(auto B:SquadButtons)B->SetSelected(BattleHUD::Id(B->ChoiceID)==SelectedSquadId);
    for(auto Entry:SquadEntries)if(Entry)Entry->SetSelected(Entry->SquadId==SelectedSquadId);
    for(auto C:MemberCards)
    {
        FBattleMemberView M;
        if(GetMemberView(C->Member.UnitId,M))
        {
            C->SetMember(M);
            const auto* PersonnelCard=Cast<UBattlePersonnelCardWidget>(C);
            if(!PersonnelCard||!PersonnelCard->UsesSharedUnitSelection())C->SetSelected(M.UnitId==SelectedMemberId);
        }
    }
    FBattleMemberView Selected;const bool HasMember=GetMemberView(SelectedMemberId,Selected);
    if(CommandMemberName)CommandMemberName->SetText(HasMember?Selected.Profile.CodeName:FText::GetEmpty());
    if(VehicleButton){VehicleButton->SetSelected(ActiveDrawer==EBattleDrawer::Vehicle);VehicleButton->SetIsEnabled(S&&S->Vehicle.VehicleId.IsValid());VehicleButton->SetToolTipText(BattleHUD::Text(S&&S->Vehicle.VehicleId.IsValid()?TEXT("查看小队车辆"):TEXT("该小队没有载具")));}
    if(S)
    {
        if(VehicleName)VehicleName->SetText(S->Vehicle.Name);
        if(VehicleImage){VehicleImage->SetBrushFromTexture(S->Vehicle.Image);VehicleImage->SetVisibility(S->Vehicle.Image?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
        if(VehicleStats)VehicleStats->SetText(BattleHUD::Text(FString::Printf(TEXT("车况  %d%%\n燃油  %d%%\n乘员  %d / %d"),FMath::RoundToInt(S->Vehicle.Condition*100),FMath::RoundToInt(S->Vehicle.Fuel*100),S->Vehicle.Occupants,S->Vehicle.Seats)));
    }
    for(auto B:CommandButtons)
    {
        const FName N=B->ChoiceID;
        if(N==TEXT("Stand"))B->SetSelected(HasMember&&Selected.Posture==EBattlePosture::Standing);
        if(N==TEXT("Crouch"))B->SetSelected(HasMember&&Selected.Posture==EBattlePosture::Crouched);
        if(N==TEXT("Prone"))B->SetSelected(HasMember&&Selected.Posture==EBattlePosture::Prone);
        if(N==TEXT("Stealth"))B->SetSelected(HasMember&&Selected.bStealth);
        if(N==TEXT("Interact")||N==TEXT("Pickup"))B->SetSelected(bTargeting&&StaticEnum<EBattleCommand>()->GetNameStringByValue(int64(TargetingCommand))==N.ToString());
        if(N==TEXT("MemberMode"))B->SetSelected(CurrentControlMode==EBattleControlMode::Member);
        if(N==TEXT("SquadMode"))B->SetSelected(CurrentControlMode==EBattleControlMode::Squad);
        if(N==TEXT("MapFollow"))B->SetSelected(bFollowMap);
        for(int32 I=0;I<4;++I)if(N==FName(*FString::Printf(TEXT("Quick%d"),I+1)))
        {const int Count=HasMember&&Selected.QuickItemCounts.IsValidIndex(I)?Selected.QuickItemCounts[I]:0;B->SetIsEnabled(Count>0);B->SetButtonText(BattleHUD::Text(FString::Printf(TEXT("%d · %d"),I+1,Count)));}
    }
    if(TacticalMap)
    {
        TacticalMap->Markers.Reset();TacticalMap->SelectedMarker=INDEX_NONE;
        if(S)for(int32 I=0;I<S->Members.Num();++I){TacticalMap->Markers.Add(S->Members[I].MapPosition);if(S->Members[I].UnitId==SelectedMemberId)TacticalMap->SelectedMarker=I;}
        TacticalMap->Center=bFollowMap&&HasMember?Selected.MapPosition:FVector2D(.5,.5);
    }
}
void UBattleMapWidget::SetDrawer(EBattleDrawer Drawer)
{
    if(ActiveDrawer!=Drawer){ActiveDrawer=Drawer;DrawerAmount=0.f;}
    for(auto B:CommandButtons)B->CancelPendingClick();
    LastWidth=0;RefreshViews();
}
void UBattleMapWidget::CloseDrawer()
{
    const uint64 Revision=PresentationRevision;
    if(MemberModePanel)MemberModePanel->CloseActivePanel();
    if(Revision!=PresentationRevision)return;
    CancelTargeting();SetDrawer(EBattleDrawer::None);
}
bool UBattleMapWidget::SelectSquad(FGuid Id)
{
    if(bEndingPresentation||bUpdatingRoster)return false;
    if(!Squads.ContainsByPredicate([&](const auto& S){return S.SquadId==Id;}))return false;
    if(SelectedSquadId==Id)return true;
    TGuardValue<bool> UpdatingGuard(bUpdatingRoster,true);
    const uint64 Revision=PresentationRevision;
    const FGuid Previous=SelectedSquadId;
    CancelTargeting();CloseModal();SelectedSquadId=Id;SelectedMemberId.Invalidate();SetDrawer(EBattleDrawer::None);
    RebuildRoster(false);
    if(bEndingPresentation||Revision!=PresentationRevision)return false;
    OnBattleSquadSelected(Previous,SelectedSquadId);
    return true;
}
bool UBattleMapWidget::SelectMember(FGuid Id)
{
    if(bEndingPresentation||bUpdatingRoster)return false;
    const auto* S=CurrentSquad();if(!S||!S->Members.ContainsByPredicate([&](const auto& M){return M.UnitId==Id;}))return false;
    if(bRosterInitialized&&MemberModePanel)
    {
        for(UBattleMemberCardWidget* Entry:MemberCards)
            if(auto* Personnel=Cast<UBattlePersonnelCardWidget>(Entry);Personnel&&Personnel->Member.UnitId==Id)
                return MemberModePanel->ShowPersonnelPanel(Personnel);
    }
    const bool Same=SelectedMemberId==Id&&ActiveDrawer==EBattleDrawer::Member;
    CancelTargeting();CloseModal();SelectedMemberId=Id;SetDrawer(Same?EBattleDrawer::None:EBattleDrawer::Member);return true;
}
bool UBattleMapWidget::ToggleVehicleDrawer()
{
    if(bEndingPresentation)return false;
    const auto* S=CurrentSquad();if(!S||!S->Vehicle.VehicleId.IsValid())return false;
    if(bRosterInitialized&&MemberModePanel&&CurrentControlMode==EBattleControlMode::Member)
        return MemberModePanel->ShowVehiclePanel();
    CancelTargeting();CloseModal();SetDrawer(ActiveDrawer==EBattleDrawer::Vehicle?EBattleDrawer::None:EBattleDrawer::Vehicle);return true;
}
void UBattleMapWidget::HandleSquadChoice(FName Choice){SelectSquad(BattleHUD::Id(Choice));}
void UBattleMapWidget::HandleMemberChoice(FName Choice){SelectMember(BattleHUD::Id(Choice));}
void UBattleMapWidget::HandleControlPanelOpening(FGuid UnitId)
{
    if(bEndingPresentation||bUpdatingRoster)return;
    const auto* Squad=CurrentSquad();
    if(UnitId.IsValid())
    {
        if(!Squad||!Squad->Members.ContainsByPredicate([&](const auto& Member){return Member.UnitId==UnitId;}))return;
        SelectedMemberId=UnitId;
    }
    CancelTargeting();CloseModal();
    // Authored member-mode panels own expansion. Keep the legacy canvas drawers closed.
    SetDrawer(EBattleDrawer::None);
}
void UBattleMapWidget::HandleChoice(FName Choice)
{
    if(Choice==TEXT("Vehicle")){ToggleVehicleDrawer();return;}
    if(Choice==TEXT("CloseDrawer")){CloseDrawer();return;}
    if(Choice==TEXT("CloseModal")){CloseModal();return;}
    const int64 V=StaticEnum<EBattleCommand>()->GetValueByNameString(Choice.ToString());
    if(V!=INDEX_NONE)RequestCommand(EBattleCommand(V));
}
void UBattleMapWidget::RequestCommand(EBattleCommand Command)
{
    if(Command==EBattleCommand::MemberMode){SetControlMode(EBattleControlMode::Member);return;}
    if(Command==EBattleCommand::SquadMode){SetControlMode(EBattleControlMode::Squad);return;}
    const int V=int(Command);if(V<0||V>int(EBattleCommand::MapCollapse))return;
    const auto* S=CurrentSquad();
    if(V<=int(EBattleCommand::Quick4))
    {FBattleMemberView M;if((ActiveDrawer!=EBattleDrawer::Member&&!(MemberModePanel&&MemberModePanel->ActivePersonnelCard))||!GetMemberView(SelectedMemberId,M))return;}
    if(V>=int(EBattleCommand::BoardVehicle)&&V<=int(EBattleCommand::LocateVehicle))
        if((ActiveDrawer!=EBattleDrawer::Vehicle&&!(MemberModePanel&&MemberModePanel->VehiclePanel&&MemberModePanel->VehiclePanel->bPanelOpen))||!S||!S->Vehicle.VehicleId.IsValid())return;
    LastRequestedCommand=Command;++RequestSerial;
    OnBattleCommandRequested(SelectedMemberId,SelectedSquadId,Command);
}
bool UBattleMapWidget::SetDisplayedPosture(FGuid Id,EBattlePosture Posture)
{
    if(int(Posture)>int(EBattlePosture::Prone))return false;
    if(auto* M=MutableMember(Id)){M->Posture=Posture;RefreshViews();return true;}return false;
}
bool UBattleMapWidget::ToggleDisplayedStealth(FGuid Id)
{if(auto* M=MutableMember(Id)){M->bStealth=!M->bStealth;RefreshViews();return true;}return false;}
bool UBattleMapWidget::UpdateMemberView(const FBattleMemberView& Data)
{
    auto* M=MutableMember(Data.UnitId);
    if(!M)return false;
    *M=Data;
    M->Health=UBattleHUDLibrary::SanitizeRatio(M->Health);
    M->Stamina=UBattleHUDLibrary::SanitizeRatio(M->Stamina);
    M->Morale=UBattleHUDLibrary::SanitizeRatio(M->Morale);
    if(!FMath::IsFinite(M->MapPosition.X)||!FMath::IsFinite(M->MapPosition.Y))M->MapPosition=FVector2D(.5,.5);
    M->QuickItemCounts.SetNum(4);
    for(auto& N:M->QuickItemCounts)N=FMath::Max(0,N);
    RefreshViews();return true;
}
void UBattleMapWidget::UpdateLayout(float Width)
{
    if(!DockPanel)return;
    const auto* S=CurrentSquad();int32 Selected=INDEX_NONE;
    if(S)Selected=S->Members.IndexOfByPredicate([this](const auto& M){return M.UnitId==SelectedMemberId;});
    const auto L=UBattleHUDLibrary::CalculateDockLayout(Width,MemberCards.Num(),Selected,ActiveDrawer,DrawerAmount,MemberDrawerWidth,VehicleDrawerWidth);
    BattleHUD::Place(SquadRail,0,0,40,DockHeight);
    for(int I=0;I<MemberCards.Num();++I)BattleHUD::Place(MemberCards[I],L.MemberX[I],0,L.MemberWidth,DockHeight);
    BattleHUD::Place(VehicleButton,L.VehicleButtonX,0,32,DockHeight);BattleHUD::Place(ModeRail,L.ModeButtonsX,0,40,DockHeight);
    if(MemberDrawerPanel){MemberDrawerPanel->SetVisibility(ActiveDrawer==EBattleDrawer::Member?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);BattleHUD::Place(MemberDrawerPanel,L.DrawerX,0,L.DrawerWidth,DockHeight);}
    if(VehicleDrawerPanel){VehicleDrawerPanel->SetVisibility(ActiveDrawer==EBattleDrawer::Vehicle?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);BattleHUD::Place(VehicleDrawerPanel,L.DrawerX,0,L.DrawerWidth,DockHeight);}
}
void UBattleMapWidget::NativeTick(const FGeometry& G,float Delta)
{
    Super::NativeTick(G,Delta);
    const float W=FMath::Max(200.f,FMath::Min(MaximumDockWidth,float(G.GetLocalSize().X)-32.f));
    const float Goal=ActiveDrawer==EBattleDrawer::None?0.f:1.f;
    const float New=FMath::FInterpConstantTo(DrawerAmount,Goal,Delta,1.f/FMath::Max(.01f,DrawerAnimationSeconds));
    if(!FMath::IsNearlyEqual(New,DrawerAmount)||W!=LastWidth)
    {
        DrawerAmount=New;LastWidth=W;
        if(DockPanel)if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(DockPanel->Slot)){CanvasSlot->SetSize({W,DockHeight});CanvasSlot->SetPosition({-W*.5f,-DockHeight-16.f});}
        UpdateLayout(W);
    }
    if(FeedbackRemaining>0){FeedbackRemaining-=Delta;if(FeedbackRemaining<=0&&FeedbackText)FeedbackText->SetVisibility(ESlateVisibility::Collapsed);}
}
void UBattleMapWidget::ShowFeedback(FText Message)
{if(FeedbackText){FeedbackText->SetText(Message);FeedbackText->SetVisibility(ESlateVisibility::HitTestInvisible);FeedbackRemaining=4.f;}}
void UBattleMapWidget::BeginTargeting(EBattleCommand Command)
{
    if((Command!=EBattleCommand::Interact&&Command!=EBattleCommand::Pickup)||!SelectedMemberId.IsValid())return;
    auto* PC=GetOwningPlayer();if(!PC)return;
    if(!bTargeting)SavedCursor=PC->CurrentMouseCursor;
    bTargeting=true;TargetingCommand=Command;PC->CurrentMouseCursor=EMouseCursor::Hand;SetKeyboardFocus();
    ShowFeedback(BattleHUD::Text(Command==EBattleCommand::Interact?TEXT("选择交互目标 · 右键 / Esc 取消"):TEXT("选择拾取物或队友 · 右键 / Esc 取消")));RefreshViews();
}
void UBattleMapWidget::CancelTargeting()
{
    if(bTargeting)
    {
        if(auto* PC=GetOwningPlayer())PC->CurrentMouseCursor=SavedCursor;
        bTargeting=false;FeedbackRemaining=0;
        if(FeedbackText)FeedbackText->SetVisibility(ESlateVisibility::Collapsed);
        RefreshViews();
    }
}
FReply UBattleMapWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& Event)
{
    if(bTargeting&&Event.GetEffectingButton()==EKeys::RightMouseButton){CancelTargeting();return FReply::Handled();}
    if(bTargeting&&Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        // Canvas decorations are hit-test invisible; never target through either HUD panel.
        if((DockPanel&&DockPanel->IsHovered())||(MinimapPanel&&MinimapPanel->IsHovered())||(ModalPanel&&ModalPanel->IsVisible()))return FReply::Handled();
        FHitResult Hit;auto* PC=GetOwningPlayer();
        if(PC&&PC->GetHitResultUnderCursor(ECC_Visibility,true,Hit)){const auto Cmd=TargetingCommand;const auto Id=SelectedMemberId;CancelTargeting();OnBattleTargetConfirmed(Id,Cmd,Hit);}
        else ShowFeedback(BattleHUD::Text(TEXT("此处没有可选目标")));
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(G,Event);
}
FReply UBattleMapWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& Event)
{
    // Preview reaches the HUD even when a child button owns keyboard focus.
    if(Event.GetKey()==EKeys::Escape)
    {
        if(bTargeting)CancelTargeting();
        else if(ModalPanel&&ModalPanel->IsVisible())CloseModal();
        else CloseDrawer();
        return FReply::Handled();
    }
    if(!Event.IsRepeat()&&!bTargeting
        &&(ActiveDrawer==EBattleDrawer::Member||(MemberModePanel&&MemberModePanel->ActivePersonnelCard))
        &&!(ModalPanel&&ModalPanel->IsVisible()))
    {
        const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four};
        for(int32 I=0;I<4;++I)if(Event.GetKey()==Keys[I])
        {RequestCommand(EBattleCommand(int32(EBattleCommand::Quick1)+I));return FReply::Handled();}
    }
    return Super::NativeOnPreviewKeyDown(G,Event);
}
void UBattleMapWidget::CloseModal(){if(ModalPanel)ModalPanel->SetVisibility(ESlateVisibility::Collapsed);}
void UBattleMapWidget::ShowTestBackpack(FGuid Id)
{
    FBattleMemberView M;if(!bUsingTestData||!GetMemberView(Id,M))return;
    if(ModalTitle)ModalTitle->SetText(BattleHUD::Text(M.Profile.CodeName.ToString()+TEXT(" · 测试背包")));
    if(ModalBody)ModalBody->SetText(BattleHUD::Text(FString::Printf(TEXT("医疗包 × %d      破片手雷 × %d\n烟雾弹 × %d      工具组 × %d\n\n此处为测试数据预览。\n正式库存入口在蓝图“背包”分支中接入。"),M.QuickItemCounts[0],M.QuickItemCounts[1],M.QuickItemCounts[2],M.QuickItemCounts[3])));
    if(ModalPanel)ModalPanel->SetVisibility(ESlateVisibility::Visible);SetKeyboardFocus();
}
void UBattleMapWidget::UseTestQuickItem(FGuid Id,int32 SlotIndex)
{
    auto* M=MutableMember(Id);if(!bUsingTestData||!M||!M->QuickItemCounts.IsValidIndex(SlotIndex)||M->QuickItemCounts[SlotIndex]<=0)return;
    --M->QuickItemCounts[SlotIndex];if(SlotIndex==0)M->Health=FMath::Min(1.f,M->Health+.25f);RefreshViews();ShowFeedback(BattleHUD::Text(FString::Printf(TEXT("测试：已使用快捷物品 %d"),SlotIndex+1)));
}
void UBattleMapWidget::ApplyTestVehicleCommand(EBattleCommand Command)
{
    if(!bUsingTestData)return;auto* S=Squads.FindByPredicate([this](const auto& V){return V.SquadId==SelectedSquadId;});if(!S||!S->Vehicle.VehicleId.IsValid())return;
    if(Command==EBattleCommand::BoardVehicle)S->Vehicle.Occupants=FMath::Min(S->Vehicle.Seats,S->Members.Num());
    else if(Command==EBattleCommand::LeaveVehicle)S->Vehicle.Occupants=0;
    else if(Command==EBattleCommand::VehicleCargo)
    {if(ModalTitle)ModalTitle->SetText(BattleHUD::Text(TEXT("小队车辆 · 测试货舱")));if(ModalBody)ModalBody->SetText(BattleHUD::Text(TEXT("补给箱 × 2\n维修工具 × 1\n\n正式货舱入口在蓝图车辆操作分支中接入。")));if(ModalPanel)ModalPanel->SetVisibility(ESlateVisibility::Visible);SetKeyboardFocus();}
    else if(Command==EBattleCommand::LocateVehicle)ShowFeedback(BattleHUD::Text(TEXT("测试车辆：实际相机定位在蓝图接入")));
    RefreshViews();
}
void UBattleMapWidget::ApplyMinimapCommand(EBattleCommand Command)
{
    if(!TacticalMap)return;
    if(Command==EBattleCommand::MapZoomIn)TacticalMap->Zoom=FMath::Min(4.f,TacticalMap->Zoom*1.25f);
    if(Command==EBattleCommand::MapZoomOut)TacticalMap->Zoom=FMath::Max(.5f,TacticalMap->Zoom/1.25f);
    if(Command==EBattleCommand::MapFollow)bFollowMap=!bFollowMap;
    if(Command==EBattleCommand::MapLayers)TacticalMap->bShowGrid=!TacticalMap->bShowGrid;
    if(Command==EBattleCommand::MapCollapse){bMinimapCollapsed=!bMinimapCollapsed;TacticalMap->SetVisibility(bMinimapCollapsed?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);if(MinimapPanel)if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(MinimapPanel->Slot))CanvasSlot->SetSize({256,bMinimapCollapsed?58.f:210.f});}
    RefreshViews();
}
