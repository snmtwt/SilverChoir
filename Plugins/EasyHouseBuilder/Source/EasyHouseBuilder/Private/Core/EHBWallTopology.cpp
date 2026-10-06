// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBWallTopology.h"

#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Wall.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBPreparedWallOpening.h"

FEHBTopologyMigrationResult UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(const AEHBBuildingActorBase* Building,FEHBWallNodeModel& Model)
{
 Model={};TSet<FGuid> OpeningWalls;
 if(Building)for(auto* E:Building->QueryElements(FEHBElementQuery()))if(const auto* W=Cast<AEHB_Wall>(E);W&&(!W->CutOperations.IsEmpty()||!W->DoorWindowConnections.IsEmpty()))OpeningWalls.Add(W->ElementGuid);
 FEHBWallNodeModel Candidate;auto Result=CaptureWallNodeModelSource(Building,Candidate,&OpeningWalls);if(!Result.bSucceeded)return Result;
 if(!OpeningWalls.IsEmpty())
 {
  FName Status;TArray<FEHBWallJunctionWallSides> Sides;
  if(!BuildWallNodeModelSides(Candidate,Sides,Status)){Result.bSucceeded=false;Result.Status=Status;return Result;}
  for(FGuid Id:OpeningWalls)
  {
   const auto* Side=Sides.FindByPredicate([&](const auto& V){return V.WallGuid==Id;});const auto* W=Cast<AEHB_Wall>(Building->FindElementActorByGuid(Id));FEHBPreparedWallOpening Prepared;
   if(!Side||!W||!W->PrepareNodeSurfaceOpening(*Side,Prepared,Status)){Result.bSucceeded=false;Result.Status=Side&&W?Status:FName(TEXT("MissingNodeOpeningHost"));return Result;}
  }
 }
 Model=MoveTemp(Candidate);return Result;
}

namespace
{
	bool GuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	void AddIssue(TArray<FEHBWallTopologyIssue>& Issues, FName Code, FGuid Element, const TCHAR* Message)
	{
		FEHBWallTopologyIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Code = Code;
		Issue.ElementGuid = Element;
		Issue.Message = Message;
	}

	void SortIssues(TArray<FEHBWallTopologyIssue>& Issues)
	{
		Issues.Sort([](const FEHBWallTopologyIssue& A, const FEHBWallTopologyIssue& B)
		{
			if (A.ElementGuid != B.ElementGuid) return GuidLess(A.ElementGuid, B.ElementGuid);
			if (A.Code != B.Code) return A.Code.LexicalLess(B.Code);
			return A.Message < B.Message;
		});
	}

	bool IsCanonicalRectangle(const TArray<FVector>& Points, float Width, float Height)
	{
		if (Points.Num() != 4 || !FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width <= 0 || Height <= 0) return false;
		int32 Mask = 0;
		int32 Corners[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector& Point = Points[Index];
			if (Point.ContainsNaN() || !FMath::IsNearlyZero(Point.Y, 0.001)
				|| !FMath::IsNearlyEqual(FMath::Abs(Point.X), Width * 0.5, 0.001)
				|| !(FMath::IsNearlyZero(Point.Z, 0.001) || FMath::IsNearlyEqual(Point.Z, static_cast<double>(Height), 0.001))) return false;
			Corners[Index] = (Point.X > 0 ? 1 : 0) | (Point.Z > Height * 0.5 ? 2 : 0);
			Mask |= 1 << Corners[Index];
		}
		if (Mask != 15) return false;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 ChangedAxes = Corners[Index] ^ Corners[(Index + 1) % 4];
			if (ChangedAxes != 1 && ChangedAxes != 2) return false; // Exclude crossed/bow-tie order.
		}
		return true;
	}
}

FEHBWallSplitPreview UEHBWallTopologyLibrary::PreviewWallSplitForRailing(
	const AEHBBuildingActorBase* Building, FGuid WallGuid, float DistanceFromStart)
{
	FEHBWallSplitPreview Result;
	Result.WallGuid = WallGuid;
	Result.Status = TEXT("InvalidInput");
	if (!Building || !WallGuid.IsValid() || !FMath::IsFinite(DistanceFromStart)) return Result;
	const AEHB_Wall* Wall = Cast<AEHB_Wall>(Building->FindElementActorByGuid(WallGuid));
	if (!Wall || Wall->OwningBuilding != Building || Wall->IsActorBeingDestroyed()) return Result;
	const FTransform BuildingTransform = Building->GetActorTransform();
	if (!BuildingTransform.GetScale3D().Equals(FVector::OneVector)
		|| !Wall->GetActorScale3D().Equals(FVector::OneVector)
		|| !BuildingTransform.GetRotation().GetUpVector().Equals(FVector::UpVector))
	{
		Result.Status = TEXT("UnsupportedTransform");
		return Result;
	}
	if (Wall->GetClass() != AEHB_Wall::StaticClass() || !FMath::IsNearlyZero(Wall->CurveControlOffset)
		|| Wall->LocalStart.ContainsNaN() || Wall->LocalEnd.ContainsNaN()
		|| !FMath::IsNearlyEqual(Wall->LocalStart.Z, Wall->LocalEnd.Z)
		|| !FMath::IsFinite(Wall->Thickness) || Wall->Thickness < 1
		|| !FMath::IsFinite(Wall->Height) || Wall->Height < 1)
	{
		Result.Status = TEXT("UnsupportedWall");
		return Result;
	}
	Result.PillarWidth = Wall->Thickness;
	Result.PillarHeight = Wall->Height;
	const float Length = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	const float HalfWidth = Result.PillarWidth * 0.5f;
	const float Margin = FMath::Max(10.0f, HalfWidth + 1.0f);
	if (DistanceFromStart <= Margin || DistanceFromStart >= Length - Margin)
	{
		Result.Status = TEXT("TooCloseToEndpoint");
		return Result;
	}
	Result.LocalPillarPosition = Wall->GetBuildingLocalLocationOnCenterAxisAtDistance(DistanceFromStart, 0.0f);
	TSet<FGuid> Seen;
	for (const FEHBWallDoorWindowConnection& Opening : Wall->DoorWindowConnections)
	{
		const AEHB_DoorWindow* Actor = Cast<AEHB_DoorWindow>(Building->FindElementActorByGuid(Opening.DoorWindowGuid));
		if (!Opening.DoorWindowGuid.IsValid() || Seen.Contains(Opening.DoorWindowGuid)
			|| !Actor || Actor->IsActorBeingDestroyed() || Actor->OwningBuilding != Building || Actor->OwningWallGuid != WallGuid
			|| !FMath::IsFinite(Opening.DistanceFromStart) || !FMath::IsFinite(Opening.OpeningWidth) || Opening.OpeningWidth <= 0
			|| !FMath::IsNearlyEqual(Actor->DistanceFromWallStart, Opening.DistanceFromStart)
			|| !FMath::IsNearlyEqual(Actor->OpeningWidth, Opening.OpeningWidth)
			|| (!Opening.LocalOutlinePoints.IsEmpty() && !IsCanonicalRectangle(Opening.LocalOutlinePoints, Opening.OpeningWidth, Opening.OpeningHeight)))
		{
			AddIssue(Result.Issues, TEXT("OpeningRequiresInspection"), Opening.DoorWindowGuid, TEXT("Missing, stale, duplicate or custom-outline opening; migration is not planned."));
			continue;
		}
		Seen.Add(Opening.DoorWindowGuid);
		const bool bHasRectangle = !Opening.LocalOutlinePoints.IsEmpty();
		if (bHasRectangle)
		{
			const auto ActorOutline = Actor->GetOpeningOutlineLocalPoints();
			bool bSameOutline = ActorOutline.Num() == Opening.LocalOutlinePoints.Num();
			for (int32 Index = 0; bSameOutline && Index < ActorOutline.Num(); ++Index)
				bSameOutline &= ActorOutline[Index].Equals(Opening.LocalOutlinePoints[Index], 0.001);
			if (Actor->GetClass() != AEHB_DoorWindow::StaticClass() || !bSameOutline
				|| !Actor->GetActorScale3D().Equals(FVector::OneVector)
				|| !Actor->GetActorQuat().Equals(Wall->GetActorQuat(), 0.001)
				|| !FMath::IsFinite(Opening.BottomHeight) || !FMath::IsFinite(Opening.OpeningHeight)
				|| Opening.BottomHeight < 0 || Opening.BottomHeight + Opening.OpeningHeight > Wall->Height
				|| !FMath::IsNearlyEqual(Actor->GetOpeningBottomHeight(), Opening.BottomHeight)
				|| !FMath::IsNearlyEqual(Actor->OpeningHeight, Opening.OpeningHeight)
				|| !Actor->GetActorLocation().Equals(Wall->GetWorldLocationOnCenterAxisAtDistance(Opening.DistanceFromStart, Opening.BottomHeight), 0.001))
			{
				AddIssue(Result.Issues, TEXT("OpeningPoseRequiresInspection"), Opening.DoorWindowGuid, TEXT("Rectangular opening pose, dimensions or stored outline disagree with its wall."));
				continue;
			}
		}
		const float Min = Opening.DistanceFromStart - Opening.OpeningWidth * 0.5f;
		const float Max = Opening.DistanceFromStart + Opening.OpeningWidth * 0.5f;
		if (Min < 0 || Max > Length)
			AddIssue(Result.Issues, TEXT("OpeningOutsideWall"), Opening.DoorWindowGuid, TEXT("Opening exceeds the source wall interval."));
		else if (Max < DistanceFromStart - HalfWidth)
			Result.BeforeOpeningGuids.Add(Opening.DoorWindowGuid);
		else if (Min > DistanceFromStart + HalfWidth)
			Result.AfterOpeningGuids.Add(Opening.DoorWindowGuid);
		else
			Result.ConflictingOpeningGuids.Add(Opening.DoorWindowGuid);
		if (bHasRectangle && (Result.BeforeOpeningGuids.Contains(Opening.DoorWindowGuid) || Result.AfterOpeningGuids.Contains(Opening.DoorWindowGuid)))
		{
			auto& Transfer = Result.OpeningTransfers.AddDefaulted_GetRef();
			Transfer.OpeningGuid = Opening.DoorWindowGuid;
			Transfer.bAfterPillar = Result.AfterOpeningGuids.Contains(Opening.DoorWindowGuid);
			const FVector Direction = (Wall->LocalEnd - Wall->LocalStart).GetSafeNormal2D();
			Transfer.ProposedWallLocalStart = Transfer.bAfterPillar ? Result.LocalPillarPosition + Direction * HalfWidth : Wall->LocalStart;
			Transfer.ProposedWallLocalEnd = Transfer.bAfterPillar ? Wall->LocalEnd : Result.LocalPillarPosition - Direction * HalfWidth;
			Transfer.NewDistanceFromStart = Opening.DistanceFromStart - (Transfer.bAfterPillar ? DistanceFromStart + HalfWidth : 0.0f);
			Transfer.PreservedWorldTransform = Actor->GetActorTransform();
		}
	}
	// Serialized relations are read directly; disabled records also need migration.
	for (const FEHBElementRelation& Relation : Building->ElementRelations)
	{
		if (Relation.Source.RefersToElement(WallGuid) || Relation.Target.RefersToElement(WallGuid))
			Result.RelatedRelationGuids.AddUnique(Relation.RelationGuid);
	}
	Result.BeforeOpeningGuids.Sort(GuidLess);
	Result.AfterOpeningGuids.Sort(GuidLess);
	Result.ConflictingOpeningGuids.Sort(GuidLess);
	Result.RelatedRelationGuids.Sort(GuidLess);
	SortIssues(Result.Issues);
	Result.bSucceeded = Result.Issues.IsEmpty() && Result.ConflictingOpeningGuids.IsEmpty();
	Result.OpeningTransfers.Sort([](const auto& A, const auto& B) { return GuidLess(A.OpeningGuid, B.OpeningGuid); });
	Result.bOpeningTransfersComplete = Result.bSucceeded && Result.OpeningTransfers.Num() == Wall->DoorWindowConnections.Num();
	if (!Result.bSucceeded) Result.OpeningTransfers.Reset();
	Result.Status = !Result.Issues.IsEmpty() ? TEXT("RequiresOpeningInspection")
		: !Result.ConflictingOpeningGuids.IsEmpty() ? TEXT("OpeningIntersectsPillar") : TEXT("RequiresDependencyMigration");
	return Result;
}

