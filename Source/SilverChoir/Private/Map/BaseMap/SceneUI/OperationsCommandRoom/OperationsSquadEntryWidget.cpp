#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsSquadEntryWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "UIBasic/PortraitLibrary.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"

void UOperationsMemberEntryWidget::SetMember(const FUnitData& Unit, bool bCaptain)
{
    if (MemberPortrait) MemberPortrait->SetUnitPortrait(Unit.Profile);
    FText Name = Unit.Profile.CodeName;
    if (Name.IsEmpty()) Name = FText::Format(FText::FromString(TEXT("{0} {1}")), Unit.Profile.FirstName, Unit.Profile.LastName);
    if (MemberNameText) MemberNameText->SetText(Name);
    if (CaptainText) CaptainText->SetVisibility(bCaptain ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
void UOperationsSquadEntryWidget::SetSquad(UPlayerSquadManagerBase* Manager, const FSquadData& Squad)
{
    SquadId = Squad.SquadId;
    if (SquadNameText) SquadNameText->SetText(Squad.SquadName);
    if (MemberCountText) MemberCountText->SetText(FText::FromString(FString::Printf(TEXT("成员 %d/%d"), Squad.MemberUnitIds.Num(), Squad.GetMaxMemberCount())));
    if (SquadIconImage && Manager)
    {
        FSlateBrush Brush = UPortraitLibrary::MakePortraitBrush(Manager->GetSquadIcon(SquadId));
        if (Squad.IconSource == ESquadIconSource::CaptainPortrait)
            if (const auto Captain = UPlayerUnitLibrary::GetUnitDataShared(this, Squad.CaptainUnitId)) Brush = UPortraitLibrary::GetSquarePortrait(Captain->Profile);
        SquadIconImage->SetBrush(Brush);
    }
    const auto Vehicle = Manager ? Manager->GetSquadVehicleShared(SquadId) : nullptr;
    UTexture2D* Picture = Vehicle ? Vehicle->Profile.PreviewImage.Get() : nullptr;
    if (VehicleImage) { VehicleImage->SetBrushFromTexture(Picture ? Picture : NoVehicleTexture.Get()); VehicleImage->SetDesiredSizeOverride(FVector2D(160,90)); }
    if (VehicleEmptyText) { VehicleEmptyText->SetText(FText::FromString(Vehicle ? TEXT("暂无图片") : TEXT("无载具"))); VehicleEmptyText->SetVisibility(Picture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
    if (Members)
    {
        Members->ClearChildren();
        if (Manager && MemberEntryClass)
            for (const auto& Unit : Manager->GetSquadUnitsShared(SquadId)) if (Unit)
            {
                auto* Row = CreateWidget<UOperationsMemberEntryWidget>(GetOwningPlayer(), MemberEntryClass);
                if (Row) { Members->AddChild(Row); Row->SetMember(*Unit, Unit->UnitId == Squad.CaptainUnitId); }
            }
    }
    UpdateReveal();
}
void UOperationsSquadEntryWidget::RequestExpansion() { OnExpansionRequested.Broadcast(SquadId); }
void UOperationsSquadEntryWidget::SetExpanded(bool bValue, bool bAnimate)
{
    bExpanded = bValue;
    if (!bAnimate) Reveal = bExpanded ? 1.f : 0.f;
    if (HeaderButton) HeaderButton->SetSelected(bExpanded);
    if (ExpandText) ExpandText->SetText(FText::FromString(bExpanded ? TEXT("−") : TEXT("+")));
    UpdateReveal();
}
void UOperationsSquadEntryWidget::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    const float Next = FMath::FInterpConstantTo(Reveal, bExpanded ? 1.f : 0.f, Delta, 1.f / FMath::Max(.01f, ExpandDuration));
    if (Next != Reveal) { Reveal = Next; UpdateReveal(); }
}
void UOperationsSquadEntryWidget::UpdateReveal()
{
    const float Eased = Reveal * Reveal * (3.f - 2.f * Reveal);
    if (MemberRevealSize) MemberRevealSize->SetHeightOverride((Members ? Members->GetChildrenCount() : 0) * MemberRowHeight * Eased);
    if (Members) Members->SetRenderOpacity(Eased);
}
