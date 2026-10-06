#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"

#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadMemberCardWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadVehicleEntryWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UIBasic/PortraitLibrary.h"
#include "Components/EditableTextBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "InputCoreTypes.h"

namespace
{
FText SquadText(const FString& Value) { return FText::FromString(Value); }
void WriteSquadText(UTextBlock* Text, const FString& Value) { if (Text) Text->SetText(SquadText(Value)); }
FName SquadChoiceId(FGuid Id) { return FName(*Id.ToString()); }
void SetSquadBrush(UBasicButtonWidget* Button, UTexture2D* Texture)
{
    FSlateBrush Brush = UPortraitLibrary::MakePortraitBrush(Texture);
    Brush.ImageSize = FVector2D(64);
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Button->SetIconBrush(Brush);
}
float PanelSlideWidth(UWidget* Panel)
{
    return Panel ? FMath::Max(280.f, float(Panel->GetCachedGeometry().GetLocalSize().X) + 24.f) : 400.f;
}
void MoveSquadPanel(UWidget* Panel, FVector2D Offset, float Opacity)
{
    if (!Panel) return;
    Panel->SetRenderTranslation(Offset);
    Panel->SetRenderOpacity(Opacity);
}
}

bool USquadMeetingRoomWidget::BindManagers()
{
    auto* Squads = UPlayerSquadLibrary::GetPlayerSquadManager(this);
    auto* Units = UPlayerUnitLibrary::GetPlayerUnitManager(this);
    if (SquadManager != Squads || UnitManager != Units) ReleaseManagers();
    SquadManager = Squads;
    UnitManager = Units;
    if (SquadManager) SquadManager->OnSquadChanged.AddUniqueDynamic(this, &ThisClass::HandleSquadChanged);
    if (UnitManager)
    {
        UnitManager->OnUnitDataChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        UnitManager->OnVehicleDataChanged.AddUniqueDynamic(this, &ThisClass::HandleVehicleChanged);
    }
    return IsValid(SquadManager) && IsValid(UnitManager);
}

void USquadMeetingRoomWidget::ReleaseManagers()
{
    if (SquadManager) SquadManager->OnSquadChanged.RemoveDynamic(this, &ThisClass::HandleSquadChanged);
    if (UnitManager)
    {
        UnitManager->OnUnitDataChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        UnitManager->OnVehicleDataChanged.RemoveDynamic(this, &ThisClass::HandleVehicleChanged);
    }
    AvailableVehicles.Reset();
    AvailableVehicleIndices.Reset();
    SquadManager = nullptr;
    UnitManager = nullptr;
}

void USquadMeetingRoomWidget::ReleaseRows()
{
    for (auto& Row : SquadRows) if (Row)
    {
        Row->CancelPendingClick();
        Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleSquadChoice);
    }
    for (auto& Row : PersonnelRows) if (Row)
    {
        Row->CancelPendingClick();
        Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandlePersonnelChoice);
        Row->ReleaseUnitData();
    }
    for (auto& Card : MemberCards) if (Card)
    {
        Card->OnRemoveRequested.RemoveDynamic(this, &ThisClass::HandleRemove);
        Card->OnCaptainRequested.RemoveDynamic(this, &ThisClass::HandlePromote);
        Card->ReleaseUnitData();
    }
    SquadRows.Reset(); PersonnelRows.Reset(); MemberCards.Reset();
    if (SquadList) SquadList->ClearChildren();
    if (PersonnelList) PersonnelList->ClearChildren();
    if (MemberList) MemberList->ClearChildren();
}

void USquadMeetingRoomWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bClosing = false;
    BindManagers();
    if (TransferConfirmPanel) TransferConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (ConfirmUnitTransferButton) ConfirmUnitTransferButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmUnitTransfer);
    if (CancelUnitTransferButton) CancelUnitTransferButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCancelUnitTransfer);
    if (CreateSquadButton) CreateSquadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCreateSquad);
    if (BackToSquadsButton) BackToSquadsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBack);
    if (SaveSquadNameButton) SaveSquadNameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSaveName);
    if (SaveSquadButton) SaveSquadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSaveSquad);
    if (ChooseSquadIconButton) ChooseSquadIconButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleChooseIcon);
    if (CloseIconPickerButton) CloseIconPickerButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseIconPicker);
    SetIconPickerVisible(false);
    if (UseCaptainPortraitButton) UseCaptainPortraitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleUseCaptain);
    if (ChooseVehicleButton) ChooseVehicleButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleChooseVehicle);
    if (CloseVehiclePickerButton) CloseVehiclePickerButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseVehiclePicker);
    if (ClearVehicleButton) ClearVehicleButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClearVehicle);
    SetVehiclePickerVisible(false);
    if (StandbyFilter) StandbyFilter->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandleFilter);
    if (AllFilter) AllFilter->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandleFilter);
    if (SquadNameInput)
    {
        SquadNameInput->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleNameChanged);
        SquadNameInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::HandleNameCommitted);
    }
    PresetIcons.Reset();
    for (UTexture2D* Icon : UPlayerSquadLibrary::GetPresetSquadIcons()) if (Icon) PresetIcons.Add(Icon);
    RefreshIcons();
    ApplyLayerVisibility();
    RefreshView();
}

void USquadMeetingRoomWidget::NativeDestruct()
{
    bClosing = true; bTransitioning = false; Transition = ETransition::None;
    CancelPanelAnimations();
    ReleaseRows(); ReleaseVehicleRows(); ReleaseManagers();
    for (auto& Button : IconButtons) if (Button)
    {
        Button->CancelPendingClick();
        Button->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleIcon);
    }
    IconButtons.Reset();
    if (CreateSquadButton) CreateSquadButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCreateSquad);
    if (BackToSquadsButton) BackToSquadsButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleBack);
    if (SaveSquadNameButton) SaveSquadNameButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSaveName);
    if (SaveSquadButton) SaveSquadButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSaveSquad);
    if (ChooseSquadIconButton) ChooseSquadIconButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleChooseIcon);
    if (CloseIconPickerButton) CloseIconPickerButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseIconPicker);
    DiscardDraft();
    if (UseCaptainPortraitButton) UseCaptainPortraitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleUseCaptain);
    if (ChooseVehicleButton) ChooseVehicleButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleChooseVehicle);
    if (CloseVehiclePickerButton) CloseVehiclePickerButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseVehiclePicker);
    if (ClearVehicleButton) ClearVehicleButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClearVehicle);
    if (ConfirmUnitTransferButton) ConfirmUnitTransferButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmUnitTransfer);
    if (CancelUnitTransferButton) CancelUnitTransferButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelUnitTransfer);
    if (StandbyFilter) StandbyFilter->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleFilter);
    if (AllFilter) AllFilter->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleFilter);
    if (SquadNameInput)
    {
        SquadNameInput->OnTextChanged.RemoveDynamic(this, &ThisClass::HandleNameChanged);
        SquadNameInput->OnTextCommitted.RemoveDynamic(this, &ThisClass::HandleNameCommitted);
    }
    Super::NativeDestruct();
}

void USquadMeetingRoomWidget::BeginDestroy()
{
    bClosing = true;
    ++DraftGeneration;
    DismissPendingUnitTransfer(false);
    CancelPanelAnimations(false);
    ReleaseRows(); ReleaseVehicleRows(); ReleaseManagers();
    Super::BeginDestroy();
}

bool USquadMeetingRoomWidget::Fail(const FText& Error)
{
    LastError = Error;
    if (ErrorText) { ErrorText->SetText(Error); ErrorText->SetVisibility(ESlateVisibility::HitTestInvisible); }
    OnOperationFailed(Error);
    return false;
}
void USquadMeetingRoomWidget::ClearError()
{
    LastError = FText::GetEmpty();
    if (ErrorText) { ErrorText->SetText(LastError); ErrorText->SetVisibility(ESlateVisibility::Collapsed); }
}

bool USquadMeetingRoomWidget::LoadSquadList(FName TileId)
{
    if (bClosing) return false;
    if (!BindManagers()) return Fail(SquadText(TEXT("玩家单位或小队子系统尚未就绪。")));
    CurrentTileId = TileId;
    VehicleListTileId = TileId;
    bVehicleOptionsDirty = true;
    DiscardDraft();
    CurrentLayer = TargetLayer = ESquadRoomLayer::SquadSelection;
    bShowAll = false; bNameDirty = false;
    // A caller may configure the tile immediately after CreateSceneUIByTag, during the entry animation.
    if (Transition != ETransition::EnterRoom)
    {
        CancelPanelAnimations();
        Transition = ETransition::None; bTransitioning = false;
        MoveSquadPanel(LeftPanel, FVector2D::ZeroVector, 1);
    }
    ApplyLayerVisibility();
    RefreshView();
    SetPanelsEnabled(!bTransitioning);
    if (TileId.IsNone()) return Fail(SquadText(TEXT("请为展示小队列表传入当前所在瓦片ID。")));
    ClearError(); return true;
}

bool USquadMeetingRoomWidget::CreateSquadAtTile(FName TileId, const FText& SquadName, FGuid& OutSquadId)
{
    OutSquadId.Invalidate();
    if (bClosing || bTransitioning) return false;
    if (!BindManagers()) return Fail(SquadText(TEXT("玩家小队子系统尚未就绪。")));
    if (TileId.IsNone() || TileId != CurrentTileId)
        return Fail(SquadText(TEXT("新建小队的瓦片必须与当前界面位置一致，请先展示该瓦片的小队列表。")));
    DiscardDraft();
    bEditingDraft = bNewSquadDraft = true;
    DraftSquad.SquadName = SquadName.IsEmptyOrWhitespace() ? SquadManager->GetNextSquadName() : SquadName;
    DraftSquad.SquadIcon = PresetIcons.IsEmpty() ? nullptr : PresetIcons[0].Get();
    DraftSquad.TileId = TileId;
    ClearError();
    if (CurrentLayer == ESquadRoomLayer::SquadManagement) RefreshView();
    else BeginLayerChange(ESquadRoomLayer::SquadManagement);
    return true;
}