TArray<FEHBWallTopologyIssue> UEHBWallTopologyLibrary::ValidateWallTopology(const FEHBWallTopologySnapshot& Snapshot)
{
	TArray<FEHBWallTopologyIssue> Issues;
	TMap<FGuid, const FEHBWallTopologyNode*> Nodes;
	for (const FEHBWallTopologyNode& Node : Snapshot.Nodes)
	{
		if (!Node.NodeGuid.IsValid()) AddIssue(Issues, TEXT("InvalidNodeId"), Node.NodeGuid, TEXT("Node has no stable ID."));
		else if (Nodes.Contains(Node.NodeGuid)) AddIssue(Issues, TEXT("DuplicateNodeId"), Node.NodeGuid, TEXT("Node ID occurs more than once."));
		else Nodes.Add(Node.NodeGuid, &Node);
		if (Node.LocalPosition.ContainsNaN()) AddIssue(Issues, TEXT("InvalidNodePosition"), Node.NodeGuid, TEXT("Node position must be finite."));
	}

	TSet<FGuid> Walls;
	for (const FEHBWallTopologyEdge& Wall : Snapshot.Walls)
	{
		if (!Wall.WallGuid.IsValid()) AddIssue(Issues, TEXT("InvalidWallId"), Wall.WallGuid, TEXT("Wall has no stable ID."));
		else if (Walls.Contains(Wall.WallGuid)) AddIssue(Issues, TEXT("DuplicateWallId"), Wall.WallGuid, TEXT("Wall ID occurs more than once."));
		Walls.Add(Wall.WallGuid);
		const FEHBWallTopologyNode* const* Start = Nodes.Find(Wall.StartNodeGuid);
		const FEHBWallTopologyNode* const* End = Nodes.Find(Wall.EndNodeGuid);
		if (!Start) AddIssue(Issues, TEXT("MissingStartNode"), Wall.WallGuid, TEXT("Wall start is not connected to a known node."));
		if (!End) AddIssue(Issues, TEXT("MissingEndNode"), Wall.WallGuid, TEXT("Wall end is not connected to a known node."));
		if (!Start || !End) continue;
		if (Wall.StartNodeGuid == Wall.EndNodeGuid)
			AddIssue(Issues, TEXT("SelfConnection"), Wall.WallGuid, TEXT("Both endpoints refer to the same node."));
		else if (FVector::DistSquared2D((*Start)->LocalPosition, (*End)->LocalPosition) <= UE_SMALL_NUMBER)
			AddIssue(Issues, TEXT("ZeroPlanarLength"), Wall.WallGuid, TEXT("Wall has no horizontal length."));
		if ((*Start)->FloorIndex > 0 && (*End)->FloorIndex > 0 && (*Start)->FloorIndex != (*End)->FloorIndex)
			AddIssue(Issues, TEXT("CrossFloorConnection"), Wall.WallGuid, TEXT("Wall endpoints belong to different assigned floors."));
	}
	SortIssues(Issues);
	return Issues;
}

TArray<FGuid> UEHBWallTopologyLibrary::GetIncidentWallGuids(const FEHBWallTopologySnapshot& Snapshot, FGuid NodeGuid)
{
	TArray<FGuid> Result;
	if (!NodeGuid.IsValid() || !Snapshot.Nodes.ContainsByPredicate([NodeGuid](const FEHBWallTopologyNode& N) { return N.NodeGuid == NodeGuid; })) return Result;
	for (const FEHBWallTopologyEdge& Wall : Snapshot.Walls)
	{
		if (Wall.WallGuid.IsValid() && (Wall.StartNodeGuid == NodeGuid || Wall.EndNodeGuid == NodeGuid)) Result.AddUnique(Wall.WallGuid);
	}
	Result.Sort(GuidLess);
	return Result;
}

