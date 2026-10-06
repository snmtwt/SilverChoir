// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/EHBWallNodeDefinitions.h"
#include "Definitions/EHBElementRelations.h"

/** Value-only identity draft, NOT a complete building copy or permission to paste Actors.
 * ElementGuids declares the caller's copy boundary. Actor properties, persisted migration
 * snapshots, outline bindings and external-host policies still need a transactional adapter.
 * Separate maps deliberately preserve the distinction between node and physical identity,
 * even when their source GUID values happen to be equal. Failure returns no partial draft. */
struct EASYHOUSEBUILDER_API FEHBWallNodeCopyDraft
{
	bool bSucceeded = false;
	FName Status;
	FGuid BuildingGuid;
	FEHBWallNodeModel Model;
	TArray<FEHBElementRelation> Relations;
	TMap<FGuid, FGuid> ElementGuids, NodeGuids, RelationGuids, RoomGuids;
};

struct EASYHOUSEBUILDER_API FEHBWallNodeCopy
{
	/** Creates fresh identities without touching a world, Actor, index or source value.
	 * Relations must use typed node topology and have both endpoints inside the declared
	 * copy boundary. World/external anchors and opaque string metadata need an explicit
	 * copy policy and are refused. Room mapping uses mapped boundary wall sets and floor,
	 * never nearest position, actor label, output order or a cached source room ID. */
	static FEHBWallNodeCopyDraft BuildDraft(FGuid SourceBuildingGuid,
		const FEHBWallNodeModel& Source, const TArray<FGuid>& ElementGuids,
		const TArray<FEHBElementRelation>& Relations);
};
