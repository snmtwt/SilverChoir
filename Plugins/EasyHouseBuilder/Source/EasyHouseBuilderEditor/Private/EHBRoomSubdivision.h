#pragma once
#include "Actors/EHB_Floor.h"
#include "EHBSlabDisplayPlan.h"
#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBWallPathPlanning.h"
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBWallNodeGeometryDraft.h"
class AEHB_FloorSlab;
class AEHB_Pillar;
class AEHB_Wall;
struct FEHBWallPathPreview;

// One dependency plan spans the ordered split sequence and complete creation route.
// Node and physical-element slots remain separate; actual command outputs map them.
struct FEHBRoomSubdivision
{
 struct FSplit { AEHB_Wall* Source=nullptr; FEHBWallSplitPreview Preview; int32 RequestIndex=INDEX_NONE; bool bCreatePhysicalColumn=true; };
 struct FSplitSlots { FGuid Node,Pillar,Left,Right; int32 RequestIndex=INDEX_NONE; };
 struct FPath
 {
  FEHBWallPathPlan Route;
  TArray<FEHBWallCreationEndpoint> Endpoints;
  TArray<int32> SourceEndpoints;
  FEHBWallCreationOptions Options;
  TArray<FGuid> ActualPoints,ActualNodes,ActualWalls;
 };
 struct FFloor { AEHB_Floor* Actor=nullptr; FGuid Id,Room; bool bNew=false,bRebound=false; TArray<FEHBFloorFinishRegion> Regions; TArray<FEHBElementRelation> Relations; EEHBOutlineSource Source; };
 struct FSlab { AEHB_FloorSlab* Actor=nullptr; FGuid Id,Room,Anchor; EEHBFloorSlabWallSide Side=EEHBFloorSlabWallSide::None; bool bNew=false,bRebound=false; TArray<FVector> Polygon; EEHBOutlineSource Source=EEHBOutlineSource::RoomBoundary; };
 TArray<FSplitSlots> SplitSlots;
 TMap<FGuid,FGuid> SourceStartSegments;
 TSet<FGuid> RemovedHosts,NewHosts;
 TArray<FGuid> PathNodes,PathPillars,PathWalls;
 TArray<FFloor> Floors;
 TArray<FSlab> Slabs;
 TArray<FGuid> Rooms;
 TArray<FEHBNodeRoomBoundary> PlannedRooms;
 int32 PlannedRoomCount=0;
 TSet<FGuid> FinishRelations;
 TSet<FGuid> PreservedOpeningWalls;
 FEHBWallNodeModel Candidate;
 FEHBWallNodeGeometryDraft CandidateGeometry;
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;
 FEHBSlabDisplayPlan Displays;
 bool Prepare(AEHBBuildingActorBase* Building,AEHB_Wall* Wall,const FEHBWallSplitPreview& Split,const TArray<AEHBElementActorBase*>& Elements,FName& Status);
 bool PrepareMultiple(AEHBBuildingActorBase* Building,const TArray<FSplit>& Splits,const TArray<AEHBElementActorBase*>& Elements,FName& Status,const FPath* Path=nullptr,const TSet<FGuid>* ValidatedRetainedOpeningWalls=nullptr);
 bool DetachRemovedHost(AEHBBuildingActorBase* Building);
 bool Apply(AEHBBuildingActorBase* Building,const TMap<FGuid,FGuid>& SplitIds,FName& Status,const FPath* Path=nullptr);
 // Materialize a prepared V2 path; completed split slots map to actual results.
 bool MaterializePath(AEHBBuildingActorBase* Building,FPath& Path,TArray<FEHBWallCreationEndpoint>& Endpoints,TArray<AEHB_Wall*>& Walls,FName& Status,const TMap<FGuid,FGuid>& SplitIds={});
 void ExportPreview(FEHBWallPathPreview& Preview) const;
 bool Contains(const AEHBElementActorBase* Element) const;
};
