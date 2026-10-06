#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/WarehousePageSession.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "Components/SIS_PlayerInventoryManager.h"
#include "Framework/Application/SlateApplication.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Widget/SlotContainerByType/SIS_InventorySlotContainer.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

namespace
{
FText Txt(const FString& Value) { return FText::FromString(Value); }
void Write(UTextBlock* Block, const FString& Value) { if (Block) Block->SetText(Txt(Value)); }
}
bool UPersonnelPreparationRoomWidget::LoadPersonnelList(FName TileId)
{
    if (UnitManager) UnitManager->OnUnitDataChanged.RemoveDynamic(this,&ThisClass::HandleUnitChanged);
    ReleaseUnitBindings();
    CurrentTileId=TileId; bUseUnitStore=true; bInitialized=true;
    UnitManager=UPlayerUnitLibrary::GetPlayerUnitManager(this);
    if (UnitManager) UnitManager->OnUnitDataChanged.AddUniqueDynamic(this,&ThisClass::HandleUnitChanged);
    RefreshRoster();
    return IsValid(UnitManager);
}
FGuid UPersonnelPreparationRoomWidget::GetSelectedUnitId() const
{
    FGuid ID; FGuid::Parse(SelectedPersonnelID.ToString(),ID); return ID;
}
void UPersonnelPreparationRoomWidget::HandleUnitChanged(FGuid UnitId)
{
    if (!SharedUnits.Contains(UnitId)) RefreshRoster();
}
void UPersonnelPreparationRoomWidget::HandleSharedUnitChanged(FGuid UnitId) { RefreshRoster(); }
void UPersonnelPreparationRoomWidget::ReleaseUnitBindings()
{
    for (auto& Pair:SharedUnits)
        if (auto* Handle=UnitBindings.Find(Pair.Key)) Pair.Value->OnDataChanged.Remove(*Handle);
    UnitBindings.Reset(); SharedUnits.Reset();
}
void UPersonnelPreparationRoomWidget::BeginDestroy()
{
    ReleaseProfileData();
    ReleaseUnitBindings();
    if (UnitManager) UnitManager->OnUnitDataChanged.RemoveDynamic(this,&ThisClass::HandleUnitChanged);
    Super::BeginDestroy();
}
void UPersonnelPreparationRoomWidget::RebuildUnitViews()
{
    if (!bUseUnitStore) return;
    PersonnelData.Reset();
    if (!IsValid(UnitManager)) return;
    for (const FGuid ID:UnitManager->GetUnitDataIDs())
    {
        const auto Unit=UnitManager->GetUnitDataShared(ID);
        if (!Unit) continue;
        if (!SharedUnits.Contains(ID))
        {
            SharedUnits.Add(ID,Unit);
            UnitBindings.Add(ID,Unit->OnDataChanged.AddUObject(this,&ThisClass::HandleSharedUnitChanged));
        }
        FPersonnelViewData P;
        P.ID=FName(*ID.ToString());
        P.Name=Txt(Unit->Profile.LastName.ToString()+Unit->Profile.FirstName.ToString());
        P.Callsign=Unit->Profile.CodeName; P.Portrait=Unit->Profile.PortraitTexture;
        P.bStandby=!CurrentTileId.IsNone() && Unit->RuntimeData.TileId==CurrentTileId;
        P.Role=Unit->RuntimeData.TileId.IsNone()?Txt(TEXT("未分配位置")):FText::FromName(Unit->RuntimeData.TileId);
        PersonnelData.Add(P);
    }
}
void UPersonnelPreparationRoomWidget::NativeConstruct()
{
    Super::NativeConstruct();
    for (int32 Index=0;Index<5;++Index)
        if (auto* Button=Cast<USelectionButtonWidget>(GetWidgetFromName(FName(*FString::Printf(TEXT("InventoryTab%02d"),Index+1)))))
            Button->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleInventoryPage);
    SelectInventoryPage(CurrentInventoryPage);
    if (!bInitialized) { PersonnelData.Reset(); bInitialized=true; }
    if (bUseUnitStore) LoadPersonnelList(CurrentTileId);
    for (auto* B : {StandbyFilter.Get(),AllFilter.Get()})
        if (B) B->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandleFilter);
    for (auto* B : {ProfileTab.Get(),EquipmentTab.Get(),TrainingTab.Get()})
        if (B) B->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandlePage);
    RefreshRoster(); SetDetailPage(CurrentPage);
}
void UPersonnelPreparationRoomWidget::NativeDestruct()
{
    if (WarehouseSession) { WarehouseSession->Close(); WarehouseSession=nullptr; }
    for (int32 Index=0;Index<5;++Index)
        if (auto* Button=Cast<USelectionButtonWidget>(GetWidgetFromName(FName(*FString::Printf(TEXT("InventoryTab%02d"),Index+1)))))
            Button->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleInventoryPage);
    ReleaseProfileData();
    ReleaseUnitBindings();
    for (auto& Row:Rows) if (auto* Entry=Cast<UPersonnelListEntryWidget>(Row)) Entry->ReleaseUnitData();
    if (UnitManager) UnitManager->OnUnitDataChanged.RemoveDynamic(this,&ThisClass::HandleUnitChanged);
    CancelListTransition();
    for (auto* B : {StandbyFilter.Get(),AllFilter.Get()})
        if (B) B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandleFilter);
    for (auto* B : {ProfileTab.Get(),EquipmentTab.Get(),TrainingTab.Get()})
        if (B) B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandlePage);
    for (auto& B:Rows) if (B) B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandlePerson);
    Super::NativeDestruct();
}

