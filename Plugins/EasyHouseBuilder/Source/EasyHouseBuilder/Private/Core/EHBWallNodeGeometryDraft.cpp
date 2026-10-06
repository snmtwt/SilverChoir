// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Core/EHBWallTopology.h"

FEHBWallNodeSideDraft FEHBWallNodeSideDraft::Build(const FEHBWallNodeModel& Source,FName& Reason)
{
 FEHBWallNodeSideDraft Result;
 if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Source,Result.Sides,Reason,nullptr,&Result.Stats))return {};
 Result.Model=Source;Result.bReady=true;Reason=TEXT("Ready");return Result;
}

FEHBWallNodeGeometryDraft FEHBWallNodeGeometryDraft::Build(const FEHBWallNodeModel& Source,FName& Reason)
{
	return BuildInternal(Source,Reason,nullptr,nullptr);
}

FEHBWallNodeGeometrySelection FEHBWallNodeGeometrySelection::Build(const FEHBWallNodeModel& Source,const TSet<FGuid>& Walls,const TSet<FGuid>& Junctions,FName& Reason)
{
	FEHBWallNodeGeometrySelection Result;Result.Data=FEHBWallNodeGeometryDraft::BuildInternal(Source,Reason,&Walls,&Junctions);return Result;
}

FEHBWallNodeGeometryDraft FEHBWallNodeGeometryDraft::BuildInternal(const FEHBWallNodeModel& Source,FName& Reason,const TSet<FGuid>* RequestedWalls,const TSet<FGuid>* RequestedJunctions)
{
 FEHBWallNodeGeometryDraft Result;
 if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Source,Result.Sides,Reason,RequestedWalls,&Result.Stats))return {};
 TSet<FGuid> Bound,Connected;
 TMap<FGuid,const FEHBWallNodeDefinition*> Nodes;
 for(const auto& Node:Source.Nodes)Nodes.Add(Node.NodeGuid,&Node);
 TMap<FGuid,TArray<FEHBWallJunctionLeg>> Legs;
 for(const auto& Binding:Source.PillarBindings)Bound.Add(Binding.NodeGuid);
 if(RequestedJunctions)for(FGuid Id:*RequestedJunctions)
 {
  if(!Nodes.Contains(Id)){Reason=TEXT("UnknownRequestedJunction");return {};}
  if(Bound.Contains(Id)){Reason=TEXT("RequestedJunctionHasPhysicalBinding");return {};}
 }
 for(const auto& Wall:Source.Walls)
 {
  Connected.Add(Wall.StartNodeGuid);Connected.Add(Wall.EndNodeGuid);
  for(bool AtStart:{true,false})
  {
   const auto* Node=Nodes.FindChecked(AtStart?Wall.StartNodeGuid:Wall.EndNodeGuid);
   if(Bound.Contains(Node->NodeGuid)||(RequestedJunctions&&!RequestedJunctions->Contains(Node->NodeGuid)))continue;
   if(RequestedWalls&&!RequestedWalls->Contains(Wall.WallGuid)){Reason=TEXT("IncompleteJunctionWallSelection");return {};}
   const auto* Other=Nodes.FindChecked(AtStart?Wall.EndNodeGuid:Wall.StartNodeGuid);
   auto& Leg=Legs.FindOrAdd(Node->NodeGuid).AddDefaulted_GetRef();
   // Keep the legacy mesh operation order exactly. The side solver normalizes
   // before rotating; sharing those footprints would change double coordinates,
   // UVs and content hashes of existing saved junctions by a few ULPs.
   Leg.Direction=Node->LocalTransform.InverseTransformVectorNoScale(Other->LocalTransform.GetLocation()-Node->LocalTransform.GetLocation()).GetSafeNormal2D();
   Leg.WallThickness=Wall.Thickness;
  }
 }
 if(RequestedJunctions)for(FGuid Id:*RequestedJunctions)if(!Connected.Contains(Id)){Reason=TEXT("RequestedJunctionHasNoWalls");return {};}
 for(const auto& Node:Source.Nodes)if(!Bound.Contains(Node.NodeGuid)&&Connected.Contains(Node.NodeGuid)&&(!RequestedJunctions||RequestedJunctions->Contains(Node.NodeGuid)))
 {
  TArray<FVector> Footprint;FEHBWallJunctionMesh Mesh;
  if(!FEHBWallJunctionGeometry::BuildFootprint(Node.JunctionDimensions.X,Node.JunctionDimensions.Y,Legs.FindChecked(Node.NodeGuid),Footprint,true)
   ||!FEHBWallJunctionMeshBuilder::BuildPrism(Footprint,Node.JunctionDimensions.Z,Mesh))
  {Reason=TEXT("UnsupportedJunctionFootprint");return {};}
  Mesh.NodeGuid=Node.NodeGuid;Mesh.LocalTransform=Node.LocalTransform;
  Result.Junctions.Add(Node.NodeGuid,MoveTemp(Mesh));
  Result.JunctionRevisions.Add(Node.NodeGuid,Node.GeometryRevision);
 }
 if(!RequestedWalls)Result.Model=Source;
 Result.bReady=true;Reason=TEXT("Ready");return Result;
}