FEHBWallTopologySnapshot UEHBWallTopologyLibrary::CaptureWallTopology(const AEHBBuildingActorBase* Building)
{
	FEHBWallTopologySnapshot Snapshot;
	if (!IsValid(Building))
	{
		AddIssue(Snapshot.Issues, TEXT("MissingBuilding"), FGuid(), TEXT("A valid building is required."));
		return Snapshot;
	}
	Snapshot.BuildingGuid = Building->BuildingGuid;
	const auto& Ownership=Building->WallNodeOwnership;
	if(Ownership.Version!=0&&Ownership.Version!=1){AddIssue(Snapshot.Issues,TEXT("UnsupportedOwnershipVersion"),{},TEXT("Unknown node ownership version."));return Snapshot;}
	if(Ownership.Version==0&&!Ownership.Bindings.IsEmpty()){AddIssue(Snapshot.Issues,TEXT("UnversionedOwnershipData"),{},TEXT("Unversioned node ownership must be repaired explicitly."));return Snapshot;}
	TMap<FGuid,FGuid> OwnedNodesByPillar,OwnedPillarsByNode;TSet<FGuid> OwnedNodeIds;
	for(const auto& Binding:Ownership.Bindings)
	{
		if(!Binding.NodeGuid.IsValid()||!Binding.PhysicalPillarGuid.IsValid()||OwnedNodesByPillar.Contains(Binding.PhysicalPillarGuid)||OwnedNodeIds.Contains(Binding.NodeGuid))
			AddIssue(Snapshot.Issues,TEXT("InvalidNodeOwnership"),Binding.NodeGuid,TEXT("Node ownership bindings must be valid and one-to-one."));
		OwnedNodesByPillar.Add(Binding.PhysicalPillarGuid,Binding.NodeGuid);OwnedNodeIds.Add(Binding.NodeGuid);OwnedPillarsByNode.Add(Binding.NodeGuid,Binding.PhysicalPillarGuid);
	}
	TMap<FGuid, const AEHB_Wall*> SourceWalls;
	const auto& Authority=Building->WallNodeAuthority;
	TMap<FGuid,const FEHBWallNodeDefinition*> AuthoredNodes;
	if(Authority.Version!=0&&!Building->HasWallNodeAuthority()){AddIssue(Snapshot.Issues,TEXT("UnsupportedNodeAuthorityVersion"),{},TEXT("Unknown node pose authority version."));return Snapshot;}
	if(Authority.Version==0&&!Authority.Nodes.IsEmpty()){AddIssue(Snapshot.Issues,TEXT("UnversionedNodeAuthority"),{},TEXT("Unversioned node values require explicit repair."));return Snapshot;}
	if(Building->HasWallNodeAuthority())
	{
		if(Ownership.Version!=1){AddIssue(Snapshot.Issues,TEXT("RequiresTypedNodeOwnership"),{},TEXT("Node pose authority requires typed connection ownership."));return Snapshot;}
		FEHBPreparedWallNodeDefinitions Values;Values.Version=1;Values.Nodes=Authority.Nodes;Values.PillarBindings=Ownership.Bindings;
		Snapshot.Issues.Append(Authority.Version==2?ValidateWallNodeModel(Values):ValidatePreparedWallNodeDefinitions(Values));
		if(!Snapshot.Issues.IsEmpty())return Snapshot;
		for(const auto& N:Authority.Nodes)AuthoredNodes.Add(N.NodeGuid,&N);
	}
	TSet<FGuid> PhysicalSourcesSeen;
	if(Authority.Version==2)
	{
		Snapshot.bLegacyActorBacked=false;
		for(const auto& N:Authority.Nodes){auto& Node=Snapshot.Nodes.AddDefaulted_GetRef();Node.NodeGuid=N.NodeGuid;Node.SourcePillarGuid=OwnedPillarsByNode.FindRef(N.NodeGuid);Node.LocalPosition=N.LocalTransform.GetLocation();Node.FloorIndex=N.FloorIndex;}
	}
	for (const AEHBElementActorBase* Element : Building->QueryElements(FEHBElementQuery()))
	{
		if (!IsValid(Element) || Element->IsActorBeingDestroyed() || Element->OwningBuilding != Building) continue;
		if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Element))
		{
			PhysicalSourcesSeen.Add(Pillar->ElementGuid);
			if(Authority.Version==2)
			{
				if(!OwnedNodesByPillar.Contains(Pillar->ElementGuid))AddIssue(Snapshot.Issues,TEXT("UnownedPhysicalPillar"),Pillar->ElementGuid,TEXT("Every remaining physical pillar needs an explicit node binding."));
				continue;
			}
			FEHBWallTopologyNode& Node = Snapshot.Nodes.AddDefaulted_GetRef();
			Node.NodeGuid = Ownership.Version==1?OwnedNodesByPillar.FindRef(Pillar->ElementGuid):Pillar->ElementGuid;
			Node.SourcePillarGuid = Pillar->ElementGuid;
			Node.LocalPosition = Pillar->GetElementLocalTransform().GetLocation();
			Node.FloorIndex = Pillar->FloorIndex;
			if(Authority.Version==1)
			{
				if(const auto* Authored=AuthoredNodes.FindRef(Node.NodeGuid)){Node.LocalPosition=Authored->LocalTransform.GetLocation();Node.FloorIndex=Authored->FloorIndex;}
				else AddIssue(Snapshot.Issues,TEXT("MissingAuthoredNode"),Node.NodeGuid,TEXT("A physical binding has no authored node values."));
			}
		}
		else if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Element))
		{
			FEHBWallTopologyEdge& Edge = Snapshot.Walls.AddDefaulted_GetRef();
			Edge.WallGuid = Wall->ElementGuid;
			SourceWalls.Add(Wall->ElementGuid, Wall);
		}
	}
	if(Ownership.Version==1&&Ownership.Bindings.Num()!=PhysicalSourcesSeen.Num())AddIssue(Snapshot.Issues,TEXT("InvalidNodeOwnership"),{},TEXT("Every physical binding must resolve to exactly one registered source."));
	Snapshot.Nodes.Sort([](const FEHBWallTopologyNode& A, const FEHBWallTopologyNode& B) { return GuidLess(A.NodeGuid, B.NodeGuid); });
	Snapshot.Walls.Sort([](const FEHBWallTopologyEdge& A, const FEHBWallTopologyEdge& B) { return GuidLess(A.WallGuid, B.WallGuid); });
	TMap<FGuid, int32> WallIndices;
	for (int32 Index = 0; Index < Snapshot.Walls.Num(); ++Index) WallIndices.Add(Snapshot.Walls[Index].WallGuid, Index);

	// Read authoritative records directly: a stale relation index must not hide
	// the very malformed/ambiguous data this diagnostic adapter is meant to expose.
	TArray<FEHBElementRelation> Relations;
	for (const FEHBElementRelation& Relation : Building->ElementRelations)
	{
		if (Relation.bEnabled && Relation.Type == EEHBElementRelationType::TopologyConnection) Relations.Add(Relation);
	}
	Relations.Sort([](const FEHBElementRelation& A, const FEHBElementRelation& B) { return GuidLess(A.RelationGuid, B.RelationGuid); });
	TSet<FGuid> AssignedStarts;
	TSet<FGuid> AssignedEnds;
	for (const FEHBElementRelation& Relation : Relations)
	{
		const int32* Index = WallIndices.Find(Relation.Source.ElementGuid);
		if (!Index)
		{
			AddIssue(Snapshot.Issues, TEXT("UnknownTopologyWall"), Relation.Source.ElementGuid, TEXT("Active topology relation does not reference a registered wall."));
			continue;
		}
		if (Relation.Source.Kind != EEHBRelationEndpointKind::BuildingElement || !Relation.Target.IsValid()
			|| !(Ownership.Version==0?Relation.Target.Kind==EEHBRelationEndpointKind::BuildingElement:(Relation.Target.Kind==EEHBRelationEndpointKind::WallNode&&(Authority.Version==2?AuthoredNodes.Contains(Relation.Target.NodeGuid):OwnedPillarsByNode.Contains(Relation.Target.NodeGuid)))))
		{
			AddIssue(Snapshot.Issues, TEXT("InvalidEndpointKind"), Relation.Source.ElementGuid, TEXT("Wall topology endpoints must refer to building elements."));
			continue;
		}
		const bool bStart = Relation.Source.SurfaceKind == EEHBElementSurfaceKind::Start;
		if (!bStart && Relation.Source.SurfaceKind != EEHBElementSurfaceKind::End)
		{
			AddIssue(Snapshot.Issues, TEXT("InvalidWallPort"), Relation.Source.ElementGuid, TEXT("Wall topology source must identify Start or End."));
			continue;
		}
		FEHBWallTopologyEdge& Edge = Snapshot.Walls[*Index];
		TSet<FGuid>& Assigned = bStart ? AssignedStarts : AssignedEnds;
		if (Assigned.Contains(Edge.WallGuid))
		{
			AddIssue(Snapshot.Issues, TEXT("AmbiguousWallPort"), Edge.WallGuid, TEXT("Multiple active relations assign the same wall endpoint. Repair explicitly."));
			continue;
		}
		Assigned.Add(Edge.WallGuid);
		(bStart ? Edge.StartNodeGuid : Edge.EndNodeGuid) = Ownership.Version==1?Relation.Target.NodeGuid:Relation.Target.ElementGuid;
	}
	for (const FEHBWallTopologyEdge& Edge : Snapshot.Walls)
	{
		const AEHB_Wall* Wall = SourceWalls.FindChecked(Edge.WallGuid);
		if (Wall->StartPillarGuid != (Ownership.Version==1?OwnedPillarsByNode.FindRef(Edge.StartNodeGuid):Edge.StartNodeGuid) || Wall->EndPillarGuid != (Ownership.Version==1?OwnedPillarsByNode.FindRef(Edge.EndNodeGuid):Edge.EndNodeGuid))
			AddIssue(Snapshot.Issues, TEXT("LegacyEndpointMismatch"), Edge.WallGuid, TEXT("Legacy wall endpoint fields disagree with active topology relations. Snapshot did not repair them."));
	}
	Snapshot.Issues.Append(ValidateWallTopology(Snapshot));
	SortIssues(Snapshot.Issues);
	return Snapshot;
}

FEHBTopologyMigrationResult UEHBWallTopologyLibrary::PrepareTopologyMigration(AEHBBuildingActorBase* Building, bool bApply)
{
	FEHBTopologyMigrationResult Result;
	const FEHBWallTopologySnapshot Current = CaptureWallTopology(Building);
	Result.Issues = Current.Issues;
	Result.NodeCount = Current.Nodes.Num();
	Result.WallCount = Current.Walls.Num();
	if (!Building || !Result.Issues.IsEmpty())
	{
		Result.Status = TEXT("InvalidSource");
		return Result;
	}
	const FEHBPersistedWallTopology& Stored = Building->TopologyMigrationBaseline;
	if (Stored.Version != 0 && Stored.Version != 1)
	{
		Result.Status = TEXT("UnsupportedVersion");
		return Result;
	}
	if (Stored.Version == 1)
	{
		FEHBWallTopologySnapshot Persisted;
		Persisted.bLegacyActorBacked=Building->WallNodeAuthority.Version!=2;
		Persisted.Nodes = Stored.Nodes;
		Persisted.Walls = Stored.Walls;
		Result.Issues = ValidateWallTopology(Persisted);
		if (!Result.Issues.IsEmpty())
		{
			Result.Status = TEXT("InvalidBaseline");
			return Result;
		}
		// Local positions allow 0.0001 cm of transform/text-import roundoff, not identity changes.
		// Compare identities and values, not array order or a graph revision that
		// does not track every legacy geometry edit. Never silently refresh IDs.
		TMap<FGuid, const FEHBWallTopologyNode*> CurrentNodes;
		TMap<FGuid, const FEHBWallTopologyEdge*> CurrentWalls;
		for (const auto& Node : Current.Nodes) CurrentNodes.Add(Node.NodeGuid, &Node);
		for (const auto& Wall : Current.Walls) CurrentWalls.Add(Wall.WallGuid, &Wall);
		bool bSame = Stored.Nodes.Num() == Current.Nodes.Num() && Stored.Walls.Num() == Current.Walls.Num();
		for (const FEHBWallTopologyNode& Node : Stored.Nodes)
		{
			const auto* Other = CurrentNodes.FindRef(Node.NodeGuid);
			bSame &= Other && Other->SourcePillarGuid == Node.SourcePillarGuid && Other->FloorIndex == Node.FloorIndex && Other->LocalPosition.Equals(Node.LocalPosition, 0.0001);
		}
		for (const FEHBWallTopologyEdge& Wall : Stored.Walls)
		{
			const auto* Other = CurrentWalls.FindRef(Wall.WallGuid);
			bSame &= Other && Other->StartNodeGuid == Wall.StartNodeGuid && Other->EndNodeGuid == Wall.EndNodeGuid;
		}
		Result.bSucceeded = bSame;
		Result.Status = bSame ? TEXT("AlreadyInitialized") : TEXT("SourceChanged");
		return Result;
	}
	if (!Stored.Nodes.IsEmpty() || !Stored.Walls.IsEmpty())
	{
		Result.Status = TEXT("UnversionedData");
		return Result;
	}
	if (Current.Nodes.IsEmpty())
	{
		Result.Status = TEXT("EmptyTopology");
		return Result;
	}
	Result.bSucceeded = true;
	Result.Status = bApply ? TEXT("Initialized") : TEXT("Ready");
	if (bApply)
	{
		Building->Modify();
		Building->TopologyMigrationBaseline.Nodes = Current.Nodes;
		Building->TopologyMigrationBaseline.Walls = Current.Walls;
		Building->TopologyMigrationBaseline.Version = 1;
		Building->MarkPackageDirty();
		Result.bChanged = true;
	}
	return Result;
}

FEHBNodeMovePreview UEHBWallTopologyLibrary::PreviewNodeMove(const AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition)
{
	FEHBNodeMoveRequest Request;
	Request.NodeGuid = NodeGuid; Request.ExpectedPosition = ExpectedPosition; Request.TargetPosition = TargetPosition;
	return PreviewNodeMoves(Building, { Request });
}

