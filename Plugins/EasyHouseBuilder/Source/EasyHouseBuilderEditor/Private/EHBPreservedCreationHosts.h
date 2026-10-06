#pragma once

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Wall.h"
#include "EHBPreservedRailing.h"
#include "Core/EHBWallPathPlanning.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeGeometryDraft.h"

// Retained hosts are validated independently from room inheritance. This policy
// does not migrate an opening or alter a railing's attachment junction.
struct FEHBPreservedCreationHosts
{
 struct FOpening
 {
  AEHB_DoorWindow* Actor=nullptr;
  AEHB_Wall* Wall=nullptr;
  FTransform Pose,WallPose;
  FVector Start,End;
  FEHBWallDoorWindowConnection Connection;
  FEHBCutOperation Cut;
  FEHBElementRelation Relation;
 };
 TArray<FOpening> Openings;
 TArray<FEHBPreservedRailing> Railings;
 TSet<FGuid> OpeningWalls,Relations;

 static bool IsRectangle(const TArray<FVector>& Points,float Width,float Height)
 {
  if(Points.Num()!=4||!FMath::IsFinite(Width)||!FMath::IsFinite(Height)||Width<=0||Height<=0)return false;
  int32 Mask=0;int32 Corners[4];
  for(int32 I=0;I<4;++I)
  {
   const auto P=Points[I];
   if(P.ContainsNaN()||!FMath::IsNearlyZero(P.Y,0.001)||!FMath::IsNearlyEqual(FMath::Abs(P.X),Width*.5,0.001)
    ||!(FMath::IsNearlyZero(P.Z,0.001)||FMath::IsNearlyEqual(P.Z,static_cast<double>(Height),0.001)))return false;
   Corners[I]=(P.X>0?1:0)|(P.Z>Height*.5?2:0);Mask|=1<<Corners[I];
  }
  if(Mask!=15)return false;
  for(int32 I=0;I<4;++I){const int32 Changed=Corners[I]^Corners[(I+1)%4];if(Changed!=1&&Changed!=2)return false;}
  return true;
 }
 bool Capture(AEHBBuildingActorBase* B,const TArray<AEHBElementActorBase*>& Elements,FName& Status)
 {
  *this={};auto Fail=[&](FName Why){*this={};Status=Why;return false;};
  TMap<FGuid,AEHBElementActorBase*> ById;
  for(auto* E:Elements){if(!E||!E->ElementGuid.IsValid()||ById.Contains(E->ElementGuid))return Fail(TEXT("InvalidPreservedElementIdentity"));ById.Add(E->ElementGuid,E);}
  for(auto* E:Elements)
  {
   if(auto* D=Cast<AEHB_DoorWindow>(E))
   {
    auto* W=Cast<AEHB_Wall>(ById.FindRef(D->OwningWallGuid));
    if(D->GetClass()!=AEHB_DoorWindow::StaticClass()||!W||W->GetClass()!=AEHB_Wall::StaticClass()
     ||D->FloorIndex!=W->FloorIndex||!D->CutOperations.IsEmpty()||!D->GetActorScale3D().Equals(FVector::OneVector)
     ||!D->GetActorQuat().Equals(W->GetActorQuat(),0.001))return Fail(TEXT("UnsupportedPreservedOpening"));
    TArray<FEHBWallDoorWindowConnection> Connections=W->DoorWindowConnections.FilterByPredicate([&](const auto& C){return C.DoorWindowGuid==D->ElementGuid;});
    TArray<FEHBCutOperation> Cuts=W->CutOperations.FilterByPredicate([&](const auto& C){return C.Source.SourceElementGuid==D->ElementGuid;});
    TArray<FEHBElementRelation> Links=B->ElementRelations.FilterByPredicate([&](const auto& R){return R.Source.RefersToElement(D->ElementGuid)||R.Target.RefersToElement(D->ElementGuid);});
    if(Connections.Num()!=1||Cuts.Num()!=1||Links.Num()!=1)return Fail(TEXT("IncompletePreservedOpening"));
    const auto& C=Connections[0];const auto& Cut=Cuts[0];const auto& R=Links[0];
    if(!IsRectangle(C.LocalOutlinePoints,C.OpeningWidth,C.OpeningHeight)||C.Kind!=D->Kind||C.DistanceFromStart!=D->DistanceFromWallStart
     ||C.OpeningWidth!=D->OpeningWidth||C.OpeningHeight!=D->OpeningHeight||C.OpeningThickness!=D->OpeningThickness
     ||C.BottomHeight!=D->GetOpeningBottomHeight()||!FMath::IsFinite(C.DistanceFromStart)||!FMath::IsFinite(C.BottomHeight)
     ||!FMath::IsFinite(C.OpeningThickness)||C.OpeningThickness<=0||C.BottomHeight<0||C.BottomHeight+C.OpeningHeight>=W->Height
     ||C.DistanceFromStart-C.OpeningWidth*.5<0||C.DistanceFromStart+C.OpeningWidth*.5>FVector::Dist2D(W->LocalStart,W->LocalEnd)
     ||!D->GetActorLocation().Equals(W->GetWorldLocationOnCenterAxisAtDistance(C.DistanceFromStart,C.BottomHeight),0.001))return Fail(TEXT("StalePreservedOpening"));
    const auto Points=D->GetOpeningOutlineLocalPoints();if(Points.Num()!=C.LocalOutlinePoints.Num())return Fail(TEXT("StalePreservedOpeningOutline"));
    for(int32 I=0;I<Points.Num();++I)if(!Points[I].Equals(C.LocalOutlinePoints[I],0.001))return Fail(TEXT("StalePreservedOpeningOutline"));
    if(!Cut.OperationGuid.IsValid()||!Cut.bEnabled||Cut.OperationType!=EEHBCutOperationType::Subtract||Cut.Stage!=EEHBCutStage::SurfaceOpening
     ||Cut.ProjectionMode!=EEHBCutProjectionMode::VerticalXZ||Cut.TransformPolicy!=EEHBCutTransformPolicy::SourceActorDriven
     ||Cut.Priority!=100||!Cut.OperationTag.IsNone()||Cut.Source.SourceType!=EEHBCutSourceType::Element||Cut.Source.SourceElement!=D
     ||!R.RelationGuid.IsValid()||!R.bEnabled||R.Type!=EEHBElementRelationType::HostedElement||R.Origin!=EEHBRelationOrigin::SystemGenerated
     ||!R.Source.RefersToElement(W->ElementGuid)||!R.Target.RefersToElement(D->ElementGuid))return Fail(TEXT("UnsupportedPreservedOpeningLink"));
    auto& P=Openings.AddDefaulted_GetRef();P.Actor=D;P.Wall=W;P.Pose=D->GetActorTransform();P.WallPose=W->GetElementLocalTransform();P.Start=W->LocalStart;P.End=W->LocalEnd;P.Connection=C;P.Cut=Cut;P.Relation=R;
    OpeningWalls.Add(W->ElementGuid);Relations.Add(R.RelationGuid);
   }
   else if(auto* R=Cast<AEHB_Railing>(E))
   {
    if(!R->CutOperations.IsEmpty()||!R->GetActorScale3D().Equals(FVector::OneVector)||!FEHBPreservedRailing::Supports(R))return Fail(TEXT("UnsupportedPreservedRailing"));
    const auto Links=B->ElementRelations.FilterByPredicate([&](const auto& L){return L.Source.RefersToElement(R->ElementGuid)||L.Target.RefersToElement(R->ElementGuid);});
    if(Links.Num()!=1||!FEHBPreservedRailing::MatchesRelation(R,Links[0]))return Fail(TEXT("IncompletePreservedRailing"));
    Railings.AddDefaulted_GetRef().Capture(R,Links[0]);Relations.Add(Links[0].RelationGuid);
   }
  }
  for(FGuid Id:OpeningWalls)
  {
   const auto* W=CastChecked<AEHB_Wall>(ById[Id]);int32 Count=0;for(const auto& O:Openings)if(O.Wall==W)++Count;
   if(W->DoorWindowConnections.Num()!=Count||W->CutOperations.Num()!=Count)return Fail(TEXT("UnplannedPreservedWallCut"));
  }
  Status=TEXT("Ready");return true;
 }
 bool Contains(const AEHBElementActorBase* E) const
 {
  return Openings.ContainsByPredicate([&](const auto& P){return P.Actor==E;})||Railings.ContainsByPredicate([&](const auto& P){return P.Actor==E;});
 }
 bool ValidateCandidate(AEHBBuildingActorBase* B,const FEHBWallNodeModel& Candidate,const FEHBWallPathPlan& Route,
  const FEHBWallCreationOptions& Options,FName& Status) const
 {
  const auto Geometry=FEHBWallNodeGeometryDraft::Build(Candidate,Status);
  return Geometry.IsReady()&&ValidateCandidate(B,Geometry,Route,Options,Status);
 }
 bool ValidateCandidate(AEHBBuildingActorBase* B,const FEHBWallNodeGeometryDraft& Geometry,const FEHBWallPathPlan& Route,
  const FEHBWallCreationOptions& Options,FName& Status) const
 {
  if(!Geometry.IsReady()){Status=TEXT("UnpreparedNodeGeometry");return false;}
  const auto& Candidate=Geometry.GetModel();
  FEHBWallNodeModel Before;
  if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Before,&OpeningWalls).bSucceeded){Status=TEXT("InvalidPreservedHostSource");return false;}
  TArray<FEHBWallJunctionWallSides> OldSides;const auto& NewSides=Geometry.GetWallSides();
  if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Before,OldSides,Status,&OpeningWalls))return false;
  for(const auto& Old:OldSides)
  {
   const auto* N=NewSides.FindByPredicate([&](const auto& V){return V.WallGuid==Old.WallGuid;});
   if(!N||!N->LocalStart.Equals(Old.LocalStart,1.e-5)||!N->LocalEnd.Equals(Old.LocalEnd,1.e-5)
    ||!N->LocalTransform.Equals(Old.LocalTransform,1.e-5)||!N->StartLeft.Equals(Old.StartLeft,1.e-5)||!N->StartRight.Equals(Old.StartRight,1.e-5)
    ||!N->EndLeft.Equals(Old.EndLeft,1.e-5)||!N->EndRight.Equals(Old.EndRight,1.e-5)){Status=TEXT("OpeningHostChangeRequiresMigration");return false;}
  }
  for(const auto Pair:Route.Segments)
  {
   const auto& A=Route.Points[Pair.X];const auto& Z=Route.Points[Pair.Y];bool SameDirection=false;
   if(A.ExistingPillarGuid.IsValid()&&Z.ExistingPillarGuid.IsValid()&&B->FindWallBetweenPillars(A.ExistingPillarGuid,Z.ExistingPillarGuid,SameDirection))continue;
   if(A.ExistingNodeGuid.IsValid()&&Z.ExistingNodeGuid.IsValid()&&Before.Walls.ContainsByPredicate([&](const auto& W){return (W.StartNodeGuid==A.ExistingNodeGuid&&W.EndNodeGuid==Z.ExistingNodeGuid)||(W.EndNodeGuid==A.ExistingNodeGuid&&W.StartNodeGuid==Z.ExistingNodeGuid);}))continue;
   for(const auto& R:Railings)if(R.Actor->FloorIndex==A.FloorIndex)
   {
    if(A.ExistingPillarGuid==R.Actor->StartAnchor.ElementGuid||Z.ExistingPillarGuid==R.Actor->StartAnchor.ElementGuid){Status=TEXT("RailingJunctionChangeRequiresPlan");return false;}
    const auto T=R.Actor->GetActorTransform();const auto BT=B->GetActorTransform();
    FVector P=A.LocalPosition,Q=Z.LocalPosition,C=BT.InverseTransformPosition(T.TransformPosition(R.Start)),D=BT.InverseTransformPosition(T.TransformPosition(R.End)),X,Y;
    P.Z=Q.Z=C.Z=D.Z=0;FMath::SegmentDistToSegmentSafe(P,Q,C,D,X,Y);
    const double Clearance=Options.WallThickness*.5+FMath::Max(R.Actor->PostWidth,R.Actor->RailThickness)*.707107;
    if(FVector::DistSquared(X,Y)<=Clearance*Clearance){Status=TEXT("CreatedWallIntersectsPreservedRailing");return false;}
    const double PostClearance=FMath::Sqrt(FMath::Square(Options.PillarWidth)+FMath::Square(Options.PillarDepth))*.5
     +FMath::Max(R.Actor->PostWidth,R.Actor->RailThickness)*.707107;
    if(Options.bCreatePhysicalColumns&&((!A.ExistingPillarGuid.IsValid()&&!A.ExistingNodeGuid.IsValid()&&FMath::PointDistToSegment(P,C,D)<=PostClearance)
     ||(!Z.ExistingPillarGuid.IsValid()&&!Z.ExistingNodeGuid.IsValid()&&FMath::PointDistToSegment(Q,C,D)<=PostClearance))){Status=TEXT("CreatedColumnIntersectsPreservedRailing");return false;}
   }
  }
  // An acute unbound junction may extend beyond the wall strip. New derived
  // fills have geometry, but no physical column identity or support relation.
  if(!Options.bCreatePhysicalColumns&&!Railings.IsEmpty())for(const auto& N:Candidate.Nodes)
   if(!Before.Nodes.ContainsByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;})&&!Candidate.PillarBindings.ContainsByPredicate([&](const auto& P){return P.NodeGuid==N.NodeGuid;}))
   {
    const auto* Mesh=Geometry.GetUnboundJunctions().Find(N.NodeGuid);if(!Mesh){Status=TEXT("MissingPlannedJunction");return false;}
    double Radius=0;for(const auto& V:Mesh->Footprint)Radius=FMath::Max(Radius,V.Size2D());
    for(const auto& R:Railings)if(R.Actor->FloorIndex==N.FloorIndex)
    {
     FVector P=N.LocalTransform.GetLocation(),A=B->GetActorTransform().InverseTransformPosition(R.Actor->GetActorTransform().TransformPosition(R.Start)),Z=B->GetActorTransform().InverseTransformPosition(R.Actor->GetActorTransform().TransformPosition(R.End));P.Z=A.Z=Z.Z=0;
     if(FMath::PointDistToSegment(P,A,Z)<=Radius+FMath::Max(R.Actor->PostWidth,R.Actor->RailThickness)*.707107){Status=TEXT("CreatedJunctionIntersectsPreservedRailing");return false;}
    }
   }
  return true;
 }
 bool IsPreserved() const
 {
  for(const auto& R:Railings)if(!R.IsPreserved(true))return false;
  for(const auto& P:Openings)
  {
   if(!IsValid(P.Actor)||P.Actor->IsActorBeingDestroyed()||!IsValid(P.Wall)||P.Wall->IsActorBeingDestroyed()
    ||P.Actor->OwningWallGuid!=P.Wall->ElementGuid||!P.Actor->GetActorTransform().Equals(P.Pose,0.001)
    ||!P.Wall->GetElementLocalTransform().Equals(P.WallPose,1.e-5)||!P.Wall->LocalStart.Equals(P.Start,1.e-5)||!P.Wall->LocalEnd.Equals(P.End,1.e-5))return false;
   const auto* C=P.Wall->DoorWindowConnections.FindByPredicate([&](const auto& V){return V.DoorWindowGuid==P.Actor->ElementGuid;});
   const auto* Cut=P.Wall->CutOperations.FindByPredicate([&](const auto& V){return V.OperationGuid==P.Cut.OperationGuid;});
   const auto* R=P.Wall->OwningBuilding->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==P.Relation.RelationGuid;});
   if(!C||!Cut||!R||!FEHBWallDoorWindowConnection::StaticStruct()->CompareScriptStruct(C,&P.Connection,0)
    ||!FEHBCutOperation::StaticStruct()->CompareScriptStruct(Cut,&P.Cut,0))return false;
   auto Expected=P.Relation;
   if(R->SourceGeometryRevision!=Expected.SourceGeometryRevision||R->TargetGeometryRevision!=Expected.TargetGeometryRevision)
   {
    if(R->SourceGeometryRevision!=P.Wall->OwningBuilding->GetElementGeometryRevision(R->Source.ElementGuid)
     ||R->TargetGeometryRevision!=P.Wall->OwningBuilding->GetElementGeometryRevision(R->Target.ElementGuid))return false;
    Expected.SourceGeometryRevision=R->SourceGeometryRevision;Expected.TargetGeometryRevision=R->TargetGeometryRevision;
   }
   if(!FEHBElementRelation::StaticStruct()->CompareScriptStruct(R,&Expected,0))return false;
  }
  return true;
 }
};
