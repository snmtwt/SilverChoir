#include "EHBRoomSubdivision.h"
#include "EHBCopyOutlinePolicy.h"
#include "EHBRoomFinishMove.h"
#include "EHBWallCreationCommand.h"
#include "Components/SceneComponent.h"
#include "EasyHouseEditorMode.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBRoomCoverageTransition.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "Algo/Reverse.h"
#include "Engine/World.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/EHBWallSplitTestHooks.h"
#endif

namespace
{
 bool AlignOutline(TArray<FVector>& Polygon,const TArray<FVector>& Original)
 {
  if(Polygon.Num()<3||Original.Num()<3)return false;
  auto Area=[](const auto& P){double A=0;for(int32 I=0;I<P.Num();++I)A+=P[I].X*P[(I+1)%P.Num()].Y-P[I].Y*P[(I+1)%P.Num()].X;return A;};
  if(Area(Polygon)*Area(Original)<0)Algo::Reverse(Polygon);
  const int32 First=Polygon.IndexOfByPredicate([&](const auto& P){return P.Equals(Original[0],0.001);});if(First==INDEX_NONE)return false;
  if(First){const auto Copy=Polygon;for(int32 I=0;I<Polygon.Num();++I)Polygon[I]=Copy[(I+First)%Copy.Num()];}return true;
 }
 bool SameCoverage(TArray<FVector> A,TArray<FVector> B,FName& Status)
 {
  for(auto& P:A)P.Z=0;for(auto& P:B)P.Z=0;
  FEHBFloorSupportSurface X,Y;X.OuterPolygon=MoveTemp(A);Y.OuterPolygon=MoveTemp(B);
  return EHBRoomFinishMove::SameTopCoverage({X},{Y},Status);
 }
}

bool FEHBRoomSubdivision::Contains(const AEHBElementActorBase* E) const
{
 return Floors.ContainsByPredicate([&](const auto& P){return P.Actor==E;})||Slabs.ContainsByPredicate([&](const auto& P){return P.Actor==E;});
}

bool FEHBRoomSubdivision::Prepare(AEHBBuildingActorBase* B,AEHB_Wall* Wall,const FEHBWallSplitPreview& Split,const TArray<AEHBElementActorBase*>& Elements,FName& Status)
{
 return PrepareMultiple(B,{{Wall,Split,0}},Elements,Status);
}

