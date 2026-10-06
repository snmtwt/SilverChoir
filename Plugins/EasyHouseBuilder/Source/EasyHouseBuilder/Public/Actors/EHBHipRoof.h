#pragma once

#include "Actors/EHBGableRoof.h"
#include "EHBHipRoof.generated.h"

UENUM(BlueprintType)
enum class EEHBHalfHipRoofKeepSide : uint8
{
	PositiveAlongAxis UMETA(DisplayName = "Positive Cross Axis"),
	NegativeAlongAxis UMETA(DisplayName = "Negative Cross Axis"),
};

UCLASS(BlueprintType, Blueprintable, DisplayName = "EHB Hip Roof")
class EASYHOUSEBUILDER_API AEHBHipRoof : public AEHBGableRoof
{
	GENERATED_BODY()

public:
	AEHBHipRoof();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Hip", meta = (DisplayName = "Half Hip Roof", ToolTip = "Keeps one half of the hip roof by cutting it through the center with a vertical plane parallel to the roof ridge axis."))
	bool bHalfHipRoof = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof|Hip", meta = (DisplayName = "Half Hip Keep Cross Side", ToolTip = "Which side across the roof width is kept when Half Hip Roof is enabled.", EditCondition = "bHalfHipRoof"))
	EEHBHalfHipRoofKeepSide HalfHipKeepSide = EEHBHalfHipRoofKeepSide::PositiveAlongAxis;

	virtual bool BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const override;
	virtual bool GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const override;
	virtual bool GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const override;
	virtual bool RebuildRoofMesh() override;

protected:
	virtual bool BuildRawRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const override;

	bool BuildUncutUnifiedHipRoofMesh(FEHBRoofUnifiedMeshData& OutMesh) const;
};
