#pragma once
#include "Blueprint/UserWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "BattleHUDWidgets.generated.h"
class UImage;
class UProgressBar;
class UPanelWidget;
class UTextBlock;

UENUM(BlueprintType)
enum class EBattleVisualKind : uint8 { Glyph, ECG, MiniMap };

/** Small vector icons/ECG/map projection; all surrounding layout remains in Widget Blueprint Designer. */
UCLASS(Blueprintable)
class SILVERCHOIR_API UBattleHUDVisual : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="绘制") EBattleVisualKind Kind = EBattleVisualKind::Glyph;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="绘制") EBattleGlyph Glyph = EBattleGlyph::Person;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="绘制") FLinearColor Tint = FLinearColor(.48f,.76f,.9f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="绘制") float HealthRatio = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="绘制") TObjectPtr<UTexture2D> Image;
    UPROPERTY(BlueprintReadWrite, Category="小地图") TArray<FVector2D> Markers;
    UPROPERTY(BlueprintReadWrite, Category="小地图") int32 SelectedMarker = INDEX_NONE;
    UPROPERTY(BlueprintReadWrite, Category="小地图") float Zoom = 1.f;
    UPROPERTY(BlueprintReadWrite, Category="小地图") bool bShowGrid = true;
    UPROPERTY(BlueprintReadWrite, Category="小地图") FVector2D Center = FVector2D(.5f,.5f);
protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
private:
    float Phase = 0.f;
};

UCLASS(Blueprintable)
class SILVERCHOIR_API UBattleHUDButton : public USelectionButtonWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗按钮") EBattleGlyph Glyph = EBattleGlyph::Person;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗按钮") bool bShowGlyph = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗按钮") bool bVerticalLabel = false;
    UFUNCTION(BlueprintPure, Category="战斗按钮", meta=(DisplayName="是否选中战斗按钮"))
    bool IsSelected() const { return IsButtonSelected(); }
protected:
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDVisual> Symbol;
    virtual void NativePreConstruct() override;
};

UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UBattleMemberCardWidget : public USelectionButtonWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="战斗UI|人员") virtual void SetMember(const FBattleMemberView& Data);
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|人员") FBattleMemberView Member;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UImage> Portrait;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UTextBlock> MemberName;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UTextBlock> HealthText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UProgressBar> StaminaBar;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UProgressBar> MoraleBar;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UBattleHUDVisual> Heartbeat;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UPanelWidget> SplitHands;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定") TObjectPtr<UPanelWidget> MergedHands;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDVisual> LeftWeapon;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDVisual> RightWeapon;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDVisual> LinkedWeapon;
protected:
    virtual void NativeConstruct() override;
private:
    void RefreshMember();
};
