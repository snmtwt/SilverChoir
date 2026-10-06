#pragma once

#include "Actors/EHBElementActorBase.h"
#include "Cutting/EHBMeshEnvelopeBuilder.h"
#include "Roof/EHBRoofMeshTypes.h"
#include "EHBRoofBase.generated.h"

class UEHBGeneratedMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;

struct FEHBResolvedDefaultRoofMaterials
{
	UMaterialInterface* Slope = nullptr;
	UMaterialInterface* SideWall = nullptr;
	UMaterialInterface* Ridge = nullptr;
	UMaterialInterface* Eave = nullptr;
	UMaterialInterface* DiagonalRidge = nullptr;
};

namespace UE::Geometry
{
	class FDynamicMesh3;
}

UCLASS(Abstract, BlueprintType, Blueprintable)
class EASYHOUSEBUILDER_API AEHBRoofBase : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	AEHBRoofBase();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> RoofBodyMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Material", meta = (DisplayName = "Roof Body Material", ToolTip = "Material used by the main roof surface/body component. Trim and wall parts can use separate semantic material slots in subclasses."))
	TObjectPtr<UMaterialInterface> RoofBodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Cut Colliding Elements", ToolTip = "When enabled, this roof cuts itself against overlapping cuttable elements after editor movement finishes. Other roofs are cut only when their own option is enabled."))
	bool bCutCollidingElements = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Remove Disconnected Cut Pieces", ToolTip = "After cutting, remove disconnected mesh pieces so the roof keeps a single intended piece."))
	bool bRemoveDisconnectedCutPieces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Keep Cut-Away Side", ToolTip = "When disconnected pieces are removed, keep the opposite cut-away side instead of the side closest to this roof's kept region heuristic.", EditCondition = "bRemoveDisconnectedCutPieces"))
	bool bKeepCutAwayDisconnectedPieces = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Use Exact Source Mesh Cutters", ToolTip = "Use the source element's original uncut aggregate mesh as the cutter before falling back to controlled envelopes."))
	bool bUseExactSourceMeshCutters = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Use Controlled Envelope Cutters", ToolTip = "Build controlled envelope cutters from source aggregate meshes. This is useful for roof-to-roof trimming and future dormer-style elements."))
	bool bUseControlledEnvelopeCutters = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting", meta = (DisplayName = "Envelope Cut Options", EditCondition = "bUseControlledEnvelopeCutters"))
	FEHBMeshEnvelopeBuildOptions EnvelopeCutOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Use Wall Footprint Cutters", ToolTip = "When a roof is cut by ordinary wall elements, group connected walls and convert their top-view footprint into oriented quad cutter volumes. This supports dormer-style roof openings without requiring a special dormer object."))
	bool bUseWallFootprintCutters = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Minimum Wall Count", ClampMin = "1", UIMin = "1", EditCondition = "bUseWallFootprintCutters", ToolTip = "Minimum connected wall count required before a wall group becomes a roof footprint cutter. Use 2 or 3 to avoid ordinary single walls cutting the roof."))
	int32 MinWallFootprintGroupWallCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Wall Group Endpoint Tolerance", ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "bUseWallFootprintCutters"))
	float WallFootprintGroupEndpointTolerance = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Footprint Cut Bias", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-20.0", UIMax = "20.0", Units = "cm", EditCondition = "bUseWallFootprintCutters", ToolTip = "Signed footprint offset for wall-driven roof openings. Negative values make the cut slightly smaller so walls cover the edge; positive values make the opening larger."))
	float WallFootprintPadding = -2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Maximum Footprint Dimension", ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "bUseWallFootprintCutters", ToolTip = "Optional largest allowed width/depth for a wall footprint cutter. 0 disables the size limit, which is the default because large roof openings can be valid."))
	float WallFootprintMaxDimension = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Cutting|Wall Footprint", meta = (DisplayName = "Minimum Footprint Area", ClampMin = "0.0", UIMin = "0.0", EditCondition = "bUseWallFootprintCutters"))
	float WallFootprintMinArea = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (DisplayName = "Show Cut Debug Visualization"))
	bool bShowCutDebugVisualization = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (EditCondition = "bShowCutDebugVisualization"))
	bool bDebugDrawRawRoofBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (EditCondition = "bShowCutDebugVisualization"))
	bool bDebugDrawSourceBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (EditCondition = "bShowCutDebugVisualization"))
	bool bDebugDrawCutterBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (EditCondition = "bShowCutDebugVisualization"))
	bool bDebugDrawResultBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (ClampMin = "0.1", UIMin = "1.0", EditCondition = "bShowCutDebugVisualization"))
	float CutDebugDrawDuration = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (ClampMin = "0.1", UIMin = "1.0", EditCondition = "bShowCutDebugVisualization"))
	float CutDebugDrawThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Debug", meta = (EditCondition = "bShowCutDebugVisualization"))
	bool bDebugDrawLabels = true;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const override;
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished) override;

	UFUNCTION(BlueprintCallable, Category = "Roof|Material")
	virtual bool ApplyMaterialToRoofComponent(
		UPrimitiveComponent* HitComponent,
		UMaterialInterface* Material,
		bool bApplyAllRoofParts = false);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Roof")
	virtual bool RebuildRoofMesh();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Roof|Cutting")
	virtual bool RefreshAutoCollisionCutOperations();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Roof|Debug")
	virtual bool RedrawRoofCutDebug();

	UFUNCTION(BlueprintCallable, Category = "Roof")
	virtual bool GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const;

	UFUNCTION(BlueprintCallable, Category = "Roof")
	virtual bool GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const;