TArray<FEHBWallTopologyIssue> UEHBWallTopologyLibrary::ValidateWallNodeModel(const FEHBWallNodeModel& Data)
{
	TArray<FEHBWallTopologyIssue> Issues;
	if(Data.Version!=1){AddIssue(Issues,TEXT("UnsupportedDefinitionVersion"),{},TEXT("Prepared node definition version must be 1."));return Issues;}
	// An explicitly versioned empty model is a valid new optional-node building.
	// Stray walls or bindings still fail the referential checks below.
	TMap<FGuid,const FEHBWallNodeDefinition*> Nodes;TSet<FGuid> Walls,BoundNodes,Pillars;
	for(const auto& N:Data.Nodes)
	{
		if(!N.NodeGuid.IsValid()||Nodes.Contains(N.NodeGuid)){AddIssue(Issues,TEXT("InvalidNodeIdentity"),N.NodeGuid,TEXT("Node identities must be valid and unique."));continue;}
		Nodes.Add(N.NodeGuid,&N);
		if(!N.LocalTransform.IsValid()||!N.LocalTransform.GetScale3D().Equals(FVector::OneVector,0.0001)||!N.LocalTransform.GetRotation().GetUpVector().Equals(FVector::UpVector,0.000001)
			||N.FloorIndex<1||N.GeometryRevision<1||N.JunctionDimensions.ContainsNaN()||N.JunctionDimensions.GetMin()<=0)
			AddIssue(Issues,TEXT("InvalidNodeDefinition"),N.NodeGuid,TEXT("Node pose, level, dimensions and geometry revision must be valid."));
	}
	for(const auto& B:Data.PillarBindings)
	{
		if(!Nodes.Contains(B.NodeGuid)||!B.PhysicalPillarGuid.IsValid()||BoundNodes.Contains(B.NodeGuid)||Pillars.Contains(B.PhysicalPillarGuid))
			AddIssue(Issues,TEXT("InvalidPillarBinding"),B.NodeGuid,TEXT("Prepared physical bindings must be unique and refer to a defined node."));
		BoundNodes.Add(B.NodeGuid);Pillars.Add(B.PhysicalPillarGuid);
	}

	for(const auto& W:Data.Walls)
	{
		if(!W.WallGuid.IsValid()||Walls.Contains(W.WallGuid))AddIssue(Issues,TEXT("InvalidWallIdentity"),W.WallGuid,TEXT("Wall identities must be valid and unique."));
		Walls.Add(W.WallGuid);const auto* A=Nodes.FindRef(W.StartNodeGuid);const auto* B=Nodes.FindRef(W.EndNodeGuid);
		if(!A||!B||A==B){AddIssue(Issues,TEXT("InvalidWallEndpoints"),W.WallGuid,TEXT("Wall endpoints must refer to distinct defined nodes."));continue;}
		if(!FMath::IsFinite(W.Thickness)||!FMath::IsFinite(W.Height)||W.Thickness<=0||W.Height<=0
			||!FMath::IsNearlyEqual(A->LocalTransform.GetLocation().Z,B->LocalTransform.GetLocation().Z,0.001)
			||FVector::DistSquared2D(A->LocalTransform.GetLocation(),B->LocalTransform.GetLocation())<=1)
			AddIssue(Issues,TEXT("InvalidWallDefinition"),W.WallGuid,TEXT("Prepared straight wall dimensions/span must be valid and horizontal."));
	}
	return Issues;
}

TArray<FEHBWallTopologyIssue> UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Data)
{
	auto Issues=ValidateWallNodeModel(Data);if(Data.Version!=1)return Issues;
	if(Data.Nodes.IsEmpty())AddIssue(Issues,TEXT("EmptyNodeDefinitions"),{},TEXT("Prepared definitions require at least one source node."));
	TSet<FGuid> BoundNodes;for(const auto& B:Data.PillarBindings)BoundNodes.Add(B.NodeGuid);
	for(const auto& N:Data.Nodes)if(!BoundNodes.Contains(N.NodeGuid))AddIssue(Issues,TEXT("MissingPreparedPillarBinding"),N.NodeGuid,TEXT("Preparation must retain every source physical pillar."));
	return Issues;
}

FEHBTopologyMigrationResult UEHBWallTopologyLibrary::CaptureWallNodeModelSource(const AEHBBuildingActorBase* Building,FEHBWallNodeModel& Definitions,const TSet<FGuid>* SeparatelyValidatedOpeningWalls)
{
	Definitions={};FEHBTopologyMigrationResult Result;
	if(!Building){Result.Status=TEXT("InvalidSource");return Result;}
	if(!Building->GetActorScale3D().Equals(FVector::OneVector,0.0001)||!FMath::IsNearlyZero(Building->GetActorRotation().Pitch)||!FMath::IsNearlyZero(Building->GetActorRotation().Roll)){Result.Status=TEXT("UnsupportedBuildingTransform");return Result;}
	const auto Graph=CaptureWallTopology(Building);if(!Graph.Issues.IsEmpty()){Result.Status=TEXT("InvalidSource");Result.Issues=Graph.Issues;return Result;}const auto Elements=Building->QueryElements(FEHBElementQuery());
	if(SeparatelyValidatedOpeningWalls)for(FGuid Id:*SeparatelyValidatedOpeningWalls)if(!Id.IsValid()||!Graph.Walls.ContainsByPredicate([&](const auto& W){return W.WallGuid==Id;})){Result.Status=TEXT("UnknownDeferredOpeningWall");return Result;}
	FEHBWallNodeModel Candidate;Candidate.Version=1;
	for(const auto& Node:Graph.Nodes)
	{
		if(Building->WallNodeAuthority.Version==2&&!Node.SourcePillarGuid.IsValid())
		{
			const auto* Authored=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node.NodeGuid;});
			if(!Authored){Result.Status=TEXT("MissingAuthoredNode");return Result;}Candidate.Nodes.Add(*Authored);continue;
		}
		const auto* Found=Elements.FindByPredicate([&](const auto* E){return E->ElementGuid==Node.SourcePillarGuid;});
		const auto* Pillar=Found?Cast<AEHB_Pillar>(*Found):nullptr;
		if(!Pillar||Pillar->GetClass()!=AEHB_Pillar::StaticClass()||Pillar->ShapeType!=EEHBPillarShapeType::Polygon||!Pillar->CutOperations.IsEmpty()||!Pillar->ConnectedSurfaceOverrides.IsEmpty())
		{Result.Status=TEXT("UnsupportedNodeSource");return Result;}
		auto& N=Candidate.Nodes.AddDefaulted_GetRef();N.NodeGuid=Node.NodeGuid;N.LocalTransform=Pillar->GetElementLocalTransform();N.FloorIndex=Node.FloorIndex;N.JunctionDimensions=FVector(Pillar->Width,Pillar->Depth,Pillar->Height);
		if(Building->HasWallNodeAuthority())
		{
			const auto* Authored=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node.NodeGuid;});
			if(!Authored||!Authored->LocalTransform.Equals(N.LocalTransform,0.000001)||Authored->FloorIndex!=Pillar->FloorIndex||Authored->JunctionDimensions!=N.JunctionDimensions)
			{Result.Status=TEXT("StalePhysicalNodeProxy");return Result;}
			N=*Authored;
		}
		auto& B=Candidate.PillarBindings.AddDefaulted_GetRef();B.NodeGuid=Node.NodeGuid;B.PhysicalPillarGuid=Pillar->ElementGuid;
	}
	for(const auto& Edge:Graph.Walls)
	{
		const auto* Found=Elements.FindByPredicate([&](const auto* E){return E->ElementGuid==Edge.WallGuid;});const auto* Wall=Found?Cast<AEHB_Wall>(*Found):nullptr;
		if(!Wall||Wall->GetClass()!=AEHB_Wall::StaticClass()||Wall->CurveControlOffset!=0||((!SeparatelyValidatedOpeningWalls||!SeparatelyValidatedOpeningWalls->Contains(Edge.WallGuid))&&(!Wall->CutOperations.IsEmpty()||!Wall->DoorWindowConnections.IsEmpty()))
			||Wall->LeftSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple||Wall->RightSurfaceStyle.SourceType!=EEHBWallSurfaceSourceType::Simple
			||!Wall->GetElementLocalTransform().GetScale3D().Equals(FVector::OneVector,0.0001)){Result.Status=TEXT("UnsupportedWallSource");return Result;}
		auto& W=Candidate.Walls.AddDefaulted_GetRef();W.WallGuid=Edge.WallGuid;W.StartNodeGuid=Edge.StartNodeGuid;W.EndNodeGuid=Edge.EndNodeGuid;W.Thickness=Wall->Thickness;W.Height=Wall->Height;
	}
	Result.NodeCount=Candidate.Nodes.Num();Result.WallCount=Candidate.Walls.Num();Result.Issues=ValidateWallNodeModel(Candidate);
	if(!Result.Issues.IsEmpty()){Result.Status=TEXT("InvalidDefinitionSource");return Result;}

	Definitions=MoveTemp(Candidate);Result.bSucceeded=true;Result.Status=TEXT("Ready");return Result;
}

FEHBTopologyMigrationResult UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(const AEHBBuildingActorBase* Building,FEHBPreparedWallNodeDefinitions& Definitions,const TSet<FGuid>* SeparatelyValidatedOpeningWalls)
{
 Definitions={};FEHBWallNodeModel Model;auto Result=CaptureWallNodeModelSource(Building,Model,SeparatelyValidatedOpeningWalls);if(!Result.bSucceeded)return Result;
 FEHBPreparedWallNodeDefinitions Prepared;static_cast<FEHBWallNodeModel&>(Prepared)=MoveTemp(Model);Result.Issues=ValidatePreparedWallNodeDefinitions(Prepared);
 if(!Result.Issues.IsEmpty()){Result.bSucceeded=false;Result.Status=TEXT("RequiresPhysicalNodeEditingAdapter");return Result;}
 Definitions=MoveTemp(Prepared);return Result;
}

