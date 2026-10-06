// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Core/EHBStraightWallOpeningMesh.h"

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Cutting/EHBCutTypes.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBWallMeshData.h"
#include "EHB_Wall.generated.h"

struct FEHBWallJunctionMesh;
struct FEHBPreparedWallOpening;
struct FEHBPreparedWallNodeDefinitions;
struct FEHBWallNodeModel;
struct FEHBNodeConnectedWallDefinition;
struct FEHBWallJunctionWallSides;
class AEHB_Pillar;
class AEHBBuildingActorBase;
class AEHB_DoorWindow;
class UMaterialInterface;
class UPrimitiveComponent;
class UEHBGeneratedMeshComponent;


/** 墙面单侧可使用的数据来源。 */
UENUM(BlueprintType, meta = (DisplayName = "墙面数据来源"))
enum class EEHBWallSurfaceSourceType : uint8
{
	Simple UMETA(DisplayName = "简单墙面", ToolTip = "使用程序自动生成的平面墙面，不读取墙面采样数据。"),
	SampledMesh UMETA(DisplayName = "采样网格体", ToolTip = "从墙面采样数据表中读取静态网格体采样结果，用作这一侧墙面。")
};

/**
 * 墙体单侧表面的生成配置。
 * 左墙和右墙各自持有一份配置，后续可以分别切换为简单墙面或采样墙面。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "墙面表面样式", ToolTip = "控制墙体某一侧表面的生成方式，包括数据来源、采样行、采样面和材质覆盖。"))
struct EASYHOUSEBUILDER_API FEHBWallSurfaceStyle
{
	GENERATED_BODY()

	/** 这一侧墙面使用简单程序墙面，还是读取墙面采样数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "数据来源", ToolTip = "指定这一侧墙面使用当前的简单程序墙面，还是读取墙面采样数据表中的采样结果。"))
	EEHBWallSurfaceSourceType SourceType = EEHBWallSurfaceSourceType::Simple;

	/** 数据来源为采样网格体时读取的数据表行。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "采样墙面行", ToolTip = "墙面采样数据表中的一行。一行内部同时保存正面和反面采样数据，生成时会根据“采样面”字段选择使用哪一侧。"))
	FDataTableRowHandle SampledWallRow;

	/** 在采样墙面行中读取正面还是反面数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "采样面", ToolTip = "当数据来源为采样网格体时，指定使用采样行中的正面数据还是反面数据。"))
	EEHBWallMeshSampleSide SampleSide = EEHBWallMeshSampleSide::Front;

	/** Whether this side used the flipped front/back mapping when the sample was applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "翻转采样正反面", ToolTip = "记录这一个墙侧面应用采样时是否翻转了采样正反面。左右墙会分别保存该状态。"))
	bool bFlipSampleSide = false;

	/** 采样墙面的归一化起始偏移，0 表示原始采样起点，1 表示偏移一个采样单元宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "采样偏移", ToolTip = "采样墙面沿墙体起点方向的归一化偏移。0 表示从采样单元起点开始，1 表示偏移一个采样单元宽度。", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SampleStartOffset = 0.0f;

	/** 可选材质覆盖。为空时保持组件或采样数据自身的材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面样式", meta = (DisplayName = "覆盖材质", ToolTip = "这一侧墙面生成后使用的材质覆盖。为空时使用默认材质，或后续采样数据中记录的源材质。"))
	TSoftObjectPtr<UMaterialInterface> OverrideMaterial;
};

/**
 * EHB_Wall：墙体元素的独立 Actor。
 * 墙体保存自己的路径、尺寸、起止柱子引用和三段程序网格，便于编辑器与运行时创建、修改、保存和重建。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_Wall", ToolTip = "墙体、隔墙或围护墙的独立 Actor。它保存墙体起点、终点、高度、厚度、左右墙面配置以及封边网格，可被单独选中、移动、保存和重建。"))
class EASYHOUSEBUILDER_API AEHB_Wall : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	// ===== 组件 =====

	/** 从起点看向终点时位于左侧的墙面网格组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙体|组件", meta = (DisplayName = "左墙网格组件", ToolTip = "显示墙体左侧表面的程序化网格组件。左侧的定义是从墙体起点看向终点时的左边，法线会朝墙体外侧。"))
	TObjectPtr<UEHBGeneratedMeshComponent> LeftWallMeshComponent;

	/** 从起点看向终点时位于右侧的墙面网格组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙体|组件", meta = (DisplayName = "右墙网格组件", ToolTip = "显示墙体右侧表面的程序化网格组件。右侧的定义是从墙体起点看向终点时的右边，法线会朝墙体外侧。"))
	TObjectPtr<UEHBGeneratedMeshComponent> RightWallMeshComponent;

	/** 顶面、端面以及后续开洞切口封闭面使用的网格组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙体|组件", meta = (DisplayName = "封边网格组件", ToolTip = "显示墙体顶面、起止端面以及后续门窗洞口切口封闭面的程序化网格组件。当前会生成顶面和两端侧面，不生成底面。"))
	TObjectPtr<UEHBGeneratedMeshComponent> CapMeshComponent;

	// ===== 路径与连接 =====

	/** 墙体中心线起点，位于所属建筑对象的本地空间中。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|路径", meta = (DisplayName = "本地起点", ToolTip = "墙体中心线的起点，使用所属建筑对象的本地坐标。墙体 Actor 的位置会放在起点与终点的中点。"))
	FVector LocalStart = FVector::ZeroVector;

	/** 墙体中心线终点，位于所属建筑对象的本地空间中。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|路径", meta = (DisplayName = "本地终点", ToolTip = "墙体中心线的终点，使用所属建筑对象的本地坐标。起点到终点方向会作为墙体 Actor 的本地 X 方向。"))
	FVector LocalEnd = FVector(400.0f, 0.0f, 0.0f);

	/** 起点连接的柱子元素唯一标识。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|连接", meta = (DisplayName = "起点柱子标识", ToolTip = "墙体起点连接的柱子 Actor 唯一标识。保存后可用它重新找回端点柱子，便于同步移动和拓扑关系。"))
	FGuid StartPillarGuid;

	/** 终点连接的柱子元素唯一标识。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|连接", meta = (DisplayName = "终点柱子标识", ToolTip = "墙体终点连接的柱子 Actor 唯一标识。保存后可用它重新找回端点柱子，便于同步移动和拓扑关系。"))
	FGuid EndPillarGuid;

	/** 当前墙体上已经放置并连接的门窗记录。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "墙体|门窗", meta = (DisplayName = "连接门窗列表", ToolTip = "当前墙体上已经放置的门窗连接记录。每条记录保存门窗标识、洞口尺寸、底边高度、距墙体起点距离和门窗相对墙体变换，后续墙体挖洞会读取这里。"))
	TArray<FEHBWallDoorWindowConnection> DoorWindowConnections;

	// ===== 尺寸 =====

	/** 墙体从底面向上的高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|尺寸", meta = (DisplayName = "墙高", ToolTip = "墙体从放置平面向上生成的高度，单位为厘米。修改后会自动重建左墙、右墙和封边网格。", ClampMin = "1.0", Units = "cm"))
	float Height = 300.0f;

	/** 墙体横向厚度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|尺寸", meta = (DisplayName = "墙厚", ToolTip = "墙体左右两侧之间的总厚度，单位为厘米。左墙位于正半厚度方向，右墙位于负半厚度方向。", ClampMin = "1.0", Units = "cm"))
	float Thickness = 20.0f;

	/** Lateral offset of the future curved-wall control point from the wall center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Curve", meta = (DisplayName = "Curve Control Offset", ToolTip = "Offset along the wall local Y axis for the curved-wall controller. The mesh is not deformed until curved-wall generation is implemented.", Units = "cm"))
	float CurveControlOffset = 0.0f;

	/** Maximum straight-space X span for triangles before circular-arc deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Curve", meta = (DisplayName = "Curve Segment Length", ToolTip = "Maximum wall-local X length for generated triangles before the wall is bent. Smaller values make smoother curved walls at higher rebuild cost.", ClampMin = "10.0", Units = "cm"))
	float CurveSegmentLength = 50.0f;

	// ===== 墙面样式 =====

	/** 左墙表面样式，默认使用简单程序墙面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|墙面", meta = (DisplayName = "左墙样式", ToolTip = "从墙体起点看向终点时左侧墙面的生成配置。当前未使用采样数据时会生成简单矩形墙面。"))
	FEHBWallSurfaceStyle LeftSurfaceStyle;

	/** 右墙表面样式，默认使用简单程序墙面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙体|墙面", meta = (DisplayName = "右墙样式", ToolTip = "从墙体起点看向终点时右侧墙面的生成配置。当前未使用采样数据时会生成简单矩形墙面。"))
	FEHBWallSurfaceStyle RightSurfaceStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Surface", meta = (DisplayName = "Cap Material Override", ToolTip = "Optional material override for wall top, end caps and opening reveal faces."))
	TSoftObjectPtr<UMaterialInterface> CapOverrideMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Surface", meta = (DisplayName = "Generate Connected End Caps", ToolTip = "When disabled, wall end caps connected to pillars are omitted."))
	bool bGenerateLinkedPillarEndCaps = true;

public:
	// ===== 构造与编辑器生命周期 =====

	AEHB_Wall();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished) override;
	virtual void OnElementActorDeleted_Implementation() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ===== 配置入口 =====

	/** 将该 Actor 配置为简单墙体，并记录所属建筑对象、起止柱子、路径和尺寸。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|配置", meta = (DisplayName = "配置为简单墙体", ToolTip = "写入墙体的所属建筑对象、起止柱子、中心线路径、高度和厚度，并立即重建左墙、右墙和封边网格。"))
	void ConfigureAsSimpleWall(AEHBBuildingActorBase* InBuilding, AEHB_Pillar* InStartPillar, AEHB_Pillar* InEndPillar, const FVector& InLocalStart, const FVector& InLocalEnd, float InHeight, float InThickness, bool bFinished = true);

	/** 根据连接柱子的当前位置、旋转和尺寸刷新墙体参考线，并重新贴合柱子边界。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|连接", meta = (DisplayName = "从连接柱子刷新墙体", ToolTip = "查找起点柱子和终点柱子，使用它们的当前位置更新墙体中心参考线，然后根据柱子边界重新裁切左墙、右墙和顶面，保证墙体尽量贴住柱子边缘。"))
	void RefreshFromConnectedPillars(bool bFinished = true);

	/** Native generation adapter. Caller owns transaction/source authority. Does not change topology or saved definitions.
	 * Rejects unsupported wall styles and mismatching wall dimensions before any mutation. */
	bool RefreshFromNodeModel(const FEHBWallNodeModel& Definitions, bool bFinished = true);
	bool RefreshFromNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Definitions, bool bFinished = true);
	bool PrepareNodeSurfaceOpening(const FEHBWallJunctionWallSides& Geometry,FEHBPreparedWallOpening& Out,FName& Status) const;
