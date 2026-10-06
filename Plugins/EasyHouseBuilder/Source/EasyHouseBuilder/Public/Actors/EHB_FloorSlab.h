// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Core/EHBOutlineProvenance.h"
#include "Core/EHBLogicalSurface.h"
#include "Cutting/EHBCutTypes.h"
#include "EHB_FloorSlab.generated.h"

class AEHBBuildingActorBase;
class UMaterialInterface;
class UEHBGeneratedMeshComponent;
struct FEHBSurfaceMeshBuildResult;
struct FEHBPlanarSurfaceRegion;


UENUM(BlueprintType)
enum class EEHBFloorSlabCutterShape : uint8
{
	Square UMETA(DisplayName = "Square"),
	Circle UMETA(DisplayName = "Circle")
};

UENUM(BlueprintType)
enum class EEHBFloorSlabWallSide : uint8
{
	None UMETA(DisplayName = "None"),
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBFloorSlabCutterData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	EEHBFloorSlabCutterShape Shape = EEHBFloorSlabCutterShape::Square;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab", meta = (ClampMin = "1.0", Units = "cm"))
	float Size = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab", meta = (ClampMin = "1.0", Units = "cm"))
	float Height = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab", meta = (ClampMin = "8", ClampMax = "96"))
	int32 CircleSideCount = 32;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBFloorSlabHole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	TArray<FVector> LocalPolygon;
};

