#pragma once
#include "CoreMinimal.h"
#include "Actors/EHB_Floor.h"
struct FEHBWallJunctionMesh;
class AEHB_FloorSlab;
class AEHB_Pillar;

struct FEHBFloorContact
{
 double Area=0;
 FVector Point=FVector::ZeroVector;
};

/** Planar polygon intersection in building-local centimeters. Quantization: 0.001 cm.
 * A successful zero-area result means no contact; false means invalid/uncomputable input. */
struct EASYHOUSEBUILDER_API FEHBFloorContactGeometry
{
 /** Native, planar slab underside from structural source; excludes visual expansion.
  * Ground-conforming foundations and tilted frames require another domain solver. */
 static bool CaptureSlabBottom(const AEHB_FloorSlab* Slab,TArray<FEHBFloorFinishRegion>& Out,FName& Status);
 /** Native polygon-column structural underside, independent of display mesh. */
 static bool CapturePillarBottom(const AEHB_Pillar* Pillar,TArray<FEHBFloorFinishRegion>& Out,FName& Status);
 static bool CaptureHorizontalBottomsFromMesh(const FEHBWallJunctionMesh& Mesh,
  const FTransform& ElementToWorld,const FTransform& BuildingToWorld,
  TArray<FEHBFloorFinishRegion>& Out,FName& Status);
 /** Value-only capture for candidate geometry, before an actor or component is changed.
  * Both transforms are world frames; output is in building-local centimeters. */
 static bool CaptureHorizontalTopsFromMesh(const FEHBWallJunctionMesh& Mesh,
  const FTransform& ElementToWorld,const FTransform& BuildingToWorld,
  TArray<FEHBFloorSupportSurface>& Out,FName& Status);
 static bool Build(const TArray<FEHBFloorFinishRegion>& Coverage,
  const TArray<FEHBFloorSupportSurface>& Tops,FEHBFloorContact& Out,FName& Status);
 /** Same validated clipping/union, without triangulating a representative point. */
 static bool MeasureArea(const TArray<FEHBFloorFinishRegion>& Coverage,
  const TArray<FEHBFloorSupportSurface>& Tops,double& OutArea,FName& Status);
 /** Legacy/generated-source comparison and compatibility path. */
 static bool CaptureGeneratedHorizontalTops(const AEHBElementActorBase* Host,TArray<FEHBFloorSupportSurface>& Out,FName& Status);
 static bool CaptureHorizontalTops(const AEHBElementActorBase* Host,
  TArray<FEHBFloorSupportSurface>& Out,FName& Status);
};

/** Diagnostic counters for derived contact solves, not model/transaction revisions. */
struct FEHBFloorContactCacheStats
{
 uint64 Solves = 0, Hits = 0, Evictions = 0;
 int32 Entries = 0;
 int64 StoredPoints = 0;
};

/** Exact value cache: callers must capture current logical/generated geometry on every query.
 * No relation IDs or actor references are cached. Failed or oversized inputs never retain old results. */
class EASYHOUSEBUILDER_API FEHBFloorContactCache
{
public:
 explicit FEHBFloorContactCache(int32 InMaxEntries=128,int64 InMaxPoints=262144)
  : MaxEntries(FMath::Max(0,InMaxEntries)),MaxPoints(FMath::Max<int64>(0,InMaxPoints)) {}
 bool Query(FGuid Host,const TArray<FEHBFloorFinishRegion>& Coverage,
  const TArray<FEHBFloorSupportSurface>& Tops,FEHBFloorContact& Out,FName& Status);
 void RetainHosts(const TSet<FGuid>& Hosts);
 FEHBFloorContactCacheStats GetStats() const { return Stats; }
private:
 struct FEntry
 {
  TArray<FEHBFloorFinishRegion> Coverage;
  TArray<FEHBFloorSupportSurface> Tops;
  FEHBFloorContact Contact;
  FName Status;
  int64 Points=0;
 };
 TMap<FGuid,FEntry> Entries;
 FEHBFloorContactCacheStats Stats;
 int32 MaxEntries;
 int64 MaxPoints;
 void Remove(FGuid Host);
};

/** Value-only finish destination; can target a future physical floor ID.
 * Callers validate source ownership/complete caches before supplying Previous.
 * No actor, graph, component, or persistent cache is read or changed here. */
struct EASYHOUSEBUILDER_API FEHBFloorFinishContactDraft
{
 FGuid FloorGuid,RoomGuid;
 // Explicit independent region, never an invalid/missing room fallback.
 bool bIndependentRegion=false;
 int32 FloorIndex=INDEX_NONE;
 TArray<FEHBFloorFinishRegion> Regions;
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> Hosts;
 TArray<FEHBElementRelation> Previous;
 bool Build(TArray<FEHBElementRelation>& Out,FName& Status) const;
};