#if WITH_DEV_AUTOMATION_TESTS
	uint64 GetNodeDefinitionRefreshSerial() const { return NodeDefinitionRefreshSerial; }
#endif
	void RebuildConnectedPillarMeshes() const;
	void SetPillarConnectionFacePoints(const AEHB_Pillar* InPillar, const FVector& LeftLocalPoint, const FVector& RightLocalPoint);
	void ClearPillarConnectionFacePoints(const AEHB_Pillar* InPillar);
	bool ApplyMaterialToHitSurface(const UPrimitiveComponent* HitComponent, UMaterialInterface* Material, bool bApplyAllWallSurfaces);
	bool ApplyMaterialToSide(bool bLeftSide, UMaterialInterface* Material);

	/** 添加或刷新一条门窗连接记录。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|门窗", meta = (DisplayName = "添加或更新门窗连接", ToolTip = "把指定门窗写入当前墙体的连接列表。如果该门窗已经存在，则刷新距离、洞口尺寸和相对墙体变换；否则添加一条新记录。"))
	void AddOrUpdateDoorWindowConnection(AEHB_DoorWindow* DoorWindow, float DistanceFromStart);

	/** Set a transient opening used while a door/window is being dragged over this wall. */
	void SetPreviewDoorWindowOpening(AEHB_DoorWindow* DoorWindow, float DistanceFromStart);

	/** Remove the transient drag-preview opening without touching saved door/window connections. */
	void ClearPreviewDoorWindowOpening(bool bRebuildWall = true);

	/** Find an existing door/window whose saved opening touches or overlaps the supplied door/window at this wall distance. */
	AEHB_DoorWindow* FindOverlappingDoorWindow(AEHB_DoorWindow* DoorWindow, float DistanceFromStart, float Tolerance = 1.0f) const;

	/** 按门窗唯一标识移除连接记录。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|门窗", meta = (DisplayName = "移除门窗连接", ToolTip = "从当前墙体的连接列表中移除指定门窗标识。门窗被删除、转移到其他墙体或取消放置时应调用。"))
	void RemoveDoorWindowConnectionByGuid(const FGuid& DoorWindowGuid);

	/** 计算世界位置投影到墙体中心线后距离起点的长度。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|门窗", meta = (DisplayName = "计算距起点距离", ToolTip = "把一个世界空间位置转换到墙体所属建筑对象的本地空间，并投影到墙体起点到终点方向上，返回距离墙体起点的长度。用于门窗放置时记录沿墙位置。", Units = "cm"))
	float CalculateDistanceFromStartForWorldLocation(const FVector& WorldLocation) const;

	/** Shared building-local axis point for previews and physical placement. */
	FVector GetBuildingLocalLocationOnCenterAxisAtDistance(float DistanceFromStart, float BottomHeight) const;

	/** 根据距起点距离取得墙体中心轴线上的世界位置。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|门窗", meta = (DisplayName = "获取中心轴世界位置", ToolTip = "根据门窗距离墙体起点的长度，返回墙体中心轴线上的世界空间位置。BottomHeight 会叠加到墙体底部高度上，用于让门窗吸附到墙体中心而不是鼠标命中的墙面表面。", Units = "cm"))
	FVector GetWorldLocationOnCenterAxisAtDistance(float DistanceFromStart, float BottomHeight) const;

	/** 将任意世界位置投影到墙体中心轴线上。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|门窗", meta = (DisplayName = "投影到中心轴", ToolTip = "把一个世界空间位置投影到墙体中心轴线上，并按指定底边离地高度修正 Z 值。拖拽门窗时用于让门窗始终贴在墙体中心轴，而不是贴到被鼠标命中的左墙或右墙表面。"))
	FVector ProjectWorldLocationToCenterAxis(const FVector& WorldLocation, float BottomHeight) const;

	/** Return the current curved-wall controller location in world space. */
	UFUNCTION(BlueprintCallable, Category = "Wall|Curve", meta = (DisplayName = "Get Curve Control World Location"))
	FVector GetCurveControlWorldLocation() const;

	/** Move the curved-wall controller by projecting the supplied world location onto the wall local Y axis. */
	UFUNCTION(BlueprintCallable, Category = "Wall|Curve", meta = (DisplayName = "Set Curve Control World Location"))
	void SetCurveControlWorldLocation(const FVector& WorldLocation);

	/** Apply curved-wall settings and refresh the wall plus its connected pillars as one operation. */
	UFUNCTION(BlueprintCallable, Category = "Wall|Curve", meta = (DisplayName = "Apply Curve Settings", Units = "cm"))
	void ApplyCurveSettings(float ControlOffset, float SegmentLength, bool bFinished = true);

	/** Map a point from generated straight wall local space onto the circular curved-wall space. */
	FVector TransformStraightWallLocalPointToCurve(const FVector& WallLocalPoint) const;

	/** Map a vector from generated straight wall local space onto the circular curved-wall frame at a point. */
	FVector TransformStraightWallLocalVectorToCurve(const FVector& WallLocalPoint, const FVector& WallLocalVector) const;
	bool BuildSideTopPolylineInBuildingSpace(bool bLeftSide, TArray<FVector>& OutBuildingLocalPoints, float MaxSegmentLength = 50.0f) const;
	bool GetSurfaceSamplePhaseAtEndpoint(bool bLeftSide, bool bConnectedAtStart, float& OutPhase) const;

	// ===== 网格生成 =====

	/** 重建整个墙体网格，是外部调用的统一入口。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|生成", meta = (DisplayName = "重建墙体网格", ToolTip = "根据当前路径、尺寸和左右墙面样式重新生成左墙、右墙和封边网格。后续门窗洞口、采样墙面等逻辑也应从这个入口进入。"))
	void RebuildWallMesh();

	/** 重建左墙网格。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|生成", meta = (DisplayName = "重建左墙", ToolTip = "重建从起点看向终点时左侧的墙面网格。若左墙未使用采样网格体，则生成简单矩形墙面。"))
	void RebuildLeftWallMesh();

	/** 重建右墙网格。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|生成", meta = (DisplayName = "重建右墙", ToolTip = "重建从起点看向终点时右侧的墙面网格。若右墙未使用采样网格体，则生成简单矩形墙面。"))
	void RebuildRightWallMesh();

	/** 重建墙体封边网格。 */
	UFUNCTION(BlueprintCallable, Category = "墙体|生成", meta = (DisplayName = "重建封边", ToolTip = "重建墙体顶面、起止端面和后续洞口切口面使用的封边网格。当前只生成顶面与两端侧面。"))
	void RebuildWallCapMesh();

	/** Current undecorated cap/reveal geometry without reading or changing components. */
	bool BuildStructuralContactMesh(FEHBWallJunctionMesh& Out) const;
	/** Read-only validation of opt-in logical-surface bindings. Legacy operations
	 * retain their previous resolver. This does not publish an edit transaction. */
	bool PrepareSurfaceOpening(const TArray<FEHBCutOperation>& Candidate, FEHBPreparedWallOpening& Out, FName& Status) const;
	bool ValidateSurfaceOpeningBindings(const TArray<FEHBCutOperation>& Operations,FName& Status) const;

