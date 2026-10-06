#pragma once

#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Toolsets/EHBBuildingToolset.h"

/** A value draft: mouse movement never writes to the building or undo buffer. */
struct FEHBRoomFloorWallDrag
{
	TWeakObjectPtr<AEHB_Wall> Wall;
	TWeakObjectPtr<AEHBBuildingActorBase> Building;
	FGuid WallGuid, StartGuid, EndGuid;
	FGuid StartNode,EndNode;
	TMap<FGuid,int32> NodeRevisions;
	bool bOptionalNodes=false;
	FEHBWallNodeModelEditDraft NodeGeometry;
	FVector Start = FVector::ZeroVector, End = FVector::ZeroVector, WorldDelta = FVector::ZeroVector;
	FTransform BuildingTransform;
	float Height = 0, Thickness = 0;
	bool bTracking = false, bCaptured = false, bCancelled = false, bInvalidTransform = false;
	FEHBToolsetOperationResult Feedback;
	FEHBNodeMovePreview Geometry;

	bool IsSet() const { return bTracking; }
	void Capture(AEHB_Wall* Source)
	{
		*this = {}; bTracking = true;
		if (!Source || !Source->OwningBuilding || Source->IsActorBeingDestroyed()) return;
		Wall = Source; Building = Source->OwningBuilding.Get(); WallGuid = Source->ElementGuid;
		StartGuid = Source->StartPillarGuid; EndGuid = Source->EndPillarGuid;
		BuildingTransform = Building->GetActorTransform(); Height = Source->Height; Thickness = Source->Thickness;
		bOptionalNodes=Building->WallNodeAuthority.Version==2;
		if(bOptionalNodes)
		{
			const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Building.Get());const auto* W=Graph.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==WallGuid;});if(!W){Refresh();return;}
			StartNode=W->StartNodeGuid;EndNode=W->EndNodeGuid;const auto* A=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==StartNode;});const auto* Z=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==EndNode;});
			if(A&&Z){Start=A->LocalTransform.GetLocation();End=Z->LocalTransform.GetLocation();NodeRevisions.Add(StartNode,A->GeometryRevision);NodeRevisions.Add(EndNode,Z->GeometryRevision);bCaptured=true;}Refresh();return;
		}
		const auto* A = Building->FindElementActorByGuid(StartGuid); const auto* B = Building->FindElementActorByGuid(EndGuid);
		if (A && B) { Start = A->GetElementLocalTransform().GetLocation(); End = B->GetElementLocalTransform().GetLocation(); bCaptured = true; }
		Refresh();
	}
	FEHBToolsetOperationResult Execute(bool bPreviewOnly) const
	{
		FEHBToolsetOperationResult Result;
		if (bCancelled) { Result.bSucceeded = true; Result.Message = TEXT("Cancelled"); return Result; }
		if (bInvalidTransform) { Result.Message = TEXT("InvalidHorizontalDelta"); return Result; }
		if (!bCaptured || !Wall.IsValid() || !Building.IsValid() || Wall->IsActorBeingDestroyed()
			|| Wall->OwningBuilding != Building.Get() || Wall->ElementGuid != WallGuid || Wall->StartPillarGuid != StartGuid || Wall->EndPillarGuid != EndGuid
			|| Wall->Height != Height || Wall->Thickness != Thickness || !Building->GetActorTransform().Equals(BuildingTransform,0.0001))
		{ Result.Message = TEXT("StaleSource"); return Result; }
		if(bOptionalNodes)
		{
			const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Building.Get());const auto* W=Graph.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==WallGuid;});
			if(Building->WallNodeAuthority.Version!=2||!W||W->StartNodeGuid!=StartNode||W->EndNodeGuid!=EndNode){Result.Message=TEXT("StaleSource");return Result;}
			return UEHBBuildingToolset::CommitWallNodeMovesNative(Building.Get(),Moves(),NodeRevisions,bPreviewOnly);
		}
		return UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building.Get(),WallGuid,Start,End,
			BuildingTransform.InverseTransformVectorNoScale(WorldDelta),bPreviewOnly);
	}
	void Refresh()
	{
		Feedback = Execute(true); Geometry = {};NodeGeometry={};
		if (!Feedback.bSucceeded || bCancelled || !Building.IsValid()) return;
		if(bOptionalNodes){FEHBWallNodeModel Model;if(UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(Building.Get(),Model).bSucceeded)NodeGeometry=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Model,Moves());return;}
		const auto Graph = UEHBWallTopologyLibrary::CaptureWallTopology(Building.Get());
		const auto* Edge = Graph.Walls.FindByPredicate([&](const auto& W){return W.WallGuid == WallGuid;});
		if (!Edge) return;
		const FVector Delta = BuildingTransform.InverseTransformVectorNoScale(WorldDelta);
		TArray<FEHBNodeMoveRequest> Requests; Requests.SetNum(2);
		Requests[0].NodeGuid = Edge->StartNodeGuid; Requests[0].ExpectedPosition = Start; Requests[0].TargetPosition = Start + Delta;
		Requests[1].NodeGuid = Edge->EndNodeGuid; Requests[1].ExpectedPosition = End; Requests[1].TargetPosition = End + Delta;
		Geometry = UEHBWallTopologyLibrary::PreviewNodeMoves(Building.Get(),Requests);
	}
	TArray<FEHBNodeMoveRequest> Moves() const
	{
		TArray<FEHBNodeMoveRequest> R;R.SetNum(2);const FVector Delta=BuildingTransform.InverseTransformVectorNoScale(WorldDelta);
		R[0].NodeGuid=StartNode;R[0].ExpectedPosition=Start;R[0].TargetPosition=Start+Delta;R[1].NodeGuid=EndNode;R[1].ExpectedPosition=End;R[1].TargetPosition=End+Delta;return R;
	}
	void AddDelta(const FVector& Delta, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Scale = FVector::ZeroVector)
	{
		if (bCancelled) return;
		if (Delta.ContainsNaN() || !FMath::IsNearlyZero(Delta.Z) || !Rotation.IsNearlyZero() || !Scale.IsNearlyZero()) bInvalidTransform = true;
		else WorldDelta += Delta;
		Refresh();
	}
	void Cancel() { bCancelled = true; WorldDelta = FVector::ZeroVector; Geometry = {}; NodeGeometry = {}; Feedback.bSucceeded = true; Feedback.Message = TEXT("Cancelled"); }
};
