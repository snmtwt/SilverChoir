#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UBasicButtonWidget;
class UCanvasPanel;
class UH5UI_View;

UENUM(BlueprintType)
enum class EMainMenuAction : uint8
{
	NewGame UMETA(DisplayName = "新游戏"),
	LoadGame UMETA(DisplayName = "加载游戏"),
	Settings UMETA(DisplayName = "设置"),
	Quit UMETA(DisplayName = "退出")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMainMenuActionEvent, EMainMenuAction, Action);

/** H5 仅绘制背景装饰；标题、菜单及全部交互均由原生 UMG 绘制。 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "主菜单界面基类"))
class SILVERCHOIR_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMainMenuWidget(const FObjectInitializer& ObjectInitializer);
	virtual void RemoveFromParent() override;
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "主菜单|背景")
	FString BackgroundURL = TEXT("coui://uiresources/MainMenu/background.html");
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "主菜单|背景", meta = (ClampMin = "1", ClampMax = "120"))
	int32 BackgroundFrameRate = 60;
	UPROPERTY(BlueprintAssignable, Category = "主菜单|事件")
	FMainMenuActionEvent OnMenuActionRequested;
	UPROPERTY(BlueprintAssignable, Category="主菜单|动画") FMainMenuActionEvent OnMenuExitFinished;
	/** 从下往上跳过所选按钮，所选按钮最后；启动间隔为两个实际引擎帧。 */
	UFUNCTION(BlueprintCallable, Category="主菜单|动画") bool PlayMenuExit(EMainMenuAction SelectedAction);
	UFUNCTION(BlueprintCallable, Category="主菜单|动画") void ResetMenuExit();
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="主菜单|动画", meta=(ClampMin="0.05")) float ExitSlideDuration = 0.32f;
	UPROPERTY(BlueprintReadOnly, Category="主菜单|动画") bool bMenuExiting = false;

	UFUNCTION(BlueprintCallable, Category = "主菜单")
	void FocusFirstButton();
	UFUNCTION(BlueprintCallable, Category = "主菜单")
	void CancelPendingInput();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	/** Designer 中必须提供这些同名控件，蓝图编译时校验绑定。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "主菜单")
	TObjectPtr<UH5UI_View> BackgroundView;
	/** 主体构图区；超宽屏时限制横向间距，背景始终铺满视口。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "主菜单")
	TObjectPtr<UCanvasPanel> ContentFrame;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "主菜单")
	TObjectPtr<UBasicButtonWidget> NewGameButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "主菜单")
	TObjectPtr<UBasicButtonWidget> LoadGameButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "主菜单")
	TObjectPtr<UBasicButtonWidget> SettingsButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "主菜单")
	TObjectPtr<UBasicButtonWidget> QuitButton;

private:
	struct FExitItem
	{
		TWeakObjectPtr<UBasicButtonWidget> Button;
		FWidgetTransform Transform;
		float Opacity = 1;
		ESlateVisibility Visibility = ESlateVisibility::Visible;
		float Elapsed = 0;
		float Distance = 600;
	};
	TArray<FExitItem> ExitItems;
	uint64 ExitStartFrame = 0;
	uint64 LastExitTickFrame = MAX_uint64;
	EMainMenuAction ExitAction = EMainMenuAction::NewGame;
	bool bMenuExited = false;
	void TickMenuExit(const FGeometry& Geometry, float DeltaTime);
	FVector2D LastContentSize = FVector2D::ZeroVector;
	void ConfigureBackground();
	UFUNCTION() void HandleNewGame();
	UFUNCTION() void HandleLoadGame();
	UFUNCTION() void HandleSettings();
	UFUNCTION() void HandleQuit();
};
