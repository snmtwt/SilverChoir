#include "EHBCopyOutlinePolicy.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBWallNodeCopy.h"
#include "Core/EHBWallNodeRooms.h"
#include "EasyHouseEditorMode.h"

namespace
{
	bool SameCyclicPolygon(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		if (A.Num() < 3 || A.Num() != B.Num()) return false;
		for (int32 Start = 0; Start < B.Num(); ++Start)
			for (int32 Direction : { 1, -1 })
			{
				bool bSame = true;
				for (int32 I = 0; I < A.Num() && bSame; ++I)
					bSame &= A[I].Equals(B[(Start + Direction * I + B.Num()) % B.Num()], 0.001);
				if (bSame) return true;
			}
		return false;
	}
}

FEHBCopyOutlinePolicy FEHBCopyOutlinePolicy::Prepare(AEHBBuildingActorBase* Building,
	const FEHBWallNodeModel& Model, const TArray<AEHBElementActorBase*>& Elements, bool bDeferOtherRelationValidation,bool bAllowFixedSlabSources)
{
	TArray<FEHBNodeRoomBoundary> Rooms;FName RoomStatus;
	if (!Building || !FEHBWallNodeRooms::Build(Building->BuildingGuid, Model, Rooms, RoomStatus))
	{ FEHBCopyOutlinePolicy Result;Result.Status=TEXT("InvalidCopyRooms");return Result; }
	return PrepareWithValidatedRooms(Building,Rooms,Elements,bDeferOtherRelationValidation,bAllowFixedSlabSources,&Model);
}

