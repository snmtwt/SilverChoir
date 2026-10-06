// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Core/EHBOutlineProvenance.h"
#include "Core/EHBBuildingActorBase.h"
#include "EHB_Floor.generated.h"

class FEHBFloorContactCache;
struct FEHBFloorContactCacheStats;
class UMaterialInterface;
class UEHBGeneratedMeshComponent;

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBFloorFinishHole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry")
	TArray<FVector> LocalPolygon;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBFloorFinishRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry")
	TArray<FVector> OuterPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry")
	TArray<FEHBFloorFinishHole> Holes;
};

/** A structural top surface that can receive a floor finish. Coordinates are in the same local space as the queried room polygon. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBFloorSupportSurface
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Fill")
	TArray<FVector> OuterPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Fill")
	TArray<FEHBFloorFinishHole> Holes;
};

/** Read-only physical-host delta for an edit draft. New IDs are reserved ELEMENT IDs,
 * never logical node IDs. This does not spawn hosts or authorize publishing relations.
 * The owning command must map/validate its final physical actors before applying it. */
struct EASYHOUSEBUILDER_API FEHBFloorHostTopologyDraft
{
	TMap<FGuid,TArray<FEHBFloorSupportSurface>> NewHostSurfaces;
	TSet<FGuid> RemovedHostGuids;
};

