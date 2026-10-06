namespace
{
 FString DisplayTopologyState(AEHBBuildingActorBase* B)
 {
  FString Result=LiveNodeAuthoritySnapshot(B);auto Elements=B->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& C){return A.ElementGuid<C.ElementGuid;});for(auto* E:Elements)if(auto* S=Cast<AEHB_FloorSlab>(E)){FString V;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,V);Result+=V;}return Result;
 }
 bool MakeDisplaySubdivisionEndpoints(AEHBBuildingActorBase* B,TArray<FEHBWallCreationEndpoint>& Points)
 {
  AEHB_Wall* Bottom=nullptr,*Top=nullptr;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E)){if(FMath::Abs(W->LocalStart.Y)<0.001&&FMath::Abs(W->LocalEnd.Y)<0.001&&FMath::Min(W->LocalStart.X,W->LocalEnd.X)<250&&FMath::Max(W->LocalStart.X,W->LocalEnd.X)>250)Bottom=W;if(FMath::Abs(W->LocalStart.Y-500)<0.001&&FMath::Abs(W->LocalEnd.Y-500)<0.001&&FMath::Min(W->LocalStart.X,W->LocalEnd.X)<250&&FMath::Max(W->LocalStart.X,W->LocalEnd.X)>250)Top=W;}if(!Bottom||!Top)return false;
  Points.Reset();for(auto* W:{Bottom,Top}){auto& P=Points.AddDefaulted_GetRef();P.Wall=W;P.WallDistance=FMath::Abs(250-W->LocalStart.X);const auto Preview=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(B,W->ElementGuid,P.WallDistance);if(!Preview.bSucceeded)return false;P.LocalLocation=Preview.LocalPillarPosition;P.WorldLocation=B->GetActorTransform().TransformPosition(P.LocalLocation);P.FloorIndex=1;}

  return true;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayTopologyMergeTest,"EHB.Topology.PartitionedDisplayMerge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayTopologyMergeTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBWallRemovalCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(bool Different:{false,true})for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true,Different,Different)||!InitializeBoundDisplays(Setup))return false;auto* B=Setup.Building();
  auto State=[&](){FString Result=LiveNodeAuthoritySnapshot(B);auto Elements=B->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& C){return A.ElementGuid<C.ElementGuid;});for(auto* E:Elements)if(auto* S=Cast<AEHB_FloorSlab>(E)){FString V;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,V);Result+=V;}return Result;};
  const auto Before=State(),Receipt=LiveNodeReceipt(B);const auto Preview=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,true);if(!TestTrue(*FString::Printf(TEXT("Masked merge preview mode%d style%d: %s"),Mode,Different,*Preview.Message),Preview.bSucceeded))return false;TestEqual(TEXT("Masked merge preview is read-only"),State(),Before);
  for(int32 Phase:{4,5,6,7}){EHBWallRemovalCommand::FailurePhase=Phase;const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);TestFalse(TEXT("Masked merge injected failure"),R.bSucceeded);TestEqual(TEXT("Merge injection consumed"),EHBWallRemovalCommand::FailurePhase,0);TestEqual(TEXT("Masked merge rollback"),State(),Before);TestEqual(TEXT("Masked merge receipt rollback"),LiveNodeReceipt(B),Receipt);}
  const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestEqual(TEXT("Merged room count"),B->GetClosedLoopsByFloor(1).Num(),1);int32 Slabs=0;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;TestTrue(TEXT("Merged partition provenance"),S->IsRecordedOutlineUnchanged());if(S->DisplayPartition.IsActive())TestEqual(TEXT("Merged mask matches source"),S->DisplayPartition.SourcePolygon,S->LocalTopPolygon);}TestEqual(TEXT("Same style consolidates, different styles remain"),Slabs,Different?2:1);
  const auto After=State();GEditor->UndoTransaction();TestEqual(TEXT("Merge undo"),State(),Before);GEditor->RedoTransaction();TestEqual(TEXT("Merge redo"),State(),After);
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;const auto Again=UEHBBuildingToolset::RemoveWalls(B,{Model.Walls[0].WallGuid},B->RelationshipGraphRevision,false);if(!TestTrue(*Again.Message,Again.bSucceeded))return false;TestEqual(TEXT("Repeated deletion opens room"),B->GetClosedLoopsByFloor(1).Num(),0);GEditor->UndoTransaction();TestEqual(TEXT("Repeated deletion undo restores allocated state"),State(),After);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplaySubdivisionTest,"EHB.Topology.PartitionedDisplaySubdivision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplaySubdivisionTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBWallSplitTestHooks::FailurePhase=EHBWallSplitTestHooks::EFailurePhase::None;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true)||!InitializeBoundDisplays(Setup))return false;auto* B=Setup.Building();
  TArray<FEHBWallCreationEndpoint> Points;if(!MakeDisplaySubdivisionEndpoints(B,Points))return false;
  auto State=[&](){return DisplayTopologyState(B);};const auto Before=State(),Receipt=LiveNodeReceipt(B);const auto Preview=EHBWallCreationCommand::CommitAnchoredPath(B,Points,false,FEHBWallCreationOptions(),true);if(!TestTrue(*FString::Printf(TEXT("Masked subdivision preview mode%d: %s"),Mode,*Preview.Status.ToString()),Preview.bSucceeded))return false;TestEqual(TEXT("Subdivision preview source unchanged"),State(),Before);
  using Phase=EHBWallSplitTestHooks::EFailurePhase;
  for(Phase F:{Phase::AfterInheritedSlabSpawn,Phase::AfterInheritedFloorSpawn,Phase::AfterBaseline,Phase::AfterEditRecord})
  {
   EHBWallSplitTestHooks::FailurePhase=F;const auto Failed=EHBWallCreationCommand::CommitAnchoredPath(B,Points,false,FEHBWallCreationOptions());TestFalse(TEXT("Masked subdivision injected failure"),Failed.bSucceeded);TestEqual(TEXT("Subdivision injection consumed"),EHBWallSplitTestHooks::FailurePhase,Phase::None);TestEqual(TEXT("Subdivision rollback source and masks"),State(),Before);TestEqual(TEXT("Subdivision receipt rollback"),LiveNodeReceipt(B),Receipt);
  }
  const auto Applied=EHBWallCreationCommand::CommitAnchoredPath(B,Points,false,FEHBWallCreationOptions());if(!TestTrue(*FString::Printf(TEXT("Masked subdivision apply mode%d: %s / %s"),Mode,*Applied.Status.ToString(),*Applied.FailureReason.ToString()),Applied.bSucceeded))return false;
  TestEqual(TEXT("Partition creates third room"),B->GetClosedLoopsByFloor(1).Num(),3);int32 Slabs=0;TSet<int32> Priorities;
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E)){++Slabs;TestTrue(TEXT("Child source provenance"),S->IsRecordedOutlineUnchanged());if(S->DisplayPartition.IsActive()){TestFalse(TEXT("Allocated children have unique priority"),Priorities.Contains(S->DisplayPartition.Priority));Priorities.Add(S->DisplayPartition.Priority);TestEqual(TEXT("Child mask matches its own source"),S->DisplayPartition.SourcePolygon,S->LocalTopPolygon);}}
  TestEqual(TEXT("Three inherited room slabs"),Slabs,3);const auto After=State();GEditor->UndoTransaction();TestEqual(TEXT("Subdivision undo source"),State(),Before);GEditor->RedoTransaction();TestEqual(TEXT("Subdivision redo source"),State(),After);
 }
 return true;
}