FEHBCopyOutlinePolicy FEHBCopyOutlinePolicy::PrepareWithValidatedRooms(AEHBBuildingActorBase* Building,
	const TArray<FEHBNodeRoomBoundary>& Rooms,const TArray<AEHBElementActorBase*>& Elements,bool bDeferOtherRelationValidation,bool bAllowFixedSlabSources,const FEHBWallNodeModel* ValidatedModel)
{
	auto Fail = [](FName Status) { FEHBCopyOutlinePolicy Empty; Empty.Status = Status; return Empty; };
	FEHBCopyOutlinePolicy Result;
	TMap<FGuid, AEHBElementActorBase*> ById;
	TMap<FGuid, const FEHBNodeRoomBoundary*> RoomsById;
	for (auto* Element : Elements) ById.Add(Element->ElementGuid, Element);
	for (const auto& Room : Rooms) RoomsById.Add(Room.RoomGuid, &Room);
	TMap<FGuid, FGuid> ClaimedFinishRelations;
	TArray<FEHBWallJunctionWallSides> ModelSides;
	if(ValidatedModel){FName Status;if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(*ValidatedModel,ModelSides,Status))return Fail(Status);}
	for (auto* Element : Elements)
	{
		if (auto* Floor = Cast<AEHB_Floor>(Element))
		{
			const bool Independent=Floor->OutlineSource==EEHBOutlineSource::RetainedRegion;
			const auto* Room = RoomsById.FindRef(Floor->RoomLoopGuid);
			if ((Independent?Floor->RoomLoopGuid.IsValid():(!Room || Room->FloorIndex != Floor->RoomFloorIndex)) || Floor->FloorIndex != Floor->RoomFloorIndex
				|| Floor->FloorRole != EEHBBuildingFloorElementRole::FloorFinish) return Fail(TEXT("StaleFloorRoomBinding"));
			if ((!Independent && Floor->OutlineSource != EEHBOutlineSource::RoomBoundary && Floor->OutlineSource != EEHBOutlineSource::RoomSupportFill) || !Floor->IsRecordedOutlineUnchanged()
				|| !Floor->CutOperations.IsEmpty() || Floor->FloorRegions.Num() != 1 || !Floor->FloorRegions[0].Holes.IsEmpty()
				|| !Floor->GetElementLocalTransform().Equals(FTransform::Identity, 0.0001)
				|| !Floor->ValidateFloorRegions(Floor->FloorRegions)) return Fail(TEXT("UnsupportedFloorCopyOutline"));
			const auto& Polygon = Floor->FloorRegions[0].OuterPolygon;
			if (Polygon.IsEmpty()) return Fail(TEXT("UnsupportedFloorCopyOutline"));
			auto RoomPolygon = Independent?Polygon:Room->Polygon;
			for (auto& Point : RoomPolygon) Point.Z = Polygon[0].Z;
			if (!SameCyclicPolygon(RoomPolygon, Polygon) || !SameCyclicPolygon(Polygon, Floor->LocalFloorPolygon))
				return Fail(TEXT("FloorCopyBoundaryMismatch"));
			for (FGuid Id : Floor->SurfaceFinishRelationGuids)
			{
				if (!Id.IsValid() || ClaimedFinishRelations.Contains(Id)) return Fail(TEXT("InvalidFloorRelationCache"));
				ClaimedFinishRelations.Add(Id, Floor->ElementGuid);
			}
		}
		else if (auto* Slab = Cast<AEHB_FloorSlab>(Element))
		{
			const bool Independent=Slab->OutlineSource==EEHBOutlineSource::RetainedRegion;
            const bool CompleteSource=bAllowFixedSlabSources&&Slab->DisplayPartition.SourceVersion==1&&Slab->DisplayPartition.IsActive()&&Slab->ValidatePartitionedSlabState(Slab->LocalTopPolygon,Slab->LocalHoles,Slab->CutOperations,Slab->DisplayPartition);
            if(CompleteSource)for(const auto& Cut:Slab->CutOperations)
            {
                if((Cut.TransformPolicy!=EEHBCutTransformPolicy::TargetLocal&&!(Cut.SurfaceHost.Version==1&&Cut.TransformPolicy==EEHBCutTransformPolicy::FollowTargetElement))||Cut.Source.SourceElement||Cut.Source.SourceElementGuid.IsValid())return Fail(TEXT("LinkedOpeningRequiresPlan"));
                // Disabled sources still need a valid identity if copied or later enabled.
                if(Cut.SurfaceHost.Version!=0 && (Cut.SurfaceHost.Version!=1 || Cut.SurfaceHost.BuildingGuid!=Building->BuildingGuid
                    || Cut.SurfaceHost.ElementGuid!=Slab->ElementGuid || !Cut.SurfaceHost.SurfaceGuid.IsValid()
                    || Cut.SurfaceHost.SurfaceGuid!=Slab->FindLogicalSurfaceIdentity(TEXT("Slab.Surface"))))return Fail(TEXT("InvalidSlabOpeningCopyHost"));
            }
			const auto* Room = RoomsById.FindRef(Slab->RoomFillLoopGuid);
			if ((Independent?Slab->RoomFillLoopGuid.IsValid():(!Room || Room->FloorIndex != Slab->RoomFillFloorIndex)) || Slab->FloorIndex != Slab->RoomFillFloorIndex
				|| Slab->FloorRole != EEHBBuildingFloorElementRole::FloorCeiling) return Fail(TEXT("StaleSlabRoomBinding"));
			if ((!Independent && Slab->OutlineSource != EEHBOutlineSource::RoomBoundary) || !Slab->IsRecordedOutlineUnchanged()
				|| Slab->bIsFoundation || Slab->bKeepFoundationBottomOnGround || Slab->bHasAIFoundationSource || !Slab->AIFoundationSourceJson.IsEmpty()
				|| !Slab->AIDesignTopPolygon.IsEmpty() || (!CompleteSource&&(!Slab->CutOperations.IsEmpty()||!Slab->LocalHoles.IsEmpty())) || !Slab->PreviewCutters.IsEmpty()
				|| !Slab->GetElementLocalTransform().GetScale3D().Equals(FVector::OneVector, 0.0001)
				|| !FMath::IsNearlyZero(Slab->GetActorRotation().Pitch) || !FMath::IsNearlyZero(Slab->GetActorRotation().Roll)
				|| !Slab->ValidateSlabOutline(Slab->LocalTopPolygon, CompleteSource?Slab->LocalHoles:TArray<FEHBFloorSlabHole>{})) return Fail(TEXT("UnsupportedSlabCopyOutline"));
			if ((Slab->bHasRoomFillAnchor && (Independent || !Room->WallGuids.Contains(Slab->RoomFillAnchorWallGuid)
				|| (Slab->RoomFillAnchorWallSide != EEHBFloorSlabWallSide::Left && Slab->RoomFillAnchorWallSide != EEHBFloorSlabWallSide::Right)))
				|| (!Slab->bHasRoomFillAnchor && (Slab->RoomFillAnchorWallGuid.IsValid() || Slab->RoomFillAnchorWallSide != EEHBFloorSlabWallSide::None)))
				return Fail(TEXT("StaleSlabWallAnchor"));
			if(Independent)continue;
			const auto Loops = Building->GetClosedLoopsByFloor(Room->FloorIndex);
			const auto* Loop = Loops.FindByPredicate([&](const auto& Candidate) { return Candidate.LoopGuid == Room->RoomGuid; });
			TArray<FVector> Polygon;
			const bool OutlineReady=ValidatedModel
				?FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(Slab,*Room,*ValidatedModel,ModelSides,Polygon)
				:(Loop&&FEasyHouseEditorMode::BuildRoomSlabFollowOutline(Slab,*Loop,{},Polygon));
			if (!Loop || !OutlineReady
				|| !SameCyclicPolygon(Polygon, Slab->LocalTopPolygon)) return Fail(TEXT("SlabCopyBoundaryMismatch"));
		}
	}
	for (const auto& Relation : Building->ElementRelations)
	{
		auto Mapped = Relation;
		if (Relation.Type == EEHBElementRelationType::SurfaceFinish)
		{
			const auto* Floor = Cast<AEHB_Floor>(ById.FindRef(Relation.Target.ElementGuid));
			const auto* Host = ById.FindRef(Relation.Source.ElementGuid);
			const auto* RoomText = Relation.StringMetadata.Find(TEXT("RoomLoopGuid"));
			const auto* RoomFloor = Relation.NumericMetadata.Find(TEXT("RoomFloorIndex"));
			FGuid RoomId;
			const bool Independent=Floor && Floor->OutlineSource==EEHBOutlineSource::RetainedRegion;
			const auto* SourceText=Relation.StringMetadata.Find(TEXT("OutlineSource"));
			if (!Floor || !Host || !Host->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport))
				|| Relation.Source.Kind != EEHBRelationEndpointKind::BuildingElement || Relation.Target.Kind != EEHBRelationEndpointKind::BuildingElement
				|| Relation.Source.SurfaceKind != EEHBElementSurfaceKind::Top || Relation.Target.SurfaceKind != EEHBElementSurfaceKind::Bottom
				|| Relation.bAffectsFloorAssignment || ClaimedFinishRelations.FindRef(Relation.RelationGuid) != Floor->ElementGuid
				|| Relation.StringMetadata.Num() != 1
				|| (Independent?(!SourceText || *SourceText!=TEXT("RetainedRegion")) : (!RoomText || !FGuid::Parse(*RoomText, RoomId) || RoomId != Floor->RoomLoopGuid))
				|| !RoomFloor || *RoomFloor != Floor->RoomFloorIndex) return Fail(TEXT("UnsupportedFinishCopyRelation"));
			if (Result.FinishRelationRooms.Contains(Relation.RelationGuid)) return Fail(TEXT("InvalidFloorRelationCache"));
			Result.FinishRelationRooms.Add(Relation.RelationGuid, RoomId);
			Mapped.StringMetadata.Reset();
		}
		else if (Relation.Type != EEHBElementRelationType::TopologyConnection)
		{
			// A compound command may validate doors/railings with its own explicit
			// policy. Whole-building copy retains its strict default behavior.
			if(!bDeferOtherRelationValidation)return Fail(TEXT("NonTopologyCopyPolicyRequired"));
			continue;
		}
		Result.RelationsWithoutRoomMetadata.Add(MoveTemp(Mapped));
	}
	if (Result.FinishRelationRooms.Num() != ClaimedFinishRelations.Num()) return Fail(TEXT("InvalidFloorRelationCache"));
	Result.bSucceeded = true;
	Result.Status = TEXT("Ready");
	return Result;
}

