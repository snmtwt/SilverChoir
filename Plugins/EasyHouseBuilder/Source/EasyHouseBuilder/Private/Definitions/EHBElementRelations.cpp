// Copyright Epic Games, Inc. All Rights Reserved.

#include "Definitions/EHBElementRelations.h"

bool FEHBElementRelationEndpoint::IsValid() const
{
	if(Kind!=EEHBRelationEndpointKind::WallNode&&NodeGuid.IsValid())return false;
	switch (Kind)
	{
	case EEHBRelationEndpointKind::BuildingElement:
		return ElementGuid.IsValid();
	case EEHBRelationEndpointKind::ExternalActor:
		return !ExternalActor.IsNull();
	case EEHBRelationEndpointKind::WallNode:
		return NodeGuid.IsValid()&&!ElementGuid.IsValid()&&ExternalActor.IsNull();
	case EEHBRelationEndpointKind::WorldGround:
		return true;
	default:
		return false;
	}
}

bool FEHBElementRelationEndpoint::RefersToElement(const FGuid& InElementGuid) const
{
	return Kind == EEHBRelationEndpointKind::BuildingElement
		&& InElementGuid.IsValid()
		&& ElementGuid == InElementGuid;
}

bool FEHBElementRelationEndpoint::RefersToNode(const FGuid& InNodeGuid) const
{
	return Kind==EEHBRelationEndpointKind::WallNode&&InNodeGuid.IsValid()&&NodeGuid==InNodeGuid;
}

FEHBElementRelationEndpoint FEHBElementRelationEndpoint::MakeNode(const FGuid& InNodeGuid)
{
	FEHBElementRelationEndpoint Endpoint;Endpoint.Kind=EEHBRelationEndpointKind::WallNode;Endpoint.NodeGuid=InNodeGuid;return Endpoint;
}

bool FEHBElementRelationEndpoint::IsEquivalentTo(const FEHBElementRelationEndpoint& Other) const
{
	return Kind == Other.Kind
		&& ElementGuid == Other.ElementGuid
		&& NodeGuid == Other.NodeGuid
		&& ExternalActor.ToSoftObjectPath() == Other.ExternalActor.ToSoftObjectPath()
		&& SurfaceKind == Other.SurfaceKind
		&& SurfaceName == Other.SurfaceName
		&& SubIndex == Other.SubIndex;
}

FEHBElementRelationEndpoint FEHBElementRelationEndpoint::MakeElement(
	const FGuid& InElementGuid,
	EEHBElementSurfaceKind InSurfaceKind,
	FName InSurfaceName,
	int32 InSubIndex)
{
	FEHBElementRelationEndpoint Endpoint;
	Endpoint.Kind = EEHBRelationEndpointKind::BuildingElement;
	Endpoint.ElementGuid = InElementGuid;
	Endpoint.SurfaceKind = InSurfaceKind;
	Endpoint.SurfaceName = InSurfaceName;
	Endpoint.SubIndex = InSubIndex;
	return Endpoint;
}

bool FEHBElementRelation::IsEquivalentTo(const FEHBElementRelation& Other) const
{
	return Type == Other.Type
		&& Source.IsEquivalentTo(Other.Source)
		&& Target.IsEquivalentTo(Other.Target);
}

bool FEHBElementRelation::InvolvesElement(const FGuid& ElementGuid) const
{
	return Source.RefersToElement(ElementGuid) || Target.RefersToElement(ElementGuid);
}