FEHBTopologyMigrationResult UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(AEHBBuildingActorBase* Building,bool bApply)
{
	FEHBTopologyMigrationResult Result;
	const auto Baseline=PrepareTopologyMigration(Building,false);
	if(!Baseline.bSucceeded||Baseline.Status!=TEXT("AlreadyInitialized"))
	{Result=Baseline;Result.bSucceeded=false;if(Baseline.Status==TEXT("Ready"))Result.Status=TEXT("BaselineMigrationRequired");return Result;}
	FEHBPreparedWallNodeDefinitions Candidate;
	Result=CaptureNodeDefinitionSource(Building,Candidate);if(!Result.bSucceeded)return Result;
	Result.bSucceeded=false; // Capture success does not validate an existing prepared payload.
	const auto& Stored=Building->PreparedWallNodeDefinitions;
	if(Stored.Version!=0)
	{
		Result.Issues=ValidatePreparedWallNodeDefinitions(Stored);if(!Result.Issues.IsEmpty()){Result.Status=TEXT("InvalidPreparedDefinitions");return Result;}
		bool Same=Stored.Nodes.Num()==Candidate.Nodes.Num()&&Stored.Walls.Num()==Candidate.Walls.Num()&&Stored.PillarBindings.Num()==Candidate.PillarBindings.Num();
		for(const auto& N:Stored.Nodes){const auto* C=Candidate.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});Same &= C&&N.LocalTransform.Equals(C->LocalTransform,0.000001)&&N.FloorIndex==C->FloorIndex&&N.JunctionDimensions==C->JunctionDimensions&&N.GeometryRevision==C->GeometryRevision;}
		for(const auto& B:Stored.PillarBindings){const auto* C=Candidate.PillarBindings.FindByPredicate([&](const auto& V){return V.NodeGuid==B.NodeGuid;});Same &= C&&B.PhysicalPillarGuid==C->PhysicalPillarGuid;}
		for(const auto& W:Stored.Walls){const auto* C=Candidate.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W.WallGuid;});Same &= C&&W.StartNodeGuid==C->StartNodeGuid&&W.EndNodeGuid==C->EndNodeGuid&&W.Thickness==C->Thickness&&W.Height==C->Height;}
		Result.bSucceeded=Same;Result.Status=Same?TEXT("AlreadyPrepared"):TEXT("DefinitionSourceChanged");return Result;
	}
	if(!Stored.Nodes.IsEmpty()||!Stored.Walls.IsEmpty()||!Stored.PillarBindings.IsEmpty()){Result.Status=TEXT("UnversionedDefinitions");return Result;}
	Result.bSucceeded=true;Result.Status=bApply?TEXT("Prepared"):TEXT("Ready");
	if(bApply){Building->Modify();Building->PreparedWallNodeDefinitions=MoveTemp(Candidate);Building->MarkPackageDirty();Result.bChanged=true;}
	return Result;
}

bool UEHBWallTopologyLibrary::BuildPreparedWallSides(const FEHBPreparedWallNodeDefinitions& Data,TArray<FEHBWallJunctionWallSides>& Sides,FName& Reason,const TSet<FGuid>* RequestedWallGuids,FEHBWallJunctionSolveStats* Stats)
{
	Sides.Reset();Reason=NAME_None;if(Stats)*Stats={};
	const auto Issues=ValidatePreparedWallNodeDefinitions(Data);if(!Issues.IsEmpty()){Reason=Issues[0].Code;return false;}
	return BuildWallNodeModelSides(Data,Sides,Reason,RequestedWallGuids,Stats);
}

bool UEHBWallTopologyLibrary::BuildWallNodeModelSides(const FEHBWallNodeModel& Data,TArray<FEHBWallJunctionWallSides>& Sides,FName& Reason,const TSet<FGuid>* RequestedWallGuids,FEHBWallJunctionSolveStats* Stats,TMap<FGuid,TArray<FVector>>* NodeFootprints)
{
	Sides.Reset();Reason=NAME_None;if(Stats)*Stats={};if(NodeFootprints)NodeFootprints->Reset();
	const auto Issues=ValidateWallNodeModel(Data);if(!Issues.IsEmpty()){Reason=Issues[0].Code;return false;}
	TArray<FEHBWallJunctionNodeInput> Nodes;TArray<FEHBWallJunctionWallInput> Walls;
	TSet<FGuid> BoundNodes;for(const auto& Binding:Data.PillarBindings)BoundNodes.Add(Binding.NodeGuid);
	for(const auto& N:Data.Nodes){auto& Input=Nodes.AddDefaulted_GetRef();Input.NodeGuid=N.NodeGuid;Input.LocalTransform=N.LocalTransform;Input.Width=N.JunctionDimensions.X;Input.Depth=N.JunctionDimensions.Y;Input.bExactWallThickness=!BoundNodes.Contains(N.NodeGuid);}
	for(const auto& W:Data.Walls){auto& Input=Walls.AddDefaulted_GetRef();Input.WallGuid=W.WallGuid;Input.StartNodeGuid=W.StartNodeGuid;Input.EndNodeGuid=W.EndNodeGuid;Input.Thickness=W.Thickness;Input.Height=W.Height;}
	return FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Sides,Reason,RequestedWallGuids,Stats,NodeFootprints);
}

FEHBWallNodeMoveDraft UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(const FEHBPreparedWallNodeDefinitions& Source,const TArray<FEHBNodeMoveRequest>& Requests,const TArray<FEHBNodeRotationRequest>& Rotations)
{
	FEHBWallNodeMoveDraft Result;
	const auto Issues=ValidatePreparedWallNodeDefinitions(Source);if(!Issues.IsEmpty()){Result.Status=Issues[0].Code;return Result;}
	auto Draft=BuildWallNodeModelMoveDraft(Source,Requests,Rotations);
	Result.bSucceeded=Draft.bSucceeded;Result.bWouldChange=Draft.bWouldChange;Result.Status=Draft.Status;
	static_cast<FEHBWallNodeModel&>(Result.Definitions)=MoveTemp(Draft.Definitions);Result.UpdatePlan=MoveTemp(Draft.UpdatePlan);return Result;
}

FEHBWallNodeModelEditDraft UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(const FEHBWallNodeModel& Source,const TArray<FEHBNodeMoveRequest>& Requests,const TArray<FEHBNodeRotationRequest>& Rotations)
{
	FEHBWallNodeModelEditDraft Result;
	auto Fail=[&](FName Status){Result={};Result.Status=Status;return Result;};
	if(Requests.Num()>2048 || Rotations.Num()>2048)return Fail(TEXT("InvalidBatchSize"));
	const auto Issues=ValidateWallNodeModel(Source);if(!Issues.IsEmpty())return Fail(Issues[0].Code);
	TMap<FGuid,int32> NodeIndices;FEHBWallTopologySnapshot Snapshot;Snapshot.bLegacyActorBacked=false;
	for(int32 I=0;I<Source.Nodes.Num();++I){const auto& N=Source.Nodes[I];NodeIndices.Add(N.NodeGuid,I);auto& Node=Snapshot.Nodes.AddDefaulted_GetRef();Node.NodeGuid=N.NodeGuid;Node.LocalPosition=N.LocalTransform.GetLocation();Node.FloorIndex=N.FloorIndex;}
	for(const auto& W:Source.Walls){auto& Wall=Snapshot.Walls.AddDefaulted_GetRef();Wall.WallGuid=W.WallGuid;Wall.StartNodeGuid=W.StartNodeGuid;Wall.EndNodeGuid=W.EndNodeGuid;}
	TSet<FGuid> Requested,Changed;
	for(const auto& Request:Requests)
	{
		if(Requested.Contains(Request.NodeGuid))return Fail(TEXT("DuplicateNodeRequest"));Requested.Add(Request.NodeGuid);
		const int32* Index=NodeIndices.Find(Request.NodeGuid);if(!Index)return Fail(TEXT("UnknownNode"));
		const FVector Position=Source.Nodes[*Index].LocalTransform.GetLocation();
		if(Request.ExpectedPosition.ContainsNaN()||Request.TargetPosition.ContainsNaN())return Fail(TEXT("InvalidPosition"));
		if(!Position.Equals(Request.ExpectedPosition,0.001))return Fail(TEXT("StalePosition"));
		if(Request.TargetPosition.Z!=Position.Z)return Fail(TEXT("VerticalMoveUnsupported"));
		if(Request.TargetPosition!=Position)Changed.Add(Request.NodeGuid);
	}
	TMap<FGuid,FQuat> ChangedRotations;
	TSet<FGuid> RotationNodes;
	for(const auto& Rotation:Rotations)
	{
		if(RotationNodes.Contains(Rotation.NodeGuid))return Fail(TEXT("DuplicateRotationRequest"));
		RotationNodes.Add(Rotation.NodeGuid);
		const int32* Index=NodeIndices.Find(Rotation.NodeGuid);if(!Index)return Fail(TEXT("UnknownNode"));
		if(!Requested.Contains(Rotation.NodeGuid))return Fail(TEXT("RotationRequiresPositionRequest"));
		const FQuat Expected=Rotation.ExpectedLocalRotation,Target=Rotation.TargetLocalRotation;
		if(Expected.ContainsNaN() || Target.ContainsNaN() || !Expected.IsNormalized() || !Target.IsNormalized())return Fail(TEXT("InvalidRotation"));
		if(!Target.GetUpVector().Equals(FVector::UpVector,0.000001))return Fail(TEXT("TiltRotationUnsupported"));
		const FQuat Current=Source.Nodes[*Index].LocalTransform.GetRotation();
		if(!Current.Equals(Expected,0.000001))return Fail(TEXT("StaleRotation"));
		// q and -q describe the same orientation; preserve the source representation on no-op.
		if(!Current.Equals(Target,0.00000001)){Changed.Add(Rotation.NodeGuid);ChangedRotations.Add(Rotation.NodeGuid,Target);}
	}
	Result.UpdatePlan=BuildWallMoveUpdatePlan(Snapshot,Changed.Array());if(!Result.UpdatePlan.bSucceeded)return Fail(Result.UpdatePlan.Status);
	for(const FGuid Id:Result.UpdatePlan.JunctionNodeGuids)if(Source.Nodes[NodeIndices.FindChecked(Id)].GeometryRevision==MAX_int32)return Fail(TEXT("GeometryRevisionOverflow"));
	Result.Definitions=Source;
	for(const auto& Request:Requests)Result.Definitions.Nodes[NodeIndices.FindChecked(Request.NodeGuid)].LocalTransform.SetLocation(Request.TargetPosition);
	for(const auto& Rotation:ChangedRotations)Result.Definitions.Nodes[NodeIndices.FindChecked(Rotation.Key)].LocalTransform.SetRotation(Rotation.Value);
	for(const FGuid Id:Result.UpdatePlan.JunctionNodeGuids)++Result.Definitions.Nodes[NodeIndices.FindChecked(Id)].GeometryRevision;
	const auto FinalIssues=ValidateWallNodeModel(Result.Definitions);if(!FinalIssues.IsEmpty())return Fail(FinalIssues[0].Code);
	Result.bSucceeded=true;Result.bWouldChange=!Changed.IsEmpty();Result.Status=Result.bWouldChange?TEXT("Ready"):TEXT("NoChange");
	return Result;
}

