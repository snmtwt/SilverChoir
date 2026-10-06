// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Core/EHBWallNodeDefinitions.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "Core/EHBWallJunctionMesh.h"

/** Complete wall sides and their owned source. This proves wall-side solvability,
 * not unbound prism mesh validity. No Actor references or cross-frame cache.
 * Separate from both full mesh drafts and selected geometry. */
class EASYHOUSEBUILDER_API FEHBWallNodeSideDraft
{
public:
 static FEHBWallNodeSideDraft Build(const FEHBWallNodeModel& Model,FName& Reason);
 bool IsReady() const { return bReady; }
 const FEHBWallNodeModel& GetModel() const { return Model; }
 const TArray<FEHBWallJunctionWallSides>& GetWallSides() const { return Sides; }
 const FEHBWallJunctionSolveStats& GetSolveStats() const { return Stats; }
private:
 bool bReady=false;
 FEHBWallNodeModel Model;
 TArray<FEHBWallJunctionWallSides> Sides;
 FEHBWallJunctionSolveStats Stats;
};

/** Owned, read-only geometry for one candidate. No Actor references, cross-frame
 * cache, or authority to commit. Each live command builds a fresh draft. */
class EASYHOUSEBUILDER_API FEHBWallNodeGeometryDraft
{
public:
 static FEHBWallNodeGeometryDraft Build(const FEHBWallNodeModel& Model,FName& Reason);
 bool IsReady() const { return bReady; }
 const FEHBWallNodeModel& GetModel() const { return Model; }
 const TArray<FEHBWallJunctionWallSides>& GetWallSides() const { return Sides; }
 const TMap<FGuid,FEHBWallJunctionMesh>& GetUnboundJunctions() const { return Junctions; }
 const int32* FindJunctionRevision(FGuid NodeGuid) const { return JunctionRevisions.Find(NodeGuid); }
 const FEHBWallJunctionSolveStats& GetSolveStats() const { return Stats; }
private:
	friend class FEHBWallNodeGeometrySelection;
	static FEHBWallNodeGeometryDraft BuildInternal(const FEHBWallNodeModel& Model,FName& Reason,const TSet<FGuid>* Walls,const TSet<FGuid>* Junctions);
 bool bReady=false;
 FEHBWallNodeModel Model;
 TArray<FEHBWallJunctionWallSides> Sides;
 TMap<FGuid,FEHBWallJunctionMesh> Junctions;
 TMap<FGuid,int32> JunctionRevisions;
 FEHBWallJunctionSolveStats Stats;
};

/** Geometry only for an explicit selection, never convertible to a complete draft.
 * Validates the whole value model, but geometric solvability only for selected
 * walls/junctions. Every incident wall of a requested unbound junction must be
 * selected; all other incident legs at selected wall endpoints still participate.
 * Caller must prove unchanged outside geometry before applying a partial edit. */
class EASYHOUSEBUILDER_API FEHBWallNodeGeometrySelection
{
public:
	static FEHBWallNodeGeometrySelection Build(const FEHBWallNodeModel& Model,const TSet<FGuid>& Walls,const TSet<FGuid>& Junctions,FName& Reason);
	bool IsReady() const { return Data.bReady; }
	const TArray<FEHBWallJunctionWallSides>& GetWallSides() const { return Data.Sides; }
	const TMap<FGuid,FEHBWallJunctionMesh>& GetUnboundJunctions() const { return Data.Junctions; }
	const int32* FindJunctionRevision(FGuid NodeGuid) const { return Data.JunctionRevisions.Find(NodeGuid); }
	const FEHBWallJunctionSolveStats& GetSolveStats() const { return Data.Stats; }
private:
	FEHBWallNodeGeometryDraft Data;
};