bool USquadMeetingRoomWidget::SelectSquad(FGuid SquadId)
{
    if (bClosing || bTransitioning) return false;
    FSquadData Squad;
    if (!SquadManager || !SquadManager->GetSquad(SquadId, Squad)) return Fail(SquadText(TEXT("所选小队不存在。")));
    if (CurrentTileId.IsNone() || Squad.TileId != CurrentTileId) return Fail(SquadText(TEXT("只能管理当前瓦片的小队。")));
    DiscardDraft();
    SelectedSquadId = SquadId;
    DraftSquad = OriginalSquad = Squad;
    bEditingDraft = true;
    ClearError();
    if (CurrentLayer == ESquadRoomLayer::SquadManagement) { RefreshView(); return true; }
    BeginLayerChange(ESquadRoomLayer::SquadManagement);
    return true;
}

void USquadMeetingRoomWidget::ReturnToSquadList()
{
    if (bClosing || bTransitioning || CurrentLayer == ESquadRoomLayer::SquadSelection) return;
    DismissPendingUnitTransfer(false);
    SetIconPickerVisible(false);
    ClearError(); BeginLayerChange(ESquadRoomLayer::SquadSelection);
}

void USquadMeetingRoomWidget::SetShowAll(bool bInShowAll)
{
    if (bClosing || bTransitioning || IsUnitTransferPending()) return;
    if (bShowAll == bInShowAll) return;
    bShowAll = bInShowAll; RefreshView();
}

bool USquadMeetingRoomWidget::GetSelectedSquad(FSquadData& OutSquad) const
{
    OutSquad = bEditingDraft ? DraftSquad : FSquadData();
    return bEditingDraft;
}

bool USquadMeetingRoomWidget::CanEditSelectedSquad(FSquadData& OutSquad)
{
    if (bClosing || bTransitioning || IsUnitTransferPending() || CurrentLayer != ESquadRoomLayer::SquadManagement) return false;
    if (!GetSelectedSquad(OutSquad)) return Fail(SquadText(TEXT("请先选择一个有效小队。")));
    if (CurrentTileId.IsNone() || OutSquad.TileId != CurrentTileId) return Fail(SquadText(TEXT("小队已离开当前瓦片，请重新选择。")));
    if (!bNewSquadDraft)
    {
        FSquadData Current;
        if (!SquadManager || !SquadManager->GetSquad(SelectedSquadId, Current) || Current.TileId != CurrentTileId)
            return Fail(SquadText(TEXT("小队已解散或离开当前瓦片，请重新选择。")));
    }
    return true;
}

bool USquadMeetingRoomWidget::AddUnitToSelectedSquad(FGuid UnitId)
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    const auto Unit = UnitManager ? UnitManager->GetUnitDataShared(UnitId) : nullptr;
    if (!Unit || Unit->RuntimeData.TileId != CurrentTileId) return Fail(SquadText(TEXT("该人员不在当前位置，无法加入小队。")));
    if (!DraftSquad.MemberUnitIds.Contains(UnitId) && DraftSquad.MemberUnitIds.Num() >= DraftSquad.GetMaxMemberCount())
        return Fail(SquadText(TEXT("当前小队已满员，请先移出成员或选择载员更多的车辆。")));
    DraftSquad.MemberUnitIds.AddUnique(UnitId);
    CapacityRemovedUnitIds.Remove(UnitId);
    if (!DraftSquad.CaptainUnitId.IsValid()) DraftSquad.CaptainUnitId = UnitId;
    ClearError(); bRefreshPending = true; return true;
}

bool USquadMeetingRoomWidget::RequestUnitTransfer(FGuid UnitId, const FSquadData& SourceSquad)
{
    FSquadData Selected;
    if (!CanEditSelectedSquad(Selected)) return false;
    if (!TransferConfirmPanel || !TransferConfirmText || !ConfirmUnitTransferButton || !CancelUnitTransferButton)
        return Fail(SquadText(TEXT("人员调动确认控件未配置，无法调入其他小队成员。")));
    CancelDraftPendingActions();
    SetIconPickerVisible(false);
    SetVehiclePickerVisible(false);
    PendingTransferUnitId = UnitId;
    PendingTransferSourceSquadId = SourceSquad.SquadId;
    PendingTransferTileId = CurrentTileId;
    PendingTransferGeneration = DraftGeneration;
    FText Error;
    if (!ValidatePendingUnitTransfer(Error))
    {
        DismissPendingUnitTransfer(false);
        return Fail(Error);
    }
    ClearError();
    RefreshPendingUnitTransfer();
    if (!IsUnitTransferPending()) return false;
    SetPanelsEnabled(false);
    TransferConfirmPanel->SetIsEnabled(true);
    TransferConfirmPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ConfirmUnitTransferButton->SetIsEnabled(true);
    CancelUnitTransferButton->SetIsEnabled(true);
    for (EUINavigation Direction : {EUINavigation::Left, EUINavigation::Right, EUINavigation::Up,
        EUINavigation::Down, EUINavigation::Next, EUINavigation::Previous})
    {
        ConfirmUnitTransferButton->SetNavigationRuleExplicit(Direction, CancelUnitTransferButton);
        CancelUnitTransferButton->SetNavigationRuleExplicit(Direction, ConfirmUnitTransferButton);
    }
    // Explicit focus moves keyboard activation out of the disabled background panels.
    CancelUnitTransferButton->SetUserFocus(GetOwningPlayer());
    return true;
}

bool USquadMeetingRoomWidget::ValidatePendingUnitTransfer(FText& OutError) const
{
    OutError = FText::GetEmpty();
    if (!IsUnitTransferPending() || bClosing || bTransitioning || !bEditingDraft
        || CurrentLayer != ESquadRoomLayer::SquadManagement || PendingTransferGeneration != DraftGeneration
        || PendingTransferTileId.IsNone() || PendingTransferTileId != CurrentTileId || DraftSquad.TileId != CurrentTileId)
    { OutError = SquadText(TEXT("当前小队编辑已变化，请重新选择需要调入的人员。")); return false; }
    const auto Unit = UnitManager ? UnitManager->GetUnitDataShared(PendingTransferUnitId) : nullptr;
    if (!Unit || Unit->RuntimeData.TileId != CurrentTileId)
    { OutError = SquadText(TEXT("该人员已不在当前位置，已取消调动确认。")); return false; }
    FSquadData Source;
    if (!SquadManager || !SquadManager->GetUnitSquad(PendingTransferUnitId, Source)
        || Source.SquadId != PendingTransferSourceSquadId || Source.SquadId == SelectedSquadId
        || Source.TileId != CurrentTileId)
    { OutError = SquadText(TEXT("该人员原小队或所在位置已变化，请重新选择并确认。")); return false; }
    if (!bNewSquadDraft)
    {
        FSquadData Current;
        if (!SquadManager->GetSquad(SelectedSquadId, Current) || Current.TileId != CurrentTileId)
        { OutError = SquadText(TEXT("当前小队已解散或离开此瓦片，已取消调动确认。")); return false; }
    }
    if (DraftSquad.MemberUnitIds.Contains(PendingTransferUnitId)
        || DraftSquad.MemberUnitIds.Num() >= DraftSquad.GetMaxMemberCount())
    { OutError = SquadText(TEXT("该人员已在草稿中或当前小队已满员，无法继续调入。")); return false; }
    return true;
}

void USquadMeetingRoomWidget::RefreshPendingUnitTransfer()
{
    if (!IsUnitTransferPending()) return;
    FText Error;
    if (!ValidatePendingUnitTransfer(Error))
    {
        DismissPendingUnitTransfer(false);
        Fail(Error);
        return;
    }
    FSquadData Source;
    SquadManager->GetUnitSquad(PendingTransferUnitId, Source);
    const auto Unit = UnitManager->GetUnitDataShared(PendingTransferUnitId);
    const FText Name = Unit->Profile.CodeName.IsEmpty()
        ? SquadText(Unit->Profile.LastName.ToString() + Unit->Profile.FirstName.ToString()) : Unit->Profile.CodeName;
    const FText Message = FText::Format(NSLOCTEXT("SquadMeetingRoom", "ConfirmUnitTransfer",
        "{0} 当前属于「{1}」。\n\n确定将该人员调入「{2}」吗？"), Name, Source.SquadName, DraftSquad.SquadName);
    if (TransferConfirmText && !TransferConfirmText->GetText().EqualTo(Message)) TransferConfirmText->SetText(Message);
}

void USquadMeetingRoomWidget::DismissPendingUnitTransfer(bool bRestoreFocus)
{
    const FGuid PreviousUnit = PendingTransferUnitId;
    PendingTransferUnitId.Invalidate();
    PendingTransferSourceSquadId.Invalidate();
    PendingTransferTileId = NAME_None;
    PendingTransferGeneration = 0;
    if (ConfirmUnitTransferButton) ConfirmUnitTransferButton->CancelPendingClick();
    if (CancelUnitTransferButton) CancelUnitTransferButton->CancelPendingClick();
    if (TransferConfirmPanel) TransferConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
    SetPanelsEnabled(!bClosing && !bTransitioning);
    if (bRestoreFocus && !bClosing && !bTransitioning && CurrentLayer == ESquadRoomLayer::SquadManagement)
    {
        const FName Choice = SquadChoiceId(PreviousUnit);
        for (auto& Row : PersonnelRows)
            if (Row && Row->ChoiceID == Choice && Row->GetIsEnabled()) { Row->SetUserFocus(GetOwningPlayer()); return; }
        if (BackToSquadsButton) BackToSquadsButton->SetUserFocus(GetOwningPlayer());
    }
}

