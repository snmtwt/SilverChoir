#include "EHBWallRemovalCommand.h"
#include "PrimitiveDrawInterface.h"
#include "Core/EHBWallPathPlanning.h"
#include "EHBWallCreationCommand.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EHBRoomFinishMove.h"
#include "EHBRoomSubdivision.h"
#include "EHBPreservedCreationHosts.h"
#include "EHBNodeAuthorityEditing.h"
#include "EHBNodeEditingActivation.h"
#include "SEasyHouseBuilderPanel.h"
// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBWallNodeCopy.h"
#include "EHBBuildingCopy.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "LevelEditor.h"
#include "EHBCopyOutlinePolicy.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "EHBChangeNotificationTestObserver.h"
#include "Core/EHBActorImportScope.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Core/EHBWallJunctionGeometry.h"
#include "Core/EHBWallJunctionMesh.h"
#include <limits>
#include "Algo/Reverse.h"
#include "EHBPreservedRailing.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Floor.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "EasyHouseEditorMode.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Diagnostics/EHBBuildingPerformanceAnalyzer.h"
#include "EHB_Building.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/EHBPlanarSurfaceComponent.h"
#include "Tests/EHBWallSplitTestHooks.h"
#include "Tests/EHBNodeMoveTestHooks.h"
#include "Tests/EHBNodeOwnershipTestHooks.h"
#include "ScopedTransaction.h"
#include "Editor/Transactor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

namespace
{
	bool HasIssue(const TArray<FEHBWallTopologyIssue>& Issues, FName Code)
	{
		return Issues.ContainsByPredicate([Code](const FEHBWallTopologyIssue& Issue) { return Issue.Code == Code; });
	}

	FEHBWallTopologySnapshot MakeDetachedGraph()
	{
		FEHBWallTopologySnapshot Graph;
		Graph.bLegacyActorBacked = false;
		for (int32 Index = 1; Index <= 3; ++Index)
		{
			FEHBWallTopologyNode& Node = Graph.Nodes.AddDefaulted_GetRef();
			Node.NodeGuid = FGuid(0, 0, 0, Index);
			Node.LocalPosition = FVector(Index * 400, 0, 0);
			Node.FloorIndex = 1;
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FEHBWallTopologyEdge& Edge = Graph.Walls.AddDefaulted_GetRef();
			Edge.WallGuid = FGuid(1, 0, 0, Index + 1);
			Edge.StartNodeGuid = Graph.Nodes[Index].NodeGuid;
			Edge.EndNodeGuid = Graph.Nodes[Index + 1].NodeGuid;
		}
		return Graph;
	}

	struct FTransientTopologyFixture
	{
		TArray<AActor*> Actors;
		AEHB_Building* Building = nullptr;
		TArray<AEHB_Pillar*> Pillars;
		TArray<AEHB_Wall*> Walls;
		~FTransientTopologyFixture()
		{
			for (int32 Index = Actors.Num() - 1; Index >= 0; --Index)
				if (IsValid(Actors[Index]) && !Actors[Index]->IsActorBeingDestroyed()) Actors[Index]->Destroy();
		}
		bool Create(UWorld* World, EObjectFlags ObjectFlags = RF_Transient)
		{
			if (!World) return false;
			FActorSpawnParameters Params;
			Params.ObjectFlags = ObjectFlags;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Building = World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(), FVector(0, 0, 180000), FRotator::ZeroRotator, Params);
			if (!Building) return false;
			Actors.Add(Building);
			const TArray<FVector> Positions = { FVector(0,0,0), FVector(600,0,0), FVector(600,500,0), FVector(0,500,0) };
			for (const FVector& Position : Positions)
			{
				AEHB_Pillar* Pillar = World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(), Building->GetActorLocation() + Position, FRotator::ZeroRotator, Params);
				if (!Pillar) return false;
				Actors.Add(Pillar);
				Pillars.Add(Pillar);
				Pillar->AttachToBuilding(Building, FTransform(Position));
				Pillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
			}
			for (int32 Index = 0; Index < Pillars.Num(); ++Index)
			{
				AEHB_Wall* Wall = Building->ConnectPillars(Pillars[Index], Pillars[(Index + 1) % Pillars.Num()], 300, 20);
				if (!Wall) return false;
				if ((ObjectFlags & RF_Transient) != 0) Wall->SetFlags(RF_Transient);
				Actors.Add(Wall);
				Walls.Add(Wall);
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRequestedWallSidesTest,"EHB.Geometry.RequestedWallSides",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRequestedWallSidesTest::RunTest(const FString& Parameters)
{
	TArray<FEHBWallJunctionNodeInput> Nodes;TArray<FEHBWallJunctionWallInput> Walls;
	const TArray<FVector> Positions={FVector(0,0,0),FVector(600,0,0),FVector(-600,0,0),FVector(300,300,0),FVector(2000,0,0),FVector(2600,0,0)};
	for(int32 I=0;I<Positions.Num();++I){auto& N=Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid(0,0,0,I+1);N.LocalTransform=FTransform(FRotator(0,17,0),Positions[I]);N.Width=N.Depth=40;}
	auto AddWall=[&](int32 A,int32 B){auto& W=Walls.AddDefaulted_GetRef();W.WallGuid=FGuid(1,0,0,Walls.Num());W.StartNodeGuid=Nodes[A].NodeGuid;W.EndNodeGuid=Nodes[B].NodeGuid;};
	AddWall(0,1);AddWall(0,2);AddWall(0,3);AddWall(4,5);
	TSet<FGuid> Requested;Requested.Add(Walls[0].WallGuid);
	TArray<FEHBWallJunctionWallSides> All,Selected;FEHBWallJunctionSolveStats FullStats,Stats;FName Reason;
	auto EqualSides=[](const auto& A,const auto& B){return A.WallGuid==B.WallGuid&&A.LocalStart.Equals(B.LocalStart,0.001)&&A.LocalEnd.Equals(B.LocalEnd,0.001)&&A.LocalTransform.Equals(B.LocalTransform,0.001)&&A.StartLeft.Equals(B.StartLeft,0.001)&&A.EndLeft.Equals(B.EndLeft,0.001)&&A.StartRight.Equals(B.StartRight,0.001)&&A.EndRight.Equals(B.EndRight,0.001);};
	auto Compare=[&]()
	{
		if(!TestTrue(TEXT("Full candidate graph solves"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,All,Reason,nullptr,&FullStats)))return;
		if(!TestTrue(TEXT("Requested boundary solves"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Selected,Reason,&Requested,&Stats)))return;
		TestEqual(TEXT("Only requested wall is returned"),Selected.Num(),1);
		if(Selected.Num()==1){const auto* Expected=All.FindByPredicate([&](const auto& W){return W.WallGuid==Selected[0].WallGuid;});TestTrue(TEXT("All endpoint geometry matches full solve including unselected incident legs"),Expected&&EqualSides(*Expected,Selected[0]));}
		TestEqual(TEXT("Full graph solves six footprints"),FullStats.NodeFootprintsSolved,6);TestEqual(TEXT("Full graph solves four walls"),FullStats.WallSidesSolved,4);
		TestEqual(TEXT("Only two requested endpoint footprints solved"),Stats.NodeFootprintsSolved,2);TestEqual(TEXT("Only one wall side geometry solved"),Stats.WallSidesSolved,1);
	};
	Compare();
	if(Selected.Num()!=1)return false;
	const auto Before=Selected[0];
	Nodes[3].LocalTransform.SetLocation(FVector(450,230,0));Compare();
	if(Selected.Num()==1)TestFalse(TEXT("Moving unselected incident leg changes requested junction geometry"),EqualSides(Before,Selected[0]));
	Swap(Nodes[0],Nodes.Last());Swap(Walls[0],Walls.Last());Compare();
	TSet<FGuid> Empty;
	TestTrue(TEXT("Explicit empty request succeeds"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Selected,Reason,&Empty,&Stats));
	TestTrue(TEXT("Empty selection returns no walls"),Selected.IsEmpty());TestEqual(TEXT("Empty selection performs no footprint solve"),Stats.NodeFootprintsSolved,0);TestEqual(TEXT("Empty selection performs no side solve"),Stats.WallSidesSolved,0);
	Requested.Add(FGuid(99,99,99,99));Stats.NodeFootprintsSolved=999;
	TestFalse(TEXT("Unknown requested ID rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Selected,Reason,&Requested,&Stats));
	TestEqual(TEXT("Unknown ID reason"),Reason,FName(TEXT("UnknownRequestedWall")));TestTrue(TEXT("No partial output on failure"),Selected.IsEmpty());TestEqual(TEXT("Failure resets work evidence"),Stats.NodeFootprintsSolved,0);
	Requested.Remove(FGuid(99,99,99,99));
	Walls[0].EndNodeGuid=FGuid(55,0,0,0);
	TestFalse(TEXT("Unselected disconnected invalid edge is still validated"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Selected,Reason,&Requested,&Stats));
	TestTrue(TEXT("Invalid graph exposes no selected geometry"),Selected.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCandidateWallSidesTest,"EHB.Geometry.CandidateWallSides",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCandidateWallSidesTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	TArray<FEHBWallJunctionNodeInput> Nodes;TArray<FEHBWallJunctionWallInput> Walls;TArray<FGuid> Roots;
	for(auto* P:Fixture.Pillars){auto& N=Nodes.AddDefaulted_GetRef();N.NodeGuid=P->ElementGuid;N.LocalTransform=P->GetElementLocalTransform();N.Width=P->Width;N.Depth=P->Depth;Roots.Add(P->ElementGuid);}
	for(auto* W:Fixture.Walls){auto& E=Walls.AddDefaulted_GetRef();E.WallGuid=W->ElementGuid;E.StartNodeGuid=W->StartPillarGuid;E.EndNodeGuid=W->EndPillarGuid;E.Thickness=W->Thickness;E.Height=W->Height;}
	auto Actual=[&]()
	{
		FString Value;
		for(auto* W:Fixture.Walls)
		{
			Value+=W->LocalStart.ToString()+W->LocalEnd.ToString()+W->GetElementLocalTransform().ToString();
			for(bool Left:{true,false}){TArray<FVector> Points;W->BuildSideTopPolylineInBuildingSpace(Left,Points);for(auto& P:Points)Value+=P.ToString();}
		}
		return Value;
	};
	TArray<FEHBWallJunctionWallSides> Result;FName Reason;
	auto CompareLive=[&]()
	{
		for(auto* W:Fixture.Walls)
		{
			const auto* Solved=Result.FindByPredicate([&](const auto& E){return E.WallGuid==W->ElementGuid;});
			if(!TestNotNull(TEXT("All wall IDs retained"),Solved))continue;
			for(bool Left:{true,false})
			{
				TArray<FVector> Points;if(!TestTrue(TEXT("Live wall side available"),W->BuildSideTopPolylineInBuildingSpace(Left,Points))||Points.Num()!=2)continue;
				TestTrue(TEXT("Candidate start matches generated wall-side endpoint"),Points[0].Equals(Left?Solved->StartLeft:Solved->StartRight,0.001));
				TestTrue(TEXT("Candidate end matches generated wall-side endpoint"),Points[1].Equals(Left?Solved->EndLeft:Solved->EndRight,0.001));
			}
		}
	};
	const auto Original=Actual();const int32 UndoCount=GEditor->Trans->GetQueueLength();
	TestTrue(TEXT("Initial value graph solves"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Result,Reason));CompareLive();
	Nodes[0].LocalTransform.SetLocation(FVector(-70,-40,0));Nodes[1].LocalTransform.SetLocation(FVector(650,-90,0));
	TestTrue(TEXT("Angled candidate graph solves before actors move"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Result,Reason));
	TestEqual(TEXT("Candidate solve does not mutate live geometry"),Actual(),Original);TestEqual(TEXT("Candidate solve creates no transaction"),GEditor->Trans->GetQueueLength(),UndoCount);
	for(int32 I=0;I<Nodes.Num();++I)Fixture.Pillars[I]->SetActorRelativeTransform(Nodes[I].LocalTransform);
	Building->RefreshWallsConnectedToPillars(Roots);
	CompareLive();
	const auto Ordered=Result;
	for(int32 I=0;I<Nodes.Num()/2;++I)Swap(Nodes[I],Nodes[Nodes.Num()-1-I]);
	for(int32 I=0;I<Walls.Num()/2;++I)Swap(Walls[I],Walls[Walls.Num()-1-I]);
	TestTrue(TEXT("Reordered candidate graph solves"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Result,Reason));
	for(int32 I=0;I<Result.Num();++I){TestEqual(TEXT("Output canonical wall order"),Result[I].WallGuid,Ordered[I].WallGuid);TestTrue(TEXT("Input order leaves endpoints unchanged"),Result[I].StartLeft.Equals(Ordered[I].StartLeft,0.001)&&Result[I].EndRight.Equals(Ordered[I].EndRight,0.001));}
	auto BadWalls=Walls;BadWalls[0].EndNodeGuid=FGuid::NewGuid();
	TestFalse(TEXT("Missing endpoint rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,BadWalls,Result,Reason));TestTrue(TEXT("Invalid graph exposes no partial walls"),Result.IsEmpty());
	BadWalls=Walls;BadWalls.Add(Walls[0]);TestFalse(TEXT("Duplicate wall ID rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,BadWalls,Result,Reason));
	BadWalls.Last().WallGuid=FGuid::NewGuid();TestFalse(TEXT("Duplicate outgoing direction rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,BadWalls,Result,Reason));
	auto BadNodes=Nodes;BadNodes[0].LocalTransform.SetScale3D(FVector(2));TestFalse(TEXT("Scaled node rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(BadNodes,Walls,Result,Reason));
	BadNodes=Nodes;BadNodes[0].LocalTransform.AddToTranslation(FVector(0,0,20));TestFalse(TEXT("Cross-height straight wall rejected"),FEHBWallJunctionGeometry::BuildStraightWallSides(BadNodes,Walls,Result,Reason));
	TestTrue(TEXT("Final rejected result empty"),Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSharedCeilingPlanTest,"EHB.Topology.SharedCeilingPlan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSharedCeilingPlanTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))return false;
	auto* Building=Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	TArray<AEHB_Pillar*> Added;
	for(const FVector P:{FVector(1000,0,0),FVector(1000,500,0)})
	{
		auto* Pillar=Building->GetWorld()->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Building->GetActorTransform().TransformPosition(P),FRotator::ZeroRotator,Params);
		if(!Pillar)return false;
		Fixture.Actors.Add(Pillar);Added.Add(Pillar);
		Pillar->AttachToBuilding(Building,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	}
	const TArray<AEHB_Pillar*> Path={Fixture.Pillars[1],Added[0],Added[1],Fixture.Pillars[2]};
	for(int32 I=0;I<3;++I)
	{
		auto* Wall=Building->ConnectPillars(Path[I],Path[I+1],300,20);
		if(!Wall)return false;
		Wall->SetFlags(RF_Transient);Fixture.Actors.Add(Wall);
	}
	const auto Rooms=Building->GetClosedLoopsByFloor(1);
	if(!TestEqual(TEXT("Two adjacent rooms"),Rooms.Num(),2))return false;
	TArray<AEHB_FloorSlab*> Ceilings;
	for(const auto& Room:Rooms)
	{
		auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Floor)return false;Fixture.Actors.Add(Floor);
		if(!Floor->ConfigureFromRoomLoop(Building,Room,0,true))return false;
	}
	for(float X:{300.0f,800.0f})
	{
		auto* Slab=Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Slab)return false;Fixture.Actors.Add(Slab);Ceilings.Add(Slab);
		Slab->ConfigureDefaultSlab(Building,FTransform(FRotator(0,13,0),FVector(X,250,300)),100,20,false);
		Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
		if(!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))return false;
	}
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	if(!UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded)return false;
	TGuardValue<int32> ReuseGuard(EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,0);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))PreviousSelection.Add(Actor);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	auto Snapshot=[&]()
	{
		FString Value,Json;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString()+Element->GetElementLocalTransform().ToString();
			if(auto* Floor=Cast<AEHB_Floor>(Element)){for(const auto& R:Floor->FloorRegions){FJsonObjectConverter::UStructToJsonObjectString(R,Json);Value+=Json;}Value+=Floor->RoomLoopGuid.ToString()+(Floor->IsRecordedOutlineUnchanged()?TEXT("current"):TEXT("modified"));}
			if(auto* Slab=Cast<AEHB_FloorSlab>(Element)){for(const auto& P:Slab->LocalTopPolygon)Value+=P.ToString();Value+=Slab->RoomFillLoopGuid.ToString()+(Slab->IsRecordedOutlineUnchanged()?TEXT("current"):TEXT("modified"));}
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
			{
				for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();
				for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
			}
		}
		return Value;
	};
	auto Move=[&](bool Preview){return UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Building,Fixture.Pillars[1]->ElementGuid,FVector(600,0,0),FVector(650,-40,0),Preview);};
	const FString Original=Snapshot();const int32 Queue=GEditor->Trans->GetQueueLength();
	TGuardValue<int32> CountGuard(EHBNodeMoveTestHooks::CandidateJunctionSolveCount,0);
	TGuardValue<int32> BatchGuard(EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	const auto Preview=Move(true);TestEqual(TEXT("Shared preview consumes one persisted definition copy"),EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,1);TestTrue(*Preview.Message,Preview.bSucceeded);
	TestTrue(TEXT("Both ceiling dependencies planned"),Preview.Message.Contains(TEXT("FollowingSlabs=2")));
	TestEqual(TEXT("Two ceilings share one candidate graph solve"),EHBNodeMoveTestHooks::CandidateJunctionSolveCount,1);
	TestEqual(TEXT("Shared preview leaves geometry unchanged"),Snapshot(),Original);TestEqual(TEXT("Shared preview leaves undo unchanged"),GEditor->Trans->GetQueueLength(),Queue);
	EHBNodeMoveTestHooks::CandidateJunctionSolveCount=0;EHBNodeMoveTestHooks::PreparedDefinitionReuseCount=0;
	const auto Applied=Move(false);TestEqual(TEXT("Shared commit revalidates and consumes definitions once"),EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,1);TestTrue(*Applied.Message,Applied.bSucceeded);
	TestEqual(TEXT("Commit independently revalidates with one solve"),EHBNodeMoveTestHooks::CandidateJunctionSolveCount,1);
	TestEqual(TEXT("Commit uses one actual definition batch generation solve"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,1);
	for(auto* Slab:Ceilings)TestTrue(TEXT("Both ceiling provenance records current"),Slab->IsRecordedOutlineUnchanged());
	if(Applied.bSucceeded)
	{
		const auto Changed=Snapshot();TestTrue(TEXT("Undo shared floor ceiling change"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores both room geometries"),Snapshot(),Original);
		TestTrue(TEXT("Redo shared floor ceiling change"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores both room geometries"),Snapshot(),Changed);GEditor->UndoTransaction(false);
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> FailGuard(EHBNodeMoveTestHooks::FailurePhase,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs);
		const auto Failed=Move(false);TestEqual(TEXT("Shared plan failure uses production rollback"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));TestEqual(TEXT("Shared plan rollback restores both rooms"),Snapshot(),Original);TestFalse(TEXT("Shared failed edit cannot redo"),GEditor->Trans->CanRedo());
	}
	GEditor->SelectNone(false,true,false);for(const auto& Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBIndependentJunctionTest,"EHB.Geometry.ActorIndependentWallJunction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBIndependentJunctionTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Polygon;
	TestTrue(TEXT("No actor required for unconnected rectangle"),FEHBWallJunctionGeometry::BuildFootprint(20,30,{},Polygon));
	TestEqual(TEXT("Fallback has four vertices"),Polygon.Num(),4);
	for(const FVector P:{FVector(-10,-15,0),FVector(10,-15,0),FVector(10,15,0),FVector(-10,15,0)})TestTrue(TEXT("Centimeter rectangle bounds"),Polygon.Contains(P));
	FVector Left,Right;
	TestTrue(TEXT("Value-only face query"),FEHBWallJunctionGeometry::ResolveFace(20,30,Polygon,FVector::ForwardVector,20,Left,Right));
	TestTrue(TEXT("Face on rectangle side with retained thickness clamp"),Left.Equals(FVector(10,-9.8,0),0.001)&&Right.Equals(FVector(10,9.8,0),0.001));
	const TArray<TArray<float>> Cases={{0},{0,90},{0,180},{0,90,180},{0,90,180,270},{0,5,180}};
	for(const auto& Angles:Cases)
	{
		TArray<FEHBWallJunctionLeg> Legs;
		for(float Angle:Angles){auto& Leg=Legs.AddDefaulted_GetRef();Leg.Direction=FVector::ForwardVector.RotateAngleAxis(Angle,FVector::UpVector).GetSafeNormal2D();Leg.WallThickness=20;}
		TestTrue(TEXT("End corner straight T cross and acute junctions solve"),FEHBWallJunctionGeometry::BuildFootprint(20,20,Legs,Polygon));
		const auto Original=Polygon;
		TestTrue(TEXT("Footprint remains finite and planar"),Polygon.Num()>=3&&!Polygon.ContainsByPredicate([](const auto& P){return P.ContainsNaN()||P.Z!=0;}));
		for(const auto& Leg:Legs)
		{
			TestTrue(TEXT("Each wall receives a face"),FEHBWallJunctionGeometry::ResolveFace(20,20,Polygon,Leg.Direction,Leg.WallThickness,Left,Right));
			TestTrue(TEXT("Balanced connection face stays finite and nonzero"),!Left.ContainsNaN()&&!Right.ContainsNaN()&&FVector::DistSquared2D(Left,Right)>0.01);
		}
		for(int32 I=0;I<Legs.Num()/2;++I)Swap(Legs[I],Legs[Legs.Num()-1-I]);
		TestTrue(TEXT("Reordered legs solve"),FEHBWallJunctionGeometry::BuildFootprint(20,20,Legs,Polygon));
		TestTrue(TEXT("Unique direction input order leaves identical footprint"),Polygon==Original);
		for(auto& Leg:Legs)Leg.Direction=Leg.Direction.RotateAngleAxis(37,FVector::UpVector).GetSafeNormal2D();
		TestTrue(TEXT("Rotated junction solves"),FEHBWallJunctionGeometry::BuildFootprint(20,20,Legs,Polygon));
		TestEqual(TEXT("Rotation preserves vertex count"),Polygon.Num(),Original.Num());
		for(const auto& P:Original)TestTrue(TEXT("Pure junction rotation is covariant"),Polygon.ContainsByPredicate([&](const auto& Q){return Q.Equals(P.RotateAngleAxis(37,FVector::UpVector),0.001);}));
	}
 // The 0/360 seam must not cyclically rotate T junction vertices after transform roundoff.
 TArray<FEHBWallJunctionLeg> SeamLegs={{FVector(1,0,0),20},{FVector(0,1,0),20},{FVector(-1,0,0),20}};
 TArray<FVector> SeamOriginal;TestTrue(TEXT("Axis seam reference solves"),FEHBWallJunctionGeometry::BuildFootprint(40,40,SeamLegs,SeamOriginal));
 for(double Noise:{-1.e-13,1.e-13})
 {
  SeamLegs[0].Direction=FVector(1,Noise,0).GetSafeNormal2D();
  TestTrue(TEXT("Transform-noise seam solves"),FEHBWallJunctionGeometry::BuildFootprint(40,40,SeamLegs,Polygon));
  TestEqual(TEXT("Seam preserves footprint count"),Polygon.Num(),SeamOriginal.Num());
  for(int32 I=0;I<FMath::Min(Polygon.Num(),SeamOriginal.Num());++I)TestTrue(TEXT("Seam preserves ordered vertices"),Polygon[I].Equals(SeamOriginal[I],1.e-9));
 }
	FEHBWallJunctionLeg Invalid;Invalid.Direction=FVector(2,0,0);
	TestFalse(TEXT("Non-unit direction rejected"),FEHBWallJunctionGeometry::BuildFootprint(20,20,{Invalid},Polygon));
	TestTrue(TEXT("Rejected footprint does not expose partial output"),Polygon.IsEmpty());
	Invalid.Direction=FVector(1,0,1);TestFalse(TEXT("Nonplanar leg rejected"),FEHBWallJunctionGeometry::BuildFootprint(20,20,{Invalid},Polygon));
	TestFalse(TEXT("Nonfinite dimension rejected"),FEHBWallJunctionGeometry::BuildFootprint(std::numeric_limits<float>::quiet_NaN(),20,{},Polygon));
	TestFalse(TEXT("Incomplete face polygon rejected"),FEHBWallJunctionGeometry::ResolveFace(20,20,{},FVector::ForwardVector,20,Left,Right));
	TestTrue(TEXT("Rejected face output reset"),Left.IsZero()&&Right.IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyDetachedGraphTest, "EHB.Topology.DetachedNodesAndIncidentWalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyDetachedGraphTest::RunTest(const FString& Parameters)
{
	const FEHBWallTopologySnapshot Graph = MakeDetachedGraph();
	TestEqual(TEXT("No Actor or pillar binding required"), UEHBWallTopologyLibrary::ValidateWallTopology(Graph).Num(), 0);
	TestEqual(TEXT("Middle node has two incident walls"), UEHBWallTopologyLibrary::GetIncidentWallGuids(Graph, Graph.Nodes[1].NodeGuid).Num(), 2);
	TestEqual(TEXT("End node does not include remote wall"), UEHBWallTopologyLibrary::GetIncidentWallGuids(Graph, Graph.Nodes[0].NodeGuid).Num(), 1);
	TestEqual(TEXT("Unknown node has no incident walls"), UEHBWallTopologyLibrary::GetIncidentWallGuids(Graph, FGuid(9,9,9,9)).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyInvalidGraphTest, "EHB.Topology.InvalidGraphDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyInvalidGraphTest::RunTest(const FString& Parameters)
{
	FEHBWallTopologySnapshot Graph = MakeDetachedGraph();
	Graph.Nodes[1].FloorIndex = 2;
	const FEHBWallTopologyNode DuplicateNode = Graph.Nodes[0];
	Graph.Nodes.Add(DuplicateNode);
	Graph.Walls[1].EndNodeGuid.Invalidate();
	const auto Issues = UEHBWallTopologyLibrary::ValidateWallTopology(Graph);
	TestTrue(TEXT("Duplicate IDs diagnosed"), HasIssue(Issues, TEXT("DuplicateNodeId")));
	TestTrue(TEXT("Missing endpoint diagnosed"), HasIssue(Issues, TEXT("MissingEndNode")));
	TestTrue(TEXT("Cross-floor connection diagnosed"), HasIssue(Issues, TEXT("CrossFloorConnection")));
	TestFalse(TEXT("Validation does not repair input"), Graph.Walls[1].EndNodeGuid.IsValid());
	Graph.Walls[0].EndNodeGuid = Graph.Walls[0].StartNodeGuid;
	TestTrue(TEXT("Self connection diagnosed"), HasIssue(UEHBWallTopologyLibrary::ValidateWallTopology(Graph), TEXT("SelfConnection")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyLegacyReadOnlyTest, "EHB.Topology.LegacyCaptureIsReadOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyLegacyReadOnlyTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Rectangle fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	TestEqual(TEXT("Fixture has one closed room"), Fixture.Building->ClosedLoops.Num(), 1);
	const int32 RelationCount = Fixture.Building->ElementRelations.Num();
	const auto Before = UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building);
	TestEqual(TEXT("Four nodes captured"), Before.Nodes.Num(), 4);
	TestEqual(TEXT("Four walls captured"), Before.Walls.Num(), 4);
	TestEqual(TEXT("Valid legacy graph"), Before.Issues.Num(), 0);
	FString JsonA, JsonB;
	FJsonObjectConverter::UStructToJsonObjectString(Before, JsonA);
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building), JsonB);
	TestEqual(TEXT("Repeated capture deterministic"), JsonA, JsonB);
	TestEqual(TEXT("Capture preserves relations"), Fixture.Building->ElementRelations.Num(), RelationCount);
	const FGuid OriginalStart = Fixture.Walls[0]->StartPillarGuid;
	Fixture.Walls[0]->StartPillarGuid = FGuid(9,9,9,9);
	const auto Stale = UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building);
	TestTrue(TEXT("Stale legacy cache diagnosed"), HasIssue(Stale.Issues, TEXT("LegacyEndpointMismatch")));
	TestEqual(TEXT("Read does not repair cache"), Fixture.Walls[0]->StartPillarGuid, FGuid(9,9,9,9));
	Fixture.Walls[0]->StartPillarGuid = OriginalStart;
	const auto Report = UEHBBuildingPerformanceAnalyzer::AnalyzeBuilding(Fixture.Building, FEHBBuildingPerformanceBudget());
	TestTrue(TEXT("Fixture performance report valid"), Report.bValid);
	AddInfo(FString::Printf(TEXT("EHB topology baseline: nodes=%d walls=%d rooms=%d. %s"), Before.Nodes.Num(), Before.Walls.Num(), Fixture.Building->ClosedLoops.Num(), *UEHBBuildingPerformanceAnalyzer::FormatPerformanceReportSummary(Report)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyAmbiguousPortTest, "EHB.Topology.AmbiguousAndDisabledRelations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyAmbiguousPortTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	FEHBElementRelation Duplicate;
	for (const FEHBElementRelation& R : Fixture.Building->ElementRelations)
	{
		if (R.Type == EEHBElementRelationType::TopologyConnection && R.Source.ElementGuid == Fixture.Walls[0]->ElementGuid && R.Source.SurfaceKind == EEHBElementSurfaceKind::Start) { Duplicate = R; break; }
	}
	Duplicate.RelationGuid = FGuid::NewGuid();
	Duplicate.Target.ElementGuid = Fixture.Pillars[2]->ElementGuid;
	// Deliberately simulate corrupt serialized data without invoking repair/index rebuilding.
	Fixture.Building->ElementRelations.Add(Duplicate);
	TestTrue(TEXT("Ambiguous port is never silently accepted"), HasIssue(UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building).Issues, TEXT("AmbiguousWallPort")));
	Fixture.Building->ElementRelations.Last().bEnabled = false;
	TestFalse(TEXT("Disabled relation excluded"), HasIssue(UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building).Issues, TEXT("AmbiguousWallPort")));
	Fixture.Building->ElementRelations.Pop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyNullTest, "EHB.Topology.MissingBuilding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyNullTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Missing building is explicit"), HasIssue(UEHBWallTopologyLibrary::CaptureWallTopology(nullptr).Issues, TEXT("MissingBuilding")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomFillWithoutModeTest, "EHB.Topology.RoomFillWithoutActiveMode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBRoomFillWithoutModeTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	// Do not switch the user's editor mode just for a test. Headless regression
	// runs start without the mode; live callers can see the explicit precondition.
	if (!TestNull(TEXT("Run this regression without Building Mode"), GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId))) return false;
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		if (AActor* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Fixture.Building, true, false);
	// Serialized relations survive loading; transient topology caches may not yet
	// contain attached children. The first command must work without a snapshot
	// query or a failed command first rebuilding those caches as a side effect.
	const int32 RelationCount = Fixture.Building->ElementRelations.Num();
	Fixture.Building->WallConnectionsByWallGuid.Reset();
	Fixture.Building->PillarConnectionsByPillarGuid.Reset();
	Fixture.Building->ClosedLoops.Reset();
	Fixture.Building->PillarToLoopGuids.Reset();
	Fixture.Building->WallToLoopGuids.Reset();
	AEHB_FloorSlab* Slab = UEHBBuildingToolset::CreateRoomFilledFloorSlabAtPoint(Fixture.Building, TEXT("EHB_Regression_Ceiling"), FVector(300, 250, 0), 300, 20, 1);
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection)
		if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	if (!TestNotNull(TEXT("MCP room slab created without active mode"), Slab)) return false;
	Slab->SetFlags(RF_Transient);
	Fixture.Actors.Add(Slab);
	TestTrue(TEXT("Filled slab has a valid outline"), Slab->LocalTopPolygon.Num() >= 4);
	TestEqual(TEXT("First fill preserves serialized relations"), Fixture.Building->ElementRelations.Num(), RelationCount);
	TestEqual(TEXT("First fill recovers the room"), Fixture.Building->ClosedLoops.Num(), 1);
	TestFalse(TEXT("Helper did not activate Building Mode"), GLevelEditorModeTools().IsModeActive(FEasyHouseEditorMode::EM_EasyHouseEditorModeId));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBTopologyMigrationTest, "EHB.Topology.PersistedMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBTopologyMigrationTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	const auto Before = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
	TestEqual(TEXT("Old assets default to uninitialized"), Building->TopologyMigrationBaseline.Version, 0);
	TestEqual(TEXT("Preview is ready"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("Ready"));
	TestEqual(TEXT("Preview did not migrate"), Building->TopologyMigrationBaseline.Version, 0);
	TestTrue(TEXT("Explicit initialization writes"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true).bChanged);
	TestEqual(TEXT("Stored node count"), Building->TopologyMigrationBaseline.Nodes.Num(), 4);
	TestEqual(TEXT("Stored edge count"), Building->TopologyMigrationBaseline.Walls.Num(), 4);
	TestEqual(TEXT("Explicit pillar mapping"), Building->TopologyMigrationBaseline.Nodes[0].SourcePillarGuid, Before.Nodes[0].SourcePillarGuid);
	const auto Repeated = UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TestTrue(TEXT("Repeated migration succeeds"), Repeated.bSucceeded);
	TestFalse(TEXT("Repeated migration does not write"), Repeated.bChanged);
	FString JsonBefore, JsonAfter;
	FJsonObjectConverter::UStructToJsonObjectString(Before, JsonBefore);
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building), JsonAfter);
	TestEqual(TEXT("Migration does not alter legacy graph"), JsonAfter, JsonBefore);

	const FProperty* Property = FindFProperty<FProperty>(AEHBBuildingActorBase::StaticClass(), TEXT("TopologyMigrationBaseline"));
	if (!TestNotNull(TEXT("Building has reflected storage"), Property)) return false;
	TestFalse(TEXT("Storage is serialized, not transient"), Property->HasAnyPropertyFlags(CPF_Transient));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FEHBPersistedWallTopology::StaticStruct()->SerializeItem(Writer, &Building->TopologyMigrationBaseline, nullptr);
	FEHBPersistedWallTopology Restored;
	FMemoryReader Reader(Bytes);
	FEHBPersistedWallTopology::StaticStruct()->SerializeItem(Reader, &Restored, nullptr);
	TestFalse(TEXT("Binary read is valid"), Reader.IsError());
	FString StoredJson, RestoredJson;
	FJsonObjectConverter::UStructToJsonObjectString(Building->TopologyMigrationBaseline, StoredJson);
	FJsonObjectConverter::UStructToJsonObjectString(Restored, RestoredJson);
	TestEqual(TEXT("Binary roundtrip preserves all fields"), RestoredJson, StoredJson);
	Building->TopologyMigrationBaseline.Nodes[0].LocalPosition.X += 1;
	TestEqual(TEXT("Divergence is explicit"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true).Status, FName("SourceChanged"));
	TestEqual(TEXT("Conflict does not overwrite persisted position"), Building->TopologyMigrationBaseline.Nodes[0].LocalPosition.X, Restored.Nodes[0].LocalPosition.X + 1);
	Building->TopologyMigrationBaseline = Restored;
	Building->TopologyMigrationBaseline.Version = 99;
	TestEqual(TEXT("Future data not overwritten"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true).Status, FName("UnsupportedVersion"));
	Building->TopologyMigrationBaseline = FEHBPersistedWallTopology();
	Fixture.Walls[0]->StartPillarGuid.Invalidate();
	TestFalse(TEXT("Invalid source rejected"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true).bSucceeded);
	TestEqual(TEXT("Invalid source did not partially initialize"), Building->TopologyMigrationBaseline.Version, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMovePreviewTest, "EHB.Topology.NodeMovePreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBNodeMovePreviewTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	const FGuid NodeId = Fixture.Pillars[0]->ElementGuid;
	TestEqual(TEXT("Migration required"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(-100,0,0)).Status, FName("MigrationRequired"));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	FString Before;
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building), Before);
	const auto Preview = UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(-100,0,0));
	TestTrue(TEXT("Safe centerline proposal accepted"), Preview.bSucceeded);
	TestEqual(TEXT("Two direct walls"), Preview.DirectWallGuids.Num(), 2);
	TestEqual(TEXT("Legacy refresh can reach all four walls"), Preview.ConnectedWallGuids.Num(), 4);
	TestEqual(TEXT("Legacy refresh can reach all four nodes"), Preview.ConnectedNodeGuids.Num(), 4);
	const auto* Proposed = Preview.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == NodeId; });
	if (TestNotNull(TEXT("Proposed node exists"), Proposed)) TestEqual(TEXT("Proposal contains target"), Proposed->LocalPosition, FVector(-100,0,0));
	TestEqual(TEXT("Pillar remains stationary"), Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(), FVector::ZeroVector);
	FString After;
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building), After);
	TestEqual(TEXT("Live graph unchanged"), After, Before);
	TestEqual(TEXT("Persisted graph unchanged"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
	TestEqual(TEXT("Stale drag rejected"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector(1,0,0), FVector(-100,0,0)).Status, FName("StalePosition"));
	TestEqual(TEXT("Floor change rejected"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(0,0,100)).Status, FName("VerticalMoveUnsupported"));
	TestEqual(TEXT("Merge not implicit"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(600,0,0)).Status, FName("NodeTooClose"));
	TestEqual(TEXT("Crossing rejected"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(700,250,0)).Status, FName("WallIntersection"));
	TestEqual(TEXT("Collinear overlapping rejected"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector(600,700,0)).Status, FName("WallIntersection"));
	TestEqual(TEXT("No-op explicit"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeId, FVector::ZeroVector, FVector::ZeroVector).Status, FName("NoChange"));
	TestEqual(TEXT("Unknown node rejected"), UEHBWallTopologyLibrary::PreviewNodeMove(Building, FGuid::NewGuid(), FVector::ZeroVector, FVector::ZeroVector).Status, FName("UnknownNode"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeBatchMovePreviewTest, "EHB.Topology.NodeBatchMovePreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBNodeBatchMovePreviewTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)) return false;
	auto* Building = Fixture.Building; Building->SetActorRotation(FRotator(0,37,0));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	TArray<FEHBNodeMoveRequest> Requests;
	for (auto* Pillar : Fixture.Pillars)
	{
		auto& R = Requests.AddDefaulted_GetRef(); R.NodeGuid = Pillar->ElementGuid;
		R.ExpectedPosition = Pillar->GetElementLocalTransform().GetLocation(); R.TargetPosition = R.ExpectedPosition + FVector(600,0,0);
	}
	auto TopologyJson = [](const FEHBWallTopologySnapshot& Graph) { FString Json; FJsonObjectConverter::UStructToJsonObjectString(Graph,Json); return Json; };
	const FString Original = TopologyJson(UEHBWallTopologyLibrary::CaptureWallTopology(Building));
	const int32 QueueCount = GEditor->Trans->GetQueueLength();
	TestEqual(TEXT("Single intermediate step would collide with unmoved corner"),UEHBWallTopologyLibrary::PreviewNodeMove(Building,Requests[0].NodeGuid,Requests[0].ExpectedPosition,Requests[0].TargetPosition).Status,FName("NodeTooClose"));
	const auto Batch = UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Requests);
	TestTrue(TEXT("Simultaneous final translation is valid"),Batch.bSucceeded);
	TestEqual(TEXT("Every unique direct wall reported"),Batch.DirectWallGuids.Num(),4);
	TestEqual(TEXT("All requested nodes retained"),Batch.NodeMoves.Num(),4);
	TestFalse(TEXT("Batch does not imply one authoritative scalar node"),Batch.NodeGuid.IsValid());
	if(TestEqual(TEXT("Shared room reported once"),Batch.RoomBoundaryChanges.Num(),1))
	{
		const auto& Change = Batch.RoomBoundaryChanges[0];
		for(int32 I=0;I<Change.ProposedPolygon.Num();++I)
			TestTrue(TEXT("Both original and proposed corners use consistent local frames"),(Change.ProposedPolygon[I]-Change.OriginalPolygon[I]).Equals(FVector(600,0,0),0.001));
	}
	FString FirstJson,ReversedJson;
	FJsonObjectConverter::UStructToJsonObjectString(Batch,FirstJson);
	TArray<FEHBNodeMoveRequest> Reversed=Requests; Swap(Reversed[0],Reversed[3]); Swap(Reversed[1],Reversed[2]);
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Reversed),ReversedJson);
	TestEqual(TEXT("Input order does not change canonical plan"),ReversedJson,FirstJson);
	TestEqual(TEXT("Empty batch rejected"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,{}).Status,FName("InvalidBatchSize"));
	TArray<FEHBNodeMoveRequest> Oversize; Oversize.SetNum(2049);
	TestEqual(TEXT("Oversized batch rejected"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Oversize).Status,FName("InvalidBatchSize"));
	TestEqual(TEXT("Duplicate request rejected"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,{Requests[0],Requests[0]}).Status,FName("DuplicateNodeRequest"));
	TArray<FEHBNodeMoveRequest> Bad=Requests; Bad[2].ExpectedPosition.X+=1;
	TestEqual(TEXT("Any stale node rejects whole batch"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Bad).Status,FName("StalePosition"));
	Bad=Requests; Bad[2].TargetPosition.Z+=1;
	TestEqual(TEXT("Any vertical move rejects batch"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Bad).Status,FName("VerticalMoveUnsupported"));
	Bad=Requests; Bad[2].TargetPosition=Bad[1].TargetPosition;
	const auto Merged=UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Bad);
	TestEqual(TEXT("Final merged nodes reject whole batch"),Merged.Status,FName("NodeTooClose"));
	TestEqual(TEXT("Rejected proposal contains no partially moved positions"),TopologyJson(Merged.ProposedTopology),Original);
	TestTrue(TEXT("Rejected batch has no room proposals"),Merged.RoomBoundaryChanges.IsEmpty());
	Bad=Requests; Swap(Bad[1].TargetPosition,Bad[2].TargetPosition);
	const auto Crossing=UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Bad);
	TestEqual(TEXT("Final self-crossing rejects batch"),Crossing.Status,FName("WallIntersection"));
	TestEqual(TEXT("Crossing rejection restores all copied positions"),TopologyJson(Crossing.ProposedTopology),Original);
	Bad=Requests; for(auto& R:Bad)R.TargetPosition=R.ExpectedPosition;
	TestEqual(TEXT("Unchanged batch explicit no-op"),UEHBWallTopologyLibrary::PreviewNodeMoves(Building,Bad).Status,FName("NoChange"));
	TestEqual(TEXT("Every preview leaves live topology untouched"),TopologyJson(UEHBWallTopologyLibrary::CaptureWallTopology(Building)),Original);
	TestEqual(TEXT("Every preview leaves transaction queue untouched"),GEditor->Trans->GetQueueLength(),QueueCount);
	TestEqual(TEXT("Every preview preserves baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMoveCommitTest, "EHB.Topology.NodeMoveCommitUndoRedo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBNodeMoveCommitTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Building, true, false);
	TGuardValue<int32> BatchGuard(EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	auto MeshSnapshot=[&]()
	{
		FString Value;
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString();
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)
				if(const auto* Section=Mesh->GetProcMeshSection(I))
				{
					Value+=FString::FromInt(Section->bEnableCollision)+FString::FromInt(Section->bSectionVisible);
					for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString()+V.Tangent.TangentX.ToString()+FString::FromInt(V.Tangent.bFlipTangentY);
					for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
				}
		}
		return Value;
	};
	const FString OriginalMeshes=MeshSnapshot();
	const FGuid NodeId = Fixture.Pillars[0]->ElementGuid;
	const FVector OldEnd = Fixture.Walls[0]->LocalStart;
	const FBoxSphereBounds OldBounds = Fixture.Walls[0]->LeftWallMeshComponent->CalcBounds(FTransform::Identity);
	const auto Committed = UEHBBuildingToolset::CommitBasicNodeMove(Building, NodeId, FVector::ZeroVector, FVector(-100,0,0));
	TestTrue(*Committed.Message, Committed.bSucceeded);
	TestEqual(TEXT("Basic movement uses one definition batch solve"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,1);
	const FString ChangedMeshes=MeshSnapshot();
	TestEqual(TEXT("Actor moves to exact preview point"), Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(), FVector(-100,0,0));
	TestTrue(TEXT("Wall endpoint refreshed"), !Fixture.Walls[0]->LocalStart.Equals(OldEnd));
	TestEqual(TEXT("Baseline synchronized"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
	TestEqual(TEXT("Room survives"), Building->ClosedLoops.Num(), 1);
	if (Committed.bSucceeded)
	{
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores all generated mesh data"),MeshSnapshot(),OriginalMeshes);
		TestEqual(TEXT("Undo restores actor"), Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(), FVector::ZeroVector);
		TestTrue(TEXT("Undo restores wall endpoint"), Fixture.Walls[0]->LocalStart.Equals(OldEnd));
		TestTrue(TEXT("Undo restores mesh bounds"), Fixture.Walls[0]->LeftWallMeshComponent->CalcBounds(FTransform::Identity).BoxExtent.Equals(OldBounds.BoxExtent));
		TestEqual(TEXT("Undo keeps baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo restores all generated mesh data"),MeshSnapshot(),ChangedMeshes);
		TestEqual(TEXT("Redo restores new point"), Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(), FVector(-100,0,0));
		TestEqual(TEXT("Redo keeps baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
	}
	const auto Stale = UEHBBuildingToolset::CommitBasicNodeMove(Building, NodeId, FVector::ZeroVector, FVector(-150,0,0));
	TestFalse(TEXT("Stale commit rejected"), Stale.bSucceeded);
	Fixture.Walls[0]->CurveControlOffset = 25;
	const auto Curve = UEHBBuildingToolset::CommitBasicNodeMove(Building, NodeId, FVector(-100,0,0), FVector(-150,0,0));
	TestFalse(TEXT("Unsupported curve rejected before mutation"), Curve.bSucceeded);
	TestEqual(TEXT("Rejected commit does not move actor"), Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(), FVector(-100,0,0));
	Fixture.Walls[0]->CurveControlOffset = 0;
	const int32 QueueBeforeNoop=GEditor->Trans->GetQueueLength();
	TestEqual(TEXT("Basic no-op"),UEHBBuildingToolset::CommitBasicNodeMove(Building,NodeId,FVector(-100,0,0),FVector(-100,0,0)).Message,FString(TEXT("NoChange")));
	TestEqual(TEXT("No-op creates no transaction"),GEditor->Trans->GetQueueLength(),QueueBeforeNoop);
	TestEqual(TEXT("No-op and refusals do not solve again"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,1);
	for(const auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> FailureGuard(EHBNodeMoveTestHooks::FailurePhase,Phase);
		const auto Failed=UEHBBuildingToolset::CommitBasicNodeMove(Building,NodeId,FVector(-100,0,0),FVector(-150,30,0));
		TestEqual(TEXT("Basic batch failure uses production rollback"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
		TestEqual(TEXT("Rollback restores all generated mesh data"),MeshSnapshot(),ChangedMeshes);
		TestEqual(TEXT("Rollback preserves position"),Fixture.Pillars[0]->GetElementLocalTransform().GetLocation(),FVector(-100,0,0));
		TestEqual(TEXT("Rollback preserves baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName(TEXT("AlreadyInitialized")));
		TestFalse(TEXT("Failed basic batch cannot redo"),GEditor->Trans->CanRedo());
	}
	// Scaled buildings were supported by this basic command before definition dispatch.
	Building->SetActorScale3D(FVector(1.25));
	const FVector ScaledStart=Fixture.Pillars[0]->GetElementLocalTransform().GetLocation();
	const int32 BeforeLegacy=EHBNodeMoveTestHooks::DefinitionBatchSolveCount;
	const auto Legacy=UEHBBuildingToolset::CommitBasicNodeMove(Building,NodeId,ScaledStart,FVector(-180,40,0));
	TestTrue(TEXT("Scaled basic command preserves legacy compatibility"),Legacy.bSucceeded);
	TestEqual(TEXT("Unsupported transform does not enter definition solver"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,BeforeLegacy);
	if(Legacy.bSucceeded)
	{
		GEditor->UndoTransaction();
		TestTrue(TEXT("Legacy undo restores local position"),Fixture.Pillars[0]->GetElementLocalTransform().GetLocation().Equals(ScaledStart,0.001));
		TestTrue(TEXT("Legacy undo preserves building scale"),Building->GetActorScale3D().Equals(FVector(1.25)));
	}

	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabSnapPreviewTest, "EHB.Topology.SharedSlabSnapPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBSlabSnapPreviewTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(World))) return false;
	auto SpawnSlab = [&](FGuid Id, bool bOwned) -> AEHB_FloorSlab*
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Slab = World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(), Fixture.Building->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!Slab) return nullptr;
		Fixture.Actors.Add(Slab);
		Slab->ElementGuid = Id;
		Slab->bIsFoundation = false;
		Slab->bKeepFoundationBottomOnGround = false;
		Slab->VisualExpansion = 0;
		if (bOwned) Slab->AttachToBuilding(Fixture.Building, FTransform::Identity);
		Slab->LocalTopPolygon = { FVector(0,0,0), FVector(600,0,0), FVector(600,500,0), FVector(0,500,0) };
		Slab->RebuildSlabMesh();
		return Slab;
	};
	// Spawn higher ID first: tied candidates must not depend on actor iteration.
	if (!TestNotNull(TEXT("First slab"), SpawnSlab(FGuid(0,0,0,3), true))) return false;
	if (!TestNotNull(TEXT("Second slab"), SpawnSlab(FGuid(0,0,0,2), true))) return false;
	if (!TestNotNull(TEXT("Unowned slab at same location"), SpawnSlab(FGuid(0,0,0,1), false))) return false;
	auto* Pillar = Fixture.Pillars[0];
	const FVector Before = Pillar->GetActorLocation();
	const FVector Desired = Fixture.Building->GetActorTransform().TransformPosition(FVector(300,5,0));
	const auto Snap = Pillar->PreviewFloorSlabBoundarySnap(Desired, FRotator::ZeroRotator, 30);
	TestTrue(TEXT("Edge candidate found"), Snap.bFound);
	TestEqual(TEXT("Only owning building slabs considered"), Snap.CandidateSlabCount, 2);
	TestEqual(TEXT("Stable tie resolves by slab ID"), Snap.SlabGuid, FGuid(0,0,0,2));
	TestEqual(TEXT("Preview does not move pillar"), Pillar->GetActorLocation(), Before);
	TestTrue(TEXT("Inset matches half pillar depth"), FMath::IsNearlyEqual(Snap.WorldLocation.Y, Fixture.Building->GetActorLocation().Y + Pillar->Depth * 0.5, 0.01));
	TestFalse(TEXT("Different height rejected"), Pillar->PreviewFloorSlabBoundarySnap(Desired + FVector(0,0,1000), FRotator::ZeroRotator, 30).bFound);
	TestFalse(TEXT("Invalid distance rejected"), Pillar->PreviewFloorSlabBoundarySnap(Desired, FRotator::ZeroRotator, -1).bFound);
	Pillar->SetActorLocation(Desired);
	TestTrue(TEXT("Manual snap applies"), Pillar->SnapToAdjacentFloorSlabBoundary(30, true));
	TestTrue(TEXT("Manual position equals preview"), Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
	TestTrue(TEXT("Manual rotation equals preview"), Pillar->GetActorRotation().Equals(Snap.WorldRotation, 0.001));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFixedSlabCommitTest, "EHB.Topology.FixedSlabCommitUndoRedo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBFixedSlabCommitTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(World))) return false;
	auto* Building = Fixture.Building;
	auto* Pillar = Fixture.Pillars[0];
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Slab = World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(), Building->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Slab created"), Slab)) return false;
	Fixture.Actors.Add(Slab);
	Slab->AttachToBuilding(Building, FTransform::Identity);
	Slab->bIsFoundation = false;
	Slab->bKeepFoundationBottomOnGround = false;
	Slab->VisualExpansion = 0;
	Slab->LocalTopPolygon = { FVector(-200,-200,0), FVector(800,-200,0), FVector(800,700,0), FVector(-200,700,0) };
	Slab->RebuildSlabMesh();
	const auto OriginalPolygon = Slab->LocalTopPolygon;
	// Rotation is intentionally absent from the migration position baseline.
	Pillar->SetActorRotation(FRotator(0, 17, 0));
	Pillar->RebuildPillarMesh();
	Building->RefreshWallsConnectedToPillar(Pillar->ElementGuid, true);
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Building, true, false);
	TGuardValue<int32> BatchGuard(EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	auto MeshSnapshot=[&]()
	{
		FString Value;
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString();TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
			{
				Value+=FString::FromInt(Section->bEnableCollision)+FString::FromInt(Section->bSectionVisible);
				for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString()+V.Tangent.TangentX.ToString()+FString::FromInt(V.Tangent.bFlipTangentY);
				for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
			}
		}
		return Value;
	};
	const FString OriginalMeshes=MeshSnapshot();
	const FVector Requested(-195, 0, 0);
	const FRotator OldRotation = Pillar->GetActorRotation();
	const auto Snap = Pillar->PreviewFloorSlabBoundarySnap(Building->GetActorTransform().TransformPosition(Requested), OldRotation, 30);
	TestTrue(TEXT("Snap found"), Snap.bFound);
	TestFalse(TEXT("Snap includes a rotation change"), Snap.WorldRotation.Equals(OldRotation, 0.001));
	auto Commit = [&](FVector Expected, FRotator ExpectedRotation, FGuid Host, FVector Point)
	{
		return UEHBBuildingToolset::CommitNodeMoveOnFixedSlab(Building, Pillar->ElementGuid, Expected, ExpectedRotation, Requested, Host, Point, Snap.WorldRotation, 30);
	};
	const int32 BeforeRelations = Building->ElementRelations.Num();
	TestEqual(TEXT("Wrong host rejected"), Commit(FVector::ZeroVector, OldRotation, FGuid::NewGuid(), Snap.WorldLocation).Message, FString("StaleSnap"));
	TestEqual(TEXT("Changed preview point rejected"), Commit(FVector::ZeroVector, OldRotation, Slab->ElementGuid, Snap.WorldLocation + FVector(1,0,0)).Message, FString("StaleSnap"));
	TestEqual(TEXT("Changed rotation rejected"), Commit(FVector::ZeroVector, FRotator::ZeroRotator, Slab->ElementGuid, Snap.WorldLocation).Message, FString("StaleRotation"));
	Slab->bHasRoomFillAnchor = true;
	TestFalse(TEXT("Room-dependent slab rejected"), Commit(FVector::ZeroVector, OldRotation, Slab->ElementGuid, Snap.WorldLocation).bSucceeded);
	Slab->bHasRoomFillAnchor = false;
	TestEqual(TEXT("Rejected plans preserve relations"), Building->ElementRelations.Num(), BeforeRelations);
	TestTrue(TEXT("Rejected plans preserve position"), Pillar->GetElementLocalTransform().GetLocation().Equals(FVector::ZeroVector));
	TestEqual(TEXT("Refused fixed-slab plans do not solve geometry"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	for(const auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> FailureGuard(EHBNodeMoveTestHooks::FailurePhase,Phase);
		const auto Failed=Commit(FVector::ZeroVector,OldRotation,Slab->ElementGuid,Snap.WorldLocation);
		TestEqual(TEXT("Fixed-slab batch failure recovers with production undo"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
		TestEqual(TEXT("Failure restores all generated mesh data"),MeshSnapshot(),OriginalMeshes);
		TestTrue(TEXT("Failure restores node pose"),Pillar->GetElementLocalTransform().GetLocation().Equals(FVector::ZeroVector,0.001)&&Pillar->GetActorRotation().Equals(OldRotation,0.001));
		TestEqual(TEXT("Failure restores host relation count"),Building->ElementRelations.Num(),BeforeRelations);
		TestEqual(TEXT("Failure preserves migration baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName(TEXT("AlreadyInitialized")));
		TestFalse(TEXT("Failed fixed-slab edit cannot redo"),GEditor->Trans->CanRedo());
	}
	const int32 BeforeCommit=EHBNodeMoveTestHooks::DefinitionBatchSolveCount;
	const auto Result = Commit(FVector::ZeroVector, OldRotation, Slab->ElementGuid, Snap.WorldLocation);
	TestTrue(*Result.Message, Result.bSucceeded);
	TestEqual(TEXT("Fixed-slab position and yaw commit uses one definition batch"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,BeforeCommit+1);
	const FString ChangedMeshes=MeshSnapshot();
	if (Result.bSucceeded)
	{
		TestTrue(TEXT("Exact world point"), Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
		TestTrue(TEXT("Exact world rotation"), Pillar->GetActorRotation().Equals(Snap.WorldRotation, 0.001));
		TestEqual(TEXT("One boundary association added"), Building->ElementRelations.Num(), BeforeRelations + 1);
		const auto* Association = Building->ElementRelations.FindByPredicate([](const auto& R) { return R.Type == EEHBElementRelationType::BoundaryAttachment; });
		if (TestNotNull(TEXT("Association exists"), Association))
		{
			TestEqual(TEXT("Slab is source"), Association->Source.ElementGuid, Slab->ElementGuid);
			TestFalse(TEXT("No inferred floor assignment"), Association->bAffectsFloorAssignment);
		}
		TestTrue(TEXT("Slab polygon remains fixed"), Slab->LocalTopPolygon == OriginalPolygon);
		TestEqual(TEXT("Baseline synchronized"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores every generated mesh"),MeshSnapshot(),OriginalMeshes);
		TestTrue(TEXT("Undo position"), Pillar->GetElementLocalTransform().GetLocation().Equals(FVector::ZeroVector));
		TestTrue(TEXT("Undo rotation"), Pillar->GetActorRotation().Equals(OldRotation, 0.001));
		TestEqual(TEXT("Undo association"), Building->ElementRelations.Num(), BeforeRelations);
		TestEqual(TEXT("Undo baseline"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName("AlreadyInitialized"));
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo restores every generated mesh"),MeshSnapshot(),ChangedMeshes);
		TestTrue(TEXT("Redo rotation"), Pillar->GetActorRotation().Equals(Snap.WorldRotation, 0.001));
		TestEqual(TEXT("Redo association"), Building->ElementRelations.Num(), BeforeRelations + 1);
		const auto Repeat = Commit(Pillar->GetElementLocalTransform().GetLocation(), Pillar->GetActorRotation(), Slab->ElementGuid, Snap.WorldLocation);
		TestEqual(TEXT("Repeated commit is no-op"), Repeat.Message, FString("NoChange"));
		TestEqual(TEXT("Repeated host pose does not solve again"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,BeforeCommit+1);
		TestEqual(TEXT("No duplicate association"), Building->ElementRelations.Num(), BeforeRelations + 1);
		// Another pillar's valid association must not block the next independent edit.
		auto* Other = Fixture.Pillars[1];
		const FVector OtherRequested(795, 0, 0);
		const auto OtherSnap = Other->PreviewFloorSlabBoundarySnap(Building->GetActorTransform().TransformPosition(OtherRequested), Other->GetActorRotation(), 30);
		const auto OtherResult = UEHBBuildingToolset::CommitNodeMoveOnFixedSlab(Building, Other->ElementGuid,
			Other->GetElementLocalTransform().GetLocation(), Other->GetActorRotation(), OtherRequested,
			OtherSnap.SlabGuid, OtherSnap.WorldLocation, OtherSnap.WorldRotation, 30);
		TestTrue(TEXT("Next pillar can also bind"), OtherResult.bSucceeded);
		TestEqual(TEXT("Both associations retained"), Building->ElementRelations.Num(), BeforeRelations + 2);
		if (OtherResult.bSucceeded)
		{
			GEditor->UndoTransaction();
			TestEqual(TEXT("Undo second edit preserves first association"), Building->ElementRelations.Num(), BeforeRelations + 1);
			TestTrue(TEXT("Undo second edit preserves first pillar"), Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
		}
	}
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBEditorSlabSnapPriorityTest, "EHB.Topology.EditorSlabSnapPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBEditorSlabSnapPriorityTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(World))) return false;
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Slab = World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(), Fixture.Building->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Slab created"), Slab)) return false;
	Fixture.Actors.Add(Slab);
	Slab->AttachToBuilding(Fixture.Building, FTransform::Identity);
	Slab->bIsFoundation = false;
	Slab->bKeepFoundationBottomOnGround = false;
	Slab->VisualExpansion = 0;
	Slab->LocalTopPolygon = { FVector(0,0.25,0), FVector(600,0.25,0), FVector(600,500,0), FVector(0,500,0) };
	Slab->RebuildSlabMesh();
	auto* Pillar = Fixture.Pillars[0];
	Pillar->Depth = 20;
	Pillar->RebuildPillarMesh();
	const FVector Desired = Fixture.Building->GetActorTransform().TransformPosition(FVector(300,5,0));
	const auto Snap = Pillar->PreviewFloorSlabBoundarySnap(Desired, Pillar->GetActorRotation(), 30);
	TestTrue(TEXT("Fractional slab edge candidate found"), Snap.bFound);
	TestTrue(TEXT("Fixture uses a fractional edge inset"), !FMath::IsNearlyEqual(Snap.WorldLocation.Y, FMath::RoundToDouble(Snap.WorldLocation.Y), 0.01));
	Pillar->PreEditChange(nullptr);
	Pillar->SetActorLocation(Desired);
	Pillar->PostEditMove(false);
	TestTrue(TEXT("Drag tick preserves slab candidate over wall-angle and integer snapping"), Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
	for (int32 Tick = 0; Tick < 5; ++Tick)
	{
		Pillar->PostEditMove(false);
		TestTrue(*FString::Printf(TEXT("Stationary drag tick %d does not accumulate snap drift"), Tick),
			Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
	}
	Pillar->PostEditMove(true);
	TestTrue(TEXT("Drag completion preserves exact slab candidate"), Pillar->GetActorLocation().Equals(Snap.WorldLocation, 0.001));
	TestTrue(TEXT("Drag completion preserves candidate rotation"), Pillar->GetActorRotation().Equals(Snap.WorldRotation, 0.001));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallSplitInspectionTest, "EHB.Topology.WallSplitInspection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallSplitInspectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(World))) return false;
	AEHB_Wall* Wall = Fixture.Walls[0];
	const float Length = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	const float Split = Length * 0.5f;
	const FString Before = [&]() { FString Json; FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building), Json); return Json; }();
	const int32 Revision = Fixture.Building->RelationshipGraphRevision;
	const auto Empty = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, Split);
	TestTrue(TEXT("Plain wall inspection succeeds"), Empty.bSucceeded);
	TestFalse(TEXT("Inspection never authorizes legacy split"), Empty.bCommitAvailable);
	TestEqual(TEXT("Column width uses wall thickness"), Empty.PillarWidth, Wall->Thickness);
	TestEqual(TEXT("Column height uses wall height"), Empty.PillarHeight, Wall->Height);
	TestTrue(TEXT("Column base is on source wall axis"), Empty.LocalPillarPosition.Equals((Wall->LocalStart + Wall->LocalEnd) * 0.5f));
	TestTrue(TEXT("Source relations are reported"), Empty.RelatedRelationGuids.Num() > 0);
	TestEqual(TEXT("Plain inspection leaves revision unchanged"), Fixture.Building->RelationshipGraphRevision, Revision);
	TestEqual(TEXT("Endpoint insertion refused"), UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, 0).Status, FName(TEXT("TooCloseToEndpoint")));
	TArray<FGuid> OpeningIds;
	for (float Ratio : {0.25f, 0.75f, 0.5f})
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		AEHB_DoorWindow* Door = World->SpawnActor<AEHB_DoorWindow>(Params);
		if (!TestNotNull(TEXT("Opening actor spawned"), Door)) return false;
		Fixture.Actors.Add(Door);
		Door->AttachToBuilding(Fixture.Building, FTransform(FVector(0, 0, 0)));
		Door->OwningWallGuid = Wall->ElementGuid;
		Door->DistanceFromWallStart = Length * Ratio;
		Door->OpeningWidth = 40;
		FEHBWallDoorWindowConnection& Connection = Wall->DoorWindowConnections.AddDefaulted_GetRef();
		Connection.DoorWindowGuid = Door->ElementGuid;
		Connection.DistanceFromStart = Door->DistanceFromWallStart;
		Connection.OpeningWidth = Door->OpeningWidth;
		OpeningIds.Add(Door->ElementGuid);
	}
	const auto Conflict = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, Split);
	TestFalse(TEXT("Column intersecting opening refused"), Conflict.bSucceeded);
	TestEqual(TEXT("First opening assigned before column"), Conflict.BeforeOpeningGuids.Num(), 1);
	TestEqual(TEXT("Second opening assigned after column"), Conflict.AfterOpeningGuids.Num(), 1);
	TestTrue(TEXT("Actual conflicting opening identified"), Conflict.ConflictingOpeningGuids.Contains(OpeningIds[2]));
	Wall->DoorWindowConnections[0].DistanceFromStart += 5;
	TestEqual(TEXT("Stale actor/connection refused"), UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, Split).Status, FName(TEXT("RequiresOpeningInspection")));
	Fixture.Building->SetActorScale3D(FVector(5, 5.75, 1));
	TestEqual(TEXT("Nonuniform scale refused"), UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, Split).Status, FName(TEXT("UnsupportedTransform")));
	Fixture.Building->SetActorScale3D(FVector::OneVector);
	FString After;
	FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Fixture.Building), After);
	TestEqual(TEXT("Wall topology unchanged by inspection"), After, Before);
	// Fixture attachment can update unrelated registration revisions; snapshot the
	// current revision to verify repeated inspection itself remains read-only.
	const int32 CurrentRevision = Fixture.Building->RelationshipGraphRevision;
	UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Fixture.Building, Wall->ElementGuid, Split);
	TestEqual(TEXT("Relation revision unchanged by inspection"), Fixture.Building->RelationshipGraphRevision, CurrentRevision);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPlainWallSplitCommitTest, "EHB.Topology.PlainWallSplitUndoRedo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBPlainWallSplitCommitTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	auto* Source = Fixture.Walls[0];
	Source->bGenerateLinkedPillarEndCaps = false;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Building, true, false);
	const FGuid SourceGuid = Source->ElementGuid;
	const FVector Start = Source->LocalStart;
	const FVector End = Source->LocalEnd;
	const float Height = Source->Height;
	const float Width = Source->Thickness;
	const float Distance = FVector::Dist2D(Start, End) * 0.5f;
	const int32 Revision = Building->RelationshipGraphRevision;
	const auto OldTopology = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
	const auto Stale = UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceGuid, Distance, Revision - 1, Start, End, Height, Width);
	TestFalse(TEXT("Stale request rejected before transaction"), Stale.bSucceeded);
	TestEqual(TEXT("Rejected request keeps graph revision"), Building->RelationshipGraphRevision, Revision);
	// A cut on another wall is also outside this conservative building-wide policy.
	Fixture.Walls[1]->CutOperations.AddDefaulted();
	const auto CutRejected = UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceGuid, Distance, Revision, Start, End, Height, Width);
	TestFalse(TEXT("Unplanned cut rejected"), CutRejected.bSucceeded);
	TestFalse(TEXT("Rejection preserves original wall"), Source->IsActorBeingDestroyed());
	Fixture.Walls[1]->CutOperations.Reset();
	const auto TooNear = UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceGuid, 12.0f, Revision, Start, End, Height, Width);
	TestFalse(TEXT("New column cannot overlap endpoint column bounds"), TooNear.bSucceeded);
	const auto Commit = UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceGuid, Distance, Revision, Start, End, Height, Width);
	TestTrue(*Commit.Message, Commit.bSucceeded);
	if (Commit.bSucceeded)
	{
		const auto NewTopology = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
		TestEqual(TEXT("One additional column"), NewTopology.Nodes.Num(), OldTopology.Nodes.Num() + 1);
		TestEqual(TEXT("One wall replaced by two"), NewTopology.Walls.Num(), OldTopology.Walls.Num() + 1);
		TestFalse(TEXT("Source wall absent after split"), NewTopology.Walls.ContainsByPredicate([&](const auto& W) { return W.WallGuid == SourceGuid; }));
		TestEqual(TEXT("Room survives split"), Building->ClosedLoops.Num(), 1);
		TestEqual(TEXT("Split baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		for (auto* Element : Building->QueryElements(FEHBElementQuery()))
		{
			if (Fixture.Actors.Contains(Element)) continue;
			Fixture.Actors.Add(Element);
			Element->SetFlags(RF_Transient);
			if (auto* Pillar = Cast<AEHB_Pillar>(Element))
			{
				TestTrue(TEXT("Column matches preview position"), Pillar->GetElementLocalTransform().GetLocation().Equals((Start + End) * 0.5f, 0.001));
				TestEqual(TEXT("Column uses wall height"), Pillar->Height, Height);
				TestEqual(TEXT("Column uses wall width"), Pillar->Width, Width);
			}
			if (auto* Wall = Cast<AEHB_Wall>(Element))
			{
				TestFalse(TEXT("New wall preserves cap policy"), Wall->bGenerateLinkedPillarEndCaps);
				TestEqual(TEXT("New wall preserves floor"), Wall->FloorIndex, Source->FloorIndex);
			}
		}
		TestTrue(TEXT("Undo split"), GEditor->UndoTransaction());
		const auto Undone = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
		TestEqual(TEXT("Undo restores node count"), Undone.Nodes.Num(), OldTopology.Nodes.Num());
		TestEqual(TEXT("Undo restores wall count"), Undone.Walls.Num(), OldTopology.Walls.Num());
		TestTrue(TEXT("Undo restores original wall identity"), Undone.Walls.ContainsByPredicate([&](const auto& W) { return W.WallGuid == SourceGuid; }));
		TestEqual(TEXT("Undo baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		GEditor->RedoTransaction();
		const auto Redone = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
		TestEqual(TEXT("Redo restores split node count"), Redone.Nodes.Num(), NewTopology.Nodes.Num());
		TestEqual(TEXT("Redo baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		TestFalse(TEXT("Repeated old request cannot duplicate column"), UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceGuid, Distance, Revision, Start, End, Height, Width).bSucceeded);
		GEditor->UndoTransaction();
	}
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallSplitRollbackTest, "EHB.Topology.PlainWallSplitFailureRollback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallSplitRollbackTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	auto* Source = Fixture.Walls[0];
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Building, true, false);
	auto Snapshot = [&]()
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building), Json);
		return Json;
	};
	auto LiveCount = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AEHBElementActorBase> It(Building->GetWorld()); It; ++It)
			if (It->OwningBuilding == Building && !It->IsActorBeingDestroyed()) ++Count;
		return Count;
	};
	const FString Original = Snapshot();
	const int32 ActorCount = LiveCount();
	const float OriginalHeight = Source->Height;
	// A real preceding edit must remain undoable after the failed split is removed.
	{
		FScopedTransaction Sentinel(NSLOCTEXT("EHBTests", "BeforeFailedSplit", "Edit Before Failed Split"));
		Source->SetFlags(RF_Transactional);
		Source->Modify();
		TInlineComponentArray<UActorComponent*> Components(Source);
		for (auto* Component : Components) { Component->SetFlags(RF_Transactional); Component->Modify(); }
		Source->Height = OriginalHeight + 1;
		Source->RebuildWallMesh();
	}
	const auto OriginalBounds = Source->LeftWallMeshComponent->CalcBounds(FTransform::Identity);
	for (auto Phase : { EHBWallSplitTestHooks::EFailurePhase::AfterGeometry, EHBWallSplitTestHooks::EFailurePhase::AfterBaseline })
	{
		const int32 Revision = Building->RelationshipGraphRevision;
		TArray<FGuid> RelationIds;
		for (const auto& Relation : Building->ElementRelations) RelationIds.Add(Relation.RelationGuid);
		TGuardValue<EHBWallSplitTestHooks::EFailurePhase> FailureGuard(EHBWallSplitTestHooks::FailurePhase, Phase);
		const auto Result = UEHBBuildingToolset::CommitPlainWallSplit(Building, Source->ElementGuid,
			FVector::Dist2D(Source->LocalStart, Source->LocalEnd) * 0.5f, Revision,
			Source->LocalStart, Source->LocalEnd, Source->Height, Source->Thickness);
		TestFalse(TEXT("Injected failure is not reported as success"), Result.bSucceeded);
		TestEqual(TEXT("Real application reached the injection point"), static_cast<uint8>(EHBWallSplitTestHooks::FailurePhase), static_cast<uint8>(EHBWallSplitTestHooks::EFailurePhase::None));
		TestEqual(TEXT("Failure follows production rollback"), Result.Message, FString(TEXT("SplitFailedRolledBack")));
		TestFalse(TEXT("Original wall actor restored"), Source->IsActorBeingDestroyed());
		TestEqual(TEXT("No leaked live column or replacement walls"), LiveCount(), ActorCount);
		TestEqual(TEXT("Exact topology restored"), Snapshot(), Original);
		TestEqual(TEXT("Graph revision restored"), Building->RelationshipGraphRevision, Revision);
		TArray<FGuid> RestoredIds;
		for (const auto& Relation : Building->ElementRelations) RestoredIds.Add(Relation.RelationGuid);
		TestTrue(TEXT("Original relationship identities restored"), RestoredIds == RelationIds);
		TestEqual(TEXT("Baseline restored"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		TestEqual(TEXT("Room remains closed"), Building->ClosedLoops.Num(), 1);
		TestTrue(TEXT("Source generated bounds restored"), Source->LeftWallMeshComponent->CalcBounds(FTransform::Identity).BoxExtent.Equals(OriginalBounds.BoxExtent));
		TestEqual(TEXT("Earlier edit not accidentally undone"), Source->Height, OriginalHeight + 1);
		TestFalse(TEXT("Failed split cannot be redone"), GEditor->Trans->CanRedo());
	}
	TestTrue(TEXT("Earlier user transaction remains undoable"), GEditor->UndoTransaction(false));
	TestEqual(TEXT("Undo targets preceding edit"), Source->Height, OriginalHeight);
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRectangularOpeningTransferPreviewTest, "EHB.Topology.RectangularOpeningTransferPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBRectangularOpeningTransferPreviewTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0, 37, 0));
	auto* Wall = Fixture.Walls[0];
	const float Split = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd) * 0.5f;
	TArray<AEHB_DoorWindow*> Doors;
	TArray<FTransform> OriginalTransforms;
	for (float Factor : {0.5f, 1.5f})
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		auto* Door = Building->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);
		if (!TestNotNull(TEXT("Real opening spawned"), Door)) return false;
		Fixture.Actors.Add(Door);
		Doors.Add(Door);
		Door->SetRectangularOpeningDimensions(60, 100, 50, 10);
		Door->AttachToBuilding(Building, FTransform::Identity);
		Door->SetActorLocationAndRotation(Wall->GetWorldLocationOnCenterAxisAtDistance(Split * Factor, 50), Wall->GetActorQuat());
		Door->BindToWall(Wall, Split * Factor);
		OriginalTransforms.Add(Door->GetActorTransform());
	}
	const int32 Revision = Building->RelationshipGraphRevision;
	const int32 CutCount = Wall->CutOperations.Num();
	const auto Preview = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, Wall->ElementGuid, Split);
	TestTrue(TEXT("Actual four-point rectangles accepted"), Preview.bSucceeded);
	TestTrue(TEXT("Both opening transfer proposals available"), Preview.bOpeningTransfersComplete);
	TestEqual(TEXT("Two transfers"), Preview.OpeningTransfers.Num(), 2);
	TestFalse(TEXT("Proposal still cannot authorize destructive commit"), Preview.bCommitAvailable);
	TestEqual(TEXT("Preview does not rebuild relations"), Building->RelationshipGraphRevision, Revision);
	TestEqual(TEXT("Preview preserves source cuts"), Wall->CutOperations.Num(), CutCount);
	for (int32 Index = 0; Index < Doors.Num(); ++Index)
		TestTrue(TEXT("Preview preserves actual world pose"), Doors[Index]->GetActorTransform().Equals(OriginalTransforms[Index]));
	if (Wall->DoorWindowConnections.Num() == 2)
	{
		const auto Original = Wall->DoorWindowConnections[0];
		Swap(Wall->DoorWindowConnections[0].LocalOutlinePoints[1], Wall->DoorWindowConnections[0].LocalOutlinePoints[2]);
		TestFalse(TEXT("Crossed four-corner outline rejected"), UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, Wall->ElementGuid, Split).bSucceeded);
		Wall->DoorWindowConnections[0] = Original;
		Doors[0]->SetActorLocation(Doors[0]->GetActorLocation() + FVector(0, 0, 5));
		TestFalse(TEXT("Stale world pose rejected"), UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, Wall->ElementGuid, Split).bSucceeded);
		Doors[0]->SetActorTransform(OriginalTransforms[0]);
	}
	// Verify proposed target frames against real legacy split geometry in this
	// disposable fixture. Detach first because deleting a wall deletes its hosts.
	for (auto* Door : Doors) Door->ClearWallBinding();
	TArray<AEHB_Wall*> NewWalls;
	auto* Pillar = Building->InsertPillarOnWall(Wall, Split, Wall->Height, Wall->Thickness, NewWalls);
	if (Pillar) Fixture.Actors.Add(Pillar);
	for (auto* NewWall : NewWalls) Fixture.Actors.Add(NewWall);
	if (TestEqual(TEXT("Reference split generated two walls"), NewWalls.Num(), 2))
	{
		for (const auto& Transfer : Preview.OpeningTransfers)
		{
			auto* Target = NewWalls[Transfer.bAfterPillar ? 1 : 0];
			TestTrue(TEXT("Predicted target start matches actual geometry"), Target->LocalStart.Equals(Transfer.ProposedWallLocalStart, 0.001));
			TestTrue(TEXT("Predicted target end matches actual geometry"), Target->LocalEnd.Equals(Transfer.ProposedWallLocalEnd, 0.001));
			TestTrue(TEXT("New distance preserves window world location including column inset"),
				Target->GetWorldLocationOnCenterAxisAtDistance(Transfer.NewDistanceFromStart, 50).Equals(Transfer.PreservedWorldTransform.GetLocation(), 0.001));
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningSplitCommitTest, "EHB.Topology.RectangularOpeningSplitCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBOpeningSplitCommitTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0, 37, 0));
	auto* Source = Fixture.Walls[0];
	const FGuid SourceId = Source->ElementGuid;
	const float Split = FVector::Dist2D(Source->LocalStart, Source->LocalEnd) * 0.5f;
	TArray<AEHB_DoorWindow*> Doors;
	TArray<FTransform> Poses;
	TArray<FGuid> CutIds;
	TArray<FGuid> RelationIds;
	for (float Factor : {0.5f, 1.5f})
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		auto* Door = Building->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);
		if (!Door) return false;
		Fixture.Actors.Add(Door); Doors.Add(Door);
		Door->Kind = Factor > 1 ? EEHBDoorWindowElementKind::Door : EEHBDoorWindowElementKind::Window;
		Door->SetRectangularOpeningDimensions(60, 100, Factor > 1 ? 0 : 50, 10);
		Door->AttachToBuilding(Building, FTransform::Identity);
		Door->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(Split * Factor, Door->GetOpeningBottomHeight()), Source->GetActorQuat());
		Door->BindToWall(Source, Split * Factor);
		Door->SetFloorAssignment(Source->FloorIndex, EEHBBuildingFloorElementRole::HostedElement);
		Poses.Add(Door->GetActorTransform());
		const auto* Cut = Source->CutOperations.FindByPredicate([&](const auto& C) { return C.Source.SourceElementGuid == Door->ElementGuid; });
		if (!TestNotNull(TEXT("Real opening cut exists"), Cut)) return false;
		CutIds.Add(Cut->OperationGuid);
		const auto* Relation = Building->ElementRelations.FindByPredicate([&](const auto& R) { return R.Type == EEHBElementRelationType::HostedElement && R.Target.RefersToElement(Door->ElementGuid); });
		if (!TestNotNull(TEXT("Real host relation exists"), Relation)) return false;
		RelationIds.Add(Relation->RelationGuid);
	}
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false); GEditor->SelectActor(Building, true, false);
	auto Commit = [&](bool bMigrate)
	{
		return UEHBBuildingToolset::CommitPlainWallSplit(Building, SourceId, Split, Building->RelationshipGraphRevision,
			Source->LocalStart, Source->LocalEnd, Source->Height, Source->Thickness, bMigrate);
	};
	TestFalse(TEXT("Default mode still rejects openings"), Commit(false).bSucceeded);
	for (auto Phase : {EHBWallSplitTestHooks::EFailurePhase::AfterGeometry, EHBWallSplitTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBWallSplitTestHooks::EFailurePhase> Guard(EHBWallSplitTestHooks::FailurePhase, Phase);
		const auto Failed = Commit(true);
		TestEqual(TEXT("Injected rehost failure rolls back"), Failed.Message, FString(TEXT("SplitFailedRolledBack")));
		for (int32 Index = 0; Index < Doors.Num(); ++Index)
		{
			TestFalse(TEXT("Window survives failed split"), Doors[Index]->IsActorBeingDestroyed());
			TestEqual(TEXT("Original host restored"), Doors[Index]->OwningWallGuid, SourceId);
			TestTrue(TEXT("Original pose restored"), Doors[Index]->GetActorTransform().Equals(Poses[Index], 0.001));
			TestTrue(TEXT("Original cut identity restored"), Source->CutOperations.ContainsByPredicate([&](const auto& C) { return C.OperationGuid == CutIds[Index]; }));
		}
		TestFalse(TEXT("Failed rehost cannot redo"), GEditor->Trans->CanRedo());
	}
	const auto Result = Commit(true);
	TestTrue(*Result.Message, Result.bSucceeded);
	if (Result.bSucceeded)
	{
		for (auto* Element : Building->QueryElements(FEHBElementQuery()))
			if (!Fixture.Actors.Contains(Element)) { Fixture.Actors.Add(Element); Element->SetFlags(RF_Transient); }
		auto CheckTransferred = [&]()
		{
			for (int32 Index = 0; Index < Doors.Num(); ++Index)
			{
				auto* Door = Doors[Index];
				TestFalse(TEXT("Window actor not deleted by source wall"), Door->IsActorBeingDestroyed());
				TestTrue(TEXT("Rehost preserves complete world transform"), Door->GetActorTransform().Equals(Poses[Index], 0.001));
				auto* Target = Cast<AEHB_Wall>(Building->FindElementActorByGuid(Door->OwningWallGuid));
				if (TestNotNull(TEXT("New host resolves"), Target))
				{
					TestTrue(TEXT("New wall differs from deleted source"), Target->ElementGuid != SourceId);
					TestTrue(TEXT("Cut ID preserved on new host"), Target->CutOperations.ContainsByPredicate([&](const auto& C) { return C.OperationGuid == CutIds[Index] && C.Source.SourceElementGuid == Door->ElementGuid; }));
					TestTrue(TEXT("Connection on new host"), Target->DoorWindowConnections.ContainsByPredicate([&](const auto& C) { return C.DoorWindowGuid == Door->ElementGuid; }));
					TestTrue(TEXT("New host coordinates reproduce old pose"), Target->GetWorldLocationOnCenterAxisAtDistance(Door->DistanceFromWallStart, Door->GetOpeningBottomHeight()).Equals(Poses[Index].GetLocation(), 0.001));
				}
				TestTrue(TEXT("Host relation ID and new source preserved"), Building->ElementRelations.ContainsByPredicate([&](const auto& R) { return R.RelationGuid == RelationIds[Index] && R.Source.RefersToElement(Door->OwningWallGuid) && R.Target.RefersToElement(Door->ElementGuid); }));
			}
			TestEqual(TEXT("Room survives with windows"), Building->ClosedLoops.Num(), 1);
			TestEqual(TEXT("Migrated topology baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		};
		CheckTransferred();
		TestTrue(TEXT("Undo rehosting"), GEditor->UndoTransaction());
		for (auto* Door : Doors) TestEqual(TEXT("Undo restores original wall binding"), Door->OwningWallGuid, SourceId);
		GEditor->RedoTransaction(); CheckTransferred();
		GEditor->UndoTransaction();
	}
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallSplitRailingTest, "EHB.Topology.WallSplitAndRailingTransaction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallSplitRailingTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!TestTrue(TEXT("Fixture created"), Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0, 37, 0));
	auto* Source = Fixture.Walls[0];
	const FGuid SourceId = Source->ElementGuid;
	const float Split = FVector::Dist2D(Source->LocalStart, Source->LocalEnd) * 0.5f;
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
	auto* Window = Building->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);
	if (!TestNotNull(TEXT("Window created"), Window)) return false;
	Fixture.Actors.Add(Window);
	Window->SetRectangularOpeningDimensions(60, 100, 50, 10);
	Window->AttachToBuilding(Building, FTransform::Identity);
	Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(Split * 0.5f, 50), Source->GetActorQuat());
	Window->BindToWall(Source, Split * 0.5f);
	Window->SetFloorAssignment(Source->FloorIndex, EEHBBuildingFloorElementRole::HostedElement);
	const FTransform WindowPose = Window->GetActorTransform();
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false); GEditor->SelectActor(Building, true, false);
	const FVector LocalStart = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, SourceId, Split).LocalPillarPosition;
	const FVector End = Building->GetActorTransform().TransformPosition(LocalStart + FVector(0, -250, 0));
	auto Commit = [&](FVector RequestedEnd, float Spacing = 80.0f)
	{
		return UEHBBuildingToolset::CommitWallSplitAndRailing(Building, SourceId, Split, Building->RelationshipGraphRevision,
			Source->LocalStart, Source->LocalEnd, Source->Height, Source->Thickness, RequestedEnd, 100, 5, Spacing);
	};
	auto LiveCount = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AEHBElementActorBase> It(Building->GetWorld()); It; ++It)
			if (!It->IsActorBeingDestroyed() && (It->OwningBuilding == Building || It->GetOwner() == Building)) ++Count;
		return Count;
	};
	const int32 BeforeCount = LiveCount();
	const int32 BeforeRevision = Building->RelationshipGraphRevision;
	TestFalse(TEXT("Slope cannot silently create stair columns"), Commit(End + FVector(0, 0, 10)).bSucceeded);
	TestFalse(TEXT("Invalid spacing rejected"), Commit(End, 0).bSucceeded);
	TestFalse(TEXT("Shallow junction outside column face rejected"), Commit(End + Building->GetActorForwardVector() * 430).bSucceeded);
	TestFalse(TEXT("Crossing opposite wall rejected"), Commit(Building->GetActorTransform().TransformPosition(LocalStart + FVector(0, 800, 0))).bSucceeded);
	TestEqual(TEXT("Preflight leaves actors unchanged"), LiveCount(), BeforeCount);
	TestEqual(TEXT("Preflight leaves relation revision unchanged"), Building->RelationshipGraphRevision, BeforeRevision);
	for (auto Phase : {EHBWallSplitTestHooks::EFailurePhase::AfterRailingSpawn, EHBWallSplitTestHooks::EFailurePhase::AfterGeometry, EHBWallSplitTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBWallSplitTestHooks::EFailurePhase> Guard(EHBWallSplitTestHooks::FailurePhase, Phase);
		TestEqual(TEXT("Combined failure rolls back"), Commit(End).Message, FString(TEXT("SplitFailedRolledBack")));
		TestEqual(TEXT("No orphan railing or column"), LiveCount(), BeforeCount);
		TestEqual(TEXT("Window old host restored"), Window->OwningWallGuid, SourceId);
		TestTrue(TEXT("Window pose restored"), Window->GetActorTransform().Equals(WindowPose, 0.001));
		TestEqual(TEXT("Relation revision restored"), Building->RelationshipGraphRevision, BeforeRevision);
		TestEqual(TEXT("Baseline restored"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
		TestFalse(TEXT("Failed combined edit cannot redo"), GEditor->Trans->CanRedo());
	}
	const auto Result = Commit(End);
	TestTrue(*Result.Message, Result.bSucceeded);
	if (Result.bSucceeded)
	{
		AEHB_Railing* Railing = nullptr;
		for (auto* Element : Building->QueryElements(FEHBElementQuery()))
		{
			if (!Fixture.Actors.Contains(Element)) { Fixture.Actors.Add(Element); Element->SetFlags(RF_Transient); }
			if (auto* Candidate = Cast<AEHB_Railing>(Element)) Railing = Candidate;
		}
		if (TestNotNull(TEXT("Railing created"), Railing))
		{
			const FGuid RailId = Railing->ElementGuid;
			const FGuid PillarId = Railing->StartAnchor.ElementGuid;
			auto CheckCombined = [&]()
			{
				TestEqual(TEXT("One split plus one railing"), LiveCount(), BeforeCount + 3);
				auto* Pillar = Cast<AEHB_Pillar>(Building->FindElementActorByGuid(PillarId));
				if (TestNotNull(TEXT("Start is building column"), Pillar))
				{
					TestEqual(TEXT("Wall-height column"), Pillar->Height, 300.0f);
					TestEqual(TEXT("Wall-width column"), Pillar->Width, 20.0f);
					TestEqual(TEXT("Railing keeps wall floor"), Railing->FloorIndex, Pillar->FloorIndex);
				}
				TestTrue(TEXT("Start railing post omitted"), Railing->bOmitStartPost);
				TestTrue(TEXT("New boundary relation resolves"), Building->ElementRelations.ContainsByPredicate([&](const auto& R) { return R.bEnabled && R.Type == EEHBElementRelationType::BoundaryAttachment && R.Source.RefersToElement(PillarId) && R.Target.RefersToElement(RailId); }));
				TestTrue(TEXT("Window moved to new wall"), Window->OwningWallGuid != SourceId && Building->FindElementActorByGuid(Window->OwningWallGuid));
				TestTrue(TEXT("Window pose preserved"), Window->GetActorTransform().Equals(WindowPose, 0.001));
				TestEqual(TEXT("Room remains closed"), Building->ClosedLoops.Num(), 1);
				TestEqual(TEXT("Combined baseline coherent"), UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false).Status, FName(TEXT("AlreadyInitialized")));
			};
			CheckCombined();
			TestTrue(TEXT("Single undo restores entire edit"), GEditor->UndoTransaction());
			TestEqual(TEXT("Undo actor count"), LiveCount(), BeforeCount);
			TestEqual(TEXT("Undo window host"), Window->OwningWallGuid, SourceId);
			TestFalse(TEXT("Undo removes railing"), IsValid(Building->FindElementActorByGuid(RailId)));
			GEditor->RedoTransaction(); CheckCombined();
			GEditor->UndoTransaction();
		}
	}
	GEditor->SelectNone(false, true, false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(), true, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallDragInteractionTest, "EHB.Topology.WallRailingDragLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallDragInteractionTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	auto* Wall = Fixture.Walls[0];
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> Selection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* A = Cast<AActor>(*It)) Selection.Add(A);
	GEditor->SelectNone(false,true,false); GEditor->SelectActor(Building,true,false);
	FEHBRailingWallDrag Request;
	Request.Capture(Wall, Wall->GetWorldLocationOnCenterAxisAtDistance(280, 120));
	const FVector Normal = Building->GetActorTransform().TransformVectorNoScale(FVector(0,-1,0));
	const FVector End = Request.ProjectEnd(Request.WorldStart + Normal*240 + Building->GetActorForwardVector()*63 + FVector(0,0,100));
	EHBDragAngleSnap::FOptions RailAssist;RailAssist.bFixedLength=true;RailAssist.LengthCm=FVector::Dist2D(Request.WorldStart,End);RailAssist.bFixedDirection=true;RailAssist.DirectionDegrees=(End-Request.WorldStart).Rotation().Yaw;
 const FVector AssistedEnd=EHBDragAngleSnap::Resolve(Request.WorldStart,Request.WorldStart+FVector(1000,400,0),RailAssist);
 TestTrue(TEXT("Exact railing helper reproduces accepted oblique endpoint"),AssistedEnd.Equals(End,0.001));
	TestTrue(TEXT("Projection preserves oblique XY and fixes elevation"), End.Equals(Request.WorldStart+Normal*240+Building->GetActorForwardVector()*63,0.001));
	const auto Overlapping = Request.Execute(Building, Request.WorldStart + Building->GetActorTransform().TransformVectorNoScale(FVector(240,-100,0)),100,8,120,true);
	TestFalse(TEXT("Shallow junction cannot attach beyond the structural column"), Overlapping.bSucceeded);
	TestTrue(TEXT("Source overlap has a specific diagnostic"), Overlapping.Message == TEXT("RailingIntersectsSourceJunction"));
	const int32 Revision = Building->RelationshipGraphRevision;
	const int32 Count = Building->QueryElements(FEHBElementQuery()).Num();
	const int32 UndoCount = GEditor->Trans->GetQueueLength();
	const int32 AppliedTransactions = UndoCount - GEditor->Trans->GetUndoCount();
	TestTrue(TEXT("Common preflight accepts drag"), Request.Execute(Building,End,100,8,120,true).bSucceeded);
	TestEqual(TEXT("Preview creates no transaction"), GEditor->Trans->GetQueueLength(), UndoCount);
	TestEqual(TEXT("Preview creates no actor"), Building->QueryElements(FEHBElementQuery()).Num(), Count);
	TestEqual(TEXT("Preview preserves revision"), Building->RelationshipGraphRevision, Revision);
	Building->SetActorLocation(Building->GetActorLocation()+FVector(0,0,10));
	TestFalse(TEXT("Moved building invalidates snapshot"), Request.Execute(Building,End,100,8,120,true).bSucceeded);
	Building->SetActorTransform(Request.BuildingTransform);
	FEasyHouseEditorMode Mode;
	Mode.ActiveBuilding = Building;
	auto Arm = [&]()
	{
		Mode.bRailingCreationToolActive = Mode.bRailingCreationDragging = true;
		Mode.RailingWallDrag = Mode.HoveredRailingWall = Request;
		Mode.RailingCreationStartLocation = Request.WorldStart;
		Mode.CreationAssist=RailAssist;Mode.RailingCreationMouseLocation = Mode.ResolveCreationFreeEnd(Request.WorldStart,Request.WorldStart+FVector(1000,400,0),nullptr);
	};
	Arm(); Mode.CancelRailingCreation();
	TestFalse(TEXT("Cancel clears drag request"), Mode.RailingWallDrag.IsSet());
	TestFalse(TEXT("Cancel clears hover request"), Mode.HoveredRailingWall.IsSet());
	TestEqual(TEXT("Cancel creates no transaction"), GEditor->Trans->GetQueueLength(), UndoCount);
	Arm(); Mode.RailingWallDrag.Revision--;
	TestFalse(TEXT("Stale release rejects without free-railing fallback"), Mode.FinishRailingCreationDrag());
	TestEqual(TEXT("Stale release creates no actor"), Building->QueryElements(FEHBElementQuery()).Num(), Count);
	TestFalse(TEXT("Rejected release clears drag"), Mode.RailingWallDrag.IsSet());
	Arm();
	TestTrue(TEXT("Actual editor finish uses combined command"), Mode.FinishRailingCreationDrag());
	for (auto* Element : Building->QueryElements(FEHBElementQuery()))
		if (!Fixture.Actors.Contains(Element)) { Fixture.Actors.Add(Element); Element->SetFlags(RF_Transient); }
	TestEqual(TEXT("One mouse release one transaction"), GEditor->Trans->GetQueueLength(), AppliedTransactions+1);
	TestEqual(TEXT("Release creates split and railing"), Building->QueryElements(FEHBElementQuery()).Num(), Count+3);
	auto CheckObliqueGeometry = [&]()
	{
		AEHB_Railing* Created = nullptr;
		for (auto* E : Building->QueryElements(FEHBElementQuery())) if (auto* R = Cast<AEHB_Railing>(E)) Created = R;
		if (!TestNotNull(TEXT("Oblique railing exists"), Created)) return;
		TestTrue(TEXT("Generated path starts at structural column"), Created->GetActorTransform().TransformPosition(Created->LinearStart).Equals(Request.WorldStart,0.01));
		TestTrue(TEXT("Generated path preserves oblique endpoint"), Created->GetActorTransform().TransformPosition(Created->LinearEnd).Equals(End,0.01));
		TestTrue(TEXT("Oblique posts generated"), Created->GeneratedPosts.Num() >= 3);
		TestTrue(TEXT("Source column replaces starting railing post"), Created->bOmitStartPost && Created->StartAnchor.ElementGuid.IsValid());
	};
	CheckObliqueGeometry();
	TestTrue(TEXT("Undo actual drag"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores elements"), Building->QueryElements(FEHBElementQuery()).Num(), Count);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo reapplies entire drag"), Building->QueryElements(FEHBElementQuery()).Num(), Count+3);
	CheckObliqueGeometry();
	GEditor->UndoTransaction();
	Mode.CancelRailingCreation();
	GEditor->SelectNone(false,true,false);
	for (const auto& A : Selection) if (A.IsValid()) GEditor->SelectActor(A.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRailingRepeatedBranchTest, "EHB.Topology.RepeatedWallRailingBranches", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBRailingRepeatedBranchTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	TArray<TWeakObjectPtr<AActor>> Selection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* A = Cast<AActor>(*It)) Selection.Add(A);
	GEditor->SelectNone(false,true,false); GEditor->SelectActor(Building,true,false);
	auto Commit = [&](AEHB_Wall* Wall, FVector Direction, float Distance = 280, bool bPreview = false)
	{
		FEHBRailingWallDrag Request; Request.Capture(Wall,Wall->GetWorldLocationOnCenterAxisAtDistance(Distance,0));
		return Request.Execute(Building,Request.WorldStart + Building->GetActorTransform().TransformVectorNoScale(Direction),100,8,120,bPreview);
	};
	auto Collect = [&]()
	{
		for (auto* E : Building->QueryElements(FEHBElementQuery()))
			if (!Fixture.Actors.Contains(E)) { Fixture.Actors.Add(E); E->SetFlags(RF_Transient); }
	};
	const auto First = Commit(Fixture.Walls[0],FVector(0,250,0));
	TestTrue(*First.Message,First.bSucceeded); Collect();
	AEHB_Railing* Railing = nullptr;
	for (auto* E : Building->QueryElements(FEHBElementQuery())) if (auto* R = Cast<AEHB_Railing>(E)) Railing = R;
	if (!TestNotNull(TEXT("First railing exists"),Railing)) return false;
	const auto* Link = Building->ElementRelations.FindByPredicate([&](const auto& R) { return R.Target.RefersToElement(Railing->ElementGuid); });
	if (!TestNotNull(TEXT("First anchor relation exists"),Link)) return false;
	FEHBPreservedRailing Preserved; Preserved.Capture(Railing,*Link);
	Railing->PostMeshComponent->BuildTreeIfOutdated(false,true);
	Railing->PostMeshComponent->UpdateBounds();
	const FBox Bounds = Railing->GetComponentsBoundingBox(true);
	auto CheckOriginal = [&]()
	{
		TestTrue(TEXT("Existing railing IDs, geometry records and semantic relation preserved; refreshed revisions valid"),Preserved.IsPreserved(true));
		const FBox After = Railing->GetComponentsBoundingBox(true);
		if (!Bounds.Min.Equals(After.Min,0.001) || !Bounds.Max.Equals(After.Max,0.001)) AddInfo(FString::Printf(TEXT("Bounds before %s / %s after %s / %s"),*Bounds.Min.ToString(),*Bounds.Max.ToString(),*After.Min.ToString(),*After.Max.ToString()));
		TestTrue(TEXT("Existing generated bounds preserved"),Bounds.Min.Equals(After.Min,0.001) && Bounds.Max.Equals(After.Max,0.001));
	};
	const int32 Before = Building->QueryElements(FEHBElementQuery()).Num();
	TestTrue(TEXT("Preview accepts retained ordinary railing"),Commit(Fixture.Walls[2],FVector(0,250,0),280,true).bSucceeded);
	TestEqual(TEXT("Preview creates no elements"),Building->QueryElements(FEHBElementQuery()).Num(),Before);
	Railing->EndAnchor.ElementGuid = Fixture.Pillars[0]->ElementGuid;
	TestFalse(TEXT("Unsupported second anchor rejected"),Commit(Fixture.Walls[2],FVector(0,250,0),280,true).bSucceeded);
	Railing->EndAnchor.ElementGuid.Invalidate();
	Railing->HostedStairGuid = FGuid::NewGuid();
	TestFalse(TEXT("Stair relationship is not relaxed"),Commit(Fixture.Walls[2],FVector(0,250,0),280,true).bSucceeded);
	Railing->HostedStairGuid.Invalidate();
	TestEqual(TEXT("Crossing first railing rejected"),Commit(Fixture.Walls[1],FVector(-500,0,0),100,true).Message,FString(TEXT("RailingIntersectsRailing")));
	for (auto Phase : {EHBWallSplitTestHooks::EFailurePhase::AfterRailingSpawn,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBWallSplitTestHooks::EFailurePhase> Guard(EHBWallSplitTestHooks::FailurePhase,Phase);
		TestEqual(TEXT("Second branch failure rolls back"),Commit(Fixture.Walls[2],FVector(0,250,0)).Message,FString(TEXT("SplitFailedRolledBack")));
		CheckOriginal();
		TestEqual(TEXT("Failed branch leaves original count"),Building->QueryElements(FEHBElementQuery()).Num(),Before);
		TestFalse(TEXT("Failed branch cannot redo"),GEditor->Trans->CanRedo());
	}
	const auto Second = Commit(Fixture.Walls[2],FVector(0,250,0));
	TestTrue(*Second.Message,Second.bSucceeded); Collect(); CheckOriginal();
	if(Second.bSucceeded)TestTrue(TEXT("Second branch advances the first command lineage"),Second.CommittedEdit.Sequence==2&&Second.CommittedEdit.ParentStateId==First.CommittedEdit.StateId);
	if (Second.bSucceeded)
	{
		TestTrue(TEXT("Undo only second branch"),GEditor->UndoTransaction()); CheckOriginal();
		TestEqual(TEXT("First branch survives undo"),Building->QueryElements(FEHBElementQuery()).Num(),Before);
		GEditor->RedoTransaction(); CheckOriginal();
		TestEqual(TEXT("Redo second branch"),Building->QueryElements(FEHBElementQuery()).Num(),Before+3);
		const auto Third = Commit(Fixture.Walls[1],FVector(250,0,0),100);
		TestTrue(*Third.Message,Third.bSucceeded); Collect(); CheckOriginal();
		if(Third.bSucceeded)TestTrue(TEXT("Third branch advances restored second command lineage"),Third.CommittedEdit.Sequence==3&&Third.CommittedEdit.ParentStateId==Second.CommittedEdit.StateId);
		TestEqual(TEXT("Room still closed after repeated branches"),Building->ClosedLoops.Num(),1);
		if (Third.bSucceeded) GEditor->UndoTransaction();
		GEditor->UndoTransaction();
	}
	GEditor->UndoTransaction();
	GEditor->SelectNone(false,true,false);
	for (const auto& A : Selection) if (A.IsValid()) GEditor->SelectActor(A.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPrepareRailingTest, "EHB.Topology.PrepareWallRailingEntry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBPrepareRailingTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)) return false;
	auto* Building = Fixture.Building;
	TArray<TWeakObjectPtr<AActor>> Selection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* A=Cast<AActor>(*It)) Selection.Add(A);
	GEditor->SelectNone(false,true,false);
	auto Prepare = [&](bool bApply)
	{
		FEHBTopologyMigrationResult Result;
		TestTrue(TEXT("Editor entry returns valid structured result"),FJsonObjectConverter::JsonObjectStringToUStruct(UEHBBuildingToolset::PrepareTopologyMigration(Building,bApply),&Result));
		return Result;
	};
	TestEqual(TEXT("Unselected target rejected"),Prepare(true).Status,FName(TEXT("TargetNotSelected")));
	GEditor->SelectActor(Building,true,false);
	const int32 Count = Building->QueryElements(FEHBElementQuery()).Num();
	const int32 Revision = Building->RelationshipGraphRevision;
	const int32 Before = GEditor->Trans->GetQueueLength();
	TestEqual(TEXT("Preparation preview ready"),Prepare(false).Status,FName(TEXT("Ready")));
	TestEqual(TEXT("Preview no undo entry"),GEditor->Trans->GetQueueLength(),Before);
	TestEqual(TEXT("Preview no baseline mutation"),Building->TopologyMigrationBaseline.Version,0);
	{
		FScopedTransaction Other(NSLOCTEXT("EHBTest","PrepareNested","Other edit"));
		TestEqual(TEXT("Preparation cannot nest in other edit"),Prepare(true).Status,FName(TEXT("RequiresIndependentEditorTransaction")));
	}
	TestEqual(TEXT("Initial preparation commits"),Prepare(true).Status,FName(TEXT("Initialized")));
	const int32 InitializedQueue = GEditor->Trans->GetQueueLength();
	TestEqual(TEXT("Repeated preparation is read-only"),Prepare(true).Status,FName(TEXT("AlreadyInitialized")));
	TestEqual(TEXT("Repeated click no empty undo entry"),GEditor->Trans->GetQueueLength(),InitializedQueue);
	const auto Stored = Building->TopologyMigrationBaseline;
	Building->TopologyMigrationBaseline.Version = 99;
	TestEqual(TEXT("Future version rejected"),Prepare(true).Status,FName(TEXT("UnsupportedVersion")));
	TestEqual(TEXT("Future version not overwritten"),Building->TopologyMigrationBaseline.Version,99);
	Building->TopologyMigrationBaseline = Stored;
	Building->TopologyMigrationBaseline.Nodes[0].LocalPosition.X += 10;
	TestEqual(TEXT("Diverged record rejected"),Prepare(true).Status,FName(TEXT("SourceChanged")));
	TestTrue(TEXT("Diverged record not overwritten"),Building->TopologyMigrationBaseline.Nodes[0].LocalPosition != Stored.Nodes[0].LocalPosition);
	TestEqual(TEXT("Rejection no empty undo entry"),GEditor->Trans->GetQueueLength(),InitializedQueue);
	Building->TopologyMigrationBaseline = Stored;
	TestEqual(TEXT("Preparation leaves actors alone"),Building->QueryElements(FEHBElementQuery()).Num(),Count);
	TestEqual(TEXT("Preparation leaves relation revision alone"),Building->RelationshipGraphRevision,Revision);
	TestTrue(TEXT("Undo first preparation"),GEditor->UndoTransaction());
	TestEqual(TEXT("Undo removes new baseline"),Building->TopologyMigrationBaseline.Version,0);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo restores coherent baseline"),Prepare(false).Status,FName(TEXT("AlreadyInitialized")));
	GEditor->UndoTransaction();
	GEditor->SelectNone(false,true,false);
	for(const auto& A:Selection) if(A.IsValid()) GEditor->SelectActor(A.Get(),true,false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRailingCutTest, "EHB.Topology.WallRailingCutGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallRailingCutTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor->GetEditorWorldContext().World())) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	auto* Wall = Fixture.Walls[0];
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	TArray<TWeakObjectPtr<AActor>> Selection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* A = Cast<AActor>(*It)) Selection.Add(A);
	GEditor->SelectNone(false,true,false); GEditor->SelectActor(Building,true,false);
	FEHBRailingWallDrag Request; Request.Capture(Wall,Wall->GetWorldLocationOnCenterAxisAtDistance(280,100));
	const FVector End = Request.WorldStart + Building->GetActorTransform().TransformVectorNoScale(FVector(180,-180,0));
	const int32 Count = Building->QueryElements(FEHBElementQuery()).Num();
	TestFalse(TEXT("Short first post cannot penetrate source wall"), Request.Execute(Building,End,100,5,5,true).bSucceeded);
	TestTrue(TEXT("45 degree preview plans cut without actors"),Request.Execute(Building,End,100,5,120,true).bSucceeded);
	TestEqual(TEXT("Preview preserves actor count"),Building->QueryElements(FEHBElementQuery()).Num(),Count);
	{
		TGuardValue<EHBWallSplitTestHooks::EFailurePhase> Guard(EHBWallSplitTestHooks::FailurePhase,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline);
		TestEqual(TEXT("Cut command rollback uses production recovery"),Request.Execute(Building,End,100,5,120,false).Message,FString(TEXT("SplitFailedRolledBack")));
		TestEqual(TEXT("Failed cut creates no orphan"),Building->QueryElements(FEHBElementQuery()).Num(),Count);
	}
	const FGuid OriginalRoomGuid = Building->ClosedLoops[0].LoopGuid;
	const auto Result = Request.Execute(Building,End,100,5,120,false);
	TestTrue(*Result.Message,Result.bSucceeded);
	for(auto* E:Building->QueryElements(FEHBElementQuery())) if(!Fixture.Actors.Contains(E)){Fixture.Actors.Add(E);E->SetFlags(RF_Transient);}
	FGuid CutRailGuid;
	auto CheckGeometry = [&]()
	{
		TestEqual(TEXT("Wall subdivision preserves room count"),Building->ClosedLoops.Num(),1);
		if(Building->ClosedLoops.Num()==1) TestEqual(TEXT("Room identity survives split and redo"),Building->ClosedLoops[0].LoopGuid,OriginalRoomGuid);
		TestEqual(TEXT("Wall subdivision preserves room identity"), Building->ClosedLoops.Num(), 1);
		if(Building->ClosedLoops.Num()==1) TestEqual(TEXT("Same room after cut and redo"), Building->ClosedLoops[0].LoopGuid, OriginalRoomGuid);
		AEHB_Railing* Rail=nullptr;
		for(auto* E:Building->QueryElements(FEHBElementQuery())) if(auto* R=Cast<AEHB_Railing>(E)) Rail=R;
		if(!TestNotNull(TEXT("Cut railing exists"),Rail)) return;
		if(CutRailGuid.IsValid()) TestEqual(TEXT("Railing identity survives redo"),Rail->ElementGuid,CutRailGuid);
		CutRailGuid=Rail->ElementGuid;
		TestTrue(TEXT("Cut is persisted on railing"),Rail->bHasStartWallCut);
		TestTrue(TEXT("Logical anchor remains column center"),Rail->GetActorTransform().TransformPosition(Rail->LinearStart).Equals(Request.WorldStart,0.01));
		const auto* Mesh=Rail->RailGeneratedMeshComponent->GetProcMeshSection(0);
		if(!TestNotNull(TEXT("Trimmed rail mesh exists"),Mesh)) return;
		const FPlane Plane(Rail->StartWallCutPoint,Rail->StartWallCutNormal);
		int32 OnPlane=0;
		for(const auto& V:Mesh->ProcVertexBuffer)
		{
			const double Distance=Plane.PlaneDot(V.Position);
			TestTrue(TEXT("No rail vertex lies inside source wall"),Distance>=-0.001);
			if(FMath::Abs(Distance)<0.001) ++OnPlane;
			TestTrue(TEXT("Generated normal stays finite"),!V.Normal.ContainsNaN());
		}
		TestTrue(TEXT("Rail touches the wall cut face"),OnPlane>=6);
		for(const auto& Post:Rail->GeneratedPosts) if(!Post.bSuppressInstance)
		{
			const FVector X=Post.LocalRotation.Vector()*Rail->PostWidth*0.5;
			const FVector Y=FVector::CrossProduct(FVector::UpVector,X);
			for(double A:{-1.0,1.0}) for(double B:{-1.0,1.0})
				TestTrue(TEXT("Visible post corners clear wall"),Plane.PlaneDot(Post.LocalBaseLocation+A*X+B*Y)>0);
		}
	};
	if(Result.bSucceeded)
	{
		CheckGeometry();
		TestTrue(TEXT("Undo cut assembly"),GEditor->UndoTransaction());
		TestEqual(TEXT("Undo restores original building"),Building->QueryElements(FEHBElementQuery()).Num(),Count);
		GEditor->RedoTransaction(); CheckGeometry();
		GEditor->UndoTransaction();
	}
	GEditor->SelectNone(false,true,false);for(const auto& A:Selection)if(A.IsValid())GEditor->SelectActor(A.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomFloorWallDragTest,"EHB.Topology.RoomFloorWallDrag",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomFloorWallDragTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building; auto* Wall=Fixture.Walls[0];
	Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Floor)return false;Fixture.Actors.Add(Floor);
	if(!Floor->ConfigureFromRoomLoop(Building,Building->GetClosedLoopsByFloor(1)[0],0,true))return false;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))PreviousSelection.Add(A);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Wall,true,false);
	FEasyHouseEditorMode Mode;Mode.SelectedWall=Wall;Mode.SetRoomFloorWallMoveEnabled(true);
	FEditorViewportClient Client(&GLevelEditorModeTools());Client.SetCurrentWidgetAxis(EAxisList::X);
	const auto OriginalPolygon=Floor->FloorRegions[0].OuterPolygon;
	const FGuid RoomId=Floor->RoomLoopGuid;
	const FVector WorldStep=Building->GetActorTransform().TransformVectorNoScale(FVector(0,-20,0));
	auto DragStep=[&](){FVector Move=WorldStep,Scale=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;return Mode.InputDelta(&Client,nullptr,Move,Rotation,Scale);};
	auto OriginalIntact=[&]()
	{
		TestTrue(TEXT("Original endpoint remains"),Fixture.Pillars[0]->GetElementLocalTransform().GetLocation().Equals(FVector::ZeroVector,0.001));
		TestTrue(TEXT("Original floor remains"),Floor->FloorRegions[0].OuterPolygon==OriginalPolygon);
		TestTrue(TEXT("Floor provenance remains valid"),Floor->IsRecordedOutlineUnchanged());
		TestEqual(TEXT("Room identity remains"),Floor->RoomLoopGuid,RoomId);
	};
	const int32 QueueBefore=GEditor->Trans->GetQueueLength();
	TestTrue(TEXT("Mode owns linked drag tracking"),Mode.StartTracking(&Client,nullptr));
	TestFalse(TEXT("Begin tracking creates no transaction"),GEditor->IsTransactionActive());
	for(int32 I=0;I<4;++I)TestTrue(TEXT("Mode consumes draft delta"),DragStep());
	TestTrue(TEXT("Draft is valid with wall alone selected"),Mode.RoomFloorWallDrag.Feedback.bSucceeded);
	TestEqual(TEXT("Preview includes three incident walls"),Mode.RoomFloorWallDrag.Geometry.DirectWallGuids.Num(),3);
	TestTrue(TEXT("Gizmo follows world draft"),Mode.GetWidgetLocation().Equals(Wall->GetActorLocation()+WorldStep*4,0.001));
	OriginalIntact();TestEqual(TEXT("Preview adds no undo records"),GEditor->Trans->GetQueueLength(),QueueBefore);
	TestTrue(TEXT("Mode applies on release"),Mode.EndTracking(&Client,nullptr));
	TestFalse(TEXT("Release clears draft"),Mode.RoomFloorWallDrag.IsSet());
	Wall->PostEditMove(true); // Native gizmo completion may still notify the selected actor.
	TestEqual(TEXT("Native post-move notification keeps baseline coherent"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	TestTrue(TEXT("Release moves both wall endpoints"),Fixture.Pillars[0]->GetElementLocalTransform().GetLocation().Equals(FVector(0,-80,0),0.001)&&Fixture.Pillars[1]->GetElementLocalTransform().GetLocation().Equals(FVector(600,-80,0),0.001));
	TestTrue(TEXT("Release updates actual floor mesh"),Floor->MeshComponent->CalcBounds(FTransform::Identity).GetBox().Min.Y < -79);
	TestTrue(TEXT("One undo restores linked drag"),GEditor->UndoTransaction());OriginalIntact();
	TestTrue(TEXT("One redo restores drag result"),GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores floor mesh"),Floor->MeshComponent->CalcBounds(FTransform::Identity).GetBox().Min.Y < -79);
	GEditor->UndoTransaction(false);
	const int32 BeforeCancel=GEditor->Trans->GetQueueLength();
	Mode.StartTracking(&Client,nullptr);DragStep();
	TestTrue(TEXT("Escape consumed by draft"),Mode.InputKey(&Client,nullptr,EKeys::Escape,IE_Pressed));
	DragStep();Mode.EndTracking(&Client,nullptr);OriginalIntact();
	TestEqual(TEXT("Escape does not add an undo entry"),GEditor->Trans->GetQueueLength(),BeforeCancel);
	Mode.StartTracking(&Client,nullptr);DragStep();Mode.SetRoomFloorWallMoveEnabled(false);Mode.EndTracking(&Client,nullptr);OriginalIntact();
	Mode.SetRoomFloorWallMoveEnabled(true);Mode.StartTracking(&Client,nullptr);DragStep();
	AddExpectedError(TEXT("EHB room wall drag rejected: NodeMoveFailedRolledBack"),EAutomationExpectedErrorFlags::Contains,1);
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> Guard(EHBNodeMoveTestHooks::FailurePhase,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline);
		Mode.EndTracking(&Client,nullptr);
		TestEqual(TEXT("Mode release reached atomic failure seam"),static_cast<uint8>(EHBNodeMoveTestHooks::FailurePhase),static_cast<uint8>(EHBNodeMoveTestHooks::EFailurePhase::None));
	}
	OriginalIntact();TestFalse(TEXT("Failed drag cannot redo"),GEditor->Trans->CanRedo());
	Mode.StartTracking(&Client,nullptr);DragStep();GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);Mode.SyncWallSelectionFromEditor();
	TestFalse(TEXT("Selection change discards draft"),Mode.RoomFloorWallDrag.IsSet());OriginalIntact();
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Wall,true,false);Mode.SelectedWall=Wall;
	const auto SavedBaseline=Building->TopologyMigrationBaseline;Building->TopologyMigrationBaseline={};
	Mode.StartTracking(&Client,nullptr);DragStep();
	TestEqual(TEXT("Missing preparation stays rejected throughout drag"),Mode.RoomFloorWallDrag.Feedback.Message,FString(TEXT("MigrationRequired")));
	AddExpectedError(TEXT("EHB room wall drag rejected: MigrationRequired"),EAutomationExpectedErrorFlags::Contains,1);
	Mode.EndTracking(&Client,nullptr);OriginalIntact();Building->TopologyMigrationBaseline=SavedBaseline;
	GEditor->SelectActor(Floor,true,false);
	Mode.StartTracking(&Client,nullptr);DragStep();
	TestFalse(TEXT("Multiple selected actors cannot authorize wall drag"),Mode.RoomFloorWallDrag.Feedback.bSucceeded);
	AddExpectedError(TEXT("EHB room wall drag rejected: TargetNotSelected"),EAutomationExpectedErrorFlags::Contains,1);
	Mode.EndTracking(&Client,nullptr);OriginalIntact();
	GEditor->SelectActor(Floor,false,false);
	Mode.StartTracking(&Client,nullptr);FVector Vertical(0,0,30),NoScale=FVector::ZeroVector;FRotator NoRotation=FRotator::ZeroRotator;
	Mode.InputDelta(&Client,nullptr,Vertical,NoRotation,NoScale);
	TestEqual(TEXT("Vertical transform is consumed and rejected"),Mode.RoomFloorWallDrag.Feedback.Message,FString(TEXT("InvalidHorizontalDelta")));
	AddExpectedError(TEXT("EHB room wall drag rejected: InvalidHorizontalDelta"),EAutomationExpectedErrorFlags::Contains,1);
	Mode.EndTracking(&Client,nullptr);OriginalIntact();

	GEditor->SelectNone(false,true,false);for(const auto& A:PreviousSelection)if(A.IsValid())GEditor->SelectActor(A.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomFloorNodeMoveTest, "EHB.Topology.RoomFloorNodeMove", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBRoomFloorNodeMoveTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)) return false;
	auto* Building = Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
	auto* Floor = Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(), Building->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Floor) return false;
	Fixture.Actors.Add(Floor);
	const auto Room = Building->GetClosedLoopsByFloor(1)[0];
	if (!TestTrue(TEXT("Configure ground room"), Floor->ConfigureFromRoomLoop(Building, Room, 0, true))) return false;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor = Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false, true, false); GEditor->SelectActor(Building, true, false);
	const FGuid NodeId = Fixture.Pillars[0]->ElementGuid;
	const FVector Target(-100,20,0);
	auto Commit = [&](FVector From, FVector To, bool bPreview=false)
	{ return UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Building, NodeId, From, To, bPreview); };
	auto Snapshot = [&]()
	{
		FString Json, Part;
		FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building), Json);
		for (auto* Element : Building->QueryElements(FEHBElementQuery()))
		{
			Json += Element->ElementGuid.ToString();
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for (auto* Mesh : Meshes) for (int32 I=0; I<Mesh->GetNumSections(); ++I)
				if (const auto* Section = Mesh->GetProcMeshSection(I))
				{
					Json += FString::FromInt(Section->bEnableCollision) + FString::FromInt(Section->bSectionVisible);
					for (const auto& V : Section->ProcVertexBuffer) Json += V.Position.ToString() + V.Normal.ToString() + V.UV0.ToString();
					for (uint32 Index : Section->ProcIndexBuffer) Json += FString::FromInt(Index) + TEXT(",");
				}
		}
		for (const auto& Region : Floor->FloorRegions) { FJsonObjectConverter::UStructToJsonObjectString(Region, Part); Json += Part; }
		Json += Floor->RoomLoopGuid.ToString() + FString::FromInt(Floor->RoomFloorIndex)
			+ FString::FromInt(static_cast<int32>(Floor->OutlineSource)) + (Floor->IsRecordedOutlineUnchanged() ? TEXT("current") : TEXT("modified"));
		return Json;
	};
	const FString Original = Snapshot();
	const int32 UndoCount = GEditor->Trans->GetQueueLength();
	TestTrue(TEXT("Full preflight succeeds"), Commit(FVector::ZeroVector,Target,true).bSucceeded);
	TestEqual(TEXT("Preview changes no geometry or bindings"),Snapshot(),Original);
	TestEqual(TEXT("Preview creates no transaction"),GEditor->Trans->GetQueueLength(),UndoCount);
	TestFalse(TEXT("Basic command does not implicitly change floors"),UEHBBuildingToolset::CommitBasicNodeMove(Building,NodeId,FVector::ZeroVector,Target).bSucceeded);
	{
		FScopedTransaction Outer(NSLOCTEXT("EHBTests","NestedFloorMove","Nested floor move"));
		TestEqual(TEXT("Nested mutation rejected"),Commit(FVector::ZeroVector,Target).Message,FString(TEXT("RequiresIndependentEditorTransaction")));
		Outer.Cancel();
	}
	TestEqual(TEXT("Changing a true corner into a subdivision rejected before mutation"),Commit(FVector::ZeroVector,FVector(300,250,0),true).Message,FString(TEXT("RoomIdentityChangeUnsupported")));
	TestEqual(TEXT("Identity rejection leaves original result"),Snapshot(),Original);
	Floor->RoomFloorIndex=2;
	TestFalse(TEXT("Stale room-floor binding rejects follow"),Commit(FVector::ZeroVector,Target).bSucceeded);
	Floor->RoomFloorIndex=1;
	Floor->OutlineSource=EEHBOutlineSource::ManualOrUnclassified;
	TestFalse(TEXT("Manual generation intent rejects follow"),Commit(FVector::ZeroVector,Target).bSucceeded);
	Floor->OutlineSource=EEHBOutlineSource::RoomBoundary;
	TestEqual(TEXT("Rejected preflights create no transaction"),GEditor->Trans->GetQueueLength(),UndoCount);
	const auto Applied = Commit(FVector::ZeroVector,Target);
	TestTrue(*Applied.Message,Applied.bSucceeded);
	TestTrue(TEXT("Floor contains moved corner"),Floor->FloorRegions[0].OuterPolygon.ContainsByPredicate([&](const FVector& P){return P.Equals(Target,0.001);}));
	TestTrue(TEXT("Floor mesh reaches moved corner"),Floor->MeshComponent->CalcBounds(FTransform::Identity).GetBox().Min.X < -99);
	TestEqual(TEXT("Room ID preserved"),Floor->RoomLoopGuid,Room.LoopGuid);
	TestTrue(TEXT("Generated basis updated for next edit"),Floor->IsRecordedOutlineUnchanged());
	const FString Moved = Snapshot();
	TestTrue(TEXT("Undo complete linked edit"),GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores actual mesh and bindings"),Snapshot(),Original);
	TestEqual(TEXT("Undo baseline coherent"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	TestTrue(TEXT("Redo complete linked edit"),GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores actual mesh and bindings"),Snapshot(),Moved);
	TestFalse(TEXT("Stale target rejected"),Commit(FVector::ZeroVector,Target).bSucceeded);
	TestEqual(TEXT("Rejected stale edit unchanged"),Snapshot(),Moved);
	GEditor->UndoTransaction(false);
	const auto Regions = Floor->FloorRegions;
	Floor->FloorRegions[0].OuterPolygon[0].X += 5;
	const FString Manual = Snapshot();
	TestFalse(TEXT("Direct manual edits never overwritten"),Commit(FVector::ZeroVector,Target).bSucceeded);
	TestEqual(TEXT("Manual floor and node untouched"),Snapshot(),Manual);
	Floor->FloorRegions = Regions;
	// A real preceding edit must survive either failure seam.
	{
		FScopedTransaction Sentinel(NSLOCTEXT("EHBTests","BeforeFailedFloorMove","Edit before failed floor move"));
		Floor->Modify(); Floor->VisualOffset += 0.25f; Floor->RebuildFloorMesh();
	}
	const FString BeforeFailure = Snapshot();
	for (auto Phase : { EHBNodeMoveTestHooks::EFailurePhase::AfterFloors, EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline })
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> Guard(EHBNodeMoveTestHooks::FailurePhase,Phase);
		const auto Failed = Commit(FVector::ZeroVector,Target);
		TestFalse(TEXT("Injected failure reported"),Failed.bSucceeded);
		TestEqual(TEXT("Production rollback invoked"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
		TestEqual(TEXT("Injection reached"),static_cast<uint8>(EHBNodeMoveTestHooks::FailurePhase),static_cast<uint8>(EHBNodeMoveTestHooks::EFailurePhase::None));
		TestEqual(TEXT("Failure restores actual floor wall pillar mesh and bindings"),Snapshot(),BeforeFailure);
		TestEqual(TEXT("Failure restores baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
		TestFalse(TEXT("Failed edit cannot be redone"),GEditor->Trans->CanRedo());
	}
	TestTrue(TEXT("Earlier transaction survives"),GEditor->UndoTransaction(false));
	TestEqual(TEXT("Earlier undo restores initial state"),Snapshot(),Original);
	auto MoveWall = [&](FVector Delta, bool bPreview=false)
	{ return UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),Delta,bPreview); };
	TestTrue(TEXT("Whole wall preflight ready"),MoveWall(FVector(0,-80,0),true).bSucceeded);
	TestEqual(TEXT("Whole wall preview leaves geometry untouched"),Snapshot(),Original);
	TestEqual(TEXT("Rendered inset endpoints are not mistaken for pillar centers"),UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,Fixture.Walls[0]->LocalStart,FVector(600,0,0),FVector(0,-80,0),true).Message,FString(TEXT("StalePosition")));
	TestFalse(TEXT("Vertical whole-wall movement rejects"),MoveWall(FVector(0,0,20)).bSucceeded);
	TestFalse(TEXT("Final collision rejects whole wall"),MoveWall(FVector(600,500,0)).bSucceeded);
	TestEqual(TEXT("Rejected whole-wall changes preserve all geometry"),Snapshot(),Original);
	const auto WallMove = MoveWall(FVector(0,-80,0));
	TestTrue(*WallMove.Message,WallMove.bSucceeded);
	TestTrue(TEXT("Start column moves in building frame"),Fixture.Pillars[0]->GetActorLocation().Equals(Building->GetActorTransform().TransformPosition(FVector(0,-80,0)),0.001));
	TestTrue(TEXT("End column moves in building frame"),Fixture.Pillars[1]->GetActorLocation().Equals(Building->GetActorTransform().TransformPosition(FVector(600,-80,0)),0.001));
	for (const FVector P : {FVector(0,-80,0),FVector(600,-80,0)})
		TestTrue(TEXT("Floor receives both final wall corners"),Floor->FloorRegions[0].OuterPolygon.ContainsByPredicate([&](const FVector& V){return V.Equals(P,0.001);}));
	TestTrue(TEXT("Whole-wall floor provenance current"),Floor->IsRecordedOutlineUnchanged());
	TestEqual(TEXT("Whole-wall baseline coherent"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	const FString WallMoved = Snapshot();
	TestTrue(TEXT("One undo restores both wall ends and floor"),GEditor->UndoTransaction());
	TestEqual(TEXT("Whole-wall undo restores actual geometry"),Snapshot(),Original);
	TestTrue(TEXT("One redo reapplies both wall ends and floor"),GEditor->RedoTransaction());
	TestEqual(TEXT("Whole-wall redo restores actual geometry"),Snapshot(),WallMoved);
	GEditor->UndoTransaction(false);
	for (auto Phase : { EHBNodeMoveTestHooks::EFailurePhase::AfterFloors, EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline })
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> Guard(EHBNodeMoveTestHooks::FailurePhase,Phase);
		const auto Failed=MoveWall(FVector(0,-80,0));
		TestEqual(TEXT("Whole-wall failure uses production recovery"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
		TestEqual(TEXT("Whole-wall failure restores both ends and floor geometry"),Snapshot(),Original);
		TestEqual(TEXT("Whole-wall failure restores baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
		TestFalse(TEXT("Failed whole-wall edit cannot redo"),GEditor->Trans->CanRedo());
	}

	GEditor->SelectNone(false,true,false);
	for (const auto& Actor : PreviousSelection) if (Actor.IsValid()) GEditor->SelectActor(Actor.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDefinitionBatchGenerationTest,"EHB.Geometry.DefinitionBatchGeneration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDefinitionBatchGenerationTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	struct FMeshState{FTransform Transform;TArray<FTransform> ComponentTransforms;TArray<FProcMeshSection> Sections;};
	auto Capture=[&](){TMap<FGuid,FMeshState> Data;for(auto* Element:Building->QueryElements(FEHBElementQuery())){auto& State=Data.Add(Element->ElementGuid);State.Transform=Element->GetElementLocalTransform();TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);for(auto* Mesh:Meshes){State.ComponentTransforms.Add(Mesh->GetRelativeTransform());for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))State.Sections.Add(*Section);}}return Data;};
	auto Same=[](const auto& A,const auto& B){if(A.Num()!=B.Num())return false;for(const auto& Pair:A){const auto* Other=B.Find(Pair.Key);if(!Other)return false;const auto& X=Pair.Value;const auto& Y=*Other;if(!X.Transform.Equals(Y.Transform,0.001)||X.ComponentTransforms.Num()!=Y.ComponentTransforms.Num()||X.Sections.Num()!=Y.Sections.Num())return false;for(int32 I=0;I<X.ComponentTransforms.Num();++I)if(!X.ComponentTransforms[I].Equals(Y.ComponentTransforms[I],0.001))return false;for(int32 I=0;I<X.Sections.Num();++I){const auto& U=X.Sections[I];const auto& V=Y.Sections[I];if(U.ProcIndexBuffer!=V.ProcIndexBuffer||U.ProcVertexBuffer.Num()!=V.ProcVertexBuffer.Num())return false;for(int32 J=0;J<U.ProcVertexBuffer.Num();++J){const auto& P=U.ProcVertexBuffer[J];const auto& Q=V.ProcVertexBuffer[J];if(!P.Position.Equals(Q.Position,0.001)||!P.Normal.Equals(Q.Normal,0.0001)||!P.UV0.Equals(Q.UV0,0.00001)||!P.Tangent.TangentX.Equals(Q.Tangent.TangentX,0.0001)||P.Tangent.bFlipTangentY!=Q.Tangent.bFlipTangentY)return false;}}}return true;};
	FEHBPreparedWallNodeDefinitions Source;TestTrue(TEXT("Current source capture requires no migration write"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Building,Source).bSucceeded);TestEqual(TEXT("Capture leaves baseline unprepared"),Building->TopologyMigrationBaseline.Version,0);
	FEHBNodeMoveRequest Request;Request.NodeGuid=Fixture.Pillars[0]->ElementGuid;Request.ExpectedPosition=Fixture.Pillars[0]->GetElementLocalTransform().GetLocation();Request.TargetPosition=Request.ExpectedPosition+FVector(-30,-40,0);
	const auto Draft=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Request});if(!TestTrue(TEXT("Batch draft ready"),Draft.bSucceeded))return false;
	const auto Original=Capture();const auto BeforePose=Building->RefreshNodeDefinitionDraft(Draft);TestFalse(TEXT("Cannot generate against poses that were not applied"),BeforePose.bSucceeded);TestTrue(TEXT("Source mismatch has no generation or writes"),BeforePose.PillarsVisited==0&&BeforePose.WallRefreshCalls==0&&BeforePose.DefinitionSolveCalls==0&&Same(Capture(),Original));
	Fixture.Pillars[0]->SetActorRelativeLocation(Request.TargetPosition);const auto Before=Capture();
	auto Bad=Draft;const FGuid Duplicate=Bad.UpdatePlan.WallGuids[0];Bad.UpdatePlan.WallGuids.Add(Duplicate);const auto Rejected=Building->RefreshNodeDefinitionDraft(Bad);TestFalse(TEXT("Malformed update plan rejected"),Rejected.bSucceeded);TestTrue(TEXT("Rejected plan leaves all live geometry untouched"),Rejected.DefinitionSolveCalls==0&&Same(Capture(),Before));
	Bad=Draft;Bad.Definitions.Walls[0].Thickness+=2;TestFalse(TEXT("Changed wall parameters reject before generation"),Building->RefreshNodeDefinitionDraft(Bad).bSucceeded);TestTrue(TEXT("Parameter mismatch preserves live geometry"),Same(Capture(),Before));
	for(auto* Pillar:Fixture.Pillars){Pillar->ConnectedWallGuids.Reset();Pillar->ConnectedPillarGuids.Reset();}
	TArray<uint64> Serials;for(auto* Wall:Fixture.Walls)Serials.Add(Wall->GetNodeDefinitionRefreshSerial());
	const auto Applied=Building->RefreshNodeDefinitionDraft(Draft);TestTrue(*Applied.Status.ToString(),Applied.bSucceeded);TestEqual(TEXT("One graph side solve for batch"),Applied.DefinitionSolveCalls,1);TestEqual(TEXT("Three affected physical junctions rebuilt once"),Applied.PillarsVisited,3);TestEqual(TEXT("Four affected walls each generated once"),Applied.WallRefreshCalls,4);for(int32 I=0;I<Fixture.Walls.Num();++I)TestEqual(TEXT("Every wall consumes one precomputed result"),Fixture.Walls[I]->GetNodeDefinitionRefreshSerial(),Serials[I]+1);
	const auto Generated=Capture();const auto Legacy=Building->RefreshWallsConnectedToPillars({Request.NodeGuid},true);TestEqual(TEXT("Legacy comparison dispatches eight wall refreshes"),Legacy.WallRefreshCalls,8);TestTrue(TEXT("Complete generated geometry matches settled legacy refresh"),Same(Capture(),Generated));
	TestEqual(TEXT("Batch generation does not activate preparation"),Building->PreparedWallNodeDefinitions.Version,0);
	FEHBPreparedWallNodeDefinitions Current;TestTrue(TEXT("Capture supports post-move current source"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Building,Current).bSucceeded);const auto NoOp=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Current,{});const auto None=Building->RefreshNodeDefinitionDraft(NoOp);TestTrue(TEXT("No-op generates nothing"),None.bSucceeded&&None.DefinitionSolveCalls==0&&None.PillarsVisited==0&&None.WallRefreshCalls==0);TestTrue(TEXT("No-op preserves geometry"),Same(Capture(),Generated));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeDefinitionMoveDraftTest,"EHB.Topology.NodeDefinitionMoveDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeDefinitionMoveDraftTest::RunTest(const FString& Parameters)
{
	FEHBPreparedWallNodeDefinitions Source;Source.Version=1;
	const TArray<FVector> Positions={FVector(0,0,0),FVector(600,0,0),FVector(600,500,0),FVector(0,500,0),FVector(2000,0,0),FVector(2600,0,0)};
	for(int32 I=0;I<Positions.Num();++I){auto& N=Source.Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid(1,0,0,I+1);N.LocalTransform=FTransform(Positions[I]);N.FloorIndex=1;N.JunctionDimensions=FVector(40,40,300);N.GeometryRevision=17;auto& B=Source.PillarBindings.AddDefaulted_GetRef();B.NodeGuid=N.NodeGuid;B.PhysicalPillarGuid=FGuid(2,0,0,I+1);}
	auto Edge=[&](int32 A,int32 B){auto& W=Source.Walls.AddDefaulted_GetRef();W.WallGuid=FGuid(3,0,0,Source.Walls.Num());W.StartNodeGuid=Source.Nodes[A].NodeGuid;W.EndNodeGuid=Source.Nodes[B].NodeGuid;};Edge(0,1);Edge(1,2);Edge(2,3);Edge(3,0);Edge(4,5);
	auto Json=[](const auto& Data){FString Value;FJsonObjectConverter::UStructToJsonObjectString(Data,Value);return Value;};const FString Original=Json(Source);
	auto Request=[&](int32 I,FVector Delta){FEHBNodeMoveRequest R;R.NodeGuid=Source.Nodes[I].NodeGuid;R.ExpectedPosition=Positions[I];R.TargetPosition=Positions[I]+Delta;return R;};
	const auto Move=Request(0,FVector(-30,-40,0));const auto Draft=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Move});
	if(!TestTrue(TEXT("Actor-independent definition batch prepares"),Draft.bSucceeded&&Draft.bWouldChange))return false;
	TestEqual(TEXT("Only changed root recorded"),Draft.UpdatePlan.MovedNodeGuids.Num(),1);TestEqual(TEXT("Root plus adjacent junctions invalidated"),Draft.UpdatePlan.JunctionNodeGuids.Num(),3);TestEqual(TEXT("Incident walls at affected junctions included"),Draft.UpdatePlan.WallGuids.Num(),4);
	for(int32 I=0;I<Source.Nodes.Num();++I){const auto& N=Draft.Definitions.Nodes[I];TestEqual(TEXT("Potentially affected junction revisions advance once"),N.GeometryRevision,(I==0||I==1||I==3)?18:17);TestTrue(TEXT("Only requested node pose changes"),N.LocalTransform.GetLocation().Equals(I==0?Move.TargetPosition:Positions[I],0.000001));TestTrue(TEXT("Dimensions and floor identity retained"),N.JunctionDimensions==Source.Nodes[I].JunctionDimensions&&N.FloorIndex==Source.Nodes[I].FloorIndex);}
	TestEqual(TEXT("Source definitions remain byte-equivalent JSON"),Json(Source),Original);
	TestEqual(TEXT("Physical binding identities remain unchanged"),Draft.Definitions.PillarBindings[0].PhysicalPillarGuid,Source.PillarBindings[0].PhysicalPillarGuid);
	FEHBWallJunctionMesh Before,After;FName Reason;const FGuid Neighbor=Source.Nodes[1].NodeGuid;
	TestTrue(TEXT("Unmoved neighbor original mesh"),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Source,Neighbor,Before,Reason));TestTrue(TEXT("Unmoved neighbor candidate mesh"),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Draft.Definitions,Neighbor,After,Reason));TestFalse(TEXT("Moving adjacent endpoint changes unmoved node junction geometry"),Before.Vertices==After.Vertices);
	auto* Component=NewObject<UEHBWallJunctionComponent>();TestTrue(TEXT("Revision-bearing component builds original data"),Component->RebuildFromNodeDefinitions(Source,Neighbor));TestEqual(TEXT("Original revision propagated"),Component->SourceGeometryRevision,17);TestTrue(TEXT("Same unmoved node rebuilds from revised draft"),Component->RebuildFromNodeDefinitions(Draft.Definitions,Neighbor));TestEqual(TEXT("Adjacent change propagates component revision"),Component->SourceGeometryRevision,18);
	const auto NoChange=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Request(0,FVector::ZeroVector)});TestTrue(TEXT("No-op succeeds without work"),NoChange.bSucceeded&&!NoChange.bWouldChange&&NoChange.UpdatePlan.JunctionNodeGuids.IsEmpty());TestEqual(TEXT("No-op retains every revision and field"),Json(NoChange.Definitions),Original);
	const auto Empty=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{});TestTrue(TEXT("Read-only no-position candidate supported"),Empty.bSucceeded&&!Empty.bWouldChange);TestEqual(TEXT("Empty request preserves values"),Json(Empty.Definitions),Original);
	const auto A=Request(0,FVector(0,-40,0)),B=Request(1,FVector(0,-40,0));const auto Batch=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{A,B});const auto Reverse=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{B,A});TestTrue(TEXT("Both wall endpoints plan together"),Batch.bSucceeded&&Reverse.bSucceeded);TestEqual(TEXT("Request order does not affect output"),Json(Batch.Definitions),Json(Reverse.Definitions));for(int32 I=0;I<4;++I)TestEqual(TEXT("Shared affected nodes advance only once per batch"),Batch.Definitions.Nodes[I].GeometryRevision,18);
	auto Reject=[&](const auto& Data,const TArray<FEHBNodeMoveRequest>& Requests,FName Expected=NAME_None){const auto Failed=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Data,Requests);TestFalse(TEXT("Invalid edit rejects"),Failed.bSucceeded);TestTrue(TEXT("Failure exposes no candidate definitions or partial update plan"),Failed.Definitions.Version==0&&Failed.Definitions.Nodes.IsEmpty()&&Failed.UpdatePlan.JunctionNodeGuids.IsEmpty());if(!Expected.IsNone())TestEqual(TEXT("Explicit rejection reason"),Failed.Status,Expected);};
	Reject(Source,{Move,Move},TEXT("DuplicateNodeRequest"));auto Stale=Move;Stale.ExpectedPosition.X+=1;Reject(Source,{Stale},TEXT("StalePosition"));auto Unknown=Move;Unknown.NodeGuid=FGuid::NewGuid();Reject(Source,{Unknown},TEXT("UnknownNode"));Reject(Source,{Request(0,FVector(0,0,1))},TEXT("VerticalMoveUnsupported"));auto Collapse=Move;Collapse.TargetPosition=Positions[1];Reject(Source,{Collapse});
	auto Overflow=Source;Overflow.Nodes[1].GeometryRevision=MAX_int32;Reject(Overflow,{Move},TEXT("GeometryRevisionOverflow"));Overflow=Source;Overflow.Nodes[4].GeometryRevision=MAX_int32;TestTrue(TEXT("Unaffected saturated revision does not block unrelated movement"),UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Overflow,{Move}).bSucceeded);
	TArray<FEHBNodeMoveRequest> Oversized;Oversized.Init(Move,2049);Reject(Source,Oversized,TEXT("InvalidBatchSize"));TestEqual(TEXT("Every preview leaves original definitions unchanged"),Json(Source),Original);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeRotationDraftTest,"EHB.Topology.NodeRotationDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeRotationDraftTest::RunTest(const FString& Parameters)
{
	FEHBPreparedWallNodeDefinitions Source;Source.Version=1;
	const TArray<FVector> Positions={FVector(0,0,0),FVector(600,0,0),FVector(600,500,0),FVector(0,500,0),FVector(2000,0,0),FVector(2600,0,0)};
	for(int32 I=0;I<Positions.Num();++I){auto& N=Source.Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid(1,0,0,I+1);N.LocalTransform=FTransform(Positions[I]);N.FloorIndex=1;N.JunctionDimensions=FVector(60,30,300);N.GeometryRevision=17;auto& B=Source.PillarBindings.AddDefaulted_GetRef();B.NodeGuid=N.NodeGuid;B.PhysicalPillarGuid=FGuid(2,0,0,I+1);}
	auto Edge=[&](int32 A,int32 B){auto& W=Source.Walls.AddDefaulted_GetRef();W.WallGuid=FGuid(3,0,0,Source.Walls.Num());W.StartNodeGuid=Source.Nodes[A].NodeGuid;W.EndNodeGuid=Source.Nodes[B].NodeGuid;};Edge(0,1);Edge(1,2);Edge(2,3);Edge(3,0);Edge(4,5);
	auto Json=[](const auto& Value){FString Text;FJsonObjectConverter::UStructToJsonObjectString(Value,Text);return Text;};const FString Original=Json(Source);
	FEHBNodeMoveRequest Position;Position.NodeGuid=Source.Nodes[0].NodeGuid;Position.ExpectedPosition=Position.TargetPosition=Positions[0];
	FEHBNodeRotationRequest Rotation;Rotation.NodeGuid=Position.NodeGuid;Rotation.TargetLocalRotation=FRotator(0,45,0).Quaternion();
	const auto Draft=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Position},{Rotation});
	if(!TestTrue(TEXT("Rotation-only edit is a real change"),Draft.bSucceeded&&Draft.bWouldChange))return false;
	TestEqual(TEXT("Untranslated rotated root enters geometry plan"),Draft.UpdatePlan.MovedNodeGuids.Num(),1);
	TestEqual(TEXT("Rotation conservatively invalidates adjacent junctions"),Draft.UpdatePlan.JunctionNodeGuids.Num(),3);
	for(int32 I=0;I<Source.Nodes.Num();++I){TestEqual(TEXT("Rotation leaves positions unchanged"),Draft.Definitions.Nodes[I].LocalTransform.GetLocation(),Positions[I]);TestEqual(TEXT("Affected revisions advance once"),Draft.Definitions.Nodes[I].GeometryRevision,(I==0||I==1||I==3)?18:17);}
	TestTrue(TEXT("Candidate uses target orientation"),Draft.Definitions.Nodes[0].LocalTransform.GetRotation().Equals(Rotation.TargetLocalRotation,0.00000001));
	TArray<FEHBWallJunctionWallSides> Before,After;FName Reason;
	TestTrue(TEXT("Original wall geometry solves"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Source,Before,Reason));TestTrue(TEXT("Rotated wall geometry solves"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Draft.Definitions,After,Reason));
	bool bGeometryChanged=false;for(const auto& W:After){const auto* Old=Before.FindByPredicate([&](const auto& E){return E.WallGuid==W.WallGuid;});if(Old)bGeometryChanged|=!Old->StartLeft.Equals(W.StartLeft,0.001)||!Old->EndLeft.Equals(W.EndLeft,0.001)||!Old->StartRight.Equals(W.StartRight,0.001)||!Old->EndRight.Equals(W.EndRight,0.001);}
	// Connected legacy footprints follow wall directions using min(width, depth).
	// Changing the node frame must preserve world wall contact while updating its
	// local mesh/UV frame; an unconnected physical rectangle has different semantics.
	TestFalse(TEXT("Rotation preserves the existing direction-driven world wall contacts"),bGeometryChanged);
	FEHBWallJunctionMesh OriginalJunction,RotatedJunction;
	TestTrue(TEXT("Original junction frame builds"),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Source,Position.NodeGuid,OriginalJunction,Reason));
	TestTrue(TEXT("Rotated junction frame builds"),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Draft.Definitions,Position.NodeGuid,RotatedJunction,Reason));
	TestFalse(TEXT("Rotation invalidates local generated vertex data"),OriginalJunction.Vertices==RotatedJunction.Vertices);
	for(const FVector P:RotatedJunction.Footprint)
	{
		const FVector WorldPoint=RotatedJunction.LocalTransform.TransformPosition(P);
		TestTrue(TEXT("Reframed junction preserves world footprint"),OriginalJunction.Footprint.ContainsByPredicate([&](const FVector Q){return OriginalJunction.LocalTransform.TransformPosition(Q).Equals(WorldPoint,0.001);}));
	}
	auto Moved=Position;Moved.TargetPosition+=FVector(-40,-30,0);const auto Combined=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Moved},{Rotation});TestTrue(TEXT("Position and yaw plan together"),Combined.bSucceeded);TestEqual(TEXT("Combined edit does not increment revision twice"),Combined.Definitions.Nodes[0].GeometryRevision,18);
	auto Equivalent=Rotation;Equivalent.TargetLocalRotation=FQuat(0,0,0,-1);const auto Noop=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,{Position},{Equivalent});TestTrue(TEXT("Opposite quaternion sign is the same orientation"),Noop.bSucceeded&&!Noop.bWouldChange);TestEqual(TEXT("Orientation no-op preserves source representation and revisions"),Json(Noop.Definitions),Original);
	auto Reject=[&](const auto& Data,const TArray<FEHBNodeMoveRequest>& Moves,const TArray<FEHBNodeRotationRequest>& Turns,FName Expected){const auto Failed=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Data,Moves,Turns);TestFalse(TEXT("Invalid rotation draft rejects"),Failed.bSucceeded);TestEqual(TEXT("Rotation rejection reason"),Failed.Status,Expected);TestTrue(TEXT("Failure exposes no partial definitions or plan"),Failed.Definitions.Nodes.IsEmpty()&&Failed.UpdatePlan.JunctionNodeGuids.IsEmpty());};
	Reject(Source,{Position},{Rotation,Rotation},TEXT("DuplicateRotationRequest"));Reject(Source,{}, {Rotation},TEXT("RotationRequiresPositionRequest"));
	auto Invalid=Rotation;Invalid.NodeGuid=FGuid::NewGuid();Reject(Source,{Position},{Invalid},TEXT("UnknownNode"));Invalid=Rotation;Invalid.ExpectedLocalRotation=FRotator(0,5,0).Quaternion();Reject(Source,{Position},{Invalid},TEXT("StaleRotation"));Invalid=Rotation;Invalid.TargetLocalRotation=FQuat(0,0,0,2);Reject(Source,{Position},{Invalid},TEXT("InvalidRotation"));Invalid=Rotation;Invalid.TargetLocalRotation=FRotator(10,0,0).Quaternion();Reject(Source,{Position},{Invalid},TEXT("TiltRotationUnsupported"));
	auto Overflow=Source;Overflow.Nodes[1].GeometryRevision=MAX_int32;Reject(Overflow,{Position},{Rotation},TEXT("GeometryRevisionOverflow"));TArray<FEHBNodeRotationRequest> Oversize;Oversize.Init(Rotation,2049);Reject(Source,{Position},Oversize,TEXT("InvalidBatchSize"));
	TestEqual(TEXT("All pose drafts leave source unchanged"),Json(Source),Original);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeRoomCompatibilityTest,"EHB.Topology.NodeRoomLegacyCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeRoomCompatibilityTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	Fixture.Building->SetActorRotation(FRotator(0,37,0));
	FEHBPreparedWallNodeDefinitions Prepared;if(!UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Fixture.Building,Prepared).bSucceeded)return false;
	FEHBWallNodeModel Model=Prepared;TArray<FEHBNodeRoomBoundary> Rooms;FName Reason;
	TestTrue(TEXT("Captured model room solve"),FEHBWallNodeRooms::Build(Fixture.Building->BuildingGuid,Model,Rooms,Reason));
	const auto Legacy=Fixture.Building->GetClosedLoopsByFloor(1);TestEqual(TEXT("Legacy and model room count"),Rooms.Num(),Legacy.Num());
	if(Rooms.Num()!=1||Legacy.Num()!=1)return false;
	TestEqual(TEXT("Migrated room identity remains unchanged"),Rooms[0].RoomGuid,Legacy[0].LoopGuid);
	TestEqual(TEXT("Migrated room area remains unchanged"),static_cast<float>(Rooms[0].Area),Legacy[0].Area);
	TArray<FVector> LegacyPolygon;for(const FGuid Id:Legacy[0].PillarGuids){const auto* Pillar=Fixture.Building->FindElementActorByGuid(Id);if(!Pillar)return false;LegacyPolygon.Add(Pillar->GetElementLocalTransform().GetLocation());}
	for(const auto& Point:LegacyPolygon)TestTrue(TEXT("Model retains each original boundary position"),Rooms[0].Polygon.Contains(Point));
	const auto BuildingId=Fixture.Building->BuildingGuid;const auto RoomId=Rooms[0].RoomGuid;
	for(int32 I=0;I<Model.PillarBindings.Num();++I)Model.PillarBindings[I].PhysicalPillarGuid=FGuid(58,2,0,I+1);
	TestTrue(TEXT("Physical IDs do not define rooms"),FEHBWallNodeRooms::Build(BuildingId,Model,Rooms,Reason));TestEqual(TEXT("Room identity independent of physical IDs"),Rooms[0].RoomGuid,RoomId);
	for(auto* Actor:Fixture.Actors)if(Actor&&!Actor->IsActorBeingDestroyed())Actor->Destroy();Fixture.Actors.Reset();
	Model.PillarBindings.Reset();
	TestTrue(TEXT("All source actors destroyed and bindings absent"),FEHBWallNodeRooms::Build(BuildingId,Model,Rooms,Reason));TestEqual(TEXT("Unbound room identity preserved"),Rooms[0].RoomGuid,RoomId);
	TestEqual(TEXT("Unbound room local point query"),FEHBWallNodeRooms::FindAtPoint(Rooms,1,FVector(300,200,100)).Num(),1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeRoomPlanTest,"EHB.Topology.NodeRoomModelQueries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeRoomPlanTest::RunTest(const FString& Parameters)
{
	const FGuid BuildingId(58,3,0,1);FEHBWallNodeModel Model;Model.Version=1;
	auto NodeId=[](int32 I){return FGuid(58,3,1,I);};auto WallId=[](int32 I){return FGuid(58,3,2,I);};
	auto AddNode=[&](FEHBWallNodeModel& Data,int32 I,FVector Point,int32 Floor=1){auto& Node=Data.Nodes.AddDefaulted_GetRef();Node.NodeGuid=NodeId(I);Node.LocalTransform=FTransform(Point);Node.FloorIndex=Floor;Node.JunctionDimensions=FVector(20,20,300);};
	auto AddWall=[&](FEHBWallNodeModel& Data,int32 I,int32 A,int32 B){auto& Wall=Data.Walls.AddDefaulted_GetRef();Wall.WallGuid=WallId(I);Wall.StartNodeGuid=NodeId(A);Wall.EndNodeGuid=NodeId(B);};
	AddNode(Model,1,FVector(0,0,0));AddNode(Model,2,FVector(300,0,0));AddNode(Model,3,FVector(600,0,0));
	AddNode(Model,4,FVector(600,300,0));AddNode(Model,5,FVector(300,300,0));AddNode(Model,6,FVector(0,300,0));
	AddWall(Model,1,1,2);AddWall(Model,2,2,3);AddWall(Model,3,3,4);AddWall(Model,4,4,5);AddWall(Model,5,5,6);AddWall(Model,6,6,1);AddWall(Model,7,2,5);
	TArray<FEHBNodeRoomBoundary> Rooms;FName Reason;
	TestTrue(TEXT("Two adjacent unbound rooms"),FEHBWallNodeRooms::Build(BuildingId,Model,Rooms,Reason));if(Rooms.Num()!=2){AddError(Reason.ToString());return false;}
	for(const auto& Room:Rooms){TestEqual(TEXT("Independent centerline area"),Room.Area,90000.0);TestEqual(TEXT("Four corners per room"),Room.NodeGuids.Num(),4);}
	const auto Original=Rooms;
	TestEqual(TEXT("Left room query"),FEHBWallNodeRooms::FindAtPoint(Rooms,1,FVector(150,150,120)).Num(),1);
	TestEqual(TEXT("Right room query"),FEHBWallNodeRooms::FindAtPoint(Rooms,1,FVector(450,150,120)).Num(),1);
	TestEqual(TEXT("Shared boundary reports both candidates"),FEHBWallNodeRooms::FindAtPoint(Rooms,1,FVector(300,150,0)).Num(),2);
	TestEqual(TEXT("Outside has no room"),FEHBWallNodeRooms::FindAtPoint(Rooms,1,FVector(-10,150,0)).Num(),0);
	TestEqual(TEXT("Explicit other floor has no room"),FEHBWallNodeRooms::FindAtPoint(Rooms,2,FVector(150,150,0)).Num(),0);
	auto Reordered=Model;for(int32 I=0;I<Reordered.Nodes.Num()/2;++I)Reordered.Nodes.Swap(I,Reordered.Nodes.Num()-1-I);for(int32 I=0;I<Reordered.Walls.Num()/2;++I)Reordered.Walls.Swap(I,Reordered.Walls.Num()-1-I);
	for(auto& Wall:Reordered.Walls)Swap(Wall.StartNodeGuid,Wall.EndNodeGuid);
	TestTrue(TEXT("Input order and wall orientation accepted"),FEHBWallNodeRooms::Build(BuildingId,Reordered,Rooms,Reason));
	for(int32 I=0;I<Rooms.Num();++I){TestEqual(TEXT("Deterministic room order"),Rooms[I].RoomGuid,Original[I].RoomGuid);TestTrue(TEXT("Deterministic cyclic boundary"),Rooms[I].NodeGuids==Original[I].NodeGuids&&Rooms[I].WallGuids==Original[I].WallGuids&&Rooms[I].Polygon==Original[I].Polygon);}
	auto Translated=Model;for(auto& Node:Translated.Nodes)Node.LocalTransform.AddToTranslation(FVector(123,-456,0));
	TestTrue(TEXT("Translated model"),FEHBWallNodeRooms::Build(BuildingId,Translated,Rooms,Reason));for(int32 I=0;I<Rooms.Num();++I)TestEqual(TEXT("Movement retains stable room identity"),Rooms[I].RoomGuid,Original[I].RoomGuid);
	auto Subdivided=Model;Subdivided.Walls.RemoveAt(0);AddNode(Subdivided,8,FVector(150,0,0));AddWall(Subdivided,8,1,8);AddWall(Subdivided,9,8,2);
	TestTrue(TEXT("Straight boundary subdivision"),FEHBWallNodeRooms::Build(BuildingId,Subdivided,Rooms,Reason));for(int32 I=0;I<Rooms.Num();++I)TestEqual(TEXT("Subdivision retains room identity"),Rooms[I].RoomGuid,Original[I].RoomGuid);
	auto MultiFloor=Model;for(const auto& Source:Model.Nodes){auto Node=Source;Node.NodeGuid.D+=100;Node.FloorIndex=2;Node.LocalTransform.AddToTranslation(FVector(0,0,350));MultiFloor.Nodes.Add(Node);}for(const auto& Source:Model.Walls){auto Wall=Source;Wall.WallGuid.D+=100;Wall.StartNodeGuid.D+=100;Wall.EndNodeGuid.D+=100;MultiFloor.Walls.Add(Wall);}
	TestTrue(TEXT("Stacked floor boundaries"),FEHBWallNodeRooms::Build(BuildingId,MultiFloor,Rooms,Reason));TestEqual(TEXT("Four rooms across two levels"),Rooms.Num(),4);TestEqual(TEXT("Upper floor query"),FEHBWallNodeRooms::FindAtPoint(Rooms,2,FVector(150,150,450)).Num(),1);
	auto Fail=[&](const FEHBWallNodeModel& Invalid,FName Expected){Rooms=Original;TestFalse(TEXT("Unsupported layout refused"),FEHBWallNodeRooms::Build(BuildingId,Invalid,Rooms,Reason));TestEqual(TEXT("Refusal explains layout requirement"),Reason,Expected);TestTrue(TEXT("Failure exposes no partial room set"),Rooms.IsEmpty());};
	auto Invalid=Model;Invalid.Nodes[0].FloorIndex=2;Fail(Invalid,TEXT("CrossFloorConnection"));
	Invalid=Model;AddNode(Invalid,9,FVector(150,-50,0));AddNode(Invalid,10,FVector(150,350,0));AddWall(Invalid,10,9,10);Fail(Invalid,TEXT("UnsplitWallIntersection"));
	Invalid=Model;AddNode(Invalid,9,FVector(150,0,0));AddNode(Invalid,10,FVector(150,-50,0));AddWall(Invalid,10,9,10);Fail(Invalid,TEXT("OverlappingOrUnsplitWallContact"));
	Invalid=Model;AddWall(Invalid,10,2,1);Fail(Invalid,TEXT("OverlappingOrUnsplitWallContact"));
	Invalid=Model;AddNode(Invalid,9,FVector(50,50,0));AddNode(Invalid,10,FVector(100,50,0));AddNode(Invalid,11,FVector(100,100,0));AddNode(Invalid,12,FVector(50,100,0));AddWall(Invalid,10,9,10);AddWall(Invalid,11,10,11);AddWall(Invalid,12,11,12);AddWall(Invalid,13,12,9);Fail(Invalid,TEXT("NestedRoomBoundariesRequireHoles"));
	Invalid=Model;AddNode(Invalid,9,FVector(150,150,0));AddWall(Invalid,10,1,9);TestTrue(TEXT("Interior half wall preserves room boundaries"),FEHBWallNodeRooms::Build(BuildingId,Invalid,Rooms,Reason));TestEqual(TEXT("Interior half wall creates no extra room"),Rooms.Num(),2);for(int32 I=0;I<Rooms.Num();++I){TestEqual(TEXT("Half wall retains room ID"),Rooms[I].RoomGuid,Original[I].RoomGuid);TestTrue(TEXT("Half wall excluded from boundary"),Rooms[I].NodeGuids==Original[I].NodeGuids&&Rooms[I].WallGuids==Original[I].WallGuids);}
	Invalid=Model;AddNode(Invalid,9,FVector(-100,-100,0));AddWall(Invalid,10,1,9);TestTrue(TEXT("Exterior open branch preserves rooms"),FEHBWallNodeRooms::Build(BuildingId,Invalid,Rooms,Reason));TestEqual(TEXT("Exterior branch room count"),Rooms.Num(),2);
	Invalid=Model;Invalid.Walls.RemoveAt(6);TestTrue(TEXT("Removing shared wall merges rooms"),FEHBWallNodeRooms::Build(BuildingId,Invalid,Rooms,Reason));TestEqual(TEXT("One merged room"),Rooms.Num(),1);if(Rooms.Num()==1){TestEqual(TEXT("Merged area"),Rooms[0].Area,180000.0);for(const auto& Prior:Original)TestNotEqual(TEXT("Merge requires explicit identity transfer"),Rooms[0].RoomGuid,Prior.RoomGuid);}
	Invalid=Model;Invalid.Walls.Reset();TestTrue(TEXT("Open empty edge graph is valid with no rooms"),FEHBWallNodeRooms::Build(BuildingId,Invalid,Rooms,Reason));TestTrue(TEXT("No invented rooms"),Rooms.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeBindingTest,"EHB.Topology.OptionalNodeBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeBindingTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	FEHBPreparedWallNodeDefinitions Prepared;if(!UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Fixture.Building,Prepared).bSucceeded)return false;
	FEHBWallNodeModel Model=Prepared;Model.PillarBindings.Reset();
	auto Json=[](const auto& Data){FString Text;FJsonObjectConverter::UStructToJsonObjectString(Data,Text);return Text;};
	TestTrue(TEXT("No physical bindings required by canonical model"),UEHBWallTopologyLibrary::ValidateWallNodeModel(Model).IsEmpty());
	FEHBWallNodeModel RoundTrip;TestTrue(TEXT("Unbound model JSON reads all definition fields"),FJsonObjectConverter::JsonObjectStringToUStruct(Json(Model),&RoundTrip));TestEqual(TEXT("Unbound model JSON round trip"),Json(RoundTrip),Json(Model));
	FEHBPreparedWallNodeDefinitions InvalidPrepared;static_cast<FEHBWallNodeModel&>(InvalidPrepared)=Model;
	TestTrue(TEXT("Preparation still requires every physical binding"),HasIssue(UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(InvalidPrepared),TEXT("MissingPreparedPillarBinding")));
	TArray<FEHBWallJunctionWallSides> Sides;FName Reason;
	TestTrue(TEXT("Unbound model generates walls"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason));TestEqual(TEXT("All unbound walls generated"),Sides.Num(),4);
	TestFalse(TEXT("Legacy preparation adapter cannot bypass missing bindings"),UEHBWallTopologyLibrary::BuildPreparedWallSides(InvalidPrepared,Sides,Reason));TestTrue(TEXT("Rejected preparation clears geometry output"),Sides.IsEmpty());
	FEHBNodeMoveRequest Move;Move.NodeGuid=Model.Nodes[0].NodeGuid;Move.ExpectedPosition=Model.Nodes[0].LocalTransform.GetLocation();Move.TargetPosition=Move.ExpectedPosition+FVector(-25,-15,0);
	const auto Draft=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Model,{Move});TestTrue(TEXT("Unbound model uses the same move draft"),Draft.bSucceeded&&Draft.bWouldChange&&Draft.Definitions.PillarBindings.IsEmpty());
	TestFalse(TEXT("Legacy draft retains strict preparation validation"),UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(InvalidPrepared,{Move}).bSucceeded);
	auto Invalid=Model;auto Binding=Prepared.PillarBindings[0];Invalid.PillarBindings={Binding,Binding};TestTrue(TEXT("Optional does not mean duplicate bindings allowed"),HasIssue(UEHBWallTopologyLibrary::ValidateWallNodeModel(Invalid),TEXT("InvalidPillarBinding")));
	Invalid=Model;Binding.NodeGuid=FGuid::NewGuid();Invalid.PillarBindings={Binding};TestTrue(TEXT("Dangling binding rejected"),HasIssue(UEHBWallTopologyLibrary::ValidateWallNodeModel(Invalid),TEXT("InvalidPillarBinding")));
	Invalid=Model;Invalid.Walls[0].EndNodeGuid=FGuid::NewGuid();TestTrue(TEXT("Missing logical node still rejected"),HasIssue(UEHBWallTopologyLibrary::ValidateWallNodeModel(Invalid),TEXT("InvalidWallEndpoints")));
	Invalid=Model;Binding=Prepared.PillarBindings[0];auto Other=Prepared.PillarBindings[1];Other.PhysicalPillarGuid=Binding.PhysicalPillarGuid;Invalid.PillarBindings={Binding,Other};TestTrue(TEXT("One physical pillar cannot occupy two nodes"),HasIssue(UEHBWallTopologyLibrary::ValidateWallNodeModel(Invalid),TEXT("InvalidPillarBinding")));
	// Reflection inheritance must keep the existing named preparation fields and structure.
	const auto PreparedJson=Json(Prepared);FEHBPreparedWallNodeDefinitions Reloaded;
	TestTrue(TEXT("Inherited preparation fields deserialize"),FJsonObjectConverter::JsonObjectStringToUStruct(PreparedJson,&Reloaded));TestEqual(TEXT("Preparation serialization remains unchanged"),Json(Reloaded),PreparedJson);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPhysicalBindingRemovalDraftTest,"EHB.Topology.PhysicalBindingRemovalDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPhysicalBindingRemovalDraftTest::RunTest(const FString& Parameters)
{
	UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;FTransientTopologyFixture Fixture;if(!Fixture.Create(World))return false;
	FActorSpawnParameters ExtraParams;ExtraParams.ObjectFlags=RF_Transient;
	auto* Branch=World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Fixture.Building->GetActorLocation(),FRotator::ZeroRotator,ExtraParams);if(!Branch)return false;
	Fixture.Actors.Add(Branch);Fixture.Pillars.Add(Branch);Branch->AttachToBuilding(Fixture.Building,FTransform(FVector(1000,0,0)));Branch->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	auto* BranchWall=Fixture.Building->ConnectPillars(Fixture.Pillars[1],Branch,300,20);if(!BranchWall)return false;BranchWall->SetFlags(RF_Transient);Fixture.Actors.Add(BranchWall);Fixture.Walls.Add(BranchWall);
	Fixture.Building->SetActorRotation(FRotator(0,37,0));
	FEHBPreparedWallNodeDefinitions Prepared;if(!UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Fixture.Building,Prepared).bSucceeded)return false;
	FEHBWallNodeModel Source=Prepared;for(int32 I=0;I<Source.PillarBindings.Num();++I)Source.PillarBindings[I].PhysicalPillarGuid=FGuid(97,0,0,I+1);const FGuid NodeGuid=Fixture.Pillars[0]->ElementGuid;const auto* SourceNode=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeGuid;});if(!SourceNode)return false;
	auto Json=[](const auto& Data){FString Text;FJsonObjectConverter::UStructToJsonObjectString(Data,Text);return Text;};const FString Original=Json(Source);
	const FGuid PhysicalGuid=Source.PillarBindings.FindByPredicate([&](const auto& B){return B.NodeGuid==NodeGuid;})->PhysicalPillarGuid;
	const auto Draft=UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(Source,NodeGuid,PhysicalGuid,SourceNode->GeometryRevision);
	if(!TestTrue(TEXT("Physical removal plans retained node geometry"),Draft.bSucceeded&&Draft.bWouldChange))return false;
	TestEqual(TEXT("Only requested physical binding removed"),Draft.Definitions.PillarBindings.Num(),Source.PillarBindings.Num()-1);TestTrue(TEXT("Removed physical identity is explicit"),Draft.RemovedPhysicalPillarGuids==TArray<FGuid>{PhysicalGuid});
	TestEqual(TEXT("Logical nodes preserved"),Draft.Definitions.Nodes.Num(),Source.Nodes.Num());TestEqual(TEXT("Connected wall definitions preserved"),Draft.Definitions.Walls.Num(),Source.Walls.Num());
	for(const auto& Wall:Source.Walls){const auto* Retained=Draft.Definitions.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Wall.WallGuid;});TestTrue(TEXT("Every wall identity endpoint and dimensions retained"),Retained&&Json(*Retained)==Json(Wall));}
	const auto* Junction=Draft.Definitions.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeGuid;});TestTrue(TEXT("Fill dimensions replace physical column dimensions"),Junction&&Junction->JunctionDimensions.Equals(FVector(20,20,300)));
	for(const auto& N:Draft.Definitions.Nodes){const auto* Before=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});TestTrue(TEXT("All logical poses unchanged"),Before&&Before->LocalTransform.Equals(N.LocalTransform));TestEqual(TEXT("Affected junction revisions increment once"),N.GeometryRevision,Before->GeometryRevision+(Draft.UpdatePlan.JunctionNodeGuids.Contains(N.NodeGuid)?1:0));}
	TestEqual(TEXT("Source model not modified by planning"),Json(Source),Original);TestEqual(TEXT("Planning does not destroy physical actors"),Fixture.Building->QueryElements(FEHBElementQuery()).Num(),Fixture.Pillars.Num()+Fixture.Walls.Num());
	auto Reject=[&](const auto& Data,FGuid Node,FGuid Physical,int32 Revision,FName Expected){const auto Failed=UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(Data,Node,Physical,Revision);TestFalse(TEXT("Invalid removal refuses"),Failed.bSucceeded);TestEqual(TEXT("Removal rejection reason"),Failed.Status,Expected);TestTrue(TEXT("Failure exposes no partial model or removals"),Failed.Definitions.Nodes.IsEmpty()&&Failed.RemovedPhysicalPillarGuids.IsEmpty()&&Failed.UpdatePlan.WallGuids.IsEmpty());};
	Reject(Source,NodeGuid,PhysicalGuid,SourceNode->GeometryRevision+1,TEXT("StaleNodeRevision"));Reject(Source,NodeGuid,FGuid::NewGuid(),SourceNode->GeometryRevision,TEXT("StalePhysicalBinding"));Reject(Source,FGuid::NewGuid(),NodeGuid,1,TEXT("UnknownNode"));
	auto Overflow=Source;for(auto& N:Overflow.Nodes)if(N.NodeGuid==NodeGuid)N.GeometryRevision=MAX_int32;Reject(Overflow,NodeGuid,PhysicalGuid,MAX_int32,TEXT("GeometryRevisionOverflow"));
	const auto Repeat=UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(Draft.Definitions,NodeGuid,PhysicalGuid,Junction->GeometryRevision);TestTrue(TEXT("Already unbound node is no-op at current revision"),Repeat.bSucceeded&&!Repeat.bWouldChange&&Repeat.RemovedPhysicalPillarGuids.IsEmpty());TestEqual(TEXT("Repeated removal preserves every value"),Json(Repeat.Definitions),Json(Draft.Definitions));
	// Remove all bindings in value space. Destruction below only disposes the isolated source fixture.
	FEHBWallNodeModel Unbound=Draft.Definitions;
	while(!Unbound.PillarBindings.IsEmpty())
	{
		const auto Binding=Unbound.PillarBindings[0];const auto* N=Unbound.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});
		const auto Next=UEHBWallTopologyLibrary::BuildPhysicalPillarRemovalDraft(Unbound,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision);if(!TestTrue(TEXT("Every corner can retain a node without a pillar"),Next.bSucceeded))return false;Unbound=Next.Definitions;
	}
	for(int32 I=Fixture.Actors.Num()-1;I>=0;--I)if(IsValid(Fixture.Actors[I])&&!Fixture.Actors[I]->IsActorBeingDestroyed())Fixture.Actors[I]->Destroy();Fixture.Actors.Reset();
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Generated=World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(),FVector(0,0,180000),FRotator(0,37,0),Params);if(!Generated)return false;Fixture.Actors.Add(Generated);
	TArray<FEHBWallJunctionWallSides> Sides;FName Reason;if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Unbound,Sides,Reason))return false;
	for(const auto& N:Unbound.Nodes)
	{
		auto* Component=NewObject<UEHBWallJunctionComponent>(Generated);Generated->AddInstanceComponent(Component);Component->AttachToComponent(Generated->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);Component->RegisterComponent();
		TestTrue(TEXT("Unbound junction component builds without source actors"),Component->RebuildFromNodeModel(Unbound,N.NodeGuid));
		FEHBWallJunctionMesh Mesh;if(!UEHBWallTopologyLibrary::BuildWallNodeModelJunctionMesh(Unbound,N.NodeGuid,Mesh,Reason))return false;
		auto OnBoundary=[&](FVector P){P=Mesh.LocalTransform.InverseTransformPosition(P);P.Z=0;for(int32 I=0;I<Mesh.Footprint.Num();++I)if(FVector::DistSquared(P,FMath::ClosestPointOnSegment(P,Mesh.Footprint[I],Mesh.Footprint[(I+1)%Mesh.Footprint.Num()]))<0.000001)return true;return false;};
		for(const auto& Wall:Unbound.Walls)if(Wall.StartNodeGuid==N.NodeGuid||Wall.EndNodeGuid==N.NodeGuid)
		{const auto* Side=Sides.FindByPredicate([&](const auto& W){return W.WallGuid==Wall.WallGuid;});TestTrue(TEXT("Wall left/right endpoints meet the unbound junction boundary"),Side&&(Wall.StartNodeGuid==N.NodeGuid?(OnBoundary(Side->StartLeft)&&OnBoundary(Side->StartRight)):(OnBoundary(Side->EndLeft)&&OnBoundary(Side->EndRight))));}
	}
	for(const auto& W:Unbound.Walls)
	{
		auto* Wall=World->SpawnActor<AEHB_Wall>(AEHB_Wall::StaticClass(),Generated->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Wall)return false;Fixture.Actors.Add(Wall);Wall->ElementGuid=W.WallGuid;Wall->Thickness=W.Thickness;Wall->Height=W.Height;
		TestTrue(TEXT("Actual wall mesh generates with zero physical bindings"),Wall->RefreshFromNodeModel(Unbound));TestTrue(TEXT("No fake physical endpoint identities required"),!Wall->StartPillarGuid.IsValid()&&!Wall->EndPillarGuid.IsValid());
		const auto* Section=Wall->LeftWallMeshComponent->GetProcMeshSection(0);TestTrue(TEXT("Generated wall has triangles"),Section&&!Section->ProcIndexBuffer.IsEmpty());
	}
	TestTrue(TEXT("Derived junctions do not register as physical pillar elements"),Generated->QueryElements(FEHBElementQuery()).IsEmpty());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBJunctionPrismTest,"EHB.Geometry.JunctionPrism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBJunctionPrismTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Concave={FVector(0,0,0),FVector(60,0,0),FVector(60,20,0),FVector(20,20,0),FVector(20,60,0),FVector(0,60,0)};
	FEHBWallJunctionMesh Mesh;
	TestTrue(TEXT("Concave junction can be filled without fan overdraw"),FEHBWallJunctionMeshBuilder::BuildPrism(Concave,300,Mesh));
	double TopArea=0,BottomArea=0,Volume=0;
	for(int32 I=0;I<Mesh.Triangles.Num();I+=3)
	{
		const int32 A=Mesh.Triangles[I],B=Mesh.Triangles[I+1],C=Mesh.Triangles[I+2];const FVector V=Mesh.Vertices[A],W=Mesh.Vertices[B],X=Mesh.Vertices[C];
		const FVector Cross=FVector::CrossProduct(W-V,X-V);TestTrue(TEXT("Triangle winding follows existing UE mesh convention"),FVector::DotProduct(Cross,Mesh.Normals[A])<0);
		if(Mesh.Normals[A].Z>0.5)TopArea+=Cross.Size()/2;if(Mesh.Normals[A].Z<-0.5)BottomArea+=Cross.Size()/2;
		Volume+=FVector::DotProduct(V,FVector::CrossProduct(W,X))/6;
	}
	TestTrue(TEXT("Concave top covers exactly 2000 square cm"),FMath::IsNearlyEqual(TopArea,2000.0,0.001));TestTrue(TEXT("Bottom has matching coverage"),FMath::IsNearlyEqual(BottomArea,2000.0,0.001));TestTrue(TEXT("Closed prism volume is 600000 cubic cm"),FMath::IsNearlyEqual(FMath::Abs(Volume),600000.0,0.001));
	TArray<FVector> Collinear=Concave;Collinear.Insert(FVector(30,0,0),1);
	TestTrue(TEXT("Redundant collinear boundary points supported"),FEHBWallJunctionMeshBuilder::BuildPrism(Collinear,300,Mesh));
	for(int32 I=0;I<Mesh.Triangles.Num();I+=3)TestTrue(TEXT("Collinear cleanup emits no degenerate triangles"),FVector::CrossProduct(Mesh.Vertices[Mesh.Triangles[I+1]]-Mesh.Vertices[Mesh.Triangles[I]],Mesh.Vertices[Mesh.Triangles[I+2]]-Mesh.Vertices[Mesh.Triangles[I]]).SizeSquared()>0.000001);
	const TArray<FVector> DiagonalJoin={FVector(-20,10,0),FVector(-20,-10,0),FVector(-9.116849750280,-10,0),FVector(-8.113442529345,-20.836795586726,0),FVector(11.801370951774,-18.992831375512,0),FVector(20,-10,0),FVector(20,10,0)};
	TestTrue(TEXT("Diagonal join supports collinear vertices left after ear removal"),FEHBWallJunctionMeshBuilder::BuildPrism(DiagonalJoin,300,Mesh));
	double ExpectedArea2=0,DiagonalTop=0;
	for(int32 I=0;I<DiagonalJoin.Num();++I){const auto A=DiagonalJoin[I],B=DiagonalJoin[(I+1)%DiagonalJoin.Num()];ExpectedArea2+=A.X*B.Y-A.Y*B.X;}
	for(int32 I=0;I<Mesh.Triangles.Num();I+=3){const int32 A=Mesh.Triangles[I],B=Mesh.Triangles[I+1],C=Mesh.Triangles[I+2];const FVector Cross=FVector::CrossProduct(Mesh.Vertices[B]-Mesh.Vertices[A],Mesh.Vertices[C]-Mesh.Vertices[A]);TestTrue(TEXT("Diagonal join emits only nonempty correctly wound faces"),FVector::DotProduct(Cross,Mesh.Normals[A])<0);if(Mesh.Normals[A].Z>0.5)DiagonalTop+=Cross.Size()/2;}
	TestTrue(TEXT("Diagonal join caps cover the boundary exactly"),FMath::IsNearlyEqual(DiagonalTop,FMath::Abs(ExpectedArea2)/2,0.0001));
	TArray<FVector> Reversed=Concave;Algo::Reverse(Reversed);TestTrue(TEXT("Clockwise polygon normalized"),FEHBWallJunctionMeshBuilder::BuildPrism(Reversed,300,Mesh));
	for(const auto& Invalid:TArray<TArray<FVector>>{{FVector(0,0,0),FVector(10,10,0),FVector(0,10,0),FVector(10,0,0)},{FVector(0,0,0),FVector(10,0,0),FVector(10,0,0),FVector(0,10,0)},{FVector(0,0,1),FVector(10,0,0),FVector(0,10,0)}})
	{TestFalse(TEXT("Crossing duplicate or nonplanar footprint rejected"),FEHBWallJunctionMeshBuilder::BuildPrism(Invalid,300,Mesh));TestTrue(TEXT("Failure exposes no partial geometry"),Mesh.Vertices.IsEmpty()&&Mesh.Triangles.IsEmpty());}
	TestFalse(TEXT("Invalid height rejected"),FEHBWallJunctionMeshBuilder::BuildPrism(Concave,0,Mesh));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDefinitionJunctionGenerationTest,"EHB.Geometry.DefinitionJunctionGeneration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDefinitionJunctionGenerationTest::RunTest(const FString& Parameters)
{
	UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;FTransientTopologyFixture Fixture;if(!Fixture.Create(World))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Extra=World->SpawnActor<AEHB_Pillar>(Params);if(!Extra)return false;Fixture.Actors.Add(Extra);Fixture.Pillars.Add(Extra);Extra->AttachToBuilding(Building,FTransform(FVector(-500,0,0)));Extra->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	auto* Spur=Building->ConnectPillars(Fixture.Pillars[0],Extra,300,20);if(!Spur)return false;Spur->SetFlags(RF_Transient);Fixture.Actors.Add(Spur);Fixture.Walls.Add(Spur);
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);if(!TestTrue(TEXT("Prepare rectangle and T junction source"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded))return false;
	const auto Definitions=Building->PreparedWallNodeDefinitions;TArray<FEHBWallJunctionMesh> Meshes;FName Reason;
	auto Isolated=Definitions;auto Unconnected=Definitions.Nodes[0];Unconnected.NodeGuid=FGuid::NewGuid();Unconnected.LocalTransform.SetLocation(FVector(2000,2000,0));Isolated.Nodes.Add(Unconnected);FEHBWallNodePillarBinding Binding;Binding.NodeGuid=Unconnected.NodeGuid;Binding.PhysicalPillarGuid=FGuid::NewGuid();Isolated.PillarBindings.Add(Binding);
	FEHBWallJunctionMesh NoJunction;TestFalse(TEXT("Isolated physical column does not create a virtual junction"),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Isolated,Unconnected.NodeGuid,NoJunction,Reason));TestEqual(TEXT("Explicit no-wall reason"),Reason,FName(TEXT("NodeHasNoWalls")));TestTrue(TEXT("No isolated rendering artifact"),NoJunction.Vertices.IsEmpty());
	TArray<FEHBWallJunctionWallSides> Walls;TestTrue(TEXT("Shared graph side solution"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Definitions,Walls,Reason));
	for(const auto& Node:Definitions.Nodes)
	{
		FEHBWallJunctionMesh Mesh;if(!TestTrue(*Reason.ToString(),UEHBWallTopologyLibrary::BuildPreparedJunctionMesh(Definitions,Node.NodeGuid,Mesh,Reason)))return false;
		TestTrue(TEXT("Node identity and local pose retained"),Mesh.NodeGuid==Node.NodeGuid&&Mesh.LocalTransform.Equals(Node.LocalTransform));
		auto OnBoundary=[&](FVector Point){Point=Node.LocalTransform.InverseTransformPosition(Point);Point.Z=0;for(int32 I=0;I<Mesh.Footprint.Num();++I){const FVector A=Mesh.Footprint[I],B=Mesh.Footprint[(I+1)%Mesh.Footprint.Num()];if(FMath::PointDistToSegment(Point,A,B)<=0.001)return true;}return false;};
		for(const auto& W:Definitions.Walls)if(W.StartNodeGuid==Node.NodeGuid||W.EndNodeGuid==Node.NodeGuid){const auto* Side=Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W.WallGuid;});if(!Side)return false;const bool Start=W.StartNodeGuid==Node.NodeGuid;TestTrue(TEXT("Wall left and right ends touch the junction boundary"),OnBoundary(Start?Side->StartLeft:Side->EndLeft)&&OnBoundary(Start?Side->StartRight:Side->EndRight));}
		Meshes.Add(MoveTemp(Mesh));
	}
	for(int32 I=Fixture.Actors.Num()-1;I>=0;--I)if(IsValid(Fixture.Actors[I])&&!Fixture.Actors[I]->IsActorBeingDestroyed())Fixture.Actors[I]->Destroy();Fixture.Actors.Reset();
	auto* Generated=World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(),FVector(0,0,180000),FRotator(0,37,0),Params);if(!Generated)return false;Fixture.Actors.Add(Generated);
	for(const auto& Mesh:Meshes)
	{
		auto* Component=NewObject<UEHBWallJunctionComponent>(Generated,NAME_None,RF_Transient);Generated->AddInstanceComponent(Component);Component->SetupAttachment(Generated->GetRootComponent());Component->RegisterComponent();
		if(!TestTrue(TEXT("Building component reconstructs junction after source Actors are gone"),Component->RebuildFromNodeDefinitions(Definitions,Mesh.NodeGuid)))return false;
		const auto* Section=Component->GetProcMeshSection(0);if(!TestNotNull(TEXT("Actual generated junction section"),Section))return false;
		bool Same=Section->ProcVertexBuffer.Num()==Mesh.Vertices.Num()&&Section->ProcIndexBuffer.Num()==Mesh.Triangles.Num();
		if(Same){for(int32 I=0;I<Mesh.Vertices.Num();++I)Same&=Section->ProcVertexBuffer[I].Position.Equals(Mesh.Vertices[I],0.001)&&Section->ProcVertexBuffer[I].Normal.Equals(Mesh.Normals[I],0.0001)&&Section->ProcVertexBuffer[I].UV0.Equals(Mesh.UVs[I],0.00001);for(int32 I=0;I<Mesh.Triangles.Num();++I)Same&=Section->ProcIndexBuffer[I]==Mesh.Triangles[I];}
		TestTrue(TEXT("Component retains all generated local geometry and transform"),Same&&Component->GetRelativeTransform().Equals(Mesh.LocalTransform,0.001));
		TestEqual(TEXT("Node generation revision recorded"),Component->SourceGeometryRevision,1);
		const auto Original=*Section;const auto Pose=Component->GetRelativeTransform();auto Bad=Definitions;Bad.Nodes[0].GeometryRevision=0;
		TestFalse(TEXT("Malformed definition preserves existing visible junction"),Component->RebuildFromNodeDefinitions(Bad,Mesh.NodeGuid));TestFalse(TEXT("Unknown node preserves existing visible junction"),Component->RebuildFromNodeDefinitions(Definitions,FGuid::NewGuid()));
		Section=Component->GetProcMeshSection(0);bool Unchanged=Section&&Section->ProcIndexBuffer==Original.ProcIndexBuffer&&Section->ProcVertexBuffer.Num()==Original.ProcVertexBuffer.Num();
		if(Unchanged)for(int32 I=0;I<Original.ProcVertexBuffer.Num();++I)Unchanged&=Section->ProcVertexBuffer[I].Position==Original.ProcVertexBuffer[I].Position&&Section->ProcVertexBuffer[I].Normal==Original.ProcVertexBuffer[I].Normal&&Section->ProcVertexBuffer[I].UV0==Original.ProcVertexBuffer[I].UV0;
		TestTrue(TEXT("Failed rebuild retains mesh pose and node metadata"),Unchanged&&Component->GetRelativeTransform().Equals(Pose)&&Component->NodeGuid==Mesh.NodeGuid&&Component->SourceGeometryRevision==1);
	}
	TestTrue(TEXT("Junction components create no pillar elements or support identities"),Generated->QueryElements(FEHBElementQuery()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDefinitionWallGenerationTest,"EHB.Geometry.DefinitionWallGeneration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDefinitionWallGenerationTest::RunTest(const FString& Parameters)
{
	UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
	FTransientTopologyFixture Fixture;if(!Fixture.Create(World))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	Fixture.Pillars[0]->SetActorRelativeLocation(FVector(-35,-50,0));
	Building->RefreshWallsConnectedToPillars({Fixture.Pillars[0]->ElementGuid},true);
	struct FMeshState { FTransform Transform; TArray<FTransform> ComponentTransforms; TArray<FProcMeshSection> Sections; };
	auto Capture=[](AEHB_Wall* Wall)
	{
		FMeshState Result;Result.Transform=Wall->GetElementLocalTransform();
		TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Wall);
		for(auto* Mesh:Meshes){Result.ComponentTransforms.Add(Mesh->GetRelativeTransform());for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))Result.Sections.Add(*Section);}
		return Result;
	};
	auto Same=[](const FMeshState& A,const FMeshState& B)
	{
		if(!A.Transform.Equals(B.Transform,0.001)||A.Sections.Num()!=B.Sections.Num()||A.ComponentTransforms.Num()!=B.ComponentTransforms.Num())return false;
		for(int32 I=0;I<A.ComponentTransforms.Num();++I)if(!A.ComponentTransforms[I].Equals(B.ComponentTransforms[I],0.001))return false;
		for(int32 I=0;I<A.Sections.Num();++I){const auto& X=A.Sections[I];const auto& Y=B.Sections[I];if(X.ProcIndexBuffer!=Y.ProcIndexBuffer||X.ProcVertexBuffer.Num()!=Y.ProcVertexBuffer.Num())return false;for(int32 J=0;J<X.ProcVertexBuffer.Num();++J){const auto& V=X.ProcVertexBuffer[J];const auto& W=Y.ProcVertexBuffer[J];if(!V.Position.Equals(W.Position,0.001)||!V.Normal.Equals(W.Normal,0.0001)||!V.UV0.Equals(W.UV0,0.00001)||!V.Tangent.TangentX.Equals(W.Tangent.TangentX,0.0001)||V.Tangent.bFlipTangentY!=W.Tangent.bFlipTangentY)return false;}}
		return true;
	};
	TArray<FMeshState> Original;
	for(auto* Wall:Fixture.Walls){Wall->bGenerateLinkedPillarEndCaps=false;Wall->RebuildWallMesh();Original.Add(Capture(Wall));}
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	if(!TestTrue(TEXT("Prepare angled definition fixture"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded))return false;
	const auto Definitions=Building->PreparedWallNodeDefinitions;
	FString Before;FJsonObjectConverter::UStructToJsonObjectString(Definitions,Before);
	for(int32 I=0;I<Fixture.Walls.Num();++I)
	{
		auto* Wall=Fixture.Walls[I];const uint64 Serial=Wall->GetNodeDefinitionRefreshSerial();Wall->RefreshFromConnectedPillars();
		TestEqual(TEXT("Normal connected refresh dispatches actual definition generation"),Wall->GetNodeDefinitionRefreshSerial(),Serial+1);
		TestTrue(TEXT("Definition bridge preserves complete mesh positions normals UV tangents indices and transforms"),Same(Capture(Wall),Original[I]));
	}
	auto* Wall=Fixture.Walls[0];const auto BeforeInvalid=Capture(Wall);
	auto Invalid=Definitions;Invalid.Nodes[0].GeometryRevision=0;
	TestFalse(TEXT("Malformed definition rejected before geometry writes"),Wall->RefreshFromNodeDefinitions(Invalid));
	TestTrue(TEXT("Malformed input leaves live mesh unchanged"),Same(Capture(Wall),BeforeInvalid));
	Invalid=Definitions;Invalid.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Wall->ElementGuid;})->Thickness+=1;
	TestFalse(TEXT("Definition cannot silently change current wall thickness"),Wall->RefreshFromNodeDefinitions(Invalid));
	Wall->CurveControlOffset=50;
	TestFalse(TEXT("Curved wall never enters plain adapter"),Wall->RefreshFromNodeDefinitions(Definitions));Wall->CurveControlOffset=0;
	TestTrue(TEXT("All rejected requests preserve original generated data"),Same(Capture(Wall),BeforeInvalid));
	const uint64 Serial=Wall->GetNodeDefinitionRefreshSerial();Fixture.Pillars[0]->Width+=16;Fixture.Pillars[0]->Depth+=16;
	Wall->RefreshFromConnectedPillars();
	TestEqual(TEXT("Changed source uses legacy path"),Wall->GetNodeDefinitionRefreshSerial(),Serial);
	const auto Changed=Capture(Wall);TestFalse(TEXT("Changed source dimensions update actual wall mesh"),Same(Changed,Original[0]));
	Building->PreparedWallNodeDefinitions={};Wall->RefreshFromConnectedPillars();
	TestTrue(TEXT("Stale-source fallback matches explicitly unprepared legacy generation"),Same(Capture(Wall),Changed));
	Building->PreparedWallNodeDefinitions=Definitions;
	FString After;FJsonObjectConverter::UStructToJsonObjectString(Building->PreparedWallNodeDefinitions,After);TestEqual(TEXT("Source preparation is not rewritten"),After,Before);
	TArray<FGuid> WallIds,StartIds,EndIds;
	for(auto* Source:Fixture.Walls){WallIds.Add(Source->ElementGuid);StartIds.Add(Source->StartPillarGuid);EndIds.Add(Source->EndPillarGuid);}
	for(int32 I=Fixture.Actors.Num()-1;I>=0;--I)if(IsValid(Fixture.Actors[I])&&!Fixture.Actors[I]->IsActorBeingDestroyed())Fixture.Actors[I]->Destroy();Fixture.Actors.Reset();
	for(int32 I=0;I<WallIds.Num();++I)
	{
		FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Generated=World->SpawnActor<AEHB_Wall>(Params);if(!Generated)return false;Fixture.Actors.Add(Generated);
		Generated->ElementGuid=WallIds[I];Generated->Height=300;Generated->Thickness=20;
		Generated->StartPillarGuid=StartIds[I];Generated->EndPillarGuid=EndIds[I];Generated->bGenerateLinkedPillarEndCaps=false;
		TestNull(TEXT("Fresh mesh adapter has no owning building or physical source"),Generated->OwningBuilding.Get());
		TestTrue(TEXT("Reconstruct wall mesh from values after every source Actor is destroyed"),Generated->RefreshFromNodeDefinitions(Definitions));
		TestTrue(TEXT("Fresh generated mesh equals original angled wall with connected end caps omitted"),Same(Capture(Generated),Original[I]));
		Generated->RebuildWallMesh();TestTrue(TEXT("Subsequent mesh rebuild retains definition-derived faces"),Same(Capture(Generated),Original[I]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDefinitionGeometryBridgeTest,"EHB.Geometry.DefinitionGeometryBridge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDefinitionGeometryBridgeTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);if(!UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded)return false;
	const auto Stored=Building->PreparedWallNodeDefinitions;FString Before;FJsonObjectConverter::UStructToJsonObjectString(Stored,Before);
	TArray<FEHBWallJunctionWallSides> Original,Sides,Expected;FName Reason;FEHBWallJunctionSolveStats Stats;
	if(!TestTrue(TEXT("Persistent value definitions solve"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Stored,Original,Reason)))return false;
	for(auto* Wall:Fixture.Walls){const auto* W=Original.FindByPredicate([&](const auto& V){return V.WallGuid==Wall->ElementGuid;});if(!W)return false;for(bool Left:{true,false}){TArray<FVector> Points;Wall->BuildSideTopPolylineInBuildingSpace(Left,Points);TestTrue(TEXT("Definitions reproduce actual legacy wall side endpoints"),Points.Num()==2&&Points[0].Equals(Left?W->StartLeft:W->StartRight,0.001)&&Points[1].Equals(Left?W->EndLeft:W->EndRight,0.001));}}
	auto Same=[](const auto& A,const auto& B){if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)if(A[I].WallGuid!=B[I].WallGuid||!A[I].LocalTransform.Equals(B[I].LocalTransform,0.001)||!A[I].LocalStart.Equals(B[I].LocalStart,0.001)||!A[I].LocalEnd.Equals(B[I].LocalEnd,0.001)||!A[I].StartLeft.Equals(B[I].StartLeft,0.001)||!A[I].EndLeft.Equals(B[I].EndLeft,0.001)||!A[I].StartRight.Equals(B[I].StartRight,0.001)||!A[I].EndRight.Equals(B[I].EndRight,0.001))return false;return true;};
	TSet<FGuid> Requested;Requested.Add(Fixture.Walls[0]->ElementGuid);TestTrue(TEXT("Definition bridge retains selected solve"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Stored,Sides,Reason,&Requested,&Stats));TestEqual(TEXT("Selected definition geometry count"),Sides.Num(),1);TestEqual(TEXT("Selected endpoint footprint work"),Stats.NodeFootprintsSolved,2);
	const FGuid Id=Fixture.Pillars[0]->ElementGuid;const FVector Target(-30,-40,0);TMap<FGuid,FVector> Positions;Positions.Add(Id,Target);
	auto Candidate=Stored;Candidate.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;})->LocalTransform.SetLocation(Target);UEHBWallTopologyLibrary::BuildPreparedWallSides(Candidate,Expected,Reason);
	TGuardValue<int32> ReuseGuard(EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,0);
	TestTrue(TEXT("Editor candidate path accepts current preparation"),FEasyHouseEditorMode::BuildCandidateRoomWallSides(Building,Positions,Sides));TestEqual(TEXT("Editor actually consumes current persisted definitions"),EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,1);TestTrue(TEXT("Candidate overrides apply only to definition copy"),Same(Sides,Expected));
	Fixture.Pillars[0]->Width=60;Fixture.Pillars[0]->Depth=60;EHBNodeMoveTestHooks::PreparedDefinitionReuseCount=0;
	Candidate.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;})->JunctionDimensions=FVector(60,60,300);const auto OldCandidate=Expected;UEHBWallTopologyLibrary::BuildPreparedWallSides(Candidate,Expected,Reason);
	TestTrue(TEXT("Stale preparation falls back to current source parameters"),FEasyHouseEditorMode::BuildCandidateRoomWallSides(Building,Positions,Sides));TestEqual(TEXT("Stale definitions not reused"),EHBNodeMoveTestHooks::PreparedDefinitionReuseCount,0);TestTrue(TEXT("Fallback geometry follows changed column dimensions"),Same(Sides,Expected)&&!Same(Sides,OldCandidate));
	FString After;FJsonObjectConverter::UStructToJsonObjectString(Building->PreparedWallNodeDefinitions,After);TestEqual(TEXT("Neither branch modifies persisted preparation"),After,Before);Fixture.Pillars[0]->Width=40;Fixture.Pillars[0]->Depth=40;
	auto Invalid=Stored;Invalid.Nodes[0].GeometryRevision=0;Stats.WallSidesSolved=999;TestFalse(TEXT("Invalid definition rejected atomically"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Invalid,Sides,Reason,nullptr,&Stats));TestTrue(TEXT("Invalid definition exposes no partial sides or stale statistics"),Sides.IsEmpty()&&Stats.WallSidesSolved==0);
	for(int32 I=Fixture.Actors.Num()-1;I>=0;--I)if(IsValid(Fixture.Actors[I])&&!Fixture.Actors[I]->IsActorBeingDestroyed())Fixture.Actors[I]->Destroy();Fixture.Actors.Reset();
	TestTrue(TEXT("Saved values still solve after every source Actor is destroyed"),UEHBWallTopologyLibrary::BuildPreparedWallSides(Stored,Sides,Reason));TestTrue(TEXT("Actor-free result remains identical"),Same(Sides,Original));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPreparedNodeDefinitionsTest,"EHB.Topology.PreparedNodeDefinitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPreparedNodeDefinitionsTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	TestEqual(TEXT("Current baseline required before definitions"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("BaselineMigrationRequired")));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	auto DataJson=[&](){FString Json;FJsonObjectConverter::UStructToJsonObjectString(Building->PreparedWallNodeDefinitions,Json);return Json;};
	auto Scene=[&](){FString Value;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);for(auto* E:Building->QueryElements(FEHBElementQuery())){Value+=E->GetElementLocalTransform().ToString();TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(E);for(auto* M:Meshes)for(int32 I=0;I<M->GetNumSections();++I)if(const auto* Section=M->GetProcMeshSection(I)){for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");}}return Value;};
	const auto OriginalScene=Scene(),Empty=DataJson();const int32 Revision=Building->RelationshipGraphRevision;
	TestEqual(TEXT("Definition preview ready"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("Ready")));TestEqual(TEXT("Preview does not write payload"),DataJson(),Empty);
	TArray<TWeakObjectPtr<AActor>> Previous;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))Previous.Add(A);GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	auto Command=[&](bool Apply){FEHBTopologyMigrationResult R;TestTrue(TEXT("Definition command returns JSON"),FJsonObjectConverter::JsonObjectStringToUStruct(UEHBBuildingToolset::PrepareWallNodeDefinitions(Building,Apply),&R));return R;};
	const auto Applied=Command(true);TestTrue(TEXT("Explicit definition preparation applies"),Applied.bSucceeded&&Applied.bChanged);
	TestEqual(TEXT("Prepared payload version"),Building->PreparedWallNodeDefinitions.Version,1);TestEqual(TEXT("All node records stored"),Building->PreparedWallNodeDefinitions.Nodes.Num(),4);TestEqual(TEXT("All physical columns separately bound"),Building->PreparedWallNodeDefinitions.PillarBindings.Num(),4);TestEqual(TEXT("All wall geometry records stored"),Building->PreparedWallNodeDefinitions.Walls.Num(),4);
	TestTrue(TEXT("Stored definitions pass independent validation"),UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Building->PreparedWallNodeDefinitions).IsEmpty());
	const auto Prepared=DataJson();const int32 Queue=GEditor->Trans->GetQueueLength();TestEqual(TEXT("Repeat preparation is idempotent"),Command(true).Status,FName(TEXT("AlreadyPrepared")));TestEqual(TEXT("Repeat creates no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
	TestEqual(TEXT("Preparation does not change live geometry"),Scene(),OriginalScene);TestEqual(TEXT("Preparation does not change relationship revision"),Building->RelationshipGraphRevision,Revision);
	TestTrue(TEXT("Undo definition preparation"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores unprepared payload"),DataJson(),Empty);TestEqual(TEXT("Undo leaves source geometry identical"),Scene(),OriginalScene);TestTrue(TEXT("Redo definition preparation"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores complete definitions and bindings"),DataJson(),Prepared);
	FString Json=Prepared;FEHBPreparedWallNodeDefinitions Restored;TestTrue(TEXT("Definition JSON deserializes"),FJsonObjectConverter::JsonObjectStringToUStruct(Json,&Restored));TestTrue(TEXT("Restored definition validates without any world"),UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Restored).IsEmpty());
	auto Bad=Restored;Bad.PillarBindings.Pop();TestTrue(TEXT("Preparation cannot silently omit physical column"),HasIssue(UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Bad),TEXT("MissingPreparedPillarBinding")));
	Bad=Restored;Bad.Nodes[0].JunctionDimensions.X=0;TestTrue(TEXT("Invalid node dimensions rejected"),HasIssue(UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Bad),TEXT("InvalidNodeDefinition")));
	Bad=Restored;Bad.Walls[0].EndNodeGuid=FGuid::NewGuid();TestTrue(TEXT("Unknown node endpoint rejected"),HasIssue(UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Bad),TEXT("InvalidWallEndpoints")));
	Bad=Restored;Bad.PillarBindings[0].PhysicalPillarGuid=Bad.PillarBindings[1].PhysicalPillarGuid;TestTrue(TEXT("Physical entity cannot bind twice"),HasIssue(UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Bad),TEXT("InvalidPillarBinding")));
	for(int32 Case=0;Case<3;++Case)
	{
		Building->PreparedWallNodeDefinitions=Restored;
		if(Case==0)Building->PreparedWallNodeDefinitions.Nodes[0].GeometryRevision=0;
		if(Case==1)Building->PreparedWallNodeDefinitions.Version=0;
		if(Case==2)Building->PreparedWallNodeDefinitions.Version=99;
		const FString InvalidJson=DataJson();const int32 QueueBeforeInvalid=GEditor->Trans->GetQueueLength();const auto Invalid=Command(true);
		TestFalse(TEXT("Successful source capture cannot make invalid stored preparation succeed"),Invalid.bSucceeded);
		TestEqual(TEXT("Invalid stored payload rejection reason"),Invalid.Status,FName(Case==1?TEXT("UnversionedDefinitions"):TEXT("InvalidPreparedDefinitions")));
		TestEqual(TEXT("Invalid preparation not overwritten"),DataJson(),InvalidJson);TestEqual(TEXT("Invalid preparation creates no undo transaction"),GEditor->Trans->GetQueueLength(),QueueBeforeInvalid);
	}
	Building->PreparedWallNodeDefinitions=Restored;
	Fixture.Pillars[0]->Width+=2;TestEqual(TEXT("Changed column dimensions make preparation stale"),Command(true).Status,FName(TEXT("DefinitionSourceChanged")));TestEqual(TEXT("Stale payload is never overwritten"),DataJson(),Prepared);Fixture.Pillars[0]->Width-=2;
	Fixture.Walls[0]->Thickness+=2;TestEqual(TEXT("Changed wall dimensions make preparation stale"),Command(true).Status,FName(TEXT("DefinitionSourceChanged")));Fixture.Walls[0]->Thickness-=2;
	Fixture.Walls[0]->CurveControlOffset=50;TestEqual(TEXT("Curved wall cannot prepare plain definitions"),Command(true).Status,FName(TEXT("UnsupportedWallSource")));Fixture.Walls[0]->CurveControlOffset=0;
	Fixture.Pillars[0]->ShapeType=EEHBPillarShapeType::Cylinder;TestEqual(TEXT("Nonpolygon column cannot prepare plain definitions"),Command(true).Status,FName(TEXT("UnsupportedNodeSource")));Fixture.Pillars[0]->ShapeType=EEHBPillarShapeType::Polygon;
	TestEqual(TEXT("Unsupported source does not overwrite preparation"),DataJson(),Prepared);
	TestEqual(TEXT("Restored source matches original preparation"),Command(false).Status,FName(TEXT("AlreadyPrepared")));
	{const FScopedTransaction Outer(NSLOCTEXT("EHBTests","OuterNodePreparation","Outer node preparation"));TestEqual(TEXT("Nested definition write rejected"),Command(true).Status,FName(TEXT("RequiresIndependentEditorTransaction")));}
	GEditor->SelectNone(false,true,false);for(const auto& A:Previous)if(A.IsValid())GEditor->SelectActor(A.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeEndpointContractTest,"EHB.Relations.NodeEndpointContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeEndpointContractTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Legacy element enum value preserved"),static_cast<uint8>(EEHBRelationEndpointKind::BuildingElement),static_cast<uint8>(0));TestEqual(TEXT("Legacy external enum value preserved"),static_cast<uint8>(EEHBRelationEndpointKind::ExternalActor),static_cast<uint8>(1));TestEqual(TEXT("Legacy ground enum value preserved"),static_cast<uint8>(EEHBRelationEndpointKind::WorldGround),static_cast<uint8>(2));TestEqual(TEXT("Node enum appended"),static_cast<uint8>(EEHBRelationEndpointKind::WallNode),static_cast<uint8>(3));
	const FGuid Id=FGuid::NewGuid();const auto Node=FEHBElementRelationEndpoint::MakeNode(Id);const auto Element=FEHBElementRelationEndpoint::MakeElement(Id);
	TestTrue(TEXT("Node value endpoint valid without Actor"),Node.IsValid());TestTrue(TEXT("Node identity belongs only to node namespace"),Node.RefersToNode(Id)&&!Node.RefersToElement(Id));TestTrue(TEXT("Element identity belongs only to element namespace"),Element.RefersToElement(Id)&&!Element.RefersToNode(Id));TestFalse(TEXT("Same numerical GUID in different namespaces is not equivalent"),Node.IsEquivalentTo(Element));
	TestFalse(TEXT("Missing node identity invalid"),FEHBElementRelationEndpoint::MakeNode(FGuid()).IsValid());
	auto Ambiguous=Node;Ambiguous.ElementGuid=Id;TestFalse(TEXT("Node carrying an element identity rejected"),Ambiguous.IsValid());Ambiguous=Element;Ambiguous.NodeGuid=Id;TestFalse(TEXT("Legacy endpoint carrying a node identity rejected"),Ambiguous.IsValid());
	FString Json;FJsonObjectConverter::UStructToJsonObjectString(Node,Json);FEHBElementRelationEndpoint Restored;TestTrue(TEXT("Node endpoint JSON deserializes"),FJsonObjectConverter::JsonObjectStringToUStruct(Json,&Restored));TestTrue(TEXT("Node endpoint roundtrip preserves typed identity"),Restored.IsEquivalentTo(Node));
	const auto LegacyJson=FJsonObjectConverter::UStructToJsonObject(Element);LegacyJson->RemoveField(TEXT("nodeGuid"));FEHBElementRelationEndpoint Legacy;
	TestTrue(TEXT("Pre-node endpoint JSON still deserializes"),FJsonObjectConverter::JsonObjectToUStruct(LegacyJson.ToSharedRef(),&Legacy));TestTrue(TEXT("Missing new property defaults to legacy identity"),Legacy.IsValid()&&!Legacy.NodeGuid.IsValid()&&Legacy.IsEquivalentTo(Element));
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;const FGuid SharedId=Fixture.Pillars[0]->ElementGuid;
	FEHBElementRelation Incoming;Incoming.RelationGuid=FGuid::NewGuid();Incoming.Type=EEHBElementRelationType::LogicalDependency;Incoming.Source=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[0]->ElementGuid);Incoming.Target=FEHBElementRelationEndpoint::MakeNode(SharedId);
	const int32 Revision=Building->RelationshipGraphRevision,Queue=GEditor->Trans->GetQueueLength(),Count=Building->ElementRelations.Num();
	for(auto Type:{EEHBElementRelationType::TopologyConnection,EEHBElementRelationType::StructuralSupport,EEHBElementRelationType::HostedElement,EEHBElementRelationType::LogicalDependency})
	{auto Candidate=Incoming;Candidate.Type=Type;TestFalse(TEXT("Node relation cannot activate before node authority migration"),Building->AddOrUpdateElementRelation(Candidate).IsValid());}
	TestEqual(TEXT("Rejected node writes preserve relation revision"),Building->RelationshipGraphRevision,Revision);TestEqual(TEXT("Rejected node writes preserve records"),Building->ElementRelations.Num(),Count);TestEqual(TEXT("Rejected node writes preserve undo queue"),GEditor->Trans->GetQueueLength(),Queue);
	// Raw unowned data is diagnostic-only: verify typed indexes never alias a real pillar GUID.
	FEHBElementRelation Outgoing=Incoming;Outgoing.RelationGuid=FGuid::NewGuid();Outgoing.Source=FEHBElementRelationEndpoint::MakeNode(SharedId);Outgoing.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[1]->ElementGuid);
	FEHBElementRelation Physical=Incoming;Physical.RelationGuid=FGuid::NewGuid();Physical.Source=FEHBElementRelationEndpoint::MakeElement(SharedId);Physical.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[2]->ElementGuid);
	Building->ElementRelations.Add(Incoming);Building->ElementRelations.Add(Outgoing);Building->ElementRelations.Add(Physical);Building->RebuildElementAndRelationshipIndexes();
	FEHBRelationQuery Query;Query.NodeGuid=SharedId;Query.Types={EEHBElementRelationType::LogicalDependency};
	TestEqual(TEXT("Node query finds both diagnostic directions"),Building->QueryElementRelations(Query).Num(),2);
	Query.Direction=EEHBRelationQueryDirection::Incoming;auto Found=Building->QueryElementRelations(Query);TestTrue(TEXT("Node incoming uses its own index"),Found.Num()==1&&Found[0].RelationGuid==Incoming.RelationGuid);
	Query.Direction=EEHBRelationQueryDirection::Outgoing;Found=Building->QueryElementRelations(Query);TestTrue(TEXT("Node outgoing uses its own index"),Found.Num()==1&&Found[0].RelationGuid==Outgoing.RelationGuid);
	Query.ElementGuid=SharedId;TestTrue(TEXT("Ambiguous two-namespace query rejected"),Building->QueryElementRelations(Query).IsEmpty());Query.NodeGuid.Invalidate();Found=Building->QueryElementRelations(Query);TestTrue(TEXT("Same-GUID element query excludes node references"),Found.Num()==1&&Found[0].RelationGuid==Physical.RelationGuid);
	const auto Issues=Building->ValidateElementRelationshipGraph(false,false);TestTrue(TEXT("Unowned node records remain validation errors"),Issues.ContainsByPredicate([&](const auto& I){return I.RelationGuid==Incoming.RelationGuid&&I.Severity==EEHBRelationValidationSeverity::Error;}));
	TestTrue(TEXT("Physical relation removable"),Building->RemoveElementRelation(Physical.RelationGuid));Query.ElementGuid.Invalidate();Query.NodeGuid=SharedId;Query.Direction=EEHBRelationQueryDirection::Both;TestEqual(TEXT("Removing element record does not remove same-GUID node records"),Building->QueryElementRelations(Query).Num(),2);
	TestTrue(TEXT("Node incoming record removable from its own index"),Building->RemoveElementRelation(Incoming.RelationGuid));TestEqual(TEXT("Node outgoing remains after incoming removal"),Building->QueryElementRelations(Query).Num(),1);
	Building->RebuildElementAndRelationshipIndexes();TestEqual(TEXT("Node index restores on rebuild"),Building->QueryElementRelations(Query).Num(),1);
	Building->ClearAllElements();TestTrue(TEXT("Whole-building clear resets node indexes too"),Building->QueryElementRelations(Query).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPillarSeparationPreviewTest,"EHB.Topology.PillarSeparationPreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPillarSeparationPreviewTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;auto* Pillar=Fixture.Pillars[0];
	Building->SetActorRotation(FRotator(0,37,0));
	FString Original,Relations;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Original);
	const int32 Revision=Building->RelationshipGraphRevision,Queue=GEditor->Trans->GetQueueLength();const auto Footprint=Pillar->PolygonPillarVertices;const auto Pose=Pillar->GetElementLocalTransform();
	const auto Plan=UEHBWallTopologyLibrary::PreviewPillarSeparation(Building,Pillar->ElementGuid);
	TestTrue(TEXT("Connected native pillar inspected"),Plan.bSucceeded);TestFalse(TEXT("Inspection never authorizes physical deletion"),Plan.bCommitAvailable);
	TestEqual(TEXT("Explicit migration blocker"),Plan.Status,FName(TEXT("NodeAuthorityMigrationRequired")));
	TestEqual(TEXT("Existing node identity retained"),Plan.RetainedNodeGuid,Pillar->ElementGuid);TestEqual(TEXT("Physical entity separately identified"),Plan.PhysicalPillarGuid,Pillar->ElementGuid);
	TestTrue(TEXT("Proposal distinguishes physical 40cm column from wall-derived 20cm junction"),Plan.PhysicalDimensions.Equals(FVector(40,40,300))&&Plan.ProposedJunctionDimensions.Equals(FVector(20,20,300)));
	TestEqual(TEXT("Both incident walls must survive future separation"),Plan.RetainedWallGuids.Num(),2);TestEqual(TEXT("Both connection records require retargeting"),Plan.TopologyRelationGuids.Num(),2);
	FString After;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),After);
	TestEqual(TEXT("Inspection leaves topology unchanged"),After,Original);TestEqual(TEXT("Inspection leaves relation revision unchanged"),Building->RelationshipGraphRevision,Revision);TestEqual(TEXT("Inspection creates no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
	TestTrue(TEXT("Inspection leaves geometry and transform unchanged"),Pillar->PolygonPillarVertices==Footprint&&Pillar->GetElementLocalTransform().Equals(Pose));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Rail=Building->GetWorld()->SpawnActor<AEHB_Railing>(AEHB_Railing::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Rail)return false;Fixture.Actors.Add(Rail);Rail->AttachToBuilding(Building,FTransform::Identity);Rail->StartAnchor.ElementGuid=Pillar->ElementGuid;
	FEHBElementRelation Relation;Relation.RelationGuid=FGuid::NewGuid();Relation.Type=EEHBElementRelationType::LogicalDependency;Relation.Source=FEHBElementRelationEndpoint::MakeElement(Pillar->ElementGuid);Relation.Target=FEHBElementRelationEndpoint::MakeElement(Rail->ElementGuid);Building->ElementRelations.Add(Relation);
	const auto WithAnchor=UEHBWallTopologyLibrary::PreviewPillarSeparation(Building,Pillar->ElementGuid);
	TestTrue(TEXT("Raw enabled physical records disclosed without index repair"),WithAnchor.OtherRelationGuids.Contains(Relation.RelationGuid));TestTrue(TEXT("Legacy railing anchor disclosed independently of relation indexing"),WithAnchor.AnchoredRailingGuids.Contains(Rail->ElementGuid));TestTrue(TEXT("Accessory remains unplanned"),WithAnchor.UnplannedElementGuids.Contains(Rail->ElementGuid));
	TestTrue(TEXT("Physical relation policy explicit"),HasIssue(WithAnchor.Issues,TEXT("PhysicalRelationsNeedPolicy")));TestTrue(TEXT("Railing policy explicit"),HasIssue(WithAnchor.Issues,TEXT("RailingAnchorNeedsPolicy")));
	Building->ElementRelations.Last().bEnabled=false;TestFalse(TEXT("Disabled relation excluded from transfer inventory"),UEHBWallTopologyLibrary::PreviewPillarSeparation(Building,Pillar->ElementGuid).OtherRelationGuids.Contains(Relation.RelationGuid));
	Pillar->ShapeType=EEHBPillarShapeType::Cylinder;const auto Unsupported=UEHBWallTopologyLibrary::PreviewPillarSeparation(Building,Pillar->ElementGuid);TestTrue(TEXT("Custom junction shape does not receive a guessed replacement"),Unsupported.ProposedJunctionDimensions.IsZero()&&HasIssue(Unsupported.Issues,TEXT("JunctionShapeNotPlanned")));Pillar->ShapeType=EEHBPillarShapeType::Polygon;
	TestFalse(TEXT("Unknown pillar rejected"),UEHBWallTopologyLibrary::PreviewPillarSeparation(Building,FGuid::NewGuid()).bSucceeded);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))PreviousSelection.Add(Actor);
	GEditor->SelectNone(false,true,false);TestTrue(TEXT("AI adapter requires explicit selected building"),UEHBBuildingToolset::PreviewPillarSeparation(Building,Pillar->ElementGuid).Contains(TEXT("TargetNotSelected")));
	GEditor->SelectActor(Building,true,false);TestTrue(TEXT("Selected adapter exports migration blocker"),UEHBBuildingToolset::PreviewPillarSeparation(Building,Pillar->ElementGuid).Contains(TEXT("NodeAuthorityMigrationRequired")));GEditor->SelectNone(false,true,false);for(const auto& Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
	// Destructive behavior is verified only on a separate transient fixture, never on user assets.
	FTransientTopologyFixture DeleteFixture;if(!DeleteFixture.Create(Building->GetWorld()))return false;
	const auto Legacy=UEHBWallTopologyLibrary::PreviewPillarSeparation(DeleteFixture.Building,DeleteFixture.Pillars[0]->ElementGuid);
	DeleteFixture.Pillars[0]->Destroy();
	for(auto* Wall:DeleteFixture.Walls)TestEqual(TEXT("Legacy deletion destroys exactly the walls the new preparation says to retain"),Wall->IsActorBeingDestroyed(),Legacy.RetainedWallGuids.Contains(Wall->ElementGuid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallMoveUpdatePlanTest,"EHB.Topology.WallMoveUpdatePlan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallMoveUpdatePlanTest::RunTest(const FString& Parameters)
{
	auto Graph=MakeDetachedGraph();
	for(int32 I=3;I<6;++I){auto& N=Graph.Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid(0,0,0,I+1);N.LocalPosition=FVector(I*400+400,0,0);N.FloorIndex=1;auto& W=Graph.Walls.AddDefaulted_GetRef();W.WallGuid=FGuid(1,0,0,I);W.StartNodeGuid=Graph.Nodes[I-1].NodeGuid;W.EndNodeGuid=N.NodeGuid;}
	const FGuid Root=Graph.Nodes[0].NodeGuid;
	auto Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Graph,{Root,Root});
	TestTrue(TEXT("Detached value graph plans without pillar Actors"),Plan.bSucceeded);TestEqual(TEXT("Duplicate moved roots canonicalized"),Plan.MovedNodeGuids.Num(),1);
	TestEqual(TEXT("Only root and immediate neighbor junctions"),Plan.JunctionNodeGuids.Num(),2);TestEqual(TEXT("Neighbor outgoing wall also included"),Plan.WallGuids.Num(),2);
	TestTrue(TEXT("Second wall is planned even though it does not touch the moved root"),Plan.WallGuids.Contains(Graph.Walls[1].WallGuid));TestFalse(TEXT("Distant connected wall excluded"),Plan.WallGuids.Contains(Graph.Walls.Last().WallGuid));
	const auto Original=Plan;
	Swap(Graph.Nodes[0],Graph.Nodes.Last());Swap(Graph.Walls[0],Graph.Walls.Last());Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Graph,{Root});
	TestTrue(TEXT("Graph order preserves canonical plan"),Plan.MovedNodeGuids==Original.MovedNodeGuids&&Plan.JunctionNodeGuids==Original.JunctionNodeGuids&&Plan.WallGuids==Original.WallGuids);
	Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Graph,{});TestTrue(TEXT("Empty root set valid and empty"),Plan.bSucceeded&&Plan.JunctionNodeGuids.IsEmpty()&&Plan.WallGuids.IsEmpty());
	Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Graph,{FGuid::NewGuid()});TestFalse(TEXT("Unknown root rejected"),Plan.bSucceeded);TestTrue(TEXT("Rejected plan has no dispatches"),Plan.JunctionNodeGuids.IsEmpty()&&Plan.WallGuids.IsEmpty());
	auto Bad=Graph;Bad.Walls[0].EndNodeGuid=FGuid::NewGuid();TestFalse(TEXT("Missing graph endpoint rejected"),UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Bad,{Root}).bSucceeded);
	Bad=Graph;Bad.Nodes.Add(Graph.Nodes[0]);TestFalse(TEXT("Duplicate graph node rejected"),UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Bad,{Root}).bSucceeded);
	Bad=Graph;Bad.Walls.Add(Graph.Walls[0]);TestFalse(TEXT("Duplicate graph wall rejected"),UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Bad,{Root}).bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallMoveNeighborhoodTest,"EHB.Topology.WallMoveNeighborhood",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallMoveNeighborhoodTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Previous=Fixture.Pillars[2];
	for(FVector P:{FVector(1000,700,0),FVector(1400,500,0),FVector(1800,700,0)})
	{
		auto* Pillar=Building->GetWorld()->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Pillar)return false;Fixture.Actors.Add(Pillar);
		Pillar->AttachToBuilding(Building,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
		auto* Wall=Building->ConnectPillars(Previous,Pillar,300,20);if(!Wall)return false;
		Wall->SetFlags(RF_Transient);Fixture.Actors.Add(Wall);Previous=Pillar;
	}
	auto Snapshot=[&]()
	{
		Building->RebuildClosedLoops();FString Value,Json;
		FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString()+Element->GetElementLocalTransform().ToString();
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
			{
				for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();
				for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
			}
		}
		return Value;
	};

	const FGuid A=Fixture.Pillars[0]->ElementGuid,B=Fixture.Pillars[1]->ElementGuid;
	auto Place=[&](int32 Count,FVector Delta){for(int32 I=0;I<2;++I){auto T=Fixture.Pillars[I]->GetElementLocalTransform();T.SetLocation(FVector(I*600,0,0)+(I<Count?Delta:FVector::ZeroVector));Fixture.Pillars[I]->SetActorRelativeTransform(T);}};
	for(int32 Count:{1,2})
	{
		Place(Count,FVector(30,-70,0));const auto Full=Building->RefreshWallsConnectedToPillars({A,B});const FString Expected=Snapshot();
		TestEqual(TEXT("Full connected graph visits seven nodes"),Full.PillarsVisited,7);TestEqual(TEXT("Full component performs fourteen endpoint dispatches"),Full.WallRefreshCalls,14);
		Place(0,FVector::ZeroVector);Building->RefreshWallsConnectedToPillars({A,B});
		Place(Count,FVector(30,-70,0));TArray<FGuid> Roots={A};if(Count==2)Roots.Add(B);Roots.Add(A);Roots.Add(FGuid());Roots.Add(FGuid::NewGuid());
		TArray<FGuid> PlannedRoots={A};if(Count==2)PlannedRoots.Add(B);
		const auto Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(UEHBWallTopologyLibrary::CaptureWallTopology(Building),PlannedRoots);
		TestTrue(TEXT("Live movement has explicit dispatch plan"),Plan.bSucceeded);
		auto BadPlan=Plan;BadPlan.WallGuids.Pop();const auto BeforeRejected=Snapshot();
		const auto Rejected=Building->RefreshWallMoveNeighborhood(PlannedRoots,true,&BadPlan);
		TestFalse(TEXT("Incomplete wall plan rejected before dispatch"),Rejected.bSucceeded);TestEqual(TEXT("Rejected plan performs no wall refresh"),Rejected.WallRefreshCalls,0);TestEqual(TEXT("Rejected dispatch leaves full scene unchanged"),Snapshot(),BeforeRejected);
		BadPlan=Plan;const FGuid DuplicatePlannedNode=BadPlan.JunctionNodeGuids[0];BadPlan.JunctionNodeGuids.Add(DuplicatePlannedNode);TestFalse(TEXT("Duplicate planned node rejected"),Building->RefreshWallMoveNeighborhood(PlannedRoots,true,&BadPlan).bSucceeded);
		TestFalse(TEXT("Unknown requested root cannot enter a reviewed plan"),Building->RefreshWallMoveNeighborhood(Roots,true,&Plan).bSucceeded);
		const auto Local=Building->RefreshWallMoveNeighborhood(PlannedRoots,true,&Plan);TestTrue(TEXT("Matching planned neighborhood accepted"),Local.bSucceeded);
		TestEqual(TEXT("Neighborhood result matches full topology and every generated mesh"),Snapshot(),Expected);
		TestEqual(TEXT("Only moved-node neighborhood visited"),Local.PillarsVisited,Count==1?3:4);
		TestEqual(TEXT("Only incident dispatches performed"),Local.WallRefreshCalls,Count==1?6:9);
		if(Count==2){Place(0,FVector::ZeroVector);Building->RefreshWallsConnectedToPillars({A,B});Place(Count,FVector(30,-70,0));Building->RefreshWallMoveNeighborhood({B,A});TestEqual(TEXT("Reversed moved roots preserve final geometry"),Snapshot(),Expected);}
		Place(0,FVector::ZeroVector);Building->RefreshWallsConnectedToPillars({A,B});
	}
	const auto Before=Snapshot();const auto Empty=Building->RefreshWallMoveNeighborhood({FGuid(),FGuid::NewGuid()});
	TestEqual(TEXT("Invalid roots perform no node work"),Empty.PillarsVisited,0);TestEqual(TEXT("Invalid roots perform no wall work"),Empty.WallRefreshCalls,0);TestEqual(TEXT("Empty neighborhood leaves scene untouched"),Snapshot(),Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBatchedWallRefreshTest,"EHB.Topology.BatchedConnectedRefresh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBatchedWallRefreshTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	TArray<AEHB_Pillar*> Island;
	for(FVector P:{FVector(2000,0,0),FVector(2600,0,0)})
	{
		auto* Pillar=Building->GetWorld()->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Pillar)return false;Fixture.Actors.Add(Pillar);Island.Add(Pillar);
		Pillar->AttachToBuilding(Building,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	}
	auto* IslandWall=Building->ConnectPillars(Island[0],Island[1],300,20);
	if(!IslandWall)return false;IslandWall->SetFlags(RF_Transient);Fixture.Actors.Add(IslandWall);
	auto Snapshot=[&]()
	{
		Building->RebuildClosedLoops();FString Value,Json;
		FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString()+Element->GetElementLocalTransform().ToString();
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
			{
				for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();
				for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
			}
		}
		return Value;
	};
	auto Place=[&](FVector Delta)
	{
		for(int32 I=0;I<2;++I){FTransform T=Fixture.Pillars[I]->GetElementLocalTransform();T.SetLocation(FVector(I*600,0,0)+Delta);Fixture.Pillars[I]->SetActorRelativeTransform(T);}
	};
	const FGuid A=Fixture.Pillars[0]->ElementGuid,B=Fixture.Pillars[1]->ElementGuid;
	Place(FVector(30,-70,0));
	Building->RefreshWallsConnectedToPillar(A);Building->RefreshWallsConnectedToPillar(B);
	const FString RepeatedResult=Snapshot();
	Place(FVector::ZeroVector);Building->RefreshWallsConnectedToPillar(A);Building->RefreshWallsConnectedToPillar(B);
	Place(FVector(30,-70,0));
	const auto Stats=Building->RefreshWallsConnectedToPillars({A,B,A,FGuid(),FGuid::NewGuid()});
	TestEqual(TEXT("Batched final topology and meshes match sequential legacy roots"),Snapshot(),RepeatedResult);
	TestEqual(TEXT("Only connected room pillars visited once"),Stats.PillarsVisited,4);
	TestEqual(TEXT("Per-endpoint wall work retained once per component"),Stats.WallRefreshCalls,8);
	const auto Again=Building->RefreshWallsConnectedToPillars({B,A});
	TestEqual(TEXT("Root order keeps same completed geometry"),Snapshot(),RepeatedResult);
	TestEqual(TEXT("Reversed roots do not repeat component work"),Again.WallRefreshCalls,8);
	const auto Empty=Building->RefreshWallsConnectedToPillars({});TestEqual(TEXT("No empty refresh work"),Empty.WallRefreshCalls,0);
	const auto Both=Building->RefreshWallsConnectedToPillars({A,Island[0]->ElementGuid,Island[1]->ElementGuid});
	TestEqual(TEXT("Explicit disconnected root adds just its component"),Both.PillarsVisited,6);
	TestEqual(TEXT("Explicit disconnected wall dispatches"),Both.WallRefreshCalls,10);
	TestEqual(TEXT("All component refresh retains geometry"),Snapshot(),RepeatedResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomSlabFollowingTest,"EHB.Topology.RoomSlabFollowing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomSlabFollowingTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	auto* Slab=Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Floor||!Slab)return false;Fixture.Actors.Append({Floor,Slab});
	const auto Room=Building->GetClosedLoopsByFloor(1)[0];
	if(!Floor->ConfigureFromRoomLoop(Building,Room,0,true))return false;
	Slab->ConfigureDefaultSlab(Building,FTransform(FRotator(0,17,0),FVector(300,250,300)),200,20,false);
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	if(!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))return false;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))PreviousSelection.Add(Actor);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	const auto OriginalSlabTransform=Slab->GetElementLocalTransform();
	TArray<FVector> OriginalBuildingPolygon;for(const auto& P:Slab->LocalTopPolygon)OriginalBuildingPolygon.Add(OriginalSlabTransform.TransformPosition(P));
	auto Snapshot=[&]()
	{
		FString Value,Json;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);
		for(auto* Element:Building->QueryElements(FEHBElementQuery()))
		{
			Value+=Element->ElementGuid.ToString();
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
			{
				for(const auto& V:Section->ProcVertexBuffer)Value+=V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();
				for(uint32 Index:Section->ProcIndexBuffer)Value+=FString::FromInt(Index)+TEXT(",");
			}
		}
		for(const FVector& P:Slab->LocalTopPolygon)Value+=P.ToString();
		for(const auto& R:Floor->FloorRegions){FJsonObjectConverter::UStructToJsonObjectString(R,Json);Value+=Json;}
		Value+=Slab->RoomFillLoopGuid.ToString()+FString::FromInt(Slab->RoomFillFloorIndex)+Slab->GetElementLocalTransform().ToString()
			+(Slab->IsRecordedOutlineUnchanged()?TEXT("slabCurrent"):TEXT("slabModified"))+(Floor->IsRecordedOutlineUnchanged()?TEXT("floorCurrent"):TEXT("floorModified"));
		return Value;
	};
	auto Move=[&](bool Preview=false){return UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),FVector(0,-70,0),Preview);};
	const FString Original=Snapshot();const int32 QueueCount=GEditor->Trans->GetQueueLength();
	const auto Preview=Move(true);TestTrue(*Preview.Message,Preview.bSucceeded);
	TestTrue(TEXT("Preflight includes ceiling"),Preview.Message.Contains(TEXT("FollowingSlabs=1")));
	TestEqual(TEXT("Ceiling preflight is read only"),Snapshot(),Original);TestEqual(TEXT("No preview transaction"),GEditor->Trans->GetQueueLength(),QueueCount);
	const auto Applied=Move();TestTrue(*Applied.Message,Applied.bSucceeded);
	TestTrue(TEXT("Ceiling keeps original location orientation and height"),Slab->GetElementLocalTransform().Equals(OriginalSlabTransform,0.001));
	for(FVector Target:OriginalBuildingPolygon)
	{
		if(Target.Y<250)Target.Y-=70;
		TestTrue(TEXT("Ceiling preserves wall-side inset while lower boundary follows"),Slab->LocalTopPolygon.ContainsByPredicate([&](const FVector& P){return OriginalSlabTransform.TransformPosition(P).Equals(Target,0.001);}));
	}
	TestTrue(TEXT("Ceiling generation signature refreshed"),Slab->IsRecordedOutlineUnchanged());
	TestTrue(TEXT("Floor generation signature refreshed"),Floor->IsRecordedOutlineUnchanged());
	TestEqual(TEXT("Ceiling room remains the same"),Slab->RoomFillLoopGuid,Room.LoopGuid);
	const FString Moved=Snapshot();
	TestTrue(TEXT("Undo floor ceiling and wall together"),GEditor->UndoTransaction());TestEqual(TEXT("Undo complete surface geometry"),Snapshot(),Original);
	TestTrue(TEXT("Redo floor ceiling and wall together"),GEditor->RedoTransaction());TestEqual(TEXT("Redo complete surface geometry"),Snapshot(),Moved);
	GEditor->UndoTransaction(false);
	const auto Angled=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),FVector(30,-70,0),true);
	TestTrue(TEXT("Changed room wall directions now share candidate junction solver"),Angled.bSucceeded);
	TestEqual(TEXT("Angled preflight preserves complete result"),Snapshot(),Original);
	const auto AngledApplied=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),FVector(30,-70,0));
	TestTrue(*AngledApplied.Message,AngledApplied.bSucceeded);
	TestTrue(TEXT("Angled follow keeps slab transform"),Slab->GetElementLocalTransform().Equals(OriginalSlabTransform,0.001));
	TestTrue(TEXT("Angled follow refreshes generation signature"),Slab->IsRecordedOutlineUnchanged());
	for(const FVector Target:{FVector(30,-70,0),FVector(630,-70,0)})TestTrue(TEXT("Angled ground floor follows moved wall ends"),Floor->FloorRegions[0].OuterPolygon.ContainsByPredicate([&](const auto& P){return P.Equals(Target,0.001);}));
	const FString AngledState=Snapshot();
	TestTrue(TEXT("Angled follow undo"),GEditor->UndoTransaction());TestEqual(TEXT("Angled follow undo restores complete geometry"),Snapshot(),Original);
	TestTrue(TEXT("Angled follow redo"),GEditor->RedoTransaction());TestEqual(TEXT("Angled follow redo restores complete geometry"),Snapshot(),AngledState);
	GEditor->UndoTransaction(false);
	const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Building);
	const auto* Corner=Graph.Nodes.FindByPredicate([](const auto& N){return N.LocalPosition.Equals(FVector::ZeroVector,0.001);});
	if(Corner)
	{
		const auto NodeMove=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Building,Corner->NodeGuid,Corner->LocalPosition,FVector(-30,-40,0));
		TestTrue(TEXT("Single corner change follows room ceiling"),NodeMove.bSucceeded);
		TestTrue(TEXT("Single corner ceiling source remains current"),Slab->IsRecordedOutlineUnchanged());
		if(NodeMove.bSucceeded){TestTrue(TEXT("Single corner edit undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Single corner undo restores surfaces"),Snapshot(),Original);}
	}
	else AddError(TEXT("Original room corner missing"));
	const auto Polygon=Slab->LocalTopPolygon;Slab->LocalTopPolygon[0].X+=5;
	const FString Manual=Snapshot();TestFalse(TEXT("Manual ceiling shape is protected"),Move().bSucceeded);TestEqual(TEXT("Rejected edit leaves manual ceiling and floor intact"),Snapshot(),Manual);Slab->LocalTopPolygon=Polygon;
	Slab->RoomFillFloorIndex=2;TestFalse(TEXT("Stale ceiling binding is protected"),Move().bSucceeded);Slab->RoomFillFloorIndex=1;
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);TestFalse(TEXT("Non-ceiling role not implicitly followed"),Move().bSucceeded);Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	for(auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline})
	{
		TGuardValue<EHBNodeMoveTestHooks::EFailurePhase> Guard(EHBNodeMoveTestHooks::FailurePhase,Phase);
		const auto Failed=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),FVector(30,-70,0));TestEqual(TEXT("Combined surface failure rolled back"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
		TestEqual(TEXT("Failure seam reached"),static_cast<uint8>(EHBNodeMoveTestHooks::FailurePhase),static_cast<uint8>(EHBNodeMoveTestHooks::EFailurePhase::None));
		TestEqual(TEXT("Failure restores wall floor ceiling geometry and source"),Snapshot(),Original);
		TestEqual(TEXT("Failure restores baseline"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
		TestFalse(TEXT("Failed combined edit cannot redo"),GEditor->Trans->CanRedo());
	}
	GEditor->SelectNone(false,true,false);for(const auto& Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabOutlineAtomicTest,"EHB.Topology.SlabOutlineAtomic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabOutlineAtomicTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Slab=Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Slab)return false;Fixture.Actors.Add(Slab);
	Slab->ConfigureDefaultSlab(Building,FTransform(FVector(300,250,300)),200,20,false);
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	if(!TestTrue(TEXT("Initial preview cutter"),Slab->AddSquarePreviewCutterInEditor()))return false;
	auto Snapshot=[&]()
	{
		FString Value,Json;
		for(const FVector& P:Slab->LocalTopPolygon)Value+=P.ToString();
		for(const auto& H:Slab->LocalHoles){FJsonObjectConverter::UStructToJsonObjectString(H,Json);Value+=Json;}
		for(const auto& C:Slab->PreviewCutters){FJsonObjectConverter::UStructToJsonObjectString(C,Json);Value+=Json;}
		Value+=Slab->RoomFillLoopGuid.ToString()+FString::FromInt(Slab->RoomFillFloorIndex)+FString::FromInt(static_cast<int32>(Slab->OutlineSource))+(Slab->IsRecordedOutlineUnchanged()?TEXT("current"):TEXT("modified"));
		if(const auto* Surface=Cast<UEHBPlanarSurfaceComponent>(Slab->MeshComponent))
		{
			for(const FVector& P:Surface->LocalBoundaryLoop)Value+=P.ToString();
			for(const auto& H:Surface->LocalHoleLoops){FJsonObjectConverter::UStructToJsonObjectString(H,Json);Value+=Json;}
		}
		for(int32 I=0;I<Slab->MeshComponent->GetNumSections();++I)if(const auto* Section=Slab->MeshComponent->GetProcMeshSection(I))
		{
			FEHBMeshSection Copy=*Section;Copy.Revision=0;
			FJsonObjectConverter::UStructToJsonObjectString(Copy,Json);Value+=Json;
		}
		return Value;
	};
	const FString Original=Snapshot();
	Slab->Thickness=0;
	TestFalse(TEXT("Actual room fill rejects invalid geometry parameters"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab));
	TestEqual(TEXT("Failed room fill preserves outline cutter mesh surface metadata and room"),Snapshot(),Original);
	Slab->Thickness=20;
	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","SlabRoomReplacement","Replace room slab"));
		TestTrue(TEXT("Actual room fill succeeds atomically"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab));
	}
	TestEqual(TEXT("Successful fill discards preview cutters"),Slab->PreviewCutters.Num(),0);
	TestTrue(TEXT("Successful fill records generated source"),Slab->IsRecordedOutlineUnchanged());
	TestEqual(TEXT("Successful fill binds exact room"),Slab->RoomFillLoopGuid,Building->GetClosedLoopsByFloor(1)[0].LoopGuid);
	const FString Filled=Snapshot();
	TestTrue(TEXT("Undo room fill"),GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores prior cutter and complete mesh"),Snapshot(),Original);
	TestTrue(TEXT("Redo room fill"),GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores generated source and complete mesh"),Snapshot(),Filled);
	const auto Outline=Slab->LocalTopPolygon;
	const int32 QueueCount=GEditor->Trans->GetQueueLength();
	TestTrue(TEXT("Valid outline readonly preflight"),Slab->ValidateSlabOutline(Outline,{}));
	TestEqual(TEXT("Validation leaves all data unchanged"),Snapshot(),Filled);
	TestEqual(TEXT("Validation has no transaction"),GEditor->Trans->GetQueueLength(),QueueCount);
	TestFalse(TEXT("Empty outline rejected"),Slab->SetSlabOutline({},{}));
	auto Bad=Outline;Bad[0].Z=1;
	TestFalse(TEXT("Nonplanar input rejected"),Slab->SetSlabOutline(Bad,{}));
	Bad=Outline;Bad[0].X=1.e12;
	TestFalse(TEXT("Unbounded clip coordinate rejected"),Slab->SetSlabOutline(Bad,{}));
	FEHBFloorSlabHole Malformed;Malformed.LocalPolygon={FVector(0,0,0),FVector(1,0,0),FVector(2,0,0)};
	TestFalse(TEXT("Degenerate hole not silently ignored"),Slab->SetSlabOutline(Outline,{Malformed}));
	FEHBFloorSlabHole Stripe;Stripe.LocalPolygon={FVector(-10,-1000,0),FVector(10,-1000,0),FVector(10,1000,0),FVector(-10,1000,0)};
	TestEqual(TEXT("All rejected replacements preserve complete prior result"),Snapshot(),Filled);
	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","SplitSlabOutline","Split slab outline"));
		TestTrue(TEXT("Disconnected cut replacement preserves both pieces"),Slab->SetSlabOutline(Outline,{Stripe}));
	}
	TArray<FEHBPlanarSurfaceRegion> SplitRegions;
	TestTrue(TEXT("Query complete split result"),Slab->BuildEffectiveDisplayRegions(SplitRegions));
	TestEqual(TEXT("Two cut fragments"),SplitRegions.Num(),2);
	TestEqual(TEXT("Component retains both fragments"),CastChecked<UEHBPlanarSurfaceComponent>(Slab->MeshComponent)->GetPlanarSurfaceRegions().Num(),2);
	TestTrue(TEXT("Undo cut replacement"),GEditor->UndoTransaction(false));
	TestEqual(TEXT("Undo split recovers complete filled state"),Snapshot(),Filled);
	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","ManualSlabOutline","Manual slab outline"));
		auto Manual=Outline;for(auto& P:Manual){P.X*=0.8;P.Y*=0.8;}
		TestTrue(TEXT("Manual outline replacement succeeds"),Slab->SetSlabOutline(Manual,{}));
	}
	TestEqual(TEXT("Manual replacement clears generated intent"),Slab->OutlineSource,EEHBOutlineSource::ManualOrUnclassified);
	TestTrue(TEXT("Undo manual replacement"),GEditor->UndoTransaction(false));
	TestEqual(TEXT("Undo recovers generated basis mesh and surface metadata"),Snapshot(),Filled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOutlineProvenanceTest, "EHB.Topology.OutlineProvenance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBOutlineProvenanceTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Floor)return false;Fixture.Actors.Add(Floor);
	TestFalse(TEXT("New/legacy floor has no confirmed generated outline"),Floor->IsRecordedOutlineUnchanged());
	const auto Room=Building->GetClosedLoopsByFloor(1)[0];
	TestTrue(TEXT("Configure room floor"),Floor->ConfigureFromRoomLoop(Building,Room,0,false));
	TestEqual(TEXT("Boundary source recorded"),Floor->OutlineSource,EEHBOutlineSource::RoomBoundary);
	TestTrue(TEXT("Successful generated floor matches signature"),Floor->IsRecordedOutlineUnchanged());
	const auto Regions=Floor->FloorRegions;
	Floor->FloorRegions[0].OuterPolygon[0].X+=5;
	TestFalse(TEXT("Direct geometry edits invalidate generation signature"),Floor->IsRecordedOutlineUnchanged());
	Floor->FloorRegions=Regions;
	TestTrue(TEXT("Restoring exact design recovers signature"),Floor->IsRecordedOutlineUnchanged());
	Floor->VisualOffset+=0.2f;Floor->RebuildFloorMesh();
	TestTrue(TEXT("Display lift does not change logical outline provenance"),Floor->IsRecordedOutlineUnchanged());
	Floor->SetActorRelativeLocation(FVector(1,0,0));
	TestFalse(TEXT("Direct transform edit detected"),Floor->IsRecordedOutlineUnchanged());
	Floor->SetActorRelativeLocation(FVector::ZeroVector);
	TestFalse(TEXT("Invalid region replacement rejected"),Floor->SetFloorRegions({},false));
	TestTrue(TEXT("Failed replacement preserves source signature"),Floor->IsRecordedOutlineUnchanged());
	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","ManualFloorOrigin","Manual floor outline"));
		TestTrue(TEXT("Manual region edit accepted"),Floor->SetFloorRegions(Regions,false));
	}
	TestEqual(TEXT("Manual setter records manual intent"),Floor->OutlineSource,EEHBOutlineSource::ManualOrUnclassified);
	TestFalse(TEXT("Manual edit is never considered generated unchanged"),Floor->IsRecordedOutlineUnchanged());
	TestTrue(TEXT("Undo restores floor provenance"),GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores saved generation signature"),Floor->IsRecordedOutlineUnchanged());
	GEditor->RedoTransaction();TestFalse(TEXT("Redo restores manual intent"),Floor->IsRecordedOutlineUnchanged());GEditor->UndoTransaction();
	auto* Slab=Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Slab)return false;Fixture.Actors.Add(Slab);
	Slab->ConfigureDefaultSlab(Building,FTransform(FVector(300,250,300)),200,20,false);
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	Slab->bHasRoomFillAnchor=true;Slab->RoomFillAnchorWallGuid=Fixture.Walls[0]->ElementGuid;
	TestFalse(TEXT("Placement anchor does not authorize generated outline"),Slab->IsRecordedOutlineUnchanged());
	TestTrue(TEXT("Actual slab room fill succeeds"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab));
	TestEqual(TEXT("Slab persists exact selected room"),Slab->RoomFillLoopGuid,Room.LoopGuid);
	TestTrue(TEXT("Slab generated outline is current"),Slab->IsRecordedOutlineUnchanged());
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	auto Preview=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[0]->ElementGuid,FVector::ZeroVector,FVector(-30,20,0));
	if(TestEqual(TEXT("One affected room"),Preview.RoomBoundaryChanges.Num(),1))
	{
		TestTrue(TEXT("Slab resolved as exact room binding"),Preview.RoomBoundaryChanges[0].BoundRoomSlabGuids.Contains(Slab->ElementGuid));
		TestTrue(TEXT("Resolved slab no longer ambiguous"),Preview.RoomBoundaryChanges[0].CandidateAnchoredSlabGuids.IsEmpty());
	}
	Slab->LocalTopPolygon[0].X+=5;
	TestFalse(TEXT("Uninstrumented slab corner edit detected"),Slab->IsRecordedOutlineUnchanged());
	Preview=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[0]->ElementGuid,FVector::ZeroVector,FVector(-30,20,0));
	TestTrue(TEXT("Planner discloses changed slab outline"),Preview.RoomBoundaryChanges.ContainsByPredicate([&](const auto& C){return C.ModifiedOrUnclassifiedOutlineGuids.Contains(Slab->ElementGuid);}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomMoveDependenciesTest, "EHB.Topology.RoomMoveDependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBRoomMoveDependenciesTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))return false;
	auto* Building=Fixture.Building;
	Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	TArray<AEHB_Pillar*> Added;
	for(const FVector P:{FVector(1000,0,0),FVector(1000,500,0)})
	{
		auto* Pillar=Building->GetWorld()->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Building->GetActorTransform().TransformPosition(P),FRotator::ZeroRotator,Params);
		if(!Pillar)return false;
		Fixture.Actors.Add(Pillar);Added.Add(Pillar);
		Pillar->AttachToBuilding(Building,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	}
	const TArray<AEHB_Pillar*> Path={Fixture.Pillars[1],Added[0],Added[1],Fixture.Pillars[2]};
	for(int32 I=0;I<3;++I)
	{
		auto* Wall=Building->ConnectPillars(Path[I],Path[I+1],300,20);
		if(!Wall)return false;
		Wall->SetFlags(RF_Transient);Fixture.Actors.Add(Wall);
	}
	const auto Rooms=Building->GetClosedLoopsByFloor(1);
	if(!TestEqual(TEXT("Two adjacent rooms"),Rooms.Num(),2))return false;
	TArray<AEHB_Floor*> Floors;
	FGuid FirstRoomId;
	for(const auto& Room:Rooms)
	{
		auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Floor)return false;
		Fixture.Actors.Add(Floor);Floors.Add(Floor);
		if(!Floor->ConfigureFromRoomLoop(Building,Room,0,false))return false;
		if(Room.WallGuids.Contains(Fixture.Walls[0]->ElementGuid))FirstRoomId=Room.LoopGuid;
	}
	auto* Slab=Building->GetWorld()->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Slab)return false;
	Fixture.Actors.Add(Slab);
	Slab->ConfigureDefaultSlab(Building,FTransform(FVector(300,250,300)),300,20,false);
	Slab->bHasRoomFillAnchor=true;Slab->RoomFillAnchorWallGuid=Fixture.Walls[1]->ElementGuid;
	TestTrue(TEXT("Prepare topology"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true).bSucceeded);
	FString Before;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Before);
	const int32 ActorCount=Building->QueryElements(FEHBElementQuery()).Num();
	const int32 QueueCount=GEditor->Trans->GetQueueLength();
	const auto Move=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[0]->ElementGuid,FVector::ZeroVector,FVector(-40,20,0));
	TestTrue(TEXT("Valid topology movement proposal"),Move.bSucceeded);
	if(TestEqual(TEXT("Only the incident room changes"),Move.RoomBoundaryChanges.Num(),1))
	{
		const auto& Change=Move.RoomBoundaryChanges[0];
		TestEqual(TEXT("Existing room ID identified"),Change.RoomGuid,FirstRoomId);
		TestEqual(TEXT("Four boundary vertices"),Change.ProposedPolygon.Num(),4);
		const int32 Corner=Change.BoundaryPillarGuids.Find(Fixture.Pillars[0]->ElementGuid);
		if(TestTrue(TEXT("Moved corner present"),Corner!=INDEX_NONE))
		{
			TestEqual(TEXT("Original corner remains local"),Change.OriginalPolygon[Corner],FVector::ZeroVector);
			TestEqual(TEXT("Proposed corner uses building coordinates under rotation"),Change.ProposedPolygon[Corner],FVector(-40,20,0));
		}
		TestEqual(TEXT("Only this room's bound finish listed"),Change.BoundFloorFinishGuids.Num(),1);
		TestTrue(TEXT("Shared-wall slab is a candidate"),Change.CandidateAnchoredSlabGuids.Contains(Slab->ElementGuid));
	}
	TestEqual(TEXT("Dependencies remain unplanned for commit"),Move.UnplannedElementGuids.Num(),3);
	const auto Shared=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[1]->ElementGuid,FVector(600,0,0),FVector(650,-40,0));
	TestTrue(TEXT("Shared corner move proposal"),Shared.bSucceeded);
	TestEqual(TEXT("Shared corner reports both rooms"),Shared.RoomBoundaryChanges.Num(),2);
	for(const auto& Change:Shared.RoomBoundaryChanges)
		TestTrue(TEXT("Shared-wall slab is not assigned to an arbitrary room"),Change.CandidateAnchoredSlabGuids.Contains(Slab->ElementGuid));
	FString After;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),After);
	TestEqual(TEXT("Dependency preview leaves topology untouched"),After,Before);
	TestEqual(TEXT("Preview creates no actors"),Building->QueryElements(FEHBElementQuery()).Num(),ActorCount);
	TestEqual(TEXT("Preview creates no transaction"),GEditor->Trans->GetQueueLength(),QueueCount);
	Floors[0]->RoomFloorIndex=99;
	const auto Stale=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[1]->ElementGuid,FVector(600,0,0),FVector(650,-40,0));
	TestTrue(TEXT("Mismatched floor binding is disclosed"),Stale.RoomBoundaryChanges.ContainsByPredicate([](const auto& Change){return Change.bHasStaleFloorBinding;}));
	const auto Invalid=UEHBWallTopologyLibrary::PreviewNodeMove(Building,Fixture.Pillars[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0));
	TestFalse(TEXT("Invalid move remains rejected"),Invalid.bSucceeded);
	TestTrue(TEXT("Rejected move has no usable room proposal"),Invalid.RoomBoundaryChanges.IsEmpty());
	Slab->SetActorRelativeLocation(FVector(600,250,300));
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	const auto BeforeAmbiguousFill=Slab->LocalTopPolygon;
	AddExpectedError(TEXT("no closed room polygon contains the selected slab location"),EAutomationExpectedErrorFlags::Contains,1);
	TestFalse(TEXT("Slab centered on shared wall does not pick an arbitrary room"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab));
	TestTrue(TEXT("Ambiguous slab fill retains outline"),Slab->LocalTopPolygon==BeforeAmbiguousFill);
	TestFalse(TEXT("Ambiguous fill records no room binding"),Slab->RoomFillLoopGuid.IsValid());
	// Removing the unsupported slab lets the explicit command update both rooms.
	Slab->Destroy(); Floors[0]->RoomFloorIndex=1;
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) if (auto* Actor=Cast<AActor>(*It)) PreviousSelection.Add(Actor);
	GEditor->SelectNone(false,true,false); GEditor->SelectActor(Building,true,false);
	TArray<FBoxSphereBounds> OldBounds;
	for (auto* Floor:Floors) OldBounds.Add(Floor->MeshComponent->CalcBounds(FTransform::Identity));
	const FVector SharedTarget(650,-40,0);
	const auto Applied=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Building,Fixture.Pillars[1]->ElementGuid,FVector(600,0,0),SharedTarget);
	TestTrue(*Applied.Message,Applied.bSucceeded);
	for (auto* Floor:Floors)
	{
		TestTrue(TEXT("Both room floors contain the moved shared corner"),Floor->FloorRegions[0].OuterPolygon.ContainsByPredicate([&](const FVector& P){return P.Equals(SharedTarget,0.001);}));
		TestTrue(TEXT("Both generated floor bases remain current"),Floor->IsRecordedOutlineUnchanged());
		FEHBBuildingClosedLoop Resolved; TestTrue(TEXT("Both room IDs still resolve"),Floor->TryGetRoomLoop(Resolved));
	}
	if(Applied.bSucceeded)
	{
		TestTrue(TEXT("One undo restores both floors"),GEditor->UndoTransaction());
		for(int32 I=0;I<Floors.Num();++I)
		{
			const auto Bounds=Floors[I]->MeshComponent->CalcBounds(FTransform::Identity);
			TestTrue(TEXT("Shared room floor bounds restored"),Bounds.Origin.Equals(OldBounds[I].Origin,0.001)&&Bounds.BoxExtent.Equals(OldBounds[I].BoxExtent,0.001));
			TestTrue(TEXT("Shared room provenance restored"),Floors[I]->IsRecordedOutlineUnchanged());
		}
	}
	GEditor->SelectNone(false,true,false);
	for(const auto& Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStableRoomIdentityTest, "EHB.Topology.StableRoomIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBStableRoomIdentityTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr))return false;
	auto* Building=Fixture.Building;
	if(!TestEqual(TEXT("Initial room exists"),Building->ClosedLoops.Num(),1))return false;
	const auto OriginalRoom=Building->ClosedLoops[0];
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
	auto* Floor=Building->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!TestNotNull(TEXT("Floor fixture created"),Floor))return false;
	Fixture.Actors.Add(Floor);
	TestTrue(TEXT("Floor binds to initial room"),Floor->ConfigureFromRoomLoop(Building,OriginalRoom,0,false));
	for(int32 Pass=0;Pass<3;++Pass)
	{
		Building->ClosedLoops.Reset();Building->RebuildClosedLoops();
		FEHBBuildingClosedLoop Resolved;
		TestTrue(TEXT("Floor binding survives regenerated room cache"),Floor->TryGetRoomLoop(Resolved));
		TestEqual(TEXT("Room identity does not depend on previous cache"),Resolved.LoopGuid,OriginalRoom.LoopGuid);
	}
	Building->ClosedLoops.Reset();Building->WallConnectionsByWallGuid.Reset();
	Building->RegisterElementActor(Fixture.Pillars[0]);
	TestEqual(TEXT("Room query recovers caches after child registration"),Building->GetClosedLoopsByFloor(1).Num(),1);
	TestEqual(TEXT("Recovered wall-to-room index"),Building->GetClosedLoopsByWallGuid(Fixture.Walls[0]->ElementGuid).Num(),1);
	TestEqual(TEXT("Recovered pillar-to-room index"),Building->GetClosedLoopsByPillarGuid(Fixture.Pillars[0]->ElementGuid).Num(),1);
	TestEqual(TEXT("World room query after registration"),Building->FindClosedLoopsContainingWorldLocation(Building->GetActorTransform().TransformPosition(FVector(300,250,100)),1).Num(),1);
	const FTransform OriginalPose=Fixture.Pillars[2]->GetElementLocalTransform();
	FTransform MovedPose=OriginalPose;MovedPose.AddToTranslation(FVector(90,30,0));
	Fixture.Pillars[2]->AttachToBuilding(Building,MovedPose);
	Building->RebuildClosedLoops();
	if(TestEqual(TEXT("Moving a corner keeps one room"),Building->ClosedLoops.Num(),1))
		TestEqual(TEXT("Corner movement preserves identity"),Building->ClosedLoops[0].LoopGuid,OriginalRoom.LoopGuid);
	Fixture.Pillars[2]->AttachToBuilding(Building,OriginalPose);
	const FGuid OriginalBuildingGuid=Building->BuildingGuid;
	Building->BuildingGuid=FGuid::NewGuid();Building->RebuildClosedLoops();
	if(TestEqual(TEXT("Building scope fixture has one room"),Building->ClosedLoops.Num(),1))
		TestNotEqual(TEXT("Room identity is scoped to building"),Building->ClosedLoops[0].LoopGuid,OriginalRoom.LoopGuid);
	Building->BuildingGuid=OriginalBuildingGuid;
	for(auto* Pillar:Fixture.Pillars)Pillar->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);
	for(auto* Wall:Fixture.Walls)Wall->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);
	Building->RebuildClosedLoops();
	if(TestEqual(TEXT("Floor scope fixture has one room"),Building->ClosedLoops.Num(),1))
		TestNotEqual(TEXT("Room identity is scoped to floor"),Building->ClosedLoops[0].LoopGuid,OriginalRoom.LoopGuid);
	for(auto* Pillar:Fixture.Pillars)Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	for(auto* Wall:Fixture.Walls)Wall->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	Building->RebuildClosedLoops();
	FEHBBuildingClosedLoop Restored;
	TestTrue(TEXT("Original floor binding recovers after restoring scope"),Floor->TryGetRoomLoop(Restored));
	TestEqual(TEXT("Original room ID restored"),Restored.LoopGuid,OriginalRoom.LoopGuid);
	return true;
}

// These opt-in tests deliberately have a different prefix from EHB. They run
// in dedicated processes against a newly-created, uniquely named test map.
namespace
{
	bool PersistencePaths(FString& MapPath, FString& EvidencePath, const FString& Profile = TEXT("Cut"))
	{
		if (!FParse::Param(FCommandLine::Get(),TEXT("EHBRunPersistence"))
			|| !FParse::Value(FCommandLine::Get(),TEXT("EHBPersistenceMap="),MapPath)
			|| !MapPath.StartsWith(TEXT("/Game/EHB_Refactor_Validation/Generated") + Profile + TEXT("_"))
			|| !FPackageName::IsValidLongPackageName(MapPath)) return false;
		EvidencePath=FPaths::ProjectSavedDir()/TEXT("EHB-Refactor")/(FPackageName::GetShortName(MapPath)+TEXT(".json"));
		return true;
	}
	FString CutSnapshot(AEHB_Railing* Rail)
	{
		TSharedRef<FJsonObject> Data=MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("schema"),3);
		Data->SetStringField(TEXT("building"),Rail->OwningBuilding->BuildingGuid.ToString());
		Data->SetStringField(TEXT("rail"),Rail->ElementGuid.ToString());
		Data->SetStringField(TEXT("anchor"),Rail->StartAnchor.ElementGuid.ToString());
		Data->SetStringField(TEXT("start"),Rail->LinearStart.ToString());
		Data->SetStringField(TEXT("end"),Rail->LinearEnd.ToString());
		Data->SetStringField(TEXT("cutPoint"),Rail->StartWallCutPoint.ToString());
		Data->SetStringField(TEXT("cutNormal"),Rail->StartWallCutNormal.ToString());
		Data->SetBoolField(TEXT("hasCut"),Rail->bHasStartWallCut);
		Data->SetStringField(TEXT("buildingPose"),Rail->OwningBuilding->GetActorTransform().ToString());
		Data->SetStringField(TEXT("railPose"),Rail->GetActorTransform().ToString());
		TArray<TSharedPtr<FJsonValue>> RoomIds;
		TArray<FString> SortedRooms;
		for(const auto& Room:Rail->OwningBuilding->GetClosedLoopsByFloor(Rail->FloorIndex)) SortedRooms.Add(Room.LoopGuid.ToString());
		SortedRooms.Sort();
		for(const FString& Id:SortedRooms) RoomIds.Add(MakeShared<FJsonValueString>(Id));
		Data->SetArrayField(TEXT("roomIds"),RoomIds);
		TArray<TSharedPtr<FJsonValue>> Floors;
		for(auto* E:Rail->OwningBuilding->QueryElements(FEHBElementQuery())) if(auto* Floor=Cast<AEHB_Floor>(E))
		{
			FEHBBuildingClosedLoop Room;
			Floors.Add(MakeShared<FJsonValueString>(Floor->ElementGuid.ToString()+TEXT("|")+Floor->RoomLoopGuid.ToString()+TEXT("|")+(Floor->TryGetRoomLoop(Room)?TEXT("resolved"):TEXT("missing"))));
			Floors.Add(MakeShared<FJsonValueString>(FString::FromInt(static_cast<int32>(Floor->OutlineSource))+TEXT("|")+(Floor->IsRecordedOutlineUnchanged()?TEXT("unchanged"):TEXT("manualOrModified"))));
			FString Geometry=Floor->GetActorTransform().ToString();
			for(const auto& Component:Floor->RegionMeshComponents)
			{
				Geometry+=TEXT("Region");
				if(Component)if(const auto* Mesh=Component->GetProcMeshSection(0))
				{
					for(const auto& V:Mesh->ProcVertexBuffer)Geometry+=V.Position.ToString()+V.Normal.ToString();
					for(uint32 I:Mesh->ProcIndexBuffer)Geometry+=TEXT("|")+FString::FromInt(I);
				}
			}
			Floors.Add(MakeShared<FJsonValueString>(Geometry));
		}
		Data->SetArrayField(TEXT("floorBindings"),Floors);
		TArray<FString> SlabRecords;
		for(auto* E:Rail->OwningBuilding->QueryElements(FEHBElementQuery()))if(auto* Slab=Cast<AEHB_FloorSlab>(E))
			SlabRecords.Add(Slab->ElementGuid.ToString()+TEXT("|")+Slab->RoomFillLoopGuid.ToString()+TEXT("|")+FString::FromInt(Slab->RoomFillFloorIndex)+TEXT("|")+FString::FromInt(static_cast<int32>(Slab->OutlineSource))+TEXT("|")+(Slab->IsRecordedOutlineUnchanged()?TEXT("unchanged"):TEXT("manualOrModified")));
		SlabRecords.Sort();TArray<TSharedPtr<FJsonValue>> Slabs;
		for(const FString& Record:SlabRecords)Slabs.Add(MakeShared<FJsonValueString>(Record));
		Data->SetArrayField(TEXT("slabBindings"),Slabs);
		TArray<TSharedPtr<FJsonValue>> Posts,Relations,Vertices;
		for(const auto& Post:Rail->GeneratedPosts)
			Posts.Add(MakeShared<FJsonValueString>(Post.PostGuid.ToString()+TEXT("|")+Post.LocalBaseLocation.ToString()));
		for(const auto& Guid:Rail->RailingRelationGuids) Relations.Add(MakeShared<FJsonValueString>(Guid.ToString()));
		if(const auto* Mesh=Rail->RailGeneratedMeshComponent->GetProcMeshSection(0))
		{
			for(const auto& V:Mesh->ProcVertexBuffer) Vertices.Add(MakeShared<FJsonValueString>(V.Position.ToString()));
			Data->SetNumberField(TEXT("indices"),Mesh->ProcIndexBuffer.Num());
		}
		Data->SetArrayField(TEXT("posts"),Posts);Data->SetArrayField(TEXT("relations"),Relations);Data->SetArrayField(TEXT("vertices"),Vertices);
		FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return Json;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutPersistenceWriteTest,"EHBValidation.Persistence.WriteCut",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCutPersistenceWriteTest::RunTest(const FString& Parameters)
{
	FString MapPath,EvidencePath;
	if(!PersistencePaths(MapPath,EvidencePath)){AddError(TEXT("Explicit isolated persistence map arguments required"));return false;}
	if(FPackageName::DoesPackageExist(MapPath)){AddError(TEXT("Refusing to overwrite existing persistence map"));return false;}
	UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(World,RF_Transactional))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));Building->SetActorLabel(TEXT("EHB_Persistence_CutFixture"));
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	FEHBRailingWallDrag Request;Request.Capture(Fixture.Walls[0],Fixture.Walls[0]->GetWorldLocationOnCenterAxisAtDistance(280,100));
	const auto Result=Request.Execute(Building,Request.WorldStart+Building->GetActorTransform().TransformVectorNoScale(FVector(180,-180,0)),100,5,120,false);
	if(!TestTrue(*Result.Message,Result.bSucceeded))return false;
	const bool bWallBranch=FParse::Param(FCommandLine::Get(),TEXT("EHBWallBranchPersistence"));
 if(bWallBranch)
 {
  auto* Source=Fixture.Walls[2];const float Distance=FVector::Dist2D(Source->LocalStart,Source->LocalEnd)*0.5f;
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
  auto* Window=World->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
  Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(60,100,50,10);Window->AttachToBuilding(Building,FTransform::Identity);
  Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(125,50),Source->GetActorQuat());Window->BindToWall(Source,125);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
  const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building,Source->ElementGuid,Distance);
  FEHBWallCreationEndpoint A,B;A.Wall=Source;A.WallDistance=Distance;A.LocalLocation=Split.LocalPillarPosition;A.WorldLocation=Building->GetActorTransform().TransformPosition(A.LocalLocation);
  B.LocalLocation=A.LocalLocation+FVector(150,300,0);B.WorldLocation=Building->GetActorTransform().TransformPosition(B.LocalLocation);
  const auto Created=EHBWallCreationCommand::CommitFromWall(Building,A,B,FEHBWallCreationOptions());
  if(!TestTrue(*Created.Status.ToString(),Created.bSucceeded))return false;
 }
	AEHB_Railing* Rail=nullptr;
	Building->ClearFlags(RF_Transient);
	for(auto* E:Building->QueryElements(FEHBElementQuery()))
	{
		E->ClearFlags(RF_Transient);
		if(!Fixture.Actors.Contains(E))Fixture.Actors.Add(E);
		if(auto* R=Cast<AEHB_Railing>(E))Rail=R;
	}
	if(!TestNotNull(TEXT("Created cut railing"),Rail))return false;
	FActorSpawnParameters FloorParams;FloorParams.ObjectFlags=RF_Transactional;
	auto* Floor=World->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,FloorParams);
	if(!TestNotNull(TEXT("Persistent floor created"),Floor))return false;
	Fixture.Actors.Add(Floor);
	if(!TestTrue(TEXT("Persistent floor binds to room"),Floor->ConfigureFromRoomLoop(Building,Building->GetClosedLoopsByFloor(1)[0],0,false)))return false;
	// Two real support patches produce two floor regions through the actual fill path.
	for(double CenterX:{130.0,470.0})
	{
		auto* Support=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,FloorParams);
		if(!Support)return false;Fixture.Actors.Add(Support);
		Support->ConfigureDefaultSlab(Building,FTransform(FVector(CenterX,250,0)),260,20,true);
		Support->SetFloorAssignment(0,EEHBBuildingFloorElementRole::Foundation);
		Support->LocalTopPolygon={FVector(-130,-250,0),FVector(130,-250,0),FVector(130,250,0),FVector(-130,250,0)};
		if(!TestTrue(TEXT("Persistence support patch built"),Support->RebuildSlabMesh()))return false;
	}
	FEasyHouseEditorMode Mode;
	if(!TestTrue(TEXT("Persist actual room-support fill"),Mode.FillFloorRoom(Floor)))return false;
	if(!TestEqual(TEXT("Two supported finish regions"),Floor->FloorRegions.Num(),2))return false;
	TestTrue(TEXT("Floor generation signature recorded"),Floor->IsRecordedOutlineUnchanged());
	auto* Ceiling=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,FloorParams);
	if(!Ceiling)return false;Fixture.Actors.Add(Ceiling);
	Ceiling->ConfigureDefaultSlab(Building,FTransform(FVector(300,250,300)),200,20,false);
	Ceiling->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	if(!TestTrue(TEXT("Persist actual slab room fill"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Ceiling)))return false;
	TestTrue(TEXT("Slab generation signature recorded"),Ceiling->IsRecordedOutlineUnchanged());
	if(!TestTrue(TEXT("Save fresh map through editor map API"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
	TestTrue(TEXT("Save identity and geometry evidence"),FFileHelper::SaveStringToFile(CutSnapshot(Rail),*EvidencePath));
	FString Receipt;FJsonObjectConverter::UStructToJsonObjectString(Building->LastCommittedEdit,Receipt);
	TestEqual(TEXT("Persist composite split command"),Building->LastCommittedEdit.Command,FName(bWallBranch?TEXT("CreateWallFromWall"):TEXT("SplitWallAndRailing")));
	TestTrue(TEXT("Persist valid split state"),Building->LastCommittedEdit.StateId.IsValid());
	TestTrue(TEXT("Save split command receipt evidence"),FFileHelper::SaveStringToFile(Receipt,*(EvidencePath+TEXT(".edit.json"))));
	Fixture.Actors.Reset(); // The dedicated editor process owns the saved world now.
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutPersistenceReadTest,"EHBValidation.Persistence.ReadCut",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCutPersistenceReadTest::RunTest(const FString& Parameters)
{
	FString MapPath,EvidencePath;
	if(!PersistencePaths(MapPath,EvidencePath)){AddError(TEXT("Explicit isolated persistence map arguments required"));return false;}
	UWorld* World=GEditor->GetEditorWorldContext().World();
	if(!World||World->GetOutermost()->GetName()!=MapPath){AddError(TEXT("Start the verifier in the saved fixture map"));return false;}
	AEHB_Railing* Rail=nullptr;
	for(TActorIterator<AEHB_Railing> It(World);It;++It){if(Rail){AddError(TEXT("Unexpected extra railing"));return false;}Rail=*It;}
	if(!TestNotNull(TEXT("Railing loaded in new process"),Rail))return false;
	FString Expected;if(!FFileHelper::LoadFileToString(Expected,*EvidencePath)){AddError(TEXT("Missing write evidence"));return false;}
	TSharedPtr<FJsonObject> Evidence;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected),Evidence) || !Evidence.IsValid() || !Evidence->HasField(TEXT("schema")) || Evidence->GetIntegerField(TEXT("schema"))!=3)
	{AddError(TEXT("Persistence evidence schema changed; generate a fresh isolated fixture using the write/read script."));return false;}
	TestEqual(TEXT("Loaded IDs, cut plane, posts and mesh match saved evidence"),CutSnapshot(Rail),Expected);
	FString ExpectedEdit;const bool bHasEditEvidence=FPaths::FileExists(EvidencePath+TEXT(".edit.json"));
	if(bHasEditEvidence)
	{
		if(!FFileHelper::LoadFileToString(ExpectedEdit,*(EvidencePath+TEXT(".edit.json"))))return false;
		FString ActualEdit;FJsonObjectConverter::UStructToJsonObjectString(Rail->OwningBuilding->LastCommittedEdit,ActualEdit);
		TestEqual(TEXT("Split receipt reloads exactly including removed and created bounds"),ActualEdit,ExpectedEdit);
		const auto& Edit=Rail->OwningBuilding->LastCommittedEdit;TestTrue(TEXT("Loaded supported composite command"),Edit.Command==TEXT("SplitWallAndRailing")||Edit.Command==TEXT("CreateWallFromWall"));TestTrue(TEXT("Loaded receipt ownership"),Edit.BuildingGuid==Rail->OwningBuilding->BuildingGuid&&Edit.StateId.IsValid());
		TestTrue(TEXT("Loaded receipt preserves deleted source area"),Edit.Elements.ContainsByPredicate([](const auto& E){return E.BeforeBounds.IsValid&&!E.AfterBounds.IsValid;}));
		TestTrue(TEXT("Loaded receipt preserves created structure area"),Edit.Elements.ContainsByPredicate([](const auto& E){return !E.BeforeBounds.IsValid&&E.AfterBounds.IsValid;}));
	}
	Rail->RebuildRailing();
	for(auto* E:Rail->OwningBuilding->QueryElements(FEHBElementQuery()))if(auto* Floor=Cast<AEHB_Floor>(E))
		TestTrue(TEXT("Loaded multi-region floor rebuilds"),Floor->RebuildFloorMesh());
	TestEqual(TEXT("Explicit reconstruction preserves saved geometry and identities"),CutSnapshot(Rail),Expected);
	if(bHasEditEvidence){FString After;FJsonObjectConverter::UStructToJsonObjectString(Rail->OwningBuilding->LastCommittedEdit,After);TestEqual(TEXT("Rebuild preserves split receipt"),After,ExpectedEdit);}
	const auto Baseline=UEHBWallTopologyLibrary::PrepareTopologyMigration(Rail->OwningBuilding,false);
	FString BaselineJson;FJsonObjectConverter::UStructToJsonObjectString(Baseline,BaselineJson);AddInfo(BaselineJson);
	TestEqual(TEXT("Topology baseline remains coherent after reload"),Baseline.Status,FName(TEXT("AlreadyInitialized")));
	return true;
}
namespace
{
	// FTransform::ToString rounds Euler angles to six decimals. Reload may spell a
	// rounded zero with a minus sign. Normalize only transform-shaped text fields;
	// topology JSON, design region values, vertex/normal/UV strings stay untouched.
	void NormalizeRoomMoveTransformText(const TSharedPtr<FJsonObject>& Data)
	{
		auto Normalize=[](FString Value){Value.ReplaceInline(TEXT("-0.000000,"),TEXT("0.000000,"));Value.ReplaceInline(TEXT("-0.000000|"),TEXT("0.000000|"));return Value;};
		Data->SetStringField(TEXT("buildingTransform"),Normalize(Data->GetStringField(TEXT("buildingTransform"))));
		TArray<TSharedPtr<FJsonValue>> Geometry;
		for(const auto& Value:Data->GetArrayField(TEXT("generatedGeometry")))Geometry.Add(MakeShared<FJsonValueString>(Normalize(Value->AsString())));
		Data->SetArrayField(TEXT("generatedGeometry"),Geometry);
	}

	TArray<TSharedPtr<FJsonValue>> BuildingGeometrySnapshot(AEHBBuildingActorBase* Building,bool bIncludeTangents=false)
	{
		TArray<TSharedPtr<FJsonValue>> Geometry;
		auto Elements = Building->QueryElements(FEHBElementQuery());
		Elements.Sort([](const auto& A,const auto& B){return A.ElementGuid.ToString()<B.ElementGuid.ToString();});
		for (auto* Element : Elements)
		{
			FString Value = Element->ElementGuid.ToString() + Element->GetElementLocalTransform().ToString();
			if(const auto* Slab=Cast<AEHB_FloorSlab>(Element))
			{
				Value+=Slab->RoomFillLoopGuid.ToString()+FString::FromInt(Slab->RoomFillFloorIndex)+FString::FromInt(static_cast<int32>(Slab->FloorRole))
					+FString::FromInt(static_cast<int32>(Slab->OutlineSource))+(Slab->IsRecordedOutlineUnchanged()?TEXT("current"):TEXT("modified"));
				for(const auto& P:Slab->LocalTopPolygon)Value+=P.ToString();
				for(const auto& H:Slab->LocalHoles){FString HoleJson;FJsonObjectConverter::UStructToJsonObjectString(H,HoleJson);Value+=HoleJson;}
			}
			TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(Element);
			Meshes.Sort([](const auto& A,const auto& B){return A.GetName()<B.GetName();});
			for (auto* Mesh : Meshes)
			{
				Value += Mesh->GetName() + Mesh->GetRelativeTransform().ToString();
				for(int32 I=0;I<Mesh->GetNumSections();++I)if(const auto* Section=Mesh->GetProcMeshSection(I))
				{
					Value += FString::FromInt(Section->bEnableCollision) + FString::FromInt(Section->bSectionVisible);
					for(const auto& V:Section->ProcVertexBuffer){Value += V.Position.ToString()+V.Normal.ToString()+V.UV0.ToString();if(bIncludeTangents)Value+=V.Tangent.TangentX.ToString()+FString::FromInt(V.Tangent.bFlipTangentY);}
					for(uint32 Index:Section->ProcIndexBuffer)Value += FString::FromInt(Index)+TEXT(",");
				}
			}
			Geometry.Add(MakeShared<FJsonValueString>(Value));
		}
		return Geometry;
	}

	FString RoomMoveSnapshot(AEHB_Floor* Floor)
	{
		auto* Building = Floor->OwningBuilding.Get();
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("schema"),TEXT("EHB.RoomMovePersistence.v2"));
		Data->SetStringField(TEXT("building"),Building->BuildingGuid.ToString());
		Data->SetStringField(TEXT("buildingTransform"),Building->GetActorTransform().ToString());
		Data->SetStringField(TEXT("floor"),Floor->ElementGuid.ToString());
		Data->SetStringField(TEXT("room"),Floor->RoomLoopGuid.ToString());
		Data->SetNumberField(TEXT("floorIndex"),Floor->FloorIndex);
		Data->SetNumberField(TEXT("roomFloorIndex"),Floor->RoomFloorIndex);
		Data->SetNumberField(TEXT("source"),static_cast<int32>(Floor->OutlineSource));
		Data->SetBoolField(TEXT("recordedOutlineUnchanged"),Floor->IsRecordedOutlineUnchanged());
		FString GraphJson; FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),GraphJson);
		Data->SetStringField(TEXT("topology"),GraphJson);
		if(Building->PreparedWallNodeDefinitions.Version>0)Data->SetObjectField(TEXT("preparedNodeDefinitions"),FJsonObjectConverter::UStructToJsonObject(Building->PreparedWallNodeDefinitions).ToSharedRef());
		if(Building->WallNodeOwnership.Version!=0)
		{
			Data->SetObjectField(TEXT("nodeOwnership"),FJsonObjectConverter::UStructToJsonObject(Building->WallNodeOwnership));
			auto Sorted=Building->ElementRelations;Sorted.Sort([](const auto& A,const auto& B){return A.RelationGuid.ToString()<B.RelationGuid.ToString();});
			TArray<TSharedPtr<FJsonValue>> Relations;
			for(const auto& Relation:Sorted){auto Value=FJsonObjectConverter::UStructToJsonObject(Relation);Value->RemoveField(TEXT("sourceGeometryRevision"));Value->RemoveField(TEXT("targetGeometryRevision"));Relations.Add(MakeShared<FJsonValueObject>(Value));}
			Data->SetArrayField(TEXT("nodeRelations"),Relations);
		}
		FEHBBuildingClosedLoop Room; Data->SetBoolField(TEXT("roomResolved"),Floor->TryGetRoomLoop(Room));
		TArray<TSharedPtr<FJsonValue>> Regions, Geometry;
		for (const auto& Region : Floor->FloorRegions)
		{ FString Json; FJsonObjectConverter::UStructToJsonObjectString(Region,Json); Regions.Add(MakeShared<FJsonValueString>(Json)); }
		Data->SetArrayField(TEXT("regions"),Regions);
		Geometry=BuildingGeometrySnapshot(Building);
		Data->SetArrayField(TEXT("generatedGeometry"),Geometry);
		NormalizeRoomMoveTransformText(Data);
		FString Json; FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json)); return Json;
	}
}
static bool RunRoomMovePersistenceWrite(FAutomationTestBase& Test,bool bNodeOwnership)

{
	FString MapPath,EvidencePath;
	if(!PersistencePaths(MapPath,EvidencePath,bNodeOwnership?TEXT("NodeOwnership"):TEXT("RoomMove"))){Test.AddError(TEXT("Explicit isolated room move map arguments required"));return false;}
	if(FPackageName::DoesPackageExist(MapPath)){Test.AddError(TEXT("Refusing to overwrite existing persistence map"));return false;}
	UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
	FTransientTopologyFixture Fixture;
	if(!Fixture.Create(World,RF_Transactional))return false;
	auto* Building=Fixture.Building; Building->SetActorRotation(FRotator(0,37,0));
	FActorSpawnParameters Params; Params.ObjectFlags=RF_Transactional;
	auto* Floor=World->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Floor)return false; Fixture.Actors.Add(Floor);
	TArray<AEHB_Pillar*> Adjacent;
	for(const FVector P:{FVector(1000,0,0),FVector(1000,500,0)})
	{
		auto* Pillar=World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
		if(!Pillar)return false;Fixture.Actors.Add(Pillar);Adjacent.Add(Pillar);
		Pillar->AttachToBuilding(Building,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
	}
	const TArray<AEHB_Pillar*> Path={Fixture.Pillars[1],Adjacent[0],Adjacent[1],Fixture.Pillars[2]};
	for(int32 I=0;I<3;++I){auto* Wall=Building->ConnectPillars(Path[I],Path[I+1],300,20);if(!Wall)return false;Fixture.Actors.Add(Wall);}
	const auto Rooms=Building->GetClosedLoopsByFloor(1);
	const auto* MainRoom=Rooms.FindByPredicate([&](const auto& R){return R.WallGuids.Contains(Fixture.Walls[0]->ElementGuid);});
	if(!Test.TestEqual(TEXT("Persistence fixture contains adjacent rooms"),Rooms.Num(),2)||!MainRoom)return false;
	if(!Floor->ConfigureFromRoomLoop(Building,*MainRoom,0,true))return false;
	 auto* Slab=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!Slab)return false;Fixture.Actors.Add(Slab);
	Slab->ConfigureDefaultSlab(Building,FTransform(FRotator(0,17,0),FVector(300,250,300)),200,20,false);
	Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	if(!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))return false;
	auto* SecondSlab=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);
	if(!SecondSlab)return false;Fixture.Actors.Add(SecondSlab);
	SecondSlab->ConfigureDefaultSlab(Building,FTransform(FRotator(0,-13,0),FVector(800,250,300)),100,20,false);
	SecondSlab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
	if(!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(SecondSlab))return false;
	if(!UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true).bSucceeded)return false;
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	const auto Moved=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Fixture.Walls[0]->ElementGuid,FVector::ZeroVector,FVector(600,0,0),FVector(30,-80,0));
	if(!Test.TestTrue(*Moved.Message,Moved.bSucceeded))return false;
	Test.TestTrue(TEXT("Generated floor current before save"),Floor->IsRecordedOutlineUnchanged());
	Test.TestEqual(TEXT("Moved baseline coherent before save"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	if(!Test.TestTrue(TEXT("Prepare node definitions before independent save"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded))return false;
	if(bNodeOwnership)
	{
		FEHBTopologyMigrationResult Migrated;FJsonObjectConverter::JsonObjectStringToUStruct(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,true),&Migrated);
		if(!Test.TestTrue(TEXT("Persist actual typed node ownership migration"),Migrated.bSucceeded&&Migrated.bChanged))return false;
	}
	if(!Test.TestTrue(TEXT("Save linked wall floor result"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
	Test.TestTrue(TEXT("Write linked movement evidence"),FFileHelper::SaveStringToFile(RoomMoveSnapshot(Floor),*EvidencePath));
	Fixture.Actors.Reset();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomMovePersistenceWriteTest,"EHBValidation.Persistence.WriteRoomMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomMovePersistenceWriteTest::RunTest(const FString& Parameters){return RunRoomMovePersistenceWrite(*this,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeOwnershipPersistenceWriteTest,"EHBValidation.Persistence.WriteNodeOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeOwnershipPersistenceWriteTest::RunTest(const FString& Parameters){return RunRoomMovePersistenceWrite(*this,true);}
static bool RunRoomMovePersistenceRead(FAutomationTestBase& Test,bool bNodeOwnership)

{
	FString MapPath,EvidencePath;
	if(!PersistencePaths(MapPath,EvidencePath,bNodeOwnership?TEXT("NodeOwnership"):TEXT("RoomMove")))return false;
	UWorld* World=GEditor->GetEditorWorldContext().World();
	if(!World||World->GetOutermost()->GetName()!=MapPath){Test.AddError(TEXT("Start verifier in saved room move map"));return false;}
	AEHB_Floor* Floor=nullptr;
	for(TActorIterator<AEHB_Floor> It(World);It;++It){if(Floor){Test.AddError(TEXT("Unexpected extra floor"));return false;}Floor=*It;}
	if(!Test.TestNotNull(TEXT("Linked floor loaded"),Floor)||!Floor->OwningBuilding)return false;
	FString Expected;if(!FFileHelper::LoadFileToString(Expected,*EvidencePath))return false;
	TSharedPtr<FJsonObject> Evidence;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected),Evidence)||!Evidence.IsValid()
		||!Evidence->HasField(TEXT("schema"))||Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.RoomMovePersistence.v2"))
	{Test.AddError(TEXT("Generate a fresh room move persistence fixture"));return false;}
	NormalizeRoomMoveTransformText(Evidence);Expected.Reset();FJsonSerializer::Serialize(Evidence.ToSharedRef(),TJsonWriterFactory<>::Create(&Expected));
	Test.TestEqual(TEXT("Saved linked geometry identity and provenance reload"),RoomMoveSnapshot(Floor),Expected);
	auto* Building=Floor->OwningBuilding.Get();
	Test.TestEqual(TEXT("Ownership is restored only when explicitly saved"),Building->WallNodeOwnership.Version,bNodeOwnership?1:0);
	if(bNodeOwnership)Test.TestEqual(TEXT("Loaded typed ownership validates"),Building->MigrateWallNodeOwnership(false).Status,FName(TEXT("AlreadyMigrated")));
	const bool bHasPreparedNodes=Evidence->HasField(TEXT("preparedNodeDefinitions"));
	if(bHasPreparedNodes)Test.TestEqual(TEXT("Saved node definitions match independently loaded source"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("AlreadyPrepared")));
	else Test.TestEqual(TEXT("Old assets remain unprepared without silent migration"),Building->PreparedWallNodeDefinitions.Version,0);
	Test.TestEqual(TEXT("Loaded moved baseline coherent"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,false).Status,FName("AlreadyInitialized"));
	for(auto* Element:Building->QueryElements(FEHBElementQuery()))
	{
		if(auto* Pillar=Cast<AEHB_Pillar>(Element))Pillar->RebuildPillarMesh();
		if(auto* Wall=Cast<AEHB_Wall>(Element))
		{
			if(bHasPreparedNodes){const uint64 Serial=Wall->GetNodeDefinitionRefreshSerial();Wall->RefreshFromConnectedPillars();Test.TestEqual(TEXT("Independent reload uses node definition mesh bridge"),Wall->GetNodeDefinitionRefreshSerial(),Serial+1);}
			else Wall->RebuildWallMesh();
		}
		if(auto* Slab=Cast<AEHB_FloorSlab>(Element))Test.TestTrue(TEXT("Loaded linked ceiling rebuilds"),Slab->RebuildSlabMesh());
	}
	Test.TestTrue(TEXT("Loaded linked floor rebuilds"),Floor->RebuildFloorMesh());
	Test.TestEqual(TEXT("Explicit geometry rebuild preserves linked result"),RoomMoveSnapshot(Floor),Expected);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Building);
	const auto* Node=Graph.Nodes.FindByPredicate([](const auto& N){return FMath::IsNearlyEqual(N.LocalPosition.Y,-80.0,0.001);});
	if(!Test.TestNotNull(TEXT("Saved moved boundary node resolved"),Node))return false;
	const auto* Edge=Graph.Walls.FindByPredicate([&](const auto& E){
		const auto* A=Graph.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==E.StartNodeGuid;});
		const auto* B=Graph.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==E.EndNodeGuid;});
		return A&&B&&FMath::IsNearlyEqual(A->LocalPosition.Y,-80.0,0.001)&&FMath::IsNearlyEqual(B->LocalPosition.Y,-80.0,0.001);});
	if(!Test.TestNotNull(TEXT("Saved moved wall resolved"),Edge))return false;
	const auto* A=Graph.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->StartNodeGuid;});
	const auto* B=Graph.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->EndNodeGuid;});
	const auto Continued=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(Building,Edge->WallGuid,A->LocalPosition,B->LocalPosition,FVector(0,-20,0));
	Test.TestTrue(TEXT("Reloaded building accepts next linked edit"),Continued.bSucceeded);
	if(Continued.bSucceeded)
	{
		if(bHasPreparedNodes)Test.TestEqual(TEXT("Further source editing makes inactive preparation stale"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("DefinitionSourceChanged")));
		Test.TestTrue(TEXT("Next edit can undo after reload"),GEditor->UndoTransaction(false));
		Test.TestEqual(TEXT("Undo after reload restores saved result"),RoomMoveSnapshot(Floor),Expected);
		if(bHasPreparedNodes)Test.TestEqual(TEXT("Undo restores the prepared source match"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("AlreadyPrepared")));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomMovePersistenceReadTest,"EHBValidation.Persistence.ReadRoomMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomMovePersistenceReadTest::RunTest(const FString& Parameters){return RunRoomMovePersistenceRead(*this,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeOwnershipPersistenceReadTest,"EHBValidation.Persistence.ReadNodeOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeOwnershipPersistenceReadTest::RunTest(const FString& Parameters){return RunRoomMovePersistenceRead(*this,true);}

namespace
{
	FString FixedSlabMoveSnapshot(AEHBBuildingActorBase* Building,FGuid PillarGuid,FGuid SlabGuid)
	{
		TSharedRef<FJsonObject> Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.FixedSlabMovePersistence.v1"));
		Data->SetStringField(TEXT("building"),Building->BuildingGuid.ToString());Data->SetStringField(TEXT("buildingTransform"),Building->GetActorTransform().ToString());
		Data->SetStringField(TEXT("pillar"),PillarGuid.ToString());Data->SetStringField(TEXT("slab"),SlabGuid.ToString());
		Data->SetObjectField(TEXT("topology"),FJsonObjectConverter::UStructToJsonObject(UEHBWallTopologyLibrary::CaptureWallTopology(Building)).ToSharedRef());
		Data->SetObjectField(TEXT("preparedNodeDefinitions"),FJsonObjectConverter::UStructToJsonObject(Building->PreparedWallNodeDefinitions).ToSharedRef());
		auto Relations=Building->ElementRelations;Relations.Sort([](const auto& A,const auto& B){return A.RelationGuid.ToString()<B.RelationGuid.ToString();});TArray<TSharedPtr<FJsonValue>> RelationValues;
		for(const auto& Relation:Relations)
		{
			auto Value=FJsonObjectConverter::UStructToJsonObject(Relation).ToSharedRef();
			// These stamps are derived validation diagnostics. All authoring IDs, endpoints,
			// flags, contact data, host pose and metadata remain part of exact evidence.
			Value->RemoveField(TEXT("sourceGeometryRevision"));Value->RemoveField(TEXT("targetGeometryRevision"));
			RelationValues.Add(MakeShared<FJsonValueObject>(Value));
		}
		Data->SetArrayField(TEXT("relations"),RelationValues);Data->SetArrayField(TEXT("generatedGeometry"),BuildingGeometrySnapshot(Building,true));
		NormalizeRoomMoveTransformText(Data);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return Json;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFixedSlabPersistenceWriteTest,"EHBValidation.Persistence.WriteFixedSlabMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFixedSlabPersistenceWriteTest::RunTest(const FString& Parameters)
{
	FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("FixedSlabMove"))){AddError(TEXT("Explicit isolated fixed-slab arguments required"));return false;}
	if(FPackageName::DoesPackageExist(MapPath)){AddError(TEXT("Refusing to overwrite an existing persistence map"));return false;}
	UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;
	auto* Building=Fixture.Building;Building->SetActorRotation(FRotator(0,37,0));auto* Pillar=Fixture.Pillars[0];Pillar->Width=60;Pillar->Depth=30;Pillar->SetActorRelativeRotation(FRotator(0,17,0));
	FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
	auto* Slab=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),Building->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Slab)return false;Fixture.Actors.Add(Slab);
	Slab->AttachToBuilding(Building,FTransform::Identity);Slab->bIsFoundation=false;Slab->bKeepFoundationBottomOnGround=false;Slab->VisualExpansion=0;
	Slab->LocalTopPolygon={FVector(-200,-200,0),FVector(800,-200,0),FVector(800,700,0),FVector(-200,700,0)};if(!Slab->RebuildSlabMesh())return false;
	Building->RefreshWallsConnectedToPillar(Pillar->ElementGuid,true);if(!UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true).bSucceeded)return false;
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	const FVector Requested(-195,0,0);const auto Snap=Pillar->PreviewFloorSlabBoundarySnap(Building->GetActorTransform().TransformPosition(Requested),Pillar->GetActorRotation(),30);
	if(!TestTrue(TEXT("Persistence snap changes orientation"),Snap.bFound&&!Snap.WorldRotation.Equals(Pillar->GetActorRotation(),0.001)))return false;
	TGuardValue<int32> BatchGuard(EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	const auto Commit=UEHBBuildingToolset::CommitNodeMoveOnFixedSlab(Building,Pillar->ElementGuid,Pillar->GetElementLocalTransform().GetLocation(),Pillar->GetActorRotation(),Requested,Snap.SlabGuid,Snap.WorldLocation,Snap.WorldRotation,30);
	if(!TestTrue(*Commit.Message,Commit.bSucceeded))return false;TestEqual(TEXT("Persist actual batch pose result"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,1);
	if(!TestTrue(TEXT("Prepare posed definitions before saving"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,true).bSucceeded))return false;
	if(!TestTrue(TEXT("Save posed fixed-slab fixture"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
	TestTrue(TEXT("Write complete fixed-slab evidence"),FFileHelper::SaveStringToFile(FixedSlabMoveSnapshot(Building,Pillar->ElementGuid,Slab->ElementGuid),*EvidencePath));Fixture.Actors.Reset();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFixedSlabPersistenceReadTest,"EHBValidation.Persistence.ReadFixedSlabMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFixedSlabPersistenceReadTest::RunTest(const FString& Parameters)
{
	FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("FixedSlabMove")))return false;UWorld* World=GEditor->GetEditorWorldContext().World();
	if(!World||World->GetOutermost()->GetName()!=MapPath){AddError(TEXT("Start verifier in the saved fixed-slab map"));return false;}
	AEHB_Building* Building=nullptr;for(TActorIterator<AEHB_Building> It(World);It;++It){if(Building){AddError(TEXT("Unexpected second fixture building"));return false;}Building=*It;}if(!Building)return false;
	FString Expected;if(!FFileHelper::LoadFileToString(Expected,*EvidencePath))return false;TSharedPtr<FJsonObject> Evidence;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected),Evidence)||!Evidence.IsValid()||Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.FixedSlabMovePersistence.v1"))return false;
	FGuid PillarGuid,SlabGuid;if(!FGuid::Parse(Evidence->GetStringField(TEXT("pillar")),PillarGuid)||!FGuid::Parse(Evidence->GetStringField(TEXT("slab")),SlabGuid))return false;
	NormalizeRoomMoveTransformText(Evidence);Expected.Reset();FJsonSerializer::Serialize(Evidence.ToSharedRef(),TJsonWriterFactory<>::Create(&Expected));
	TestEqual(TEXT("Fixed-slab poses relations and all mesh data reload"),FixedSlabMoveSnapshot(Building,PillarGuid,SlabGuid),Expected);
	auto* Pillar=Cast<AEHB_Pillar>(Building->FindElementActorByGuid(PillarGuid));auto* Slab=Cast<AEHB_FloorSlab>(Building->FindElementActorByGuid(SlabGuid));if(!Pillar||!Slab)return false;
	TestEqual(TEXT("Posed preparation matches loaded source"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("AlreadyPrepared")));
	for(auto* Element:Building->QueryElements(FEHBElementQuery())){if(auto* P=Cast<AEHB_Pillar>(Element))P->RebuildPillarMesh();if(auto* W=Cast<AEHB_Wall>(Element))W->RefreshFromConnectedPillars();if(auto* S=Cast<AEHB_FloorSlab>(Element))S->RebuildSlabMesh();}
	TestEqual(TEXT("Explicit rebuild preserves saved pose geometry and host association"),FixedSlabMoveSnapshot(Building,PillarGuid,SlabGuid),Expected);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);const FVector Requested(-195,40,0);const auto Snap=Pillar->PreviewFloorSlabBoundarySnap(Building->GetActorTransform().TransformPosition(Requested),Pillar->GetActorRotation(),30);if(!Snap.bFound)return false;
	TGuardValue<int32> BatchGuard(EHBNodeMoveTestHooks::DefinitionBatchSolveCount,0);
	const auto Moved=UEHBBuildingToolset::CommitNodeMoveOnFixedSlab(Building,PillarGuid,Pillar->GetElementLocalTransform().GetLocation(),Pillar->GetActorRotation(),Requested,Snap.SlabGuid,Snap.WorldLocation,Snap.WorldRotation,30);
	TestTrue(TEXT("Continue fixed-slab editing after independent load"),Moved.bSucceeded);TestEqual(TEXT("Continued pose edit uses one batch"),EHBNodeMoveTestHooks::DefinitionBatchSolveCount,1);
	if(Moved.bSucceeded){TestEqual(TEXT("Continued edit invalidates inactive preparation"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("DefinitionSourceChanged")));TestTrue(TEXT("Continued fixed-slab edit can undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo restores saved poses geometry and exact authoring relations"),FixedSlabMoveSnapshot(Building,PillarGuid,SlabGuid),Expected);TestEqual(TEXT("Undo restores source match"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName(TEXT("AlreadyPrepared")));}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBScaleBaselineTest,"EHBValidation.Performance.MultiBuildingBaseline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBScaleBaselineTest::RunTest(const FString& Parameters)
{
	if(!FParse::Param(FCommandLine::Get(),TEXT("EHBRunScaleBaseline"))){AddError(TEXT("Run scale baseline only in a dedicated process"));return false;}
	TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("measurement"),TEXT("NullRHI editor CPU mesh rebuild submission; not GPU/frame-time or completed async collision cooking"));
	Root->SetStringField(TEXT("cpu"),FPlatformMisc::GetCPUBrand());
	TArray<TSharedPtr<FJsonValue>> Cases;
	for(int32 BuildingCount:{1,8,32})
	{
		TArray<TUniquePtr<FTransientTopologyFixture>> Fixtures;
		const double CreateStart=FPlatformTime::Seconds();
		for(int32 I=0;I<BuildingCount;++I)
		{
			auto Fixture=MakeUnique<FTransientTopologyFixture>();
			if(!Fixture->Create(GEditor->GetEditorWorldContext().World()))return false;
			Fixture->Building->SetActorLocation(FVector((I%8)*1200,(I/8)*1200,180000));
			Fixtures.Add(MoveTemp(Fixture));
		}
		const double CreateMs=(FPlatformTime::Seconds()-CreateStart)*1000;
		TArray<double> Samples;
		for(int32 Pass=0;Pass<6;++Pass)
		{
			const double Begin=FPlatformTime::Seconds();
			for(const auto& Fixture:Fixtures)
			{
				Fixture->Building->RebuildElementAndRelationshipIndexes();
				Fixture->Building->RebuildClosedLoops();
				for(auto* Wall:Fixture->Walls)Wall->RebuildWallMesh();
				for(auto* Pillar:Fixture->Pillars)Pillar->RebuildPillarMesh();
			}
			if(Pass>0)Samples.Add((FPlatformTime::Seconds()-Begin)*1000);
		}
		Samples.Sort();
		TSharedRef<FJsonObject> Case=MakeShared<FJsonObject>();
		Case->SetNumberField(TEXT("buildings"),BuildingCount);Case->SetNumberField(TEXT("creationMs"),CreateMs);
		Case->SetNumberField(TEXT("cpuUnchangedRebuildMedianMs"),Samples[2]);Case->SetNumberField(TEXT("cpuUnchangedRebuildMinMs"),Samples[0]);Case->SetNumberField(TEXT("cpuUnchangedRebuildMaxMs"),Samples.Last());
		Samples.Reset();
		for(int32 Pass=0;Pass<6;++Pass)
		{
			const double Begin=FPlatformTime::Seconds();
			for(const auto& Fixture:Fixtures)
			{
				Fixture->Building->RebuildElementAndRelationshipIndexes();Fixture->Building->RebuildClosedLoops();
				for(auto* Wall:Fixture->Walls){Wall->Height=301.0f+(Pass%2);Wall->RebuildWallMesh();}
				for(auto* Pillar:Fixture->Pillars){Pillar->Height=301.0f+(Pass%2);Pillar->RebuildPillarMesh();}
			}
			if(Pass>0)Samples.Add((FPlatformTime::Seconds()-Begin)*1000);
		}
		Samples.Sort();
		Case->SetNumberField(TEXT("cpuChangedGeometryMedianMs"),Samples[2]);Case->SetNumberField(TEXT("cpuChangedGeometryMinMs"),Samples[0]);Case->SetNumberField(TEXT("cpuChangedGeometryMaxMs"),Samples.Last());
		TArray<TSharedPtr<FJsonValue>> Reports;
		for(const auto& Fixture:Fixtures)
		{
			const auto Report=UEHBBuildingPerformanceAnalyzer::AnalyzeBuilding(Fixture->Building,FEHBBuildingPerformanceBudget());
			TestTrue(TEXT("Building report valid"),Report.bValid);
			TestEqual(TEXT("Each baseline building has four columns and four walls"),Report.ElementCount,8);
			TSharedRef<FJsonObject> JsonReport=MakeShared<FJsonObject>();
			FJsonObjectConverter::UStructToJsonObject(FEHBBuildingPerformanceReport::StaticStruct(),&Report,JsonReport,0,0);
			Reports.Add(MakeShared<FJsonValueObject>(JsonReport));
		}
		Case->SetArrayField(TEXT("reports"),Reports);Cases.Add(MakeShared<FJsonValueObject>(Case));
	}
	Root->SetArrayField(TEXT("cases"),Cases);
	FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
	const FString Path=FPaths::ProjectSavedDir()/TEXT("EHB-Refactor")/(TEXT("ScaleBaseline_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT(".json"));
	TestTrue(TEXT("Scale baseline exported"),FFileHelper::SaveStringToFile(Json,*Path));AddInfo(Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeOwnershipMigrationTest,"EHB.Topology.NodeOwnershipMigration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeOwnershipMigrationTest::RunTest(const FString& Parameters)
{
	FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
	auto* Building=Fixture.Building;
	TArray<TWeakObjectPtr<AActor>> PreviousSelection;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))PreviousSelection.Add(Actor);
	GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
	auto ResultOf=[](const FString& Json){FEHBTopologyMigrationResult Result;FJsonObjectConverter::JsonObjectStringToUStruct(Json,&Result);return Result;};
	auto Snapshot=[&](){auto Data=MakeShared<FJsonObject>();Data->SetObjectField(TEXT("ownership"),FJsonObjectConverter::UStructToJsonObject(Building->WallNodeOwnership));Data->SetObjectField(TEXT("topology"),FJsonObjectConverter::UStructToJsonObject(UEHBWallTopologyLibrary::CaptureWallTopology(Building)));TArray<TSharedPtr<FJsonValue>> Relations;for(const auto& Relation:Building->ElementRelations){auto Value=FJsonObjectConverter::UStructToJsonObject(Relation);Value->RemoveField(TEXT("sourceGeometryRevision"));Value->RemoveField(TEXT("targetGeometryRevision"));Relations.Add(MakeShared<FJsonValueObject>(Value));}Data->SetArrayField(TEXT("relations"),Relations);Data->SetArrayField(TEXT("geometry"),BuildingGeometrySnapshot(Building,true));NormalizeRoomMoveTransformText(Data);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return Json;};
	const FString Before=Snapshot();const FGuid OriginalRoom=Building->GetClosedLoopsByFloor(1)[0].LoopGuid;
	const auto OriginalRelations=Building->ElementRelations;
	TestEqual(TEXT("Ownership preview ready"),ResultOf(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,false)).Status,FName(TEXT("Ready")));TestEqual(TEXT("Preview changes no records or geometry"),Snapshot(),Before);
	const auto Applied=ResultOf(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,true));TestTrue(*Applied.Status.ToString(),Applied.bSucceeded&&Applied.bChanged);
	if(!Applied.bSucceeded)return false;
	TestEqual(TEXT("Ownership enabled explicitly"),Building->WallNodeOwnership.Version,1);TestEqual(TEXT("One binding per current pillar"),Building->WallNodeOwnership.Bindings.Num(),4);
	TestEqual(TEXT("Room identity unchanged"),Building->GetClosedLoopsByFloor(1)[0].LoopGuid,OriginalRoom);
	TestTrue(TEXT("Typed capture valid"),UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty());
	for(const auto& Prior:OriginalRelations)
	{
		const auto* Current=Building->ElementRelations.FindByPredicate([&](const auto& R){return R.RelationGuid==Prior.RelationGuid;});if(!TestNotNull(TEXT("Relation identity preserved"),Current))return false;
		auto Expected=Prior;Expected.Target.Kind=EEHBRelationEndpointKind::WallNode;Expected.Target.NodeGuid=Prior.Target.ElementGuid;Expected.Target.ElementGuid.Invalidate();
		FString A,B;FJsonObjectConverter::UStructToJsonObjectString(*Current,A);FJsonObjectConverter::UStructToJsonObjectString(Expected,B);TestEqual(TEXT("Only endpoint kind/id changed, all relation metadata preserved"),A,B);
	}
	FEHBRelationQuery Query;Query.NodeGuid=Fixture.Pillars[0]->ElementGuid;TestEqual(TEXT("Typed incoming node index"),Building->QueryElementRelations(Query).Num(),2);
	Query.NodeGuid.Invalidate();Query.ElementGuid=Fixture.Pillars[0]->ElementGuid;TestEqual(TEXT("Physical element query no longer conflates node connections"),Building->QueryElementRelations(Query).Num(),0);
	const FString Migrated=Snapshot();const int32 Revision=Building->RelationshipGraphRevision;
	TestEqual(TEXT("Idempotent apply"),ResultOf(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,true)).Status,FName(TEXT("AlreadyMigrated")));TestEqual(TEXT("No-op leaves graph revision"),Building->RelationshipGraphRevision,Revision);
	TestTrue(TEXT("Undo actual migration"),GEditor->UndoTransaction());TestEqual(TEXT("Undo all migration records and generated geometry"),Snapshot(),Before);
	TestTrue(TEXT("Redo actual migration"),GEditor->RedoTransaction());TestEqual(TEXT("Redo all migration records and generated geometry"),Snapshot(),Migrated);
	GEditor->UndoTransaction(false);
	{TGuardValue<bool> Failure(EHBNodeOwnershipTestHooks::FailAfterApply,true);const auto Failed=ResultOf(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,true));TestEqual(TEXT("Post-apply failure uses production rollback"),Failed.Status,FName(TEXT("NodeOwnershipFailedRolledBack")));TestEqual(TEXT("Failure restores full original state"),Snapshot(),Before);TestFalse(TEXT("Failed migration cannot redo"),GEditor->Trans->CanRedo());}
	if(!ResultOf(UEHBBuildingToolset::MigrateWallNodeOwnership(Building,true)).bSucceeded)return false;
	UEHBWallTopologyLibrary::PrepareTopologyMigration(Building,true);
	const auto Move=UEHBBuildingToolset::CommitBasicNodeMove(Building,Fixture.Pillars[0]->ElementGuid,FVector::ZeroVector,FVector(-60,-30,0));TestTrue(*Move.Message,Move.bSucceeded);
	if(Move.bSucceeded){TestTrue(TEXT("Move undo with typed relations"),GEditor->UndoTransaction(false));TestEqual(TEXT("Move undo retains node authority"),Building->WallNodeOwnership.Version,1);}
	const auto Split=UEHBBuildingToolset::CommitPlainWallSplit(Building,Fixture.Walls[0]->ElementGuid,250,Building->RelationshipGraphRevision,Fixture.Walls[0]->LocalStart,Fixture.Walls[0]->LocalEnd,Fixture.Walls[0]->Height,Fixture.Walls[0]->Thickness);TestTrue(*Split.Message,Split.bSucceeded);
	if(Split.bSucceeded){TestEqual(TEXT("New split node owned"),Building->WallNodeOwnership.Bindings.Num(),5);TestTrue(TEXT("Split typed graph valid"),UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty());const FString SplitState=Snapshot();TestTrue(TEXT("Split undo"),GEditor->UndoTransaction());TestEqual(TEXT("Split undo removes new binding"),Building->WallNodeOwnership.Bindings.Num(),4);TestTrue(TEXT("Split redo"),GEditor->RedoTransaction());TestEqual(TEXT("Split redo restores binding, typed relations and full geometry"),Snapshot(),SplitState);GEditor->UndoTransaction(false);}
	FEHBElementRelation Logical;Logical.Type=EEHBElementRelationType::LogicalDependency;Logical.Source=FEHBElementRelationEndpoint::MakeNode(Fixture.Pillars[0]->ElementGuid);Logical.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[0]->ElementGuid);
	const FGuid First=Building->AddOrUpdateElementRelation(Logical);TestTrue(TEXT("Owned logical node accepted"),First.IsValid());Logical.Source=FEHBElementRelationEndpoint::MakeNode(Fixture.Pillars[1]->ElementGuid);const FGuid Second=Building->AddOrUpdateElementRelation(Logical);TestTrue(TEXT("Second logical node is not equivalent"),Second.IsValid()&&First!=Second);
	TestTrue(TEXT("Full endpoint identity validation has no false duplicates"),Building->ValidateElementRelationshipGraph(false,false).IsEmpty());
	Logical.Type=EEHBElementRelationType::StructuralSupport;TestFalse(TEXT("Node cannot become a structural support"),Building->AddOrUpdateElementRelation(Logical).IsValid());Logical.Type=EEHBElementRelationType::LogicalDependency;Logical.Source=FEHBElementRelationEndpoint::MakeNode(FGuid::NewGuid());TestFalse(TEXT("Unknown node not owned"),Building->AddOrUpdateElementRelation(Logical).IsValid());
	Building->RemoveElementRelation(First);Building->RemoveElementRelation(Second);
	const auto SavedOwnership=Building->WallNodeOwnership;Building->WallNodeOwnership.Version=99;const FString InvalidBefore=Snapshot();TestFalse(TEXT("Unknown version rejected"),Building->MigrateWallNodeOwnership(true).bSucceeded);TestEqual(TEXT("Unknown version never overwritten"),Snapshot(),InvalidBefore);Building->WallNodeOwnership=SavedOwnership;
	const auto Duplicate=Building->WallNodeOwnership.Bindings[0];Building->WallNodeOwnership.Bindings.Add(Duplicate);TestFalse(TEXT("Duplicate ownership rejected"),Building->MigrateWallNodeOwnership(true).bSucceeded);Building->WallNodeOwnership=SavedOwnership;
	const int32 ElementsBefore=Building->QueryElements(FEHBElementQuery()).Num();Fixture.Pillars[0]->Destroy();
	TestEqual(TEXT("Old full pillar delete retains documented wall deletion semantics"),Building->QueryElements(FEHBElementQuery()).Num(),ElementsBefore-3);
	TestEqual(TEXT("Full deletion removes owned binding"),Building->WallNodeOwnership.Bindings.Num(),3);TestTrue(TEXT("No dangling typed graph after full deletion"),UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty());
	Building->ClearAllElements();TestEqual(TEXT("Clear resets explicit ownership mode"),Building->WallNodeOwnership.Version,0);TestTrue(TEXT("Clear removes ownership records and relationships"),Building->WallNodeOwnership.Bindings.IsEmpty()&&Building->ElementRelations.IsEmpty());
	GEditor->SelectNone(false,true,false);for(const auto& Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBEditorDuplicateBaselineTest,"EHBValidation.Duplication.NativeEditorBaseline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBEditorDuplicateBaselineTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;
 UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!GUnrealEd||!Fixture.Create(World,RF_Transactional))return false;
 auto* Source=Fixture.Building;
 if(!TestTrue(TEXT("Source ownership migrates"),Source->MigrateWallNodeOwnership(true).bSucceeded))return false;
 auto Snapshot=[](AEHBBuildingActorBase* Building){FString Value;FJsonObjectConverter::UStructToJsonObjectString(UEHBWallTopologyLibrary::CaptureWallTopology(Building),Value);return Value;};
 const FString Before=Snapshot(Source);const FGuid SourceGuid=Source->BuildingGuid;
 TSet<FGuid> SourceIds;for(auto* Actor:Fixture.Actors)if(auto* Element=Cast<AEHBElementActorBase>(Actor))SourceIds.Add(Element->ElementGuid);
 TArray<AActor*> Copies;
 {FScopedTransaction Transaction(FText::FromString(TEXT("EHB native duplication baseline")));GUnrealEd->DuplicateActors(Fixture.Actors,Copies,Source->GetLevel(),FVector(2000,0,0));}
 TestEqual(TEXT("All selected actors duplicated"),Copies.Num(),Fixture.Actors.Num());
 AEHBBuildingActorBase* Copy=nullptr;for(auto* Actor:Copies){Fixture.Actors.Add(Actor);if(auto* B=Cast<AEHBBuildingActorBase>(Actor))Copy=B;}
 TestEqual(TEXT("Source topology unchanged by native import"),Snapshot(Source),Before);
 if(TestNotNull(TEXT("Copied building exists"),Copy))
 {
  TestTrue(TEXT("Copied building has distinct identity"),Copy->BuildingGuid!=SourceGuid);
  int32 SharedIds=0;for(auto* Actor:Copies)if(auto* Element=Cast<AEHBElementActorBase>(Actor)){SharedIds+=SourceIds.Contains(Element->ElementGuid)?1:0;TestTrue(TEXT("Copied element owned by copy"),Element->OwningBuilding==Copy);}
  TestEqual(TEXT("Copied element IDs independent"),SharedIds,0);
  const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Copy);
  for(const auto& Issue:Graph.Issues)AddInfo(Issue.Code.ToString()+TEXT(": ")+Issue.Message);
  TestTrue(TEXT("Copied typed topology valid"),Graph.Issues.IsEmpty());
  TestEqual(TEXT("Copied room remains closed"),Copy->GetClosedLoopsByFloor(1).Num(),1);
  AddInfo(FString::Printf(TEXT("Native paste baseline: copies=%d sharedIds=%d issues=%d sourceGuid=%s copyGuid=%s"),Copies.Num(),SharedIds,Graph.Issues.Num(),*SourceGuid.ToString(),*Copy->BuildingGuid.ToString()));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallNodeCopyDraftTest,"EHB.Topology.WallNodeCopyDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallNodeCopyDraftTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
 auto* Building=Fixture.Building;
 if(!TestTrue(TEXT("Migrate source ownership"),Building->MigrateWallNodeOwnership(true).bSucceeded))return false;
 FEHBPreparedWallNodeDefinitions Source;
 if(!TestTrue(TEXT("Capture current model"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Building,Source).bSucceeded))return false;
 TArray<FGuid> Elements;for(auto* Element:Building->QueryElements(FEHBElementQuery()))Elements.Add(Element->ElementGuid);
 auto Relations=Building->ElementRelations;
 FEHBElementRelation Logical;Logical.RelationGuid=FGuid::NewGuid();Logical.Type=EEHBElementRelationType::LogicalDependency;
 Logical.Source=FEHBElementRelationEndpoint::MakeNode(Source.Nodes[0].NodeGuid);
 Logical.Target=FEHBElementRelationEndpoint::MakeElement(Source.PillarBindings[0].PhysicalPillarGuid,EEHBElementSurfaceKind::Top,TEXT("Cap"),7);
 Logical.bEnabled=false;Logical.bGeometryDependent=true;Logical.Origin=EEHBRelationOrigin::ImportedLegacy;Logical.ContactPoint=FVector(12,34,56);Logical.ContactArea=73;
 Logical.NumericMetadata.Add(TEXT("Distance"),42.5);Logical.SourceGeometryRevision=17;Logical.TargetGeometryRevision=23;Relations.Add(Logical);
 auto Serialize=[](const auto& Value){FString Json;FJsonObjectConverter::UStructToJsonObjectString(Value,Json);return Json;};
 const FString OriginalModel=Serialize(Source),OriginalGraph=Serialize(UEHBWallTopologyLibrary::CaptureWallTopology(Building));
 const auto Draft=FEHBWallNodeCopy::BuildDraft(Building->BuildingGuid,Source,Elements,Relations);
 if(!TestTrue(*Draft.Status.ToString(),Draft.bSucceeded))return false;
 TestEqual(TEXT("Source model unchanged"),Serialize(Source),OriginalModel);TestEqual(TEXT("Source graph unchanged"),Serialize(UEHBWallTopologyLibrary::CaptureWallTopology(Building)),OriginalGraph);
 TestEqual(TEXT("Complete element mapping"),Draft.ElementGuids.Num(),Elements.Num());TestEqual(TEXT("Complete node mapping"),Draft.NodeGuids.Num(),Source.Nodes.Num());TestEqual(TEXT("Complete relation mapping"),Draft.RelationGuids.Num(),Relations.Num());TestEqual(TEXT("Single room mapped"),Draft.RoomGuids.Num(),1);
 TSet<FGuid> OldIds;OldIds.Add(Building->BuildingGuid);for(FGuid Id:Elements)OldIds.Add(Id);for(const auto& N:Source.Nodes)OldIds.Add(N.NodeGuid);for(const auto& R:Relations)OldIds.Add(R.RelationGuid);for(const auto& Pair:Draft.RoomGuids)OldIds.Add(Pair.Key);
 TSet<FGuid> NewIds;
 auto CheckId=[&](FGuid Id){TestTrue(TEXT("New identity valid, fresh and unique across domains"),Id.IsValid()&&!OldIds.Contains(Id)&&!NewIds.Contains(Id));NewIds.Add(Id);};CheckId(Draft.BuildingGuid);
 for(const auto* Map:{&Draft.ElementGuids,&Draft.NodeGuids,&Draft.RelationGuids,&Draft.RoomGuids})for(const auto& Pair:*Map)CheckId(Pair.Value);
 const FGuid SharedSourceId=Source.PillarBindings[0].PhysicalPillarGuid;
 TestTrue(TEXT("Fixture has numeric node/physical alias"),Draft.NodeGuids.Contains(SharedSourceId));
 if(Draft.NodeGuids.Contains(SharedSourceId))TestTrue(TEXT("Equal source values mapped in separate typed namespaces"),Draft.NodeGuids.FindChecked(SharedSourceId)!=Draft.ElementGuids.FindChecked(SharedSourceId));
 for(int32 I=0;I<Relations.Num();++I)
 {
  auto Expected=Relations[I];Expected.RelationGuid=Draft.RelationGuids.FindChecked(Expected.RelationGuid);
  for(auto* Endpoint:{&Expected.Source,&Expected.Target}){if(Endpoint->Kind==EEHBRelationEndpointKind::WallNode)Endpoint->NodeGuid=Draft.NodeGuids.FindChecked(Endpoint->NodeGuid);else Endpoint->ElementGuid=Draft.ElementGuids.FindChecked(Endpoint->ElementGuid);}
  TestEqual(TEXT("Only typed identities change; ports, flags, metadata, contact and revisions preserved"),Serialize(Draft.Relations[I]),Serialize(Expected));
 }
 TArray<FEHBWallJunctionWallSides> BeforeSides,AfterSides;FName Reason;
 TestTrue(TEXT("Source geometry solves"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Source,BeforeSides,Reason));
 TestTrue(TEXT("Copied geometry solves"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Draft.Model,AfterSides,Reason));
 for(const auto& Before:BeforeSides)
 {
  const auto* After=AfterSides.FindByPredicate([&](const auto& W){return W.WallGuid==Draft.ElementGuids.FindChecked(Before.WallGuid);});
  TestTrue(TEXT("Mapped walls retain full side geometry"),After&&Before.LocalTransform.Equals(After->LocalTransform,0.001)&&Before.StartLeft.Equals(After->StartLeft,0.001)&&Before.EndLeft.Equals(After->EndLeft,0.001)&&Before.StartRight.Equals(After->StartRight,0.001)&&Before.EndRight.Equals(After->EndRight,0.001));
 }
 TArray<FEHBNodeRoomBoundary> BeforeRooms,AfterRooms;TestTrue(TEXT("Source rooms solve"),FEHBWallNodeRooms::Build(Building->BuildingGuid,Source,BeforeRooms,Reason));TestTrue(TEXT("Copied rooms solve"),FEHBWallNodeRooms::Build(Draft.BuildingGuid,Draft.Model,AfterRooms,Reason));
 for(const auto& Room:BeforeRooms){const auto* Match=AfterRooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Draft.RoomGuids.FindChecked(Room.RoomGuid);});TestTrue(TEXT("Room remap preserves floor and area"),Match&&Match->FloorIndex==Room.FloorIndex&&FMath::IsNearlyEqual(Match->Area,Room.Area,0.001));}
 auto Refused=[&](const FEHBWallNodeModel& Model,const TArray<FGuid>& Ids,const TArray<FEHBElementRelation>& Edges,FName Expected)
 {
  const auto Failed=FEHBWallNodeCopy::BuildDraft(Building->BuildingGuid,Model,Ids,Edges);TestFalse(TEXT("Invalid copy refuses"),Failed.bSucceeded);TestEqual(TEXT("Specific failure reason"),Failed.Status,Expected);
  TestTrue(TEXT("Failure exposes no partial new identities, geometry or relations"),!Failed.BuildingGuid.IsValid()&&Failed.Model.Version==0&&Failed.Model.Nodes.IsEmpty()&&Failed.Relations.IsEmpty()&&Failed.NodeGuids.IsEmpty()&&Failed.ElementGuids.IsEmpty()&&Failed.RelationGuids.IsEmpty()&&Failed.RoomGuids.IsEmpty());
 };
 auto BadModel=Source;BadModel.Version=99;Refused(BadModel,Elements,Relations,TEXT("UnsupportedDefinitionVersion"));
 auto BadIds=Elements;BadIds.Add(Elements[0]);Refused(Source,BadIds,Relations,TEXT("InvalidElementIdentity"));
 BadIds=Elements;BadIds.Remove(Source.Walls[0].WallGuid);Refused(Source,BadIds,Relations,TEXT("WallOutsideCopyBoundary"));
 BadIds=Elements;BadIds.Remove(Source.PillarBindings[0].PhysicalPillarGuid);Refused(Source,BadIds,Relations,TEXT("PillarOutsideCopyBoundary"));
 auto BadRelations=Relations;BadRelations.Last().Source.NodeGuid=FGuid::NewGuid();Refused(Source,Elements,BadRelations,TEXT("NodeOutsideCopyBoundary"));
 BadRelations=Relations;BadRelations.Last().Target.ElementGuid=FGuid::NewGuid();Refused(Source,Elements,BadRelations,TEXT("ElementOutsideCopyBoundary"));
 BadRelations=Relations;BadRelations.Last().Target.Kind=EEHBRelationEndpointKind::WorldGround;Refused(Source,Elements,BadRelations,TEXT("ExternalCopyPolicyRequired"));
 BadRelations=Relations;BadRelations.Last().Target.Kind=EEHBRelationEndpointKind::ExternalActor;Refused(Source,Elements,BadRelations,TEXT("ExternalCopyPolicyRequired"));
 BadRelations=Relations;BadRelations.Last().StringMetadata.Add(TEXT("PostGuid"),FGuid::NewGuid().ToString());Refused(Source,Elements,BadRelations,TEXT("MetadataCopyPolicyRequired"));
 BadRelations=Relations;BadRelations.Last().Type=EEHBElementRelationType::StructuralSupport;Refused(Source,Elements,BadRelations,TEXT("NodeCannotBePhysicalHost"));
 BadRelations=Relations;BadRelations.Add(Relations[0]);Refused(Source,Elements,BadRelations,TEXT("InvalidRelationIdentity"));
 BadRelations=Relations;BadRelations.Add(Relations[0]);BadRelations.Last().RelationGuid=FGuid::NewGuid();Refused(Source,Elements,BadRelations,TEXT("DuplicateRelation"));
 BadRelations=Relations;BadRelations.RemoveAt(0);Refused(Source,Elements,BadRelations,TEXT("MissingWallPort"));
 BadRelations=Relations;const FGuid PreviousTarget=BadRelations[0].Target.NodeGuid;for(const auto& Node:Source.Nodes)if(Node.NodeGuid!=PreviousTarget){BadRelations[0].Target.NodeGuid=Node.NodeGuid;break;}Refused(Source,Elements,BadRelations,TEXT("TopologyModelMismatch"));
 auto Unbound=Source;const FGuid RemovedPhysical=Unbound.PillarBindings[0].PhysicalPillarGuid;Unbound.PillarBindings.RemoveAt(0);auto UnboundElements=Elements;UnboundElements.Remove(RemovedPhysical);
 const auto UnboundDraft=FEHBWallNodeCopy::BuildDraft(Building->BuildingGuid,Unbound,UnboundElements,Building->ElementRelations);
 TestTrue(TEXT("Unbound model copies without resurrecting physical pillar"),UnboundDraft.bSucceeded&&UnboundDraft.Model.PillarBindings.Num()==Source.PillarBindings.Num()-1&&!UnboundDraft.ElementGuids.Contains(RemovedPhysical));
 auto Reordered=Source;Algo::Reverse(Reordered.Nodes);Algo::Reverse(Reordered.Walls);Algo::Reverse(Reordered.PillarBindings);auto ReverseRelations=Relations;Algo::Reverse(ReverseRelations);auto ReverseElements=Elements;Algo::Reverse(ReverseElements);
 const auto ReorderedDraft=FEHBWallNodeCopy::BuildDraft(Building->BuildingGuid,Reordered,ReverseElements,ReverseRelations);TestTrue(TEXT("Reordered input preserves completeness without using array order to identify a room"),ReorderedDraft.bSucceeded&&ReorderedDraft.RoomGuids.Num()==Draft.RoomGuids.Num());

 // Two unequal rooms verify one-to-one room correspondence, not just equal counts.
 FEHBWallNodeModel TwoRooms=Source;TArray<FGuid> TwoElements=Elements;auto TwoRelations=Building->ElementRelations;
 TMap<FGuid,FGuid> SecondNodes;
 for(const auto& Node:Source.Nodes){auto Copy=Node;Copy.NodeGuid=FGuid::NewGuid();FVector P=Copy.LocalTransform.GetLocation();P.X=P.X*0.5+1500;Copy.LocalTransform.SetLocation(P);SecondNodes.Add(Node.NodeGuid,Copy.NodeGuid);TwoRooms.Nodes.Add(Copy);}
 for(const auto& Binding:Source.PillarBindings){auto Copy=Binding;Copy.NodeGuid=SecondNodes.FindChecked(Binding.NodeGuid);Copy.PhysicalPillarGuid=FGuid::NewGuid();TwoRooms.PillarBindings.Add(Copy);TwoElements.Add(Copy.PhysicalPillarGuid);}
 for(const auto& Wall:Source.Walls)
 {
  auto Copy=Wall;Copy.WallGuid=FGuid::NewGuid();Copy.StartNodeGuid=SecondNodes.FindChecked(Wall.StartNodeGuid);Copy.EndNodeGuid=SecondNodes.FindChecked(Wall.EndNodeGuid);TwoRooms.Walls.Add(Copy);TwoElements.Add(Copy.WallGuid);
  for(int32 Port=0;Port<2;++Port){FEHBElementRelation R;R.RelationGuid=FGuid::NewGuid();R.Type=EEHBElementRelationType::TopologyConnection;R.Source=FEHBElementRelationEndpoint::MakeElement(Copy.WallGuid,Port==0?EEHBElementSurfaceKind::Start:EEHBElementSurfaceKind::End);R.Target=FEHBElementRelationEndpoint::MakeNode(Port==0?Copy.StartNodeGuid:Copy.EndNodeGuid);TwoRelations.Add(R);}
 }
 Algo::Reverse(TwoRooms.Nodes);Algo::Reverse(TwoRooms.Walls);Algo::Reverse(TwoRelations);Algo::Reverse(TwoElements);
 const auto TwoDraft=FEHBWallNodeCopy::BuildDraft(Building->BuildingGuid,TwoRooms,TwoElements,TwoRelations);
 TestTrue(TEXT("Two unequal rooms produce complete independent mappings"),TwoDraft.bSucceeded&&TwoDraft.RoomGuids.Num()==2);
 if(TwoDraft.bSucceeded)
 {
  TArray<FEHBNodeRoomBoundary> OriginalRooms,CopiedRooms;FEHBWallNodeRooms::Build(Building->BuildingGuid,TwoRooms,OriginalRooms,Reason);FEHBWallNodeRooms::Build(TwoDraft.BuildingGuid,TwoDraft.Model,CopiedRooms,Reason);
  for(const auto& Room:OriginalRooms){const auto* Match=CopiedRooms.FindByPredicate([&](const auto& R){return R.RoomGuid==TwoDraft.RoomGuids.FindChecked(Room.RoomGuid);});TestTrue(TEXT("Each reordered room maps to its own geometry"),Match&&FMath::IsNearlyEqual(Room.Area,Match->Area,0.001));if(Match){TSet<FGuid> Expected;for(FGuid W:Room.WallGuids)Expected.Add(TwoDraft.ElementGuids.FindChecked(W));for(FGuid W:Match->WallGuids)TestTrue(TEXT("Copied boundary belongs to the matched source room"),Expected.Contains(W));}}
 }
 TArray<TWeakObjectPtr<AActor>> Selection;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))Selection.Add(Actor);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
 TSharedPtr<FJsonObject> Preview;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UEHBBuildingToolset::PreviewWallNodeCopy(Building)),Preview);
 TestTrue(TEXT("Editor preview exposes a successful draft, never authorizes live commit"),Preview.IsValid()&&Preview->GetBoolField(TEXT("bSucceeded"))&&!Preview->GetBoolField(TEXT("bCanCommit")));
 TestEqual(TEXT("Editor preview leaves source graph unchanged"),Serialize(UEHBWallTopologyLibrary::CaptureWallTopology(Building)),OriginalGraph);
 GEditor->SelectNone(false,true,false);for(const auto& Actor:Selection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
 return true;
}

namespace
{
 FString NormalizeCopySnapshotText(FString Json)
 {
  // A yaw-only FTransform may print either -180 or +180 for the same quaternion.
  // Restrict normalization to the complete rotation field, never positions or design numbers.
  Json.ReplaceInline(TEXT("|0.000000,-180.000000,0.000000|"),TEXT("|0.000000,180.000000,0.000000|"));
  // Textual signed zero is not a geometry difference; retain all IDs and nonzero numbers.
  Json.ReplaceInline(TEXT("-0.000000,"),TEXT("0.000000,"));Json.ReplaceInline(TEXT("-0.000000|"),TEXT("0.000000|"));
  // FVector/UV text uses three decimals. Normalize only a complete signed-zero
  // token after '=', never a JSON design number or a higher-precision nonzero.
  int32 Search=0;
  while((Search=Json.Find(TEXT("=-0.000"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Search))!=INDEX_NONE)
  {
   const int32 End=Search+7;
   if(End==Json.Len()||!FChar::IsDigit(Json[End]))Json.RemoveAt(Search+1,1);
   ++Search;
  }
  return Json;
 }
 FString BuildingCopySnapshot(AEHBBuildingActorBase* Building)
 {
  auto Data=MakeShared<FJsonObject>();
  Data->SetStringField(TEXT("buildingTransform"),Building->GetActorTransform().ToString());
  Data->SetObjectField(TEXT("graph"),FJsonObjectConverter::UStructToJsonObject(UEHBWallTopologyLibrary::CaptureWallTopology(Building)));
  Data->SetObjectField(TEXT("ownership"),FJsonObjectConverter::UStructToJsonObject(Building->WallNodeOwnership));
  if(Building->WallNodeAuthority.Version!=0||!Building->WallNodeAuthority.Nodes.IsEmpty())Data->SetObjectField(TEXT("nodeAuthority"),FJsonObjectConverter::UStructToJsonObject(Building->WallNodeAuthority));
  Data->SetObjectField(TEXT("baseline"),FJsonObjectConverter::UStructToJsonObject(Building->TopologyMigrationBaseline));
  Data->SetObjectField(TEXT("prepared"),FJsonObjectConverter::UStructToJsonObject(Building->PreparedWallNodeDefinitions));
  auto Relations=Building->ElementRelations;Relations.Sort([](const auto& A,const auto& B){return A.RelationGuid<B.RelationGuid;});
  TArray<TSharedPtr<FJsonValue>> Values;
  for(const auto& Relation:Relations){auto V=FJsonObjectConverter::UStructToJsonObject(Relation);V->RemoveField(TEXT("sourceGeometryRevision"));V->RemoveField(TEXT("targetGeometryRevision"));Values.Add(MakeShared<FJsonValueObject>(V));}
  Data->SetArrayField(TEXT("relations"),Values);Data->SetArrayField(TEXT("geometry"),BuildingGeometrySnapshot(Building,true));
  NormalizeRoomMoveTransformText(Data);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return NormalizeCopySnapshotText(Json);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWholeBuildingCopyTest,"EHB.Topology.WholeBuildingCopyTransaction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWholeBuildingCopyTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional))return false;
 auto* Source=Fixture.Building;
 if(!Source->MigrateWallNodeOwnership(true).bSucceeded||!UEHBWallTopologyLibrary::PrepareTopologyMigration(Source,true).bSucceeded||!UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Source,true).bSucceeded)return false;

 const auto OriginalBaseline=Source->TopologyMigrationBaseline;
 Source->TopologyMigrationBaseline.Nodes[0].LocalPosition.X+=0.0000001;
 TestEqual(TEXT("Baseline accepts transform roundoff"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Source,false).Status,FName("AlreadyInitialized"));
 Source->TopologyMigrationBaseline.Nodes[0].LocalPosition.X+=0.001;
 TestEqual(TEXT("Baseline still detects actual position changes"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Source,false).Status,FName("SourceChanged"));
 Source->TopologyMigrationBaseline=OriginalBaseline;
 const FString Before=BuildingCopySnapshot(Source);
 auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};
 const int32 BeforeCount=Count();
 const auto Copied=EHBBuildingCopy::Execute(Source,FVector(2000,300,0));
 if(!TestTrue(*Copied.Status.ToString(),Copied.bSucceeded))return false;
 auto* Copy=Copied.Building;Fixture.Actors.Append(Copied.Actors);
 TestEqual(TEXT("Exactly nine copied actors"),Count(),BeforeCount+9);
 TestEqual(TEXT("Import preserves source identity, geometry and all authoring records"),BuildingCopySnapshot(Source),Before);
 TestFalse(TEXT("Staging scope closed"),FEHBActorImportScope::IsActive());
 TestTrue(TEXT("World offset applied once"),Copy->GetActorLocation().Equals(Source->GetActorLocation()+FVector(2000,300,0),0.001));
 TestTrue(TEXT("Copy topology valid"),UEHBWallTopologyLibrary::CaptureWallTopology(Copy).Issues.IsEmpty());
 TestEqual(TEXT("Mapped baseline still matches"),UEHBWallTopologyLibrary::PrepareTopologyMigration(Copy,false).Status,FName("AlreadyInitialized"));
 TestEqual(TEXT("Mapped preparation still matches"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Copy,false).Status,FName("AlreadyPrepared"));
 const auto CopyRooms=Copy->GetClosedLoopsByFloor(1);TestEqual(TEXT("Room boundary rebuilt"),CopyRooms.Num(),1);
 if(!CopyRooms.IsEmpty())TestTrue(TEXT("Copy room has independent ID"),CopyRooms[0].LoopGuid!=Source->GetClosedLoopsByFloor(1)[0].LoopGuid);
 TSet<FGuid> OriginalIds;OriginalIds.Add(Source->BuildingGuid);for(auto* E:Source->QueryElements(FEHBElementQuery()))OriginalIds.Add(E->ElementGuid);for(auto& B:Source->WallNodeOwnership.Bindings)OriginalIds.Add(B.NodeGuid);for(auto& R:Source->ElementRelations)OriginalIds.Add(R.RelationGuid);
 TestFalse(TEXT("New building identity"),OriginalIds.Contains(Copy->BuildingGuid));
 for(auto* E:Copy->QueryElements(FEHBElementQuery())){TestFalse(TEXT("New element identity"),OriginalIds.Contains(E->ElementGuid));TestTrue(TEXT("Correct copy owner"),E->OwningBuilding==Copy);}
 for(auto& B:Copy->WallNodeOwnership.Bindings){TestFalse(TEXT("New node identity"),OriginalIds.Contains(B.NodeGuid));TestTrue(TEXT("Separate new node and pillar domains"),B.NodeGuid!=B.PhysicalPillarGuid);}
 for(auto& R:Copy->ElementRelations)TestFalse(TEXT("New relation identity"),OriginalIds.Contains(R.RelationGuid));
 const FString CopiedSnapshot=BuildingCopySnapshot(Copy);
 TestTrue(TEXT("Undo whole creation"),GEditor->UndoTransaction());TestEqual(TEXT("Undo removes whole group"),Count(),BeforeCount);TestEqual(TEXT("Undo preserves source"),BuildingCopySnapshot(Source),Before);
 TestTrue(TEXT("Redo whole creation"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores all actors"),Count(),BeforeCount+9);TestEqual(TEXT("Redo restores exact identities, poses, snapshots and mesh data"),BuildingCopySnapshot(Copy),CopiedSnapshot);
 TArray<TWeakObjectPtr<AActor>> Selection;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* Actor=Cast<AActor>(*It))Selection.Add(Actor);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy,true,false);
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(Copy);const auto& Node=Graph.Nodes[0];
 const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Copy,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(15,0,0),false);
 TestTrue(*Move.Message,Move.bSucceeded);TestEqual(TEXT("Editing copy leaves source unchanged"),BuildingCopySnapshot(Source),Before);
 if(Move.bSucceeded){TestTrue(TEXT("Undo copied-node move"),GEditor->UndoTransaction(false));TestEqual(TEXT("Copy edit undo restores exact copied state"),BuildingCopySnapshot(Copy),CopiedSnapshot);}
 GEditor->SelectNone(false,true,false);for(const auto& Actor:Selection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
 for(bool bAfterApply:{false,true})
 {
  if(bAfterApply)EHBBuildingCopy::FailAfterApply=true;else EHBBuildingCopy::FailAfterImport=true;
  const auto Failed=EHBBuildingCopy::Execute(Source,FVector(4000,0,0));
  TestFalse(TEXT("Injected failure is not success"),Failed.bSucceeded);TestEqual(TEXT("Formal transaction rollback"),Failed.Status,FName("BuildingCopyFailedRolledBack"));
  TestEqual(TEXT("No leaked imported actors"),Count(),BeforeCount+9);TestEqual(TEXT("Failure keeps source exact"),BuildingCopySnapshot(Source),Before);TestEqual(TEXT("Failure keeps existing copy exact"),BuildingCopySnapshot(Copy),CopiedSnapshot);
  TestFalse(TEXT("Failed copy cannot redo"),GEditor->RedoTransaction());TestFalse(TEXT("Failure leaves no import staging"),FEHBActorImportScope::IsActive());
 }
 const auto Prepared=Source->PreparedWallNodeDefinitions;
 Source->PreparedWallNodeDefinitions.Nodes[0].NodeGuid=FGuid::NewGuid();
 TestEqual(TEXT("Unknown historical identity rejected before import"),EHBBuildingCopy::Execute(Source,FVector(5000,0,0)).Status,FName("HistoricalIdentityCopyPolicyRequired"));
 Source->PreparedWallNodeDefinitions=Prepared;TestEqual(TEXT("Preflight creates nothing"),Count(),BeforeCount+9);
 TestEqual(TEXT("Nonfinite offset refused"),EHBBuildingCopy::Execute(Source,FVector(std::numeric_limits<double>::quiet_NaN(),0,0)).Status,FName("InvalidWorldOffset"));
 {FScopedTransaction Outer(FText::FromString(TEXT("Nested copy rejection")));TestEqual(TEXT("Nested transaction rejected"),EHBBuildingCopy::Execute(Source,FVector(5000,0,0)).Status,FName("RequiresIndependentEditorTransaction"));Outer.Cancel();}
 const auto Again=EHBBuildingCopy::Execute(Copy,FVector(4000,0,0));TestTrue(TEXT("Already copied building can be copied again"),Again.bSucceeded);
 if(Again.bSucceeded){Fixture.Actors.Append(Again.Actors);TestTrue(TEXT("Second-generation copy topology valid"),UEHBWallTopologyLibrary::CaptureWallTopology(Again.Building).Issues.IsEmpty());TestTrue(TEXT("Undo second-generation copy"),GEditor->UndoTransaction(false));}
 TestEqual(TEXT("Second-generation copy preserves first copy"),BuildingCopySnapshot(Copy),CopiedSnapshot);TestEqual(TEXT("Second-generation copy leaves original untouched"),BuildingCopySnapshot(Source),Before);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Source,true,false);
 TSharedPtr<FJsonObject> ToolResult;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UEHBBuildingToolset::CopyWallNodeBuilding(Source,FVector(6000,0,0))),ToolResult);
 const bool bToolCopied=ToolResult.IsValid()&&ToolResult->GetBoolField(TEXT("bSucceeded"));TestTrue(TEXT("Selected-building tool executes actual copy"),bToolCopied);
 if(bToolCopied){TestEqual(TEXT("Tool reports complete group"),ToolResult->GetIntegerField(TEXT("actorCount")),9);TestTrue(TEXT("Undo tool copy"),GEditor->UndoTransaction(false));}
 GEditor->SelectNone(false,true,false);for(const auto& Actor:Selection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false);
 TestEqual(TEXT("No actor leaks after all commands"),Count(),BeforeCount+9);

 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCopyPersistenceWriteTest,"EHBValidation.Persistence.WriteBuildingCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCopyPersistenceWriteTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("BuildingCopy")))return false;
 if(FPackageName::DoesPackageExist(MapPath)){AddError(TEXT("Refusing to overwrite existing persistence map"));return false;}
 UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;
 auto* Source=Fixture.Building;Source->SetActorRotation(FRotator(0,37,0));
 if(!Source->MigrateWallNodeOwnership(true).bSucceeded||!UEHBWallTopologyLibrary::PrepareTopologyMigration(Source,true).bSucceeded||!UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Source,true).bSucceeded)return false;
 const auto Copy=EHBBuildingCopy::Execute(Source,FVector(2000,500,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;Fixture.Actors.Append(Copy.Actors);
 auto Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("schema"),TEXT("EHB.BuildingCopyPersistence.v1"));
 Evidence->SetStringField(TEXT("sourceGuid"),Source->BuildingGuid.ToString());Evidence->SetStringField(TEXT("copyGuid"),Copy.Building->BuildingGuid.ToString());
 Evidence->SetStringField(TEXT("source"),BuildingCopySnapshot(Source));Evidence->SetStringField(TEXT("copy"),BuildingCopySnapshot(Copy.Building));
 if(!TestTrue(TEXT("Save source and copied building together"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
 FString Json;FJsonSerializer::Serialize(Evidence,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Write full copy evidence"),FFileHelper::SaveStringToFile(Json,*EvidencePath));Fixture.Actors.Reset();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCopyPersistenceReadTest,"EHBValidation.Persistence.ReadBuildingCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCopyPersistenceReadTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("BuildingCopy")))return false;
 UWorld* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=MapPath)return false;
 FString Json;TSharedPtr<FJsonObject> Evidence;if(!FFileHelper::LoadFileToString(Json,*EvidencePath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Evidence)||Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.BuildingCopyPersistence.v1"))return false;
 Evidence->SetStringField(TEXT("source"),NormalizeCopySnapshotText(Evidence->GetStringField(TEXT("source"))));Evidence->SetStringField(TEXT("copy"),NormalizeCopySnapshotText(Evidence->GetStringField(TEXT("copy"))));
 AEHB_Building *Source=nullptr,*Copy=nullptr;int32 Count=0;
 for(TActorIterator<AEHB_Building> It(World);It;++It){++Count;if(It->BuildingGuid.ToString()==Evidence->GetStringField(TEXT("sourceGuid")))Source=*It;if(It->BuildingGuid.ToString()==Evidence->GetStringField(TEXT("copyGuid")))Copy=*It;}
 if(!TestEqual(TEXT("Two independent persisted buildings"),Count,2)||!Source||!Copy||Source==Copy)return false;
 TestEqual(TEXT("Source exact after independent reload"),BuildingCopySnapshot(Source),Evidence->GetStringField(TEXT("source")));
 TestEqual(TEXT("Copy identities, snapshots and full generated geometry reload"),BuildingCopySnapshot(Copy),Evidence->GetStringField(TEXT("copy")));
 for(auto* Building:{Source,Copy})
 {
  TestTrue(TEXT("Loaded topology valid"),UEHBWallTopologyLibrary::CaptureWallTopology(Building).Issues.IsEmpty());
  TestEqual(TEXT("Loaded room resolves"),Building->GetClosedLoopsByFloor(1).Num(),1);
  TestEqual(TEXT("Loaded preparation matches"),UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Building,false).Status,FName("AlreadyPrepared"));
  Building->RebuildElementAndRelationshipIndexes();for(auto* E:Building->QueryElements(FEHBElementQuery())){if(auto* P=Cast<AEHB_Pillar>(E))P->RebuildPillarMesh();if(auto* W=Cast<AEHB_Wall>(E))W->RefreshFromConnectedPillars();}
 }
 TestEqual(TEXT("Rebuild preserves copied geometry and data"),BuildingCopySnapshot(Copy),Evidence->GetStringField(TEXT("copy")));
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy,true,false);
 const auto Node=UEHBWallTopologyLibrary::CaptureWallTopology(Copy).Nodes[0];
 const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Copy,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0),false);
 TestTrue(*Move.Message,Move.bSucceeded);TestEqual(TEXT("Loaded copy edit does not affect source"),BuildingCopySnapshot(Source),Evidence->GetStringField(TEXT("source")));
 if(Move.bSucceeded){TestTrue(TEXT("Undo copy edit after load"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo returns to persisted copy"),BuildingCopySnapshot(Copy),Evidence->GetStringField(TEXT("copy")));}
 return true;
}

namespace
{
 bool AddCopyRoomOutlines(FTransientTopologyFixture& Fixture,bool bSurfaceFinish,bool bDetached=false,bool bAddFinishes=true)
 {
  auto* B=Fixture.Building;auto* World=B->GetWorld();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
  TArray<AEHB_Pillar*> Adjacent;
  const TArray<FVector> AdjacentPositions=bDetached?TArray<FVector>{FVector(3000,0,0),FVector(3600,0,0),FVector(3600,500,0),FVector(3000,500,0)}:TArray<FVector>{FVector(1000,0,0),FVector(1000,500,0)};
  for(FVector P:AdjacentPositions)
  {
   auto* Pillar=World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(),B->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Pillar)return false;
   Fixture.Actors.Add(Pillar);Adjacent.Add(Pillar);Pillar->AttachToBuilding(B,FTransform(P));Pillar->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
  }
  TArray<AEHB_Pillar*> Path=bDetached?Adjacent:TArray<AEHB_Pillar*>{Fixture.Pillars[1],Adjacent[0],Adjacent[1],Fixture.Pillars[2]};if(bDetached)Path.Add(Adjacent[0]);
  for(int32 I=0;I+1<Path.Num();++I){auto* Wall=B->ConnectPillars(Path[I],Path[I+1],300,20);if(!Wall)return false;Fixture.Actors.Add(Wall);}
  if(!B->MigrateWallNodeOwnership(true).bSucceeded)return false;
  const auto Rooms=B->GetClosedLoopsByFloor(1);if(Rooms.Num()!=2)return false;
  for(const auto& Room:Rooms)
  {
   if(!bAddFinishes)continue;
   FVector Center=FVector::ZeroVector;for(FGuid Id:Room.PillarGuids)Center+=B->FindElementActorByGuid(Id)->GetElementLocalTransform().GetLocation();Center/=Room.PillarGuids.Num();Center.Z=300;
   auto* Slab=World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(),B->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Slab)return false;Fixture.Actors.Add(Slab);
   Slab->ConfigureDefaultSlab(B,FTransform(FRotator(0,17,0),Center),100,20,false);Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);
   if(!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))return false;
   Slab->bHasRoomFillAnchor=true;Slab->RoomFillAnchorWallGuid=Room.WallGuids[0];Slab->RoomFillAnchorWallSide=EEHBFloorSlabWallSide::Left;Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
   auto* Floor=World->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(),B->GetActorLocation(),FRotator::ZeroRotator,Params);if(!Floor)return false;Fixture.Actors.Add(Floor);
   if(!Floor->ConfigureFromRoomLoop(B,Room,bSurfaceFinish?300:0,true))return false;
   if(bSurfaceFinish)Floor->RecordOutlineSource(EEHBOutlineSource::RoomSupportFill);
  }
  return UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true).bSucceeded&&UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(B,true).bSucceeded;
 }
 FString BuildingOutlineCopySnapshot(AEHBBuildingActorBase* Building)
 {
  TSharedPtr<FJsonObject> Data;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BuildingCopySnapshot(Building)),Data);
  auto Elements=Building->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& B){return A.ElementGuid<B.ElementGuid;});TArray<TSharedPtr<FJsonValue>> Values;
  for(auto* E:Elements)
  {
   auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("id"),E->ElementGuid.ToString());Value->SetNumberField(TEXT("floor"),E->FloorIndex);Value->SetNumberField(TEXT("role"),static_cast<int32>(E->FloorRole));
   if(auto* F=Cast<AEHB_Floor>(E))
   {
    Value->SetStringField(TEXT("room"),F->RoomLoopGuid.ToString());Value->SetNumberField(TEXT("roomFloor"),F->RoomFloorIndex);Value->SetNumberField(TEXT("source"),static_cast<int32>(F->OutlineSource));Value->SetBoolField(TEXT("recorded"),F->IsRecordedOutlineUnchanged());
    TArray<TSharedPtr<FJsonValue>> Regions,Ids;for(const auto& R:F->FloorRegions)Regions.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(R)));for(FGuid Id:F->SurfaceFinishRelationGuids)Ids.Add(MakeShared<FJsonValueString>(Id.ToString()));Value->SetArrayField(TEXT("regions"),Regions);Value->SetArrayField(TEXT("finishRelations"),Ids);
   }
   else if(auto* S=Cast<AEHB_FloorSlab>(E))
   {
    Value->SetStringField(TEXT("room"),S->RoomFillLoopGuid.ToString());Value->SetNumberField(TEXT("roomFloor"),S->RoomFillFloorIndex);Value->SetNumberField(TEXT("source"),static_cast<int32>(S->OutlineSource));Value->SetBoolField(TEXT("recorded"),S->IsRecordedOutlineUnchanged());Value->SetStringField(TEXT("anchor"),S->RoomFillAnchorWallGuid.ToString());Value->SetBoolField(TEXT("hasAnchor"),S->bHasRoomFillAnchor);Value->SetNumberField(TEXT("side"),static_cast<int32>(S->RoomFillAnchorWallSide));
    FEHBFloorFinishRegion R;R.OuterPolygon=S->LocalTopPolygon;Value->SetObjectField(TEXT("polygon"),FJsonObjectConverter::UStructToJsonObject(R));
   }
   else continue;
   Values.Add(MakeShared<FJsonValueObject>(Value));
  }
  Data->SetArrayField(TEXT("outlines"),Values);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return NormalizeCopySnapshotText(Json);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBuildingOutlineCopyTest,"EHB.Topology.BuildingOutlineCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBuildingOutlineCopyTest::RunTest(const FString& Parameters)
{
 for(bool bSurfaceFinish:{false,true})
 {
  FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
  if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,bSurfaceFinish))return false;
  auto* Source=Fixture.Building;const FString Before=BuildingOutlineCopySnapshot(Source);
  auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};const int32 BeforeCount=Count(),GroupCount=Fixture.Actors.Num();
  const auto Result=EHBBuildingCopy::Execute(Source,FVector(3000,500,0));if(!TestTrue(*Result.Status.ToString(),Result.bSucceeded))return false;Fixture.Actors.Append(Result.Actors);auto* Copy=Result.Building;
  TestEqual(TEXT("Complete room outlines copied"),Result.Actors.Num(),GroupCount);TestEqual(TEXT("Source entirely unchanged"),BuildingOutlineCopySnapshot(Source),Before);
  const FString Copied=BuildingOutlineCopySnapshot(Copy);TSet<FGuid> SourceRooms;for(const auto& R:Source->GetClosedLoopsByFloor(1))SourceRooms.Add(R.LoopGuid);
  int32 Floors=0,Slabs=0,Finishes=0;
  for(auto* E:Copy->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E))
   {
    ++Floors;FEHBBuildingClosedLoop Room;TestTrue(TEXT("Copied floor resolves its own room"),F->TryGetRoomLoop(Room));TestFalse(TEXT("Floor does not refer to source room"),SourceRooms.Contains(F->RoomLoopGuid));TestTrue(TEXT("Copied floor provenance current"),F->IsRecordedOutlineUnchanged());
    for(FGuid Id:F->SurfaceFinishRelationGuids)
    {
     ++Finishes;const auto* R=Copy->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;});if(!TestNotNull(TEXT("Mapped floor relation cache resolves"),R))return false;
     TestEqual(TEXT("Mapped finish targets copied floor"),R->Target.ElementGuid,F->ElementGuid);FGuid RoomId;TestTrue(TEXT("Room metadata mapped explicitly"),FGuid::Parse(R->StringMetadata.FindRef(TEXT("RoomLoopGuid")),RoomId)&&RoomId==F->RoomLoopGuid);
     TestNotNull(TEXT("Finish source belongs to copied building"),Copy->FindElementActorByGuid(R->Source.ElementGuid));
    }
   }
   if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;TestTrue(TEXT("Copied slab provenance current"),S->IsRecordedOutlineUnchanged());TestFalse(TEXT("Slab uses copied room identity"),SourceRooms.Contains(S->RoomFillLoopGuid));TestNotNull(TEXT("Slab wall anchor mapped"),Copy->FindElementActorByGuid(S->RoomFillAnchorWallGuid));}
  }
  TestEqual(TEXT("Two room floors"),Floors,2);TestEqual(TEXT("Two room ceilings"),Slabs,2);TestTrue(TEXT("Surface metadata path exercised when requested"),bSurfaceFinish?Finishes>0:Finishes==0);
  TestTrue(TEXT("Undo outline building creation"),GEditor->UndoTransaction());TestEqual(TEXT("Undo removes entire copied group"),Count(),BeforeCount);
  TestTrue(TEXT("Redo outline building creation"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores outlines, provenance, metadata, graph and mesh"),BuildingOutlineCopySnapshot(Copy),Copied);
  // A valid copied room cycle may use a different start than its unchanged slab outline.
  for(auto& Room:Copy->ClosedLoops)if(Room.PillarGuids.Num()>1&&Room.PillarGuids.Num()==Room.WallGuids.Num())
  {const auto P=Room.PillarGuids,W=Room.WallGuids;for(int32 I=0;I<P.Num();++I){Room.PillarGuids[I]=P[(I+1)%P.Num()];Room.WallGuids[I]=W[(I+1)%W.Num()];}}
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy,true,false);const auto Node=UEHBWallTopologyLibrary::CaptureWallTopology(Copy).Nodes[0];
  const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Copy,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0),false);
  {TestTrue(*Move.Message,Move.bSucceeded);if(Move.bSucceeded){TestTrue(TEXT("Undo copied floor-follow move"),GEditor->UndoTransaction(false));TestEqual(TEXT("Follow undo restores full copied state"),BuildingOutlineCopySnapshot(Copy),Copied);}}
  TestEqual(TEXT("Copy editing leaves source unchanged"),BuildingOutlineCopySnapshot(Source),Before);GEditor->SelectNone(false,true,false);
  for(bool bAfterApply:{false,true})
  {
   if(bAfterApply)EHBBuildingCopy::FailAfterApply=true;else EHBBuildingCopy::FailAfterImport=true;
   const auto Failed=EHBBuildingCopy::Execute(Source,FVector(6000,0,0));TestEqual(TEXT("Outline copy failure rolls back"),Failed.Status,FName("BuildingCopyFailedRolledBack"));TestEqual(TEXT("No failed copy actors left"),Count(),BeforeCount+GroupCount);TestEqual(TEXT("Failed copy preserves original"),BuildingOutlineCopySnapshot(Source),Before);TestEqual(TEXT("Failed copy preserves existing copy"),BuildingOutlineCopySnapshot(Copy),Copied);TestFalse(TEXT("No redoable failed outline copy"),GEditor->RedoTransaction());
  }
  auto* Floor=Cast<AEHB_Floor>(*Source->QueryElements(FEHBElementQuery()).FindByPredicate([](auto* E){return E->IsA<AEHB_Floor>();}));
  const FGuid SavedRoom=Floor->RoomLoopGuid;Floor->RoomLoopGuid=FGuid::NewGuid();TestEqual(TEXT("Unknown floor room refused before import"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("StaleFloorRoomBinding"));Floor->RoomLoopGuid=SavedRoom;
  Floor->FloorRegions[0].OuterPolygon[0].X+=5;TestEqual(TEXT("Hand-edited generated outline refused"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("UnsupportedFloorCopyOutline"));Floor->FloorRegions[0].OuterPolygon[0].X-=5;
  if(bSurfaceFinish)
  {
   auto* Relation=Source->ElementRelations.FindByPredicate([](const auto& R){return R.Type==EEHBElementRelationType::SurfaceFinish;});const auto Original=*Relation;
   Relation->StringMetadata.Add(TEXT("UnknownIdentity"),FGuid::NewGuid().ToString());TestEqual(TEXT("Opaque extra metadata refused"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("UnsupportedFinishCopyRelation"));*Relation=Original;
   Relation->StringMetadata[TEXT("RoomLoopGuid")]=FGuid::NewGuid().ToString();TestEqual(TEXT("Stale finish room refused"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("UnsupportedFinishCopyRelation"));*Relation=Original;
  }

  auto* Slab=Cast<AEHB_FloorSlab>(*Source->QueryElements(FEHBElementQuery()).FindByPredicate([](auto* E){return Cast<AEHB_FloorSlab>(E)!=nullptr;}));
  const FGuid Anchor=Slab->RoomFillAnchorWallGuid;Slab->RoomFillAnchorWallGuid=FGuid::NewGuid();Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);TestEqual(TEXT("Unknown slab anchor refused even with fresh signature"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("StaleSlabWallAnchor"));Slab->RoomFillAnchorWallGuid=Anchor;Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
  if(bSurfaceFinish)
  {
   const auto RelationIds=Floor->SurfaceFinishRelationGuids;Floor->SurfaceFinishRelationGuids.Reset();TestEqual(TEXT("Floor relation cache must agree with graph"),EHBBuildingCopy::Execute(Source,FVector(6000,0,0)).Status,FName("UnsupportedFinishCopyRelation"));Floor->SurfaceFinishRelationGuids=RelationIds;
  }
  TestEqual(TEXT("Preflight leaves world actor count unchanged"),Count(),BeforeCount+GroupCount);
 }
 return true;
}


namespace
{
 // Decimal strings deliberately round-trip every double. ToString's three decimals
 // are retained in the legacy model evidence, but never used for this mesh payload.
 FString MeshEvidenceScalar(double V){return FString::Printf(TEXT("%.17g"),V==0.0?0.0:V);}
 TArray<TSharedPtr<FJsonValue>> MeshEvidenceNumbers(std::initializer_list<double> Values)
 {
  TArray<TSharedPtr<FJsonValue>> Result;for(double V:Values)Result.Add(MakeShared<FJsonValueString>(MeshEvidenceScalar(V)));return Result;
 }
 TArray<TSharedPtr<FJsonValue>> MeshEvidencePose(const FTransform& T)
 {
  const auto P=T.GetLocation(),S=T.GetScale3D();auto Q=T.GetRotation();if(Q.W<0)Q=Q*-1;
  return MeshEvidenceNumbers({P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z});
 }
 FString OutlineCopyMeshEvidence(UWorld* World)
 {
  auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.GeneratedMeshEvidence.v1"));
  Data->SetNumberField(TEXT("junctionFootprintVersion"),FEHBWallJunctionGeometry::OutputVersion);
  Data->SetNumberField(TEXT("junctionPrismVersion"),FEHBWallJunctionMeshBuilder::OutputVersion);
  Data->SetStringField(TEXT("map"),World->GetOutermost()->GetName());
  TArray<AEHB_Building*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(*It);
  Buildings.Sort([](const auto& A,const auto& B){return A.BuildingGuid<B.BuildingGuid;});
  TArray<TSharedPtr<FJsonValue>> BuildingValues;
  for(auto* B:Buildings)
  {
   auto BV=MakeShared<FJsonObject>();BV->SetStringField(TEXT("id"),B->BuildingGuid.ToString());BV->SetStringField(TEXT("model"),BuildingOutlineCopySnapshot(B));
   BV->SetArrayField(TEXT("pose"),MeshEvidencePose(B->GetActorTransform()));BV->SetObjectField(TEXT("receipt"),FJsonObjectConverter::UStructToJsonObject(B->LastCommittedEdit));
   auto Elements=B->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& C){return A.ElementGuid<C.ElementGuid;});
   TArray<TSharedPtr<FJsonValue>> ElementValues;
   for(auto* E:Elements)
   {
    auto EV=MakeShared<FJsonObject>();EV->SetStringField(TEXT("id"),E->ElementGuid.ToString());EV->SetStringField(TEXT("class"),E->GetClass()->GetPathName());EV->SetArrayField(TEXT("localPose"),MeshEvidencePose(E->GetElementLocalTransform()));
    TInlineComponentArray<UEHBGeneratedMeshComponent*> Meshes(E);Meshes.Sort([](const auto& A,const auto& C){return A.GetName()<C.GetName();});TArray<TSharedPtr<FJsonValue>> MeshValues;
    for(auto* M:Meshes)
    {
     auto MV=MakeShared<FJsonObject>();MV->SetStringField(TEXT("name"),M->GetName());MV->SetArrayField(TEXT("relativePose"),MeshEvidencePose(M->GetRelativeTransform()));
     MV->SetBoolField(TEXT("visible"),M->IsVisible());MV->SetNumberField(TEXT("collisionEnabled"),static_cast<int32>(M->GetCollisionEnabled()));MV->SetStringField(TEXT("collisionProfile"),M->GetCollisionProfileName().ToString());
     TArray<TSharedPtr<FJsonValue>> Materials;for(int32 I=0;I<M->GetNumMaterials();++I)Materials.Add(MakeShared<FJsonValueString>(GetPathNameSafe(M->GetMaterial(I))));MV->SetArrayField(TEXT("materials"),Materials);
     TArray<TSharedPtr<FJsonValue>> Sections;
     for(int32 I=0;I<M->GetNumSections();++I)
     {
      const auto* Section=M->GetProcMeshSection(I);if(!Section){Sections.Add(MakeShared<FJsonValueNull>());continue;}
      auto SV=MakeShared<FJsonObject>();SV->SetStringField(TEXT("name"),Section->SectionName.ToString());SV->SetBoolField(TEXT("collision"),Section->bEnableCollision);SV->SetBoolField(TEXT("visible"),Section->bSectionVisible);
      SV->SetArrayField(TEXT("bounds"),MeshEvidenceNumbers({double(Section->SectionLocalBox.IsValid),Section->SectionLocalBox.Min.X,Section->SectionLocalBox.Min.Y,Section->SectionLocalBox.Min.Z,Section->SectionLocalBox.Max.X,Section->SectionLocalBox.Max.Y,Section->SectionLocalBox.Max.Z}));
      TArray<TSharedPtr<FJsonValue>> Vertices,Indices;
      for(const auto& V:Section->ProcVertexBuffer)
      {
       const auto P=V.Position,N=V.Normal,T=V.Tangent.TangentX;
       Vertices.Add(MakeShared<FJsonValueArray>(MeshEvidenceNumbers({P.X,P.Y,P.Z,N.X,N.Y,N.Z,V.UV0.X,V.UV0.Y,V.UV1.X,V.UV1.Y,V.UV2.X,V.UV2.Y,V.UV3.X,V.UV3.Y,T.X,T.Y,T.Z,double(V.Tangent.bFlipTangentY),double(V.Color.R),double(V.Color.G),double(V.Color.B),double(V.Color.A)})));
      }
      for(uint32 Index:Section->ProcIndexBuffer)Indices.Add(MakeShared<FJsonValueNumber>(Index));SV->SetArrayField(TEXT("vertices"),Vertices);SV->SetArrayField(TEXT("indices"),Indices);Sections.Add(MakeShared<FJsonValueObject>(SV));
     }
     MV->SetArrayField(TEXT("sections"),Sections);MeshValues.Add(MakeShared<FJsonValueObject>(MV));
    }
    EV->SetArrayField(TEXT("meshes"),MeshValues);ElementValues.Add(MakeShared<FJsonValueObject>(EV));
   }
   BV->SetArrayField(TEXT("elements"),ElementValues);BuildingValues.Add(MakeShared<FJsonValueObject>(BV));
  }
  Data->SetArrayField(TEXT("buildings"),BuildingValues);FString Text;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Text));return Text;
 }
 bool CurrentMeshEvidencePaths(FString& Path,UWorld*& World)
 {
  FString Map,LegacyPath;if(!PersistencePaths(Map,LegacyPath,TEXT("OutlineCopy"))||!FParse::Value(FCommandLine::Get(),TEXT("EHBMeshEvidence="),Path))return false;
  Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeFilename(Path);
  const FString Allowed=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("EHB-Refactor/Persistence"));
  if(!FPaths::IsUnderDirectory(Path,Allowed)||!Path.EndsWith(TEXT("/CurrentMeshEvidence.json")))return false;
  World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;return World&&World->GetOutermost()->GetName()==Map;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBMeshEvidencePrecisionTest,"EHB.Topology.MeshEvidencePrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBMeshEvidencePrecisionTest::RunTest(const FString& Parameters)
{
 const double X=123.12345678901234;TestEqual(TEXT("Evidence scalar round trips double"),FCString::Atod(*MeshEvidenceScalar(X)),X);
 TestNotEqual(TEXT("Submillimeter changes survive evidence"),MeshEvidenceScalar(X),MeshEvidenceScalar(X+1e-10));
 TestEqual(TEXT("Signed zero canonicalized"),MeshEvidenceScalar(-0.0),MeshEvidenceScalar(0.0));
 TestNotEqual(TEXT("Index and vertex ordering remain strict"),MeshEvidencePose(FTransform(FVector(0,0,1)))[2]->AsString(),MeshEvidencePose(FTransform(FVector(0,0,2)))[2]->AsString());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCaptureOutlineMeshesTest,"EHBValidation.MeshEvidence.CaptureOutlineCopyMeshes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCaptureOutlineMeshesTest::RunTest(const FString& Parameters)
{
 FString Path;UWorld* World=nullptr;if(!TestTrue(TEXT("Existing isolated map and evidence path required"),CurrentMeshEvidencePaths(Path,World)))return false;
 if(FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite mesh evidence"));return false;}
 int32 Count=0;for(TActorIterator<AEHB_Building> It(World);It;++It)++Count;if(!TestEqual(TEXT("Four existing buildings required"),Count,4))return false;
 return TestTrue(TEXT("Capture current full-precision evidence without saving map"),FFileHelper::SaveStringToFile(OutlineCopyMeshEvidence(World),*Path));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBReadOutlineMeshesTest,"EHBValidation.MeshEvidence.ReadOutlineCopyMeshes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBReadOutlineMeshesTest::RunTest(const FString& Parameters)
{
 FString Path,Expected;UWorld* World=nullptr;if(!CurrentMeshEvidencePaths(Path,World)||!FFileHelper::LoadFileToString(Expected,*Path))return false;
 TestEqual(TEXT("Separate process regenerates exact full-precision versioned meshes"),OutlineCopyMeshEvidence(World),Expected);
 for(TActorIterator<AEHB_Building> It(World);It;++It)
 {
  It->RebuildElementAndRelationshipIndexes();
  for(auto* E:It->QueryElements(FEHBElementQuery())){if(auto* P=Cast<AEHB_Pillar>(E))P->RebuildPillarMesh();else if(auto* W=Cast<AEHB_Wall>(E))W->RebuildWallMesh();else if(auto* F=Cast<AEHB_Floor>(E))F->RebuildFloorMesh();else if(auto* S=Cast<AEHB_FloorSlab>(E))S->RebuildSlabMesh();}
 }
 TestEqual(TEXT("Explicit reconstruction retains full-precision vertices UVs colors indices materials and flags"),OutlineCopyMeshEvidence(World),Expected);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOutlineCopyPersistenceWriteTest,"EHBValidation.Persistence.WriteOutlineCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOutlineCopyPersistenceWriteTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("OutlineCopy")))return false;
 if(FPackageName::DoesPackageExist(MapPath)){AddError(TEXT("Refusing to overwrite copy fixture"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Pairs;
 FTransientTopologyFixture Cleanup;
 for(bool bSurfaceFinish:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;Fixture.Building->SetActorRotation(FRotator(0,37,0));
  if(!AddCopyRoomOutlines(Fixture,bSurfaceFinish))return false;const auto Copy=EHBBuildingCopy::Execute(Fixture.Building,FVector(bSurfaceFinish?6000:3000,500,0));
  if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;Fixture.Actors.Append(Copy.Actors);
  const auto OriginalSource=BuildingOutlineCopySnapshot(Fixture.Building);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy.Building,true,false);
  const auto Node=UEHBWallTopologyLibrary::CaptureWallTopology(Copy.Building).Nodes[0];
  const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Copy.Building,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0),false);
  if(!TestTrue(*Move.Message,Move.bSucceeded))return false;
  TestEqual(TEXT("Saving moved copy preserves source"),BuildingOutlineCopySnapshot(Fixture.Building),OriginalSource);
  GEditor->SelectNone(false,true,false);
  auto Pair=MakeShared<FJsonObject>();Pair->SetBoolField(TEXT("surfaceFinish"),bSurfaceFinish);Pair->SetStringField(TEXT("sourceGuid"),Fixture.Building->BuildingGuid.ToString());Pair->SetStringField(TEXT("copyGuid"),Copy.Building->BuildingGuid.ToString());Pair->SetStringField(TEXT("source"),BuildingOutlineCopySnapshot(Fixture.Building));Pair->SetStringField(TEXT("copy"),BuildingOutlineCopySnapshot(Copy.Building));FString SourceEdit,CopyEdit;FJsonObjectConverter::UStructToJsonObjectString(Fixture.Building->LastCommittedEdit,SourceEdit);FJsonObjectConverter::UStructToJsonObjectString(Copy.Building->LastCommittedEdit,CopyEdit);Pair->SetStringField(TEXT("sourceEdit"),SourceEdit);Pair->SetStringField(TEXT("copyEdit"),CopyEdit);TestTrue(TEXT("Saved moved copy has a committed edit receipt"),Copy.Building->LastCommittedEdit.StateId.IsValid()&&Copy.Building->LastCommittedEdit.Sequence==1);Pairs.Add(MakeShared<FJsonValueObject>(Pair));
  Cleanup.Actors.Append(Fixture.Actors);Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save four independent buildings with mapped room outlines"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
 auto Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("schema"),TEXT("EHB.OutlineCopyPersistence.v3"));Evidence->SetArrayField(TEXT("pairs"),Pairs);FString Json;FJsonSerializer::Serialize(Evidence,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Write outline copy evidence"),FFileHelper::SaveStringToFile(Json,*EvidencePath));Cleanup.Actors.Reset();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOutlineCopyPersistenceReadTest,"EHBValidation.Persistence.ReadOutlineCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOutlineCopyPersistenceReadTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("OutlineCopy")))return false;auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=MapPath)return false;
 FString Json;TSharedPtr<FJsonObject> Evidence;if(!FFileHelper::LoadFileToString(Json,*EvidencePath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Evidence)||!Evidence.IsValid()||(Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.OutlineCopyPersistence.v1")&&Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.OutlineCopyPersistence.v2")&&Evidence->GetStringField(TEXT("schema"))!=TEXT("EHB.OutlineCopyPersistence.v3")))return false;
 TMap<FString,AEHBBuildingActorBase*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(It->BuildingGuid.ToString(),*It);TestEqual(TEXT("Four buildings reload independently"),Buildings.Num(),4);
 for(const auto& Value:Evidence->GetArrayField(TEXT("pairs")))
 {
  const auto Pair=Value->AsObject();auto* Source=Buildings.FindRef(Pair->GetStringField(TEXT("sourceGuid")));auto* Copy=Buildings.FindRef(Pair->GetStringField(TEXT("copyGuid")));if(!Source||!Copy)return false;
  auto ReceiptJson=[](const FEHBCommittedEdit& Edit){FString Text;FJsonObjectConverter::UStructToJsonObjectString(Edit,Text);return Text;};
  const auto InitialEdit=Copy->LastCommittedEdit;const auto SourceEdit=ReceiptJson(Source->LastCommittedEdit);
  if(Evidence->GetStringField(TEXT("schema"))==TEXT("EHB.OutlineCopyPersistence.v3"))
  {
   TestEqual(TEXT("Saved source edit state reloads"),SourceEdit,Pair->GetStringField(TEXT("sourceEdit")));
   TestEqual(TEXT("Saved command identity sequence and affected regions reload exactly"),ReceiptJson(InitialEdit),Pair->GetStringField(TEXT("copyEdit")));
   TestTrue(TEXT("Persisted receipt belongs to copied building"),InitialEdit.BuildingGuid==Copy->BuildingGuid&&InitialEdit.StateId.IsValid()&&InitialEdit.Sequence==1);
  }
  const FString ExpectedSource=NormalizeCopySnapshotText(Pair->GetStringField(TEXT("source"))),ExpectedCopy=NormalizeCopySnapshotText(Pair->GetStringField(TEXT("copy")));
  TestEqual(TEXT("Source outlines metadata and geometry reload exactly"),BuildingOutlineCopySnapshot(Source),ExpectedSource);TestEqual(TEXT("Copied outlines metadata and geometry reload exactly"),BuildingOutlineCopySnapshot(Copy),ExpectedCopy);
  for(auto* E:Copy->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E))
  {
   TestEqual(TEXT("Loaded contact cache starts empty"),F->GetContactCacheStats().Entries,0);
   FEHBBuildingClosedLoop ContactRoom;TArray<FEHBElementRelation> ContactPlan;FName ContactStatus;
   if(!F->TryGetRoomLoop(ContactRoom))return false;
   TestTrue(TEXT("Loaded contact cache reconstructs from actual surfaces"),F->BuildSurfaceFinishRelationPlan(ContactRoom,F->FloorRegions,ContactPlan,ContactStatus));
   const auto Cold=F->GetContactCacheStats();TestTrue(TEXT("Loaded contact solve populated cache"),Cold.Solves>0&&Cold.Entries>0);
   TestTrue(TEXT("Loaded repeated contact plan"),F->BuildSurfaceFinishRelationPlan(ContactRoom,F->FloorRegions,ContactPlan,ContactStatus));
   TestEqual(TEXT("Loaded repeated contact plan avoids solves"),F->GetContactCacheStats().Solves,Cold.Solves);
  }
  TestEqual(TEXT("Loaded dependency index starts cold"),Copy->GetRoomDependencyCacheStats().FullBuilds,0);
  TArray<FEHBRoomDependencyMembers> LoadedDependencies;TestTrue(TEXT("Loaded room dependencies rebuild from actual binding fields"),Copy->QueryRoomDependencies(Copy->GetClosedLoopsByFloor(1),LoadedDependencies));TestEqual(TEXT("Both loaded rooms indexed"),LoadedDependencies.Num(),2);for(const auto& M:LoadedDependencies){TestEqual(TEXT("Loaded floor binding indexed"),M.Floors.Num(),1);TestEqual(TEXT("Loaded room slab indexed"),M.Slabs.Num(),1);}
  FEHBPreparedWallNodeDefinitions Model;TestTrue(TEXT("Loaded typed model valid"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Copy,Model).bSucceeded);const auto Policy=FEHBCopyOutlinePolicy::Prepare(Copy,Model,Copy->QueryElements(FEHBElementQuery()));TestTrue(*Policy.Status.ToString(),Policy.bSucceeded);
  Copy->RebuildElementAndRelationshipIndexes();for(auto* E:Copy->QueryElements(FEHBElementQuery())){if(auto* F=Cast<AEHB_Floor>(E))F->RebuildFloorMesh();if(auto* S=Cast<AEHB_FloorSlab>(E))S->RebuildSlabMesh();}
  TestEqual(TEXT("Rebuild preserves saved outline authority and generated meshes"),BuildingOutlineCopySnapshot(Copy),ExpectedCopy);
  TestEqual(TEXT("Geometry rebuilding preserves stored receipt"),ReceiptJson(Copy->LastCommittedEdit),ReceiptJson(InitialEdit));
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy,true,false);const auto Node=UEHBWallTopologyLibrary::CaptureWallTopology(Copy).Nodes[0];
  const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(Copy,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0),false);
  {TestTrue(*Move.Message,Move.bSucceeded);if(Move.bSucceeded){TestTrue(TEXT("Undo loaded room-follow edit"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo recovers saved copied outlines"),BuildingOutlineCopySnapshot(Copy),ExpectedCopy);}}
  TestEqual(TEXT("Undo restores saved command identity and regions"),ReceiptJson(Copy->LastCommittedEdit),ReceiptJson(InitialEdit));TestEqual(TEXT("Source command receipt untouched"),ReceiptJson(Source->LastCommittedEdit),SourceEdit);
  TestEqual(TEXT("Loaded copy edit never changes source"),BuildingOutlineCopySnapshot(Source),ExpectedSource);GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFloorRelationPlanTest,"EHB.Topology.FloorRelationPlanning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFloorRelationPlanTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;AEHB_Floor* Floor=nullptr;
 for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){Floor=F;break;}
 if(!Floor || Floor->SurfaceFinishRelationGuids.IsEmpty())return false;
 FEHBBuildingClosedLoop Room;if(!Floor->TryGetRoomLoop(Room))return false;
 FName InitialStatus;const auto InitialIds=Floor->SurfaceFinishRelationGuids;
 if(!Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,InitialStatus))return false;
 for(FGuid Id:InitialIds)TestTrue(TEXT("Newly added neighboring slab does not replace existing host IDs"),Floor->SurfaceFinishRelationGuids.Contains(Id));
 auto GraphText=[&](){auto O=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Values;for(const auto& R:B->ElementRelations){auto V=FJsonObjectConverter::UStructToJsonObject(R);V->RemoveField(TEXT("sourceGeometryRevision"));V->RemoveField(TEXT("targetGeometryRevision"));Values.Add(MakeShared<FJsonValueObject>(V));}O->SetArrayField(TEXT("relations"),Values);FString Text;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Text));return Text;};
 const auto OriginalGraph=GraphText();const auto OriginalIds=Floor->SurfaceFinishRelationGuids;
 TArray<FEHBElementRelation> Plan;FName Status;
 TestTrue(TEXT("Read-only plan succeeds"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Plan,Status));
 TestEqual(TEXT("Plan leaves source graph exact"),GraphText(),OriginalGraph);
 TestEqual(TEXT("Plan covers original hosts"),Plan.Num(),OriginalIds.Num());
 for(const auto& R:Plan)TestTrue(TEXT("Existing relation ID reused"),OriginalIds.Contains(R.RelationGuid));
 const auto First=Plan[0];auto* Host=B->FindElementActorByGuid(First.Source.ElementGuid);if(!Host)return false;
 const auto SourcePose=Host->GetActorTransform();const auto SourceBounds=Host->GetBuildingLocalBounds();
 TArray<FEHBFloorSupportSurface> Tops;if(!FEHBFloorContactGeometry::CaptureHorizontalTops(Host,Tops,Status))return false;
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> Candidates;Candidates.Add(Host->ElementGuid,Tops);
 for(auto& Top:Candidates[Host->ElementGuid])for(auto& P:Top.OuterPolygon)P+=FVector(2,0,0);
 TestTrue(TEXT("Candidate contact planned before geometry moves"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Plan,Status,&Candidates));
 const auto* Updated=Plan.FindByPredicate([&](const auto& R){return R.RelationGuid==First.RelationGuid;});
 TestTrue(TEXT("Candidate contact uses proposed host bounds"),Updated && Updated->ContactArea>0 && SourceBounds.ExpandBy(2).IsInsideOrOn(Updated->ContactPoint));
 TestTrue(TEXT("Preview never moves host"),Host->GetActorTransform().Equals(SourcePose));TestEqual(TEXT("Preview never publishes graph"),GraphText(),OriginalGraph);
 Candidates[Host->ElementGuid]=Tops;for(auto& Top:Candidates[Host->ElementGuid])for(auto& P:Top.OuterPolygon)P+=FVector(0,0,50);
 TestTrue(TEXT("Candidate loss of contact planned"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Plan,Status,&Candidates));
 TestFalse(TEXT("Separated host omitted"),Plan.ContainsByPredicate([&](const auto& R){return R.RelationGuid==First.RelationGuid;}));
 for(const auto& R:Plan)TestTrue(TEXT("Other hosts keep identity"),OriginalIds.Contains(R.RelationGuid));
 Candidates.Add(FGuid::NewGuid(),Tops);TestFalse(TEXT("Unknown candidate host rejected"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Plan,Status,&Candidates));TestTrue(TEXT("Failed plan has no partial output"),Plan.IsEmpty());
 TestTrue(TEXT("Stable refresh succeeds"),Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,Status));
 TestEqual(TEXT("Unchanged refresh preserves entire graph"),GraphText(),OriginalGraph);TestTrue(TEXT("Unchanged refresh preserves IDs"),Floor->SurfaceFinishRelationGuids==OriginalIds);
 // A poisoned cache must not delete or overwrite a wall topology relation.
 const auto* Foreign=B->ElementRelations.FindByPredicate([](const auto& R){return R.Type==EEHBElementRelationType::TopologyConnection;});if(!Foreign)return false;const FGuid ForeignId=Foreign->RelationGuid;
 Floor->SurfaceFinishRelationGuids.Add(ForeignId);
 TestFalse(TEXT("Foreign cache rejects before publication"),Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,Status));TestEqual(TEXT("Foreign relationship untouched"),GraphText(),OriginalGraph);Floor->SurfaceFinishRelationGuids=OriginalIds;
 Floor->SurfaceFinishRelationGuids.Add(FGuid::NewGuid());TestFalse(TEXT("Missing cached relation refuses"),Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,Status));TestEqual(TEXT("Missing cache rejection read-only"),GraphText(),OriginalGraph);Floor->SurfaceFinishRelationGuids=OriginalIds;
 // Actual host movement plus stable relation publication participates in one editor transaction.
 {
  FScopedTransaction Tx(NSLOCTEXT("EHBTests","FloorContactMove","Move finish host and refresh"));B->Modify();Host->Modify();Floor->Modify();
  FTransform Local=Host->GetElementLocalTransform();Local.AddToTranslation(FVector(2,0,0));Host->SetActorRelativeTransform(Local);
  TestTrue(TEXT("Moved host refresh succeeds"),Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,Status));
 }
 const auto* Actual=B->ElementRelations.FindByPredicate([&](const auto& R){return R.RelationGuid==First.RelationGuid;});
 TestTrue(TEXT("Actual contact updates while identity survives"),Actual && Actual->ContactArea>0 && Host->GetBuildingLocalBounds().ExpandBy(0.001).IsInsideOrOn(Actual->ContactPoint));
 const auto ChangedGraph=GraphText();TestTrue(TEXT("Undo host and relations"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores graph exactly"),GraphText(),OriginalGraph);TestTrue(TEXT("Undo restores host transform"),Host->GetActorTransform().Equals(SourcePose,0.001));
 TestTrue(TEXT("Redo host and relations"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores changed contact graph"),GraphText(),ChangedGraph);TestTrue(TEXT("Cleanup undo"),GEditor->UndoTransaction(false));
 {
  FScopedTransaction Tx(NSLOCTEXT("EHBTests","ClearFinishPoison","Clear finish with foreign cache"));Floor->Modify();B->Modify();
  Floor->SurfaceFinishRelationGuids.Add(ForeignId);Floor->ClearSurfaceFinishRelations();
  TestTrue(TEXT("Clear removes own cache"),Floor->SurfaceFinishRelationGuids.IsEmpty());
  TestTrue(TEXT("Clear never removes foreign topology"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==ForeignId;}));
 }
 TestTrue(TEXT("Undo clearing restores graph and cache"),GEditor->UndoTransaction(false));TestEqual(TEXT("Clear undo full graph"),GraphText(),OriginalGraph);TestTrue(TEXT("Clear undo IDs"),Floor->SurfaceFinishRelationGuids==OriginalIds);

 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFloorHostTopologyDraftTest,"EHB.Topology.FloorHostTopologyDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFloorHostTopologyDraftTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;AEHB_Floor* Floor=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){Floor=F;break;}if(!Floor)return false;
 FEHBBuildingClosedLoop Room;if(!Floor->TryGetRoomLoop(Room))return false;FName Status;TArray<FEHBElementRelation> Before;
 if(!Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Before,Status)||Before.IsEmpty())return false;
 const auto* Old=Before.FindByPredicate([&](const auto& R){return Cast<AEHB_Wall>(B->FindElementActorByGuid(R.Source.ElementGuid))!=nullptr;});if(!Old)return false;
 FEHBFloorHostTopologyDraft Draft;const FGuid NewId=FGuid::NewGuid();Draft.RemovedHostGuids.Add(Old->Source.ElementGuid);
 if(!FEHBFloorContactGeometry::CaptureHorizontalTops(B->FindElementActorByGuid(Old->Source.ElementGuid),Draft.NewHostSurfaces.Add(NewId),Status))return false;
 const auto Original=BuildingOutlineCopySnapshot(B);const auto Cached=Floor->SurfaceFinishRelationGuids;const auto CacheStats=Floor->GetContactCacheStats();TArray<FEHBElementRelation> Planned;
 TestTrue(TEXT("Plan replacement physical host without Actor"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Planned,Status,nullptr,&Draft));
 TestEqual(TEXT("Same coverage retains contact count"),Planned.Num(),Before.Num());const auto* New=Planned.FindByPredicate([&](const auto& R){return R.Source.ElementGuid==NewId;});
 TestTrue(TEXT("Replacement retains area but owns new relationship identity"),New&&New->ContactArea==Old->ContactArea&&New->RelationGuid!=Old->RelationGuid);
 TestNull(TEXT("Planning never spawns virtual host"),B->FindElementActorByGuid(NewId));TestEqual(TEXT("Preview host never enters persistent contact cache"),Floor->GetContactCacheStats().Entries,CacheStats.Entries);
 for(const auto& R:Planned)if(R.Source.ElementGuid!=NewId){const auto* Previous=Before.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source);});TestTrue(TEXT("Retained physical host keeps relation"),Previous&&Previous->RelationGuid==R.RelationGuid);}
 auto Check=[&](const FEHBFloorHostTopologyDraft& Invalid){Planned=Before;TestFalse(TEXT("Malformed host delta rejected"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Planned,Status,nullptr,&Invalid));TestTrue(TEXT("No partial relation plan escapes"),Planned.IsEmpty());};
 auto Bad=Draft;Bad.NewHostSurfaces.Add(Old->Source.ElementGuid,Bad.NewHostSurfaces[NewId]);Check(Bad);
 Bad=Draft;Bad.RemovedHostGuids.Add(FGuid::NewGuid());Check(Bad);
 Bad=Draft;Bad.NewHostSurfaces[NewId].Reset();Check(Bad);
 Bad=Draft;Bad.NewHostSurfaces[NewId][0].OuterPolygon[0].X=std::numeric_limits<double>::quiet_NaN();Check(Bad);
 TestEqual(TEXT("Host draft leaves complete scene unchanged"),BuildingOutlineCopySnapshot(B),Original);TestTrue(TEXT("Host draft preserves floor relation cache"),Floor->SurfaceFinishRelationGuids==Cached);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomSubdivisionTest,"EHB.Topology.RoomSubdivisionDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomSubdivisionTest::RunTest(const FString& Parameters)
{
 for(bool Railing:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
  auto* Wall=Fixture.Walls[0];const FGuid OldId=Wall->ElementGuid,StartId=Wall->StartPillarGuid,EndId=Wall->EndPillarGuid;
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E))if(B->GetClosedLoopsByWallGuid(OldId).ContainsByPredicate([&](const auto& R){return R.LoopGuid==S->RoomFillLoopGuid;})){S->RoomFillAnchorWallGuid=OldId;S->RoomFillAnchorWallSide=EEHBFloorSlabWallSide::Left;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(Railing)
  {
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
   Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
   Window->SetActorLocationAndRotation(Wall->GetWorldLocationOnCenterAxisAtDistance(100,50),Wall->GetActorQuat());Window->BindToWall(Wall,100);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
   auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("SubdivisionPreservedRail"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
   if(!TestNotNull(TEXT("Existing railing with room finishes"),Rail))return false;Fixture.Actors.Add(Rail);
  }
  const auto Original=BuildingOutlineCopySnapshot(B);const auto Rooms=B->GetClosedLoopsByFloor(1);TArray<FGuid> RoomIds;for(const auto& R:Rooms)RoomIds.Add(R.LoopGuid);const auto OldRelations=B->ElementRelations;
  auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,OldId,250);if(!TestTrue(*Split.Status.ToString(),Split.bSucceeded))return false;
  FEHBRoomSubdivision Preview;FName Status;if(!TestTrue(*Status.ToString(),Preview.Prepare(B,Wall,Split,B->QueryElements(FEHBElementQuery()),Status))){AddError(Status.ToString());return false;}
  TestTrue(TEXT("Complete floor and slab plans prepared"),Preview.Floors.Num()==2&&Preview.Slabs.Num()==2);TestEqual(TEXT("Subdivision preparation leaves full model unchanged"),BuildingOutlineCopySnapshot(B),Original);
  auto Commit=[&](){return Railing?UEHBBuildingToolset::CommitWallSplitAndRailing(B,OldId,250,B->RelationshipGraphRevision,Wall->LocalStart,Wall->LocalEnd,Wall->Height,Wall->Thickness,B->GetActorTransform().TransformPosition(Split.LocalPillarPosition+FVector(0,-300,0)),100,5,80,false):UEHBBuildingToolset::CommitPlainWallSplit(B,OldId,250,B->RelationshipGraphRevision,Wall->LocalStart,Wall->LocalEnd,Wall->Height,Wall->Thickness,true);};
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
  {EHBWallSplitTestHooks::FailurePhase=Phase;const auto Failed=Commit();TestEqual(TEXT("Dependent split failure rolls back"),Failed.Message,FString(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Rollback restores floor/slab/host IDs and complete geometry"),BuildingOutlineCopySnapshot(B),Original);}
  const auto Applied=Commit();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  TestNull(TEXT("Old source removed"),B->FindElementActorByGuid(OldId));for(const auto& R:B->GetClosedLoopsByFloor(1))TestTrue(TEXT("Room identity retained"),RoomIds.Contains(R.LoopGuid));
  AEHB_Wall* Left=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))if(W->StartPillarGuid==StartId&&W->EndPillarGuid!=EndId)Left=W;
  TestNotNull(TEXT("Mapped source-start segment exists"),Left);
  for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E))
   {
    FEHBBuildingClosedLoop Room;TArray<FEHBElementRelation> Plan;TestTrue(TEXT("Room floor binding still queryable"),F->TryGetRoomLoop(Room));TestTrue(TEXT("Final contact cache matches live hosts"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));
    TestEqual(TEXT("Complete contact count"),Plan.Num(),F->SurfaceFinishRelationGuids.Num());for(const auto& R:Plan){TestTrue(TEXT("Contact host is a live physical element"),B->FindElementActorByGuid(R.Source.ElementGuid)!=nullptr&&R.Source.ElementGuid!=OldId);const auto* Old=OldRelations.FindByPredicate([&](const auto& V){return V.IsEquivalentTo(R);});if(Old)TestEqual(TEXT("Unchanged host identity retained"),R.RelationGuid,Old->RelationGuid);}
   }
   if(auto* S=Cast<AEHB_FloorSlab>(E))TestTrue(TEXT("No slab retains removed wall anchor"),S->RoomFillAnchorWallGuid!=OldId&&B->FindElementActorByGuid(S->RoomFillAnchorWallGuid)!=nullptr);
  }
  FEHBPreparedWallNodeDefinitions Model;TSet<FGuid> OpeningWalls;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))if(!W->DoorWindowConnections.IsEmpty())OpeningWalls.Add(W->ElementGuid);UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Model,&OpeningWalls);TestTrue(TEXT("Subdivided outlines remain valid with separate door/railing policy"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery()),true).bSucceeded);
  const auto Final=BuildingOutlineCopySnapshot(B);const auto Receipt=B->LastCommittedEdit;
  TestTrue(TEXT("Single undo restores dependencies"),GEditor->UndoTransaction());TestEqual(TEXT("Full subdivision undo"),BuildingOutlineCopySnapshot(B),Original);
  TestTrue(TEXT("Single redo restores dependencies"),GEditor->RedoTransaction());TestEqual(TEXT("Full subdivision redo"),BuildingOutlineCopySnapshot(B),Final);TestEqual(TEXT("One redo restores receipt identity"),B->LastCommittedEdit.StateId,Receipt.StateId);
  if(!Railing)
  {
   const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* Node=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==StartId;});
   if(Node){const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node->NodeGuid,Node->LocalPosition,Node->LocalPosition+FVector(-10,0,0),false);TestTrue(*Move.Message,Move.bSucceeded);if(Move.bSucceeded){GEditor->UndoTransaction(false);TestEqual(TEXT("Continued editing undo restores subdivided dependencies"),BuildingOutlineCopySnapshot(B),Final);}}
  }
  GEditor->UndoTransaction(false);GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFloorContactGeometryTest,"EHB.Topology.FloorContactGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFloorContactGeometryTest::RunTest(const FString& Parameters)
{
 auto Rect=[](double X0,double Y0,double X1,double Y1,double Z=300){return TArray<FVector>{FVector(X0,Y0,Z),FVector(X1,Y0,Z),FVector(X1,Y1,Z),FVector(X0,Y1,Z)};};
 FEHBFloorFinishRegion Floor;Floor.OuterPolygon=Rect(0,0,100,100);
 FEHBFloorSupportSurface Top;Top.OuterPolygon=Floor.OuterPolygon;
 FEHBFloorContact C;FName Status;
 TestTrue(TEXT("Full planar intersection"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Square contact cm squared"),C.Area,10000.0);
 TestTrue(TEXT("Representative point strictly inside square"),C.Point.X>0&&C.Point.X<100&&C.Point.Y>0&&C.Point.Y<100&&C.Point.Z==300);
 TestTrue(TEXT("Duplicate overlapping regions and tops union once"),FEHBFloorContactGeometry::Build({Floor,Floor},{Top,Top},C,Status));TestEqual(TEXT("No doubled area"),C.Area,10000.0);
 Floor.Holes.AddDefaulted_GetRef().LocalPolygon=Rect(20,20,80,80);
 TestTrue(TEXT("Floor hole subtraction"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Donut contact area"),C.Area,6400.0);TestTrue(TEXT("Contact stays outside central hole"),C.Point.X<20||C.Point.X>80||C.Point.Y<20||C.Point.Y>80);
 Top.OuterPolygon=Rect(30,30,70,70);TestTrue(TEXT("Host wholly in floor hole is valid non-contact"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Hole has no contact"),C.Area,0.0);
 Floor.Holes.Reset();Top.OuterPolygon=Floor.OuterPolygon;Top.Holes.AddDefaulted_GetRef().LocalPolygon=Rect(20,20,80,80);
 TestTrue(TEXT("Host opening subtraction"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Host hole contact area"),C.Area,6400.0);
 Top.Holes.Reset();Top.OuterPolygon={FVector(90,120,300),FVector(120,90,300),FVector(120,120,300)};
 TestTrue(TEXT("Diagonal AABB overlap without surface overlap"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("False broad-phase contact removed"),C.Area,0.0);
 Top.OuterPolygon=Rect(100,0,200,100);TestTrue(TEXT("Edge touch is not area contact"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Zero contact on edge"),C.Area,0.0);
 Top.OuterPolygon=Rect(0,0,100,100,301.01);TestTrue(TEXT("Height mismatch"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("No contact above tolerance"),C.Area,0.0);
 Top.OuterPolygon=Rect(0,0,100,100);Top.OuterPolygon[0].Z+=2;TestFalse(TEXT("Nonplanar candidate refused"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestEqual(TEXT("Failure clears contact output"),C.Area,0.0);
 Top.OuterPolygon=Rect(0,0,100,100);Top.OuterPolygon[0].X=1.e10;TestFalse(TEXT("Coordinates outside quantized domain refused"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!Fixture.Create(World,RF_Transactional))return false;
 auto* Slab=World->SpawnActor<AEHB_FloorSlab>();if(!Slab)return false;Fixture.Actors.Add(Slab);
 Slab->ConfigureDefaultSlab(Fixture.Building,FTransform(FVector(0,0,300)),100,20,false);
 FEHBFloorSlabHole Hole;Hole.LocalPolygon=Rect(20,20,80,80,0);
 TestTrue(TEXT("Create actual slab with hole"),Slab->SetSlabOutline(Rect(0,0,100,100,0),{Hole}));
 TArray<FEHBFloorSupportSurface> Captured;TestTrue(TEXT("Capture authored horizontal slab tops"),FEHBFloorContactGeometry::CaptureHorizontalTops(Slab,Captured,Status));TestTrue(TEXT("Captured actual slab top with holes"),!Captured.IsEmpty());
 TestTrue(TEXT("Actual cut slab contact"),FEHBFloorContactGeometry::Build({Floor},Captured,C,Status));TestTrue(TEXT("Authored hole remains absent from contact"),FMath::IsNearlyEqual(C.Area,6400.0,0.01));
 Slab->VisualExpansion=10;Slab->RebuildSlabMesh();
 TestTrue(TEXT("Capture excludes display expansion"),FEHBFloorContactGeometry::CaptureHorizontalTops(Slab,Captured,Status));TestTrue(TEXT("Contact still computes"),FEHBFloorContactGeometry::Build({Floor},Captured,C,Status));TestTrue(TEXT("Display expansion never changes logical contact"),FMath::IsNearlyEqual(C.Area,6400.0,0.01));
 TestTrue(TEXT("Reset slab to full top"),Slab->SetSlabOutline(Rect(0,0,100,100,0),{}));
 FEHBFloorSlabCutterData Cutter;Cutter.Size=20;Cutter.LocalTransform=FTransform(FQuat::Identity,FVector(50,50,0),FVector(1,6,1));Slab->PreviewCutters.Add(Cutter);
 TestTrue(TEXT("Capture split logical slab without rebuilding render mesh"),FEHBFloorContactGeometry::CaptureHorizontalTops(Slab,Captured,Status));TestEqual(TEXT("Both cut islands retained"),Captured.Num(),2);
 TestTrue(TEXT("Split logical slab contact"),FEHBFloorContactGeometry::Build({Floor},Captured,C,Status));TestTrue(TEXT("Both islands area counted"),FMath::IsNearlyEqual(C.Area,8000.0,0.01));
 Top.OuterPolygon=Rect(0,0,0.001,100);TestTrue(TEXT("Narrow quantized contact supported"),FEHBFloorContactGeometry::Build({Floor},{Top},C,Status));TestTrue(TEXT("Narrow area and interior point retained"),FMath::IsNearlyEqual(C.Area,0.1,1.e-6)&&C.Point.X>0&&C.Point.X<0.001);

 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSurfaceFinishRoomMoveTest,"EHB.Topology.SurfaceFinishRoomMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSurfaceFinishRoomMoveTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("Mesh signed zero spelling normalized"),NormalizeCopySnapshotText(TEXT("X=-0.000 Y=0.000 Z=-0.000X=1.000")),FString(TEXT("X=0.000 Y=0.000 Z=0.000X=1.000")));
 TestEqual(TEXT("Nonzero geometry and numeric design precision retained"),NormalizeCopySnapshotText(TEXT("X=-0.0001 \"x\":-0.00001")),FString(TEXT("X=-0.0001 \"x\":-0.00001")));
 FEHBFloorSupportSurface Square;Square.OuterPolygon={FVector(0,0,300),FVector(100,0,300),FVector(100,100,300),FVector(0,100,300)};FName CoverageStatus;
 auto Shifted=Square;for(auto& P:Shifted.OuterPolygon)P.X+=10;
 TestFalse(TEXT("Equal-area shifted tops fail final verification"),EHBRoomFinishMove::SameTopCoverage({Square},{Shifted},CoverageStatus));
 auto Raised=Square;for(auto& P:Raised.OuterPolygon)P.Z+=0.5;
 TestFalse(TEXT("Final host height verification stricter than contact matching tolerance"),EHBRoomFinishMove::SameTopCoverage({Square},{Raised},CoverageStatus));
 FEHBFloorSupportSurface A,Bb;A.OuterPolygon={Square.OuterPolygon[0],Square.OuterPolygon[1],Square.OuterPolygon[2]};Bb.OuterPolygon={Square.OuterPolygon[0],Square.OuterPolygon[2],Square.OuterPolygon[3]};
 TestTrue(TEXT("Equivalent triangulations accepted"),EHBRoomFinishMove::SameTopCoverage({Square},{A,Bb},CoverageStatus));

 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* Shared=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==Fixture.Pillars[1]->ElementGuid;});if(!Shared)return false;const auto Node=*Shared;const FVector Target=Node.LocalPosition+FVector(10,0,0);
 const auto Before=BuildingOutlineCopySnapshot(B);
 const auto Preview=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target,true);
 TestTrue(*Preview.Message,Preview.bSucceeded);TestEqual(TEXT("Finish move preview leaves all source data untouched"),BuildingOutlineCopySnapshot(B),Before);
 const auto OldRelations=B->ElementRelations;
 const auto Moved=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
 TestTrue(*Moved.Message,Moved.bSucceeded);if(!Moved.bSucceeded)return false;
 for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* Floor=Cast<AEHB_Floor>(E))
 {
  TestTrue(TEXT("Logical elevated floor height preserved"),FMath::IsNearlyEqual(Floor->FloorRegions[0].OuterPolygon[0].Z,300.0));
  TestTrue(TEXT("Moved floor provenance current"),Floor->IsRecordedOutlineUnchanged());
  FEHBBuildingClosedLoop Room;TestTrue(TEXT("Moved floor still in stable room"),Floor->TryGetRoomLoop(Room));
  TArray<FEHBElementRelation> Expected;FName Status;TestTrue(TEXT("Final exact contacts computable"),Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Expected,Status));
  TestEqual(TEXT("Every expected contact published"),Floor->SurfaceFinishRelationGuids.Num(),Expected.Num());
  for(const auto& R:Expected)
  {
   const auto* Published=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});
   TestTrue(TEXT("Contact identity and area match current geometry"),Published&&Published->ContactArea>0&&FMath::IsNearlyEqual(Published->ContactArea,R.ContactArea,0.01));
   const auto* Old=OldRelations.FindByPredicate([&](const auto& V){return V.IsEquivalentTo(R);});
   if(Old)TestEqual(TEXT("Retained host keeps relationship ID"),R.RelationGuid,Old->RelationGuid);
   if(Published){TestEqual(TEXT("Contact source revision refreshed last"),Published->SourceGeometryRevision,B->GetElementGeometryRevision(Published->Source.ElementGuid));TestEqual(TEXT("Contact target revision refreshed last"),Published->TargetGeometryRevision,B->GetElementGeometryRevision(Floor->ElementGuid));}
  }
 }
 const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("Finish move changes complete building state"),After!=Before);
 TestTrue(TEXT("Undo whole finish move"),GEditor->UndoTransaction());TestEqual(TEXT("Undo recovers geometry, rooms, contacts and provenance"),BuildingOutlineCopySnapshot(B),Before);
 TestTrue(TEXT("Redo whole finish move"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact finish state"),BuildingOutlineCopySnapshot(B),After);TestTrue(TEXT("Return to source"),GEditor->UndoTransaction(false));
 for(auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterContacts,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline})
 {
  EHBNodeMoveTestHooks::FailurePhase=Phase;
  const auto Failed=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
  TestFalse(TEXT("Injected full finish edit fails"),Failed.bSucceeded);TestEqual(TEXT("Production transaction rollback"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
  TestEqual(TEXT("Failure restores source geometry graph and caches"),BuildingOutlineCopySnapshot(B),Before);TestFalse(TEXT("No redoable half-applied finish move"),GEditor->RedoTransaction());
 }
 EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::None;
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMoveNotificationBatchTest,"EHB.Topology.NodeMoveNotificationBatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeMoveNotificationBatchTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);
 const auto* Shared=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==Fixture.Pillars[1]->ElementGuid;});if(!Shared)return false;
 const auto Node=*Shared;const FVector Target=Node.LocalPosition+FVector(10,0,0);
 auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();
 B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);
 B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);
 B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);
 int32 Events=0;TSet<FGuid> RelationEvents,GeometryEvents;bool ReentryChecked=false;
 Observer->Observe=[&](FGuid Id,FName Kind,bool Finished)
 {
  ++Events;TestFalse(TEXT("Command notifications fire outside editor transaction"),GEditor->IsTransactionActive());
  const auto Current=UEHBWallTopologyLibrary::CaptureWallTopology(B);
  const auto* Moved=Current.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Node.NodeGuid;});
  TestTrue(TEXT("Every observer sees final moved node"),Moved&&Moved->LocalPosition.Equals(Target,0.001));
  if(Kind==TEXT("Geometry")){TestFalse(TEXT("One notification per changed geometry"),GeometryEvents.Contains(Id));GeometryEvents.Add(Id);TestTrue(TEXT("Finished geometry state retained"),Finished);}
  else{TestFalse(TEXT("One notification per changed relation"),RelationEvents.Contains(Id));RelationEvents.Add(Id);}
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* Floor=Cast<AEHB_Floor>(E))
  {
   TestTrue(TEXT("Observers see final floor provenance"),Floor->IsRecordedOutlineUnchanged());
   FEHBBuildingClosedLoop Room;TArray<FEHBElementRelation> Plan;FName Status;
   TestTrue(TEXT("Observers can plan exact final contacts"),Floor->TryGetRoomLoop(Room)&&Floor->BuildSurfaceFinishRelationPlan(Room,Floor->FloorRegions,Plan,Status));
   TestEqual(TEXT("Observers see all final published contacts"),Floor->SurfaceFinishRelationGuids.Num(),Plan.Num());
   for(const auto& R:Plan){const auto* Actual=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});TestTrue(TEXT("Observer contact is current"),Actual&&FMath::IsNearlyEqual(Actual->ContactArea,R.ContactArea,0.01)&&Actual->SourceGeometryRevision==B->GetElementGeometryRevision(Actual->Source.ElementGuid)&&Actual->TargetGeometryRevision==B->GetElementGeometryRevision(Actual->Target.ElementGuid));}
  }
  if(!ReentryChecked){ReentryChecked=true;const auto Reentry=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Target,Target+FVector(1,0,0));TestFalse(TEXT("Reentrant edit refused during publication"),Reentry.bSucceeded);TestEqual(TEXT("Reentry reason explicit"),Reentry.Message,FString(TEXT("BuildingChangePublicationBusy")));}
 };
 const FString Before=BuildingOutlineCopySnapshot(B);
 auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
 TestTrue(*Move.Message,Move.bSucceeded);TestTrue(TEXT("Both contact and geometry notifications published"),!RelationEvents.IsEmpty()&&!GeometryEvents.IsEmpty());
 Observer->Observe=[&](FGuid,FName,bool){++Events;};
 if(Move.bSucceeded){TestTrue(TEXT("Undo completed move"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo source restored"),BuildingOutlineCopySnapshot(B),Before);}
 auto RevisionSnapshot=[&](){TArray<FString> Values;for(auto* E:B->QueryElements(FEHBElementQuery()))Values.Add(E->ElementGuid.ToString()+TEXT(":")+FString::FromInt(B->GetElementGeometryRevision(E->ElementGuid)));for(const auto& R:B->ElementRelations)Values.Add(R.RelationGuid.ToString()+FString::Printf(TEXT(":%d:%d"),R.SourceGeometryRevision,R.TargetGeometryRevision));Values.Sort();return FString::Join(Values,TEXT("|"));};
 const int32 GraphRevision=B->RelationshipGraphRevision;const auto Revisions=RevisionSnapshot();
 for(auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterContacts,EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline})
 {
  Events=0;EHBNodeMoveTestHooks::FailurePhase=Phase;
  const auto Failed=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
  TestEqual(TEXT("Injected failure restored transaction"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));
  TestEqual(TEXT("No intermediate or rollback notifications leak"),Events,0);
  TestEqual(TEXT("Failed command graph version unchanged"),B->RelationshipGraphRevision,GraphRevision);
  TestEqual(TEXT("Failed command geometry and relation revisions unchanged"),RevisionSnapshot(),Revisions);
  TestEqual(TEXT("Failed command source state unchanged"),BuildingOutlineCopySnapshot(B),Before);
 }
 EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::None;
 // A single batch reports net membership, not temporary add/remove pairs.
 Events=0;RelationEvents.Reset();GeometryEvents.Reset();
 Observer->Observe=[&](FGuid Id,FName Kind,bool Finished){++Events;if(Kind==TEXT("Geometry")){GeometryEvents.Add(Id);TestTrue(TEXT("Geometry completion is OR-coalesced"),Finished);}else RelationEvents.Add(Id);};
 {
  FEHBChangeNotificationBatch Batch(*B);TestTrue(TEXT("Can start independent notification batch"),Batch.IsActive());
  FEHBChangeNotificationBatch Nested(*B);TestFalse(TEXT("Nested batch rejected"),Nested.IsActive());
  FEHBElementRelation Temp;Temp.Type=EEHBElementRelationType::LogicalDependency;Temp.Source=FEHBElementRelationEndpoint::MakeElement(Fixture.Pillars[0]->ElementGuid);Temp.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Pillars[1]->ElementGuid);
  const FGuid Id=B->AddOrUpdateElementRelation(Temp,false);TestTrue(TEXT("Temporary relation created"),Id.IsValid());TestTrue(TEXT("Temporary relation removed"),B->RemoveElementRelation(Id));
  B->NotifyElementGeometryChanged(Fixture.Pillars[0]->ElementGuid,false);B->NotifyElementGeometryChanged(Fixture.Pillars[0]->ElementGuid,true);B->NotifyElementGeometryChanged(Fixture.Pillars[0]->ElementGuid,false);
  TestEqual(TEXT("No events before explicit publication"),Events,0);Batch.Publish();
 }
 TestTrue(TEXT("Temporary relation has no external event"),RelationEvents.IsEmpty());TestEqual(TEXT("Repeated geometry emits once"),GeometryEvents.Num(),1);
 FEHBElementRelation Kept;Kept.Type=EEHBElementRelationType::LogicalDependency;Kept.Source=FEHBElementRelationEndpoint::MakeElement(Fixture.Pillars[0]->ElementGuid);Kept.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Pillars[1]->ElementGuid);
 Kept.RelationGuid=B->AddOrUpdateElementRelation(Kept,false);Events=0;RelationEvents.Reset();
 {FEHBChangeNotificationBatch Batch(*B);Kept.ContactArea=1;B->AddOrUpdateElementRelation(Kept,true);Kept.ContactArea=2;B->AddOrUpdateElementRelation(Kept,true);TestEqual(TEXT("Updates remain private until publish"),Events,0);Batch.Publish();}
 TestEqual(TEXT("Repeated retained relationship update emits once"),Events,1);TestTrue(TEXT("Update retains relationship identity"),RelationEvents.Contains(Kept.RelationGuid));
 Events=0;Observer->Observe=[&](FGuid Id,FName Kind,bool){++Events;TestEqual(TEXT("Removal event identity"),Id,Kept.RelationGuid);TestEqual(TEXT("Only final removal is reported"),Kind,FName(TEXT("Removed")));TestFalse(TEXT("Removed relation absent before observer"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Id;}));};
 {FEHBChangeNotificationBatch Batch(*B);B->AddOrUpdateElementRelation(Kept,true);B->RemoveElementRelation(Kept.RelationGuid);Batch.Publish();}
 TestEqual(TEXT("Update then removal reports one final removal"),Events,1);
 Observer->Observe=nullptr;B->OnElementRelationAdded.RemoveAll(Observer);B->OnElementRelationRemoved.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCommittedEditRecordTest,"EHB.Topology.CommittedEditRecord",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCommittedEditRecordTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* Shared=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==Fixture.Pillars[1]->ElementGuid;});if(!Shared)return false;
 const auto Node=*Shared;const FVector Target=Node.LocalPosition+FVector(10,0,0);
 auto Json=[](const FEHBCommittedEdit& Edit){FString Text;FJsonObjectConverter::UStructToJsonObjectString(Edit,Text);return Text;};
 const auto Before=Json(B->LastCommittedEdit);const auto BeforeBounds=Fixture.Pillars[1]->GetBuildingLocalBounds();
 auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();int32 Commits=0;
 B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
 Observer->ObserveCommit=[&](const FEHBCommittedEdit& Edit){++Commits;TestFalse(TEXT("Receipt is published after transaction completion"),GEditor->IsTransactionActive());TestEqual(TEXT("Receipt matches stored state"),Json(Edit),Json(B->LastCommittedEdit));TestTrue(TEXT("Receipt refers to this building"),Edit.BuildingGuid==B->BuildingGuid);};
 const auto Preview=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target,true);
 TestTrue(*Preview.Message,Preview.bSucceeded);TestEqual(TEXT("Preview does not advance edit state"),Json(B->LastCommittedEdit),Before);TestEqual(TEXT("Preview emits no receipt"),Commits,0);
 const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
 TestTrue(*Move.Message,Move.bSucceeded);if(!Move.bSucceeded)return false;
 const auto First=B->LastCommittedEdit;TestEqual(TEXT("One command advances once"),First.Sequence,int64(1));TestTrue(TEXT("First commit has independent identity"),First.StateId.IsValid()&&!First.ParentStateId.IsValid());TestEqual(TEXT("One receipt event"),Commits,1);TestEqual(TEXT("Operation result exposes receipt"),Json(Move.CommittedEdit),Json(First));
 TestTrue(TEXT("Receipt includes moved logical node"),First.NodeGuids.Contains(Node.NodeGuid));
 const auto* Range=First.Elements.FindByPredicate([&](const auto& E){return E.ElementGuid==Node.SourcePillarGuid;});
 TestTrue(TEXT("Moved physical pillar has both regions"),Range&&Range->BeforeBounds.Equals(BeforeBounds,0.001)&&Range->AfterBounds.Equals(Fixture.Pillars[1]->GetBuildingLocalBounds(),0.001));
 for(const auto& Room:B->GetClosedLoopsByFloor(1))TestTrue(TEXT("Shared room invalidation includes both rooms"),First.RoomGuids.Contains(Room.LoopGuid));
 TestTrue(TEXT("Old region invalidated"),First.Intersects(BeforeBounds));TestFalse(TEXT("Distant region remains outside conservative bounds"),First.Intersects(FBox(FVector(1.e7),FVector(1.e7+100))));
 FEHBCommittedEdit Unknown;Unknown.bRequiresFullSpatialRefresh=true;TestTrue(TEXT("Unknown geometry asks full refresh"),Unknown.Intersects(FBox(FVector(1.e7),FVector(1.e7+100))));
 for(FGuid Id:First.UpdatedRelationGuids)TestTrue(TEXT("Updated receipt relation is actually present"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Id;}));
 const auto NoChange=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Target,Target);TestTrue(TEXT("No-op succeeds"),NoChange.bSucceeded);TestEqual(TEXT("No-op keeps receipt"),Json(B->LastCommittedEdit),Json(First));TestEqual(TEXT("No-op emits no event"),Commits,1);
 const auto Next=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Target,Target+FVector(5,0,0));TestTrue(*Next.Message,Next.bSucceeded);TestEqual(TEXT("Second sequence"),B->LastCommittedEdit.Sequence,int64(2));TestEqual(TEXT("Second receipt names parent"),B->LastCommittedEdit.ParentStateId,First.StateId);
 TestTrue(TEXT("Undo second command"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo restores parent receipt"),Json(B->LastCommittedEdit),Json(First));
 TestTrue(TEXT("Undo first command"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores initial receipt"),Json(B->LastCommittedEdit),Before);
 TestTrue(TEXT("Redo first command"),GEditor->RedoTransaction());TestEqual(TEXT("Redo retains exact command identity and ranges"),Json(B->LastCommittedEdit),Json(First));TestTrue(TEXT("Return to initial branch"),GEditor->UndoTransaction(false));
 Commits=0;EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::AfterEditRecord;
 const auto Failed=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);
 TestEqual(TEXT("Failure after receipt rolls back"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));TestEqual(TEXT("Failed receipt never published"),Commits,0);TestEqual(TEXT("Failed receipt never retained"),Json(B->LastCommittedEdit),Before);
 const auto Branch=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Target);TestTrue(*Branch.Message,Branch.bSucceeded);TestEqual(TEXT("New branch may reuse sequence"),B->LastCommittedEdit.Sequence,int64(1));TestTrue(TEXT("New branch never reuses state ID"),B->LastCommittedEdit.StateId!=First.StateId);
 TSharedPtr<FJsonObject> Snapshot;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UEHBBuildingToolset::GetBuildingSnapshot(B)),Snapshot);TestTrue(TEXT("Public snapshot exposes latest command receipt"),Snapshot.IsValid()&&Snapshot->HasTypedField<EJson::Object>(TEXT("lastCommittedEdit")));
 const int64 OriginalSequence=B->LastCommittedEdit.Sequence;B->LastCommittedEdit.Sequence=MAX_int64;const int32 CountBeforeOverflow=Commits;
 const auto Overflow=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Target,Target+FVector(1,0,0));TestEqual(TEXT("Sequence overflow refuses before mutation"),Overflow.Message,FString(TEXT("EditSequenceExhausted")));TestEqual(TEXT("Overflow emits no commit"),Commits,CountBeforeOverflow);B->LastCommittedEdit.Sequence=OriginalSequence;
 const auto SourceReceipt=Json(B->LastCommittedEdit);const auto Copy=EHBBuildingCopy::Execute(B,FVector(5000,0,0));TestTrue(*Copy.Status.ToString(),Copy.bSucceeded);if(Copy.bSucceeded){Fixture.Actors.Append(Copy.Actors);TestTrue(TEXT("Copied building does not inherit source history IDs"),!Copy.Building->LastCommittedEdit.StateId.IsValid()&&Copy.Building->LastCommittedEdit.Sequence==0);TestEqual(TEXT("Copy leaves source receipt intact"),Json(B->LastCommittedEdit),SourceReceipt);}
 Observer->ObserveCommit=nullptr;B->OnEditCommitted.RemoveAll(Observer);EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::None;GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomDependencyCacheTest,"EHB.Topology.RoomDependencyCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomDependencyCacheTest::RunTest(const FString& Parameters)
{
 const FGuid BuildingId=FGuid::NewGuid(),RoomA=FGuid::NewGuid(),RoomB=FGuid::NewGuid(),Wall=FGuid::NewGuid();
 FEHBRoomDependencyBinding Floor;Floor.ElementGuid=FGuid::NewGuid();Floor.RoomGuid=RoomA;
 FEHBRoomDependencyBinding Slab;Slab.ElementGuid=FGuid::NewGuid();Slab.Kind=EEHBRoomDependencyKind::Slab;Slab.AnchorWallGuid=Wall;
 FEHBRoomDependencyCache Cache;FEHBCommittedEdit Receipt;TArray<FEHBRoomDependencyBinding> Input={Floor,Slab};
 TestTrue(TEXT("Cold source indexed"),Cache.Update(BuildingId,Receipt,Input));TestEqual(TEXT("Initial full build"),Cache.GetStats().FullBuilds,1);
 TestTrue(TEXT("Room floor lookup"),Cache.Query(RoomA,{Wall}).Floors.Contains(Floor.ElementGuid));TestEqual(TEXT("Shared anchor input deduplicated"),Cache.Query(RoomA,{Wall,Wall}).AnchoredSlabs.Num(),1);
 Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Unchanged data reuses index"),Cache.GetStats().Reuses,1);
 Receipt.BuildingGuid=BuildingId;Receipt.StateId=FGuid::NewGuid();Receipt.Elements.AddDefaulted_GetRef().ElementGuid=Floor.ElementGuid;Input[0].RoomGuid=RoomB;
 Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Covered continuous edit updates membership"),Cache.GetStats().LastReason,FName(TEXT("ReceiptAdvanced")));TestTrue(TEXT("Old room membership removed"),Cache.Query(RoomA,{}).Floors.IsEmpty());TestTrue(TEXT("New room membership added"),Cache.Query(RoomB,{}).Floors.Contains(Floor.ElementGuid));
 Input[0].RoomGuid=RoomA;Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Raw binding edit forces full build without new state ID"),Cache.GetStats().LastReason,FName(TEXT("LegacyBindingChanged")));
 Receipt.ParentStateId=FGuid::NewGuid();Receipt.StateId=FGuid::NewGuid();Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Missed receipts force full build"),Cache.GetStats().LastReason,FName(TEXT("ReceiptGap")));
 Receipt.ParentStateId=Receipt.StateId;Receipt.StateId=FGuid::NewGuid();Receipt.Elements.Reset();Input[1].RoomGuid=RoomA;Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Uncovered binding change refuses incremental reuse"),Cache.GetStats().LastReason,FName(TEXT("ReceiptMissingBindingCoverage")));
 TestTrue(TEXT("Bound slab supersedes old anchor"),Cache.Query(RoomA,{Wall}).Slabs.Contains(Slab.ElementGuid)&&Cache.Query(RoomA,{Wall}).AnchoredSlabs.IsEmpty());
 Receipt.ParentStateId=Receipt.StateId;Receipt.StateId=FGuid::NewGuid();Receipt.bRequiresFullSpatialRefresh=true;Cache.Update(BuildingId,Receipt,Input);TestEqual(TEXT("Unknown affected area uses full build"),Cache.GetStats().LastReason,FName(TEXT("FullSpatialRefresh")));
 Cache.Update(BuildingId,Receipt,Input,true);TestEqual(TEXT("In-flight edits never reuse cached membership"),Cache.GetStats().LastReason,FName(TEXT("EditInFlight")));
 const auto DuplicateBinding=Input[0];Input.Add(DuplicateBinding);TestFalse(TEXT("Duplicate source identity refuses"),Cache.Update(BuildingId,Receipt,Input));TestTrue(TEXT("Invalid update exposes no stale membership"),Cache.Query(RoomA,{Wall}).Floors.IsEmpty());Input.Pop();Cache.Update(BuildingId,Receipt,Input);
 Cache.Update(FGuid::NewGuid(),FEHBCommittedEdit(),Input);TestEqual(TEXT("Other building invalidates state"),Cache.GetStats().LastReason,FName(TEXT("BuildingChanged")));

 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;const auto Rooms=B->GetClosedLoopsByFloor(1);if(Rooms.Num()!=2)return false;
 TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Building query uses membership cache"),B->QueryRoomDependencies(Rooms,Members));
 for(const auto& M:Members){TestEqual(TEXT("One floor per fixture room"),M.Floors.Num(),1);TestEqual(TEXT("One slab per fixture room"),M.Slabs.Num(),1);}
 const auto Warm=B->GetRoomDependencyCacheStats();B->QueryRoomDependencies(Rooms,Members);TestEqual(TEXT("Second query reuses actual building cache"),B->GetRoomDependencyCacheStats().Reuses,Warm.Reuses+1);
 auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(Members[0].Floors[0]));if(!F)return false;const auto OriginalRoom=F->RoomLoopGuid;
 F->RoomLoopGuid=Members[1].RoomGuid;B->QueryRoomDependencies(Rooms,Members);TestEqual(TEXT("Unnotified property reassignment detected"),B->GetRoomDependencyCacheStats().LastReason,FName(TEXT("LegacyBindingChanged")));
 const auto* OldMembers=Members.FindByPredicate([&](const auto& M){return M.RoomGuid==OriginalRoom;});TestTrue(TEXT("Reassigned floor no longer in old room"),OldMembers&&!OldMembers->Floors.Contains(F->ElementGuid));F->RoomLoopGuid=OriginalRoom;B->QueryRoomDependencies(Rooms,Members);
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* Shared=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==Fixture.Pillars[1]->ElementGuid;});if(!Shared)return false;const auto Node=*Shared;
 const auto Source=F->OutlineSource;const auto RoomFloor=F->RoomFloorIndex;F->OutlineSource=EEHBOutlineSource::ManualOrUnclassified;F->RoomFloorIndex=2;
 const auto Preview=UEHBWallTopologyLibrary::PreviewNodeMove(B,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0));
 const auto* Changed=Preview.RoomBoundaryChanges.FindByPredicate([&](const auto& C){return C.RoomGuid==OriginalRoom;});TestTrue(TEXT("Warm cache still evaluates live outline and floor mismatch"),Changed&&Changed->ModifiedOrUnclassifiedOutlineGuids.Contains(F->ElementGuid)&&Changed->bHasStaleFloorBinding);F->OutlineSource=Source;F->RoomFloorIndex=RoomFloor;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto BeforeAdvance=B->GetRoomDependencyCacheStats();const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0));TestTrue(*Move.Message,Move.bSucceeded);if(!Move.bSucceeded)return false;
 B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members);TestEqual(TEXT("Successful real command receipt consumed"),B->GetRoomDependencyCacheStats().ReceiptUpdates,BeforeAdvance.ReceiptUpdates+1);
 TestTrue(TEXT("Undo move"),GEditor->UndoTransaction(false));B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members);TestEqual(TEXT("Undo breaks lineage and rebuilds"),B->GetRoomDependencyCacheStats().LastReason,FName(TEXT("ReceiptGap")));
 const auto BeforeFailure=B->GetRoomDependencyCacheStats();EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::AfterEditRecord;
 const auto Failed=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Node.NodeGuid,Node.LocalPosition,Node.LocalPosition+FVector(10,0,0));TestEqual(TEXT("Injected command restored"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members);TestEqual(TEXT("Failed command consumes no new receipt"),B->GetRoomDependencyCacheStats().ReceiptUpdates,BeforeFailure.ReceiptUpdates);
 EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::None;GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFloorContactCacheTest,"EHB.Topology.FloorContactCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFloorContactCacheTest::RunTest(const FString& Parameters)
{
 auto Rect=[](double X0,double Y0,double X1,double Y1,double Z=300.0){return TArray<FVector>{{X0,Y0,Z},{X1,Y0,Z},{X1,Y1,Z},{X0,Y1,Z}};};
 FEHBFloorFinishRegion Floor;Floor.OuterPolygon=Rect(0,0,100,100);
 FEHBFloorSupportSurface Top;Top.OuterPolygon=Rect(0,0,100,100);
 const FGuid Host=FGuid::NewGuid(),Other=FGuid::NewGuid();FEHBFloorContactCache Cache;
 FEHBFloorContact Actual,Reference;FName Status,ReferenceStatus;
 TestTrue(TEXT("Cold contact computes"),Cache.Query(Host,{Floor},{Top},Actual,Status));TestEqual(TEXT("Full contact"),Actual.Area,10000.0);
 TestTrue(TEXT("Unchanged contact reuses"),Cache.Query(Host,{Floor},{Top},Actual,Status));TestEqual(TEXT("One solve"),Cache.GetStats().Solves,uint64(1));TestEqual(TEXT("One hit"),Cache.GetStats().Hits,uint64(1));
 Cache.Query(Other,{Floor},{Top},Actual,Status);
 Top.Holes.AddDefaulted_GetRef().LocalPolygon=Rect(20,20,80,80);
 TestTrue(TEXT("Direct hole edit computes"),Cache.Query(Host,{Floor},{Top},Actual,Status));TestEqual(TEXT("Opening subtracts contact"),Actual.Area,6400.0);
 const auto Changed=Cache.GetStats();FEHBFloorSupportSurface Unchanged;Unchanged.OuterPolygon=Rect(0,0,100,100);Cache.Query(Other,{Floor},{Unchanged},Actual,Status);TestEqual(TEXT("Another host remains cached"),Cache.GetStats().Solves,Changed.Solves);TestEqual(TEXT("Another host reused"),Cache.GetStats().Hits,Changed.Hits+1);
 // Even changes below clipping resolution invalidate exact inputs; Build decides quantization.
 Top.OuterPolygon[0].X+=0.00001;Cache.Query(Host,{Floor},{Top},Actual,Status);TestEqual(TEXT("Sub-grid edit not hidden by fuzzy cache key"),Cache.GetStats().Solves,Changed.Solves+1);
 FEHBFloorContactGeometry::Build({Floor},{Top},Reference,ReferenceStatus);TestEqual(TEXT("Cache matches uncached area"),Actual.Area,Reference.Area);TestEqual(TEXT("Cache matches uncached point"),Actual.Point,Reference.Point);
 Top.OuterPolygon[0].Z+=2;TestFalse(TEXT("Invalid changed top refuses"),Cache.Query(Host,{Floor},{Top},Actual,Status));TestEqual(TEXT("No stale contact on error"),Actual.Area,0.0);TestEqual(TEXT("Invalid host entry removed"),Cache.GetStats().Entries,1);
 Top=Unchanged;for(auto& P:Top.OuterPolygon)P.X+=1000;Cache.Query(Host,{Floor},{Top},Actual,Status);TestEqual(TEXT("No contact is valid"),Actual.Area,0.0);const auto Zero=Cache.GetStats();Cache.Query(Host,{Floor},{Top},Actual,Status);TestEqual(TEXT("Zero contact also cached"),Cache.GetStats().Hits,Zero.Hits+1);
 Cache.RetainHosts({Host});TestEqual(TEXT("Removed hosts pruned"),Cache.GetStats().Entries,1);Cache.RetainHosts({});TestEqual(TEXT("Empty sources release points"),Cache.GetStats().StoredPoints,int64(0));
 FEHBFloorContactCache Limited(1,8);Limited.Query(Host,{Floor},{Unchanged},Actual,Status);Limited.Query(Other,{Floor},{Unchanged},Actual,Status);TestEqual(TEXT("Entry limit bounded"),Limited.GetStats().Entries,1);TestEqual(TEXT("Point budget bounded"),Limited.GetStats().StoredPoints,int64(8));
 Floor.Holes.AddDefaulted_GetRef().LocalPolygon=Rect(20,20,80,80);Limited.Query(Other,{Floor},{Unchanged},Actual,Status);TestEqual(TEXT("Oversized input computes correct area"),Actual.Area,6400.0);TestEqual(TEXT("Oversized replacement removes old entry"),Limited.GetStats().Entries,0);
 TestFalse(TEXT("Invalid cache identity refuses"),Limited.Query(FGuid(),{Floor},{Unchanged},Actual,Status));TestEqual(TEXT("Invalid identity clears output"),Actual.Area,0.0);

 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
 if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;AEHB_Floor* F=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if((F=Cast<AEHB_Floor>(E)))break;
 if(!F)return false;FEHBBuildingClosedLoop Room;if(!F->TryGetRoomLoop(Room))return false;
 TArray<FEHBElementRelation> Plan;TestTrue(TEXT("Actual plan warms contact cache"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));const auto Warm=F->GetContactCacheStats();
 TestTrue(TEXT("Actual repeated plan"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("Repeated plan no polygon solves"),F->GetContactCacheStats().Solves,Warm.Solves);TestTrue(TEXT("Repeated plan uses hits"),F->GetContactCacheStats().Hits>Warm.Hits);
 auto* Pillar=Fixture.Pillars[0];TArray<FEHBFloorSupportSurface> Candidate;if(!FEHBFloorContactGeometry::CaptureHorizontalTops(Pillar,Candidate,Status))return false;
 for(auto& T:Candidate)for(auto& P:T.OuterPolygon)P.X+=5;
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> Candidates;Candidates.Add(Pillar->ElementGuid,Candidate);const auto BeforeCandidate=F->GetContactCacheStats();
 TestTrue(TEXT("Candidate contact plan"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status,&Candidates));TestEqual(TEXT("Only changed candidate host solved"),F->GetContactCacheStats().Solves,BeforeCandidate.Solves+1);
 const auto BeforeRestore=F->GetContactCacheStats();TestTrue(TEXT("Actual geometry after candidate recomputed"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("Candidate cannot poison current geometry"),F->GetContactCacheStats().Solves,BeforeRestore.Solves+1);
 const auto OriginalTransform=Pillar->GetActorTransform();Pillar->SetActorLocation(Pillar->GetActorLocation()+FVector(5,0,0));const auto BeforeRaw=F->GetContactCacheStats();
 TestTrue(TEXT("Unnotified transform detected by capture"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("Raw moved host alone recomputed"),F->GetContactCacheStats().Solves,BeforeRaw.Solves+1);Pillar->SetActorTransform(OriginalTransform);
 TestTrue(TEXT("Restored geometry plan"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* Node=Graph.Nodes.FindByPredicate([&](const auto& N){return N.SourcePillarGuid==Fixture.Pillars[1]->ElementGuid;});if(!Node)return false;const auto N=*Node;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Move=UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,N.NodeGuid,N.LocalPosition,N.LocalPosition+FVector(10,0,0));TestTrue(*Move.Message,Move.bSucceeded);if(!Move.bSucceeded)return false;
 TestTrue(TEXT("Undo restores actual cached geometry sources"),GEditor->UndoTransaction(false));if(!F->TryGetRoomLoop(Room))return false;
 TestTrue(TEXT("Plan after undo remains valid"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));
 for(const auto& R:Plan){auto* H=B->FindElementActorByGuid(R.Source.ElementGuid);TArray<FEHBFloorSupportSurface> Tops;TestTrue(TEXT("Capture restored host"),FEHBFloorContactGeometry::CaptureHorizontalTops(H,Tops,Status));TestTrue(TEXT("Uncached restored solve"),FEHBFloorContactGeometry::Build(F->FloorRegions,Tops,Reference,Status));TestEqual(TEXT("Undo contact equals uncached area"),R.ContactArea,static_cast<float>(Reference.Area));TestEqual(TEXT("Undo contact equals uncached point"),R.ContactPoint,Reference.Point);}
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStructuralContactSourceTest,"EHB.Topology.StructuralContactSources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStructuralContactSourceTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!Fixture.Create(World,RF_Transactional))return false;
 FName Status;TArray<FEHBFloorSupportSurface> Logical,Rendered,Again;
 auto Same=[&](const auto& A,const auto& B){return EHBRoomFinishMove::SameTopCoverage(A,B,Status);};
 for(auto* E:Fixture.Building->QueryElements(FEHBElementQuery()))
 {
  if(!Cast<AEHB_Wall>(E)&&!Cast<AEHB_Pillar>(E))continue;
  TestTrue(TEXT("Read structural tops"),FEHBFloorContactGeometry::CaptureHorizontalTops(E,Logical,Status));TestEqual(TEXT("Explicit structural route"),Status,FName(TEXT("CapturedStructural")));
  TestTrue(TEXT("Capture rendered comparison"),FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(E,Rendered,Status));TestTrue(TEXT("Undecorated structural coverage matches render"),Same(Logical,Rendered));
 }
 auto* Wall=Fixture.Walls[0];auto* Pillar=Fixture.Pillars[0];
 FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Logical,Status);
 TArray<UEHBGeneratedMeshComponent*> Components;Wall->GetGeneratedMeshComponents(Components);for(auto* C:Components)C->ClearAllMeshSections();
 TestTrue(TEXT("Wall structure independent of empty render components"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status));TestTrue(TEXT("Wall structure unchanged after clearing display"),Same(Logical,Again));
 const float OriginalHeight=Wall->Height;Wall->Height+=17;TestTrue(TEXT("Raw height uses fresh wall structure"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status));TestTrue(TEXT("Fresh top height without mesh rebuild"),Again.ContainsByPredicate([&](const auto& T){return !T.OuterPolygon.IsEmpty()&&FMath::IsNearlyEqual(T.OuterPolygon[0].Z,static_cast<double>(Wall->Height),0.001);}));Wall->Height=OriginalHeight;Wall->RebuildWallMesh();
 FEHBFloorContactGeometry::CaptureHorizontalTops(Pillar,Logical,Status);const auto OldFootprint=Pillar->PolygonPillarFootprint;Pillar->PolygonPillarFootprint={{-999,-999,0},{999,-999,0},{999,999,0},{-999,999,0}};
 Pillar->GetGeneratedMeshComponents(Components);for(auto* C:Components)C->ClearAllMeshSections();TestTrue(TEXT("Fresh pillar footprint ignores old display footprint"),FEHBFloorContactGeometry::CaptureHorizontalTops(Pillar,Again,Status));TestTrue(TEXT("Pillar structure unchanged with corrupted derived buffers"),Same(Logical,Again));Pillar->PolygonPillarFootprint=OldFootprint;Pillar->RebuildPillarMesh();
 // Curves and ordinary opening reveals keep their current geometry behavior.
 Wall->CurveControlOffset=75;Wall->RebuildWallMesh();TestTrue(TEXT("Curved wall structural tops"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Logical,Status));FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(Wall,Rendered,Status);TestTrue(TEXT("Curved structural/render coverage matches"),Same(Logical,Rendered));Wall->CurveControlOffset=0;Wall->RebuildWallMesh();
 auto SetOpening=[&](double TopZ)
 {
  Wall->CutOperations.Reset();auto& Cut=Wall->CutOperations.AddDefaulted_GetRef();Cut.OperationGuid=FGuid::NewGuid();Cut.Stage=EEHBCutStage::SurfaceOpening;Cut.OperationType=EEHBCutOperationType::Subtract;Cut.ProjectionMode=EEHBCutProjectionMode::VerticalXZ;
  for(const auto& P:TArray<FVector>{{-50,0,100},{50,0,100},{50,0,TopZ},{-50,0,TopZ}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;
 };
 SetOpening(200);Wall->RebuildWallMesh();TestTrue(TEXT("Opening structural reveals"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Logical,Status));FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(Wall,Rendered,Status);TestTrue(TEXT("Opening reveals match undecorated render"),Same(Logical,Rendered));TestTrue(TEXT("Opening sill survives independent source"),Logical.ContainsByPredicate([](const auto& T){return !T.OuterPolygon.IsEmpty()&&FMath::IsNearlyEqual(T.OuterPolygon[0].Z,100.0,0.001);}));
 FEHBFloorFinishRegion Coverage;Coverage.OuterPolygon={{-10000,-10000,OriginalHeight},{10000,-10000,OriginalHeight},{10000,10000,OriginalHeight},{-10000,10000,OriginalHeight}};FEHBFloorContact Before,After;
 TestTrue(TEXT("Top before notch"),FEHBFloorContactGeometry::Build({Coverage},Logical,Before,Status));
 SetOpening(OriginalHeight+50);TestTrue(TEXT("Top-reaching cut updates structure before render"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Logical,Status));TestTrue(TEXT("Notched contact computes"),FEHBFloorContactGeometry::Build({Coverage},Logical,After,Status));TestTrue(TEXT("Notch removes full thickness contact strip"),FMath::IsNearlyEqual(Before.Area-After.Area,100.0*Wall->Thickness,0.01));
 Wall->RebuildWallMesh();FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(Wall,Rendered,Status);TestTrue(TEXT("Render top also respects notch"),Same(Logical,Rendered));
 SetOpening(OriginalHeight);FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status);TestTrue(TEXT("Opening ending exactly at top is open"),Same(Logical,Again));
 SetOpening(OriginalHeight-1);FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status);FEHBFloorContactGeometry::Build({Coverage},Again,After,Status);TestEqual(TEXT("Opening below top retains cap"),After.Area,Before.Area);
 SetOpening(OriginalHeight+50);auto Overlap=Wall->CutOperations[0];Overlap.OperationGuid=FGuid::NewGuid();for(auto& P:Overlap.Source.ExplicitPolygon.Points)P.LocalPosition.X+=50;Wall->CutOperations.Add(Overlap);
 FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status);FEHBFloorContactGeometry::Build({Coverage},Again,After,Status);TestTrue(TEXT("Overlapping top openings subtract union only"),FMath::IsNearlyEqual(Before.Area-After.Area,150.0*Wall->Thickness,0.01));
 SetOpening(OriginalHeight+50);for(auto& P:Wall->CutOperations[0].Source.ExplicitPolygon.Points)P.LocalPosition.X*=1000;
 TestTrue(TEXT("Full top removal remains a valid contact source"),FEHBFloorContactGeometry::CaptureHorizontalTops(Wall,Again,Status));TestTrue(TEXT("Fully removed top returns zero contact"),FEHBFloorContactGeometry::Build({Coverage},Again,After,Status));TestEqual(TEXT("Full top has no support area"),After.Area,0.0);
 Wall->CutOperations.Reset();Wall->RebuildWallMesh();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallSplitCommitProtocolTest,"EHB.Topology.WallSplitCommitProtocol",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallSplitCommitProtocolTest::RunTest(const FString& Parameters)
{
 for(bool bRailing:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr,RF_Transactional))return false;
  auto* B=Fixture.Building;auto* Source=Fixture.Walls[0];const auto Id=Source->ElementGuid;const FVector Start=Source->LocalStart,End=Source->LocalEnd;const float Height=Source->Height,Width=Source->Thickness,Distance=FVector::Dist2D(Start,End)*0.5f;
  UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!bRailing){UEHBBuildingToolset::MigrateWallNodeOwnership(B,true);if(B->WallNodeOwnership.Version!=1)return false;UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);}
  const auto OldRooms=B->GetClosedLoopsByWallGuid(Id);const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Id,Distance);const auto RailingEnd=B->GetActorTransform().TransformPosition(Split.LocalPillarPosition+FVector(0,-250,0));
  auto Commit=[&](){return bRailing?UEHBBuildingToolset::CommitWallSplitAndRailing(B,Id,Distance,B->RelationshipGraphRevision,Start,End,Height,Width,RailingEnd,100,5,80):UEHBBuildingToolset::CommitPlainWallSplit(B,Id,Distance,B->RelationshipGraphRevision,Start,End,Height,Width);};
  auto ReceiptText=[&](){FString Text;FJsonObjectConverter::UStructToJsonObjectString(B->LastCommittedEdit,Text);return Text;};
  auto Revisions=[&](){TArray<FString> V;for(auto* E:B->QueryElements(FEHBElementQuery()))V.Add(E->ElementGuid.ToString()+TEXT(":")+FString::FromInt(B->GetElementGeometryRevision(E->ElementGuid)));for(const auto& R:B->ElementRelations)V.Add(R.RelationGuid.ToString()+FString::Printf(TEXT(":%d:%d"),R.SourceGeometryRevision,R.TargetGeometryRevision));V.Sort();return FString::Join(V,TEXT("|"));};
  const FString OriginalReceipt=ReceiptText(),OriginalSnapshot=BuildingOutlineCopySnapshot(B);const int32 OriginalGraph=B->RelationshipGraphRevision;const FString OriginalRevisions=Revisions();
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>(B);int32 Events=0,Commits=0;bool bCheckFinal=false,bReentered=false;
  Observer->Observe=[&](FGuid,FName,bool){++Events;if(bCheckFinal){TestFalse(TEXT("Split notifications outside transaction"),GEditor->IsTransactionActive());TestNull(TEXT("Source already deleted at notification"),B->FindElementActorByGuid(Id));TestTrue(TEXT("Observers see coherent topology"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Issues.IsEmpty());}};
  Observer->ObserveCommit=[&](const FEHBCommittedEdit& R){++Commits;TestFalse(TEXT("Split receipt outside transaction"),GEditor->IsTransactionActive());TestEqual(TEXT("One split command identity"),R.Command,FName(bRailing?TEXT("SplitWallAndRailing"):TEXT("SplitWall")));if(!bReentered){bReentered=true;auto* W=Fixture.Walls[2];const auto Reentry=UEHBBuildingToolset::CommitPlainWallSplit(B,W->ElementGuid,FVector::Dist2D(W->LocalStart,W->LocalEnd)*0.5f,B->RelationshipGraphRevision,W->LocalStart,W->LocalEnd,W->Height,W->Thickness);TestEqual(TEXT("Reentrant split rejected before dependent work"),Reentry.Message,FString(TEXT("BuildingChangePublicationBusy")));}};
  B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
  {
   Events=Commits=0;EHBWallSplitTestHooks::FailurePhase=Phase;const auto Failed=Commit();TestEqual(TEXT("Failed split restored"),Failed.Message,FString(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Failed split zero relation/geometry events"),Events,0);TestEqual(TEXT("Failed split zero receipts"),Commits,0);TestEqual(TEXT("Failed split restores record"),ReceiptText(),OriginalReceipt);TestEqual(TEXT("Failed split restores model and geometry"),BuildingOutlineCopySnapshot(B),OriginalSnapshot);TestEqual(TEXT("Failed split graph revision restored"),B->RelationshipGraphRevision,OriginalGraph);TestEqual(TEXT("Failed split element/contact revisions restored"),Revisions(),OriginalRevisions);
  }
  Events=Commits=0;bCheckFinal=true;const auto Result=Commit();TestTrue(*Result.Message,Result.bSucceeded);if(!Result.bSucceeded)return false;TestEqual(TEXT("Single composite receipt"),Commits,1);TestTrue(TEXT("Final graph events emitted"),Events>0);TestTrue(TEXT("Result exposes receipt identity"),Result.CommittedEdit.StateId==B->LastCommittedEdit.StateId&&Result.CommittedEdit.Sequence==1);
  const auto& Receipt=B->LastCommittedEdit;const auto* Removed=Receipt.Elements.FindByPredicate([&](const auto& E){return E.ElementGuid==Id;});TestTrue(TEXT("Deleted source has old area only"),Removed&&Removed->BeforeBounds.IsValid&&!Removed->AfterBounds.IsValid);
  int32 CreatedWalls=0,CreatedPillars=0,CreatedRails=0;for(const auto& E:Receipt.Elements)if(!E.BeforeBounds.IsValid&&E.AfterBounds.IsValid){const auto* Actor=B->FindElementActorByGuid(E.ElementGuid);CreatedWalls+=Cast<AEHB_Wall>(Actor)!=nullptr;CreatedPillars+=Cast<AEHB_Pillar>(Actor)!=nullptr;CreatedRails+=Cast<AEHB_Railing>(Actor)!=nullptr;}
  TestEqual(TEXT("Both replacement walls represented"),CreatedWalls,2);TestEqual(TEXT("New physical column represented"),CreatedPillars,1);TestEqual(TEXT("Optional rail represented"),CreatedRails,bRailing?1:0);TestEqual(TEXT("Endpoint and inserted logical nodes recorded"),Receipt.NodeGuids.Num(),3);for(const auto& Room:OldRooms)TestTrue(TEXT("Previous room identity retained in invalidation"),Receipt.RoomGuids.Contains(Room.LoopGuid));for(const auto& Room:B->GetClosedLoopsByFloor(Source->FloorIndex))TestTrue(TEXT("Resulting room identity retained in invalidation"),Receipt.RoomGuids.Contains(Room.LoopGuid));
  const auto CommittedState=Receipt.StateId;const auto CommittedText=ReceiptText();bCheckFinal=false;TestTrue(TEXT("Split undo"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores prior receipt"),ReceiptText(),OriginalReceipt);TestTrue(TEXT("Split redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores same command record"),ReceiptText(),CommittedText);TestTrue(TEXT("Undo before new branch"),GEditor->UndoTransaction(false));
  bCheckFinal=true;const auto Branch=Commit();TestTrue(*Branch.Message,Branch.bSucceeded);TestTrue(TEXT("Alternative split branch uses new state identity"),Branch.CommittedEdit.StateId!=CommittedState&&Branch.CommittedEdit.Sequence==1);bCheckFinal=false;GEditor->UndoTransaction(false);
  B->LastCommittedEdit.Sequence=MAX_int64;Events=Commits=0;const auto Exhausted=Commit();TestEqual(TEXT("Split sequence exhaustion refuses before mutation"),Exhausted.Message,FString(TEXT("EditSequenceExhausted")));TestEqual(TEXT("Exhaustion zero events"),Events,0);TestEqual(TEXT("Exhaustion zero receipts"),Commits,0);B->LastCommittedEdit.Sequence=0;
  Observer->Observe=nullptr;Observer->ObserveCommit=nullptr;GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallPathCommandTest,"EHB.Topology.WallPathCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallPathCommandTest::RunTest(const FString& Parameters)
{
 for (bool bClosed : {false, true})
 {
  FTransientTopologyFixture Fixture;
  if (!Fixture.Create(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, RF_Transactional)) return false;
  auto* B = Fixture.Building;
  UEHBWallTopologyLibrary::PrepareTopologyMigration(B, true);
  GEditor->SelectNone(false, true, false); GEditor->SelectActor(B, true, false);
  if (bClosed) { UEHBBuildingToolset::MigrateWallNodeOwnership(B, true); if (B->WallNodeOwnership.Version != 1) return false; }
  FEHBWallCreationOptions Options;
  TArray<FEHBWallCreationEndpoint> Input;
  for (const FVector& P : {FVector(1500, 0, 0), FVector(2100, 0, 0), FVector(2100, 500, 0), FVector(1500, 500, 0)})
  {
   auto& E = Input.AddDefaulted_GetRef(); E.LocalLocation = P; E.WorldLocation = B->GetActorTransform().TransformPosition(P);
  }
  if (!bClosed) Input.SetNum(2);
  auto Commit = [&]() { return EHBWallCreationCommand::Commit(B, Input, bClosed, Options); };
  const FString Original = BuildingOutlineCopySnapshot(B);
  const auto OriginalReceipt = B->LastCommittedEdit;
  const int32 OriginalRevision = B->RelationshipGraphRevision;
  auto* Observer = NewObject<UEHBChangeNotificationTestObserver>(B);
  int32 Events = 0, Commits = 0;
  Observer->Observe = [&](FGuid, FName, bool) { ++Events; TestFalse(TEXT("Creation publishes outside transaction"), GEditor->IsTransactionActive()); };
  Observer->ObserveCommit = [&](const FEHBCommittedEdit& Edit)
  {
   ++Commits; TestFalse(TEXT("Creation receipt outside transaction"), GEditor->IsTransactionActive());
   TestTrue(TEXT("Complete final topology visible"), UEHBWallTopologyLibrary::CaptureWallTopology(B).Issues.IsEmpty());
   TestEqual(TEXT("Publication reentry is refused"), Commit().Status, FName(TEXT("BuildingChangePublicationBusy")));
  };
  B->OnElementGeometryChanged.AddDynamic(Observer, &UEHBChangeNotificationTestObserver::Geometry);
  B->OnElementRelationAdded.AddDynamic(Observer, &UEHBChangeNotificationTestObserver::Added);
  B->OnElementRelationRemoved.AddDynamic(Observer, &UEHBChangeNotificationTestObserver::Removed);
  B->OnEditCommitted.AddDynamic(Observer, &UEHBChangeNotificationTestObserver::Committed);
  TestEqual(TEXT("Read-only validation ready"), EHBWallCreationCommand::Validate(B, Input, bClosed, Options), FName(TEXT("Ready")));
  TestEqual(TEXT("Validation does not change geometry"), BuildingOutlineCopySnapshot(B), Original);
  const int32 EdgeCount = bClosed ? 4 : 1;
  for (int32 Phase = 1; Phase <= EdgeCount + 1; ++Phase)
  {
   EHBWallCreationCommand::FailAfterEdge = Phase <= EdgeCount ? Phase : 0;
   EHBWallCreationCommand::bFailAfterRecord = Phase > EdgeCount;
   auto Failed = Commit();
   TestEqual(TEXT("Failed path restores all previous edges"), Failed.Status, FName(TEXT("CreateFailedRolledBack")));
   TestTrue(TEXT("Failure returns no invalid actors"), Failed.Walls.IsEmpty() && Failed.Endpoints.IsEmpty() && !Failed.PrimaryWall);
   TestEqual(TEXT("Failed path restores geometry and identity"), BuildingOutlineCopySnapshot(B), Original);
   TestEqual(TEXT("Failed path restores graph revision"), B->RelationshipGraphRevision, OriginalRevision);
   TestEqual(TEXT("Failure emits no geometry/relation events"), Events, 0);
   TestEqual(TEXT("Failure emits no receipt"), Commits, 0);
  }
  const auto Result = Commit(); TestTrue(*Result.Status.ToString(), Result.bSucceeded);
  if (!Result.bSucceeded) return false;
  TestEqual(TEXT("All edges returned"), Result.Walls.Num(), EdgeCount);
  TestEqual(TEXT("One path receipt"), Commits, 1);
  TestTrue(TEXT("Geometry events published"), Events > 0);
  int32 Created = 0;
  for (const auto& E : Result.CommittedEdit.Elements) if (!E.BeforeBounds.IsValid && E.AfterBounds.IsValid) ++Created;
  TestEqual(TEXT("Receipt covers new pillars and walls"), Created, EdgeCount + Input.Num());
  if (bClosed) TestEqual(TEXT("Closed path creates second room"), B->GetClosedLoopsByFloor(1).Num(), 2);
  const FString Applied = BuildingOutlineCopySnapshot(B); const auto State = Result.CommittedEdit.StateId;
  Observer->Observe = nullptr; Observer->ObserveCommit = nullptr;
  TestTrue(TEXT("Undo whole path"), GEditor->UndoTransaction());
  TestEqual(TEXT("Undo restores complete original"), BuildingOutlineCopySnapshot(B), Original);
  TestEqual(TEXT("Undo restores original record"), B->LastCommittedEdit.StateId, OriginalReceipt.StateId);
  TestTrue(TEXT("Redo whole path"), GEditor->RedoTransaction());
  TestEqual(TEXT("Redo preserves generated model"), BuildingOutlineCopySnapshot(B), Applied);
  TestEqual(TEXT("Redo restores same record identity"), B->LastCommittedEdit.StateId, State);
  TestTrue(TEXT("Undo before alternative"), GEditor->UndoTransaction(false));
  auto Branch = Commit(); TestTrue(TEXT("Alternative creation gets distinct receipt"), Branch.bSucceeded && Branch.CommittedEdit.StateId != State);
  GEditor->UndoTransaction(false);
  auto Invalid = Input; Invalid[1].WorldLocation = Invalid[0].WorldLocation; Invalid[1].LocalLocation = Invalid[0].LocalLocation;
  TestEqual(TEXT("Collapsed edge refused before transaction"), EHBWallCreationCommand::Commit(B, Invalid, bClosed, Options).Status, FName(TEXT("CollapsedEdge")));
  Invalid = Input; Invalid[0].Wall = Fixture.Walls[0];
  TestEqual(TEXT("Wall anchors require split plan"), EHBWallCreationCommand::Commit(B, Invalid, bClosed, Options).Status, FName(TEXT("RequiresWallSplitPlan")));
  B->LastCommittedEdit.Sequence = MAX_int64;
  TestEqual(TEXT("Creation overflow refuses"), Commit().Status, FName(TEXT("EditSequenceExhausted")));
  B->LastCommittedEdit = OriginalReceipt;
  TestEqual(TEXT("Invalid requests preserve scene"), BuildingOutlineCopySnapshot(B), Original);
  // Drive the actual release handlers, including failure; they must not fall back
  // to a second mutation or keep the first two edges of a failed rectangle.
  if (bClosed)
  {
   auto* Middle = B->CreatePillarAtLocalLocation(FVector(1800,0,0), FRotator::ZeroRotator, 300,20,20,1,TEXT("PathMiddle"));
   if (!Middle) return false;
   Fixture.Actors.Add(Middle);
  }
  const FString EntryOriginal = BuildingOutlineCopySnapshot(B);
  FEasyHouseEditorMode Mode;
  auto Release = [&]()
  {
   Mode.BeginWallCreation(B, 300, 20);
   Mode.WallCreationStartLocation = Input[0].WorldLocation;
   Mode.WallCreationMouseLocation = bClosed ? Input[2].WorldLocation : Input[1].WorldLocation;
   Mode.bWallCreationDragging = true;
   return bClosed ? Mode.FinishWallCreationRectangleDrag() : Mode.FinishWallCreationDrag();
  };
  EHBWallCreationCommand::FailAfterEdge = bClosed ? 2 : 1;
  TestFalse(TEXT("Release reports failed command"), Release());
  TestFalse(TEXT("Failed release resets drag"), Mode.bWallCreationDragging);
  TestEqual(TEXT("Release does not run legacy fallback or leave partial path"), BuildingOutlineCopySnapshot(B), EntryOriginal);
  TestTrue(TEXT("Actual release commits"), Release());
  TestEqual(TEXT("Release uses command receipt"), B->LastCommittedEdit.Command, FName(bClosed ? TEXT("CreateClosedWallPath") : TEXT("CreateWallPath")));
  TestEqual(TEXT("Release respects existing intermediate pillar"), UEHBWallTopologyLibrary::CaptureWallTopology(B).Walls.Num(), bClosed ? 9 : 5);
  Mode.CancelWallCreation();
  TestTrue(TEXT("One undo reverts release"), GEditor->UndoTransaction(false));
  TestEqual(TEXT("Release undo restores geometry and identity"), BuildingOutlineCopySnapshot(B), EntryOriginal);
  if (bClosed)
  {
   // A partition that would detach an existing room finish is recovered, while
   // the unrelated room creation above remains available in the same building.
   FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transactional;
   auto* Floor = B->GetWorld()->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(), B->GetActorTransform(), Spawn);
   if (!Floor) return false;
   Fixture.Actors.Add(Floor); Floor->AttachToBuilding(B, FTransform::Identity);
   Floor->RoomLoopGuid = B->ClosedLoops[0].LoopGuid;
   const FString WithFloor = BuildingOutlineCopySnapshot(B);
   TArray<FEHBWallCreationEndpoint> Partition;
   for (auto* Pillar : {Fixture.Pillars[0], Fixture.Pillars[2]})
   { auto& E = Partition.AddDefaulted_GetRef(); E.Pillar = Pillar; E.LocalLocation = Pillar->GetElementLocalTransform().GetLocation(); E.WorldLocation = Pillar->GetActorLocation(); }
   const auto Rejected = EHBWallCreationCommand::Commit(B, Partition, false, Options);
   TestFalse(TEXT("Malformed room-dependent partition rejected in preflight"), Rejected.bSucceeded);
   TestTrue(TEXT("Preflight exposes a dependency diagnostic"), !Rejected.Status.IsNone() && Rejected.Status != FName(TEXT("CreateFailedRolledBack")));
   TestEqual(TEXT("Partition preserves floor and old room"), BuildingOutlineCopySnapshot(B), WithFloor);
  }


  GEditor->SelectNone(false, true, false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallPathPlanningTest,"EHB.Topology.WallPathPlanning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallPathPlanningTest::RunTest(const FString& Parameters)
{
 auto Point=[](int32 Id,FVector Position,int32 Floor=1){FEHBWallPathPoint P;if(Id)P.ExistingPillarGuid=FGuid(1,0,0,Id);P.LocalPosition=Position;P.FloorIndex=Floor;return P;};
 for(int32 Angle=0;Angle<360;Angle+=45)
 {
  const FVector Direction=FRotator(0,Angle,0).Vector(),Right=FVector::CrossProduct(FVector::UpVector,Direction);
  TArray<FEHBWallPathPoint> Existing={Point(1,Direction*300+Right*5),Point(2,Direction*200+FVector(0,0,300)),Point(3,Direction*400,2),Point(4,Direction*400+Right*40)};
  const TArray<FEHBWallPathPoint> Endpoints={Point(0,FVector::ZeroVector),Point(0,Direction*600)};
  const auto Plan=FEHBWallPathPlanning::Build(Existing,Endpoints,false,20);
  TestTrue(TEXT("Eight-direction pure route succeeds"),Plan.bSucceeded);TestEqual(TEXT("Only same-plane nearby middle pillar splits the wall"),Plan.Segments.Num(),2);
  if(Plan.Segments.Num()!=2)return false;
  TestEqual(TEXT("Existing physical identity is retained"),Plan.Points[Plan.Segments[0].Y].ExistingPillarGuid,Existing[0].ExistingPillarGuid);
  TestEqual(TEXT("Free endpoint is a request slot, not a node or physical GUID"),Plan.Points[Plan.Segments[0].X].EndpointIndex,0);
  Algo::Reverse(Existing);const auto Reordered=FEHBWallPathPlanning::Build(Existing,Endpoints,false,20);
  TestTrue(TEXT("Source iteration order does not change route"),Plan.Segments==Reordered.Segments&&Plan.PathEdges==Reordered.PathEdges);
 }
 TArray<FEHBWallPathPoint> Rectangle={Point(0,FVector(0,0,0)),Point(0,FVector(600,0,0)),Point(0,FVector(600,500,0)),Point(0,FVector(0,500,0))};
 const auto Plan=FEHBWallPathPlanning::Build({Point(1,FVector(300,0,0))},Rectangle,true,20);
 TestTrue(TEXT("Closed route succeeds"),Plan.bSucceeded);TestEqual(TEXT("Rectangle plans five wall segments"),Plan.Segments.Num(),5);
 TestEqual(TEXT("First input edge owns its subdivisions"),Plan.PathEdges,TArray<int32>({0,0,1,2,3}));
 auto Reject=[&](const TArray<FEHBWallPathPoint>& Existing,const TArray<FEHBWallPathPoint>& Endpoints,FName Reason)
 {const auto R=FEHBWallPathPlanning::Build(Existing,Endpoints,false,20);TestEqual(TEXT("Invalid route reason"),R.Status,Reason);TestFalse(TEXT("Invalid route not usable"),R.bSucceeded);TestTrue(TEXT("Failure clears whole output"),R.Points.IsEmpty()&&R.Segments.IsEmpty()&&R.PathEdges.IsEmpty());};
 Reject({Point(1,FVector::ZeroVector),Point(1,FVector(300,0,0))},{Rectangle[0],Rectangle[1]},TEXT("InvalidExistingPillar"));
 Reject({},{Point(77,FVector::ZeroVector),Rectangle[1]},TEXT("StaleEndpoint"));
 Reject({},{Rectangle[0],Rectangle[0]},TEXT("CollapsedEdge"));
 Reject({},{Rectangle[0],Point(0,FVector(600,0,300))},TEXT("EndpointLevelMismatch"));
 Reject({},{Rectangle[0],Point(0,FVector(600,0,0),2)},TEXT("EndpointLevelMismatch"));
 Reject({},{Rectangle[0],Point(0,FVector(std::numeric_limits<double>::quiet_NaN(),0,0))},TEXT("InvalidEndpoint"));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallPathPlanCommitTest,"EHB.Topology.WallPathPlanCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallPathPlanCommitTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr,RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
 auto* Middle=B->CreatePillarAtLocalLocation(FVector(1800,0,0),FRotator::ZeroRotator,300,20,20,1,TEXT("PlannedMiddle"));
 auto* Raised=B->CreatePillarAtLocalLocation(FVector(1700,0,200),FRotator::ZeroRotator,300,20,20,1,TEXT("SameFloorRaisedPillar"));
 if(!Middle||!Raised)return false;Fixture.Actors.Add(Middle);Fixture.Actors.Add(Raised);
 TArray<FEHBWallCreationEndpoint> Endpoints;
 for(FVector Position:{FVector(1500.25,0,0),FVector(2100.25,0,0)})
 {auto& E=Endpoints.AddDefaulted_GetRef();E.LocalLocation=Position;E.WorldLocation=B->GetActorTransform().TransformPosition(Position);}
 FEHBWallCreationOptions Options;Options.bSnapToIntegerBuildingCoordinates=false;
 const FString Before=BuildingOutlineCopySnapshot(B);
 const auto Plan=FEHBWallPathPlanning::BuildForBuilding(B,Endpoints,false,Options);
 TestTrue(TEXT("Rotated-building readonly adapter succeeds"),Plan.bSucceeded);TestEqual(TEXT("Only coplanar pillar is planned"),Plan.Segments.Num(),2);
 TestEqual(TEXT("Planning does not change model, geometry or identity"),BuildingOutlineCopySnapshot(B),Before);
 struct FRecordingPDI : FPrimitiveDrawInterface
 {
  struct FLine { FVector A,B; FLinearColor Color; }; TArray<FLine> Lines;
  FRecordingPDI():FPrimitiveDrawInterface(nullptr){}
  bool IsHitTesting() override {return false;}
  void SetHitProxy(HHitProxy*) override {}
  void RegisterDynamicResource(FDynamicPrimitiveResource*) override {}
  void AddReserveLines(uint8,int32,bool,bool) override {}
  void DrawSprite(const FVector&,float,float,const FTexture*,const FLinearColor&,uint8,float,float,float,float,uint8,float) override {}
  void DrawLine(const FVector& A,const FVector& B,const FLinearColor& C,uint8,float,float,bool) override {Lines.Add({A,B,C});}
  void DrawTranslucentLine(const FVector& A,const FVector& B,const FLinearColor& C,uint8,float,float,bool) override {Lines.Add({A,B,C});}
  void DrawPoint(const FVector&,const FLinearColor&,float,uint8) override {}
  int32 DrawMesh(const FMeshBatch&) override {return 0;}
 } PDI;
 FEasyHouseEditorMode Mode;Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;
 Mode.WallCreationStartLocation=Endpoints[0].WorldLocation;Mode.WallCreationMouseLocation=Endpoints[1].WorldLocation;
 Mode.DrawWallCreationPreview(&PDI);
 int32 MiddleLines=0;
 for(const auto& Line:PDI.Lines)if(Line.Color.Equals(FLinearColor(0.1f,1,0.45f,0.95f)))
 {
  ++MiddleLines;const FVector Local=B->GetActorTransform().InverseTransformPosition(Line.A);
  TestTrue(TEXT("Only intended intermediate pillar is highlighted"),FMath::Abs(Local.X-1800)<30&&FMath::Abs(Local.Y)<30);
 }
 TestTrue(TEXT("Actual preview draws intermediate pillar"),MiddleLines>0);
 PDI.Lines.Reset();Mode.HoveredWallCreationPillarFloorIndex=2;Mode.DrawWallCreationPreview(&PDI);
 TestEqual(TEXT("Invalid cross-level route draws only one warning line"),PDI.Lines.Num(),1);
 TestTrue(TEXT("Invalid preview is red"),PDI.Lines.Num()==1&&PDI.Lines[0].Color==FLinearColor::Red);
 Mode.CancelWallCreation();
 TestEqual(TEXT("Actual preview callback is readonly"),BuildingOutlineCopySnapshot(B),Before);

 const auto Result=EHBWallCreationCommand::Commit(B,Endpoints,false,Options);
 TestTrue(*Result.Status.ToString(),Result.bSucceeded);if(!Result.bSucceeded)return false;
 TestEqual(TEXT("Commit matches planned segment count"),Result.Walls.Num(),2);
 for(const auto* Wall:Result.Walls)
 {
  TestTrue(TEXT("Planned middle pillar connected"),Wall->StartPillarGuid==Middle->ElementGuid||Wall->EndPillarGuid==Middle->ElementGuid);
  TestFalse(TEXT("Different actual elevation is not an intermediate host"),Wall->StartPillarGuid==Raised->ElementGuid||Wall->EndPillarGuid==Raised->ElementGuid);
 }
 TestTrue(TEXT("Whole planned path undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Undo restores all previous state"),BuildingOutlineCopySnapshot(B),Before);
 Endpoints[1].FloorIndex=2;
 TestEqual(TEXT("Cross-level path rejected before spawning"),EHBWallCreationCommand::Commit(B,Endpoints,false,Options).Status,FName(TEXT("EndpointLevelMismatch")));
 TestEqual(TEXT("Preflight rejection has no side effects"),BuildingOutlineCopySnapshot(B),Before);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallFromWallCommandTest,"EHB.Topology.WallFromWallCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallFromWallCommandTest::RunTest(const FString& Parameters)
{
 for(bool bReverse:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr,RF_Transactional))return false;
  auto* B=Fixture.Building;auto* Source=Fixture.Walls[0];const FGuid SourceId=Source->ElementGuid;
  B->SetActorRotation(FRotator(0,37,0));
  const float Distance=FVector::Dist2D(Source->LocalStart,Source->LocalEnd)*0.5f;
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
  auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
  Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(60,100,50,10);
  Window->AttachToBuilding(B,FTransform::Identity);
  Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(Distance*0.5f,50),Source->GetActorQuat());
  Window->BindToWall(Source,Distance*0.5f);Window->SetFloorAssignment(Source->FloorIndex,EEHBBuildingFloorElementRole::HostedElement);
  const auto Pose=Window->GetActorTransform();const auto CutId=Source->CutOperations[0].OperationGuid;
  const auto* Host=B->ElementRelations.FindByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::HostedElement&&R.Target.RefersToElement(Window->ElementGuid);});if(!Host)return false;const auto HostId=Host->RelationGuid;
  const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,SourceId,Distance);
  FEHBWallCreationEndpoint WallEnd,Other;
  WallEnd.Wall=Source;WallEnd.WallDistance=Distance;WallEnd.LocalLocation=Split.LocalPillarPosition;WallEnd.WorldLocation=B->GetActorTransform().TransformPosition(WallEnd.LocalLocation);
  Other.LocalLocation=Split.LocalPillarPosition+FVector(300,-300,0);Other.WorldLocation=B->GetActorTransform().TransformPosition(Other.LocalLocation);
  if(bReverse)
  {
   Other.Pillar=B->CreatePillarAtLocalLocation(Other.LocalLocation,FRotator::ZeroRotator,300,20,20,1,TEXT("BranchTarget"));if(!Other.Pillar)return false;Fixture.Actors.Add(Other.Pillar);
   UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);UEHBBuildingToolset::MigrateWallNodeOwnership(B,true);
   if(B->WallNodeOwnership.Version!=1)return false;
   auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("RetainedBranchRailing"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
   if(!TestNotNull(TEXT("Existing anchored railing created"),Rail))return false;Fixture.Actors.Add(Rail);
   if(!TestTrue(TEXT("Railing matches preserved dependency contract"),FEHBPreservedRailing::Supports(Rail)))return false;

  }
  GEditor->SelectNone(false,true,false);
  FEHBWallCreationOptions Options;
  const auto Start=bReverse?Other:WallEnd,End=bReverse?WallEnd:Other;
  auto Commit=[&](bool bPreview=false){return EHBWallCreationCommand::CommitFromWall(B,Start,End,Options,bPreview);};
  const auto Original=BuildingOutlineCopySnapshot(B);const auto OriginalRecord=B->LastCommittedEdit;const int32 OriginalBaseline=B->TopologyMigrationBaseline.Version;
  const auto Ready=Commit(true);TestTrue(*Ready.Status.ToString(),Ready.bSucceeded);TestTrue(TEXT("Readonly compound preview has no output actors"),Ready.Walls.IsEmpty()&&Ready.Endpoints.IsEmpty());TestEqual(TEXT("Compound preview preserves source"),BuildingOutlineCopySnapshot(B),Original);
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>(B);int32 Events=0,Commits=0;
  Observer->Observe=[&](FGuid,FName,bool){++Events;TestFalse(TEXT("Compound geometry published after transaction"),GEditor->IsTransactionActive());};
  Observer->ObserveCommit=[&](const FEHBCommittedEdit& Edit){++Commits;TestFalse(TEXT("Compound receipt after transaction"),GEditor->IsTransactionActive());TestEqual(TEXT("One combined command identity"),Edit.Command,FName(TEXT("CreateWallFromWall")));TestNull(TEXT("Source removed before publication"),B->FindElementActorByGuid(SourceId));TestTrue(TEXT("Window pose already final"),Window->GetActorTransform().Equals(Pose,0.001));TestEqual(TEXT("Compound reentry rejected"),Commit().Status,FName(TEXT("BuildingChangePublicationBusy")));};
  B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
  {
   EHBWallSplitTestHooks::FailurePhase=Phase;const auto Failed=Commit();TestEqual(TEXT("Whole split and branch recover"),Failed.Status,FName(TEXT("SplitFailedRolledBack")));
   TestTrue(TEXT("Failed compound exposes no stale actors"),Failed.Walls.IsEmpty()&&Failed.Endpoints.IsEmpty()&&!Failed.PrimaryWall);
   TestEqual(TEXT("Failure restores source, window and baseline"),BuildingOutlineCopySnapshot(B),Original);TestEqual(TEXT("Failure emits no intermediate events"),Events,0);TestEqual(TEXT("Failure emits no receipt"),Commits,0);
  }
  const auto Result=Commit();TestTrue(*Result.Status.ToString(),Result.bSucceeded);if(!Result.bSucceeded)return false;
  TestEqual(TEXT("One branch wall returned"),Result.Walls.Num(),1);TestEqual(TEXT("Single compound receipt"),Commits,1);TestTrue(TEXT("Final events delivered"),Events>0);
  TestEqual(TEXT("Old wall split and branch created"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Walls.Num(),6);
  TestTrue(TEXT("Window pose preserved through branch junction"),Window->GetActorTransform().Equals(Pose,0.001));
  auto* NewHost=Cast<AEHB_Wall>(B->FindElementActorByGuid(Window->OwningWallGuid));TestTrue(TEXT("Window cut identity retained"),NewHost&&NewHost->CutOperations.ContainsByPredicate([&](const auto& Cut){return Cut.OperationGuid==CutId;}));
  TestTrue(TEXT("Window relationship identity retained"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==HostId&&R.Source.RefersToElement(Window->OwningWallGuid);}));
  TestEqual(TEXT("Missing baseline initialized with combined operation"),B->TopologyMigrationBaseline.Version,1);
  const FString Applied=BuildingOutlineCopySnapshot(B);const auto State=Result.CommittedEdit.StateId;
  Observer->Observe=nullptr;Observer->ObserveCommit=nullptr;
  TestTrue(TEXT("One undo reverts split, branch and window transfer"),GEditor->UndoTransaction());TestEqual(TEXT("Undo entire compound"),BuildingOutlineCopySnapshot(B),Original);TestEqual(TEXT("Undo also restores baseline initialization"),B->TopologyMigrationBaseline.Version,OriginalBaseline);TestEqual(TEXT("Undo restores original receipt"),B->LastCommittedEdit.StateId,OriginalRecord.StateId);
  TestTrue(TEXT("Redo entire compound"),GEditor->RedoTransaction());TestEqual(TEXT("Redo all identities and geometry"),BuildingOutlineCopySnapshot(B),Applied);TestEqual(TEXT("Redo keeps same receipt"),B->LastCommittedEdit.StateId,State);GEditor->UndoTransaction(false);
  FEasyHouseEditorMode Mode;
  auto Release=[&]()
  {
   Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;
   Mode.WallCreationStartLocation=Start.WorldLocation;Mode.WallCreationMouseLocation=End.WorldLocation;
   Mode.WallCreationStartWall=Start.Wall;Mode.WallCreationStartWallDistance=Start.WallDistance;Mode.WallCreationStartPillar=Start.Pillar;
   Mode.HoveredWallCreationWall=End.Wall;Mode.HoveredWallCreationWallDistance=End.WallDistance;Mode.HoveredWallCreationPillar=End.Pillar;
   return Mode.FinishWallCreationDrag();
  };
  EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord;
  TestFalse(TEXT("Actual source-wall release reports failure"),Release());TestEqual(TEXT("Actual release never falls back after rollback"),BuildingOutlineCopySnapshot(B),Original);
  TestTrue(TEXT("Actual source-wall release commits"),Release());TestEqual(TEXT("Actual release uses compound command"),B->LastCommittedEdit.Command,FName(TEXT("CreateWallFromWall")));Mode.CancelWallCreation();GEditor->UndoTransaction(false);TestEqual(TEXT("Actual release undo preserves window"),BuildingOutlineCopySnapshot(B),Original);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRelationRemovalAfterArrayRestoreTest,"EHB.Topology.RelationRemovalAfterArrayRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRelationRemovalAfterArrayRestoreTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr))return false;
 auto* B=Fixture.Building;const auto Before=B->ElementRelations;if(Before.Num()<3)return false;
 const FGuid Requested=Before[0].RelationGuid,Other=Before[1].RelationGuid;
 // Mimic a serialized transaction array restored before the derived cache callback.
 Swap(B->ElementRelations[0],B->ElementRelations[1]);
 TestTrue(TEXT("Requested relation removed despite stale but in-range index"),B->RemoveElementRelation(Requested));
 TestFalse(TEXT("Requested identity absent"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Requested;}));
 TestTrue(TEXT("Other relation at old cached slot survives"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Other;}));
 for(const auto& R:Before)if(R.RelationGuid!=Requested)TestTrue(TEXT("All unrelated identities survive"),B->ElementRelations.ContainsByPredicate([&](const auto& Current){return Current.RelationGuid==R.RelationGuid;}));
 B->ElementRelations=Before;
 TestTrue(TEXT("Restored identity missing from old cache is found"),B->RemoveElementRelation(Requested));
 const FGuid Gone=B->ElementRelations.Last().RelationGuid;B->ElementRelations.Pop();const int32 Count=B->ElementRelations.Num();
 TestFalse(TEXT("Stale out-of-range identity is not removed twice"),B->RemoveElementRelation(Gone));TestEqual(TEXT("Absent target never removes a different relation"),B->ElementRelations.Num(),Count);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBAnchoredWallPathCommandTest,"EHB.Topology.AnchoredWallPathCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBAnchoredWallPathCommandTest::RunTest(const FString& Parameters)
{
 for(int32 Scenario=0;Scenario<3;++Scenario)
 {
  const bool bClosed=Scenario==2,bReverse=Scenario==1;
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr,RF_Transactional))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
  TArray<FEHBWallCreationEndpoint> Anchors;TArray<AEHB_DoorWindow*> Windows;TArray<FTransform> Poses;TArray<FGuid> SourceIds,CutIds,HostIds;
  for(int32 SourceIndex:{0,2})
  {
   auto* Source=Fixture.Walls[SourceIndex];const float Distance=FVector::Dist2D(Source->LocalStart,Source->LocalEnd)*0.5f;
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
   auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
   Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(60,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
   Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(Distance*0.4f,50),Source->GetActorQuat());
   Window->BindToWall(Source,Distance*0.4f);Window->SetFloorAssignment(Source->FloorIndex,EEHBBuildingFloorElementRole::HostedElement);
   Windows.Add(Window);Poses.Add(Window->GetActorTransform());SourceIds.Add(Source->ElementGuid);CutIds.Add(Source->CutOperations[0].OperationGuid);
   const auto* Host=B->ElementRelations.FindByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::HostedElement&&R.Target.RefersToElement(Window->ElementGuid);});if(!Host)return false;HostIds.Add(Host->RelationGuid);
   const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Source->ElementGuid,Distance);if(!TestTrue(*Split.Status.ToString(),Split.bSucceeded))return false;
   auto& E=Anchors.AddDefaulted_GetRef();E.Wall=Source;E.WallDistance=Distance;E.LocalLocation=Split.LocalPillarPosition;E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);
  }
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("MultiSourceRetainedRailing"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
  if(!TestNotNull(TEXT("Retained rail fixture created through selected toolset"),Rail))return false;Fixture.Actors.Add(Rail);
  if(bClosed){UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);UEHBBuildingToolset::MigrateWallNodeOwnership(B,true);if(B->WallNodeOwnership.Version!=1)return false;}
  GEditor->SelectNone(false,true,false);
  TArray<FEHBWallCreationEndpoint> Endpoints=Anchors;
  if(bReverse)Swap(Endpoints[0],Endpoints[1]);
  if(bClosed)
  {
   auto Bottom=Anchors[0],Top=Anchors[1];Bottom.Wall=Top.Wall=nullptr;Bottom.LocalLocation.X=Top.LocalLocation.X=850;
   Bottom.WorldLocation=B->GetActorTransform().TransformPosition(Bottom.LocalLocation);Top.WorldLocation=B->GetActorTransform().TransformPosition(Top.LocalLocation);
   Endpoints={Anchors[0],Bottom,Top,Anchors[1]};
  }
  FEHBWallCreationOptions Options;auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::CommitAnchoredPath(B,Endpoints,bClosed,Options,Preview);};
  const auto Original=BuildingOutlineCopySnapshot(B);const auto OriginalRecord=B->LastCommittedEdit;
  auto Ready=Commit(true);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;
  TestTrue(TEXT("Multi-source preview returns no actors"),Ready.Walls.IsEmpty()&&Ready.Endpoints.IsEmpty());TestEqual(TEXT("Multi-source preflight readonly"),BuildingOutlineCopySnapshot(B),Original);
  auto Repeated=Endpoints;Repeated.Last()=Repeated[0];const auto Refused=EHBWallCreationCommand::CommitAnchoredPath(B,Repeated,bClosed,Options,true);
  TestEqual(TEXT("Repeated identical endpoint is a collapsed path"),Refused.Status,FName(TEXT("CollapsedEdge")));TestEqual(TEXT("Unsupported repeated source readonly"),BuildingOutlineCopySnapshot(B),Original);
  int32 Events=0,Commits=0;auto* Observer=NewObject<UEHBChangeNotificationTestObserver>(B);
  Observer->Observe=[&](FGuid,FName,bool){++Events;TestFalse(TEXT("Multi-source geometry observer outside transaction"),GEditor->IsTransactionActive());};
  Observer->ObserveCommit=[&](const FEHBCommittedEdit& Edit)
  {
   ++Commits;TestFalse(TEXT("Multi-source receipt outside transaction"),GEditor->IsTransactionActive());
   for(const auto& Id:SourceIds)TestNull(TEXT("Every source removed before publication"),B->FindElementActorByGuid(Id));
   for(int32 I=0;I<Windows.Num();++I)TestTrue(TEXT("Every hosted pose final before publication"),Windows[I]->GetActorTransform().Equals(Poses[I],0.001));
   TestEqual(TEXT("Multi-source reentry refused"),Commit().Status,FName(TEXT("BuildingChangePublicationBusy")));
  };
  B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
  auto CheckFailure=[&]()
  {
   const auto Failed=Commit();TestEqual(TEXT("Compound failure fully rolls back"),Failed.Status,FName(TEXT("SplitFailedRolledBack")));
   TestTrue(TEXT("Failed compound has no invalid result actors"),Failed.Walls.IsEmpty()&&Failed.Endpoints.IsEmpty()&&!Failed.PrimaryWall);
   TestEqual(TEXT("All source windows, walls, rail, baseline restored"),BuildingOutlineCopySnapshot(B),Original);
   TestEqual(TEXT("No intermediate event escaped"),Events,0);TestEqual(TEXT("No failed receipt escaped"),Commits,0);
  };
  for(int32 Split:{1,2}){EHBWallCreationCommand::FailAfterSplit=Split;CheckFailure();}
  for(int32 Edge=1;Edge<=(bClosed?4:1);++Edge){EHBWallCreationCommand::FailAfterEdge=Edge;CheckFailure();}
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord}){EHBWallSplitTestHooks::FailurePhase=Phase;CheckFailure();}
  const auto Applied=Commit();if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
  TestEqual(TEXT("One receipt for every split and edge"),Commits,1);TestTrue(TEXT("Final geometry published"),Events>0);
  TestEqual(TEXT("Expected final topology count"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Walls.Num(),bClosed?10:7);
  TestEqual(TEXT("Returned route includes reused boundary segments"),Applied.Walls.Num(),bClosed?6:1);
  TestEqual(TEXT("All resolved endpoints returned"),Applied.Endpoints.Num(),Endpoints.Num());
  for(int32 I=0;I<Windows.Num();++I)
  {
   auto* Window=Windows[I];auto* Host=Cast<AEHB_Wall>(B->FindElementActorByGuid(Window->OwningWallGuid));
   TestTrue(TEXT("Each window cut identity retained"),Host&&Host->CutOperations.ContainsByPredicate([&](const auto& C){return C.OperationGuid==CutIds[I];}));
   TestTrue(TEXT("Each hosted relation identity retained"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==HostIds[I]&&R.Source.RefersToElement(Window->OwningWallGuid);}));
  }
  const auto Final=BuildingOutlineCopySnapshot(B);const auto Receipt=Applied.CommittedEdit;
  Observer->Observe=nullptr;Observer->ObserveCommit=nullptr;
  TestTrue(TEXT("Single undo restores every split and path edge"),GEditor->UndoTransaction());TestEqual(TEXT("Whole command undo"),BuildingOutlineCopySnapshot(B),Original);TestEqual(TEXT("Original receipt restored"),B->LastCommittedEdit.StateId,OriginalRecord.StateId);
  TestTrue(TEXT("Single redo restores whole anchored path"),GEditor->RedoTransaction());TestEqual(TEXT("Whole command redo"),BuildingOutlineCopySnapshot(B),Final);TestEqual(TEXT("Receipt identity survives redo"),B->LastCommittedEdit.StateId,Receipt.StateId);GEditor->UndoTransaction(false);
  FEasyHouseEditorMode Mode;
  auto Release=[&]()
  {
   Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;
   Mode.WallCreationStartLocation=Endpoints[0].WorldLocation;Mode.WallCreationMouseLocation=bClosed?Endpoints[2].WorldLocation:Endpoints[1].WorldLocation;
   Mode.WallCreationStartWall=Endpoints[0].Wall;Mode.WallCreationStartWallDistance=Endpoints[0].WallDistance;
   if(bClosed)return Mode.FinishWallCreationRectangleDrag();
   Mode.HoveredWallCreationWall=Endpoints[1].Wall;Mode.HoveredWallCreationWallDistance=Endpoints[1].WallDistance;
   return Mode.FinishWallCreationDrag();
  };
  EHBWallCreationCommand::FailAfterSplit=2;TestFalse(TEXT("Actual release failure after second split"),Release());TestEqual(TEXT("Actual release does not leave first split"),BuildingOutlineCopySnapshot(B),Original);
  TestTrue(TEXT("Actual anchored release commits"),Release());TestEqual(TEXT("Actual release uses same command"),B->LastCommittedEdit.Command,Receipt.Command);
  Mode.CancelWallCreation();GEditor->UndoTransaction(false);TestEqual(TEXT("Actual anchored release undo"),BuildingOutlineCopySnapshot(B),Original);
 }
 return true;
}

namespace
{
 // ClosedLoops is transient. Cross-process evidence compares oriented paired cycles,
 // while in-session tests still verify that an existing cache keeps its exact start.
 FString NormalizeAnchoredRoomCycles(const FString& Text)
 {
  TSharedPtr<FJsonObject> Data;
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data.IsValid())return Text;
  const TArray<TSharedPtr<FJsonValue>>* Rooms=nullptr;
  if(!Data->TryGetArrayField(TEXT("rooms"),Rooms))return Text;
  for(const auto& Value:*Rooms)
  {
   const auto Room=Value->AsObject();if(!Room.IsValid())return Text;
   const TArray<TSharedPtr<FJsonValue>> *Pillars=nullptr,*Walls=nullptr;
   if(!Room->TryGetArrayField(TEXT("pillarGuids"),Pillars)||!Room->TryGetArrayField(TEXT("wallGuids"),Walls)||Pillars->Num()<3||Pillars->Num()!=Walls->Num())return Text;
   int32 First=0;for(int32 I=1;I<Pillars->Num();++I)if((*Pillars)[I]->AsString()<(*Pillars)[First]->AsString())First=I;
   TArray<TSharedPtr<FJsonValue>> P,W;
   for(int32 I=0;I<Pillars->Num();++I){P.Add((*Pillars)[(I+First)%Pillars->Num()]);W.Add((*Walls)[(I+First)%Walls->Num()]);}
   Room->SetArrayField(TEXT("pillarGuids"),P);Room->SetArrayField(TEXT("wallGuids"),W);
  }
  FString Result;FJsonSerializer::Serialize(Data.ToSharedRef(),TJsonWriterFactory<>::Create(&Result));return Result;
 }
 FString AnchoredPathPersistenceSnapshot(AEHBBuildingActorBase* B)
 {
  auto Data=MakeShared<FJsonObject>();Data->SetNumberField(TEXT("schema"),1);Data->SetStringField(TEXT("building"),B->BuildingGuid.ToString());
  Data->SetStringField(TEXT("modelAndGeometry"),BuildingOutlineCopySnapshot(B));Data->SetObjectField(TEXT("receipt"),FJsonObjectConverter::UStructToJsonObject(B->LastCommittedEdit));
  auto Rooms=B->ClosedLoops;Rooms.Sort([](const auto& A,const auto& C){return A.LoopGuid<C.LoopGuid;});TArray<TSharedPtr<FJsonValue>> RoomValues;
  for(const auto& R:Rooms)RoomValues.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(R)));Data->SetArrayField(TEXT("rooms"),RoomValues);
  auto Elements=B->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& C){return A.ElementGuid<C.ElementGuid;});TArray<TSharedPtr<FJsonValue>> Hosted;
  for(auto* E:Elements)if(auto* Door=Cast<AEHB_DoorWindow>(E))
  {
   auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("id"),Door->ElementGuid.ToString());V->SetStringField(TEXT("wall"),Door->OwningWallGuid.ToString());
   V->SetNumberField(TEXT("distance"),Door->DistanceFromWallStart);V->SetStringField(TEXT("pose"),Door->GetActorTransform().ToString());
   if(auto* Wall=Cast<AEHB_Wall>(B->FindElementActorByGuid(Door->OwningWallGuid)))for(const auto& Cut:Wall->CutOperations)if(Cut.Source.SourceElementGuid==Door->ElementGuid)
   {
    V->SetStringField(TEXT("cut"),Cut.OperationGuid.ToString());TArray<TSharedPtr<FJsonValue>> Points;
    for(const auto& Point:Cut.Source.ExplicitPolygon.Points)Points.Add(MakeShared<FJsonValueString>(Point.PointGuid.ToString()));V->SetArrayField(TEXT("cutPoints"),Points);
   }
   Hosted.Add(MakeShared<FJsonValueObject>(V));
  }
  Data->SetArrayField(TEXT("windows"),Hosted);FString Text;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Text));return NormalizeCopySnapshotText(Text);
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomCycleEvidenceTest,"EHB.Topology.RoomCycleEvidence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomCycleEvidenceTest::RunTest(const FString& Parameters)
{
 auto Snapshot=[](const TArray<FString>& P,const TArray<FString>& W,int32 Floor=1)
 {
  auto D=MakeShared<FJsonObject>(),R=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Ps,Ws;
  for(const auto& V:P)Ps.Add(MakeShared<FJsonValueString>(V));for(const auto& V:W)Ws.Add(MakeShared<FJsonValueString>(V));
  R->SetArrayField(TEXT("pillarGuids"),Ps);R->SetArrayField(TEXT("wallGuids"),Ws);R->SetNumberField(TEXT("floor"),Floor);
  D->SetArrayField(TEXT("rooms"),{MakeShared<FJsonValueObject>(R)});FString T;FJsonSerializer::Serialize(D,TJsonWriterFactory<>::Create(&T));return NormalizeAnchoredRoomCycles(T);
 };
 const auto Baseline=Snapshot({TEXT("A"),TEXT("B"),TEXT("C")},{TEXT("X"),TEXT("Y"),TEXT("Z")});
 TestEqual(TEXT("Paired same-direction cycle shift is equivalent"),Snapshot({TEXT("B"),TEXT("C"),TEXT("A")},{TEXT("Y"),TEXT("Z"),TEXT("X")}),Baseline);
 TestNotEqual(TEXT("Reversed cycle rejected"),Snapshot({TEXT("A"),TEXT("C"),TEXT("B")},{TEXT("Z"),TEXT("Y"),TEXT("X")}),Baseline);
 TestNotEqual(TEXT("Wall pairing change rejected"),Snapshot({TEXT("A"),TEXT("B"),TEXT("C")},{TEXT("Y"),TEXT("Z"),TEXT("X")}),Baseline);
 TestNotEqual(TEXT("Identity change rejected"),Snapshot({TEXT("A"),TEXT("B"),TEXT("D")},{TEXT("X"),TEXT("Y"),TEXT("Z")}),Baseline);
 TestNotEqual(TEXT("Other room metadata retained"),Snapshot({TEXT("A"),TEXT("B"),TEXT("C")},{TEXT("X"),TEXT("Y"),TEXT("Z")},2),Baseline);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBAnchoredPathPersistenceWriteTest,"EHBValidation.Persistence.WriteAnchoredPath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBAnchoredPathPersistenceWriteTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("AnchoredPath"))){AddError(TEXT("Explicit isolated anchored-path map required"));return false;}
 if(FPackageName::DoesPackageExist(MapPath)){AddError(TEXT("Refusing to overwrite existing persistence map"));return false;}
 UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));TArray<FEHBWallCreationEndpoint> Anchors;
 const bool bRepeatedSource=FParse::Param(FCommandLine::Get(),TEXT("EHBRepeatedSourcePersistence"));
 for(int32 I:{0,2})
 {
  auto* Source=Fixture.Walls[bRepeatedSource?0:I];const float Distance=bRepeatedSource?(I==0?180.f:360.f):FVector::Dist2D(Source->LocalStart,Source->LocalEnd)*0.5f;
  TArray<float> WindowDistances;if(!bRepeatedSource)WindowDistances={125.f};else if(I==0)WindowDistances={90.f,270.f,450.f};
  for(float WindowDistance:WindowDistances)
  {
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=World->SpawnActor<AEHB_DoorWindow>(Params);if(!TestNotNull(TEXT("Persistent window created"),Window))return false;Fixture.Actors.Add(Window);
   Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
   Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(WindowDistance,50),Source->GetActorQuat());Window->BindToWall(Source,WindowDistance);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
  }
  const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Source->ElementGuid,Distance);if(!TestTrue(*Split.Status.ToString(),Split.bSucceeded))return false;
  auto& A=Anchors.AddDefaulted_GetRef();A.Wall=Source;A.WallDistance=Distance;A.LocalLocation=Split.LocalPillarPosition;A.WorldLocation=B->GetActorTransform().TransformPosition(A.LocalLocation);
 }
 UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);UEHBBuildingToolset::MigrateWallNodeOwnership(B,true);
 auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("PersistenceAnchoredRail"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
 if(!TestNotNull(TEXT("Persistent retained railing created"),Rail))return false;Fixture.Actors.Add(Rail);
 auto Bottom=Anchors[0],Top=Anchors[1];Bottom.Wall=Top.Wall=nullptr;
 if(bRepeatedSource)Bottom.LocalLocation.Y=Top.LocalLocation.Y=-300;else Bottom.LocalLocation.X=Top.LocalLocation.X=850;
 Bottom.WorldLocation=B->GetActorTransform().TransformPosition(Bottom.LocalLocation);Top.WorldLocation=B->GetActorTransform().TransformPosition(Top.LocalLocation);
 const TArray<FEHBWallCreationEndpoint> Endpoints=bRepeatedSource?TArray<FEHBWallCreationEndpoint>{Anchors[0],Anchors[1],Top,Bottom}:TArray<FEHBWallCreationEndpoint>{Anchors[0],Bottom,Top,Anchors[1]};
 const auto Applied=EHBWallCreationCommand::CommitAnchoredPath(B,Endpoints,true,FEHBWallCreationOptions());
 if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 TestEqual(TEXT("Persistent rectangle uses combined receipt"),Applied.CommittedEdit.Command,FName(TEXT("CreateAnchoredClosedWallPath")));
 B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery())){E->ClearFlags(RF_Transient);Fixture.Actors.AddUnique(E);}
 if(!TestTrue(TEXT("Save new anchored rectangle map"),UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)))return false;
 TestTrue(TEXT("Save anchored geometry identities rooms receipt and window evidence"),FFileHelper::SaveStringToFile(AnchoredPathPersistenceSnapshot(B),*EvidencePath));Fixture.Actors.Reset();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBAnchoredPathPersistenceReadTest,"EHBValidation.Persistence.ReadAnchoredPath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBAnchoredPathPersistenceReadTest::RunTest(const FString& Parameters)
{
 FString MapPath,EvidencePath;if(!PersistencePaths(MapPath,EvidencePath,TEXT("AnchoredPath")))return false;
 UWorld* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=MapPath)return false;
 AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHB_Building> It(World);It;++It){if(B){AddError(TEXT("Unexpected extra building"));return false;}B=*It;}
 if(!TestNotNull(TEXT("Saved anchored building loaded"),B))return false;
 FString Expected;if(!FFileHelper::LoadFileToString(Expected,*EvidencePath)){AddError(TEXT("Missing anchored-path evidence"));return false;}
 Expected=NormalizeAnchoredRoomCycles(Expected);
 TestEqual(TEXT("Independent load preserves model geometry rooms windows and receipt"),NormalizeAnchoredRoomCycles(AnchoredPathPersistenceSnapshot(B)),Expected);
 B->RebuildElementAndRelationshipIndexes();B->RebuildClosedLoops();
 for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* P=Cast<AEHB_Pillar>(E))P->RebuildPillarMesh();
 for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* W=Cast<AEHB_Wall>(E))W->RebuildWallMesh();else if(auto* R=Cast<AEHB_Railing>(E))R->RebuildRailing();}
 TestEqual(TEXT("Reconstruction preserves complete saved anchored model"),NormalizeAnchoredRoomCycles(AnchoredPathPersistenceSnapshot(B)),Expected);
 TestEqual(TEXT("Loaded multi-source baseline coherent"),UEHBWallTopologyLibrary::PrepareTopologyMigration(B,false).Status,FName(TEXT("AlreadyInitialized")));
 FEHBWallCreationEndpoint A,C;
 // Simulate a new outward user request from the rightmost upper column after reload.
 for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* P=Cast<AEHB_Pillar>(E))
 {
  const auto Position=P->GetElementLocalTransform().GetLocation();
  if(!A.Pillar || Position.X>A.LocalLocation.X || (FMath::IsNearlyEqual(Position.X,A.LocalLocation.X,0.001)&&Position.Y>A.LocalLocation.Y))
  {A.Pillar=P;A.LocalLocation=Position;}
 }
 if(!TestNotNull(TEXT("Loaded outward endpoint resolved for new user request"),A.Pillar))return false;
 A.WorldLocation=B->GetActorTransform().TransformPosition(A.LocalLocation);
 C.LocalLocation=A.LocalLocation+FVector(200,250,0);C.WorldLocation=B->GetActorTransform().TransformPosition(C.LocalLocation);
 const auto Edited=EHBWallCreationCommand::Commit(B,{A,C},false,FEHBWallCreationOptions());if(!TestTrue(*Edited.Status.ToString(),Edited.bSucceeded))return false;
 TestTrue(TEXT("Undo continued editing after reload"),GEditor->UndoTransaction(false));TestEqual(TEXT("Continued editing undo preserves original saved command"),NormalizeAnchoredRoomCycles(AnchoredPathPersistenceSnapshot(B)),Expected);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallSplitIntervalPlanningTest,"EHB.Topology.WallSplitIntervalPlanning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallSplitIntervalPlanningTest::RunTest(const FString& Parameters)
{
 const FGuid Source(1,0,0,1),A(2,0,0,1),B(2,0,0,2),C(2,0,0,3);
 TArray<FEHBWallSplitIntervalRequest> Requests={{Source,FVector::ZeroVector,FVector(600,0,0),200,20},{Source,FVector::ZeroVector,FVector(600,0,0),400,20}};
 const TArray<FEHBWallSplitOpeningInterval> Openings={{Source,A,100,40},{Source,B,300,40},{Source,C,500,40}};
 const auto Plan=FEHBWallSplitIntervalPlanning::Build(Requests,Openings);if(!TestTrue(TEXT("Pure repeated-source plan succeeds"),Plan.bSucceeded))return false;
 TestEqual(TEXT("Both operations planned"),Plan.Steps.Num(),2);TestEqual(TEXT("Farther split first retains original start"),Plan.Steps[0].RequestIndex,1);
 TestEqual(TEXT("First split moves all windows"),Plan.Steps[0].Openings.Num(),3);TestEqual(TEXT("Second split consumes only remaining windows"),Plan.Steps[1].Openings.Num(),2);
 TestTrue(TEXT("Remaining end includes earlier pillar clearance"),Plan.Steps[1].EffectiveSourceEnd.Equals(FVector(390,0,0)));
 const auto* Middle=Plan.Steps[1].Openings.FindByPredicate([&](const auto& O){return O.OpeningGuid==B;});
 TestTrue(TEXT("Middle window maps to bounded middle segment"),Middle&&Middle->bAfter&&Middle->NewDistance==90&&Middle->TargetStart.Equals(FVector(210,0,0))&&Middle->TargetEnd.Equals(FVector(390,0,0)));
 Swap(Requests[0],Requests[1]);const auto Reversed=FEHBWallSplitIntervalPlanning::Build(Requests,Openings);
 TestTrue(TEXT("Input permutation keeps split geometry order"),Reversed.bSucceeded&&Reversed.Steps[0].RequestIndex==0&&Reversed.Steps[1].EffectiveSourceEnd==Plan.Steps[1].EffectiveSourceEnd);
 auto CheckFailure=[&](const auto& R,const auto& O,FName Expected){const auto Failed=FEHBWallSplitIntervalPlanning::Build(R,O);TestEqual(TEXT("Invalid interval reason"),Failed.Status,Expected);TestTrue(TEXT("No partial intervals escape failure"),!Failed.bSucceeded&&Failed.Steps.IsEmpty());};
 auto Bad=Requests;Bad[1].Distance=Bad[0].Distance;CheckFailure(Bad,Openings,TEXT("OverlappingSplitPillars"));
 Bad=Requests;Bad[1].ColumnWidth=30;CheckFailure(Bad,Openings,TEXT("InconsistentSplitSource"));
 auto BadOpenings=Openings;BadOpenings[0].Distance=200;CheckFailure(Requests,BadOpenings,TEXT("OpeningIntersectsPillar"));
 BadOpenings=Openings;BadOpenings[0].SourceGuid=FGuid::NewGuid();CheckFailure(Requests,BadOpenings,TEXT("InvalidSplitOpening"));
 BadOpenings=Openings;BadOpenings.Add(Openings[0]);CheckFailure(Requests,BadOpenings,TEXT("InvalidSplitOpening"));
 Bad=Requests;Bad[1].Distance=std::numeric_limits<float>::quiet_NaN();CheckFailure(Bad,Openings,TEXT("InvalidSplitSource"));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRepeatedWallAnchorsTest,"EHB.Topology.RepeatedWallAnchors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRepeatedWallAnchorsTest::RunTest(const FString& Parameters)
{
 for(int32 Scenario=0;Scenario<3;++Scenario)
 {
  const bool bClosed=Scenario==2;
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor?GEditor->GetEditorWorldContext().World():nullptr,RF_Transactional))return false;
  auto* B=Fixture.Building;auto* Source=Fixture.Walls[0];B->SetActorRotation(FRotator(0,37,0));
  TArray<AEHB_DoorWindow*> Windows;TArray<FGuid> CutIds;TArray<FTransform> Poses;
  for(float Distance:{90.f,270.f,450.f})
  {
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);
   if(!TestNotNull(TEXT("Repeated-source window created"),Window))return false;Fixture.Actors.Add(Window);
   Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
   Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(Distance,50),Source->GetActorQuat());Window->BindToWall(Source,Distance);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
   Windows.Add(Window);Poses.Add(Window->GetActorTransform());CutIds.Add(Source->CutOperations.Last().OperationGuid);
  }
  TArray<FEHBWallCreationEndpoint> Ends;
  for(float Distance:{180.f,360.f})
  {
   const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Source->ElementGuid,Distance);if(!TestTrue(*Split.Status.ToString(),Split.bSucceeded))return false;
   auto& E=Ends.AddDefaulted_GetRef();E.Wall=Source;E.WallDistance=Distance;E.LocalLocation=Split.LocalPillarPosition;E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);
  }
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("RepeatedAnchorRail"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
  if(!TestNotNull(TEXT("Repeated-source retained rail created"),Rail))return false;Fixture.Actors.Add(Rail);
  if(bClosed){UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);UEHBBuildingToolset::MigrateWallNodeOwnership(B,true);if(!TestEqual(TEXT("Repeated-source migrated fixture"),B->WallNodeOwnership.Version,1))return false;}
  if(Scenario==1)Swap(Ends[0],Ends[1]);
  if(bClosed)
  {
   auto C=Ends[1],D=Ends[0];C.Wall=D.Wall=nullptr;C.LocalLocation.Y=D.LocalLocation.Y=-300;
   C.WorldLocation=B->GetActorTransform().TransformPosition(C.LocalLocation);D.WorldLocation=B->GetActorTransform().TransformPosition(D.LocalLocation);Ends.Add(C);Ends.Add(D);
  }
  GEditor->SelectNone(false,true,false);FEHBWallCreationOptions Options;
  const auto Before=BuildingOutlineCopySnapshot(B);const auto ReceiptBefore=B->LastCommittedEdit;
  auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::CommitAnchoredPath(B,Ends,bClosed,Options,Preview);};
  const auto Ready=Commit(true);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;TestEqual(TEXT("Repeated-source preview readonly"),BuildingOutlineCopySnapshot(B),Before);
  int32 Commits=0,Events=0;auto* Observer=NewObject<UEHBChangeNotificationTestObserver>(B);
  Observer->Observe=[&](FGuid,FName,bool){++Events;};Observer->ObserveCommit=[&](const FEHBCommittedEdit&){++Commits;TestFalse(TEXT("Repeated-source publishes outside transaction"),GEditor->IsTransactionActive());};
  B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
  auto Failure=[&](){const auto Failed=Commit();TestEqual(TEXT("Repeated-source whole rollback"),Failed.Status,FName(TEXT("SplitFailedRolledBack")));TestTrue(TEXT("Repeated-source failure clears actors"),Failed.Walls.IsEmpty()&&Failed.Endpoints.IsEmpty()&&!Failed.PrimaryWall);TestEqual(TEXT("All three windows and original source restored"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("No intermediate receipt"),Commits,0);TestEqual(TEXT("No intermediate relationship event"),Events,0);};
  for(int32 Split:{1,2}){EHBWallCreationCommand::FailAfterSplit=Split;Failure();}
  for(int32 Edge=1;Edge<=(bClosed?4:1);++Edge){EHBWallCreationCommand::FailAfterEdge=Edge;Failure();}
  EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord;Failure();
  const auto Applied=Commit();if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
  TestEqual(TEXT("One repeated-source command"),Commits,1);TestEqual(TEXT("Requested path walls include reused middle segment"),Applied.Walls.Num(),bClosed?4:1);
  TestEqual(TEXT("Source replaced by three bounded intervals"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Walls.Num(),bClosed?9:6);
  TestFalse(TEXT("Consumed intermediate wall never leaks into net receipt"),Applied.CommittedEdit.Elements.ContainsByPredicate([](const auto& E){return !E.BeforeBounds.IsValid&&!E.AfterBounds.IsValid;}));
  TestTrue(TEXT("Original removed source retains its before bounds"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==Source->ElementGuid&&E.BeforeBounds.IsValid&&!E.AfterBounds.IsValid;}));
  TSet<FGuid> Hosts;
  for(int32 I=0;I<Windows.Num();++I)
  {
   auto* W=Windows[I];auto* Host=Cast<AEHB_Wall>(B->FindElementActorByGuid(W->OwningWallGuid));Hosts.Add(W->OwningWallGuid);
   TestTrue(TEXT("Each window retains its world pose"),W->GetActorTransform().Equals(Poses[I],0.001));
   TestTrue(TEXT("Each interval preserves cut identity"),Host&&Host->CutOperations.ContainsByPredicate([&](const auto& C){return C.OperationGuid==CutIds[I];}));
  }
  TestEqual(TEXT("Three windows finish on three distinct wall segments"),Hosts.Num(),3);
  TestTrue(TEXT("Middle window uses returned reused wall"),Applied.Walls.ContainsByPredicate([&](const auto* W){return W->ElementGuid==Windows[1]->OwningWallGuid;}));
  // Simulate a saved room whose valid cycle starts on a different edge than a fresh extraction.
  for(auto& Room:B->ClosedLoops)if(Room.PillarGuids.Num()>1&&Room.PillarGuids.Num()==Room.WallGuids.Num())
  {const auto P=Room.PillarGuids,W=Room.WallGuids;for(int32 I=0;I<P.Num();++I){Room.PillarGuids[I]=P[(I+1)%P.Num()];Room.WallGuids[I]=W[(I+1)%W.Num()];}}
  const auto SavedRooms=B->ClosedLoops;B->RebuildElementAndRelationshipIndexes();B->RebuildClosedLoops();
  for(const auto& Saved:SavedRooms)
  {
   const auto* Restored=B->ClosedLoops.FindByPredicate([&](const auto& Room){return Room.LoopGuid==Saved.LoopGuid;});
   TestTrue(TEXT("Room cache rebuild retains saved paired boundary order"),Restored&&Restored->PillarGuids==Saved.PillarGuids&&Restored->WallGuids==Saved.WallGuids);
  }
  const auto After=BuildingOutlineCopySnapshot(B);const auto Receipt=Applied.CommittedEdit;Observer->Observe=nullptr;Observer->ObserveCommit=nullptr;
  TestTrue(TEXT("Undo full repeated-source command"),GEditor->UndoTransaction());TestEqual(TEXT("Repeated-source undo geometry"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Undo receipt"),B->LastCommittedEdit.StateId,ReceiptBefore.StateId);
  TestTrue(TEXT("Redo repeated-source command"),GEditor->RedoTransaction());TestEqual(TEXT("Repeated-source redo geometry"),BuildingOutlineCopySnapshot(B),After);TestEqual(TEXT("Redo receipt"),B->LastCommittedEdit.StateId,Receipt.StateId);GEditor->UndoTransaction(false);
  FEasyHouseEditorMode Mode;auto Release=[&]()
  {
   Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=Ends[0].WorldLocation;Mode.WallCreationMouseLocation=bClosed?Ends[2].WorldLocation:Ends[1].WorldLocation;
   Mode.WallCreationStartWall=Ends[0].Wall;Mode.WallCreationStartWallDistance=Ends[0].WallDistance;
   Mode.HoveredWallCreationWall=Ends[1].Wall;Mode.HoveredWallCreationWallDistance=Ends[1].WallDistance;
   return bClosed?Mode.FinishWallCreationRectangleDrag():Mode.FinishWallCreationDrag();
  };
  EHBWallCreationCommand::FailAfterSplit=2;TestFalse(TEXT("Actual same-source release failure"),Release());TestEqual(TEXT("Actual same-source release no legacy fallback"),BuildingOutlineCopySnapshot(B),Before);
  TestTrue(TEXT("Actual same-source release commits"),Release());TestEqual(TEXT("Actual same-source receipt"),B->LastCommittedEdit.Command,Receipt.Command);Mode.CancelWallCreation();GEditor->UndoTransaction(false);TestEqual(TEXT("Actual same-source release undo"),BuildingOutlineCopySnapshot(B),Before);
 }
 return true;
}

namespace
{
 // The complete evidence remains 17-digit text. Serialized actor quaternion
 // composition may change derived geometry by a few double ULPs. This audit
 // applies an absolute 1e-12 bound ONLY to declared geometry scalar arrays;
 // IDs, indices, array sizes/order, flags, colors and all model text stay exact.
 bool SubdivisionMeshValues(const TSharedPtr<FJsonValue>& A,const TSharedPtr<FJsonValue>& B,const FString& Path,int32& Rounded)
 {
  if(!A||!B||A->Type!=B->Type)return false;
  if(A->Type==EJson::Object)
  {
   const auto X=A->AsObject(),Y=B->AsObject();if(X->Values.Num()!=Y->Values.Num())return false;
   for(const auto& P:X->Values){const auto* V=Y->Values.Find(P.Key);if(!V||!SubdivisionMeshValues(P.Value,*V,Path/TEXT("")+P.Key,Rounded))return false;}return true;
  }
  if(A->Type==EJson::Array)
  {
   const auto& X=A->AsArray();const auto& Y=B->AsArray();if(X.Num()!=Y.Num())return false;
   for(int32 I=0;I<X.Num();++I)if(!SubdivisionMeshValues(X[I],Y[I],Path/FString::FromInt(I),Rounded))return false;return true;
  }
  if(A->Type==EJson::String)
  {
   const FString X=A->AsString(),Y=B->AsString();if(X==Y)return true;
   TArray<FString> Parts;Path.ParseIntoArray(Parts,TEXT("/"));if(Parts.Num()<2)return false;
   const int32 Index=FCString::Atoi(*Parts.Last());const FString Parent=Parts[Parts.Num()-2];
   const bool Pose=(Parent==TEXT("localPose")||Parent==TEXT("relativePose")||Parent==TEXT("pose"))&&Index<10;
   const bool Bounds=Parent==TEXT("bounds")&&Index>0&&Index<7;
   const bool Vertex=Parts.Num()>2&&Parts[Parts.Num()-3]==TEXT("vertices")&&Index<17;
   if(!Pose&&!Bounds&&!Vertex)return false;
   double U=0,V=0;if(!LexTryParseString(U,*X)||!LexTryParseString(V,*Y)||!FMath::IsFinite(U)||!FMath::IsFinite(V)||FMath::Abs(U-V)>1e-12)return false;
   ++Rounded;return true;
  }
  if(A->Type==EJson::Number)return A->AsNumber()==B->AsNumber();
  if(A->Type==EJson::Boolean)return A->AsBool()==B->AsBool();
  return A->Type==EJson::Null;
 }
 bool SubdivisionMeshEvidenceMatches(const FString& Expected,const FString& Actual,int32& Rounded)
 {
  TSharedPtr<FJsonObject> A,B;Rounded=0;
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected),A)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Actual),B))return false;
  return SubdivisionMeshValues(MakeShared<FJsonValueObject>(A),MakeShared<FJsonValueObject>(B),TEXT(""),Rounded);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSubdivisionMeshAuditTest,"EHB.Topology.SubdivisionMeshEvidenceAudit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSubdivisionMeshAuditTest::RunTest(const FString& Parameters)
{
 int32 Rounded=0;
 auto Scalar=[&](FString X,FString Y,FString Path){return SubdivisionMeshValues(MakeShared<FJsonValueString>(X),MakeShared<FJsonValueString>(Y),Path,Rounded);};
 TestTrue(TEXT("Double ULP coordinate rounding accepted"),Scalar(TEXT("10"),TEXT("10.000000000000005"),TEXT("/vertices/0/0")));
 TestFalse(TEXT("Coordinate drift above absolute tolerance rejected"),Scalar(TEXT("10"),TEXT("10.0000000001"),TEXT("/vertices/0/0")));
 TestFalse(TEXT("Color changes never treated as floating geometry"),Scalar(TEXT("1"),TEXT("1.000000000000001"),TEXT("/vertices/0/18")));
 TestFalse(TEXT("Identity strings remain exact"),Scalar(TEXT("1"),TEXT("1.000000000000001"),TEXT("/id")));
 TestFalse(TEXT("Nonfinite coordinates rejected"),Scalar(TEXT("1"),TEXT("nan"),TEXT("/vertices/0/0")));
 TestFalse(TEXT("Index buffers remain exact"),SubdivisionMeshEvidenceMatches(TEXT("{\"indices\":[0,1,2]}"),TEXT("{\"indices\":[0,2,1]}"),Rounded));
 TestFalse(TEXT("Vertex count stays exact"),SubdivisionMeshEvidenceMatches(TEXT("{\"vertices\":[[\"1\"]]}"),TEXT("{\"vertices\":[]}"),Rounded));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSubdivisionWriteTest,"EHBValidation.Persistence.WriteRoomSubdivision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSubdivisionWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("RoomSubdivision")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite subdivision evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
 auto* Wall=Fixture.Walls[0];const FGuid Start=Wall->StartPillarGuid,OldId=Wall->ElementGuid;
 for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E))if(B->GetClosedLoopsByWallGuid(OldId).ContainsByPredicate([&](const auto& R){return R.LoopGuid==S->RoomFillLoopGuid;})){S->RoomFillAnchorWallGuid=OldId;S->RoomFillAnchorWallSide=EEHBFloorSlabWallSide::Left;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
 const bool Ordinary=FParse::Param(FCommandLine::Get(),TEXT("EHBOrdinaryPathPersistence"));
 const bool OrdinaryHosts=FParse::Param(FCommandLine::Get(),TEXT("EHBOrdinaryHostsPersistence"));
 const bool NodeAuthority=FParse::Param(FCommandLine::Get(),TEXT("EHBNodeAuthorityPersistence"));
 if(NodeAuthority&&!TestTrue(TEXT("Activate node authority before subdivision and hosts"),B->MigrateWallNodeAuthority(true).bSucceeded))return false;
 if(!Ordinary||OrdinaryHosts)
 {
 auto* OpeningHost=Wall;
 if(OrdinaryHosts)for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))if(W->LocalStart.X>900&&W->LocalEnd.X>900)OpeningHost=W;
 FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=World->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
 Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
 Window->SetActorLocationAndRotation(OpeningHost->GetWorldLocationOnCenterAxisAtDistance(100,50),OpeningHost->GetActorQuat());Window->BindToWall(OpeningHost,100);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("SubdivisionSavedRail"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);if(!Rail)return false;Fixture.Actors.Add(Rail);
 }
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,OldId,250);if(!Split.bSucceeded)return false;
 const bool Branch=FParse::Param(FCommandLine::Get(),TEXT("EHBWallBranchPersistence"));
 const bool Multi=FParse::Param(FCommandLine::Get(),TEXT("EHBMultiAnchorPersistence"));
 const bool Partition=FParse::Param(FCommandLine::Get(),TEXT("EHBPartitionPersistence"));
 if(Ordinary)
 {
  TArray<FEHBWallCreationEndpoint> Points;for(auto* P:{Fixture.Pillars[0],Fixture.Pillars[2]}){auto& E=Points.AddDefaulted_GetRef();E.Pillar=P;E.LocalLocation=P->GetElementLocalTransform().GetLocation();E.WorldLocation=P->GetActorLocation();E.FloorIndex=1;}
  const auto Applied=EHBWallCreationCommand::Commit(B,Points,false,FEHBWallCreationOptions());if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 }
 else if(Partition)
 {
  FEHBWallCreationEndpoint A,Z;A.Wall=Wall;A.WallDistance=250;A.LocalLocation=Split.LocalPillarPosition;
  Z.Wall=Fixture.Walls[2];Z.WallDistance=FMath::Abs(Z.Wall->LocalStart.X-A.LocalLocation.X);
  const auto Other=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Z.Wall->ElementGuid,Z.WallDistance);if(!Other.bSucceeded)return false;Z.LocalLocation=Other.LocalPillarPosition;
  for(auto* E:{&A,&Z}){E->WorldLocation=B->GetActorTransform().TransformPosition(E->LocalLocation);E->FloorIndex=1;}
  const auto Applied=EHBWallCreationCommand::CommitAnchoredPath(B,{A,Z},false,FEHBWallCreationOptions());if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 }
 else if(Multi)
 {
  FEHBWallCreationEndpoint A,Z,C,D;A.Wall=Z.Wall=Wall;A.WallDistance=250;Z.WallDistance=400;
  A.LocalLocation=Split.LocalPillarPosition;const auto Other=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,OldId,400);if(!Other.bSucceeded)return false;Z.LocalLocation=Other.LocalPillarPosition;
  C.LocalLocation=A.LocalLocation+FVector(0,-300,0);D.LocalLocation=Z.LocalLocation+FVector(0,-300,0);
  for(auto* E:{&A,&Z,&C,&D}){E->WorldLocation=B->GetActorTransform().TransformPosition(E->LocalLocation);E->FloorIndex=1;}
  const auto Applied=EHBWallCreationCommand::CommitAnchoredPath(B,{A,Z,D,C},true,FEHBWallCreationOptions());if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 }
 else if(Branch)
 {
  FEHBWallCreationEndpoint Anchor,End;Anchor.Wall=Wall;Anchor.WallDistance=250;Anchor.LocalLocation=Split.LocalPillarPosition;End.LocalLocation=Anchor.LocalLocation+FVector(150,-300,0);
  Anchor.FloorIndex=End.FloorIndex=1;Anchor.WorldLocation=B->GetActorTransform().TransformPosition(Anchor.LocalLocation);End.WorldLocation=B->GetActorTransform().TransformPosition(End.LocalLocation);
  const auto Applied=EHBWallCreationCommand::CommitFromWall(B,Anchor,End,FEHBWallCreationOptions());if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 }
 else
 {
  const auto Applied=UEHBBuildingToolset::CommitWallSplitAndRailing(B,OldId,250,B->RelationshipGraphRevision,Wall->LocalStart,Wall->LocalEnd,Wall->Height,Wall->Thickness,B->GetActorTransform().TransformPosition(Split.LocalPillarPosition+FVector(0,-300,0)),100,5,80,false);
  if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
 }
 B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);
 if(!TestTrue(TEXT("Save subdivided room floors slabs window and railings"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.RoomSubdivisionPersistence.v1"));Data->SetBoolField(TEXT("wallBranch"),Branch);Data->SetBoolField(TEXT("multiAnchor"),Multi);Data->SetBoolField(TEXT("partition"),Partition);Data->SetBoolField(TEXT("ordinary"),Ordinary);Data->SetBoolField(TEXT("ordinaryHosts"),OrdinaryHosts);Data->SetBoolField(TEXT("nodeAuthority"),NodeAuthority);Data->SetNumberField(TEXT("roomCount"),B->GetClosedLoopsByFloor(1).Num());
 TArray<TSharedPtr<FJsonValue>> BoundRooms;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E))BoundRooms.Add(MakeShared<FJsonValueString>(F->RoomLoopGuid.ToString()));Data->SetArrayField(TEXT("boundRooms"),BoundRooms);Data->SetStringField(TEXT("state"),NormalizeAnchoredRoomCycles(AnchoredPathPersistenceSnapshot(B)));
 Data->SetStringField(TEXT("meshes"),OutlineCopyMeshEvidence(World));Data->SetStringField(TEXT("oldWall"),OldId.ToString());Data->SetStringField(TEXT("start"),Start.ToString());
 FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save immutable subdivision evidence"),FFileHelper::SaveStringToFile(Json,*Path));Fixture.Actors.Reset();GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSubdivisionReadTest,"EHBValidation.Persistence.ReadRoomSubdivision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSubdivisionReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("RoomSubdivision"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.RoomSubdivisionPersistence.v1"))return false;
 const bool Ordinary=Data->HasField(TEXT("ordinary"))&&Data->GetBoolField(TEXT("ordinary"));
 const bool OrdinaryHosts=Data->HasField(TEXT("ordinaryHosts"))&&Data->GetBoolField(TEXT("ordinaryHosts"));
 AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHB_Building> It(World);It;++It){if(B)return false;B=*It;}if(!B)return false;
 const bool NodeAuthority=Data->HasField(TEXT("nodeAuthority"))&&Data->GetBoolField(TEXT("nodeAuthority"));
 TestEqual(TEXT("Saved node authority mode survives cold load"),B->WallNodeAuthority.Version,NodeAuthority?1:0);
 const auto Expected=Data->GetStringField(TEXT("state"));auto Snapshot=[&](){return NormalizeAnchoredRoomCycles(AnchoredPathPersistenceSnapshot(B));};
 TestEqual(TEXT("Independent subdivision load preserves all identities geometry and receipt"),Snapshot(),Expected);
 int32 Rounded=0;TestTrue(TEXT("Independent subdivision mesh evidence agrees within explicit 1e-12 scalar tolerance"),SubdivisionMeshEvidenceMatches(Data->GetStringField(TEXT("meshes")),OutlineCopyMeshEvidence(World),Rounded));AddInfo(FString::Printf(TEXT("Independent load: %d rounded geometry scalars; indices and model remain exact"),Rounded));
 FGuid OldId,Start;FGuid::Parse(Data->GetStringField(TEXT("oldWall")),OldId);FGuid::Parse(Data->GetStringField(TEXT("start")),Start);if(Ordinary)TestNotNull(TEXT("Original boundary wall retained"),B->FindElementActorByGuid(OldId));else TestNull(TEXT("Removed wall not resurrected"),B->FindElementActorByGuid(OldId));
 TArray<FEHBRoomDependencyMembers> Dependencies;TestTrue(TEXT("Reloaded subdivision rebuilds room index"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Dependencies));TestEqual(TEXT("All room dependencies restored"),Dependencies.Num(),Data->HasField(TEXT("roomCount"))?static_cast<int32>(Data->GetNumberField(TEXT("roomCount"))):2);
 for(const auto& D:Dependencies)
 {
  const bool Bound=!Data->HasField(TEXT("boundRooms"))||Data->GetArrayField(TEXT("boundRooms")).ContainsByPredicate([&](const auto& V){return V->AsString()==D.RoomGuid.ToString();});
  TestEqual(TEXT("Restored room floor membership"),D.Floors.Num(),Bound?1:0);TestEqual(TEXT("Restored room slab membership"),D.Slabs.Num(),Bound?1:0);
 }
 AEHB_Wall* Left=nullptr;
 for(auto* E:B->QueryElements(FEHBElementQuery()))
 {
  if(auto* F=Cast<AEHB_Floor>(E)){FEHBBuildingClosedLoop R;TArray<FEHBElementRelation> Plan;FName Status;TestTrue(TEXT("Loaded floor resolves stable room"),F->TryGetRoomLoop(R));TestTrue(TEXT("Loaded floor resolves migrated physical hosts"),F->BuildSurfaceFinishRelationPlan(R,F->FloorRegions,Plan,Status));TestEqual(TEXT("Loaded complete finish count"),Plan.Num(),F->SurfaceFinishRelationGuids.Num());F->RebuildFloorMesh();}
  else if(auto* S=Cast<AEHB_FloorSlab>(E)){TestTrue(TEXT("Loaded slab anchor is a live replacement wall"),(Ordinary||S->RoomFillAnchorWallGuid!=OldId)&&B->FindElementActorByGuid(S->RoomFillAnchorWallGuid)!=nullptr);S->RebuildSlabMesh();}
  else if(auto* P=Cast<AEHB_Pillar>(E))P->RebuildPillarMesh();
  else if(auto* W=Cast<AEHB_Wall>(E)){W->RebuildWallMesh();if(W->StartPillarGuid==Start&&(Ordinary?W->ElementGuid==OldId:!W->DoorWindowConnections.IsEmpty()))Left=W;}
  else if(auto* R=Cast<AEHB_Railing>(E))R->RebuildRailing();
 }
 TestEqual(TEXT("Explicit rebuild preserves full saved subdivision state"),Snapshot(),Expected);
 TestTrue(TEXT("Explicit subdivision rebuild agrees within explicit 1e-12 scalar tolerance"),SubdivisionMeshEvidenceMatches(Data->GetStringField(TEXT("meshes")),OutlineCopyMeshEvidence(World),Rounded));AddInfo(FString::Printf(TEXT("Explicit rebuild: %d rounded geometry scalars; indices and model remain exact"),Rounded));
 if(!TestNotNull(TEXT("Resolve saved boundary segment for continued edit"),Left))return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 if(OrdinaryHosts)
 {
  TArray<FEHBWallCreationEndpoint> Points;for(FVector P:{FVector(-600,-300,0),FVector(-300,-300,0)}){auto& E=Points.AddDefaulted_GetRef();E.FloorIndex=1;E.LocalLocation=P;E.WorldLocation=B->GetActorTransform().TransformPosition(P);}
  const auto Applied=EHBWallCreationCommand::Commit(B,Points,false,FEHBWallCreationOptions());if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 }
 else
 {
 const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Left->ElementGuid,170);if(!TestTrue(*Split.Status.ToString(),Split.bSucceeded))return false;
 const auto Applied=UEHBBuildingToolset::CommitWallSplitAndRailing(B,Left->ElementGuid,170,B->RelationshipGraphRevision,Left->LocalStart,Left->LocalEnd,Left->Height,Left->Thickness,B->GetActorTransform().TransformPosition(Split.LocalPillarPosition+FVector(0,-300,0)),100,5,80,false);
 if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
 }
 TestTrue(TEXT("Undo continued loaded subdivision"),GEditor->UndoTransaction());TestEqual(TEXT("Undo loaded edit restores saved dependencies and receipt"),Snapshot(),Expected);
 TestTrue(TEXT("Redo continued loaded subdivision"),GEditor->RedoTransaction());TestTrue(TEXT("Final undo loaded edit"),GEditor->UndoTransaction(false));TestEqual(TEXT("Final undo restores saved state"),Snapshot(),Expected);
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomBranchDependenciesTest,"EHB.Topology.RoomBranchDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomBranchDependenciesTest::RunTest(const FString& Parameters)
{
 for(int32 Case=0;Case<3;++Case)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
  auto* Source=Fixture.Walls[0];const FGuid OldId=Source->ElementGuid;
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
  Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
  Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(100,50),Source->GetActorQuat());Window->BindToWall(Source,100);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
  const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,OldId,250);if(!Split.bSucceeded)return false;
  FEHBWallCreationEndpoint Anchor,Free;Anchor.Wall=Source;Anchor.WallDistance=250;Anchor.FloorIndex=Free.FloorIndex=1;
  Anchor.LocalLocation=Split.LocalPillarPosition;Free.LocalLocation=Anchor.LocalLocation+FVector(Case==0?0:150,-300,0);
  Anchor.WorldLocation=B->GetActorTransform().TransformPosition(Anchor.LocalLocation);Free.WorldLocation=B->GetActorTransform().TransformPosition(Free.LocalLocation);
  const bool Reverse=Case==2;const auto Start=Reverse?Free:Anchor,End=Reverse?Anchor:Free;
  auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::CommitFromWall(B,Start,End,FEHBWallCreationOptions(),Preview);};
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto Before=BuildingOutlineCopySnapshot(B);const auto ReceiptBefore=B->LastCommittedEdit;const auto OldRooms=B->GetClosedLoopsByFloor(1);
  const auto Ready=Commit(true);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;
  TestTrue(TEXT("Branch preview returns no actors"),Ready.Walls.IsEmpty()&&!Ready.PrimaryWall);TestEqual(TEXT("Branch preview preserves full dependencies"),BuildingOutlineCopySnapshot(B),Before);
  EHBWallCreationCommand::FailAfterSplit=1;TestEqual(TEXT("Split failure rolls back floors"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Split failure complete restoration"),BuildingOutlineCopySnapshot(B),Before);
  EHBWallCreationCommand::FailAfterEdge=1;TestEqual(TEXT("Branch failure rolls back floors"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Branch failure complete restoration"),BuildingOutlineCopySnapshot(B),Before);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
  {EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Final branch failure rolls back dependencies"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Final failure restores geometry relations and outlines"),BuildingOutlineCopySnapshot(B),Before);}
  const auto Applied=Commit();if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
  TestEqual(TEXT("One actual branch wall"),Applied.Walls.Num(),1);TestNull(TEXT("Old wall removed"),B->FindElementActorByGuid(OldId));TestEqual(TEXT("One combined branch receipt"),Applied.CommittedEdit.Command,FName(TEXT("CreateWallFromWall")));
  const auto NewRooms=B->GetClosedLoopsByFloor(1);TestEqual(TEXT("Room count retained"),NewRooms.Num(),OldRooms.Num());for(const auto& R:OldRooms)TestTrue(TEXT("Room identity retained"),NewRooms.ContainsByPredicate([&](const auto& V){return V.LoopGuid==R.LoopGuid;}));
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){FEHBBuildingClosedLoop Room;FName Status;TArray<FEHBElementRelation> Plan;TestTrue(TEXT("Branch floor resolves room"),F->TryGetRoomLoop(Room));TestTrue(TEXT("Branch floor contacts agree with actual host surfaces"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("Branch complete contact count"),Plan.Num(),F->SurfaceFinishRelationGuids.Num());}
  const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("Undo entire room branch"),GEditor->UndoTransaction());TestEqual(TEXT("Branch undo all dependencies"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Branch undo receipt"),B->LastCommittedEdit.StateId,ReceiptBefore.StateId);
  TestTrue(TEXT("Redo entire room branch"),GEditor->RedoTransaction());TestEqual(TEXT("Branch redo all dependencies"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
  FEasyHouseEditorMode Mode;auto Release=[&](){Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=Start.WorldLocation;Mode.WallCreationMouseLocation=End.WorldLocation;Mode.WallCreationStartWall=Start.Wall;Mode.WallCreationStartWallDistance=Start.WallDistance;Mode.WallCreationStartPillar=Start.Pillar;Mode.HoveredWallCreationWall=End.Wall;Mode.HoveredWallCreationWallDistance=End.WallDistance;Mode.HoveredWallCreationPillar=End.Pillar;return Mode.FinishWallCreationDrag();};
  EHBWallCreationCommand::FailAfterEdge=1;TestFalse(TEXT("Actual room branch release failure"),Release());TestEqual(TEXT("Actual room branch release no fallback mutation"),BuildingOutlineCopySnapshot(B),Before);
  TestTrue(TEXT("Actual room branch release success"),Release());Mode.CancelWallCreation();GEditor->UndoTransaction(false);TestEqual(TEXT("Actual room branch undo complete"),BuildingOutlineCopySnapshot(B),Before);GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBMultiRoomPathTest,"EHB.Topology.MultiAnchorRoomDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBMultiRoomPathTest::RunTest(const FString& Parameters)
{
 for(bool SameSource:{false,true})for(bool Closed:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
  AEHB_Wall* First=Fixture.Walls[0];AEHB_Wall* Second=SameSource?First:nullptr;
  if(!SameSource)for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))if(W->LocalStart.X>600&&FMath::Abs(W->LocalStart.Y)<1&&W->LocalEnd.X>W->LocalStart.X)Second=W;
  if(!TestNotNull(TEXT("Resolve second source wall"),Second))return false;
  for(auto* Source:TSet<AEHB_Wall*>{First,Second})
  {
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
   Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
   Window->SetActorLocationAndRotation(Source->GetWorldLocationOnCenterAxisAtDistance(90,50),Source->GetActorQuat());Window->BindToWall(Source,90);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
  }
  FEHBWallCreationEndpoint A,Z,C,D;A.Wall=First;Z.Wall=Second;A.WallDistance=180;Z.WallDistance=SameSource?360:180;
  for(auto* E:{&A,&Z}){const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,E->Wall->ElementGuid,E->WallDistance);if(!Split.bSucceeded)return false;E->LocalLocation=Split.LocalPillarPosition;E->WorldLocation=B->GetActorTransform().TransformPosition(E->LocalLocation);E->FloorIndex=1;}
  C.LocalLocation=A.LocalLocation+FVector(0,-300,0);D.LocalLocation=Z.LocalLocation+FVector(0,-300,0);C.WorldLocation=B->GetActorTransform().TransformPosition(C.LocalLocation);D.WorldLocation=B->GetActorTransform().TransformPosition(D.LocalLocation);C.FloorIndex=D.FloorIndex=1;
  const TArray<FEHBWallCreationEndpoint> Points=Closed?TArray<FEHBWallCreationEndpoint>{A,Z,D,C}:TArray<FEHBWallCreationEndpoint>{A,C,D,Z};
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto Before=BuildingOutlineCopySnapshot(B);const auto BeforeRooms=B->GetClosedLoopsByFloor(1);const auto BeforeReceipt=B->LastCommittedEdit;
  auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::CommitAnchoredPath(B,Points,Closed,FEHBWallCreationOptions(),Preview);};
  const auto Ready=Commit(true);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;TestEqual(TEXT("Multi-anchor room preview is readonly"),BuildingOutlineCopySnapshot(B),Before);TestTrue(TEXT("Preview exposes no new actors"),Ready.Walls.IsEmpty());
  for(int32 Split:{1,2}){EHBWallCreationCommand::FailAfterSplit=Split;TestEqual(TEXT("Every split failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Split failure restores both rooms and finishes"),BuildingOutlineCopySnapshot(B),Before);}
  for(int32 Edge=1;Edge<=(Closed?4:3);++Edge){EHBWallCreationCommand::FailAfterEdge=Edge;TestEqual(TEXT("Every edge failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Edge failure restores both rooms and finishes"),BuildingOutlineCopySnapshot(B),Before);}
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord}){EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Final phase failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Final failure restores all dependencies"),BuildingOutlineCopySnapshot(B),Before);}
  const auto Applied=Commit();if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
  const auto Rooms=B->GetClosedLoopsByFloor(1);TestEqual(TEXT("Exterior path adds one room"),Rooms.Num(),BeforeRooms.Num()+1);for(const auto& R:BeforeRooms)TestTrue(TEXT("Existing room identity preserved"),Rooms.ContainsByPredicate([&](const auto& V){return R.LoopGuid==V.LoopGuid;}));
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){FEHBBuildingClosedLoop Room;TArray<FEHBElementRelation> Plan;FName Status;TestTrue(TEXT("Floor room resolves after all splits"),F->TryGetRoomLoop(Room));TestTrue(TEXT("Floor contact matches final physical hosts"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("All cached finish identities restored"),Plan.Num(),F->SurfaceFinishRelationGuids.Num());}
  for(FGuid Id:Applied.CommittedEdit.RoomGuids)TestTrue(TEXT("Receipt contains only actual room identities"),Rooms.ContainsByPredicate([&](const auto& R){return R.LoopGuid==Id;}));
  const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("One undo all source splits path and dependencies"),GEditor->UndoTransaction());TestEqual(TEXT("Multi-anchor complete undo"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Undo restores original receipt"),B->LastCommittedEdit.StateId,BeforeReceipt.StateId);
  TestTrue(TEXT("One redo all source splits path and dependencies"),GEditor->RedoTransaction());TestEqual(TEXT("Multi-anchor complete redo"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
  if(Closed)
  {
   FEasyHouseEditorMode Mode;auto Release=[&](){Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=A.WorldLocation;Mode.WallCreationMouseLocation=D.WorldLocation;Mode.WallCreationStartWall=A.Wall;Mode.WallCreationStartWallDistance=A.WallDistance;return Mode.FinishWallCreationRectangleDrag();};
   EHBWallCreationCommand::FailAfterSplit=2;TestFalse(TEXT("Actual dependent rectangle release rolls back second split"),Release());TestEqual(TEXT("Actual rectangle release cannot leave partial room changes"),BuildingOutlineCopySnapshot(B),Before);
   TestTrue(TEXT("Actual dependent rectangle release commits"),Release());Mode.CancelWallCreation();GEditor->UndoTransaction(false);TestEqual(TEXT("Actual rectangle undo restores floor and slab dependencies"),BuildingOutlineCopySnapshot(B),Before);
  }
  GEditor->SelectNone(false,true,false);
 }
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;auto* Wall=Fixture.Walls[0];const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Wall->ElementGuid,180);if(!Split.bSucceeded)return false;
  FEHBWallCreationEndpoint Existing,Free,Anchor;Existing.Pillar=Fixture.Pillars[1];Existing.LocalLocation=FVector(9999,9999,0); // Ignored by the existing-pillar creation contract.
  Anchor.Wall=Wall;Anchor.WallDistance=180;Anchor.LocalLocation=Split.LocalPillarPosition;Anchor.WorldLocation=B->GetActorTransform().TransformPosition(Anchor.LocalLocation);
  Free.LocalLocation=Anchor.LocalLocation+FVector(0,-300,0);Free.WorldLocation=B->GetActorTransform().TransformPosition(Free.LocalLocation);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=BuildingOutlineCopySnapshot(B);
  const auto Applied=EHBWallCreationCommand::CommitAnchoredPath(B,{Existing,Free,Anchor},false,FEHBWallCreationOptions());
  if(!TestTrue(TEXT("Existing pillar endpoint derives new free pillar rotation from actual pose"),Applied.bSucceeded)){AddError(Applied.Status.ToString());return false;}
  GEditor->UndoTransaction(false);TestEqual(TEXT("Existing endpoint route undo restores original rooms"),BuildingOutlineCopySnapshot(B),Before);GEditor->SelectNone(false,true,false);
 }

 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPartitionRoomTest,"EHB.Topology.PartitionRoomDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPartitionRoomTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
 auto* Bottom=Fixture.Walls[0];auto* TopWall=Fixture.Walls[2];const auto Parent=B->GetClosedLoopsByWallGuid(Bottom->ElementGuid)[0];
 AEHB_Floor* ParentFloor=nullptr;AEHB_FloorSlab* ParentSlab=nullptr;
 for(auto* E:B->QueryElements(FEHBElementQuery()))
 {
  if(auto* F=Cast<AEHB_Floor>(E);F&&F->RoomLoopGuid==Parent.LoopGuid){ParentFloor=F;F->VisualOffset=0.25;F->bEnableEditorCollision=true;F->SemanticTags={TEXT("InheritedFinish")};F->FloorMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));F->RebuildFloorMesh();F->RecordOutlineSource(EEHBOutlineSource::RoomSupportFill);}
  if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->RoomFillLoopGuid==Parent.LoopGuid){ParentSlab=S;S->VisualExpansion=2;S->SemanticTags={TEXT("InheritedSlab")};S->RebuildSlabMesh();S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
 }
 if(!ParentFloor||!ParentSlab)return false;const FGuid FloorId=ParentFloor->ElementGuid,SlabId=ParentSlab->ElementGuid;
 FEHBWallCreationEndpoint A,Z;A.Wall=Bottom;Z.Wall=TopWall;A.WallDistance=250;const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Bottom->ElementGuid,250);if(!Split.bSucceeded)return false;
 A.LocalLocation=Split.LocalPillarPosition;Z.WallDistance=FMath::Abs(TopWall->LocalStart.X-A.LocalLocation.X);const auto Other=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,TopWall->ElementGuid,Z.WallDistance);if(!Other.bSucceeded)return false;Z.LocalLocation=Other.LocalPillarPosition;
 for(auto* E:{&A,&Z}){E->WorldLocation=B->GetActorTransform().TransformPosition(E->LocalLocation);E->FloorIndex=1;}
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=BuildingOutlineCopySnapshot(B);const auto Receipt=B->LastCommittedEdit;
 auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::CommitAnchoredPath(B,{A,Z},false,FEHBWallCreationOptions(),Preview);};
 const auto Ready=Commit(true);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;TestEqual(TEXT("Partition preview does not spawn child finishes"),BuildingOutlineCopySnapshot(B),Before);
 for(int32 I:{1,2}){EHBWallCreationCommand::FailAfterSplit=I;TestEqual(TEXT("Partition split failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Partition split restores parent finishes"),BuildingOutlineCopySnapshot(B),Before);}
 for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterInheritedSlabSpawn,EHBWallSplitTestHooks::EFailurePhase::AfterInheritedFloorSpawn,EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord}){EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Partition child-creation failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Partition rollback removes child actors and restores parent bindings"),BuildingOutlineCopySnapshot(B),Before);}
 const auto Applied=Commit();if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded))return false;
 const auto Rooms=B->GetClosedLoopsByFloor(1);TestEqual(TEXT("Partition creates two child rooms plus unchanged neighbor"),Rooms.Num(),3);TestFalse(TEXT("Parent room boundary replaced"),Rooms.ContainsByPredicate([&](const auto& R){return R.LoopGuid==Parent.LoopGuid;}));
 TestTrue(TEXT("Retained floor identity exists"),B->FindElementActorByGuid(FloorId)==ParentFloor);TestTrue(TEXT("Retained slab identity exists"),B->FindElementActorByGuid(SlabId)==ParentSlab);
 int32 Floors=0,Slabs=0,StyledFloors=0,StyledSlabs=0;
 for(auto* E:B->QueryElements(FEHBElementQuery()))
 {
  if(auto* F=Cast<AEHB_Floor>(E)){++Floors;FEHBBuildingClosedLoop Room;TestTrue(TEXT("Each child floor resolves a live room"),F->TryGetRoomLoop(Room));TArray<FEHBElementRelation> Plan;FName Status;TestTrue(TEXT("Each child floor contact validates"),F->BuildSurfaceFinishRelationPlan(Room,F->FloorRegions,Plan,Status));TestEqual(TEXT("Each child owns complete contact cache"),Plan.Num(),F->SurfaceFinishRelationGuids.Num());if(F->SemanticTags.Contains(TEXT("InheritedFinish"))){++StyledFloors;TestEqual(TEXT("Child visual lift inherited"),F->VisualOffset,0.25f);TestTrue(TEXT("Child editor collision inherited"),F->bEnableEditorCollision);TestEqual(TEXT("Child material inherited"),F->FloorMaterial,ParentFloor->FloorMaterial);}}
  if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;TestTrue(TEXT("Child slab resolves live anchor"),B->FindElementActorByGuid(S->RoomFillAnchorWallGuid)!=nullptr);if(S->SemanticTags.Contains(TEXT("InheritedSlab"))){++StyledSlabs;TestEqual(TEXT("Child display expansion inherited"),S->VisualExpansion,2.0f);}}
 }
 TestEqual(TEXT("Three room floors"),Floors,3);TestEqual(TEXT("Three room slabs"),Slabs,3);TestEqual(TEXT("Both child floor styles inherited"),StyledFloors,2);TestEqual(TEXT("Both child slab styles inherited"),StyledSlabs,2);
 TestTrue(TEXT("Invalidation includes removed parent"),Applied.CommittedEdit.RoomGuids.Contains(Parent.LoopGuid));for(const auto& R:Rooms)TestTrue(TEXT("Invalidation includes final rooms"),Applied.CommittedEdit.RoomGuids.Contains(R.LoopGuid));
 const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("Undo complete partition"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores parents and removes inherited children"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Undo restores original receipt"),B->LastCommittedEdit.StateId,Receipt.StateId);
 TestTrue(TEXT("Redo complete partition"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores child identities and geometry"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
 FEasyHouseEditorMode Mode;auto Release=[&](){Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=A.WorldLocation;Mode.WallCreationMouseLocation=Z.WorldLocation;Mode.WallCreationStartWall=A.Wall;Mode.WallCreationStartWallDistance=A.WallDistance;Mode.HoveredWallCreationWall=Z.Wall;Mode.HoveredWallCreationWallDistance=Z.WallDistance;return Mode.FinishWallCreationDrag();};
 EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::AfterGeometry;TestFalse(TEXT("Actual partition release reports rollback"),Release());TestEqual(TEXT("Actual partition release has no fallback mutation"),BuildingOutlineCopySnapshot(B),Before);
 TestTrue(TEXT("Actual partition release inherits finishes"),Release());Mode.CancelWallCreation();TestEqual(TEXT("Release partitions the room"),B->GetClosedLoopsByFloor(1).Num(),3);GEditor->UndoTransaction(false);TestEqual(TEXT("Actual partition release undo restores parents"),BuildingOutlineCopySnapshot(B),Before);
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFutureFinishPlanTest,"EHB.Topology.FutureFloorContactDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFutureFinishPlanTest::RunTest(const FString& Parameters)
{
 FEHBFloorFinishContactDraft D;D.FloorGuid=FGuid::NewGuid();D.RoomGuid=FGuid::NewGuid();D.FloorIndex=1;
 FEHBFloorFinishRegion R;R.OuterPolygon={{0,0,300},{100,0,300},{100,100,300},{0,100,300}};D.Regions={R};FEHBFloorSupportSurface Top;Top.OuterPolygon=R.OuterPolygon;const FGuid Host=FGuid::NewGuid();D.Hosts.Add(Host,{Top});
 TArray<FEHBElementRelation> Out;FName Status;TestTrue(TEXT("Future floor contact requires no Actor"),D.Build(Out,Status));TestEqual(TEXT("Future floor has one contact"),Out.Num(),1);if(Out.Num()!=1)return false;TestEqual(TEXT("Future floor target identity"),Out[0].Target.ElementGuid,D.FloorGuid);TestEqual(TEXT("Future floor area"),Out[0].ContactArea,10000.f);
 D.Previous=Out;const FGuid Id=Out[0].RelationGuid;D.RoomGuid=FGuid::NewGuid();TestTrue(TEXT("Rebind retained floor to child room"),D.Build(Out,Status));TestEqual(TEXT("Retained host relation identity"),Out[0].RelationGuid,Id);TestEqual(TEXT("Child room metadata"),Out[0].StringMetadata.FindRef(TEXT("RoomLoopGuid")),D.RoomGuid.ToString(EGuidFormats::DigitsWithHyphens));
 D.Previous[0].Target.ElementGuid=FGuid::NewGuid();TestFalse(TEXT("Foreign prior relation rejected"),D.Build(Out,Status));TestTrue(TEXT("Failure exposes no partial output"),Out.IsEmpty());D.Previous.Reset();D.Hosts[Host][0].OuterPolygon[0].Z=301;TestFalse(TEXT("Invalid host plane rejected"),D.Build(Out,Status));TestTrue(TEXT("Geometry failure clears draft"),Out.IsEmpty());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOrdinaryRoomPathTest,"EHB.Topology.OrdinaryPathRoomDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOrdinaryRoomPathTest::RunTest(const FString& Parameters)
{
 for(int32 Kind:{0,1,2,3})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
  auto Existing=[&](AEHB_Pillar* P){FEHBWallCreationEndpoint E;E.Pillar=P;E.FloorIndex=1;E.LocalLocation=FVector(9999,9999,0);E.WorldLocation=P->GetActorLocation();return E;};
  auto Free=[&](FVector P){FEHBWallCreationEndpoint E;E.LocalLocation=P;E.WorldLocation=B->GetActorTransform().TransformPosition(P);E.FloorIndex=1;return E;};
  const auto A=Existing(Fixture.Pillars[0]),Z=Existing(Fixture.Pillars[2]);
  const bool Closed=Kind>=2;TArray<FEHBWallCreationEndpoint> Points;
  if(Kind==0)Points={A,Z};
  if(Kind==1)Points={A,Free(FVector(180.2,320.3,0)),Z};
  if(Kind==2)Points={A,Existing(Fixture.Pillars[1]),Z};
  if(Kind==3)Points={A,Free(FVector(-300,0,0)),Free(FVector(-300,-300,0)),Free(FVector(0,-300,0))};
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto Before=BuildingOutlineCopySnapshot(B);const auto BeforeRooms=B->GetClosedLoopsByFloor(1);const auto Receipt=B->LastCommittedEdit;
  auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::Commit(B,Points,Closed,FEHBWallCreationOptions(),Preview);};
  const auto Preview=Commit(true);if(!TestTrue(*FString::Printf(TEXT("Kind %d preview %s"),Kind,*Preview.Status.ToString()),Preview.bSucceeded))return false;
  TestTrue(TEXT("Ordinary room preflight creates no output actors"),Preview.Walls.IsEmpty());TestEqual(TEXT("Ordinary room preflight preserves model and geometry"),BuildingOutlineCopySnapshot(B),Before);
  for(int32 Edge=1;Edge<=(Closed?Points.Num():Points.Num()-1);++Edge){EHBWallCreationCommand::FailAfterEdge=Edge;TestEqual(TEXT("Each ordinary path edge rolls back"),Commit().Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Edge failure restores room outlines and contacts"),BuildingOutlineCopySnapshot(B),Before);}
  TArray<EHBWallSplitTestHooks::EFailurePhase> Phases={EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord};
  if(Kind!=3){Phases.Add(EHBWallSplitTestHooks::EFailurePhase::AfterInheritedSlabSpawn);Phases.Add(EHBWallSplitTestHooks::EFailurePhase::AfterInheritedFloorSpawn);}
  for(auto Phase:Phases){EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Ordinary dependency phase failure rolls back"),Commit().Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("No inherited actor or partial binding remains"),BuildingOutlineCopySnapshot(B),Before);}
  const auto Applied=Commit();if(!TestTrue(*FString::Printf(TEXT("Kind %d commit %s: %s"),Kind,*Applied.Status.ToString(),*Applied.FailureReason.ToString()),Applied.bSucceeded))return false;
  const auto Rooms=B->GetClosedLoopsByFloor(1);TestEqual(TEXT("Ordinary path creates three final rooms"),Rooms.Num(),3);
  int32 Floors=0,Slabs=0;TMap<FGuid,int32> RoomFloors,RoomSlabs;
  for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E)){++Floors;++RoomFloors.FindOrAdd(F->RoomLoopGuid);FEHBBuildingClosedLoop R;TArray<FEHBElementRelation> Contacts;FName Status;TestTrue(TEXT("Inherited floor resolves live room"),F->TryGetRoomLoop(R));TestTrue(TEXT("Inherited floor contact plan validates actual geometry"),F->BuildSurfaceFinishRelationPlan(R,F->FloorRegions,Contacts,Status));TestEqual(TEXT("Complete inherited floor contact cache"),Contacts.Num(),F->SurfaceFinishRelationGuids.Num());}
   if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;++RoomSlabs.FindOrAdd(S->RoomFillLoopGuid);TestTrue(TEXT("Inherited slab resolves live wall anchor"),B->FindElementActorByGuid(S->RoomFillAnchorWallGuid)!=nullptr);}
  }
  TestEqual(TEXT("No unexpected floor duplication"),Floors,Kind==3?2:3);TestEqual(TEXT("No unexpected slab duplication"),Slabs,Kind==3?2:3);
  for(const auto& R:Rooms){TestTrue(TEXT("Receipt includes actual final room"),Applied.CommittedEdit.RoomGuids.Contains(R.LoopGuid));if(Kind!=3){TestEqual(TEXT("Each child has one floor"),RoomFloors.FindRef(R.LoopGuid),1);TestEqual(TEXT("Each child has one slab"),RoomSlabs.FindRef(R.LoopGuid),1);}}
  for(const auto& R:BeforeRooms)TestTrue(TEXT("Receipt includes old rooms"),Applied.CommittedEdit.RoomGuids.Contains(R.LoopGuid));
  const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("Undo ordinary partition and dependencies"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores room inheritance and contact identities"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Undo original receipt"),B->LastCommittedEdit.StateId,Receipt.StateId);
  TestTrue(TEXT("Redo ordinary partition and dependencies"),GEditor->RedoTransaction());TestEqual(TEXT("Redo retains inherited identities and full geometry"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
  if(Kind==0||Kind==3)
  {
   FEasyHouseEditorMode Mode;auto Release=[&](){Mode.BeginWallCreation(B,300,20);Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=A.WorldLocation;Mode.WallCreationStartPillar=A.Pillar;Mode.WallCreationMouseLocation=Kind==0?Z.WorldLocation:Points[2].WorldLocation;Mode.HoveredWallCreationPillar=Kind==0?Z.Pillar:nullptr;return Kind==0?Mode.FinishWallCreationDrag():Mode.FinishWallCreationRectangleDrag();};
   EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::AfterGeometry;TestFalse(TEXT("Actual ordinary release reports dependency rollback"),Release());TestEqual(TEXT("Actual release does not fall back after failure"),BuildingOutlineCopySnapshot(B),Before);
   TestTrue(TEXT("Actual ordinary release commits room dependencies"),Release());Mode.CancelWallCreation();TestEqual(TEXT("Release resolves three rooms"),B->GetClosedLoopsByFloor(1).Num(),3);GEditor->UndoTransaction(false);TestEqual(TEXT("Actual ordinary release undo"),BuildingOutlineCopySnapshot(B),Before);
  }
  GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBContactRepresentationTest,"EHB.Topology.ContactTriangulationInvariant",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBContactRepresentationTest::RunTest(const FString& Parameters)
{
 // Captured from the real bent-path rejection. The two triangle cap and the
 // four-corner surface quantize to exactly the same external boundary.
 const TArray<FVector> Points={{185.25225644451262,333.13064291316812,300},{551.28075513908811,489.99999949655762,300},{559.1591411106715,471.61709889619647,300},{193.13064241609595,314.74774231280696,300}};
 FEHBFloorSupportSurface Quad,A,Z;Quad.OuterPolygon=Points;A.OuterPolygon={Points[0],Points[1],Points[2]};Z.OuterPolygon={Points[0],Points[2],Points[3]};
 FEHBFloorFinishRegion Floor;Floor.OuterPolygon={{0,0,300},{600,0,300},{600,500,300},{180,320,300}};
 FEHBFloorContact Expected,Triangles,Reverse;FName Status;
 TestTrue(TEXT("Single quad contact"),FEHBFloorContactGeometry::Build({Floor},{Quad},Expected,Status));
 TestTrue(TEXT("Triangulated contact"),FEHBFloorContactGeometry::Build({Floor},{A,Z},Triangles,Status));
 TestTrue(TEXT("Contact area independent of internal diagonal"),FMath::Abs(Expected.Area-Triangles.Area)<0.000001);
 TestTrue(TEXT("Reordered cap contact"),FEHBFloorContactGeometry::Build({Floor},{Z,A},Reverse,Status));TestEqual(TEXT("Triangle ordering does not change area"),Reverse.Area,Triangles.Area);
 FEHBFloorFinishRegion F1,F2;F1.OuterPolygon={Floor.OuterPolygon[0],Floor.OuterPolygon[1],Floor.OuterPolygon[2]};F2.OuterPolygon={Floor.OuterPolygon[0],Floor.OuterPolygon[2],Floor.OuterPolygon[3]};
 TestTrue(TEXT("Coverage decomposition contact"),FEHBFloorContactGeometry::Build({F2,F1},{A,Z},Reverse,Status));TestTrue(TEXT("Contact area independent of both decompositions"),FMath::Abs(Expected.Area-Reverse.Area)<0.000001);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBReusedPrefixTransactionTest,"EHB.Topology.ReusedPrefixPreservesUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBReusedPrefixTransactionTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;auto* P=Fixture.Pillars[0];const FName OldName=P->ElementName;
 {FScopedTransaction Edit(FText::FromString(TEXT("User edit before reused path")));P->Modify();P->ElementName=TEXT("KeepPriorUserEdit");}
 const auto UserUndo=GEditor->Trans->GetUndoContext().TransactionId;const auto Before=BuildingOutlineCopySnapshot(B);
 TArray<FEHBWallCreationEndpoint> Points;for(auto* Pillar:{Fixture.Pillars[0],Fixture.Pillars[1],Fixture.Pillars[2]}){auto& E=Points.AddDefaulted_GetRef();E.Pillar=Pillar;E.WorldLocation=Pillar->GetActorLocation();E.LocalLocation=Pillar->GetElementLocalTransform().GetLocation();}
 for(int32 Edge:{1,2})
 {
  EHBWallCreationCommand::FailAfterEdge=Edge;const auto R=EHBWallCreationCommand::Commit(B,Points,true,FEHBWallCreationOptions());
  TestEqual(TEXT("Unchanged prefix needs no undo"),R.Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Prior user edit still applied"),P->ElementName,FName(TEXT("KeepPriorUserEdit")));TestEqual(TEXT("Unchanged failed prefix preserves undo identity"),GEditor->Trans->GetUndoContext().TransactionId,UserUndo);TestEqual(TEXT("No geometry changed"),BuildingOutlineCopySnapshot(B),Before);
 }
 TestTrue(TEXT("Prior user edit remains undoable"),GEditor->UndoTransaction());TestEqual(TEXT("Undo reaches original user name"),P->ElementName,OldName);
 const auto UserRedo=GEditor->Trans->GetRedoContext().TransactionId;EHBWallCreationCommand::FailAfterEdge=1;const auto R=EHBWallCreationCommand::Commit(B,Points,true,FEHBWallCreationOptions());TestEqual(TEXT("Empty failure with redo pending"),R.Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Previous redo preserved"),GEditor->Trans->GetRedoContext().TransactionId,UserRedo);TestTrue(TEXT("User redo still works"),GEditor->RedoTransaction());TestEqual(TEXT("Redone user name"),P->ElementName,FName(TEXT("KeepPriorUserEdit")));GEditor->UndoTransaction(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSplitAxisPrecisionTest,"EHB.Topology.SplitAxisPreviewPrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSplitAxisPrecisionTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
 for(auto* P:{Fixture.Pillars[0],Fixture.Pillars[3]}){auto T=P->GetElementLocalTransform();auto V=T.GetLocation();V.X=7.6204986572265625;T.SetLocation(V);P->SetElementLocalTransform(T,true);}
 auto* W=Fixture.Walls[0];const auto Ready=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,W->ElementGuid,170);if(!TestTrue(*Ready.Status.ToString(),Ready.bSucceeded))return false;
 const auto Axis=W->GetBuildingLocalLocationOnCenterAxisAtDistance(170,0);
 TestTrue(TEXT("Preview uses the exact placement formula"),Ready.LocalPillarPosition.Equals(Axis,1.e-12));
 TestTrue(TEXT("World axis query uses the same local definition"),W->GetWorldLocationOnCenterAxisAtDistance(170,0).Equals(B->GetActorTransform().TransformPosition(Axis),1.e-12));
 const auto Baseline=UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true);if(!TestTrue(*Baseline.Status.ToString(),Baseline.bSucceeded))return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=BuildingOutlineCopySnapshot(B);
 const auto Applied=UEHBBuildingToolset::CommitPlainWallSplit(B,W->ElementGuid,170,B->RelationshipGraphRevision,W->LocalStart,W->LocalEnd,W->Height,W->Thickness,false);
 if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
 bool Found=false;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* P=Cast<AEHB_Pillar>(E))if(P->GetElementLocalTransform().GetLocation().Equals(Axis,1.e-10)){Found=true;const auto V=P->GetElementLocalTransform().GetLocation();TestTrue(TEXT("Declared local position survives world-space movement exactly"),V==Axis);TestTrue(TEXT("Declared local rotation has no world-space roundoff"),P->GetElementLocalTransform().GetRotation()==FQuat::Identity);TestEqual(TEXT("Placement uses the preview contact grid cell"),FMath::RoundToInt64(V.X*1000),FMath::RoundToInt64(Axis.X*1000));}
 TestTrue(TEXT("Actual split column preserves preview precision"),Found);GEditor->UndoTransaction(false);TestEqual(TEXT("Precision fix keeps complete undo"),BuildingOutlineCopySnapshot(B),Before);GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOrdinaryPreservedHostsTest,"EHB.Topology.OrdinaryCreationPreservedHosts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOrdinaryPreservedHostsTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true))return false;
 AEHB_Wall* Host=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))if(W->LocalStart.X>900&&W->LocalEnd.X>900)Host=W;
 if(!TestNotNull(TEXT("Far room exterior window host"),Host))return false;
 FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Window=B->GetWorld()->SpawnActor<AEHB_DoorWindow>(Params);if(!Window)return false;Fixture.Actors.Add(Window);
 Window->Kind=EEHBDoorWindowElementKind::Window;Window->SetRectangularOpeningDimensions(50,100,50,10);Window->AttachToBuilding(B,FTransform::Identity);
 Window->SetActorLocationAndRotation(Host->GetWorldLocationOnCenterAxisAtDistance(200,50),Host->GetActorQuat());Window->BindToWall(Host,200);Window->SetFloorAssignment(1,EEHBBuildingFloorElementRole::HostedElement);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 auto* Rail=UEHBBuildingToolset::CreateRailing(B,TEXT("PreservedOrdinaryRail"),FVector(0,500,0),FVector(-300,500,0),100,80,5,EEHBRailingFillMode::PostsAndRails,1,5,80,Fixture.Pillars[3],nullptr);
 if(!TestNotNull(TEXT("Retained ordinary railing"),Rail))return false;Fixture.Actors.Add(Rail);
 auto Existing=[&](AEHB_Pillar* P){FEHBWallCreationEndpoint E;E.Pillar=P;E.FloorIndex=1;E.LocalLocation=P->GetElementLocalTransform().GetLocation();E.WorldLocation=P->GetActorLocation();return E;};
 auto Free=[&](FVector P){FEHBWallCreationEndpoint E;E.FloorIndex=1;E.LocalLocation=P;E.WorldLocation=B->GetActorTransform().TransformPosition(P);return E;};
 const TArray<FEHBWallCreationEndpoint> Points={Existing(Fixture.Pillars[0]),Existing(Fixture.Pillars[2])};
 const auto Before=BuildingOutlineCopySnapshot(B);FEHBPreservedCreationHosts Captured;FName Status;
 if(!TestTrue(*Status.ToString(),Captured.Capture(B,B->QueryElements(FEHBElementQuery()),Status)))return false;
 auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::Commit(B,Points,false,FEHBWallCreationOptions(),Preview);};
 const auto Preview=Commit(true);if(!TestTrue(*Preview.Status.ToString(),Preview.bSucceeded))return false;
 TestEqual(TEXT("Retained host preview is read-only"),BuildingOutlineCopySnapshot(B),Before);
 for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterInheritedSlabSpawn,EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
 {EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Host-aware failure rolls back"),Commit().Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Rollback restores existing window railing room and mesh"),BuildingOutlineCopySnapshot(B),Before);}
 const auto Applied=Commit();if(!TestTrue(*FString::Printf(TEXT("Preserved hosts %s %s"),*Applied.Status.ToString(),*Applied.FailureReason.ToString()),Applied.bSucceeded))return false;
 TestTrue(TEXT("Existing hosted data retained"),Captured.IsPreserved());TestEqual(TEXT("Room split with existing hosts"),B->GetClosedLoopsByFloor(1).Num(),3);
 const auto After=BuildingOutlineCopySnapshot(B);TestTrue(TEXT("Undo hosted ordinary split"),GEditor->UndoTransaction());TestEqual(TEXT("Whole hosted undo"),BuildingOutlineCopySnapshot(B),Before);
 TestTrue(TEXT("Redo hosted ordinary split"),GEditor->RedoTransaction());TestEqual(TEXT("Whole hosted redo"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
 auto Rejected=[&](FName Expected){const auto R=Commit(true);TestEqual(TEXT("Malformed hosted data rejected"),R.Status,Expected);};
 const auto SavedCut=Host->CutOperations[0];Host->CutOperations[0].Priority=99;Rejected(TEXT("UnsupportedPreservedOpeningLink"));Host->CutOperations[0]=SavedCut;
 const auto SavedLinks=B->ElementRelations;B->ElementRelations.RemoveAll([&](const auto& R){return R.Target.RefersToElement(Window->ElementGuid);});Rejected(TEXT("IncompletePreservedOpening"));B->ElementRelations=SavedLinks;B->RebuildElementAndRelationshipIndexes();
 const auto Cross=EHBWallCreationCommand::Commit(B,{Free(FVector(-150,300,0)),Free(FVector(-150,700,0))},false,FEHBWallCreationOptions(),true);
 TestEqual(TEXT("New wall cannot cut through retained railing"),Cross.Status,FName(TEXT("CreatedWallIntersectsPreservedRailing")));
 const auto Junction=EHBWallCreationCommand::Commit(B,{Existing(Fixture.Pillars[3]),Free(FVector(-200,400,0))},false,FEHBWallCreationOptions(),true);
 TestEqual(TEXT("Changed railing attachment junction still requires a plan"),Junction.Status,FName(TEXT("RailingJunctionChangeRequiresPlan")));
 FEHBPreparedWallNodeDefinitions Candidate;TestTrue(TEXT("Explicit validated host capture"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Candidate,&Captured.OpeningWalls).bSucceeded);
 const auto* W=Candidate.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Host->ElementGuid;});if(!W)return false;const auto NodeId=W->StartNodeGuid;
 for(auto& N:Candidate.Nodes)if(N.NodeGuid==NodeId)N.LocalTransform.AddToTranslation(FVector(0,30,0));
 const auto Route=FEHBWallPathPlanning::BuildForBuilding(B,Points,false,FEHBWallCreationOptions());TestFalse(TEXT("Host geometry changes are not treated as preservation"),Captured.ValidateCandidate(B,Candidate,Route,FEHBWallCreationOptions(),Status));TestEqual(TEXT("Host needs migration"),Status,FName(TEXT("OpeningHostChangeRequiresMigration")));
 TestEqual(TEXT("All rejected previews leave original building intact"),BuildingOutlineCopySnapshot(B),Before);GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodePoseAuthorityTest,"EHB.Topology.NodePoseAuthority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodePoseAuthorityTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
 TestEqual(TEXT("Legacy mode must migrate connection IDs first"),B->MigrateWallNodeAuthority(true).Status,FName(TEXT("RequiresTypedNodeOwnership")));
 if(!B->MigrateWallNodeOwnership(true).bSucceeded)return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 auto ResultOf=[](const FString& Json){FEHBTopologyMigrationResult R;FJsonObjectConverter::JsonObjectStringToUStruct(Json,&R);return R;};
 auto Migrate=[&](bool Apply){return ResultOf(UEHBBuildingToolset::MigrateWallNodeAuthority(B,Apply));};
 const FString Before=BuildingCopySnapshot(B);const auto Room=B->GetClosedLoopsByFloor(1)[0].LoopGuid;const int32 Queue=GEditor->Trans->GetQueueLength();
 TestEqual(TEXT("Authority preview ready"),Migrate(false).Status,FName(TEXT("Ready")));TestEqual(TEXT("Preview is read only"),BuildingCopySnapshot(B),Before);TestEqual(TEXT("Preview has no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
 const auto Applied=Migrate(true);if(!TestTrue(*Applied.Status.ToString(),Applied.bSucceeded&&Applied.bChanged))return false;
 const auto Active=BuildingCopySnapshot(B);TestEqual(TEXT("Four building-owned node records"),B->WallNodeAuthority.Nodes.Num(),4);TestEqual(TEXT("Pose authority active"),B->WallNodeAuthority.Version,1);
 TestEqual(TEXT("Activation keeps room identity"),B->GetClosedLoopsByFloor(1)[0].LoopGuid,Room);
 TestEqual(TEXT("Idempotent authority apply"),Migrate(true).Status,FName(TEXT("AlreadyMigrated")));TestEqual(TEXT("Idempotent apply preserves values and geometry"),BuildingCopySnapshot(B),Active);
 TestTrue(TEXT("Authority migration undo"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores all records and geometry"),BuildingCopySnapshot(B),Before);
 TestTrue(TEXT("Authority migration redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores all records and geometry"),BuildingCopySnapshot(B),Active);GEditor->UndoTransaction(false);
 {TGuardValue<bool> Fail(EHBNodeOwnershipTestHooks::FailAfterApply,true);TestEqual(TEXT("Failed activation production rollback"),Migrate(true).Status,FName(TEXT("NodeAuthorityFailedRolledBack")));TestEqual(TEXT("Activation failure restores everything"),BuildingCopySnapshot(B),Before);TestFalse(TEXT("Failed activation cannot redo"),GEditor->Trans->CanRedo());}
 if(!Migrate(true).bSucceeded)return false;
 auto* P=Fixture.Pillars[0];const auto Pose=P->GetElementLocalTransform();const FGuid Id=B->FindNodeForPhysicalPillar(P->ElementGuid);
 auto Values=[&](){FString Json;FJsonObjectConverter::UStructToJsonObjectString(B->WallNodeAuthority,Json);return Json;};const auto SavedValues=Values();
 // Bypassing an authored command must not silently replace authoritative node values.
 P->SetActorRelativeLocation(Pose.GetLocation()+FVector(-80,-40,0));P->ReregisterAllComponents();B->RebuildElementAndRelationshipIndexes();P->RebuildPillarMesh();
 const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(B);const auto* N=Graph.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});TestTrue(TEXT("Graph uses stored node position despite changed proxy"),N&&N->LocalPosition==Pose.GetLocation());
 FEHBPreparedWallNodeDefinitions Source;TestEqual(TEXT("Edit preflight rejects stale physical proxy"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Source).Status,FName(TEXT("StalePhysicalNodeProxy")));TestEqual(TEXT("Query and rebuild never recapture proxy"),Values(),SavedValues);
 P->SetActorRelativeTransform(Pose);P->RebuildPillarMesh();B->RefreshWallsConnectedToPillar(P->ElementGuid,true);
 TestTrue(TEXT("Restored proxy can be captured"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Source).bSucceeded);
 if(!UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true).bSucceeded)return false;
 const auto Original=BuildingCopySnapshot(B);
 const auto Move=UEHBBuildingToolset::CommitBasicNodeMove(B,Id,Pose.GetLocation(),Pose.GetLocation()+FVector(-60,-30,0));if(!TestTrue(*Move.Message,Move.bSucceeded))return false;
 TestTrue(TEXT("Moved proxy and node coherent"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Source).bSucceeded);TestEqual(TEXT("Moved node retains identity"),B->FindNodeForPhysicalPillar(P->ElementGuid),Id);
 const auto Moved=BuildingCopySnapshot(B);TestTrue(TEXT("Move undo"),GEditor->UndoTransaction());TestEqual(TEXT("Move undo restores values and geometry"),BuildingCopySnapshot(B),Original);TestTrue(TEXT("Move redo"),GEditor->RedoTransaction());TestEqual(TEXT("Move redo restores values and geometry"),BuildingCopySnapshot(B),Moved);GEditor->UndoTransaction(false);
 EHBNodeMoveTestHooks::FailurePhase=EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline;
 const auto Failed=UEHBBuildingToolset::CommitBasicNodeMove(B,Id,Pose.GetLocation(),Pose.GetLocation()+FVector(-60,-30,0));TestEqual(TEXT("Movement failure rolls back node authority"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));TestEqual(TEXT("Failure restores node records and full geometry"),BuildingCopySnapshot(B),Original);
 const auto Split=UEHBBuildingToolset::CommitPlainWallSplit(B,Fixture.Walls[0]->ElementGuid,250,B->RelationshipGraphRevision,Fixture.Walls[0]->LocalStart,Fixture.Walls[0]->LocalEnd,Fixture.Walls[0]->Height,Fixture.Walls[0]->Thickness);if(!TestTrue(*Split.Message,Split.bSucceeded))return false;
 TestEqual(TEXT("Split creates new authoritative node"),B->WallNodeAuthority.Nodes.Num(),5);TestTrue(TEXT("New node actual pose and dimensions match"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Source).bSucceeded);
 const auto SplitState=BuildingCopySnapshot(B);TestTrue(TEXT("Split undo"),GEditor->UndoTransaction());TestEqual(TEXT("Split undo removes new node"),BuildingCopySnapshot(B),Original);TestTrue(TEXT("Split redo"),GEditor->RedoTransaction());TestEqual(TEXT("Split redo restores authored identity and geometry"),BuildingCopySnapshot(B),SplitState);GEditor->UndoTransaction(false);
 const auto Copied=EHBBuildingCopy::Execute(B,FVector(2000,0,0));if(!TestTrue(*Copied.Status.ToString(),Copied.bSucceeded))return false;Fixture.Actors.Append(Copied.Actors);
 TestEqual(TEXT("Copy retains active authority"),Copied.Building->WallNodeAuthority.Version,1);TestTrue(TEXT("Copy remaps node values and physical bindings"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Copied.Building,Source).bSucceeded);
 for(const auto& C:Copied.Building->WallNodeAuthority.Nodes)TestFalse(TEXT("Copy never shares source logical ID"),B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& V){return V.NodeGuid==C.NodeGuid;}));
 const auto CopyState=BuildingCopySnapshot(Copied.Building);TestTrue(TEXT("Copy undo"),GEditor->UndoTransaction());TestTrue(TEXT("Copy redo"),GEditor->RedoTransaction());TestEqual(TEXT("Copied authority survives redo"),BuildingCopySnapshot(Copied.Building),CopyState);GEditor->UndoTransaction(false);
 {FScopedTransaction T(FText::FromString(TEXT("EHB authored pillar resize")));P->Modify();P->GetRootComponent()->Modify();P->ConfigureAsPolygonPillar(330,25,30,Pose,true);}
 TestTrue(TEXT("Configure writes node dimensions"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Source).bSucceeded);const auto* Resized=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});TestTrue(TEXT("Node dimensions reflect explicit edit"),Resized&&Resized->JunctionDimensions==FVector(25,30,330));GEditor->UndoTransaction(false);
 TestEqual(TEXT("Explicit resize undo restores authority and generated geometry"),BuildingCopySnapshot(B),Original);
 {FScopedTransaction T(FText::FromString(TEXT("EHB authored floor assignment")));P->Modify();P->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);}
 TestTrue(TEXT("Explicit floor API writes node floor"),B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& V){return V.NodeGuid==Id&&V.FloorIndex==2;}));GEditor->UndoTransaction(false);TestEqual(TEXT("Floor API undo restores authority"),BuildingCopySnapshot(B),Original);
 {FScopedTransaction T(FText::FromString(TEXT("EHB details floor edit")));P->Modify();P->PreEditChange(nullptr);P->FloorIndex=2;FPropertyChangedEvent Event(FindFProperty<FProperty>(AEHBElementActorBase::StaticClass(),GET_MEMBER_NAME_CHECKED(AEHBElementActorBase,FloorIndex)),EPropertyChangeType::ValueSet);P->PostEditChangeProperty(Event);}
 TestTrue(TEXT("Details floor edit writes node floor"),B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& V){return V.NodeGuid==Id&&V.FloorIndex==2;}));GEditor->UndoTransaction(false);TestEqual(TEXT("Details undo restores authority"),BuildingCopySnapshot(B),Original);

 const auto Stored=B->WallNodeAuthority;B->WallNodeAuthority.Version=99;TestFalse(TEXT("Unknown authority not migrated over"),B->MigrateWallNodeAuthority(true).bSucceeded);TestEqual(TEXT("Unknown version preserved"),B->WallNodeAuthority.Version,99);B->WallNodeAuthority=Stored;
 P->Destroy();TestEqual(TEXT("Full delete removes node and two walls under existing semantics"),B->WallNodeAuthority.Nodes.Num(),3);TestTrue(TEXT("No dangling node records after full delete"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Issues.IsEmpty());
 B->ClearAllElements();TestEqual(TEXT("Clear resets authority"),B->WallNodeAuthority.Version,0);TestTrue(TEXT("Clear removes node values"),B->WallNodeAuthority.Nodes.IsEmpty());GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeAuthorityRoomDependenciesTest,"EHB.Topology.NodeAuthorityRoomDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeAuthorityRoomDependenciesTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;if(!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Before=BuildingOutlineCopySnapshot(B);const auto Receipt=B->LastCommittedEdit;
 auto* P=Fixture.Pillars[0];const FGuid Id=B->FindNodeForPhysicalPillar(P->ElementGuid);const FVector Start=P->GetElementLocalTransform().GetLocation();
 auto Move=[&](bool Preview=false){return UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(B,Id,Start,Start+FVector(-40,-20,0),Preview);};
 const auto Preview=Move(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestEqual(TEXT("Authority room move preview is read only"),BuildingOutlineCopySnapshot(B),Before);
 for(auto Phase:{EHBNodeMoveTestHooks::EFailurePhase::AfterFloors,EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs,EHBNodeMoveTestHooks::EFailurePhase::AfterContacts,EHBNodeMoveTestHooks::EFailurePhase::AfterEditRecord})
 {
  EHBNodeMoveTestHooks::FailurePhase=Phase;const auto Failed=Move();TestEqual(TEXT("Composite room failure restores authority"),Failed.Message,FString(TEXT("NodeMoveFailedRolledBack")));TestEqual(TEXT("Room failure restores all node floor slab contact and geometry data"),BuildingOutlineCopySnapshot(B),Before);TestEqual(TEXT("Failure preserves edit receipt"),B->LastCommittedEdit.StateId,Receipt.StateId);TestFalse(TEXT("Failed room edit not redoable"),GEditor->Trans->CanRedo());
 }
 const auto Applied=Move();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;const auto After=BuildingOutlineCopySnapshot(B);
 FEHBPreparedWallNodeDefinitions Model;TestTrue(TEXT("After moving room authority and proxies coherent"),UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(B,Model).bSucceeded);TestEqual(TEXT("Both rooms retained"),B->GetClosedLoopsByFloor(1).Num(),2);
 TestTrue(TEXT("Room authority undo"),GEditor->UndoTransaction());TestEqual(TEXT("Undo full floor slab node and geometry"),BuildingOutlineCopySnapshot(B),Before);TestTrue(TEXT("Room authority redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo full composite data"),BuildingOutlineCopySnapshot(B),After);GEditor->UndoTransaction(false);
 B->RebuildElementAndRelationshipIndexes();B->RebuildClosedLoops();TestEqual(TEXT("Rebuild does not change authority or room outlines"),BuildingOutlineCopySnapshot(B),Before);
 GEditor->SelectNone(false,true,false);return true;
}

namespace
{
 FString LiveNodeAuthoritySnapshot(AEHBBuildingActorBase* B)
 {
  auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("building"),BuildingOutlineCopySnapshot(B));
  TInlineComponentArray<UEHBWallJunctionComponent*> ActualComponents(B);Data->SetNumberField(TEXT("liveJunctionComponents"),ActualComponents.Num());
  TArray<TSharedPtr<FJsonValue>> Meshes;auto Nodes=B->WallNodeAuthority.Nodes;Nodes.Sort([](const auto& A,const auto& C){return A.NodeGuid<C.NodeGuid;});
  for(const auto& N:Nodes)if(auto* C=B->FindWallNodeJunction(N.NodeGuid))
  {
   auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("node"),N.NodeGuid.ToString());M->SetNumberField(TEXT("revision"),C->SourceGeometryRevision);M->SetStringField(TEXT("pose"),C->GetRelativeTransform().ToString());
   TArray<TSharedPtr<FJsonValue>> Sections;for(const auto& S:C->GetMeshSections()){auto Section=FJsonObjectConverter::UStructToJsonObject(S);Section->RemoveField(TEXT("revision"));Sections.Add(MakeShared<FJsonValueObject>(Section));}M->SetArrayField(TEXT("sections"),Sections);Meshes.Add(MakeShared<FJsonValueObject>(M));
  }
  Data->SetArrayField(TEXT("junctions"),Meshes);FString Text;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Text));return Text;
 }
 FString LiveNodeReceipt(AEHBBuildingActorBase* B)
 {
  FString Text;FJsonObjectConverter::UStructToJsonObjectString(B->LastCommittedEdit,Text);return Text;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeEditingActivationTest,"EHB.Topology.NodeEditingActivation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeEditingActivationTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 auto Select=[&](AActor* B){GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);};
 ON_SCOPE_EXIT{EHBNodeEditingActivation::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 // Legacy -> typed -> prepared -> pose-authoritative buildings, with rotated
 // room finishes, all enter the same optional-column model without losing actors.
 for(int32 Variant=0;Variant<6;++Variant)
 {
  FTransientTopologyFixture Fixture;
  if(Variant==0){FActorSpawnParameters P;P.ObjectFlags=RF_Transactional;Fixture.Building=World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(),FVector(0,0,190000),FRotator::ZeroRotator,P);Fixture.Actors.Add(Fixture.Building);}
  else if(!Fixture.Create(World,RF_Transactional))return false;
  auto* B=Fixture.Building;if(!B)return false;B->SetActorRotation(FRotator(0,37,0));Select(B);
  if(Variant==2&&!B->MigrateWallNodeOwnership(true).bSucceeded)return false;
  if(Variant>=3&&!AddCopyRoomOutlines(Fixture,true))return false;
  if(Variant>=4&&!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  if(Variant==5){const auto N=B->WallNodeAuthority.Nodes[0];const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,N.NodeGuid,B->FindPhysicalPillarForNode(N.NodeGuid),N.GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const int32 Count=B->QueryElements(FEHBElementQuery()).Num(),Queue=GEditor->Trans->GetQueueLength();
  auto Execute=[&](bool Preview=false){return UEHBBuildingToolset::EnableWallNodeEditing(B,Preview);};
  const auto Preview=Execute(true);if(!TestTrue(*FString::Printf(TEXT("Variant %d activation preview: %s"),Variant,*Preview.Message),Preview.bSucceeded))return false;
  TestEqual(TEXT("Activation preview preserves complete state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Activation preview has no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
  if(Variant==5){TestEqual(TEXT("Already optional is idempotent"),Execute().Message,FString(TEXT("AlreadyEnabled")));TestEqual(TEXT("Idempotent activation preserves all data"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Idempotent activation has no receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("Idempotent activation has no undo entry"),GEditor->Trans->GetQueueLength(),Queue);continue;}
  const auto PriorNodes=B->WallNodeAuthority.Nodes;
  for(int32 Phase=1;Phase<=(Variant>=3?6:3);++Phase)
  {
   EHBNodeEditingActivation::FailurePhase=Phase;const auto Failed=Execute();TestEqual(TEXT("Injected activation rolls back"),Failed.Message,FString(TEXT("NodeActivationFailedRolledBack")));TestEqual(TEXT("Failure reached its intended phase"),EHBNodeEditingActivation::FailurePhase,0);TestEqual(TEXT("Activation rollback restores exact state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Activation rollback restores receipt"),LiveNodeReceipt(B),Receipt);TestFalse(TEXT("Failed activation cannot redo"),GEditor->Trans->CanRedo());
  }
  const auto Applied=Execute();if(!TestTrue(*FString::Printf(TEXT("Variant %d commit: %s"),Variant,*Applied.Message),Applied.bSucceeded))return false;
  TestEqual(TEXT("Optional node authority enabled"),B->WallNodeAuthority.Version,2);TestEqual(TEXT("Typed ownership enabled"),B->WallNodeOwnership.Version,1);TestEqual(TEXT("No actors created or removed by activation"),B->QueryElements(FEHBElementQuery()).Num(),Count);TestEqual(TEXT("Bound activation creates no derived column substitute"),B->GetInstanceComponents().Num(),0);
  for(const auto& N:PriorNodes){const auto* A=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});TestTrue(TEXT("Existing node identity pose and revision retained"),A&&A->LocalTransform.Equals(N.LocalTransform,0)&&A->GeometryRevision==N.GeometryRevision);}
  FEHBWallNodeModel Actual;TestTrue(TEXT("Activated model captures including empty building"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual).bSucceeded);TestTrue(TEXT("Activated finishes and contacts validate"),FEHBCopyOutlinePolicy::Prepare(B,Actual,B->QueryElements(FEHBElementQuery())).bSucceeded);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestEqual(TEXT("Receipt records activation"),B->LastCommittedEdit.Command,FName(TEXT("EnableWallNodeEditing")));TestTrue(TEXT("Activated rebuild succeeds"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Activated rebuild is exact"),LiveNodeAuthoritySnapshot(B),After);
  TestTrue(TEXT("One undo restores pre-activation building"),GEditor->UndoTransaction());TestEqual(TEXT("Activation undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Activation undo receipt exact"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Activation redo"),GEditor->RedoTransaction());TestEqual(TEXT("Activation redo exact"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Activation redo receipt exact"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  // Corrupt baselines and unversioned payloads are not silently overwritten.
  auto Baseline=B->TopologyMigrationBaseline;B->TopologyMigrationBaseline.Version=99;TestFalse(TEXT("Unknown baseline blocks activation"),Execute(true).bSucceeded);B->TopologyMigrationBaseline=Baseline;
  auto Prepared=B->PreparedWallNodeDefinitions;B->PreparedWallNodeDefinitions.Version=0;B->PreparedWallNodeDefinitions.Nodes.AddDefaulted();TestFalse(TEXT("Unversioned prepared data blocks activation"),Execute(true).bSucceeded);B->PreparedWallNodeDefinitions=Prepared;
  GEditor->SelectNone(false,true,false);TestEqual(TEXT("Unselected activation blocked"),Execute().Message,FString(TEXT("TargetNotSelected")));Select(B);
  if(Variant==1)
  {
   auto Panel=SNew(SEasyHouseBuilderPanel);Panel->SetActiveBuilding(B);Panel->HandleEnableWallNodeEditingClicked();TestEqual(TEXT("Actual panel callback enables selected building"),B->WallNodeAuthority.Version,2);TestTrue(TEXT("Panel callback is one undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Panel activation undo restores state"),LiveNodeAuthoritySnapshot(B),Before);
  }
 }
 FEHBWallNodeModel Empty;Empty.Version=1;TestTrue(TEXT("Explicit empty optional model is valid"),UEHBWallTopologyLibrary::ValidateWallNodeModel(Empty).IsEmpty());
 Empty.Walls.AddDefaulted();TestFalse(TEXT("Empty model with dangling wall is invalid"),UEHBWallTopologyLibrary::ValidateWallNodeModel(Empty).IsEmpty());
 FEHBPreparedWallNodeDefinitions Strict;Strict.Version=1;TestFalse(TEXT("Legacy prepared source still requires nodes"),UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Strict).IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBUnboundNodeEditingTest,"EHB.Topology.UnboundNodeEditing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBUnboundNodeEditingTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!B->MigrateWallNodeOwnership(true).bSucceeded||!B->MigrateWallNodeAuthority(true).bSucceeded||!UEHBWallTopologyLibrary::PrepareTopologyMigration(B,true).bSucceeded)return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto Room=B->GetClosedLoopsByFloor(1)[0].LoopGuid;const FGuid Physical=Fixture.Pillars[0]->ElementGuid,Id=B->FindNodeForPhysicalPillar(Physical);
 auto Revision=[&](){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});return N?N->GeometryRevision:0;};
 auto Remove=[&](bool Preview=false){return UEHBBuildingToolset::RemovePhysicalColumn(B,Id,Physical,Revision(),Preview);};

 FEHBElementRelation Dependency;Dependency.Type=EEHBElementRelationType::LogicalDependency;Dependency.Source=FEHBElementRelationEndpoint::MakeElement(Physical);Dependency.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[0]->ElementGuid);const auto Link=B->AddOrUpdateElementRelation(Dependency);if(!TestTrue(TEXT("Dependency fixture added"),Link.IsValid()))return false;
 const auto WithDependency=LiveNodeAuthoritySnapshot(B);TestEqual(TEXT("Unplanned logical dependency blocks independent deletion"),Remove(true).Message,FString(TEXT("RequiresNodeDependencyPlan")));TestEqual(TEXT("Dependency rejection is read-only"),LiveNodeAuthoritySnapshot(B),WithDependency);B->RemoveElementRelation(Link);
 const auto Before=LiveNodeAuthoritySnapshot(B);const auto Receipt=B->LastCommittedEdit;const int32 Queue=GEditor->Trans->GetQueueLength();
 const auto Preview=Remove(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestEqual(TEXT("Physical removal preview preserves all data"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview does not create undo"),GEditor->Trans->GetQueueLength(),Queue);
 for(int32 Phase=1;Phase<=3;++Phase)
 {
  EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Remove();TestEqual(TEXT("Physical removal failure rolls back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Rollback restores physical actor logical IDs geometry and derived components"),LiveNodeAuthoritySnapshot(B),Before);TestNotNull(TEXT("Physical actor restored by failed transaction"),B->FindElementActorByGuid(Physical));TestEqual(TEXT("Failed edit receipt unchanged"),B->LastCommittedEdit.StateId,Receipt.StateId);TestFalse(TEXT("Failed delete cannot redo"),GEditor->Trans->CanRedo());
 }
 const auto Applied=Remove();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
 TestEqual(TEXT("Optional binding authority enabled explicitly"),B->WallNodeAuthority.Version,2);TestNull(TEXT("Physical Actor truly absent"),B->FindElementActorByGuid(Physical));TestEqual(TEXT("No hidden replacement Actor"),B->QueryElements(FEHBElementQuery()).Num(),7);TestEqual(TEXT("All four walls retained"),UEHBWallTopologyLibrary::CaptureWallTopology(B).Walls.Num(),4);
 auto* Fill=B->FindWallNodeJunction(Id);if(!TestNotNull(TEXT("Live building owns derived junction"),Fill))return false;TestTrue(TEXT("Derived fill has no separate physical element identity"),Fill->GetOwner()==B&&Fill->HasAnyFlags(RF_Transient));TestTrue(TEXT("Junction is registered and has geometry"),Fill->IsRegistered()&&Fill->GetNumSections()>0&&Fill->GetProcMeshSection(0)->ProcIndexBuffer.Num()>0);
 FEHBWallNodeModel Model;TestTrue(TEXT("Live model captures absent physical binding"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestEqual(TEXT("One physical binding removed"),Model.PillarBindings.Num(),3);
 const auto Rooms=B->FindClosedLoopsContainingWorldLocation(B->GetActorTransform().TransformPosition(FVector(300,250,0)),1);TestTrue(TEXT("Runtime location query keeps same room without column"),Rooms.Num()==1&&Rooms[0].LoopGuid==Room);
 const auto Removed=LiveNodeAuthoritySnapshot(B);TestEqual(TEXT("Repeated removal is no-op with current revision"),Remove().Message,FString(TEXT("NoChange")));TestEqual(TEXT("No-op preserves snapshot"),LiveNodeAuthoritySnapshot(B),Removed);
 TestTrue(TEXT("Undo actual column removal"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores physical actor and removes derived fill"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo actual column removal"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores node identity and exact geometry"),LiveNodeAuthoritySnapshot(B),Removed);
 FEasyHouseEditorMode Mode;TestTrue(TEXT("Actual editor mode selects the actor-free node"),Mode.SelectUnboundWallNode(B,Id));TestTrue(TEXT("Node exposes translate widget"),Mode.ShouldDrawWidget()&&Mode.UsesTransformWidget(UE::Widget::WM_Translate));TestFalse(TEXT("Node rejects rotate widget"),Mode.UsesTransformWidget(UE::Widget::WM_Rotate));
 struct FNodePDI : FPrimitiveDrawInterface
 {
  TArray<FVector> Points;int32 ProxyCount=0;TRefCountPtr<HHitProxy> Current;
  FNodePDI():FPrimitiveDrawInterface(nullptr){}
  bool IsHitTesting() override{return true;}void SetHitProxy(HHitProxy* P) override{Current=P;if(P)++ProxyCount;}
  void RegisterDynamicResource(FDynamicPrimitiveResource*) override{}void AddReserveLines(uint8,int32,bool,bool) override{}
  void DrawSprite(const FVector&,float,float,const FTexture*,const FLinearColor&,uint8,float,float,float,float,uint8,float) override{}
  void DrawLine(const FVector&,const FVector&,const FLinearColor&,uint8,float,float,bool) override{}
  void DrawTranslucentLine(const FVector&,const FVector&,const FLinearColor&,uint8,float,float,bool) override{}
  void DrawPoint(const FVector& P,const FLinearColor&,float,uint8) override{Points.Add(P);}int32 DrawMesh(const FMeshBatch&) override{return 0;}
 } PDI;
 Mode.DrawUnboundWallNodeControls(&PDI);TestEqual(TEXT("Viewport emits one selectable actor-free node handle"),PDI.ProxyCount,1);TestTrue(TEXT("Selectable marker matches widget location"),PDI.Points.Num()==1&&PDI.Points[0].Equals(Mode.GetWidgetLocation(),1.e-6));

 FEditorViewportClient Client(&GLevelEditorModeTools());Client.SetCurrentWidgetAxis(EAxisList::X);
 TestTrue(TEXT("Actual mode begins node drag without transaction"),Mode.StartTracking(&Client,nullptr));TestFalse(TEXT("Begin remains a value draft"),GEditor->IsTransactionActive());FVector Delta=B->GetActorTransform().TransformVectorNoScale(FVector(-45,-20,0)),Scale=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;
 TestTrue(TEXT("Actual delta callback handles logical node"),Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale));TestTrue(*Mode.NodeHandleDrag.Feedback.Message,Mode.NodeHandleDrag.Feedback.bSucceeded);TestEqual(TEXT("Dragging is read-only before release"),LiveNodeAuthoritySnapshot(B),Removed);
 TestTrue(TEXT("Actual Escape cancels logical node drag"),Mode.InputKey(nullptr,nullptr,EKeys::Escape,IE_Pressed));TestTrue(TEXT("Release cancelled drag"),Mode.EndTracking(nullptr,nullptr));TestEqual(TEXT("Cancel restores original values without mutation"),LiveNodeAuthoritySnapshot(B),Removed);
 TestTrue(TEXT("Actual mode begins second node drag"),Mode.StartTracking(&Client,nullptr));Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);TestTrue(TEXT("Actual release commits logical node"),Mode.EndTracking(nullptr,nullptr));
 const auto Moved=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Released node actually moved"),Moved!=Removed);TestEqual(TEXT("Release receipt"),B->LastCommittedEdit.Command,FName(TEXT("MoveUnboundNode")));TestEqual(TEXT("Moving does not resurrect pillar"),B->QueryElements(FEHBElementQuery()).Num(),7);
 TestTrue(TEXT("Undo logical node movement"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores geometry and node revisions"),LiveNodeAuthoritySnapshot(B),Removed);TestTrue(TEXT("Redo logical node movement"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores exact movement"),LiveNodeAuthoritySnapshot(B),Moved);GEditor->UndoTransaction(false);
 const auto* Node=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});const auto Start=Node->LocalTransform.GetLocation();
 for(int32 Phase=1;Phase<=3;++Phase){EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=UEHBBuildingToolset::MoveUnboundWallNode(B,Id,Revision(),Start,Start+FVector(-45,-20,0),false);TestEqual(TEXT("Unbound movement fault restores transaction"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Fault restores complete model and junction geometry"),LiveNodeAuthoritySnapshot(B),Removed);}
 TestFalse(TEXT("Stale node revision rejected"),UEHBBuildingToolset::MoveUnboundWallNode(B,Id,Revision()+1,Start,Start+FVector(-45,-20,0),false).bSucceeded);
 TestFalse(TEXT("Vertical movement rejected"),UEHBBuildingToolset::MoveUnboundWallNode(B,Id,Revision(),Start,Start+FVector(0,0,20),false).bSucceeded);
 // Delete the remaining physical columns through the same command: the building is
 // still represented by real wall Actors and derived junction components, not proxy pillars.
 const auto Bindings=B->WallNodeOwnership.Bindings;
 for(const auto& Binding:Bindings){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
 TestEqual(TEXT("Zero pillar Actors remain"),B->QueryElements(FEHBElementQuery()).Num(),4);TestTrue(TEXT("No physical bindings remain"),B->WallNodeOwnership.Bindings.IsEmpty());TestTrue(TEXT("All-unbound live model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);
 TestEqual(TEXT("Room retained with zero pillars"),B->GetClosedLoopsByFloor(1)[0].LoopGuid,Room);const auto AllUnbound=LiveNodeAuthoritySnapshot(B);B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Explicit node geometry rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Full rebuild deterministic with zero physical actors"),LiveNodeAuthoritySnapshot(B),AllUnbound);
 FEHBRelationQuery Query;Query.NodeGuid=Id;TestEqual(TEXT("Logical node still knows two incident walls"),B->QueryElementRelations(Query).Num(),2);

 FEHBElementRelation InvalidSupport;InvalidSupport.Type=EEHBElementRelationType::StructuralSupport;InvalidSupport.Source=FEHBElementRelationEndpoint::MakeNode(Id);InvalidSupport.Target=FEHBElementRelationEndpoint::MakeElement(Fixture.Walls[0]->ElementGuid);TestFalse(TEXT("Actor-free junction cannot become a physical support"),B->AddOrUpdateElementRelation(InvalidSupport).IsValid());
 FTransientTopologyFixture Restored;if(!Restored.Create(B->GetWorld(),RF_Transactional)||!Restored.Building->MigrateWallNodeOwnership(true).bSucceeded||!Restored.Building->MigrateWallNodeAuthority(true).bSucceeded)return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Restored.Building,true,false);auto* RestoredPillar=Restored.Pillars[0];const FGuid RestoredId=Restored.Building->FindNodeForPhysicalPillar(RestoredPillar->ElementGuid);
 const auto* RN=Restored.Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==RestoredId;});const auto RemovedAgain=UEHBBuildingToolset::RemovePhysicalColumn(Restored.Building,RestoredId,RestoredPillar->ElementGuid,RN->GeometryRevision,false);if(!TestTrue(*RemovedAgain.Message,RemovedAgain.bSucceeded))return false;
 TestTrue(TEXT("Restore column for normal delete lifecycle"),GEditor->UndoTransaction(false));RestoredPillar->Destroy();TestEqual(TEXT("Normal full deletion still removes incident walls after undo restored column"),Restored.Building->QueryElements(FEHBElementQuery()).Num(),5);TestTrue(TEXT("Normal deletion leaves no dangling topology"),UEHBWallTopologyLibrary::CaptureWallTopology(Restored.Building).Issues.IsEmpty());
 FTransientTopologyFixture Finished;if(!Finished.Create(B->GetWorld(),RF_Transactional)||!AddCopyRoomOutlines(Finished,true)||!Finished.Building->MigrateWallNodeAuthority(true).bSucceeded)return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(Finished.Building,true,false);const auto* FN=Finished.Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Finished.Building->FindNodeForPhysicalPillar(Finished.Pillars[0]->ElementGuid);});
 if(!TestNotNull(TEXT("Resolve room finish node"),FN))return false;const auto FinishBefore=BuildingOutlineCopySnapshot(Finished.Building);
 TestTrue(TEXT("Actual room floor slab contacts have an explicit unbinding plan"),UEHBBuildingToolset::RemovePhysicalColumn(Finished.Building,FN->NodeGuid,Finished.Pillars[0]->ElementGuid,FN->GeometryRevision,true).bSucceeded);TestEqual(TEXT("Dependency preview preserves room floor slab and contacts"),BuildingOutlineCopySnapshot(Finished.Building),FinishBefore);
 Mode.NodeHandleDrag={};Mode.SelectedWallNode.Invalidate();Mode.NodeHandleBuilding.Reset();GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBUnboundNodeFinishTest,"EHB.Topology.UnboundNodeFinishDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBUnboundNodeFinishTest::RunTest(const FString& Parameters)
{
 for(bool SurfaceFinish:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,SurfaceFinish))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  if(SurfaceFinish)for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E)){auto Regions=F->FloorRegions;Algo::Reverse(Regions[0].OuterPolygon);if(!F->SetFloorRegions(Regions,false))return false;F->RecordOutlineSource(EEHBOutlineSource::RoomSupportFill);}
   else if(auto* S=Cast<AEHB_FloorSlab>(E)){auto Polygon=S->LocalTopPolygon;Algo::Reverse(Polygon);if(!S->SetSlabOutline(Polygon,{}))return false;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  }
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const FGuid Physical=Fixture.Pillars[0]->ElementGuid,Id=B->FindNodeForPhysicalPillar(Physical);
  auto Node=[&](){return B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});};
  auto Remove=[&](bool Preview=false){return UEHBBuildingToolset::RemovePhysicalColumn(B,Id,Physical,Node()->GeometryRevision,Preview);};
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeReceipt=LiveNodeReceipt(B);const auto OriginalRooms=B->GetClosedLoopsByFloor(1);
  auto Winding=[](const TArray<FVector>& P){double A=0;for(int32 I=0;I<P.Num();++I)A+=P[I].X*P[(I+1)%P.Num()].Y-P[I].Y*P[(I+1)%P.Num()].X;return A;};TMap<FGuid,double> OriginalWinding;
  for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* F=Cast<AEHB_Floor>(E))OriginalWinding.Add(F->ElementGuid,Winding(F->LocalFloorPolygon));else if(auto* S=Cast<AEHB_FloorSlab>(E))OriginalWinding.Add(S->ElementGuid,Winding(S->LocalTopPolygon));}
  auto* SampleFloor=Cast<AEHB_Floor>(*B->QueryElements(FEHBElementQuery()).FindByPredicate([](const auto* E){return E->IsA<AEHB_Floor>();}));
  const auto Provenance=SampleFloor->OutlineSource;SampleFloor->OutlineSource=EEHBOutlineSource::ManualOrUnclassified;TestFalse(TEXT("Manual unclassified floor cannot silently follow node edit"),Remove(true).bSucceeded);SampleFloor->OutlineSource=Provenance;
  if(SurfaceFinish){auto* Contact=B->ElementRelations.FindByPredicate([](const auto& R){return R.Type==EEHBElementRelationType::SurfaceFinish;});if(!Contact)return false;const float Area=Contact->ContactArea;Contact->ContactArea+=20;TestEqual(TEXT("Stale contact metadata rejected before geometry"),Remove(true).Message,FString(TEXT("StaleFinishContacts")));Contact->ContactArea=Area;}
  TestEqual(TEXT("Rejected previews preserve original building"),LiveNodeAuthoritySnapshot(B),Before);
  auto CheckRooms=[&]()
  {
   TestEqual(TEXT("Two adjacent room IDs preserved"),B->GetClosedLoopsByFloor(1).Num(),OriginalRooms.Num());
   for(const auto& R:OriginalRooms){FEHBNodeRoomBoundary Boundary;TestTrue(TEXT("Public runtime logical room boundary resolves"),B->TryGetRoomBoundary(R.LoopGuid,R.FloorIndex,Boundary));TestEqual(TEXT("Logical cycle has paired polygon and wall slots"),Boundary.NodeGuids.Num(),Boundary.Polygon.Num());TestEqual(TEXT("Logical cycle has paired walls"),Boundary.NodeGuids.Num(),Boundary.WallGuids.Num());for(FGuid N:Boundary.NodeGuids)TestTrue(TEXT("Logical boundary never contains invalid physical IDs"),N.IsValid());}
   for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* F=Cast<AEHB_Floor>(E))TestTrue(TEXT("Floor control winding remains authored"),OriginalWinding.FindChecked(F->ElementGuid)*Winding(F->LocalFloorPolygon)>0);else if(auto* S=Cast<AEHB_FloorSlab>(E))TestTrue(TEXT("Slab control winding remains authored"),OriginalWinding.FindChecked(S->ElementGuid)*Winding(S->LocalTopPolygon)>0);}
  };
  const auto Preview=Remove(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestEqual(TEXT("Full finish preflight is read only"),LiveNodeAuthoritySnapshot(B),Before);
  for(int32 Phase=1;Phase<=6;++Phase){EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Remove();TestEqual(TEXT("Every physical deletion finish phase rolls back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Full floor slab contact node and mesh restored"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Failed composite receipt restored"),LiveNodeReceipt(B),BeforeReceipt);TestFalse(TEXT("Failed finish operation cannot redo"),GEditor->Trans->CanRedo());}
  const auto Applied=Remove();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  const auto Removed=LiveNodeAuthoritySnapshot(B),RemovedReceipt=LiveNodeReceipt(B);CheckRooms();TestNull(TEXT("Physical host actually removed"),B->FindElementActorByGuid(Physical));
  for(const auto& R:B->ElementRelations)TestFalse(TEXT("No contact retains deleted physical host or invents logical support"),R.Source.RefersToElement(Physical)||(R.Type==EEHBElementRelationType::SurfaceFinish&&R.Source.Kind==EEHBRelationEndpointKind::WallNode));
  TestTrue(TEXT("Undo complete deletion with finishes"),GEditor->UndoTransaction());TestEqual(TEXT("Undo all source data and meshes"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo complete deletion with finishes"),GEditor->RedoTransaction());TestEqual(TEXT("Redo all dependent identities and meshes"),LiveNodeAuthoritySnapshot(B),Removed);
  const auto Start=Node()->LocalTransform.GetLocation();auto Move=[&](){return UEHBBuildingToolset::MoveUnboundWallNode(B,Id,Node()->GeometryRevision,Start,Start+FVector(-45,-20,0),false);};
  for(int32 Phase=1;Phase<=6;++Phase){EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Move();TestEqual(TEXT("Every movement finish phase rolls back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Move rollback recovers all finishes and physical contacts"),LiveNodeAuthoritySnapshot(B),Removed);TestEqual(TEXT("Move rollback receipt"),LiveNodeReceipt(B),RemovedReceipt);}
  FEasyHouseEditorMode Mode;TestTrue(TEXT("Select unbound node with finished rooms"),Mode.SelectUnboundWallNode(B,Id));FEditorViewportClient Client(&GLevelEditorModeTools());Client.SetCurrentWidgetAxis(EAxisList::X);TestTrue(TEXT("Begin actual finished-room node drag"),Mode.StartTracking(&Client,nullptr));
  FVector Delta=B->GetActorTransform().TransformVectorNoScale(FVector(-45,-20,0)),Scale=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);TestEqual(TEXT("Live finished-room drag remains read-only"),LiveNodeAuthoritySnapshot(B),Removed);Mode.EndTracking(nullptr,nullptr);
  const auto Moved=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Release moves logical corner and dependent outlines"),Moved!=Removed);CheckRooms();TestEqual(TEXT("Composite release receipt"),B->LastCommittedEdit.Command,FName(TEXT("MoveUnboundNode")));
  TestTrue(TEXT("Undo release with floor slab contacts"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores all dependent data"),LiveNodeAuthoritySnapshot(B),Removed);TestTrue(TEXT("Redo release with floor slab contacts"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact dependent data"),LiveNodeAuthoritySnapshot(B),Moved);
  const auto Bindings=B->WallNodeOwnership.Bindings;
  for(const auto& Binding:Bindings){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  TestTrue(TEXT("Finished adjacent rooms support zero physical columns"),B->WallNodeOwnership.Bindings.IsEmpty());CheckRooms();
  TArray<FEHBNodeRoomBoundary> Boundaries;for(const auto& R:OriginalRooms){FEHBNodeRoomBoundary Boundary;B->TryGetRoomBoundary(R.LoopGuid,1,Boundary);Boundaries.Add(Boundary);}
  B->RebuildElementAndRelationshipIndexes();for(const auto& R:Boundaries){FEHBNodeRoomBoundary Boundary;B->TryGetRoomBoundary(R.RoomGuid,1,Boundary);TestTrue(TEXT("Zero-binding logical cycle stable across rebuild"),R.NodeGuids==Boundary.NodeGuids&&R.WallGuids==Boundary.WallGuids&&R.Polygon==Boundary.Polygon);}
  FEHBWallNodeModel Model;TestTrue(TEXT("Optional node model with finishes valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery()),true);TestTrue(*Policy.Status.ToString(),Policy.bSucceeded);
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* NewFloor=B->GetWorld()->SpawnActor<AEHB_Floor>(Params);Fixture.Actors.Add(NewFloor);TestTrue(TEXT("New floor can initialize from room without pillar actors"),NewFloor&&NewFloor->ConfigureFromRoomLoop(B,B->GetClosedLoopsByFloor(1)[0],0,true));
  auto* NewSlab=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(Params);Fixture.Actors.Add(NewSlab);if(!NewSlab)return false;NewSlab->ConfigureDefaultSlab(B,FTransform(FVector(300,250,300)),100,20,false);NewSlab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);TestTrue(TEXT("Actual room slab auto-fill works with zero physical columns"),FEasyHouseEditorMode::FillFloorSlabRoomForToolset(NewSlab));
  GEditor->SelectNone(false,true,false);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMoveRoomNotificationTest,"EHB.Topology.NodeMoveRoomNotifications",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeMoveRoomNotificationTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 // 0 detached/no finishes, 1 detached/finished, 2 detached/finish repair,
 // 3 shared wall, 4 detached/full structural recovery, 5 rollback after receipt, 6 stale remote contact revision.
 for(int32 BindingMode=0;BindingMode<3;++BindingMode)for(int32 Case=0;Case<7;++Case)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true,Case!=3,Case!=0))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!TestTrue(TEXT("Activate scoped room fixture"),EHBNodeEditingActivation::Execute(B,false).bSucceeded))return false;
  const FGuid First=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid),Second=B->FindNodeForPhysicalPillar(Fixture.Pillars[3]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==First))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  if(!B->RebuildWallNodeAuthorityGeometry())return false;
  FEHBWallNodeModel Source;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source).bSucceeded)return false;
  TArray<FEHBNodeRoomBoundary> Rooms;FName Reason;if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Source,Rooms,Reason))return false;
  FGuid Near,Far;for(const auto& R:Rooms){if(R.NodeGuids.Contains(First))Near=R.RoomGuid;else Far=R.RoomGuid;}if(!Near.IsValid()||!Far.IsValid())return false;
  auto CheckQueries=[&]()
  {
   TArray<FEHBRoomDependencyMembers> Members;const auto Loops=B->GetClosedLoopsByFloor(1);TestTrue(TEXT("Room dependencies remain queryable"),B->QueryRoomDependencies(Loops,Members));TestEqual(TEXT("Both room memberships remain present"),Members.Num(),2);
   for(const auto& R:Rooms){FEHBNodeRoomBoundary Actual;TestTrue(TEXT("Stable room identity still resolves"),B->TryGetRoomBoundary(R.RoomGuid,1,Actual));if(R.RoomGuid==Far)TestTrue(TEXT("Unaffected centerline boundary remains exact"),Actual.Polygon==R.Polygon);}
   for(const auto& M:Members){TestEqual(TEXT("Floor membership retained"),M.Floors.Num(),Case==0?0:1);TestEqual(TEXT("Slab membership retained"),M.Slabs.Num(),Case==0?0:1);}
  };
  EHBRoomFinishMove::FNodeEditPlan Warm;if(!Warm.Prepare(B,Source,Source,B->QueryElements(FEHBElementQuery()),{},Reason)||!Warm.Apply(B,Reason))return false;
  CheckQueries();const auto CacheBefore=B->GetRoomDependencyCacheStats();
  if(Case==2)for(auto* E:B->QueryElements(FEHBElementQuery()))
  {if(auto* F=Cast<AEHB_Floor>(E);F&&F->RoomLoopGuid==Far)F->MeshComponent->GetProcMeshSection(0)->ProcVertexBuffer[0].Normal=FVector::ForwardVector;}
  if(Case==4){const auto* FarRoom=Rooms.FindByPredicate([&](const auto& R){return R.RoomGuid==Far;});CastChecked<AEHB_Wall>(B->FindElementActorByGuid(FarRoom->WallGuids[0]))->LeftWallMeshComponent->ClearAllMeshSections();}
  if(Case==6)for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E);F&&F->RoomLoopGuid==Far&&!F->SurfaceFinishRelationGuids.IsEmpty())
   {auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==F->SurfaceFinishRelationGuids[0];});if(!R)return false;--R->SourceGeometryRevision;break;}
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeReceipt=LiveNodeReceipt(B);int32 Events=0;FEHBCommittedEdit Observed;
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const FEHBCommittedEdit& R){++Events;Observed=R;};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
  ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);};
  TArray<FEHBNodeMoveRequest> Requests;TMap<FGuid,int32> Revisions;
  for(FGuid Id:{First,Second}){const auto* N=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});auto& R=Requests.AddDefaulted_GetRef();R.NodeGuid=Id;R.ExpectedPosition=N->LocalTransform.GetLocation();R.TargetPosition=R.ExpectedPosition+FVector(-40,0,0);Revisions.Add(Id,N->GeometryRevision);}
  if(Case==5)EHBNodeAuthorityEditing::FailurePhase=3;
  const auto Applied=EHBNodeAuthorityEditing::ExecuteMoves(B,Requests,Revisions,false);
  if(Case==5)
  {
   TestEqual(TEXT("Receipt-phase failure rolls back"),Applied.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Rollback emits no room notification"),Events,0);TestEqual(TEXT("Rollback restores receipt"),LiveNodeReceipt(B),BeforeReceipt);TestEqual(TEXT("Rollback restores scene"),LiveNodeAuthoritySnapshot(B),Before);CheckQueries();TestEqual(TEXT("Rollback does not advance membership receipt"),B->GetRoomDependencyCacheStats().ReceiptUpdates,CacheBefore.ReceiptUpdates);continue;
  }
  if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  TestEqual(TEXT("Only one composite room notification"),Events,1);TestTrue(TEXT("Changed room is notified"),Observed.RoomGuids.Contains(Near));
  const bool ExpectFar=Case==2||Case==3||Case==4||Case==6;
  TestEqual(TEXT("Remote or shared room coverage matches actual affected scope"),Observed.RoomGuids.Contains(Far),ExpectFar);TestEqual(TEXT("No fabricated or duplicate room notifications"),Observed.RoomGuids.Num(),ExpectFar?2:1);
  TestEqual(TEXT("Structural recovery uses conservative full scope"),EHBNodeAuthorityEditing::LastGeometryRefresh.bUsedScopedUpdate,Case!=4);
  CheckQueries();TestEqual(TEXT("Actual receipt advances dependency membership safely"),B->GetRoomDependencyCacheStats().ReceiptUpdates,CacheBefore.ReceiptUpdates+1);
  const auto After=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo room notification move"),GEditor->UndoTransaction());CheckQueries();TestEqual(TEXT("Undo restores prior receipt"),LiveNodeReceipt(B),BeforeReceipt);
  TestTrue(TEXT("Redo room notification move"),GEditor->RedoTransaction());CheckQueries();TestEqual(TEXT("Redo restores exact room receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("Redo preserves complete scene"),LiveNodeAuthoritySnapshot(B),After);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMoveFinishOutputTest,"EHB.Topology.NodeMoveFinishOutputs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeMoveFinishOutputTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(int32 BindingMode=0;BindingMode<3;++BindingMode)for(bool Damage:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!TestTrue(TEXT("Activate finished move fixture"),EHBNodeEditingActivation::Execute(B,false).bSucceeded))return false;
  const FGuid First=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid),Second=B->FindNodeForPhysicalPillar(Fixture.Pillars[3]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==First))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  AEHB_Floor* FarFloor=nullptr;AEHB_FloorSlab* FarSlab=nullptr;
  for(auto* E:B->QueryElements(FEHBElementQuery()))
  {
   if(auto* F=Cast<AEHB_Floor>(E)){double X=0;for(auto P:F->LocalFloorPolygon)X+=P.X;if(X/F->LocalFloorPolygon.Num()>650)FarFloor=F;}
   if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->GetElementLocalTransform().GetLocation().X>650)FarSlab=S;
  }
  if(!TestNotNull(TEXT("Unaffected room floor"),FarFloor)||!TestNotNull(TEXT("Unaffected room slab"),FarSlab))return false;
  auto* Surface=CastChecked<UEHBPlanarSurfaceComponent>(FarSlab->MeshComponent);FName Reason;FEHBWallNodeModel Source;
  if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source).bSucceeded)return false;
  EHBRoomFinishMove::FNodeEditPlan Warm;if(!Warm.Prepare(B,Source,Source,B->QueryElements(FEHBElementQuery()),{},Reason)||!Warm.Apply(B,Reason))return false;
  const auto WarmState=LiveNodeAuthoritySnapshot(B);const int32 Graph=B->RelationshipGraphRevision;
  const uint64 FloorCalls=FarFloor->MeshComponent->GetSubmittedSectionUpdateCount()+FarFloor->MeshComponent->GetSkippedIdenticalSectionUpdateCount();const int32 SurfaceRevision=Surface->SurfaceGeometryRevision;
  EHBRoomFinishMove::FNodeEditPlan NoChange;if(!NoChange.Prepare(B,Source,Source,B->QueryElements(FEHBElementQuery()),{},Reason)||!NoChange.Apply(B,Reason))return false;
  TestEqual(TEXT("Identical plan does not submit floor geometry"),FarFloor->MeshComponent->GetSubmittedSectionUpdateCount()+FarFloor->MeshComponent->GetSkippedIdenticalSectionUpdateCount(),FloorCalls);
  TestEqual(TEXT("Identical plan does not publish slab surface revision"),Surface->SurfaceGeometryRevision,SurfaceRevision);TestEqual(TEXT("Identical contacts do not rewrite relationship graph"),B->RelationshipGraphRevision,Graph);TestEqual(TEXT("No-change finish plan preserves complete scene"),LiveNodeAuthoritySnapshot(B),WarmState);
  TArray<FGuid> ReportedRooms={FGuid::NewGuid()};if(!NoChange.Apply(B,Reason,nullptr,&ReportedRooms))return false;TestTrue(TEXT("Successful unchanged finish apply reports no rooms"),ReportedRooms.IsEmpty());
  for(int32 Phase:{4,5,6})
  {
   ReportedRooms={FGuid::NewGuid()};EHBNodeAuthorityEditing::FailurePhase=Phase;
   TestFalse(TEXT("Failed finish apply does not publish partial room scope"),NoChange.Apply(B,Reason,&EHBNodeAuthorityEditing::FailAt,&ReportedRooms));TestTrue(TEXT("Failed finish apply clears prior room scope"),ReportedRooms.IsEmpty());
  }
  if(Damage){FarFloor->MeshComponent->GetProcMeshSection(0)->ProcVertexBuffer[0].Normal=FVector::ForwardVector;Surface->LocalBoundaryLoop[0].X+=10;FarSlab->MeshComponent->GetProcMeshSection(0)->ProcVertexBuffer[0].UV0.X+=1;}
  const auto Before=LiveNodeAuthoritySnapshot(B);
  TArray<FEHBNodeMoveRequest> Requests;TMap<FGuid,int32> Revisions;
  for(FGuid Id:{First,Second}){const auto* N=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});auto& R=Requests.AddDefaulted_GetRef();R.NodeGuid=Id;R.ExpectedPosition=N->LocalTransform.GetLocation();R.TargetPosition=R.ExpectedPosition+FVector(-40,0,0);Revisions.Add(Id,N->GeometryRevision);}
  const auto Applied=EHBNodeAuthorityEditing::ExecuteMoves(B,Requests,Revisions,false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  if(!Damage)
  {
   TestEqual(TEXT("Unchanged adjacent floor avoids mesh submission"),FarFloor->MeshComponent->GetSubmittedSectionUpdateCount()+FarFloor->MeshComponent->GetSkippedIdenticalSectionUpdateCount(),FloorCalls);
   TestEqual(TEXT("Unchanged adjacent slab avoids surface notification"),Surface->SurfaceGeometryRevision,SurfaceRevision);
  }
  else
  {
   TestTrue(TEXT("Unchanged floor with damaged mesh is repaired"),FarFloor->MeshComponent->GetProcMeshSection(0)->ProcVertexBuffer[0].Normal==FVector::UpVector);
   TestTrue(TEXT("Damaged slab metadata and mesh are regenerated"),Surface->SurfaceGeometryRevision>SurfaceRevision);
   bool Changed=true;if(!FarSlab->SetSlabOutlineIfNeeded(FarSlab->LocalTopPolygon,Changed))return false;TestFalse(TEXT("Repaired slab matches its source on next check"),Changed);
  }
  const auto After=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Changed room was updated"),After!=Before);TestTrue(TEXT("Undo complete local finish move"),GEditor->UndoTransaction());
  // Undo repairs derived geometry from source; corrupted derived input is not saved authority.
  TestEqual(TEXT("Undo restores pre-move authored geometry and repaired output"),LiveNodeAuthoritySnapshot(B),WarmState);
  TestTrue(TEXT("Redo complete local finish move"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact final finish output"),LiveNodeAuthoritySnapshot(B),After);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeMovePreparationTest,"EHB.Topology.NodeMovePreparation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeMovePreparationTest::RunTest(const FString& Parameters)
{
 for(bool HasFinishes:{false,true})for(int32 BindingMode=0;BindingMode<3;++BindingMode)
 {
  FTransientTopologyFixture Fixture;if(!TestTrue(TEXT("Create preparation fixture"),Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional))||(HasFinishes&&!TestTrue(TEXT("Create dependent finish fixture"),AddCopyRoomOutlines(Fixture,true))))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!TestTrue(TEXT("Prepare physical ownership for fixture with or without finishes"),B->MigrateWallNodeOwnership(true).bSucceeded)||!TestTrue(TEXT("Prepare authority for planning fixture"),B->MigrateWallNodeAuthority(true).bSucceeded))return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Bindings=B->WallNodeOwnership.Bindings;
  for(int32 I=0;I<Bindings.Num();++I)if(BindingMode==0||(BindingMode==1&&I%2==0))
  {const auto& V=Bindings[I];const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& R){return R.NodeGuid==V.NodeGuid;});const auto Removed=UEHBBuildingToolset::RemovePhysicalColumn(B,V.NodeGuid,V.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;}
  FEHBWallNodeModel Source;if(!TestTrue(TEXT("Capture planning fixture"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source).bSucceeded))return false;
  FEHBNodeMoveRequest Move;Move.NodeGuid=Source.Nodes[0].NodeGuid;Move.ExpectedPosition=Source.Nodes[0].LocalTransform.GetLocation();Move.TargetPosition=Move.ExpectedPosition+FVector(-25,-30,0);
  const auto Candidate=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Source,{Move});if(!TestTrue(*Candidate.Status.ToString(),Candidate.bSucceeded))return false;
  const auto Before=LiveNodeAuthoritySnapshot(B);FName Reason;EHBRoomFinishMove::FNodeEditPlan Plan;
  const bool Prepared=Plan.Prepare(B,Source,Candidate.Definitions,B->QueryElements(FEHBElementQuery()),{},Reason);if(!TestTrue(*Reason.ToString(),Prepared))return false;
  FString Expected,Actual;FJsonObjectConverter::UStructToJsonObjectString(Candidate.Definitions,Expected);FJsonObjectConverter::UStructToJsonObjectString(Plan.GetCandidateSides().GetModel(),Actual);TestEqual(TEXT("Plan retains the exact same candidate as the move"),Actual,Expected);
  TArray<FEHBNodeRoomBoundary> Rooms;if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Candidate.Definitions,Rooms,Reason))return false;
  TestEqual(TEXT("Plan rooms equal independent candidate room count"),Plan.GetRooms().Num(),Rooms.Num());
  for(const auto& Room:Rooms){const auto* Found=Plan.GetRooms().FindByPredicate([&](const auto& R){return R.RoomGuid==Room.RoomGuid;});TestTrue(TEXT("Plan preserves exact room cycle and boundary"),Found&&Found->NodeGuids==Room.NodeGuids&&Found->WallGuids==Room.WallGuids&&Found->Polygon==Room.Polygon&&Found->FloorIndex==Room.FloorIndex);}
  TestTrue(TEXT("Only a plan with floor slab consumers has finish work"),HasFinishes?(!Plan.Floors.IsEmpty()&&!Plan.Slabs.IsEmpty()&&!Plan.Tops.IsEmpty()):(Plan.Floors.IsEmpty()&&Plan.Slabs.IsEmpty()&&Plan.Tops.IsEmpty()));
  auto Bad=Candidate.Definitions;Bad.Walls[0].EndNodeGuid=FGuid::NewGuid();TestFalse(TEXT("Invalid subsequent preparation is rejected"),Plan.Prepare(B,Source,Bad,B->QueryElements(FEHBElementQuery()),{},Reason));
  TestTrue(TEXT("Failed reprepare cannot leak previous rooms geometry or finish plans"),Plan.GetRooms().IsEmpty()&&!Plan.GetCandidateSides().IsReady()&&Plan.Floors.IsEmpty()&&Plan.Slabs.IsEmpty()&&Plan.Tops.IsEmpty()&&Plan.OwnedRelations.IsEmpty());
  TestEqual(TEXT("Successful and failed preparation never write scene geometry"),LiveNodeAuthoritySnapshot(B),Before);
  GEditor->SelectNone(false,true,false);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBMixedNodeEditingTest,"EHB.Topology.MixedNodeEditing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBMixedNodeEditingTest::RunTest(const FString& Parameters)
{
 FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
 auto Select=[](AActor* A){GEditor->SelectNone(false,true,false);if(A)GEditor->SelectActor(A,true,false);};Select(B);
 const FGuid RemovedPhysical=Fixture.Pillars[0]->ElementGuid,Unbound=B->FindNodeForPhysicalPillar(RemovedPhysical);auto* P=Fixture.Pillars[1];const FGuid Bound=B->FindNodeForPhysicalPillar(P->ElementGuid);auto* W=Fixture.Walls[0];
 auto Node=[&](FGuid Id){return B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});};
 const auto Removed=UEHBBuildingToolset::RemovePhysicalColumn(B,Unbound,RemovedPhysical,Node(Unbound)->GeometryRevision,false);if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;
 const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const auto Rooms=B->GetClosedLoopsByFloor(1);
 auto Requests=[&](FVector Delta){TArray<FEHBVersionedNodeMoveRequest> Moves;for(FGuid Id:{Unbound,Bound}){auto& M=Moves.AddDefaulted_GetRef();M.NodeGuid=Id;M.ExpectedRevision=Node(Id)->GeometryRevision;M.ExpectedPosition=Node(Id)->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+Delta;}return Moves;};
 const auto Moves=Requests(FVector(0,-35,0));auto Commit=[&](bool Preview=false){return UEHBBuildingToolset::CommitWallNodeMoves(B,Moves,Preview);};
 const auto Preview=Commit(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestEqual(TEXT("Versioned mixed command preview is read only"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview preserves receipt"),LiveNodeReceipt(B),Receipt);
 for(int32 I=0;I<2;++I){auto Stale=Moves;++Stale[I].ExpectedRevision;TestEqual(TEXT("Each endpoint revision is checked"),UEHBBuildingToolset::CommitWallNodeMoves(B,Stale,false).Message,FString(TEXT("StaleNodeRevision")));}
 auto Duplicate=Moves;Duplicate[1]=Duplicate[0];TestFalse(TEXT("Duplicate node identity cannot be hidden by request array"),UEHBBuildingToolset::CommitWallNodeMoves(B,Duplicate,false).bSucceeded);
 auto Vertical=Moves;Vertical[0].TargetPosition.Z+=10;TestFalse(TEXT("Mixed wall vertical move rejected"),UEHBBuildingToolset::CommitWallNodeMoves(B,Vertical,false).bSucceeded);
 Select(Fixture.Pillars[2]);TestEqual(TEXT("Unrelated selection rejected"),Commit().Message,FString(TEXT("TargetNotSelected")));Select(B);
 for(int32 Phase=1;Phase<=6;++Phase){EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Commit();TestEqual(TEXT("Every mixed movement phase rolls back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Mixed rollback restores nodes proxies finishes contacts and mesh"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Mixed rollback restores receipt"),LiveNodeReceipt(B),Receipt);TestFalse(TEXT("Failed mixed edit cannot redo"),GEditor->Trans->CanRedo());}
 auto CheckModel=[&]()
 {
  FEHBWallNodeModel Model;TestTrue(TEXT("Mixed node authority and physical proxies agree"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Floor slab and physical contact provenance remains valid"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery()),true).bSucceeded);
  TestEqual(TEXT("Both room identities survive"),B->GetClosedLoopsByFloor(1).Num(),Rooms.Num());for(const auto& R:Rooms){FEHBNodeRoomBoundary Boundary;TestTrue(TEXT("Same logical room resolves after movement"),B->TryGetRoomBoundary(R.LoopGuid,1,Boundary));}
  TestNull(TEXT("No replacement actor resurrected"),B->FindElementActorByGuid(RemovedPhysical));
 };
 const auto Applied=Commit();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;CheckModel();
 for(const auto& M:Moves){TestTrue(TEXT("Both endpoint positions applied exactly"),Node(M.NodeGuid)->LocalTransform.GetLocation()==M.TargetPosition);TestTrue(TEXT("Moved endpoint advances its revision"),Node(M.NodeGuid)->GeometryRevision>M.ExpectedRevision);}
 TestTrue(TEXT("Stored physical relative pose exactly matches authored endpoint"),P->GetRootComponent()->GetRelativeLocation()==Moves[1].TargetPosition);
 TestTrue(TEXT("Physical world pose matches transformed endpoint"),P->GetActorLocation().Equals(B->GetActorTransform().TransformPosition(Moves[1].TargetPosition),1.e-8));
 const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);P->PostEditMove(true);W->PostEditMove(true);TestEqual(TEXT("Native completion does not replay snap or movement"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Native completion does not publish a second command"),LiveNodeReceipt(B),AfterReceipt);
 TestTrue(TEXT("Undo mixed wall command"),GEditor->UndoTransaction());TestEqual(TEXT("Undo exact mixed geometry and contacts"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo mixed wall command"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact mixed geometry and contacts"),LiveNodeAuthoritySnapshot(B),After);GEditor->UndoTransaction(false);
 FEasyHouseEditorMode Mode;FEditorViewportClient Client(&GLevelEditorModeTools());Client.SetCurrentWidgetAxis(EAxisList::X);FVector Scale=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;
 FVector Delta=B->GetActorTransform().TransformVectorNoScale(FVector(15,-20,0));Select(P);TestTrue(TEXT("Bound pillar begins draft instead of legacy direct movement"),Mode.StartTracking(&Client,nullptr));TestTrue(TEXT("Bound draft captured"),Mode.NodeHandleDrag.bCaptured);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);TestTrue(*Mode.NodeHandleDrag.Feedback.Message,Mode.NodeHandleDrag.Feedback.bSucceeded);TestEqual(TEXT("Bound drag is read only"),LiveNodeAuthoritySnapshot(B),Before);
 TestTrue(TEXT("Bound widget follows draft without changing actor"),Mode.GetWidgetLocation().Equals(P->GetActorLocation()+Delta,1.e-6));Mode.InputKey(nullptr,nullptr,EKeys::Escape,IE_Pressed);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Escape consumes later bound delta without fallback"),LiveNodeAuthoritySnapshot(B),Before);
 Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Select(Fixture.Pillars[2]);Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Changed selection invalidates captured bound drag"),LiveNodeAuthoritySnapshot(B),Before);
 Select(P);Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);++Node(Bound)->GeometryRevision;TestEqual(TEXT("Bound release checks captured revision"),Mode.NodeHandleDrag.Execute(false).Message,FString(TEXT("StaleNodeRevision")));--Node(Bound)->GeometryRevision;Mode.InputKey(nullptr,nullptr,EKeys::Escape,IE_Pressed);Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Stale draft made no geometry edits"),LiveNodeAuthoritySnapshot(B),Before);
 Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Mode.EndTracking(nullptr,nullptr);const auto BoundMoved=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Bound pillar release moves same logical model"),BoundMoved!=Before);CheckModel();P->PostEditMove(true);TestEqual(TEXT("Pillar native completion keeps planned coordinates and outlines"),LiveNodeAuthoritySnapshot(B),BoundMoved);TestTrue(TEXT("Bound release undo"),GEditor->UndoTransaction());TestEqual(TEXT("Bound release complete undo"),LiveNodeAuthoritySnapshot(B),Before);
 Select(W);Mode.SelectedWall=W;Mode.SetRoomFloorWallMoveEnabled(true);Delta=B->GetActorTransform().TransformVectorNoScale(FVector(0,-35,0));
 TestTrue(TEXT("Mixed endpoint wall starts actual editor draft"),Mode.StartTracking(&Client,nullptr));Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);TestTrue(*Mode.RoomFloorWallDrag.Feedback.Message,Mode.RoomFloorWallDrag.Feedback.bSucceeded);TestTrue(TEXT("Wall draft uses logical endpoint geometry"),Mode.RoomFloorWallDrag.bOptionalNodes&&Mode.RoomFloorWallDrag.NodeGeometry.bSucceeded);TestEqual(TEXT("Whole-wall preview keeps complete building read only"),LiveNodeAuthoritySnapshot(B),Before);
 struct FPreviewPDI : FPrimitiveDrawInterface
 {
  TArray<TPair<FVector,FVector>> Lines;FPreviewPDI():FPrimitiveDrawInterface(nullptr){}bool IsHitTesting() override{return false;}void SetHitProxy(HHitProxy*) override{}void RegisterDynamicResource(FDynamicPrimitiveResource*) override{}void AddReserveLines(uint8,int32,bool,bool) override{}
  void DrawSprite(const FVector&,float,float,const FTexture*,const FLinearColor&,uint8,float,float,float,float,uint8,float) override{}void DrawLine(const FVector& A,const FVector& Z,const FLinearColor&,uint8,float,float,bool) override{Lines.Emplace(A,Z);}void DrawTranslucentLine(const FVector&,const FVector&,const FLinearColor&,uint8,float,float,bool) override{}void DrawPoint(const FVector&,const FLinearColor&,float,uint8) override{}int32 DrawMesh(const FMeshBatch&) override{return 0;}
 } PDI;
 Mode.DrawNodeEditPreview(&PDI,B,Mode.RoomFloorWallDrag.NodeGeometry);TestTrue(TEXT("Actual renderer emits dependent wall floor slab outlines"),PDI.Lines.Num()>=32);TestEqual(TEXT("Renderer does not mutate building"),LiveNodeAuthoritySnapshot(B),Before);
 const auto& Candidate=Mode.RoomFloorWallDrag.NodeGeometry;TArray<FEHBWallJunctionWallSides> PreviewSides;FName PreviewReason;
 if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Candidate.Definitions,PreviewSides,PreviewReason))return false;
 auto HasLine=[&](FVector A,FVector Z){A=B->GetActorTransform().TransformPosition(A);Z=B->GetActorTransform().TransformPosition(Z);return PDI.Lines.ContainsByPredicate([&](const auto& L){return (L.Key.Equals(A,1.e-6)&&L.Value.Equals(Z,1.e-6))||(L.Value.Equals(A,1.e-6)&&L.Key.Equals(Z,1.e-6));});};
 for(const auto& Side:PreviewSides)if(Candidate.UpdatePlan.WallGuids.Contains(Side.WallGuid))
 {
  const auto* Def=Candidate.Definitions.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Side.WallGuid;});const FVector Height(0,0,Def->Height);
  TestTrue(TEXT("Moved wall preview has actual base on both sides"),HasLine(Side.StartLeft-Height,Side.EndLeft-Height)&&HasLine(Side.StartRight-Height,Side.EndRight-Height));
  TestTrue(TEXT("Moved wall preview has actual top on both sides"),HasLine(Side.StartLeft,Side.EndLeft)&&HasLine(Side.StartRight,Side.EndRight));
  TestTrue(TEXT("Moved wall preview connects actual base to top"),HasLine(Side.StartLeft-Height,Side.StartLeft)&&HasLine(Side.EndRight-Height,Side.EndRight));
  TestFalse(TEXT("Moved wall preview never adds wall height above actual top"),HasLine(Side.StartLeft+Height,Side.EndLeft+Height)||HasLine(Side.StartRight+Height,Side.EndRight+Height));
 }
 AEHB_Floor* PreviewFloor=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E)){PreviewFloor=F;break;}if(!PreviewFloor)return false;
 const auto OutlineSource=PreviewFloor->OutlineSource;PreviewFloor->OutlineSource=EEHBOutlineSource::ManualOrUnclassified;const auto InvalidPreviewSource=LiveNodeAuthoritySnapshot(B);PDI.Lines.Reset();
 Mode.DrawNodeEditPreview(&PDI,B,Candidate);TestEqual(TEXT("Failed dependent preview draws no partial wall geometry"),PDI.Lines.Num(),0);TestEqual(TEXT("Failed renderer leaves invalid source untouched"),LiveNodeAuthoritySnapshot(B),InvalidPreviewSource);PreviewFloor->OutlineSource=OutlineSource;
 Mode.SetRoomFloorWallMoveEnabled(false);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Mode.EndTracking(nullptr,nullptr);TestEqual(TEXT("Disabling follow during drag cancels without legacy fallback"),LiveNodeAuthoritySnapshot(B),Before);
 // Optional-node mode always keeps dependent room finishes coherent even when the legacy opt-in is off.
 Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);++Node(Bound)->GeometryRevision;TestEqual(TEXT("Wall release checks second endpoint captured revision"),Mode.RoomFloorWallDrag.Execute(false).Message,FString(TEXT("StaleNodeRevision")));--Node(Bound)->GeometryRevision;Mode.InputKey(nullptr,nullptr,EKeys::Escape,IE_Pressed);Mode.EndTracking(nullptr,nullptr);
 Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);Mode.EndTracking(nullptr,nullptr);const auto WallMoved=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Whole-wall release works with one absent column"),WallMoved!=Before);CheckModel();W->PostEditMove(true);P->PostEditMove(true);TestEqual(TEXT("Whole-wall native completion does not replay legacy snapping"),LiveNodeAuthoritySnapshot(B),WallMoved);TestTrue(TEXT("Whole-wall release undo"),GEditor->UndoTransaction());TestEqual(TEXT("Whole-wall release exact undo"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Whole-wall release redo"),GEditor->RedoTransaction());TestEqual(TEXT("Whole-wall release exact redo"),LiveNodeAuthoritySnapshot(B),WallMoved);GEditor->UndoTransaction(false);
 Select(B);for(const auto& Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings)){const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,Node(Binding.NodeGuid)->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
 const auto NoColumns=LiveNodeAuthoritySnapshot(B);Select(W);Mode.StartTracking(&Client,nullptr);Mode.InputDelta(nullptr,nullptr,Delta,Rotation,Scale);TestTrue(*Mode.RoomFloorWallDrag.Feedback.Message,Mode.RoomFloorWallDrag.Feedback.bSucceeded);Mode.EndTracking(nullptr,nullptr);TestTrue(TEXT("Whole wall moves without either physical endpoint"),LiveNodeAuthoritySnapshot(B)!=NoColumns);TestTrue(TEXT("No columns are recreated by whole-wall movement"),B->WallNodeOwnership.Bindings.IsEmpty());CheckModel();TestTrue(TEXT("Zero-column wall undo"),GEditor->UndoTransaction());TestEqual(TEXT("Zero-column wall undo restores dependent geometry"),LiveNodeAuthoritySnapshot(B),NoColumns);
 Mode.SelectedWall.Reset();Select(nullptr);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeCopyTest,"EHB.Topology.OptionalNodeBuildingCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeCopyTest::RunTest(const FString& Parameters)
{
 for(bool AllUnbound:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* Source=Fixture.Building;Source->SetActorRotation(FRotator(0,37,0));if(!Source->MigrateWallNodeAuthority(true).bSucceeded)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Source,true,false);const FGuid First=Source->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  const auto Bindings=Source->WallNodeOwnership.Bindings;
  for(const auto& Binding:Bindings)if(AllUnbound||Binding.NodeGuid==First){const auto* N=Source->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(Source,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  const auto Before=LiveNodeAuthoritySnapshot(Source),Receipt=LiveNodeReceipt(Source);const int32 ElementCount=Source->QueryElements(FEHBElementQuery()).Num();
  auto CountActors=[&](){int32 N=0;for(TActorIterator<AActor> It(Source->GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};
  const int32 BeforeCount=CountActors();
  auto* Custom=NewObject<USceneComponent>(Source,NAME_None,RF_Transient);Custom->SetupAttachment(Source->GetRootComponent());Source->AddInstanceComponent(Custom);Custom->RegisterComponent();TestEqual(TEXT("Optional junction policy does not allow arbitrary custom components"),EHBBuildingCopy::Execute(Source,FVector(2100,400,0)).Status,FName(TEXT("CustomComponentCopyPolicyRequired")));Custom->DestroyComponent();
  const auto History=Source->TopologyMigrationBaseline;Source->TopologyMigrationBaseline.Nodes[0].SourcePillarGuid=FGuid::NewGuid();TestEqual(TEXT("Unknown nonempty historical physical ID is not treated as optional"),EHBBuildingCopy::Execute(Source,FVector(2100,400,0)).Status,FName(TEXT("HistoricalIdentityCopyPolicyRequired")));Source->TopologyMigrationBaseline=History;
  for(bool AfterApply:{false,true})
  {
   if(AfterApply)EHBBuildingCopy::FailAfterApply=true;else EHBBuildingCopy::FailAfterImport=true;
   const auto Failed=EHBBuildingCopy::Execute(Source,FVector(2100,400,0));TestEqual(TEXT("Optional copy rolls back both import and final apply failure"),Failed.Status,FName(TEXT("BuildingCopyFailedRolledBack")));TestFalse(TEXT("Failed copy publishes no partial mapping"),Failed.IdentityDraft.bSucceeded);TestEqual(TEXT("Failed copy leaks no physical actors"),CountActors(),BeforeCount);TestEqual(TEXT("Failed copy preserves original junction components and meshes"),LiveNodeAuthoritySnapshot(Source),Before);TestEqual(TEXT("Failed copy preserves source receipt"),LiveNodeReceipt(Source),Receipt);TestFalse(TEXT("Failed copy cannot redo"),GEditor->Trans->CanRedo());
  }
  const auto Copied=EHBBuildingCopy::Execute(Source,FVector(2100,400,0));if(!TestTrue(*Copied.Status.ToString(),Copied.bSucceeded))return false;auto* Copy=Copied.Building;Fixture.Actors.Append(Copied.Actors);const auto& Map=Copied.IdentityDraft;
  TestTrue(TEXT("Validated copy publishes complete identity draft"),Map.bSucceeded&&Map.BuildingGuid==Copy->BuildingGuid);TestEqual(TEXT("Copied only existing actors, never replacement pillars"),Copied.Actors.Num(),ElementCount+1);TestEqual(TEXT("Optional authority retained"),Copy->WallNodeAuthority.Version,2);TestEqual(TEXT("Exact physical binding count retained"),Copy->WallNodeOwnership.Bindings.Num(),Source->WallNodeOwnership.Bindings.Num());TestEqual(TEXT("Original complete state unchanged"),LiveNodeAuthoritySnapshot(Source),Before);
  TestEqual(TEXT("All logical identities mapped explicitly"),Map.NodeGuids.Num(),Source->WallNodeAuthority.Nodes.Num());TestEqual(TEXT("All room identities mapped explicitly"),Map.RoomGuids.Num(),Source->GetClosedLoopsByFloor(1).Num());
  TSet<FGuid> SourceIds;SourceIds.Add(Source->BuildingGuid);for(const auto& Pair:Map.NodeGuids)SourceIds.Add(Pair.Key);for(const auto& Pair:Map.ElementGuids)SourceIds.Add(Pair.Key);for(const auto& Pair:Map.RoomGuids)SourceIds.Add(Pair.Key);for(const auto& Pair:Map.RelationGuids)SourceIds.Add(Pair.Key);
  auto CheckMap=[&](const TMap<FGuid,FGuid>& Ids){for(const auto& Pair:Ids)TestFalse(TEXT("Copied domain never aliases any source identity"),SourceIds.Contains(Pair.Value));};CheckMap(Map.NodeGuids);CheckMap(Map.ElementGuids);CheckMap(Map.RoomGuids);CheckMap(Map.RelationGuids);
  int32 JunctionCount=0;for(const auto& N:Source->WallNodeAuthority.Nodes)
  {
   const FGuid Id=Map.NodeGuids.FindChecked(N.NodeGuid);const auto* NewNode=Copy->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});if(!TestNotNull(TEXT("Mapped node exists"),NewNode))return false;TestTrue(TEXT("Copy retains exact authored node pose and revision"),NewNode->LocalTransform.Equals(N.LocalTransform,0.0)&&NewNode->GeometryRevision==N.GeometryRevision);
   const FGuid Physical=Source->FindPhysicalPillarForNode(N.NodeGuid);if(Physical.IsValid())TestEqual(TEXT("Bound physical mapping uses element domain"),Copy->FindPhysicalPillarForNode(Id),Map.ElementGuids.FindChecked(Physical));
   else{++JunctionCount;auto* C=Copy->FindWallNodeJunction(Id);auto* S=Source->FindWallNodeJunction(N.NodeGuid);if(!TestNotNull(TEXT("Copied unbound node regenerated actual fill"),C)||!S)return false;TestTrue(TEXT("Derived fill has independent owner component and correct source version"),C!=S&&C->GetOwner()==Copy&&C->HasAnyFlags(RF_Transient)&&C->SourceGeometryRevision==N.GeometryRevision);TestEqual(TEXT("Derived fill keeps generated triangle count"),C->GetProcMeshSection(0)->ProcIndexBuffer.Num(),S->GetProcMeshSection(0)->ProcIndexBuffer.Num());}
  }
  TInlineComponentArray<UEHBWallJunctionComponent*> Junctions(Copy);TestEqual(TEXT("No imported orphan or duplicated junction component"),Junctions.Num(),JunctionCount);
  FEHBWallNodeModel Model;TestTrue(TEXT("Copied mixed model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(Copy,Model).bSucceeded);TestTrue(TEXT("Copied floor slab and contact metadata valid"),FEHBCopyOutlinePolicy::Prepare(Copy,Model,Copy->QueryElements(FEHBElementQuery())).bSucceeded);TestFalse(TEXT("Copy starts a fresh edit history"),Copy->LastCommittedEdit.StateId.IsValid());
  for(const auto& R:Source->GetClosedLoopsByFloor(1)){FEHBNodeRoomBoundary A,Z;Source->TryGetRoomBoundary(R.LoopGuid,1,A);TestTrue(TEXT("Mapped room queries independently"),Copy->TryGetRoomBoundary(Map.RoomGuids.FindChecked(R.LoopGuid),1,Z));TestEqual(TEXT("Room copy preserves area"),A.Area,Z.Area);for(FGuid Id:A.NodeGuids)TestTrue(TEXT("Room uses mapped logical corner"),Z.NodeGuids.Contains(Map.NodeGuids.FindChecked(Id)));}
  const auto After=LiveNodeAuthoritySnapshot(Copy);TestTrue(TEXT("Undo complete optional copy"),GEditor->UndoTransaction());TestEqual(TEXT("Undo removes complete copied group"),CountActors(),BeforeCount);TestEqual(TEXT("Copy undo preserves original"),LiveNodeAuthoritySnapshot(Source),Before);TestTrue(TEXT("Redo complete optional copy"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores exact copied model and live geometry"),LiveNodeAuthoritySnapshot(Copy),After);
  const FGuid CopiedNode=Map.NodeGuids.FindChecked(First);const auto* N=Copy->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==CopiedNode;});GEditor->SelectNone(false,true,false);GEditor->SelectActor(Copy,true,false);const auto Start=N->LocalTransform.GetLocation();const auto Moved=UEHBBuildingToolset::MoveUnboundWallNode(Copy,CopiedNode,N->GeometryRevision,Start,Start+FVector(-20,-10,0),false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;TestEqual(TEXT("Editing copied node cannot modify original"),LiveNodeAuthoritySnapshot(Source),Before);TestTrue(TEXT("Undo copied node movement"),GEditor->UndoTransaction(false));TestEqual(TEXT("Copied movement complete undo"),LiveNodeAuthoritySnapshot(Copy),After);
  TestEqual(TEXT("Undo reconciles stored instance registry as well as live owned junctions"),Copy->GetInstanceComponents().Num(),JunctionCount);
  const auto Again=EHBBuildingCopy::Execute(Copy,FVector(2100,0,0));if(!TestTrue(*FString::Printf(TEXT("Copy second generation: %s / %s"),*Again.Status.ToString(),*Again.FailureReason.ToString()),Again.bSucceeded))return false;Fixture.Actors.Append(Again.Actors);TestTrue(TEXT("Undo second generation"),GEditor->UndoTransaction(false));TestEqual(TEXT("Second generation leaves first copied mesh exact"),LiveNodeAuthoritySnapshot(Copy),After);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Source,true,false);TSharedPtr<FJsonObject> Tool;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UEHBBuildingToolset::CopyWallNodeBuilding(Source,FVector(4200,400,0))),Tool);if(!TestTrue(TEXT("Existing toolset can copy optional group"),Tool&&Tool->GetBoolField(TEXT("bSucceeded"))))return false;TestEqual(TEXT("Tool returns explicit logical map"),Tool->GetArrayField(TEXT("nodeIds")).Num(),Map.NodeGuids.Num());TestEqual(TEXT("Tool returns explicit room map"),Tool->GetArrayField(TEXT("roomIds")).Num(),Map.RoomGuids.Num());TestTrue(TEXT("Undo tool copy"),GEditor->UndoTransaction(false));TestEqual(TEXT("No leaks after tool copy"),CountActors(),BeforeCount+ElementCount+1);TestEqual(TEXT("All copy operations preserve source receipt"),LiveNodeReceipt(Source),Receipt);GEditor->SelectNone(false,true,false);
 }
 return true;
}
namespace
{
 TSharedRef<FJsonObject> NodeCopyPersistenceEvidence(AEHBBuildingActorBase* B)
 {
  auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("guid"),B->BuildingGuid.ToString());Data->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(B));Data->SetStringField(TEXT("receipt"),LiveNodeReceipt(B));Data->SetNumberField(TEXT("physicalCount"),B->WallNodeOwnership.Bindings.Num());Data->SetNumberField(TEXT("elementCount"),B->QueryElements(FEHBElementQuery()).Num());
  TArray<TSharedPtr<FJsonValue>> Rooms;for(const auto& R:B->GetClosedLoopsByFloor(1)){FEHBNodeRoomBoundary Boundary;if(B->TryGetRoomBoundary(R.LoopGuid,1,Boundary))Rooms.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Boundary)));}Data->SetArrayField(TEXT("rooms"),Rooms);return Data;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeWallPathTest,"EHB.Topology.OptionalNodeWallPath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeWallPathTest::RunTest(const FString& Parameters)
{
 // Equal bits in the two namespaces must still address different route points.
 FEHBWallPathPoint A,Middle,Z;A.ExistingNodeGuid=FGuid(1,0,0,1);A.NodeRevision=2;Middle.ExistingPillarGuid=A.ExistingNodeGuid;Middle.LocalPosition=FVector(100,0,0);Z.ExistingNodeGuid=FGuid(1,0,0,2);Z.NodeRevision=4;Z.LocalPosition=FVector(200,0,0);
 const auto Pure=FEHBWallPathPlanning::Build({A,Middle,Z},{A,Z},false,20);TestTrue(TEXT("Logical and physical route identity namespaces stay distinct"),Pure.bSucceeded);TestEqual(TEXT("Intermediate physical point remains in the logical route"),Pure.Segments.Num(),2);auto Stale=A;++Stale.NodeRevision;TestFalse(TEXT("Pure route rejects stale logical revision"),FEHBWallPathPlanning::Build({A,Middle,Z},{Stale,Z},false,20).bSucceeded);
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 for(bool Physical:{true,false})for(bool Finishes:{false,true})for(bool AllUnbound:{false,true})for(int32 Variant=0;Variant<3;++Variant)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(Finishes&&!AddCopyRoomOutlines(Fixture,true))return false;if(!TestTrue(TEXT("Prepare typed ownership for logical path fixture"),B->MigrateWallNodeOwnership(true).bSucceeded)||!TestTrue(TEXT("Prepare node authority for logical path fixture"),B->MigrateWallNodeAuthority(true).bSucceeded))return false;
  const FGuid StartNode=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid),OppositeNode=B->FindNodeForPhysicalPillar(Fixture.Pillars[2]->ElementGuid);const auto Bindings=B->WallNodeOwnership.Bindings;TSet<FGuid> Unbound;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  for(const auto& Binding:Bindings)if(AllUnbound||Binding.NodeGuid==StartNode){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;Unbound.Add(Binding.NodeGuid);}
  auto Anchor=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=N->GeometryRevision;E.LocalLocation=N->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=N->FloorIndex;return E;};
  const auto Start=Anchor(StartNode);FEHBWallCreationEndpoint End=Variant==1?Anchor(OppositeNode):FEHBWallCreationEndpoint();if(Variant!=1){End.LocalLocation=Variant==0?FVector(-240,-130,0):FVector(1300,0,0);End.WorldLocation=B->GetActorTransform().TransformPosition(End.LocalLocation);}
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=Physical;const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const auto NodesBefore=B->WallNodeAuthority.Nodes;const int32 PhysicalBefore=B->WallNodeOwnership.Bindings.Num();auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)++N;return N;};const int32 CountBefore=Count();
  auto Commit=[&](bool Preview=false){return EHBWallCreationCommand::Commit(B,{Start,End},false,Options,Preview);};
  FEHBWallCreationEndpoint Snapped;TestTrue(TEXT("Runtime endpoint snap resolves optional node"),B->ResolveWallCreationEndpoint(Start.WorldLocation+FVector(2,1,0),30,20,1,nullptr,Snapped));TestTrue(TEXT("Snap carries exact logical ID revision and authored pose"),Snapped.NodeGuid==StartNode&&Snapped.ExpectedNodeRevision==Start.ExpectedNodeRevision&&Snapped.LocalLocation==Start.LocalLocation&&!Snapped.Pillar);
  auto Wrong=Start;++Wrong.ExpectedNodeRevision;TestEqual(TEXT("Stale logical anchor rejected before mutation"),EHBWallCreationCommand::Commit(B,{Wrong,End},false,Options).Status,FName(TEXT("StaleNodeEndpoint")));Wrong=Start;Wrong.LocalLocation.X+=3;Wrong.WorldLocation=B->GetActorTransform().TransformPosition(Wrong.LocalLocation);TestEqual(TEXT("Moved logical anchor rejected before mutation"),EHBWallCreationCommand::Commit(B,{Wrong,End},false,Options).Status,FName(TEXT("StaleNodeEndpointPosition")));Wrong=Start;Wrong.FloorIndex=2;TestFalse(TEXT("Wrong floor rejected"),EHBWallCreationCommand::Commit(B,{Wrong,End},false,Options).bSucceeded);Wrong=Start;Wrong.NodeGuid=FGuid::NewGuid();TestFalse(TEXT("Unknown node rejected"),EHBWallCreationCommand::Commit(B,{Wrong,End},false,Options).bSucceeded);
  FEHBWallCreationResult Raw;auto RawStart=Start,RawEnd=End;TestFalse(TEXT("Old raw actor API cannot materialize a logical anchor"),B->CreateOrReuseWallSegment(RawStart,RawEnd,Options,Raw));TestEqual(TEXT("All endpoint guard failures are read only"),LiveNodeAuthoritySnapshot(B),Before);
  const auto Preview=Commit(true);if(!TestTrue(*FString::Printf(TEXT("Logical path preview F%d U%d V%d: %s"),Finishes,AllUnbound,Variant,*Preview.Status.ToString()),Preview.bSucceeded))return false;TestEqual(TEXT("Logical path preview keeps model mesh finishes and receipt"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview creates no actors"),Count(),CountBefore);
  EHBWallCreationCommand::FailAfterEdge=1;const auto Interrupted=Commit();TestEqual(TEXT("Logical path edge failure rolls back"),Interrupted.Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Edge failure restores full source state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Failed path creates no leaked actors"),Count(),CountBefore);TestTrue(TEXT("Failure returns no dead result actors"),Interrupted.Walls.IsEmpty()&&Interrupted.Endpoints.IsEmpty()&&!Interrupted.PrimaryWall);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord}){EHBWallSplitTestHooks::FailurePhase=Phase;const auto R=Commit();TestEqual(TEXT("Logical path final phase rolls back"),R.Status,FName(TEXT("CreateFailedRolledBack")));TestEqual(TEXT("Final failure restores exact model geometry and finishes"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Final failure restores receipt"),LiveNodeReceipt(B),Receipt);TestFalse(TEXT("Failed logical path cannot be redone"),GEditor->Trans->CanRedo());}
  const auto R=Commit();if(!TestTrue(*FString::Printf(TEXT("Logical path F%d U%d V%d: %s / %s"),Finishes,AllUnbound,Variant,*R.Status.ToString(),*R.FailureReason.ToString()),R.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);
  TestEqual(TEXT("Only free requests create physical columns"),B->WallNodeOwnership.Bindings.Num(),PhysicalBefore+(!Physical||Variant==1?0:1));TestEqual(TEXT("Only free requests create logical nodes"),B->WallNodeAuthority.Nodes.Num(),NodesBefore.Num()+(Variant==1?0:1));for(const auto& N:NodesBefore){const auto* Current=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});TestTrue(TEXT("Existing logical anchors retain pose and revision"),Current&&Current->LocalTransform.Equals(N.LocalTransform,0)&&Current->GeometryRevision==N.GeometryRevision);if(Unbound.Contains(N.NodeGuid))TestFalse(TEXT("No existing empty binding is filled"),B->FindPhysicalPillarForNode(N.NodeGuid).IsValid());}
  const int32 ExpectedRooms=(Finishes?2:1)+(Variant==1?1:0);TestEqual(TEXT("Logical path room partition count"),B->GetClosedLoopsByFloor(1).Num(),ExpectedRooms);FEHBWallNodeModel Model;TestTrue(TEXT("Logical path final model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Logical path full finish and relation policy validates"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);
  if(Variant==2)TestEqual(TEXT("Collinear path explicitly reuses intermediate logical wall segments"),R.Walls.Num(),Finishes?3:2);
  if(Finishes){TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Room dependencies queried after logical path"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members));for(const auto& M:Members){TestEqual(TEXT("Each inherited room has one floor"),M.Floors.Num(),1);TestEqual(TEXT("Each inherited room has one slab"),M.Slabs.Num(),1);}}
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Logical path derived geometry rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Logical path explicit rebuild is stable"),LiveNodeAuthoritySnapshot(B),After);TestTrue(TEXT("Undo logical path"),GEditor->UndoTransaction());TestEqual(TEXT("Logical path exact undo"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Logical path receipt undo"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo logical path"),GEditor->RedoTransaction());TestEqual(TEXT("Logical path exact redo"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Logical path receipt redo"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  if(Variant==0)
  {
   FEasyHouseEditorMode Mode;Mode.SetActiveBuilding(B);Mode.SetWallCreationPhysicalColumns(Physical);Mode.WallCreationHeight=300;Mode.WallCreationThickness=20;Mode.WallCreationStartPillarFloorIndex=1;FEasyHouseEditorMode::FWallCreationEndpointSnap S,E;TestTrue(TEXT("Actual mode snap adapter resolves empty corner"),Mode.ResolveWallCreationEndpointSnap(B,Start.WorldLocation,nullptr,S));TestEqual(TEXT("Actual mode carries logical snap identity"),S.NodeGuid,StartNode);E.LocalLocation=End.LocalLocation;E.WorldLocation=End.WorldLocation;AEHB_Wall* Primary=nullptr;TestTrue(TEXT("Actual mode command adapter creates from logical endpoint"),Mode.CommitWallCreationPath(B,{S,E},false,Primary));for(auto* Element:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(Element);TestTrue(TEXT("Mode logical path undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Mode adapter exact undo"),LiveNodeAuthoritySnapshot(B),Before);
  }
  if(Variant==0)
  {
   FEasyHouseEditorMode Mode;Mode.SetActiveBuilding(B);Mode.SetWallCreationPhysicalColumns(Physical);Mode.WallCreationHeight=300;Mode.WallCreationThickness=20;Mode.WallCreationStartPillarFloorIndex=1;Mode.WallCreationStartLocation=Start.WorldLocation;Mode.WallCreationMouseLocation=B->GetActorTransform().TransformPosition(FVector(-300,-250,0));Mode.bWallCreationDragging=true;
   if(!TestTrue(TEXT("Actual rectangle release reuses an existing actor-free corner"),Mode.FinishWallCreationRectangleDrag()))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);TestEqual(TEXT("Rectangle creates only three free columns"),B->WallNodeOwnership.Bindings.Num(),PhysicalBefore+(Physical?3:0));TestFalse(TEXT("Rectangle preserves original empty corner binding"),B->FindPhysicalPillarForNode(StartNode).IsValid());TestEqual(TEXT("Rectangle adds one separate closed room"),B->GetClosedLoopsByFloor(1).Num(),(Finishes?2:1)+1);TestTrue(TEXT("Actual rectangle is one undoable transaction"),GEditor->UndoTransaction(false));TestEqual(TEXT("Rectangle release undo restores original complete building"),LiveNodeAuthoritySnapshot(B),Before);Mode.CancelWallCreation();
  }
  GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeAnchoredPathTest,"EHB.Topology.OptionalNodeAnchoredPath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeAnchoredPathTest::RunTest(const FString& Parameters)
{
 // Regression for the multi-leg corner discovered before saving the new map.
 {
  const TArray<FVector> Polygon={FVector(0,-10,0),FVector(20,-10,0),FVector(40,10,0),FVector(0,10,0)};FVector Left,Right;
  TestTrue(TEXT("Exact unbound face uses actual asymmetric intersections"),FEHBWallJunctionGeometry::ResolveFace(20,20,Polygon,FVector::ForwardVector,20,Left,Right,true));
  TestTrue(TEXT("Both asymmetric endpoints stay on the original boundary"),Left.Equals(FVector(20,-10,0),0.001)&&Right.Equals(FVector(40,10,0),0.001));
  TArray<FEHBWallJunctionNodeInput> Nodes;TArray<FEHBWallJunctionWallInput> Walls;const TArray<FVector> Positions={FVector::ZeroVector,FVector(600,0,0),FVector(600,500,0),FVector(300,500,0),FVector(0,500,0)};
  for(int32 I=0;I<Positions.Num();++I){auto& N=Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid(92,0,0,I+1);N.LocalTransform=FTransform(Positions[I]);N.Width=N.Depth=20;N.bExactWallThickness=true;if(I){auto& W=Walls.AddDefaulted_GetRef();W.WallGuid=FGuid(92,1,0,I);W.StartNodeGuid=Nodes[0].NodeGuid;W.EndNodeGuid=N.NodeGuid;W.Thickness=20;W.Height=300;}}
  TArray<FEHBWallJunctionWallSides> Sides;FName Reason;TestTrue(*FString::Printf(TEXT("Four-leg asymmetric corner solves: %s"),*Reason.ToString()),FEHBWallJunctionGeometry::BuildStraightWallSides(Nodes,Walls,Sides,Reason));TestEqual(TEXT("All four full-thickness sides produced"),Sides.Num(),4);
  TArray<FEHBWallJunctionLeg> Legs;for(int32 I=1;I<Positions.Num();++I){auto& Leg=Legs.AddDefaulted_GetRef();Leg.Direction=Positions[I].GetSafeNormal2D();Leg.WallThickness=20;}TArray<FVector> Footprint;FEHBWallJunctionMesh Mesh;
  TestTrue(TEXT("Four-leg footprint builds"),FEHBWallJunctionGeometry::BuildFootprint(20,20,Legs,Footprint,true));TestTrue(TEXT("Actual asymmetric junction prism has valid cap triangulation"),FEHBWallJunctionMeshBuilder::BuildPrism(Footprint,300,Mesh));
  auto Short=Nodes;for(auto& N:Short)N.LocalTransform.SetLocation(N.LocalTransform.GetLocation()*0.03);Sides.Reset();TestFalse(TEXT("Miters cannot reverse or consume a short authored wall span"),FEHBWallJunctionGeometry::BuildStraightWallSides(Short,Walls,Sides,Reason));TestTrue(TEXT("Rejected short span exposes no partial geometry"),Sides.IsEmpty());
 }
 struct FPathPDI : FPrimitiveDrawInterface
 {
  struct FLine{FVector A,B;FLinearColor Color;};TArray<FLine> Lines;TArray<FVector> Points;
  FPathPDI():FPrimitiveDrawInterface(nullptr){}bool IsHitTesting() override{return false;}void SetHitProxy(HHitProxy*) override{}void RegisterDynamicResource(FDynamicPrimitiveResource*) override{}void AddReserveLines(uint8,int32,bool,bool) override{}
  void DrawSprite(const FVector&,float,float,const FTexture*,const FLinearColor&,uint8,float,float,float,float,uint8,float) override{}void DrawLine(const FVector& A,const FVector& B,const FLinearColor& C,uint8,float,float,bool) override{Lines.Add({A,B,C});}void DrawTranslucentLine(const FVector&,const FVector&,const FLinearColor&,uint8,float,float,bool) override{}void DrawPoint(const FVector& P,const FLinearColor&,float,uint8) override{Points.Add(P);}int32 DrawMesh(const FMeshBatch&) override{return 0;}
 };
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 for(bool Physical:{true,false})for(bool Finishes:{false,true})for(bool AllUnbound:{false,true})for(int32 Variant=0;Variant<5;++Variant)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));if(Finishes&&!AddCopyRoomOutlines(Fixture,true))return false;if(!B->MigrateWallNodeOwnership(true).bSucceeded||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  const FGuid Target=B->FindNodeForPhysicalPillar(Fixture.Pillars[Variant==2?3:Variant==4?0:2]->ElementGuid);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);TSet<FGuid> Unbound;
  for(const auto& Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(AllUnbound||Binding.NodeGuid==Target){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;Unbound.Add(Binding.NodeGuid);}
  auto NodeEndpoint=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=N->GeometryRevision;E.LocalLocation=N->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=N->FloorIndex;return E;};
  auto FreeEndpoint=[&](FVector P){FEHBWallCreationEndpoint E;E.LocalLocation=P;E.WorldLocation=B->GetActorTransform().TransformPosition(P);return E;};
  auto WallEndpoint=[&](AEHB_Wall* W,float Distance){const auto P=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,W->ElementGuid,Distance);auto E=FreeEndpoint(P.LocalPillarPosition);E.Wall=W;E.WallDistance=Distance;E.FloorIndex=W->FloorIndex;return E;};
  if(Variant==4){const FGuid Opposite=B->WallNodeAuthority.Nodes.FindByPredicate([](const auto& N){return N.LocalTransform.GetLocation()==FVector(600,500,0);})->NodeGuid;const auto Initial=EHBWallCreationCommand::Commit(B,{NodeEndpoint(Target),NodeEndpoint(Opposite)},false,FEHBWallCreationOptions());if(!TestTrue(TEXT("Initial diagonal before second anchored leg"),Initial.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);}
  const auto Logical=NodeEndpoint(Target);TArray<FEHBWallCreationEndpoint> Endpoints={WallEndpoint(Fixture.Walls[0],300),Logical};
  if(Variant==1)Swap(Endpoints[0],Endpoints[1]);
  if(Variant==2)Endpoints={WallEndpoint(Fixture.Walls[0],300),Logical,WallEndpoint(Fixture.Walls[2],300)};
  if(Variant==3)Endpoints={WallEndpoint(Fixture.Walls[0],200),FreeEndpoint(FVector(200,-240,0)),FreeEndpoint(FVector(450,-240,0)),WallEndpoint(Fixture.Walls[0],450)};
  if(Variant==4)Endpoints={WallEndpoint(Fixture.Walls[2],300),Logical};
  const bool Closed=Variant==3;const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const auto NodesBefore=B->WallNodeAuthority.Nodes;const int32 PhysicalBefore=B->WallNodeOwnership.Bindings.Num();auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};const int32 CountBefore=Count();
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=Physical;auto Commit=[&](bool Preview=false,FEHBWallPathPreview* Geometry=nullptr){return EHBWallCreationCommand::CommitAnchoredPath(B,Endpoints,Closed,Options,Preview,Geometry);};
  FEHBWallPathPreview Preview;const auto Proposed=Commit(true,&Preview);if(!TestTrue(*FString::Printf(TEXT("Anchored preview F%d U%d V%d: %s / %s"),Finishes,AllUnbound,Variant,*Proposed.Status.ToString(),*Proposed.FailureReason.ToString()),Proposed.bSucceeded))return false;
  TestEqual(TEXT("Preview returns complete candidate model"),Preview.Model.Nodes.Num(),NodesBefore.Num()+(Variant==3?4:Variant==2?2:1));TestEqual(TEXT("Preview marks only actually requested new physical columns"),Preview.NewPhysicalNodeGuids.Num(),!Physical?0:Variant==3?4:Variant==2?2:1);TestEqual(TEXT("Preview read only"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview creates no actors"),Count(),CountBefore);
  FEasyHouseEditorMode Mode;Mode.SetActiveBuilding(B);Mode.SetWallCreationPhysicalColumns(Physical);Mode.WallCreationHeight=300;Mode.WallCreationThickness=20;Mode.WallCreationStartPillarFloorIndex=1;
  TArray<FEasyHouseEditorMode::FWallCreationEndpointSnap> Snaps;for(const auto& E:Endpoints){auto& S=Snaps.AddDefaulted_GetRef();S.LocalLocation=E.LocalLocation;S.WorldLocation=E.WorldLocation;S.Pillar=E.Pillar;S.Wall=E.Wall;S.WallDistance=E.WallDistance;S.FloorIndex=E.FloorIndex;S.NodeGuid=E.NodeGuid;S.ExpectedNodeRevision=E.ExpectedNodeRevision;}
  FPathPDI PDI;TestTrue(TEXT("Actual renderer handles anchored optional path"),Mode.DrawWallCreationPathPreview(&PDI,B,Snaps,Closed));TestEqual(TEXT("Renderer draws candidate walls and only requested physical columns"),PDI.Lines.Num(),12*(Preview.WallGuids.Num()+Preview.NewPhysicalNodeGuids.Num()));TestTrue(TEXT("Every valid preview line green"),!PDI.Lines.ContainsByPredicate([](const auto& L){return L.Color==FLinearColor::Red;}));
  bool InBounds=true;for(const auto& Line:PDI.Lines)for(const FVector P:{Line.A,Line.B}){const double Z=B->GetActorTransform().InverseTransformPosition(P).Z;InBounds&=Z>=-0.001&&Z<=300.001;}TestTrue(TEXT("Preview walls stay between floor and actual wall top"),InBounds);
  TArray<FEHBWallJunctionWallSides> ReferencePreviewSides;FName PreviewReason;TestTrue(TEXT("Independent preview side reference succeeds"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Preview.Model,ReferencePreviewSides,PreviewReason));TestEqual(TEXT("Preview carries every validated side"),Preview.Sides.Num(),ReferencePreviewSides.Num());
  bool Matches=true;for(const auto& Side:ReferencePreviewSides)if(Preview.WallGuids.Contains(Side.WallGuid))for(FVector P:{Side.StartLeft,Side.StartRight,Side.EndLeft,Side.EndRight})for(double Z:{0.,-300.}){P.Z+=Z;const FVector WorldP=B->GetActorTransform().TransformPosition(P);Matches&=PDI.Lines.ContainsByPredicate([&](const auto& L){return L.A.Equals(WorldP,0.001)||L.B.Equals(WorldP,0.001);});P.Z-=Z;}TestTrue(TEXT("Drawn base and top corners match independent solved wall geometry"),Matches);
  auto StalePreview=Preview;auto BadOptions=Options;BadOptions.WallThickness=0;TestFalse(TEXT("Invalid next request is freshly rejected"),EHBWallCreationCommand::CommitAnchoredPath(B,Endpoints,Closed,BadOptions,true,&StalePreview).bSucceeded);TestTrue(TEXT("Invalid next request clears previous model sides and markers"),StalePreview.Model.Nodes.IsEmpty()&&StalePreview.Sides.IsEmpty()&&StalePreview.WallGuids.IsEmpty()&&StalePreview.NewPhysicalNodeGuids.IsEmpty());
  if(Variant==0){auto* Changed=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Target;});++Changed->GeometryRevision;FPathPDI Dirty;Mode.DrawWallCreationPathPreview(&Dirty,B,Snaps,Closed);TestTrue(TEXT("Unrecorded source revision rejects the old preview request"),Dirty.Points.IsEmpty()&&Dirty.Lines.Num()==Snaps.Num()-1&&!Dirty.Lines.ContainsByPredicate([](const auto& L){return L.Color!=FLinearColor::Red;}));--Changed->GeometryRevision;TestEqual(TEXT("No receipt or state mutation needed to invalidate a request"),LiveNodeAuthoritySnapshot(B),Before);}
  if(Finishes)
  {
   AEHB_FloorSlab* Slab=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E)){Slab=S;break;}if(!Slab)return false;
   TArray<FEHBNodeRoomBoundary> Rooms;TArray<FEHBWallJunctionWallSides> Sides;FName Reason;if(!FEHBWallNodeRooms::Build(B->BuildingGuid,Preview.Model,Rooms,Reason)||!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Preview.Model,Sides,Reason))return false;
   for(const auto& Room:Rooms)
   {
    TArray<FVector> Reference;TestTrue(TEXT("Candidate room polygon builds"),FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(Slab,Room,Preview.Model,Sides,Reference));
    for(int32 Shift=1;Shift<Room.NodeGuids.Num();++Shift){auto Rotated=Room;for(int32 I=0;I<Room.NodeGuids.Num();++I){Rotated.NodeGuids[I]=Room.NodeGuids[(I+Shift)%Room.NodeGuids.Num()];Rotated.WallGuids[I]=Room.WallGuids[(I+Shift)%Room.WallGuids.Num()];}TArray<FVector> Polygon;TestTrue(TEXT("Rotated room polygon builds"),FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(Slab,Rotated,Preview.Model,Sides,Polygon));TestTrue(TEXT("Net slab contour is independent of cyclic node/wall enumeration start"),Polygon==Reference);}
   }
  }
  TSet<FGuid> ShownNodes;for(const auto& W:Preview.Model.Walls)if(Preview.WallGuids.Contains(W.WallGuid)){ShownNodes.Add(W.StartNodeGuid);ShownNodes.Add(W.EndNodeGuid);}int32 LogicalMarkers=0;for(FGuid Id:ShownNodes)if(!Preview.Model.PillarBindings.ContainsByPredicate([&](const auto& P){return P.NodeGuid==Id;}))++LogicalMarkers;TestEqual(TEXT("Unbound corners use point markers, never fabricated pillar boxes"),PDI.Points.Num(),LogicalMarkers);TestEqual(TEXT("Rendering keeps model mesh finishes and receipt read only"),LiveNodeAuthoritySnapshot(B),Before);
  if(Variant!=3)
  {
   auto Bad=Snaps;for(auto& S:Bad)if(S.NodeGuid.IsValid())++S.ExpectedNodeRevision;FPathPDI Rejected;Mode.DrawWallCreationPathPreview(&Rejected,B,Bad,Closed);TestTrue(TEXT("Stale preview draws red request only"),Rejected.Points.IsEmpty()&&Rejected.Lines.Num()==Bad.Num()-1&&!Rejected.Lines.ContainsByPredicate([](const auto& L){return L.Color!=FLinearColor::Red;}));
  }
  EHBWallCreationCommand::FailAfterSplit=1;TestEqual(TEXT("Split failure rolls back full anchored path"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Split rollback exact"),LiveNodeAuthoritySnapshot(B),Before);
  EHBWallCreationCommand::FailAfterEdge=1;const auto Failed=Commit();TestEqual(TEXT("Path failure rolls back source splits"),Failed.Status,FName(TEXT("SplitFailedRolledBack")));TestTrue(TEXT("Failed path publishes no dead output actors"),Failed.Walls.IsEmpty()&&Failed.Endpoints.IsEmpty()&&!Failed.PrimaryWall);TestEqual(TEXT("Path rollback exact"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Path rollback actor count"),Count(),CountBefore);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord}){EHBWallSplitTestHooks::FailurePhase=Phase;TestEqual(TEXT("Final dependency stage failure rolls back"),Commit().Status,FName(TEXT("SplitFailedRolledBack")));TestTrue(TEXT("Requested final fault was reached"),EHBWallSplitTestHooks::FailurePhase==EHBWallSplitTestHooks::EFailurePhase::None);EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::None;TestEqual(TEXT("Final rollback restores model and finishes"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Final rollback restores receipt"),LiveNodeReceipt(B),Receipt);TestFalse(TEXT("Failed composite path cannot redo"),GEditor->Trans->CanRedo());}
  const auto R=Commit();if(!TestTrue(*FString::Printf(TEXT("Anchored commit F%d U%d V%d: %s / %s"),Finishes,AllUnbound,Variant,*R.Status.ToString(),*R.FailureReason.ToString()),R.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);
  TestEqual(TEXT("Actual physical count matches preview"),B->WallNodeOwnership.Bindings.Num(),PhysicalBefore+Preview.NewPhysicalNodeGuids.Num());for(const auto& N:NodesBefore){const auto* Current=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});TestTrue(TEXT("Original logical poses and revisions preserved"),Current&&Current->LocalTransform.Equals(N.LocalTransform,0)&&Current->GeometryRevision==N.GeometryRevision);if(Unbound.Contains(N.NodeGuid))TestFalse(TEXT("Original unbound nodes remain without physical columns"),B->FindPhysicalPillarForNode(N.NodeGuid).IsValid());}
  FEHBWallNodeModel Actual;TestTrue(TEXT("Actual anchored model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual).bSucceeded);TestTrue(TEXT("Actual room floors slabs contacts validate"),FEHBCopyOutlinePolicy::Prepare(B,Actual,B->QueryElements(FEHBElementQuery())).bSucceeded);TestEqual(TEXT("Anchored operation creates one new room"),B->GetClosedLoopsByFloor(1).Num(),(Finishes?2:1)+(Variant==4?2:1));
  TArray<FEHBWallJunctionWallSides> ExpectedSides,ActualSides;FName Why;TestTrue(TEXT("Candidate geometry solves"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Preview.Model,ExpectedSides,Why));TestTrue(TEXT("Actual geometry solves"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Actual,ActualSides,Why));
  auto MapNode=[&](FGuid Id){const auto* P=Preview.Model.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});const auto* A=P?Actual.Nodes.FindByPredicate([&](const auto& N){return N.LocalTransform.GetLocation().Equals(P->LocalTransform.GetLocation(),0.001);}):nullptr;return A?A->NodeGuid:FGuid();};
  for(const auto& S:ExpectedSides)if(Preview.WallGuids.Contains(S.WallGuid)){const auto* W=Preview.Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==S.WallGuid;});const auto* A=Actual.Walls.FindByPredicate([&](const auto& V){return V.StartNodeGuid==MapNode(W->StartNodeGuid)&&V.EndNodeGuid==MapNode(W->EndNodeGuid);});const auto* AS=A?ActualSides.FindByPredicate([&](const auto& V){return V.WallGuid==A->WallGuid;}):nullptr;TestTrue(TEXT("Every displayed wall exactly matches committed junction outline"),AS&&AS->StartLeft.Equals(S.StartLeft,0.001)&&AS->EndLeft.Equals(S.EndLeft,0.001)&&AS->StartRight.Equals(S.StartRight,0.001)&&AS->EndRight.Equals(S.EndRight,0.001));}
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Explicit rebuild after anchored operation"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Anchored rebuild stable"),LiveNodeAuthoritySnapshot(B),After);TestTrue(TEXT("Undo complete anchored operation"),GEditor->UndoTransaction());TestEqual(TEXT("Undo exact source state"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo complete anchored operation"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact anchored state"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Redo exact receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  if(Variant==0)
  {
   FEasyHouseEditorMode::FWallCreationEndpointSnap Hover;FHitResult Hit;Hit.Component=B->FindWallNodeJunction(Target);Hit.bBlockingHit=true;Hit.ImpactPoint=Logical.WorldLocation+FVector(0,0,300);
   TestTrue(TEXT("Actual junction hit resolves logical corner, not next-floor support"),Mode.ResolveWallCreationLogicalNodeSnap(B,Hit.ImpactPoint,2,&Hit,Hover));TestTrue(TEXT("Junction hit restores exact identity revision base and floor"),Hover.NodeGuid==Target&&Hover.ExpectedNodeRevision==Logical.ExpectedNodeRevision&&Hover.WorldLocation==Logical.WorldLocation&&Hover.FloorIndex==1&&!Hover.Pillar&&!Hover.Wall);
   TestTrue(TEXT("Ground proximity resolves unbound logical corner"),Mode.ResolveWallCreationLogicalNodeSnap(B,Logical.WorldLocation+FVector(2,1,0),1,nullptr,Hover));TestFalse(TEXT("Proximity on a different floor cannot reuse the corner"),Mode.ResolveWallCreationLogicalNodeSnap(B,Logical.WorldLocation,2,nullptr,Hover));
   auto HoverLogical=[&](){Mode.bWallCreationToolActive=true;Mode.HoveredWallCreationNode=Logical.NodeGuid;Mode.HoveredWallCreationNodeRevision=Logical.ExpectedNodeRevision;Mode.HoveredWallCreationPillar.Reset();Mode.HoveredWallCreationWall.Reset();Mode.HoveredWallCreationPillarFloorIndex=1;Mode.WallCreationMouseLocation=Logical.WorldLocation;};
   HoverLogical();FPathPDI Idle;Mode.DrawWallCreationPreview(&Idle);TestTrue(TEXT("Idle unbound preview is one marker, no pillar"),Idle.Lines.IsEmpty()&&Idle.Points.Num()==1);Mode.CaptureWallCreationStart(nullptr);TestEqual(TEXT("Mouse-down captures node identity"),Mode.WallCreationStartNode,Target);TestTrue(TEXT("Single click selects logical node without creating a column"),Mode.FinishWallCreationSinglePillar());TestEqual(TEXT("Logical single click read only"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Logical click selects the existing node"),Mode.SelectedWallNode==Target);
   HoverLogical();Mode.CaptureWallCreationStart(nullptr);++Mode.WallCreationStartNodeRevision;TestFalse(TEXT("Stale single click never falls back to free pillar"),Mode.FinishWallCreationSinglePillar());TestEqual(TEXT("Stale click exact source state"),LiveNodeAuthoritySnapshot(B),Before);
   HoverLogical();Mode.CaptureWallCreationStart(nullptr);++Mode.WallCreationStartNodeRevision;Mode.HoveredWallCreationNode.Invalidate();Mode.WallCreationMouseLocation=B->GetActorTransform().TransformPosition(FVector(850,750,0));TestFalse(TEXT("Rectangle release preserves and rejects stale captured start"),Mode.FinishWallCreationRectangleDrag());TestEqual(TEXT("Rejected rectangle makes no replacement column"),LiveNodeAuthoritySnapshot(B),Before);TestFalse(TEXT("Rejected rectangle clears logical start"),Mode.WallCreationStartNode.IsValid());
   auto CaptureSource=[&](){Mode.bWallCreationToolActive=true;Mode.HoveredWallCreationNode.Invalidate();Mode.HoveredWallCreationWall=Endpoints[0].Wall;Mode.HoveredWallCreationWallDistance=Endpoints[0].WallDistance;Mode.WallCreationMouseLocation=Endpoints[0].WorldLocation;Mode.CaptureWallCreationStart(nullptr);HoverLogical();};
   CaptureSource();FPathPDI Drag;Mode.DrawWallCreationPreview(&Drag);TestTrue(TEXT("Actual drag renderer uses optional anchored candidate"),Drag.Lines.Num()>12&&!Drag.Lines.ContainsByPredicate([](const auto& L){return L.Color==FLinearColor::Red;}));TestTrue(TEXT("Actual drag release joins wall to logical endpoint"),Mode.FinishWallCreationDrag());for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);TestFalse(TEXT("Release still does not fill logical endpoint"),B->FindPhysicalPillarForNode(Target).IsValid());TestTrue(TEXT("Actual release one undo"),GEditor->UndoTransaction(false));TestEqual(TEXT("Actual release undo exact"),LiveNodeAuthoritySnapshot(B),Before);Mode.CancelWallCreation();TestFalse(TEXT("Cancel clears hover node"),Mode.HoveredWallCreationNode.IsValid());
  }
  Mode.CancelWallCreation();GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallColumnCreationPolicyTest,"EHB.Topology.WallColumnCreationPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallColumnCreationPolicyTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(bool Finishes:{false,true})
 {
  FTransientTopologyFixture F;
  if(Finishes){if(!F.Create(World,RF_Transactional)||!AddCopyRoomOutlines(F,true))return false;}
  else {FActorSpawnParameters P;P.ObjectFlags=RF_Transactional;F.Building=World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(),FVector(0,0,190000),FRotator::ZeroRotator,P);F.Actors.Add(F.Building);}
  auto* B=F.Building;if(!B)return false;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!TestTrue(TEXT("Activate optional corners through public command"),UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded))return false;
  FEasyHouseEditorMode Mode;Mode.SetActiveBuilding(B);Mode.SetWallCreationPhysicalColumns(false);Mode.SetWallCreationDefaults(300,20);

  const auto PriorPrecise=LiveNodeAuthoritySnapshot(B);
  Mode.CreationAssist.bFixedLength=true;Mode.CreationAssist.LengthCm=237.5;Mode.CreationAssist.bFixedDirection=true;Mode.CreationAssist.DirectionDegrees=33.7;
  Mode.bWallCreationDragging=true;Mode.WallCreationStartPillarFloorIndex=1;
  Mode.WallCreationStartLocation=B->GetActorTransform().TransformPosition(FVector(-3000,-3000,0));
  const FVector PreciseStart=Mode.WallCreationStartLocation;
  const FVector PreciseEnd=Mode.ResolveCreationFreeEnd(PreciseStart,PreciseStart+FVector(600,200,0),nullptr);
  Mode.WallCreationMouseLocation=PreciseEnd;
  if(!TestTrue(TEXT("Actual release creates dimensioned wall"),Mode.FinishWallCreationDrag()))return false;
  bool FoundPrecise=false;for(auto* E:B->QueryElements(FEHBElementQuery()))F.Actors.AddUnique(E);
  FEHBWallNodeModel PreciseModel;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,PreciseModel).bSucceeded)return false;
  for(const auto& Edge:PreciseModel.Walls)
  {
   const auto* ANode=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge.StartNodeGuid;});
   const auto* ZNode=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge.EndNodeGuid;});if(!ANode||!ZNode)continue;
   const auto A=B->GetActorTransform().TransformPosition(ANode->LocalTransform.GetLocation()),Z=B->GetActorTransform().TransformPosition(ZNode->LocalTransform.GetLocation());
   if(A.Equals(PreciseStart,0.001)&&Z.Equals(PreciseEnd,0.001)){FoundPrecise=true;TestTrue(TEXT("Created endpoint distance keeps fractional cm"),FMath::IsNearlyEqual(FVector::Dist2D(A,Z),237.5,0.001));}
  }
  TestTrue(TEXT("Created wall matches assisted preview endpoints"),FoundPrecise);
  const auto PreciseState=LiveNodeAuthoritySnapshot(B);GEditor->UndoTransaction();TestEqual(TEXT("Precise creation exact undo"),LiveNodeAuthoritySnapshot(B),PriorPrecise);
  GEditor->RedoTransaction();TestEqual(TEXT("Precise creation exact redo"),LiveNodeAuthoritySnapshot(B),PreciseState);GEditor->UndoTransaction();Mode.CreationAssist={};

  auto CountRectangleWalls=[&](){int32 Count=0;for(auto* E:B->QueryElements(FEHBElementQuery()))if(Cast<AEHB_Wall>(E))++Count;return Count;};
  for(bool Physical:{false,true})for(int SignX:{-1,1})for(int SignY:{-1,1})
  {
   Mode.SetWallCreationPhysicalColumns(Physical);Mode.CreationAssist.bFixedRectangle=true;Mode.CreationAssist.RectangleWidthCm=313.25;Mode.CreationAssist.RectangleDepthCm=427.75;
   const auto PriorRectangle=LiveNodeAuthoritySnapshot(B);const int32 PriorWalls=CountRectangleWalls();
   const FVector Start=B->GetActorTransform().TransformPosition(FVector(-5000,-5000,0));
   Mode.bWallCreationDragging=true;Mode.WallCreationStartPillarFloorIndex=1;Mode.WallCreationStartLocation=Start;
   Mode.WallCreationMouseLocation=Mode.ResolveCreationRectangleEnd(Start,B->GetActorTransform().TransformPosition(FVector(-5000+SignX*100,-5000+SignY*100,0)),B->GetActorTransform(),nullptr);
   const FVector LocalA=B->GetActorTransform().InverseTransformPosition(Start),LocalZ=B->GetActorTransform().InverseTransformPosition(Mode.WallCreationMouseLocation);
   const TArray<FVector> Expected={LocalA,FVector(LocalZ.X,LocalA.Y,LocalA.Z),LocalZ,FVector(LocalA.X,LocalZ.Y,LocalA.Z)};
   if(!TestTrue(TEXT("Actual release creates exact rectangle"),Mode.FinishWallCreationRectangleDrag()))return false;
   for(auto* E:B->QueryElements(FEHBElementQuery()))F.Actors.AddUnique(E);
   TestEqual(TEXT("Rectangle creates four walls"),CountRectangleWalls(),PriorWalls+4);
   for(const auto& Corner:Expected)TestTrue(TEXT("Rectangle authoring node equals preview corner"),B->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.LocalTransform.GetLocation().Equals(Corner,0.001);}));
   const auto RectangleState=LiveNodeAuthoritySnapshot(B);GEditor->UndoTransaction();TestEqual(TEXT("Dimensioned rectangle undo"),LiveNodeAuthoritySnapshot(B),PriorRectangle);
   GEditor->RedoTransaction();TestEqual(TEXT("Dimensioned rectangle redo"),LiveNodeAuthoritySnapshot(B),RectangleState);GEditor->UndoTransaction();
  }
  Mode.CreationAssist={};Mode.SetWallCreationPhysicalColumns(false);
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const int32 Columns=B->WallNodeOwnership.Bindings.Num();
  Mode.bWallCreationDragging=true;Mode.WallCreationStartLocation=B->GetActorTransform().TransformPosition(FVector(-900,-700,0));Mode.WallCreationStartPillarFloorIndex=1;
  TestTrue(TEXT("Wall-only free click handled"),Mode.FinishWallCreationSinglePillar());TestEqual(TEXT("Wall-only click creates no orphan nodes or actors"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Free click does not publish edit"),LiveNodeReceipt(B),Receipt);
  Mode.bWallCreationDragging=true;Mode.WallCreationStartPillarFloorIndex=1;Mode.WallCreationStartLocation=B->GetActorTransform().TransformPosition(FVector(-900,-700,0));Mode.WallCreationMouseLocation=B->GetActorTransform().TransformPosition(FVector(-300,-200,0));
  TestTrue(TEXT("Wall-only rectangle from free space"),Mode.FinishWallCreationRectangleDrag());for(auto* E:B->QueryElements(FEHBElementQuery()))F.Actors.AddUnique(E);
  TestEqual(TEXT("New rectangle has no new physical columns"),B->WallNodeOwnership.Bindings.Num(),Columns);TestEqual(TEXT("New rectangle adds four junction components"),B->GetInstanceComponents().Num(),4);
  const auto Rectangle=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo wall-only rectangle"),GEditor->UndoTransaction());TestEqual(TEXT("Rectangle undo exact including empty building"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo wall-only rectangle"),GEditor->RedoTransaction());TestEqual(TEXT("Rectangle redo exact"),LiveNodeAuthoritySnapshot(B),Rectangle);
  AEHB_Wall* Wall=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E);W&&W->LocalStart.X<-290&&W->LocalStart.Y<-190){Wall=W;break;}if(!TestNotNull(TEXT("Resolve actual newly created wall"),Wall))return false;
  // Explicit click creates a node in a wall, and returns that identity separately
  // from a null physical actor. Legacy InsertColumn callers still create columns.
  const float Distance=FVector::Dist2D(Wall->LocalStart,Wall->LocalEnd)*0.5f;
  auto Insert=[&](bool Preview=false){return EHBWallCreationCommand::InsertNodeOnWall(B,Wall,Distance,300,20,false,Preview);};
  TestTrue(TEXT("Logical click split preview"),Insert(true).bSucceeded);TestEqual(TEXT("Click preview read only"),LiveNodeAuthoritySnapshot(B),Rectangle);
  EHBWallCreationCommand::FailAfterSplit=1;TestEqual(TEXT("Logical single split failure rollback"),Insert().Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Failed single split exact"),LiveNodeAuthoritySnapshot(B),Rectangle);
  Mode.WallCreationStartWall=Wall;Mode.WallCreationStartWallDistance=Distance;Mode.bWallCreationDragging=true;
  TestTrue(TEXT("Actual single-click creates and selects unbound wall node"),Mode.FinishWallCreationSinglePillar());for(auto* E:B->QueryElements(FEHBElementQuery()))F.Actors.AddUnique(E);
  TestTrue(TEXT("Logical inserted node selected"),Mode.SelectedWallNode.IsValid()&&B->FindWallNodeJunction(Mode.SelectedWallNode));TestEqual(TEXT("Click adds no physical column"),B->WallNodeOwnership.Bindings.Num(),Columns);
  TestTrue(TEXT("Undo clicked node"),GEditor->UndoTransaction(false));TestEqual(TEXT("Clicked node undo restores rectangle"),LiveNodeAuthoritySnapshot(B),Rectangle);
  Mode.bWallCreationDragging=true;Mode.SetWallCreationPhysicalColumns(true);TestFalse(TEXT("Changing creation policy cancels active drag"),Mode.bWallCreationDragging);TestTrue(TEXT("Physical policy restored"),Mode.ShouldCreateWallColumns());Mode.CancelWallCreation();
  if(Finishes){FEHBWallNodeModel M;TestTrue(TEXT("Final model with preserved room finishes captures"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,M).bSucceeded);TestTrue(TEXT("Unrelated existing finishes preserved"),FEHBCopyOutlinePolicy::Prepare(B,M,B->QueryElements(FEHBElementQuery())).bSucceeded);}
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeWallSplitTest,"EHB.Topology.OptionalNodeWallSplit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeWallSplitTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 for(bool Finishes:{false,true})for(bool AllUnbound:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
  if(Finishes&&!AddCopyRoomOutlines(Fixture,true))return false;if(!B->MigrateWallNodeOwnership(true).bSucceeded||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Bindings=B->WallNodeOwnership.Bindings;TSet<FGuid> Unbound;
  for(int32 I=0;I<Bindings.Num();++I)if(AllUnbound||Bindings[I].PhysicalPillarGuid==Fixture.Pillars[1]->ElementGuid)
  {
   const auto& Binding=Bindings[I];const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto Removed=UEHBBuildingToolset::RemovePhysicalColumn(B,N->NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;Unbound.Add(Binding.NodeGuid);
  }
  if(!TestEqual(TEXT("Split fixture has optional authority"),B->WallNodeAuthority.Version,2))return false;
  auto* Source=Fixture.Walls[1];const auto SourceGuid=Source->ElementGuid;const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const int32 PhysicalBefore=B->WallNodeOwnership.Bindings.Num();const auto NodesBefore=B->WallNodeAuthority.Nodes;
  auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)++N;return N;};const int32 CountBefore=Count();
  auto Commit=[&](){return UEHBBuildingToolset::CommitPlainWallSplit(B,SourceGuid,210,B->RelationshipGraphRevision,Source->LocalStart,Source->LocalEnd,Source->Height,Source->Thickness);};
  const auto Preview=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,SourceGuid,210);TestTrue(TEXT("Unbound wall split geometric preview succeeds"),Preview.bSucceeded);
  if(Finishes){FEHBRoomSubdivision Plan;FName Reason;TestTrue(*FString::Printf(TEXT("Optional room dependency plan: %s"),*Reason.ToString()),Plan.Prepare(B,Source,Preview,B->QueryElements(FEHBElementQuery()),Reason));TestEqual(TEXT("Room plan keeps every unbound logical node"),Plan.Candidate.PillarBindings.Num(),PhysicalBefore+1);}
  TestEqual(TEXT("Preview is read only"),LiveNodeAuthoritySnapshot(B),Before);
  EHBWallCreationCommand::FailAfterSplit=1;const auto Interrupted=Commit();TestEqual(TEXT("Optional split interrupted before final dependency application rolls back"),Interrupted.Message,FString(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Interrupted split restores model geometry and finishes"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Interrupted split has no leaked column or wall"),Count(),CountBefore);
  for(auto Phase:{EHBWallSplitTestHooks::EFailurePhase::AfterGeometry,EHBWallSplitTestHooks::EFailurePhase::AfterBaseline,EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord})
  {
   EHBWallSplitTestHooks::FailurePhase=Phase;const auto Failed=Commit();TestEqual(TEXT("Optional split failure rolls back completely"),Failed.Message,FString(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Optional split rollback exact model and geometry"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Optional split rollback exact receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("Optional split rollback actor count"),Count(),CountBefore);TestFalse(TEXT("No redo of failed split"),GEditor->Trans->CanRedo());
  }
  const auto Applied=Commit();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);
  TestNull(TEXT("Old source wall removed"),B->FindElementActorByGuid(SourceGuid));TestEqual(TEXT("Split adds one physical column only"),B->WallNodeOwnership.Bindings.Num(),PhysicalBefore+1);TestEqual(TEXT("Split adds exactly one logical node"),B->WallNodeAuthority.Nodes.Num(),NodesBefore.Num()+1);TestEqual(TEXT("Two new walls replace one, plus one inserted column"),Count(),CountBefore+2);
  for(const auto& N:NodesBefore){const auto* Actual=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});TestTrue(TEXT("Every original logical node keeps its exact pose and revision"),Actual&&Actual->LocalTransform.Equals(N.LocalTransform,0)&&Actual->GeometryRevision==N.GeometryRevision);if(Unbound.Contains(N.NodeGuid))TestFalse(TEXT("Existing unbound node never gains a manufactured physical column"),B->FindPhysicalPillarForNode(N.NodeGuid).IsValid());}
  FEHBWallNodeModel Model;TestTrue(TEXT("Final optional split model validates"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);if(Finishes)TestTrue(TEXT("Final floor/slab outlines contacts and room identities validate"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);TestEqual(TEXT("Rooms remain closed after subdivision of their wall"),B->GetClosedLoopsByFloor(1).Num(),Finishes?2:1);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Derived fills rebuild after split"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Explicit post-split rebuild is stable"),LiveNodeAuthoritySnapshot(B),After);
  TestTrue(TEXT("Undo optional split"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores the old source wall, geometry, bindings and finishes"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Undo restores prior receipt"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo optional split"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact optional split state"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Redo exact split receipt"),LiveNodeReceipt(B),AfterReceipt);
  GEditor->UndoTransaction(false);TestEqual(TEXT("Undo leaves initial actor count"),Count(),CountBefore);
  if(Finishes)
  {
   FEHBWallCreationEndpoint Anchor,Free;Anchor.Wall=Source;Anchor.WallDistance=210;Anchor.LocalLocation=Preview.LocalPillarPosition;Anchor.WorldLocation=B->GetActorTransform().TransformPosition(Anchor.LocalLocation);Free.LocalLocation=Anchor.LocalLocation+FVector(-180,0,0);Free.WorldLocation=B->GetActorTransform().TransformPosition(Free.LocalLocation);
   auto Branch=[&](bool PreviewOnly=false){return EHBWallCreationCommand::CommitAnchoredPath(B,{Anchor,Free},false,FEHBWallCreationOptions(),PreviewOnly);};
   const auto Proposed=Branch(true);if(!TestTrue(*FString::Printf(TEXT("Optional wall branch preview: %s"),*Proposed.Status.ToString()),Proposed.bSucceeded))return false;TestEqual(TEXT("Optional branch plan is read only"),LiveNodeAuthoritySnapshot(B),Before);
   EHBWallCreationCommand::FailAfterEdge=1;const auto Failed=Branch();TestEqual(TEXT("Branch failure after new edge rolls back split and room edits"),Failed.Status,FName(TEXT("SplitFailedRolledBack")));TestEqual(TEXT("Failed optional branch restores full source state"),LiveNodeAuthoritySnapshot(B),Before);
   const auto Branched=Branch();if(!TestTrue(*FString::Printf(TEXT("Optional wall branch commit: %s / %s"),*Branched.Status.ToString(),*Branched.FailureReason.ToString()),Branched.bSucceeded))return false;for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);TestEqual(TEXT("Branch creates only the requested split and free columns"),B->WallNodeOwnership.Bindings.Num(),PhysicalBefore+2);for(FGuid Id:Unbound)TestFalse(TEXT("Wall branch never materializes original empty corners"),B->FindPhysicalPillarForNode(Id).IsValid());FEHBWallNodeModel BranchModel;TestTrue(TEXT("Optional branch model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,BranchModel).bSucceeded);TestTrue(TEXT("Optional branch room finishes validate"),FEHBCopyOutlinePolicy::Prepare(B,BranchModel,B->QueryElements(FEHBElementQuery())).bSucceeded);
   TArray<FEHBNodeRoomBoundary> PlannedRooms;FName RoomReason;TestTrue(TEXT("Branch room model builds"),FEHBWallNodeRooms::Build(B->BuildingGuid,BranchModel,PlannedRooms,RoomReason));TestEqual(TEXT("An open branch does not subdivide rooms"),PlannedRooms.Num(),2);
   for(const auto& Room:PlannedRooms){FEHBNodeRoomBoundary Cached;TestTrue(TEXT("Runtime cache and planner share the same branch room identity"),B->TryGetRoomBoundary(Room.RoomGuid,Room.FloorIndex,Cached));TestTrue(TEXT("Runtime branch boundary contains exactly the planned paired edges"),Cached.NodeGuids==Room.NodeGuids&&Cached.WallGuids==Room.WallGuids&&Cached.Polygon==Room.Polygon);TestFalse(TEXT("Dangling wall is not a room boundary edge"),Cached.WallGuids.Contains(Branched.PrimaryWall->ElementGuid));}
   const auto BranchState=LiveNodeAuthoritySnapshot(B);B->RebuildClosedLoops();TestTrue(TEXT("Branch geometry full rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Branch explicit rebuild stays exact"),LiveNodeAuthoritySnapshot(B),BranchState);TestTrue(TEXT("Undo compound optional branch"),GEditor->UndoTransaction());TestEqual(TEXT("Optional branch undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo compound optional branch"),GEditor->RedoTransaction());TestEqual(TEXT("Optional branch redo exact"),LiveNodeAuthoritySnapshot(B),BranchState);GEditor->UndoTransaction(false);
  }
  FEasyHouseEditorMode Mode;Mode.SetActiveBuilding(B);
  auto Click=[&](){Mode.WallCreationStartWall=Source;Mode.WallCreationStartWallDistance=210;Mode.WallCreationHeight=Source->Height;Mode.WallCreationThickness=Source->Thickness;Mode.bWallCreationDragging=true;return Mode.FinishWallCreationSinglePillar();};
  EHBWallCreationCommand::FailAfterSplit=1;TestFalse(TEXT("Actual single-click callback reports split rollback"),Click());TestEqual(TEXT("Click failure does not fall back to free-column creation"),LiveNodeAuthoritySnapshot(B),Before);TestFalse(TEXT("Click failure clears dragging"),Mode.bWallCreationDragging);
  const auto Different=EHBWallCreationCommand::InsertColumnOnWall(B,Source,210,400,40,true);TestEqual(TEXT("Unplanned different column dimensions explicitly rejected"),Different.Status,FName(TEXT("WallColumnDimensionsRequirePlan")));TestEqual(TEXT("Dimension rejection keeps all data"),LiveNodeAuthoritySnapshot(B),Before);
  TestTrue(TEXT("Actual single-click callback inserts into optional wall"),Click());auto* SelectedPillar=GEditor->GetSelectedActors()->GetTop<AEHB_Pillar>();TestTrue(TEXT("Click selects the actual new physical column"),SelectedPillar&&SelectedPillar->OwningBuilding==B&&!Before.Contains(SelectedPillar->ElementGuid.ToString()));for(auto* E:B->QueryElements(FEHBElementQuery()))Fixture.Actors.AddUnique(E);TestEqual(TEXT("Click creates one column and one net wall"),Count(),CountBefore+2);TestTrue(TEXT("Single click is one undoable composite operation"),GEditor->UndoTransaction(false));TestEqual(TEXT("Single click undo restores exact initial state"),LiveNodeAuthoritySnapshot(B),Before);GEditor->SelectNone(false,true,false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLevelEditorBuildingDuplicateTest,"EHB.Topology.LevelEditorBuildingDuplicate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLevelEditorBuildingDuplicateTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World||!GUnrealEd)return false;
 auto Commands=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetGlobalLevelEditorActions();const TSharedRef<const FUICommandInfo> Command=FGenericCommands::Get().Duplicate.ToSharedRef();
 if(!TestFalse(TEXT("Regression starts outside Building Mode"),GLevelEditorModeTools().IsModeActive(FEasyHouseEditorMode::EM_EasyHouseEditorModeId)))return false;
 ON_SCOPE_EXIT{GLevelEditorModeTools().DeactivateMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);};
 TArray<TWeakObjectPtr<AActor>> PreviousSelection;for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))PreviousSelection.Add(A);
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);for(auto A:PreviousSelection)if(A.IsValid()&&!A->HasAnyFlags(RF_NewerVersionExists))GEditor->SelectActor(A.Get(),true,false);};
 auto Select=[&](const TArray<AActor*>& Actors){GEditor->SelectNone(false,true,false);for(auto* A:Actors)GEditor->SelectActor(A,true,false);GEditor->NoteSelectionChange();};
 auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(World);It;++It)++N;return N;};
 // Exercise the very command list used by Ctrl+W, without calling the copy helper directly.
 for(int32 Variant=0;Variant<3;++Variant)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* Source=Fixture.Building;Source->SetActorRotation(FRotator(0,37,0));if(!Source->MigrateWallNodeOwnership(true).bSucceeded)return false;
  Select({Source});
  if(Variant>0)
  {
   if(!Source->MigrateWallNodeAuthority(true).bSucceeded)return false;const auto Bindings=Source->WallNodeOwnership.Bindings;
   for(int32 I=0;I<Bindings.Num();++I)if(Variant==2||I==0){const auto& Binding=Bindings[I];const auto* N=Source->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(Source,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  }
  const auto Before=LiveNodeAuthoritySnapshot(Source),Receipt=LiveNodeReceipt(Source);const int32 BeforeCount=Count();const FVector Offset=EHBBuildingCopy::SuggestedWorldOffset(Source);
  if(Variant==2){GLevelEditorModeTools().ActivateMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);auto* Mode=GLevelEditorModeTools().GetActiveModeTyped<FEasyHouseEditorMode>(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);if(!TestNotNull(TEXT("Activate actual registered Building Mode"),Mode))return false;Mode->SetActiveBuilding(Source);Mode->BeginWallCreation(Source,300,20);Select({Source});}
  TestTrue(TEXT("Building duplicate command enabled with or without Building Mode"),Commands->CanExecuteAction(Command));
  const auto Chord=Command->GetActiveChord(EMultipleKeyBindingIndex::Primary);if(!TestTrue(TEXT("Duplicate has an active shortcut"),Chord->IsValidChord()))return false;
  const FKeyEvent KeyEvent(Chord->Key,FModifierKeysState(Chord->bShift,false,Chord->bCtrl,false,Chord->bAlt,false,Chord->bCmd,false,false),0,false,0,0);
  TestTrue(TEXT("Actual Level Editor shortcut binding executes"),Commands->ProcessCommandBindings(KeyEvent));
  auto* Copy=Cast<AEHBBuildingActorBase>(GEditor->GetSelectedActors()->GetTop(AEHBBuildingActorBase::StaticClass()));
  if(!TestTrue(TEXT("Command selects an independent building, not source"),Copy&&Copy!=Source))return false;
  if(Variant==2){auto* Mode=GLevelEditorModeTools().GetActiveModeTyped<FEasyHouseEditorMode>(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);TestEqual(TEXT("Command also switches actual Building Mode to copy"),Mode->GetActiveBuilding(),Copy);}
  Fixture.Actors.Add(Copy);for(auto* E:Copy->QueryElements(FEHBElementQuery()))Fixture.Actors.Add(E);
  TestEqual(TEXT("Command copied every authored element from root-only selection"),Copy->QueryElements(FEHBElementQuery()).Num(),Source->QueryElements(FEHBElementQuery()).Num());
  TestTrue(TEXT("Suggested placement separates building bounding boxes"),(Copy->GetActorLocation()-Source->GetActorLocation()).Equals(Offset,1e-6));
  TestEqual(TEXT("Command preserves source exact geometry and IDs"),LiveNodeAuthoritySnapshot(Source),Before);TestEqual(TEXT("Command preserves source receipt"),LiveNodeReceipt(Source),Receipt);
  FEHBWallNodeModel Model;TestTrue(TEXT("Command copy has valid logical topology"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(Copy,Model).bSucceeded);TestTrue(TEXT("Command copy retains complete rooms floor slab contacts"),FEHBCopyOutlinePolicy::Prepare(Copy,Model,Copy->QueryElements(FEHBElementQuery())).bSucceeded);
  TestTrue(TEXT("Building identities independent"),Source->BuildingGuid!=Copy->BuildingGuid);for(auto* E:Copy->QueryElements(FEHBElementQuery()))TestFalse(TEXT("Copied element ID never aliases source"),Source->FindElementActorByGuid(E->ElementGuid)!=nullptr);
  const auto After=LiveNodeAuthoritySnapshot(Copy);TestTrue(TEXT("One undo removes the full command copy"),GEditor->UndoTransaction());TestEqual(TEXT("Undo leaves no partial actors"),Count(),BeforeCount);TestTrue(TEXT("Redo complete command copy"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact copied state"),LiveNodeAuthoritySnapshot(Copy),After);GEditor->UndoTransaction(false);
  // Duplicate root plus a subset of its children still expands from the root exactly once.
  Select({Source,Fixture.Walls[0]});TestTrue(TEXT("Root plus child action executes"),Commands->TryExecuteAction(Command));auto* Other=Cast<AEHBBuildingActorBase>(GEditor->GetSelectedActors()->GetTop(AEHBBuildingActorBase::StaticClass()));if(!TestTrue(TEXT("Root and child selection makes one complete copy"),Other&&Other!=Source&&Count()==BeforeCount+Source->QueryElements(FEHBElementQuery()).Num()+1))return false;Fixture.Actors.Add(Other);for(auto* E:Other->QueryElements(FEHBElementQuery()))Fixture.Actors.Add(E);GEditor->UndoTransaction(false);
  for(int32 Phase=0;Phase<2;++Phase)
  {
   Select({Source});EHBBuildingCopy::FailAfterImport=Phase==0;EHBBuildingCopy::FailAfterApply=Phase==1;
   Commands->TryExecuteAction(Command);EHBBuildingCopy::FailAfterImport=false;EHBBuildingCopy::FailAfterApply=false;
   TestEqual(TEXT("Failed command does not fall back to ordinary duplicate"),Count(),BeforeCount);TestEqual(TEXT("Failed command preserves original exact state"),LiveNodeAuthoritySnapshot(Source),Before);TestEqual(TEXT("Failed command preserves receipt"),LiveNodeReceipt(Source),Receipt);TestFalse(TEXT("Failed copied group cannot be redone"),GEditor->Trans->CanRedo());
  }
  Select({Fixture.Walls[0]});Commands->TryExecuteAction(Command);TestEqual(TEXT("Partial wall selection is consumed without import"),Count(),BeforeCount);
  Select({Source});GEditor->SelectComponent(Fixture.Walls[0]->GetRootComponent(),true,false);TestTrue(TEXT("Fixture selects an actual component"),GEditor->GetSelectedComponents()->CountSelections<UActorComponent>()>0);Commands->TryExecuteAction(Command);TestEqual(TEXT("Component selection cannot fall back to actor import"),Count(),BeforeCount);
  auto* Plain=World->SpawnActor<AActor>();Fixture.Actors.Add(Plain);Select({Source,Plain});Commands->TryExecuteAction(Command);TestEqual(TEXT("Mixed unrelated selection produces no partial work"),Count(),BeforeCount+1);
  FTransientTopologyFixture Second;if(!Second.Create(World,RF_Transactional))return false;const auto BothCount=Count();Select({Source,Second.Building});Commands->TryExecuteAction(Command);TestEqual(TEXT("Multiple buildings rejected atomically"),Count(),BothCount);
  Select({Second.Building});Commands->TryExecuteAction(Command);TestEqual(TEXT("Unprepared building cannot fall through to raw duplicate"),Count(),BothCount);TestEqual(TEXT("Command does not silently migrate ownership"),Second.Building->WallNodeOwnership.Version,0);
  Select({Source});{FScopedTransaction Transaction(FText::FromString(TEXT("EHB duplicate disabled during edit")));TestFalse(TEXT("Duplicate disabled inside another edit transaction"),Commands->CanExecuteAction(Command));TestFalse(TEXT("Command cannot start nested copy transaction"),Commands->TryExecuteAction(Command));Transaction.Cancel();}
  TestEqual(TEXT("All command guards leave source exact"),LiveNodeAuthoritySnapshot(Source),Before);GEditor->SelectNone(false,true,false);if(Variant==2)GLevelEditorModeTools().DeactivateMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);
 }
 // Verify actual native fallback, rather than substituting a test delegate for it.
 FTransientTopologyFixture PlainFixture;auto* Plain=World->SpawnActor<AActor>();Plain->SetFlags(RF_Transactional);Plain->SetActorLabel(TEXT("EHB ordinary duplicate regression"));PlainFixture.Actors.Add(Plain);Select({Plain});const int32 NativeCount=Count();
 TestTrue(TEXT("Ordinary non-building duplicate retains original enable behavior"),Commands->CanExecuteAction(Command));TestTrue(TEXT("Ordinary non-building native command executes"),Commands->TryExecuteAction(Command));auto* PlainCopy=GEditor->GetSelectedActors()->GetTop<AActor>();
 if(TestTrue(TEXT("Native fallback duplicates ordinary actor"),PlainCopy&&PlainCopy!=Plain&&Count()==NativeCount+1)){PlainFixture.Actors.Add(PlainCopy);TestTrue(TEXT("Native ordinary duplicate still undoes"),GEditor->UndoTransaction(false));TestEqual(TEXT("Native undo actor count"),Count(),NativeCount);}
 GEditor->SelectNone(false,true,false);
 const auto InstalledHandle=Commands->GetActionForCommand(Command)->ExecuteAction.GetHandle();EHBBuildingCopy::UnregisterDuplicateCommand();const FUIAction Original=*Commands->GetActionForCommand(Command);TestTrue(TEXT("Unregister restores original Level Editor execute delegate"),Original.ExecuteAction.GetHandle()!=InstalledHandle);
 EHBBuildingCopy::RegisterDuplicateCommand();const auto Reinstalled=Commands->GetActionForCommand(Command)->ExecuteAction.GetHandle();EHBBuildingCopy::RegisterDuplicateCommand();TestTrue(TEXT("Repeated registration never wraps itself"),Commands->GetActionForCommand(Command)->ExecuteAction.GetHandle()==Reinstalled);EHBBuildingCopy::UnregisterDuplicateCommand();const auto* Restored=Commands->GetActionForCommand(Command);TestTrue(TEXT("Lifecycle preserves original execute and enable delegates"),Restored->ExecuteAction.GetHandle()==Original.ExecuteAction.GetHandle()&&Restored->CanExecuteAction.GetHandle()==Original.CanExecuteAction.GetHandle());EHBBuildingCopy::RegisterDuplicateCommand();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeCreationPolicyWriteTest,"EHBValidation.Persistence.WriteNodeCreationPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeCreationPolicyWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("NodeCreationPolicy")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite node creation policy evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!World)return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.NodeCreationPolicy.v1"));TArray<TSharedPtr<FJsonValue>> Entries;
 for(int32 Variant=0;Variant<4;++Variant)
 {
  FTransientTopologyFixture Fixture;
  if(Variant>=2){if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;}
  else {FActorSpawnParameters P;P.ObjectFlags=RF_Transactional;Fixture.Building=World->SpawnActor<AEHB_Building>(AEHB_Building::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,P);Fixture.Actors.Add(Fixture.Building);}
  auto* B=Fixture.Building;if(!B)return false;B->SetActorLocationAndRotation(FVector(0,Variant*2400,0),FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto Enable=UEHBBuildingToolset::EnableWallNodeEditing(B,false);if(!TestTrue(*Enable.Message,Enable.bSucceeded))return false;
  FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=false;
  if(Variant==1)
  {
   TArray<FEHBWallCreationEndpoint> Points;for(FVector P:{FVector(0,0,0),FVector(600,0,0),FVector(600,500,0),FVector(0,500,0)}){auto& E=Points.AddDefaulted_GetRef();E.LocalLocation=P;E.WorldLocation=B->GetActorTransform().TransformPosition(P);}
   const auto R=EHBWallCreationCommand::Commit(B,Points,true,Options);if(!TestTrue(*R.Status.ToString(),R.bSucceeded))return false;
  }
  if(Variant==2){const auto R=EHBWallCreationCommand::InsertNodeOnWall(B,Fixture.Walls[0],210,300,20,false);if(!TestTrue(*R.Status.ToString(),R.bSucceeded))return false;}
  auto Evidence=NodeCopyPersistenceEvidence(B);Evidence->SetNumberField(TEXT("variant"),Variant);Entries.Add(MakeShared<FJsonValueObject>(Evidence));
  B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 Data->SetArrayField(TEXT("buildings"),Entries);
 if(!TestTrue(TEXT("Save empty activated, new wall-only, mixed split and migrated finished buildings"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Write exact node-creation evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeCreationPolicyReadTest,"EHBValidation.Persistence.ReadNodeCreationPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeCreationPolicyReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("NodeCreationPolicy"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.NodeCreationPolicy.v1"))return false;
 TMap<FString,AEHBBuildingActorBase*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(It->BuildingGuid.ToString(),*It);TestEqual(TEXT("Four authored policy fixtures cold loaded"),Buildings.Num(),4);
 TMap<AEHBBuildingActorBase*,FString> Expected;
 for(const auto& Entry:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Entry->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));if(!TestNotNull(TEXT("Saved policy building resolves"),B))return false;
  const auto State=E->GetStringField(TEXT("state"));Expected.Add(B,State);TestEqual(TEXT("Cold policy model and geometry exact"),LiveNodeAuthoritySnapshot(B),State);TestEqual(TEXT("Cold activation or creation receipt exact"),LiveNodeReceipt(B),E->GetStringField(TEXT("receipt")));TestEqual(TEXT("Cold optional policy retained"),B->WallNodeAuthority.Version,2);TestEqual(TEXT("Cold authored elements exact"),B->QueryElements(FEHBElementQuery()).Num(),E->GetIntegerField(TEXT("elementCount")));TestEqual(TEXT("Cold physical bindings exact"),B->WallNodeOwnership.Bindings.Num(),E->GetIntegerField(TEXT("physicalCount")));
  TestEqual(TEXT("Only incident unbound nodes have derived component registry"),B->GetInstanceComponents().Num(),B->WallNodeAuthority.Nodes.Num()-B->WallNodeOwnership.Bindings.Num());
  FEHBWallNodeModel Model;TestTrue(TEXT("Cold model captures, including empty model"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold room outlines and contacts validate"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);
  for(const auto& Room:E->GetArrayField(TEXT("rooms"))){FEHBNodeRoomBoundary Saved,Actual;FJsonObjectConverter::JsonObjectToUStruct(Room->AsObject().ToSharedRef(),&Saved);TestTrue(TEXT("Cold paired room cycle restored"),B->TryGetRoomBoundary(Saved.RoomGuid,Saved.FloorIndex,Actual)&&Saved.NodeGuids==Actual.NodeGuids&&Saved.WallGuids==Actual.WallGuids&&Saved.Polygon==Actual.Polygon);}
  B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Cold policy geometry rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Cold rebuild leaves saved state exact"),LiveNodeAuthoritySnapshot(B),State);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);TestEqual(TEXT("Public activation is idempotent on cold load"),UEHBBuildingToolset::EnableWallNodeEditing(B,false).Message,FString(TEXT("AlreadyEnabled")));TestEqual(TEXT("Cold idempotence read only"),LiveNodeAuthoritySnapshot(B),State);
 }
 for(const auto& Entry:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Entry->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));const int32 Variant=E->GetIntegerField(TEXT("variant")),Physical=B->WallNodeOwnership.Bindings.Num();GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto Receipt=LiveNodeReceipt(B);FEHBWallCreationOptions Options;Options.bCreatePhysicalColumns=false;TArray<FEHBWallCreationEndpoint> Points;
  auto Free=[&](FVector P){FEHBWallCreationEndpoint R;R.LocalLocation=P;R.WorldLocation=B->GetActorTransform().TransformPosition(P);return R;};
  if(Variant==0){for(FVector P:{FVector(0,0,0),FVector(600,0,0),FVector(600,500,0),FVector(0,500,0)})Points.Add(Free(P));}
  else if(Variant==1)
  {
   FEHBWallNodeModel M;if(!TestTrue(TEXT("Capture cold logical source wall model"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,M).bSucceeded))return false;
   const auto* A=M.Nodes.FindByPredicate([](const auto& N){return N.LocalTransform.GetLocation().Equals(FVector::ZeroVector,0.001);});
   const auto* Z=M.Nodes.FindByPredicate([](const auto& N){return N.LocalTransform.GetLocation().Equals(FVector(600,0,0),0.001);});
   const auto* Edge=A&&Z?M.Walls.FindByPredicate([&](const auto& W){return W.StartNodeGuid==A->NodeGuid&&W.EndNodeGuid==Z->NodeGuid;}):nullptr;
   auto* Wall=Edge?Cast<AEHB_Wall>(B->FindElementActorByGuid(Edge->WallGuid)):nullptr;if(!TestNotNull(TEXT("Resolve source by logical endpoints rather than clipped visible span"),Wall))return false;
   const auto P=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Wall->ElementGuid,300);auto Anchor=Free(P.LocalPillarPosition);Anchor.Wall=Wall;Anchor.WallDistance=300;Points={Anchor,Free(FVector(300,-240,0))};
  }
  else
  {
   const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([](const auto& V){return V.LocalTransform.GetLocation().Equals(FVector::ZeroVector,0.001);});if(!N)return false;auto A=Free(N->LocalTransform.GetLocation());A.NodeGuid=N->NodeGuid;A.ExpectedNodeRevision=N->GeometryRevision;Points={A,Free(FVector(-240,-130,0))};
  }
  auto Commit=[&](bool Preview=false){return Variant==1?EHBWallCreationCommand::CommitAnchoredPath(B,Points,false,Options,Preview):EHBWallCreationCommand::Commit(B,Points,Variant==0,Options,Preview);};
  TestTrue(TEXT("Cold next operation previews"),Commit(true).bSucceeded);TestEqual(TEXT("Cold continuation preview preserves snapshot"),LiveNodeAuthoritySnapshot(B),Expected.FindChecked(B));
  const auto R=Commit();if(!TestTrue(*FString::Printf(TEXT("Cold variant %d continued creation %s / %s"),Variant,*R.Status.ToString(),*R.FailureReason.ToString()),R.bSucceeded))return false;
  TestEqual(TEXT("Cold continuation never manufactures columns"),B->WallNodeOwnership.Bindings.Num(),Physical);for(const auto& Pair:Expected)if(Pair.Key!=B)TestEqual(TEXT("Cold continuation isolates every other building"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
  FEHBWallNodeModel Model;TestTrue(TEXT("Cold continued model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold continued room finishes valid"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo cold policy continuation"),GEditor->UndoTransaction());TestEqual(TEXT("Cold policy undo exact"),LiveNodeAuthoritySnapshot(B),Expected.FindChecked(B));TestEqual(TEXT("Cold policy undo receipt"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo cold policy continuation"),GEditor->RedoTransaction());TestEqual(TEXT("Cold policy redo exact"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold policy redo receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
 }
 GEditor->SelectNone(false,true,false);return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodePathWriteTest,"EHBValidation.Persistence.WriteOptionalNodePath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodePathWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("OptionalNodePath")))return false;if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite logical path persistence evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!World)return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.OptionalNodePath.v1"));const bool Anchored=FParse::Param(FCommandLine::Get(),TEXT("EHBLogicalAnchoredPath"));Data->SetBoolField(TEXT("anchored"),Anchored);TArray<TSharedPtr<FJsonValue>> Values;
 for(bool AllUnbound:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorLocationAndRotation(FVector(0,AllUnbound?2200:0,0),FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true)||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  const FGuid StartId=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid),EndId=B->FindNodeForPhysicalPillar(Fixture.Pillars[2]->ElementGuid),NextId=B->FindNodeForPhysicalPillar(Fixture.Pillars[3]->ElementGuid);const auto Bindings=B->WallNodeOwnership.Bindings;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);TArray<TSharedPtr<FJsonValue>> Unbound;
  for(const auto& Binding:Bindings)if(AllUnbound||Binding.NodeGuid==StartId||Binding.NodeGuid==NextId){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;Unbound.Add(MakeShared<FJsonValueString>(Binding.NodeGuid.ToString()));}
  auto Anchor=[&](FGuid Id){FEHBWallCreationEndpoint E;const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});E.NodeGuid=Id;E.ExpectedNodeRevision=N->GeometryRevision;E.LocalLocation=N->LocalTransform.GetLocation();E.WorldLocation=B->GetActorTransform().TransformPosition(E.LocalLocation);E.FloorIndex=N->FloorIndex;return E;};
  const auto Partition=EHBWallCreationCommand::Commit(B,{Anchor(StartId),Anchor(EndId)},false,FEHBWallCreationOptions());if(!TestTrue(*FString::Printf(TEXT("Persisted logical partition: %s / %s"),*Partition.Status.ToString(),*Partition.FailureReason.ToString()),Partition.bSucceeded))return false;
  FEHBWallCreationEndpoint Free;Free.LocalLocation=FVector(-240,-130,0);Free.WorldLocation=B->GetActorTransform().TransformPosition(Free.LocalLocation);
  FEHBWallCreationEndpoint Source;if(Anchored){Source.Wall=Fixture.Walls[2];Source.WallDistance=300;const auto P=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,Source.Wall->ElementGuid,300);Source.LocalLocation=P.LocalPillarPosition;Source.WorldLocation=B->GetActorTransform().TransformPosition(Source.LocalLocation);}
  const auto Branch=Anchored?EHBWallCreationCommand::CommitAnchoredPath(B,{Source,Anchor(StartId)},false,FEHBWallCreationOptions()):EHBWallCreationCommand::Commit(B,{Anchor(StartId),Free},false,FEHBWallCreationOptions());if(!TestTrue(*FString::Printf(TEXT("Persisted logical branch: %s / %s"),*Branch.Status.ToString(),*Branch.FailureReason.ToString()),Branch.bSucceeded))return false;
  auto Evidence=NodeCopyPersistenceEvidence(B);Evidence->SetArrayField(TEXT("unbound"),Unbound);Evidence->SetStringField(TEXT("nextNode"),NextId.ToString());Values.Add(MakeShared<FJsonValueObject>(Evidence));B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 Data->SetArrayField(TEXT("buildings"),Values);if(!TestTrue(TEXT("Save two actual node-path buildings after room partition and extension"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save immutable logical path geometry and identity evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodePathReadTest,"EHBValidation.Persistence.ReadOptionalNodePath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodePathReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("OptionalNodePath"))||!FFileHelper::LoadFileToString(Json,*Path))return false;auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.OptionalNodePath.v1"))return false;
 TMap<FString,AEHBBuildingActorBase*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(It->BuildingGuid.ToString(),*It);TestEqual(TEXT("Two node-path buildings cold loaded"),Buildings.Num(),2);TMap<AEHBBuildingActorBase*,FString> Expected;
 for(const auto& Value:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Value->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));if(!TestNotNull(TEXT("Loaded node-path building identity resolves"),B))return false;const auto State=E->GetStringField(TEXT("state"));Expected.Add(B,State);TestEqual(TEXT("Cold node-path exact geometry and model"),LiveNodeAuthoritySnapshot(B),State);TestEqual(TEXT("Cold path receipt exact"),LiveNodeReceipt(B),E->GetStringField(TEXT("receipt")));TestEqual(TEXT("Cold physical binding count exact"),B->WallNodeOwnership.Bindings.Num(),E->GetIntegerField(TEXT("physicalCount")));TestEqual(TEXT("Cold authored element count exact"),B->QueryElements(FEHBElementQuery()).Num(),E->GetIntegerField(TEXT("elementCount")));TestEqual(TEXT("Cold logical junction registry exact"),B->GetInstanceComponents().Num(),B->WallNodeAuthority.Nodes.Num()-B->WallNodeOwnership.Bindings.Num());
  for(const auto& Id:E->GetArrayField(TEXT("unbound"))){FGuid Node;FGuid::Parse(Id->AsString(),Node);TestFalse(TEXT("Original empty corners remain physically unbound on load"),B->FindPhysicalPillarForNode(Node).IsValid());TestNotNull(TEXT("Original empty corner derived geometry restored"),B->FindWallNodeJunction(Node));}
  bool Anchored=false;Data->TryGetBoolField(TEXT("anchored"),Anchored);TestEqual(TEXT("Saved partition room count retained"),B->GetClosedLoopsByFloor(1).Num(),Anchored?4:3);for(const auto& R:E->GetArrayField(TEXT("rooms"))){FEHBNodeRoomBoundary Before,After;if(!FJsonObjectConverter::JsonObjectToUStruct(R->AsObject().ToSharedRef(),&Before))return false;TestTrue(TEXT("Saved room identity resolves"),B->TryGetRoomBoundary(Before.RoomGuid,Before.FloorIndex,After));TestTrue(TEXT("Saved logical node/wall cycle and polygon retained"),Before.NodeGuids==After.NodeGuids&&Before.WallGuids==After.WallGuids&&Before.Polygon==After.Polygon);}
  FEHBWallNodeModel Model;TestTrue(TEXT("Cold path model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold path floor/slab/contact policy valid"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Cold partition dependencies restored"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members));for(const auto& M:Members){TestEqual(TEXT("Each loaded partition room has one floor"),M.Floors.Num(),1);TestEqual(TEXT("Each loaded partition room has one slab"),M.Slabs.Num(),1);}B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Cold path derived rebuild succeeds"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Cold path rebuild preserves exact saved state"),LiveNodeAuthoritySnapshot(B),State);
 }
 for(const auto& Value:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Value->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));FGuid Node;FGuid::Parse(E->GetStringField(TEXT("nextNode")),Node);const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Node;});if(!N)return false;FEHBWallCreationEndpoint Start,End;Start.NodeGuid=Node;Start.ExpectedNodeRevision=N->GeometryRevision;Start.LocalLocation=N->LocalTransform.GetLocation();Start.WorldLocation=B->GetActorTransform().TransformPosition(Start.LocalLocation);Start.FloorIndex=N->FloorIndex;End.LocalLocation=FVector(-240,630,0);End.WorldLocation=B->GetActorTransform().TransformPosition(End.LocalLocation);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Receipt=LiveNodeReceipt(B);
  bool Anchored=false;Data->TryGetBoolField(TEXT("anchored"),Anchored);TArray<FEHBWallCreationEndpoint> ContinueEndpoints={Start,End};
  if(Anchored)
  {
   FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;
   const auto* W=Model.Walls.FindByPredicate([&](const auto& Wall){const auto* A=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Wall.StartNodeGuid;});const auto* Z=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Wall.EndNodeGuid;});return A&&Z&&A->LocalTransform.GetLocation()==FVector(0,500,0)&&Z->LocalTransform.GetLocation()==FVector(0,0,0);});if(!W)return false;
   FEHBWallCreationEndpoint Source;Source.Wall=Cast<AEHB_Wall>(B->FindElementActorByGuid(W->WallGuid));Source.WallDistance=210;const auto P=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,W->WallGuid,210);if(!P.bSucceeded)return false;Source.LocalLocation=P.LocalPillarPosition;Source.WorldLocation=B->GetActorTransform().TransformPosition(Source.LocalLocation);End.LocalLocation=Source.LocalLocation+FVector(-240,0,0);End.WorldLocation=B->GetActorTransform().TransformPosition(End.LocalLocation);ContinueEndpoints={Source,End,Start};
   FEHBWallPathPreview Preview;TestTrue(TEXT("Cold anchored continuation read-only candidate"),EHBWallCreationCommand::CommitAnchoredPath(B,ContinueEndpoints,false,FEHBWallCreationOptions(),true,&Preview).bSucceeded);TestEqual(TEXT("Cold anchored preview preserves exact saved state"),LiveNodeAuthoritySnapshot(B),Expected.FindChecked(B));
  }
  const auto R=Anchored?EHBWallCreationCommand::CommitAnchoredPath(B,ContinueEndpoints,false,FEHBWallCreationOptions()):EHBWallCreationCommand::Commit(B,ContinueEndpoints,false,FEHBWallCreationOptions());if(!TestTrue(*FString::Printf(TEXT("Continue loaded logical wall: %s / %s"),*R.Status.ToString(),*R.FailureReason.ToString()),R.bSucceeded))return false;for(const auto& Pair:Expected)if(Pair.Key!=B)TestEqual(TEXT("Continuing logical path isolates other loaded building"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo loaded logical wall path"),GEditor->UndoTransaction());TestEqual(TEXT("Cold path exact undo"),LiveNodeAuthoritySnapshot(B),Expected.FindChecked(B));TestEqual(TEXT("Cold path undo receipt"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo loaded logical wall path"),GEditor->RedoTransaction());TestEqual(TEXT("Cold path exact redo"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold path redo receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
 }
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeSplitWriteTest,"EHBValidation.Persistence.WriteOptionalNodeSplit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeSplitWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("OptionalNodeSplit")))return false;if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite node split persistence evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!World)return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.OptionalNodeSplit.v1"));TArray<TSharedPtr<FJsonValue>> Values;
 for(bool AllUnbound:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorLocation(FVector(0,AllUnbound?2000:0,0));B->SetActorRotation(FRotator(0,37,0));if(!AddCopyRoomOutlines(Fixture,true)||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Bindings=B->WallNodeOwnership.Bindings;TArray<TSharedPtr<FJsonValue>> Unbound;
  for(const auto& Binding:Bindings)if(AllUnbound||Binding.PhysicalPillarGuid==Fixture.Pillars[1]->ElementGuid)
  {
   const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;Unbound.Add(MakeShared<FJsonValueString>(Binding.NodeGuid.ToString()));
  }
  auto* Source=Fixture.Walls[1];FEHBWallNodeModel Before;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Before).bSucceeded)return false;const FGuid StartNode=Before.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Source->ElementGuid;})->StartNodeGuid;
  const auto Applied=UEHBBuildingToolset::CommitPlainWallSplit(B,Source->ElementGuid,210,B->RelationshipGraphRevision,Source->LocalStart,Source->LocalEnd,Source->Height,Source->Thickness);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  FEHBWallNodeModel After;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,After).bSucceeded)return false;const auto* Inserted=After.Nodes.FindByPredicate([&](const auto& N){return !Before.Nodes.ContainsByPredicate([&](const auto& O){return O.NodeGuid==N.NodeGuid;});});if(!Inserted)return false;
  const auto* Next=After.Walls.FindByPredicate([&](const auto& W){return W.StartNodeGuid==StartNode&&W.EndNodeGuid==Inserted->NodeGuid;});if(!Next)return false;
  auto Evidence=NodeCopyPersistenceEvidence(B);Evidence->SetArrayField(TEXT("unbound"),Unbound);Evidence->SetStringField(TEXT("nextWall"),Next->WallGuid.ToString());Values.Add(MakeShared<FJsonValueObject>(Evidence));
  B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Fixture.Actors.Reset();
 }
 Data->SetArrayField(TEXT("buildings"),Values);if(!TestTrue(TEXT("Save mixed and formerly zero-column buildings after splitting"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save node split identity and full geometry evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeSplitReadTest,"EHBValidation.Persistence.ReadOptionalNodeSplit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeSplitReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("OptionalNodeSplit"))||!FFileHelper::LoadFileToString(Json,*Path))return false;auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.OptionalNodeSplit.v1"))return false;
 TMap<FString,AEHBBuildingActorBase*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(It->BuildingGuid.ToString(),*It);TestEqual(TEXT("Two actual post-split buildings cold loaded"),Buildings.Num(),2);TMap<AEHBBuildingActorBase*,FString> ExpectedStates;
 for(const auto& Value:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Value->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));if(!TestNotNull(TEXT("Resolve loaded building by saved identity"),B))return false;const auto State=E->GetStringField(TEXT("state"));ExpectedStates.Add(B,State);
  TestEqual(TEXT("Cold split exact model geometry and relations"),LiveNodeAuthoritySnapshot(B),State);TestEqual(TEXT("Cold split exact receipt"),LiveNodeReceipt(B),E->GetStringField(TEXT("receipt")));TestEqual(TEXT("Cold split authored element count"),B->QueryElements(FEHBElementQuery()).Num(),E->GetIntegerField(TEXT("elementCount")));TestEqual(TEXT("Cold split physical count"),B->WallNodeOwnership.Bindings.Num(),E->GetIntegerField(TEXT("physicalCount")));
  TestEqual(TEXT("Derived junction registry reconciles on load"),B->GetInstanceComponents().Num(),B->WallNodeAuthority.Nodes.Num()-B->WallNodeOwnership.Bindings.Num());
  for(const auto& Id:E->GetArrayField(TEXT("unbound"))){FGuid Node;FGuid::Parse(Id->AsString(),Node);TestFalse(TEXT("Formerly unbound corners still lack physical columns"),B->FindPhysicalPillarForNode(Node).IsValid());TestNotNull(TEXT("Loaded unbound corner has derived geometry"),B->FindWallNodeJunction(Node));}
  for(const auto& R:E->GetArrayField(TEXT("rooms"))){FEHBNodeRoomBoundary Before,After;if(!FJsonObjectConverter::JsonObjectToUStruct(R->AsObject().ToSharedRef(),&Before))return false;TestTrue(TEXT("Cold split room resolves by ID"),B->TryGetRoomBoundary(Before.RoomGuid,1,After));TestTrue(TEXT("Cold split paired logical boundaries exactly match"),Before.NodeGuids==After.NodeGuids&&Before.WallGuids==After.WallGuids&&Before.Polygon==After.Polygon&&Before.Area==After.Area);}
  FEHBWallNodeModel Model;TestTrue(TEXT("Cold split model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold split room floor slab and contact references valid"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Cold split dependency cache restored"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members));for(const auto& M:Members){TestEqual(TEXT("Each loaded room has one floor"),M.Floors.Num(),1);TestEqual(TEXT("Each loaded room has one slab"),M.Slabs.Num(),1);}
  B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Cold split full derived rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Loaded rebuild preserves saved split state"),LiveNodeAuthoritySnapshot(B),State);
 }
 for(const auto& Value:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Value->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));FGuid WallId;FGuid::Parse(E->GetStringField(TEXT("nextWall")),WallId);auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(WallId));if(!W)return false;const auto Receipt=LiveNodeReceipt(B);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto R=UEHBBuildingToolset::CommitPlainWallSplit(B,WallId,FVector::Dist2D(W->LocalStart,W->LocalEnd)*.5,B->RelationshipGraphRevision,W->LocalStart,W->LocalEnd,W->Height,W->Thickness);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);
  for(const auto& Pair:ExpectedStates)if(Pair.Key!=B)TestEqual(TEXT("Continuing a split does not alter another loaded building"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
  TestTrue(TEXT("Undo continued cold split"),GEditor->UndoTransaction());TestEqual(TEXT("Continued cold split exact undo"),LiveNodeAuthoritySnapshot(B),ExpectedStates.FindChecked(B));TestEqual(TEXT("Cold split receipt undo"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo continued cold split"),GEditor->RedoTransaction());TestEqual(TEXT("Continued cold split exact redo"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold split receipt redo"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
 }
 // Continue a wall-anchored open branch on the exact saved map. No replacement
 // fixture is written; its runtime room identities must agree with the planner.
 for(const auto& Value:Data->GetArrayField(TEXT("buildings")))
 {
  const auto E=Value->AsObject();auto* B=Buildings.FindRef(E->GetStringField(TEXT("guid")));FGuid WallId;FGuid::Parse(E->GetStringField(TEXT("nextWall")),WallId);auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(WallId));if(!W)return false;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  FEHBWallCreationEndpoint Anchor,Free;Anchor.Wall=W;Anchor.WallDistance=FVector::Dist2D(W->LocalStart,W->LocalEnd)*.5;const auto Preview=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,WallId,Anchor.WallDistance);if(!Preview.bSucceeded)return false;Anchor.LocalLocation=Preview.LocalPillarPosition;Anchor.WorldLocation=B->GetActorTransform().TransformPosition(Anchor.LocalLocation);Free.LocalLocation=Anchor.LocalLocation+FVector(-180,0,0);Free.WorldLocation=B->GetActorTransform().TransformPosition(Free.LocalLocation);
  const auto Receipt=LiveNodeReceipt(B);const auto R=EHBWallCreationCommand::CommitAnchoredPath(B,{Anchor,Free},false,FEHBWallCreationOptions());if(!TestTrue(*FString::Printf(TEXT("Cold optional branch: %s"),*R.Status.ToString()),R.bSucceeded))return false;
  FEHBWallNodeModel Model;TestTrue(TEXT("Cold branch source model valid"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold branch floor/slab/contact dependencies validate"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);TArray<FEHBNodeRoomBoundary> Rooms;FName Reason;TestTrue(TEXT("Cold branch room draft valid"),FEHBWallNodeRooms::Build(B->BuildingGuid,Model,Rooms,Reason));TestEqual(TEXT("Cold open branch retains two rooms"),Rooms.Num(),2);
  for(const auto& Room:Rooms){FEHBNodeRoomBoundary Actual;TestTrue(TEXT("Loaded runtime resolves planner room identity"),B->TryGetRoomBoundary(Room.RoomGuid,Room.FloorIndex,Actual));TestTrue(TEXT("Loaded branch room edge cycle matches"),Actual.NodeGuids==Room.NodeGuids&&Actual.WallGuids==Room.WallGuids);}
  for(const auto& Pair:ExpectedStates)if(Pair.Key!=B)TestEqual(TEXT("Cold branch isolates other saved building"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo continued cold branch"),GEditor->UndoTransaction());TestEqual(TEXT("Cold branch exact undo"),LiveNodeAuthoritySnapshot(B),ExpectedStates.FindChecked(B));TestEqual(TEXT("Cold branch receipt undo"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo continued cold branch"),GEditor->RedoTransaction());TestEqual(TEXT("Cold branch exact redo"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold branch receipt redo"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
 }
 GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeCopyWriteTest,"EHBValidation.Persistence.WriteOptionalNodeCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeCopyWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("OptionalNodeCopy")))return false;if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite copied node evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.OptionalNodeCopy.v1"));TArray<TSharedPtr<FJsonValue>> Pairs;
 for(bool AllUnbound:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;auto* B=Fixture.Building;B->SetActorLocationAndRotation(FVector(0,AllUnbound?2200:0,0),FRotator(0,37,0));if(!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  const auto Bindings=B->WallNodeOwnership.Bindings;const FGuid Id=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  for(const auto& Binding:Bindings)if(AllUnbound||Binding.NodeGuid==Id){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});const auto R=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  const auto Before=LiveNodeAuthoritySnapshot(B);const auto Copied=EHBBuildingCopy::Execute(B,FVector(2100,400,0));if(!TestTrue(*Copied.Status.ToString(),Copied.bSucceeded))return false;TestEqual(TEXT("Saving copy does not alter source building"),LiveNodeAuthoritySnapshot(B),Before);Fixture.Actors.Append(Copied.Actors);
  for(auto* Building:{static_cast<AEHBBuildingActorBase*>(B),Copied.Building}){Building->ClearFlags(RF_Transient);for(auto* E:Building->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);}
  auto Pair=MakeShared<FJsonObject>();Pair->SetObjectField(TEXT("source"),NodeCopyPersistenceEvidence(B));Pair->SetObjectField(TEXT("copy"),NodeCopyPersistenceEvidence(Copied.Building));Pair->SetStringField(TEXT("editNode"),Copied.IdentityDraft.NodeGuids.FindChecked(Id).ToString());
  TArray<TSharedPtr<FJsonValue>> Nodes;for(const auto& Mapping:Copied.IdentityDraft.NodeGuids){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("source"),Mapping.Key.ToString());Item->SetStringField(TEXT("copy"),Mapping.Value.ToString());Nodes.Add(MakeShared<FJsonValueObject>(Item));}Pair->SetArrayField(TEXT("nodeMap"),Nodes);Pairs.Add(MakeShared<FJsonValueObject>(Pair));Fixture.Actors.Reset();
 }
 Data->SetArrayField(TEXT("pairs"),Pairs);if(!TestTrue(TEXT("Save four actual source/copy buildings"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save immutable copy identity and geometry evidence"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOptionalNodeCopyReadTest,"EHBValidation.Persistence.ReadOptionalNodeCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOptionalNodeCopyReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("OptionalNodeCopy"))||!FFileHelper::LoadFileToString(Json,*Path))return false;auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.OptionalNodeCopy.v1"))return false;
 TMap<FString,AEHBBuildingActorBase*> Buildings;for(TActorIterator<AEHB_Building> It(World);It;++It)Buildings.Add(It->BuildingGuid.ToString(),*It);TestEqual(TEXT("Cold load contains exactly the original and two copied buildings"),Buildings.Num(),4);TestEqual(TEXT("Both mixed and zero physical binding scenarios saved"),Data->GetArrayField(TEXT("pairs")).Num(),2);
 TMap<AEHBBuildingActorBase*,FString> ExpectedStates;TArray<AEHBBuildingActorBase*> Copies;TArray<FGuid> EditNodes;
 for(const auto& Value:Data->GetArrayField(TEXT("pairs")))
 {
  auto Pair=Value->AsObject();for(const TCHAR* Key:{TEXT("source"),TEXT("copy")})
  {
   auto Saved=Pair->GetObjectField(Key);auto* B=Buildings.FindRef(Saved->GetStringField(TEXT("guid")));if(!TestNotNull(TEXT("Loaded actor resolved by saved building identity"),B))return false;const auto Expected=Saved->GetStringField(TEXT("state"));ExpectedStates.Add(B,Expected);
   TestEqual(TEXT("Cold load instance registry has no serialized null slots or dead fills"),B->GetInstanceComponents().Num(),B->WallNodeAuthority.Nodes.Num()-B->WallNodeOwnership.Bindings.Num());for(const auto* C:B->GetInstanceComponents())TestTrue(TEXT("Cold instance registry contains live components only"),IsValid(C));
   TestEqual(TEXT("Cold copy model and all geometry match writer exactly"),LiveNodeAuthoritySnapshot(B),Expected);TestEqual(TEXT("Cold copy receipt matches writer exactly"),LiveNodeReceipt(B),Saved->GetStringField(TEXT("receipt")));TestEqual(TEXT("Cold optional authority version retained"),B->WallNodeAuthority.Version,2);TestEqual(TEXT("Cold physical binding count retained"),B->WallNodeOwnership.Bindings.Num(),Saved->GetIntegerField(TEXT("physicalCount")));TestEqual(TEXT("Cold element count retained"),B->QueryElements(FEHBElementQuery()).Num(),Saved->GetIntegerField(TEXT("elementCount")));
   for(const auto& R:Saved->GetArrayField(TEXT("rooms"))){FEHBNodeRoomBoundary Before,After;if(!FJsonObjectConverter::JsonObjectToUStruct(R->AsObject().ToSharedRef(),&Before))return false;TestTrue(TEXT("Cold room boundary resolves by copied room ID"),B->TryGetRoomBoundary(Before.RoomGuid,1,After));TestTrue(TEXT("Cold room keeps exact paired identities and boundary order"),Before.NodeGuids==After.NodeGuids&&Before.WallGuids==After.WallGuids&&Before.Polygon==After.Polygon&&Before.Area==After.Area);}
   FEHBWallNodeModel Model;TestTrue(TEXT("Cold copied optional model validates"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);TestTrue(TEXT("Cold copied room and finish references validate"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Cold copied room dependency cache resolves"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members));for(const auto& M:Members){TestEqual(TEXT("Loaded room owns one floor"),M.Floors.Num(),1);TestEqual(TEXT("Loaded room owns one slab"),M.Slabs.Num(),1);}
   B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Cold copied junction geometry rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Explicit rebuild preserves entire saved copy state"),LiveNodeAuthoritySnapshot(B),Expected);
  }
  auto* Source=Buildings.FindRef(Pair->GetObjectField(TEXT("source"))->GetStringField(TEXT("guid")));auto* Copy=Buildings.FindRef(Pair->GetObjectField(TEXT("copy"))->GetStringField(TEXT("guid")));
  for(const auto& M:Pair->GetArrayField(TEXT("nodeMap"))){FGuid A,Z;FGuid::Parse(M->AsObject()->GetStringField(TEXT("source")),A);FGuid::Parse(M->AsObject()->GetStringField(TEXT("copy")),Z);TestTrue(TEXT("Persisted map refers to two independent actual logical nodes"),A!=Z&&Source->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==A;})&&Copy->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==Z;}));auto* SourceFill=Source->FindWallNodeJunction(A);if(SourceFill){auto* CopyFill=Copy->FindWallNodeJunction(Z);TestTrue(TEXT("Cold regenerated fill never shares the source component"),CopyFill&&CopyFill!=SourceFill&&CopyFill->GetOwner()==Copy);}}
  FGuid EditNode;FGuid::Parse(Pair->GetStringField(TEXT("editNode")),EditNode);Copies.Add(Copy);EditNodes.Add(EditNode);
 }
 for(int32 I=0;I<Copies.Num();++I)
 {
  auto* B=Copies[I];const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==EditNodes[I];});if(!N)return false;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Start=N->LocalTransform.GetLocation();const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);
  const auto Moved=UEHBBuildingToolset::MoveUnboundWallNode(B,N->NodeGuid,N->GeometryRevision,Start,Start+FVector(-20,-10,0),false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);for(const auto& Pair:ExpectedStates)if(Pair.Key!=B)TestEqual(TEXT("Editing one loaded copy preserves every other building"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
  TestTrue(TEXT("Undo edit of cold copied building"),GEditor->UndoTransaction());TestEqual(TEXT("Cold copied edit exact undo"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Cold copied receipt undo"),LiveNodeReceipt(B),Receipt);TestTrue(TEXT("Redo edit of cold copied building"),GEditor->RedoTransaction());TestEqual(TEXT("Cold copied edit exact redo"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Cold copied receipt redo"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  const auto Again=EHBBuildingCopy::Execute(B,FVector(0,4500,0));if(!TestTrue(*FString::Printf(TEXT("Cold copied building copy: %s / %s"),*Again.Status.ToString(),*Again.FailureReason.ToString()),Again.bSucceeded))return false;TestTrue(TEXT("Undo copy made from cold copied building"),GEditor->UndoTransaction(false));for(const auto& Pair:ExpectedStates)TestEqual(TEXT("Further copying leaves all saved buildings intact"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Commands=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetGlobalLevelEditorActions();TestTrue(TEXT("Loaded building uses actual duplicate command"),Commands->TryExecuteAction(FGenericCommands::Get().Duplicate.ToSharedRef()));auto* CommandCopy=GEditor->GetSelectedActors()->GetTop<AEHBBuildingActorBase>();if(!TestTrue(TEXT("Loaded copy command selects independent building"),CommandCopy&&CommandCopy!=B))return false;const auto CommandState=LiveNodeAuthoritySnapshot(CommandCopy);TestTrue(TEXT("Undo cold command copy"),GEditor->UndoTransaction());TestTrue(TEXT("Redo cold command copy"),GEditor->RedoTransaction());TestEqual(TEXT("Cold command redo exact state"),LiveNodeAuthoritySnapshot(CommandCopy),CommandState);GEditor->UndoTransaction(false);for(const auto& Pair:ExpectedStates)TestEqual(TEXT("Cold duplicate command leaves all saved buildings intact"),LiveNodeAuthoritySnapshot(Pair.Key),Pair.Value);
 }
 GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBUnboundNodesWriteTest,"EHBValidation.Persistence.WriteUnboundNodes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBUnboundNodesWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("UnboundNodes")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite independent node evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;
 auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));const bool Finishes=FParse::Param(FCommandLine::Get(),TEXT("EHBUnboundFinishes"));if(Finishes&&!AddCopyRoomOutlines(Fixture,true))return false;if(!B->MigrateWallNodeOwnership(true).bSucceeded||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
 const auto Room=B->GetClosedLoopsByFloor(1)[0].LoopGuid;const auto Bindings=B->WallNodeOwnership.Bindings;const auto Id=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
 const bool Mixed=FParse::Param(FCommandLine::Get(),TEXT("EHBMixedNodes"));const auto OtherId=B->FindNodeForPhysicalPillar(Fixture.Pillars[1]->ElementGuid);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);TArray<TSharedPtr<FJsonValue>> Removed;
 for(const auto& Binding:Bindings)
 {
  if(Mixed&&Binding.NodeGuid!=Id)continue;
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});
  const auto Result=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*Result.Message,Result.bSucceeded))return false;
  Removed.Add(MakeShared<FJsonValueString>(Binding.PhysicalPillarGuid.ToString()));
 }
 const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});const auto Start=N->LocalTransform.GetLocation();
 FEHBToolsetOperationResult Moved;
 if(Mixed){TArray<FEHBVersionedNodeMoveRequest> Moves;for(FGuid NodeId:{Id,OtherId}){const auto* V=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& A){return A.NodeGuid==NodeId;});auto& M=Moves.AddDefaulted_GetRef();M.NodeGuid=NodeId;M.ExpectedRevision=V->GeometryRevision;M.ExpectedPosition=V->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(0,-35,0);}Moved=UEHBBuildingToolset::CommitWallNodeMoves(B,Moves,false);}
 else Moved=UEHBBuildingToolset::MoveUnboundWallNode(B,Id,N->GeometryRevision,Start,Start+FVector(-45,-20,0),false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
 TestEqual(TEXT("Saving real walls finishes and only authored physical columns"),B->QueryElements(FEHBElementQuery()).Num(),(Finishes?11:4)+(Mixed?Bindings.Num()-1:0));
 B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);
 if(!TestTrue(TEXT("Save building with no physical node actors"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.UnboundNodes.v1"));Data->SetStringField(TEXT("building"),B->BuildingGuid.ToString());Data->SetStringField(TEXT("node"),Id.ToString());Data->SetStringField(TEXT("room"),Room.ToString());Data->SetArrayField(TEXT("removed"),Removed);
 Data->SetBoolField(TEXT("finishes"),Finishes);Data->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(B));Data->SetStringField(TEXT("receipt"),LiveNodeReceipt(B));
 Data->SetBoolField(TEXT("mixed"),Mixed);Data->SetStringField(TEXT("otherNode"),OtherId.ToString());
 TArray<TSharedPtr<FJsonValue>> Boundaries;for(const auto& R:B->GetClosedLoopsByFloor(1)){FEHBNodeRoomBoundary Boundary;if(!B->TryGetRoomBoundary(R.LoopGuid,R.FloorIndex,Boundary))return false;Boundaries.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Boundary)));}Data->SetArrayField(TEXT("boundaries"),Boundaries);
 FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save immutable node evidence"),FFileHelper::SaveStringToFile(Json,*Path));Fixture.Actors.Reset();GEditor->SelectNone(false,true,false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBUnboundNodesReadTest,"EHBValidation.Persistence.ReadUnboundNodes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBUnboundNodesReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("UnboundNodes"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 auto* World=GEditor->GetEditorWorldContext().World();if(!World||World->GetOutermost()->GetName()!=Map)return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.UnboundNodes.v1"))return false;
 const bool Finishes=Data->HasField(TEXT("finishes"))&&Data->GetBoolField(TEXT("finishes"));
 const bool Mixed=Data->HasField(TEXT("mixed"))&&Data->GetBoolField(TEXT("mixed"));
 AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHB_Building> It(World);It;++It){if(B)return false;B=*It;}if(!TestNotNull(TEXT("Cold loaded independent node building"),B))return false;
 FGuid Id,Room;FGuid::Parse(Data->GetStringField(TEXT("node")),Id);FGuid::Parse(Data->GetStringField(TEXT("room")),Room);
 TestEqual(TEXT("Building identity preserved"),B->BuildingGuid.ToString(),Data->GetStringField(TEXT("building")));TestEqual(TEXT("Optional binding mode restored"),B->WallNodeAuthority.Version,2);
 const int32 BindingCount=Mixed?(Finishes?5:3):0;TestEqual(TEXT("Cold load preserves exact optional physical binding count"),B->WallNodeOwnership.Bindings.Num(),BindingCount);TestEqual(TEXT("Walls and optional floor slabs restored"),B->QueryElements(FEHBElementQuery()).Num(),(Finishes?11:4)+BindingCount);
 for(const auto& Removed:Data->GetArrayField(TEXT("removed"))){FGuid Physical;FGuid::Parse(Removed->AsString(),Physical);TestNull(TEXT("Deleted physical identity never resurrects on load"),B->FindElementActorByGuid(Physical));}
 for(const auto& N:B->WallNodeAuthority.Nodes){auto* C=B->FindWallNodeJunction(N.NodeGuid);const FGuid Physical=B->FindPhysicalPillarForNode(N.NodeGuid);if(Physical.IsValid()){TestNotNull(TEXT("Bound node restores original physical actor"),B->FindElementActorByGuid(Physical));TestNull(TEXT("Bound node does not also get a derived junction"),C);continue;}if(!TestNotNull(TEXT("Cold load regenerates derived junction"),C))return false;TestTrue(TEXT("Generated junction registered transient and populated"),C->IsRegistered()&&C->HasAnyFlags(RF_Transient)&&C->GetNumSections()>0);}
 const auto Expected=Data->GetStringField(TEXT("state")),Receipt=Data->GetStringField(TEXT("receipt"));TestEqual(TEXT("Independent load preserves exact node model IDs revision geometry and component count"),LiveNodeAuthoritySnapshot(B),Expected);TestEqual(TEXT("Independent load preserves complete edit receipt"),LiveNodeReceipt(B),Receipt);
 FEHBWallNodeModel Model;TestTrue(TEXT("Unbound model valid after cold load"),UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded);
 if(Data->HasField(TEXT("boundaries")))for(const auto& Value:Data->GetArrayField(TEXT("boundaries"))){FEHBNodeRoomBoundary Saved,Actual;if(!FJsonObjectConverter::JsonObjectToUStruct(Value->AsObject().ToSharedRef(),&Saved))return false;TestTrue(TEXT("Cold load restores public logical room boundary"),B->TryGetRoomBoundary(Saved.RoomGuid,Saved.FloorIndex,Actual));TestTrue(TEXT("Independent logical cycles and exact positions match"),Saved.NodeGuids==Actual.NodeGuids&&Saved.WallGuids==Actual.WallGuids&&Saved.Polygon==Actual.Polygon&&Saved.Area==Actual.Area);}
 if(Finishes){const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery()),true);TestTrue(*Policy.Status.ToString(),Policy.bSucceeded);TArray<FEHBRoomDependencyMembers> Members;TestTrue(TEXT("Loaded logical rooms restore finish dependency cache"),B->QueryRoomDependencies(B->GetClosedLoopsByFloor(1),Members));for(const auto& M:Members){TestEqual(TEXT("Loaded room has its floor"),M.Floors.Num(),1);TestEqual(TEXT("Loaded room has its slab"),M.Slabs.Num(),1);}}
 const auto Rooms=B->FindClosedLoopsContainingWorldLocation(B->GetActorTransform().TransformPosition(FVector(300,250,0)),1);TestTrue(TEXT("Room query restored without any pillars"),Rooms.Num()==1&&Rooms[0].LoopGuid==Room);
 B->RebuildElementAndRelationshipIndexes();TestTrue(TEXT("Cold load explicit node geometry rebuild"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Rebuild changes no saved node or geometry"),LiveNodeAuthoritySnapshot(B),Expected);
 const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});if(!TestNotNull(TEXT("Resolve persisted node for editing"),N))return false;const auto Start=N->LocalTransform.GetLocation();GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 FEHBToolsetOperationResult Moved;
 if(Mixed){FGuid Other;FGuid::Parse(Data->GetStringField(TEXT("otherNode")),Other);TArray<FEHBVersionedNodeMoveRequest> Moves;for(FGuid NodeId:{Id,Other}){const auto* V=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& A){return A.NodeGuid==NodeId;});if(!V)return false;auto& M=Moves.AddDefaulted_GetRef();M.NodeGuid=NodeId;M.ExpectedRevision=V->GeometryRevision;M.ExpectedPosition=V->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(0,-20,0);}Moved=UEHBBuildingToolset::CommitWallNodeMoves(B,Moves,false);}
 else Moved=UEHBBuildingToolset::MoveUnboundWallNode(B,Id,N->GeometryRevision,Start,Start+FVector(-20,-10,0),false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
 const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Continued edit moves actual node"),After!=Expected);TestTrue(TEXT("Undo cold loaded node move"),GEditor->UndoTransaction());TestEqual(TEXT("Loaded node undo geometry"),LiveNodeAuthoritySnapshot(B),Expected);TestEqual(TEXT("Loaded node undo receipt"),LiveNodeReceipt(B),Receipt);
 TestTrue(TEXT("Redo cold loaded node move"),GEditor->RedoTransaction());TestEqual(TEXT("Loaded node redo geometry"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Loaded node redo receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSharedAuthorityGeometryTest,"EHB.Topology.SharedAuthorityGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSharedAuthorityGeometryTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(bool Finishes:{false,true})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;
  B->SetActorRotation(FRotator(0,37,0));if(Finishes&&!AddCopyRoomOutlines(Fixture,true))return false;
  if(!B->MigrateWallNodeOwnership(true).bSucceeded||!B->MigrateWallNodeAuthority(true).bSucceeded)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))
  {
   const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});
   const auto Removed=UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false);if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;
  }
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);TMap<FGuid,UEHBWallJunctionComponent*> Existing;
  for(const auto& N:B->WallNodeAuthority.Nodes)Existing.Add(N.NodeGuid,B->FindWallNodeJunction(N.NodeGuid));
  TestTrue(TEXT("Whole authority rebuild succeeds"),B->RebuildWallNodeAuthorityGeometry());
  TestEqual(TEXT("Repeated rebuild keeps full geometry and model"),LiveNodeAuthoritySnapshot(B),Before);
  for(const auto& Pair:Existing)TestTrue(TEXT("Repeated rebuild reuses live components"),Pair.Value==B->FindWallNodeJunction(Pair.Key));
  TestEqual(TEXT("Rebuild cannot invent a command receipt"),LiveNodeReceipt(B),Receipt);
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;
  const FGuid First=Model.Nodes[0].NodeGuid;TArray<UEHBWallJunctionComponent*> Extra;
  for(bool Orphan:{false,true})
  {
   auto* C=NewObject<UEHBWallJunctionComponent>(B,NAME_None,RF_Transient);C->ComponentTags.Add(TEXT("EHB.NodeAuthorityDerived"));C->SetupAttachment(B->GetRootComponent());B->AddInstanceComponent(C);C->RegisterComponent();
   if(!TestTrue(TEXT("Create duplicate cleanup fixture"),C->RebuildFromNodeModel(Model,First)))return false;
   if(Orphan)C->NodeGuid=FGuid::NewGuid();Extra.Add(C);
  }
  const auto Instances=B->GetInstanceComponents();const auto Authority=B->WallNodeAuthority;const auto Copy=B->WallNodeAuthority.Nodes[0];B->WallNodeAuthority.Nodes.Add(Copy);
  TestFalse(TEXT("Invalid authority refuses before registry reconciliation"),B->RebuildWallNodeAuthorityGeometry());
  TestTrue(TEXT("Rejected input retains complete instance registry"),Instances==B->GetInstanceComponents());
  for(auto* C:Extra)TestTrue(TEXT("Rejected input cannot partially destroy owned components"),IsValid(C)&&C->IsRegistered()&&C->GetProcMeshSection(0));
  for(const auto& Pair:Existing)TestTrue(TEXT("Rejected input preserves derived index"),Pair.Value==B->FindWallNodeJunction(Pair.Key));
  B->WallNodeAuthority=Authority;
  TestTrue(TEXT("Valid input reconciles duplicate and orphan components"),B->RebuildWallNodeAuthorityGeometry());
  TInlineComponentArray<UEHBWallJunctionComponent*> Actual(B);TestEqual(TEXT("Exactly one live junction per connected unbound node"),Actual.Num(),Model.Nodes.Num());
  TestEqual(TEXT("Instance registry contains only actual junctions"),B->GetInstanceComponents().Num(),Model.Nodes.Num());
  TSet<FGuid> Ids;for(auto* C:Actual){TestFalse(TEXT("No duplicate node component survives"),Ids.Contains(C->NodeGuid));Ids.Add(C->NodeGuid);TestTrue(TEXT("Every live junction indexed"),B->FindWallNodeJunction(C->NodeGuid)==C);}
  TestEqual(TEXT("Reconciliation restores exact original geometry and authority"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Reconciliation preserves receipt"),LiveNodeReceipt(B),Receipt);
  TestTrue(TEXT("Finishes remain coherent after shared regeneration"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);
 }
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBScopedAuthorityMoveTest,"EHB.Topology.ScopedAuthorityMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBScopedAuthorityMoveTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(int32 BindingMode:{0,1,2})for(bool Batch:{false,true})for(double Yaw:{17.0,45.0})
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,Yaw,0));
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  if(!TestTrue(TEXT("Enable move fixture"),EHBNodeEditingActivation::Execute(B,false).bSucceeded))return false;
  const FGuid First=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid),Second=B->FindNodeForPhysicalPillar(Fixture.Pillars[3]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==First))
  {
   const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});
   if(!TestTrue(TEXT("Unbind fixture column"),UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded))return false;
  }
  TestTrue(TEXT("Known full output before local edit"),B->RebuildWallNodeAuthorityGeometry());
  FEHBWallNodeModel Source;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source).bSucceeded)return false;
  TArray<FEHBNodeMoveRequest> Requests;TMap<FGuid,int32> Revisions;
  for(FGuid Id:TArray<FGuid>{First,Second})if(Id==First||Batch)
  {
   const auto* N=Source.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});auto& R=Requests.AddDefaulted_GetRef();R.NodeGuid=Id;R.ExpectedPosition=N->LocalTransform.GetLocation();R.TargetPosition=R.ExpectedPosition+FVector(-40,-20,0);Revisions.Add(Id,N->GeometryRevision);
  }
  const auto Draft=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Source,Requests);if(!TestTrue(*Draft.Status.ToString(),Draft.bSucceeded))return false;
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);
  TestFalse(TEXT("Cannot apply geometry before candidate poses"),B->RefreshWallNodeMoveGeometry(Source,Draft).bSucceeded);
  auto Bad=Draft;const auto Duplicate=Bad.UpdatePlan.WallGuids[0];Bad.UpdatePlan.WallGuids.Add(Duplicate);
  TestEqual(TEXT("Modified dispatch plan refused"),B->RefreshWallNodeMoveGeometry(Source,Bad).Status,FName(TEXT("ChangedMoveDraft")));
  TestEqual(TEXT("Invalid drafts do not touch actual geometry"),LiveNodeAuthoritySnapshot(B),Before);
  TMap<FGuid,uint64> Serials;TMap<FGuid,int32> ElementRevisions;TMap<FGuid,FString> OutsideJunctions;
  for(const auto& W:Source.Walls){Serials.Add(W.WallGuid,CastChecked<AEHB_Wall>(B->FindElementActorByGuid(W.WallGuid))->GetNodeDefinitionRefreshSerial());ElementRevisions.Add(W.WallGuid,B->GetElementGeometryRevision(W.WallGuid));}
  for(const auto& N:Source.Nodes)if(!Draft.UpdatePlan.JunctionNodeGuids.Contains(N.NodeGuid))if(auto* C=B->FindWallNodeJunction(N.NodeGuid))
  {FString Text;FJsonObjectConverter::UStructToJsonObjectString(*C->GetProcMeshSection(0),Text);OutsideJunctions.Add(N.NodeGuid,Text);}
  if(!TestTrue(TEXT("Actual unified move commits"),EHBNodeAuthorityEditing::ExecuteMoves(B,Requests,Revisions,false).bSucceeded))return false;
  const auto Work=EHBNodeAuthorityEditing::LastGeometryRefresh;
  TSet<FGuid> SideNodes;for(const auto& W:Source.Walls)if(Draft.UpdatePlan.WallGuids.Contains(W.WallGuid)){SideNodes.Add(W.StartNodeGuid);SideNodes.Add(W.EndNodeGuid);}
  int32 UnboundCount=0;for(FGuid Id:Draft.UpdatePlan.JunctionNodeGuids)if(!Source.PillarBindings.ContainsByPredicate([&](const auto& V){return V.NodeGuid==Id;}))++UnboundCount;
  TestTrue(TEXT("Actual move uses scoped calculation"),Work.bSucceeded&&Work.bUsedScopedUpdate);
  TestEqual(TEXT("Runtime solver computes only planned walls"),Work.WallSidesSolved,Draft.UpdatePlan.WallGuids.Num());
  TestEqual(TEXT("Runtime solver builds only selected side endpoint footprints"),Work.NodeFootprintsSolved,SideNodes.Num());
  TestEqual(TEXT("Runtime solver builds only selected unbound prisms"),Work.UnboundMeshesBuilt,UnboundCount);

  int32 Unaffected=0;
  for(const auto& W:Source.Walls)
  {
   const bool Affected=Draft.UpdatePlan.WallGuids.Contains(W.WallGuid);auto* Wall=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(W.WallGuid));
   TestEqual(TEXT("Only affected walls consume one ready geometry result"),Wall->GetNodeDefinitionRefreshSerial(),Serials.FindChecked(W.WallGuid)+(Affected?1:0));
   if(!Affected){++Unaffected;TestEqual(TEXT("Outside wall geometry revision not reset or incremented"),B->GetElementGeometryRevision(W.WallGuid),ElementRevisions.FindChecked(W.WallGuid));}
  }
  TestTrue(TEXT("Fixture includes walls outside dispatch boundary"),Unaffected>0);
  for(const auto& Pair:OutsideJunctions){FString Text;FJsonObjectConverter::UStructToJsonObjectString(*B->FindWallNodeJunction(Pair.Key)->GetProcMeshSection(0),Text);TestEqual(TEXT("Outside junction section including generation revision unchanged"),Text,Pair.Value);}
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);
  TestTrue(TEXT("Full rebuild after local result"),B->RebuildWallNodeAuthorityGeometry());TestEqual(TEXT("Local result exactly equals full geometry, finishes and node metadata"),LiveNodeAuthoritySnapshot(B),After);
  TestTrue(TEXT("Undo local composite"),GEditor->UndoTransaction());TestEqual(TEXT("Local undo complete state"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Local undo receipt"),LiveNodeReceipt(B),Receipt);
  TestTrue(TEXT("Redo local composite"),GEditor->RedoTransaction());TestEqual(TEXT("Local redo complete state"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Local redo receipt"),LiveNodeReceipt(B),AfterReceipt);GEditor->UndoTransaction(false);
  if(BindingMode==2)
  {
	if(!TestTrue(TEXT("Warm exact baseline before deliberate corruption"),B->RebuildWallNodeAuthorityGeometry()))return false;
   const auto* Outside=Source.Nodes.FindByPredicate([&](const auto& N){return !Draft.UpdatePlan.JunctionNodeGuids.Contains(N.NodeGuid);});if(!Outside)return false;
   auto* C=B->FindWallNodeJunction(Outside->NodeGuid);if(!C)return false;
   // Distinct recovery cases: stale metadata, moved component, missing component,
   // and an extra duplicate. The real command must repair all, not preserve damage.
   if(!Batch&&Yaw==17.0)++C->SourceGeometryRevision;
   else if(!Batch)C->SetRelativeLocation(C->GetRelativeLocation()+FVector(0,0,11));
   else if(Yaw==17.0)C->DestroyComponent();
   else
   {
    auto* Extra=NewObject<UEHBWallJunctionComponent>(B,NAME_None,RF_Transient);Extra->ComponentTags.Add(TEXT("EHB.NodeAuthorityDerived"));Extra->SetupAttachment(B->GetRootComponent());B->AddInstanceComponent(Extra);Extra->RegisterComponent();if(!Extra->RebuildFromNodeModel(Source,Outside->NodeGuid))return false;
   }
   Serials.Reset();for(const auto& W:Source.Walls)Serials.Add(W.WallGuid,CastChecked<AEHB_Wall>(B->FindElementActorByGuid(W.WallGuid))->GetNodeDefinitionRefreshSerial());
   if(!TestTrue(TEXT("Damaged derived state uses full fallback"),EHBNodeAuthorityEditing::ExecuteMoves(B,Requests,Revisions,false).bSucceeded))return false;
   const auto Fallback=EHBNodeAuthorityEditing::LastGeometryRefresh;TestFalse(TEXT("Damaged state never uses partial geometry"),Fallback.bUsedScopedUpdate);TestEqual(TEXT("Recovery calculates every wall"),Fallback.WallSidesSolved,Source.Walls.Num());TestEqual(TEXT("Recovery calculates every unbound mesh"),Fallback.UnboundMeshesBuilt,Source.Nodes.Num());

   for(const auto& W:Source.Walls)TestEqual(TEXT("Fallback regenerates every wall once"),CastChecked<AEHB_Wall>(B->FindElementActorByGuid(W.WallGuid))->GetNodeDefinitionRefreshSerial(),Serials.FindChecked(W.WallGuid)+1);
   TestEqual(TEXT("Fallback repairs exact complete output"),LiveNodeAuthoritySnapshot(B),After);GEditor->UndoTransaction(false);
  }
  if(BindingMode==0)
  {
   if(!B->RebuildWallNodeAuthorityGeometry())return false;
   const auto* Outside=Source.Walls.FindByPredicate([&](const auto& W){return !Draft.UpdatePlan.WallGuids.Contains(W.WallGuid);});if(!Outside)return false;
   auto* Wall=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Outside->WallGuid));
   if(Yaw==17.0)Wall->GetRootComponent()->SetRelativeLocation(Wall->GetRootComponent()->GetRelativeLocation()+FVector(0,0,12));
   else Wall->LeftWallMeshComponent->ClearAllMeshSections();
   const auto Corrupt=LiveNodeAuthoritySnapshot(B),CorruptReceipt=LiveNodeReceipt(B);const int32 Queue=GEditor->Trans->GetQueueLength();
   const auto Recovery=EHBNodeAuthorityEditing::ExecuteMoves(B,Requests,Revisions,false);
   if(Yaw==17.0)
   {
    // Moving an actual host changes floor/slab source geometry. Dependency
    // preflight must reject before any derived-output recovery can discard it.
    TestFalse(TEXT("Conflicting physical wall pose is not silently repaired by an unrelated move"),Recovery.bSucceeded);
    AddInfo(FString::Printf(TEXT("Outside host pose refusal: %s"),*Recovery.Message));
    TestEqual(TEXT("Host pose conflict is a dependency rejection"),Recovery.Message,FString(TEXT("StaleFinishContacts")));
    TestEqual(TEXT("Rejected host conflict preserves complete input"),LiveNodeAuthoritySnapshot(B),Corrupt);TestEqual(TEXT("Rejected host conflict preserves receipt"),LiveNodeReceipt(B),CorruptReceipt);TestEqual(TEXT("Rejected host conflict has no undo entry"),GEditor->Trans->GetQueueLength(),Queue);
    continue;
   }
   if(!TestTrue(*FString::Printf(TEXT("Outside wall mesh recovery: %s"),*Recovery.Message),Recovery.bSucceeded))return false;
   TestFalse(TEXT("Outside wall frame/mesh mismatch forces full computation"),EHBNodeAuthorityEditing::LastGeometryRefresh.bUsedScopedUpdate);
   TestEqual(TEXT("Outside wall recovery reproduces exact full output"),LiveNodeAuthoritySnapshot(B),After);GEditor->UndoTransaction(false);
  }


 }
 return true;
}

#include "EHBWallRemovalCases.inl"
#include "EHBRetainedFinishCases.inl"
#include "EHBRetainedFinishPersistence.inl"
#include "EHBSurfaceRoomCoverageLive.inl"
#include "EHBFinishRegionEditing.inl"
#include "EHBFinishRegionPersistence.inl"
#include "EHBStyledMergeCases.inl"
#include "EHBStyledMergePersistence.inl"
#include "EHBLogicalSurfaceLive.inl"
#include "EHBDisplayPartitionPersistence.inl"
#include "EHBDisplayRemovalCases.inl"
#include "EHBDisplayFollowingCases.inl"
#include "EHBDisplayFollowingPersistence.inl"
#include "EHBDisplayTopologyCases.inl"
#include "EHBDisplayTopologyPersistence.inl"
#include "EHBCutSlabRegionsPersistence.inl"
#include "EHBSlabOutlineEditCases.inl"
#include "EHBSlabOutlineEditPersistence.inl"
#include "EHBCutPartitionPersistence.inl"
#include "EHBOpeningRegionEditCases.inl"
#include "EHBLogicalWallTopologyCases.inl"
#include "EHBWallOpeningSources.inl"
#include "EHBSurfaceOpeningCases.inl"
#include "EHBPreparedWallOpeningCases.inl"
#include "EHBWallOpeningCommandCases.inl"
#include "EHBOpeningUnionCases.inl"
#include "EHBWallEndCapCases.inl"
#include "EHBWallOpeningPersistence.inl"
#include "EHBOpeningRegionPersistence.inl"

#include "EHBRoomTrackerCases.inl"
#include "EHBRoomExplorationSaveCases.inl"
#include "EHBLocalBuildingViewCases.inl"
#endif
