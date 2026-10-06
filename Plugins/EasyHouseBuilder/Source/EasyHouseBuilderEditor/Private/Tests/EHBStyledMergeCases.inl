#include "Geometry/EHBFloorContactGeometry.h"
namespace
{
 struct FStyledMergeFixture
 {
  FTransientTopologyFixture Fixture;
  FGuid Shared,Node;
  TArray<FGuid> Floors,Slabs;
  TMap<FGuid,TSoftObjectPtr<UMaterialInterface>> Materials;
  AEHB_Building* Building() const{return Fixture.Building;}
  bool Create(UWorld* World,int32 BindingMode,bool Surface,bool FloorStyles=true,bool SlabStyles=true)
  {
   if(!Fixture.Create(World,RF_Transactional)||!AddCopyRoomOutlines(Fixture,Surface))return false;auto* B=Building();B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
   if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
   for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==Node))
   {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
   const auto Rooms=B->GetClosedLoopsByFloor(1);if(Rooms.Num()!=2)return false;for(FGuid Id:Rooms[0].WallGuids)if(Rooms[1].WallGuids.Contains(Id))Shared=Id;if(!Shared.IsValid())return false;
   auto Material=[](bool Other){return TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Other?TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"):TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));};
   for(auto* E:B->QueryElements(FEHBElementQuery()))
   {
    if(auto* F=Cast<AEHB_Floor>(E)){Floors.Add(F->ElementGuid);F->FloorMaterial=Material(FloorStyles&&F->RoomLoopGuid==Rooms[1].LoopGuid);Materials.Add(F->ElementGuid,F->FloorMaterial);if(!F->RebuildFloorMesh())return false;}
    if(auto* S=Cast<AEHB_FloorSlab>(E)){Slabs.Add(S->ElementGuid);S->SurfaceMaterial=Material(SlabStyles&&S->RoomFillLoopGuid==Rooms[1].LoopGuid);Materials.Add(S->ElementGuid,S->SurfaceMaterial);if(!S->RebuildSlabMesh())return false;}
   }
   for(FGuid Id:Floors){auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(Id));FEHBBuildingClosedLoop Room;if(!F->TryGetRoomLoop(Room))return false;F->RefreshSurfaceFinishRelationsFromRoomLoop(Room);}
   return Floors.Num()==2&&Slabs.Num()==2;
  }
 };
 bool CheckStyledMerge(FAutomationTestBase& Test,AEHBBuildingActorBase* B,const TArray<FGuid>& Floors,const TArray<FGuid>& Slabs,const TMap<FGuid,TSoftObjectPtr<UMaterialInterface>>& Materials)
 {
  FName Status;const auto Rooms=B->GetClosedLoopsByFloor(1);if(!Test.TestEqual(TEXT("Actual single merged room"),Rooms.Num(),1))return false;
  TArray<FEHBFloorSupportSurface> Combined;TArray<TArray<FEHBFloorSupportSurface>> BySlab;AEHB_FloorSlab* First=nullptr;
  for(FGuid Id:Slabs)
  {
   auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));if(!Test.TestNotNull(TEXT("Different-style slab actor retained"),S))return false;First=S;
   Test.TestEqual(TEXT("Slab authored material retained"),S->SurfaceMaterial,Materials.FindChecked(Id));Test.TestTrue(TEXT("Slab independent provenance"),S->OutlineSource==EEHBOutlineSource::RetainedRegion&&S->IsRecordedOutlineUnchanged()&&!S->RoomFillLoopGuid.IsValid()&&!S->bHasRoomFillAnchor);
   TArray<FEHBFloorSupportSurface> Actual;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(S,Actual,Status))return false;Combined.Append(Actual);BySlab.Add(Actual);
   TArray<UEHBGeneratedMeshComponent*> Meshes;S->GetGeneratedMeshComponents(Meshes);for(auto* M:Meshes)Test.TestEqual(TEXT("Actual slab mesh retains its material"),M->GetMaterial(0),Materials.FindChecked(Id).LoadSynchronous());
  }
  TArray<FVector> Destination;if(!First||!FEasyHouseEditorMode::BuildRoomSlabFollowOutline(First,Rooms[0],{},Destination))return false;for(auto& P:Destination)P=First->GetElementLocalTransform().TransformPosition(P);FEHBFloorSupportSurface Expected;Expected.OuterPolygon=Destination;
  Test.TestTrue(TEXT("Generated slab union fills merged net boundary without wall-width gap"),EHBRoomFinishMove::SameTopCoverage({Expected},Combined,Status));
  TArray<FEHBFloorFinishRegion> Left;for(const auto& T:BySlab[0]){auto& R=Left.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double Overlap=0;Test.TestTrue(TEXT("Actual seam overlap can be measured"),FEHBFloorContactGeometry::MeasureArea(Left,BySlab[1],Overlap,Status));Test.TestTrue(TEXT("Different materials have no overlapping generated top area"),Overlap<=0.01);
  for(FGuid Id:Floors)
  {
   auto* F=Cast<AEHB_Floor>(B->FindElementActorByGuid(Id));if(!Test.TestNotNull(TEXT("Different-style floor actor retained"),F))return false;
   Test.TestEqual(TEXT("Floor authored material retained"),F->FloorMaterial,Materials.FindChecked(Id));Test.TestTrue(TEXT("Floor independent provenance"),F->OutlineSource==EEHBOutlineSource::RetainedRegion&&F->IsRecordedOutlineUnchanged()&&!F->RoomLoopGuid.IsValid());
   TArray<UEHBGeneratedMeshComponent*> Meshes;F->GetGeneratedMeshComponents(Meshes);for(auto* M:Meshes)Test.TestEqual(TEXT("Actual floor mesh retains its material"),M->GetMaterial(0),Materials.FindChecked(Id).LoadSynchronous());
   TArray<FEHBElementRelation> Contacts;if(!F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Contacts,Status))return false;Test.TestEqual(TEXT("Styled region complete contact cache"),Contacts.Num(),F->SurfaceFinishRelationGuids.Num());
   for(const auto& R:Contacts){const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});Test.TestTrue(TEXT("Saved styled contact matches generated host"),Saved&&FMath::Abs(Saved->ContactArea-R.ContactArea)<=0.01&&Saved->StringMetadata.OrderIndependentCompareEqual(R.StringMetadata));}
   FEHBSurfaceRoomCoverage C;if(!B->QuerySurfaceRoomCoverage(Id,C,Status))return false;Test.TestTrue(TEXT("Partial source spatially belongs to merged room"),C.Rooms.Num()==1&&C.Rooms[0].RoomGuid==Rooms[0].LoopGuid&&FMath::IsNearlyZero(C.OutsideAreaCm2,0.01));
  }
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;return Test.TestTrue(TEXT("Styled source remains valid for later commands"),FEHBCopyOutlinePolicy::Prepare(B,Model,B->QueryElements(FEHBElementQuery())).bSucceeded);
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStyledMergeTest,"EHB.Floors.MaterialPartitionMerge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStyledMergeTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBWallRemovalCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Binding=0;Binding<3;++Binding)for(bool Surface:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Binding,Surface))return false;auto* B=Setup.Building();
  const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);const auto Relations=B->ElementRelations;const int32 Count=B->QueryElements(FEHBElementQuery()).Num();TMap<FGuid,TArray<FVector>> FloorPolygons;for(FGuid Id:Setup.Floors)FloorPolygons.Add(Id,Cast<AEHB_Floor>(B->FindElementActorByGuid(Id))->LocalFloorPolygon);
  int32 Commits=0,Events=0;auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const auto&){++Commits;};Observer->Observe=[&](FGuid,FName,bool){++Events;};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);B->OnElementRelationRemoved.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Removed);ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);B->OnElementRelationAdded.RemoveAll(Observer);B->OnElementRelationRemoved.RemoveAll(Observer);};
  auto Remove=[&](bool Preview=false){return UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,Preview);};const auto Preview=Remove(true);if(!TestTrue(*FString::Printf(TEXT("Styled merge preview: %s"),*Preview.Message),Preview.bSucceeded))return false;TestEqual(TEXT("Styled preview has no authoring mutation"),LiveNodeAuthoritySnapshot(B),Before);
  for(int32 Phase=1;Phase<=7;++Phase){EHBWallRemovalCommand::FailurePhase=Phase;const auto R=Remove();TestEqual(TEXT("Real styled merge phase reached"),EHBWallRemovalCommand::FailurePhase,0);TestEqual(TEXT("Styled merge rollback"),R.Message,FString(TEXT("WallRemovalFailedRolledBack")));TestEqual(TEXT("Rollback preserves both styles and outlines"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Rollback restores receipt"),LiveNodeReceipt(B),Receipt);TestEqual(TEXT("No failed commit"),Commits,0);TestEqual(TEXT("No failed external geometry or contact events"),Events,0);}
  const auto Applied=Remove();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("One styled merge receipt"),Commits,1);TestEqual(TEXT("Only the requested wall retires"),B->QueryElements(FEHBElementQuery()).Num(),Count-1);if(!CheckStyledMerge(*this,B,Setup.Floors,Setup.Slabs,Setup.Materials))return false;
  for(const auto& P:FloorPolygons)TestEqual(TEXT("Floor partition retains exact authored vertices"),Cast<AEHB_Floor>(B->FindElementActorByGuid(P.Key))->LocalFloorPolygon,P.Value);
  for(const auto& R:B->ElementRelations)if(R.Type==EEHBElementRelationType::SurfaceFinish){const auto* Old=Relations.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});if(Old)TestEqual(TEXT("Surviving styled contact identity"),R.RelationGuid,Old->RelationGuid);}
  for(const auto& P:Setup.Materials)TestTrue(TEXT("Receipt includes migrated styled element"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& V){return V.ElementGuid==P.Key;}));
  const auto After=LiveNodeAuthoritySnapshot(B),AfterReceipt=LiveNodeReceipt(B);TestTrue(TEXT("Undo styled merge"),GEditor->UndoTransaction());TestEqual(TEXT("Undo styled merge exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Redo styled merge"),GEditor->RedoTransaction());TestEqual(TEXT("Redo styled merge exact"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Redo styled receipt"),LiveNodeReceipt(B),AfterReceipt);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(3200,0,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;Setup.Fixture.Actors.Append(Copy.Actors);GEditor->UndoTransaction(false);TestEqual(TEXT("Copy keeps original styled partitions"),LiveNodeAuthoritySnapshot(B),After);
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Setup.Node;});FEHBNodeMoveRequest M;M.NodeGuid=N->NodeGuid;M.ExpectedPosition=N->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(-25,0,0);const auto Moved=EHBNodeAuthorityEditing::ExecuteMoves(B,{M},{{M.NodeGuid,N->GeometryRevision}},false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
  for(const auto& P:FloorPolygons)TestEqual(TEXT("Independent material partition stays fixed on later wall move"),Cast<AEHB_Floor>(B->FindElementActorByGuid(P.Key))->LocalFloorPolygon,P.Value);GEditor->UndoTransaction(false);TestEqual(TEXT("Undo later wall move"),LiveNodeAuthoritySnapshot(B),After);
  for(FGuid Id:{Setup.Slabs[0],Setup.Floors[0]}){auto* E=B->FindElementActorByGuid(Id);auto P=EHBFinishRegionCommand::GetPolygon(E);const auto C=FBox(P).GetCenter();for(auto& V:P){V.X=C.X+(V.X-C.X)*0.9;V.Y=C.Y+(V.Y-C.Y)*0.9;}const auto Edit=UEHBBuildingToolset::EditFinishRegion(B,Id,B->RelationshipGraphRevision,B->GetElementGeometryRevision(Id),P,{},false);if(!TestTrue(*Edit.Message,Edit.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Region edit undo preserves partition seam"),LiveNodeAuthoritySnapshot(B),After);}
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStyledMergeMixedTest,"EHB.Floors.MaterialPartitionMixed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStyledMergeMixedTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(bool FloorStyles:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),0,true,FloorStyles,!FloorStyles))return false;auto* B=Setup.Building();const auto Before=LiveNodeAuthoritySnapshot(B);
  const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;int32 Floors=0,Slabs=0;for(auto* E:B->QueryElements(FEHBElementQuery()))
  {if(auto* F=Cast<AEHB_Floor>(E)){++Floors;TestEqual(TEXT("Floor binding follows whether styles merged"),F->OutlineSource==EEHBOutlineSource::RetainedRegion,FloorStyles);}if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;TestEqual(TEXT("Slab binding follows whether styles merged"),S->OutlineSource==EEHBOutlineSource::RetainedRegion,!FloorStyles);}}
  TestEqual(TEXT("Different floors preserve both owners"),Floors,FloorStyles?2:1);TestEqual(TEXT("Different slabs preserve both owners"),Slabs,FloorStyles?1:2);GEditor->UndoTransaction(false);TestEqual(TEXT("Mixed style policy undo exact"),LiveNodeAuthoritySnapshot(B),Before);
 }
 return true;
}