private:
	friend struct FEHBWallOpeningCommand;
	bool ApplyPreparedSurfaceOpening(const FEHBPreparedWallOpening& Prepared,FName& Status);

	friend class AEHBBuildingActorBase;
	bool CanApplyNodeDefinition(const FEHBNodeConnectedWallDefinition& Definition,bool bValidatedSurfaceOpenings=false) const;
	void StageResolvedNodeGeometry(const FEHBWallJunctionWallSides& Geometry,bool bFinished);
	void ApplyResolvedNodeGeometry(const FEHBWallJunctionWallSides& Geometry,bool bFinished);

	// ===== 内部生成工具 =====

	/** 计算墙体长度，长度不足时返回 0。 */
	float GetWallLength() const;

	/** 使用当前路径计算墙体 Actor 相对建筑对象的本地变换。 */
	FTransform MakeWallLocalTransform() const;

	/** 查找指定唯一标识对应的柱子 Actor。 */
	AEHB_Pillar* FindPillarByGuid(const FGuid& PillarGuid) const;

	/** 从所属建筑对象的子 Actor 中查找指定唯一标识对应的门窗 Actor。 */
	AEHB_DoorWindow* FindDoorWindowByGuid(const FGuid& DoorWindowGuid) const;
	void RefreshConnectedDoorWindowTransforms();
	bool MakeDoorWindowWorldTransform(float DistanceFromStart, float BottomHeight, const FVector& WorldScale, FTransform& OutWorldTransform) const;

	/** 删除当前墙体记录的所有门窗，并清理双方连接关系。 */
	void DeleteConnectedDoorWindows();

	/** Build wall-side connection data from the current door/window state. */
	FEHBWallDoorWindowConnection MakeDoorWindowConnection(
		AEHB_DoorWindow* DoorWindow,
		float DistanceFromStart) const;
	bool BuildConnectionOpeningPolygon(
		const FEHBWallResolvedGeometry& Geometry,
		const FEHBWallDoorWindowConnection& Connection,
		TArray<FVector2d>& OutOpeningPolygon) const;
	bool BuildCutOperationOpeningPolygon(
		const FEHBCutOperation& Operation,
		TArray<FVector2d>& OutOpeningPolygon) const;
	bool ResolveSurfaceBoundOpening(const FEHBCutOperation& Operation,TArray<FVector2d>& Polygon,FName& Status) const;
	void SyncDoorWindowCutOperation(const FEHBWallDoorWindowConnection& Connection, AEHB_DoorWindow* DoorWindow);
	void RemoveDoorWindowCutOperationByGuid(const FGuid& DoorWindowGuid);
	void RemoveAllDoorWindowCutOperations();

	/** 根据连接柱子的矩形边界计算左墙、右墙和封边实际使用的起终点。 */
	bool BuildResolvedWallGeometry(FEHBWallResolvedGeometry& OutGeometry) const;
	bool ApplyCachedPillarConnectionFaceToGeometry(bool bStartPillar, FEHBWallResolvedGeometry& OutGeometry) const;
	bool ProjectCachedPillarFacePointToCurveSide(const FVector& WallLocalPoint, float WallY, bool bStartPillar, float& OutX, float& OutDistanceSquared) const;

	/** 把柱子的矩形横截面转换到墙体 Actor 的本地空间。 */
	bool GetPillarCornersInWallLocal(const AEHB_Pillar* Pillar, TArray<FVector2D>& OutCorners) const;

	/** 计算墙体某一条侧边线与柱子横截面边界的交点 X 坐标。 */
	bool FindPillarIntersectionXAtWallY(const TArray<FVector2D>& PillarCorners, float WallY, bool bUseMaxX, float& OutX) const;

	/**
	 * 根据当前尺寸生成某一侧的简单墙面网格。
	 * 函数会把 DoorWindowConnections 中保存的样条轮廓投影到墙体局部 XZ 平面，
	 * 通过二维约束三角化从墙面中减去封闭窗洞或与底边相交的凹形门洞。
	 */
	void BuildSimpleWallSurfaceMesh(bool bLeftSide, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const TArray<TArray<FVector2d>>* PreparedOpenings=nullptr, bool* Succeeded=nullptr) const;

	/** Build the selected sampled wall surface, clipped by current door/window openings. */
	bool BuildSampledWallSurfaceMesh(
		bool bLeftSide,
		const FEHBWallSurfaceStyle& SurfaceStyle,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<int32>& OutTriangleMaterialIndices,
		TArray<TSoftObjectPtr<UMaterialInterface>>& OutMaterials) const;

	float CalculateSurfaceSamplePhaseAtStart(bool bLeftSide, const FEHBWallSurfaceStyle& SurfaceStyle) const;
	float CalculateSurfaceSamplePhaseAtStartInternal(bool bLeftSide, const FEHBWallSurfaceStyle& SurfaceStyle, TSet<FString>& VisitedSurfaces) const;
	bool GetResolvedSurfaceLength(bool bLeftSide, float& OutLength) const;

	/** 根据当前尺寸生成顶面和两端侧面的封边网格。 */
	void BuildSimpleWallCapMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const TArray<TArray<FVector2d>>* PreparedOpenings=nullptr, bool* Succeeded=nullptr) const;

	bool IsCurveDeformationEnabled() const;
	void ApplyCurveDeformationToMesh(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs, TArray<int32>* TriangleMaterialIndices = nullptr) const;

	/** Append reveal caps by bridging matching cut edges from the left and right wall meshes. */
	void BuildOpeningRevealCapMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, const FEHBWallJunctionMesh* Left=nullptr, const FEHBWallJunctionMesh* Right=nullptr, const TArray<TArray<FVector2d>>* PreparedOpenings=nullptr) const;

	/** Convert saved and preview door/window openings to wall-local XZ polygons. */
	void BuildDoorWindowOpeningPolygons(const FEHBWallResolvedGeometry& Geometry, TArray<TArray<FVector2d>>& OutOpeningPolygons) const;

	/** 将生成出的网格数据写入指定程序化网格组件。 */
	void ApplyMeshToComponent(UEHBGeneratedMeshComponent* TargetComponent, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, const TSoftObjectPtr<UMaterialInterface>& MaterialOverride) const;

	/** Write mesh data into one or more material sections. */
	void ApplyMeshToComponent(
		UEHBGeneratedMeshComponent* TargetComponent,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs,
		const TSoftObjectPtr<UMaterialInterface>& MaterialOverride,
		const TArray<int32>& TriangleMaterialIndices,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& SourceMaterials) const;

	/** 墙体正在根据柱子刷新时为 true，用于避免移动回调反向改写参考线。 */
