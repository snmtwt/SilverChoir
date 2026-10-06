#pragma once
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Core/EHBWallNodeRooms.h"
#include "Actors/EHB_Floor.h"
#include "EHBSlabDisplayPlan.h"
#include "Core/EHBPreparedWallOpening.h"
class AEHB_FloorSlab;
class AEHB_DoorWindow;
class FEHBWallNodeGeometryDraft;
namespace EHBRoomFinishMove
{
 bool BuildTopologyTops(const FEHBWallNodeModel& Definitions,
  TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status);
 // Consumes an immutable validated solve; no caller-provided model/side mismatch.
 bool BuildTopologyTops(const FEHBWallNodeGeometryDraft& Geometry,
  TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status);
 bool BuildTopologyTops(const FEHBWallNodeSideDraft& Geometry,
  TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status);
 bool SameTopCoverage(const TArray<FEHBFloorSupportSurface>& Planned,
  const TArray<FEHBFloorSupportSurface>& Actual,FName& Status);

 /** Room identity is unchanged; node geometry and physical finish contacts are planned separately. */
 struct FNodeEditPlan
 {
  struct FFloor { AEHB_Floor* Actor=nullptr; TArray<FEHBFloorFinishRegion> Regions; TArray<FEHBElementRelation> Relations; EEHBOutlineSource Source=EEHBOutlineSource::ManualOrUnclassified; };
  struct FSlab { AEHB_FloorSlab* Actor=nullptr; TArray<FVector> Polygon; };
  TArray<FFloor> Floors;
  TArray<FSlab> Slabs;
  FEHBSlabDisplayPlan Displays;
  TSet<FGuid> OwnedRelations,RemovedHosts;
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;
  TSet<FGuid> SupportRelationIds;
  TArray<FEHBElementRelation> SupportRelations;
  TArray<FEHBElementRelation> OriginalSupportRelations;
  TArray<FGuid> RemovedSupportRelations;
  TSet<FGuid> HostedRelationIds;
  bool Prepare(AEHBBuildingActorBase* Building,const FEHBWallNodeModel& Before,const FEHBWallNodeModel& After,
   const TArray<AEHBElementActorBase*>& Elements,FGuid RemovedPhysical,FName& Status,bool bFixedSlabSources=false);
  const TArray<FEHBNodeRoomBoundary>& GetRooms() const { return Rooms; }
  const FEHBWallNodeSideDraft& GetCandidateSides() const { return CandidateSides; }
  const TMap<FGuid,FEHBPreparedWallOpening>& GetWallOpenings() const { return WallOpenings; }
  void DetachRemovedHostContacts(AEHBBuildingActorBase* Building) const;
  // Changed rooms are published only after every apply postcondition succeeds.
  bool Apply(AEHBBuildingActorBase* Building,FName& Status,bool(*Failure)(int32)=nullptr,TArray<FGuid>* OutChangedRooms=nullptr) const;
 private:
  bool bFixedSourcePreparationOnly=false;
  TArray<FEHBNodeRoomBoundary> OldRooms,Rooms;
  FEHBWallNodeSideDraft CandidateSides;
  bool PrepareSupportRelations(AEHBBuildingActorBase* Building,const TArray<AEHBElementActorBase*>& Elements,FName& Status);
  struct FHosted { AEHB_DoorWindow* Actor=nullptr;FGuid Wall,Relation;float Distance=0;FTransform World; };
  TArray<FHosted> Hosted;
  bool PrepareHostedElements(AEHBBuildingActorBase* Building,const TArray<AEHBElementActorBase*>& Elements,FName& Status);
  bool VerifyHostedElements(AEHBBuildingActorBase* Building,FName& Status) const;
  bool CaptureSupportBottom(AEHBElementActorBase* Element,bool Candidate,TArray<FEHBFloorFinishRegion>& Out,FName& Status) const;
  // Candidate evidence is consumed only within the node edit transaction.
  TMap<FGuid,FEHBPreparedWallOpening> WallOpenings;
  TSet<FGuid> RemovedContactRooms;
 };
}
