#pragma once

#include "UIBasic/SelectionButtonWidget.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "BattleSquadEntryWidget.generated.h"

class UBattleHUDVisual;
class UWidgetTree;

/** Compact squad choice. Only the owning HUD changes selection through SetSelected. */
UCLASS(Blueprintable, meta=(DisplayName="战斗小队项"))
class SILVERCHOIR_API UBattleSquadEntryWidget : public USelectionButtonWidget
{
    GENERATED_BODY()
public:
    UBattleSquadEntryWidget(const FObjectInitializer& Initializer);

    /** DisplayIndex is zero based; the visible ordinal starts at 01. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|小队", meta=(DisplayName="设置小队数据"))
    void SetSquad(const FBattleSquadView& Data, int32 DisplayIndex);
    UFUNCTION(BlueprintPure, Category="战斗UI|小队", meta=(DisplayName="是否选中小队"))
    bool IsSquadSelected() const { return IsButtonSelected(); }

    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|小队") FBattleSquadView Squad;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|小队") FGuid SquadId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|小队") int32 SquadDisplayIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|设计预览") bool bPreviewSelected = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|设计预览") FText PreviewSquadName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|设计预览", meta=(ClampMin="0")) int32 PreviewMemberCount = 4;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UImage> SquadIcon;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UTextBlock> SquadNameLabel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UTextBlock> MemberCountLabel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UTextBlock> SquadIndexLabel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UBattleHUDVisual> FallbackSymbol;

    /** Shared by the native fallback and the editor migration; never replaces an existing root. */
    static void BuildDefaultWidgetTree(UWidgetTree* Tree);
    virtual void SynchronizeProperties() override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual int32 PaintButtonFrame(const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;

private:
    bool bHasSquadData = false;
    void RefreshSquad();
};
