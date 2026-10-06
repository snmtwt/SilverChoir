#pragma once

#include "Actors/EHBDefaultRoofBase.h"
#include "EHBGableRoof.generated.h"

class UEHBGeneratedMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;

UCLASS(BlueprintType, Blueprintable)
class EASYHOUSEBUILDER_API AEHBGableRoof : public AEHBDefaultRoofBase
{
	GENERATED_BODY()

public:
	AEHBGableRoof();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof|Gable|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> RidgeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof|Gable|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> EaveMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof|Gable|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> GableRakeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof|Gable|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> GableEndWallMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Length", ToolTip = "Roof-local length before eave overhang. For AI and tools, derive this from the enclosed wall footprint, not outdoor platforms.", ClampMin = "1.0", UIMin = "100.0"))
	float Length = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Width", ToolTip = "Roof-local width before eave overhang. Ridge direction is controlled by Axis Mode.", ClampMin = "1.0", UIMin = "100.0"))
	float Width = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Pitch Degrees", ToolTip = "Slope angle of the roof planes. Normal residential roofs usually stay around 18-35 degrees.", ClampMin = "1.0", ClampMax = "89.0", UIMin = "5.0", UIMax = "60.0"))
	float PitchDegrees = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Body Thickness", ToolTip = "Thickness of the unified roof body used for rendering and cutting.", ClampMin = "0.1", UIMin = "5.0"))
	float Thickness = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Eave Offset", ToolTip = "Overhang distance added around the covered footprint. Keep it modest for AI-generated roofs.", ClampMin = "0.0", UIMin = "0.0"))
	float EaveOffset = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Ridge Offset Ratio", ToolTip = "Moves the ridge across the width. 0 keeps the ridge centered; positive and negative values create asymmetric gables.", ClampMin = "-0.45", ClampMax = "0.45"))
	float RidgeOffsetRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable", meta = (DisplayName = "Axis Mode", ToolTip = "RidgeAlongX means the ridge follows roof-local X and Length projects along X; RidgeAlongY swaps the projection."))
	EEHBRoofAxisMode AxisMode = EEHBRoofAxisMode::RidgeAlongX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Generate Ridge", AdvancedDisplay))
	bool bGenerateRidge = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Generate Eaves", AdvancedDisplay))
	bool bGenerateEaves = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Generate Gable Rakes", AdvancedDisplay))
	bool bGenerateGableRakes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Generate Gable End Walls", AdvancedDisplay))
	bool bGenerateGableEndWalls = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Ridge Width", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateRidge", AdvancedDisplay))
	float RidgeWidth = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Ridge Height", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateRidge", AdvancedDisplay))
	float RidgeHeight = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Eave Width", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateEaves", AdvancedDisplay))
	float EaveWidth = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Eave Height", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateEaves", AdvancedDisplay))
	float EaveHeight = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Gable Rake Width", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateGableRakes", AdvancedDisplay))
	float GableRakeWidth = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Gable Rake Height", ClampMin = "0.1", UIMin = "1.0", EditCondition = "bGenerateGableRakes", AdvancedDisplay))
	float GableRakeHeight = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Advanced", meta = (DisplayName = "Gable End Wall Boundary Inset", ToolTip = "Distance from the roof end boundary to the outside face of generated gable end walls. Positive values move the wall inward; 0 keeps the wall face on the roof boundary.", ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "bGenerateGableEndWalls", AdvancedDisplay))
	float GableEndWallBoundaryInset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Material", meta = (DisplayName = "Accessory Fallback Material", ToolTip = "Fallback material for ridge, eave and gable rake parts when their specific material is not set."))
	TObjectPtr<UMaterialInterface> AccessoryMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Material", meta = (DisplayName = "Ridge Material", ToolTip = "Material used by the ridge component only. Dragging a material onto the ridge updates this slot."))
	TObjectPtr<UMaterialInterface> RidgeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Material", meta = (DisplayName = "Eave Material", ToolTip = "Material used by eave components only."))
	TObjectPtr<UMaterialInterface> EaveMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Material", meta = (DisplayName = "Gable Rake Material", ToolTip = "Material used by the sloped gable edge/rake component only."))
	TObjectPtr<UMaterialInterface> GableRakeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Gable|Material", meta = (DisplayName = "Gable End Wall Material", ToolTip = "Material used by the triangular gable end wall component only."))
	TObjectPtr<UMaterialInterface> GableEndWallMaterial;

	virtual void GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const override;
	virtual bool BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const override;
	virtual bool GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const override;
	virtual bool GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const override;
	virtual bool ApplyMaterialToRoofComponent(
		UPrimitiveComponent* HitComponent,
		UMaterialInterface* Material,
		bool bApplyAllRoofParts = false) override;
	virtual bool RebuildRoofMesh() override;

protected:
	virtual bool BuildRawRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const override;
	virtual void RebuildRoofAccessories(const FEHBRoofUnifiedMeshData& CutBodyMesh) override;

	bool BuildUncutUnifiedRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const;
};
