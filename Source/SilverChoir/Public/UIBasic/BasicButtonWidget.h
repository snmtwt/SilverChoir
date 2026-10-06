#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Templates/PimplPtr.h"
#include "BasicButtonWidget.generated.h"

class UImage;
class UTextBlock;
class USizeBox;
class UHorizontalBoxSlot;
class USoundBase;
struct FDelayedButtonPress;

UENUM(BlueprintType)
enum class EBasicButtonContent : uint8
{
	IconAndText UMETA(DisplayName = "图标和文字"),
	TextOnly UMETA(DisplayName = "仅文字"),
	IconOnly UMETA(DisplayName = "仅图标")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBasicButtonEvent);

/** Shared game tooltip presentation; content continues to use the standard UMG tooltip text. */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FGameToolTipStyle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="背景颜色"))
	FLinearColor BackgroundColor = FLinearColor(FColor(6,18,28,246));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="边框颜色"))
	FLinearColor BorderColor = FLinearColor(FColor(35,91,112,220));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="强调颜色"))
	FLinearColor AccentColor = FLinearColor(FColor(36,201,220));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="文字颜色"))
	FLinearColor TextColor = FLinearColor(FColor(205,225,235));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="字号",ClampMin="8",ClampMax="32"))
	float FontSize = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="最大宽度",ClampMin="96",ClampMax="960"))
	float MaxWidth = 320.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="提示样式", meta=(DisplayName="鼠标静止等待时间",ClampMin="0",ClampMax="10",Units="s"))
	float HoverDelaySeconds = 1.5f;
};

/** 原生 UMG 按钮，不使用 UButton；输入开始后两帧显示按下反馈。 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "基础按钮"))
class SILVERCHOIR_API UBasicButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBasicButtonWidget(const FObjectInitializer& ObjectInitializer);
	virtual ~UBasicButtonWidget() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "显示模式"))
	EBasicButtonContent ContentMode = EBasicButtonContent::TextOnly;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "文字"))
	FText ButtonText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "副标题"))
	FText ButtonSubtitle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "编号"))
	FText ButtonIndex;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "图标"))
	FSlateBrush IconBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "字体"))
	FSlateFontInfo Font;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|内容", meta = (DisplayName = "图标尺寸"))
	FVector2D IconSize = FVector2D(24.0, 24.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|布局", meta = (DisplayName = "最小尺寸"))
	FVector2D MinimumSize = FVector2D(240.0, 58.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|样式", meta = (DisplayName = "背景颜色"))
	FLinearColor BackgroundColor;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|样式", meta = (DisplayName = "边框颜色"))
	FLinearColor BorderColor;
	/** 按实际屏幕比例取整，至少一像素；四边采用同一厚度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|样式", meta = (DisplayName = "边框厚度", ClampMin = "1.0"))
	float BorderThickness = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|样式", meta = (DisplayName = "强调颜色"))
	FLinearColor AccentColor;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|样式", meta = (DisplayName = "文字颜色"))
	FLinearColor ButtonForegroundColor;
	/** 可选；输入开始时立即播放，留空时只发出 OnPressStarted。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|声音", meta = (DisplayName = "点击预播放声音"))
	TObjectPtr<USoundBase> PressSound;
	/** 每次鼠标进入可交互按钮时播放一次；留空则静音。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|声音", meta = (DisplayName = "悬浮声音"))
	TObjectPtr<USoundBase> HoverSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="按钮|悬浮提示", meta=(DisplayName="使用游戏提示样式"))
	bool bUseGameToolTipStyle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="按钮|悬浮提示", meta=(DisplayName="提示样式",EditCondition="bUseGameToolTipStyle"))
	FGameToolTipStyle ToolTipStyle;

	UPROPERTY(BlueprintAssignable, Category = "按钮|事件", meta = (DisplayName = "点击"))
	FBasicButtonEvent OnClicked;
	UPROPERTY(BlueprintAssignable, Category = "按钮|事件", meta = (DisplayName = "输入开始（声音入口）"))
	FBasicButtonEvent OnPressStarted;
	UPROPERTY(BlueprintAssignable, Category = "按钮|事件", meta = (DisplayName = "鼠标进入"))
	FBasicButtonEvent OnHovered;
	UPROPERTY(BlueprintAssignable, Category = "按钮|事件", meta = (DisplayName = "鼠标离开"))
	FBasicButtonEvent OnUnhovered;

	UFUNCTION(BlueprintCallable, Category = "按钮")
	void SetButtonText(const FText& InText);
	UFUNCTION(BlueprintCallable, Category = "按钮")
	void SetIconBrush(const FSlateBrush& InBrush);
	UFUNCTION(BlueprintCallable, Category = "按钮")
	void SetContentMode(EBasicButtonContent InMode);
	UFUNCTION(BlueprintCallable, Category = "按钮")
	void SetSelected(bool bInSelected);
	UFUNCTION(BlueprintCallable, Category = "按钮")
	void CancelPendingClick();
	UFUNCTION(BlueprintPure, Category = "按钮")
	bool IsPressPending() const;
	/** True only after the delayed press feedback begins, not while input is pending. */
	UFUNCTION(BlueprintPure, Category = "按钮")
	bool IsButtonVisuallyPressed() const;
	virtual void SetIsEnabled(bool bInIsEnabled) override;
	virtual void SetVisibility(ESlateVisibility InVisibility) override;
	virtual void SynchronizeProperties() override;

protected:
    virtual int32 PaintButtonFrame(const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const;
    float GetButtonVisualStrength() const;
    bool IsButtonSelected() const { return bSelected; }
	/** Derived button styles can tint white icons without writing a second color every tick. */
	virtual FLinearColor GetButtonContentTint(bool bEnabled) const;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeOnFocusLost(const FFocusEvent& Event) override;

private:
	friend class FSilverChoirButtonWidgetTest;
	friend class FSilverChoirButtonIdleFeedbackTest;
    friend class SBasicButtonFrame;
	void ApplyContent();
	bool BeginPress(const FKey& Key);
	bool IsInputAllowed() const;
	TPimplPtr<FDelayedButtonPress> Press;
	FKey ActiveKey;
	int32 ActivePointerUser = 0;
	uint32 ActivePointerIndex = 0;
	bool bPointerHovered = false;
	bool bSelected = false;
	float HoverAmount = 0.0f;

#if WITH_DEV_AUTOMATION_TESTS
	// Count actual feedback writes/Slate capture queries for the idle UI benchmark.
	uint64 FeedbackColorWriteCount = 0;
	uint64 CaptureQueryCount = 0;
#endif

protected:
	/** 可在 Widget Blueprint 中重新排布。未提供 Designer 树时仍有基础图文布局。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<USizeBox> ButtonSize;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<USizeBox> IconSizeBox;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<UImage> IconImage;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<UTextBlock> Label;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<UTextBlock> DetailLabel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "按钮|绑定") TObjectPtr<UTextBlock> IndexLabel;

private:
	UPROPERTY(Transient) TObjectPtr<UHorizontalBoxSlot> IconSlot;
};
