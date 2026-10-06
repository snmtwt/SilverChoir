#include "Core/EHBRoomRuntimeSubsystem.h"
#include "Components/EHBRoomTrackerComponent.h"
#include "Components/SceneComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomTrackerTest,"EHB.Runtime.RoomTracker",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomTrackerTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();auto* Rooms=World->GetSubsystem<UEHBRoomRuntimeSubsystem>();if(!TestNotNull(TEXT("Room subsystem exists"),Rooms))return false;
 FStyledMergeFixture First,Second;if(!First.Create(World,2,true)||!Second.Create(World,0,true))return false;
 auto* B=First.Building();auto* Other=Second.Building();Other->SetActorLocation(Other->GetActorLocation()+FVector(5000,0,0));
 auto Center=[](AEHBBuildingActorBase* Building,const FEHBBuildingClosedLoop& Loop)
 {
  FEHBNodeRoomBoundary Boundary;Building->TryGetRoomBoundary(Loop.LoopGuid,Loop.FloorIndex,Boundary);FVector Sum=FVector::ZeroVector;
  for(FGuid Id:Boundary.NodeGuids){const auto* N=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& Node){return Node.NodeGuid==Id;});if(N)Sum+=N->LocalTransform.GetLocation();}
  return Building->GetActorTransform().TransformPosition(Sum/FMath::Max(1,Boundary.NodeGuids.Num())+FVector(0,0,50));
 };
 const auto Loops=B->GetClosedLoopsByFloor(1);const FVector Point=Center(B,Loops[0]),Destination=Center(Other,Other->GetClosedLoopsByFloor(1)[0]);
 const auto Initial=Rooms->QueryLocation(Point,{});if(!TestEqual(TEXT("Registry finds spawned building"),Initial.Status,EEHBRoomLocationStatus::Inside))return false;
 TestEqual(TEXT("Stable room identity"),Initial.Identity.RoomGuid,Loops[0].LoopGuid);TestEqual(TEXT("Floor identity"),Initial.FloorIndex,1);
 auto* Shared=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(First.Shared));const FVector Boundary=B->GetActorTransform().TransformPosition((Shared->LocalStart+Shared->LocalEnd)*.5+FVector(0,0,50));
 TestEqual(TEXT("Unseeded shared boundary is explicit ambiguity"),Rooms->QueryLocation(Boundary,{}).Status,EEHBRoomLocationStatus::Ambiguous);
 TestTrue(TEXT("Boundary keeps still valid previous room"),Rooms->QueryLocation(Boundary,Initial)==Initial);
 const auto SecondRoom=Rooms->QueryLocation(Destination,Initial);TestTrue(TEXT("Other building selected"),SecondRoom.Building.Get()==Other);
 const FVector Outside=Point+FVector(0,0,100000);TestEqual(TEXT("Vertical exclusion"),Rooms->QueryLocation(Outside,Initial).Status,EEHBRoomLocationStatus::Outside);
 FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;auto* Unit=World->SpawnActor<AActor>(Spawn);if(!Unit)return false;First.Fixture.Actors.Add(Unit);
 auto* Root=NewObject<USceneComponent>(Unit);Unit->SetRootComponent(Root);Unit->AddInstanceComponent(Root);Root->RegisterComponent();Unit->SetActorLocation(Point);
 auto* Tracker=NewObject<UEHBRoomTrackerComponent>(Unit);Unit->AddInstanceComponent(Tracker);Tracker->RegisterComponent();Tracker->ExplorationObserver=TEXT("EHB_Test_A");
 ON_SCOPE_EXIT{Rooms->ClearExploration(TEXT("EHB_Test_A"));Rooms->ClearExploration(TEXT("EHB_Test_B"));};
 int32 Events=0;Tracker->OnRoomChangedNative.AddLambda([&](const FEHBRoomLocation&,const FEHBRoomLocation&){++Events;});
 Tracker->RefreshRoom(0,true);TestEqual(TEXT("Initial room event"),Events,1);TestTrue(TEXT("Tracker immediate entry"),Tracker->CurrentRoom==Initial);
 TestTrue(TEXT("Visit records exploration"),Rooms->IsExplored(TEXT("EHB_Test_A"),Initial.Identity));TestFalse(TEXT("Observer exploration isolated"),Rooms->IsExplored(TEXT("EHB_Test_B"),Initial.Identity));
 Unit->SetActorLocation(Destination);Tracker->RefreshRoom(.05,false);TestTrue(TEXT("Short transition retains room"),Tracker->CurrentRoom==Initial);
 Unit->SetActorLocation(Point);Tracker->RefreshRoom(.1,false);TestTrue(TEXT("Returning cancels transition"),Tracker->CurrentRoom==Initial);
 Unit->SetActorLocation(Destination);Tracker->RefreshRoom(.05,false);Tracker->RefreshRoom(.11,false);TestTrue(TEXT("Stable transition publishes next building"),Tracker->CurrentRoom==SecondRoom);
 TestEqual(TEXT("Jitter produces no duplicate transition events"),Events,2);
 const auto Saved=Rooms->ExportExploration(TEXT("EHB_Test_A"));TestEqual(TEXT("Two distinct visited rooms"),Saved.Num(),2);Rooms->ClearExploration(TEXT("EHB_Test_A"));Rooms->ImportExploration(TEXT("EHB_Test_B"),Saved,true);
 TestFalse(TEXT("Clear only requested observer"),Rooms->IsExplored(TEXT("EHB_Test_A"),Initial.Identity));TestTrue(TEXT("Exploration imported by stable identity"),Rooms->IsExplored(TEXT("EHB_Test_B"),SecondRoom.Identity));
 Unit->SetActorLocation(Outside);Tracker->RefreshRoom(0,true);TestEqual(TEXT("Immediate teleport clears room"),Tracker->CurrentRoom.Status,EEHBRoomLocationStatus::Outside);
 Other->SetActorLocation(Other->GetActorLocation()+FVector(2000,0,0));TestEqual(TEXT("Building transform invalidates old location"),Rooms->QueryLocation(Destination,SecondRoom).Status,EEHBRoomLocationStatus::Outside);
 Unit->SetActorLocation(Destination+FVector(2000,0,0));Tracker->RefreshRoom(0,true);Other->Destroy();Tracker->RefreshRoom(0,false);
 TestEqual(TEXT("Destroyed building clears without delay"),Tracker->CurrentRoom.Status,EEHBRoomLocationStatus::Outside);
 Tracker->OnRoomChangedNative.Clear();
 return true;
}
