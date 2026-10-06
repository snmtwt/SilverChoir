#include "Core/EHBWallPathPlanning.h"
#include "Core/EHBBuildingActorBase.h"
#include "Actors/EHB_Pillar.h"

FEHBWallPathPlan FEHBWallPathPlanning::Build(const TArray<FEHBWallPathPoint>& Existing,
 const TArray<FEHBWallPathPoint>& Endpoints, bool bClosed, float Thickness)
{
 FEHBWallPathPlan Result;
 auto Fail = [&](FName Reason) { Result = {}; Result.Status = Reason; return Result; };
 if (Endpoints.Num() < (bClosed ? 3 : 2) || Endpoints.Num() > 2048) return Fail(TEXT("InvalidPath"));
 if (!FMath::IsFinite(Thickness) || Thickness < 1) return Fail(TEXT("InvalidDimensions"));
 Result.Points = Existing;
 Result.Points.Sort([](const auto& A, const auto& B) { if(A.ExistingNodeGuid.IsValid()!=B.ExistingNodeGuid.IsValid())return A.ExistingNodeGuid.IsValid();const auto X=A.ExistingNodeGuid.IsValid()?A.ExistingNodeGuid:A.ExistingPillarGuid,Y=B.ExistingNodeGuid.IsValid()?B.ExistingNodeGuid:B.ExistingPillarGuid; return X.A!=Y.A?X.A<Y.A:X.B!=Y.B?X.B<Y.B:X.C!=Y.C?X.C<Y.C:X.D<Y.D; });
 TMap<FGuid, int32> ExistingIndices,NodeIndices;
 for (int32 I = 0; I < Result.Points.Num(); ++I)
 {
  auto& Point = Result.Points[I];
  if ((!Point.ExistingPillarGuid.IsValid()&&!Point.ExistingNodeGuid.IsValid())
   || (Point.ExistingPillarGuid.IsValid()&&ExistingIndices.Contains(Point.ExistingPillarGuid))
   || (Point.ExistingNodeGuid.IsValid()&&(NodeIndices.Contains(Point.ExistingNodeGuid)||Point.NodeRevision<0))
   || Point.LocalPosition.ContainsNaN() || Point.FloorIndex < 1) return Fail(TEXT("InvalidExistingPillar"));
  Point.EndpointIndex = INDEX_NONE;
  if(Point.ExistingPillarGuid.IsValid())ExistingIndices.Add(Point.ExistingPillarGuid, I);
  if(Point.ExistingNodeGuid.IsValid())NodeIndices.Add(Point.ExistingNodeGuid,I);
 }
 TArray<int32> Resolved; Resolved.Init(INDEX_NONE, Endpoints.Num());
 for (const auto& Endpoint : Endpoints)
  if (Endpoint.LocalPosition.ContainsNaN() || Endpoint.FloorIndex < 1) return Fail(TEXT("InvalidEndpoint"));
 auto Resolve = [&](int32 Index) -> int32
 {
  if (Resolved[Index] != INDEX_NONE) return Resolved[Index];
  const auto& Endpoint = Endpoints[Index];
  if(Endpoint.ExistingNodeGuid.IsValid())
  {
   const auto* Found=NodeIndices.Find(Endpoint.ExistingNodeGuid);if(!Found)return INDEX_NONE;
   const auto& Current=Result.Points[*Found];
   if(Current.NodeRevision!=Endpoint.NodeRevision||Current.FloorIndex!=Endpoint.FloorIndex||!Current.LocalPosition.Equals(Endpoint.LocalPosition,0.001)
    ||(Endpoint.ExistingPillarGuid.IsValid()&&Current.ExistingPillarGuid!=Endpoint.ExistingPillarGuid))return INDEX_NONE;
   Resolved[Index]=*Found;
  }
  else if (Endpoint.ExistingPillarGuid.IsValid())
  {
   const auto* Found = ExistingIndices.Find(Endpoint.ExistingPillarGuid);
   if (!Found) return INDEX_NONE;
   Resolved[Index] = *Found;
  }
  else
  {
   auto Point = Endpoint; Point.EndpointIndex = Index;
   Resolved[Index] = Result.Points.Add(Point);
  }
  return Resolved[Index];
 };
 const int32 EdgeCount = bClosed ? Endpoints.Num() : Endpoints.Num() - 1;
 for (int32 Edge = 0; Edge < EdgeCount; ++Edge)
 {
  const int32 Start = Resolve(Edge), End = Resolve((Edge + 1) % Endpoints.Num());
  if (Start == INDEX_NONE || End == INDEX_NONE) return Fail(TEXT("StaleEndpoint"));
  const auto A = Result.Points[Start], B = Result.Points[End];
  const float Length = FVector::Dist2D(A.LocalPosition, B.LocalPosition);
  if (Length <= 10) return Fail(TEXT("CollapsedEdge"));
  if (A.FloorIndex != B.FloorIndex || !FMath::IsNearlyEqual(A.LocalPosition.Z, B.LocalPosition.Z, 0.01)) return Fail(TEXT("EndpointLevelMismatch"));
  const FVector Direction = (B.LocalPosition - A.LocalPosition).GetSafeNormal2D();
  TArray<TPair<int32, float>> Chain; Chain.Emplace(Start, 0); Chain.Emplace(End, Length);
  for (int32 I = 0; I < Result.Points.Num(); ++I)
  {
   const auto& Point = Result.Points[I];
   if (I == Start || I == End || Point.FloorIndex != A.FloorIndex
    || !FMath::IsNearlyEqual(Point.LocalPosition.Z, A.LocalPosition.Z, 0.01)) continue;
   const FVector Delta = Point.LocalPosition - A.LocalPosition;
   const float Distance = FVector::DotProduct(FVector(Delta.X, Delta.Y, 0), Direction);
   if (Distance <= 10 || Distance >= Length - 10) continue;
   if (FVector::Dist2D(A.LocalPosition + Direction * Distance, Point.LocalPosition) > FMath::Max(5.0f, Thickness * 0.75f)) continue;
   Chain.Emplace(I, Distance);
  }
  Chain.Sort([](const auto& A, const auto& B) { return A.Value == B.Value ? A.Key < B.Key : A.Value < B.Value; });
  for (int32 I = 0; I + 1 < Chain.Num(); ++I)
  {
   if (FVector::DistSquared2D(Result.Points[Chain[I].Key].LocalPosition, Result.Points[Chain[I+1].Key].LocalPosition) <= 1)
    return Fail(TEXT("CoincidentChainPillars"));
   Result.Segments.Emplace(Chain[I].Key, Chain[I+1].Key); Result.PathEdges.Add(Edge);
  }
 }
 Result.bSucceeded = true; Result.Status = TEXT("Ready"); return Result;
}

