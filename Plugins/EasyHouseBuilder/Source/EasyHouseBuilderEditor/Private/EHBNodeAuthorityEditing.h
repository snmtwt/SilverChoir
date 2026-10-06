#pragma once
#include "Toolsets/EHBBuildingToolset.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "Core/EHBWallNodeRooms.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_DoorWindow.h"
#include "EHBRoomFinishMove.h"
#include "EHB_Building.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "JsonObjectConverter.h"

namespace EHBNodeAuthorityEditing
{
#if WITH_DEV_AUTOMATION_TESTS
	inline int32 FailurePhase=0;
	inline FEHBConnectedWallRefreshStats LastGeometryRefresh;
	inline bool FailAt(int32 Phase){if(FailurePhase!=Phase)return false;FailurePhase=0;return true;}
#else
	inline bool FailAt(int32){return false;}
#endif
	inline FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* B,FGuid NodeId,FGuid ExpectedPillar,int32 ExpectedRevision,
		const FVector& ExpectedPosition,const FVector& TargetPosition,bool bRemovePhysical,bool bPreview,
		const TArray<FEHBNodeMoveRequest>* Batch=nullptr,const TMap<FGuid,int32>* Revisions=nullptr,FName MoveCommand=TEXT("MoveUnboundNode"))
	{
		FEHBToolsetOperationResult Result;auto Fail=[&](FName Reason){Result.Message=Reason.ToString();return Result;};
		if(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()||GIsTransacting)return Fail(TEXT("RequiresIndependentEditorTransaction"));
		if(!IsValid(B)||B->GetClass()!=AEHB_Building::StaticClass()||B->GetWorld()!=GEditor->GetEditorWorldContext().World()||B->GetAttachParentActor()||B->IsActorBeingDestroyed())return Fail(TEXT("RequiresNativeEditorBuilding"));
		if(FLevelUtils::IsLevelLocked(B->GetLevel())||!FLevelUtils::IsLevelVisible(B->GetLevel()))return Fail(TEXT("RequiresVisibleUnlockedLevel"));
		if(!B->HasWallNodeAuthority()||B->IsChangeNotificationBusy())return Fail(TEXT("RequiresReadyNodeAuthority"));
		FEHBWallNodeModel Source;auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Source);if(!Capture.bSucceeded)return Fail(Capture.Status);
		const auto* Node=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeId;});if(!Node)return Fail(TEXT("UnknownNode"));
		if(Node->GeometryRevision!=ExpectedRevision)return Fail(TEXT("StaleNodeRevision"));
		TArray<FEHBNodeMoveRequest> Requests;if(Batch)Requests=*Batch;
		else if(!bRemovePhysical){auto& R=Requests.AddDefaulted_GetRef();R.NodeGuid=NodeId;R.ExpectedPosition=ExpectedPosition;R.TargetPosition=TargetPosition;}
		if(Batch)
		{
			if(!Revisions||Revisions->Num()!=Requests.Num())return Fail(TEXT("IncompleteNodeRevisions"));
			for(const auto& R:Requests){const auto* N=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==R.NodeGuid;});const auto* Revision=Revisions->Find(R.NodeGuid);if(!N||!Revision||*Revision!=N->GeometryRevision)return Fail(TEXT("StaleNodeRevision"));}
		}
		bool Selected=GEditor->GetSelectedActors()->IsSelected(B);
		if(!Selected&&GEditor->GetSelectedActors()->Num()==1)for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
		{
			if(const auto* P=Cast<AEHB_Pillar>(*It);P&&P->OwningBuilding==B)
				Selected=bRemovePhysical?P->ElementGuid==ExpectedPillar:Requests.Num()==1&&B->FindNodeForPhysicalPillar(P->ElementGuid)==Requests[0].NodeGuid;
			else if(const auto* W=Cast<AEHB_Wall>(*It);W&&W->OwningBuilding==B&&!bRemovePhysical&&Requests.Num()==2)
			{
				const auto* Definition=Source.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});
				Selected=Definition&&Requests.ContainsByPredicate([&](const auto& R){return R.NodeGuid==Definition->StartNodeGuid;})&&Requests.ContainsByPredicate([&](const auto& R){return R.NodeGuid==Definition->EndNodeGuid;});
			}
		}
		if(!Selected)return Fail(TEXT("TargetNotSelected"));
		TArray<AActor*> Attached;B->GetAttachedActors(Attached,true,true);const auto Elements=B->QueryElements(FEHBElementQuery());
		if(Attached.Num()!=Elements.Num())return Fail(TEXT("IncompleteNativeGroup"));
		for(auto* E:Elements)if(!Attached.Contains(E)||E->GetAttachParentActor()!=B||E->GetLevel()!=B->GetLevel()
			||(E->GetClass()!=AEHB_Pillar::StaticClass()&&E->GetClass()!=AEHB_Wall::StaticClass()&&E->GetClass()!=AEHB_Floor::StaticClass()&&E->GetClass()!=AEHB_FloorSlab::StaticClass()&&E->GetClass()!=AEHB_DoorWindow::StaticClass())||!E->GetInstanceComponents().IsEmpty())return Fail(TEXT("RequiresNodeDependencyPlan"));
		for(auto* C:B->GetInstanceComponents())if(IsValid(C)&&(!Cast<UEHBWallJunctionComponent>(C)||!C->ComponentHasTag(TEXT("EHB.NodeAuthorityDerived"))))return Fail(TEXT("CustomBuildingComponentRequiresPlan"));
		AEHB_Pillar* Physical=nullptr;FEHBWallNodeModelEditDraft Draft;
		if(bRemovePhysical)
		{
			Draft=UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(Source,NodeId,ExpectedPillar,ExpectedRevision);
			if(Draft.bWouldChange){Physical=Cast<AEHB_Pillar>(B->FindElementActorByGuid(ExpectedPillar));if(!Physical)return Fail(TEXT("StalePhysicalBinding"));}
		}
		else
		{
			if(!Batch&&Source.PillarBindings.ContainsByPredicate([&](const auto& V){return V.NodeGuid==NodeId;}))return Fail(TEXT("UseBoundNodeMoveCommand"));
			Draft=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Source,Requests);
		}
		if(!Draft.bSucceeded)return Fail(Draft.Status);
		FName Reason;
		EHBRoomFinishMove::FNodeEditPlan Finishes;
		if(!Finishes.Prepare(B,Source,Draft.Definitions,Elements,Physical?Physical->ElementGuid:FGuid(),Reason))return Fail(Reason);
		const auto& Rooms=Finishes.GetRooms();
		for(const auto& R:B->ElementRelations)if(R.Type!=EEHBElementRelationType::TopologyConnection
			&&!(R.Type==EEHBElementRelationType::SurfaceFinish&&Finishes.OwnedRelations.Contains(R.RelationGuid))&&!Finishes.SupportRelationIds.Contains(R.RelationGuid)&&!Finishes.HostedRelationIds.Contains(R.RelationGuid))return Fail(TEXT("RequiresNodeDependencyPlan"));
		if(!Draft.bWouldChange||bPreview){Result.bSucceeded=true;Result.Message=Draft.bWouldChange?TEXT("Ready"):TEXT("NoChange");return Result;}
		FEHBChangeNotificationBatch Notifications(*B);if(!Notifications.IsActive())return Fail(TEXT("ChangeNotificationBusy"));
		bool Applied=false,bScopedRoomUpdates=false;TArray<FGuid> ChangedFinishRooms;
		{
			FScopedTransaction Transaction(bRemovePhysical?NSLOCTEXT("EasyHouseBuilder","RemovePhysicalColumn","Remove Physical Column, Keep Wall Node"):NSLOCTEXT("EasyHouseBuilder","MoveUnboundWallNode","Move Wall Node"));
			B->SetFlags(RF_Transactional);B->Modify();
			for(auto* E:Elements){E->SetFlags(RF_Transactional);E->Modify();TInlineComponentArray<UActorComponent*> Components(E);for(auto* C:Components){C->SetFlags(RF_Transactional);C->Modify();}}
			Finishes.DetachRemovedHostContacts(B);
			B->WallNodeAuthority.Version=2;B->WallNodeAuthority.Nodes=Draft.Definitions.Nodes;B->WallNodeOwnership.Bindings=Draft.Definitions.PillarBindings;
			Applied=true;
			if(Physical)
			{
				// Detach the physical identity before destruction. The logical endpoint and
				// its topology relations remain in the building; no hidden Actor replaces it.
				B->UnregisterElementActor(Physical);Physical->OwningBuilding=nullptr;Physical->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);Applied=Physical->Destroy();
			}
			// Apply all bound proxies from the same final candidate before any geometry
			// rebuild. A missing binding has no Actor to move or replace.
			for(const auto& Binding:Draft.Definitions.PillarBindings)
			{
				const auto* Target=Draft.Definitions.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Binding.NodeGuid;});
				const auto* Original=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Binding.NodeGuid;});
				if(Target&&Original&&!Target->LocalTransform.Equals(Original->LocalTransform,0.0))
					if(auto* P=Cast<AEHB_Pillar>(B->FindElementActorByGuid(Binding.PhysicalPillarGuid)))
					{
						P->SetActorRelativeTransform(Target->LocalTransform);
						// The normal setter round-trips through world space. Keep the exact
						// planned local pose, but never mask a non-numerical movement failure.
						if(auto* Root=P->GetRootComponent();Root&&Root->GetRelativeTransform().Equals(Target->LocalTransform,1.e-8))
						{
							Root->SetRelativeLocation_Direct(Target->LocalTransform.GetLocation());
							Root->SetRelativeRotationExact(Target->LocalTransform.Rotator());
							Root->UpdateComponentToWorld(EUpdateTransformFlags::None,ETeleportType::TeleportPhysics);
						}
						else Applied=false;
					}
			}
			if(FailAt(1))Applied=false;
			// Pure movement preserves identity, binding and relations. An index rebuild
			// also regenerates the entire authority and resets unrelated revisions.
			if(Applied&&bRemovePhysical){B->RebuildElementAndRelationshipIndexes();Applied=B->RebuildWallNodeAuthorityGeometry();}
			else if(Applied)
			{
				const auto GeometryRefresh=B->RefreshWallNodeMoveGeometry(Source,Draft);
#if WITH_DEV_AUTOMATION_TESTS
				LastGeometryRefresh=GeometryRefresh;
#endif
				Applied=GeometryRefresh.bSucceeded;bScopedRoomUpdates=GeometryRefresh.bUsedScopedUpdate;
				if(!Applied)UE_LOG(LogTemp,Warning,TEXT("Node geometry move refused: %s"),*GeometryRefresh.Status.ToString());
			}
			if(FailAt(2))Applied=false;
			if(Applied&&!Finishes.Apply(B,Reason,&FailAt,&ChangedFinishRooms)){Applied=false;UE_LOG(LogTemp,Warning,TEXT("EHB node finish edit rejected during apply: %s"),*Reason.ToString());}
			if(Applied)
			{
				FEHBWallNodeModel Actual;Applied=UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Actual).bSucceeded&&Actual.Nodes.Num()==Draft.Definitions.Nodes.Num()&&Actual.PillarBindings.Num()==Draft.Definitions.PillarBindings.Num();
				if(Applied){FString Expected,Observed;FJsonObjectConverter::UStructToJsonObjectString(Draft.Definitions,Expected);FJsonObjectConverter::UStructToJsonObjectString(Actual,Observed);Applied=Expected==Observed;}
				const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);Applied&=Graph.Issues.IsEmpty();
				if(Applied){B->TopologyMigrationBaseline.Version=1;B->TopologyMigrationBaseline.Nodes=Graph.Nodes;B->TopologyMigrationBaseline.Walls=Graph.Walls;B->PreparedWallNodeDefinitions={};}
				for(const auto& R:Rooms){const auto Loops=B->GetClosedLoopsByFloor(R.FloorIndex);Applied&=Loops.ContainsByPredicate([&](const auto& L){return L.LoopGuid==R.RoomGuid&&FMath::IsNearlyEqual(static_cast<double>(L.Area),R.Area,0.1);});}
			}
			if(Applied)for(auto* E:Elements)if(IsValid(E)&&!E->IsActorBeingDestroyed())E->SynchronizePlannedEditorMove();
			if(Applied)
   {
    // A shared wall/junction can affect the net boundary of either room even when
    // its centerline polygon stays fixed. Full geometry recovery stays conservative.
    TSet<FGuid> Nodes(Draft.UpdatePlan.JunctionNodeGuids),Walls(Draft.UpdatePlan.WallGuids);
    TArray<FGuid> RoomIds=ChangedFinishRooms;
    for(const auto& R:Rooms)if(!bScopedRoomUpdates||R.NodeGuids.ContainsByPredicate([&](FGuid Id){return Nodes.Contains(Id);})||R.WallGuids.ContainsByPredicate([&](FGuid Id){return Walls.Contains(Id);}))RoomIds.AddUnique(R.RoomGuid);
    Applied=Notifications.RecordCommittedEdit(bRemovePhysical?FName(TEXT("RemovePhysicalColumn")):MoveCommand,Draft.UpdatePlan.JunctionNodeGuids,RoomIds,Finishes.Displays.AuthoredElements);
   }
			if(FailAt(3))Applied=false;
			B->MarkPackageDirty();
		}
		if(!Applied)
		{
			const bool Restored=GEditor->UndoTransaction(false);if(Restored){B->RebuildElementAndRelationshipIndexes();Notifications.Rollback();}else Notifications.Publish();
			return Fail(Restored?TEXT("NodeEditFailedRolledBack"):TEXT("NodeEditRollbackFailed"));
		}
		Result.bSucceeded=true;Result.Message=TEXT("Committed");Result.CommittedEdit=B->LastCommittedEdit;Notifications.Publish();GEditor->RedrawLevelEditingViewports();return Result;
	}
	inline FEHBToolsetOperationResult ExecuteMoves(AEHBBuildingActorBase* B,const TArray<FEHBNodeMoveRequest>& Requests,const TMap<FGuid,int32>& Revisions,bool Preview,FName Command=TEXT("MoveWallNodes"))
	{
		FEHBToolsetOperationResult R;if(!IsValid(B)||B->WallNodeAuthority.Version!=2){R.Message=TEXT("RequiresOptionalNodeAuthority");return R;}
		if(Requests.IsEmpty()||!Revisions.Contains(Requests[0].NodeGuid)){R.Message=TEXT("IncompleteNodeRevisions");return R;}
		const auto& First=Requests[0];return Execute(B,First.NodeGuid,{},Revisions.FindChecked(First.NodeGuid),First.ExpectedPosition,First.TargetPosition,false,Preview,&Requests,&Revisions,Command);
	}
}