void USquadMeetingRoomWidget::CancelPendingUnitTransfer()
{
    if (!IsUnitTransferPending()) return;
    DismissPendingUnitTransfer(true);
}

bool USquadMeetingRoomWidget::ConfirmPendingUnitTransfer()
{
    if (!IsUnitTransferPending()) return false;
    FText Error;
    if (!ValidatePendingUnitTransfer(Error))
    {
        DismissPendingUnitTransfer(true);
        return Fail(Error);
    }
    const FGuid UnitId = PendingTransferUnitId;
    const FGuid SourceId = PendingTransferSourceSquadId;
    const uint64 ConfirmingGeneration = PendingTransferGeneration;
    DismissPendingUnitTransfer(false);
    FSquadData CurrentSource;
    if (bClosing || DraftGeneration != ConfirmingGeneration || !SquadManager
        || !SquadManager->GetUnitSquad(UnitId, CurrentSource) || CurrentSource.SquadId != SourceId)
        return false;
    if (!AddUnitToSelectedSquad(UnitId)) return false;
    ConfirmedTransferSources.Add(UnitId, SourceId);
    if (BackToSquadsButton) BackToSquadsButton->SetUserFocus(GetOwningPlayer());
    return true;
}

bool USquadMeetingRoomWidget::RemoveUnitFromSelectedSquad(FGuid UnitId)
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    if (!Squad.MemberUnitIds.Contains(UnitId)) return Fail(SquadText(TEXT("该人员不属于当前小队。")));
    DraftSquad.MemberUnitIds.Remove(UnitId);
    ConfirmedTransferSources.Remove(UnitId);
    if (DraftSquad.CaptainUnitId == UnitId)
        DraftSquad.CaptainUnitId = DraftSquad.MemberUnitIds.IsEmpty() ? FGuid() : DraftSquad.MemberUnitIds[0];
    ClearError(); bRefreshPending = true; return true;
}

bool USquadMeetingRoomWidget::PromoteMemberToCaptain(FGuid UnitId)
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    if (!DraftSquad.MemberUnitIds.Contains(UnitId)) return Fail(SquadText(TEXT("队长必须是草稿中的小队成员。")));
    DraftSquad.CaptainUnitId = UnitId;
    ClearError(); bRefreshPending = true; return true;
}

bool USquadMeetingRoomWidget::RenameSelectedSquad(const FText& Name)
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    DraftSquad.SquadName = Name;
    bNameDirty = false; ClearError(); RefreshDraftHeader(); return true;
}

bool USquadMeetingRoomWidget::SelectPresetIcon(int32 IconIndex)
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    if (!PresetIcons.IsValidIndex(IconIndex)) return Fail(SquadText(TEXT("预设小队图标不存在。")));
    DraftSquad.SquadIcon = PresetIcons[IconIndex];
    DraftSquad.IconSource = ESquadIconSource::Preset;
    SetIconPickerVisible(false);
    ClearError(); bRefreshPending = true; return true;
}

bool USquadMeetingRoomWidget::UseCaptainPortrait()
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    if (!DraftSquad.CaptainUnitId.IsValid()) return Fail(SquadText(TEXT("请先选择一名队长。")));
    DraftSquad.IconSource = ESquadIconSource::CaptainPortrait;
    SetIconPickerVisible(false);
    ClearError(); bRefreshPending = true; return true;
}

bool USquadMeetingRoomWidget::SetSelectedSquadVehicle(const FVehicleData& Vehicle, TArray<FGuid>& OutRemovedUnitIds)
{
    OutRemovedUnitIds.Reset();
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    if (!Vehicle.VehicleId.IsValid()) return Fail(SquadText(TEXT("请选择具有有效车辆ID的车辆；移除车辆请使用清除当前小队车辆。")));
    const TSharedPtr<FVehicleData> Canonical = UnitManager ? UnitManager->GetVehicleDataShared(Vehicle.VehicleId) : nullptr;
    if (!Canonical.IsValid()) return Fail(SquadText(TEXT("该车辆不在玩家单位子系统中，请先加载车辆数据。")));
    if (Canonical->RuntimeData.TileId.IsNone() || Canonical->RuntimeData.TileId != DraftSquad.TileId)
        return Fail(SquadText(TEXT("车辆必须与当前小队位于同一瓦片。")));
    if (Canonical->Attributes.PassengerCapacity < 0)
        return Fail(SquadText(TEXT("车辆载员上限不能小于0。")));
    FSquadData VehicleSquad;
    if (SquadManager && SquadManager->GetVehicleSquad(Vehicle.VehicleId, VehicleSquad)
        && VehicleSquad.SquadId != SelectedSquadId)
        return Fail(SquadText(TEXT("该车辆已分配给其他小队，请先解除原有分配。")));

    DraftSquad.AssignedVehicle = *Canonical;
    OutRemovedUnitIds = DraftSquad.TrimMembersToCapacity();
    for (FGuid Id : OutRemovedUnitIds) { CapacityRemovedUnitIds.AddUnique(Id); ConfirmedTransferSources.Remove(Id); }
    SetVehiclePickerVisible(false);
    ClearError(); bRefreshPending = true; RefreshDraftHeader();
    return true;
}

bool USquadMeetingRoomWidget::ClearSelectedSquadVehicle(TArray<FGuid>& OutRemovedUnitIds)
{
    OutRemovedUnitIds.Reset();
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    DraftSquad.AssignedVehicle = FVehicleData();
    OutRemovedUnitIds = DraftSquad.TrimMembersToCapacity();
    for (FGuid Id : OutRemovedUnitIds) { CapacityRemovedUnitIds.AddUnique(Id); ConfirmedTransferSources.Remove(Id); }
    SetVehiclePickerVisible(false);
    ClearError(); bRefreshPending = true; RefreshDraftHeader();
    return true;
}

bool USquadMeetingRoomWidget::LoadAvailableVehicles(FName TileId)
{
    if (bClosing || IsUnitTransferPending()) return false;
    if (!BindManagers()) return Fail(SquadText(TEXT("玩家单位子系统尚未就绪。")));
    if (TileId.IsNone()) return Fail(SquadText(TEXT("请为车辆列表传入当前所在瓦片ID。")));
    CancelVehiclePendingActions();
    VehicleListTileId = TileId;
    bVehicleOptionsDirty = true;
    ClearError();
    RefreshVehicleOptions();
    return true;
}

bool USquadMeetingRoomWidget::SetAvailableVehicles(const TArray<FVehicleData>& Vehicles)
{
    if (bClosing || !BindManagers()) return false;
    TSet<FGuid> Ids;
    for (const FVehicleData& Vehicle : Vehicles)
    {
        if (!Vehicle.VehicleId.IsValid() || Ids.Contains(Vehicle.VehicleId)
            || !UnitManager->GetVehicleDataShared(Vehicle.VehicleId).IsValid())
            return Fail(SquadText(TEXT("请先将有效且不重复的车辆加载到玩家单位子系统，再加载当前瓦片车辆列表。")));
        Ids.Add(Vehicle.VehicleId);
    }
    return LoadAvailableVehicles(VehicleListTileId.IsNone() ? CurrentTileId : VehicleListTileId);
}

TArray<FVehicleData> USquadMeetingRoomWidget::GetAvailableVehicles() const
{
    return UnitManager && !VehicleListTileId.IsNone()
        ? UnitManager->GetVehicleDataAtTile(VehicleListTileId) : TArray<FVehicleData>();
}

void USquadMeetingRoomWidget::RefreshAvailableVehicleData()
{
    AvailableVehicles.Reset();
    AvailableVehicleIndices.Reset();
    if (!UnitManager || VehicleListTileId.IsNone()) return;
    for (const FGuid VehicleId : UnitManager->GetVehicleDataIDs())
    {
        const TSharedPtr<FVehicleData> Vehicle = UnitManager->GetVehicleDataShared(VehicleId);
        if (!Vehicle.IsValid() || Vehicle->RuntimeData.TileId != VehicleListTileId) continue;
        AvailableVehicleIndices.Add(VehicleId, AvailableVehicles.Num());
        AvailableVehicles.Add(Vehicle);
    }
}

bool USquadMeetingRoomWidget::SelectAvailableVehicle(FGuid VehicleId)
{
    if (bClosing || bTransitioning) return false;
    const TSharedPtr<FVehicleData> Vehicle = UnitManager ? UnitManager->GetVehicleDataShared(VehicleId) : nullptr;
    if (!Vehicle.IsValid() || VehicleListTileId.IsNone() || Vehicle->RuntimeData.TileId != VehicleListTileId)
        return Fail(SquadText(TEXT("该车辆已不在当前可选列表中，请重新选择。")));
    // Always resolve the latest authoritative allocation, including delayed click callbacks.
    TArray<FGuid> RemovedIds;
    return SetSelectedSquadVehicle(*Vehicle, RemovedIds);
}

bool USquadMeetingRoomWidget::CanSelectVehicle(const FVehicleData& Vehicle, const TSet<FGuid>& OccupiedVehicles, FText& OutStatus) const
{
    if (!bEditingDraft || bClosing || bTransitioning || CurrentLayer != ESquadRoomLayer::SquadManagement)
    {
        OutStatus = SquadText(TEXT("请先选择小队"));
        return false;
    }
    if (!Vehicle.VehicleId.IsValid() || Vehicle.Attributes.PassengerCapacity < 0)
    {
        OutStatus = SquadText(TEXT("车辆数据无效"));
        return false;
    }
    if (Vehicle.RuntimeData.TileId.IsNone() || Vehicle.RuntimeData.TileId != DraftSquad.TileId)
    {
        OutStatus = SquadText(Vehicle.RuntimeData.TileId.IsNone() ? TEXT("未设置位置 · 不可选") : TEXT("不在当前瓦片 · 不可选"));
        return false;
    }
    if (OccupiedVehicles.Contains(Vehicle.VehicleId))
    {
        OutStatus = SquadText(TEXT("已分配给其他小队 · 不可选"));
        return false;
    }
    OutStatus = SquadText(DraftSquad.HasAssignedVehicle() && DraftSquad.AssignedVehicle.VehicleId == Vehicle.VehicleId
        ? TEXT("当前选择") : TEXT("可选择 · 保存后分配"));
    return true;
}

