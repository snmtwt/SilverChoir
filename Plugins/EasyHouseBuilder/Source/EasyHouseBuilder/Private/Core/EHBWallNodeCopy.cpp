// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBWallNodeCopy.h"

#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBWallTopology.h"

namespace
{
	struct FRelationKey
	{
		EEHBElementRelationType Type;
		FEHBElementRelationEndpoint Source, Target;
		bool operator==(const FRelationKey& Other) const
		{
			return Type == Other.Type && Source.IsEquivalentTo(Other.Source) && Target.IsEquivalentTo(Other.Target);
		}
		friend uint32 GetTypeHash(const FRelationKey& Key)
		{
			uint32 Hash = GetTypeHash(static_cast<uint8>(Key.Type));
			for (const auto* Endpoint : { &Key.Source, &Key.Target })
			{
				Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Endpoint->Kind)));
				Hash = HashCombine(Hash, GetTypeHash(Endpoint->ElementGuid));
				Hash = HashCombine(Hash, GetTypeHash(Endpoint->NodeGuid));
				Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Endpoint->SurfaceKind)));
				Hash = HashCombine(Hash, GetTypeHash(Endpoint->SurfaceName));
				Hash = HashCombine(Hash, GetTypeHash(Endpoint->SubIndex));
			}
			return Hash;
		}
	};

	FString RoomKey(int32 Floor, TArray<FGuid> Walls)
	{
		Walls.Sort([](const FGuid& A, const FGuid& B) { return A < B; });
		FString Key = FString::FromInt(Floor);
		for (const auto& Wall : Walls) Key += TEXT(":") + Wall.ToString();
		return Key;
	}
}

