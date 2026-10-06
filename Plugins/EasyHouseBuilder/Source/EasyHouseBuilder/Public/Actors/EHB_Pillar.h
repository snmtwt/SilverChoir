// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_Wall.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBPillarMeshData.h"
#include "EHB_Pillar.generated.h"

struct FEHBWallJunctionMesh;

class UMaterialInterface;
class UEHBGeneratedMeshComponent;
class UStaticMesh;

/** Value-only result shared by manual snapping and command previews. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPillarSlabSnapPreview
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Snap") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "Snap") FGuid SlabGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Snap") FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Snap") FRotator WorldRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Snap") bool bCorner = false;
	UPROPERTY(BlueprintReadOnly, Category = "Snap") int32 CandidateSlabCount = 0;
};

UENUM(BlueprintType, meta = (DisplayName = "Pillar Shape Type"))
enum class EEHBPillarShapeType : uint8
{
	Polygon UMETA(DisplayName = "多边形柱子", ToolTip = "根据基础尺寸和已连接墙体方向动态生成多边形柱体。"),
	Cylinder UMETA(DisplayName = "Cylinder", ToolTip = "Reserved for cylinder pillar generation."),
	StaticMesh UMETA(DisplayName = "Static Mesh", ToolTip = "Uses sampled static-mesh data as the pillar body.")
};

USTRUCT(BlueprintType, meta = (DisplayName = "Pillar Connected Surface Override"))
struct EASYHOUSEBUILDER_API FEHBPillarConnectedSurfaceOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Connected Wall Guid"))
	FGuid WallGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Surface Style"))
	FEHBWallSurfaceStyle SurfaceStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Use Per-Side Surface Styles"))
	bool bUsePerSideSurfaceStyles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Left Wall Side Surface Style"))
	FEHBWallSurfaceStyle LeftWallSideSurfaceStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Right Wall Side Surface Style"))
	FEHBWallSurfaceStyle RightWallSideSurfaceStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Wall End Cap Surface Style"))
	FEHBWallSurfaceStyle WallEndCapSurfaceStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Apply Left Wall Side"))
	bool bApplyLeftWallSide = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Apply Right Wall Side"))
	bool bApplyRightWallSide = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Apply Wall End Cap"))
	bool bApplyWallEndCap = true;
};

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_Pillar", ToolTip = "Standalone pillar actor managed by the parametric building toolset."))
class EASYHOUSEBUILDER_API AEHB_Pillar : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pillar|Component", meta = (DisplayName = "Pillar Mesh Component"))
	TObjectPtr<UEHBGeneratedMeshComponent> PillarMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Basic", meta = (DisplayName = "Shape Type"))
	EEHBPillarShapeType ShapeType = EEHBPillarShapeType::Polygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Size", meta = (DisplayName = "Height", ClampMin = "1.0", Units = "cm"))
	float Height = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|多边形", meta = (DisplayName = "宽度", ClampMin = "1.0", Units = "cm"))
	float Width = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|多边形", meta = (DisplayName = "深度", ClampMin = "1.0", Units = "cm"))
	float Depth = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Cylinder", meta = (DisplayName = "Radius", ClampMin = "1.0", Units = "cm"))
	float Radius = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Cylinder", meta = (DisplayName = "Cylinder Side Count", ClampMin = "3", ClampMax = "128"))
	int32 CylinderSideCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Static Mesh", meta = (DisplayName = "Source Static Mesh"))
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Static Mesh", meta = (DisplayName = "Sampled Pillar Row"))
	FDataTableRowHandle SampledPillarRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Material", meta = (DisplayName = "Override Material"))
	TSoftObjectPtr<UMaterialInterface> OverrideMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Inherit Connected Wall Surface Samples", ToolTip = "When enabled, polygon pillar faces that line up with sampled wall surfaces reuse the same wall surface sample and UV phase unless an explicit pillar surface override is present."))
	bool bInheritConnectedWallSurfaceSamples = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Generate Connected End Caps", ToolTip = "When disabled, polygon pillar cap side faces connected to walls are omitted."))
	bool bGenerateLinkedWallFaces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Surface", meta = (DisplayName = "Connected Surface Overrides", ToolTip = "Optional per-connected-wall surface samples. These override automatic inheritance from the connected wall."))
	TArray<FEHBPillarConnectedSurfaceOverride> ConnectedSurfaceOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Editor Snap", meta = (DisplayName = "Enable Rotation Snap"))
	bool bEnableEditorRotationSnap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pillar|Editor Snap", meta = (DisplayName = "Rotation Snap Angle Threshold", ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float RotationSnapAngleThreshold = 10.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Pillar|Connection", meta = (DisplayName = "Connected Wall Guids"))
	TArray<FGuid> ConnectedWallGuids;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Pillar|Connection", meta = (DisplayName = "Connected Pillar Guids"))
	TArray<FGuid> ConnectedPillarGuids;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "多边形柱子顶点"))
	TArray<FVector> PolygonPillarVertices;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "多边形柱子三角面"))
	TArray<int32> PolygonPillarTriangles;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "多边形柱子法线"))
	TArray<FVector> PolygonPillarNormals;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "多边形柱子 UV"))
	TArray<FVector2D> PolygonPillarUVs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "Polygon Pillar Triangle Material Indices"))
	TArray<int32> PolygonPillarTriangleMaterialIndices;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "Polygon Pillar Source Materials"))
	TArray<TSoftObjectPtr<UMaterialInterface>> PolygonPillarSourceMaterials;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pillar|Generated", meta = (DisplayName = "多边形柱子轮廓"))
	TArray<FVector> PolygonPillarFootprint;

public:
	AEHB_Pillar();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished) override;
	virtual void OnElementActorDeleted_Implementation() override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void SynchronizePlannedEditorMove() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Pillar|多边形", meta = (DisplayName = "配置为多边形柱子"))
	void ConfigureAsPolygonPillar(float InHeight, float InWidth, float InDepth, const FTransform& InLocalTransform, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Editor Snap", meta = (DisplayName = "Resolve Pillar Rotation Snap Transform"))
	bool ResolvePillarRotationSnapTransform(const FTransform& DesiredLocalTransform, FTransform& ResolvedLocalTransform) const;

	UFUNCTION(BlueprintCallable, Category = "Pillar|Editor Snap", meta = (DisplayName = "Snap To Adjacent Floor Slab Corner"))
	bool SnapToAdjacentFloorSlabCorner(float MaxDistance = 30.0f);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Editor Snap", meta = (DisplayName = "Snap To Adjacent Floor Slab Boundary"))
	bool SnapToAdjacentFloorSlabBoundary(float MaxDistance = 30.0f, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Static Mesh", meta = (DisplayName = "Configure From Sampled Pillar Row"))
	bool ConfigureFromSampledPillarRow(UDataTable* InTable, FName InRowName, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Surface", meta = (DisplayName = "Set Connected Wall Surface Override"))
	bool SetConnectedWallSurfaceOverride(FGuid InWallGuid, const FEHBWallSurfaceStyle& InSurfaceStyle, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Surface", meta = (DisplayName = "Set Connected Wall Side Surface Override"))
	bool SetConnectedWallSurfaceOverrideForSide(FGuid InWallGuid, const FEHBWallSurfaceStyle& InSurfaceStyle, bool bLeftSide, bool bIncludeEndCap = true, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Surface", meta = (DisplayName = "Clear Connected Wall Surface Override"))
	bool ClearConnectedWallSurfaceOverride(FGuid InWallGuid, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Pillar|Connection", meta = (DisplayName = "Resolve Wall Connection Point Toward"))
	bool ResolveWallConnectionPointToward(const FVector& TargetLocalLocation, float WallThickness, FVector& OutLocalLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Pillar|Connection", meta = (DisplayName = "Resolve Wall Connection Face Toward"))
	bool ResolveWallConnectionFaceToward(const FVector& TargetLocalLocation, float WallThickness, FVector& OutLeftLocalLocation, FVector& OutRightLocalLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Pillar|Generated", meta = (DisplayName = "Rebuild Pillar Mesh"))
	void RebuildPillarMesh();

	UFUNCTION(BlueprintCallable, Category = "Pillar|多边形", meta = (DisplayName = "重建多边形柱子网格数据"))
	void RebuildPolygonPillarMeshData();

	UFUNCTION(BlueprintCallable, Category = "Pillar|Generated", meta = (DisplayName = "Apply Pillar Mesh To Component"))
	void ApplyPillarMeshToComponent();

	UFUNCTION(BlueprintCallable, Category = "Pillar|多边形", meta = (DisplayName = "获取多边形柱子网格数据"))
	void GetPolygonPillarMeshData(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs) const;

	/** Fresh polygon structure, excluding sampled side ornament and cached render buffers. */
	bool BuildStructuralContactMesh(FEHBWallJunctionMesh& Out) const;

	UFUNCTION(BlueprintCallable, Category = "Pillar|多边形", meta = (DisplayName = "获取柱子本地轮廓点"))
	void GetPillarFootprintLocalPoints(TArray<FVector>& OutLocalPoints) const;

	/** Resolves a candidate without moving or rebuilding the pillar. Distance is in world cm. */
	UFUNCTION(BlueprintPure, Category = "Pillar|Snap")
	FEHBPillarSlabSnapPreview PreviewFloorSlabBoundarySnap(FVector DesiredWorldLocation, FRotator DesiredWorldRotation, float MaxDistance = 30.0f) const;

