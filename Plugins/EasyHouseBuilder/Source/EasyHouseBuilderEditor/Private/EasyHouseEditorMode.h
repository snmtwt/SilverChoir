// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "Definitions/EHBBuildingTypes.h"
#include "EdMode.h"
#include "EHBRailingWallDrag.h"
#include "EHBRoomFloorWallDrag.h"
#include "EHBUnboundNodeDrag.h"
#include "EHBFinishRegionDrag.h"
#include "EHBSlabCutterDraft.h"
#include "InputCoreTypes.h"
#include "ScopedTransaction.h"

class AEHBBuildingActorBase;
class AActor;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class AEHB_Floor;
class AEHB_Pillar;
class AEHB_Railing;
class AEHB_Stair;
class AEHB_Wall;
class AEHB_DoorWindow;
class AEHBGableRoof;
class UDataTable;
class UStaticMesh;
class UStaticMeshComponent;
struct FPointerEvent;
struct FHitResult;
struct FEHBDoorWindowMeshData;
struct FEHBWallMeshData;
enum class EEHBRailingSide : uint8;

enum class EEHBFloorSlabEditHandleKind : uint8
{
	None,
	Corner,
	Edge,
	Cutter
};

enum class EEHBRoofEditHandleKind : uint8
{
	None,
	Corner,
	Edge,
	Height
};

enum class EEHBRailingEndpointHandleKind : uint8
{
	None,
	Start,
	End
};

/**
 * 程序化建筑工具的编辑模式。
 * 该模式负责接入 UE 编辑器 Mode 系统，并在进入模式时创建对应的 Toolkit UI。
 */
class FEasyHouseEditorMode : public FEdMode
{
#if WITH_DEV_AUTOMATION_TESTS
	friend class FEHBWallDragInteractionTest;
	friend class FEHBWallPathCommandTest;
	friend class FEHBWallPathPlanCommitTest;
	friend class FEHBWallFromWallCommandTest;
 friend class FEHBRoomBranchDependenciesTest;
 friend class FEHBMultiRoomPathTest;
 friend class FEHBPartitionRoomTest;
 friend class FEHBOrdinaryRoomPathTest;
 friend class FEHBAnchoredWallPathCommandTest;
 friend class FEHBRepeatedWallAnchorsTest;
	friend class FEHBFloorFinishRelationshipTest;
	friend class FEHBCutPersistenceWriteTest;
	friend class FEHBRoomFloorWallDragTest;
	friend class FEHBUnboundNodeEditingTest;
	friend class FEHBMixedNodeEditingTest;
	friend class FEHBOptionalNodeWallSplitTest;
	friend class FEHBOptionalNodeWallPathTest;
	friend class FEHBOptionalNodeAnchoredPathTest;
	friend class FEHBWallColumnCreationPolicyTest;
	friend class FEHBFinishRegionControlsTest;
#endif
public:
 EHBDragAngleSnap::FOptions CreationAssist;
 FVector ResolveCreationRectangleEnd(const FVector& Start,const FVector& End,const FTransform& Frame,FEditorViewportClient* ViewportClient);
 FVector ResolveCreationFreeEnd(const FVector& Start,const FVector& End,FEditorViewportClient* ViewportClient) const;

	/** 编辑模式唯一 ID。注册、激活和 Toolkit 查询都依赖这个 ID。 */
	static const FEditorModeID EM_EasyHouseEditorModeId;

	bool IsRoomFloorWallMoveEnabled() const { return bRoomFloorWallMoveEnabled; }
	void SetRoomFloorWallMoveEnabled(bool bEnabled) { bRoomFloorWallMoveEnabled = bEnabled; if (RoomFloorWallDrag.IsSet()) RoomFloorWallDrag.Cancel(); }

	/** 进入建筑编辑模式时创建 Toolkit。 */
	virtual void Enter() override;

	/** 退出建筑编辑模式时关闭 Toolkit 并释放 UI。 */
	virtual void Exit() override;

	/** 告诉 UE 该编辑模式使用 Toolkit 提供左侧/右侧面板 UI。 */
	virtual bool UsesToolkits() const override { return true; }

	/** 每帧更新墙体创建工具的鼠标落点，并请求视口重绘绿色预览。 */
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

	/** 绘制墙体创建工具的绿色柱体和墙体预览。 */
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	/** 处理墙体创建工具的左键拖拽、右键取消和 Escape 取消。 */
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	/** Consumes native V2 wall deletion, including refusals, so UE cannot fall back to raw Actor deletion. */
	bool RemoveSelectedWalls();

