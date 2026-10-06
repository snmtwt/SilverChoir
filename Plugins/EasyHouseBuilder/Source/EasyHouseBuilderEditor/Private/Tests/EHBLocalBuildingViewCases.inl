#include "Components/EHBLocalBuildingViewComponent.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLocalBuildingViewTest,"EHB.Runtime.LocalBuildingView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLocalBuildingViewTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();
 FStyledMergeFixture Setup,Other;
 if(!Setup.Create(World,2,true)||!Other.Create(World,2,true))return false;
 auto* B=Setup.Building();Other.Building()->SetActorLocation(FVector(5000,0,0));
 auto Center=[](AEHBBuildingActorBase* Building)
 {
  const auto Room=Building->GetClosedLoopsByFloor(1)[0];FEHBNodeRoomBoundary Boundary;Building->TryGetRoomBoundary(Room.LoopGuid,1,Boundary);FVector Sum=FVector::ZeroVector;
  for(FGuid Id:Boundary.NodeGuids)Sum+=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;})->LocalTransform.GetLocation();
  return Building->GetActorTransform().TransformPosition(Sum/Boundary.NodeGuids.Num()+FVector(0,0,50));
 };
 FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
 auto* Unit=World->SpawnActor<AActor>(Spawn);Setup.Fixture.Actors.Add(Unit);
 auto* Root=NewObject<USceneComponent>(Unit);Unit->SetRootComponent(Root);Unit->AddInstanceComponent(Root);Root->RegisterComponent();Unit->SetActorLocation(Center(B));
 auto* Tracker=NewObject<UEHBRoomTrackerComponent>(Unit);Unit->AddInstanceComponent(Tracker);Tracker->RegisterComponent();Tracker->bRememberVisitedRooms=false;Tracker->RefreshRoom();
 if(!TestEqual(TEXT("Real unit query is inside"),Tracker->CurrentRoom.Status,EEHBRoomLocationStatus::Inside))return false;
 auto SpawnSlab=[&](int32 Floor,EEHBBuildingFloorElementRole Role)
 {
  auto* Slab=World->SpawnActor<AEHB_FloorSlab>(Spawn);Setup.Fixture.Actors.Add(Slab);
  Slab->ConfigureDefaultSlab(B,FTransform(FVector(0,0,Floor*300)),200,20,false);Slab->SetFloorAssignment(Floor,Role);return Slab;
 };
 auto* Upper=SpawnSlab(2,EEHBBuildingFloorElementRole::FloorCeiling);
 auto* Roof=SpawnSlab(3,EEHBBuildingFloorElementRole::Roof);
 auto* Wall=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
 auto* Ceiling=CastChecked<AEHBElementActorBase>(B->FindElementActorByGuid(Setup.Slabs[0]));
 auto Primitive=[](AActor* Actor){return Actor->FindComponentByClass<UPrimitiveComponent>();};
 auto* UpperMesh=Primitive(Upper);auto* RoofMesh=Primitive(Roof);auto* CeilingMesh=Primitive(Ceiling);auto* WallMesh=Wall->LeftWallMeshComponent.Get();
 if(!TestNotNull(TEXT("Upper slab has primitive"),UpperMesh)||!TestNotNull(TEXT("Roof role has primitive"),RoofMesh)||!TestNotNull(TEXT("Ceiling has primitive"),CeilingMesh))return false;
 const bool Collision=Upper->GetActorEnableCollision(),ActorHidden=Upper->IsHidden(),Visible=UpperMesh->IsVisible();
 const auto CollisionMode=UpperMesh->GetCollisionEnabled();
 auto* PC=World->SpawnActor<APlayerController>(Spawn);auto* PC2=World->SpawnActor<APlayerController>(Spawn);Setup.Fixture.Actors.Add(PC);Setup.Fixture.Actors.Add(PC2);
 auto AddView=[](APlayerController* Controller){auto* View=NewObject<UEHBLocalBuildingViewComponent>(Controller);Controller->AddInstanceComponent(View);View->RegisterComponent();return View;};
 auto* View=AddView(PC);auto* View2=AddView(PC2);
 PC->HiddenPrimitiveComponents.Add(WallMesh); // Another system already hides this primitive.
 if(!TestTrue(TEXT("Target unit accepted"),View->SetTrackedUnit(Unit)))return false;
 if(!TestEqual(TEXT("Local view applies"),View->LastStatus,FName(TEXT("Applied"))))return false;
 TestTrue(TEXT("Upper floor hidden for focus controller"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));
 TestTrue(TEXT("Roof hidden"),PC->HiddenPrimitiveComponents.Contains(RoofMesh));
 TestTrue(TEXT("Current ceiling hidden"),PC->HiddenPrimitiveComponents.Contains(CeilingMesh));
 TestFalse(TEXT("Other local controller unaffected"),PC2->HiddenPrimitiveComponents.Contains(UpperMesh));
 TSet<FPrimitiveComponentId> RenderHidden;PC->BuildHiddenComponentList(FVector::ZeroVector,RenderHidden);
 TestTrue(TEXT("Actual engine view consumes hidden primitive"),RenderHidden.Contains(UpperMesh->GetPrimitiveSceneId()));
 TestEqual(TEXT("Actor collision preserved"),Upper->GetActorEnableCollision(),Collision);TestEqual(TEXT("Component collision preserved"),UpperMesh->GetCollisionEnabled(),CollisionMode);
 TestEqual(TEXT("No global actor hide"),Upper->IsHidden(),ActorHidden);TestEqual(TEXT("No global component hide"),UpperMesh->IsVisible(),Visible);
 const int32 HiddenCount=PC->HiddenPrimitiveComponents.Num();View->RefreshView();TestEqual(TEXT("Repeated view update does not duplicate entries"),PC->HiddenPrimitiveComponents.Num(),HiddenCount);
 View2->SetTrackedUnit(Unit);View->SetTrackedUnit(nullptr);
 TestFalse(TEXT("Clearing first target restores first view"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));
 TestTrue(TEXT("Second player retains its view"),PC2->HiddenPrimitiveComponents.Contains(UpperMesh));
 TestTrue(TEXT("Preexisting hidden entry is preserved"),PC->HiddenPrimitiveComponents.Contains(WallMesh));
 View->SetTrackedUnit(Unit);View->bHideCurrentCeiling=false;View->RefreshView();TestFalse(TEXT("Ceiling option applies"),PC->HiddenPrimitiveComponents.Contains(CeilingMesh));
 View->bHideHigherFloors=false;View->bHideRoofs=false;View->RefreshView();TestFalse(TEXT("Upper floors can be restored"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));TestFalse(TEXT("Roofs can be restored"),PC->HiddenPrimitiveComponents.Contains(RoofMesh));
 View->bHideHigherFloors=true;View->bHideRoofs=true;View->bHideCurrentCeiling=true;View->RefreshView();
 // Feed a changed authoritative floor value to isolate the display policy.
 // This is not a stair traversal or complete two-storey room-query test.
 const auto OriginalRoom=Tracker->CurrentRoom;Tracker->CurrentRoom.FloorIndex=2;View->bHideCurrentCeiling=false;View->RefreshView();TestFalse(TEXT("Focused upper floor restores its slab"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));Tracker->CurrentRoom=OriginalRoom;
 Unit->SetActorLocation(Center(Other.Building()));Tracker->RefreshRoom();View->RefreshView();TestFalse(TEXT("Moving to another building restores old building"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));
 Unit->SetActorLocation(Center(B));Tracker->RefreshRoom();View->RefreshView();
 View->Deactivate();TestFalse(TEXT("Deactivation clears owned view state"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));View->Activate();View->RefreshView();
 Unit->Destroy();View->RefreshView();TestFalse(TEXT("Destroyed target restores view immediately"),PC->HiddenPrimitiveComponents.Contains(UpperMesh));
 View2->DestroyComponent();TestFalse(TEXT("Component teardown restores second player"),PC2->HiddenPrimitiveComponents.Contains(UpperMesh));
 TestTrue(TEXT("Unrelated visibility survives all cleanup"),PC->HiddenPrimitiveComponents.Contains(WallMesh));
 return true;
}
