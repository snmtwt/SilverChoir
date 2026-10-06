#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RegionBlock.generated.h"

class USplineComponent;
class UEHBGeneratedMeshComponent;
class UWidgetComponent;
class URegionNameWidget;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class ARegionBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRegionBlockClicked, ARegionBlock*, Region);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRegionBlockHover, ARegionBlock*, Region, bool, bHovered);

/** 闭合 XY 样条沿局部 +Z 拉伸的可交互区域。顶面名称朝局部 +Z。 */
UCLASS(Blueprintable)
class SILVERCHOIR_API ARegionBlock : public AActor
{
	GENERATED_BODY()
public:
	ARegionBlock();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="区域|组件") TObjectPtr<USplineComponent> Boundary;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="区域|组件") TObjectPtr<UEHBGeneratedMeshComponent> VolumeMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="区域|组件") TObjectPtr<UWidgetComponent> NameWidget;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域") FText RegionName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|形状", meta=(ClampMin="1")) float Height = 200;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|形状", meta=(ClampMin="1")) float RectangleWidth = 1000;
	/** 0 表示与宽度相同，生成正方形。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|形状", meta=(ClampMin="0")) float RectangleLength = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|形状", meta=(ClampMin="1")) float CurveSampleSpacing = 80;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|形状") bool bAutoRebuild = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="区域|形状") FText LastBuildError;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|外观") TSoftObjectPtr<UMaterialInterface> RegionMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|外观") FLinearColor NormalColor = FLinearColor(.025f, .22f, .36f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|外观") FLinearColor HighlightColor = FLinearColor(.12f, .7f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称") TSubclassOf<URegionNameWidget> NameWidgetClass;
	/** 原生静态名称默认仅在外观改变时重绘；开启后逐帧更新。自定义名称类始终持续更新，保留蓝图动画。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称", meta=(DisplayName="名称持续更新")) bool bContinuouslyUpdateName = false;
	/** WidgetComponent 表面材质，需提供 SlateUI 纹理参数；不是 TextBlock 的字体材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称") TSoftObjectPtr<UMaterialInterface> NameMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称", meta=(ClampMin="0", DisplayName="名称发光强度")) float NameGlowStrength = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称", meta=(ClampMin="0", DisplayName="名称高亮发光强度")) float NameHoverGlowStrength = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称") FVector2D NameDrawSize = FVector2D(600, 100);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称", meta=(ClampMin="0.01")) float NameScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域|名称", meta=(ClampMin="0.1")) float NameTopOffset = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="区域") bool bInitiallyShown = true;
	UPROPERTY(BlueprintReadOnly, Category="区域") bool bHovered = false;
	UPROPERTY(BlueprintAssignable, Category="区域") FRegionBlockClicked OnRegionClicked;
	/** 在子蓝图中重写点击行为；仅在可交互区域收到鼠标左键点击时触发。 */
	UFUNCTION(BlueprintNativeEvent, Category="区域", meta=(DisplayName="区域被点击"))
	void ReceiveRegionClicked();
	virtual void ReceiveRegionClicked_Implementation();
	UPROPERTY(BlueprintAssignable, Category="区域") FRegionBlockHover OnRegionHoverChanged;
	UFUNCTION(CallInEditor, BlueprintCallable, Category="区域|形状", meta=(DisplayName="根据闭合曲线生成体积")) bool RebuildVolume();
	UFUNCTION(CallInEditor, BlueprintCallable, Category="区域|形状", meta=(DisplayName="生成矩形曲线与体积")) void GenerateRectangle();
	UFUNCTION(BlueprintCallable, Category="区域", meta=(DisplayName="设置区域显示")) void SetRegionShown(bool bShown);
	UFUNCTION(BlueprintCallable, Category="区域", meta=(DisplayName="显示区域")) void ShowRegion() { SetRegionShown(true); }
	UFUNCTION(BlueprintCallable, Category="区域", meta=(DisplayName="隐藏区域")) void HideRegion() { SetRegionShown(false); }
	UFUNCTION(BlueprintPure, Category="区域") bool IsRegionShown() const { return bShown && bHasValidMesh; }
	UFUNCTION(BlueprintCallable, Category="区域") void SetRegionName(const FText& Name);
private:
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
	UPROPERTY() bool bHasValidMesh = false;
	bool bShown = true;
	bool bClickPulse = false;
	FTimerHandle ClickTimer;
	void UpdateAppearance();
	void ConfigureAppearance();
	void EndClickPulse();
	UFUNCTION() void CursorEntered(UPrimitiveComponent* Component);
	UFUNCTION() void CursorLeft(UPrimitiveComponent* Component);
	UFUNCTION() void Clicked(UPrimitiveComponent* Component, FKey Button);
};