#if WITH_DEV_AUTOMATION_TESTS
	uint64 NodeDefinitionRefreshSerial = 0;
#endif
	bool bRefreshingFromConnectedPillars = false;
	bool bRebuildingWallMesh = false;
	bool bApplyingResolvedNodeGeometry = false;

	UPROPERTY(Transient)
	bool bHasStartPillarConnectionFace = false;

	UPROPERTY(Transient)
	FVector StartPillarConnectionLeftLocal = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector StartPillarConnectionRightLocal = FVector::ZeroVector;

	UPROPERTY(Transient)
	bool bHasEndPillarConnectionFace = false;

	UPROPERTY(Transient)
	FVector EndPillarConnectionLeftLocal = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector EndPillarConnectionRightLocal = FVector::ZeroVector;

	/**
	 * Drag placement must preview a real cut without adding a saved connection.
	 * These fields intentionally are not UPROPERTY values, so they never serialize.
	 */
	FEHBWallDoorWindowConnection PreviewDoorWindowConnection;
	bool bHasPreviewDoorWindowConnection = false;

	UPROPERTY(Transient)
	mutable TArray<FVector> CachedLeftWallVertices;
	UPROPERTY(Transient)
	mutable TArray<int32> CachedLeftWallTriangles;
	UPROPERTY(Transient)
	mutable TArray<FVector> CachedRightWallVertices;
	UPROPERTY(Transient)
	mutable TArray<int32> CachedRightWallTriangles;
};
