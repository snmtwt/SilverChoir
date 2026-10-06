#include "EHBRoomFinishMove.h"
#include "Widgets/SEHBElementEditorPanel.h"
#include "EHBRoomFloorWallDrag.h"
#include "EHBWallOpeningCommand.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningControlsTest,"EHB.Surfaces.WallOpeningControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningControlsTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;};
 FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),1,true))return false;
 auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
 auto Panel=SNew(SEHBElementEditorPanel);Panel->SetSelectedActor(W);const double Area=WallOpeningSources128::MeshArea(W->LeftWallMeshComponent);
 Panel->ApplyWallOpeningRectangle(false);if(!TestEqual(TEXT("Panel creates opening"),W->CutOperations.Num(),1))return false;
 TestTrue(TEXT("Panel dimensions are centimeters"),FMath::Abs(Area-WallOpeningSources128::MeshArea(W->LeftWallMeshComponent)-12000)<0.1);
 const auto Source=OpeningRegionState(B);Panel->WallOpeningRectangle.Z=100000;Panel->ApplyWallOpeningRectangle(false);TestEqual(TEXT("Invalid UI rectangle never changes building"),OpeningRegionState(B),Source);
 FEHBRoomFloorWallDrag Drag;Drag.Capture(W);if(!TestTrue(TEXT("Actual viewport controller accepts panel-created opening"),Drag.Feedback.bSucceeded))return false;
 Drag.AddDelta(B->GetActorTransform().TransformVectorNoScale(FVector(10,0,0)));const auto R=Drag.Execute(false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
 TestTrue(TEXT("Viewport controller actually moves wall"),OpeningRegionState(B)!=Source);GEditor->UndoTransaction();TestEqual(TEXT("Viewport move undo"),OpeningRegionState(B),Source);
 Panel->ApplyWallOpeningRectangle(true);TestTrue(TEXT("Panel removes selected opening"),W->CutOperations.IsEmpty());GEditor->UndoTransaction();TestEqual(TEXT("Panel removal undo"),OpeningRegionState(B),Source);
 const auto First=W->CutOperations[0];
 Panel->WallOpeningRectangle=FVector4f(200,90,80,100);Panel->ApplyWallOpeningRectangle(false);
 if(!TestEqual(TEXT("Two independent openings"),W->CutOperations.Num(),2))return false;
 const auto Second=W->CutOperations[1];const auto Two=OpeningRegionState(B);
 TestTrue(TEXT("Existing opening selectable by stable identity"),Panel->SelectWallOpening(First.OperationGuid));
 TestTrue(TEXT("Selection restores actual dimensions"),Panel->WallOpeningRectangle==FVector4f(40,90,100,120));
 Panel->WallOpeningRectangle=FVector4f(45,80,110,125);
 FEHBWallOpeningCommand::FailurePhase=1;Panel->ApplyWallOpeningRectangle(false,true);
 TestEqual(TEXT("UI edit reaches command rollback"),FEHBWallOpeningCommand::FailurePhase,0);
 TestEqual(TEXT("Failed resize preserves both openings and finishes"),OpeningRegionState(B),Two);
 Panel->ApplyWallOpeningRectangle(false,true);
 const auto Resized=OpeningRegionState(B);
 TestTrue(TEXT("Apply edits real geometry"),Resized!=Two);
 TestTrue(TEXT("Actual wall cut area follows both rectangles"),FMath::Abs(Area-WallOpeningSources128::MeshArea(W->LeftWallMeshComponent)-(110*125+80*100))<0.1);
 TestEqual(TEXT("Editing preserves operation identity"),W->CutOperations[0].OperationGuid,First.OperationGuid);
 for(int32 I=0;I<4;++I)TestEqual(TEXT("Editing preserves point identity"),W->CutOperations[0].Source.ExplicitPolygon.Points[I].PointGuid,First.Source.ExplicitPolygon.Points[I].PointGuid);
 TestTrue(TEXT("Other opening unchanged"),FEHBCutOperation::StaticStruct()->CompareScriptStruct(&W->CutOperations[1],&Second,0));
 GEditor->UndoTransaction();TestEqual(TEXT("Resize undo"),OpeningRegionState(B),Two);
 // The panel still contains the edited draft; an undo must not allow it to
 // silently overwrite the now-restored operation without a fresh selection.
 Panel->ApplyWallOpeningRectangle(false,true);TestEqual(TEXT("Stale draft rejected after undo"),OpeningRegionState(B),Two);
 GEditor->RedoTransaction();TestEqual(TEXT("Resize redo"),OpeningRegionState(B),Resized);
 Panel->SelectWallOpening(First.OperationGuid);Panel->ApplyWallOpeningRectangle(true);
 if(!TestEqual(TEXT("Delete non-last opening only"),W->CutOperations.Num(),1))return false;
 TestEqual(TEXT("Second opening survives targeted deletion"),W->CutOperations[0].OperationGuid,Second.OperationGuid);
 GEditor->UndoTransaction();TestEqual(TEXT("Targeted deletion undo"),OpeningRegionState(B),Resized);
 Panel->SelectWallOpening(First.OperationGuid);Panel->WallOpeningRectangle.Z=std::numeric_limits<float>::quiet_NaN();Panel->ApplyWallOpeningRectangle(false,true);
 TestEqual(TEXT("Nonfinite draft is rejected"),OpeningRegionState(B),Resized);
 Panel->SetSelectedActor(B);Panel->SetSelectedActor(W);
 TestFalse(TEXT("Changing walls clears selected cut"),Panel->SelectedWallOpeningSource.IsSet());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeOpeningFinishTest,"EHB.Surfaces.NodeOpeningFinishPlan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeOpeningFinishTest::RunTest(const FString& Parameters)
{
 for(int32 Binding:{0,1,2})
 {
 FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Binding,true))return false;
 auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));FName Status;
 TArray<FEHBLogicalSurfaceDefinition> Hosts;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 W->CutOperations={SurfaceOpening129::Make(Hosts[0],40,W->Height-30)};W->RebuildWallMesh();
 for(FGuid Id:Setup.Floors){auto* F=CastChecked<AEHB_Floor>(B->FindElementActorByGuid(Id));FEHBBuildingClosedLoop Room;if(!F->TryGetRoomLoop(Room))return false;F->RefreshSurfaceFinishRelationsFromRoomLoop(Room);}
 const TSet<FGuid> Validated{W->ElementGuid};FEHBWallNodeModel Model;const auto Captured=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model,&Validated);if(!TestTrue(*Captured.Status.ToString(),Captured.bSucceeded))return false;
 const auto Elements=B->QueryElements(FEHBElementQuery());const auto Before=OpeningRegionState(B);
 EHBRoomFinishMove::FNodeEditPlan Plan;
 if(!TestTrue(*Status.ToString(),Plan.Prepare(B,Model,Model,Elements,{},Status)))return false;
 TestEqual(TEXT("One cut host has complete candidate evidence"),Plan.GetWallOpenings().Num(),1);
 TestEqual(TEXT("Both floor plans remain present"),Plan.Floors.Num(),2);TestEqual(TEXT("Both slab plans remain present"),Plan.Slabs.Num(),2);
 TArray<FEHBFloorSupportSurface> Actual;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(W,Actual,Status))return false;
 TestTrue(TEXT("No-move plan matches cut generated top"),EHBRoomFinishMove::SameTopCoverage(Plan.Tops.FindChecked(W->ElementGuid),Actual,Status));
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> Uncut;if(!EHBRoomFinishMove::BuildTopologyTops(Plan.GetCandidateSides(),Uncut,Status))return false;
 TestFalse(TEXT("Top opening never falls back to uncut host coverage"),EHBRoomFinishMove::SameTopCoverage(Plan.Tops.FindChecked(W->ElementGuid),Uncut.FindChecked(W->ElementGuid),Status));
 TestFalse(TEXT("Read-only opening plan cannot publish finishes before mesh/support transaction"),Plan.Apply(B,Status));TestEqual(TEXT("Explicit commit boundary"),Status,FName(TEXT("NodeOpeningCommitRequiresPlan")));
 auto Moved=Model;const FVector Delta(30,70,0);for(auto& N:Moved.Nodes)N.LocalTransform.AddToTranslation(Delta);
 if(!TestTrue(*Status.ToString(),Plan.Prepare(B,Model,Moved,Elements,{},Status)))return false;
 for(auto& Top:Actual){for(auto& V:Top.OuterPolygon)V+=Delta;for(auto& Hole:Top.Holes)for(auto& V:Hole.LocalPolygon)V+=Delta;}
 TestTrue(TEXT("Moved room plan consumes moved cut contacts"),EHBRoomFinishMove::SameTopCoverage(Plan.Tops.FindChecked(W->ElementGuid),Actual,Status));
 TestFalse(TEXT("Stale source model refuses"),Plan.Prepare(B,Moved,Moved,Elements,{},Status));TestTrue(TEXT("Failed plan clears all candidate state"),Plan.GetWallOpenings().IsEmpty()&&Plan.Tops.IsEmpty()&&Plan.Floors.IsEmpty()&&!Plan.GetCandidateSides().IsReady());
 TestEqual(TEXT("Planning and rejected apply never modify live building"),OpeningRegionState(B),Before);
 // Exercise the same command reached by wall dragging, including real editor
 // transactions. The cut wall separates two rooms with different finishes.
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
 const auto* Edge=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!Edge)return false;
 const auto* Start=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Edge->StartNodeGuid;});const auto* End=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Edge->EndNodeGuid;});if(!Start||!End)return false;
 auto Move=[&](bool Preview){return UEHBBuildingToolset::CommitWallMoveWithRoomFloors(B,W->ElementGuid,Start->LocalTransform.GetLocation(),End->LocalTransform.GetLocation(),FVector(25,0,0),Preview);};
 const auto Preview=Move(true);if(!TestTrue(*FString::Printf(TEXT("Opening wall move preview: %s"),*Preview.Message),Preview.bSucceeded))return false;
 const auto SourceSnapshot=OpeningRegionState(B);
 for(int32 Phase=1;Phase<=6;++Phase)
 {
  EHBNodeAuthorityEditing::FailurePhase=Phase;const auto R=Move(false);
  TestEqual(TEXT("Move reaches transaction fault"),EHBNodeAuthorityEditing::FailurePhase,0);TestEqual(TEXT("Failed opening move rolls back"),R.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Failure restores opening room and finish state"),OpeningRegionState(B),SourceSnapshot);
 }
 const auto Committed=Move(false);if(!TestTrue(*FString::Printf(TEXT("Opening wall move commit: %s"),*Committed.Message),Committed.bSucceeded))return false;
 const auto MovedSnapshot=OpeningRegionState(B);TestTrue(TEXT("Opening wall actually moved"),MovedSnapshot!=SourceSnapshot);
 TestTrue(TEXT("Undo move"),GEditor->UndoTransaction());TestEqual(TEXT("Undo complete opening move"),OpeningRegionState(B),SourceSnapshot);
 TestTrue(TEXT("Redo move"),GEditor->RedoTransaction());TestEqual(TEXT("Redo complete opening move"),OpeningRegionState(B),MovedSnapshot);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBNodeSupportMoveTest,"EHB.Surfaces.NodeSupportMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBNodeSupportMoveTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBNodeAuthorityEditing::FailurePhase=0;};
 for(bool Structural:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),1,true))return false;auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Slab=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(Params);Setup.Fixture.Actors.Add(Slab);
  auto Frame=W->GetElementLocalTransform();Frame.AddToTranslation(FVector(0,0,W->Height+20));Slab->ConfigureDefaultSlab(B,Frame,100,20,false);Slab->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);Slab->RoomFillFloorIndex=1;Slab->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
  FName Status;TArray<FEHBFloorSupportSurface> Tops;TArray<FEHBFloorFinishRegion> Bottom;FEHBFloorContact Contact;
  if(!FEHBFloorContactGeometry::CaptureHorizontalTops(W,Tops,Status)||!FEHBFloorContactGeometry::CaptureSlabBottom(Slab,Bottom,Status)||!FEHBFloorContactGeometry::Build(Bottom,Tops,Contact,Status)||!TestTrue(TEXT("Fixture has real support area"),Contact.Area>0))return false;
  FEHBElementRelation Relation;Relation.Type=Structural?EEHBElementRelationType::StructuralSupport:EEHBElementRelationType::PhysicalContact;Relation.bAffectsFloorAssignment=Structural;Relation.bGeometryDependent=true;Relation.Origin=EEHBRelationOrigin::UserAuthored;
  Relation.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::Top);Relation.Target=FEHBElementRelationEndpoint::MakeElement(Slab->ElementGuid,EEHBElementSurfaceKind::Bottom);Relation.ContactArea=Contact.Area;Relation.ContactPoint=Contact.Point;Relation.ContactNormal=FVector::UpVector;
  const FGuid Id=B->AddOrUpdateElementRelation(Relation);if(!Id.IsValid())return false;
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Model).bSucceeded)return false;
  const auto* Edge=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!Edge)return false;
  const auto* A=Model.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->StartNodeGuid;});const auto* Z=Model.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Edge->EndNodeGuid;});if(!A||!Z)return false;
  auto* Base=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(Params);Setup.Fixture.Actors.Add(Base);Base->ConfigureDefaultSlab(B,W->GetElementLocalTransform(),1500,20,false);Base->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);Base->RoomFillFloorIndex=1;Base->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
  const FGuid PillarId=B->FindPhysicalPillarForNode(A->NodeGuid);auto* P=Cast<AEHB_Pillar>(B->FindElementActorByGuid(PillarId));if(!TestNotNull(TEXT("Incoming support fixture has bound column"),P))return false;
  for(AEHBElementActorBase* Target:{static_cast<AEHBElementActorBase*>(W),static_cast<AEHBElementActorBase*>(P)})
  {
   Tops.Reset();Bottom.Reset();if(!FEHBFloorContactGeometry::CaptureHorizontalTops(Base,Tops,Status))return false;
   if(Target==W){FEHBPreparedWallOpening V;if(!W->PrepareSurfaceOpening({},V,Status))return false;Bottom=V.HorizontalBottoms;}
   else if(!FEHBFloorContactGeometry::CapturePillarBottom(P,Bottom,Status))return false;
   if(!FEHBFloorContactGeometry::Build(Bottom,Tops,Contact,Status)||!TestTrue(TEXT("Incoming contact exists"),Contact.Area>0))return false;
   auto Incoming=Relation;Incoming.RelationGuid=FGuid::NewGuid();Incoming.Source=FEHBElementRelationEndpoint::MakeElement(Base->ElementGuid,EEHBElementSurfaceKind::Top);Incoming.Target=FEHBElementRelationEndpoint::MakeElement(Target->ElementGuid,EEHBElementSurfaceKind::Bottom);Incoming.ContactArea=Contact.Area;Incoming.ContactPoint=Contact.Point;if(!B->AddOrUpdateElementRelation(Incoming).IsValid())return false;
  }
  auto Move=[&](double X,bool Preview=false){return UEHBBuildingToolset::CommitWallMoveWithRoomFloors(B,W->ElementGuid,A->LocalTransform.GetLocation(),Z->LocalTransform.GetLocation(),FVector(X,0,0),Preview);};
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=OpeningRegionState(B);const auto Preview=Move(25,true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestEqual(TEXT("Support preview unchanged"),OpeningRegionState(B),Before);
  EHBNodeAuthorityEditing::FailurePhase=6;const auto Failed=Move(25);TestEqual(TEXT("Support fault reached"),EHBNodeAuthorityEditing::FailurePhase,0);TestEqual(TEXT("Support transaction rolled back"),Failed.Message,FString(TEXT("NodeEditFailedRolledBack")));TestEqual(TEXT("Support failure restores original"),OpeningRegionState(B),Before);
  const auto R=Move(25);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  const auto* Changed=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;});if(!TestNotNull(TEXT("Contact keeps identity"),Changed))return false;
  Tops.Reset();Bottom.Reset();if(!FEHBFloorContactGeometry::CaptureHorizontalTops(W,Tops,Status)||!FEHBFloorContactGeometry::CaptureSlabBottom(Slab,Bottom,Status)||!FEHBFloorContactGeometry::Build(Bottom,Tops,Contact,Status))return false;
  TestTrue(TEXT("Published contact agrees with both actual moved surfaces"),FMath::Abs(Changed->ContactArea-Contact.Area)<0.01&&Changed->ContactPoint.Equals(Contact.Point,0.001));const auto After=OpeningRegionState(B);
  GEditor->UndoTransaction();TestEqual(TEXT("Support undo"),OpeningRegionState(B),Before);GEditor->RedoTransaction();TestEqual(TEXT("Support redo"),OpeningRegionState(B),After);GEditor->UndoTransaction();
  const auto Lost=Move(150);if(!TestTrue(*Lost.Message,Lost.bSucceeded))return false;TestFalse(TEXT("Non-contact no longer counts as support"),B->ElementRelations.ContainsByPredicate([&](const auto& V){return V.RelationGuid==Id;}));GEditor->UndoTransaction();TestEqual(TEXT("Lost support undo restores source"),OpeningRegionState(B),Before);
  if(Structural)
  {
   Slab->SetAutomaticFloorAssignment();const auto Automatic=OpeningRegionState(B);const auto Rejected=Move(150,true);
   TestFalse(TEXT("Loss requiring floor migration cannot partially move rooms"),Rejected.bSucceeded);TestEqual(TEXT("Explicit floor migration boundary"),Rejected.Message,FString(TEXT("NodeSupportFloorMigrationRequired")));TestEqual(TEXT("Rejected floor migration leaves whole source"),OpeningRegionState(B),Automatic);
  }
 }
 return true;
}
