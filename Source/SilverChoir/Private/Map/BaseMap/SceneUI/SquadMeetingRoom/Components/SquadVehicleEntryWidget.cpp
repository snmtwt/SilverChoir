#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadVehicleEntryWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"

USquadVehicleEntryWidget::USquadVehicleEntryWidget(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    bUseTabStyle = false;
    MinimumSize = FVector2D(180.f, 200.f);
}

void USquadVehicleEntryWidget::SetVehicleEntry(const FVehicleData& Vehicle, bool bInSelected,
    bool bCanSelect, const FText& Status)
{
    const auto* Manager = UPlayerUnitLibrary::GetPlayerUnitManager(this);
    SetVehicleDataShared(Manager ? Manager->GetVehicleDataShared(Vehicle.VehicleId) : nullptr,
        bInSelected, bCanSelect, Status);
}

void USquadVehicleEntryWidget::SetVehicleDataShared(const TSharedPtr<FVehicleData>& Vehicle, bool bInSelected,
    bool bCanSelect, const FText& Status)
{
    VehicleData = Vehicle;
    EntryStatus = Status;
    if (!VehicleData.IsValid())
    {
        ChoiceID = NAME_None;
        ButtonText = FText::GetEmpty();
        ButtonSubtitle = FText::GetEmpty();
        SetIsEnabled(false);
        SynchronizeProperties();
        return;
    }
    ChoiceID = FName(*Vehicle->VehicleId.ToString());
    ButtonText = Vehicle->Profile.VehicleName.IsEmpty()
        ? NSLOCTEXT("SquadVehicles", "UnnamedVehicle", "未命名车辆") : Vehicle->Profile.VehicleName;
    ButtonSubtitle = FText::FromString(FString::Printf(TEXT("%d 座（含驾驶席）  ·  %s"),
        Vehicle->Attributes.PassengerCapacity, Vehicle->RuntimeData.TileId.IsNone()
            ? TEXT("位置未设置") : *Vehicle->RuntimeData.TileId.ToString()));
    SetSelected(bInSelected);
    SetIsEnabled(bCanSelect);
    SynchronizeProperties();
}

void USquadVehicleEntryWidget::ReleaseVehicleData()
{
    CancelPendingClick();
    VehicleData.Reset();
}

void USquadVehicleEntryWidget::NativeDestruct()
{
    ReleaseVehicleData();
    Super::NativeDestruct();
}

void USquadVehicleEntryWidget::BeginDestroy()
{
    ReleaseVehicleData();
    Super::BeginDestroy();
}

void USquadVehicleEntryWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (VehicleImage)
    {
        UTexture2D* Preview = VehicleData.IsValid() ? VehicleData->Profile.PreviewImage.Get() : nullptr;
        if (VehicleImage->GetBrush().GetResourceObject() != Preview)
            VehicleImage->SetBrushFromTexture(Preview);
        VehicleImage->SetVisibility(Preview
            ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    }
    if (VehicleNameText && !VehicleNameText->GetText().EqualTo(ButtonText)) VehicleNameText->SetText(ButtonText);
    if (VehicleDetailsText && !VehicleDetailsText->GetText().EqualTo(ButtonSubtitle)) VehicleDetailsText->SetText(ButtonSubtitle);
    if (VehicleStatusText && !VehicleStatusText->GetText().EqualTo(EntryStatus)) VehicleStatusText->SetText(EntryStatus);
}
