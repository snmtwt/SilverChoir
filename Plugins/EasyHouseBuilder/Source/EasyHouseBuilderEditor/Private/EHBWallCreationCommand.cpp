#include "EHBWallCreationCommand.h"
#include "EHBRoomSubdivision.h"
#include "EHBPreservedCreationHosts.h"
#include "EngineUtils.h"
#include "Core/EHBWallPathPlanning.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Level.h"
#include "ScopedTransaction.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/EHBWallSplitTestHooks.h"
int32 EHBWallCreationCommand::FailAfterSplit = 0;
int32 EHBWallCreationCommand::FailAfterEdge = 0;
bool EHBWallCreationCommand::bFailAfterRecord = false;
#endif

namespace
{
FName ValidateAndPlan(const AEHBBuildingActorBase* Building,
 const TArray<FEHBWallCreationEndpoint>& Endpoints, bool bClosed, const FEHBWallCreationOptions& Options,FEHBWallPathPlan& Route)
{
 if (!IsValid(Building) || Building->IsActorBeingDestroyed() || !Building->GetWorld()) return TEXT("InvalidBuilding");
 if (!Options.bCreatePhysicalColumns && Building->WallNodeAuthority.Version!=2) return TEXT("RequiresOptionalNodeAuthority");
 if (Building->IsChangeNotificationBusy()) return TEXT("BuildingChangePublicationBusy");
 if (Endpoints.Num() < (bClosed ? 3 : 2)) return TEXT("InvalidPath");
 for (float Value : {Options.WallHeight, Options.WallThickness, Options.PillarHeight, Options.PillarWidth, Options.PillarDepth})
  if (!FMath::IsFinite(Value) || Value < 1) return TEXT("InvalidDimensions");
 if (Options.FreePillarLocalRotation.ContainsNaN()) return TEXT("InvalidRotation");
 TArray<FVector> Positions;
 for (const auto& Endpoint : Endpoints)
 {
  if (Endpoint.Wall) return TEXT("RequiresWallSplitPlan");
  if (Endpoint.LocalLocation.ContainsNaN() || Endpoint.WorldLocation.ContainsNaN() || Endpoint.FloorIndex < 1) return TEXT("InvalidEndpoint");
  if (Endpoint.Pillar && (!IsValid(Endpoint.Pillar) || Endpoint.Pillar->IsActorBeingDestroyed()
   || Endpoint.Pillar->OwningBuilding != Building || !Endpoint.Pillar->ElementGuid.IsValid())) return TEXT("StaleEndpoint");
  // World position is authoritative for free endpoints; never silently disagree with preview.
  FVector Position = Endpoint.Pillar ? Endpoint.Pillar->GetElementLocalTransform().GetLocation()
   : Building->GetActorTransform().InverseTransformPosition(Endpoint.WorldLocation);
  if (!Endpoint.Pillar && !Position.Equals(Endpoint.LocalLocation, 0.001)) return TEXT("EndpointFrameMismatch");
  if (!Endpoint.Pillar && !Endpoint.NodeGuid.IsValid() && Options.bSnapToIntegerBuildingCoordinates) Position = Building->RoundBuildingLocalCoordinates(Position);
  Positions.Add(Position);
 }
 const int32 Edges = bClosed ? Endpoints.Num() : Endpoints.Num() - 1;
 for (int32 Index = 0; Index < Edges; ++Index)
 {
  const int32 Next = (Index + 1) % Endpoints.Num();
  if (FVector::Dist2D(Positions[Index], Positions[Next]) <= 10) return TEXT("CollapsedEdge");
 }
 if (!UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty()) return TEXT("IncoherentTopology");
 if (Building->LastCommittedEdit.Sequence == MAX_int64) return TEXT("EditSequenceExhausted");
 Route=FEHBWallPathPlanning::BuildForBuilding(Building, Endpoints, bClosed, Options);return Route.Status;
}
}

