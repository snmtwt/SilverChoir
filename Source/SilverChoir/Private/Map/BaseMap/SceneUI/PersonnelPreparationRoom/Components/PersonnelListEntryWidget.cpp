#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Engine/Texture2D.h"
#include "Components/TextBlock.h"
void UPersonnelListEntryWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    // SynchronizeProperties can run again after construction; retain the profile's face crop.
    if (UnitData && ListPortrait) ListPortrait->SetUnitPortrait(UnitData->Profile);
    else SetPortraitTexture(PortraitTexture);
}
void UPersonnelListEntryWidget::SetPortraitTexture(UTexture2D* Texture)
{
    PortraitTexture=Texture;
    if (ListPortrait) ListPortrait->SetPortraitTexture(Texture);
}

void UPersonnelListEntryWidget::BindUnitData()
{
    if (!UnitData) return;
    UnitData->OnDataChanged.Remove(DataChangedHandle);
    DataChangedHandle=UnitData->OnDataChanged.AddUObject(this,&ThisClass::RefreshUnitData);
    RefreshUnitData(UnitData->UnitId);
}
void UPersonnelListEntryWidget::SetUnitData(TSharedPtr<FUnitData> InData,FName InTileId)
{
    ReleaseUnitData(); UnitData=MoveTemp(InData); CurrentTileId=InTileId; BindUnitData();
}
void UPersonnelListEntryWidget::SetInteractionAllowed(bool bInAllowed)
{
    bInteractionAllowed = bInAllowed;
    RefreshInteractionState();
}
void UPersonnelListEntryWidget::SetCurrentTileId(FName InTileId)
{
    if (CurrentTileId == InTileId) return;
    CurrentTileId = InTileId;
    RefreshUnitData(FGuid());
}
void UPersonnelListEntryWidget::SetSquadAssignmentPresentation(const FText& Text, bool bOtherSquad)
{
    bUseSquadAssignmentPresentation = true;
    bOtherSquadAssignment = bOtherSquad;
    SquadAssignmentText = Text;
    SynchronizeProperties();
}
void UPersonnelListEntryWidget::SynchronizeProperties()
{
    if (bUseSquadAssignmentPresentation) ButtonSubtitle = SquadAssignmentText;
    Super::SynchronizeProperties();
    if (!DetailLabel) return;
    if (!bCapturedSubtitleColor)
    {
        OriginalSubtitleColor = DetailLabel->GetColorAndOpacity();
        bCapturedSubtitleColor = true;
    }
    const FSlateColor Color = bUseSquadAssignmentPresentation && bOtherSquadAssignment
        ? FSlateColor(OtherSquadSubtitleColor) : OriginalSubtitleColor;
    if (DetailLabel->GetColorAndOpacity() != Color) DetailLabel->SetColorAndOpacity(Color);
}
void UPersonnelListEntryWidget::RefreshInteractionState()
{
    const bool bLocal = UnitData && !CurrentTileId.IsNone() && UnitData->RuntimeData.TileId == CurrentTileId;
    SetIsEnabled(bInteractionAllowed && bLocal);
}
void UPersonnelListEntryWidget::ReleaseUnitData()
{
    if (UnitData) UnitData->OnDataChanged.Remove(DataChangedHandle);
    DataChangedHandle.Reset(); UnitData.Reset();
    bUseSquadAssignmentPresentation = bOtherSquadAssignment = false;
    SquadAssignmentText = FText::GetEmpty();
    if (DetailLabel && bCapturedSubtitleColor) DetailLabel->SetColorAndOpacity(OriginalSubtitleColor);
}
void UPersonnelListEntryWidget::NativeConstruct() { Super::NativeConstruct(); BindUnitData(); }
void UPersonnelListEntryWidget::NativeDestruct() { ReleaseUnitData(); Super::NativeDestruct(); }
void UPersonnelListEntryWidget::BeginDestroy() { ReleaseUnitData(); Super::BeginDestroy(); }
void UPersonnelListEntryWidget::RefreshUnitData(FGuid Id)
{
    if (!UnitData) return;
    const auto& P=UnitData->Profile;
    SetPortraitTexture(P.PortraitTexture);
    if (ListPortrait) ListPortrait->SetUnitPortrait(P);
    ButtonText=FText::FromString(P.CodeName.ToString()+TEXT("  /  ")+P.LastName.ToString()+P.FirstName.ToString());
    const bool bLocal=!CurrentTileId.IsNone() && UnitData->RuntimeData.TileId==CurrentTileId;
    ButtonSubtitle=FText::FromString(UnitData->RuntimeData.TileId.ToString()+(bLocal?TEXT("  ·  待命"):TEXT("  ·  异地")));
    RefreshInteractionState();
    SynchronizeProperties();
}