private:
	bool bIsTrackingEditorRotationDrag = false;
	FTransform EditorRotationDragStartLocalTransform = FTransform::Identity;
	float EditorRotationDragUnsnappedYaw = 0.0f;
	float EditorRotationDragLastAppliedYaw = 0.0f;
	bool bIsTrackingEditorTranslationDrag = false;
	FTransform EditorTranslationDragStartLocalTransform = FTransform::Identity;
	FVector EditorTranslationDragUnsnappedLocalLocation = FVector::ZeroVector;
	FVector EditorTranslationDragLastAppliedLocalLocation = FVector::ZeroVector;

	bool ResolvePolygonPillarRotationSnapTransform(const FTransform& DesiredLocalTransform, FTransform& ResolvedLocalTransform) const;
	bool FindBestConnectedWallSideSnapYaw(float DesiredYaw, float& OutSnappedYaw, float& OutBestDelta) const;
	bool ResolveFloorSlabBoundarySnap(
		const FVector& DesiredWorldLocation,
		const FRotator& DesiredWorldRotation,
		float MaxDistance,
		FVector& OutWorldLocation,
		FRotator& OutWorldRotation) const;

	bool BuildSampledPillarMeshData(
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<int32>& OutTriangleMaterialIndices,
		TArray<TSoftObjectPtr<UMaterialInterface>>& OutMaterials) const;

	bool BuildPolygonPillarFootprint(TArray<FVector>& OutLocalFootprint) const;
	void UpdateConnectedWallConnectionFaces();

	void ApplyPillarMeshToComponent(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs,
		const TArray<int32>& TriangleMaterialIndices,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& SourceMaterials,
		bool bUseOverrideMaterialForAllSections = true);
};