bool FEHBRoomSubdivision::PrepareMultiple(AEHBBuildingActorBase* B,const TArray<FSplit>& Splits,const TArray<AEHBElementActorBase*>& Elements,FName& Status,const FPath* Path,const TSet<FGuid>* ValidatedRetainedOpeningWalls)
{
 *this={};auto Fail=[&](FName Reason){*this={};Status=Reason;return false;};
 if(!B||(Splits.IsEmpty()&&!Path)||B->WallNodeOwnership.Version!=1)return Fail(TEXT("RoomSubdivisionRequiresTypedNodes"));
 for(const auto& S:Splits){if(!S.Source||!S.Preview.bSucceeded)return Fail(TEXT("InvalidSubdivisionSplit"));RemovedHosts.Add(S.Source->ElementGuid);}
 if(ValidatedRetainedOpeningWalls)PreservedOpeningWalls=*ValidatedRetainedOpeningWalls;
 for(FGuid Id:PreservedOpeningWalls)if(RemovedHosts.Contains(Id))return Fail(TEXT("RetainedOpeningHostRemoved"));
 auto DeferredOpenings=RemovedHosts;DeferredOpenings.Append(PreservedOpeningWalls);
 const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Candidate,&DeferredOpenings);if(!Capture.bSucceeded)return Fail(Capture.Status);
 const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Candidate,Elements,true);if(!Policy.bSucceeded)return Fail(Policy.Status);
 for(const auto& Pair:Policy.FinishRelationRooms)FinishRelations.Add(Pair.Key);
 TSet<FGuid> OriginalHosts;for(const auto& W:Candidate.Walls)OriginalHosts.Add(W.WallGuid);for(const auto& P:Candidate.PillarBindings)OriginalHosts.Add(P.PhysicalPillarGuid);
 TArray<FEHBNodeRoomBoundary> Before,After;
 if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Candidate,Before,Status))return Fail(Status);
 for(const auto& S:Splits)
 {
  const FGuid Original=S.Source->ElementGuid;
  const FGuid Current=SourceStartSegments.Contains(Original)?SourceStartSegments[Original]:Original;
  const auto* Source=Candidate.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Current;});if(!Source)return Fail(TEXT("MissingSubdivisionWall"));
  const auto Definition=*Source;
  auto& Slots=SplitSlots.AddDefaulted_GetRef();Slots.Node=FGuid::NewGuid();if(S.bCreatePhysicalColumn)Slots.Pillar=FGuid::NewGuid();Slots.Left=FGuid::NewGuid();Slots.Right=FGuid::NewGuid();Slots.RequestIndex=S.RequestIndex;
  FEHBWallNodeDefinition Node;Node.NodeGuid=Slots.Node;Node.FloorIndex=S.Source->FloorIndex;
  Node.LocalTransform=FTransform((S.Source->LocalEnd-S.Source->LocalStart).Rotation(),S.Preview.LocalPillarPosition);
  Node.JunctionDimensions=FVector(S.Preview.PillarWidth,S.Preview.PillarWidth,S.Preview.PillarHeight);
  Candidate.Nodes.Add(Node);if(Slots.Pillar.IsValid())Candidate.PillarBindings.Add({Slots.Node,Slots.Pillar});
  Candidate.Walls.RemoveAll([&](const auto& W){return W.WallGuid==Current;});
  auto Left=Definition,Right=Definition;Left.WallGuid=Slots.Left;Left.EndNodeGuid=Slots.Node;Right.WallGuid=Slots.Right;Right.StartNodeGuid=Slots.Node;Candidate.Walls.Append({Left,Right});
  SourceStartSegments.Add(Original,Slots.Left);
 }
 if(Path)
 {
  const auto& Route=Path->Route;const auto& O=Path->Options;
  if(!Route.bSucceeded||Route.PathEdges.Num()!=Route.Segments.Num()||Path->SourceEndpoints.Num()!=Splits.Num())return Fail(TEXT("InvalidSubdivisionPath"));
  PathNodes.SetNum(Route.Points.Num());PathPillars.SetNum(Route.Points.Num());PathWalls.SetNum(Route.Segments.Num());
  for(int32 I=0;I<Route.Points.Num();++I)
  {
   const auto& P=Route.Points[I];
   if(P.ExistingNodeGuid.IsValid())
   {
    const auto* Node=Candidate.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==P.ExistingNodeGuid;});
    if(!Node||Node->GeometryRevision!=P.NodeRevision||Node->FloorIndex!=P.FloorIndex||!Node->LocalTransform.GetLocation().Equals(P.LocalPosition,0.001))return Fail(TEXT("StalePathNode"));
    const auto* Binding=Candidate.PillarBindings.FindByPredicate([&](const auto& V){return V.NodeGuid==P.ExistingNodeGuid;});
    if((Binding?Binding->PhysicalPillarGuid:FGuid())!=P.ExistingPillarGuid)return Fail(TEXT("PathNodeBindingMismatch"));
    PathNodes[I]=P.ExistingNodeGuid;PathPillars[I]=P.ExistingPillarGuid;
   }
   else if(P.ExistingPillarGuid.IsValid())
   {
    const auto* Binding=Candidate.PillarBindings.FindByPredicate([&](const auto& V){return V.PhysicalPillarGuid==P.ExistingPillarGuid;});if(!Binding)return Fail(TEXT("MissingPathNodeBinding"));
    PathNodes[I]=Binding->NodeGuid;PathPillars[I]=P.ExistingPillarGuid;
   }
   else
   {
    const int32 Request=Path->SourceEndpoints.Find(P.EndpointIndex);
    if(Request!=INDEX_NONE){const auto* Slot=SplitSlots.FindByPredicate([&](const auto& V){return V.RequestIndex==Request;});if(!Slot)return Fail(TEXT("MissingPathSplitSlot"));PathNodes[I]=Slot->Node;PathPillars[I]=Slot->Pillar;}
   }
  }
  for(int32 I=0;I<Route.Segments.Num();++I)
  {
   const auto Pair=Route.Segments[I];const int32 Edge=Route.PathEdges[I];
   if(!Path->Endpoints.IsValidIndex(Edge)||!Route.Points.IsValidIndex(Pair.X)||!Route.Points.IsValidIndex(Pair.Y))return Fail(TEXT("InvalidPathSegment"));
   const auto& A=Path->Endpoints[Edge];const auto& Z=Path->Endpoints[(Edge+1)%Path->Endpoints.Num()];
   const FVector Direction=(Z.LocalLocation-A.LocalLocation).GetSafeNormal2D();
   if(Direction.IsNearlyZero()||!FMath::IsNearlyZero(O.FreePillarLocalRotation.Pitch)||!FMath::IsNearlyZero(O.FreePillarLocalRotation.Roll))return Fail(TEXT("InvalidSubdivisionPathRotation"));
   for(int32 Index:{Pair.X,Pair.Y})if(!PathNodes[Index].IsValid())
   {
    const auto& Point=Route.Points[Index];FEHBWallNodeDefinition Node;
    Node.NodeGuid=PathNodes[Index]=FGuid::NewGuid();if(O.bCreatePhysicalColumns)PathPillars[Index]=FGuid::NewGuid();Node.FloorIndex=Point.FloorIndex;
    Node.LocalTransform=FTransform(O.FreePillarLocalRotation.IsNearlyZero()?Direction.Rotation():O.FreePillarLocalRotation,Point.LocalPosition);
    Node.JunctionDimensions=O.bCreatePhysicalColumns?FVector(O.PillarWidth,O.PillarDepth,O.PillarHeight):FVector(O.WallThickness,O.WallThickness,O.WallHeight);
    Candidate.Nodes.Add(Node);if(PathPillars[Index].IsValid())Candidate.PillarBindings.Add({Node.NodeGuid,PathPillars[Index]});
   }
   const FGuid X=PathNodes[Pair.X],Y=PathNodes[Pair.Y];
   const auto* Reused=Candidate.Walls.FindByPredicate([&](const auto& W){return (W.StartNodeGuid==X&&W.EndNodeGuid==Y)||(W.StartNodeGuid==Y&&W.EndNodeGuid==X);});
   if(Reused){if(Reused->Height!=O.WallHeight||Reused->Thickness!=O.WallThickness)return Fail(TEXT("ReusedPathWallDimensionsChanged"));PathWalls[I]=Reused->WallGuid;}
   else {FEHBNodeConnectedWallDefinition W;W.WallGuid=PathWalls[I]=FGuid::NewGuid();W.StartNodeGuid=X;W.EndNodeGuid=Y;W.Height=O.WallHeight;W.Thickness=O.WallThickness;Candidate.Walls.Add(W);}
  }
 }
 // One owned, validated geometry result supplies meshes, slab sides and drawing.
 CandidateGeometry=FEHBWallNodeGeometryDraft::Build(Candidate,Status);
 if(!CandidateGeometry.IsReady())return Fail(Status);
 if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Candidate,After,Status))return Fail(Status);
 // Share correspondence with future removal/merge commands. This creation adapter
 // only materializes retained/split rooms; classification is not finish-delete permission.
 const auto Transition=FEHBRoomCoverageTransition::BuildFromValidatedRooms(Before,After,Status);
 if(!Transition.IsReady())return Fail(Status);
 TMap<FGuid,TArray<const FEHBNodeRoomBoundary*>> Children;
 for(const auto& C:Transition.GetChanges())
 {
  if(C.Kind==EEHBRoomCoverageChange::Added)continue;
  if(C.Kind==EEHBRoomCoverageChange::Merged)return Fail(TEXT("RoomMergeRequiresInheritancePlan"));
  if(C.Kind==EEHBRoomCoverageChange::Removed)return Fail(TEXT("IncompleteRoomPartitionCoverage"));
  auto& List=Children.Add(C.BeforeRooms[0]);
  for(FGuid Id:C.AfterRooms)List.Add(After.FindByPredicate([&](const auto& R){return R.RoomGuid==Id;}));
 }
 for(const auto& R:Before)Rooms.Add(R.RoomGuid);PlannedRoomCount=After.Num();PlannedRooms=After;
 for(const auto& W:Candidate.Walls)if(!OriginalHosts.Contains(W.WallGuid))NewHosts.Add(W.WallGuid);
 for(const auto& P:Candidate.PillarBindings)if(!OriginalHosts.Contains(P.PhysicalPillarGuid))NewHosts.Add(P.PhysicalPillarGuid);
 const auto& Sides=CandidateGeometry.GetWallSides();
 if(!EHBRoomFinishMove::BuildTopologyTops(CandidateGeometry,Tops,Status))return Fail(Status);
 auto AlignChild=[&](TArray<FVector>& Polygon,const TArray<FVector>& Original,bool Changed)
 {
  if(AlignOutline(Polygon,Original))return true;if(!Changed||Polygon.Num()<3)return false;
  int32 Min=0;for(int32 I=1;I<Polygon.Num();++I)if(Polygon[I].X<Polygon[Min].X||(Polygon[I].X==Polygon[Min].X&&Polygon[I].Y<Polygon[Min].Y))Min=I;
  const auto Copy=Polygon;for(int32 I=0;I<Polygon.Num();++I)Polygon[I]=Copy[(Min+I)%Copy.Num()];return true;
 };
 for(auto* E:Elements)
 {
  if(auto* F=Cast<AEHB_Floor>(E))
  {
   if(F->OutlineSource==EEHBOutlineSource::RetainedRegion)
   {auto& P=Floors.AddDefaulted_GetRef();P.Actor=F;P.Id=F->ElementGuid;P.Source=F->OutlineSource;P.Regions=F->FloorRegions;continue;}
   const auto* List=Children.Find(F->RoomLoopGuid);if(!List)return Fail(TEXT("MissingSubdivisionFloorRoom"));
   for(int32 I=0;I<List->Num();++I)
   {
    const auto* R=(*List)[I];auto& P=Floors.AddDefaulted_GetRef();P.Actor=F;P.Id=I?FGuid::NewGuid():F->ElementGuid;P.Room=R->RoomGuid;P.bNew=I!=0;P.bRebound=R->RoomGuid!=F->RoomLoopGuid;P.Regions=F->FloorRegions;P.Source=F->OutlineSource;
    if(P.bRebound&&F->GetClass()!=AEHB_Floor::StaticClass())return Fail(TEXT("UnsupportedInheritedFloorClass"));
    const double Z=P.Regions[0].OuterPolygon[0].Z;P.Regions[0].OuterPolygon=R->Polygon;for(auto& V:P.Regions[0].OuterPolygon)V.Z=Z;
    if(!AlignChild(P.Regions[0].OuterPolygon,F->FloorRegions[0].OuterPolygon,P.bRebound)||!F->ValidateFloorRegions(P.Regions))return Fail(TEXT("InvalidSubdivisionFloor"));
   }
  }
  else if(auto* S=Cast<AEHB_FloorSlab>(E))
  {
   if(S->OutlineSource==EEHBOutlineSource::RetainedRegion)
   {
    auto& P=Slabs.AddDefaulted_GetRef();P.Actor=S;P.Id=S->ElementGuid;P.Source=S->OutlineSource;P.Polygon=S->LocalTopPolygon;
    FEHBFloorSupportSurface T;for(auto V:P.Polygon){V.Z=S->GetTopZ();T.OuterPolygon.Add(S->GetElementLocalTransform().TransformPosition(V));}Tops.Add(P.Id,{MoveTemp(T)});continue;
   }
   const auto* List=Children.Find(S->RoomFillLoopGuid);if(!List)return Fail(TEXT("MissingSubdivisionSlabRoom"));
   for(int32 I=0;I<List->Num();++I)
   {
    const auto* R=(*List)[I];auto& P=Slabs.AddDefaulted_GetRef();P.Actor=S;P.Id=I?FGuid::NewGuid():S->ElementGuid;P.Room=R->RoomGuid;P.bNew=I!=0;P.bRebound=R->RoomGuid!=S->RoomFillLoopGuid;
    if(P.bRebound&&S->GetClass()!=AEHB_FloorSlab::StaticClass())return Fail(TEXT("UnsupportedInheritedSlabClass"));
    // A new junction leg can change an unchanged neighbor's net corner: its
    // original first vertex need not survive even when the room ID does.
    // Preserve the start if possible; otherwise use the deterministic new start.
    if(!FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(S,*R,Candidate,Sides,P.Polygon)||!AlignChild(P.Polygon,S->LocalTopPolygon,true) )return Fail(TEXT("InvalidSubdivisionSlab"));
    TArray<FEHBLogicalSurfaceRegion> Unallocated;if(!(S->DisplayPartition.IsActive()?S->BuildUnallocatedDisplayRegion(P.Polygon,Unallocated):S->ValidateSlabOutline(P.Polygon,{})))return Fail(TEXT("InvalidSubdivisionSlab"));
    P.Anchor=S->RoomFillAnchorWallGuid;P.Side=S->RoomFillAnchorWallSide;
    if(const auto* W=SourceStartSegments.Find(P.Anchor))P.Anchor=*W;
    if(P.bRebound&&!R->WallGuids.Contains(P.Anchor))
    {
     P.Anchor=R->WallGuids[0];const auto* W=Candidate.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==P.Anchor;});if(!W)return Fail(TEXT("MissingInheritedSlabAnchor"));
     P.Side=W->StartNodeGuid==R->NodeGuids[0]?EEHBFloorSlabWallSide::Left:EEHBFloorSlabWallSide::Right;
    }
    FEHBFloorSupportSurface T;for(const auto& V:P.Polygon)T.OuterPolygon.Add(S->GetElementLocalTransform().TransformPosition(V));Tops.Add(P.Id,{MoveTemp(T)});if(P.bNew)NewHosts.Add(P.Id);
   }
  }
 }
 for(const auto& P:Slabs){auto& C=Displays.Slabs.AddDefaulted_GetRef();C.Actor=P.Actor;C.Polygon=P.Polygon;C.PlannedElementGuid=P.Id;C.bNew=P.bNew;}
 for(const auto& P:Slabs)if((P.bNew||P.Polygon!=P.Actor->LocalTopPolygon)&&Displays.HasConstraintsInLayer(P.Actor))Displays.DisplayLayers.AddUnique(P.Actor);
 if(!Displays.Prepare(Status))return Fail(Status);
 FEHBFloorHostTopologyDraft Hosts;Hosts.RemovedHostGuids=RemovedHosts;
 for(FGuid Id:NewHosts){const auto* Top=Tops.Find(Id);if(!Top)return Fail(TEXT("MissingSubdivisionHost"));Hosts.NewHostSurfaces.Add(Id,*Top);}
 auto ExistingTops=Tops;for(const auto& Pair:Hosts.NewHostSurfaces)ExistingTops.Remove(Pair.Key);
 for(auto& P:Floors)
 {
  if(P.bRebound)
  {
   FEHBFloorFinishContactDraft Draft;Draft.FloorGuid=P.Id;Draft.RoomGuid=P.Room;Draft.FloorIndex=P.Actor->RoomFloorIndex;Draft.Regions=P.Regions;Draft.Hosts=Tops;
   if(!P.bNew)for(const auto& R:B->ElementRelations)if(P.Actor->SurfaceFinishRelationGuids.Contains(R.RelationGuid))Draft.Previous.Add(R);
   if(!Draft.Build(P.Relations,Status))return Fail(Status);
  }
  else {if(!P.Actor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,P.Relations,Status,&ExistingTops,&Hosts))return Fail(Status);}
 }
 Status=TEXT("Ready");return true;
}