void UPersonnelPreparationRoomWidget::HandleInventoryPage(FName ChoiceID)
{
    for (int32 Index=0;Index<5;++Index)
        if (ChoiceID==FName(*FString::Printf(TEXT("Inventory%02d"),Index+1))) { SelectInventoryPage(Index); return; }
}
bool UPersonnelPreparationRoomWidget::InitializeWarehouseSession()
{
    auto* Camera=UPlayerLibrary::GetPlayerCamera(this);
    auto* PC=GetOwningPlayer();
    if (!Camera || !PC) return false;
    if (!WarehouseSession) WarehouseSession=NewObject<UWarehousePageSession>(this);
    if (!WarehouseSession->Initialize(UPlayerLibrary::GetPlayerManager(this),Camera->UnitInventoryComponent,
        PC->FindComponentByClass<USIS_PlayerInventoryManager>(),GetWarehouseInventoryContainer())) return false;
    RestoreInventoryPageSelection(WarehouseSession->GetCurrentPage());
    return true;
}

void UPersonnelPreparationRoomWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry,InDeltaTime);
    if (!WarehouseSession) return;
    WarehouseSession->Update();
    if (bClosing || bListTransitioning || !WarehousePanel || !WarehousePanel->IsVisible() ||
        !GetIsEnabled() || !WarehouseSession->IsDragging() || !FSlateApplication::IsInitialized())
    {
        WarehouseSession->ResetHover(); return;
    }
    const FVector2D MousePosition=FSlateApplication::Get().GetCursorPos();
    int32 Hovered=INDEX_NONE;
    for (int32 Index=0;Index<5;++Index)
        if (auto* Button=GetWidgetFromName(FName(*FString::Printf(TEXT("InventoryTab%02d"),Index+1)));
            Button && Button->IsVisible() && Button->GetIsEnabled() && Button->GetCachedGeometry().IsUnderLocation(MousePosition))
        { Hovered=Index; break; }
    if (WarehouseSession->UpdateHover(Hovered,MousePosition,FPlatformTime::Seconds(),DragPageHoverSeconds,DragPageCursorTolerance))
        SelectInventoryPage(Hovered);
}

