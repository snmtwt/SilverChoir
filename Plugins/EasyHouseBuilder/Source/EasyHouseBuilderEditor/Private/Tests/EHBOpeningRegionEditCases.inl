#include <limits>
#include "Widgets/SEHBElementEditorPanel.h"
#include "Cutting/EHBPolygonClipper.h"
namespace
{
 bool SeedRegionOpenings(AEHBBuildingActorBase* B,AEHB_FloorSlab* S)
 {
  const auto C=FBox(S->LocalTopPolygon).GetCenter();auto Rect=[&](double X){return TArray<FVector>{{X-10,C.Y-10,0},{X+10,C.Y-10,0},{X+10,C.Y+10,0},{X-10,C.Y+10,0}};};
  TArray<FEHBFloorSlabHole> Holes;Holes.AddDefaulted_GetRef().LocalPolygon=Rect(C.X-50);FEHBCutOperation Cut;for(const auto& P:Rect(C.X+50))Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Cut.EnsureGuids();
  auto Partition=S->DisplayPartition;Partition.Regions.Reset();
  for(const auto& R:S->DisplayPartition.Regions)
  {
   TArray<FVector> Outer;for(const auto& V:R.Boundary)Outer.Add(FVector(V.X,V.Y,0));TArray<TArray<FVector>> Voids={Holes[0].LocalPolygon,Rect(C.X+50)};
   for(const auto& H:R.Holes){auto& Hole=Voids.AddDefaulted_GetRef();for(const auto& V:H.Vertices)Hole.Add(FVector(V.X,V.Y,0));}
   FEHBPolygonClipResult Result;if(!FEHBPolygonClipper::DifferenceXY(Outer,Voids,Result))return false;
   for(const auto& Part:Result.Regions){auto& Out=Partition.Regions.AddDefaulted_GetRef();for(const auto& V:Part.OuterLoop)Out.Boundary.Add({V.X,V.Y});for(const auto& H:Part.HoleLoops){auto& Hole=Out.Holes.AddDefaulted_GetRef().Vertices;for(const auto& V:H.ToLocalPositions())Hole.Add({V.X,V.Y});}}
  }
  S->CaptureDisplayPartitionSource(S->LocalTopPolygon,Holes,{Cut},Partition);if(!S->SetPartitionedSlabState(S->LocalTopPolygon,Holes,{Cut},Partition))return false;S->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
  // Establish an internally consistent fixture through the actual contact planner.
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* F=Cast<AEHB_Floor>(E))
  {
   TArray<FEHBElementRelation> Plan;FName Status;if(!F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Plan,Status))return false;const auto Previous=F->SurfaceFinishRelationGuids;TArray<FGuid> Next;
   for(const auto& R:Plan){const auto Id=B->AddOrUpdateElementRelation(R,true);if(Id!=R.RelationGuid)return false;Next.Add(Id);}for(FGuid Id:Previous)if(!Next.Contains(Id))B->RemoveElementRelation(Id);F->SurfaceFinishRelationGuids=MoveTemp(Next);
  }
  return true;
 }
 FString OpeningRegionState(AEHBBuildingActorBase* B)
 {
  FString Result=DisplayTopologyState(B);for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E))Result+=SlabEditState118(S);return Result;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningRegionEditTest,"EHB.Topology.OpeningRegionEdit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningRegionEditTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=10;S->RebuildSlabMesh();S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  auto Removed=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));if(!TestTrue(TEXT("Seed actual retained slab openings"),SeedRegionOpenings(B,S)))return false;
  const auto Cut=S->CutOperations[0];const auto Hole=S->LocalHoles[0].LocalPolygon;auto Polygon=S->LocalTopPolygon;const auto Center=FBox(Polygon).GetCenter();for(auto& V:Polygon){V.X=Center.X+(V.X-Center.X)*0.9;V.Y=Center.Y+(V.Y-Center.Y)*0.9;}
  const auto Before=OpeningRegionState(B),Receipt=LiveNodeReceipt(B);auto Edit=[&](bool Preview){return UEHBBuildingToolset::EditFinishRegion(B,S->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Polygon,{},Preview);};
  const auto Preview=Edit(true);if(!TestTrue(*FString::Printf(TEXT("Opening region preview: %s"),*Preview.Message),Preview.bSucceeded))return false;TestTrue(TEXT("Preview preserves source, meshes and masks"),OpeningRegionState(B)==Before);
  for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;const auto R=Edit(false);TestEqual(TEXT("Opening edit injection reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Opening edit rolled back"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Rollback preserves all source and mesh"),OpeningRegionState(B)==Before);TestEqual(TEXT("Rollback preserves receipt"),LiveNodeReceipt(B),Receipt);}
  const auto Applied=Edit(false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("Edited source polygon"),S->LocalTopPolygon,Polygon);TestTrue(TEXT("Authored hole retained"),S->LocalHoles[0].LocalPolygon==Hole);TestTrue(TEXT("Cut identity and source retained"),FEHBCutOperation::StaticStruct()->CompareScriptStruct(&S->CutOperations[0],&Cut,0));TestTrue(TEXT("New complete snapshot recorded"),S->DisplayPartition.SourceVersion==1&&S->IsRecordedOutlineUnchanged());
  TArray<FEHBFloorSupportSurface> All;double Sum=0;FName Status;for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));TArray<FEHBFloorSupportSurface> Tops;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(A,Tops,Status))return false;TArray<FEHBFloorFinishRegion> Regions;for(const auto& T:Tops){auto& R=Regions.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double Area=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions,Tops,Area,Status))return false;Sum+=Area;All.Append(Tops);TestEqual(TEXT("Material preserved"),A->SurfaceMaterial,Setup.Materials.FindChecked(Id));}
  TArray<FEHBFloorFinishRegion> UnionRegions;for(const auto& T:All){auto& R=UnionRegions.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double Area=0;if(!FEHBFloorContactGeometry::MeasureArea(UnionRegions,All,Area,Status))return false;TestTrue(TEXT("Actual adjacent materials have no overlap"),FMath::Abs(Sum-Area)<=0.01);
  const auto After=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Opening edit undo all state"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Opening edit redo all state"),OpeningRegionState(B)==After);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningToolCommandTest,"EHB.Topology.OpeningToolCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningToolCommandTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Retained:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));A->VisualExpansion=10;A->RebuildSlabMesh();A->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));const auto Before=OpeningRegionState(B);const auto Kind=S->OutlineSource;const auto Room=S->RoomFillLoopGuid,Anchor=S->RoomFillAnchorWallGuid;
  const auto Center=FBox(S->LocalTopPolygon).GetCenter();TArray<FEHBFloorSlabHole> Holes;Holes.AddDefaulted_GetRef().LocalPolygon={{Center.X-10,Center.Y-10,0},{Center.X+10,Center.Y-10,0},{Center.X+10,Center.Y+10,0},{Center.X-10,Center.Y+10,0}};
  auto Edit=[&](const TArray<FEHBFloorSlabHole>& H,bool Preview){return UEHBBuildingToolset::SetFloorSlabOpenings(S,H,{},B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Preview);};
  auto R=Edit(Holes,true);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Opening tool preview is read only"),OpeningRegionState(B)==Before);
  R=UEHBBuildingToolset::SetFloorSlabOpenings(S,Holes,{},B->RelationshipGraphRevision-1,B->GetElementGeometryRevision(S->ElementGuid),false);TestEqual(TEXT("Stale opening revision rejected"),R.Message,FString(TEXT("StaleRegionRevision")));
  for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;R=Edit(Holes,false);TestEqual(TEXT("Opening phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Opening phase rollback"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Opening rollback state"),OpeningRegionState(B)==Before);}
  R=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,Center,20,20,0);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  TestEqual(TEXT("Opening created"),S->LocalHoles.Num(),1);TestEqual(TEXT("Source intent preserved"),S->OutlineSource,Kind);TestEqual(TEXT("Room preserved"),S->RoomFillLoopGuid,Room);TestEqual(TEXT("Anchor preserved"),S->RoomFillAnchorWallGuid,Anchor);TestTrue(TEXT("Provenance current"),S->IsRecordedOutlineUnchanged());const auto Created=OpeningRegionState(B);
  GEditor->UndoTransaction();TestTrue(TEXT("Tool opening undo"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Tool opening redo"),OpeningRegionState(B)==Created);
  R=Edit(S->LocalHoles,false);TestEqual(TEXT("Same source no-op"),R.Message,FString(TEXT("NoChange")));TestTrue(TEXT("No-op state"),OpeningRegionState(B)==Created);
  R=Edit({},false);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Last opening removed"),S->LocalHoles.IsEmpty()&&S->IsRecordedOutlineUnchanged());GEditor->UndoTransaction();TestTrue(TEXT("Removal undo"),OpeningRegionState(B)==Created);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningDragTest,"EHB.Topology.OpeningDrag",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningDragTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Retained:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));A->VisualExpansion=10;A->RebuildSlabMesh();A->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));const auto C=FBox(S->LocalTopPolygon).GetCenter();const auto Added=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,C-FVector(50,0,0),30,30,0);if(!Added.bSucceeded)return false;
  FEHBCutOperation Cut;for(const auto& V:TArray<FVector>{{C.X+35,C.Y-15,0},{C.X+65,C.Y-15,0},{C.X+65,C.Y+15,0},{C.X+35,C.Y+15,0}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=V;Cut.EnsureGuids();if(Mode==2){Cut.Source.LocalTransform=FTransform(FQuat::Identity,FVector(5,0,(S->GetTopZ()+S->GetBottomZ())*0.5),FVector(1,1,2));Cut.Source.Height=20;}const auto AddedCut=UEHBBuildingToolset::SetFloorSlabOpenings(S,S->LocalHoles,{Cut},B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),false);if(!AddedCut.bSucceeded)return false;GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);
  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)})for(bool Corner:{false,true})
  {

   if(Corner)
   {
    auto Panel=SNew(SEHBElementEditorPanel);Panel->SetSelectedActor(S);Panel->SetSelectedFloorSlabCorner(S,Loop,0);
    const auto OriginalState=OpeningRegionState(B);
    for(bool Previous:{false,true})
    {
     EHBFinishRegionCommand::FailurePhase=2;Panel->HandleFloorSlabCornerDistanceCommitted(31,ETextCommit::OnEnter,Previous);
     TestEqual(TEXT("Dimension input uses unified transaction failure hook"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Dimension failure restores mesh and metadata"),OpeningRegionState(B),OriginalState);
     Panel->HandleFloorSlabCornerDistanceCommitted(31,ETextCommit::OnEnter,Previous);
     TArray<FVector> Actual;if(!S->GetEditableLoopCopy(Loop,Actual))return false;const int32 Adjacent=Previous?Actual.Num()-1:1;
     TestTrue(TEXT("Panel publishes exact opening side distance"),FMath::IsNearlyEqual(FVector::Dist2D(Actual[0],Actual[Adjacent]),31.0,0.001));
     TestEqual(TEXT("Numeric edit keeps cut identity"),S->CutOperations[0].OperationGuid,Cut.OperationGuid);
     TestTrue(TEXT("Numeric edit keeps recorded source valid"),S->IsRecordedOutlineUnchanged());
     const auto Edited=OpeningRegionState(B);GEditor->UndoTransaction();TestEqual(TEXT("Numeric opening edit undo"),OpeningRegionState(B),OriginalState);GEditor->RedoTransaction();TestEqual(TEXT("Numeric opening edit redo"),OpeningRegionState(B),Edited);GEditor->UndoTransaction();
    }
    Panel->HandleFloorSlabCornerDistanceCommitted(std::numeric_limits<float>::quiet_NaN(),ETextCommit::OnEnter,true);TestEqual(TEXT("Non-finite numeric opening input is ignored"),OpeningRegionState(B),OriginalState);
    Panel->HandleFloorSlabCornerDistanceCommitted(31,ETextCommit::OnCleared,true);TestEqual(TEXT("Cancelled field does not edit"),OpeningRegionState(B),OriginalState);
   }
   const auto Before=OpeningRegionState(B);FEHBFinishRegionDrag Drag;Drag.Begin(S,0,1,Corner,Loop);if(!TestTrue(*Drag.Feedback.Message,Drag.bReady))return false;TestTrue(TEXT("Opening drag capture read only"),OpeningRegionState(B)==Before);TestEqual(TEXT("Unmoved opening drag no-op"),Drag.Execute(false).Message,FString(TEXT("NoChange")));
   Drag.Update(S->GetActorTransform().TransformVector(FVector(3,-3,0)));if(!TestTrue(*Drag.Feedback.Message,Drag.Feedback.bSucceeded))return false;TestTrue(TEXT("Opening drag previews without mutation"),OpeningRegionState(B)==Before);auto Cancelled=Drag;Cancelled.bCancelled=true;TestEqual(TEXT("Opening drag cancel"),Cancelled.Execute(false).Message,FString(TEXT("Cancelled")));TestTrue(TEXT("Cancel leaves source and meshes"),OpeningRegionState(B)==Before);
   EHBFinishRegionCommand::FailurePhase=2;TestEqual(TEXT("Opening drag contact rollback"),Drag.Execute(false).Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Opening drag failure keeps all state"),OpeningRegionState(B)==Before);
   const auto Applied=Drag.Execute(false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("Cut identity retained"),S->CutOperations[0].OperationGuid,Cut.OperationGuid);TestEqual(TEXT("Drag preserves cut center height"),S->CutOperations[0].Source.LocalTransform.GetLocation().Z,Cut.Source.LocalTransform.GetLocation().Z);TestEqual(TEXT("Drag preserves cut vertical scale"),S->CutOperations[0].Source.LocalTransform.GetScale3D().Z,Cut.Source.LocalTransform.GetScale3D().Z);TestEqual(TEXT("Drag preserves cut height"),S->CutOperations[0].Source.Height,Cut.Source.Height);const auto After=OpeningRegionState(B);TestTrue(TEXT("Opening drag changes geometry"),After!=Before);TestFalse(TEXT("Same captured drag becomes stale"),Drag.Execute(false).bSucceeded);GEditor->UndoTransaction();TestTrue(TEXT("Opening drag undo"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Opening drag redo"),OpeningRegionState(B)==After);GEditor->UndoTransaction();
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabCutterDraftTest,"EHB.Topology.SlabCutterDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabCutterDraftTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Retained:{false,true})for(auto Shape:{EEHBFloorSlabCutterShape::Square,EEHBFloorSlabCutterShape::Circle})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));A->VisualExpansion=10;A->RebuildSlabMesh();A->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);const auto Before=OpeningRegionState(B);FEHBSlabCutterDraft Draft;if(!TestTrue(TEXT("Cutter draft starts"),Draft.Begin(S,Shape)))return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B->FindElementActorByGuid(Setup.Slabs[1]),true,false);TestEqual(TEXT("Other slab selection rejected"),Draft.Execute(true).Message,FString(TEXT("TargetNotSelected")));GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);
  const auto CutId=Draft.Operation.OperationGuid;Draft.Cutter.Size=40;Draft.Update(S->GetActorTransform().TransformVector(FVector(5,7,0)),FRotator(0,15,0),FVector(0.1,0.2,0));if(!TestTrue(*Draft.Feedback.Message,Draft.Feedback.bSucceeded))return false;
  TestTrue(TEXT("Cutter transforms only draft"),OpeningRegionState(B)==Before&&S->PreviewCutters.IsEmpty());TestEqual(TEXT("Preview keeps operation identity"),Draft.Operation.OperationGuid,CutId);auto Cancelled=Draft;Cancelled.Cancel();TestFalse(TEXT("Cancelled cutter cannot commit"),Cancelled.Execute(false).bSucceeded);TestTrue(TEXT("Cancel preserves complete building"),OpeningRegionState(B)==Before);
  auto Invalid=Draft;Invalid.Cutter.Size=100000;TestFalse(TEXT("Complete erasure candidate rejected"),Invalid.Execute(true).bSucceeded);TestTrue(TEXT("Invalid candidate keeps building"),OpeningRegionState(B)==Before);Invalid.Cutter.Size=40;TestTrue(TEXT("Invalid placement remains correctable"),Invalid.Execute(true).bSucceeded);
  for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;const auto R=Draft.Execute(false);TestEqual(TEXT("Draft failure phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Draft apply rolled back"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Draft rollback all state"),OpeningRegionState(B)==Before);}
  const auto Applied=Draft.Execute(false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestTrue(TEXT("Draft commits only cut source"),S->PreviewCutters.IsEmpty()&&S->CutOperations.Num()==1);TestEqual(TEXT("Committed draft cut ID"),S->CutOperations[0].OperationGuid,CutId);TestTrue(TEXT("Committed transformed source"),S->CutOperations[0].Source.LocalTransform.Equals(Draft.Cutter.LocalTransform,0));TestTrue(TEXT("Committed slab source current"),S->IsRecordedOutlineUnchanged());const auto After=OpeningRegionState(B);TestFalse(TEXT("Committed draft revision stale"),Draft.Execute(false).bSucceeded);GEditor->UndoTransaction();TestTrue(TEXT("Cutter commit undo"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Cutter commit redo"),OpeningRegionState(B)==After);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningVertexCommandTest,"EHB.Topology.OpeningVertexCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningVertexCommandTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)for(bool Retained:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));A->VisualExpansion=10;A->RebuildSlabMesh();A->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));const auto Center=FBox(S->LocalTopPolygon).GetCenter();const auto HoleAdded=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,Center-FVector(50,0,0),30,30,0);if(!HoleAdded.bSucceeded)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);FEHBSlabCutterDraft Draft;if(!Draft.Begin(S,EEHBFloorSlabCutterShape::Square))return false;Draft.Cutter.Size=30;Draft.Update(S->GetActorTransform().TransformVector(FVector(50,0,0)),FRotator(0,13,0),FVector(0.1,0.2,0.5));if(!Draft.Execute(false).bSucceeded)return false;const auto OriginalCut=S->CutOperations[0];
  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)})
  {
   const auto Before=OpeningRegionState(B);auto Edit=[&](int32 A,int32 C,bool Insert,bool Preview=false){return EHBSlabOpeningEdit::Execute(S,Loop,A,C,Insert,Preview);};
   auto R=Edit(3,0,true,true);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Vertex preview read only"),OpeningRegionState(B)==Before);TestFalse(TEXT("Non-adjacent insertion rejected"),Edit(0,2,true).bSucceeded);TestFalse(TEXT("Invalid deletion rejected"),Edit(99,INDEX_NONE,false).bSucceeded);TestTrue(TEXT("Invalid handles preserve state"),OpeningRegionState(B)==Before);
   for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;R=Edit(3,0,true);TestEqual(TEXT("Vertex failure phase reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Vertex edit rollback"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Vertex rollback full state"),OpeningRegionState(B)==Before);}
   R=Edit(3,0,true);if(!TestTrue(*R.Message,R.bSucceeded))return false;TArray<FVector> Polygon;S->GetEditableLoopCopy(Loop,Polygon);TestEqual(TEXT("Closing edge insertion adds vertex"),Polygon.Num(),5);const auto Inserted=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Insert undo"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Insert redo"),OpeningRegionState(B)==Inserted);
   if(Loop<INDEX_NONE){const auto& Cut=S->CutOperations[0];TestEqual(TEXT("Vertex edit preserves operation ID"),Cut.OperationGuid,OriginalCut.OperationGuid);TestEqual(TEXT("Vertex edit preserves center Z"),Cut.Source.LocalTransform.GetLocation().Z,OriginalCut.Source.LocalTransform.GetLocation().Z);TestEqual(TEXT("Vertex edit preserves vertical scale"),Cut.Source.LocalTransform.GetScale3D().Z,OriginalCut.Source.LocalTransform.GetScale3D().Z);TestEqual(TEXT("Vertex edit preserves height"),Cut.Source.Height,OriginalCut.Source.Height);const auto Points=Cut.Source.ExplicitPolygon.Points;R=Edit(0,1,true);if(!TestTrue(*R.Message,R.bSucceeded))return false;for(const auto& Point:Points)TestTrue(TEXT("Existing point IDs survive second insertion"),S->CutOperations[0].Source.ExplicitPolygon.Points.ContainsByPredicate([&](const auto& V){return V.PointGuid==Point.PointGuid;}));R=Edit(1,INDEX_NONE,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
   R=Edit(0,INDEX_NONE,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;S->GetEditableLoopCopy(Loop,Polygon);TestEqual(TEXT("Remove inserted vertex returns quadrilateral"),Polygon.Num(),4);R=Edit(0,INDEX_NONE,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;S->GetEditableLoopCopy(Loop,Polygon);TestEqual(TEXT("Corner removal leaves triangle"),Polygon.Num(),3);const auto Triangle=OpeningRegionState(B);R=Edit(0,INDEX_NONE,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Last triangle deletes complete opening"),Loop==0?S->LocalHoles.IsEmpty():S->CutOperations.IsEmpty());const auto Deleted=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Whole opening deletion undo"),OpeningRegionState(B)==Triangle);GEditor->RedoTransaction();TestTrue(TEXT("Whole opening deletion redo"),OpeningRegionState(B)==Deleted);
  }
 }
 return true;
}
