#pragma once
#include "Core/EHBWallNodeRooms.h"

struct FEHBRoomSubdivision;

enum class EEHBRoomCoverageChange : uint8
{
 Retained, Split, Merged, Removed, Added
};

/** A coverage correspondence, not permission to delete or merge authored finishes.
 * IDs refer to the actual before/after room queries, never newly allocated aliases.
 * Within either side, the largest room comes first; ties use geometry, not GUIDs.
 * This gives split consumers a retained child and merge consumers a preferred source,
 * but conflicting materials, anchors, hosted objects and support still need a plan. */
struct FEHBRoomCoverageChange
{
 EEHBRoomCoverageChange Kind=EEHBRoomCoverageChange::Retained;
 int32 FloorIndex=INDEX_NONE;
 TArray<FGuid> BeforeRooms,AfterRooms;
};

/** Value-only correspondence for coverage-preserving boundary edits and completely
 * added/removed rooms. Partial loss/growth and many-to-many repartition are rejected.
 * No Actor access, ID allocation, saved schema or cache. Failure yields an empty plan. */
class EASYHOUSEBUILDER_API FEHBRoomCoverageTransition
{
public:
 static FEHBRoomCoverageTransition Build(FGuid BuildingGuid,const FEHBWallNodeModel& Before,
  const FEHBWallNodeModel& After,FName& Status);
 bool IsReady() const { return bReady; }
 const TArray<FEHBRoomCoverageChange>& GetChanges() const { return Changes; }
private:
 // The existing subdivision adapter already owns validated before/after face queries.
 // Keep this private so arbitrary raw polygons cannot bypass model validation.
 friend struct FEHBRoomSubdivision;
 static FEHBRoomCoverageTransition BuildFromValidatedRooms(const TArray<FEHBNodeRoomBoundary>& Before,
  const TArray<FEHBNodeRoomBoundary>& After,FName& Status);
 bool bReady=false;
 TArray<FEHBRoomCoverageChange> Changes;
};
