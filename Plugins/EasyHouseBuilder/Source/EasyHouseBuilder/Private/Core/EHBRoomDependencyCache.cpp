#include "Core/EHBRoomDependencyCache.h"

void FEHBRoomDependencyCache::Add(const FEHBRoomDependencyBinding& V)
{
 if(V.RoomGuid.IsValid())(V.Kind==EEHBRoomDependencyKind::Floor?FloorsByRoom:SlabsByRoom).FindOrAdd(V.RoomGuid).AddUnique(V.ElementGuid);
 else if(V.Kind==EEHBRoomDependencyKind::Slab&&V.AnchorWallGuid.IsValid())SlabsByAnchor.FindOrAdd(V.AnchorWallGuid).AddUnique(V.ElementGuid);
}
void FEHBRoomDependencyCache::Remove(const FEHBRoomDependencyBinding& V)
{
 auto RemoveId=[&](auto& Map,FGuid Key){if(auto* Values=Map.Find(Key)){Values->Remove(V.ElementGuid);if(Values->IsEmpty())Map.Remove(Key);}};
 if(V.RoomGuid.IsValid())RemoveId(V.Kind==EEHBRoomDependencyKind::Floor?FloorsByRoom:SlabsByRoom,V.RoomGuid);
 else if(V.Kind==EEHBRoomDependencyKind::Slab&&V.AnchorWallGuid.IsValid())RemoveId(SlabsByAnchor,V.AnchorWallGuid);
}
bool FEHBRoomDependencyCache::Update(FGuid Building,const FEHBCommittedEdit& Receipt,const TArray<FEHBRoomDependencyBinding>& Input,bool bEditInFlight)
{
 Stats.SourceEntriesRead+=Input.Num();TMap<FGuid,FEHBRoomDependencyBinding> Current;
 for(const auto& V:Input)
 {
  if(!Building.IsValid()||!V.ElementGuid.IsValid()||Current.Contains(V.ElementGuid))
  {bValid=false;Sources.Reset();FloorsByRoom.Reset();SlabsByRoom.Reset();SlabsByAnchor.Reset();Stats.LastReason=TEXT("InvalidBindings");return false;}
  Current.Add(V.ElementGuid,V);
 }
 if(!Building.IsValid()){bValid=false;Stats.LastReason=TEXT("InvalidBuilding");return false;}
 const FGuid NewState=Receipt.BuildingGuid==Building?Receipt.StateId:FGuid();
 TSet<FGuid> Changed;
 for(const auto& Pair:Sources){const auto* V=Current.Find(Pair.Key);if(!V||!(*V==Pair.Value))Changed.Add(Pair.Key);}
 for(const auto& Pair:Current)if(!Sources.Contains(Pair.Key))Changed.Add(Pair.Key);
 if(bValid&&BuildingGuid==Building&&!bEditInFlight&&StateId==NewState&&Changed.IsEmpty())
 {++Stats.Reuses;Stats.LastReason=TEXT("Reused");return true;}
 const bool bContinuous=bValid&&BuildingGuid==Building&&!bEditInFlight&&NewState.IsValid()&&NewState!=StateId&&Receipt.ParentStateId==StateId&&!Receipt.bRequiresFullSpatialRefresh;
 bool bCovered=bContinuous;
 for(FGuid Id:Changed)if(!Receipt.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==Id;})){bCovered=false;break;}
 if(bCovered)
 {
  for(FGuid Id:Changed){if(const auto* Old=Sources.Find(Id))Remove(*Old);if(const auto* V=Current.Find(Id))Add(*V);}
  ++Stats.ReceiptUpdates;Stats.LastReason=TEXT("ReceiptAdvanced");
 }
 else
 {
  Stats.LastReason=!bValid?TEXT("ColdBuild"):BuildingGuid!=Building?TEXT("BuildingChanged"):bEditInFlight?TEXT("EditInFlight"):StateId==NewState?TEXT("LegacyBindingChanged"):Receipt.bRequiresFullSpatialRefresh?TEXT("FullSpatialRefresh"):bContinuous?TEXT("ReceiptMissingBindingCoverage"):TEXT("ReceiptGap");
  FloorsByRoom.Reset();SlabsByRoom.Reset();SlabsByAnchor.Reset();for(const auto& Pair:Current)Add(Pair.Value);++Stats.FullBuilds;
 }
 Sources=MoveTemp(Current);BuildingGuid=Building;StateId=NewState;bValid=true;return true;
}
FEHBRoomDependencyMembers FEHBRoomDependencyCache::Query(FGuid Room,const TArray<FGuid>& Walls) const
{
 FEHBRoomDependencyMembers Result;Result.RoomGuid=Room;if(!bValid)return Result;
 if(const auto* V=FloorsByRoom.Find(Room))Result.Floors=*V;
 if(const auto* V=SlabsByRoom.Find(Room))Result.Slabs=*V;
 for(FGuid Wall:Walls)if(const auto* V=SlabsByAnchor.Find(Wall))for(FGuid Id:*V)Result.AnchoredSlabs.AddUnique(Id);
 Result.Floors.Sort();Result.Slabs.Sort();Result.AnchoredSlabs.Sort();return Result;
}
