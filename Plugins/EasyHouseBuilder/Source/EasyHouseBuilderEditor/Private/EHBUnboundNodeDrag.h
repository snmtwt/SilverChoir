#pragma once
#include "Core/EHBBuildingActorBase.h"
#include "Actors/EHBElementActorBase.h"
#include "Toolsets/EHBBuildingToolset.h"

/** Viewport draft only. The building is written once by the release command. */
struct FEHBUnboundNodeDrag
{
	TWeakObjectPtr<AEHBBuildingActorBase> Building;
	FGuid NodeGuid,PhysicalGuid;
	TWeakObjectPtr<AActor> PhysicalActor;
	int32 Revision=0;
	FVector Start=FVector::ZeroVector,Target=FVector::ZeroVector;
	FTransform BuildingTransform;
	bool bTracking=false,bCaptured=false,bCancelled=false,bInvalid=false;
	FEHBToolsetOperationResult Feedback;
	FEHBWallNodeModelEditDraft Geometry;
	bool Capture(AEHBBuildingActorBase* B,FGuid Id,bool AllowBound=false)
	{
		*this={};bTracking=true;if(!IsValid(B)||B->WallNodeAuthority.Version!=2)return false;
		PhysicalGuid=B->FindPhysicalPillarForNode(Id);if(PhysicalGuid.IsValid()&&!AllowBound)return false;PhysicalActor=B->FindElementActorByGuid(PhysicalGuid);
		const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});if(!N)return false;
		Building=B;NodeGuid=Id;Revision=N->GeometryRevision;Start=Target=N->LocalTransform.GetLocation();BuildingTransform=B->GetActorTransform();bCaptured=true;Refresh();return Feedback.bSucceeded;
	}
	FEHBToolsetOperationResult Execute(bool Preview) const
	{
		FEHBToolsetOperationResult R;
		if(bCancelled){R.bSucceeded=true;R.Message=TEXT("Cancelled");return R;}
		if(bInvalid){R.Message=TEXT("HorizontalMovementOnly");return R;}
		if(!bCaptured||!Building.IsValid()||!Building->GetActorTransform().Equals(BuildingTransform,1.e-8)){R.Message=TEXT("StaleSource");return R;}
		if(Building->FindPhysicalPillarForNode(NodeGuid)!=PhysicalGuid||(PhysicalGuid.IsValid()&&(!PhysicalActor.IsValid()||!PhysicalActor->IsSelected()))){R.Message=TEXT("StaleSource");return R;}
		if(PhysicalGuid.IsValid()){FEHBNodeMoveRequest Move;Move.NodeGuid=NodeGuid;Move.ExpectedPosition=Start;Move.TargetPosition=Target;TMap<FGuid,int32> Revisions;Revisions.Add(NodeGuid,Revision);return UEHBBuildingToolset::CommitWallNodeMovesNative(Building.Get(),{Move},Revisions,Preview);}
		return UEHBBuildingToolset::MoveUnboundWallNode(Building.Get(),NodeGuid,Revision,Start,Target,Preview);
	}
	void Refresh()
	{
		Feedback=Execute(true);Geometry={};if(!Feedback.bSucceeded||bCancelled)return;
		FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(Building.Get(),Model).bSucceeded)return;
		FEHBNodeMoveRequest Request;Request.NodeGuid=NodeGuid;Request.ExpectedPosition=Start;Request.TargetPosition=Target;
		Geometry=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Model,{Request});
	}
	void AddDelta(const FVector& Delta,const FRotator& Rotation,const FVector& Scale)
	{
		if(bCancelled)return;
		const FVector Local=BuildingTransform.InverseTransformVectorNoScale(Delta);
		if(Local.ContainsNaN()||!FMath::IsNearlyZero(Local.Z)||!Rotation.IsNearlyZero()||!Scale.IsNearlyZero())bInvalid=true;
		else{Target.X+=Local.X;Target.Y+=Local.Y;}
		Refresh();
	}
	void Cancel(){bCancelled=true;Target=Start;Geometry={};Feedback.bSucceeded=true;Feedback.Message=TEXT("Cancelled");}
};
