#include "EHBRoomFinishMove.h"
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EHBCopyOutlinePolicy.h"
#include "EasyHouseEditorMode.h"
#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBWallNodeRooms.h"
#include "Algo/Reverse.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBWallOpeningCandidate.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_DoorWindow.h"
#include "Core/EHBFloorAssignmentPlan.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "EHBNodeSupportMove.inl"
#include "EHBNodeDoorWindowMove.inl"

namespace
{
// Only the validated public adapters below may supply matching model and sides.
bool BuildTopologyTopsFromSides(const FEHBWallNodeModel& Definitions,const TArray<FEHBWallJunctionWallSides>& Sides,
 TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status)
{
 Out.Reset();TMap<FGuid,TArray<FEHBFloorSupportSurface>> Result;
 for(const auto& Side:Sides)
 {
  FEHBFloorSupportSurface Top;Top.OuterPolygon={Side.StartLeft,Side.EndLeft,Side.EndRight,Side.StartRight};
  Result.Add(Side.WallGuid,{MoveTemp(Top)});
 }
 for(const auto& Binding:Definitions.PillarBindings)
 {
  const auto* Node=Definitions.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Binding.NodeGuid;});
  if(!Node){Status=TEXT("MissingFinishNode");return false;}
  TArray<FEHBWallJunctionLeg> Legs;
  for(const auto& Wall:Definitions.Walls)if(Wall.StartNodeGuid==Node->NodeGuid||Wall.EndNodeGuid==Node->NodeGuid)
  {
   const auto* Side=Sides.FindByPredicate([&](const auto& V){return V.WallGuid==Wall.WallGuid;});
   if(!Side){Status=TEXT("MissingFinishWallSide");return false;}
   // Bound physical pillars rebuild against staged wall frames, not center-to-center
   // vectors. Predict that same frame after wall-face offsets have been resolved.
   FVector Direction=Side->LocalTransform.TransformVectorNoScale(FVector::ForwardVector);
   if(Wall.EndNodeGuid==Node->NodeGuid)Direction*=-1;
   auto& Leg=Legs.AddDefaulted_GetRef();Leg.Direction=Node->LocalTransform.InverseTransformVectorNoScale(Direction).GetSafeNormal2D();Leg.WallThickness=Wall.Thickness;
  }
  TArray<FVector> Footprint;
  if(!FEHBWallJunctionGeometry::BuildFootprint(Node->JunctionDimensions.X,Node->JunctionDimensions.Y,Legs,Footprint)){Status=TEXT("InvalidFinishFootprint");return false;}
  FEHBFloorSupportSurface Top;
  for(auto P:Footprint){P.Z=Node->JunctionDimensions.Z;Top.OuterPolygon.Add(Node->LocalTransform.TransformPosition(P));}
  Result.Add(Binding.PhysicalPillarGuid,{MoveTemp(Top)});
 }
 Out=MoveTemp(Result);Status=TEXT("Ready");return true;
}

}

bool EHBRoomFinishMove::BuildTopologyTops(const FEHBWallNodeModel& Definitions,
 TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status)
{
 Out.Reset();TArray<FEHBWallJunctionWallSides> Sides;
 if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Definitions,Sides,Status))return false;
 return BuildTopologyTopsFromSides(Definitions,Sides,Out,Status);
}

bool EHBRoomFinishMove::BuildTopologyTops(const FEHBWallNodeGeometryDraft& Geometry,
 TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status)
{
 Out.Reset();if(!Geometry.IsReady()){Status=TEXT("UnpreparedNodeGeometry");return false;}
 return BuildTopologyTopsFromSides(Geometry.GetModel(),Geometry.GetWallSides(),Out,Status);
}

bool EHBRoomFinishMove::BuildTopologyTops(const FEHBWallNodeSideDraft& Geometry,
 TMap<FGuid,TArray<FEHBFloorSupportSurface>>& Out,FName& Status)
{
 Out.Reset();if(!Geometry.IsReady()){Status=TEXT("UnpreparedNodeGeometry");return false;}
 return BuildTopologyTopsFromSides(Geometry.GetModel(),Geometry.GetWallSides(),Out,Status);
}