bool UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(const FEHBPreparedWallNodeDefinitions& Data,FGuid NodeGuid,FEHBWallJunctionMesh& Mesh,FName& Reason)
{
	Mesh={};Reason=NAME_None;const auto Issues=ValidatePreparedWallNodeDefinitions(Data);if(!Issues.IsEmpty()){Reason=Issues[0].Code;return false;}
	return BuildWallNodeModelJunctionMesh(Data,NodeGuid,Mesh,Reason);
}

bool UEHBWallTopologyLibrary::BuildWallNodeModelJunctionMesh(const FEHBWallNodeModel& Data,FGuid NodeGuid,FEHBWallJunctionMesh& Mesh,FName& Reason)
{
	Mesh={};Reason=NAME_None;
	const auto Issues=ValidateWallNodeModel(Data);if(!Issues.IsEmpty()){Reason=Issues[0].Code;return false;}
	const auto* Node=Data.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeGuid;});if(!Node){Reason=TEXT("UnknownNode");return false;}
	TSet<FGuid> Requested;TArray<FEHBWallJunctionLeg> Legs;
	for(const auto& W:Data.Walls)if(W.StartNodeGuid==NodeGuid||W.EndNodeGuid==NodeGuid)
	{
		Requested.Add(W.WallGuid);const FGuid Other=W.StartNodeGuid==NodeGuid?W.EndNodeGuid:W.StartNodeGuid;
		const auto* End=Data.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Other;});
		auto& Leg=Legs.AddDefaulted_GetRef();Leg.Direction=Node->LocalTransform.InverseTransformVectorNoScale(End->LocalTransform.GetLocation()-Node->LocalTransform.GetLocation()).GetSafeNormal2D();Leg.WallThickness=W.Thickness;
	}
	if(Requested.IsEmpty()){Reason=TEXT("NodeHasNoWalls");return false;}
	TArray<FEHBWallJunctionWallSides> Sides;if(!BuildWallNodeModelSides(Data,Sides,Reason,&Requested))return false;
	TArray<FVector> Footprint;
	if(!FEHBWallJunctionGeometry::BuildFootprint(Node->JunctionDimensions.X,Node->JunctionDimensions.Y,Legs,Footprint,!Data.PillarBindings.ContainsByPredicate([&](const auto& B){return B.NodeGuid==NodeGuid;}))
		||!FEHBWallJunctionMeshBuilder::BuildPrism(Footprint,Node->JunctionDimensions.Z,Mesh)){Reason=TEXT("UnsupportedJunctionFootprint");return false;}
	Mesh.NodeGuid=NodeGuid;Mesh.LocalTransform=Node->LocalTransform;Reason=TEXT("Ready");return true;
}

FEHBWallNodeModelEditDraft UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(const FEHBWallNodeModel& Source,FGuid NodeGuid,FGuid ExpectedPhysicalPillarGuid,int32 ExpectedNodeRevision)
{
	FEHBWallNodeModelEditDraft Result;auto Fail=[&](FName Status){Result={};Result.Status=Status;return Result;};
	const auto Issues=ValidateWallNodeModel(Source);if(!Issues.IsEmpty())return Fail(Issues[0].Code);
	const auto* Node=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeGuid;});if(!Node)return Fail(TEXT("UnknownNode"));
	if(Node->GeometryRevision!=ExpectedNodeRevision)return Fail(TEXT("StaleNodeRevision"));
	const auto* Binding=Source.PillarBindings.FindByPredicate([&](const auto& B){return B.NodeGuid==NodeGuid;});
	if(!Binding){Result.bSucceeded=true;Result.Status=TEXT("NoChange");Result.Definitions=Source;Result.UpdatePlan.bSucceeded=true;Result.UpdatePlan.Status=TEXT("NoChange");return Result;}
	if(!ExpectedPhysicalPillarGuid.IsValid() || Binding->PhysicalPillarGuid!=ExpectedPhysicalPillarGuid)return Fail(TEXT("StalePhysicalBinding"));
	FEHBWallTopologySnapshot Graph;Graph.bLegacyActorBacked=false;
	for(const auto& N:Source.Nodes){auto& G=Graph.Nodes.AddDefaulted_GetRef();G.NodeGuid=N.NodeGuid;G.LocalPosition=N.LocalTransform.GetLocation();G.FloorIndex=N.FloorIndex;}
	for(const auto& W:Source.Walls){auto& G=Graph.Walls.AddDefaulted_GetRef();G.WallGuid=W.WallGuid;G.StartNodeGuid=W.StartNodeGuid;G.EndNodeGuid=W.EndNodeGuid;}
	Result.UpdatePlan=BuildWallMoveUpdatePlan(Graph,{NodeGuid});if(!Result.UpdatePlan.bSucceeded)return Fail(Result.UpdatePlan.Status);
	for(const auto& N:Source.Nodes)if(Result.UpdatePlan.JunctionNodeGuids.Contains(N.NodeGuid) && N.GeometryRevision==MAX_int32)return Fail(TEXT("GeometryRevisionOverflow"));
	Result.Definitions=Source;Result.Definitions.PillarBindings.RemoveAll([&](const auto& B){return B.NodeGuid==NodeGuid;});Result.RemovedPhysicalPillarGuids.Add(Binding->PhysicalPillarGuid);
	float Thickness=0,Height=0;for(const auto& W:Source.Walls)if(W.StartNodeGuid==NodeGuid || W.EndNodeGuid==NodeGuid){Thickness=FMath::Max(Thickness,W.Thickness);Height=FMath::Max(Height,W.Height);}
	for(auto& N:Result.Definitions.Nodes)
	{
		if(Result.UpdatePlan.JunctionNodeGuids.Contains(N.NodeGuid))++N.GeometryRevision;
		if(N.NodeGuid==NodeGuid && Thickness>0)N.JunctionDimensions=FVector(Thickness,Thickness,Height);
	}
	// Prove the replacement geometry can be generated before returning a usable value draft.
	TSet<FGuid> Requested;for(FGuid Id:Result.UpdatePlan.WallGuids)Requested.Add(Id);TArray<FEHBWallJunctionWallSides> Sides;FName Reason;
	if(!BuildWallNodeModelSides(Result.Definitions,Sides,Reason,&Requested))return Fail(Reason);
	for(FGuid Id:Result.UpdatePlan.JunctionNodeGuids)
		if(Source.Walls.ContainsByPredicate([&](const auto& W){return W.StartNodeGuid==Id||W.EndNodeGuid==Id;}))
		{FEHBWallJunctionMesh Mesh;if(!BuildWallNodeModelJunctionMesh(Result.Definitions,Id,Mesh,Reason))return Fail(Reason);}
	Result.bSucceeded=true;Result.bWouldChange=true;Result.Status=TEXT("Ready");return Result;
}