FEHBWallPathPlan FEHBWallPathPlanning::BuildForBuilding(const AEHBBuildingActorBase* Building,
 const TArray<FEHBWallCreationEndpoint>& Endpoints, bool bClosed, const FEHBWallCreationOptions& Options)
{
 auto Fail = [](FName Reason) { FEHBWallPathPlan Result; Result.Status = Reason; return Result; };
 if (!IsValid(Building) || Building->IsActorBeingDestroyed()) return Fail(TEXT("InvalidBuilding"));
 TArray<FEHBWallPathPoint> Existing, Requests;
 if(Building->WallNodeAuthority.Version==2)
 {
  if(!UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty())return Fail(TEXT("IncoherentTopology"));
  for(const auto& N:Building->WallNodeAuthority.Nodes)
  {auto& P=Existing.AddDefaulted_GetRef();P.ExistingNodeGuid=N.NodeGuid;P.NodeRevision=N.GeometryRevision;P.ExistingPillarGuid=Building->FindPhysicalPillarForNode(N.NodeGuid);P.LocalPosition=N.LocalTransform.GetLocation();P.FloorIndex=N.FloorIndex;}
 }
 else for (const auto* Element : Building->QueryElements(FEHBElementQuery()))
  if (const auto* Pillar = Cast<AEHB_Pillar>(Element); Pillar && !Pillar->IsActorBeingDestroyed())
  {
   auto& Point = Existing.AddDefaulted_GetRef(); Point.ExistingPillarGuid = Pillar->ElementGuid;
   Point.LocalPosition = Pillar->GetElementLocalTransform().GetLocation(); Point.FloorIndex = Pillar->FloorIndex;
  }
 for (const auto& Endpoint : Endpoints)
 {
  if (Endpoint.Wall) return Fail(TEXT("RequiresWallSplitPlan"));
  auto& Point = Requests.AddDefaulted_GetRef();
  if(Endpoint.NodeGuid.IsValid())
  {
   if(Building->WallNodeAuthority.Version!=2)return Fail(TEXT("LogicalEndpointRequiresNodeAuthority"));
   const auto* Found=Existing.FindByPredicate([&](const auto& P){return P.ExistingNodeGuid==Endpoint.NodeGuid;});
   if(!Found||Endpoint.ExpectedNodeRevision!=Found->NodeRevision)return Fail(TEXT("StaleNodeEndpoint"));
   if(Endpoint.FloorIndex!=Found->FloorIndex||Endpoint.LocalLocation.ContainsNaN()||Endpoint.WorldLocation.ContainsNaN()
    ||!Found->LocalPosition.Equals(Endpoint.LocalLocation,0.001)||!Building->GetActorTransform().InverseTransformPosition(Endpoint.WorldLocation).Equals(Found->LocalPosition,0.001))return Fail(TEXT("StaleNodeEndpointPosition"));
   if(Endpoint.Pillar&&(!IsValid(Endpoint.Pillar)||Endpoint.Pillar->IsActorBeingDestroyed()||Endpoint.Pillar->OwningBuilding!=Building||Endpoint.Pillar->ElementGuid!=Found->ExistingPillarGuid))return Fail(TEXT("NodeEndpointBindingMismatch"));
   Point=*Found;
  }
  else if (Endpoint.Pillar)
  {
   if (!IsValid(Endpoint.Pillar) || Endpoint.Pillar->IsActorBeingDestroyed() || Endpoint.Pillar->OwningBuilding != Building) return Fail(TEXT("StaleEndpoint"));
   Point.ExistingPillarGuid = Endpoint.Pillar->ElementGuid;
   Point.LocalPosition = Endpoint.Pillar->GetElementLocalTransform().GetLocation(); Point.FloorIndex = Endpoint.Pillar->FloorIndex;
   if(Building->WallNodeAuthority.Version==2)
   {const auto* Found=Existing.FindByPredicate([&](const auto& P){return P.ExistingPillarGuid==Point.ExistingPillarGuid;});if(!Found)return Fail(TEXT("StaleEndpoint"));Point=*Found;}
  }
  else
  {
   Point.LocalPosition = Building->GetActorTransform().InverseTransformPosition(Endpoint.WorldLocation);
   if (Point.LocalPosition.ContainsNaN() || !Point.LocalPosition.Equals(Endpoint.LocalLocation, 0.001)) return Fail(TEXT("EndpointFrameMismatch"));
   if (Options.bSnapToIntegerBuildingCoordinates) Point.LocalPosition = Building->RoundBuildingLocalCoordinates(Point.LocalPosition);
   Point.FloorIndex = Endpoint.FloorIndex;
  }
 }
 return Build(Existing, Requests, bClosed, Options.WallThickness);
}

