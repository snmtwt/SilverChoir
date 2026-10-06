#pragma once
#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "HMS_InteractionPoseChannel.generated.h"

/** Matches the remaining root displacement and turn, before ranking compatible foot poses. */
UCLASS(EditInlineNew, meta=(DisplayName="HMS Interaction Target"))
class HYBRIDMOTIONSYSTEM_API UHMS_InteractionPoseChannel : public UPoseSearchFeatureChannel
{
 GENERATED_BODY()
public:
 /** Compatibility fallback for older montages. Refresh Entry Database writes a per-montage HMS.EntryContact marker. */
 UPROPERTY(EditAnywhere, Category="Interaction", meta=(ClampMin="0"))
 float ContactEndOffset = 0.23333333f;
 UPROPERTY(EditAnywhere, Category="Interaction", meta=(ClampMin="1"))
 float PositionTolerance = 25.f;
 UPROPERTY(EditAnywhere, Category="Interaction", meta=(ClampMin="1",ClampMax="45"))
 float FacingTolerance = 25.f;
 virtual bool Finalize(UPoseSearchSchema* Schema) override;
 virtual void BuildQuery(UE::PoseSearch::FSearchContext& Context) const override;
 virtual bool IsFilterActive() const override { return true; }
 virtual bool IsFilterValid(TConstArrayView<float> Pose, TConstArrayView<float> Query, int32 PoseIdx, const UE::PoseSearch::FPoseMetadata& Metadata) const override;
#if WITH_EDITOR
 virtual void FillWeights(TArrayView<float> Weights) const override;
 virtual bool IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const override;
#endif
private:
 UPROPERTY() int8 RootIndex = 0;
};
