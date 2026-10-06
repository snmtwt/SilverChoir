// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Components/EHBGeneratedMeshComponent.h"
#include "EHBWallJunctionComponent.generated.h"
struct FEHBPreparedWallNodeDefinitions;
struct FEHBWallNodeModel;
struct FEHBWallJunctionMesh;
class FEHBWallNodeGeometryDraft;
class FEHBWallNodeGeometrySelection;

/** Derived junction rendering, owned by a building rather than a physical pillar Actor.
 * Native adapter only until explicit node authority/migration is enabled. No support/host capability. */
UCLASS(ClassGroup=Rendering)
class EASYHOUSEBUILDER_API UEHBWallJunctionComponent : public UEHBGeneratedMeshComponent
{
	GENERATED_BODY()
public:
	/** Caller owns transaction and authority. Failure preserves existing geometry and source metadata. */
	bool RebuildFromNodeModel(const FEHBWallNodeModel& Definitions,FGuid InNodeGuid);
	bool RebuildFromNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Definitions,FGuid InNodeGuid);
	/** Consume a complete immutable solve without repeating model or junction calculation. */
	bool RebuildFromGeometryDraft(const FEHBWallNodeGeometryDraft& Geometry,FGuid InNodeGuid);
	/** Explicit selected-result adapter; absent nodes refuse without touching output. */
	bool RebuildFromGeometrySelection(const FEHBWallNodeGeometrySelection& Geometry,FGuid InNodeGuid);
	UPROPERTY(Transient,VisibleAnywhere,Category="Junction") FGuid NodeGuid;
	UPROPERTY(Transient,VisibleAnywhere,Category="Junction") int32 SourceGeometryRevision=0;
private:
	void ApplyValidatedMesh(const FEHBWallJunctionMesh& Mesh,int32 GeometryRevision);
};