bool EHBRoomFinishMove::SameTopCoverage(const TArray<FEHBFloorSupportSurface>& Planned,
 const TArray<FEHBFloorSupportSurface>& Actual,FName& Status)
{
 if(Planned.IsEmpty()||Actual.IsEmpty()){const bool Same=Planned.IsEmpty()&&Actual.IsEmpty();Status=Same?TEXT("Matched"):TEXT("FinishHostCoverageChanged");return Same;}
 auto MatchesPlanes=[](const auto& A,const auto& B){for(const auto& Top:A){if(Top.OuterPolygon.IsEmpty())return false;const double Z=Top.OuterPolygon[0].Z;if(!B.ContainsByPredicate([&](const auto& Other){return !Other.OuterPolygon.IsEmpty()&&FMath::Abs(Other.OuterPolygon[0].Z-Z)<=0.001;}))return false;}return true;};
 if(!MatchesPlanes(Planned,Actual)||!MatchesPlanes(Actual,Planned)){Status=TEXT("FinishHostPlaneChanged");return false;}
 auto Regions=[](const TArray<FEHBFloorSupportSurface>& Tops){TArray<FEHBFloorFinishRegion> R;for(const auto& T:Tops){auto& V=R.AddDefaulted_GetRef();V.OuterPolygon=T.OuterPolygon;V.Holes=T.Holes;}return R;};
 FEHBFloorContact P,A,Intersection;
 if(!FEHBFloorContactGeometry::Build(Regions(Planned),Planned,P,Status)
  || !FEHBFloorContactGeometry::Build(Regions(Actual),Actual,A,Status)
  || !FEHBFloorContactGeometry::Build(Regions(Planned),Actual,Intersection,Status))return false;
 // Equality of both union areas with their intersection proves coverage equality
 // to the clipping grid; comparing area alone would accept shifted surfaces.
 const bool bSame=FMath::Abs(P.Area-Intersection.Area)<=0.01 && FMath::Abs(A.Area-Intersection.Area)<=0.01;
 Status=bSame?TEXT("Matched"):TEXT("FinishHostCoverageChanged");return bSame;
}

namespace
{
 bool SameFinishContact(const FEHBElementRelation& A,const FEHBElementRelation& B)
 {
  return A.Source.IsEquivalentTo(B.Source)&&A.Target.IsEquivalentTo(B.Target)
   &&FMath::Abs(A.ContactArea-B.ContactArea)<=0.01&&A.ContactPoint.Equals(B.ContactPoint,0.001)
   &&A.ContactNormal.Equals(B.ContactNormal,0.0001)&&A.StringMetadata.OrderIndependentCompareEqual(B.StringMetadata)
   &&A.NumericMetadata.OrderIndependentCompareEqual(B.NumericMetadata);
 }
 void AlignSlabStart(TArray<FVector>& Polygon,const TArray<FVector>& Previous)
 {
  if(Polygon.IsEmpty())return;
  auto Area=[](const auto& Points){double A=0;for(int32 I=0;I<Points.Num();++I)A+=Points[I].X*Points[(I+1)%Points.Num()].Y-Points[I].Y*Points[(I+1)%Points.Num()].X;return A;};
  if(Area(Polygon)*Area(Previous)<0)Algo::Reverse(Polygon);
  int32 Start=Previous.IsEmpty()?INDEX_NONE:Polygon.IndexOfByPredicate([&](const auto& P){return P.Equals(Previous[0],0.001);});
  if(Start==INDEX_NONE){Start=0;for(int32 I=1;I<Polygon.Num();++I)if(Polygon[I].X<Polygon[Start].X||(Polygon[I].X==Polygon[Start].X&&Polygon[I].Y<Polygon[Start].Y))Start=I;}
  const auto Copy=Polygon;for(int32 I=0;I<Polygon.Num();++I)Polygon[I]=Copy[(Start+I)%Copy.Num()];
 }
}

