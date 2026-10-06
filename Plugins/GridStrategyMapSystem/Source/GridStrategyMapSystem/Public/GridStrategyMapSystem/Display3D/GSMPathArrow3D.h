#pragma once

#include "Components/SplineMeshComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMPathArrow3D.generated.h"

class AGSMTile3D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class USplineComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 网格策略地图路径箭头。
 *
 * 该 Actor 用一条样条线驱动实体箭头路径：起点为半圆头，中段为可拉伸样条网格，终点为箭头头部。
 * 设计上允许用户创建蓝图子类，并在蓝图中配置中段网格体、起点网格体、终点网格体和材质。
 *
 * 注意：传入路径点表示“逻辑导航点”，不是网格体端面的实际位置。
 * StartBodyOffsetFromPathStart 和 EndBodyOffsetFromPathEnd 会把实体箭头从逻辑点向内收缩，
 * 用来实现“红点在箭头内部而不是顶头”的视觉效果。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "网格策略地图路径箭头"))
class GRIDSTRATEGYMAPSYSTEM_API AGSMPathArrow3D : public AActor
{
	GENERATED_BODY()

public:
	AGSMPathArrow3D();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** 使用完整寻路结果显示箭头。优先缓存路径瓦片，以便地图缩放或平移后可以刷新到瓦片的新位置。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "按路径结果显示箭头", ToolTip = "根据寻路结果生成或刷新实体箭头路径，并缓存路径瓦片用于后续刷新。"))
	bool ShowPathFromResult(
		UPARAM(DisplayName = "路径结果") const FGSMPathResult& PathResult
	);

	/** 使用世界坐标点显示箭头。适合显示非瓦片驱动的临时路径。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "按世界点显示箭头", ToolTip = "根据一组世界坐标点生成实体箭头路径，不绑定具体瓦片。"))
	bool ShowPathFromWorldLocations(
		UPARAM(DisplayName = "世界点") const TArray<FVector>& WorldLocations
	);

	/** 使用瓦片数组显示箭头。路径会跟随瓦片位置刷新，适合地图缩放和平移场景。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "按瓦片显示箭头", ToolTip = "根据瓦片数组生成实体箭头路径，并缓存瓦片用于后续刷新。"))
	bool ShowPathFromTiles(
		UPARAM(DisplayName = "路径瓦片") const TArray<AGSMTile3D*>& PathTiles
	);

	/** 根据缓存瓦片或缓存世界点重新构建箭头。地图移动、缩放或瓦片位置变化后调用。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "刷新缓存路径", ToolTip = "使用上一次缓存的瓦片或世界点重新计算箭头位置。"))
	bool RefreshPathFromCachedTiles();

	/** 清空当前箭头显示并隐藏所有池化的中段组件。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "清空箭头路径", ToolTip = "隐藏起点、终点和中段箭头组件，并清空缓存路径。"))
	void ClearPath();

	/** 设置整条箭头路径是否可见。不会销毁已缓存的路径和池化组件。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径箭头", meta = (DisplayName = "设置箭头可见", ToolTip = "仅切换箭头显示状态，不清空路径缓存。"))
	void SetPathVisible(
		UPARAM(DisplayName = "可见") bool bNewVisible
	);

	/** 获取内部样条组件，便于蓝图读取路径形状或调试绘制。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径箭头", meta = (DisplayName = "获取路径样条组件", ToolTip = "返回用于驱动箭头中段的样条组件。"))
	void SetMaterialBoundsMask(
		const FVector& WorldCenter,
		const FVector& WorldAxisX,
		const FVector& WorldAxisY,
		const FVector2D& WorldHalfSize,
		float Feather,
		bool bEnabled
	);

	USplineComponent* GetPathSplineComponent() const { return PathSplineComponent; }

protected:
	bool RebuildPathFromWorldLocations(const TArray<FVector>& WorldLocations);
	bool BuildTrimmedLocalPath(
		const TArray<FVector>& LocalPoints,
		TArray<FVector>& OutTrimmedPoints,
		FVector& OutStartDirection,
		FVector& OutEndDirection
	) const;
	void RebuildVisualComponents(
		const TArray<FVector>& TrimmedLocalPoints,
		const FVector& StartDirection,
		const FVector& EndDirection
	);
	void ClearBodySplineMeshes();
	void HideBodySplineMeshes();
	void HideUnusedBodySplineMeshes(int32 FirstUnusedIndex);
	USplineMeshComponent* GetOrCreateBodySplineMeshComponent(int32 SegmentIndex);
	bool BuildPathRibbonMesh(const TArray<FVector>& CenterlinePoints);
	void HidePathRibbonMesh();
	void DisableDecalReceivingOnComponents();
	void ConfigureMeshComponent(UStaticMeshComponent* MeshComponent, UStaticMesh* Mesh);
	void ApplyMaterialOverride(UStaticMeshComponent* MeshComponent);
	UMaterialInterface* ResolveOrCreatePathMaterial(UStaticMeshComponent* MeshComponent);
	UMaterialInterface* ResolveOrCreatePathMaterial(UProceduralMeshComponent* MeshComponent);
	UMaterialInterface* ResolvePathMaterial() const;
	void ApplyMaterialBoundsMaskParameters();
	void ApplyMaterialBoundsMaskToMaterial(UMaterialInstanceDynamic* DynamicMaterial) const;
	FVector GetStartCapJoinLocalOffset() const;
	FVector GetArrowHeadTailLocalOffset() const;
	FVector GetEffectivePathMeshScale() const;
	float GetPathLengthScale() const;
	bool BuildRoundedLocalPath(const TArray<FVector>& InPoints, TArray<FVector>& OutPoints) const;
	static float CalculatePolylineLength(const TArray<FVector>& Points);
	static FVector GetPointAtPolylineDistance(const TArray<FVector>& Points, float Distance);
	static FVector GetDirectionAtPolylineDistance(const TArray<FVector>& Points, float Distance);

protected:
	/** 根组件。所有箭头可视组件都挂接在这里，保持与地图 Actor 一致的本地空间。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "根组件", ToolTip = "路径箭头 Actor 的根组件。"))
	TObjectPtr<USceneComponent> RootSceneComponent;

	/** 路径样条。中段样条网格会沿该组件的各段生成。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "路径样条组件", ToolTip = "用于描述箭头中段走向的样条组件。"))
	TObjectPtr<USplineComponent> PathSplineComponent;

	/** 起点半圆头组件。显示路径起点的圆头端面。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "起点半圆头组件", ToolTip = "路径起点处的半圆头静态网格组件。"))
	TObjectPtr<UStaticMeshComponent> StartCapMeshComponent;

	/** 终点箭头组件。显示路径终点方向。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "终点箭头组件", ToolTip = "路径终点处的箭头静态网格组件。"))
	TObjectPtr<UStaticMeshComponent> EndArrowMeshComponent;

	/** 圆角路径模式下使用的连续中段网格。它避免多个样条网格在拐角处产生断口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "连续路径带状网格组件", ToolTip = "圆角路径模式下使用的单个连续中段网格。"))
	TObjectPtr<UProceduralMeshComponent> PathRibbonMeshComponent;

	/** 中段样条网格池。为了降低鼠标悬浮导航时的卡顿，组件会复用而不是频繁销毁重建。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> BodySplineMeshComponents;

	/** 当前路径实际使用的中段组件数量。池中多余组件会隐藏。 */
	UPROPERTY(Transient)
	int32 ActiveBodySplineMeshCount = 0;

	UPROPERTY(Transient)
	bool bPathRibbonMeshActive = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PathMaterialInstances;

	UPROPERTY(Transient)
	FVector MaterialBoundsMaskCenter = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector MaterialBoundsMaskAxisX = FVector::ForwardVector;

	UPROPERTY(Transient)
	FVector MaterialBoundsMaskAxisY = FVector::RightVector;

	UPROPERTY(Transient)
	FVector2D MaterialBoundsMaskHalfSize = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	float MaterialBoundsMaskFeather = 0.0f;

	UPROPERTY(Transient)
	bool bMaterialBoundsMaskEnabled = false;

	/** 中段网格体。推荐使用 GSM_PathArrow_Body_Rect_Matched_100cm，前向轴为本地 X。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|网格体", meta = (DisplayName = "中段样条网格体", ToolTip = "路径中段使用的样条网格体。固定箭头资源推荐使用 GSM_PathArrow_Body_Rect_Matched_100cm。"))
	TObjectPtr<UStaticMesh> BodyMesh;

	/** 起点网格体。推荐使用与中段宽度匹配的半圆头。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|网格体", meta = (DisplayName = "起点半圆头网格体", ToolTip = "路径起点处使用的半圆头网格体。"))
	TObjectPtr<UStaticMesh> StartCapMesh;

	/** 终点网格体。推荐使用与中段宽度匹配的箭头头部。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|网格体", meta = (DisplayName = "终点箭头网格体", ToolTip = "路径终点处使用的箭头头部网格体。"))
	TObjectPtr<UStaticMesh> ArrowHeadMesh;

	/** 材质覆盖。设置后会覆盖起点、中段和终点的第 0 号材质槽。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|材质", meta = (DisplayName = "材质覆盖", ToolTip = "统一覆盖整条箭头路径使用的材质。为空时可选择复用网格体自身材质。"))
	TObjectPtr<UMaterialInterface> MaterialOverride;

	/** 未设置材质覆盖时，自动从箭头网格体中取第一个可用材质，避免显示默认棋盘格材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|材质", meta = (DisplayName = "无覆盖材质时复用网格体材质", ToolTip = "未设置材质覆盖时，从终点箭头、起点半圆头或中段网格体中取第一个材质作为统一材质。"))
	bool bUseFirstMeshMaterialWhenNoOverride = true;

	/** 路径整体高度偏移。用于让箭头浮在地图表面上方，避免与地形或贴花闪烁。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "路径高度偏移", ToolTip = "在路径点本地 Z 上增加的高度偏移。"))
	float PathHeightOffset = 8.0f;

	/** 从逻辑起点向路径内部缩进的距离。用于让起点红点落在半圆头内部而不是端面上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "起点到中段起点距离", ClampMin = "0.0", ToolTip = "从导航逻辑起点向路径内部移动多远后开始生成中段。"))
	float StartBodyOffsetFromPathStart = 9.0f;

	/** 从逻辑终点向路径内部缩进的距离。用于让终点红点落在箭头头部内部而不是尖端上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "终点到中段终点距离", ClampMin = "0.0", ToolTip = "从导航逻辑终点向路径内部回退多远后结束中段。"))
	float EndBodyOffsetFromPathEnd = 60.0f;

	/** 起点半圆头连接面的本地 X 坐标。关闭自动边界对齐时使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "起点半圆头连接本地横向坐标", ToolTip = "关闭自动边界对齐时，用这个本地 X 坐标表示半圆头与中段连接的端面。"))
	float StartCapJoinLocalX = 0.0f;

	/** 起点半圆头连接面的额外本地偏移。用于修正特殊导入枢轴或美术资源偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "起点半圆头连接本地偏移", ToolTip = "在自动连接点基础上额外添加的本地偏移，用于精细修正枢轴误差。"))
	FVector StartCapJoinLocalOffset = FVector::ZeroVector;

	/** 终点箭头尾部连接面的本地 X 坐标。关闭自动边界对齐时使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "箭头尾部连接本地横向坐标", ToolTip = "关闭自动边界对齐时，用这个本地 X 坐标表示箭头尾部与中段连接的端面。"))
	float ArrowHeadTailLocalX = -60.0f;

	/** 终点箭头尾部连接面的额外本地偏移。用于修正特殊导入枢轴或美术资源偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "箭头尾部连接本地偏移", ToolTip = "在自动连接点基础上额外添加的本地偏移，用于精细修正枢轴误差。"))
	FVector ArrowHeadTailLocalOffset = FVector::ZeroVector;

	/** 最短中段长度。路径过短时会按比例压缩起点和终点缩进距离，避免中段长度为零。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "最小中段长度", ClampMin = "1.0", ToolTip = "保证中段至少保留的长度。"))
	float MinBodyLength = 8.0f;

	/** 是否允许使用曲线样条。使用固定矩形中段资源时，通常应保持强制线性段开启。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "使用平滑样条", ToolTip = "是否把路径点设置为曲线样条。固定矩形中段资源更适合使用线性段。"))
	bool bUseSmoothSpline = true;

	/** 固定使用匹配箭头资源时，强制中段按直线段生成，避免拐角弯曲导致开口。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "匹配网格强制线性段", ToolTip = "开启后即使启用了平滑样条，中段仍按直线段生成，适合当前固定箭头资源。"))
	bool bForceLinearSegmentsForMatchedMeshes = true;

	/** 是否把导航路径的内部拐点切成圆角。保持开启时仍然使用当前中段网格体，只是增加圆弧采样段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "圆角化路径拐点", ToolTip = "开启后会在内部拐点前后裁切一小段，并插入圆弧采样点，让 L 形路径拐角更柔和。", InlineEditConditionToggle))
	bool bRoundPathCorners = true;

	/** 拐角圆角半径。本地空间单位，通常略小于半个瓦片尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "路径拐角半径", ClampMin = "0.0", EditCondition = "bRoundPathCorners", ToolTip = "每个内部拐点两侧被裁切的距离。数值越大，圆角越宽。"))
	float PathCornerRadius = 22.0f;

	/** 每个圆角使用的采样段数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "路径拐角分段", ClampMin = "1", ClampMax = "16", EditCondition = "bRoundPathCorners", ToolTip = "每个圆角使用多少段中段网格来近似。数值越大越圆滑，但组件数量也会增加。"))
	int32 PathCornerSegments = 5;

	/** 连续路径带状网格相对中段网格体高度的额外偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "连续路径高度偏移", EditCondition = "bRoundPathCorners", ToolTip = "圆角路径使用连续带状网格时，在中段网格体包围盒高度基础上额外增加的高度。偏高就降低这个值。"))
	float PathRibbonExtraHeightOffset = 0.0f;

	/** 连续路径中段宽度缩放。只影响圆角连续中段的宽度，不改变路径连接点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "连续路径宽度缩放", ClampMin = "0.1", EditCondition = "bRoundPathCorners", ToolTip = "圆角路径连续中段的额外宽度缩放。中段太粗时降低这个值。"))
	float PathRibbonWidthScale = 1.0f;

	/** 连续路径带状网格厚度。小于等于 0 时会自动使用中段网格体包围盒厚度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "连续路径厚度", ClampMin = "0.0", EditCondition = "bRoundPathCorners", ToolTip = "圆角路径中段的厚度。设置为 0 时从中段网格体包围盒自动推断。"))
	float PathRibbonThickness = 0.0f;

	/** 内部拐点处是否让相邻中段轻微重叠。用于遮住矩形段在转角处天然产生的缝隙。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "拐点处重叠中段", ToolTip = "开启后，相邻中段会在内部拐点处少量重叠，减少 L 形转角缝隙。"))
	bool bOverlapBodySegmentsAtJoints = true;

	/** 中段在内部拐点处的重叠长度。数值越大越不容易露缝，但可能让拐角处更厚。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "中段拐点重叠长度", ClampMin = "0.0", ToolTip = "内部拐点处每段向外延伸的长度，用于遮盖转角缝隙。"))
	float BodySegmentJointOverlap = 12.0f;

	/** 是否根据网格体边界自动推导起点和终点连接面，避免导入枢轴不同导致错位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|位置", meta = (DisplayName = "按网格边界自动对齐端部", ToolTip = "开启后使用起点和终点网格体的包围盒推导连接面位置，优先适配当前固定箭头资源。"))
	bool bAutoAlignEndCapsToMeshBounds = true;

	/** 样条切线倍率。曲线段时影响弯曲程度；强制线性段时通常保持 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "样条切线倍率", ClampMin = "0.0", ToolTip = "控制样条切线长度。固定资源线性段通常保持 1。"))
	float SplineTangentScale = 1.0f;

	/** 中段样条网格体的前向轴。当前固定资源沿本地 X 轴延伸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|样条", meta = (DisplayName = "样条网格前向轴", ToolTip = "中段网格体被样条拉伸时使用的前向轴。当前固定资源使用 X 轴。"))
	TEnumAsByte<ESplineMeshAxis::Type> SplineForwardAxis = ESplineMeshAxis::X;

	/** 是否投射阴影。路径 UI 通常不需要阴影，关闭可减少视觉干扰。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|渲染", meta = (DisplayName = "投射阴影", ToolTip = "控制箭头路径组件是否投射阴影。"))
	bool bCastShadow = false;

	/** 路径网格缩放。中段会只缩放横截面，避免样条端点被组件缩放带偏。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|渲染", meta = (DisplayName = "路径网格缩放", ToolTip = "统一缩放起点、终点和中段横截面。中段不会用组件缩放移动样条点。"))
	FVector PathMeshScale = FVector::OneVector;

	/** 路径整体视觉缩放。会同时缩放中段、起点、终点箭头和首尾连接距离，避免缩小后断开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|路径箭头|渲染", meta = (DisplayName = "路径整体视觉缩放", ClampMin = "0.1", ToolTip = "统一控制整条路径箭头的视觉大小。小于 1 会让中段和箭头整体变小，并同步缩短连接裁切距离。"))
	float PathVisualScale = 0.5f;

	/** 缓存的路径瓦片。仅 C++ 内部使用，不暴露给蓝图，避免弱引用数组的 UHT 限制。 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AGSMTile3D>> CachedPathTiles;

	/** 缓存的世界路径点。没有瓦片缓存时用于重新构建路径。 */
	UPROPERTY(Transient)
	TArray<FVector> CachedWorldLocations;
};