FEHBPillarSeparationPreview UEHBWallTopologyLibrary::PreviewPillarSeparation(const AEHBBuildingActorBase* Building,FGuid PillarGuid)
{
	FEHBPillarSeparationPreview Result;
	const auto Graph=CaptureWallTopology(Building);
	if(!Building||!Graph.Issues.IsEmpty()){Result.Status=TEXT("InvalidSource");Result.Issues=Graph.Issues;return Result;}
	const auto* Node=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==PillarGuid;});
	const auto* Pillar=Node?Cast<AEHB_Pillar>(Building->FindElementActorByGuid(PillarGuid)):nullptr;
	if(!Pillar){Result.Status=TEXT("UnknownPillar");return Result;}
	Result.RetainedNodeGuid=Node->NodeGuid;Result.PhysicalPillarGuid=PillarGuid;
	Result.NodeLocalTransform=Pillar->GetElementLocalTransform();Result.FloorIndex=Node->FloorIndex;
	Result.PhysicalDimensions=FVector(Pillar->Width,Pillar->Depth,Pillar->Height);
	Result.RetainedWallGuids=GetIncidentWallGuids(Graph,Node->NodeGuid);
	for(const auto& Relation:Building->ElementRelations)
	{
		if(!Relation.bEnabled||(!Relation.Source.RefersToElement(PillarGuid)&&!Relation.Target.RefersToElement(PillarGuid)&&!Relation.Source.RefersToNode(Node->NodeGuid)&&!Relation.Target.RefersToNode(Node->NodeGuid)))continue;
		if(Relation.Type==EEHBElementRelationType::TopologyConnection)Result.TopologyRelationGuids.AddUnique(Relation.RelationGuid);
		else Result.OtherRelationGuids.AddUnique(Relation.RelationGuid);
	}
	for(const auto* Element:Building->QueryElements(FEHBElementQuery()))
	{
		if(!Element)continue;
		if(!Element->IsA<AEHB_Pillar>()&&!Element->IsA<AEHB_Wall>())Result.UnplannedElementGuids.AddUnique(Element->ElementGuid);
		if(const auto* Railing=Cast<AEHB_Railing>(Element))
			if(Railing->StartAnchor.ElementGuid==PillarGuid||Railing->EndAnchor.ElementGuid==PillarGuid)Result.AnchoredRailingGuids.AddUnique(Railing->ElementGuid);
	}
	Result.TopologyRelationGuids.Sort(GuidLess);Result.OtherRelationGuids.Sort(GuidLess);Result.AnchoredRailingGuids.Sort(GuidLess);Result.UnplannedElementGuids.Sort(GuidLess);
	bool bPlain=Pillar->GetClass()==AEHB_Pillar::StaticClass()&&Pillar->ShapeType==EEHBPillarShapeType::Polygon
		&& Pillar->ConnectedSurfaceOverrides.IsEmpty()&&Pillar->CutOperations.IsEmpty()
		&& FMath::IsFinite(Pillar->Width)&&FMath::IsFinite(Pillar->Depth)&&FMath::IsFinite(Pillar->Height)&&Pillar->Width>0&&Pillar->Depth>0&&Pillar->Height>0
		&& Result.NodeLocalTransform.IsValid()&&Result.NodeLocalTransform.GetScale3D().Equals(FVector::OneVector,0.0001)
		&& Result.NodeLocalTransform.GetRotation().GetUpVector().Equals(FVector::UpVector,0.000001);
	float Width=0,Height=0;
	for(FGuid Id:Result.RetainedWallGuids)
	{
		const auto* Wall=Cast<AEHB_Wall>(Building->FindElementActorByGuid(Id));
		if(!Wall||Wall->GetClass()!=AEHB_Wall::StaticClass()||Wall->CurveControlOffset!=0||!FMath::IsFinite(Wall->Thickness)||Wall->Thickness<=0
			||!FMath::IsFinite(Wall->Height)||Wall->Height<=0){bPlain=false;continue;}
		Width=FMath::Max(Width,Wall->Thickness);Height=FMath::Max(Height,Wall->Height);
	}
	if(bPlain&&!Result.RetainedWallGuids.IsEmpty())Result.ProposedJunctionDimensions=FVector(Width,Width,Height);
	else AddIssue(Result.Issues,TEXT("JunctionShapeNotPlanned"),PillarGuid,TEXT("A plain connected polygon junction is required for dimension preparation."));
	if(!Result.OtherRelationGuids.IsEmpty())AddIssue(Result.Issues,TEXT("PhysicalRelationsNeedPolicy"),PillarGuid,TEXT("Support, host, finish and other physical relations must not be silently transferred to a connection node."));
	if(!Result.AnchoredRailingGuids.IsEmpty())AddIssue(Result.Issues,TEXT("RailingAnchorNeedsPolicy"),PillarGuid,TEXT("Railing anchors need an explicit retained support or post policy; node identity alone is insufficient."));
	Result.bSucceeded=true;Result.Status=TEXT("NodeAuthorityMigrationRequired");
	return Result;
}

FEHBWallMoveUpdatePlan UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(const FEHBWallTopologySnapshot& Snapshot,const TArray<FGuid>& MovedNodeGuids)
{
	FEHBWallMoveUpdatePlan Result;
	auto Fail=[&](FName Status){Result=FEHBWallMoveUpdatePlan();Result.Status=Status;return Result;};
	TSet<FGuid> KnownNodes,KnownWalls,Roots,Nodes,Walls;
	for(const auto& N:Snapshot.Nodes){if(!N.NodeGuid.IsValid()||KnownNodes.Contains(N.NodeGuid))return Fail(TEXT("InvalidNodeIdentity"));KnownNodes.Add(N.NodeGuid);}
	for(const auto& W:Snapshot.Walls)
	{
		if(!W.WallGuid.IsValid()||KnownWalls.Contains(W.WallGuid))return Fail(TEXT("InvalidWallIdentity"));
		if(W.StartNodeGuid==W.EndNodeGuid||!KnownNodes.Contains(W.StartNodeGuid)||!KnownNodes.Contains(W.EndNodeGuid))return Fail(TEXT("InvalidWallEndpoints"));
		KnownWalls.Add(W.WallGuid);
	}
	for(FGuid Id:MovedNodeGuids){if(!KnownNodes.Contains(Id))return Fail(TEXT("UnknownMovedNode"));Roots.Add(Id);Nodes.Add(Id);}
	for(const auto& W:Snapshot.Walls)if(Roots.Contains(W.StartNodeGuid)||Roots.Contains(W.EndNodeGuid)){Nodes.Add(W.StartNodeGuid);Nodes.Add(W.EndNodeGuid);}
	for(const auto& W:Snapshot.Walls)if(Nodes.Contains(W.StartNodeGuid)||Nodes.Contains(W.EndNodeGuid))Walls.Add(W.WallGuid);
	Result.MovedNodeGuids=Roots.Array();Result.JunctionNodeGuids=Nodes.Array();Result.WallGuids=Walls.Array();
	Result.MovedNodeGuids.Sort(GuidLess);Result.JunctionNodeGuids.Sort(GuidLess);Result.WallGuids.Sort(GuidLess);
	Result.bSucceeded=true;Result.Status=TEXT("Ready");return Result;
}

