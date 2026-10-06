#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RegionNameWidget.generated.h"

class UTextBlock;

/** 可用子蓝图替换布局；可选文本控件命名 RegionNameText。 */
UCLASS(Blueprintable)
class SILVERCHOIR_API URegionNameWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="区域")
	void UpdateRegionLabel(const FText& Name, FLinearColor Color, bool bHighlighted);
	UPROPERTY(BlueprintReadOnly, Category="区域") FText RegionName;
	UPROPERTY(BlueprintReadOnly, Category="区域") bool bIsHighlighted = false;
	UPROPERTY(BlueprintReadOnly, Category="区域") FLinearColor LabelColor = FLinearColor::White;
	UFUNCTION(BlueprintImplementableEvent, Category="区域") void OnRegionLabelUpdated();
protected:
	bool bUsesNativeLabel = false;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="区域") TObjectPtr<UTextBlock> RegionNameText;
};
