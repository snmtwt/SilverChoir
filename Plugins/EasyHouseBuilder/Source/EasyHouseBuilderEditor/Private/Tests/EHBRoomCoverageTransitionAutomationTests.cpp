#if WITH_DEV_AUTOMATION_TESTS
#include "Core/EHBRoomCoverageTransition.h"
#include "Core/EHBWallNodeDefinitions.h"
#include "Misc/AutomationTest.h"
#include "JsonObjectConverter.h"
#include "Algo/Reverse.h"
#include <limits>

namespace
{
 FEHBWallNodeModel Grid(const TArray<double>& X,const TArray<double>& Y,bool Vertical,bool Horizontal,int32 Floor=1,double Z=0)
 {
  FEHBWallNodeModel M;M.Version=1;
  for(int32 J=0;J<Y.Num();++J)for(int32 I=0;I<X.Num();++I)
  {
   auto& N=M.Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid::NewDeterministicGuid(FString::Printf(TEXT("RoomTransitionNode.%d.%d.%d"),Floor,I,J));
   N.FloorIndex=Floor;N.JunctionDimensions={20,20,300};N.LocalTransform=FTransform(FVector(X[I],Y[J],Z));
  }
  auto Edge=[&](int32 A,int32 B)
  {
   auto& W=M.Walls.AddDefaulted_GetRef();W.WallGuid=FGuid::NewDeterministicGuid(FString::Printf(TEXT("RoomTransitionWall.%d.%d.%d"),Floor,A,B));
   W.StartNodeGuid=M.Nodes[A].NodeGuid;W.EndNodeGuid=M.Nodes[B].NodeGuid;
  };
  for(int32 J=0;J<Y.Num();++J)if(J==0||J==Y.Num()-1||Horizontal)for(int32 I=0;I<X.Num()-1;++I)Edge(J*X.Num()+I,J*X.Num()+I+1);
  for(int32 I=0;I<X.Num();++I)if(I==0||I==X.Num()-1||Vertical)for(int32 J=0;J<Y.Num()-1;++J)Edge(J*X.Num()+I,(J+1)*X.Num()+I);
  return M;
 }
 FString Snapshot(const FEHBWallNodeModel& M){FString S;FJsonObjectConverter::UStructToJsonObjectString(M,S);return S;}
 FString Snapshot(const FEHBRoomCoverageTransition& P)
 {
  FString S;for(const auto& C:P.GetChanges())
  {
   S+=FString::Printf(TEXT("%d/%d:"),static_cast<int32>(C.Kind),C.FloorIndex);
   for(FGuid Id:C.BeforeRooms)S+=Id.ToString()+TEXT(",");S+=TEXT("->");
   for(FGuid Id:C.AfterRooms)S+=Id.ToString()+TEXT(",");S+=TEXT(";");
  }return S;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomCoverageTransitionTest,"EHB.Topology.RoomCoverageTransition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomCoverageTransitionTest::RunTest(const FString& Parameters)
{
 const FGuid Building=FGuid::NewGuid();FName Status;
 const auto Single=Grid({0,100,400},{0,300},false,false),Two=Grid({0,100,400},{0,300},true,false);
 auto Check=[&](const FEHBWallNodeModel& Before,const FEHBWallNodeModel& After,EEHBRoomCoverageChange Kind,int32 Old,int32 New)
 {
  const auto Source=Snapshot(Before),Target=Snapshot(After);const auto P=FEHBRoomCoverageTransition::Build(Building,Before,After,Status);
  if(!TestTrue(*FString::Printf(TEXT("Coverage transition %d: %s"),static_cast<int32>(Kind),*Status.ToString()),P.IsReady()))return P;
  TestEqual(TEXT("Single correspondence"),P.GetChanges().Num(),1);if(P.GetChanges().Num()==1)
  {
   const auto& C=P.GetChanges()[0];TestTrue(TEXT("Actual transition kind"),C.Kind==Kind);TestEqual(TEXT("Before room count"),C.BeforeRooms.Num(),Old);TestEqual(TEXT("After room count"),C.AfterRooms.Num(),New);
  }
  TestEqual(TEXT("Planning preserves before values"),Snapshot(Before),Source);TestEqual(TEXT("Planning preserves after values"),Snapshot(After),Target);
  auto A=Before,B=After;Algo::Reverse(A.Nodes);Algo::Reverse(A.Walls);Algo::Reverse(B.Nodes);Algo::Reverse(B.Walls);
  for(auto& W:B.Walls)Swap(W.StartNodeGuid,W.EndNodeGuid);
  const auto Reordered=FEHBRoomCoverageTransition::Build(Building,A,B,Status);TestTrue(TEXT("Reordered inputs still valid"),Reordered.IsReady());TestEqual(TEXT("Node/wall order and traversal do not change correspondence"),Snapshot(Reordered),Snapshot(P));
  return P;
 };
 const auto Same=Check(Single,Single,EEHBRoomCoverageChange::Retained,1,1);
 if(!Same.IsReady())return false;
 TestEqual(TEXT("Retained room keeps actual queried identity"),Same.GetChanges()[0].BeforeRooms,Same.GetChanges()[0].AfterRooms);
 const auto Split=Check(Single,Two,EEHBRoomCoverageChange::Split,1,2);
 const auto Merge=Check(Two,Single,EEHBRoomCoverageChange::Merged,2,1);
 if(!Split.IsReady()||!Merge.IsReady())return false;
 TestEqual(TEXT("Split and reverse merge use same preferred lineage"),Split.GetChanges()[0].AfterRooms,Merge.GetChanges()[0].BeforeRooms);
 TArray<FEHBNodeRoomBoundary> Rooms;FEHBWallNodeRooms::Build(Building,Two,Rooms,Status);
 const auto* Largest=Rooms.FindByPredicate([](const auto& R){return R.Area>60000;});
 TestTrue(TEXT("Largest source/child is preferred, never GUID order"),Largest&&Merge.GetChanges()[0].BeforeRooms[0]==Largest->RoomGuid);
 const auto Triple=Grid({0,100,200,400},{0,300},true,false),Whole=Grid({0,100,200,400},{0,300},false,false);
 Check(Whole,Triple,EEHBRoomCoverageChange::Split,1,3);Check(Triple,Whole,EEHBRoomCoverageChange::Merged,3,1);
 auto Open=Single;Open.Walls.RemoveAt(0);
 Check(Single,Open,EEHBRoomCoverageChange::Removed,1,0);Check(Open,Single,EEHBRoomCoverageChange::Added,0,1);
 FEHBWallNodeModel Empty;Empty.Version=1;const auto EmptyPlan=FEHBRoomCoverageTransition::Build(Building,Empty,Empty,Status);
 TestTrue(TEXT("Empty model has an explicit valid empty plan"),EmptyPlan.IsReady()&&EmptyPlan.GetChanges().IsEmpty());

 // Split at arbitrary direction with the same external boundary.
 auto Diagonal=Single;auto& Cut=Diagonal.Walls.AddDefaulted_GetRef();Cut.WallGuid=FGuid::NewGuid();Cut.StartNodeGuid=Single.Nodes[0].NodeGuid;Cut.EndNodeGuid=Single.Nodes.Last().NodeGuid;
 Check(Single,Diagonal,EEHBRoomCoverageChange::Split,1,2);Check(Diagonal,Single,EEHBRoomCoverageChange::Merged,2,1);
 // Concave L with three children, sharing real model nodes and edges.
 auto Cells=Grid({0,200,400},{0,200,400},true,true);
 const auto Corner=Cells.Nodes.Last().NodeGuid;
 Cells.Walls.RemoveAll([&](const auto& W){return W.StartNodeGuid==Corner||W.EndNodeGuid==Corner;});
 auto LShape=Cells;
 const FGuid Center=Cells.Nodes[4].NodeGuid,Bottom=Cells.Nodes[1].NodeGuid,Left=Cells.Nodes[3].NodeGuid;
 LShape.Walls.RemoveAll([&](const auto& W){return (W.StartNodeGuid==Center&&(W.EndNodeGuid==Bottom||W.EndNodeGuid==Left))||(W.EndNodeGuid==Center&&(W.StartNodeGuid==Bottom||W.StartNodeGuid==Left));});
 Check(LShape,Cells,EEHBRoomCoverageChange::Split,1,3);Check(Cells,LShape,EEHBRoomCoverageChange::Merged,3,1);

 // Identical XY on another floor must never become a parent of this merge.
 auto MultiBefore=Two,MultiAfter=Single;const auto Upper=Grid({0,100,400},{0,300},true,false,2,300);
 MultiBefore.Nodes.Append(Upper.Nodes);MultiBefore.Walls.Append(Upper.Walls);MultiAfter.Nodes.Append(Upper.Nodes);MultiAfter.Walls.Append(Upper.Walls);
 const auto Multi=FEHBRoomCoverageTransition::Build(Building,MultiBefore,MultiAfter,Status);
 TestTrue(TEXT("Multi-floor query valid"),Multi.IsReady());TestEqual(TEXT("One merge and two independent upper rooms"),Multi.GetChanges().Num(),3);
 int32 Preserved=0,Merged=0;for(const auto& C:Multi.GetChanges()){if(C.Kind==EEHBRoomCoverageChange::Merged){++Merged;TestEqual(TEXT("Only lower floor merged"),C.FloorIndex,1);}else if(C.Kind==EEHBRoomCoverageChange::Retained){++Preserved;TestEqual(TEXT("Upper identities preserved"),C.BeforeRooms,C.AfterRooms);}}
 TestEqual(TEXT("One lower merge"),Merged,1);TestEqual(TEXT("Two preserved upper rooms"),Preserved,2);

 // Distant room removal is a separate correspondence, not a false merge by area.
 auto DetachedBefore=Single,DetachedAfter=Single,Detached=Grid({0,400},{0,300},false,false,3,0);
 for(auto& N:Detached.Nodes){N.FloorIndex=1;N.LocalTransform.AddToTranslation(FVector(2000,0,0));}
 DetachedBefore.Nodes.Append(Detached.Nodes);DetachedBefore.Walls.Append(Detached.Walls);
 DetachedAfter.Nodes.Append(Detached.Nodes);Detached.Walls.RemoveAt(0);DetachedAfter.Walls.Append(Detached.Walls);
 const auto Selective=FEHBRoomCoverageTransition::Build(Building,DetachedBefore,DetachedAfter,Status);
 TestTrue(TEXT("Independent opening is classified"),Selective.IsReady());TestEqual(TEXT("Retained room and removed room stay separate"),Selective.GetChanges().Num(),2);
 TestTrue(TEXT("Independent preserved coverage"),Selective.GetChanges().ContainsByPredicate([](const auto& C){return C.Kind==EEHBRoomCoverageChange::Retained;}));
 TestTrue(TEXT("Independent removed coverage"),Selective.GetChanges().ContainsByPredicate([](const auto& C){return C.Kind==EEHBRoomCoverageChange::Removed;}));

 for(int32 Binding=0;Binding<3;++Binding)
 {
  auto A=Two,B=Single;
  for(int32 I=0;I<A.Nodes.Num();++I)if(Binding==2||(Binding==1&&I%2==0))
  {const FEHBWallNodePillarBinding P={A.Nodes[I].NodeGuid,FGuid::NewGuid()};A.PillarBindings.Add(P);B.PillarBindings.Add(P);}
  Check(A,B,EEHBRoomCoverageChange::Merged,2,1);
 }

 // Equal-area ranking is independent of random prospective IDs.
 auto Equal=Grid({0,200,400},{0,300},true,false),EqualWhole=Grid({0,200,400},{0,300},false,false);
 for(int32 Round=0;Round<4;++Round)
 {
  const auto P=Check(Equal,EqualWhole,EEHBRoomCoverageChange::Merged,2,1);if(!P.IsReady())return false;
  FEHBWallNodeRooms::Build(Building,Equal,Rooms,Status);const auto* Chosen=Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==P.GetChanges()[0].BeforeRooms[0];});
  TestTrue(TEXT("Equal rooms choose the same geometric side"),Chosen&&!Chosen->Polygon.ContainsByPredicate([](FVector V){return V.X>200;}));
  TMap<FGuid,FGuid> Ids;for(auto& N:Equal.Nodes){auto Old=N.NodeGuid;N.NodeGuid=FGuid::NewGuid();Ids.Add(Old,N.NodeGuid);}for(auto& N:EqualWhole.Nodes)N.NodeGuid=Ids[N.NodeGuid];
  for(auto* M:{&Equal,&EqualWhole})for(auto& W:M->Walls){W.StartNodeGuid=Ids[W.StartNodeGuid];W.EndNodeGuid=Ids[W.EndNodeGuid];W.WallGuid=FGuid::NewGuid();}
 }
 auto Reject=[&](const FEHBWallNodeModel& A,const FEHBWallNodeModel& Z,const TCHAR* Reason)
 {
  auto P=Split;P=FEHBRoomCoverageTransition::Build(Building,A,Z,Status);TestFalse(Reason,P.IsReady());TestTrue(TEXT("Failure clears previous correspondence"),P.GetChanges().IsEmpty());TestEqual(TEXT("Exact refusal reason"),Status,FName(Reason));
 };
 auto Shrink=Single;for(auto& N:Shrink.Nodes)if(N.LocalTransform.GetLocation().X==400)N.LocalTransform.AddToTranslation(FVector(-25,0,0));Reject(Single,Shrink,TEXT("IncompleteRoomTransitionCoverage"));
 auto Grow=Single;for(auto& N:Grow.Nodes)if(N.LocalTransform.GetLocation().X==400)N.LocalTransform.AddToTranslation(FVector(25,0,0));Reject(Single,Grow,TEXT("IncompleteRoomTransitionCoverage"));
 auto Relocate=Single;for(auto& N:Relocate.Nodes)N.LocalTransform.AddToTranslation(FVector(1000,0,0));Reject(Single,Relocate,TEXT("RoomIdentityLeftCoverage"));
 auto SameArea=Single;for(auto& N:SameArea.Nodes)N.LocalTransform.AddToTranslation(FVector(10,0,0));Reject(Single,SameArea,TEXT("IncompleteRoomTransitionCoverage"));
 auto OtherPlane=Single;for(auto& N:OtherPlane.Nodes)N.LocalTransform.AddToTranslation(FVector(0,0,300));Reject(Single,OtherPlane,TEXT("RoomIdentityLeftCoverage"));
 const auto Vertical=Grid({0,200,400},{0,150,300},true,false),Horizontal=Grid({0,200,400},{0,150,300},false,true);Reject(Vertical,Horizontal,TEXT("RoomRepartitionRequiresPlan"));
 auto Partial=Two;Partial.Walls.RemoveAt(0);Reject(Single,Partial,TEXT("IncompleteRoomTransitionCoverage"));
 auto Bad=Single;Bad.Nodes[0].LocalTransform.SetTranslation(FVector(std::numeric_limits<double>::quiet_NaN(),0,0));
 const auto Invalid=FEHBRoomCoverageTransition::Build(Building,Single,Bad,Status);TestFalse(TEXT("Invalid model is rejected before coverage"),Invalid.IsReady());TestTrue(TEXT("Invalid model has no partial result"),Invalid.GetChanges().IsEmpty());
 const auto InvalidBuilding=FEHBRoomCoverageTransition::Build({},Single,Two,Status);TestFalse(TEXT("Missing building identity rejected"),InvalidBuilding.IsReady());
 return true;
}
#endif