bool EHBRoomFinishMove::FNodeEditPlan::Prepare(AEHBBuildingActorBase* B,const FEHBWallNodeModel& Before,const FEHBWallNodeModel& After,
 const TArray<AEHBElementActorBase*>& Elements,FGuid RemovedPhysical,FName& Status,bool bFixedSlabSources)
{
 *this={};auto Fail=[&](FName R){*this={};Status=R;return false;};
 bFixedSourcePreparationOnly=bFixedSlabSources;
 if(bFixedSlabSources&&!FEHBWallNodeModel::StaticStruct()->CompareScriptStruct(&Before,&After,0))return Fail(TEXT("OpeningTopologyChangeRequiresPlan"));
 if(!B||!FEHBWallNodeRooms::Build(B->BuildingGuid,Before,OldRooms,Status))return Fail(TEXT("InvalidCopyRooms"));
 const auto Policy=FEHBCopyOutlinePolicy::PrepareWithValidatedRooms(B,OldRooms,Elements,true,bFixedSlabSources,&Before);if(!Policy.bSucceeded)return Fail(Policy.Status);
 for(const auto& Pair:Policy.FinishRelationRooms)OwnedRelations.Add(Pair.Key);
 if(RemovedPhysical.IsValid())RemovedHosts.Add(RemovedPhysical);
 if(!FEHBWallNodeRooms::Build(B->BuildingGuid,After,Rooms,Status))return Fail(Status);
 if(OldRooms.Num()!=Rooms.Num())return Fail(TEXT("RoomTopologyChangeRequiresPlan"));
 for(const auto& R:OldRooms)if(!Rooms.ContainsByPredicate([&](const auto& V){return V.RoomGuid==R.RoomGuid&&V.FloorIndex==R.FloorIndex;}))return Fail(TEXT("RoomTopologyChangeRequiresPlan"));
 CandidateSides=FEHBWallNodeSideDraft::Build(After,Status);if(!CandidateSides.IsReady())return Fail(Status);
 TSet<FGuid> ValidatedOpeningWalls;
 const auto SourceSides=FEHBWallNodeSideDraft::Build(Before,Status);if(!SourceSides.IsReady())return Fail(Status);
 for(auto* E:Elements)if(auto* W=Cast<AEHB_Wall>(E);W&&(!W->CutOperations.IsEmpty()||!W->DoorWindowConnections.IsEmpty()))
 {
  const auto* Prior=SourceSides.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==W->ElementGuid;});
  const auto* Side=CandidateSides.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==W->ElementGuid;});
  FEHBPreparedWallOpening Current,Candidate;
  if(W->OwningBuilding!=B||!Prior||!Side||!W->PrepareNodeSurfaceOpening(*Prior,Current,Status)||!W->PrepareNodeSurfaceOpening(*Side,Candidate,Status))return Fail(Status);
  ValidatedOpeningWalls.Add(W->ElementGuid);WallOpenings.Add(W->ElementGuid,MoveTemp(Candidate));
 }
 if(!ValidatedOpeningWalls.IsEmpty())
 {
  FEHBWallNodeModel Captured;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Captured,&ValidatedOpeningWalls);
  if(!Capture.bSucceeded)return Fail(Capture.Status);
  if(!FEHBWallNodeModel::StaticStruct()->CompareScriptStruct(&Before,&Captured,0))return Fail(TEXT("StaleNodeOpeningModel"));
 }
 if(!PrepareHostedElements(B,Elements,Status))return Fail(Status);
 // The complete candidate sides remain validated even with no finish actors.
 // Top/contact planning has no consumers on that path.
 const bool HasFinishes=Elements.ContainsByPredicate([](const AEHBElementActorBase* E){return E->IsA<AEHB_Floor>()||E->IsA<AEHB_FloorSlab>();});
 const bool HasSupports=!bFixedSlabSources&&B->ElementRelations.ContainsByPredicate([](const auto& R){return R.Type==EEHBElementRelationType::StructuralSupport||R.Type==EEHBElementRelationType::PhysicalContact;});
 if(!HasFinishes&&!HasSupports){Status=TEXT("Ready");return true;}
 const auto& Sides=CandidateSides.GetWallSides();
 if(!BuildTopologyTops(CandidateSides,Tops,Status))return Fail(Status);
 // A top-edge opening can remove all or part of a host. An empty result is
 // meaningful and must replace, rather than fall back to, the uncut rectangle.
 for(const auto& Pair:WallOpenings)Tops.Add(Pair.Key,Pair.Value.HorizontalTops);
 for(auto* E:Elements)if(auto* Slab=Cast<AEHB_FloorSlab>(E))
 {
  const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Slab->RoomFillLoopGuid&&R.FloorIndex==Slab->RoomFillFloorIndex;});
  auto& P=Slabs.AddDefaulted_GetRef();P.Actor=Slab;
  // The fixed-source path has already verified identical node models and the
  // current room boundary. A remapped room may enumerate its corners in another
  // order; do not turn an opening-only edit into a fresh outline allocation.
  if(bFixedSlabSources||Slab->OutlineSource==EEHBOutlineSource::RetainedRegion)P.Polygon=Slab->LocalTopPolygon;
  else if(!Room||!FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(Slab,*Room,After,Sides,P.Polygon))return Fail(TEXT("MissingNodeEditSlabOutline"));
  AlignSlabStart(P.Polygon,Slab->LocalTopPolygon);
  TArray<FEHBLogicalSurfaceRegion> Unallocated;const bool Valid=bFixedSlabSources?Slab->BuildCandidateDisplayRegions(P.Polygon,Slab->LocalHoles,Slab->CutOperations,Unallocated,Status,false):Slab->DisplayPartition.IsActive()&&P.Polygon!=Slab->LocalTopPolygon?Slab->BuildUnallocatedDisplayRegion(P.Polygon,Unallocated):Slab->ValidateSlabOutline(P.Polygon,{});
  if(!Valid)return Fail(TEXT("InvalidNodeEditSlabOutline"));
  if(bFixedSlabSources){TArray<FEHBFloorSupportSurface> SourceTops;for(const auto& R:Unallocated){auto& Top=SourceTops.AddDefaulted_GetRef();for(const auto& V:R.Boundary)Top.OuterPolygon.Add(Slab->GetElementLocalTransform().TransformPosition(FVector(V.X,V.Y,Slab->GetTopZ())));for(const auto& H:R.Holes){auto& Hole=Top.Holes.AddDefaulted_GetRef().LocalPolygon;for(const auto& V:H.Vertices)Hole.Add(Slab->GetElementLocalTransform().TransformPosition(FVector(V.X,V.Y,Slab->GetTopZ())));}}Tops.Add(Slab->ElementGuid,MoveTemp(SourceTops));}
  else {FEHBFloorSupportSurface Top;for(auto Point:P.Polygon){Point.Z=Slab->GetTopZ();Top.OuterPolygon.Add(Slab->GetElementLocalTransform().TransformPosition(Point));}Tops.Add(Slab->ElementGuid,{MoveTemp(Top)});}
 }
 for(const auto& P:Slabs){auto& C=Displays.Slabs.AddDefaulted_GetRef();C.Actor=P.Actor;C.Polygon=P.Polygon;}
 for(const auto& P:Slabs)if(P.Polygon!=P.Actor->LocalTopPolygon&&Displays.HasConstraintsInLayer(P.Actor))Displays.DisplayLayers.Add(P.Actor);
 if(!Displays.Prepare(Status))return Fail(Status);
 FEHBFloorHostTopologyDraft Hosts;Hosts.RemovedHostGuids=RemovedHosts;
 for(auto* E:Elements)if(auto* Floor=Cast<AEHB_Floor>(E))
 {
  const auto* Old=OldRooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Floor->RoomLoopGuid;});
  const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Floor->RoomLoopGuid;});if(Floor->OutlineSource!=EEHBOutlineSource::RetainedRegion&&(!Old||!Room))return Fail(TEXT("MissingNodeEditFloorRoom"));
  auto& P=Floors.AddDefaulted_GetRef();P.Actor=Floor;P.Source=Floor->OutlineSource;P.Regions=Floor->FloorRegions;
  if(P.Source!=EEHBOutlineSource::RetainedRegion)
  {
  // Preserve the floor's authored first logical corner even when that corner moves.
  const auto First=P.Regions[0].OuterPolygon[0];const int32 OldIndex=Old->Polygon.IndexOfByPredicate([&](const auto& V){return FVector2D(V).Equals(FVector2D(First),0.001);});
  if(OldIndex==INDEX_NONE)return Fail(TEXT("MissingFloorStartNode"));const int32 Start=Room->NodeGuids.Find(Old->NodeGuids[OldIndex]);if(Start==INDEX_NONE)return Fail(TEXT("MissingFloorStartNode"));
  const int32 Second=Old->Polygon.IndexOfByPredicate([&](const auto& V){return FVector2D(V).Equals(FVector2D(P.Regions[0].OuterPolygon[1]),0.001);});
  const int32 Direction=Second==(OldIndex+1)%Old->Polygon.Num()?1:Second==(OldIndex+Old->Polygon.Num()-1)%Old->Polygon.Num()?-1:0;if(!Direction)return Fail(TEXT("InvalidFloorNodeOrder"));
  auto& Polygon=P.Regions[0].OuterPolygon;Polygon.Reset();for(int32 I=0;I<Room->Polygon.Num();++I){auto V=Room->Polygon[(Start+Direction*I+Room->Polygon.Num())%Room->Polygon.Num()];V.Z=First.Z;Polygon.Add(V);}
  }
  if(!Floor->ValidateFloorRegions(P.Regions))return Fail(TEXT("InvalidNodeEditFloorOutline"));
  TArray<FEHBElementRelation> Current;
  if(!Floor->BuildCurrentSurfaceFinishRelationPlan(Floor->FloorRegions,Current,Status))return Fail(Status);
  for(const auto& R:Current)if(RemovedHosts.Contains(R.Source.ElementGuid))RemovedContactRooms.Add(Floor->RoomLoopGuid);
  if(Current.Num()!=Floor->SurfaceFinishRelationGuids.Num())return Fail(TEXT("StaleFinishContacts"));
  for(const auto& R:Current){const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});if(!Saved||!SameFinishContact(R,*Saved))return Fail(TEXT("StaleFinishContacts"));}
  if(!Floor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,P.Relations,Status,&Tops,&Hosts))return Fail(Status);
 }
 if(HasSupports&&!PrepareSupportRelations(B,Elements,Status))return Fail(Status);
 Status=TEXT("Ready");return true;
}