	/** 创建墙面时临时处理右键视角旋转，让它不被 UE 解释成左键+右键平移。 */
	virtual bool InputAxis(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const override;
	virtual bool GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const override;

	/** 同步当前建筑对象；所有新建墙体和柱子都会写入这个建筑对象。 */
	void SetActiveBuilding(AEHBBuildingActorBase* Building);

	/** 返回当前建筑对象，供面板和调试逻辑确认上下文。 */
	AEHBBuildingActorBase* GetActiveBuilding() const;
	/** Finish switching to a newly copied building without carrying an old creation draft. */
	void AdoptCopiedBuilding(AEHBBuildingActorBase* Building);
	bool SelectUnboundWallNode(AEHBBuildingActorBase* Building,FGuid NodeGuid);
	bool HasSelectedUnboundWallNode() const;

	/** 开启墙体拖拽创建工具，并记录本次创建使用的墙高和墙厚度。 */
	void BeginWallCreation(AEHBBuildingActorBase* Building, float InWallHeight, float InWallThickness);
	void SetWallCreationDefaults(float InWallHeight, float InWallThickness);
	void SetWallCreationPhysicalColumns(bool bEnabled);
	bool ShouldCreateWallColumns() const;
	void SetWallsPanelActive(bool bInWallsPanelActive);
	bool CanConnectSelectedPillars() const;
	AEHB_Wall* ConnectSelectedPillars();

	/** 取消墙体创建工具，清理拖拽状态和预览。 */
	void CancelWallCreation();

	void BeginRailingCreation(AEHBBuildingActorBase* Building, float InRailingHeight, float InPostSpacing, float InRailThickness);
	void SetRailingCreationDefaults(float InRailingHeight, float InPostSpacing, float InRailThickness);
	void CancelRailingCreation();

	/** 从门窗面板开始拖拽放置门窗，并创建一个跟随鼠标的预览 Actor。 */
	void BeginDoorWindowPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName);
	void BeginDefaultDoorWindowPlacement(AEHBBuildingActorBase* Building, EEHBDoorWindowElementKind Kind);

	/** 拖拽过程中根据 Slate 鼠标事件刷新门窗预览位置和贴墙状态。 */
	void UpdateDoorWindowPlacementFromPointerEvent(const FPointerEvent& MouseEvent);

	/** 松开拖拽时提交门窗放置；命中墙体则保留并记录关系，未命中墙体则删除预览 Actor。 */
	void FinishDoorWindowPlacementFromPointerEvent(const FPointerEvent& MouseEvent);

	/** 取消门窗拖拽放置并删除当前预览 Actor。 */
	void CancelDoorWindowPlacement();

	/** 从墙体页卡开始拖拽墙面采样项，命中已有墙体时显示绿色预览标识。 */
	void BeginWallSurfacePlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName, bool bInCoverBothSides, bool bInFlipSampleSides);

	/** 拖拽过程中根据 Slate 鼠标事件刷新墙体命中预览。 */
	void UpdateWallSurfacePlacementFromPointerEvent(const FPointerEvent& MouseEvent);

	/** 松开拖拽时提交墙面采样覆盖设置，并清理预览。 */
	void FinishWallSurfacePlacementFromPointerEvent(const FPointerEvent& MouseEvent);

	/** 取消墙面采样覆盖拖拽状态。 */
	void CancelWallSurfacePlacement();

	void BeginPillarMeshPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName);
	void UpdatePillarMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent);
	void FinishPillarMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent);
	void CancelPillarMeshPlacement();
	void BeginRailingMeshPlacement(AEHBBuildingActorBase* Building, UDataTable* SourceTable, FName RowName, bool bApplyToSinglePost = false);
	void UpdateRailingMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent);
	void FinishRailingMeshPlacementFromPointerEvent(const FPointerEvent& MouseEvent);
	void CancelRailingMeshPlacement();
	void BeginFloorSlabPlacement(AEHBBuildingActorBase* Building, bool bIsFoundation, float InSize, float InThickness);
	void CancelFloorSlabPlacement();
	void BeginFloorPlacement(AEHBBuildingActorBase* Building, float InSize);
	void CancelFloorPlacement();
	void BeginRoofPlacement(
		AEHBBuildingActorBase* Building,
		float InLength,
		float InWidth,
		float InPitchDegrees,
		float InThickness,
		float InEaveOffset,
		bool bInGenerateRidge = true,
		bool bInGenerateEaves = true,
		bool bInGenerateGableRakes = true,
		bool bInGenerateGableEndWalls = true,
		float InGableEndWallBoundaryInset = 0.0f,
		bool bInUseHipRoof = false,
		bool bInUseHalfHipRoof = false);
	void CancelRoofPlacement();
	void BeginStairPlacement(
		AEHBBuildingActorBase* Building,
		float InStairHeight,
		float InStairWidth,
		float InTreadDepth,
		bool bInGenerateTreads,
		bool bInFillRisers,
		bool bInFillBottomPart,
		bool bInGenerateSides,
		bool bInGenerateSideGuards,
		bool bInGenerateRailing,
		bool bInGenerateLeftRailing,
		bool bInGenerateRightRailing,
		int32 InRailingStepsPerPost,
		float InRailingEdgeInset,
		float InRailingPostForwardOffset);
	void CancelStairPlacement();
	// Shared by the viewport and MCP; room filling must not require an active mode instance.
	// Read-only room-side outline planning. Position overrides use the candidate straight-wall junction solver.
	// Prepared side arrays are request-local; callers must not retain them across edits.
	static bool BuildCandidateRoomWallSides(AEHBBuildingActorBase* Building,const TMap<FGuid,FVector>& Positions,TArray<FEHBWallJunctionWallSides>& Sides,const TSet<FGuid>* RequestedWallGuids=nullptr);
	static bool BuildRoomSlabOutlineFromWallSides(AEHB_FloorSlab* Slab,const FEHBBuildingClosedLoop& Room,const TArray<FEHBWallJunctionWallSides>& Sides,TArray<FVector>& Polygon);
	static bool BuildRoomSlabOutlineFromDefinition(AEHB_FloorSlab* Slab,const struct FEHBNodeRoomBoundary& Room,const struct FEHBWallNodeModel& Model,const TArray<FEHBWallJunctionWallSides>& Sides,TArray<FVector>& Polygon);
	static bool BuildRoomSlabFollowOutline(AEHB_FloorSlab* Slab, const FEHBBuildingClosedLoop& Room, const TMap<FGuid,FVector>& Positions, TArray<FVector>& Polygon);
	static bool FillFloorSlabRoomForToolset(AEHB_FloorSlab* FloorSlab) { return FillFloorSlabRoom(FloorSlab); }

