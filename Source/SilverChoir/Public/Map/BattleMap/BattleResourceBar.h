#pragma once

#include "CoreMinimal.h"
#include "Components/ProgressBar.h"
#include "BattleResourceBar.generated.h"

/**
 * Compact, bottom-to-top resource display. The containing slot supplies its size.
 * Push values with the inherited SetPercent and tint with SetFillColorAndOpacity.
 * Native Slate attributes observe those properties without a widget tick. Legacy
 * UProgressBar delegate bindings, marquee, brushes and other fill directions are
 * intentionally unused; gameplay and data subscriptions remain in the owner.
 */
UCLASS(BlueprintType, meta=(DisplayName="战斗纵向状态条"), HideCategories=(Style))
class SILVERCHOIR_API UBattleResourceBar : public UProgressBar
{
    GENERATED_BODY()

public:
    UBattleResourceBar(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="轨道背景"))
    FLinearColor TrackColor = FLinearColor(.012f, .025f, .037f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="细边颜色"))
    FLinearColor FrameColor = FLinearColor(.070f, .130f, .170f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="填充底端颜色"))
    FLinearColor BottomColor = FLinearColor(.025f, .180f, .420f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="填充顶端颜色"))
    FLinearColor TopColor = FLinearColor(.120f, .620f, .940f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="填充高光颜色"))
    FLinearColor HighlightColor = FLinearColor(.640f, .870f, 1.f, .75f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="刻度颜色"))
    FLinearColor TickColor = FLinearColor(.300f, .420f, .490f, .35f);

    /** Zero disables ticks. Short bars automatically use fewer divisions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态条|样式", meta=(DisplayName="刻度分段数", ClampMin="0", ClampMax="8", UIMin="0", UIMax="8"))
    int32 Divisions = 4;

    /** These three colors are multiplied by the inherited FillColorAndOpacity. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|状态条", meta=(DisplayName="设置状态条渐变颜色"))
    void SetGradientColors(FLinearColor InBottomColor, FLinearColor InTopColor, FLinearColor InHighlightColor);

    virtual void SynchronizeProperties() override;

#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override;
    virtual void OnCreationFromPalette() override;
#endif

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
