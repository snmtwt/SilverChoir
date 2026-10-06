#pragma once
#include "Core/EHBBuildingActorBase.h"

// Editor transaction boundary for free, physical-pillar and explicit V2 logical
// endpoints. Wall-surface endpoints use the separate anchored split/dependency command.
struct FEHBWallPathResult
{
 bool bSucceeded = false;
 FName Status;
 FName FailureReason;
 TArray<FEHBWallCreationEndpoint> Endpoints;
 TArray<AEHB_Wall*> Walls;
 AEHB_Wall* PrimaryWall = nullptr;
 FEHBCommittedEdit CommittedEdit;
};
// Value-only geometry from the same accepted dependency draft used by commit.
struct FEHBWallPathPreview
{
 FEHBWallNodeModel Model;
 TArray<FEHBWallJunctionWallSides> Sides;
 TArray<FGuid> WallGuids,NewPhysicalNodeGuids;
};
namespace EHBWallCreationCommand
{
	/** Single click insertion using the same split/dependency transaction as compound paths. */
	FEHBWallPathResult InsertColumnOnWall(AEHBBuildingActorBase* Building,AEHB_Wall* Wall,float Distance,float ColumnHeight,float ColumnWidth,bool bPreviewOnly=false);
	FEHBWallPathResult InsertNodeOnWall(AEHBBuildingActorBase* Building,AEHB_Wall* Wall,float Distance,float Height,float Width,bool bCreatePhysicalColumn,bool bPreviewOnly=false);
 // One transaction for anchored paths, with source-relative interval planning for repeated source walls.
 FEHBWallPathResult CommitAnchoredPath(AEHBBuildingActorBase* Building, const TArray<FEHBWallCreationEndpoint>& Endpoints,
  bool bClosed, const FEHBWallCreationOptions& Options, bool bPreviewOnly = false,FEHBWallPathPreview* Preview=nullptr);
 // Compound split + branch. Exactly one source-wall endpoint; other endpoint free or an existing pillar.
 FEHBWallPathResult CommitFromWall(AEHBBuildingActorBase* Building, const FEHBWallCreationEndpoint& Start,
  const FEHBWallCreationEndpoint& End, const FEHBWallCreationOptions& Options, bool bPreviewOnly = false);
 FName Validate(const AEHBBuildingActorBase* Building, const TArray<FEHBWallCreationEndpoint>& Endpoints,
  bool bClosed, const FEHBWallCreationOptions& Options);
 FEHBWallPathResult Commit(AEHBBuildingActorBase* Building, const TArray<FEHBWallCreationEndpoint>& Endpoints,
  bool bClosed, const FEHBWallCreationOptions& Options, bool bPreviewOnly = false,FEHBWallPathPreview* Preview=nullptr);
#if WITH_DEV_AUTOMATION_TESTS
 // One-shot fault after N successfully completed edges, or after receipt assignment.
 extern int32 FailAfterSplit;
 extern int32 FailAfterEdge;
 extern bool bFailAfterRecord;
#endif
}
