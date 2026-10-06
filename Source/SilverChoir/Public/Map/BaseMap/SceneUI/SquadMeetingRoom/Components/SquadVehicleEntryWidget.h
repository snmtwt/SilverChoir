#pragma once

#include "UIBasic/SelectionButtonWidget.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "SquadVehicleEntryWidget.generated.h"

/** 车辆选择卡。蓝图负责 16:9 图片和文字布局，沿用基础按钮的声音与延迟点击。 */
UCLASS(Blueprintable, meta=(DisplayName="小队车辆选择卡"))
class SILVERCHOIR_API USquadVehicleEntryWidget : public USelectionButtonWidget
{
    GENERATED_BODY()
public:
    USquadVehicleEntryWidget(const FObjectInitializer& Initializer);

    UFUNCTION(BlueprintCallable, Category="小队|车辆")
    void SetVehicleEntry(const FVehicleData& Vehicle, bool bInSelected, bool bCanSelect, const FText& Status);
    void SetVehicleDataShared(const TSharedPtr<FVehicleData>& Vehicle, bool bInSelected, bool bCanSelect, const FText& Status);
    void ReleaseVehicleData();
    UFUNCTION(BlueprintPure, Category="小队|车辆")
    FGuid GetVehicleId() const { return VehicleData.IsValid() ? VehicleData->VehicleId : FGuid(); }

    virtual void SynchronizeProperties() override;
    virtual void BeginDestroy() override;
protected:
    virtual void NativeDestruct() override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UImage> VehicleImage;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UTextBlock> VehicleNameText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UTextBlock> VehicleDetailsText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UTextBlock> VehicleStatusText;
private:
    // Shared canonical record only. The owning room listens to manager change notifications.
    TSharedPtr<FVehicleData> VehicleData;
    UPROPERTY(Transient) FText EntryStatus;
};
