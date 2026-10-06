#pragma once

#include "UIBasic/BasicButtonWidget.h"
#include "RaisedImageButtonWidget.generated.h"

class UOverlay;
class UTexture2D;
namespace RaisedImageButtonTest { class FInput; class FStates; }

UENUM(BlueprintType)
enum class ERaisedImageButtonPreviewState : uint8
{
	Normal UMETA(DisplayName = "正常"),
	Hovered UMETA(DisplayName = "悬浮"),
	Pressed UMETA(DisplayName = "按下"),
	Disabled UMETA(DisplayName = "禁用"),
	Selected UMETA(DisplayName = "选中"),
	SelectedPressed UMETA(DisplayName = "选中并按下")
};

/** Flat tactical icon button. The existing class name is retained for saved Blueprint references. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "战术图标按钮"))
class SILVERCHOIR_API URaisedImageButtonWidget : public UBasicButtonWidget
{
	GENERATED_BODY()

public:
	URaisedImageButtonWidget(const FObjectInitializer& ObjectInitializer);

	/** Retained only so older Blueprint assets load; flat buttons have no physical elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "按钮|旧版兼容", meta = (DeprecatedProperty, DeprecationMessage = "Flat icon buttons no longer use elevation."))
	float Elevation = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "按钮|旧版兼容", meta = (DeprecatedProperty, DeprecationMessage = "Use PressInset and PressedIconScale."))
	float PressedDepth = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "按钮|旧版兼容", meta = (DeprecatedProperty, DeprecationMessage = "Flat icon buttons use open corner brackets."))
	float CornerRadius = 7.0f;

	/** Use a white image with transparency; these colors tint its visible pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|图标颜色", meta = (DisplayName = "正常颜色"))
	FLinearColor NormalIconColor = FLinearColor(FColor::FromHex(TEXT("8EBAD0")));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|图标颜色", meta = (DisplayName = "悬浮颜色"))
	FLinearColor HoverIconColor = FLinearColor(FColor::FromHex(TEXT("24E5EF")));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|图标颜色", meta = (DisplayName = "选中颜色"))
	FLinearColor SelectedIconColor = FLinearColor(FColor::FromHex(TEXT("C4F8FF")));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|图标颜色", meta = (DisplayName = "按下颜色"))
	FLinearColor PressedIconColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|战术样式", meta = (DisplayName = "角标长度", ClampMin = "2.0", ClampMax = "24.0"))
	float CornerLength = 7.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|战术样式", meta = (DisplayName = "按下角标内收", ClampMin = "0.0", ClampMax = "8.0"))
	float PressInset = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|战术样式", meta = (DisplayName = "按下图标比例", ClampMin = "0.6", ClampMax = "1.0"))
	float PressedIconScale = 0.90f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|战术样式", meta = (DisplayName = "选中底色透明度", ClampMin = "0.0", ClampMax = "1.0"))
	float SelectionFillOpacity = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|悬浮扫描", meta = (DisplayName = "启用悬浮扫描"))
	bool bEnableHoverScan = true;
	/** Seconds for the scan to travel from the lower edge to the upper edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|悬浮扫描", meta = (DisplayName = "扫描周期", ClampMin = "0.25", ClampMax = "10.0", Units = "s", EditCondition = "bEnableHoverScan"))
	float ScanDuration = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|悬浮扫描", meta = (DisplayName = "扫描亮度", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableHoverScan"))
	float ScanOpacity = 0.24f;
	/** Fit textures inside IconSize while retaining their aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "按钮|内容", meta = (DisplayName = "保持图片比例"))
	bool bPreserveImageAspectRatio = true;
	/** Designer-only preview; runtime appearance always follows the real input state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "按钮|设计器预览", meta = (DisplayName = "预览状态"))
	ERaisedImageButtonPreviewState DesignerPreviewState = ERaisedImageButtonPreviewState::Normal;

	/** Set or replace the icon texture. Passing null clears the image. */
	UFUNCTION(BlueprintCallable, Category = "按钮|内容", meta = (DisplayName = "设置按钮图片"))
	void SetButtonImage(UTexture2D* Texture);
	UFUNCTION(BlueprintCallable, Category = "按钮|图标颜色", meta = (DisplayName = "设置图标状态颜色"))
	void SetIconTintColors(FLinearColor Normal, FLinearColor Hovered, FLinearColor Selected, FLinearColor Pressed);

	virtual void SynchronizeProperties() override;
	virtual void SetIsEnabled(bool bInIsEnabled) override;
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
	virtual FLinearColor GetButtonContentTint(bool bEnabled) const override;
	virtual int32 PaintButtonFrame(const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

	/** Optional content host, retained for the existing Blueprint tree. It remains stationary. */
	UPROPERTY(BlueprintReadOnly, Category = "按钮|绑定", meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> FaceContent;

private:
	friend class RaisedImageButtonTest::FInput;
	friend class RaisedImageButtonTest::FStates;
	void ApplyFlatContent(bool bRefreshTint = true);
	bool IsFacePressed() const;
	bool IsFaceSelected() const;
	bool IsFaceHovered() const;
	bool IsPreviewDisabled() const;
	void InvalidateFrame() const;
	uint32 GetFrameStyleHash() const;
	uint32 LastFrameStyleHash = 0;
	uint8 LastFrameState = 255;
	float HoverScanPhase = 0.0f;
	bool bScanVisible = false;
	bool bPointerInside = false;
	uint32 VisualGeneration = 0;
	bool bVisualsActive = false;
};