FEHBWallSplitIntervalPlan FEHBWallSplitIntervalPlanning::Build(const TArray<FEHBWallSplitIntervalRequest>& Requests,
 const TArray<FEHBWallSplitOpeningInterval>& Openings)
{
 FEHBWallSplitIntervalPlan Result;auto Fail=[&](FName Why){Result={};Result.Status=Why;return Result;};
 if(Requests.IsEmpty()||Requests.Num()>2048)return Fail(TEXT("InvalidSplitRequests"));
 TMap<FGuid,int32> Sources;TArray<int32> Order;
 for(int32 I=0;I<Requests.Num();++I)
 {
  const auto& R=Requests[I];
  if(!R.SourceGuid.IsValid()||R.LocalStart.ContainsNaN()||R.LocalEnd.ContainsNaN()
   ||!FMath::IsFinite(R.Distance)||!FMath::IsFinite(R.ColumnWidth)||R.ColumnWidth<1
   ||R.LocalStart.Z!=R.LocalEnd.Z)return Fail(TEXT("InvalidSplitSource"));
  const float Length=FVector::Dist2D(R.LocalStart,R.LocalEnd),Margin=FMath::Max(10.f,R.ColumnWidth*.5f+1.f);
  if(!FMath::IsFinite(Length))return Fail(TEXT("InvalidSplitSource"));
  if(R.Distance<=Margin||R.Distance>=Length-Margin)return Fail(TEXT("TooCloseToEndpoint"));
  if(const int32* Previous=Sources.Find(R.SourceGuid))
  {
   const auto& P=Requests[*Previous];
   if(P.LocalStart!=R.LocalStart||P.LocalEnd!=R.LocalEnd||P.ColumnWidth!=R.ColumnWidth)return Fail(TEXT("InconsistentSplitSource"));
  }
  else Sources.Add(R.SourceGuid,I);
  Order.Add(I);
 }
 TSet<FGuid> SeenOpenings;
 for(const auto& O:Openings)
 {
  const auto* I=Sources.Find(O.SourceGuid);
  if(!I||!O.OpeningGuid.IsValid()||SeenOpenings.Contains(O.OpeningGuid)||!FMath::IsFinite(O.Distance)||!FMath::IsFinite(O.Width)||O.Width<=0)return Fail(TEXT("InvalidSplitOpening"));
  const auto& S=Requests[*I];const float Length=FVector::Dist2D(S.LocalStart,S.LocalEnd);
  if(O.Distance-O.Width*.5f<0||O.Distance+O.Width*.5f>Length)return Fail(TEXT("OpeningOutsideWall"));
  SeenOpenings.Add(O.OpeningGuid);
 }
 Order.Sort([&](int32 A,int32 B){return Requests[A].SourceGuid==Requests[B].SourceGuid?Requests[A].Distance>Requests[B].Distance:Requests[A].SourceGuid<Requests[B].SourceGuid;});
 TMap<FGuid,float> Remaining;
 for(int32 I:Order)
 {
  const auto& R=Requests[I];const float Length=FVector::Dist2D(R.LocalStart,R.LocalEnd),Half=R.ColumnWidth*.5f;
  const float End=Remaining.Contains(R.SourceGuid)?Remaining[R.SourceGuid]:Length;
  if(R.Distance>=End-FMath::Max(10.f,Half+1.f))return Fail(TEXT("OverlappingSplitPillars"));
  const FVector Direction=(R.LocalEnd-R.LocalStart).GetSafeNormal2D();
  const FVector Position=FMath::Lerp(R.LocalStart,R.LocalEnd,R.Distance/Length);
  auto& Step=Result.Steps.AddDefaulted_GetRef();Step.RequestIndex=I;Step.EffectiveSourceEnd=R.LocalStart+Direction*End;
  for(const auto& O:Openings)
  {
   if(O.SourceGuid!=R.SourceGuid)continue;
   const float Min=O.Distance-O.Width*.5f,Max=O.Distance+O.Width*.5f;
   if(Min>End)continue; // Already moved to a final right-hand interval by an earlier step.
   if(Max>End)return Fail(TEXT("OpeningCrossesSplitInterval"));
   if(!(Max<R.Distance-Half||Min>R.Distance+Half))return Fail(TEXT("OpeningIntersectsPillar"));
   auto& Route=Step.Openings.AddDefaulted_GetRef();Route.OpeningGuid=O.OpeningGuid;Route.bAfter=Min>R.Distance+Half;
   Route.NewDistance=O.Distance-(Route.bAfter?R.Distance+Half:0.f);
   Route.TargetStart=Route.bAfter?Position+Direction*Half:R.LocalStart;
   Route.TargetEnd=Route.bAfter?Step.EffectiveSourceEnd:Position-Direction*Half;
  }
  Step.Openings.Sort([](const auto& A,const auto& B){return A.OpeningGuid<B.OpeningGuid;});
  Remaining.Add(R.SourceGuid,R.Distance-Half);
 }
 Result.bSucceeded=true;Result.Status=TEXT("Ready");return Result;
}