private:
	void DrawUnboundWallNodeControls(FPrimitiveDrawInterface* PDI) const;
	void DrawNodeEditPreview(FPrimitiveDrawInterface* PDI,AEHBBuildingActorBase* Building,const FEHBWallNodeModelEditDraft& Draft) const;
	/** 根据当前视口鼠标位置计算世界落点；优先射线命中场景，未命中时落到建筑对象高度所在的水平面。 */
	bool GetViewportDropLocation(FEditorViewportClient* ViewportClient, FVector& OutLocation) const;

	/** 根据 Slate 鼠标事件同步视口鼠标坐标，并返回当前视口射线。 */
	bool GetViewportDropRay(FVector& OutRayStart, FVector& OutRayDirection, const FPointerEvent* MouseEvent = nullptr) const;

	/** 根据当前视口射线获取命中结果，可用于检测鼠标是否碰到墙体。 */
	bool GetViewportDropHitResult(FHitResult& OutHitResult, const FPointerEvent* MouseEvent = nullptr) const;

	/** 更新墙体创建工具的当前鼠标落点。 */
	bool UpdateWallCreationMouseLocation(FEditorViewportClient* ViewportClient);

	/** 鼠标释放时提交一次墙体创建操作。 */
	bool FinishWallCreationDrag();
	bool FinishWallCreationSinglePillar();
	bool FinishWallCreationRectangleDrag();
	bool IsWallCreationRectangleModeActive(FViewport* Viewport = nullptr) const;

	struct FWallCreationEndpointSnap
	{
		FVector WorldLocation = FVector::ZeroVector;
		FVector LocalLocation = FVector::ZeroVector;
		AEHB_Pillar* Pillar = nullptr;
		AEHB_Wall* Wall = nullptr;
		float WallDistance = 0.0f;
		int32 FloorIndex = 1;
		FGuid NodeGuid;
		int32 ExpectedNodeRevision = INDEX_NONE;
	};

	bool ResolveWallCreationEndpointSnap(
		const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation,
		const AEHB_Pillar* IgnoredPillar,
		FWallCreationEndpointSnap& OutSnap) const;
	bool ResolveWallCreationLogicalNodeSnap(const AEHBBuildingActorBase* Building,
		const FVector& DesiredWorldLocation, int32 FloorIndex, const FHitResult* Hit,
		FWallCreationEndpointSnap& OutSnap) const;
	void CaptureWallCreationStart(FViewport* Viewport);
	void ResetWallCreationDragState();
	void BuildWallCreationRectangleEndpoints(const AEHBBuildingActorBase* Building,
		const TArray<FVector>& LocalCorners, TArray<FWallCreationEndpointSnap>& OutEndpoints) const;
	bool CommitWallCreationPath(AEHBBuildingActorBase* Building, const TArray<FWallCreationEndpointSnap>& Endpoints, bool bClosed, AEHB_Wall*& OutPrimaryWall) const;


	bool UpdateRailingCreationMouseLocation(FEditorViewportClient* ViewportClient);
	bool FinishRailingCreationDrag();
	bool ResolveRailingCreationSnapFromHit(
		const FHitResult& HitResult,
		AEHBBuildingActorBase* Building,
		FVector& OutWorldLocation,
		AEHB_Pillar*& OutPillar,
		AEHB_Railing*& OutRailing,
		AEHB_Stair*& OutStair,
		EEHBRailingSide& OutStairSide,
		FGuid& OutPostGuid) const;
	AEHB_Railing* ResolveRailingFromHit(const FHitResult& HitResult) const;
	AEHB_Stair* ResolveStairFromHit(const FHitResult& HitResult) const;
	bool ResolveNearestRailingPostSnap(
		AEHBBuildingActorBase* Building,
		const FVector& ReferenceWorldLocation,
		float SnapDistance,
		AEHB_Railing*& OutRailing,
		AEHB_Stair*& OutStair,
		EEHBRailingSide& OutStairSide,
		FGuid& OutPostGuid,
		FVector& OutWorldLocation) const;

	/** 创建简单柱子元素，并写入本地位置、高度和横向尺寸。 */
	AEHB_Pillar* CreateSimplePillar(AEHBBuildingActorBase* Building, const FVector& LocalLocation, const FRotator& LocalRotation, const FString& NamePrefix, int32 FloorIndex = 1) const;

	/** 创建独立墙体 Actor，并让它在内部生成左墙、右墙和封边网格。 */
	AEHB_Wall* CreateSimpleWall(AEHBBuildingActorBase* Building, AEHB_Pillar* StartPillar, AEHB_Pillar* EndPillar, const FVector& LocalStart, const FVector& LocalEnd) const;
	AEHB_Railing* CreateSimpleRailing(AEHBBuildingActorBase* Building, const FVector& WorldStart, const FVector& WorldEnd) const;
	bool GetSelectedPillarPair(AEHB_Pillar*& OutFirstPillar, AEHB_Pillar*& OutSecondPillar) const;
	void DrawPillarConnectionToolbar(FEditorViewportClient* ViewportClient, FViewport* Viewport, FCanvas* Canvas) const;
	void DrawWallMeasurementHud(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) const;
	void DrawWallMeasurementLabel(FViewport* Viewport, const FSceneView* View, FCanvas* Canvas, const FVector& LocalStart, const FVector& LocalEnd, const FVector& WorldStart, const FVector& WorldEnd, bool bDrawAngleAtPillars, int32 StartAngleLabelIndex = 0, int32 EndAngleLabelIndex = 0) const;
	bool HasSelectedWallOrPillarForMeasurement() const;
	bool MoveSelectedWallByDelta(const FVector& WorldDelta, bool bFinished);
	void RefreshSelectedWallMoveNeighborhood(AEHB_Wall* Wall, bool bFinished) const;

	/** 创建门窗预览 Actor，并把数据表行中的网格体、尺寸和类型写入 Actor。 */
	AEHB_DoorWindow* CreateDoorWindowPreviewActor(AEHBBuildingActorBase* Building, const FEHBDoorWindowMeshData& RowData) const;
	AEHB_DoorWindow* CreateDefaultDoorWindowPreviewActor(AEHBBuildingActorBase* Building, EEHBDoorWindowElementKind Kind) const;
	AEHB_DoorWindow* SpawnDoorWindowPreviewActor(AEHBBuildingActorBase* Building, TSubclassOf<AEHB_DoorWindow> DoorWindowClass) const;
	TSubclassOf<AEHB_DoorWindow> ResolveDefaultDoorWindowActorClass(EEHBDoorWindowElementKind Kind) const;

	/** 将门窗采样表中的一行数据应用到指定门窗 Actor。 */
	void ApplyDoorWindowRowToActor(AEHB_DoorWindow* DoorWindow, const FEHBDoorWindowMeshData& RowData) const;
	void ApplyDefaultDoorWindowToActor(AEHB_DoorWindow* DoorWindow, EEHBDoorWindowElementKind Kind, UClass* DoorWindowClass) const;

	/** 从命中结果中解析墙体 Actor。 */
	AEHB_Wall* ResolveWallFromHit(const FHitResult& HitResult) const;
	AEHB_DoorWindow* ResolveDoorWindowFromHit(const FHitResult& HitResult) const;
	AEHB_DoorWindow* FindDoorWindowUnderCursor(const FHitResult& FirstHitResult) const;

	AEHB_Pillar* ResolvePillarFromHit(const FHitResult& HitResult) const;
	AEHB_FloorSlab* ResolveFloorSlabFromHit(const FHitResult& HitResult) const;
	AEHB_Floor* ResolveFloorFromHit(const FHitResult& HitResult) const;
	AEHBGableRoof* ResolveRoofFromHit(const FHitResult& HitResult) const;
	AActor* ResolveSelectedElementForEditor() const;
	void RefreshSelectedElementEditor();
	void SyncWallSelectionFromEditor();
	void SyncRailingSelectionFromEditor();
	void SyncFloorSlabSelectionFromEditor();
	void SyncFloorSelectionFromEditor();
	void SyncRoofSelectionFromEditor();
	void SelectWall(AEHB_Wall* Wall);
	void ClearWallSelection();
	void SelectWallCurveControl(AEHB_Wall* Wall);
	bool IsWallCurveControlSelected() const;
	FVector GetWallCurveControlWorldLocation() const;
	void DrawWallCurveControl(FPrimitiveDrawInterface* PDI) const;
	void SelectRailing(AEHB_Railing* Railing);
	void ClearRailingSelection();
	void SelectRailingEndpointHandle(AEHB_Railing* Railing, EEHBRailingEndpointHandleKind HandleKind);
	bool IsRailingEndpointHandleSelected() const;
	FVector GetRailingEndpointHandleWorldLocation() const;
	bool GetRailingEndpointHandleWorldLocation(const AEHB_Railing* Railing, EEHBRailingEndpointHandleKind HandleKind, FVector& OutWorldLocation) const;
	bool MoveSelectedRailingEndpointHandle(const FVector& WorldDelta);
	void DrawRailingEndpointHandles(FPrimitiveDrawInterface* PDI) const;
	void SyncStairSelectionFromEditor();
	void SelectStair(AEHB_Stair* Stair);
	void ClearStairSelection();
	void SelectStairBottomControl(AEHB_Stair* Stair);
	bool IsStairBottomControlSelected() const;
	FVector GetStairBottomControlWorldLocation() const;
	void SelectStairIntermediateControl(AEHB_Stair* Stair, int32 ControlIndex);
	bool IsStairIntermediateControlSelected() const;
	FVector GetStairIntermediateControlWorldLocation() const;
	void DrawStairBottomControl(FPrimitiveDrawInterface* PDI) const;
	void SelectFloorSlab(AEHB_FloorSlab* FloorSlab);
	void ClearFloorSlabSelection();
	void SelectFloor(AEHB_Floor* Floor);
	void ClearFloorSelection();
	void SelectFloorSlabHandle(AEHB_FloorSlab* FloorSlab, EEHBFloorSlabEditHandleKind HandleKind, int32 FirstIndex, int32 SecondIndex = INDEX_NONE, int32 LoopIndex = INDEX_NONE);
	void SelectRoof(AEHBGableRoof* Roof);
	void ClearRoofSelection();
	void SelectRoofHandle(AEHBGableRoof* Roof, EEHBRoofEditHandleKind HandleKind, int32 FirstIndex, int32 SecondIndex = INDEX_NONE);
	bool CancelSelectedFloorSlabPreviewCutter();
	bool IsFloorSlabOperationActive() const;
	bool ShouldUseBuildingViewportNavigation() const;
	void BeginRightMouseNavigation(FViewport* Viewport);
	bool FinishRightMouseNavigation(FViewport* Viewport);
	void ResetRightMouseNavigation(FViewport* Viewport = nullptr);
	void RestoreViewportMouse(FViewport* Viewport = nullptr) const;
	void RequestViewportMouseRestore(FViewport* Viewport = nullptr);
	bool IsViewportNavigationKey(FKey Key) const;
	bool ExitCurrentBuildingStateByRightClick();
	bool IsFloorSlabHandleSelected() const;
	FVector GetFloorSlabHandleWorldLocation() const;
	bool IsRoofHandleSelected() const;
	FVector GetRoofHandleWorldLocation() const;
	void UpdateHoveredControls(FViewport* Viewport);
	void ClearHoveredControls();
	bool IsHoveredFloorSlabHandle(AEHB_FloorSlab* FloorSlab, EEHBFloorSlabEditHandleKind HandleKind, int32 FirstIndex, int32 SecondIndex = INDEX_NONE, int32 LoopIndex = INDEX_NONE) const;
	bool IsHoveredRoofHandle(AEHBGableRoof* Roof, EEHBRoofEditHandleKind HandleKind, int32 FirstIndex, int32 SecondIndex = INDEX_NONE) const;
	bool UpdateFloorSlabPlacement();
	bool CommitFloorSlabPlacement();
	AEHB_FloorSlab* SpawnFloorSlabActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, bool bIsFoundation, float InSize, float InThickness, bool bPreview);
	bool UpdateFloorPlacement();
	bool CommitFloorPlacement();
	AEHB_Floor* SpawnFloorActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, float InSize, int32 InFloorIndex, bool bPreview);
	bool UpdateRoofPlacement();
	bool CommitRoofPlacement();
	AEHBGableRoof* SpawnRoofActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, bool bPreview);
	void ApplyRoofPlacementSettings(AEHBGableRoof* Roof) const;
	bool UpdateStairPlacement();
	bool CommitStairPlacement();
	AEHB_Stair* SpawnStairActor(AEHBBuildingActorBase* Building, const FVector& WorldLocation, const FRotator& WorldRotation, bool bPreview);
	AEHB_Railing* SpawnStairRailingActor(AEHBBuildingActorBase* Building, AEHB_Stair* Stair, EEHBRailingSide Side, bool bPreview);
	bool ResolveStairSidePlacementFromHit(const FHitResult& HitResult, FVector& OutWorldLocation, FRotator& OutWorldRotation, float& OutResolvedHeight, FVector& OutBottomWorldLocation, bool& bOutUseAutoBottom) const;
	bool FindStairAutoBottomPlacement(const FVector& TopEdgeWorldLocation, const FVector& OutwardDirection, float PlatformSurfaceZ, FVector& OutBottomWorldLocation, float& OutResolvedHeight, float& OutTopStepHeight) const;
	void ApplyCurrentStairPlacementData(AEHB_Stair* Stair) const;
	void ApplyCurrentStairRailingPlacementData(AEHB_Railing* Railing, AEHB_Stair* Stair, EEHBRailingSide Side) const;
	int32 RebuildRailingsHostedByStair(AEHB_Stair* Stair, bool bModifyRailings) const;
	void ClearPreviewStairRailings();
	static bool FillFloorSlabRoom(AEHB_FloorSlab* FloorSlab);
	bool FillFloorRoom(AEHB_Floor* Floor);
	void DrawFloorSlabEditHandles(FPrimitiveDrawInterface* PDI) const;
	void DrawFloorSlabCornerSnapGuides(FPrimitiveDrawInterface* PDI) const;
	void DrawFloorSlabHandleMeasurementHud(FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) const;
	void DrawRoofEditHandles(FPrimitiveDrawInterface* PDI) const;
	void DrawFloorSlabCuttingToolbar(FEditorViewportClient* ViewportClient, FViewport* Viewport, FCanvas* Canvas) const;
	void HandleFloorSlabToolbarCommand(int32 CommandIndex);
	void DrawFloorFillToolbar(FEditorViewportClient* ViewportClient, FViewport* Viewport, FCanvas* Canvas) const;
	void HandleFloorToolbarCommand(int32 CommandIndex);
	/** 刷新门窗拖拽预览位置、旋转和当前命中的墙体。 */
	bool UpdateDoorWindowPlacement(const FPointerEvent* MouseEvent = nullptr);

	/** 把预览门窗提交到当前命中的墙体上，并写入双方连接关系。 */
	bool CommitDoorWindowPlacement();

	/** 刷新墙面采样覆盖的命中墙体。 */
	bool UpdateWallSurfacePlacement(const FPointerEvent* MouseEvent = nullptr);

	/** 将当前墙面采样覆盖配置提交到命中的墙体。 */
	bool CommitWallSurfacePlacement();

	bool UpdatePillarMeshPlacement(const FPointerEvent* MouseEvent = nullptr);
	bool CommitPillarMeshPlacement();
	bool UpdateRailingMeshPlacement(const FPointerEvent* MouseEvent = nullptr);
	bool CommitRailingMeshPlacement();
	/** 绘制单个柱子的绿色预览框。 */
	void DrawPreviewPillar(FPrimitiveDrawInterface* PDI, const FVector& Center, const FVector& Forward, const FVector& Right) const;
	void DrawExistingPillarPreview(FPrimitiveDrawInterface* PDI, const AEHB_Pillar* Pillar, const FLinearColor& Color) const;

	/** 绘制拖拽中的墙体绿色预览框。 */
	bool DrawWallCreationPathPreview(FPrimitiveDrawInterface* PDI, const AEHBBuildingActorBase* Building, const TArray<FWallCreationEndpointSnap>& Endpoints, bool bClosed) const;
	void DrawWallCreationPreview(FPrimitiveDrawInterface* PDI) const;
	void DrawRailingCreationPreview(FPrimitiveDrawInterface* PDI) const;
	void DrawRailingCreationPostPreview(FPrimitiveDrawInterface* PDI, const FVector& Center, const FVector& Forward, const FVector& Right) const;
	void DrawRailingCreationPostSnapIndicator(FPrimitiveDrawInterface* PDI, const FVector& Center) const;

	/** 绘制墙面采样拖拽命中的绿色墙体预览轮廓。 */
	void DrawWallSurfacePlacementPreview(FPrimitiveDrawInterface* PDI) const;
	void DrawPillarMeshPlacementPreview(FPrimitiveDrawInterface* PDI) const;
	void DrawRailingMeshPlacementPreview(FPrimitiveDrawInterface* PDI) const;
	bool GetMousePointOnPlane(float PlaneZ, FVector& OutLocation) const;

	/** 当前面板选中的建筑对象。 */
	TWeakObjectPtr<AEHBBuildingActorBase> ActiveBuilding;
	TWeakObjectPtr<AActor> ElementEditorSelectedActor;

	/** 墙体创建工具是否处于激活状态。 */
	bool bWallCreationToolActive = false;
	bool bWallsPanelActive = false;

	/** 用户是否已经按下左键，正在拖拽墙体终点。 */
	bool bWallCreationDragging = false;
	TWeakObjectPtr<AEHB_Pillar> HoveredWallCreationPillar;
	TWeakObjectPtr<AEHB_Pillar> WallCreationStartPillar;
	FGuid HoveredWallCreationNode, WallCreationStartNode;
	int32 HoveredWallCreationNodeRevision = 0, WallCreationStartNodeRevision = 0;
	TWeakObjectPtr<AEHB_Wall> HoveredWallCreationWall;
	TWeakObjectPtr<AEHB_Wall> WallCreationStartWall;
	float HoveredWallCreationWallDistance = 0.0f;
	float WallCreationStartWallDistance = 0.0f;
	int32 HoveredWallCreationPillarFloorIndex = 1;
	int32 WallCreationStartPillarFloorIndex = 1;
	bool bHoveredWallCreationTopSnap = false;
	bool bWallCreationStartTopSnap = false;
	bool bWallCreationRectangleDragLatched = false;
 bool bWallCreationRectangleConstrained = false;
	bool bWallPillarMeasurementTracking = false;

	/** 创建墙面时右键是否处于按下状态；用于区分轻点退出和按住导航。 */
	bool bRightMouseNavigationDown = false;

	/** 右键按下后鼠标是否移动超过阈值；超过阈值时认为用户正在旋转视口。 */
	bool bRightMouseNavigationMoved = false;

	/** 右键按下期间是否使用了 WASD/QE 导航键；使用过则右键抬起不会退出创建状态。 */
	bool bRightMouseNavigationUsed = false;

	/** 右键按下瞬间的屏幕坐标，用于判断是否只是一次轻点。 */
	FIntPoint RightMouseNavigationStart = FIntPoint::ZeroValue;
	int32 ForceViewportCursorVisibleFrames = 0;

	/** 本次拖拽起点的世界坐标。 */
	FVector WallCreationStartLocation = FVector::ZeroVector;

	/** 当前鼠标射线落点的世界坐标。 */
	FVector WallCreationMouseLocation = FVector::ZeroVector;

	/** 本次创建使用的墙高，单位为厘米。 */
	float WallCreationHeight = 300.0f;

	/** 本次创建使用的墙厚度，单位为厘米。 */
	float WallCreationThickness = 20.0f;
	bool bWallCreationPhysicalColumns = true;

	FEHBRailingWallDrag HoveredRailingWall, RailingWallDrag;
	FString RailingWallStatus;
	bool bRailingWallReady = false;
	bool bRailingCreationToolActive = false;
	bool bRailingCreationDragging = false;
	TWeakObjectPtr<AEHB_Pillar> HoveredRailingCreationPillar;
	TWeakObjectPtr<AEHB_Pillar> RailingCreationStartPillar;
	TWeakObjectPtr<AEHB_Railing> HoveredRailingCreationRailing;
	TWeakObjectPtr<AEHB_Railing> RailingCreationStartRailing;
	TWeakObjectPtr<AEHB_Stair> HoveredRailingCreationStair;
	TWeakObjectPtr<AEHB_Stair> RailingCreationStartStair;
	FGuid HoveredRailingCreationPostGuid;
	FGuid RailingCreationStartPostGuid;
	EEHBRailingSide HoveredRailingCreationStairSide = EEHBRailingSide::Left;
	EEHBRailingSide RailingCreationStartStairSide = EEHBRailingSide::Left;
	FVector RailingCreationStartLocation = FVector::ZeroVector;
	FVector RailingCreationMouseLocation = FVector::ZeroVector;
	float RailingCreationHeight = 100.0f;
	float RailingCreationPostSpacing = 120.0f;
	float RailingCreationThickness = 8.0f;

	/** 门窗拖拽放置是否处于激活状态。 */
	bool bDoorWindowPlacementActive = false;

	/** 当前拖拽使用的门窗采样表。 */
	TWeakObjectPtr<UDataTable> DoorWindowPlacementTable;

	/** 当前拖拽使用的数据表行名。 */
	FName DoorWindowPlacementRowName = NAME_None;

	bool bDoorWindowPlacementUsesDefaultClass = false;
	EEHBDoorWindowElementKind DoorWindowPlacementDefaultKind = EEHBDoorWindowElementKind::Window;
	TWeakObjectPtr<UClass> DoorWindowPlacementDefaultClass;

	/** 拖拽中临时生成并跟随鼠标移动的门窗 Actor。 */
	TWeakObjectPtr<AEHB_DoorWindow> PreviewDoorWindowActor;

	/** 当前鼠标命中的墙体；为空时松开鼠标会删除预览门窗。 */
	TWeakObjectPtr<AEHB_Wall> HoveredDoorWindowWall;

	/** 当前门窗预览的世界位置。 */
	FVector DoorWindowPlacementWorldLocation = FVector::ZeroVector;

	/** 墙面采样覆盖拖拽是否处于激活状态。 */
	bool bWallSurfacePlacementActive = false;

	/** 当前拖拽使用的墙面采样表。 */
	TWeakObjectPtr<UDataTable> WallSurfacePlacementTable;

	/** 当前拖拽使用的数据表行名。 */
	FName WallSurfacePlacementRowName = NAME_None;

	/** 是否同时覆盖墙体左右两侧。 */
	bool bWallSurfacePlacementCoverBothSides = true;

	/** 是否交换采样表行中的正反面。 */
	bool bWallSurfacePlacementFlipSampleSides = false;

	/** 当前鼠标命中的墙体。 */
	TWeakObjectPtr<AEHB_Wall> HoveredWallSurfaceWall;

	/** 当前命中是否来自左墙面组件。 */
	bool bHoveredWallSurfaceLeftSide = true;

	bool bPillarMeshPlacementActive = false;
	TWeakObjectPtr<UDataTable> PillarMeshPlacementTable;
	FName PillarMeshPlacementRowName = NAME_None;
	TWeakObjectPtr<AEHB_Pillar> HoveredPillarMeshPillar;
	bool bRailingMeshPlacementActive = false;
	TWeakObjectPtr<UDataTable> RailingMeshPlacementTable;
	FName RailingMeshPlacementRowName = NAME_None;
	TWeakObjectPtr<AEHB_Railing> HoveredRailingMeshRailing;
	TWeakObjectPtr<AEHB_Stair> HoveredRailingMeshStair;
	FGuid HoveredRailingMeshPostGuid;
	EEHBRailingSide HoveredRailingMeshStairSide = EEHBRailingSide::Left;
	bool bRailingMeshPlacementApplyToSinglePost = false;
	TWeakObjectPtr<AEHB_FloorSlab> SelectedFloorSlab;
	EEHBFloorSlabEditHandleKind SelectedFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	int32 SelectedFloorSlabHandleLoopIndex = INDEX_NONE;
	int32 SelectedFloorSlabHandleFirstIndex = INDEX_NONE;
	int32 SelectedFloorSlabHandleSecondIndex = INDEX_NONE;
	TUniquePtr<class FScopedTransaction> ActiveFloorSlabEditTransaction;
	TWeakObjectPtr<AEHB_FloorSlab> HoveredFloorSlabHandleOwner;
	EEHBFloorSlabEditHandleKind HoveredFloorSlabHandleKind = EEHBFloorSlabEditHandleKind::None;
	int32 HoveredFloorSlabHandleLoopIndex = INDEX_NONE;
	int32 HoveredFloorSlabHandleFirstIndex = INDEX_NONE;
	int32 HoveredFloorSlabHandleSecondIndex = INDEX_NONE;
	int32 HoveredFloorSlabToolbarCommandIndex = INDEX_NONE;
	TWeakObjectPtr<AEHB_Floor> SelectedFloor;
	int32 HoveredFloorToolbarCommandIndex = INDEX_NONE;

	TWeakObjectPtr<AEHB_Wall> SelectedWall;
	bool bWallCurveControlSelected = false;
	TUniquePtr<class FScopedTransaction> ActiveWallMoveTransaction;
	bool bRoomFloorWallMoveEnabled = false;
	FEHBRoomFloorWallDrag RoomFloorWallDrag;
	TWeakObjectPtr<AEHBBuildingActorBase> NodeHandleBuilding;
	FGuid SelectedWallNode;
	FEHBUnboundNodeDrag NodeHandleDrag;
	FEHBFinishRegionDrag FinishRegionDrag;
 FEHBSlabCutterDraft SlabCutterDraft;
	TUniquePtr<class FScopedTransaction> ActiveWallCurveControlEditTransaction;
	TWeakObjectPtr<AEHB_Wall> HoveredWallCurveControl;
	TWeakObjectPtr<AEHB_Railing> SelectedRailing;
	EEHBRailingEndpointHandleKind SelectedRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	TUniquePtr<class FScopedTransaction> ActiveRailingEndpointEditTransaction;
	TWeakObjectPtr<AEHB_Railing> HoveredRailingEndpointHandleOwner;
	EEHBRailingEndpointHandleKind HoveredRailingEndpointHandleKind = EEHBRailingEndpointHandleKind::None;
	TWeakObjectPtr<AEHB_Stair> SelectedStair;
	bool bStairBottomControlSelected = false;
	int32 SelectedStairIntermediateControlIndex = INDEX_NONE;
	TUniquePtr<class FScopedTransaction> ActiveStairBottomControlEditTransaction;
	TWeakObjectPtr<AEHB_Stair> HoveredStairBottomControl;
	int32 HoveredStairIntermediateControlIndex = INDEX_NONE;
	bool bHoveredPillarConnectionToolbarButton = false;

	bool bFloorSlabPlacementActive = false;
	bool bFloorSlabPlacementIsFoundation = false;
	float FloorSlabPlacementSize = 500.0f;
	float FloorSlabPlacementThickness = 20.0f;
	FVector FloorSlabPlacementWorldLocation = FVector::ZeroVector;
	FRotator FloorSlabPlacementWorldRotation = FRotator::ZeroRotator;
	bool bFloorSlabPlacementHasRoomFillAnchor = false;
	FGuid FloorSlabPlacementRoomFillAnchorWallGuid;
	EEHBFloorSlabWallSide FloorSlabPlacementRoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;
	int32 FloorSlabPlacementFloorIndex = 0;
	EEHBBuildingFloorElementRole FloorSlabPlacementFloorRole = EEHBBuildingFloorElementRole::None;
	TWeakObjectPtr<AEHB_FloorSlab> PreviewFloorSlabActor;

	bool bFloorPlacementActive = false;
	float FloorPlacementSize = 300.0f;
	FVector FloorPlacementWorldLocation = FVector::ZeroVector;
	FRotator FloorPlacementWorldRotation = FRotator::ZeroRotator;
	int32 FloorPlacementFloorIndex = 1;
	TWeakObjectPtr<AEHB_Floor> PreviewFloorActor;

	TWeakObjectPtr<AEHBGableRoof> SelectedRoof;
	TArray<TWeakObjectPtr<AEHBGableRoof>> SelectedRoofs;
	EEHBRoofEditHandleKind SelectedRoofHandleKind = EEHBRoofEditHandleKind::None;
	int32 SelectedRoofHandleFirstIndex = INDEX_NONE;
	int32 SelectedRoofHandleSecondIndex = INDEX_NONE;
	TUniquePtr<class FScopedTransaction> ActiveRoofEditTransaction;
	bool bRoofEditInputDeltaLogged = false;
	TWeakObjectPtr<AEHBGableRoof> HoveredRoofHandleOwner;
	EEHBRoofEditHandleKind HoveredRoofHandleKind = EEHBRoofEditHandleKind::None;
	int32 HoveredRoofHandleFirstIndex = INDEX_NONE;
	int32 HoveredRoofHandleSecondIndex = INDEX_NONE;
	bool bRoofPlacementActive = false;
	float RoofPlacementLength = 600.0f;
	float RoofPlacementWidth = 500.0f;
	float RoofPlacementPitchDegrees = 25.0f;
	float RoofPlacementThickness = 20.0f;
	float RoofPlacementEaveOffset = 30.0f;
	bool bRoofPlacementGenerateRidge = true;
	bool bRoofPlacementGenerateEaves = true;
	bool bRoofPlacementGenerateGableRakes = true;
	bool bRoofPlacementGenerateGableEndWalls = true;
	float RoofPlacementGableEndWallBoundaryInset = 0.0f;
	bool bRoofPlacementUseHipRoof = false;
	bool bRoofPlacementUseHalfHipRoof = false;
	TArray<FVector> RoofPlacementFittedFootprint;
	bool bRoofPlacementFitsFloorSlab = false;
	int32 RoofPlacementFloorIndex = 1;
	FVector RoofPlacementWorldLocation = FVector::ZeroVector;
	FRotator RoofPlacementWorldRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AEHBGableRoof> PreviewRoofActor;

	bool bStairPlacementActive = false;
	float StairPlacementHeight = 280.0f;
	float StairPlacementWidth = 150.0f;
	float StairPlacementTreadDepth = 30.0f;
	bool bStairPlacementGenerateTreads = true;
	bool bStairPlacementFillRisers = true;
	bool bStairPlacementFillBottomPart = false;
	bool bStairPlacementGenerateSides = true;
	bool bStairPlacementGenerateSideGuards = false;
	bool bStairPlacementGenerateRailing = false;
	bool bStairPlacementGenerateLeftRailing = true;
	bool bStairPlacementGenerateRightRailing = true;
	int32 StairPlacementRailingStepsPerPost = 2;
	float StairPlacementRailingEdgeInset = 10.0f;
	float StairPlacementRailingPostForwardOffset = 0.0f;
	float StairPlacementResolvedHeight = 280.0f;
	bool bStairPlacementUseAutoBottom = false;
	FVector StairPlacementAutoBottomWorldLocation = FVector::ZeroVector;
	FVector StairPlacementWorldLocation = FVector::ZeroVector;
	FRotator StairPlacementWorldRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AEHB_Stair> PreviewStairActor;
	TWeakObjectPtr<AEHB_Railing> PreviewStairLeftRailingActor;
	TWeakObjectPtr<AEHB_Railing> PreviewStairRightRailingActor;
};
