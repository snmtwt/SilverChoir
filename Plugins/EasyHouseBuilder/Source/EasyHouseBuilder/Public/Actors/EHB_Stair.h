// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_Railing.h"
#include "Engine/DataTable.h"
#include "EHB_Stair.generated.h"

class AEHBBuildingActorBase;
class UMaterialInterface;
class UActorComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UEHBGeneratedMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBStairData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Dimensions")
	bool bUseActualDimensions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float TreadDepth = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float StairWidth = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float StairHeight = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Default Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float DefaultTreadDepth = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Default Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float DefaultStairWidth = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Default Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float DefaultStairHeight = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation")
	bool bGenerateTreads = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation")
	bool bFillRisers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation")
	bool bFillBottomPart = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "生成锯齿侧板"))
	bool bGenerateSides = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "生成侧挡板"))
	bool bGenerateSideGuards = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	bool bGenerateRailing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	bool bGenerateLeftRailing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	bool bGenerateRightRailing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "1"))
	int32 RailingStepsPerPost = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "0.0", Units = "cm"))
	float RailingEdgeInset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (Units = "cm"))
	float RailingPostForwardOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "0.1", Units = "cm"))
	float RailingPostWidth = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "1.0", Units = "cm"))
	float RailingPostHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "1.0", Units = "cm"))
	float RailingRailHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "0.1", Units = "cm"))
	float RailingRailThickness = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (ClampMin = "1.0", Units = "cm"))
	float RailingMaxRailSegmentLength = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	TSoftObjectPtr<UStaticMesh> RailingPostMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	TSoftObjectPtr<UStaticMesh> RailingRailMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing", meta = (DisplayName = "扶手采样项"))
	FDataTableRowHandle SampledRailingRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	TSoftObjectPtr<UMaterialInterface> RailingPostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	TSoftObjectPtr<UMaterialInterface> RailingRailMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Railing")
	TArray<FEHBRailingPostMeshOverride> RailingPostMeshOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (ClampMin = "1.0", Units = "cm"))
	float MinStepHeight = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxStepHeight = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (ClampMin = "0.1", Units = "cm"))
	float PanelThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (ClampMin = "0.0", Units = "cm"))
	float NosingLength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (ClampMin = "0.0", Units = "cm"))
	float SideProtruding = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "锯齿侧板厚度", ClampMin = "0.1", Units = "cm"))
	float SideThickness = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "锯齿侧板高度", ClampMin = "1.0", Units = "cm"))
	float SideBoardHeight = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "Side Board Top Offset", ClampMin = "0.0", Units = "cm"))
	float SideBoardTopOffset = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "侧挡板厚度", ClampMin = "0.1", Units = "cm"))
	float SideGuardThickness = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "侧挡板高度", ClampMin = "1.0", Units = "cm"))
	float SideGuardHeight = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Generation", meta = (DisplayName = "侧挡板上沿偏移", Units = "cm"))
	float SideGuardTopOffset = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Bottom Control", meta = (DisplayName = "底部台阶位置", Units = "cm"))
	FVector2D BottomStepLocation = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Bottom Control", meta = (DisplayName = "底部台阶方向偏移", Units = "deg"))
	float BottomStepYawOffset = 0.0f;

	UPROPERTY()
	bool bBottomStepControlInitialized = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Path Controls", meta = (DisplayName = "Use Intermediate Controls"))
	bool bUseIntermediateControls = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Path Controls", meta = (DisplayName = "中间控制点偏移"))
	TArray<FVector2D> IntermediateControlOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Material")
	TSoftObjectPtr<UMaterialInterface> TreadMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Material")
	TSoftObjectPtr<UMaterialInterface> RiserMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair|Material")
	TSoftObjectPtr<UMaterialInterface> SideMaterial;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBStairPathSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling", meta = (Units = "cm"))
	float Distance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling")
	int32 StepIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling", meta = (Units = "cm"))
	float StepTopZ = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling")
	FVector LocalLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling")
	FVector LocalForward = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Sampling")
	FVector LocalRight = FVector::RightVector;
};