USTRUCT()
struct FEHBSlabDisplayPartition
{
 GENERATED_BODY()
 UPROPERTY() int32 SourceVersion=0;
 UPROPERTY() float SourceThickness=0;
 UPROPERTY() TArray<FEHBFloorSlabHole> SourceHoles;
 UPROPERTY() TArray<FEHBCutOperation> SourceCuts;
 UPROPERTY() TArray<FVector> SourcePolygon;
 UPROPERTY() TArray<FEHBLogicalSurfaceRegion> Regions;
 UPROPERTY() float SourceExpansion=0;
 UPROPERTY() int32 Priority=0;
 bool IsActive() const { return !Regions.IsEmpty(); }
};

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_FloorSlab"))
class EASYHOUSEBUILDER_API AEHB_FloorSlab : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Slab")
	TObjectPtr<UEHBGeneratedMeshComponent> MeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab", meta = (ClampMin = "1.0", Units = "cm"))
	float Thickness = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab", meta = (Units = "cm"))
	float Offset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	bool bIsFoundation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	bool bKeepFoundationBottomOnGround = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Foundation", meta = (ClampMin = "10.0", Units = "cm", DisplayName = "Ground Trace Spacing"))
	float FoundationGroundTraceSpacing = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	TArray<FVector> LocalTopPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Appearance", meta = (DisplayName = "可视扩边", ToolTip = "只在生成显示网格时向外扩展外轮廓，不会修改真实控制点、孔洞或保存的楼板轮廓。", ClampMin = "0.0", Units = "cm"))
	float VisualExpansion = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Snapping")
	bool bEnableAdjacentSlabSnap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|AI")
	bool bHasAIFoundationSource = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|AI", meta = (MultiLine = "true"))
	FString AIFoundationSourceJson;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|AI")
	TArray<FVector> AIDesignTopPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|AI", meta = (Units = "cm"))
	float AIFoundationExpansion = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Deprecated", meta = (DeprecatedProperty, DeprecationMessage = "Use CutOperations instead."))
	TArray<FEHBFloorSlabHole> LocalHoles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Deprecated", meta = (DeprecatedProperty, DeprecationMessage = "Use CutOperations instead."))
	TArray<FEHBFloorSlabCutterData> PreviewCutters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab")
	TSoftObjectPtr<UMaterialInterface> SurfaceMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Room Fill")
	bool bHasRoomFillAnchor = false;

	/** Exact room selected by the last successful room fill; a wall anchor alone is insufficient. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Slab|Room Fill")
	FGuid RoomFillLoopGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Slab|Room Fill")
	int32 RoomFillFloorIndex = INDEX_NONE;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Room Fill")
	FGuid RoomFillAnchorWallGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Slab|Room Fill")
	EEHBFloorSlabWallSide RoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;

	AEHB_FloorSlab();

	/** Last successful generation source. This does not enable automatic updates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Outline|Source", meta=(DisplayName="轮廓生成来源"))
	EEHBOutlineSource OutlineSource = EEHBOutlineSource::ManualOrUnclassified;

	UFUNCTION(BlueprintPure, Category = "Outline|Source", meta=(DisplayName="生成轮廓是否保持未修改"))
	bool IsRecordedOutlineUnchanged() const;

	/** Internal generation paths record only after the complete operation succeeds. */
	void RecordOutlineSource(EEHBOutlineSource Source);


	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void ConfigureDefaultSlab(AEHBBuildingActorBase* InBuilding, const FVector& LocalCenter, float InSize, float InThickness, bool bInIsFoundation);
	void ConfigureDefaultSlab(AEHBBuildingActorBase* InBuilding, const FTransform& LocalTransform, float InSize, float InThickness, bool bInIsFoundation);

	UFUNCTION(BlueprintCallable, Category = "Floor Slab")
	bool RebuildSlabMesh();

	/** Node edit path: retain matching output and provenance; report actual updates. */
	bool SetSlabOutlineIfNeeded(const TArray<FVector>& OuterPolygon,bool& bOutChanged);

	/** Candidate replacement is built before any design/component mutation. Caller owns the transaction. */
	UFUNCTION(BlueprintCallable, Category = "Floor Slab|Geometry")
	bool SetSlabOutline(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes, bool bDiscardPreviewCutters = false);
	bool ValidateSlabOutline(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes, bool bDiscardPreviewCutters = false) const;
	/** Explicit candidate display allocation; source geometry and display are applied together. */
	bool SetPartitionedSlabOutline(const TArray<FVector>& OuterPolygon, const FEHBSlabDisplayPartition& Partition);
	bool ValidatePartitionedSlabOutline(const TArray<FVector>& OuterPolygon, const FEHBSlabDisplayPartition& Partition) const;
	bool BuildUnallocatedDisplayRegion(const TArray<FVector>& OuterPolygon, TArray<FEHBLogicalSurfaceRegion>& Regions) const;
	/** Read-only committed candidate display, before ownership allocation. Ignores current
	 * partition and preview cutters; preserves every region from the supplied source.
	 * Uses this slab's thickness/expansion. Does not authorize applying a partition. */
	bool BuildCandidateDisplayRegions(const TArray<FVector>& OuterPolygon,
		const TArray<FEHBFloorSlabHole>& Holes, const TArray<FEHBCutOperation>& Cuts,
		TArray<FEHBLogicalSurfaceRegion>& Regions, FName& Status, bool bIncludeVisualExpansion=true) const;
	/** Capture complete source; validate/apply together, caller owns transaction. */
	void CaptureDisplayPartitionSource(const TArray<FVector>& Outer, const TArray<FEHBFloorSlabHole>& Holes, const TArray<FEHBCutOperation>& Cuts, FEHBSlabDisplayPartition& Partition) const;
	bool SetPartitionedSlabState(const TArray<FVector>& Outer, const TArray<FEHBFloorSlabHole>& Holes, const TArray<FEHBCutOperation>& Cuts, const FEHBSlabDisplayPartition& Partition);
	bool ValidatePartitionedSlabState(const TArray<FVector>& Outer, const TArray<FEHBFloorSlabHole>& Holes, const TArray<FEHBCutOperation>& Cuts, const FEHBSlabDisplayPartition& Partition) const;
	UPROPERTY(VisibleAnywhere,Category="Surface") FEHBSlabDisplayPartition DisplayPartition;

	UFUNCTION(BlueprintCallable, Category = "Floor Slab|Cutting")
	bool AddSquarePreviewCutterInEditor();

	UFUNCTION(BlueprintCallable, Category = "Floor Slab|Cutting")
	bool AddCirclePreviewCutterInEditor();

	UFUNCTION(BlueprintCallable, Category = "Floor Slab|Cutting")
	bool CommitPreviewCutters();

	bool InsertCornerOnEdge(int32 FirstPointIndex, int32 SecondPointIndex);
	bool InsertCornerOnEdge(int32 LoopIndex, int32 FirstPointIndex, int32 SecondPointIndex);
	bool RemoveCorner(int32 PointIndex);
	bool RemoveCorner(int32 LoopIndex, int32 PointIndex);
	bool UpdateCornerWorldLocation(int32 PointIndex, const FVector& WorldLocation);
	bool UpdateCornerWorldLocation(int32 LoopIndex, int32 PointIndex, const FVector& WorldLocation);
	bool UpdateCornerDistanceToAdjacentPoint(int32 LoopIndex, int32 PointIndex, bool bPreviousPoint, float NewDistance);
	bool OffsetEdgeWorldLocation(int32 FirstPointIndex, int32 SecondPointIndex, const FVector& WorldDelta);
	bool OffsetEdgeWorldLocation(int32 LoopIndex, int32 FirstPointIndex, int32 SecondPointIndex, const FVector& WorldDelta);
	bool UpdateCutterWorldTransform(int32 CutterIndex, const FTransform& WorldTransform);
	bool RemovePreviewCutter(int32 CutterIndex);
	static bool IsCutOperationLoopIndex(int32 LoopIndex);
	static int32 MakeCutOperationLoopIndex(int32 CutOperationIndex);
	static int32 GetCutOperationIndexFromLoopIndex(int32 LoopIndex);
	int32 GetEditableCutOperationCount() const;
	bool GetEditableCutOperationLoop(int32 CutOperationIndex, TArray<FVector>& OutLocalLoop) const;
	const TArray<FVector>* GetEditableLoop(int32 LoopIndex) const;
	bool GetEditableLoopCopy(int32 LoopIndex, TArray<FVector>& OutLocalLoop) const;

	TArray<FVector> BuildCutterLocalPolygon(const FEHBFloorSlabCutterData& CutterData) const;
	TArray<TArray<FVector>> BuildPreviewHolePolygons() const;
	/** All logical cut regions, excluding display expansion. Read-only; retains split islands. */
	bool BuildLogicalTopRegions(FEHBPolygonClipResult& OutClipResult) const { return BuildCutGeometry(OutClipResult, true); }
 // Full display-domain query. The legacy single-loop query refuses multiple
 // regions rather than silently returning only the largest fragment.
 bool BuildEffectiveDisplayRegions(TArray<FEHBPlanarSurfaceRegion>& OutRegions,bool bIncludeVisualExpansion=true,bool bIncludePreviewCutters=true) const;
	bool BuildEffectiveOuterPolygon(TArray<FVector>& OutLocalPolygon, bool bIncludeVisualExpansion = true, bool bIncludePreviewCutters = true) const;
 bool BuildEffectiveOuterWorldPolygons(TArray<TArray<FVector>>& OutWorldPolygons,bool bIncludeVisualExpansion=true,bool bIncludePreviewCutters=true) const;
	bool BuildEffectiveOuterWorldPolygon(TArray<FVector>& OutWorldPolygon, bool bIncludeVisualExpansion = true, bool bIncludePreviewCutters = true) const;
	/** Read-only candidate validation; never edits author data or generated components. */
	bool ValidateCutOperations(const TArray<FEHBCutOperation>& CandidateCuts,bool bDiscardPreviewCutters=false) const;
	bool AddCutOperation(FEHBCutOperation Operation);
	bool RemoveCutOperation(const FGuid& OperationGuid);
	bool UpdateCutOperation(const FEHBCutOperation& Operation);
	FEHBCutOperation* FindCutOperation(const FGuid& OperationGuid);
	const FEHBCutOperation* FindCutOperation(const FGuid& OperationGuid) const;
	bool RemoveCutPolygonPoint(const FGuid& OperationGuid, const FGuid& PointGuid);
	bool UpdateCutPolygonPoint(const FGuid& OperationGuid, const FGuid& PointGuid, const FVector& NewLocalPosition);
	bool SnapFoundationBottomToGround(float TraceDistance = 100000.0f);
	bool SnapSideToAdjacentFloorSlab(float MaxAngleDegrees = 15.0f);
	bool SnapCornerToAdjacentAxesWorldLocation(int32 LoopIndex, int32 PointIndex, FVector& InOutWorldLocation, float MaxDistance = 45.0f) const;
	bool SnapOuterCornerHandleWorldLocation(int32 PointIndex, FVector& InOutWorldLocation, float MaxDistance = 45.0f) const;
	bool SnapOuterEdgeHandleWorldDelta(int32 FirstPointIndex, int32 SecondPointIndex, FVector& InOutWorldDelta, float MaxAngleDegrees = 15.0f, float MaxDistance = 45.0f) const;
	float GetTopZ() const;
	float GetBottomZ() const;

