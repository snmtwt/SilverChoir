// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EHBWallTopology.h"
#include "Definitions/EHBCommittedEdit.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_RailingGate.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHBHipRoof.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "EHBBuildingToolset.generated.h"

class AEHBBuildingActorBase;
class AEHBElementActorBase;
class AEHB_DoorWindow;
class AEHB_Pillar;
class AEHB_Stair;
class AEHB_Wall;
struct FEHBPreparedWallOpening;

/** One identity, version and target per entry; also supported by UE tool JSON schema generation. */
USTRUCT(BlueprintType)
struct FEHBVersionedNodeMoveRequest
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB") FGuid NodeGuid;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB") int32 ExpectedRevision=0;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB") FVector ExpectedPosition=FVector::ZeroVector;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB") FVector TargetPosition=FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FEHBToolsetStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	bool bHasEditorWorld = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	bool bIsBuildingModeActive = false;

	/** True when an EHB_Building actor is selected in the editor. */
	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	bool bHasSelectedBuilding = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	bool bHasActiveBuilding = false;

	/** The selected EHB_Building actor used by AI creation tools. */
	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	TObjectPtr<AEHBBuildingActorBase> SelectedBuilding = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	TObjectPtr<AEHBBuildingActorBase> ActiveBuilding = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	FString LevelName;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	FString ReadinessMessage;
};

USTRUCT(BlueprintType)
struct FEHBToolsetOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB")
	FString Message;
	/** Populated only by a successfully committed unified movement command. */
	UPROPERTY(BlueprintReadOnly,Category="EHB") FEHBCommittedEdit CommittedEdit;
};

USTRUCT(BlueprintType)
struct FEHBToolsetRectRoomSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "EHB")
	FString Name;

	/** Building-local room center. Z can be left at 0 for automatic floor stacking. */
	UPROPERTY(BlueprintReadWrite, Category = "EHB")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "EHB")
	float Width = 600.0f;

	UPROPERTY(BlueprintReadWrite, Category = "EHB")
	float Depth = 400.0f;

	UPROPERTY(BlueprintReadWrite, Category = "EHB")
	int32 FloorIndex = 1;
};

/**
 * AI-facing MCP toolset for the Parametric Building Toolset.
 *
 * Read GetModelingGuide first. This plugin is relation-driven: walls are created
 * by connecting pillars, doors/windows are attached to walls, foundations are
 * created before upper building elements, and room slabs should be created
 * through room-fill tools. Ground-touching or low platform slabs should be
 * foundations, not normal room slabs. Do not create unsupported plugin
 * elements or spawn EHB actors directly through generic editor tools.
 */