FText USquadMeetingRoomWidget::GetVehicleCapacityNotice() const
{
    if (!CapacityRemovedUnitIds.IsEmpty())
        return SquadText(FString::Printf(TEXT("因座位不足已从草稿移出 %d 名成员 · 保存后生效"), CapacityRemovedUnitIds.Num()));
    return SquadText(TEXT("座位包含驾驶席 · 修改保存后生效"));
}

void USquadMeetingRoomWidget::CancelVehiclePendingActions()
{
    for (auto& Row : VehicleRows) if (Row) Row->CancelPendingClick();
    for (UBasicButtonWidget* Button : {ChooseVehicleButton.Get(), CloseVehiclePickerButton.Get(), ClearVehicleButton.Get()})
        if (Button) Button->CancelPendingClick();
}

void USquadMeetingRoomWidget::ReleaseVehicleRows()
{
    CancelVehiclePendingActions();
    for (auto& Row : VehicleRows) if (Row)
    {
        Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleVehicleChoice);
        Row->ReleaseVehicleData();
    }
    VehicleRows.Reset();
    if (VehicleOptions) VehicleOptions->ClearChildren();
}

void USquadMeetingRoomWidget::RefreshVehicleOptions()
{
    RefreshAvailableVehicleData();
    bVehicleOptionsDirty = false;
    if (VehicleEmptyText)
    {
        VehicleEmptyText->SetText(SquadText(TEXT("当前瓦片暂无车辆")));
        VehicleEmptyText->SetVisibility(AvailableVehicles.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (VehiclePickerStatus)
    {
        FString Status = bEditingDraft && DraftSquad.HasAssignedVehicle()
            ? FString::Printf(TEXT("当前选择  %s · %d 座"), *DraftSquad.AssignedVehicle.Profile.VehicleName.ToString(), DraftSquad.GetMaxMemberCount())
            : TEXT("未分配车辆 · 默认4人");
        if (bEditingDraft && DraftSquad.HasAssignedVehicle() && !AvailableVehicleIndices.Contains(DraftSquad.AssignedVehicle.VehicleId))
            Status += TEXT("\n当前车辆未包含在本次列表中，选择保持不变");
        if (!CapacityRemovedUnitIds.IsEmpty()) Status += TEXT("\n") + GetVehicleCapacityNotice().ToString();
        VehiclePickerStatus->SetText(SquadText(Status));
    }
    if (ClearVehicleButton) ClearVehicleButton->SetIsEnabled(bEditingDraft && DraftSquad.HasAssignedVehicle() && !bClosing && !bTransitioning);
    if (!VehicleOptions || !GetOwningPlayer()) return;

    TSet<FGuid> OccupiedVehicles;
    if (SquadManager) for (const FSquadData& Other : SquadManager->GetAllSquads())
        if (Other.SquadId != SelectedSquadId && Other.HasAssignedVehicle()) OccupiedVehicles.Add(Other.AssignedVehicle.VehicleId);
    const float Offset = VehicleOptions->GetScrollOffset();
    TMap<FGuid, USquadVehicleEntryWidget*> ExistingRows;
    for (auto& Row : VehicleRows) if (Row) ExistingRows.Add(Row->GetVehicleId(), Row.Get());
    TArray<TObjectPtr<USquadVehicleEntryWidget>> NextRows;
    // Keep Slate wrappers alive across any necessary reorder; unchanged ID order never
    // detaches a row, avoiding flashes when selection/availability alone changes.
    TArray<TSharedRef<SWidget>> SlateRows;
    const TSubclassOf<USquadVehicleEntryWidget> Class = VehicleEntryClass ? VehicleEntryClass.Get() : USquadVehicleEntryWidget::StaticClass();
    for (const TSharedPtr<FVehicleData>& SharedVehicle : AvailableVehicles)
    {
        if (!SharedVehicle.IsValid()) continue;
        const FVehicleData& Vehicle = *SharedVehicle;
        USquadVehicleEntryWidget* Row = ExistingRows.FindRef(Vehicle.VehicleId);
        if (!Row) Row = CreateWidget<USquadVehicleEntryWidget>(GetOwningPlayer(), Class);
        if (!Row) continue;
        SlateRows.Add(Row->TakeWidget());
        FText Status;
        const bool bCanSelect = CanSelectVehicle(Vehicle, OccupiedVehicles, Status);
        const bool bSelected = bEditingDraft && DraftSquad.HasAssignedVehicle() && DraftSquad.AssignedVehicle.VehicleId == Vehicle.VehicleId;
        if (bSelected && !bCanSelect) Status = SquadText(TEXT("当前选择 · ") + Status.ToString());
        Row->SetVehicleDataShared(SharedVehicle, bSelected, bCanSelect, Status);
        Row->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandleVehicleChoice);
        Row->ForceLayoutPrepass();
        NextRows.Add(Row);
    }
    for (auto& Row : VehicleRows) if (Row && !NextRows.Contains(Row))
    {
        Row->CancelPendingClick();
        Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleVehicleChoice);
        Row->ReleaseVehicleData();
        Row->RemoveFromParent();
    }
    bool bOrderChanged = VehicleOptions->GetChildrenCount() != NextRows.Num();
    for (int32 Index = 0; !bOrderChanged && Index < NextRows.Num(); ++Index)
        bOrderChanged = VehicleOptions->GetChildAt(Index) != NextRows[Index];
    if (bOrderChanged)
    {
        VehicleOptions->ClearChildren();
        for (auto& Row : NextRows)
            if (auto* RowSlot = Cast<UScrollBoxSlot>(VehicleOptions->AddChild(Row)))
                RowSlot->SetPadding(FMargin(0, 0, 6, 10));
    }
    VehicleRows = MoveTemp(NextRows);
    VehicleOptions->ForceLayoutPrepass();
    VehicleOptions->SetScrollOffset(Offset);
}

bool USquadMeetingRoomWidget::IsVehiclePickerVisible() const
{
    return VehiclePickerPanel && VehiclePickerPanel->IsVisible();
}

void USquadMeetingRoomWidget::SetVehiclePickerVisible(bool bVisible)
{
    const bool bShow = bVisible && bEditingDraft && !bClosing && !bTransitioning && !IsUnitTransferPending()
        && CurrentLayer == ESquadRoomLayer::SquadManagement;
    if (!bShow) CancelVehiclePendingActions();
    if (!VehiclePickerPanel) return;
    if (bShow)
    {
        SetIconPickerVisible(false);
        RefreshVehicleOptions();
    }
    VehiclePickerPanel->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

int32 USquadMeetingRoomWidget::GetSelectedSquadCapacity() const
{
    return bEditingDraft ? DraftSquad.GetMaxMemberCount() : 0;
}

bool USquadMeetingRoomWidget::SaveSelectedSquad()
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return false;
    for (const auto& Approval : ConfirmedTransferSources)
    {
        if (!DraftSquad.MemberUnitIds.Contains(Approval.Key)) continue;
        FSquadData CurrentSource;
        if (!SquadManager || !SquadManager->GetUnitSquad(Approval.Key, CurrentSource)
            || CurrentSource.SquadId != Approval.Value)
            return Fail(SquadText(TEXT("已确认转入的人员所属小队发生变化，请先移出该人员并重新选择确认，再保存。")));
    }
    if (SquadNameInput) DraftSquad.SquadName = SquadNameInput->GetText();
    // Commit broadcasts synchronously. Pass stable copies because a listener may replace
    // the UI draft, change the selected squad, or begin closing this very widget.
    const uint64 SavingGeneration = DraftGeneration;
    UPlayerSquadManagerBase* SavingManager = SquadManager.Get();
    if (!SavingManager) return Fail(SquadText(TEXT("玩家小队子系统尚未就绪。")));
    const FSquadData SubmittedDraft = DraftSquad;
    const FSquadData SubmittedOriginal = OriginalSquad;
    const bool bSubmittedNew = bNewSquadDraft;
    const auto IsSameEditingContext = [&]()
    {
        return IsValid(this) && !bClosing && bEditingDraft && !bTransitioning
            && DraftGeneration == SavingGeneration && SquadManager.Get() == SavingManager
            && CurrentLayer == ESquadRoomLayer::SquadManagement;
    };
    FGuid SavedId;
    FText Error;
    if (!SavingManager->CommitSquadDraft(SubmittedDraft, SubmittedOriginal, bSubmittedNew, SavedId, Error))
        return IsSameEditingContext() ? Fail(Error) : false;
    if (IsSameEditingContext())
    {
        FSquadData SavedSquad;
        if (SavingManager->GetSquad(SavedId, SavedSquad))
        {
            SelectedSquadId = SavedId;
            DraftSquad = OriginalSquad = SavedSquad;
            bNewSquadDraft = bNameDirty = false;
            ClearError(); SetIconPickerVisible(false); RefreshView();
            // Start the normal return before the saved event. Never navigate after a
            // callback has chosen a different draft or closed the room.
            ReturnToSquadList();
        }
    }
    // The committed operation still succeeded if another active draft was selected.
    // A closing/detached widget must not start a new Blueprint flow via this event.
    if (IsValid(this) && !bClosing) OnSquadSaved(SavedId);
    return true;
}