FName EHBWallCreationCommand::Validate(const AEHBBuildingActorBase* Building,
 const TArray<FEHBWallCreationEndpoint>& Endpoints,bool bClosed,const FEHBWallCreationOptions& Options)
{
 FEHBWallPathPlan Route;return ValidateAndPlan(Building,Endpoints,bClosed,Options,Route);
}

FEHBWallPathResult EHBWallCreationCommand::Commit(AEHBBuildingActorBase* Building,
 const TArray<FEHBWallCreationEndpoint>& Endpoints, bool bClosed, const FEHBWallCreationOptions& Options, bool bPreviewOnly,FEHBWallPathPreview* Preview)
{
 if(Preview)*Preview={};
 FEHBWallPathResult Result;
 FEHBWallPathPlan Route;
 Result.Status = ValidateAndPlan(Building, Endpoints, bClosed, Options,Route);
 if (Result.Status != FName(TEXT("Ready"))) return Result;
 if (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive() || Building->GetWorld()->WorldType!=EWorldType::Editor)
 { Result.Status = TEXT("RequiresIndependentEditorTransaction"); return Result; }
 auto* TransBuffer=Cast<UTransBuffer>(GEditor->Trans);
 if(!TransBuffer){Result.Status=TEXT("RequiresEditorTransactionBuffer");return Result;}
 if (!Route.bSucceeded) { Result.Status = Route.Status; return Result; }
 const auto BeforeGraph = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
 TArray<FGuid> Nodes, Rooms;
 // Conservative scope until the creation dependency planner can narrow room changes.
 for (const auto& Node : BeforeGraph.Nodes) Nodes.AddUnique(Node.NodeGuid);
 for (const auto& Node : BeforeGraph.Nodes)
  for (const auto& Room : Building->GetClosedLoopsByFloor(Node.FloorIndex)) Rooms.AddUnique(Room.LoopGuid);
 // Inspect actual ownership, including elements missing from a stale query cache.
 TArray<AEHBElementActorBase*> Elements;
 for(TActorIterator<AEHBElementActorBase> It(Building->GetWorld());It;++It)
  if(It->OwningBuilding==Building&&!It->IsActorBeingDestroyed())Elements.Add(*It);
 const bool UseNodes=Building->WallNodeAuthority.Version==2;
 const bool HasRoomPlan=UseNodes||Elements.ContainsByPredicate([](const auto* E){return E->IsA<AEHB_Floor>()||E->IsA<AEHB_FloorSlab>();});
 FEHBRoomSubdivision Subdivision;FEHBRoomSubdivision::FPath RoomPath;FEHBPreservedCreationHosts PreservedHosts;
 if(HasRoomPlan)
 {
  RoomPath.Route=Route;RoomPath.Endpoints=Endpoints;RoomPath.Options=Options;
  for(auto& E:RoomPath.Endpoints)
  {
   E.LocalLocation=E.Pillar?E.Pillar->GetElementLocalTransform().GetLocation():Building->GetActorTransform().InverseTransformPosition(E.WorldLocation);
   if(!E.Pillar&&!E.NodeGuid.IsValid()&&Options.bSnapToIntegerBuildingCoordinates)E.LocalLocation=Building->RoundBuildingLocalCoordinates(E.LocalLocation);
  }
  if(!PreservedHosts.Capture(Building,Elements,Result.Status)
   ||!Subdivision.PrepareMultiple(Building,{},Elements,Result.Status,&RoomPath,&PreservedHosts.OpeningWalls)
   ||!PreservedHosts.ValidateCandidate(Building,Subdivision.CandidateGeometry,Route,Options,Result.Status))return Result;
  for(const auto* E:Elements)
   if(!E->IsA<AEHB_Pillar>()&&!E->IsA<AEHB_Wall>()&&!Subdivision.Contains(E)&&!PreservedHosts.Contains(E))
   {Result.Status=TEXT("UnplannedRoomCreationElement");return Result;}
  // A ceiling supporting upper elements cannot simply shrink to a child room.
  for(const auto& R:Building->ElementRelations)
   if(!(R.Type==EEHBElementRelationType::TopologyConnection&&R.bEnabled)&&!Subdivision.FinishRelations.Contains(R.RelationGuid)&&!PreservedHosts.Relations.Contains(R.RelationGuid))
   {Result.Status=TEXT("UnplannedRoomCreationRelation");return Result;}
  RoomPath.ActualPoints.SetNum(Route.Points.Num());RoomPath.ActualWalls.SetNum(Route.Segments.Num());
 }
 if(bPreviewOnly){if(Preview&&HasRoomPlan)Subdivision.ExportPreview(*Preview);Result.bSucceeded=true;Result.Status=TEXT("Ready");return Result;}
 FEHBChangeNotificationBatch Notifications(*Building);
 if (!Notifications.IsActive()) { Result.Status = TEXT("BuildingChangePublicationBusy"); return Result; }
 auto Working = Endpoints;
 bool bApplied = true;
 bool NoChanges=false;FGuid TransactionId;FDelegateHandle FinalizedHandle;
 {
  FScopedTransaction Transaction(NSLOCTEXT("EHB", "CreateWallPath", "Create Building Wall Path"));
  TransactionId=TransBuffer->GetTransaction(TransBuffer->GetQueueLength()-1)->GetId();
  FinalizedHandle=TransBuffer->OnTransactionStateChanged().AddLambda([&](const FTransactionContext& Context,ETransactionStateEventType Event)
  {
   if(Context.TransactionId!=TransactionId||Event!=ETransactionStateEventType::TransactionFinalized)return;
   const int32 Index=TransBuffer->FindTransactionIndex(TransactionId);
   const auto* Finalized=Index==INDEX_NONE?nullptr:TransBuffer->GetTransaction(Index);
   // End() drops a finalized, ineffective editor transaction and restores the
   // previous undo/redo stack. It must never trigger an undo of that prior edit.
   NoChanges=Finalized&&Finalized->IsTransient();
  });
  Building->SetFlags(RF_Transactional);
  Building->Modify();
  Building->GetLevel()->Modify();
  for (auto* Element : Elements)
  {
   Element->SetFlags(RF_Transactional); Element->Modify();
   for (auto* Component : Element->GetComponents()) if (Component)
   { Component->SetFlags(RF_Transactional); Component->Modify(); }
  }
  const int32 Edges = bClosed ? Working.Num() : Working.Num() - 1;
  if(UseNodes)
  {
   bApplied=Subdivision.MaterializePath(Building,RoomPath,Working,Result.Walls,Result.FailureReason);
   if(bApplied&&!Result.Walls.IsEmpty())Result.PrimaryWall=Result.Walls[0];
  }
  else for (int32 Index = 0; Index < Edges; ++Index)
  {
   FEHBWallCreationResult Segment;
   bApplied = Building->CreateOrReuseWallSegment(Working[Index], Working[(Index + 1) % Working.Num()], Options, Segment);
   if (!bApplied) break;
   auto ResolvePointGuid = [&](int32 PointIndex)
   {
    const auto& Point = Route.Points[PointIndex];
    if (Point.ExistingPillarGuid.IsValid()) return Point.ExistingPillarGuid;
    return Working.IsValidIndex(Point.EndpointIndex) && Working[Point.EndpointIndex].Pillar
     ? Working[Point.EndpointIndex].Pillar->ElementGuid : FGuid();
   };
   int32 ExpectedSegments = 0;
   for (int32 Planned = 0; Planned < Route.Segments.Num(); ++Planned)
   {
    if (Route.PathEdges[Planned] != Index) continue;
    ++ExpectedSegments;
    const auto Pair = Route.Segments[Planned];
    const FGuid A = ResolvePointGuid(Pair.X), B = ResolvePointGuid(Pair.Y);
    const auto* Actual=Segment.Walls.FindByPredicate([&](const auto* Wall)
    {return (Wall->StartPillarGuid==A&&Wall->EndPillarGuid==B)||(Wall->StartPillarGuid==B&&Wall->EndPillarGuid==A);});
    bApplied &= A.IsValid()&&B.IsValid()&&Actual!=nullptr;
    if(HasRoomPlan&&Actual)RoomPath.ActualWalls[Planned]=(*Actual)->ElementGuid;
   }
   bApplied &= ExpectedSegments == Segment.Walls.Num();
   if (!bApplied) { Result.FailureReason = TEXT("CreatedPathDiffersFromPlan"); break; }
   for (auto* Wall : Segment.Walls) Result.Walls.AddUnique(Wall);
   if (!Result.PrimaryWall) Result.PrimaryWall = Segment.PrimaryWall;
#if WITH_DEV_AUTOMATION_TESTS
   if (FailAfterEdge == Index + 1) { FailAfterEdge = 0; bApplied = false; break; }
#endif
  }
  if (bApplied)
  {
   Building->RebuildClosedLoops();
   const auto AfterGraph = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
   bApplied = !Result.Walls.IsEmpty() && AfterGraph.Issues.IsEmpty();
   if(bApplied&&HasRoomPlan)
   {
    if(!UseNodes)for(int32 I=0;I<Route.Points.Num();++I)
    {
     const auto& P=Route.Points[I];RoomPath.ActualPoints[I]=P.ExistingPillarGuid;
     if(!P.ExistingPillarGuid.IsValid()&&Working.IsValidIndex(P.EndpointIndex)&&Working[P.EndpointIndex].Pillar)
      RoomPath.ActualPoints[I]=Working[P.EndpointIndex].Pillar->ElementGuid;
    }
    bApplied=Subdivision.Apply(Building,{},Result.FailureReason,&RoomPath);
    if(bApplied&&!PreservedHosts.IsPreserved()){bApplied=false;Result.FailureReason=TEXT("RetainedCreationHostChanged");}
   }
#if WITH_DEV_AUTOMATION_TESTS
   if(bApplied&&EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterGeometry))bApplied=false;
#endif
   if (bApplied)
   {
    // Keep an existing migration baseline coherent; do not silently initialize migration.
    if (Building->TopologyMigrationBaseline.Version > 0)
    { Building->TopologyMigrationBaseline.Nodes = AfterGraph.Nodes; Building->TopologyMigrationBaseline.Walls = AfterGraph.Walls; }
#if WITH_DEV_AUTOMATION_TESTS
    if(EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterBaseline))bApplied=false;
#endif
    for (const auto& Node : AfterGraph.Nodes)
    {
     Nodes.AddUnique(Node.NodeGuid);
     for (const auto& Room : Building->GetClosedLoopsByFloor(Node.FloorIndex)) Rooms.AddUnique(Room.LoopGuid);
    }
    if(bApplied) bApplied = Notifications.RecordCommittedEdit(bClosed ? TEXT("CreateClosedWallPath") : TEXT("CreateWallPath"), Nodes, Rooms,Subdivision.Displays.AuthoredElements);
#if WITH_DEV_AUTOMATION_TESTS
    if (bFailAfterRecord) { bFailAfterRecord = false; bApplied = false; }
    if(EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord))bApplied=false;
#endif
    Building->MarkPackageDirty();
   }
  }
 }
 TransBuffer->OnTransactionStateChanged().Remove(FinalizedHandle);
 if (!bApplied)
 {
  const bool bRestored = NoChanges || (TransBuffer->GetUndoContext().TransactionId==TransactionId&&GEditor->UndoTransaction(false));
  if (bRestored) Notifications.Rollback(); else Notifications.Publish();
  // Never return transaction-created pointers after recovery.
  Result.Walls.Reset(); Result.PrimaryWall = nullptr;
  Result.Status = bRestored ? TEXT("CreateFailedRolledBack") : TEXT("CreateFailedRollbackFailed");
  return Result;
 }
 Result.bSucceeded = true; Result.Status = TEXT("Committed");
 Result.Endpoints = MoveTemp(Working); Result.CommittedEdit = Building->LastCommittedEdit;
 Notifications.Publish();
 return Result;
}
