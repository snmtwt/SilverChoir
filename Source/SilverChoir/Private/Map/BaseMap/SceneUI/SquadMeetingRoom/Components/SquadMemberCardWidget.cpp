#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadMemberCardWidget.h"

#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "UIBasic/BasicButtonWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

void USquadMemberCardWidget::SetUnitData(TSharedPtr<FUnitData> InData, bool bInCaptain, bool bInEditable, int32 InSlotIndex)
{
    if (!InData)
    {
        SetEmptySlot(InSlotIndex);
        return;
    }
    if (UnitData != InData)
    {
        ReleaseUnitData();
        UnitData = MoveTemp(InData);
        if (UnitData) ChangedHandle = UnitData->OnDataChanged.AddUObject(this, &ThisClass::RefreshData);
    }
    bCaptain = bInCaptain;
    bEditable = bInEditable;
    bEmptySlot = false;
    SlotIndex = InSlotIndex;
    RefreshData(FGuid());
}

void USquadMemberCardWidget::SetEmptySlot(int32 InSlotIndex)
{
    ReleaseUnitData();
    bEmptySlot = true;
    bCaptain = bEditable = false;
    SlotIndex = InSlotIndex;
    RefreshData(FGuid());
}

void USquadMemberCardWidget::ReleaseUnitData()
{
    if (UnitData) UnitData->OnDataChanged.Remove(ChangedHandle);
    ChangedHandle.Reset();
    UnitData.Reset();
    bCaptain = bEditable = false;
    bEmptySlot = true;
    SlotIndex = INDEX_NONE;
    CancelPendingActions();
}

void USquadMemberCardWidget::CancelPendingActions()
{
    if (RemoveButton) RemoveButton->CancelPendingClick();
    if (PromoteButton) PromoteButton->CancelPendingClick();
}

void USquadMemberCardWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (RemoveButton) RemoveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRemove);
    if (PromoteButton) PromoteButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePromote);
    RefreshData(FGuid());
}

void USquadMemberCardWidget::NativeDestruct()
{
    if (RemoveButton) RemoveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRemove);
    if (PromoteButton) PromoteButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePromote);
    ReleaseUnitData();
    Super::NativeDestruct();
}

void USquadMemberCardWidget::BeginDestroy() { ReleaseUnitData(); Super::BeginDestroy(); }

void USquadMemberCardWidget::RefreshData(FGuid Id)
{
    SetRenderOpacity(bEmptySlot ? .65f : 1.f);
    if (MemberPortrait)
    {
        MemberPortrait->SetUnitPortrait(UnitData ? UnitData->Profile : FUnitProfile());
        MemberPortrait->SetRenderOpacity(bEmptySlot ? .35f : 1.f);
    }
    if (MemberName)
    {
        const FString Name = UnitData ? UnitData->Profile.CodeName.ToString() : FString();
        const FString DisplayName = bEmptySlot
            ? (SlotIndex >= 0 ? FString::Printf(TEXT("空位 %02d"), SlotIndex + 1) : FString(TEXT("空位")))
            : (Name.IsEmpty() && UnitData ? UnitData->Profile.LastName.ToString() + UnitData->Profile.FirstName.ToString() : Name);
        MemberName->SetText(FText::FromString(DisplayName));
        MemberName->SetToolTipText(MemberName->GetText());
    }
    if (CaptainLabel)
    {
        CaptainLabel->SetText(FText::FromString(bEmptySlot ? TEXT("待编入") : bCaptain ? TEXT("队长") : TEXT("队员")));
        CaptainLabel->SetColorAndOpacity(bCaptain ? CaptainTextColor : MemberTextColor);
        FSlateFontInfo RoleFont = CaptainLabel->GetFont();
        RoleFont.TypefaceFontName = bCaptain ? TEXT("Bold") : TEXT("Regular");
        CaptainLabel->SetFont(RoleFont);
    }
    if (CaptainAccent) CaptainAccent->SetColorAndOpacity(bCaptain ? CaptainTextColor : MemberTextColor);
    if (RemoveButton)
    {
        RemoveButton->SetVisibility(bEmptySlot ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
        RemoveButton->SetIsEnabled(bEditable && UnitData.IsValid() && !bEmptySlot);
    }
    if (PromoteButton)
    {
        PromoteButton->SetVisibility(bEmptySlot ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
        PromoteButton->SetButtonText(FText::FromString(bCaptain ? TEXT("当前队长") : TEXT("升为队长")));
        PromoteButton->SetIsEnabled(bEditable && UnitData.IsValid() && !bCaptain && !bEmptySlot);
    }
}

void USquadMemberCardWidget::HandleRemove()
{
    if (bEditable && UnitData) OnRemoveRequested.Broadcast(UnitData->UnitId);
}

void USquadMemberCardWidget::HandlePromote()
{
    if (bEditable && !bCaptain && UnitData) OnCaptainRequested.Broadcast(UnitData->UnitId);
}
