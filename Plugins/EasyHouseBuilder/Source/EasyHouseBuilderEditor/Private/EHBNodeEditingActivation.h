#pragma once
#include "EHBNodeAuthorityEditing.h"

// Enable optional column bodies as one editor command. Existing identities and
// physical columns survive; room finishes are checked before and after migration.
namespace EHBNodeEditingActivation
{
#if WITH_DEV_AUTOMATION_TESTS
 inline int32 FailurePhase=0;
 inline bool FailAt(int32 Phase){if(FailurePhase!=Phase)return false;FailurePhase=0;return true;}
#else
 inline bool FailAt(int32){return false;}
#endif
 inline FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* B,bool Preview)
 {
  FEHBToolsetOperationResult Result;auto Fail=[&](FName Reason){Result.Message=Reason.ToString();return Result;};
  if(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()||GIsTransacting)return Fail(TEXT("RequiresIndependentEditorTransaction"));
  if(!IsValid(B)||B->GetClass()!=AEHB_Building::StaticClass()||B->GetWorld()!=GEditor->GetEditorWorldContext().World()||B->GetAttachParentActor()||B->IsActorBeingDestroyed())return Fail(TEXT("RequiresNativeEditorBuilding"));
  if(!GEditor->GetSelectedActors()->IsSelected(B)||GEditor->GetSelectedActors()->Num()!=1)return Fail(TEXT("TargetNotSelected"));
  if(FLevelUtils::IsLevelLocked(B->GetLevel())||!FLevelUtils::IsLevelVisible(B->GetLevel()))return Fail(TEXT("RequiresVisibleUnlockedLevel"));
  if(B->IsChangeNotificationBusy())return Fail(TEXT("ChangeNotificationBusy"));
  const auto Elements=B->QueryElements(FEHBElementQuery());TArray<AActor*> Attached;B->GetAttachedActors(Attached,true,true);
  if(Attached.Num()!=Elements.Num())return Fail(TEXT("IncompleteNativeGroup"));
  for(auto* E:Elements)if(!Attached.Contains(E)||E->GetAttachParentActor()!=B||E->GetLevel()!=B->GetLevel()
   ||(E->GetClass()!=AEHB_Pillar::StaticClass()&&E->GetClass()!=AEHB_Wall::StaticClass()&&E->GetClass()!=AEHB_Floor::StaticClass()&&E->GetClass()!=AEHB_FloorSlab::StaticClass())||!E->GetInstanceComponents().IsEmpty())return Fail(TEXT("RequiresNodeDependencyPlan"));
  for(auto* C:B->GetInstanceComponents())if(IsValid(C)&&(!Cast<UEHBWallJunctionComponent>(C)||!C->ComponentHasTag(TEXT("EHB.NodeAuthorityDerived"))))return Fail(TEXT("CustomBuildingComponentRequiresPlan"));
  FEHBWallNodeModel Source;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source);if(!Capture.bSucceeded)return Fail(Capture.Status);
  const auto Baseline=UEHBWallTopologyLibrary::PrepareTopologyMigration(B,false);
  // Only a truly empty, uninitialised baseline may be initialised without nodes.
  if(!Baseline.bSucceeded&&!(Source.Nodes.IsEmpty()&&Elements.IsEmpty()&&Baseline.Status==TEXT("EmptyTopology")))return Fail(Baseline.Status);
  const auto& Stored=B->PreparedWallNodeDefinitions;
  if(Stored.Version!=0)
  {
   const auto Prepared=UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(B,false);if(!Prepared.bSucceeded)return Fail(Prepared.Status);
  }
  else if(!Stored.Nodes.IsEmpty()||!Stored.Walls.IsEmpty()||!Stored.PillarBindings.IsEmpty())return Fail(TEXT("UnversionedDefinitions"));
  if(B->WallNodeAuthority.Version!=2&&!Source.Nodes.IsEmpty())
  {
   const auto Ownership=B->MigrateWallNodeOwnership(false);if(!Ownership.bSucceeded)return Fail(Ownership.Status);
  }
  EHBRoomFinishMove::FNodeEditPlan Finishes;FName Reason;
  if(!Finishes.Prepare(B,Source,Source,Elements,{},Reason))return Fail(Reason);
  for(const auto& R:B->ElementRelations)if(R.Type!=EEHBElementRelationType::TopologyConnection
   &&!(R.Type==EEHBElementRelationType::SurfaceFinish&&Finishes.OwnedRelations.Contains(R.RelationGuid)))return Fail(TEXT("RequiresNodeDependencyPlan"));
  const auto& Rooms=Finishes.GetRooms();
  if(B->WallNodeAuthority.Version==2||Preview){Result.bSucceeded=true;Result.Message=B->WallNodeAuthority.Version==2?TEXT("AlreadyEnabled"):TEXT("Ready");return Result;}
  FEHBChangeNotificationBatch Notifications(*B);if(!Notifications.IsActive())return Fail(TEXT("ChangeNotificationBusy"));
  bool Applied=false;
  {
   FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","EnableWallNodeEditing","Enable Linked Wall Corner Editing"));
   B->SetFlags(RF_Transactional);B->Modify();
   for(auto* E:Elements){E->SetFlags(RF_Transactional);E->Modify();TInlineComponentArray<UActorComponent*> Components(E);for(auto* C:Components){C->SetFlags(RF_Transactional);C->Modify();}}
   if(Source.Nodes.IsEmpty()){B->WallNodeOwnership.Version=1;Applied=true;}
   else Applied=B->MigrateWallNodeOwnership(true).bSucceeded;
   if(Applied){B->WallNodeAuthority.Version=2;B->WallNodeAuthority.Nodes=Source.Nodes;}
   if(FailAt(1))Applied=false;
   if(Applied){B->RebuildElementAndRelationshipIndexes();Applied=B->RebuildWallNodeAuthorityGeometry();}
   if(FailAt(2))Applied=false;
   if(Applied)Applied=Finishes.Apply(B,Reason,&FailAt);
   if(Applied)
   {
    FEHBWallNodeModel Actual;Applied=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual).bSucceeded;
    FString Expected,Observed;if(Applied){FJsonObjectConverter::UStructToJsonObjectString(Source,Expected);FJsonObjectConverter::UStructToJsonObjectString(Actual,Observed);Applied=Expected==Observed;}
    const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);Applied&=Graph.Issues.IsEmpty();
    if(Applied){B->TopologyMigrationBaseline.Version=1;B->TopologyMigrationBaseline.Nodes=Graph.Nodes;B->TopologyMigrationBaseline.Walls=Graph.Walls;B->PreparedWallNodeDefinitions={};}
    for(const auto& R:Rooms){const auto Loops=B->GetClosedLoopsByFloor(R.FloorIndex);Applied&=Loops.ContainsByPredicate([&](const auto& L){return L.LoopGuid==R.RoomGuid&&FMath::IsNearlyEqual(static_cast<double>(L.Area),R.Area,0.1);});}
   }
   if(Applied)
   {
    for(auto* E:Elements)E->SynchronizePlannedEditorMove();
    TArray<FGuid> Nodes,RoomIds;for(const auto& N:Source.Nodes)Nodes.Add(N.NodeGuid);for(const auto& R:Rooms)RoomIds.Add(R.RoomGuid);
    Applied=Notifications.RecordCommittedEdit(TEXT("EnableWallNodeEditing"),Nodes,RoomIds);
   }
   if(FailAt(3))Applied=false;
   B->MarkPackageDirty();
  }
  if(!Applied)
  {
   const bool Restored=GEditor->UndoTransaction(false);if(Restored){B->RebuildElementAndRelationshipIndexes();Notifications.Rollback();}else Notifications.Publish();
   return Fail(Restored?TEXT("NodeActivationFailedRolledBack"):TEXT("NodeActivationRollbackFailed"));
  }
  Result.bSucceeded=true;Result.Message=TEXT("Committed");Result.CommittedEdit=B->LastCommittedEdit;Notifications.Publish();GEditor->RedrawLevelEditingViewports();return Result;
 }
}