protected:
	static FName AutoCollisionCutTag();

	virtual bool BuildRawRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const PURE_VIRTUAL(AEHBRoofBase::BuildRawRoofMesh, return false;);
	virtual void RebuildRoofAccessories(const FEHBRoofUnifiedMeshData& CutBodyMesh);

	bool SubmitRoofBodyMesh(const FEHBRoofUnifiedMeshData& MeshData);
	bool ApplySourceMeshCuts(FEHBRoofUnifiedMeshData& InOutMesh, bool bFilterDisconnectedPieces = true) const;
	bool ConvertUnifiedMeshToDynamicMesh(const FEHBRoofUnifiedMeshData& MeshData, UE::Geometry::FDynamicMesh3& OutMesh) const;
	bool ConvertDynamicMeshToUnifiedMesh(const UE::Geometry::FDynamicMesh3& DynamicMesh, FEHBRoofUnifiedMeshData& OutMesh) const;
	bool FilterDisconnectedCutPieces(UE::Geometry::FDynamicMesh3& Mesh) const;
	bool IsRoofCutDebugEnabled() const;
	void DrawRoofCutDebugBounds(const FBox& LocalBounds, const FColor& Color, const FString& Label) const;
	void DrawRoofCutDebugMeshBounds(const FEHBRoofUnifiedMeshData& MeshData, const FColor& Color, const FString& Label) const;
	void DrawRoofCutDebugDynamicBounds(const UE::Geometry::FDynamicMesh3& Mesh, const FColor& Color, const FString& Label) const;

	bool ResolveCutSourceElement(const FEHBCutOperation& Operation, AEHBElementActorBase*& OutElement) const;
	FEHBResolvedDefaultRoofMaterials ResolveConfiguredDefaultRoofMaterials() const;
	void CollectAutoCutCandidates(TArray<AEHBElementActorBase*>& OutCandidates) const;
	bool DoesOverlapElementForAutoCut(const AEHBElementActorBase* OtherElement) const;
	bool UpsertAutoCollisionCutOperation(AEHBElementActorBase* TargetElement, AEHBElementActorBase* SourceElement) const;
	bool RemoveAutoCollisionCutOperations(AEHBElementActorBase* TargetElement, const FGuid& SourceGuid) const;
	bool IsAutoCollisionCutOperation(const FEHBCutOperation& Operation, const FGuid* OptionalSourceGuid = nullptr) const;
	bool RebuildCuttableRoofTarget(AEHBElementActorBase* TargetElement) const;
};