FEHBNodeMovePreview UEHBWallTopologyLibrary::PreviewNodeMoves(const AEHBBuildingActorBase* Building, const TArray<FEHBNodeMoveRequest>& Requests)
{
	FEHBNodeMovePreview Result;
	if (Requests.IsEmpty() || Requests.Num() > 2048) { Result.Status = TEXT("InvalidBatchSize"); return Result; }
	if (Requests.Num() == 1) { Result.NodeGuid = Requests[0].NodeGuid; Result.TargetPosition = Requests[0].TargetPosition; }
	Result.ProposedTopology = CaptureWallTopology(Building);
	if (!Building || !Result.ProposedTopology.Issues.IsEmpty())
	{
		Result.Status = TEXT("InvalidSource");
		return Result;
	}
	// Check the persisted baseline without calling a mutable preparation helper.
	const auto& Stored = Building->TopologyMigrationBaseline;
	if (Stored.Version != 1)
	{
		Result.Status = TEXT("MigrationRequired");
		return Result;
	}
	TMap<FGuid, const FEHBWallTopologyNode*> StoredNodes;
	TMap<FGuid, const FEHBWallTopologyEdge*> StoredWalls;
	for (const auto& N : Stored.Nodes) StoredNodes.Add(N.NodeGuid, &N);
	for (const auto& W : Stored.Walls) StoredWalls.Add(W.WallGuid, &W);
	bool bMatches = Stored.Nodes.Num() == Result.ProposedTopology.Nodes.Num() && Stored.Walls.Num() == Result.ProposedTopology.Walls.Num()
		&& StoredNodes.Num() == Stored.Nodes.Num() && StoredWalls.Num() == Stored.Walls.Num();
	for (const auto& N : Result.ProposedTopology.Nodes)
	{
		const auto* S = StoredNodes.FindRef(N.NodeGuid);
		bMatches &= S && S->LocalPosition.Equals(N.LocalPosition, 0.0001) && S->FloorIndex == N.FloorIndex && S->SourcePillarGuid == N.SourcePillarGuid;
	}
	for (const auto& W : Result.ProposedTopology.Walls)
	{
		const auto* S = StoredWalls.FindRef(W.WallGuid);
		bMatches &= S && S->StartNodeGuid == W.StartNodeGuid && S->EndNodeGuid == W.EndNodeGuid;
	}
	if (!bMatches)
	{
		Result.Status = TEXT("SourceChanged");
		return Result;
	}
	TMap<FGuid, FVector> OriginalPositions;
	TSet<FGuid> RequestedIds, ChangedIds;
	Result.NodeMoves = Requests;
	Result.NodeMoves.Sort([](const auto& A, const auto& B) { return GuidLess(A.NodeGuid, B.NodeGuid); });
	for (auto& Request : Result.NodeMoves)
	{
		if (RequestedIds.Contains(Request.NodeGuid)) { Result.Status = TEXT("DuplicateNodeRequest"); return Result; }
		RequestedIds.Add(Request.NodeGuid);
		const auto* Node = Result.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == Request.NodeGuid; });
		if (!Node) { Result.Status = TEXT("UnknownNode"); return Result; }
		if (Requests.Num() == 1) Result.OriginalPosition = Node->LocalPosition;
		if (Request.ExpectedPosition.ContainsNaN() || Request.TargetPosition.ContainsNaN()) { Result.Status = TEXT("InvalidPosition"); return Result; }
		if (!Node->LocalPosition.Equals(Request.ExpectedPosition, 0.001)) { Result.Status = TEXT("StalePosition"); return Result; }
		if (Request.TargetPosition.Z != Node->LocalPosition.Z) { Result.Status = TEXT("VerticalMoveUnsupported"); return Result; }
		OriginalPositions.Add(Request.NodeGuid, Node->LocalPosition);
		Request.ExpectedPosition = Node->LocalPosition;
		if (Request.TargetPosition != Node->LocalPosition) ChangedIds.Add(Request.NodeGuid);
		for (FGuid Id : GetIncidentWallGuids(Result.ProposedTopology, Request.NodeGuid)) Result.DirectWallGuids.AddUnique(Id);
	}
	Result.DirectWallGuids.Sort(GuidLess);
	Result.UpdatePlan=BuildWallMoveUpdatePlan(Result.ProposedTopology,RequestedIds.Array());
	if(!Result.UpdatePlan.bSucceeded){Result.Status=Result.UpdatePlan.Status;return Result;}
	TMap<FGuid, TArray<int32>> IncidentEdges;
	for (int32 I = 0; I < Result.ProposedTopology.Walls.Num(); ++I)
	{
		const auto& W = Result.ProposedTopology.Walls[I];
		IncidentEdges.FindOrAdd(W.StartNodeGuid).Add(I);
		IncidentEdges.FindOrAdd(W.EndNodeGuid).Add(I);
	}
	TSet<FGuid> Nodes, Walls;
	TArray<FGuid> Queue;
	for (const auto& Request : Result.NodeMoves) { Queue.Add(Request.NodeGuid); Nodes.Add(Request.NodeGuid); }
	for (int32 I = 0; I < Queue.Num(); ++I)
	{
		const auto* Edges = IncidentEdges.Find(Queue[I]);
		if (!Edges) continue;
		for (int32 EdgeIndex : *Edges)
		{
			const auto& W = Result.ProposedTopology.Walls[EdgeIndex];
			Walls.Add(W.WallGuid);
			const FGuid Other = W.StartNodeGuid == Queue[I] ? W.EndNodeGuid : W.StartNodeGuid;
			if (!Nodes.Contains(Other)) { Nodes.Add(Other); Queue.Add(Other); }
		}
	}
	Result.ConnectedNodeGuids = Nodes.Array();
	Result.ConnectedWallGuids = Walls.Array();
	Result.ConnectedNodeGuids.Sort(GuidLess);
	Result.ConnectedWallGuids.Sort(GuidLess);
	// Conservative disclosure: non pillar/wall elements need hosted/surface
	// dependency planning. This is not a claim they are all geometrically affected.
	for (const auto* Element : Building->QueryElements(FEHBElementQuery()))
		if (Element && !Element->IsA<AEHB_Pillar>() && !Element->IsA<AEHB_Wall>()) Result.UnplannedElementGuids.Add(Element->ElementGuid);
	Result.UnplannedElementGuids.Sort(GuidLess);
	Result.bWouldChange = !ChangedIds.IsEmpty();
	if (!Result.bWouldChange)
	{
		Result.bSucceeded = true;
		Result.Status = TEXT("NoChange");
		return Result;
	}
	for (auto& Node : Result.ProposedTopology.Nodes)
		if (const auto* Request = Result.NodeMoves.FindByPredicate([&](const auto& R) { return R.NodeGuid == Node.NodeGuid; }))
			Node.LocalPosition = Request->TargetPosition;
	auto RestoreProposal = [&]()
	{
		for (auto& Node : Result.ProposedTopology.Nodes)
			if (const auto* Original = OriginalPositions.Find(Node.NodeGuid)) Node.LocalPosition = *Original;
		Result.RoomBoundaryChanges.Reset();
	};
	for (const auto& Node : Result.ProposedTopology.Nodes)
	{
		if (!ChangedIds.Contains(Node.NodeGuid)) continue;
		for (const auto& Other : Result.ProposedTopology.Nodes)
			if (Other.NodeGuid != Node.NodeGuid && Other.FloorIndex == Node.FloorIndex && FVector::DistSquared2D(Other.LocalPosition, Node.LocalPosition) < 1.0)
			{ Result.Status = TEXT("NodeTooClose"); RestoreProposal(); return Result; }
	}
	// Moving centerlines may not cross or touch unrelated centerlines. Wall
	// thickness, corner meshes, curves and hosted cutters are deliberately not
	// validated here; commit remains unavailable until they have a shared solver.
	TMap<FGuid, const FEHBWallTopologyNode*> ProposedNodes;
	for (const auto& N : Result.ProposedTopology.Nodes) ProposedNodes.Add(N.NodeGuid, &N);
	auto Cross = [](FVector A, FVector B, FVector P) { return (B.X-A.X)*(P.Y-A.Y)-(B.Y-A.Y)*(P.X-A.X); };
	auto OnSegment = [&](FVector A, FVector B, FVector P)
	{
		return FMath::Abs(Cross(A,B,P)) <= 0.001 && P.X >= FMath::Min(A.X,B.X)-0.001 && P.X <= FMath::Max(A.X,B.X)+0.001
			&& P.Y >= FMath::Min(A.Y,B.Y)-0.001 && P.Y <= FMath::Max(A.Y,B.Y)+0.001;
	};
	for (const auto& A : Result.ProposedTopology.Walls)
	{
		if (!ChangedIds.Contains(A.StartNodeGuid) && !ChangedIds.Contains(A.EndNodeGuid)) continue;
		const auto* A0 = ProposedNodes.FindChecked(A.StartNodeGuid);
		const auto* A1 = ProposedNodes.FindChecked(A.EndNodeGuid);
		for (const auto& B : Result.ProposedTopology.Walls)
		{
			if (A.WallGuid == B.WallGuid) continue;
			const auto* B0 = ProposedNodes.FindChecked(B.StartNodeGuid);
			const auto* B1 = ProposedNodes.FindChecked(B.EndNodeGuid);
			if (A0->FloorIndex != B0->FloorIndex) continue;
			const bool bShared = A.StartNodeGuid == B.StartNodeGuid || A.StartNodeGuid == B.EndNodeGuid || A.EndNodeGuid == B.StartNodeGuid || A.EndNodeGuid == B.EndNodeGuid;
			const FVector P = A0->LocalPosition, Q = A1->LocalPosition, R = B0->LocalPosition, S = B1->LocalPosition;
			bool bConflict;
			if (bShared)
			{
				bConflict = (B.StartNodeGuid != A.StartNodeGuid && B.StartNodeGuid != A.EndNodeGuid && OnSegment(P,Q,R))
					|| (B.EndNodeGuid != A.StartNodeGuid && B.EndNodeGuid != A.EndNodeGuid && OnSegment(P,Q,S))
					|| (A.StartNodeGuid != B.StartNodeGuid && A.StartNodeGuid != B.EndNodeGuid && OnSegment(R,S,P))
					|| (A.EndNodeGuid != B.StartNodeGuid && A.EndNodeGuid != B.EndNodeGuid && OnSegment(R,S,Q));
			}
			else
			{
				bConflict = (Cross(P,Q,R)*Cross(P,Q,S) < 0 && Cross(R,S,P)*Cross(R,S,Q) < 0)
					|| OnSegment(P,Q,R) || OnSegment(P,Q,S) || OnSegment(R,S,P) || OnSegment(R,S,Q);
			}
			if (bConflict)
			{
				Result.Status = TEXT("WallIntersection");
				RestoreProposal();
				return Result;
			}
		}
	}
	// This reports explicit room/anchor bindings only. It does not authorize a
	// floor to follow automatically or treat every anchored slab as the same room.
	TMap<FGuid, const FEHBWallTopologyNode*> NodesByPillar;
	for (const auto& N : Result.ProposedTopology.Nodes) NodesByPillar.Add(N.SourcePillarGuid, &N);
	TMap<FGuid, FEHBBuildingClosedLoop> AffectedRooms;
	for (const auto& Node : Result.ProposedTopology.Nodes)
		if (ChangedIds.Contains(Node.NodeGuid))
			for (const auto& Room : Building->GetClosedLoopsByPillarGuid(Node.SourcePillarGuid)) AffectedRooms.Add(Room.LoopGuid, Room);
	TArray<FEHBBuildingClosedLoop> DependencyRooms;AffectedRooms.GenerateValueArray(DependencyRooms);
	TArray<FEHBRoomDependencyMembers> Dependencies;
	if(!Building->QueryRoomDependencies(DependencyRooms,Dependencies)){RestoreProposal();Result.Status=TEXT("InvalidRoomDependencyBindings");return Result;}
	for (const auto& Pair : AffectedRooms)
	{
		const auto& Room = Pair.Value;
		FEHBRoomBoundaryMovePreview Change;
		Change.RoomGuid = Room.LoopGuid;
		Change.FloorIndex = Room.FloorIndex;
		// Stable start within the ordered loop keeps serialized previews repeatable.
		int32 First = 0;
		for (int32 I=1; I<Room.PillarGuids.Num(); ++I)
			if (GuidLess(Room.PillarGuids[I],Room.PillarGuids[First])) First=I;
		for (int32 I=0; I<Room.PillarGuids.Num(); ++I)
		{
			const FGuid PillarId=Room.PillarGuids[(First+I)%Room.PillarGuids.Num()];
			const auto* N=NodesByPillar.FindRef(PillarId);
			if (!N)
			{
				RestoreProposal();
				Result.Status=TEXT("RoomBoundarySourceMismatch");
				return Result;
			}
			Change.BoundaryPillarGuids.Add(PillarId);
			const FVector* Original = OriginalPositions.Find(N->NodeGuid);
			Change.OriginalPolygon.Add(Original ? *Original : N->LocalPosition);
			Change.ProposedPolygon.Add(N->LocalPosition);
		}
		const auto* Members=Dependencies.FindByPredicate([&](const auto& D){return D.RoomGuid==Room.LoopGuid;});
		if(!Members){RestoreProposal();Result.Status=TEXT("MissingRoomDependencies");return Result;}
		for(FGuid Id:Members->Floors)if(const auto* Floor=Cast<AEHB_Floor>(Building->FindElementActorByGuid(Id)))
		{
			Change.BoundFloorFinishGuids.Add(Id);
			if(!Floor->IsRecordedOutlineUnchanged())Change.ModifiedOrUnclassifiedOutlineGuids.Add(Id);
			Change.bHasStaleFloorBinding |= Floor->RoomFloorIndex!=Room.FloorIndex || Floor->FloorIndex!=Room.FloorIndex;
		}
		for(FGuid Id:Members->Slabs)if(const auto* Slab=Cast<AEHB_FloorSlab>(Building->FindElementActorByGuid(Id)))
		{
			Change.BoundRoomSlabGuids.Add(Id);Change.bHasStaleSlabBinding |= Slab->RoomFillFloorIndex!=Room.FloorIndex;
			if(!Slab->IsRecordedOutlineUnchanged())Change.ModifiedOrUnclassifiedOutlineGuids.Add(Id);
		}
		Change.CandidateAnchoredSlabGuids=Members->AnchoredSlabs;
		Change.ModifiedOrUnclassifiedOutlineGuids.Append(Members->AnchoredSlabs);
		Change.BoundFloorFinishGuids.Sort(GuidLess);
		Change.CandidateAnchoredSlabGuids.Sort(GuidLess);
		Change.BoundRoomSlabGuids.Sort(GuidLess);
		Change.ModifiedOrUnclassifiedOutlineGuids.Sort(GuidLess);
		Result.RoomBoundaryChanges.Add(MoveTemp(Change));
	}
	Result.RoomBoundaryChanges.Sort([](const auto& A, const auto& B){return GuidLess(A.RoomGuid,B.RoomGuid);});
	Result.bSucceeded = true;
	Result.Status = Result.UnplannedElementGuids.IsEmpty() ? TEXT("TopologyPreviewOnly") : TEXT("RequiresDependencyPlanning");
	return Result;
}
