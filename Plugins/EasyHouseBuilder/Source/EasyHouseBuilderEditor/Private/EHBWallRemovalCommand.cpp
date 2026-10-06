#include "EHBWallRemovalCommand.h"
#include "EHBNodeAuthorityEditing.h"
#include "EHBCopyOutlinePolicy.h"
#include "Core/EHBRoomCoverageTransition.h"
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EasyHouseEditorMode.h"
#include "Algo/Reverse.h"
#include "EHBRoomFinishPartition.h"
#include "EHBSlabDisplayPlan.h"

namespace EHBWallRemovalCommand
{
namespace
{
 bool FailAt(int32 Phase)
 {
#if WITH_DEV_AUTOMATION_TESTS
  if(FailurePhase==Phase){FailurePhase=0;return true;}
#endif
  return false;
 }
 void Align(TArray<FVector>& Polygon,const TArray<FVector>& Original)
 {
  auto Area=[](const auto& V){double A=0;for(int32 I=0;I<V.Num();++I)A+=V[I].X*V[(I+1)%V.Num()].Y-V[I].Y*V[(I+1)%V.Num()].X;return A;};
  if(Area(Polygon)*Area(Original)<0)Algo::Reverse(Polygon);
  int32 First=Original.IsEmpty()?INDEX_NONE:Polygon.IndexOfByPredicate([&](const auto& P){return P.Equals(Original[0],0.001);});
  if(First==INDEX_NONE){First=0;for(int32 I=1;I<Polygon.Num();++I)if(Polygon[I].X<Polygon[First].X||(Polygon[I].X==Polygon[First].X&&Polygon[I].Y<Polygon[First].Y))First=I;}
  const auto Copy=Polygon;for(int32 I=0;I<Polygon.Num();++I)Polygon[I]=Copy[(First+I)%Polygon.Num()];
 }
 bool ValidateCandidateSlab(const AEHB_FloorSlab* Slab,const TArray<FVector>& Polygon)
 {
  if(!Slab->DisplayPartition.IsActive()||Polygon==Slab->LocalTopPolygon)return Slab->ValidateSlabOutline(Polygon,{});
  TArray<FEHBLogicalSurfaceRegion> Unallocated;return Slab->BuildUnallocatedDisplayRegion(Polygon,Unallocated);
 }
 bool Common(const AEHBElementActorBase* A,const AEHBElementActorBase* B)
 {
  return A->ElementName==B->ElementName&&A->SemanticTags==B->SemanticTags&&A->Tags==B->Tags
   &&A->bNotifyWhenMovedInEditor==B->bNotifyWhenMovedInEditor&&A->IsHidden()==B->IsHidden()
   &&A->FloorIndex==B->FloorIndex&&A->FloorRole==B->FloorRole&&A->FloorAssignmentPolicy==B->FloorAssignmentPolicy
   &&A->ElementCapabilities==B->ElementCapabilities;
 }
 bool Compatible(const AEHB_Floor* A,const AEHB_Floor* B)
 {
  return Common(A,B)&&A->FloorMaterial==B->FloorMaterial&&A->VisualOffset==B->VisualOffset
   &&A->bEnableRuntimeCollision==B->bEnableRuntimeCollision&&A->bEnableEditorCollision==B->bEnableEditorCollision
   &&A->OutlineSource==B->OutlineSource&&FMath::IsNearlyEqual(A->FloorRegions[0].OuterPolygon[0].Z,B->FloorRegions[0].OuterPolygon[0].Z,0.001);
 }
 bool Compatible(const AEHB_FloorSlab* A,const AEHB_FloorSlab* B)
 {
  return Common(A,B)&&A->Thickness==B->Thickness&&A->Offset==B->Offset&&A->SurfaceMaterial==B->SurfaceMaterial
   &&A->VisualExpansion==B->VisualExpansion&&A->bEnableAdjacentSlabSnap==B->bEnableAdjacentSlabSnap
   &&FMath::IsNearlyEqual(A->GetElementLocalTransform().TransformPosition(FVector(0,0,A->GetTopZ())).Z,B->GetElementLocalTransform().TransformPosition(FVector(0,0,B->GetTopZ())).Z,0.001);
 }
 struct FPlan
 {
  struct FFloor {AEHB_Floor* Actor=nullptr;FGuid Room;TArray<FEHBFloorFinishRegion> Regions;TArray<FEHBElementRelation> Relations;EEHBOutlineSource Source;};
  struct FSlab {AEHB_FloorSlab* Actor=nullptr;FGuid Room,Anchor;EEHBFloorSlabWallSide Side=EEHBFloorSlabWallSide::None;TArray<FVector> Polygon;EEHBOutlineSource Source=EEHBOutlineSource::RoomBoundary;FEHBSlabDisplayPartition Display;};
  FEHBSlabDisplayPlan DisplayPlan;
  TArray<AEHB_FloorSlab*> DisplayLayers;
  FEHBWallNodeModel Source,Candidate;
  FEHBWallNodeGeometryDraft Geometry;
  TArray<AEHB_Wall*> Walls;
  TArray<AEHBElementActorBase*> Retired;
  TArray<FFloor> Floors;TArray<FSlab> Slabs;
  TSet<FGuid> RemovedHosts,RetiredIds,OwnedRelations;
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;
  TArray<FEHBNodeRoomBoundary> Rooms;
  TArray<FGuid> RoomIds,NodeIds,AuthoredElements;
  bool AllocateDisplays(FName& Status)
  {
   DisplayPlan.DisplayLayers=DisplayLayers;
   for(const auto& P:Slabs){auto& C=DisplayPlan.Slabs.AddDefaulted_GetRef();C.Actor=P.Actor;C.Polygon=P.Polygon;}
   if(!DisplayPlan.Prepare(Status))return false;
   for(int32 I=0;I<Slabs.Num();++I)Slabs[I].Display=DisplayPlan.Slabs[I].Display;
   for(FGuid Id:DisplayPlan.AuthoredElements)AuthoredElements.AddUnique(Id);
   return true;
  }
  bool VerifyDisplays(AEHBBuildingActorBase* B,FName& Status) const {return DisplayPlan.Verify(Status);}
  bool Prepare(AEHBBuildingActorBase* B,const TArray<FGuid>& Requested,const TArray<AEHBElementActorBase*>& Elements,FName& Status)
  {
   auto Fail=[&](FName Why){Status=Why;return false;};
   const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source);if(!Capture.bSucceeded)return Fail(Capture.Status);
   EHBRoomFinishMove::FNodeEditPlan Original;if(!Original.Prepare(B,Source,Source,Elements,{},Status))return false;
   OwnedRelations=Original.OwnedRelations;
   for(const auto& R:B->ElementRelations)if(R.Type!=EEHBElementRelationType::TopologyConnection&&!(R.Type==EEHBElementRelationType::SurfaceFinish&&OwnedRelations.Contains(R.RelationGuid)))return Fail(TEXT("RequiresWallRemovalDependencyPlan"));
   Candidate=Source;
   for(FGuid Id:Requested)
   {
    const auto* W=Source.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Id;});auto* Actor=Cast<AEHB_Wall>(B->FindElementActorByGuid(Id));
    if(!W||!Actor)return Fail(TEXT("UnknownRemovalWall"));
    Walls.Add(Actor);RemovedHosts.Add(Id);NodeIds.AddUnique(W->StartNodeGuid);NodeIds.AddUnique(W->EndNodeGuid);
   }
   Candidate.Walls.RemoveAll([&](const auto& W){return RemovedHosts.Contains(W.WallGuid);});
   for(auto& N:Candidate.Nodes)if(NodeIds.Contains(N.NodeGuid))++N.GeometryRevision;
   Geometry=FEHBWallNodeGeometryDraft::Build(Candidate,Status);if(!Geometry.IsReady())return false;
   const auto Transition=FEHBRoomCoverageTransition::Build(B->BuildingGuid,Source,Candidate,Status);if(!Transition.IsReady())return false;
   if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Candidate,Rooms,Status))return false;
   for(const auto& R:Original.GetRooms())RoomIds.AddUnique(R.RoomGuid);for(const auto& R:Rooms)RoomIds.AddUnique(R.RoomGuid);
   auto PreserveFloor=[&](AEHB_Floor* F){auto& P=Floors.AddDefaulted_GetRef();P.Actor=F;P.Regions=F->FloorRegions;P.Source=EEHBOutlineSource::RetainedRegion;};
   auto PreserveSlab=[&](AEHB_FloorSlab* S){auto& P=Slabs.AddDefaulted_GetRef();P.Actor=S;P.Polygon=S->LocalTopPolygon;P.Source=EEHBOutlineSource::RetainedRegion;};
   // Already independent surfaces stay independent through every later edit.
   for(auto* E:Elements)
   {
    if(auto* F=Cast<AEHB_Floor>(E);F&&F->OutlineSource==EEHBOutlineSource::RetainedRegion)PreserveFloor(F);
    if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->OutlineSource==EEHBOutlineSource::RetainedRegion)PreserveSlab(S);
   }
   for(const auto& C:Transition.GetChanges())
   {
    if(C.Kind==EEHBRoomCoverageChange::Added||C.Kind==EEHBRoomCoverageChange::Split)return Fail(TEXT("UnexpectedWallRemovalRoomChange"));
    TArray<AEHB_Floor*> GroupFloors;TArray<AEHB_FloorSlab*> GroupSlabs;
    for(FGuid Id:C.BeforeRooms)
    {
     int32 NF=0,NS=0;
     for(auto* E:Elements)
     {
      if(auto* F=Cast<AEHB_Floor>(E);F&&F->RoomLoopGuid==Id){GroupFloors.Add(F);++NF;}
      if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->RoomFillLoopGuid==Id){GroupSlabs.Add(S);++NS;}
     }
     if(NF>1||NS>1)return Fail(TEXT("LayeredRoomMergeRequiresPlan"));
    }
    if(C.Kind==EEHBRoomCoverageChange::Removed)
    {for(auto* F:GroupFloors)PreserveFloor(F);for(auto* S:GroupSlabs)PreserveSlab(S);continue;}
    if((!GroupFloors.IsEmpty()&&GroupFloors.Num()!=C.BeforeRooms.Num())||(!GroupSlabs.IsEmpty()&&GroupSlabs.Num()!=C.BeforeRooms.Num()))return Fail(TEXT("IncompleteMergeFinishCoverage"));
    const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==C.AfterRooms[0];});if(!Room)return Fail(TEXT("MissingRemovalRoom"));
    if(!GroupFloors.IsEmpty())
    {
     auto* F=GroupFloors[0];const bool SameStyle=GroupFloors.ContainsByPredicate([&](const auto* V){return !Compatible(F,V);})==false;
     if(!SameStyle)
     {
      auto Destination=Room->Polygon;for(auto& V:Destination)V.Z=F->FloorRegions[0].OuterPolygon[0].Z;
      TArray<EHBRoomFinishPartition::FSource> Inputs;
      for(auto* A:GroupFloors){const auto* Old=Original.GetRooms().FindByPredicate([&](const auto& V){return V.RoomGuid==A->RoomLoopGuid;});if(!Old)return Fail(TEXT("MissingFinishPartitionRoom"));Inputs.Add({A->ElementGuid,Old->Polygon,A->FloorRegions[0].OuterPolygon});}
      TMap<FGuid,TArray<FVector>> Partitions;if(!EHBRoomFinishPartition::Build(Destination,Inputs,Partitions,Status))return false;
      // Whole-room centerline floors already tile the merged centerline domain;
      // retain their exact authored vertices as well as their separate styles.
      for(auto* A:GroupFloors)PreserveFloor(A);
     }
     else
     {
     for(int32 I=1;I<GroupFloors.Num();++I)Retired.Add(GroupFloors[I]);
     auto& P=Floors.AddDefaulted_GetRef();P.Actor=F;P.Room=Room->RoomGuid;P.Source=F->OutlineSource;P.Regions=F->FloorRegions;
     const double Z=P.Regions[0].OuterPolygon[0].Z;P.Regions[0].OuterPolygon=Room->Polygon;for(auto& V:P.Regions[0].OuterPolygon)V.Z=Z;
     Align(P.Regions[0].OuterPolygon,F->FloorRegions[0].OuterPolygon);if(!F->ValidateFloorRegions(P.Regions))return Fail(TEXT("InvalidMergedFloor"));
     }
    }
    if(!GroupSlabs.IsEmpty())
    {
     for(auto* A:GroupSlabs)if(A->DisplayPartition.IsActive())DisplayLayers.AddUnique(A);
     auto* S=GroupSlabs[0];const bool SameStyle=GroupSlabs.ContainsByPredicate([&](const auto* V){return !Compatible(S,V);})==false;
     if(!SameStyle)
     {
      for(auto* A:GroupSlabs)if(A->VisualExpansion>UE_KINDA_SMALL_NUMBER){DisplayLayers.AddUnique(A);break;}
      TArray<FVector> Destination;if(!FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(S,*Room,Candidate,Geometry.GetWallSides(),Destination))return Fail(TEXT("InvalidMergedSlab"));
      for(auto& V:Destination)V=S->GetElementLocalTransform().TransformPosition(V);
      TArray<EHBRoomFinishPartition::FSource> Inputs;
      for(auto* A:GroupSlabs)
      {
       const auto* Old=Original.GetRooms().FindByPredicate([&](const auto& V){return V.RoomGuid==A->RoomFillLoopGuid;});if(!Old)return Fail(TEXT("MissingFinishPartitionRoom"));
       auto Polygon=A->LocalTopPolygon;for(auto& V:Polygon)V=A->GetElementLocalTransform().TransformPosition(V);Inputs.Add({A->ElementGuid,Old->Polygon,MoveTemp(Polygon)});
      }
      TMap<FGuid,TArray<FVector>> Partitions;if(!EHBRoomFinishPartition::Build(Destination,Inputs,Partitions,Status))return false;
      for(auto* A:GroupSlabs)
      {
       auto& P=Slabs.AddDefaulted_GetRef();P.Actor=A;P.Source=EEHBOutlineSource::RetainedRegion;P.Polygon=Partitions.FindChecked(A->ElementGuid);
       for(auto& V:P.Polygon){V=A->GetElementLocalTransform().InverseTransformPosition(V);V.Z=A->GetTopZ();}Align(P.Polygon,A->LocalTopPolygon);
       if(!ValidateCandidateSlab(A,P.Polygon))return Fail(TEXT("InvalidPartitionedSlab"));
      }
     }
     else
     {
     for(int32 I=1;I<GroupSlabs.Num();++I){Retired.Add(GroupSlabs[I]);RemovedHosts.Add(GroupSlabs[I]->ElementGuid);}
     auto& P=Slabs.AddDefaulted_GetRef();P.Actor=S;P.Room=Room->RoomGuid;
     if(!FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(S,*Room,Candidate,Geometry.GetWallSides(),P.Polygon))return Fail(TEXT("InvalidMergedSlab"));
     Align(P.Polygon,S->LocalTopPolygon);if(!ValidateCandidateSlab(S,P.Polygon))return Fail(TEXT("InvalidMergedSlab"));
     P.Anchor=S->RoomFillAnchorWallGuid;P.Side=S->RoomFillAnchorWallSide;
     if(!Room->WallGuids.Contains(P.Anchor))
     {
      P.Anchor=Room->WallGuids[0];const auto* W=Candidate.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==P.Anchor;});if(!W)return Fail(TEXT("MissingMergedSlabAnchor"));
      P.Side=W->StartNodeGuid==Room->NodeGuids[0]?EEHBFloorSlabWallSide::Left:EEHBFloorSlabWallSide::Right;
     }
     }
    }
   }
   if(!AllocateDisplays(Status))return false;
   for(const auto& P:Floors)if(P.Actor->RoomLoopGuid!=P.Room||P.Actor->OutlineSource!=P.Source)AuthoredElements.AddUnique(P.Actor->ElementGuid);
   for(const auto& P:Slabs)if(P.Actor->RoomFillLoopGuid!=P.Room||P.Actor->OutlineSource!=P.Source||P.Actor->RoomFillAnchorWallGuid!=P.Anchor)AuthoredElements.AddUnique(P.Actor->ElementGuid);
   for(auto* E:Retired)RetiredIds.Add(E->ElementGuid);
   if(!EHBRoomFinishMove::BuildTopologyTops(Geometry,Tops,Status))return false;
   for(const auto& P:Slabs){FEHBFloorSupportSurface T;for(auto V:P.Polygon){V.Z=P.Actor->GetTopZ();T.OuterPolygon.Add(P.Actor->GetElementLocalTransform().TransformPosition(V));}Tops.Add(P.Actor->ElementGuid,{MoveTemp(T)});}
   for(auto& P:Floors)
   {
    FEHBFloorFinishContactDraft Draft;Draft.FloorGuid=P.Actor->ElementGuid;Draft.RoomGuid=P.Room;Draft.bIndependentRegion=P.Source==EEHBOutlineSource::RetainedRegion;Draft.FloorIndex=P.Actor->RoomFloorIndex;Draft.Regions=P.Regions;Draft.Hosts=Tops;
    for(const auto& R:B->ElementRelations)if(P.Actor->SurfaceFinishRelationGuids.Contains(R.RelationGuid))Draft.Previous.Add(R);
    if(!Draft.Build(P.Relations,Status))return false;
   }
   Status=TEXT("Ready");return true;
  }
  bool ApplyFinishes(AEHBBuildingActorBase* B,FName& Status)
  {
   auto Fail=[&](FName Why){Status=Why;return false;};
   for(const auto& P:Slabs)
   {
    P.Actor->RoomFillLoopGuid=P.Room;P.Actor->RoomFillFloorIndex=P.Actor->FloorIndex;
    P.Actor->bHasRoomFillAnchor=P.Anchor.IsValid();P.Actor->RoomFillAnchorWallGuid=P.Anchor;P.Actor->RoomFillAnchorWallSide=P.Side;
    bool Changed=false;if(P.Display.IsActive()){if(!P.Actor->SetPartitionedSlabOutline(P.Polygon,P.Display))return Fail(TEXT("MergedSlabDisplayApplyFailed"));}else if(!P.Actor->SetSlabOutlineIfNeeded(P.Polygon,Changed))return Fail(TEXT("MergedSlabApplyFailed"));P.Actor->RecordOutlineSource(P.Source);
   }
   if(FailAt(4))return Fail(TEXT("InjectedRemovalSlabFailure"));
   if(!VerifyDisplays(B,Status))return false;
   for(const auto& P:Floors)
   {
    P.Actor->RoomLoopGuid=P.Room;P.Actor->RoomFloorIndex=P.Actor->FloorIndex;bool Changed=false;
    if(!P.Actor->SetFloorRegionsIfNeeded(P.Regions,Changed))return Fail(TEXT("MergedFloorApplyFailed"));P.Actor->RecordOutlineSource(P.Source);
   }
   if(FailAt(5))return Fail(TEXT("InjectedRemovalFloorFailure"));
   for(const auto& Pair:Tops){TArray<FEHBFloorSupportSurface> Actual;if(!FEHBFloorContactGeometry::CaptureHorizontalTops(B->FindElementActorByGuid(Pair.Key),Actual,Status)||!EHBRoomFinishMove::SameTopCoverage(Pair.Value,Actual,Status))return false;}
   for(const auto& P:Floors)
   {
    FEHBBuildingClosedLoop Room;TArray<FEHBElementRelation> Actual;
    if(!P.Actor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,Actual,Status)||Actual.Num()!=P.Relations.Num())return Fail(TEXT("MergedFinishContactMismatch"));
    const auto Previous=P.Actor->SurfaceFinishRelationGuids;TArray<FGuid> Next;
    for(auto R:P.Relations)
    {
     const auto* A=Actual.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});
     if(!A||FMath::Abs(A->ContactArea-R.ContactArea)>0.01)return Fail(TEXT("MergedFinishContactMismatch"));
     R.ContactPoint=A->ContactPoint;R.ContactArea=A->ContactArea;R.ContactNormal=A->ContactNormal;
     if(B->AddOrUpdateElementRelation(R,true)!=R.RelationGuid)return Fail(TEXT("MergedFinishPublishFailed"));Next.Add(R.RelationGuid);
    }
    for(FGuid Id:Previous)if(!Next.Contains(Id)&&!B->RemoveElementRelation(Id))return Fail(TEXT("MergedFinishRemovalFailed"));P.Actor->SurfaceFinishRelationGuids=MoveTemp(Next);
   }
   if(FailAt(6))return Fail(TEXT("InjectedRemovalContactFailure"));
   FEHBWallNodeModel Actual;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual);if(!Capture.bSucceeded)return Fail(Capture.Status);
   const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Actual,B->QueryElements(FEHBElementQuery()),true);if(!Policy.bSucceeded)return Fail(Policy.Status);
   Status=TEXT("Applied");return true;
  }
 };
}

FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* B,const TArray<FGuid>& WallGuids,int32 ExpectedGraphRevision,bool Preview)
{
 FEHBToolsetOperationResult Result;auto Fail=[&](FName Why){Result.Message=Why.ToString();return Result;};
 if(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()||GIsTransacting)return Fail(TEXT("RequiresIndependentEditorTransaction"));
 if(!IsValid(B)||B->GetClass()!=AEHB_Building::StaticClass()||B->GetWorld()!=GEditor->GetEditorWorldContext().World()||B->GetAttachParentActor()||B->IsActorBeingDestroyed())return Fail(TEXT("RequiresNativeEditorBuilding"));
 if(B->WallNodeAuthority.Version!=2)return Fail(TEXT("RequiresOptionalNodeAuthority"));
 if(B->RelationshipGraphRevision!=ExpectedGraphRevision)return Fail(TEXT("StaleRemovalGraphRevision"));
 if(B->IsChangeNotificationBusy())return Fail(TEXT("ChangeNotificationBusy"));
 if(FLevelUtils::IsLevelLocked(B->GetLevel())||!FLevelUtils::IsLevelVisible(B->GetLevel()))return Fail(TEXT("RequiresVisibleUnlockedLevel"));
 TSet<FGuid> Unique;for(FGuid Id:WallGuids){if(!Id.IsValid()||Unique.Contains(Id))return Fail(TEXT("InvalidRemovalWallList"));Unique.Add(Id);}if(Unique.IsEmpty())return Fail(TEXT("EmptyRemovalWallList"));
 bool Selected=GEditor->GetSelectedActors()->Num()==1&&GEditor->GetSelectedActors()->IsSelected(B);
 if(!Selected&&GEditor->GetSelectedActors()->Num()==Unique.Num())
 {
  Selected=true;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It){auto* W=Cast<AEHB_Wall>(*It);if(!W||W->OwningBuilding!=B||!Unique.Contains(W->ElementGuid)){Selected=false;break;}}
 }
 if(!Selected)return Fail(TEXT("TargetNotSelected"));
 const auto Elements=B->QueryElements(FEHBElementQuery());TArray<AActor*> Attached;B->GetAttachedActors(Attached,true,true);
 if(Attached.Num()!=Elements.Num())return Fail(TEXT("IncompleteNativeGroup"));
 for(auto* E:Elements)if(!Attached.Contains(E)||E->GetAttachParentActor()!=B||E->GetLevel()!=B->GetLevel()
  ||(E->GetClass()!=AEHB_Pillar::StaticClass()&&E->GetClass()!=AEHB_Wall::StaticClass()&&E->GetClass()!=AEHB_Floor::StaticClass()&&E->GetClass()!=AEHB_FloorSlab::StaticClass())||!E->GetInstanceComponents().IsEmpty())return Fail(TEXT("RequiresWallRemovalDependencyPlan"));
 for(auto* C:B->GetInstanceComponents())if(IsValid(C)&&(!Cast<UEHBWallJunctionComponent>(C)||!C->ComponentHasTag(TEXT("EHB.NodeAuthorityDerived"))))return Fail(TEXT("CustomBuildingComponentRequiresPlan"));
 auto Requested=WallGuids;Requested.Sort();FPlan Plan;FName Reason;if(!Plan.Prepare(B,Requested,Elements,Reason))return Fail(Reason);
 if(Preview){Result.bSucceeded=true;Result.Message=TEXT("Ready");return Result;}
 FEHBChangeNotificationBatch Notifications(*B);if(!Notifications.IsActive())return Fail(TEXT("ChangeNotificationBusy"));
 bool Applied=true;
 {
  FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","RemoveWallsAndMergeRooms","Remove Walls And Merge Rooms"));
  B->SetFlags(RF_Transactional);B->Modify();for(auto* E:Elements){E->SetFlags(RF_Transactional);E->Modify();TInlineComponentArray<UActorComponent*> Components(E);for(auto* C:Components){C->SetFlags(RF_Transactional);C->Modify();}}
  // Retire affected finish references before either endpoint disappears. Other
  // retained host-floor pairs keep their original relation identities.
  TArray<FGuid> Remove;for(const auto& R:B->ElementRelations)if(Plan.OwnedRelations.Contains(R.RelationGuid)&&(Plan.RemovedHosts.Contains(R.Source.ElementGuid)||Plan.RetiredIds.Contains(R.Target.ElementGuid)))Remove.Add(R.RelationGuid);
  for(FGuid Id:Remove)
  {
   const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;});
   if(R)if(auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(R->Target.ElementGuid)))F->SurfaceFinishRelationGuids.Remove(Id);
   Applied&=B->RemoveElementRelation(Id);
  }
  auto Destroy=[&](AEHBElementActorBase* E){B->UnregisterElementActor(E);E->OwningBuilding=nullptr;E->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);return E->Destroy();};
  // Unregister removes the wall's typed topology relations by element identity.
  // Legacy physical endpoint fields may both be empty on an actor-free junction.
  for(auto* W:Plan.Walls)if(!Destroy(W)){Applied=false;Reason=TEXT("RemovalWallDestroyFailed");break;}
  if(Applied&&FailAt(1)){Applied=false;Reason=TEXT("InjectedRemovalWallFailure");}
  if(Applied)for(auto* E:Plan.Retired)Applied&=Destroy(E);
  if(Applied&&FailAt(2)){Applied=false;Reason=TEXT("InjectedRemovalRetirementFailure");}
  if(Applied){B->WallNodeAuthority.Nodes=Plan.Candidate.Nodes;B->RebuildElementAndRelationshipIndexes();Applied=B->RebuildWallNodeAuthorityGeometry();}
  if(Applied&&FailAt(3)){Applied=false;Reason=TEXT("InjectedRemovalGeometryFailure");}
  if(Applied)Applied=Plan.ApplyFinishes(B,Reason);
  if(Applied)
  {
   FEHBWallNodeModel Actual;Applied=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual).bSucceeded;
   FString A,Z;if(Applied){FJsonObjectConverter::UStructToJsonObjectString(Plan.Candidate,A);FJsonObjectConverter::UStructToJsonObjectString(Actual,Z);Applied=A==Z;}
   TArray<FEHBNodeRoomBoundary> Rooms;if(Applied)Applied=FEHBWallNodeRooms::Build(B->BuildingGuid,Actual,Rooms,Reason)&&Rooms.Num()==Plan.Rooms.Num();
   for(const auto& R:Plan.Rooms)Applied&=Rooms.ContainsByPredicate([&](const auto& V){return V.RoomGuid==R.RoomGuid&&V.FloorIndex==R.FloorIndex&&FMath::IsNearlyEqual(V.Area,R.Area,0.01);});
   const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);Applied&=Graph.Issues.IsEmpty();
   if(Applied){B->TopologyMigrationBaseline.Version=1;B->TopologyMigrationBaseline.Nodes=Graph.Nodes;B->TopologyMigrationBaseline.Walls=Graph.Walls;B->PreparedWallNodeDefinitions={};}
  }
  if(Applied){for(auto* E:Elements)if(IsValid(E)&&!E->IsActorBeingDestroyed())E->SynchronizePlannedEditorMove();Applied=Notifications.RecordCommittedEdit(TEXT("RemoveWalls"),Plan.NodeIds,Plan.RoomIds,Plan.AuthoredElements);}
  if(Applied&&FailAt(7)){Applied=false;Reason=TEXT("InjectedRemovalReceiptFailure");}
  B->MarkPackageDirty();
 }
 if(!Applied)
 {
  UE_LOG(LogTemp,Warning,TEXT("EHB wall removal rejected during apply: %s"),*Reason.ToString());
  const bool Restored=GEditor->UndoTransaction(false);if(Restored){B->RebuildElementAndRelationshipIndexes();Notifications.Rollback();}else Notifications.Publish();
  return Fail(Restored?TEXT("WallRemovalFailedRolledBack"):TEXT("WallRemovalRollbackFailed"));
 }
 Result.bSucceeded=true;Result.Message=TEXT("Committed");Result.CommittedEdit=B->LastCommittedEdit;Notifications.Publish();GEditor->RedrawLevelEditingViewports();return Result;
}
}
