// Copyright Epic Games, Inc. All Rights Reserved.
#include "Components/EHBWallJunctionComponent.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeGeometryDraft.h"

bool UEHBWallJunctionComponent::RebuildFromNodeModel(const FEHBWallNodeModel& Definitions,FGuid InNodeGuid)
{
	FEHBWallJunctionMesh Mesh;FName Reason;
	if(!UEHBWallTopologyLibrary::BuildWallNodeModelJunctionMesh(Definitions,InNodeGuid,Mesh,Reason))return false;
	const auto* Node=Definitions.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==InNodeGuid;});
	ApplyValidatedMesh(Mesh,Node->GeometryRevision);return true;
}

bool UEHBWallJunctionComponent::RebuildFromGeometryDraft(const FEHBWallNodeGeometryDraft& Geometry,FGuid InNodeGuid)
{
	if(!Geometry.IsReady())return false;
	const auto* Mesh=Geometry.GetUnboundJunctions().Find(InNodeGuid);
	const auto* Revision=Geometry.FindJunctionRevision(InNodeGuid);
	if(!Mesh||!Revision)return false;
	ApplyValidatedMesh(*Mesh,*Revision);return true;
}

bool UEHBWallJunctionComponent::RebuildFromGeometrySelection(const FEHBWallNodeGeometrySelection& Geometry,FGuid InNodeGuid)
{
	if(!Geometry.IsReady())return false;
	const auto* Mesh=Geometry.GetUnboundJunctions().Find(InNodeGuid);
	const auto* Revision=Geometry.FindJunctionRevision(InNodeGuid);
	if(!Mesh||!Revision)return false;
	ApplyValidatedMesh(*Mesh,*Revision);return true;
}

void UEHBWallJunctionComponent::ApplyValidatedMesh(const FEHBWallJunctionMesh& Mesh,int32 GeometryRevision)
{
	TArray<FLinearColor> Colors;Colors.Init(FLinearColor::White,Mesh.Vertices.Num());TArray<FEHBMeshTangent> Tangents;
	Modify();FEHBScopedGeneratedMeshUpdate Update(this);
	SetRelativeTransform(Mesh.LocalTransform);
	CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Triangles,Mesh.Normals,Mesh.UVs,Colors,Tangents,true);
	SetMeshSectionName(0,TEXT("WallJunction"));ClearMeshSectionsFrom(1);
	NodeGuid=Mesh.NodeGuid;SourceGeometryRevision=GeometryRevision;
}

bool UEHBWallJunctionComponent::RebuildFromNodeDefinitions(const FEHBPreparedWallNodeDefinitions& Definitions,FGuid InNodeGuid)
{
	if(!UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Definitions).IsEmpty())return false;
	return RebuildFromNodeModel(Definitions,InNodeGuid);
}
