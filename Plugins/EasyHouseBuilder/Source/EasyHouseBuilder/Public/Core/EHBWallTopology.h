// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/EHBWallNodeDefinitions.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "Core/EHBWallJunctionMesh.h"
#include "EHBWallTopology.generated.h"

class AEHBBuildingActorBase;

/** Value-only junction data. SourcePillarGuid is a migration link, not a required mesh. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallTopologyNode
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid NodeGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid SourcePillarGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector LocalPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") int32 FloorIndex = 0;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallTopologyEdge
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid WallGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid StartNodeGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid EndNodeGuid;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallTopologyIssue
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FName Code;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid ElementGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FString Message;
};

/**
 * Read model v1, NOT a replacement for the building's serialized authoring data.
 * IDs are scoped to BuildingGuid. The legacy adapter maps NodeGuid to pillar ID;
 * consumers must not interpret that as requiring a pillar Actor in future versions.
 * Arrays are sorted by GUID so comparisons do not depend on map iteration order.
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallTopologySnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") int32 SchemaVersion = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid BuildingGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bLegacyActorBacked = true;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyNode> Nodes;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyEdge> Walls;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyIssue> Issues;
};

/** Building-owned migration baseline. Version 0 means never initialized.
 * Legacy actors remain authoritative until movement/deletion have migrated.
 * No owner GUID is stored: duplicated buildings get their own identity scope.
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPersistedWallTopology
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") int32 Version = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyNode> Nodes;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyEdge> Walls;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBTopologyMigrationResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bChanged = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FName Status;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") int32 NodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") int32 WallCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyIssue> Issues;
};

/** Boundary proposal in building-local centimeters. IDs name the existing room;
 * split/merge identity transfer and follow policies still require explicit planning.
 * Wall-anchored slabs are candidates: a shared wall may border more than one room. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRoomBoundaryMovePreview
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") FGuid RoomGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") int32 FloorIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FGuid> BoundaryPillarGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FVector> OriginalPolygon;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FVector> ProposedPolygon;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FGuid> BoundFloorFinishGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FGuid> CandidateAnchoredSlabGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FGuid> BoundRoomSlabGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FGuid> ModifiedOrUnclassifiedOutlineGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") bool bHasStaleSlabBinding = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") bool bHasStaleFloorBinding = false;
};

/** One requested node position in a simultaneous movement proposal, in building-local cm. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBNodeMoveRequest
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Topology") FGuid NodeGuid;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Topology") FVector ExpectedPosition = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Topology") FVector TargetPosition = FVector::ZeroVector;
};

/** Value-only dispatch boundary for plain straight-wall position edits, not a complete support/mesh plan. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallMoveUpdatePlan
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Topology") bool bSucceeded=false;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FName Status;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> MovedNodeGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> JunctionNodeGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> WallGuids;
};

/** Optional yaw change paired with a position request (including a no-position-change request).
 * Local rotations are normalized; expected orientation guards a stale pose draft. */
struct EASYHOUSEBUILDER_API FEHBNodeRotationRequest
{
	FGuid NodeGuid;
	FQuat ExpectedLocalRotation=FQuat::Identity;
	FQuat TargetLocalRotation=FQuat::Identity;
};

/** Detached definition edit draft. Never activates or overwrites a building's preparation.
 * Revisions mark potentially changed junction geometry, including unmoved adjacent nodes. */
struct EASYHOUSEBUILDER_API FEHBWallNodeMoveDraft
{
	bool bSucceeded=false,bWouldChange=false;
	FName Status;
	FEHBPreparedWallNodeDefinitions Definitions;
	FEHBWallMoveUpdatePlan UpdatePlan;
};

/** Detached edits for the optional-binding model. Caller plans relations/rooms/hosts and owns commit/rollback. */
struct EASYHOUSEBUILDER_API FEHBWallNodeModelEditDraft
{
	bool bSucceeded=false,bWouldChange=false;
	FName Status;
	FEHBWallNodeModel Definitions;
	FEHBWallMoveUpdatePlan UpdatePlan;
	TArray<FGuid> RemovedPhysicalPillarGuids;
};

/** Read-only movement proposal. Not a commit authorization or a full geometry dependency plan. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBNodeMovePreview
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bWouldChange = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FName Status;
	/** Canonical requests; legacy scalar fields below are populated for a single request only. */
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBNodeMoveRequest> NodeMoves;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid NodeGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector OriginalPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector TargetPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> DirectWallGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FEHBWallMoveUpdatePlan UpdatePlan;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> ConnectedNodeGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> ConnectedWallGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> UnplannedElementGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FEHBWallTopologySnapshot ProposedTopology;
	/** Informational dependencies only; does not remove elements from UnplannedElementGuids. */
	UPROPERTY(BlueprintReadOnly, Category = "Topology|Rooms") TArray<FEHBRoomBoundaryMovePreview> RoomBoundaryChanges;
};

