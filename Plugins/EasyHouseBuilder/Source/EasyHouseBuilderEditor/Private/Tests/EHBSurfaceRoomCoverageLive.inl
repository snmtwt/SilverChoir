IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLiveSurfaceRoomCoverageTest,"EHB.Rooms.SurfaceCoverageLive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLiveSurfaceRoomCoverageTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};
 for(int32 BindingMode=0;BindingMode<3;++BindingMode)
 {
  FTransientTopologyFixture Fixture;if(!Fixture.Create(GEditor->GetEditorWorldContext().World(),RF_Transactional)||!AddCopyRoomOutlines(Fixture,true))return false;
  auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);if(!UEHBBuildingToolset::EnableWallNodeEditing(B,false).bSucceeded)return false;
  const FGuid Node=B->FindNodeForPhysicalPillar(Fixture.Pillars[0]->ElementGuid);
  for(const auto Binding:TArray<FEHBWallNodePillarBinding>(B->WallNodeOwnership.Bindings))if(BindingMode==2||(BindingMode==1&&Binding.NodeGuid==Node))
  {const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Binding.NodeGuid;});if(!UEHBBuildingToolset::RemovePhysicalColumn(B,Binding.NodeGuid,Binding.PhysicalPillarGuid,N->GeometryRevision,false).bSucceeded)return false;}
  const FGuid Removed=Fixture.Walls[0]->ElementGuid;const auto Rooms=B->GetClosedLoopsByWallGuid(Removed);if(Rooms.Num()!=1)return false;const FGuid RoomId=Rooms[0].LoopGuid;
  AEHB_Floor* Floor=nullptr;AEHB_FloorSlab* Slab=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery())){if(auto* F=Cast<AEHB_Floor>(E);F&&F->RoomLoopGuid==RoomId)Floor=F;if(auto* S=Cast<AEHB_FloorSlab>(E);S&&S->RoomFillLoopGuid==RoomId)Slab=S;}if(!Floor||!Slab)return false;
  const FGuid FloorId=Floor->ElementGuid;FName Status;FEHBSurfaceRoomCoverage Coverage;const auto Before=LiveNodeAuthoritySnapshot(B),Receipt=LiveNodeReceipt(B);
  TestTrue(TEXT("Runtime floor coverage query"),B->QuerySurfaceRoomCoverage(FloorId,Coverage,Status));TestTrue(TEXT("Floor footprint completely covers its current room"),Coverage.Rooms.Num()==1&&Coverage.Rooms[0].RoomGuid==RoomId&&FMath::IsNearlyEqual(Coverage.Rooms[0].SurfaceFraction,1.0,0.00001));
  TestTrue(TEXT("Transformed slab coverage query"),B->QuerySurfaceRoomCoverage(Slab->ElementGuid,Coverage,Status));TestTrue(TEXT("Slab net surface belongs geometrically inside room"),Coverage.Rooms.Num()==1&&Coverage.Rooms[0].RoomGuid==RoomId&&FMath::IsNearlyZero(Coverage.OutsideAreaCm2,0.01)&&Coverage.Rooms[0].RoomFraction<1);
  TestEqual(TEXT("Coverage queries do not edit building"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Coverage queries do not issue receipts"),LiveNodeReceipt(B),Receipt);
  TestFalse(TEXT("Unknown element query refuses"),B->QuerySurfaceRoomCoverage(FGuid::NewGuid(),Coverage,Status));TestTrue(TEXT("Refusal clears prior coverage"),Coverage.Rooms.IsEmpty()&&!Coverage.ElementGuid.IsValid());
  {
   FEHBChangeNotificationBatch InFlight(*B);TestFalse(TEXT("Partial transaction has no spatial answer"),B->QuerySurfaceRoomCoverage(FloorId,Coverage,Status));TestEqual(TEXT("Specific in-flight refusal"),Status,FName(TEXT("SurfaceCoverageEditInFlight")));InFlight.Rollback();
  }
  bool CallbackRead=false;auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);Observer->ObserveCommit=[&](const auto&){FEHBSurfaceRoomCoverage C;FName Why;CallbackRead=B->QuerySurfaceRoomCoverage(FloorId,C,Why)&&C.Rooms.IsEmpty();};B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);
  const auto Applied=UEHBBuildingToolset::RemoveWalls(B,{Removed},B->RelationshipGraphRevision,false);B->OnEditCommitted.RemoveAll(Observer);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestTrue(TEXT("Final committed callback can query spatial state"),CallbackRead);
  TestTrue(TEXT("Retained floor spatial query"),B->QuerySurfaceRoomCoverage(FloorId,Coverage,Status));TestTrue(TEXT("Open region reports outside without stale ownership"),Coverage.Rooms.IsEmpty()&&FMath::IsNearlyEqual(Coverage.AreaCm2,Coverage.OutsideAreaCm2,0.01));TestFalse(TEXT("Spatial query does not bind independent floor"),Floor->RoomLoopGuid.IsValid());
  const auto After=LiveNodeAuthoritySnapshot(B);TestTrue(TEXT("Undo opening"),GEditor->UndoTransaction());TestTrue(TEXT("Room coverage recovers after undo"),B->QuerySurfaceRoomCoverage(FloorId,Coverage,Status));TestTrue(TEXT("Restored actual room ID"),Coverage.Rooms.Num()==1&&Coverage.Rooms[0].RoomGuid==RoomId);TestTrue(TEXT("Redo opening"),GEditor->RedoTransaction());TestEqual(TEXT("Query did not modify transaction result"),LiveNodeAuthoritySnapshot(B),After);
  // Spatial coverage also handles authored region changes without requiring the
  // stale generation RoomGuid to be changed; this is a query, not edit permission.
  const auto Original=Floor->FloorRegions;auto Spanning=Original;Spanning[0].OuterPolygon={{0,0,300},{1000,0,300},{1000,500,300},{0,500,300}};
  Floor->FloorRegions=Spanning;TestTrue(TEXT("Fresh region values observed without receipt changes"),B->QuerySurfaceRoomCoverage(FloorId,Coverage,Status));TestTrue(TEXT("Partial coverage across outside and remaining room"),Coverage.Rooms.Num()==1&&FMath::IsNearlyEqual(Coverage.Rooms[0].AreaCm2,200000.0,0.01)&&FMath::IsNearlyEqual(Coverage.OutsideAreaCm2,300000.0,0.01));Floor->FloorRegions=Original;
  TestEqual(TEXT("Query leaves authored source unmodified"),LiveNodeAuthoritySnapshot(B),After);
 }
 return true;
}
