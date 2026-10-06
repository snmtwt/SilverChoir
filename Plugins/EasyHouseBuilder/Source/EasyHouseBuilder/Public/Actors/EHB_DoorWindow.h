// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Interfaces/EHBCutSourceProvider.h"
#include "EHB_DoorWindow.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UEHBDoorWindowOpeningSplineComponent;
class UBoxComponent;
class AEHB_Wall;

/**
 * EHB_DoorWindow：门、窗和普通墙面洞口构件的独立 Actor。
 * 它保存洞口尺寸、离地高度和源静态网格体；后续墙体布尔挖洞时会读取这些数据生成洞口。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_DoorWindow", ToolTip = "门窗或墙面洞口的独立 Actor。它保存源静态网格体、洞口宽高、离地高度和门窗类型，后续墙体会根据这些数据进行布尔挖洞与封边。"))
class EASYHOUSEBUILDER_API AEHB_DoorWindow : public AEHBElementActorBase, public IEHBCutSourceProvider
{
	GENERATED_BODY()

public:
	// ===== 组件 =====

	/** 显示门窗外观的静态网格体组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "门窗|组件", meta = (DisplayName = "门窗网格组件", ToolTip = "用于显示门窗源静态网格体的组件。采样生成蓝图后，该组件会根据源静态网格体自动设置朝向和位置。"))
	TObjectPtr<UStaticMeshComponent> DoorWindowMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DoorWindow|Components", meta = (DisplayName = "Opening Outline Spline", ToolTip = "Editable closed cutting outline constrained to the door/window local XZ plane."))
	TObjectPtr<UEHBDoorWindowOpeningSplineComponent> OpeningSplineComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DoorWindow|Components", meta = (DisplayName = "Invalid Placement Indicator", ToolTip = "Editor-only red outline shown while a dragged door/window is outside the valid wall opening range."))
	TObjectPtr<UBoxComponent> InvalidPlacementIndicatorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DoorWindow|Components", meta = (DisplayName = "Selection Proxy", ToolTip = "Editor picking volume that makes thin door/window meshes easier to select in front of walls."))
	TObjectPtr<UBoxComponent> SelectionProxyComponent;

	// ===== 基础参数 =====

	/** 当前构件是门还是窗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|基础", meta = (DisplayName = "构件类型", ToolTip = "指定当前构件是门还是窗。门默认离地高度为 0；窗通常使用窗台高度作为洞口底边离地高度。"))
	EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;

	/** 作为门窗可视外观和洞口尺寸来源的静态网格体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|基础", meta = (DisplayName = "源静态网格体", ToolTip = "用于生成门窗蓝图和推导洞口尺寸的源静态网格体。采样器会把该网格体写入蓝图默认值。"))
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	/** 窗台高度或洞口底边离地高度，单位为厘米。门会自动使用 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|基础", meta = (DisplayName = "离地高度", ToolTip = "洞口底边相对墙体底部或当前楼层地面的高度，单位为厘米。门通常为 0；窗通常大于 0。", ClampMin = "0.0", Units = "cm"))
	float SillHeight = 90.0f;

	/** 是否根据源静态网格体的 XY 长短自动调整门窗网格朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|基础", meta = (DisplayName = "自动调整网格朝向", ToolTip = "开启后，如果源网格体在 Y 方向比 X 方向更宽，会自动旋转网格显示，使门窗宽度统一落在本地 X 方向，厚度落在本地 Y 方向。"))
	bool bAutoOrientMesh = true;

	// ===== 洞口尺寸 =====

	/** 洞口水平宽度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|洞口", meta = (DisplayName = "洞口宽度", ToolTip = "墙体布尔挖洞时使用的洞口水平宽度，单位为厘米。采样时取源网格体 XY 中较长的方向。", ClampMin = "1.0", Units = "cm"))
	float OpeningWidth = 120.0f;

	/** 洞口垂直高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|洞口", meta = (DisplayName = "洞口高度", ToolTip = "墙体布尔挖洞时使用的洞口垂直高度，单位为厘米。采样时取源网格体 Z 方向尺寸。", ClampMin = "1.0", Units = "cm"))
	float OpeningHeight = 120.0f;

	/** 洞口或构件的厚度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|洞口", meta = (DisplayName = "洞口厚度", ToolTip = "门窗构件在墙体厚度方向上的尺寸，单位为厘米。采样时取源网格体 XY 中较短的方向。", ClampMin = "1.0", Units = "cm"))
	float OpeningThickness = 10.0f;

	/** 当前门窗依附的墙体唯一标识，后续放置和挖洞时用于找回墙体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|连接", meta = (DisplayName = "所属墙体标识", ToolTip = "当前门窗依附的墙体 Actor 唯一标识。后续门窗放置到墙体上后，墙体会读取该关系重建洞口。"))
	FGuid OwningWallGuid;

	/** 门窗沿所属墙体起点到终点方向的放置距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗|连接", meta = (DisplayName = "距墙体起点距离", ToolTip = "门窗中心投影到所属墙体中心线后，距离墙体起点的长度。保存和重建门窗位置时会使用这个数值。", ClampMin = "0.0", Units = "cm"))
	float DistanceFromWallStart = 0.0f;

	// ===== 采样记录 =====

	/** 源静态网格体原始包围盒最小值。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "门窗|采样记录", meta = (DisplayName = "源包围盒最小值", ToolTip = "采样时记录的源静态网格体本地包围盒最小值，用于调试和后续重采样判断。"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	/** 源静态网格体原始包围盒最大值。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "门窗|采样记录", meta = (DisplayName = "源包围盒最大值", ToolTip = "采样时记录的源静态网格体本地包围盒最大值，用于调试和后续重采样判断。"))
	FVector SourceBoundsMax = FVector::ZeroVector;

public:
	// ===== 构造与生命周期 =====

	AEHB_DoorWindow();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished) override;
	virtual void OnElementActorDeleted_Implementation() override;
	virtual bool ResolveCutVolumesForTarget(
		const AEHBElementActorBase* TargetElement,
		TArray<FEHBResolvedCutVolume>& OutVolumes) const override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ===== 配置与重建 =====

	/** 使用静态网格体配置门窗，并立即刷新洞口尺寸和可视组件。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|配置", meta = (DisplayName = "从静态网格体配置", ToolTip = "把指定静态网格体写入门窗，按网格体包围盒推导洞口宽度、高度、厚度，并根据门窗类型设置离地高度。"))
	void ConfigureFromStaticMesh(UStaticMesh* InStaticMesh, EEHBDoorWindowElementKind InKind, float InSillHeight);

	/** 根据当前源静态网格体重新推导洞口尺寸。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|采样", meta = (DisplayName = "刷新洞口尺寸", ToolTip = "读取当前源静态网格体的包围盒：Z 轴作为洞口高度，XY 较长方向作为宽度，XY 较短方向作为厚度。"))
	void RefreshOpeningDimensionsFromSourceMesh();

	/** 刷新静态网格组件的网格体、朝向和相对位置。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|生成", meta = (DisplayName = "重建门窗显示", ToolTip = "把源静态网格体应用到显示组件，并将网格体底部移动到 Actor 原点，宽度统一对齐到本地 X 方向。"))
	void RebuildDoorWindow();

	UFUNCTION(BlueprintCallable, Category = "DoorWindow|Opening", meta = (DisplayName = "Set Rectangular Opening Dimensions", ToolTip = "Set the rectangular opening width, height, bottom height and thickness in centimeters."))
	void SetRectangularOpeningDimensions(float InWidth, float InHeight, float InBottomHeight, float InThickness);

	UFUNCTION(BlueprintCallable, Category = "DoorWindow|Opening", meta = (DisplayName = "Set Window Sill Height"))
	bool SetWindowSillHeight(float InSillHeight);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DoorWindow|Opening", meta = (DisplayName = "Rebuild Opening Outline From Source Mesh"))
	void RebuildOpeningSplineFromSourceMeshBounds();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DoorWindow|Opening", meta = (DisplayName = "Initialize Default Window Opening"))
	void InitializeDefaultWindowOpening();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DoorWindow|Opening", meta = (DisplayName = "Initialize Default Door Opening"))
	void InitializeDefaultDoorOpening();

	UFUNCTION(BlueprintPure, Category = "DoorWindow|Opening", meta = (DisplayName = "Get Local Opening Outline Points"))
	TArray<FVector> GetOpeningOutlineLocalPoints() const;

	UFUNCTION(BlueprintPure, Category = "DoorWindow|Opening", meta = (DisplayName = "Get World Opening Outline Points"))
	TArray<FVector> GetOpeningOutlineWorldPoints() const;

	void HandleOpeningSplineEdited();

	/** 返回洞口底边离地高度。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|洞口", meta = (DisplayName = "获取洞口底边离地高度", ToolTip = "返回墙体挖洞时使用的洞口底边高度。门默认通常为 0；窗通常为窗台高度。"))
	float GetOpeningBottomHeight() const;

	/** 返回墙体布尔挖洞时可直接读取的结构化洞口数据。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|洞口", meta = (DisplayName = "获取墙体挖洞数据", ToolTip = "把当前门窗的宽度、高度、厚度、底边离地高度、所属墙体标识和世界变换整理成一份结构化数据，供墙体重建和布尔挖洞使用。"))
	FEHBDoorWindowOpeningData MakeOpeningData() const;

	/** 把当前门窗绑定到指定墙体，并记录沿墙距离。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|连接", meta = (DisplayName = "绑定到墙体", ToolTip = "将当前门窗绑定到指定墙体，并记录门窗中心距离墙体起点的长度。墙体也会保存对应连接记录，便于双方互相找回。"))
	void BindToWall(AEHB_Wall* Wall, float InDistanceFromWallStart);
	void ApplyWallDrivenTransform(AEHB_Wall* Wall, const FTransform& WorldTransform, float InDistanceFromWallStart);

	/** Replace an existing wall-hosted door/window with ReplacementDoorWindow, preserving the replacement actor's current wall position. */
	UFUNCTION(BlueprintCallable, Category = "DoorWindow|Connection", meta = (DisplayName = "Replace Door Window"))
	static bool ReplaceDoorWindow(AEHB_DoorWindow* ReplacementDoorWindow, AEHB_DoorWindow* ExistingDoorWindow);

	/** 清除当前门窗和墙体之间的连接记录。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|连接", meta = (DisplayName = "清除墙体绑定", ToolTip = "解除当前门窗与所属墙体之间的连接关系。会同时从墙体的连接门窗列表中移除当前门窗记录。"))
	void ClearWallBinding();

	/** 根据当前变换刷新墙体绑定状态。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|连接", meta = (DisplayName = "刷新墙体绑定状态", ToolTip = "当门窗移动后调用。若门窗仍在所属墙体中心轴附近，则更新距起点距离；如果已经离开墙体范围，则解除门窗和墙体的相互记录。"))
	void RefreshWallBindingAfterTransformChanged(bool bFinished);

	/** 使用当前样条轮廓请求墙体显示或刷新实时预览洞口。 */
	UFUNCTION(BlueprintCallable, Category = "门窗|预览", meta = (DisplayName = "请求墙体预览洞口", ToolTip = "拖拽门窗贴近墙体时，使用当前样条轮廓在墙面上实时预览洞口。预览数据不会保存到关卡。"))
	void RequestPreviewWallOpening(AEHB_Wall* Wall);

