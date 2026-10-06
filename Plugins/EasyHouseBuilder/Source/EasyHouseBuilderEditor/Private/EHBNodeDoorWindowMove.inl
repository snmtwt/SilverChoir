bool EHBRoomFinishMove::FNodeEditPlan::PrepareHostedElements(AEHBBuildingActorBase* B,const TArray<AEHBElementActorBase*>& Elements,FName& Status)
{
 for(auto* E:Elements)if(auto* D=Cast<AEHB_DoorWindow>(E))
 {
  auto Fail=[&](){Status=TEXT("StaleNodeDoorWindowBinding");return false;};
  auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(D->OwningWallGuid));if(!W||D->GetClass()!=AEHB_DoorWindow::StaticClass()||D->GetAttachParentActor()!=B)return Fail();
  const auto Connections=W->DoorWindowConnections.FilterByPredicate([&](const auto& C){return C.DoorWindowGuid==D->ElementGuid;});if(Connections.Num()!=1)return Fail();const auto& C=Connections[0];
  const auto Data=D->MakeOpeningData();const auto Relative=D->GetActorTransform().GetRelativeTransform(W->GetActorTransform());
  if(C.Kind!=Data.Kind||!FMath::IsNearlyEqual(C.DistanceFromStart,D->DistanceFromWallStart,0.001f)||!FMath::IsNearlyEqual(C.BottomHeight,D->GetOpeningBottomHeight(),0.001f)
   ||!FMath::IsNearlyEqual(C.OpeningWidth,Data.Width,0.001f)||!FMath::IsNearlyEqual(C.OpeningHeight,Data.Height,0.001f)||!C.DoorWindowLocalToWall.Equals(Relative,0.001)||C.LocalOutlinePoints!=Data.LocalOutlinePoints)return Fail();
  const auto Relations=B->ElementRelations.FilterByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::HostedElement&&R.Target.ElementGuid==D->ElementGuid;});
  if(Relations.Num()!=1)return Fail();const auto& R=Relations[0];const auto* Distance=R.NumericMetadata.Find(TEXT("DistanceFromWallStart"));
  if(!R.bEnabled||R.Source.Kind!=EEHBRelationEndpointKind::BuildingElement||R.Source.ElementGuid!=W->ElementGuid||R.Source.SurfaceKind!=EEHBElementSurfaceKind::Opening
   ||R.Target.Kind!=EEHBRelationEndpointKind::BuildingElement||!Distance||FMath::Abs(*Distance-C.DistanceFromStart)>0.001||!R.TargetRelativeToSource.Equals(Relative,0.001))return Fail();
  const auto* Side=CandidateSides.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==W->ElementGuid;});if(!Side)return Fail();
  const double Length=FVector::Distance(Side->LocalStart,Side->LocalEnd);if(C.DistanceFromStart<0||C.DistanceFromStart>Length){Status=TEXT("NodeDoorWindowOutsideWall");return false;}
  const auto Frame=Side->LocalTransform*B->GetActorTransform();auto& P=Hosted.AddDefaulted_GetRef();P.Actor=D;P.Wall=W->ElementGuid;P.Relation=R.RelationGuid;P.Distance=C.DistanceFromStart;
  P.World=FTransform(Frame.GetRotation(),Frame.TransformPosition(FVector(-Length*.5+C.DistanceFromStart,0,D->GetOpeningBottomHeight())),D->GetActorScale3D());HostedRelationIds.Add(R.RelationGuid);
 }
 for(auto* E:Elements)if(auto* W=Cast<AEHB_Wall>(E))for(const auto& C:W->DoorWindowConnections)if(!Hosted.ContainsByPredicate([&](const auto& P){return P.Wall==W->ElementGuid&&P.Actor->ElementGuid==C.DoorWindowGuid;})){Status=TEXT("MissingNodeDoorWindowActor");return false;}
 return true;
}

bool EHBRoomFinishMove::FNodeEditPlan::VerifyHostedElements(AEHBBuildingActorBase* B,FName& Status) const
{
 for(const auto& P:Hosted)
 {
  auto* W=B->FindElementActorByGuid(P.Wall);const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==P.Relation;});
  if(!IsValid(P.Actor)||!W||P.Actor->OwningWallGuid!=P.Wall||!P.Actor->GetActorTransform().Equals(P.World,0.001)||!FMath::IsNearlyEqual(P.Actor->DistanceFromWallStart,P.Distance,0.001f)
   ||!R||!R->TargetRelativeToSource.Equals(P.Actor->GetActorTransform().GetRelativeTransform(W->GetActorTransform()),0.001))
  {Status=TEXT("NodeDoorWindowPublicationMismatch");return false;}
 }
 return true;
}
