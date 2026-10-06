#pragma once
#include "UIBasic/SelectionButtonWidget.h"
#include "Data/Units/UnitStructs.h"
#include "PersonnelListEntryWidget.generated.h"
class UPersonnelPortraitWidget;
class UTexture2D;

UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UPersonnelListEntryWidget : public USelectionButtonWidget
{
    GENERATED_BODY()
public:
    void SetUnitData(TSharedPtr<FUnitData> InData, FName InTileId);
    /** 宿主的额外交互限制（例如小队已满）；默认允许，始终与本地瓦片资格共同生效。 */
    void SetInteractionAllowed(bool bInAllowed);
    /** 复用同一单位行时更新宿主位置，不释放或重绑单位数据。 */
    void SetCurrentTileId(FName InTileId);
    /** 小队会议室的归属文案；保留到下一次宿主更新，不被单位属性通知临时覆盖。 */
    void SetSquadAssignmentPresentation(const FText& Text, bool bOtherSquad);
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|样式", meta=(DisplayName="其他小队名称颜色"))
    FLinearColor OtherSquadSubtitleColor = FLinearColor(FColor::FromHex(TEXT("B69B61")));
    TSharedPtr<FUnitData> GetUnitData() const { return UnitData; }
    void ReleaseUnitData();
    virtual void BeginDestroy() override;
    virtual void SynchronizeProperties() override;
    UFUNCTION(BlueprintCallable, Category="人员") void SetPortraitTexture(UTexture2D* Texture);
protected:
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPersonnelPortraitWidget> ListPortrait;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> PortraitTexture;
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    TSharedPtr<FUnitData> UnitData;
    FDelegateHandle DataChangedHandle;
    FName CurrentTileId;
    bool bInteractionAllowed = true;
    bool bUseSquadAssignmentPresentation = false;
    bool bOtherSquadAssignment = false;
    bool bCapturedSubtitleColor = false;
    FSlateColor OriginalSubtitleColor;
    FText SquadAssignmentText;
    void RefreshUnitData(FGuid Id);
    void RefreshInteractionState();
    void BindUnitData();
};