/** Read-only separation preparation. Legacy deletion still destroys incident walls; no detach commit exists yet. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPillarSeparationPreview
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Topology") bool bSucceeded=false;
	UPROPERTY(BlueprintReadOnly, Category="Topology") bool bCommitAvailable=false;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FName Status;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FGuid RetainedNodeGuid;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FGuid PhysicalPillarGuid;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FTransform NodeLocalTransform;
	UPROPERTY(BlueprintReadOnly, Category="Topology") int32 FloorIndex=INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category="Topology") FVector PhysicalDimensions=FVector::ZeroVector;
	/** Proposed plain junction dimensions from incident walls, not physical column dimensions. */
	UPROPERTY(BlueprintReadOnly, Category="Topology") FVector ProposedJunctionDimensions=FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> RetainedWallGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> TopologyRelationGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> OtherRelationGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> AnchoredRailingGuids;
	/** Conservative disclosure; not every listed element is necessarily affected. */
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FGuid> UnplannedElementGuids;
	UPROPERTY(BlueprintReadOnly, Category="Topology") TArray<FEHBWallTopologyIssue> Issues;
};

/** Proposed transfer for an axis-aligned rectangular opening; target walls do not yet exist. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallOpeningTransfer
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid OpeningGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bAfterPillar = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") float NewDistanceFromStart = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector ProposedWallLocalStart = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector ProposedWallLocalEnd = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FTransform PreservedWorldTransform = FTransform::Identity;
};

/** Read-only insertion inspection. Never authorizes the legacy destructive split. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallSplitPreview
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bCommitAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FName Status;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FGuid WallGuid;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") FVector LocalPillarPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") float PillarWidth = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") float PillarHeight = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> BeforeOpeningGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> AfterOpeningGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> ConflictingOpeningGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FGuid> RelatedRelationGuids;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") bool bOpeningTransfersComplete = false;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallOpeningTransfer> OpeningTransfers;
	UPROPERTY(BlueprintReadOnly, Category = "Topology") TArray<FEHBWallTopologyIssue> Issues;
};

UCLASS()
class EASYHOUSEBUILDER_API UEHBWallTopologyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Inspect a straight unit-scale wall insertion in building-local centimeters.
	 * Copies dependency IDs only; no spawning, splitting, index repair or transaction.
	 */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology")
	static FEHBWallSplitPreview PreviewWallSplitForRailing(const AEHBBuildingActorBase* Building, FGuid WallGuid, float DistanceFromStart);

	/** Persist an immutable migration preparation payload. Does not activate node ownership or alter actors.
	 * Requires current legacy baseline; caller owns transaction. Stale/existing data never overwritten. */
	UFUNCTION(BlueprintCallable,Category="EHB|Topology")
	static FEHBTopologyMigrationResult PrepareWallNodeDefinitions(AEHBBuildingActorBase* Building,bool bApply=false);
	/** Pure canonical model validation: optional, unique physical bindings; no Actor lookup. */
	static TArray<FEHBWallTopologyIssue> ValidateWallNodeModel(const FEHBWallNodeModel& Data);
	static bool BuildWallNodeModelSides(const FEHBWallNodeModel& Data,TArray<FEHBWallJunctionWallSides>& Sides,FName& Reason,
		const TSet<FGuid>* RequestedWallGuids=nullptr,FEHBWallJunctionSolveStats* Stats=nullptr,
		TMap<FGuid,TArray<FVector>>* NodeFootprints=nullptr);
	static bool BuildWallNodeModelJunctionMesh(const FEHBWallNodeModel& Data,FGuid NodeGuid,FEHBWallJunctionMesh& Mesh,FName& Reason);
	static FEHBWallNodeModelEditDraft BuildWallNodeModelMoveDraft(const FEHBWallNodeModel& Source,const TArray<FEHBNodeMoveRequest>& Requests,
		const TArray<FEHBNodeRotationRequest>& Rotations={});
	/** Remove only a physical binding in a copy, retain node/wall identity and reduce fill to incident-wall dimensions.
	 * Expected revision/physical ID guards stale plans. Does not inspect or modify live dependencies/Actors. */
	static FEHBWallNodeModelEditDraft BuildPhysicalPillarRemovalDraft(const FEHBWallNodeModel& Source,FGuid NodeGuid,FGuid ExpectedPhysicalPillarGuid,int32 ExpectedNodeRevision);

	UFUNCTION(BlueprintPure,Category="EHB|Topology")
	static TArray<FEHBWallTopologyIssue> ValidatePreparedWallNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Data);

	/** Value-only bridge from validated prepared definitions to canonical wall sides; no Actor lookup. */
	static bool BuildPreparedWallSides(const FEHBPreparedWallNodeDefinitions& Data,TArray<FEHBWallJunctionWallSides>& Sides,FName& Reason,
		const TSet<FGuid>* RequestedWallGuids=nullptr,FEHBWallJunctionSolveStats* Stats=nullptr);

	/** Read current supported legacy values without requiring/updating the persisted baseline. Atomic output. */
	/** Optional wall IDs defer only hosted-opening checks to the enclosing command.
	 * Captures base node/wall geometry; never validates or applies those openings. */
	static FEHBTopologyMigrationResult CaptureNodeDefinitionSource(const AEHBBuildingActorBase* Building,FEHBPreparedWallNodeDefinitions& Definitions,
		const TSet<FGuid>* SeparatelyValidatedOpeningWalls=nullptr);
	/** Live source capture; V2 authority permits a logical node with no physical pillar. */
	static FEHBTopologyMigrationResult CaptureWallNodeModelSource(const AEHBBuildingActorBase* Building,FEHBWallNodeModel& Model,
		const TSet<FGuid>* SeparatelyValidatedOpeningWalls=nullptr);
	/** Complete node source capture with native surface-bound wall openings validated against the node solve. */
	static FEHBTopologyMigrationResult CaptureWallNodeModelWithSurfaceOpenings(const AEHBBuildingActorBase* Building,FEHBWallNodeModel& Model);

	/** Pure batched pose edit and revision planning; geometry/collision/support commit checks remain caller-owned. */
	static FEHBWallNodeMoveDraft BuildNodeDefinitionMoveDraft(const FEHBPreparedWallNodeDefinitions& Source,const TArray<FEHBNodeMoveRequest>& Requests,
		const TArray<FEHBNodeRotationRequest>& Rotations={});

	/** Pure junction fill generation. No pillar Actor or support identity is created. */
	static bool BuildPreparedJunctionMesh(const FEHBPreparedWallNodeDefinitions& Data,FGuid NodeGuid,FEHBWallJunctionMesh& Mesh,FName& Reason);

	/** Prepare the distinction between a retained connection node and removable physical column.
	 * Does not delete, migrate, resize, repair indexes, or authorize the legacy delete operation. */
	UFUNCTION(BlueprintCallable, Category="EHB|Topology")
	static FEHBPillarSeparationPreview PreviewPillarSeparation(const AEHBBuildingActorBase* Building,FGuid PillarGuid);

	/** Pure XY proposal. ExpectedPosition guards stale drags; no snapping, Actor movement or writes. */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology")
	static FEHBNodeMovePreview PreviewNodeMove(const AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition);

	/** Validate all final XY positions together, avoiding invalid intermediate single-node states.
	 * Rejects duplicate node requests and batches outside 1..2048. Never commits.
	 */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology")
	static FEHBNodeMovePreview PreviewNodeMoves(const AEHBBuildingActorBase* Building, const TArray<FEHBNodeMoveRequest>& Requests);

	/** Explicit opt-in baseline; preview never writes. Existing/stale data is never overwritten.
	 * Caller owns the editor transaction. No automatic migration on load.
	 */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology")
	static FEHBTopologyMigrationResult PrepareTopologyMigration(AEHBBuildingActorBase* Building, bool bApply = false);

	/** Read active topology relations without rebuilding indexes, modifying actors or repairing data. */
	UFUNCTION(BlueprintCallable, Category = "EHB|Topology")
	static FEHBWallTopologySnapshot CaptureWallTopology(const AEHBBuildingActorBase* Building);

	/** Validate value data independently of a world or pillar Actors. Does not modify the snapshot. */
	UFUNCTION(BlueprintPure, Category = "EHB|Topology")
	static TArray<FEHBWallTopologyIssue> ValidateWallTopology(const FEHBWallTopologySnapshot& Snapshot);

	/** Canonical moved roots, one-hop junction nodes, and all incident wall IDs. No Actor access.
	 * Validates graph identities/endpoints; does not authorize a geometry or support edit. */
	UFUNCTION(BlueprintPure, Category="EHB|Topology")
	static FEHBWallMoveUpdatePlan BuildWallMoveUpdatePlan(const FEHBWallTopologySnapshot& Snapshot,const TArray<FGuid>& MovedNodeGuids);

	/** Immediate incident walls only; this is not the complete mesh/material rebuild dependency set. */
	UFUNCTION(BlueprintPure, Category = "EHB|Topology")
	static TArray<FGuid> GetIncidentWallGuids(const FEHBWallTopologySnapshot& Snapshot, FGuid NodeGuid);
};