UCLASS()
class UEHBBuildingToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
 UFUNCTION(CallInEditor,Category="Easy House Builder|Editing")
 static FEHBToolsetOperationResult SetWallOpenings(AEHB_Wall* Wall,const TArray<FEHBCutOperation>& Cuts,int32 ExpectedGraphRevision,int32 ExpectedGeometryRevision,bool bPreview=false);
 // Read-only C++ preview. Not a persistent edit or a viewport/MCP tool yet.
 static FEHBToolsetOperationResult PreviewWallOpenings(AEHB_Wall* Wall,const TArray<FEHBCutOperation>& Cuts,int32 ExpectedGraphRevision,int32 ExpectedGeometryRevision,FEHBPreparedWallOpening& Out);

	/**
	 * Start here. Returns the modeling rules, safe call order, coordinate system,
	 * common recipes and anti-patterns for AI agents using this toolset.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Guide")
	static FString GetModelingGuide();

	/**
	 * Return a structured JSON capability manifest for AI agents: safe startup
	 * protocol, coordinate conventions, recommended functions, key parameters,
	 * defaults and common failure modes. Prefer this for machine planning and
	 * GetModelingGuide for human-readable rules.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Guide")
	static FString GetToolsetCapabilities();

	/**
	 * Inspect whether an editor world exists and whether an existing EHB_Building
	 * actor is selected or uniquely resolvable. AI creation tools must use that
	 * existing building. If bHasSelectedBuilding is false, stop and ask the user
	 * to select a building object; do not create elements under an arbitrary or
	 * newly spawned building.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static FEHBToolsetStatus GetStatus();

	/**
	 * Return the selected/current existing EHB_Building actor. If exactly one
	 * building exists, the toolset may select and reuse it. If this returns null,
	 * stop and ask the user to select a building object before creating elements.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHBBuildingActorBase* GetActiveBuilding();

	/**
	 * Guarded compatibility entry point for AI clients. Reuses the existing
	 * selected/current building if available and does not auto-spawn a new
	 * building for ordinary AI generation.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHBBuildingActorBase* CreateBuilding(const FString& Name, FVector WorldLocation);

	/**
	 * Delete all EHB elements attached to a building. This is destructive and
	 * requires bConfirm=true by design.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static FEHBToolsetOperationResult ClearBuilding(AEHBBuildingActorBase* Building, bool bConfirm);

	/**
	 * Create a structural pillar in building-local coordinates. Local (0,0,0)
	 * is the active Building actor location/center. Walls must use pillars
	 * created by this function, then call ConnectPillars.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHB_Pillar* CreatePillar(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalLocation,
		float Height = 300.0f,
		float Width = 30.0f,
		float Depth = 30.0f,
		int32 FloorIndex = 1);

	/**
	 * Create a wall by connecting two existing pillars. This is the only safe
	 * way for AI to create walls because it registers pillar-wall topology,
	 * floor assignment and closed-loop room data on the owning building.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHB_Wall* ConnectPillars(
		AEHBBuildingActorBase* Building,
		AEHB_Pillar* StartPillar,
		AEHB_Pillar* EndPillar,
		float WallHeight = 300.0f,
		float WallThickness = 20.0f);

	/**
	 * Apply curved-wall settings to an existing wall, rebuild it, and refresh its connected pillars.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static FEHBToolsetOperationResult SetWallCurve(AEHB_Wall* Wall, float ControlOffset, float SegmentLength = 50.0f);

	/**
	 * Create a foundation or exceptional manual slab from a building-local
	 * polygon. For normal room floor/ceiling slabs, do not call this directly:
	 * use CreateRoomFilledFloorSlabAtPoint so the plugin's Fill Room logic owns the
	 * room polygon. If a slab-like platform should sit on the ground or be only a
	 * little above ground, create it as a foundation with bFoundation=true and
	 * control its height with Thickness/TopZ instead of creating a normal floor slab
	 * at ground level.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHB_FloorSlab* CreateFloorSlab(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		const TArray<FVector>& LocalTopPolygon,
		float Thickness = 20.0f,
		float TopZ = 0.0f,
		bool bFoundation = false,
		bool bKeepFoundationBottomOnGround = false,
		float VisualExpansion = 0.0f,
		int32 FloorIndex = 1);

	/**
	 * Create a current-architecture gable roof in building-local coordinates.
	 * The roof stores its uncut parametric data and may auto-cut itself against
	 * overlapping cuttable elements when bCutCollidingElements is true.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Roof")
	static AEHBGableRoof* CreateGableRoof(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalCenter,
		float YawDegrees = 0.0f,
		float Length = 600.0f,
		float Width = 400.0f,
		float PitchDegrees = 25.0f,
		float Thickness = 20.0f,
		float EaveOffset = 30.0f,
		float RidgeOffsetRatio = 0.0f,
		EEHBRoofAxisMode AxisMode = EEHBRoofAxisMode::RidgeAlongX,
		bool bGenerateRidge = true,
		bool bGenerateEaves = true,
		bool bGenerateGableRakes = true,
		bool bGenerateGableEndWalls = true,
		float RidgeWidth = 18.0f,
		float RidgeHeight = 10.0f,
		float EaveWidth = 18.0f,
		float EaveHeight = 18.0f,
		float GableRakeWidth = 16.0f,
		float GableRakeHeight = 10.0f,
		bool bCutCollidingElements = false,
		bool bRemoveDisconnectedCutPieces = true,
		bool bKeepCutAwayDisconnectedPieces = false,
		bool bUseExactSourceMeshCutters = true,
		bool bUseControlledEnvelopeCutters = true,
		float EnvelopeConcavityBridgeDistance = 80.0f,
		float EnvelopeProjectionPadding = 2.0f,
		float EnvelopeZPadding = 80.0f,
		float EnvelopeMinExtrudeHeight = 120.0f,
		float EnvelopeMinProjectedArea = 4.0f,
		float EnvelopeThinProjectionFallbackWidth = 4.0f,
		float EnvelopePathCleanTolerance = 0.05f,
		int32 FloorIndex = 1,
		bool bUseWallFootprintCutters = true,
		int32 MinWallFootprintGroupWallCount = 2,
		float WallFootprintGroupEndpointTolerance = 80.0f,
		float WallFootprintPadding = -2.0f,
		float WallFootprintMaxDimension = 0.0f,
		float WallFootprintMinArea = 100.0f,
		bool bShowCutDebugVisualization = false,
		bool bDebugDrawRawRoofBounds = true,
		bool bDebugDrawSourceBounds = true,
		bool bDebugDrawCutterBounds = true,
		bool bDebugDrawResultBounds = true,
		float CutDebugDrawDuration = 12.0f,
		float CutDebugDrawThickness = 2.0f,
		float GableEndWallBoundaryInset = 0.0f);

	/**
	 * Create a default hip roof in building-local coordinates. Hip roofs share
	 * the same unified cutting pipeline as gable roofs, but use four sloped roof
	 * planes and hip ridges instead of triangular gable end walls.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Roof")
	static AEHBHipRoof* CreateHipRoof(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalCenter,
		float YawDegrees = 0.0f,
		float Length = 600.0f,
		float Width = 400.0f,
		float PitchDegrees = 25.0f,
		float Thickness = 20.0f,
		float EaveOffset = 30.0f,
		float RidgeOffsetRatio = 0.0f,
		EEHBRoofAxisMode AxisMode = EEHBRoofAxisMode::RidgeAlongX,
		bool bGenerateRidge = true,
		bool bGenerateEaves = true,
		bool bGenerateHipRidges = true,
		float RidgeWidth = 18.0f,
		float RidgeHeight = 10.0f,
		float EaveWidth = 18.0f,
		float EaveHeight = 18.0f,
		float HipRidgeWidth = 16.0f,
		float HipRidgeHeight = 10.0f,
		bool bCutCollidingElements = false,
		bool bRemoveDisconnectedCutPieces = true,
		bool bKeepCutAwayDisconnectedPieces = false,
		bool bUseExactSourceMeshCutters = true,
		bool bUseControlledEnvelopeCutters = true,
		float EnvelopeConcavityBridgeDistance = 80.0f,
		float EnvelopeProjectionPadding = 2.0f,
		float EnvelopeZPadding = 80.0f,
		float EnvelopeMinExtrudeHeight = 120.0f,
		float EnvelopeMinProjectedArea = 4.0f,
		float EnvelopeThinProjectionFallbackWidth = 4.0f,
		float EnvelopePathCleanTolerance = 0.05f,
		int32 FloorIndex = 1,
		bool bUseWallFootprintCutters = true,
		int32 MinWallFootprintGroupWallCount = 2,
		float WallFootprintGroupEndpointTolerance = 80.0f,
		float WallFootprintPadding = -2.0f,
		float WallFootprintMaxDimension = 0.0f,
		float WallFootprintMinArea = 100.0f,
		bool bShowCutDebugVisualization = false,
		bool bDebugDrawRawRoofBounds = true,
		bool bDebugDrawSourceBounds = true,
		bool bDebugDrawCutterBounds = true,
		bool bDebugDrawResultBounds = true,
		float CutDebugDrawDuration = 12.0f,
		float CutDebugDrawThickness = 2.0f);

	/**
	 * Replace an existing floor slab/foundation outline with an arbitrary
	 * building-local polygon. This is the preferred AI interface for irregular
	 * foundations or manually edited slab shapes. Existing holes are preserved
	 * only when they remain inside the new outline.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult SetFloorSlabTopPolygon(
		AEHB_FloorSlab* FloorSlab,
		const TArray<FVector>& LocalTopPolygon,
		bool bPreserveExistingHoles = true);

	/**
	 * Insert one corner after an existing floor slab/foundation corner. Use this
	 * for incremental control-point style editing.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult InsertFloorSlabCorner(
		AEHB_FloorSlab* FloorSlab,
		int32 AfterPointIndex,
		FVector NewLocalLocation);

	/** Move one existing floor slab/foundation corner in slab local space. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult MoveFloorSlabCorner(
		AEHB_FloorSlab* FloorSlab,
		int32 PointIndex,
		FVector NewLocalLocation);

	/** Remove one floor slab/foundation corner while keeping at least 3 points. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult RemoveFloorSlabCorner(
		AEHB_FloorSlab* FloorSlab,
		int32 PointIndex);

	/**
	 * Advanced compatibility floor/ceiling room fill using an anchor wall side.
	 * AI should prefer CreateRoomFilledFloorSlabAtPoint because shared wall
	 * AnchorSide is easy to choose incorrectly.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|RoomFill")
	static AEHB_FloorSlab* CreateRoomFilledFloorSlab(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		AEHB_Wall* AnchorWall,
		EEHBFloorSlabWallSide AnchorSide,
		float TopZ,
		float Thickness = 20.0f,
		int32 FloorIndex = 1);

	/**
	 * AI-preferred room fill entry. RoomInteriorPoint must be inside the target
	 * closed room in Building local coordinates. The slab actor origin is placed
	 * at that interior point, then Fill Room chooses the containing closed room.
	 * This avoids guessing AnchorSide on shared walls.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|RoomFill")
	static AEHB_FloorSlab* CreateRoomFilledFloorSlabAtPoint(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector RoomInteriorPoint,
		float TopZ,
		float Thickness = 20.0f,
		int32 FloorIndex = 1);

	/**
	 * Cut a rectangular committed hole in an existing floor slab. Coordinates
	 * are in the slab's local top-polygon space, usually matching the active
	 * Building local XY layout. Use this for stair openings before creating a
	 * stair. Do not create separate generic mesh cutters.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult AddFloorSlabRectangularHole(
		AEHB_FloorSlab* FloorSlab,
		FVector LocalCenter,
		float Width,
		float Depth,
		float YawDegrees = 0.0f);

 /** Replace the complete opening sources of a topology-owned slab atomically. */
 UFUNCTION(meta=(AICallable), Category="EasyHouse|FloorSlab")
 static FEHBToolsetOperationResult SetFloorSlabOpenings(AEHB_FloorSlab* FloorSlab,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,int32 ExpectedGraphRevision,int32 ExpectedGeometryRevision,bool bPreview=false);

	/**
	 * Cut a circular committed hole in an existing floor slab. Coordinates are
	 * in the slab's local top-polygon space. SideCount is clamped to a safe
	 * polygon count.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|FloorSlab")
	static FEHBToolsetOperationResult AddFloorSlabCircularHole(
		AEHB_FloorSlab* FloorSlab,
		FVector LocalCenter,
		float Diameter,
		int32 SideCount = 32);

	/**
	 * Create a basic procedural stair owned by the active Building. LocalTopLocation
	 * is the actual upper landing/start point in Building local coordinates.
	 * The stair descends from that point along its local +X direction, rotated
	 * by YawDegrees around +Z. For a stair through a slab, call
	 * AddFloorSlabRectangularHole first.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Stair")
	static AEHB_Stair* CreateStair(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalTopLocation,
		float YawDegrees,
		float StairHeight = 300.0f,
		float StairWidth = 150.0f,
		float TreadDepth = 30.0f,
		bool bGenerateTreads = true,
		bool bFillRisers = true,
		bool bGenerateSides = true,
		bool bFillBottomPart = false,
		bool bGenerateSideGuards = false,
		int32 FloorIndex = 1,
		float MinStepHeight = 10.0f,
		float MaxStepHeight = 20.0f,
		float NosingLength = 2.0f);

	/**
	 * Grounded stair creation entry. LocalBottomLocation is the lower-floor
	 * stair foot in Building local coordinates. UpDirectionYawDegrees points
	 * from the lower floor toward the upper landing. The tool computes the
	 * internal top anchor and creates a grounded stair, avoiding the common
	 * top-anchor/direction inversion mistake. Prefer CreateStairFromSide when
	 * a wall side, slab side or upper slab hole side is available.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Stair")
	static AEHB_Stair* CreateStairFromBottom(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalBottomLocation,
		float UpDirectionYawDegrees,
		float StairHeight = 300.0f,
		float StairWidth = 150.0f,
		float TreadDepth = 30.0f,
		bool bGenerateTreads = true,
		bool bFillRisers = true,
		bool bGenerateSides = true,
		bool bFillBottomPart = false,
		bool bGenerateSideGuards = false,
		int32 FloorIndex = 1,
		float MinStepHeight = 10.0f,
		float MaxStepHeight = 20.0f,
		float NosingLength = 2.0f);

	/**
	 * Create a stair by sampling a side of an existing floor slab or wall.
	 * Provide exactly one of SourceFloorSlab or SourceWall. SideSegmentIndex
	 * selects the slab edge, slab-hole edge or wall-side segment; DistanceAlongSide
	 * measures along that segment; bUseLeftSide chooses which side normal points
	 * away from the sampled edge; SideOffset offsets the stair anchor along that
	 * normal. For indoor stairs, cut the upper floor slab first, pass that slab
	 * as SourceFloorSlab and set SlabHoleIndex to the committed hole index. A
	 * valid SlabHoleIndex is always treated as the upper landing edge so the
	 * stair touches the hole side without leaving a visible slab gap. For wall
	 * sides or lower platform sides, leave SlabHoleIndex=-1 and use
	 * bSideIsUpperLanding only when the sampled side is the upper landing edge.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Stair")
	static AEHB_Stair* CreateStairFromSide(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		AEHB_FloorSlab* SourceFloorSlab = nullptr,
		AEHB_Wall* SourceWall = nullptr,
		int32 SideSegmentIndex = 0,
		float DistanceAlongSide = 150.0f,
		bool bUseLeftSide = true,
		float SideOffset = 0.0f,
		int32 SlabHoleIndex = -1,
		bool bSideIsUpperLanding = false,
		float StairHeight = 300.0f,
		float StairWidth = 150.0f,
		float TreadDepth = 30.0f,
		bool bGenerateTreads = true,
		bool bFillRisers = true,
		bool bGenerateSides = true,
		bool bFillBottomPart = false,
		bool bGenerateSideGuards = false,
		int32 FloorIndex = 1,
		float MinStepHeight = 10.0f,
		float MaxStepHeight = 20.0f,
		float NosingLength = 2.0f);

	/**
	 * Create a stair from explicit lower and upper landing endpoints. This is
	 * the AI-preferred entry for curved/turned stairs: the path is constrained
	 * by the upper landing position/direction and the lower step position/
	 * direction only. Intermediate stair controls are disabled and ignored.
	 * BottomUpDirectionYawDegrees points from the lower foot toward the upper
	 * landing. TopDownDirectionYawDegrees points from the upper landing toward
	 * the lower foot.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Stair")
	static AEHB_Stair* CreateStairBetweenLandings(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalBottomLocation,
		float BottomUpDirectionYawDegrees,
		FVector LocalTopLocation,
		float TopDownDirectionYawDegrees,
		float StairWidth = 150.0f,
		float TreadDepth = 30.0f,
		bool bGenerateTreads = true,
		bool bFillRisers = true,
		bool bGenerateSides = true,
		bool bFillBottomPart = false,
		bool bGenerateSideGuards = false,
		int32 FloorIndex = 1,
		float MinStepHeight = 10.0f,
		float MaxStepHeight = 20.0f,
		float NosingLength = 2.0f);

	/**
	 * Create a standalone procedural railing along a Building-local line.
	 * LocalStart and LocalEnd are the visible railing endpoints in active
	 * Building local centimeters. Use this for balcony, terrace, porch and
	 * platform guardrails. If StartPillar or EndPillar is provided, that pillar's
	 * Building-local location replaces the matching endpoint and the railing
	 * records a boundary anchor to the pillar. If the railing should start or end
	 * at a pillar, pass that pillar directly as StartPillar or EndPillar; do not
	 * create nearby filler railings, overlapping railings, helper walls or dummy
	 * wall segments to simulate the connection. For stair-attached railings, use
	 * CreateRailingOnStair.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Railing")
	static AEHB_Railing* CreateRailing(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		FVector LocalStart,
		FVector LocalEnd,
		float RailingHeight = 100.0f,
		float PostSpacing = 120.0f,
		float RailThickness = 8.0f,
		EEHBRailingFillMode FillMode = EEHBRailingFillMode::PostsAndRails,
		int32 FloorIndex = 1,
		float PostWidth = 8.0f,
		float MaxRailSegmentLength = 120.0f,
		AEHB_Pillar* StartPillar = nullptr,
		AEHB_Pillar* EndPillar = nullptr);

	/**
	 * Create a procedural railing hosted by an existing EHB stair. The railing
	 * path follows the stair as it is edited and records a HostedElement
	 * relationship instead of becoming a free-floating accessory. Pass
	 * FloorIndex=0 to inherit the stair's floor index.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Railing")
	static AEHB_Railing* CreateRailingOnStair(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		AEHB_Stair* Stair,
		EEHBRailingSide Side = EEHBRailingSide::Left,
		float RailingHeight = 100.0f,
		int32 StepsPerPost = 1,
		float RailThickness = 8.0f,
		float StairSideOffset = 10.0f,
		int32 FloorIndex = 0);

	/**
	 * Create a gate hosted by an existing EHB railing. The gate cuts its opening
	 * from the owning railing through GateConnections and creates its own gate
	 * leaf actor. Do not fake gate openings with extra short railings or wall
	 * segments.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Railing")
	static AEHB_RailingGate* CreateRailingGate(
		AEHBBuildingActorBase* Building,
		const FString& Name,
		AEHB_Railing* Railing,
		float DistanceFromStart = 150.0f,
		float GateWidth = 90.0f,
		float GateHeight = 95.0f,
		float GateThickness = 6.0f,
		EEHBRailingGateHingeSide HingeSide = EEHBRailingGateHingeSide::Left,
		float OpenAngleDegrees = 0.0f,
		int32 FloorIndex = 0);

	/**
	 * Return a JSON plan for matching foundation thickness and stair riser
	 * height. Use DesiredStepHeight when the stair cadence is known; use
	 * FoundationThickness when the foundation height is fixed.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Stair")
	static FString GetStairFoundationPlan(
		float StairHeight = 300.0f,
		float TreadDepth = 30.0f,
		float DesiredStepHeight = 0.0f,
		float FoundationThickness = 0.0f,
		float MinStepHeight = 10.0f,
		float MaxStepHeight = 20.0f,
		float NosingLength = 2.0f);

	/**
	 * Add a door or window to an existing wall. Do not create door/window actors
	 * directly; this function binds the opening to the wall and rebuilds the wall
	 * mesh so boolean-like openings stay synchronized.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static AEHB_DoorWindow* AddDoorWindow(
		AEHB_Wall* Wall,
		bool bDoor,
		float DistanceFromStart,
		float Width = 120.0f,
		float Height = 180.0f,
		float SillHeight = 90.0f,
		float OpeningThickness = 0.0f);

	/**
	 * Replace an existing wall-hosted door/window with another door/window actor.
	 * The replacement actor keeps its class, spline shape and current wall
	 * position; the old actor is removed.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static bool ReplaceDoorWindow(
		AEHB_DoorWindow* ReplacementDoorWindow,
		AEHB_DoorWindow* ExistingDoorWindow);

	/**
	 * Create a rectangular room using the correct EHB relationship flow:
	 * optional foundation first, then four pillars, four connected walls and an
	 * optional ceiling slab generated through Fill Room logic. LocalCenter is
	 * relative to the active Building actor, so LocalCenter=(0,0,0) centers the
	 * room on the Building actor location.
	 * Prefer this for simple rooms instead of manually calling low-level tools.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Recipes")
	static FString CreateRectangularRoom(
		AEHBBuildingActorBase* Building,
		const FString& NamePrefix,
		FVector LocalCenter,
		float Width,
		float Depth,
		float WallHeight = 300.0f,
		float WallThickness = 20.0f,
		float PillarSize = 20.0f,
		bool bCreateFoundation = true,
		bool bCreateCeilingSlab = true,
		int32 FloorIndex = 1,
		float FoundationThickness = 20.0f);

	/**
	 * Create a larger rectangular-room floor plan while reusing exact shared
	 * corner pillars and full matching wall edges. Use CreateAIHouseFromRoomPlan
	 * instead for ordinary AI-generated houses, T-junctions, partial shared
	 * walls or overlapping wall spans. Room LocalCenter.Z may be left at 0;
	 * FloorHeight is then used to stack floors by FloorIndex. FloorHeight is the
	 * distance from one floor wall base to the next floor wall base, usually
	 * equal to WallHeight. Do not add slab thickness to it; floor slabs embed
	 * downward into the lower floor volume.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Recipes")
	static FString CreateRectangularFloorPlan(
		AEHBBuildingActorBase* Building,
		const FString& NamePrefix,
		const TArray<FEHBToolsetRectRoomSpec>& Rooms,
		float WallHeight = 300.0f,
		float WallThickness = 20.0f,
		float PillarSize = 20.0f,
		float FloorHeight = 300.0f,
		bool bCreateFoundation = true,
		bool bCreateCeilingSlabs = true,
		float FoundationPadding = 20.0f,
		float FoundationThickness = 20.0f);

	/**
	 * AI-preferred house shell generator from rectangular room specs. It turns
	 * room rectangles into a wall-line graph, automatically reuses pillars,
	 * splits walls at shared endpoints/T-junctions, creates unique short wall
	 * segments, then optionally creates a foundation and room-filled slabs.
	 * Prefer this over CreateRectangularFloorPlan for real multi-room houses.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Recipes")
	static FString CreateAIHouseFromRoomPlan(
		AEHBBuildingActorBase* Building,
		const FString& NamePrefix,
		const TArray<FEHBToolsetRectRoomSpec>& Rooms,
		float WallHeight = 300.0f,
		float WallThickness = 20.0f,
		float PillarSize = 20.0f,
		float FloorHeight = 300.0f,
		bool bCreateFoundation = true,
		bool bCreateRoomSlabs = true,
		float FoundationPadding = 20.0f,
		float FoundationThickness = 20.0f);

	/**
	 * Explicitly correct an element's floor membership. Use this when AI edits
	 * existing geometry or creates a multi-floor building and needs the same
	 * FloorIndex/FloorRole data that manual tools maintain.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Floors")
	static FEHBToolsetOperationResult SetElementFloorAssignment(
		AEHBElementActorBase* Element,
		int32 FloorIndex,
		EEHBBuildingFloorElementRole FloorRole);

	/**
	 * Return a JSON summary grouped by FloorIndex, including element roles and
	 * closed-loop room counts. Use this after multi-floor generation.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Floors")
	static FString GetFloorSummary(AEHBBuildingActorBase* Building);

	/**
	 * Return a JSON snapshot of the building: actor path, guid, attached
	 * elements, relation count and closed-loop count. Use this after edits.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse")
	static FString GetBuildingSnapshot(AEHBBuildingActorBase* Building);

	/** Read-only versioned junction/wall data and migration diagnostics. Does not rebuild or repair the selected building. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString GetWallTopologySnapshot(AEHBBuildingActorBase* Building);

	/** Preview or explicitly initialize the persisted migration baseline. Does not replace legacy authoring. */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology", meta = (AICallable))
	static FString PrepareTopologyMigration(AEHBBuildingActorBase* Building, bool bApply = false);

	/** Prepare persistent node definitions under one transaction; never activates node authority. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FString PrepareWallNodeDefinitions(AEHBBuildingActorBase* Building,bool bApply=false);

	/** Explicitly migrate wall connection endpoints to owned node identities in one undoable command.
	 * Physical pillar pose/geometry authority and ordinary full-delete semantics are retained. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FString MigrateWallNodeOwnership(AEHBBuildingActorBase* Building,bool bApply=false);

	/** Activate building-owned node values for native bound nodes. Preview is read-only;
	 * application is one undoable transaction. Independent physical deletion is not enabled. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FString MigrateWallNodeAuthority(AEHBBuildingActorBase* Building,bool bApply=false);

	/** Enable linked wall corner editing atomically, preserving existing physical columns. */
	UFUNCTION(BlueprintCallable, Category="EHB|Topology", meta=(AICallable))
	static FEHBToolsetOperationResult EnableWallNodeEditing(AEHBBuildingActorBase* Building,bool bPreviewOnly=true);

	/** Remove only a physical column while retaining the logical wall node. Dependencies require a plan. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FEHBToolsetOperationResult RemovePhysicalColumn(AEHBBuildingActorBase* Building,FGuid NodeGuid,FGuid ExpectedPillarGuid,int32 ExpectedNodeRevision,bool bPreviewOnly=true);
	/** Remove selected native walls and migrate compatible whole-room finishes atomically.
	 * Opening a room preserves its authored finishes as independent regions.
	 * Different styles preserve separate independent regions; slab partitions
	 * fill the removed-wall strip by original room ownership. Display-expanded
	 * slab seams and incomplete/layered coverage still require a separate plan. */
	UFUNCTION(BlueprintCallable,meta=(AICallable),Category="EasyHouse|Topology")
	static FEHBToolsetOperationResult RemoveWalls(AEHBBuildingActorBase* Building,const TArray<FGuid>& WallGuids,int32 ExpectedGraphRevision,bool bPreviewOnly=true);
	/** Edit a retained floor/slab outer polygon in element-local centimeters, at its existing elevation.
	 * Rebind instead: supply an empty polygon and an explicit overlapping room GUID.
	 * Both revisions must describe the current source; preview changes nothing. */
	UFUNCTION(BlueprintCallable,meta=(AICallable),Category="EasyHouse|Floor")
	static FEHBToolsetOperationResult EditFinishRegion(AEHBBuildingActorBase* Building,FGuid ElementGuid,int32 ExpectedGraphRevision,int32 ExpectedGeometryRevision,const TArray<FVector>& LocalPolygon,FGuid RoomGuid,bool bPreviewOnly=true);
	/** Move an actor-free wall node on its existing horizontal floor. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FEHBToolsetOperationResult MoveUnboundWallNode(AEHBBuildingActorBase* Building,FGuid NodeGuid,int32 ExpectedNodeRevision,FVector ExpectedPosition,FVector TargetPosition,bool bPreviewOnly=true);

	/** Read-only fresh node/element/relation/room identity draft. Does not paste or repair Actors. */
	UFUNCTION(meta=(AICallable),Category="EasyHouse|Topology")
	static FString PreviewWallNodeCopy(AEHBBuildingActorBase* Building);

	/** Explicit native wall/pillar building copy, including validated full-room floors/ceilings; separate from generic paste. Offset uses world centimeters. */
	UFUNCTION(meta=(AICallable), Category="EasyHouse|Topology")
	static FString CopyWallNodeBuilding(AEHBBuildingActorBase* Building, FVector WorldOffset);


	/** Read-only pillar/node separation preparation. No deletion or migration commit is available. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString PreviewPillarSeparation(AEHBBuildingActorBase* Building, FGuid PillarGuid);

	/** Read-only XY centerline proposal. Not a commit endpoint; surface/host dependencies remain unplanned. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString PreviewNodeMove(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition);

	/** Read-only wall insertion inspection; never authorizes a split commit. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString PreviewWallSplitForRailing(AEHBBuildingActorBase* Building, FGuid WallGuid, float DistanceFromStart);

	/** Explicit bounded split: plain native pillar/wall buildings only, initialized baseline required.
	 * Independent editor transaction; does not create a railing. Explicit opt-in may
	 * migrate verified native rectangular openings on the source wall only.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitPlainWallSplit(AEHBBuildingActorBase* Building, FGuid WallGuid,
		float DistanceFromStart, int32 ExpectedGraphRevision, FVector ExpectedStart, FVector ExpectedEnd,
		float ExpectedHeight, float ExpectedThickness, bool bMigrateRectangularOpenings = false);

	/** Independent atomic split + native horizontal railing with a free end.
	 * Preserves verified native linear branches anchored to retained columns.
	 * Does not attach to stairs or replace existing columns.
	 * Length and spacing are bounded to 2048 segments. bPreviewOnly executes the same preflight without creating a transaction or actors.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitWallSplitAndRailing(AEHBBuildingActorBase* Building, FGuid WallGuid,
		float DistanceFromStart, int32 ExpectedGraphRevision, FVector ExpectedStart, FVector ExpectedEnd,
		float ExpectedHeight, float ExpectedThickness, FVector RailingWorldEnd,
		float RailingHeight = 100.0f, float RailingThickness = 5.0f, float PostSpacing = 100.0f, bool bPreviewOnly = false);

	/** Read-only simultaneous final-position planning; does not authorize a batch commit. */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString PreviewNodeMoves(AEHBBuildingActorBase* Building, const TArray<FEHBNodeMoveRequest>& Requests);

	/** Transactional XY move for native simple pillar/straight-wall buildings only.
	 * Rejects unplanned hosts/cuts/sampled geometry. Centerline validation is not thickness collision validation.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitBasicNodeMove(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition);

	/** Atomic final-position edit for V2 logical nodes, whether physically bound or not.
	 * Includes supported room finishes. Every request needs its captured source revision. */
	UFUNCTION(meta=(AICallable), Category="EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitWallNodeMoves(AEHBBuildingActorBase* Building,const TArray<FEHBVersionedNodeMoveRequest>& Requests,bool bPreviewOnly=true);
	static FEHBToolsetOperationResult CommitWallNodeMovesNative(AEHBBuildingActorBase* Building,const TArray<FEHBNodeMoveRequest>& Requests,const TMap<FGuid,int32>& ExpectedNodeRevisions,bool bPreviewOnly=true);

	/** Explicit one-command following for unchanged native ground-room floors and room ceiling slabs.
	 * Rejects manual shapes, support/cut dependencies and room identity changes.
	 * Preview performs the same preflight without starting a transaction.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitNodeMoveWithRoomFloors(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition, bool bPreviewOnly = false);

	/** Translate both ends of a native wall in one final-state transaction, following
	 * only supported unchanged ground-room floors and room ceiling slabs. Expected positions are pillar centers
	 * in building-local centimeters, not the inset rendered wall endpoints.
	 * Accepts the selected owning building or this wall as the sole selected actor.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitWallMoveWithRoomFloors(AEHBBuildingActorBase* Building, FGuid WallGuid,
		FVector ExpectedStartPillarPosition, FVector ExpectedEndPillarPosition, FVector LocalDelta, bool bPreviewOnly = false);

	/** Uses the same slab snap solver as manual movement, then previews the resolved centerline target.
	 * Returns snap rotation and host identity separately; does not commit or bind the host.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FString PreviewNodeMoveWithSlabSnap(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition, float MaxSnapDistance = 30.0f);

	/** Explicit fixed-slab policy: revalidate the preview host and world pose, then commit
	 * position, rotation and a non-structural boundary association in one transaction.
	 * Only native simple geometry at unit world scale; room-filled slabs remain unsupported.
	 */
	UFUNCTION(meta = (AICallable), Category = "EasyHouse|Topology")
	static FEHBToolsetOperationResult CommitNodeMoveOnFixedSlab(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FRotator ExpectedWorldRotation, FVector RequestedPosition, FGuid ExpectedSlabGuid, FVector ExpectedSnapWorldPosition, FRotator ExpectedSnapWorldRotation, float MaxSnapDistance = 30.0f);
};
