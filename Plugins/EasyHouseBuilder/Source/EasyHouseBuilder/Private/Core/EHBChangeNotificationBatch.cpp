#include "Core/EHBChangeNotificationBatch.h"
#include "Core/EHBBuildingActorBase.h"
#include "Actors/EHBElementActorBase.h"

FEHBChangeNotificationBatch::FEHBChangeNotificationBatch(AEHBBuildingActorBase& InBuilding)
{
 if(InBuilding.ActiveChangeNotificationBatch||InBuilding.bPublishingChangeNotifications)return;
 Building=&InBuilding;bActive=true;
 OriginalEdit=InBuilding.LastCommittedEdit;
 for(auto* E:InBuilding.QueryElements(FEHBElementQuery()))OriginalBounds.Add(E->ElementGuid,E->GetBuildingLocalBounds());
 OriginalGraphRevision=InBuilding.RelationshipGraphRevision;
 OriginalGeometryRevisions=InBuilding.ElementGeometryRevisionByGuid;
 for(const auto& R:InBuilding.ElementRelations)
 {
  OriginalRelationRevisions.Add(R.RelationGuid,{R.SourceGeometryRevision,R.TargetGeometryRevision});
  auto& Elements=OriginalRelationElements.Add(R.RelationGuid);
  for(const auto* Endpoint:{&R.Source,&R.Target})
  {
   FGuid Id=Endpoint->Kind==EEHBRelationEndpointKind::BuildingElement?Endpoint->ElementGuid:Endpoint->Kind==EEHBRelationEndpointKind::WallNode?InBuilding.FindPhysicalPillarForNode(Endpoint->NodeGuid):FGuid();
   if(Id.IsValid())Elements.AddUnique(Id);
  }
 }
 InBuilding.ActiveChangeNotificationBatch=this;
}
FEHBChangeNotificationBatch::~FEHBChangeNotificationBatch()
{
 // If a caller exits without restoring its transaction, report actual changes.
 // Never pretend that discarding events also rolls back geometry.
 Publish();
}
bool FEHBChangeNotificationBatch::RecordCommittedEdit(FName Command,const TArray<FGuid>& Nodes,const TArray<FGuid>& Rooms,const TArray<FGuid>& AuthoredElements)
{
 auto* B=Building.Get();if(!bActive||!B||bRecordedEdit||Command.IsNone()||OriginalEdit.Sequence==MAX_int64)return false;
 FEHBCommittedEdit Edit;Edit.Sequence=OriginalEdit.Sequence+1;Edit.BuildingGuid=B->BuildingGuid;
 Edit.StateId=FGuid::NewGuid();Edit.ParentStateId=OriginalEdit.StateId;Edit.Command=Command;
 for(FGuid Id:Nodes)if(Id.IsValid())Edit.NodeGuids.AddUnique(Id);Edit.NodeGuids.Sort();
 for(FGuid Id:Rooms)if(Id.IsValid())Edit.RoomGuids.AddUnique(Id);Edit.RoomGuids.Sort();
 TSet<FGuid> Elements;for(const auto& Pair:ChangedGeometry)Elements.Add(Pair.Key);
 // Creation/deletion may have no geometry notification (or any relation at all).
 // Compare membership and bounds inside the committed transaction so the receipt
 // includes both old occupied regions and newly occupied regions.
 TMap<FGuid,FBox> CurrentBounds;
 for(const auto* E:B->QueryElements(FEHBElementQuery()))
 {
  const FBox Bounds=E->GetBuildingLocalBounds();CurrentBounds.Add(E->ElementGuid,Bounds);
  const auto* Before=OriginalBounds.Find(E->ElementGuid);
  if(!Before||Before->IsValid!=Bounds.IsValid||Before->Min!=Bounds.Min||Before->Max!=Bounds.Max)Elements.Add(E->ElementGuid);
 }
 for(const auto& Pair:OriginalBounds)if(!CurrentBounds.Contains(Pair.Key))Elements.Add(Pair.Key);
 for(FGuid Id:AuthoredElements)
 {
  if(!Id.IsValid()||(!OriginalBounds.Contains(Id)&&!CurrentBounds.Contains(Id)))return false;
  Elements.Add(Id);
 }

 for(FGuid Id:ChangedRelations)
 {
  if(const auto* Old=OriginalRelationElements.Find(Id))for(FGuid E:*Old)Elements.Add(E);
  if(const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;}))
  {
   Edit.UpdatedRelationGuids.Add(Id);
   for(const auto* Endpoint:{&R->Source,&R->Target})
   {
    const FGuid E=Endpoint->Kind==EEHBRelationEndpointKind::BuildingElement?Endpoint->ElementGuid:Endpoint->Kind==EEHBRelationEndpointKind::WallNode?B->FindPhysicalPillarForNode(Endpoint->NodeGuid):FGuid();
    if(E.IsValid())Elements.Add(E);else Edit.bRequiresFullSpatialRefresh=true;
   }
  }
  else if(OriginalRelationRevisions.Contains(Id))Edit.RemovedRelationGuids.Add(Id);
 }
 Edit.UpdatedRelationGuids.Sort();Edit.RemovedRelationGuids.Sort();
 TArray<FGuid> Sorted=Elements.Array();Sorted.Sort();
 for(FGuid Id:Sorted)
 {
  // Objects created and consumed inside this transaction have no external lifetime.
  if(!OriginalBounds.Contains(Id)&&!CurrentBounds.Contains(Id))continue;
  auto& Change=Edit.Elements.AddDefaulted_GetRef();Change.ElementGuid=Id;
  if(const auto* Before=OriginalBounds.Find(Id)){Change.BeforeBounds=*Before;if(!Before->IsValid)Edit.bRequiresFullSpatialRefresh=true;}
  if(const auto* After=CurrentBounds.Find(Id)){Change.AfterBounds=*After;if(!Change.AfterBounds.IsValid)Edit.bRequiresFullSpatialRefresh=true;}
 }
 B->Modify();B->LastCommittedEdit=MoveTemp(Edit);bRecordedEdit=true;return true;
}
void FEHBChangeNotificationBatch::Publish()
{
 auto* B=Building.Get();if(!bActive||!B)return;
 bActive=false;B->ActiveChangeNotificationBatch=nullptr;
 TGuardValue<bool> Publishing(B->bPublishingChangeNotifications,true);
 if(bRecordedEdit){const FEHBCommittedEdit Receipt=B->LastCommittedEdit;B->OnEditCommitted.Broadcast(Receipt);}
 TArray<FGuid> Relations=ChangedRelations.Array();Relations.Sort();
 for(FGuid Id:Relations)
 {
  const bool Present=B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Id;});
  if(Present)B->OnElementRelationAdded.Broadcast(Id);
  else if(OriginalRelationRevisions.Contains(Id))B->OnElementRelationRemoved.Broadcast(Id);
 }
 TArray<FGuid> Geometry;ChangedGeometry.GetKeys(Geometry);Geometry.Sort();
 for(FGuid Id:Geometry)if(B->FindElementActorByGuid(Id))B->OnElementGeometryChanged.Broadcast(Id,ChangedGeometry.FindChecked(Id));
}
void FEHBChangeNotificationBatch::Rollback()
{
 auto* B=Building.Get();if(!bActive||!B)return;
 B->LastCommittedEdit=OriginalEdit;
 B->RelationshipGraphRevision=OriginalGraphRevision;
 B->ElementGeometryRevisionByGuid=OriginalGeometryRevisions;
 for(auto& R:B->ElementRelations)if(const auto* Revisions=OriginalRelationRevisions.Find(R.RelationGuid))
 {R.SourceGeometryRevision=Revisions->Key;R.TargetGeometryRevision=Revisions->Value;}
 B->ActiveChangeNotificationBatch=nullptr;bActive=false;
}
