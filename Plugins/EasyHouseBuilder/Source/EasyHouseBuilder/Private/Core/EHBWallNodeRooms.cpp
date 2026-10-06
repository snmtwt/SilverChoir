// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBRoomIdentity.h"
#include "Core/EHBWallTopology.h"
#include "Algo/Sort.h"

namespace
{
	struct FEHBDirectedWallEdge
	{
		FGuid FromNodeGuid;
		FGuid ToNodeGuid;
		FGuid WallGuid;
		int32 ReverseEdgeIndex = INDEX_NONE;
		double Angle = 0.0;
	};

	double CalculateSignedArea2D(const TArray<FVector>& Points)
	{
		if (Points.Num() < 3)
		{
			return 0.0;
		}

		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector& A = Points[Index];
			const FVector& B = Points[(Index + 1) % Points.Num()];
			TwiceArea += static_cast<double>(A.X) * static_cast<double>(B.Y)
				- static_cast<double>(B.X) * static_cast<double>(A.Y);
		}

		return TwiceArea * 0.5;
	}

}

void FEHBWallNodeRooms::ExtractFaces(FGuid BuildingGuid,const TMap<FGuid,FVector>& NodeLocations,
	const TArray<FEHBRoomGraphEdge>& Connections,TArray<FEHBNodeRoomBoundary>& Rooms)
{
	Rooms.Reset();
	TMap<FGuid,int32> FloorsByWall;for(const auto& Edge:Connections)FloorsByWall.Add(Edge.WallGuid,Edge.FloorIndex);
	TArray<FEHBDirectedWallEdge> DirectedEdges;
	TMap<FGuid, TArray<int32>> OutgoingEdgesByNode;

	auto ResolveNodeLocation = [&NodeLocations](const FGuid& Id, FVector& Out) -> bool
	{
		const auto* Point=NodeLocations.Find(Id);if(!Point)return false;Out=*Point;return true;
	};

	for (const FEHBRoomGraphEdge& Connection : Connections)
	{
		if (!Connection.WallGuid.IsValid()
			|| !Connection.StartNodeGuid.IsValid()
			|| !Connection.EndNodeGuid.IsValid()
			|| Connection.StartNodeGuid == Connection.EndNodeGuid)
		{
			continue;
		}

		FVector StartLocation = FVector::ZeroVector;
		FVector EndLocation = FVector::ZeroVector;
		if (!ResolveNodeLocation(Connection.StartNodeGuid, StartLocation)
			|| !ResolveNodeLocation(Connection.EndNodeGuid, EndLocation))
		{
			continue;
		}

		const FVector Direction = (EndLocation - StartLocation).GetSafeNormal2D();
		if (Direction.IsNearlyZero())
		{
			continue;
		}

		const int32 ForwardIndex = DirectedEdges.Num();
		FEHBDirectedWallEdge& ForwardEdge = DirectedEdges.AddDefaulted_GetRef();
		ForwardEdge.FromNodeGuid = Connection.StartNodeGuid;
		ForwardEdge.ToNodeGuid = Connection.EndNodeGuid;
		ForwardEdge.WallGuid = Connection.WallGuid;
		ForwardEdge.Angle = FMath::Atan2(Direction.Y, Direction.X);

		const int32 ReverseIndex = DirectedEdges.Num();
		FEHBDirectedWallEdge& ReverseEdge = DirectedEdges.AddDefaulted_GetRef();
		ReverseEdge.FromNodeGuid = Connection.EndNodeGuid;
		ReverseEdge.ToNodeGuid = Connection.StartNodeGuid;
		ReverseEdge.WallGuid = Connection.WallGuid;
		ReverseEdge.Angle = FMath::Atan2(-Direction.Y, -Direction.X);

		DirectedEdges[ForwardIndex].ReverseEdgeIndex = ReverseIndex;
		DirectedEdges[ReverseIndex].ReverseEdgeIndex = ForwardIndex;
		OutgoingEdgesByNode.FindOrAdd(Connection.StartNodeGuid).Add(ForwardIndex);
		OutgoingEdgesByNode.FindOrAdd(Connection.EndNodeGuid).Add(ReverseIndex);
	}

	for (TPair<FGuid, TArray<int32>>& Pair : OutgoingEdgesByNode)
	{
		Algo::Sort(
			Pair.Value,
			[&DirectedEdges](int32 A, int32 B)
			{
				return DirectedEdges[A].Angle < DirectedEdges[B].Angle;
			});
	}

	TSet<int32> VisitedDirectedEdges;
	for (int32 StartEdgeIndex = 0; StartEdgeIndex < DirectedEdges.Num(); ++StartEdgeIndex)
	{
		if (VisitedDirectedEdges.Contains(StartEdgeIndex))
		{
			continue;
		}

		TArray<int32> FaceEdgeIndices;
		int32 CurrentEdgeIndex = StartEdgeIndex;
		bool bClosed = false;

		for (int32 StepCount = 0; StepCount < DirectedEdges.Num() + 1; ++StepCount)
		{
			if (CurrentEdgeIndex == INDEX_NONE || !DirectedEdges.IsValidIndex(CurrentEdgeIndex))
			{
				break;
			}

			if (CurrentEdgeIndex == StartEdgeIndex && FaceEdgeIndices.Num() > 0)
			{
				bClosed = true;
				break;
			}

			if (VisitedDirectedEdges.Contains(CurrentEdgeIndex))
			{
				break;
			}

			VisitedDirectedEdges.Add(CurrentEdgeIndex);
			FaceEdgeIndices.Add(CurrentEdgeIndex);

			const FEHBDirectedWallEdge& CurrentEdge = DirectedEdges[CurrentEdgeIndex];
			const TArray<int32>* OutgoingEdges = OutgoingEdgesByNode.Find(CurrentEdge.ToNodeGuid);
			if (!OutgoingEdges || OutgoingEdges->IsEmpty())
			{
				break;
			}

			const int32 ReverseIndex = CurrentEdge.ReverseEdgeIndex;
			const int32 ReversePosition = OutgoingEdges->Find(ReverseIndex);
			if (ReversePosition == INDEX_NONE)
			{
				break;
			}

			const int32 NextPosition = (ReversePosition - 1 + OutgoingEdges->Num()) % OutgoingEdges->Num();
			CurrentEdgeIndex = (*OutgoingEdges)[NextPosition];
		}

		if (!bClosed || FaceEdgeIndices.Num() < 3)
		{
			continue;
		}

		TArray<FGuid> LoopNodeGuids;
		TArray<FGuid> LoopWallGuids;
		TArray<FVector> LoopPoints;
		LoopNodeGuids.Reserve(FaceEdgeIndices.Num());
		LoopWallGuids.Reserve(FaceEdgeIndices.Num());
		LoopPoints.Reserve(FaceEdgeIndices.Num());

		for (int32 EdgeIndex : FaceEdgeIndices)
		{
			const FEHBDirectedWallEdge& Edge = DirectedEdges[EdgeIndex];
			FVector NodeLocation = FVector::ZeroVector;
			if (!ResolveNodeLocation(Edge.FromNodeGuid, NodeLocation))
			{
				LoopPoints.Reset();
				break;
			}

			LoopNodeGuids.Add(Edge.FromNodeGuid);
			LoopWallGuids.Add(Edge.WallGuid);
			LoopPoints.Add(NodeLocation);
		}

		if (LoopPoints.Num() < 3)
		{
			continue;
		}

		const double SignedArea = CalculateSignedArea2D(LoopPoints);
		if (SignedArea <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		FEHBNodeRoomBoundary& NewLoop = Rooms.AddDefaulted_GetRef();
		const FGuid FirstWall=LoopWallGuids[0];
		NewLoop.FloorIndex=FloorsByWall.FindRef(FirstWall);
		NewLoop.Polygon=LoopPoints;
		NewLoop.NodeGuids = MoveTemp(LoopNodeGuids);
		NewLoop.WallGuids = MoveTemp(LoopWallGuids);
		NewLoop.Area = FMath::Abs(SignedArea);
		NewLoop.RoomGuid = FEHBRoomIdentity::Make(BuildingGuid, NewLoop.FloorIndex, NewLoop.NodeGuids, LoopPoints);

	}
}

namespace
{
	bool RoomGuidLess(const FGuid& A,const FGuid& B)
	{
		if(A.A!=B.A)return A.A<B.A;if(A.B!=B.B)return A.B<B.B;if(A.C!=B.C)return A.C<B.C;return A.D<B.D;
	}
	bool RoomContains(const TArray<FVector>& Polygon,FVector Point,bool bIncludeBoundary)
	{
		bool bInside=false;
		for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
		{
			FVector A=Polygon[J],B=Polygon[I];A.Z=B.Z=Point.Z=0;
			if(FVector::DistSquared(Point,FMath::ClosestPointOnSegment(Point,A,B))<=0.000001)return bIncludeBoundary;
			if((A.Y>Point.Y)!=(B.Y>Point.Y) && Point.X<(B.X-A.X)*(Point.Y-A.Y)/(B.Y-A.Y)+A.X)bInside=!bInside;
		}
		return bInside;
	}
	double CrossXY(FVector A,FVector B){return A.X*B.Y-A.Y*B.X;}
}

bool FEHBWallNodeRooms::Build(FGuid BuildingGuid,const FEHBWallNodeModel& Model,
	TArray<FEHBNodeRoomBoundary>& Rooms,FName& Reason)
{
	Rooms.Reset();Reason=TEXT("InvalidBuildingIdentity");if(!BuildingGuid.IsValid())return false;
	const auto Issues=UEHBWallTopologyLibrary::ValidateWallNodeModel(Model);
	if(!Issues.IsEmpty()){Reason=Issues[0].Code;return false;}
	TMap<FGuid,FVector> Locations;TMap<FGuid,int32> Floors;
	for(const auto& Node:Model.Nodes){Locations.Add(Node.NodeGuid,Node.LocalTransform.GetLocation());Floors.Add(Node.NodeGuid,Node.FloorIndex);}
	TArray<FEHBRoomGraphEdge> Edges;
	for(const auto& Wall:Model.Walls)
	{
		if(Floors[Wall.StartNodeGuid]!=Floors[Wall.EndNodeGuid]){Reason=TEXT("CrossFloorConnection");return false;}
		auto& Edge=Edges.AddDefaulted_GetRef();Edge.WallGuid=Wall.WallGuid;Edge.StartNodeGuid=Wall.StartNodeGuid;Edge.EndNodeGuid=Wall.EndNodeGuid;Edge.FloorIndex=Floors[Wall.StartNodeGuid];
	}
	// Sweep sorted X bounds; pair checks stop at the end of each interval. Worst case is quadratic.
	Edges.Sort([&](const auto& A,const auto& B)
	{
		if(A.FloorIndex!=B.FloorIndex)return A.FloorIndex<B.FloorIndex;
		const double AX=FMath::Min(Locations[A.StartNodeGuid].X,Locations[A.EndNodeGuid].X);
		const double BX=FMath::Min(Locations[B.StartNodeGuid].X,Locations[B.EndNodeGuid].X);
		return AX!=BX?AX<BX:RoomGuidLess(A.WallGuid,B.WallGuid);
	});
	for(int32 I=0;I<Edges.Num();++I)
	{
		const auto& First=Edges[I];const FVector A=Locations[First.StartNodeGuid],B=Locations[First.EndNodeGuid];
		for(int32 J=I+1;J<Edges.Num();++J)
		{
			const auto& Second=Edges[J];const FVector C=Locations[Second.StartNodeGuid],D=Locations[Second.EndNodeGuid];
			if(Second.FloorIndex!=First.FloorIndex || FMath::Min(C.X,D.X)>FMath::Max(A.X,B.X)+0.001)break;
			if(!FMath::IsNearlyEqual(A.Z,C.Z,0.001) || FMath::Min(C.Y,D.Y)>FMath::Max(A.Y,B.Y)+0.001 || FMath::Min(A.Y,B.Y)>FMath::Max(C.Y,D.Y)+0.001)continue;
			const double AB=B.X-A.X,AY=B.Y-A.Y,CD=D.X-C.X,CY=D.Y-C.Y;
			const double AC=CrossXY(B-A,C-A),AD=CrossXY(B-A,D-A),CA=CrossXY(D-C,A-C),CB=CrossXY(D-C,B-C);
			const double FirstTolerance=0.001*FMath::Sqrt(AB*AB+AY*AY),SecondTolerance=0.001*FMath::Sqrt(CD*CD+CY*CY);
			if(((AC>FirstTolerance&&AD< -FirstTolerance)||(AC< -FirstTolerance&&AD>FirstTolerance)) && ((CA>SecondTolerance&&CB< -SecondTolerance)||(CA< -SecondTolerance&&CB>SecondTolerance)))
			{Reason=TEXT("UnsplitWallIntersection");return false;}
			auto InvalidContact=[](FVector Point,FGuid PointId,FVector From,FGuid FromId,FVector To,FGuid ToId)
			{
				Point.Z=From.Z=To.Z=0;
				if(FVector::DistSquared(Point,FMath::ClosestPointOnSegment(Point,From,To))>0.000001)return false;
				return PointId!=FromId&&PointId!=ToId;
			};
			if(InvalidContact(A,First.StartNodeGuid,C,Second.StartNodeGuid,D,Second.EndNodeGuid)||InvalidContact(B,First.EndNodeGuid,C,Second.StartNodeGuid,D,Second.EndNodeGuid)
				||InvalidContact(C,Second.StartNodeGuid,A,First.StartNodeGuid,B,First.EndNodeGuid)||InvalidContact(D,Second.EndNodeGuid,A,First.StartNodeGuid,B,First.EndNodeGuid)
				||((First.StartNodeGuid==Second.StartNodeGuid&&First.EndNodeGuid==Second.EndNodeGuid)||(First.StartNodeGuid==Second.EndNodeGuid&&First.EndNodeGuid==Second.StartNodeGuid)))
			{Reason=TEXT("OverlappingOrUnsplitWallContact");return false;}
		}
	}
	// Bridges do not separate faces: a half wall or open corridor link must not put
	// repeated nodes into a room polygon. Iterative Tarjan avoids a deep C++ call stack.
	TArray<FGuid> NodeIds;Locations.GetKeys(NodeIds);TMap<FGuid,int32> NodeIndices;
	for(int32 I=0;I<NodeIds.Num();++I)NodeIndices.Add(NodeIds[I],I);
	TArray<TArray<int32>> Incident;Incident.SetNum(NodeIds.Num());
	for(int32 I=0;I<Edges.Num();++I){Incident[NodeIndices[Edges[I].StartNodeGuid]].Add(I);Incident[NodeIndices[Edges[I].EndNodeGuid]].Add(I);}
	TArray<int32> Discovery,Low;Discovery.Init(0,NodeIds.Num());Low.Init(0,NodeIds.Num());TSet<int32> Bridges;int32 Clock=0;
	struct FFrame{int32 Node,ParentEdge,Next;};TArray<FFrame> Stack;
	for(int32 Root=0;Root<NodeIds.Num();++Root)
	{
		if(Discovery[Root])continue;Discovery[Root]=Low[Root]=++Clock;Stack.Add({Root,INDEX_NONE,0});
		while(!Stack.IsEmpty())
		{
			auto& Frame=Stack.Last();const int32 Current=Frame.Node;
			if(Frame.Next==Incident[Current].Num())
			{
				const int32 ParentEdge=Frame.ParentEdge;Stack.Pop(EAllowShrinking::No);
				if(ParentEdge!=INDEX_NONE)
				{
					const auto& Edge=Edges[ParentEdge];const int32 Parent=NodeIndices[Edge.StartNodeGuid==NodeIds[Current]?Edge.EndNodeGuid:Edge.StartNodeGuid];
					if(Low[Current]>Discovery[Parent])Bridges.Add(ParentEdge);Low[Parent]=FMath::Min(Low[Parent],Low[Current]);
				}
				continue;
			}
			const int32 EdgeIndex=Incident[Current][Frame.Next++];if(EdgeIndex==Frame.ParentEdge)continue;
			const auto& Edge=Edges[EdgeIndex];const int32 Next=NodeIndices[Edge.StartNodeGuid==NodeIds[Current]?Edge.EndNodeGuid:Edge.StartNodeGuid];
			if(!Discovery[Next]){Discovery[Next]=Low[Next]=++Clock;Stack.Add({Next,EdgeIndex,0});}
			else Low[Current]=FMath::Min(Low[Current],Discovery[Next]);
		}
	}
	TArray<FEHBRoomGraphEdge> FaceEdges;FaceEdges.Reserve(Edges.Num()-Bridges.Num());for(int32 I=0;I<Edges.Num();++I)if(!Bridges.Contains(I))FaceEdges.Add(Edges[I]);
	TArray<FEHBNodeRoomBoundary> Candidate;ExtractFaces(BuildingGuid,Locations,FaceEdges,Candidate);
	for(auto& Room:Candidate)
	{
		TSet<FGuid> Seen;
		for(const FGuid Id:Room.NodeGuids){if(Seen.Contains(Id)){Reason=TEXT("NonSimpleRoomBoundary");return false;}Seen.Add(Id);}
		int32 First=0;for(int32 I=1;I<Room.NodeGuids.Num();++I)if(RoomGuidLess(Room.NodeGuids[I],Room.NodeGuids[First]))First=I;
		if(First!=0)
		{
			const auto Copy=Room;
			for(int32 I=0;I<Room.NodeGuids.Num();++I){const int32 Index=(I+First)%Room.NodeGuids.Num();Room.NodeGuids[I]=Copy.NodeGuids[Index];Room.WallGuids[I]=Copy.WallGuids[Index];Room.Polygon[I]=Copy.Polygon[Index];}
		}
	}
	for(int32 I=0;I<Candidate.Num();++I)for(int32 J=I+1;J<Candidate.Num();++J)
	{
		const auto& A=Candidate[I];const auto& B=Candidate[J];
		if(A.FloorIndex==B.FloorIndex&&FMath::IsNearlyEqual(A.Polygon[0].Z,B.Polygon[0].Z,0.001)
			&&(RoomContains(A.Polygon,B.Polygon[0],false)||RoomContains(B.Polygon,A.Polygon[0],false)))
		{Reason=TEXT("NestedRoomBoundariesRequireHoles");return false;}
	}
	Candidate.Sort([](const auto& A,const auto& B){return RoomGuidLess(A.RoomGuid,B.RoomGuid);});
	Rooms=MoveTemp(Candidate);Reason=TEXT("Ready");return true;
}

TArray<FGuid> FEHBWallNodeRooms::FindAtPoint(const TArray<FEHBNodeRoomBoundary>& Rooms,int32 FloorIndex,FVector LocalPoint)
{
	TArray<FGuid> Found;if(LocalPoint.ContainsNaN()||FloorIndex<1)return Found;
	for(const auto& Room:Rooms)if(Room.FloorIndex==FloorIndex&&RoomContains(Room.Polygon,LocalPoint,true))Found.AddUnique(Room.RoomGuid);
	Found.Sort(RoomGuidLess);return Found;
}
