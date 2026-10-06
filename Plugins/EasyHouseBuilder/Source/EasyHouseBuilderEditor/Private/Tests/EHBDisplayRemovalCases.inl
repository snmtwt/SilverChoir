IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayRemovalTest,"EHB.Topology.StyledDisplayRemoval",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayRemovalTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;EHBWallRemovalCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  auto* Remote=GEditor->GetEditorWorldContext().World()->SpawnActor<AEHB_FloorSlab>();Setup.Fixture.Actors.Add(Remote);Remote->AttachToBuilding(B,FTransform(FVector(3000,3000,300)));Remote->FloorIndex=1;Remote->RoomFillFloorIndex=1;Remote->FloorRole=EEHBBuildingFloorElementRole::FloorCeiling;
  if(!Remote->SetSlabOutline({{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}},{}))return false;Remote->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
  const auto RemotePolygon=Remote->LocalTopPolygon;
  for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=10;S->RebuildSlabMesh();S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  auto DisplayState=[&](){FString Text;for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));FString V;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,V);Text+=V;}return Text;};
  const auto Before=LiveNodeAuthoritySnapshot(B),BeforeDisplay=DisplayState(),Receipt=LiveNodeReceipt(B);
  const auto Preview=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;
  TestEqual(TEXT("Expanded removal preview preserves source"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Preview does not assign partition"),DisplayState(),BeforeDisplay);
  for(int32 Phase:{4,5,6,7})
  {
   EHBWallRemovalCommand::FailurePhase=Phase;const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);
   TestEqual(TEXT("Expanded removal injected phase reached"),EHBWallRemovalCommand::FailurePhase,0);TestEqual(TEXT("Expanded removal rolls back"),R.Message,FString(TEXT("WallRemovalFailedRolledBack")));TestEqual(TEXT("Rollback restores display author data"),DisplayState(),BeforeDisplay);TestEqual(TEXT("Rollback restores entire source"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Rollback restores receipt"),LiveNodeReceipt(B),Receipt);
  }
  const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  TestEqual(TEXT("Real wall removal merges room"),B->GetClosedLoopsByFloor(1).Num(),1);
  TestFalse(TEXT("Distant unchanged slab stays unconstrained"),Remote->DisplayPartition.IsActive());TestEqual(TEXT("Distant author outline unchanged"),Remote->LocalTopPolygon,RemotePolygon);
  auto RemoteEdit=RemotePolygon;RemoteEdit[0].X-=10;TestTrue(TEXT("Distant slab retains ordinary edit validation"),Remote->ValidateSlabOutline(RemoteEdit,{}));
  TArray<FEHBFloorSupportSurface> All;double Sum=0;FName Status;
  for(FGuid Id:Setup.Slabs)
  {
   auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));TestTrue(TEXT("Real deletion applied partition"),S->DisplayPartition.IsActive());TestEqual(TEXT("Authored expansion retained"),S->VisualExpansion,10.0f);TestEqual(TEXT("Authored style retained"),S->SurfaceMaterial,Setup.Materials.FindChecked(Id));TestTrue(TEXT("Partition provenance recorded"),S->IsRecordedOutlineUnchanged());
   TArray<FEHBFloorSupportSurface> Tops;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(S,Tops,Status))return false;TArray<FEHBFloorFinishRegion> Regions;for(const auto& T:Tops){auto& V=Regions.AddDefaulted_GetRef();V.OuterPolygon=T.OuterPolygon;V.Holes=T.Holes;}double Area=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions,Tops,Area,Status))return false;Sum+=Area;All.Append(Tops);
  }
  TArray<FEHBFloorFinishRegion> Regions;for(const auto& T:All){auto& V=Regions.AddDefaulted_GetRef();V.OuterPolygon=T.OuterPolygon;V.Holes=T.Holes;}double Union=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions,All,Union,Status))return false;TestTrue(TEXT("Rotated actual allocated slabs do not overlap"),FMath::Abs(Sum-Union)<=0.01);
  const auto After=LiveNodeAuthoritySnapshot(B),AfterDisplay=DisplayState();GEditor->UndoTransaction();TestEqual(TEXT("Undo expanded deletion"),LiveNodeAuthoritySnapshot(B),Before);TestEqual(TEXT("Undo allocations"),DisplayState(),BeforeDisplay);GEditor->RedoTransaction();TestEqual(TEXT("Redo expanded deletion"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Redo allocations exact"),DisplayState(),AfterDisplay);

  auto* Edited=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));auto Smaller=Edited->LocalTopPolygon;const auto Center=FBox(Smaller).GetCenter();
  for(auto& V:Smaller){V.X=Center.X+(V.X-Center.X)*0.9;V.Y=Center.Y+(V.Y-Center.Y)*0.9;}
  auto Edit=[&](bool PreviewOnly){return UEHBBuildingToolset::EditFinishRegion(B,Edited->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(Edited->ElementGuid),Smaller,{},PreviewOnly);};
  const auto EditReceipt=LiveNodeReceipt(B);const auto EditPreview=Edit(true);if(!TestTrue(*EditPreview.Message,EditPreview.bSucceeded))return false;
  TestEqual(TEXT("Partition resize preview source unchanged"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Partition resize preview masks unchanged"),DisplayState(),AfterDisplay);
  for(int32 Phase=1;Phase<=4;++Phase)
  {
   EHBFinishRegionCommand::FailurePhase=Phase;const auto Failed=Edit(false);
   TestEqual(TEXT("Partition edit failure phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Partition edit rolled back"),Failed.Message,FString(TEXT("RegionEditFailedRolledBack")));
   TestEqual(TEXT("Partition edit rollback source"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Partition edit rollback masks"),DisplayState(),AfterDisplay);TestEqual(TEXT("Partition edit rollback receipt"),LiveNodeReceipt(B),EditReceipt);
  }
  const auto EditedResult=Edit(false);if(!TestTrue(*EditedResult.Message,EditedResult.bSucceeded))return false;
  TestEqual(TEXT("Resized mask tracks new author contour"),Edited->DisplayPartition.SourcePolygon,Smaller);TestTrue(TEXT("Resized provenance recorded"),Edited->IsRecordedOutlineUnchanged());TestFalse(TEXT("Resize leaves remote display unconstrained"),Remote->DisplayPartition.IsActive());
  const auto Resized=LiveNodeAuthoritySnapshot(B),ResizedDisplay=DisplayState();GEditor->UndoTransaction();TestEqual(TEXT("Resize undo source"),LiveNodeAuthoritySnapshot(B),After);TestEqual(TEXT("Resize undo masks"),DisplayState(),AfterDisplay);GEditor->RedoTransaction();TestEqual(TEXT("Resize redo source"),LiveNodeAuthoritySnapshot(B),Resized);TestEqual(TEXT("Resize redo masks"),DisplayState(),ResizedDisplay);
 }
 return true;
}