bool USquadMeetingRoomWidget::HasUnsavedSquadChanges() const
{
    return bEditingDraft && (bNewSquadDraft || !DraftSquad.MatchesSnapshot(OriginalSquad));
}

void USquadMeetingRoomWidget::DiscardDraft()
{
    ++DraftGeneration;
    DismissPendingUnitTransfer(false);
    ConfirmedTransferSources.Reset();
    CancelDraftPendingActions();
    SetIconPickerVisible(false);
    SetVehiclePickerVisible(false);
    CapacityRemovedUnitIds.Reset();
    bEditingDraft = bNewSquadDraft = bNameDirty = false;
    SelectedSquadId.Invalidate();
    DraftSquad = OriginalSquad = FSquadData();
}

void USquadMeetingRoomWidget::CancelDraftPendingActions()
{
    for (auto& Row : SquadRows) if (Row) Row->CancelPendingClick();
    for (auto& Row : PersonnelRows) if (Row) Row->CancelPendingClick();
    for (auto& Card : MemberCards) if (Card) Card->CancelPendingActions();
    for (auto& Button : IconButtons) if (Button) Button->CancelPendingClick();
    CancelVehiclePendingActions();
    for (UBasicButtonWidget* Button : {CreateSquadButton.Get(), BackToSquadsButton.Get(), SaveSquadNameButton.Get(),
        SaveSquadButton.Get(), ChooseSquadIconButton.Get(), CloseIconPickerButton.Get(), UseCaptainPortraitButton.Get(),
        ChooseVehicleButton.Get(), ConfirmUnitTransferButton.Get(), CancelUnitTransferButton.Get(),
        static_cast<UBasicButtonWidget*>(StandbyFilter.Get()), static_cast<UBasicButtonWidget*>(AllFilter.Get())})
        if (Button) Button->CancelPendingClick();
}

void USquadMeetingRoomWidget::SetIconPickerVisible(bool bVisible)
{
    if (!IconPickerPanel) return;
    const bool bShow = bVisible && bEditingDraft && !bClosing && !bTransitioning && !IsUnitTransferPending()
        && CurrentLayer == ESquadRoomLayer::SquadManagement;
    if (bShow) SetVehiclePickerVisible(false);
    else
    {
        for (auto& Button : IconButtons) if (Button) Button->CancelPendingClick();
        if (CloseIconPickerButton) CloseIconPickerButton->CancelPendingClick();
        if (UseCaptainPortraitButton) UseCaptainPortraitButton->CancelPendingClick();
    }
    IconPickerPanel->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bShow) UpdateIconPickerPlacement();
}

void USquadMeetingRoomWidget::UpdateIconPickerPlacement()
{
    if (!IconPickerPanel || !IconPickerPanel->IsVisible() || !ChooseSquadIconButton) return;
    auto* Fit = GetWidgetFromName(TEXT("IconPickerFit"));
    auto* Extent = GetWidgetFromName(TEXT("IconPickerExtent"));
    auto* FitSlot = Fit ? Cast<UCanvasPanelSlot>(Fit->Slot) : nullptr;
    auto* Layout = IconPickerPanel->GetParent();
    if (!FitSlot || !Extent || !Layout) return;

    // Position in the parent canvas' local space, so DPI, aspect ratio and the user-edited
    // management panel position are respected. The full-size picker root does not intercept input.
    const FGeometry& RootGeometry = Layout->GetCachedGeometry();
    const FVector2D Available = RootGeometry.GetLocalSize();
    if (Available.X <= 16 || Available.Y <= 16) return;
    Extent->ForceLayoutPrepass();
    FVector2D Size = Extent->GetDesiredSize();
    if (Size.X <= 0 || Size.Y <= 0) return;
    const double Scale = FMath::Min(1.0, FMath::Min((Available.X-16)/Size.X, (Available.Y-16)/Size.Y));
    Size *= Scale;
    const FGeometry& Emblem = ChooseSquadIconButton->GetCachedGeometry();
    const FVector2D Anchor = RootGeometry.AbsoluteToLocal(Emblem.LocalToAbsolute(FVector2D::ZeroVector));
    FVector2D Position(Anchor.X - Size.X - 10, Anchor.Y);
    if (Position.X < 8)
    {
        const double Right = RootGeometry.AbsoluteToLocal(Emblem.LocalToAbsolute(Emblem.GetLocalSize())).X + 10;
        if (Right + Size.X <= Available.X-8) Position.X = Right;
    }
    Position.X = FMath::Clamp(Position.X, 8.0, Available.X-Size.X-8);
    Position.Y = FMath::Clamp(Position.Y, 8.0, Available.Y-Size.Y-8);
    if (!FitSlot->GetPosition().Equals(Position, 0.1)) FitSlot->SetPosition(Position);
    if (!FitSlot->GetSize().Equals(Size, 0.1)) FitSlot->SetSize(Size);
}

UTexture2D* USquadMeetingRoomWidget::GetDraftIcon() const
{
    if (!bEditingDraft) return nullptr;
    if (DraftSquad.IconSource == ESquadIconSource::CaptainPortrait && UnitManager)
        if (const auto Captain = UnitManager->GetUnitDataShared(DraftSquad.CaptainUnitId); Captain && Captain->Profile.PortraitTexture)
            return Captain->Profile.PortraitTexture;
    return DraftSquad.SquadIcon;
}

void USquadMeetingRoomWidget::RefreshIcons()
{
    for (auto& Button : IconButtons) if (Button) Button->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleIcon);
    IconButtons.Reset();
    if (!IconOptions || !GetOwningPlayer()) return;
    IconOptions->ClearChildren();
    const TSubclassOf<USelectionButtonWidget> Class = IconButtonClass ? IconButtonClass.Get() : USelectionButtonWidget::StaticClass();
    for (int32 Index = 0; Index < PresetIcons.Num(); ++Index)
    {
        auto* Button = CreateWidget<USelectionButtonWidget>(GetOwningPlayer(), Class);
        if (!Button) continue;
        Button->ChoiceID = FName(*FString::FromInt(Index));
        Button->MinimumSize = FVector2D(64); Button->IconSize = FVector2D(42);
        Button->SetContentMode(EBasicButtonContent::IconOnly);
        SetSquadBrush(Button, PresetIcons[Index]);
        const TSharedRef<SWidget> SlateButton = Button->TakeWidget();
        Button->SynchronizeProperties(); Button->ForceLayoutPrepass();
        Button->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandleIcon);
        IconOptions->AddChildToWrapBox(Button); IconButtons.Add(Button);
    }
}