void EHBRoomFinishMove::FNodeEditPlan::DetachRemovedHostContacts(AEHBBuildingActorBase* B) const
{
 for(const auto& P:Floors)
 {
  TArray<FGuid> Remove;for(const auto& R:B->ElementRelations)if(P.Actor->SurfaceFinishRelationGuids.Contains(R.RelationGuid)&&RemovedHosts.Contains(R.Source.ElementGuid))Remove.Add(R.RelationGuid);
  for(FGuid Id:Remove){P.Actor->SurfaceFinishRelationGuids.Remove(Id);B->RemoveElementRelation(Id);}
 }
}

bool EHBRoomFinishMove::FNodeEditPlan::Apply(AEHBBuildingActorBase* B,FName& Status,bool(*Failure)(int32),TArray<FGuid>* OutChangedRooms) const
{
 if(!WallOpenings.IsEmpty()&&(!B||!B->HasUnpublishedEdit())){Status=TEXT("NodeOpeningCommitRequiresPlan");return false;}

 if(bFixedSourcePreparationOnly){Status=TEXT("FixedSourcePlanRequiresRegionApply");return false;}
 if(!VerifyHostedElements(B,Status))return false;
 if(OutChangedRooms)OutChangedRooms->Reset();TSet<FGuid> ChangedRooms=RemovedContactRooms;
 auto Fail=[&](FName R){Status=R;return false;};if(Floors.IsEmpty()&&Slabs.IsEmpty()&&SupportRelationIds.IsEmpty()){Status=TEXT("NoFinishes");return true;}
 for(const auto& P:Floors){bool Changed=false;if(!P.Actor->SetFloorRegionsIfNeeded(P.Regions,Changed))return Fail(TEXT("NodeEditFloorFailed"));if(Changed){P.Actor->RecordOutlineSource(P.Source);ChangedRooms.Add(P.Actor->RoomLoopGuid);}if(!P.Actor->IsRecordedOutlineUnchanged())return Fail(TEXT("NodeEditFloorProvenanceFailed"));}
 if(Failure&&Failure(4))return Fail(TEXT("InjectedNodeFloorFailure"));
 for(int32 I=0;I<Slabs.Num();++I)
 {
  const auto& P=Slabs[I];const auto Source=P.Actor->OutlineSource;bool Changed=false;
  if(Displays.Slabs[I].Display.IsActive()){if(!P.Actor->SetPartitionedSlabOutline(P.Polygon,Displays.Slabs[I].Display))return Fail(TEXT("NodeEditDisplayFailed"));Changed=true;}
  else if(!P.Actor->SetSlabOutlineIfNeeded(P.Polygon,Changed))return Fail(TEXT("NodeEditSlabFailed"));
  if(Changed){P.Actor->RecordOutlineSource(Source);ChangedRooms.Add(P.Actor->RoomFillLoopGuid);}if(!P.Actor->IsRecordedOutlineUnchanged())return Fail(TEXT("NodeEditSlabProvenanceFailed"));
 }
 if(!Displays.Verify(Status))return false;
 if(Failure&&Failure(5))return Fail(TEXT("InjectedNodeSlabFailure"));
 for(const auto& Pair:Tops){TArray<FEHBFloorSupportSurface> Actual;if(!FEHBFloorContactGeometry::CaptureHorizontalTops(B->FindElementActorByGuid(Pair.Key),Actual,Status)||!SameTopCoverage(Pair.Value,Actual,Status))return false;}
 for(const auto& P:Floors)
 {
  TArray<FEHBElementRelation> Actual;
  if(!P.Actor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,Actual,Status)||Actual.Num()!=P.Relations.Num())return Fail(TEXT("NodeEditFinishContactMismatch"));
  for(const auto& R:P.Relations)if(!Actual.ContainsByPredicate([&](const auto& V){return SameFinishContact(R,V);}))return Fail(TEXT("NodeEditFinishContactMismatch"));
  const auto Previous=P.Actor->SurfaceFinishRelationGuids;TArray<FGuid> Next;
  for(const auto& R:P.Relations)
  {
   auto Expected=R;Expected.SourceGeometryRevision=B->GetElementGeometryRevision(R.Source.ElementGuid);Expected.TargetGeometryRevision=B->GetElementGeometryRevision(R.Target.ElementGuid);
   const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});
   if(Saved&&FEHBElementRelation::StaticStruct()->CompareScriptStruct(Saved,&Expected,0)){Next.Add(R.RelationGuid);continue;}
   const auto Id=B->AddOrUpdateElementRelation(R,true);if(Id!=R.RelationGuid)return Fail(TEXT("NodeEditFinishPublishFailed"));Next.Add(Id);ChangedRooms.Add(P.Actor->RoomLoopGuid);
  }
  for(FGuid Id:Previous)if(!Next.Contains(Id)){B->RemoveElementRelation(Id);ChangedRooms.Add(P.Actor->RoomLoopGuid);}if(P.Actor->SurfaceFinishRelationGuids!=Next)P.Actor->SurfaceFinishRelationGuids=MoveTemp(Next);
 }
 for(const auto& Previous:OriginalSupportRelations)
 {
  auto* Source=B->FindElementActorByGuid(Previous.Source.ElementGuid);auto* Target=B->FindElementActorByGuid(Previous.Target.ElementGuid);
  TArray<FEHBFloorSupportSurface> Top;TArray<FEHBFloorFinishRegion> Bottom;FEHBFloorContact Actual;
  if(Source&&!FEHBFloorContactGeometry::CaptureHorizontalTops(Source,Top,Status))return false;
  if(Target&&!CaptureSupportBottom(Target,false,Bottom,Status))return false;
  if(!Top.IsEmpty()&&!Bottom.IsEmpty()&&!FEHBFloorContactGeometry::Build(Bottom,Top,Actual,Status))return false;
  const auto* Planned=SupportRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Previous.RelationGuid;});
  if(!Planned){if(Actual.Area>0.01)return Fail(TEXT("NodeSupportRemovalMismatch"));if(B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Previous.RelationGuid;})&&!B->RemoveElementRelation(Previous.RelationGuid))return Fail(TEXT("NodeSupportRemovalFailed"));}
  else
  {
   if(FMath::Abs(Actual.Area-Planned->ContactArea)>0.01||!Actual.Point.Equals(Planned->ContactPoint,0.001))return Fail(TEXT("NodeSupportPublicationMismatch"));
   if(B->AddOrUpdateElementRelation(*Planned,true)!=Planned->RelationGuid)return Fail(TEXT("NodeSupportPublicationFailed"));
  }
 }
 if(Failure&&Failure(6))return Fail(TEXT("InjectedNodeContactFailure"));
 FEHBWallNodeModel Current;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Current).bSucceeded)return Fail(TEXT("InvalidFinalNodeFinishModel"));
 const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Current,B->QueryElements(FEHBElementQuery()),true);if(!Policy.bSucceeded)return Fail(Policy.Status);
 ChangedRooms.Remove(FGuid());
 if(OutChangedRooms){*OutChangedRooms=ChangedRooms.Array();OutChangedRooms->Sort();}
 Status=TEXT("Applied");return true;
}