private:
	UPROPERTY()
	FGuid RecordedOutlineSignature;
	FGuid BuildOutlineSignature() const;
	TArray<FVector>* GetMutableEditableLoop(int32 LoopIndex);
	bool SetEditedOutlineLoop(int32 LoopIndex,const TArray<FVector>& Loop);
	FEHBCutOperation* GetEditableCutOperationByLoopIndex(int32 LoopIndex);
	const FEHBCutOperation* GetEditableCutOperationByLoopIndex(int32 LoopIndex) const;
	bool EnsureCutOperationEditablePolygon(FEHBCutOperation& Operation);
	bool ValidateCutStateSources(const TArray<FEHBCutOperation>& Cuts,const TArray<FEHBFloorSlabCutterData>& Preview) const;
	bool SetCutState(const TArray<FEHBCutOperation>& CandidateCuts,const TArray<FEHBFloorSlabCutterData>& CandidatePreview);
	bool BuildPreparedSlabMesh(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes,
		bool bIncludePreviewCutters, bool bValidateCandidate, FEHBSurfaceMeshBuildResult& OutMesh,
		TArray<FVector>& OutRenderPolygon, TArray<TArray<FVector>>& OutRenderHoles, const FEHBSlabDisplayPartition* OverrideDisplay = nullptr, TArray<FEHBPlanarSurfaceRegion>* OutPreparedRegions = nullptr, const TArray<FEHBCutOperation>* OverrideCuts = nullptr, const TArray<FEHBFloorSlabCutterData>* OverridePreview = nullptr, bool bIncludeVisualExpansion=true) const;
	/** Shared display policy for prepared meshes and read-only effective outlines. */
	bool BuildDisplayRegions(const FEHBPolygonClipResult& ClipResult, bool bIncludeVisualExpansion,
		TArray<FEHBPlanarSurfaceRegion>& OutRegions, const FEHBSlabDisplayPartition* OverrideDisplay = nullptr) const;
	bool CheckDisplayPartition(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes, const FEHBSlabDisplayPartition& Partition, const TArray<FEHBCutOperation>& Cuts, const TArray<FEHBFloorSlabCutterData>& Preview) const;
	void ApplyPreparedSlabMesh(const FEHBSurfaceMeshBuildResult& Mesh, const TArray<FEHBPlanarSurfaceRegion>& Regions);
	bool BuildCutGeometry(FEHBPolygonClipResult& OutClipResult, bool bIncludePreviewCutters,
		const TArray<FVector>* OverridePolygon = nullptr, const TArray<FEHBFloorSlabHole>* OverrideHoles = nullptr, const TArray<FEHBCutOperation>* OverrideCuts = nullptr, const TArray<FEHBFloorSlabCutterData>* OverridePreview = nullptr) const;
	bool ResolveCutOperationToLocalPolygon(const FEHBCutOperation& Operation, TArray<FVector>& OutLocalPolygon) const;
 bool ResolveSurfaceOpeningCuts(const TArray<FEHBCutOperation>& Cuts,const TArray<FVector>& Outer,
  const TArray<FEHBFloorSlabHole>& Holes,TArray<FEHBCutOperation>& Resolved) const;
	void GatherHorizontalCutPolygons(TArray<TArray<FVector>>& OutCutPolygons, bool bIncludePreviewCutters, const TArray<FEHBFloorSlabHole>* OverrideHoles = nullptr, const TArray<FEHBCutOperation>* OverrideCuts = nullptr, const TArray<FEHBFloorSlabCutterData>* OverridePreview = nullptr) const;
	FEHBCutOperation MakePrimitiveCutOperation(EEHBCutPrimitiveShape Shape) const;
	void AppendTopOrBottomMesh(bool bTop, const TArray<FVector>& TopPolygon, const TArray<TArray<FVector>>& HolePolygons, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;
	void AppendSideLoop(const TArray<FVector>& Loop, bool bInnerSide, bool bLoopCounterClockwise, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;
	void AppendFoundationSideLoop(const TArray<FVector>& Loop, bool bInnerSide, bool bLoopCounterClockwise, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const;
	float ResolveGroundLocalZAt(const FVector& LocalTopPoint) const;
	bool TraceGroundWorldZAt(const FVector& WorldPoint, float TraceDistance, float& OutGroundZ) const;
	void GetApproxWorldVerticalRange(float& OutMinZ, float& OutMaxZ) const;
	void ApplyMesh(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs);
	void ApplyMesh(const FEHBSurfaceMeshBuildResult& BuildResult);
	bool AddPreviewCutterWithShape(EEHBFloorSlabCutterShape Shape);
};
