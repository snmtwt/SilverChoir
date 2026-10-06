#pragma once
#include "CoreMinimal.h"
#include "Definitions/EHBElementRelations.h"

class AEHBBuildingActorBase;
class AEHBElementActorBase;
struct FEHBWallNodeModel;
struct FEHBWallNodeCopyDraft;
struct FEHBNodeRoomBoundary;
namespace EHBRoomFinishMove { struct FNodeEditPlan; }

/** Explicit room-bound outline and SurfaceFinish metadata policy for whole-building copy.
 * The generic value copier still refuses arbitrary string metadata. */
struct FEHBCopyOutlinePolicy
{
	bool bSucceeded = false;
	FName Status;
	TArray<FEHBElementRelation> RelationsWithoutRoomMetadata;
	TMap<FGuid, FGuid> FinishRelationRooms;
	static FEHBCopyOutlinePolicy Prepare(AEHBBuildingActorBase* Building,
		const FEHBWallNodeModel& Model, const TArray<AEHBElementActorBase*>& Elements,
		bool bDeferOtherRelationValidation=false, bool bAllowFixedSlabSources=false);
	bool RestoreMappedMetadata(FEHBWallNodeCopyDraft& Draft) const;
	static bool ApplyActorReferences(const FEHBWallNodeCopyDraft& Draft,
		const TMap<FGuid, AEHBElementActorBase*>& ImportedByOldId);
private:
	// Only the node plan can supply its freshly validated source rooms.
	friend struct EHBRoomFinishMove::FNodeEditPlan;
	static FEHBCopyOutlinePolicy PrepareWithValidatedRooms(AEHBBuildingActorBase* Building,
		const TArray<FEHBNodeRoomBoundary>& Rooms, const TArray<AEHBElementActorBase*>& Elements,
		bool bDeferOtherRelationValidation, bool bAllowFixedSlabSources=false, const FEHBWallNodeModel* ValidatedModel=nullptr);
};
