#pragma once
#include "CoreMinimal.h"
#include "MTS_MapLoadingWidget.h"
#include "MenuLoadingWidget.generated.h"

class UH5UI_View;
class UCanvasPanel;
class UTextBlock;
class UProgressBar;

/** 与主菜单共享背景；布局由 /Game/System/UIBasic/WBP_MenuLoading 编辑。 */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UMenuLoadingWidget : public UMTS_MapLoadingWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="加载界面") FString BackgroundURL = TEXT("coui://uiresources/MainMenu/background.html");
protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UH5UI_View> BackgroundView;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UCanvasPanel> ContentFrame;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UTextBlock> LoadingTitle;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UTextBlock> LoadingStatus;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UTextBlock> LoadingPercent;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget)) TObjectPtr<UProgressBar> LoadingProgress;
private:
	void ConfigureBackground();
	void RefreshProgress();
};