bool UPersonnelPreparationRoomWidget::SelectInventoryPage(int32 PageIndex)
{
    auto* Container=GetWarehouseInventoryContainer();
    if (!Container || PageIndex<0 || PageIndex>=5) return false;
    const int32 PreviousPageIndex=CurrentInventoryPage;
    const bool bChanged=PreviousPageIndex!=PageIndex;
    if (bChanged && (!InitializeWarehouseSession() || !WarehouseSession->SwitchPage(PageIndex))) return false;
    CurrentInventoryPage=PageIndex;
    for (int32 Index=0;Index<5;++Index)
        if (auto* Button=Cast<USelectionButtonWidget>(GetWidgetFromName(FName(*FString::Printf(TEXT("InventoryTab%02d"),Index+1))))) Button->SetSelected(Index==PageIndex);
    if (auto* Title=Cast<UTextBlock>(GetWidgetFromName(TEXT("InventoryPageTitle"))))
        Title->SetText(FText::Format(NSLOCTEXT("PersonnelWarehouse","Title","库存 / {0}"),FText::FromString(FString::Printf(TEXT("%02d"),PageIndex+1))));
    if (bChanged) OnInventoryPageChanged(PageIndex,Container,PreviousPageIndex);
    return bChanged;
}
bool UPersonnelPreparationRoomWidget::RestoreInventoryPageSelection(int32 PageIndex)
{
    if (PageIndex<0 || PageIndex>=5 || !GetWarehouseInventoryContainer()) return false;
    CurrentInventoryPage=PageIndex;
    SelectInventoryPage(PageIndex);
    return true;
}
void UPersonnelPreparationRoomWidget::LoadSceneUI_Implementation()
{
    InitializeWarehouseSession();
    bClosing=false; bListAnimationFinished=false; bDetailAnimationFinished=false; bPanelCompletionReported=false;
    SetIsEnabled(true);
    RefreshRoster(); SetDetailPage(CurrentPage);
    Super::LoadSceneUI_Implementation();
}
void UPersonnelPreparationRoomWidget::UnloadSceneUI_Implementation()
{
    if (WarehouseSession) { WarehouseSession->Close(); WarehouseSession=nullptr; }
    bClosing=true; bListAnimationFinished=false; bDetailAnimationFinished=false; bPanelCompletionReported=false;
    CancelListTransition();
    StopAllAnimations();
    SetIsEnabled(false);
    for (auto* B:{StandbyFilter.Get(),AllFilter.Get(),ProfileTab.Get(),EquipmentTab.Get(),TrainingTab.Get()}) if (B) B->CancelPendingClick();
    for (auto& B:Rows) if (B) { B->CancelPendingClick(); B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandlePerson); }
    Super::UnloadSceneUI_Implementation();
}
void UPersonnelPreparationRoomWidget::SetPersonnelData(const TArray<FPersonnelViewData>& Data)
{
    if (UnitManager) UnitManager->OnUnitDataChanged.RemoveDynamic(this,&ThisClass::HandleUnitChanged);
    ReleaseUnitBindings();
    bUseUnitStore=false;
    PersonnelData.Reset();
    TSet<FName> Seen;
    for (const auto& P:Data) if (!P.ID.IsNone() && !Seen.Contains(P.ID)) { Seen.Add(P.ID); PersonnelData.Add(P); }
    bInitialized=true; RefreshRoster();
}
bool UPersonnelPreparationRoomWidget::MatchesFilter(const FPersonnelViewData& P) const
{
    return CurrentFilter==EPersonnelRosterFilter::All || P.bStandby;
}
int32 UPersonnelPreparationRoomWidget::GetVisiblePersonnelCount() const
{
    int32 Count=0; for (const auto& P:PersonnelData) if (MatchesFilter(P)) ++Count; return Count;
}
void UPersonnelPreparationRoomWidget::SetRosterFilter(EPersonnelRosterFilter Filter)
{
    if (Filter!=EPersonnelRosterFilter::All && Filter!=EPersonnelRosterFilter::Standby) return;
    CurrentFilter=Filter; RefreshRoster();
}
void UPersonnelPreparationRoomWidget::RefreshRoster()
{
    RebuildUnitViews();
    if (!PersonnelList) return;
    for (auto& B:Rows) if (B) { B->CancelPendingClick(); B->OnSelectionRequested.RemoveDynamic(this,&ThisClass::HandlePerson); }
    for (auto& Row:Rows) if (auto* Entry=Cast<UPersonnelListEntryWidget>(Row)) Entry->ReleaseUnitData();
    Rows.Reset(); PersonnelList->ClearChildren();
    FName First; bool bFound=false;
    for (const auto& P:PersonnelData)
    {
        if (!MatchesFilter(P)) continue;
        if (P.bStandby && First.IsNone()) First=P.ID;
        if (P.bStandby && SelectedPersonnelID==P.ID) bFound=true;
        if (!PersonnelRowClass || !GetOwningPlayer()) continue;
        auto* Row=CreateWidget<USelectionButtonWidget>(GetOwningPlayer(),PersonnelRowClass);
        if (!Row) continue;
        Row->ChoiceID=P.ID; Row->ButtonIndex=FText::GetEmpty();
        if (auto* Entry=Cast<UPersonnelListEntryWidget>(Row))
        {
            FGuid ID;
            if (bUseUnitStore && FGuid::Parse(P.ID.ToString(),ID)) Entry->SetUnitData(SharedUnits.FindRef(ID),CurrentTileId);
            else Entry->SetPortraitTexture(P.Portrait);
        }
        Row->ButtonText=Txt(P.Callsign.ToString()+TEXT("  /  ")+P.Name.ToString());
        Row->ButtonSubtitle=Txt(P.Role.ToString()+TEXT("  ·  ")+(P.bStandby?TEXT("待命"):TEXT("异地")));
        Row->SetIsEnabled(P.bStandby);
        // Filters may rebuild during Slate tick, after this frame's normal prepass.
        // Construct and measure each row before the scroll box can arrange/paint it.
        Row->TakeWidget();
        Row->SynchronizeProperties();
        Row->ForceLayoutPrepass();
        Row->OnSelectionRequested.AddUniqueDynamic(this,&ThisClass::HandlePerson);
        auto* RowSlot=Cast<UScrollBoxSlot>(PersonnelList->AddChild(Row)); if (RowSlot) RowSlot->SetPadding(FMargin(0,0,8,8));
        Rows.Add(Row);
    }
    const FName Old=SelectedPersonnelID;
    if (!bFound) SelectedPersonnelID=First;
    for (auto& Row:Rows) Row->SetSelected(Row->ChoiceID==SelectedPersonnelID);
    if (StandbyFilter) StandbyFilter->SetSelected(CurrentFilter==EPersonnelRosterFilter::Standby);
    if (AllFilter) AllFilter->SetSelected(CurrentFilter==EPersonnelRosterFilter::All);
    Write(RosterCount,FString::Printf(TEXT("人员名册   %02d / %02d"),GetVisiblePersonnelCount(),PersonnelData.Num()));
    if (EmptyRoster) EmptyRoster->SetVisibility(GetVisiblePersonnelCount()==0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    Write(DataSourceLabel,TEXT("人员数据库 / PERSONNEL DATABASE"));
    PersonnelList->InvalidateLayoutAndVolatility();
    PersonnelList->ForceLayoutPrepass();
    if (Old!=SelectedPersonnelID) OnPersonnelSelected.Broadcast(SelectedPersonnelID);
}
bool UPersonnelPreparationRoomWidget::GetSelectedPersonnel(FPersonnelViewData& Data) const
{
    for (const auto& P:PersonnelData) if (P.ID==SelectedPersonnelID && !SelectedPersonnelID.IsNone()) { Data=P; return true; }
    Data=FPersonnelViewData(); return false;
}
bool UPersonnelPreparationRoomWidget::SelectPersonnel(FName ID)
{
    if (bClosing) return false;
    const auto* P=PersonnelData.FindByPredicate([&](const FPersonnelViewData& Entry){ return Entry.ID==ID && Entry.bStandby && MatchesFilter(Entry); });
    if (!P) return false;
    // Recheck authoritative location as well, including delayed/programmatic clicks.
    if (bUseUnitStore)
    {
        FGuid UnitId;
        if (!IsValid(UnitManager) || !FGuid::Parse(ID.ToString(),UnitId)) return false;
        const auto Unit=UnitManager->GetUnitDataShared(UnitId);
        if (!Unit || CurrentTileId.IsNone() || Unit->RuntimeData.TileId!=CurrentTileId) return false;
    }
    if (SelectedPersonnelID==ID) return true;
    SelectedPersonnelID=ID;
    for (auto& Row:Rows) Row->SetSelected(Row->ChoiceID==ID);
    OnPersonnelSelected.Broadcast(ID); return true;
}
bool UPersonnelPreparationRoomWidget::SelectUnitById(FGuid UnitId)
{
    if (!UnitId.IsValid() || !SelectPersonnel(FName(*UnitId.ToString()))) return false;
    OnPersonnelClicked(UnitId);
    return true;
}
void UPersonnelPreparationRoomWidget::SetDetailPage(EPersonnelDetailPage Page)
{
    if (bClosing || uint8(Page)>uint8(EPersonnelDetailPage::Training)) return;
    CurrentPage=Page;
    RequestedListDisplay=Page==EPersonnelDetailPage::Equipment?EPersonnelListDisplay::Warehouse:EPersonnelListDisplay::Personnel;
    if (!bListTransitioning)
    {
        if (RequestedListDisplay!=CurrentListDisplay) StartListTransition();
        else ApplyListVisibility();
    }
    if (DetailPages) DetailPages->SetActiveWidgetIndex(int32(Page));
    if (ProfileTab) ProfileTab->SetSelected(Page==EPersonnelDetailPage::Profile);
    if (EquipmentTab) EquipmentTab->SetSelected(Page==EPersonnelDetailPage::Equipment);
    if (TrainingTab) TrainingTab->SetSelected(Page==EPersonnelDetailPage::Training);
}
void UPersonnelPreparationRoomWidget::ApplyListVisibility()
{
    if (RosterPanel) RosterPanel->SetVisibility(CurrentListDisplay==EPersonnelListDisplay::Personnel?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if (WarehousePanel) WarehousePanel->SetVisibility(CurrentListDisplay==EPersonnelListDisplay::Warehouse?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
}
void UPersonnelPreparationRoomWidget::StartListTransition()
{
    if (bClosing || bListTransitioning) return;
    bListTransitioning=true;
    if (auto* Entry=GetCurrentListAnimation(true)) StopAnimation(Entry);
    PlayListTransitionAnimation(false);
}
void UPersonnelPreparationRoomWidget::PlayListTransitionAnimation(bool bEntering)
{
    bListSlidingOut=!bEntering;
    ListTransitionAnimation=GetCurrentListAnimation(bEntering);
    if (!ListTransitionAnimation) { HandleListTransitionFinished(); return; }
    FWidgetAnimationDynamicEvent Finished;
    Finished.BindDynamic(this,&ThisClass::HandleListTransitionFinished);
    BindToAnimationFinished(ListTransitionAnimation,Finished);
    PlayAnimation(ListTransitionAnimation);
}
void UPersonnelPreparationRoomWidget::HandleListTransitionFinished()
{
    if (ListTransitionAnimation)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this,&ThisClass::HandleListTransitionFinished);
        UnbindFromAnimationFinished(ListTransitionAnimation,Finished);
        ListTransitionAnimation=nullptr;
    }
    if (bClosing || !bListTransitioning) return;
    if (bListSlidingOut)
    {
        CurrentListDisplay=RequestedListDisplay;
        ApplyListVisibility();
        PlayListTransitionAnimation(true);
    }
    else
    {
        bListTransitioning=false;
        if (RequestedListDisplay!=CurrentListDisplay) StartListTransition();
    }
}
void UPersonnelPreparationRoomWidget::CancelListTransition()
{
    bListTransitioning=false;
    if (!ListTransitionAnimation) return;
    FWidgetAnimationDynamicEvent Finished;
    Finished.BindDynamic(this,&ThisClass::HandleListTransitionFinished);
    UnbindFromAnimationFinished(ListTransitionAnimation,Finished);
    UWidgetAnimation* Animation=ListTransitionAnimation;
    ListTransitionAnimation=nullptr;
    StopAnimation(Animation);
}
UWidgetAnimation* UPersonnelPreparationRoomWidget::GetCurrentListAnimation(bool bEntering) const
{
    const FName Name=CurrentListDisplay==EPersonnelListDisplay::Warehouse
        ? (bEntering?WarehouseEnterAnimation:WarehouseExitAnimation)
        : (bEntering?PersonnelEnterAnimation:PersonnelExitAnimation);
    if (auto* Class=GetWidgetTreeOwningClass())
        for (UWidgetAnimation* Animation:Class->Animations)
            if (Animation && (Animation->GetFName()==Name || Animation->GetName()==Name.ToString()+TEXT("_INST"))) return Animation;
    return nullptr;
}
bool UPersonnelPreparationRoomWidget::RecordPanelAnimationFinished(bool bListPanel,bool bEntering)
{
    if (bEntering==bClosing || bPanelCompletionReported) return false;
    if (bListPanel) bListAnimationFinished=true; else bDetailAnimationFinished=true;
    bPanelCompletionReported=bListAnimationFinished && bDetailAnimationFinished;
    return bPanelCompletionReported;
}
void UPersonnelPreparationRoomWidget::HandleFilter(FName ID) { SetRosterFilter(ID==TEXT("All")?EPersonnelRosterFilter::All:EPersonnelRosterFilter::Standby); }
void UPersonnelPreparationRoomWidget::HandlePage(FName ID) { SetDetailPage(ID==TEXT("Equipment")?EPersonnelDetailPage::Equipment:ID==TEXT("Training")?EPersonnelDetailPage::Training:EPersonnelDetailPage::Profile); }
void UPersonnelPreparationRoomWidget::HandlePerson(FName ID)
{
    if (SelectPersonnel(ID)) OnPersonnelClicked(GetSelectedUnitId());
}
bool UPersonnelPreparationRoomWidget::BindPersonnelProfile(FGuid UnitId)
{
    ReleaseProfileData();
    ProfileUnitData=UPlayerUnitLibrary::GetUnitDataShared(this,UnitId);
    if (ProfileUnitData) ProfileDataHandle=ProfileUnitData->OnDataChanged.AddUObject(this,&ThisClass::HandleProfileDataChanged);
    RefreshDetails();
    return ProfileUnitData.IsValid();
}
void UPersonnelPreparationRoomWidget::ReleaseProfileData()
{
    if (ProfileUnitData) ProfileUnitData->OnDataChanged.Remove(ProfileDataHandle);
    ProfileDataHandle.Reset(); ProfileUnitData.Reset();
}
void UPersonnelPreparationRoomWidget::HandleProfileDataChanged(FGuid UnitId) { RefreshDetails(); }
void UPersonnelPreparationRoomWidget::RefreshDetails()
{
    auto Value=[](const FText& Text){ return Text.IsEmpty()?FString(TEXT("—")):Text.ToString(); };
    if (!ProfileUnitData)
    {
        Write(SelectedMeta,TEXT("代号  —")); Write(SelectedName,TEXT("姓名  —"));
        Write(SelectedAge,TEXT("年龄  —")); Write(SelectedGender,TEXT("性别  —"));
        Write(SelectedNationality,TEXT("国籍  —")); Write(SelectedSquad,TEXT("所属小队  —"));
        Write(SelectedLocation,TEXT("当前位置  —"));
        Write(BiographyText,TEXT("暂无简历")); Write(EvaluationText,TEXT("暂无评价"));
        Write(ServiceDurationText,TEXT("服役时间  —")); Write(BattleRecordText,TEXT("参与战斗  —"));
        Write(InjuryRecordText,TEXT("负伤记录  —")); Write(ActionLogText,TEXT("暂无服役记录"));
        if (DetailPortrait) DetailPortrait->SetPortraitTexture(nullptr);
        return;
    }
    const auto& P=ProfileUnitData->Profile;
    const auto& E=ProfileUnitData->Evaluation;
    const auto& S=ProfileUnitData->ServiceRecord;
    if (DetailPortrait) DetailPortrait->SetUnitPortrait(P);
    Write(SelectedMeta,TEXT("代号  ")+Value(P.CodeName));
    const FString Name=P.LastName.ToString()+P.FirstName.ToString();
    Write(SelectedName,TEXT("姓名  ")+(Name.IsEmpty()?FString(TEXT("—")):Name));
    Write(SelectedAge,FString::Printf(TEXT("年龄  %d"),P.Age));
    Write(SelectedGender,TEXT("性别  ")+StaticEnum<ECharacterGender>()->GetDisplayNameTextByValue(int64(P.Gender)).ToString());
    Write(SelectedNationality,TEXT("国籍  ")+Value(P.Nationality));
    FSquadData Squad;
    const bool bHasSquad = UPlayerSquadLibrary::GetUnitSquad(this, ProfileUnitData->UnitId, Squad);
    Write(SelectedSquad,TEXT("所属小队  ")+(bHasSquad?Squad.SquadName.ToString():FString(TEXT("未编入小队"))));
    Write(SelectedLocation,TEXT("当前位置  ")+(ProfileUnitData->RuntimeData.TileId.IsNone()?FString(TEXT("—")):ProfileUnitData->RuntimeData.TileId.ToString()));
    Write(BiographyText,P.PersonnelResume.IsEmpty()?TEXT("暂无简历"):P.PersonnelResume.ToString());
    Write(EvaluationText,E.Content.IsEmpty()?TEXT("暂无评价"):E.Content.ToString()+(E.Evaluator.IsEmpty()?FString():TEXT("\n— ")+E.Evaluator.ToString()));
    Write(ServiceDurationText,FString::Printf(TEXT("服役时间  %d 天"),S.ServiceDays));
    Write(BattleRecordText,FString::Printf(TEXT("参与战斗  %d 次"),S.BattleCount));
    FString Injuries=FString::Printf(TEXT("负伤记录  %d 次"),S.InjuryRecords.Num());
    FString Log;
    for (const FText& Entry:S.InjuryRecords) Log+=TEXT("负伤 · ")+Entry.ToString()+TEXT("\n");
    for (const FText& Entry:S.Entries) Log+=Entry.ToString()+TEXT("\n");
    Write(InjuryRecordText,Injuries);
    Write(ActionLogText,Log.IsEmpty()?TEXT("暂无服役记录"):Log);
}

USIS_InventorySlotContainer* UPersonnelPreparationRoomWidget::GetWarehouseInventoryContainer() const
{
    if (auto* Container=Cast<USIS_InventorySlotContainer>(GetWidgetFromName(TEXT("PlayerInventory")))) return Container;
    return Cast<USIS_InventorySlotContainer>(GetWidgetFromName(TEXT("PlayerInventory_1")));
}