bool FEHBCopyOutlinePolicy::RestoreMappedMetadata(FEHBWallNodeCopyDraft& Draft) const
{
	for (const auto& Pair : FinishRelationRooms)
	{
		const auto* RelationId = Draft.RelationGuids.Find(Pair.Key);
		const auto* RoomId = Draft.RoomGuids.Find(Pair.Value);
		if (!RelationId || (Pair.Value.IsValid() && !RoomId)) return false;
		auto* Relation = Draft.Relations.FindByPredicate([&](const auto& R) { return R.RelationGuid == *RelationId; });
		if (!Relation) return false;
		if(RoomId)Relation->StringMetadata.Add(TEXT("RoomLoopGuid"), RoomId->ToString(EGuidFormats::DigitsWithHyphens));
		else Relation->StringMetadata.Add(TEXT("OutlineSource"),TEXT("RetainedRegion"));
	}
	return true;
}

bool FEHBCopyOutlinePolicy::ApplyActorReferences(const FEHBWallNodeCopyDraft& Draft,
	const TMap<FGuid, AEHBElementActorBase*>& ImportedByOldId)
{
	auto Remap = [](FGuid& Id, const TMap<FGuid, FGuid>& Map) { const auto* Found = Map.Find(Id); if (!Found) return false; Id = *Found; return true; };
	for (const auto& Pair : ImportedByOldId)
	{
		if (auto* Floor = Cast<AEHB_Floor>(Pair.Value))
		{
			if (Floor->OutlineSource!=EEHBOutlineSource::RetainedRegion && !Remap(Floor->RoomLoopGuid, Draft.RoomGuids)) return false;
			for (auto& Id : Floor->SurfaceFinishRelationGuids) if (!Remap(Id, Draft.RelationGuids)) return false;
		}
		else if (auto* Slab = Cast<AEHB_FloorSlab>(Pair.Value))
		{
			if (Slab->OutlineSource!=EEHBOutlineSource::RetainedRegion && !Remap(Slab->RoomFillLoopGuid, Draft.RoomGuids)) return false;
			if (Slab->bHasRoomFillAnchor && !Remap(Slab->RoomFillAnchorWallGuid, Draft.ElementGuids)) return false;
		}
	}
	return true;
}
