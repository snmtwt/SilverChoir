#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSOCoverTypes.h"
#include "CSOCoverVolume.generated.h"

class UBoxComponent;
class USmartObjectDefinition;

/** Place over loaded, built navigation geometry and bake in the editor. No actor per cover is required. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Cover Smart Object Volume"))
class COVERSMARTOBJECTS_API ACSOCoverVolume : public AActor
{
    GENERATED_BODY()

public:
    ACSOCoverVolume();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover") TObjectPtr<UBoxComponent> GenerationBounds;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover") FCSOAgentProfile AgentProfile;
    /** Optional custom behavior definition. Leave empty to use the subsystem's default cover definition. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover") TObjectPtr<USmartObjectDefinition> SmartObjectDefinition;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover|Bake") TArray<FCSOBakedCover> BakedCovers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="5", ClampMax="500")) float SampleSpacing = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="5", ClampMax="500")) float MinCoverSpacing = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="10", ClampMax="1000")) float WallSearchDistance = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="10", ClampMax="1000")) float PeekProbeDistance = 150.f;
    /** Maximum displacement from the covered eye to a side firing position, including the margin beyond the wall edge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="10", ClampMax="500")) float MaxSidePeekDistance = 100.f;
    /** Resolution when finding the end of the occluding surface; the final margin is conservative by up to this amount. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="1", ClampMax="25")) float SideEdgeSearchStep = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="0", ClampMax="89")) float MaxFloorSlopeDegrees = 45.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation") TEnumAsByte<ECollisionChannel> GeometryTraceChannel = ECC_Visibility;
    /** Character movement collision is independent of whether an obstacle blocks sight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation") TEnumAsByte<ECollisionChannel> MovementTraceChannel = ECC_Pawn;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation") bool bTraceComplex = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="1", ClampMax="1000000")) int32 MaxSamples = 100000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Generation", meta=(ClampMin="1", ClampMax="100000")) int32 MaxCoverPoints = 10000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Debug") bool bDrawDebug = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Debug", meta=(ClampMin="1", ClampMax="10000")) int32 MaxDebugPoints = 500;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover|Bake") FString LastBakeReport;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover|Bake") bool bBakeComplete = false;

    /** Runs only in an editor world. Build navigation and load the desired World Partition cells first. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Cover|Bake") void BakeCover();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Cover|Bake") void ClearBakedCover();
    UFUNCTION(BlueprintPure, Category="Cover|Bake") bool IsBakeTransformValid() const;
    UFUNCTION(BlueprintPure, Category="Cover|Bake") bool IsBakeDataValid() const;
    FCSOBakedCover GetWorldCover(const FCSOBakedCover& LocalCover) const;

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY() FCSOAgentProfile BakedProfile;
    UPROPERTY() FTransform BakeTransform = FTransform::Identity;
    UPROPERTY() FTransform BakeBoundsTransform = FTransform::Identity;
    UPROPERTY() FVector BakeBoundsExtent = FVector::ZeroVector;
    UPROPERTY() TEnumAsByte<ECollisionChannel> BakedTraceChannel = ECC_Visibility;
    UPROPERTY() TEnumAsByte<ECollisionChannel> BakedMovementTraceChannel = ECC_Pawn;
    UPROPERTY() bool bBakedTraceComplex = false;
    UPROPERTY() float BakedMaxSidePeekDistance = 100.f;
    UPROPERTY() float BakedSideEdgeSearchStep = 5.f;
    UPROPERTY() bool bHasBake = false;
};