void FEHBRoomSubdivision::ExportPreview(FEHBWallPathPreview& Preview) const
{
 Preview={};Preview.Model=CandidateGeometry.GetModel();Preview.Sides=CandidateGeometry.GetWallSides();
 for(FGuid Id:PathWalls)Preview.WallGuids.AddUnique(Id);
 // Existing walls meeting a path node can receive a different junction outline.
 for(const auto& W:Candidate.Walls)if(PathNodes.Contains(W.StartNodeGuid)||PathNodes.Contains(W.EndNodeGuid))Preview.WallGuids.AddUnique(W.WallGuid);
 for(FGuid Id:NewHosts)
 {
  if(Candidate.Walls.ContainsByPredicate([&](const auto& W){return W.WallGuid==Id;}))Preview.WallGuids.AddUnique(Id);
  if(const auto* Binding=Candidate.PillarBindings.FindByPredicate([&](const auto& B){return B.PhysicalPillarGuid==Id;}))Preview.NewPhysicalNodeGuids.AddUnique(Binding->NodeGuid);
 }
}

bool FEHBRoomSubdivision::MaterializePath(AEHBBuildingActorBase* B,FPath& Path,TArray<FEHBWallCreationEndpoint>& Endpoints,TArray<AEHB_Wall*>& Walls,FName& Status,const TMap<FGuid,FGuid>& SplitIds)
{
 auto Fail=[&](FName Why){Status=Why;return false;};Walls.Reset();
 if(!B||B->WallNodeAuthority.Version!=2||!Path.Route.bSucceeded
  ||PathNodes.Num()!=Path.Route.Points.Num()||PathWalls.Num()!=Path.Route.Segments.Num())return Fail(TEXT("InvalidNodePathPlan"));
 Path.ActualPoints.SetNum(PathNodes.Num());Path.ActualNodes.SetNum(PathNodes.Num());Path.ActualWalls.SetNum(PathWalls.Num());
 auto Ids=SplitIds;
 for(const auto& Slot:SplitSlots)if(!Ids.Contains(Slot.Node)||(Slot.Pillar.IsValid()&&!Ids.Contains(Slot.Pillar))||!Ids.Contains(Slot.Left)||!Ids.Contains(Slot.Right))return Fail(TEXT("MissingMaterializedSplitSlot"));
 for(int32 I=0;I<PathNodes.Num();++I)
 {
  const auto& Point=Path.Route.Points[I];if(!PathNodes[I].IsValid())continue;
  if(Point.ExistingNodeGuid.IsValid()||Point.ExistingPillarGuid.IsValid())
  {
   Path.ActualPoints[I]=Point.ExistingPillarGuid;Path.ActualNodes[I]=Point.ExistingNodeGuid.IsValid()?Point.ExistingNodeGuid:B->FindNodeForPhysicalPillar(Point.ExistingPillarGuid);
  }
  else if(SplitIds.Contains(PathNodes[I]))
  {
   Path.ActualNodes[I]=SplitIds.FindChecked(PathNodes[I]);Path.ActualPoints[I]=SplitIds.FindRef(PathPillars[I]);
   if(!Endpoints.IsValidIndex(Point.EndpointIndex))return Fail(TEXT("SplitEndpointResultMismatch"));
   const auto* P=Endpoints[Point.EndpointIndex].Pillar;if((P?P->ElementGuid:FGuid())!=Path.ActualPoints[I])return Fail(TEXT("SplitEndpointResultMismatch"));
   auto& Endpoint=Endpoints[Point.EndpointIndex];const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Path.ActualNodes[I];});if(!N)return Fail(TEXT("MissingSplitNodeResult"));
   Endpoint.NodeGuid=N->NodeGuid;Endpoint.ExpectedNodeRevision=N->GeometryRevision;
  }
  else
  {
   const auto* Node=Candidate.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==PathNodes[I];});
   if(!Node||!Endpoints.IsValidIndex(Point.EndpointIndex))return Fail(TEXT("MissingNewPathNodeSlot"));
   AEHB_Pillar* Pillar=nullptr;
   if(PathPillars[I].IsValid())
   {
   Pillar=B->CreatePillarAtLocalLocation(Node->LocalTransform.GetLocation(),Node->LocalTransform.Rotator(),Node->JunctionDimensions.Z,Node->JunctionDimensions.X,Node->JunctionDimensions.Y,Node->FloorIndex,Path.Options.NewPillarNamePrefix,false);
   if(!Pillar||!Pillar->GetRootComponent()||!Pillar->GetElementLocalTransform().Equals(Node->LocalTransform,1.e-8))return Fail(TEXT("PathColumnMaterializationFailed"));
   Pillar->GetRootComponent()->SetRelativeLocation_Direct(Node->LocalTransform.GetLocation());Pillar->GetRootComponent()->SetRelativeRotation_Direct(Node->LocalTransform.Rotator());Pillar->GetRootComponent()->UpdateComponentToWorld();
   B->RegisterAuthoredWallNode(Pillar);B->RecordAuthoredWallNode(Pillar);Pillar->SynchronizePlannedEditorMove();
   Path.ActualPoints[I]=Pillar->ElementGuid;Path.ActualNodes[I]=B->FindNodeForPhysicalPillar(Pillar->ElementGuid);
   }
   else
   {
    if(B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==Node->NodeGuid;}))return Fail(TEXT("NewPathNodeIdentityCollision"));
    B->Modify();B->WallNodeAuthority.Nodes.Add(*Node);Path.ActualNodes[I]=Node->NodeGuid;
   }
   auto& Endpoint=Endpoints[Point.EndpointIndex];Endpoint.Pillar=Pillar;Endpoint.NodeGuid=Path.ActualNodes[I];Endpoint.LocalLocation=Node->LocalTransform.GetLocation();Endpoint.WorldLocation=B->GetActorTransform().TransformPosition(Endpoint.LocalLocation);Endpoint.FloorIndex=Node->FloorIndex;
   const auto* Actual=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Endpoint.NodeGuid;});if(!Actual)return Fail(TEXT("MissingMaterializedPathNode"));Endpoint.ExpectedNodeRevision=Actual->GeometryRevision;
  }
  if(!Path.ActualNodes[I].IsValid()||B->FindPhysicalPillarForNode(Path.ActualNodes[I])!=Path.ActualPoints[I])return Fail(TEXT("InvalidMaterializedPathBinding"));
  Ids.Add(PathNodes[I],Path.ActualNodes[I]);if(PathPillars[I].IsValid())Ids.Add(PathPillars[I],Path.ActualPoints[I]);
 }
 const auto Map=[&](FGuid Id){const auto* Value=Ids.Find(Id);return Value?*Value:Id;};
 auto Model=Candidate;for(auto& N:Model.Nodes)N.NodeGuid=Map(N.NodeGuid);for(auto& P:Model.PillarBindings){P.NodeGuid=Map(P.NodeGuid);P.PhysicalPillarGuid=Map(P.PhysicalPillarGuid);}for(auto& W:Model.Walls){W.WallGuid=Map(W.WallGuid);W.StartNodeGuid=Map(W.StartNodeGuid);W.EndNodeGuid=Map(W.EndNodeGuid);}
 TArray<FEHBWallJunctionWallSides> Sides;if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Status))return false;
 for(int32 I=0;I<PathWalls.Num();++I)
 {
  const FGuid Id=Map(PathWalls[I]);const auto* Definition=Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Id;});const auto* Geometry=Sides.FindByPredicate([&](const auto& S){return S.WallGuid==Id;});if(!Definition||!Geometry)return Fail(TEXT("MissingPathWallPlan"));
  auto* Wall=Cast<AEHB_Wall>(B->FindElementActorByGuid(Id));if(!Wall)Wall=B->CreateWallFromNodePlan(*Definition,*Geometry);
  if(!Wall)return Fail(TEXT("PathWallMaterializationFailed"));Path.ActualWalls[I]=Wall->ElementGuid;Walls.AddUnique(Wall);