/**
 * 楼梯为扶手提供的柱位采样。
 *
 * 这不是扶手最终柱数据，只是楼梯局部空间下的候选位置：
 * 扶手 Actor 会再转换到自己的局部空间，并叠加门洞裁剪、Guid 复用和端点合并。
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBStairRailingPostSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Railing", meta = (Units = "cm"))
	float Distance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Railing")
	int32 StepIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Railing")
	FVector LocalBaseLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Railing")
	FRotator LocalRotation = FRotator::ZeroRotator;
};

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_Stair", ToolTip = "Procedurally generated stair actor."))
class EASYHOUSEBUILDER_API AEHB_Stair : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> TreadMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> RiserMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> SideMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LeftRailingPostMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RightRailingPostMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> LeftRailingRailMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> RightRailingRailMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Stair|Components")
	TArray<TObjectPtr<UStaticMeshComponent>> LeftRailingPostOverrideMeshComponents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Stair|Components")
	TArray<TObjectPtr<UStaticMeshComponent>> RightRailingPostOverrideMeshComponents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stair")
	FEHBStairData StairData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Generated")
	int32 GeneratedStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Generated", meta = (Units = "cm"))
	float GeneratedStepHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Generated")
	TArray<FEHBRailingPost> LeftGeneratedRailingPosts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stair|Generated")
	TArray<FEHBRailingPost> RightGeneratedRailingPosts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Stair|Generated")
	TArray<FGuid> LeftRailingPostInstanceGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Stair|Generated")
	TArray<FGuid> RightRailingPostInstanceGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Stair|Generated")
	TArray<FGuid> LeftRailingPostOverrideComponentGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Stair|Generated")
	TArray<FGuid> RightRailingPostOverrideComponentGuids;

	AEHB_Stair();

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Stair")
	bool RebuildStairMesh();

	UFUNCTION(BlueprintCallable, Category = "Stair")
	void ConfigureDefaultStair(
		AEHBBuildingActorBase* InBuilding,
		const FTransform& LocalTransform,
		float InStairHeight = 280.0f,
		float InStairWidth = 150.0f,
		float InTreadDepth = 30.0f);

	UFUNCTION(BlueprintCallable, Category = "Stair")
	bool CalculateStairs(float TotalHeight, int32& OutNumSteps, float& OutStepHeight) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Bottom Control")
	float GetStairLength() const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Bottom Control")
	FVector GetBottomControlWorldLocation() const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Bottom Control")
	FRotator GetBottomControlWorldRotation() const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Bottom Control")
	void SetBottomControlWorldLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Stair|Bottom Control")
	void AddBottomControlYaw(float DeltaYaw);

	UFUNCTION(BlueprintCallable, Category = "Stair|Path Controls")
	FVector GetIntermediateControlWorldLocation(int32 ControlIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Path Controls")
	void SetIntermediateControlWorldLocation(int32 ControlIndex, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Stair|Path Controls")
	void ResetIntermediateControlOffsets();

	/**
	 * 采样楼梯侧边扶手路径。
	 *
	 * 返回楼梯局部空间中的位置、前进方向和右方向。
	 * 扶手不要直接理解楼梯踏步几何，而是通过这个接口跟随楼梯的弯曲路径和台阶高度。
	 */
	UFUNCTION(BlueprintCallable, Category = "Stair|Sampling")
	bool SamplePathForRailing(
		EEHBRailingSide Side,
		float Distance,
		float LateralOffset,
		float BaseHeightOffset,
		FEHBStairPathSample& OutSample) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Sampling")
	bool SampleRailPathForRailing(
		EEHBRailingSide Side,
		float Distance,
		float LateralOffset,
		float BaseHeightOffset,
		FEHBStairPathSample& OutSample) const;

	/** 根据距离或踏步对齐策略生成候选扶手柱位。 */
	UFUNCTION(BlueprintCallable, Category = "Stair|Sampling")
	bool BuildRailingPostSamples(
		EEHBRailingSide Side,
		EEHBRailingPostSpacingMode SpacingMode,
		float PostSpacing,
		int32 StepsPerPost,
		float LateralOffset,
		float BaseHeightOffset,
		TArray<FEHBStairRailingPostSample>& OutSamples) const;

	const TArray<FEHBRailingPost>& GetEmbeddedRailingPosts(EEHBRailingSide Side) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool FindEmbeddedRailingPostByGuid(FGuid PostGuid, FEHBRailingPost& OutPost) const;

	bool FindEmbeddedRailingPostByGuidWithSide(FGuid PostGuid, FEHBRailingPost& OutPost, EEHBRailingSide& OutSide) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool GetEmbeddedRailingPostGuidForInstanceIndex(EEHBRailingSide Side, int32 InstanceIndex, FGuid& OutPostGuid) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool GetEmbeddedRailingPostGuidForOverrideComponent(const UActorComponent* Component, FGuid& OutPostGuid, EEHBRailingSide& OutSide) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool GetEmbeddedRailingPostWorldLocation(FGuid PostGuid, FVector& OutWorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool ApplyRailingMeshSampleToEmbeddedPost(FGuid PostGuid, UDataTable* InTable, FName InRowName, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool ApplyRailingMeshSampleToEmbeddedRailings(UDataTable* InTable, FName InRowName, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Stair|Railing")
	bool IsEmbeddedRailingSideGenerated(EEHBRailingSide Side) const;

	FVector TransformStraightStairLocalPointToPath(const FVector& StairLocalPoint) const;

private:
	bool TryResolveAttachedFloorSlabLandingTopLocalZ(float ExpectedLandingTopZ, float& OutLandingTopLocalZ) const;
	float ResolveSideBoardLandingTopLocalZ(float StairHeight, float StepHeight) const;
	void NormalizeStairData();
	void RebuildEmbeddedRailings();
	void RebuildEmbeddedRailingSide(
		EEHBRailingSide Side,
		UHierarchicalInstancedStaticMeshComponent* PostComponent,
		UEHBGeneratedMeshComponent* RailComponent);
	void ClearEmbeddedRailingSide(
		UHierarchicalInstancedStaticMeshComponent* PostComponent,
		UEHBGeneratedMeshComponent* RailComponent) const;
	TArray<FEHBRailingPost>& GetMutableEmbeddedRailingPosts(EEHBRailingSide Side);
	TArray<FGuid>& GetMutableEmbeddedRailingPostInstanceGuids(EEHBRailingSide Side);
	TArray<FGuid>& GetMutableEmbeddedRailingPostOverrideComponentGuids(EEHBRailingSide Side);
	TArray<TObjectPtr<UStaticMeshComponent>>& GetMutableEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side);
	const TArray<FGuid>& GetEmbeddedRailingPostInstanceGuids(EEHBRailingSide Side) const;
	const TArray<FGuid>& GetEmbeddedRailingPostOverrideComponentGuids(EEHBRailingSide Side) const;
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side) const;
	void TrimEmbeddedRailingPostOverrideMeshComponents(EEHBRailingSide Side, int32 DesiredCount);
	UStaticMeshComponent* GetOrCreateEmbeddedRailingPostOverrideMeshComponent(EEHBRailingSide Side, int32 ComponentIndex);
	void RebuildEmbeddedRailingPostOverrideMeshes(EEHBRailingSide Side);
	const FEHBRailingPostMeshOverride* FindEmbeddedRailingPostMeshOverride(FGuid PostGuid) const;
	bool HasEmbeddedRailingPostMeshOverride(FGuid PostGuid) const;
};