/**
 * Non-structural floor finish actor.
 *
 * The actor owns room/floor semantics and surface-finish relations. Its mesh is
 * offset slightly upward to avoid z-fighting, but the actor is never a support
 * source for structural placement or floor propagation.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_Floor"))
class EASYHOUSEBUILDER_API AEHB_Floor : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	AEHB_Floor();

	/** Last successful generation source. This does not enable automatic updates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Outline|Source", meta=(DisplayName="轮廓生成来源"))
	EEHBOutlineSource OutlineSource = EEHBOutlineSource::ManualOrUnclassified;

	UFUNCTION(BlueprintPure, Category = "Outline|Source", meta=(DisplayName="生成轮廓是否保持未修改"))
	bool IsRecordedOutlineUnchanged() const;

	/** Internal generation paths record only after the complete operation succeeds. */
	void RecordOutlineSource(EEHBOutlineSource Source);


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor|Component")
	TObjectPtr<UEHBGeneratedMeshComponent> MeshComponent;

	/** Region mesh components. The first entry is MeshComponent; additional entries are created when unsupported holes split the floor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Floor|Component")
	TArray<TObjectPtr<UEHBGeneratedMeshComponent>> RegionMeshComponents;

	/** Legacy/preview outline in this actor's local space. Multi-region fills use FloorRegions and keep this synchronized to the first region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry")
	TArray<FVector> LocalFloorPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry")
	TArray<FEHBFloorFinishRegion> FloorRegions;

	/** Rendering/collision surface lift in centimeters. Relationship and floor semantics stay on the logical support surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Geometry", meta = (ClampMin = "0.0", Units = "cm"))
	float VisualOffset = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Material")
	TSoftObjectPtr<UMaterialInterface> FloorMaterial;

	/** Runtime collision lets traces/characters identify the room by hitting this floor finish. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Collision")
	bool bEnableRuntimeCollision = true;

	/** Editor collision is off by default so placement/support detection ignores floor finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor|Collision")
	bool bEnableEditorCollision = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor|Room")
	FGuid RoomLoopGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor|Room")
	int32 RoomFloorIndex = INDEX_NONE;

	/** SurfaceFinish relation ids from structural/boundary sources to this floor. The building relation graph remains authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor|Relations")
	TArray<FGuid> SurfaceFinishRelationGuids;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Floor|Configure")
	bool ConfigureFromRoomLoop(
		AEHBBuildingActorBase* InBuilding,
		const FEHBBuildingClosedLoop& RoomLoop,
		float LocalSurfaceZ = 0.0f,
		bool bCreateSurfaceRelations = true);

	UFUNCTION(BlueprintCallable, Category = "Floor|Configure")
	bool ConfigureDefaultFloor(
		AEHBBuildingActorBase* InBuilding,
		const FTransform& LocalTransform,
		float InSize,
		int32 InFloorIndex);

	UFUNCTION(BlueprintCallable, Category = "Floor|Geometry")
	bool SetFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions, bool bRefreshRelations = true);

	/** Freshly prepares geometry and skips output only when source and actual output match. */
	bool SetFloorRegionsIfNeeded(const TArray<FEHBFloorFinishRegion>& InRegions,bool& bOutChanged);

	/** Read-only triangulation validation; never changes design, components or relations. */
	bool ValidateFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions) const;

	UFUNCTION(BlueprintCallable, Category = "Floor|Geometry")
	bool RebuildFloorMesh();

	UFUNCTION(BlueprintCallable, Category = "Floor|Relations")
	void RefreshSurfaceFinishRelationsFromRoomLoop(const FEHBBuildingClosedLoop& RoomLoop);

 /** Read-only relation draft. Candidate horizontal top surfaces use building-local space; absent hosts use current generated tops.
  * Polygon contact uses 0.001 cm quantization and 1 cm height tolerance; this is not load-bearing proof. */
 bool BuildSurfaceFinishRelationPlan(const FEHBBuildingClosedLoop& RoomLoop,
  const TArray<FEHBFloorFinishRegion>& InRegions, TArray<FEHBElementRelation>& OutRelations,
  FName& Status, const TMap<FGuid,TArray<FEHBFloorSupportSurface>>* CandidateHostSurfaces=nullptr,
 const FEHBFloorHostTopologyDraft* HostTopology=nullptr) const;
 /** Resolve the live source explicitly. Retained regions have no room; this
  * does not fabricate a closed-loop query result or accept stale room bindings. */
 bool BuildCurrentSurfaceFinishRelationPlan(const TArray<FEHBFloorFinishRegion>& InRegions,
  TArray<FEHBElementRelation>& OutRelations,FName& Status,
  const TMap<FGuid,TArray<FEHBFloorSupportSurface>>* CandidateHostSurfaces=nullptr,
  const FEHBFloorHostTopologyDraft* HostTopology=nullptr) const;
 /** Stable-ID refresh. Caller owns the surrounding edit transaction; malformed old caches refuse before writes. */
 FEHBFloorContactCacheStats GetContactCacheStats() const;
 bool TryRefreshSurfaceFinishRelationsFromRoomLoop(const FEHBBuildingClosedLoop& RoomLoop,FName& Status);

	UFUNCTION(BlueprintCallable, Category = "Floor|Relations")
	void ClearSurfaceFinishRelations();

	UFUNCTION(BlueprintPure, Category = "Floor|Room")
	bool TryGetRoomLoop(FEHBBuildingClosedLoop& OutRoomLoop) const;

	UFUNCTION(BlueprintCallable, Category = "Floor|Collision")
	void UpdateCollisionSettings();

	static bool BuildFloorFinishRegionsFromSupportSurfaces(
		const TArray<FVector>& RoomPolygon,
		const TArray<FEHBFloorSupportSurface>& SupportSurfaces,
		float LocalSurfaceZ,
		TArray<FEHBFloorFinishRegion>& OutRegions);

	float GetRenderSurfaceZ() const;

private:
	UPROPERTY()
	FGuid RecordedOutlineSignature;
	FGuid BuildOutlineSignature() const;
	bool ApplyFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions, bool bRecordTransaction, bool* bOutChanged=nullptr);
	UEHBGeneratedMeshComponent* GetOrCreateRegionMeshComponent(int32 RegionIndex);
	void TrimRegionMeshComponents(int32 DesiredCount);
	bool BuildRegionMesh(
		const FEHBFloorFinishRegion& Region,
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs) const;
	void ApplyMeshToComponent(
		UEHBGeneratedMeshComponent* TargetComponent,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs);
private:
 // Derived value-only solves; rebuilt after load and verified against actual inputs after undo.
 mutable TSharedPtr<FEHBFloorContactCache> ContactCache;
};