#if WITH_DEV_AUTOMATION_TESTS
  const int32 Edge=Path.Route.PathEdges[I];if((I+1==PathWalls.Num()||Path.Route.PathEdges[I+1]!=Edge)&&EHBWallCreationCommand::FailAfterEdge==Edge+1){EHBWallCreationCommand::FailAfterEdge=0;return Fail(TEXT("InjectedPathEdgeFailure"));}
#endif
 }
 B->RebuildElementAndRelationshipIndexes();if(!B->RebuildWallNodeAuthorityGeometry())return Fail(TEXT("NodePathGeometryFailed"));
 Status=TEXT("Ready");return !Walls.IsEmpty();
}

bool FEHBRoomSubdivision::DetachRemovedHost(AEHBBuildingActorBase* B)
{
 for(auto& P:Floors)
 {
  if(P.bNew)continue;
  const auto Ids=P.Actor->SurfaceFinishRelationGuids;
  for(FGuid Id:Ids)
  {
   const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;});if(!R)return false;
   if(RemovedHosts.Contains(R->Source.ElementGuid)){if(!B->RemoveElementRelation(Id))return false;P.Actor->SurfaceFinishRelationGuids.Remove(Id);}
  }
 }
 return true;
}

bool FEHBRoomSubdivision::Apply(AEHBBuildingActorBase* B,const TMap<FGuid,FGuid>& SplitIds,FName& Status,const FPath* Path)
{
 auto Fail=[&](FName Reason){Status=Reason;return false;};auto Ids=SplitIds;
 if(Path)
 {
  if(Path->ActualPoints.Num()!=PathPillars.Num()||Path->ActualWalls.Num()!=PathWalls.Num())return Fail(TEXT("MissingSubdivisionPathResult"));
  if(!Path->ActualNodes.IsEmpty()&&Path->ActualNodes.Num()!=PathNodes.Num())return Fail(TEXT("MissingSubdivisionNodeResult"));
  for(int32 I=0;I<PathPillars.Num();++I)if(PathNodes[I].IsValid())
  {
   if(PathPillars[I].IsValid())
   {if(!Path->ActualPoints[I].IsValid())return Fail(TEXT("MissingPathPointResult"));Ids.Add(PathPillars[I],Path->ActualPoints[I]);}
   else if(Path->ActualPoints[I].IsValid())return Fail(TEXT("UnexpectedPhysicalPathResult"));
   const FGuid ActualNode=Path->ActualNodes.IsEmpty()?(Path->ActualPoints[I].IsValid()?B->FindNodeForPhysicalPillar(Path->ActualPoints[I]):Path->Route.Points[I].ExistingNodeGuid):Path->ActualNodes[I];
   if(!ActualNode.IsValid()||B->FindPhysicalPillarForNode(ActualNode)!=Path->ActualPoints[I])return Fail(TEXT("InvalidLogicalPathResult"));
   Ids.Add(PathNodes[I],ActualNode);
  }
  for(int32 I=0;I<PathWalls.Num();++I)Ids.Add(PathWalls[I],Path->ActualWalls[I]);
 }
 auto Map=[&](FGuid Id){const auto* Found=Ids.Find(Id);return Found?*Found:Id;};

 TSet<FGuid> OpeningWalls=PreservedOpeningWalls;for(const auto& W:Candidate.Walls)if(NewHosts.Contains(W.WallGuid))OpeningWalls.Add(Map(W.WallGuid));
 FEHBWallNodeModel Actual;TArray<FEHBNodeRoomBoundary> ActualRooms;
 if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual,&OpeningWalls).bSucceeded||!FEHBWallNodeRooms::Build(B->BuildingGuid,Actual,ActualRooms,Status)||ActualRooms.Num()!=PlannedRoomCount)return Fail(TEXT("SubdivisionTopologyMismatch"));
 // Newly enclosed room IDs depend on final wall identities, so derive expected
 // rooms from the mapped model, rather than treating temporary IDs as final IDs.
 auto Mapped=Candidate;for(auto& N:Mapped.Nodes)N.NodeGuid=Map(N.NodeGuid);for(auto& W:Mapped.Walls){W.WallGuid=Map(W.WallGuid);W.StartNodeGuid=Map(W.StartNodeGuid);W.EndNodeGuid=Map(W.EndNodeGuid);}for(auto& P:Mapped.PillarBindings){P.NodeGuid=Map(P.NodeGuid);P.PhysicalPillarGuid=Map(P.PhysicalPillarGuid);}
 TArray<FEHBNodeRoomBoundary> ExpectedRooms;if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Mapped,ExpectedRooms,Status))return false;
 for(const auto& R:ActualRooms)if(!ExpectedRooms.ContainsByPredicate([&](const auto& E){return E.RoomGuid==R.RoomGuid&&E.FloorIndex==R.FloorIndex;}))return Fail(TEXT("SubdivisionRoomIdentityMismatch"));
 if(Actual.Nodes.Num()!=Candidate.Nodes.Num()||Actual.Walls.Num()!=Candidate.Walls.Num()||Actual.PillarBindings.Num()!=Mapped.PillarBindings.Num())return Fail(TEXT("SubdivisionModelCountMismatch"));
 for(const auto& Binding:Mapped.PillarBindings)if(!Actual.PillarBindings.ContainsByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid&&V.PhysicalPillarGuid==Binding.PhysicalPillarGuid;}))return Fail(TEXT("SubdivisionBindingMismatch"));
 for(const auto& N:Candidate.Nodes)
 {
  const auto* A=Actual.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Map(N.NodeGuid);});
  if(!A||A->FloorIndex!=N.FloorIndex||!A->LocalTransform.Equals(N.LocalTransform,0.001)||!A->JunctionDimensions.Equals(N.JunctionDimensions,0.001))return Fail(TEXT("SubdivisionNodeMismatch"));
 }
 for(const auto& W:Candidate.Walls)
 {
  const auto* A=Actual.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Map(W.WallGuid);});
  if(!A||A->StartNodeGuid!=Map(W.StartNodeGuid)||A->EndNodeGuid!=Map(W.EndNodeGuid)||A->Height!=W.Height||A->Thickness!=W.Thickness)return Fail(TEXT("SubdivisionWallMismatch"));
 }
 TMap<FGuid,FGuid> RoomIds;
 for(const auto& R:PlannedRooms)
 {
  TSet<FGuid> Walls;for(FGuid Id:R.WallGuids)Walls.Add(Map(Id));
  const auto* ActualRoom=ExpectedRooms.FindByPredicate([&](const auto& C){if(C.FloorIndex!=R.FloorIndex||C.WallGuids.Num()!=Walls.Num())return false;for(FGuid Id:C.WallGuids)if(!Walls.Contains(Id))return false;return true;});
  if(!ActualRoom)return Fail(TEXT("UnmappedInheritedRoom"));RoomIds.Add(R.RoomGuid,ActualRoom->RoomGuid);
 }
 auto CopyCommon=[](AEHBElementActorBase* A,const AEHBElementActorBase* Source){A->ElementName=Source->ElementName;A->SemanticTags=Source->SemanticTags;A->Tags=Source->Tags;A->bNotifyWhenMovedInEditor=Source->bNotifyWhenMovedInEditor;A->SetActorHiddenInGame(Source->IsHidden());};
 for(auto& P:Slabs)if(P.bNew)
 {
  auto* Source=P.Actor;FActorSpawnParameters Params;Params.Owner=B;Params.OverrideLevel=B->GetLevel();Params.ObjectFlags=RF_Transactional;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* A=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(Params);if(!A)return Fail(TEXT("InheritedSlabSpawnFailed"));A->Modify();A->ElementGuid=P.Id;CopyCommon(A,Source);
  A->Thickness=Source->Thickness;A->Offset=Source->Offset;A->SurfaceMaterial=Source->SurfaceMaterial;A->VisualExpansion=Source->VisualExpansion;A->bEnableAdjacentSlabSnap=Source->bEnableAdjacentSlabSnap;
  A->AttachToBuilding(B,Source->GetElementLocalTransform());A->SetFloorAssignment(Source->FloorIndex,Source->FloorRole);A->FloorAssignmentPolicy=Source->FloorAssignmentPolicy;P.Actor=A;Ids.Add(P.Id,A->ElementGuid);
#if WITH_DEV_AUTOMATION_TESTS
  if(EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterInheritedSlabSpawn))return Fail(TEXT("InjectedInheritedSlabFailure"));