void USquadMeetingRoomWidget::RefreshView()
{
    bRefreshPending = false;
    if (bClosing) return;
    RefreshPendingUnitTransfer();
    if (bClosing) return;
    if (bEditingDraft && !bNewSquadDraft)
    {
        FSquadData Current;
        if (!SquadManager || !SquadManager->GetSquad(SelectedSquadId, Current) || Current.TileId != CurrentTileId)
            DiscardDraft();
    }
    FSquadData Selected;
    const bool bHasSelected = GetSelectedSquad(Selected);
    if (CurrentLayer == ESquadRoomLayer::SquadManagement && !bHasSelected && !bTransitioning)
    {
        ReturnToSquadList(); return;
    }
    const float SquadOffset = SquadList ? SquadList->GetScrollOffset() : 0;
    const float PersonnelOffset = PersonnelList ? PersonnelList->GetScrollOffset() : 0;
    const float MemberOffset = MemberList ? MemberList->GetScrollOffset() : 0;
    // Keep mounted rows (and their shared data/portrait/Slate geometry) for unchanged IDs.
    // Previously every button cleared all three lists, causing a blank/layout frame.
    TSet<FName> VisibleSquadIds, VisibleUnitIds;
    const bool bManaging = CurrentLayer == ESquadRoomLayer::SquadManagement;
    const bool bEditable = bHasSelected && !CurrentTileId.IsNone() && Selected.TileId == CurrentTileId;
    const int32 Capacity = bHasSelected ? Selected.GetMaxMemberCount() : 0;
    const bool bFull = bHasSelected && Selected.MemberUnitIds.Num() >= Capacity;
    int32 VisibleSquads = 0;
    if (SquadManager) for (const FSquadData& Squad : SquadManager->GetAllSquads())
    {
        const bool bLocal = !CurrentTileId.IsNone() && Squad.TileId == CurrentTileId;
        if (!bShowAll && !bLocal) continue;
        ++VisibleSquads;
        if (!SquadList || !SquadRowClass || !GetOwningPlayer()) continue;
        const FName Choice = SquadChoiceId(Squad.SquadId);
        VisibleSquadIds.Add(Choice);
        auto* ExistingRow = SquadRows.FindByPredicate([&](const auto& Candidate) { return Candidate && Candidate->ChoiceID == Choice; });
        auto* Row = ExistingRow ? ExistingRow->Get() : CreateWidget<USelectionButtonWidget>(GetOwningPlayer(), SquadRowClass);
        if (!Row) continue;
        Row->ChoiceID = SquadChoiceId(Squad.SquadId);
        Row->ButtonText = Squad.SquadName;
        Row->ButtonSubtitle = SquadText(FString::Printf(TEXT("%02d / %02d 人  ·  %s%s"), Squad.MemberUnitIds.Num(), Squad.GetMaxMemberCount(), *Squad.TileId.ToString(), bLocal ? TEXT("") : TEXT("  ·  异地")));
        Row->SetContentMode(EBasicButtonContent::IconAndText);
        SetSquadBrush(Row, SquadManager->GetSquadIcon(Squad.SquadId));
        if (Squad.IconSource == ESquadIconSource::CaptainPortrait && UnitManager)
            if (const auto Captain = UnitManager->GetUnitDataShared(Squad.CaptainUnitId)) Row->SetIconBrush(UPortraitLibrary::GetSquarePortrait(Captain->Profile));
        Row->SetIsEnabled(bLocal); Row->SetSelected(Squad.SquadId == SelectedSquadId);
        const TSharedRef<SWidget> SlateRow = Row->TakeWidget();
        Row->SynchronizeProperties(); Row->ForceLayoutPrepass();
        Row->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandleSquadChoice);
        if (Row->GetParent() != SquadList)
            if (auto* RowSlot = Cast<UScrollBoxSlot>(SquadList->AddChild(Row))) RowSlot->SetPadding(FMargin(0,0,6,8));
        SquadRows.AddUnique(Row);
    }
    int32 VisibleUnits = 0;
    if (bManaging && UnitManager) for (const FGuid Id : UnitManager->GetUnitDataIDs())
    {
        const auto Unit = UnitManager->GetUnitDataShared(Id);
        if (!Unit) continue;
        const bool bLocal = !CurrentTileId.IsNone() && Unit->RuntimeData.TileId == CurrentTileId;
        if (!bShowAll && !bLocal) continue;
        ++VisibleUnits;
        if (!PersonnelList || !PersonnelRowClass || !GetOwningPlayer()) continue;
        const FName Choice = SquadChoiceId(Id);
        VisibleUnitIds.Add(Choice);
        auto* ExistingRow = PersonnelRows.FindByPredicate([&](const auto& Candidate) { return Candidate && Candidate->ChoiceID == Choice; });
        auto* Row = ExistingRow ? ExistingRow->Get() : CreateWidget<UPersonnelListEntryWidget>(GetOwningPlayer(), PersonnelRowClass);
        if (!Row) continue;
        Row->ChoiceID = SquadChoiceId(Id);
        // Keep the Slate wrapper alive until the scroll box owns it. Dropping a temporary
        // TakeWidget() result here calls NativeDestruct and releases the shared unit data.
        const TSharedRef<SWidget> SlateRow = Row->TakeWidget();
        if (Row->GetUnitData() != Unit) Row->SetUnitData(Unit, CurrentTileId);
        else Row->SetCurrentTileId(CurrentTileId);
        FSquadData Existing;
        const bool bAssigned = SquadManager->GetUnitSquad(Id, Existing);
        const bool bInDraft = Selected.MemberUnitIds.Contains(Id);
        const bool bOtherSquad = bAssigned && Existing.SquadId != SelectedSquadId;
        const FString Assignment = bInDraft ? (bOtherSquad
            ? Existing.SquadName.ToString() + TEXT("  ·  待调入") : FString(TEXT("已选入当前小队")))
            : (bAssigned ? Existing.SquadName.ToString() : FString(TEXT("未编入小队")));
        const FText AssignmentText = SquadText(bLocal
            ? Assignment + (bFull && !bInDraft ? TEXT("  ·  当前小队已满") : TEXT(""))
            : Assignment + TEXT("  ·  ") + Unit->RuntimeData.TileId.ToString() + TEXT("  ·  异地"));
        Row->SetSquadAssignmentPresentation(AssignmentText, bOtherSquad);
        Row->SetInteractionAllowed(bEditable && (bInDraft || !bFull));
        Row->SetSelected(Selected.MemberUnitIds.Contains(Id));
        Row->SynchronizeProperties(); Row->ForceLayoutPrepass();
        Row->OnSelectionRequested.AddUniqueDynamic(this, &ThisClass::HandlePersonnelChoice);
        if (Row->GetParent() != PersonnelList)
            if (auto* RowSlot = Cast<UScrollBoxSlot>(PersonnelList->AddChild(Row))) RowSlot->SetPadding(FMargin(0,0,6,8));
        PersonnelRows.AddUnique(Row);
    }
    // A card belongs to a visible seat, not to a temporary copied unit record. Reuse the
    // mounted seat widgets so adding/removing a member only changes affected bindings.
    const int32 DesiredCardCount = bManaging && bHasSelected && MemberList && MemberCardClass && GetOwningPlayer() ? Capacity : 0;
    while (MemberCards.Num() > DesiredCardCount)
    {
        if (auto* Card = MemberCards.Last().Get())
        {
            Card->OnRemoveRequested.RemoveDynamic(this, &ThisClass::HandleRemove);
            Card->OnCaptainRequested.RemoveDynamic(this, &ThisClass::HandlePromote);
            Card->ReleaseUnitData();
            Card->RemoveFromParent();
        }
        MemberCards.RemoveAt(MemberCards.Num() - 1);
    }
    while (MemberCards.Num() < DesiredCardCount) MemberCards.Add(nullptr);
    for (int32 SlotIndex = 0; SlotIndex < DesiredCardCount; ++SlotIndex)
    {
        auto* Card = MemberCards[SlotIndex].Get();
        if (!Card)
        {
            Card = CreateWidget<USquadMemberCardWidget>(GetOwningPlayer(), MemberCardClass);
            MemberCards[SlotIndex] = Card;
        }
        if (!Card) continue;
        const TSharedRef<SWidget> SlateCard = Card->TakeWidget();
        const FGuid UnitId = Selected.MemberUnitIds.IsValidIndex(SlotIndex) ? Selected.MemberUnitIds[SlotIndex] : FGuid();
        const auto Unit = UnitManager && UnitId.IsValid() ? UnitManager->GetUnitDataShared(UnitId) : nullptr;
        if (Unit)
        {
            Card->SetUnitData(Unit, UnitId == Selected.CaptainUnitId, bEditable, SlotIndex);
            Card->OnRemoveRequested.AddUniqueDynamic(this, &ThisClass::HandleRemove);
            Card->OnCaptainRequested.AddUniqueDynamic(this, &ThisClass::HandlePromote);
        }
        else
        {
            Card->OnRemoveRequested.RemoveDynamic(this, &ThisClass::HandleRemove);
            Card->OnCaptainRequested.RemoveDynamic(this, &ThisClass::HandlePromote);
            Card->SetEmptySlot(SlotIndex);
        }
        Card->ForceLayoutPrepass();
        if (Card->GetParent() != MemberList)
            if (auto* CardSlot = Cast<UScrollBoxSlot>(MemberList->AddChild(Card)))
            {
                CardSlot->SetPadding(FMargin(0,0,8,6));
                CardSlot->SetVerticalAlignment(VAlign_Top);
            }
    }
    for (int32 Index = SquadRows.Num()-1; Index >= 0; --Index)
        if (auto* Row = SquadRows[Index].Get(); !Row || !VisibleSquadIds.Contains(Row->ChoiceID))
        {
            if (Row) { Row->CancelPendingClick(); Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandleSquadChoice); Row->RemoveFromParent(); }
            SquadRows.RemoveAt(Index);
        }
    for (int32 Index = PersonnelRows.Num()-1; Index >= 0; --Index)
        if (auto* Row = PersonnelRows[Index].Get(); !Row || !VisibleUnitIds.Contains(Row->ChoiceID))
        {
            if (Row) { Row->CancelPendingClick(); Row->OnSelectionRequested.RemoveDynamic(this, &ThisClass::HandlePersonnelChoice); Row->ReleaseUnitData(); Row->RemoveFromParent(); }
            PersonnelRows.RemoveAt(Index);
        }
    if (SquadList) { SquadList->ForceLayoutPrepass(); SquadList->SetScrollOffset(SquadOffset); }
    if (PersonnelList) { PersonnelList->ForceLayoutPrepass(); PersonnelList->SetScrollOffset(PersonnelOffset); }
    if (MemberList) { MemberList->ForceLayoutPrepass(); MemberList->SetScrollOffset(MemberOffset); }
    WriteSquadText(ListTitle, FString::Printf(TEXT("%s  %02d"), bManaging ? TEXT("人员名册") : TEXT("小队列表"), bManaging ? VisibleUnits : VisibleSquads));
    WriteSquadText(CurrentTileText, CurrentTileId.IsNone() ? TEXT("尚未指定当前瓦片") : TEXT("当前位置  /  ") + CurrentTileId.ToString());
    if (EmptyListText)
    {
        EmptyListText->SetText(SquadText(CurrentTileId.IsNone() ? TEXT("请先传入当前瓦片ID")
            : (bManaging ? TEXT("当前位置暂无人员") : TEXT("暂无小队，点击下方新建"))));
        EmptyListText->SetVisibility((bManaging ? VisibleUnits : VisibleSquads) == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (StandbyFilter) StandbyFilter->SetSelected(!bShowAll);
    if (AllFilter) AllFilter->SetSelected(bShowAll);
    if (CreateSquadButton) CreateSquadButton->SetIsEnabled(!CurrentTileId.IsNone() && IsValid(SquadManager));
    WriteSquadText(MemberCountText, FString::Printf(TEXT("小队成员  %02d / %02d"), Selected.MemberUnitIds.Num(), Capacity));
    WriteSquadText(SquadLocationText, bHasSelected ? TEXT("当前位置  ") + Selected.TileId.ToString() : FString());
    WriteSquadText(VehicleText, bHasSelected && Selected.HasAssignedVehicle()
        ? FString::Printf(TEXT("%s  ·  %d 座"), Selected.AssignedVehicle.Profile.VehicleName.IsEmpty()
            ? TEXT("未命名车辆") : *Selected.AssignedVehicle.Profile.VehicleName.ToString(), Capacity)
        : FString(TEXT("未分配车辆  ·  默认4人")));
    if (VehicleHint) VehicleHint->SetText(GetVehicleCapacityNotice());
    if (VehiclePreviewImage)
    {
        UTexture2D* Preview = bHasSelected && Selected.HasAssignedVehicle() ? Selected.AssignedVehicle.Profile.PreviewImage.Get() : nullptr;
        if (VehiclePreviewImage->GetBrush().GetResourceObject() != Preview) VehiclePreviewImage->SetBrushFromTexture(Preview);
        VehiclePreviewImage->SetVisibility(Preview ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (ClearVehicleButton) ClearVehicleButton->SetIsEnabled(bEditable && Selected.HasAssignedVehicle());
    if (IsVehiclePickerVisible() && bVehicleOptionsDirty) RefreshVehicleOptions();
    RefreshDraftHeader();
    if (UseCaptainPortraitButton)
    {
        UseCaptainPortraitButton->SetSelected(bHasSelected && Selected.IconSource == ESquadIconSource::CaptainPortrait);
        UseCaptainPortraitButton->SetIsEnabled(bEditable && Selected.CaptainUnitId.IsValid());
    }
    if (SaveSquadNameButton) SaveSquadNameButton->SetIsEnabled(bEditable);
    if (SquadNameInput) SquadNameInput->SetIsEnabled(bEditable);
    if (ChooseVehicleButton) ChooseVehicleButton->SetIsEnabled(bEditable);
    for (int32 Index = 0; Index < IconButtons.Num(); ++Index)
    {
        IconButtons[Index]->SetIsEnabled(bEditable);
        IconButtons[Index]->SetSelected(bHasSelected && Selected.IconSource == ESquadIconSource::Preset && PresetIcons[Index] == Selected.SquadIcon);
    }
}

void USquadMeetingRoomWidget::RefreshDraftHeader()
{
    if (SquadIconImage)
    {
        SquadIconImage->SetBrush(UPortraitLibrary::MakePortraitBrush(GetDraftIcon()));
        if (DraftSquad.IconSource == ESquadIconSource::CaptainPortrait && UnitManager)
            if (const auto Captain = UnitManager->GetUnitDataShared(DraftSquad.CaptainUnitId)) SquadIconImage->SetBrush(UPortraitLibrary::GetSquarePortrait(Captain->Profile));
    }
    if (SquadNameInput && !bNameDirty && !SquadNameInput->GetText().EqualTo(DraftSquad.SquadName))
    {
        TGuardValue<bool> Guard(bWritingName, true);
        SquadNameInput->SetText(bEditingDraft ? DraftSquad.SquadName : FText::GetEmpty());
    }
    const bool bDirty = HasUnsavedSquadChanges();
    WriteSquadText(DraftStatusText, bNewSquadDraft ? TEXT("未保存的新小队 · 保存后创建")
        : bDirty ? TEXT("有未保存修改 · 返回将放弃") : TEXT("修改后点击保存小队"));
    if (SaveSquadButton) SaveSquadButton->SetIsEnabled(bEditingDraft && bDirty && !bClosing && !bTransitioning);
    if (ChooseSquadIconButton) ChooseSquadIconButton->SetIsEnabled(bEditingDraft && !bClosing);
}

void USquadMeetingRoomWidget::SetPanelsEnabled(bool bEnabled)
{
    for (UWidget* Panel : {LeftPanel.Get(), ManagementPanel.Get(), MemberStripPanel.Get()})
        if (Panel) Panel->SetIsEnabled(bEnabled && !IsUnitTransferPending());
}

void USquadMeetingRoomWidget::ApplyLayerVisibility()
{
    const bool bManaging = CurrentLayer == ESquadRoomLayer::SquadManagement;
    if (LeftPages) LeftPages->SetActiveWidgetIndex(bManaging ? 1 : 0);
    if (ManagementPanel) ManagementPanel->SetVisibility(bManaging ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (MemberStripPanel) MemberStripPanel->SetVisibility(bManaging ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (SelectionPrompt) SelectionPrompt->SetVisibility(bManaging ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (BackToSquadsButton) BackToSquadsButton->SetVisibility(bManaging ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (CreateSquadButton) CreateSquadButton->SetVisibility(bManaging ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

UWidgetAnimation* USquadMeetingRoomWidget::GetPanelTransitionAnimation(ESquadRoomPanel Panel, bool bEntering) const
{
    FName Name;
    switch (Panel)
    {
    case ESquadRoomPanel::LeftList:
        Name = CurrentLayer == ESquadRoomLayer::SquadManagement
            ? (bEntering ? PersonnelEnterAnimation : PersonnelExitAnimation)
            : (bEntering ? SquadEnterAnimation : SquadExitAnimation);
        break;
    case ESquadRoomPanel::Management: Name = bEntering ? ManagementEnterAnimation : ManagementExitAnimation; break;
    case ESquadRoomPanel::Members: Name = bEntering ? MembersEnterAnimation : MembersExitAnimation; break;
    default: return nullptr;
    }
    if (Name.IsNone()) return nullptr;
    for (const UClass* Class = GetClass(); Class; Class = Class->GetSuperClass())
        if (const auto* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(Class))
            for (UWidgetAnimation* Animation : WidgetClass->Animations)
                if (Animation && (Animation->GetFName() == Name || Animation->GetName() == Name.ToString() + TEXT("_INST")))
                    return Animation;
    return nullptr;
}

bool USquadMeetingRoomWidget::TryPlayWidgetTransition(bool bEntering)
{
    if (!bUseWidgetAnimations || TransitionDuration <= 0.f) return false;
    TArray<UWidgetAnimation*> Animations;
    const bool bManaging = CurrentLayer == ESquadRoomLayer::SquadManagement;
    for (ESquadRoomPanel Panel : {ESquadRoomPanel::LeftList, ESquadRoomPanel::Management, ESquadRoomPanel::Members})
    {
        if (Panel != ESquadRoomPanel::LeftList && !bManaging) continue;
        UWidgetAnimation* Animation = GetPanelTransitionAnimation(Panel, bEntering);
        // Fall back as a group rather than mixing a native transform writer with UMG animations.
        if (!Animation || Animation->GetEndTime() <= Animation->GetStartTime()) return false;
        Animations.AddUnique(Animation);
    }
    CancelPanelAnimations();
    bPlayingWidgetTransition = true;
    PendingPanelAnimations = Animations.Num();
    PanelAnimationFinishedDelegate.BindDynamic(this, &ThisClass::HandlePanelAnimationFinished);
    if (bEntering)
    {
        if (LeftPanel) LeftPanel->SetRenderOpacity(0.f);
        if (bManaging && ManagementPanel) ManagementPanel->SetRenderOpacity(0.f);
        if (bManaging && MemberStripPanel) MemberStripPanel->SetRenderOpacity(0.f);
    }
    for (UWidgetAnimation* Animation : Animations)
    {
        PlayingPanelAnimations.Add(Animation);
        BindToAnimationFinished(Animation, PanelAnimationFinishedDelegate);
        PlayAnimation(Animation, 0.f, 1, EUMGSequencePlayMode::Forward,
            FMath::Max(0.01f, WidgetAnimationPlaybackSpeed), false);
    }
    return true;
}

void USquadMeetingRoomWidget::CancelPanelAnimations(bool bStop)
{
    bPlayingWidgetTransition = false;
    PendingPanelAnimations = 0;
    const auto Animations = MoveTemp(PlayingPanelAnimations);
    // StopAnimation also reports completion. Detach every callback before stopping any player.
    // Reuse the original delegate: creating a new binding during BeginDestroy is invalid.
    for (UWidgetAnimation* Animation : Animations) if (Animation) UnbindFromAnimationFinished(Animation, PanelAnimationFinishedDelegate);
    PanelAnimationFinishedDelegate.Unbind();
    if (bStop) for (UWidgetAnimation* Animation : Animations) if (Animation) StopAnimation(Animation);
}

void USquadMeetingRoomWidget::HandlePanelAnimationFinished()
{
    if (bPlayingWidgetTransition && PendingPanelAnimations > 0) --PendingPanelAnimations;
}

void USquadMeetingRoomWidget::BeginLayerChange(ESquadRoomLayer Layer)
{
    SetIconPickerVisible(false);
    SetVehiclePickerVisible(false);
    TargetLayer = Layer; Transition = ETransition::ChangeLayerOut; TransitionTime = 0; bTransitioning = true;
    SetPanelsEnabled(false);
    TryPlayWidgetTransition(false);
    if (TransitionDuration <= 0) { UpdateTransition(0); UpdateTransition(0); }
}

void USquadMeetingRoomWidget::LoadSceneUI_Implementation()
{
    CancelPanelAnimations();
    bClosing = false;
    BindManagers();
    CurrentLayer = TargetLayer = ESquadRoomLayer::SquadSelection;
    DiscardDraft();
    ApplyLayerVisibility(); RefreshView();
    Transition = ETransition::EnterRoom; TransitionTime = 0; bTransitioning = true;
    MoveSquadPanel(LeftPanel, FVector2D(-PanelSlideWidth(LeftPanel),0),0);
    SetPanelsEnabled(false);
    TryPlayWidgetTransition(true);
    Super::LoadSceneUI_Implementation();
}

void USquadMeetingRoomWidget::UnloadSceneUI_Implementation()
{
    const bool bInterrupted = bTransitioning;
    bClosing = true;
    DiscardDraft();
    // Keep the currently mounted layer until its exit finishes; cancel any incoming phase.
    int32 PanelIndex = 0;
    for (UWidget* Panel : {LeftPanel.Get(), ManagementPanel.Get(), MemberStripPanel.Get()})
    {
        ExitOffsets[PanelIndex] = Panel ? Panel->GetRenderTransform().Translation : FVector2D::ZeroVector;
        ExitOpacities[PanelIndex] = Panel ? Panel->GetRenderOpacity() : 0.f;
        ++PanelIndex;
    }
    Transition = ETransition::ExitRoom; TransitionTime = 0; bTransitioning = true;
    CancelPanelAnimations();
    StopAllAnimations(); SetPanelsEnabled(false);
    // On interruption continue from the captured pose instead of snapping to the exit's first key.
    if (!bInterrupted) TryPlayWidgetTransition(false);
    Super::UnloadSceneUI_Implementation();
}

void USquadMeetingRoomWidget::UpdateTransition(float DeltaTime)
{
    if (Transition == ETransition::None) return;
    if (bPlayingWidgetTransition)
    {
        // Completion is processed on Tick, outside the animation callback stack. This also leaves
        // time for callers of CreateSceneUIByTag to bind OnLoadCompleted before it can fire.
        if (PendingPanelAnimations == 0)
        {
            CancelPanelAnimations(false);
            FinishTransition();
        }
        return;
    }
    TransitionTime += DeltaTime;
    const float T = TransitionDuration <= 0 ? 1.f : FMath::Clamp(TransitionTime / TransitionDuration, 0.f, 1.f);
    const float Eased = FMath::InterpEaseInOut(0.f,1.f,T,2.f);
    const bool bExit = Transition == ETransition::ExitRoom || Transition == ETransition::ChangeLayerOut;
    const float VisibleAmount = bExit ? 1.f - Eased : Eased;
    if (Transition == ETransition::ExitRoom)
    {
        MoveSquadPanel(LeftPanel, FMath::Lerp(ExitOffsets[0], FVector2D(-PanelSlideWidth(LeftPanel),0), Eased), ExitOpacities[0]*(1-Eased));
        MoveSquadPanel(ManagementPanel, FMath::Lerp(ExitOffsets[1], FVector2D(PanelSlideWidth(ManagementPanel),0), Eased), ExitOpacities[1]*(1-Eased));
        MoveSquadPanel(MemberStripPanel, FMath::Lerp(ExitOffsets[2], FVector2D(0,200), Eased), ExitOpacities[2]*(1-Eased));
    }
    else
    {
        MoveSquadPanel(LeftPanel, FVector2D(-(1.f-VisibleAmount)*PanelSlideWidth(LeftPanel),0), VisibleAmount);
        if (CurrentLayer == ESquadRoomLayer::SquadManagement)
        {
            MoveSquadPanel(ManagementPanel, FVector2D((1.f-VisibleAmount)*PanelSlideWidth(ManagementPanel),0), VisibleAmount);
            MoveSquadPanel(MemberStripPanel, FVector2D(0,(1.f-VisibleAmount)*200.f), VisibleAmount);
        }
    }
    if (T < 1.f) return;
    FinishTransition();
}

void USquadMeetingRoomWidget::FinishTransition()
{
    const ETransition Finished = Transition;
    TransitionTime = 0;
    if (Finished == ETransition::ChangeLayerOut)
    {
        CurrentLayer = TargetLayer;
        if (CurrentLayer == ESquadRoomLayer::SquadSelection) DiscardDraft();
        bNameDirty = false; bShowAll = false;
        ApplyLayerVisibility(); RefreshView();
        Transition = ETransition::ChangeLayerIn;
        MoveSquadPanel(LeftPanel, FVector2D(-PanelSlideWidth(LeftPanel),0),0);
        if (CurrentLayer == ESquadRoomLayer::SquadManagement)
        {
            MoveSquadPanel(ManagementPanel, FVector2D(PanelSlideWidth(ManagementPanel),0),0);
            MoveSquadPanel(MemberStripPanel, FVector2D(0,200),0);
        }
        TryPlayWidgetTransition(true);
        return;
    }
    Transition = ETransition::None; bTransitioning = false;
    if (Finished == ETransition::ExitRoom) { NotifyUnloadCompleted(); return; }
    SetPanelsEnabled(true);
    RefreshDraftHeader();
    if (Finished == ETransition::EnterRoom) NotifyLoadCompleted();
    else
    {
        // A squad can be disbanded by another system while the panels are moving.
        // Its earlier notification may already have been consumed by RefreshView.
        FSquadData Squad;
        if (CurrentLayer == ESquadRoomLayer::SquadManagement && (!GetSelectedSquad(Squad)
            || (!bNewSquadDraft && (!SquadManager || !SquadManager->GetSquad(SelectedSquadId, Squad)))))
        {
            ReturnToSquadList();
            return;
        }
        OnLayerChanged(CurrentLayer);
    }
}

void USquadMeetingRoomWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    if (bRefreshPending && !bClosing) RefreshView();
    UpdateTransition(DeltaTime);
    UpdateIconPickerPlacement();
}

FReply USquadMeetingRoomWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (!IsUnitTransferPending()) return Super::NativeOnPreviewKeyDown(Geometry, Event);
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
    {
        CancelPendingUnitTransfer();
        return FReply::Handled();
    }
    if (Key == EKeys::Tab || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down
        || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right
        || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down)
    {
        UBasicButtonWidget* Next = ConfirmUnitTransferButton && ConfirmUnitTransferButton->HasUserFocus(GetOwningPlayer())
            ? CancelUnitTransferButton.Get() : ConfirmUnitTransferButton.Get();
        if (Next) Next->SetUserFocus(GetOwningPlayer());
        return FReply::Handled();
    }
    // Only activation keys reach the two modal buttons; shortcuts cannot escape to room controls.
    if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
        return Super::NativeOnPreviewKeyDown(Geometry, Event);
    return FReply::Handled();
}

void USquadMeetingRoomWidget::HandleSquadChanged(FGuid Id) { bRefreshPending = true; bVehicleOptionsDirty = true; }
void USquadMeetingRoomWidget::HandleUnitChanged(FGuid Id) { bRefreshPending = true; }
void USquadMeetingRoomWidget::HandleVehicleChanged(FGuid Id)
{
    // A source update invalidates delayed activations before the next layout refresh.
    CancelVehiclePendingActions();
    bRefreshPending = true;
    bVehicleOptionsDirty = true;
}
void USquadMeetingRoomWidget::HandleSquadChoice(FName Id) { FGuid Guid; if (FGuid::Parse(Id.ToString(),Guid)) SelectSquad(Guid); }
void USquadMeetingRoomWidget::HandlePersonnelChoice(FName Id)
{
    FGuid Guid;
    FSquadData Selected;
    if (!FGuid::Parse(Id.ToString(), Guid) || !CanEditSelectedSquad(Selected)) return;
    FSquadData Source;
    if (!DraftSquad.MemberUnitIds.Contains(Guid) && SquadManager && SquadManager->GetUnitSquad(Guid, Source)
        && Source.SquadId != SelectedSquadId)
        RequestUnitTransfer(Guid, Source);
    else AddUnitToSelectedSquad(Guid);
}
void USquadMeetingRoomWidget::HandleConfirmUnitTransfer() { ConfirmPendingUnitTransfer(); }
void USquadMeetingRoomWidget::HandleCancelUnitTransfer() { CancelPendingUnitTransfer(); }
void USquadMeetingRoomWidget::HandleFilter(FName Id) { SetShowAll(Id == TEXT("All")); }
void USquadMeetingRoomWidget::HandleIcon(FName Id) { SelectPresetIcon(FCString::Atoi(*Id.ToString())); }
void USquadMeetingRoomWidget::HandleCreateSquad()
{
    FGuid Id;
    CreateSquadAtTile(CurrentTileId, SquadManager ? SquadManager->GetNextSquadName() : FText::GetEmpty(), Id);
}
void USquadMeetingRoomWidget::HandleBack() { ReturnToSquadList(); }
void USquadMeetingRoomWidget::HandleSaveName() { if (SquadNameInput) RenameSelectedSquad(SquadNameInput->GetText()); }
void USquadMeetingRoomWidget::HandleSaveSquad() { SaveSelectedSquad(); }
void USquadMeetingRoomWidget::HandleChooseIcon() { SetIconPickerVisible(!IconPickerPanel || !IconPickerPanel->IsVisible()); }
void USquadMeetingRoomWidget::HandleCloseIconPicker() { SetIconPickerVisible(false); }
void USquadMeetingRoomWidget::HandleUseCaptain() { UseCaptainPortrait(); }
void USquadMeetingRoomWidget::HandleChooseVehicle()
{
    FSquadData Squad;
    if (!CanEditSelectedSquad(Squad)) return;
    const uint64 RequestedGeneration = DraftGeneration;
    // Notification may still close this UI or select another squad; source comes from the manager.
    OnVehicleSelectionRequested(SelectedSquadId);
    if (IsValid(this) && RequestedGeneration == DraftGeneration && !bClosing && !bTransitioning
        && bEditingDraft && CurrentLayer == ESquadRoomLayer::SquadManagement)
        SetVehiclePickerVisible(true);
}
void USquadMeetingRoomWidget::HandleVehicleChoice(FName Id)
{
    if (!IsVehiclePickerVisible()) return;
    FGuid VehicleId;
    if (FGuid::Parse(Id.ToString(), VehicleId)) SelectAvailableVehicle(VehicleId);
}
void USquadMeetingRoomWidget::HandleCloseVehiclePicker() { SetVehiclePickerVisible(false); }
void USquadMeetingRoomWidget::HandleClearVehicle()
{
    TArray<FGuid> RemovedIds;
    ClearSelectedSquadVehicle(RemovedIds);
}
void USquadMeetingRoomWidget::HandleRemove(FGuid Id) { RemoveUnitFromSelectedSquad(Id); }
void USquadMeetingRoomWidget::HandlePromote(FGuid Id) { PromoteMemberToCaptain(Id); }
void USquadMeetingRoomWidget::HandleNameChanged(const FText& Text)
{
    if (bWritingName || !bEditingDraft || bClosing || IsUnitTransferPending()) return;
    bNameDirty = true;
    DraftSquad.SquadName = Text;
    ClearError(); RefreshDraftHeader();
}
void USquadMeetingRoomWidget::HandleNameCommitted(const FText& Text, ETextCommit::Type Method)
{
    if (Method == ETextCommit::OnEnter) RenameSelectedSquad(Text);
}