private:
	// ===== 内部连接工具 =====

	/** 从所属建筑对象的子 Actor 中查找当前绑定的墙体。 */
	AEHB_Wall* FindBoundWall() const;

	/** 判断当前门窗位置是否仍然贴附在指定墙体的可用范围内。 */
	bool IsStillAttachedToWall(const AEHB_Wall* Wall, float& OutDistanceFromStart) const;

	bool CanPreviewAgainstWall(const AEHB_Wall* Wall, float& OutDistanceFromStart) const;
	bool DoesOpeningFitWithinWallLength(const AEHB_Wall* Wall, float DistanceFromStart, float Tolerance) const;
	void UpdateOpeningDimensionsFromSpline();
	void UpdateSelectionProxy();

	UPROPERTY()
	bool bOpeningSplineInitializedFromSourceMesh = false;
	bool bApplyingWallDrivenTransform = false;

#if WITH_EDITOR
	AEHB_Wall* FindEditorMoveSnapWall() const;
	FTransform MakeEditorMoveWorldTransformFromElementLocal(const FTransform& LocalTransform) const;
	void SetInvalidPlacementIndicatorVisible(bool bVisible);
	void RestoreLastValidEditorMovePlacement();

	TWeakObjectPtr<AEHB_Wall> EditorMoveSnapWall;
	TWeakObjectPtr<AEHB_Wall> EditorMoveLastValidWall;
	bool bEvaluatingEditorMoveSnap = false;
	bool bTrackingEditorMoveDrag = false;
	bool bEditorMovePlacementInvalid = false;
	bool bHasEditorMoveLastValidPlacement = false;
	FTransform EditorMoveUnsnappedWorldTransform = FTransform::Identity;
	FTransform EditorMoveLastAppliedWorldTransform = FTransform::Identity;
	FTransform EditorMoveLastValidWorldTransform = FTransform::Identity;
	float EditorMoveLastValidDistanceFromStart = 0.0f;
#endif
};