FEHBWallNodeCopyDraft FEHBWallNodeCopy::BuildDraft(FGuid SourceBuildingGuid,
	const FEHBWallNodeModel& Source, const TArray<FGuid>& ElementGuids,
	const TArray<FEHBElementRelation>& Relations)
{
	auto Fail = [](FName Reason) { FEHBWallNodeCopyDraft Empty; Empty.Status = Reason; return Empty; };
	if (!SourceBuildingGuid.IsValid()) return Fail(TEXT("InvalidBuildingIdentity"));
	const auto Issues = UEHBWallTopologyLibrary::ValidateWallNodeModel(Source);
	if (!Issues.IsEmpty()) return Fail(Issues[0].Code);
	TSet<FGuid> Elements, Nodes, UsedIds, RelationIds;
	UsedIds.Add(SourceBuildingGuid);
	for (FGuid Id : ElementGuids)
	{
		if (!Id.IsValid() || Elements.Contains(Id)) return Fail(TEXT("InvalidElementIdentity"));
		Elements.Add(Id); UsedIds.Add(Id);
	}
	for (const auto& Node : Source.Nodes) { Nodes.Add(Node.NodeGuid); UsedIds.Add(Node.NodeGuid); }
	TMap<FGuid, const FEHBNodeConnectedWallDefinition*> Walls;
	for (const auto& Wall : Source.Walls)
	{
		if (!Elements.Contains(Wall.WallGuid)) return Fail(TEXT("WallOutsideCopyBoundary"));
		Walls.Add(Wall.WallGuid, &Wall);
	}
	for (const auto& Binding : Source.PillarBindings)
	{
		if (!Elements.Contains(Binding.PhysicalPillarGuid)) return Fail(TEXT("PillarOutsideCopyBoundary"));
		if (Walls.Contains(Binding.PhysicalPillarGuid)) return Fail(TEXT("ConflictingPhysicalIdentity"));
	}
	TMap<FGuid, uint8> WallPorts;
	TSet<FRelationKey> RelationKeys;
	for (const auto& Relation : Relations)
	{
		if (!Relation.RelationGuid.IsValid() || RelationIds.Contains(Relation.RelationGuid)) return Fail(TEXT("InvalidRelationIdentity"));
		RelationIds.Add(Relation.RelationGuid); UsedIds.Add(Relation.RelationGuid);
		if (Relation.Type <= EEHBElementRelationType::None || Relation.Type > EEHBElementRelationType::LogicalDependency) return Fail(TEXT("InvalidRelationType"));
		if (!Relation.StringMetadata.IsEmpty()) return Fail(TEXT("MetadataCopyPolicyRequired"));
		for (const auto* Endpoint : { &Relation.Source, &Relation.Target })
		{
			if (Endpoint->Kind != EEHBRelationEndpointKind::BuildingElement && Endpoint->Kind != EEHBRelationEndpointKind::WallNode) return Fail(TEXT("ExternalCopyPolicyRequired"));
			if (!Endpoint->IsValid() || !Endpoint->ExternalActor.IsNull()) return Fail(TEXT("InvalidRelationEndpoint"));
			if (Endpoint->Kind == EEHBRelationEndpointKind::BuildingElement && !Elements.Contains(Endpoint->ElementGuid)) return Fail(TEXT("ElementOutsideCopyBoundary"));
			if (Endpoint->Kind == EEHBRelationEndpointKind::WallNode)
			{
				if (!Nodes.Contains(Endpoint->NodeGuid)) return Fail(TEXT("NodeOutsideCopyBoundary"));
				if (Relation.Type != EEHBElementRelationType::TopologyConnection && Relation.Type != EEHBElementRelationType::LogicalDependency) return Fail(TEXT("NodeCannotBePhysicalHost"));
			}
		}
		const FRelationKey Key{ Relation.Type, Relation.Source, Relation.Target };
		if (RelationKeys.Contains(Key)) return Fail(TEXT("DuplicateRelation"));
		RelationKeys.Add(Key);
		if (Relation.Type == EEHBElementRelationType::TopologyConnection)
		{
			const auto* Wall = Walls.FindRef(Relation.Source.ElementGuid);
			if (!Relation.bEnabled || Relation.Source.Kind != EEHBRelationEndpointKind::BuildingElement
				|| Relation.Target.Kind != EEHBRelationEndpointKind::WallNode || !Wall) return Fail(TEXT("InvalidTypedTopology"));
			const uint8 Port = Relation.Source.SurfaceKind == EEHBElementSurfaceKind::Start ? 1
				: Relation.Source.SurfaceKind == EEHBElementSurfaceKind::End ? 2 : 0;
			if (!Port || Relation.Target.NodeGuid != (Port == 1 ? Wall->StartNodeGuid : Wall->EndNodeGuid)) return Fail(TEXT("TopologyModelMismatch"));
			uint8& Seen = WallPorts.FindOrAdd(Wall->WallGuid);
			if (Seen & Port) return Fail(TEXT("DuplicateWallPort"));
			Seen |= Port;
		}
	}
	for (const auto& Wall : Source.Walls)
		if (WallPorts.FindRef(Wall.WallGuid) != 3) return Fail(TEXT("MissingWallPort"));

	TArray<FEHBNodeRoomBoundary> SourceRooms, CopyRooms; FName Reason;
	if (!FEHBWallNodeRooms::Build(SourceBuildingGuid, Source, SourceRooms, Reason)) return Fail(Reason);
	for (const auto& Room : SourceRooms) UsedIds.Add(Room.RoomGuid);
	auto FreshId = [&]()
	{
		FGuid Id;
		do { Id = FGuid::NewGuid(); } while (!Id.IsValid() || UsedIds.Contains(Id));
		UsedIds.Add(Id); return Id;
	};
	FEHBWallNodeCopyDraft Draft;
	Draft.BuildingGuid = FreshId();
	for (FGuid Id : ElementGuids) Draft.ElementGuids.Add(Id, FreshId());
	for (const auto& Node : Source.Nodes) Draft.NodeGuids.Add(Node.NodeGuid, FreshId());
	for (const auto& Relation : Relations) Draft.RelationGuids.Add(Relation.RelationGuid, FreshId());
	Draft.Model = Source;
	for (auto& Node : Draft.Model.Nodes) Node.NodeGuid = Draft.NodeGuids.FindChecked(Node.NodeGuid);
	for (auto& Binding : Draft.Model.PillarBindings)
	{
		Binding.NodeGuid = Draft.NodeGuids.FindChecked(Binding.NodeGuid);
		Binding.PhysicalPillarGuid = Draft.ElementGuids.FindChecked(Binding.PhysicalPillarGuid);
	}
	for (auto& Wall : Draft.Model.Walls)
	{
		Wall.WallGuid = Draft.ElementGuids.FindChecked(Wall.WallGuid);
		Wall.StartNodeGuid = Draft.NodeGuids.FindChecked(Wall.StartNodeGuid);
		Wall.EndNodeGuid = Draft.NodeGuids.FindChecked(Wall.EndNodeGuid);
	}
	Draft.Relations = Relations;
	for (auto& Relation : Draft.Relations)
	{
		Relation.RelationGuid = Draft.RelationGuids.FindChecked(Relation.RelationGuid);
		for (auto* Endpoint : { &Relation.Source, &Relation.Target })
		{
			if (Endpoint->Kind == EEHBRelationEndpointKind::WallNode) Endpoint->NodeGuid = Draft.NodeGuids.FindChecked(Endpoint->NodeGuid);
			else Endpoint->ElementGuid = Draft.ElementGuids.FindChecked(Endpoint->ElementGuid);
		}
	}
	if (!FEHBWallNodeRooms::Build(Draft.BuildingGuid, Draft.Model, CopyRooms, Reason)) return Fail(Reason);
	if (CopyRooms.Num() != SourceRooms.Num()) return Fail(TEXT("CopiedRoomCountMismatch"));
	TMap<FString, FGuid> CopyRoomByBoundary;
	for (const auto& Room : CopyRooms)
	{
		const FString Key = RoomKey(Room.FloorIndex, Room.WallGuids);
		if (CopyRoomByBoundary.Contains(Key) || !Room.RoomGuid.IsValid() || UsedIds.Contains(Room.RoomGuid)) return Fail(TEXT("AmbiguousCopiedRoomIdentity"));
		CopyRoomByBoundary.Add(Key, Room.RoomGuid); UsedIds.Add(Room.RoomGuid);
	}
	for (const auto& Room : SourceRooms)
	{
		TArray<FGuid> MappedWalls;
		for (FGuid Id : Room.WallGuids) MappedWalls.Add(Draft.ElementGuids.FindChecked(Id));
		const FString Key = RoomKey(Room.FloorIndex, MoveTemp(MappedWalls));
		const auto* Id = CopyRoomByBoundary.Find(Key);
		if (!Id || Draft.RoomGuids.Contains(Room.RoomGuid)) return Fail(TEXT("AmbiguousSourceRoomIdentity"));
		Draft.RoomGuids.Add(Room.RoomGuid, *Id); CopyRoomByBoundary.Remove(Key);
	}
	if (!CopyRoomByBoundary.IsEmpty()) return Fail(TEXT("UnmappedCopiedRoom"));
	Draft.bSucceeded = true;
	Draft.Status = TEXT("IdentityDraftReady");
	return Draft;
}
