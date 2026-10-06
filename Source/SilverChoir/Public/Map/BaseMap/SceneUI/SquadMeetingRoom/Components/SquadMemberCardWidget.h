#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Units/UnitStructs.h"
#include "SquadMemberCardWidget.generated.h"

class UPersonnelPortraitWidget;
class UTextBlock;
class UBasicButtonWidget;
class UImage;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSquadMemberAction, FGuid, UnitId);

/** 横向席位卡片。实员只持有单位管理类的共享数据；空位不持有或订阅单位数据。 */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API USquadMemberCardWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetUnitData(TSharedPtr<FUnitData> InData, bool bInCaptain, bool bInEditable, int32 InSlotIndex = INDEX_NONE);
    /** SlotIndex 从 0 开始，界面编号从 1 开始；解除原单位绑定，保留卡片布局。 */
    UFUNCTION(BlueprintCallable, Category="小队|成员", meta=(DisplayName="设置为空成员席位"))
    void SetEmptySlot(int32 InSlotIndex);
    UFUNCTION(BlueprintPure, Category="小队|成员", meta=(DisplayName="是否为空成员席位"))
    bool IsEmptySlot() const { return bEmptySlot; }
    UFUNCTION(BlueprintPure, Category="小队|成员", meta=(DisplayName="获取成员席位索引"))
    int32 GetSlotIndex() const { return SlotIndex; }
    UFUNCTION(BlueprintPure, Category="小队|成员", meta=(DisplayName="获取成员单位ID"))
    FGuid GetUnitId() const { return UnitData ? UnitData->UnitId : FGuid(); }
    /** 仅取消尚未触发的延迟按钮操作；不释放头像/单位引用，供换草稿和退场使用。 */
    void CancelPendingActions();
    void ReleaseUnitData();
    TSharedPtr<FUnitData> GetUnitDataShared() const { return UnitData; }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|样式")
    FLinearColor CaptainTextColor = FLinearColor(FColor::FromHex(TEXT("B69B61")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|样式")
    FLinearColor MemberTextColor = FLinearColor(FColor::FromHex(TEXT("638EA5")));
    UPROPERTY(BlueprintAssignable, Category="小队|成员") FSquadMemberAction OnRemoveRequested;
    UPROPERTY(BlueprintAssignable, Category="小队|成员") FSquadMemberAction OnCaptainRequested;
    virtual void BeginDestroy() override;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UPersonnelPortraitWidget> MemberPortrait;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UTextBlock> MemberName;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UTextBlock> CaptainLabel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UImage> CaptainAccent;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UBasicButtonWidget> RemoveButton;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UBasicButtonWidget> PromoteButton;
private:
    TSharedPtr<FUnitData> UnitData;
    FDelegateHandle ChangedHandle;
    bool bCaptain = false;
    bool bEditable = false;
    bool bEmptySlot = true;
    int32 SlotIndex = INDEX_NONE;
    void RefreshData(FGuid Id);
    UFUNCTION() void HandleRemove();
    UFUNCTION() void HandlePromote();
};