#endif
 }
 for(auto& P:Floors)if(P.bNew)
 {
  auto* Source=P.Actor;FActorSpawnParameters Params;Params.Owner=B;Params.OverrideLevel=B->GetLevel();Params.ObjectFlags=RF_Transactional;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* A=B->GetWorld()->SpawnActor<AEHB_Floor>(Params);if(!A)return Fail(TEXT("InheritedFloorSpawnFailed"));A->Modify();A->ElementGuid=P.Id;CopyCommon(A,Source);
  A->VisualOffset=Source->VisualOffset;A->FloorMaterial=Source->FloorMaterial;A->bEnableRuntimeCollision=Source->bEnableRuntimeCollision;A->bEnableEditorCollision=Source->bEnableEditorCollision;
  A->AttachToBuilding(B,FTransform::Identity);A->SetFloorAssignment(Source->FloorIndex,Source->FloorRole);A->FloorAssignmentPolicy=Source->FloorAssignmentPolicy;P.Actor=A;Ids.Add(P.Id,A->ElementGuid);
#if WITH_DEV_AUTOMATION_TESTS
  if(EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterInheritedFloorSpawn))return Fail(TEXT("InjectedInheritedFloorFailure"));
#endif
 }
 for(int32 SlabIndex=0;SlabIndex<Slabs.Num();++SlabIndex)
 {
  auto& P=Slabs[SlabIndex];auto& Display=Displays.Slabs[SlabIndex];Display.Actor=P.Actor;
  if(P.Source==EEHBOutlineSource::RetainedRegion)
  {bool Changed=false;if(Display.Display.IsActive()){if(!P.Actor->SetPartitionedSlabOutline(P.Polygon,Display.Display))return Fail(TEXT("RetainedDisplayApplyFailed"));}else if(!P.Actor->SetSlabOutlineIfNeeded(P.Polygon,Changed))return Fail(TEXT("RetainedSlabApplyFailed"));P.Actor->RecordOutlineSource(P.Source);continue;}
  P.Actor->RoomFillLoopGuid=RoomIds.FindChecked(P.Room);P.Actor->RoomFillFloorIndex=P.Actor->FloorIndex;
  if(P.bRebound)P.Actor->bHasRoomFillAnchor=P.Anchor.IsValid();
  if(P.Actor->bHasRoomFillAnchor){P.Actor->RoomFillAnchorWallGuid=Map(P.Anchor);P.Actor->RoomFillAnchorWallSide=P.Side;}
  const auto RoomValues=B->GetClosedLoopsByFloor(P.Actor->RoomFillFloorIndex);const auto* Room=RoomValues.FindByPredicate([&](const auto& R){return R.LoopGuid==P.Actor->RoomFillLoopGuid;});TArray<FVector> Polygon;
  if(!Room)return Fail(TEXT("SubdivisionSlabRoomMissing"));
  if(!FEasyHouseEditorMode::BuildRoomSlabFollowOutline(P.Actor,*Room,{},Polygon))return Fail(TEXT("SubdivisionSlabOutlineFailed"));
  if(!AlignOutline(Polygon,P.Polygon)||Polygon.Num()!=P.Polygon.Num())return Fail(TEXT("SubdivisionSlabVertexMismatch"));
  for(int32 I=0;I<Polygon.Num();++I)if(!Polygon[I].Equals(P.Polygon[I],0.001))
  {
   UE_LOG(LogTemp,Warning,TEXT("Subdivision slab point %d: planned %s, actual %s"),I,*P.Polygon[I].ToString(),*Polygon[I].ToString());
   return Fail(TEXT("SubdivisionSlabMismatch"));
  }
  if(Display.Display.IsActive()){auto Partition=Display.Display;Partition.SourcePolygon=Polygon;P.Actor->ResolveLogicalSurfaceIdentity(TEXT("Slab.Surface"),INDEX_NONE);if(!P.Actor->SetPartitionedSlabOutline(Polygon,Partition))return Fail(TEXT("SubdivisionDisplayApplyFailed"));}
  else if(!P.Actor->SetSlabOutline(Polygon,{}))return Fail(TEXT("SubdivisionSlabApplyFailed"));P.Actor->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
 }
 if(!Displays.Verify(Status))return false;
 for(FGuid Id:NewHosts)if(!Ids.Contains(Id)||!Map(Id).IsValid()||!B->FindElementActorByGuid(Map(Id)))return Fail(TEXT("MissingSubdivisionHostResult"));
 for(const auto& Pair:Tops)
 {
  TArray<FEHBFloorSupportSurface> Surfaces;
  if(!FEHBFloorContactGeometry::CaptureHorizontalTops(B->FindElementActorByGuid(Map(Pair.Key)),Surfaces,Status))return false;
  // The topology draft describes base caps. Opening migration is validated by
  // the enclosing command. Ignore additional reveals only when neither a base
  // cap nor any planned finish can contact their plane. A top-reaching opening
  // still changes cap coverage and fails; a finish at sill height also fails
  // closed until the draft explicitly models that reveal.
  Surfaces.RemoveAll([&](const FEHBFloorSupportSurface& Surface)
  {
   if(Surface.OuterPolygon.IsEmpty())return false;
   const double Z=Surface.OuterPolygon[0].Z;
   for(const auto& Top:Pair.Value)if(!Top.OuterPolygon.IsEmpty()&&FMath::Abs(Top.OuterPolygon[0].Z-Z)<=1.0)return false;
   for(const auto& Floor:Floors)for(const auto& Region:Floor.Regions)
    if(!Region.OuterPolygon.IsEmpty()&&FMath::Abs(Region.OuterPolygon[0].Z-Z)<=1.0)return false;
   return true;
  });
  if(!EHBRoomFinishMove::SameTopCoverage(Pair.Value,Surfaces,Status))return false;
 }
 for(auto& P:Floors)
 {
  P.Actor->RoomLoopGuid=P.Source==EEHBOutlineSource::RetainedRegion?FGuid():RoomIds.FindChecked(P.Room);P.Actor->RoomFloorIndex=P.Actor->FloorIndex;
  if(!P.Actor->SetFloorRegions(P.Regions,false))return Fail(TEXT("SubdivisionFloorApplyFailed"));P.Actor->RecordOutlineSource(P.Source);
  TArray<FEHBElementRelation> Check;if(!P.Actor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,Check,Status)||Check.Num()!=P.Relations.Num())return Fail(TEXT("SubdivisionContactCountMismatch"));
  TArray<FGuid> Next;const auto Previous=P.Actor->SurfaceFinishRelationGuids;
  for(auto R:P.Relations)
  {
   R.Source.ElementGuid=Map(R.Source.ElementGuid);R.Target.ElementGuid=P.Actor->ElementGuid;if(P.Source!=EEHBOutlineSource::RetainedRegion)R.StringMetadata.Add(TEXT("RoomLoopGuid"),P.Actor->RoomLoopGuid.ToString(EGuidFormats::DigitsWithHyphens));
   const auto* A=Check.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});
   if(!A||FMath::Abs(A->ContactArea-R.ContactArea)>0.01)return Fail(TEXT("SubdivisionContactMismatch"));
   // Contact.Point is a representative interior triangle point, not an identity.
   // Equal top coverage and area were verified above; record the final solver's
   // valid interior point even when an equivalent contour triangulates differently.
   R.ContactPoint=A->ContactPoint;R.ContactArea=A->ContactArea;R.ContactNormal=A->ContactNormal;
   if(B->AddOrUpdateElementRelation(R,true)!=R.RelationGuid)return Fail(TEXT("SubdivisionRelationApplyFailed"));Next.Add(R.RelationGuid);
  }
  for(FGuid Id:Previous)if(!Next.Contains(Id)&&!B->RemoveElementRelation(Id))return Fail(TEXT("SubdivisionRelationRemovalFailed"));P.Actor->SurfaceFinishRelationGuids=MoveTemp(Next);
 }
 Status=TEXT("Applied");return true;
}
