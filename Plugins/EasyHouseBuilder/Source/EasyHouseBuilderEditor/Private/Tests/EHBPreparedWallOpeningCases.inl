#include "Core/EHBPreparedWallOpening.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include <limits>
#include "Core/EHBFloorAssignmentPlan.h"
#include "Core/EHBWallOpeningMeasure.h"
#include "Core/EHBWallSurfaceHosts.h"
#include "EHBStraightWallCandidateMeshCases.inl"
#include "EHBWallOpeningCandidateCases.inl"
#include "EHBNodeOpeningFinishCases.inl"
#include "EHBNodeDoorWindowMoveCases.inl"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallCandidateHostsTest,"EHB.Surfaces.WallCandidateHosts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallCandidateHostsTest::RunTest(const FString& Parameters)
{
 FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),2,true))return false;auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
 FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;FName Status;auto Draft=FEHBWallNodeSideDraft::Build(Model,Status);if(!TestTrue(*Status.ToString(),Draft.IsReady()))return false;
 FEHBWallSurfaceHostSource Source;Source.BuildingGuid=B->BuildingGuid;Source.LeftSurfaceGuid=W->FindLogicalSurfaceIdentity(TEXT("Wall.Left"));Source.RightSurfaceGuid=W->FindLogicalSurfaceIdentity(TEXT("Wall.Right"));Source.FloorIndex=W->FloorIndex;Source.Height=W->Height;Source.Thickness=W->Thickness;Source.bStructural=W->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural));
 const auto* Sides=Draft.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==W->ElementGuid;});if(!Sides)return false;Source.Sides=*Sides;
 TArray<FEHBLogicalSurfaceDefinition> Actual,Original,Candidate;if(!W->QueryLogicalBaseSurfaces(Actual,Status)||!FEHBWallSurfaceHosts::Build(Source,Original,Status))return false;
 for(int32 I=0;I<2;++I){TestEqual(TEXT("Value host preserves semantic identity"),Original[I].SurfaceGuid,Actual[I].SurfaceGuid);TestTrue(TEXT("Value host matches live chart"),Original[I].PlaneToBuilding.Equals(Actual[I].PlaneToBuilding,1.e-6));TestTrue(TEXT("Value host matches live area"),FMath::Abs(Original[I].GetAreaCm2()-Actual[I].GetAreaCm2())<0.01);}
 const auto Before=OpeningRegionState(B);const FVector Delta(250,70,0);auto Moved=Model;for(auto& Node:Moved.Nodes)Node.LocalTransform.AddToTranslation(Delta);auto MovedDraft=FEHBWallNodeSideDraft::Build(Moved,Status);if(!TestTrue(*Status.ToString(),MovedDraft.IsReady()))return false;
 auto MovedSource=Source;const auto* MovedSides=MovedDraft.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==W->ElementGuid;});if(!MovedSides)return false;MovedSource.Sides=*MovedSides;if(!FEHBWallSurfaceHosts::Build(MovedSource,Candidate,Status))return false;
 for(int32 I=0;I<2;++I)
 {
  const auto Cut=SurfaceOpening129::Make(Original[I],40,60);TArray<FVector> OldPoints,NewPoints;
  if(!TestTrue(TEXT("Current host opening resolves"),FEHBSurfaceOpening::Resolve(Cut,Original[I],FTransform::Identity,OldPoints,Status))||!TestTrue(TEXT("Moved candidate host opening resolves"),FEHBSurfaceOpening::Resolve(Cut,Candidate[I],FTransform::Identity,NewPoints,Status)))return false;
  TestEqual(TEXT("Moved candidate preserves point count"),NewPoints.Num(),OldPoints.Num());for(int32 P=0;P<OldPoints.Num();++P)TestTrue(TEXT("Opening follows candidate without moving actor"),NewPoints[P].Equals(OldPoints[P]+Delta,0.001));
 }
 TestEqual(TEXT("Candidate host planning never writes live building"),OpeningRegionState(B),Before);
 auto Short=Source;const auto Along=(Short.Sides.EndRight-Short.Sides.StartRight).GetSafeNormal();Short.Sides.EndRight=Short.Sides.StartRight+Along*50;
 if(!TestTrue(TEXT("Short candidate host is geometrically valid"),FEHBWallSurfaceHosts::Build(Short,Candidate,Status)))return false;TArray<FVector> Rejected;const auto Cut=SurfaceOpening129::Make(Original[1],40,60);TestFalse(TEXT("Opening cannot survive shortening outside host"),FEHBSurfaceOpening::Resolve(Cut,Candidate[1],FTransform::Identity,Rejected,Status));TestTrue(TEXT("Failed candidate opening leaves no points"),Rejected.IsEmpty());
 auto Invalid=Source;Invalid.Sides.EndRight.X=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Invalid second side fails whole candidate"),FEHBWallSurfaceHosts::Build(Invalid,Candidate,Status));TestTrue(TEXT("No partial first-side publication"),Candidate.IsEmpty());
 Invalid=Source;Invalid.RightSurfaceGuid=Invalid.LeftSurfaceGuid;TestFalse(TEXT("Ambiguous candidate host identity refuses"),FEHBWallSurfaceHosts::Build(Invalid,Candidate,Status));TestTrue(TEXT("Ambiguous identity output empty"),Candidate.IsEmpty());
 Invalid=Source;Invalid.Sides.LocalTransform.SetScale3D(FVector(2,1,1));TestFalse(TEXT("Scaled candidate host refuses"),FEHBWallSurfaceHosts::Build(Invalid,Candidate,Status));TestEqual(TEXT("All failures preserve live state"),OpeningRegionState(B),Before);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBoundWallCopyTest,"EHB.Surfaces.BoundWallCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBoundWallCopyTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 ON_SCOPE_EXIT{EHBBuildingCopy::FailAfterImport=false;EHBBuildingCopy::FailAfterApply=false;GEditor->SelectNone(false,true,false);};
 for(int32 Mode:{0,2})for(int32 Side:{0,1})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=2)return false;
  auto Cut=SurfaceOpening129::Make(Hosts[Side],Side==0?0:80,Side==0?0:60);GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
  auto R=UEHBBuildingToolset::SetWallOpenings(W,{Cut},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));if(!TestTrue(*R.Message,R.bSucceeded))return false;
  const auto Original=Source(W),Before=OpeningRegionState(B);const double Left=MeshArea(W->LeftWallMeshComponent),Right=MeshArea(W->RightWallMeshComponent),Caps=MeshArea(W->CapMeshComponent);
  auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(B->GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};const int32 CountBefore=Count();
  for(bool Applied:{false,true}){if(Applied)EHBBuildingCopy::FailAfterApply=true;else EHBBuildingCopy::FailAfterImport=true;const auto Failed=EHBBuildingCopy::Execute(B,FVector(2500,500,0));TestEqual(TEXT("Bound wall copy failure rollback"),Failed.Status,FName(TEXT("BuildingCopyFailedRolledBack")));TestEqual(TEXT("Bound wall copy leaks no actors"),Count(),CountBefore);TestEqual(TEXT("Bound wall copy failure source"),Source(W),Original);TestEqual(TEXT("Bound wall copy failure building"),OpeningRegionState(B),Before);TestFalse(TEXT("No failed wall copy redo"),GEditor->Trans->CanRedo());}
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(2500,500,0));if(!TestTrue(*FString::Printf(TEXT("Wall copy %s/%s"),*Copy.Status.ToString(),*Copy.FailureReason.ToString()),Copy.bSucceeded))return false;Setup.Fixture.Actors.Append(Copy.Actors);
  auto* Target=CastChecked<AEHB_Wall>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(W->ElementGuid)));const auto& Host=Target->CutOperations[0].SurfaceHost;
  TestEqual(TEXT("Copied wall binding building"),Host.BuildingGuid,Copy.Building->BuildingGuid);TestEqual(TEXT("Copied wall binding element"),Host.ElementGuid,Target->ElementGuid);TestEqual(TEXT("Copied wall binding side"),Host.SurfaceGuid,Target->FindLogicalSurfaceIdentity(Side==0?TEXT("Wall.Left"):TEXT("Wall.Right")));TestNotEqual(TEXT("Wall copied surface independent"),Host.SurfaceGuid,Cut.SurfaceHost.SurfaceGuid);
  auto CheckMesh=[&](){TestTrue(TEXT("Copied left actual area"),FMath::Abs(MeshArea(Target->LeftWallMeshComponent)-Left)<0.02);TestTrue(TEXT("Copied right actual area"),FMath::Abs(MeshArea(Target->RightWallMeshComponent)-Right)<0.02);TestTrue(TEXT("Copied actual caps and reveals"),FMath::Abs(MeshArea(Target->CapMeshComponent)-Caps)<0.02);};CheckMesh();
  const auto Mapped=Source(Target);TestTrue(TEXT("Undo wall copy"),GEditor->UndoTransaction());TestEqual(TEXT("Undo removes wall copy group"),Count(),CountBefore);TestTrue(TEXT("Redo wall copy"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores wall bound source"),Source(Target),Mapped);CheckMesh();
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Target,true,false);R=UEHBBuildingToolset::SetWallOpenings(Target,{},Copy.Building->RelationshipGraphRevision,Copy.Building->GetElementGeometryRevision(Target->ElementGuid));if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Copied wall opening removed"),Target->CutOperations.IsEmpty());TestEqual(TEXT("Original wall opening unaffected"),Source(W),Original);TestEqual(TEXT("Original building unaffected"),OpeningRegionState(B),Before);TestTrue(TEXT("Undo copied wall edit"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores mapped wall source"),Source(Target),Mapped);CheckMesh();
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBoundSlabCopyTest,"EHB.Surfaces.BoundSlabCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBoundSlabCopyTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBBuildingCopy::FailAfterImport=false;EHBBuildingCopy::FailAfterApply=false;GEditor->SelectNone(false,true,false);};
 for(bool Retained:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),2,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* Slab=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));Slab->VisualExpansion=0;Slab->RebuildSlabMesh();Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!S->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=1)return false;
  const FVector C=FBox(S->LocalTopPolygon).GetCenter();auto Cut=SurfaceOpening129::Make(Hosts[0],C.X-10,C.Y-15);Cut.Source.Height=S->Thickness;
  auto R=UEHBBuildingToolset::SetFloorSlabOpenings(S,S->LocalHoles,{Cut},B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  const auto Before=OpeningRegionState(B);auto Count=[&](){int32 N=0;for(TActorIterator<AActor> It(B->GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};const int32 BeforeCount=Count();
  for(bool AfterApply:{false,true})
  {
   if(AfterApply)EHBBuildingCopy::FailAfterApply=true;else EHBBuildingCopy::FailAfterImport=true;
   const auto Failed=EHBBuildingCopy::Execute(B,FVector(2500,500,0));TestEqual(TEXT("Bound copy failure rolls back"),Failed.Status,FName(TEXT("BuildingCopyFailedRolledBack")));TestEqual(TEXT("No leaked copied actors"),Count(),BeforeCount);TestEqual(TEXT("Failure preserves complete source"),OpeningRegionState(B),Before);TestFalse(TEXT("Failed copy cannot redo"),GEditor->Trans->CanRedo());
  }
  const auto Copied=EHBBuildingCopy::Execute(B,FVector(2500,500,0));if(!TestTrue(*FString::Printf(TEXT("%s/%s"),*Copied.Status.ToString(),*Copied.FailureReason.ToString()),Copied.bSucceeded))return false;
  ON_SCOPE_EXIT{for(auto* Actor:Copied.Actors)if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed())Actor->Destroy();};
  auto* Target=CastChecked<AEHB_FloorSlab>(Copied.Building->FindElementActorByGuid(Copied.IdentityDraft.ElementGuids.FindChecked(S->ElementGuid)));
  const auto& Mapped=Target->CutOperations[0];TestEqual(TEXT("Bound building mapped"),Mapped.SurfaceHost.BuildingGuid,Copied.Building->BuildingGuid);TestEqual(TEXT("Bound element mapped"),Mapped.SurfaceHost.ElementGuid,Target->ElementGuid);TestEqual(TEXT("Bound surface mapped"),Mapped.SurfaceHost.SurfaceGuid,Target->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")));TestNotEqual(TEXT("New surface is independent"),Mapped.SurfaceHost.SurfaceGuid,Cut.SurfaceHost.SurfaceGuid);TestEqual(TEXT("Local operation identity retained"),Mapped.OperationGuid,Cut.OperationGuid);
  TestTrue(TEXT("Copied partition and source agree"),Target->ValidatePartitionedSlabState(Target->LocalTopPolygon,Target->LocalHoles,Target->CutOperations,Target->DisplayPartition));TestTrue(TEXT("Copied provenance current"),Target->IsRecordedOutlineUnchanged());TestEqual(TEXT("Successful copy preserves original"),OpeningRegionState(B),Before);
  const auto Applied=OpeningRegionState(Copied.Building);TestTrue(TEXT("Undo bound copy"),GEditor->UndoTransaction());TestEqual(TEXT("Undo entire group"),Count(),BeforeCount);TestTrue(TEXT("Redo bound copy"),GEditor->RedoTransaction());TestEqual(TEXT("Redo exact bound copy"),OpeningRegionState(Copied.Building),Applied);
  for(auto* E:Copied.Building->QueryElements(FEHBElementQuery()))if(auto* A=Cast<AEHB_FloorSlab>(E)){TArray<FEHBLogicalSurfaceRegion> Regions;FName Why;const bool Ok=A->BuildCandidateDisplayRegions(A->LocalTopPolygon,A->LocalHoles,A==Target?TArray<FEHBCutOperation>{}:A->CutOperations,Regions,Why);if(!Ok)AddError(FString::Printf(TEXT("Copied candidate %s target=%d mesh=%s thickness=%f expansion=%f status=%s"),*A->GetName(),A==Target,*GetNameSafe(A->MeshComponent),A->Thickness,A->VisualExpansion,*Why.ToString()));}
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Target,true,false);R=UEHBBuildingToolset::SetFloorSlabOpenings(Target,Target->LocalHoles,{},Copied.Building->RelationshipGraphRevision,Copied.Building->GetElementGeometryRevision(Target->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestEqual(TEXT("Editing copy preserves original opening"),OpeningRegionState(B),Before);TestTrue(TEXT("Undo copied source deletion"),GEditor->UndoTransaction());TestEqual(TEXT("Undo copied edit restores source and mesh"),OpeningRegionState(Copied.Building),Applied);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBoundSlabCommandTest,"EHB.Surfaces.BoundSlabOpeningCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBoundSlabCommandTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(bool Retained:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),2,true))return false;auto* B=Setup.Building();
  for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=0;S->RebuildSlabMesh();S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  if(Retained){const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!S->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=1)return false;
  const FVector C=FBox(S->LocalTopPolygon).GetCenter();auto Cut=SurfaceOpening129::Make(Hosts[0],C.X-10,C.Y-15);Cut.Source.Height=S->Thickness;
  const auto Before=OpeningRegionState(B);auto Edit=[&](const TArray<FEHBCutOperation>& Cuts,bool Preview=false){return UEHBBuildingToolset::SetFloorSlabOpenings(S,S->LocalHoles,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Preview);};
  auto R=Edit({Cut},true);if(!TestTrue(*R.Message,R.bSucceeded))return false;TestEqual(TEXT("Bound slab command preview read only"),OpeningRegionState(B),Before);
  for(int32 Phase=1;Phase<=4;++Phase){EHBFinishRegionCommand::FailurePhase=Phase;R=Edit({Cut});TestEqual(TEXT("Bound slab command rolls back"),R.Message,FString(TEXT("RegionEditFailedRolledBack")));TestEqual(TEXT("Bound slab rollback preserves full region state"),OpeningRegionState(B),Before);}
  R=Edit({Cut});if(!TestTrue(*R.Message,R.bSucceeded))return false;TestEqual(TEXT("Command preserves surface ID"),S->CutOperations[0].SurfaceHost.SurfaceGuid,Cut.SurfaceHost.SurfaceGuid);TestTrue(TEXT("Command updates source provenance"),S->IsRecordedOutlineUnchanged());const auto Applied=OpeningRegionState(B);
  TestTrue(TEXT("Bound slab command undo"),GEditor->UndoTransaction());TestEqual(TEXT("Command undo complete state"),OpeningRegionState(B),Before);TestTrue(TEXT("Bound slab command redo"),GEditor->RedoTransaction());TestEqual(TEXT("Command redo complete state"),OpeningRegionState(B),Applied);
  R=Edit({});if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Deleting bound source via command"),S->CutOperations.IsEmpty());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBBoundSlabOpeningTest,"EHB.Surfaces.BoundSlabOpening",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBBoundSlabOpeningTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* S=World->SpawnActor<AEHB_FloorSlab>(Params);ON_SCOPE_EXIT{S->Destroy();B->Destroy();};
 S->ConfigureDefaultSlab(B,FTransform(FRotator(25,35,0),FVector(200,100,300)),100,10,false);S->VisualExpansion=0;S->RebuildSlabMesh();
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!S->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=1)return false;
 auto Cut=SurfaceOpening129::Make(Hosts[0],-20,-15);
 auto TopArea=[&](){double Area=0;const auto* M=S->MeshComponent->GetProcMeshSection(0);if(!M)return -1.0;for(int32 I=0;I<M->ProcIndexBuffer.Num();I+=3){const auto& A=M->ProcVertexBuffer[M->ProcIndexBuffer[I]];const auto& Bv=M->ProcVertexBuffer[M->ProcIndexBuffer[I+1]];const auto& C=M->ProcVertexBuffer[M->ProcIndexBuffer[I+2]];if(A.Normal.Z>0.99&&Bv.Normal.Z>0.99&&C.Normal.Z>0.99)Area+=FVector::CrossProduct(Bv.Position-A.Position,C.Position-A.Position).Size()*0.5;}return Area;};
 auto Source=[&](){FString Text;for(const auto& C:S->CutOperations){FString Part;FJsonObjectConverter::UStructToJsonObjectString(C,Part);Text+=Part;}return Text;};
 TestTrue(TEXT("Tilted slab initial local top area"),FMath::Abs(TopArea()-10000)<0.01);TestTrue(TEXT("Bound slab preview"),S->ValidateCutOperations({Cut}));TestTrue(TEXT("Preview retains source"),S->CutOperations.IsEmpty());
 {FScopedTransaction T(NSLOCTEXT("EHBTests","BoundSlabOpening","Bound slab opening"));if(!TestTrue(TEXT("Actual tilted slab consumes bound opening"),S->AddCutOperation(Cut)))return false;}
 TestTrue(TEXT("Actual top loses600 square centimeters"),FMath::Abs(TopArea()-9400)<0.01);TestEqual(TEXT("Authored surface binding preserved"),S->CutOperations[0].SurfaceHost.SurfaceGuid,Cut.SurfaceHost.SurfaceGuid);TestEqual(TEXT("Authored stage is not legacy XY"),S->CutOperations[0].Stage,EEHBCutStage::SurfaceOpening);
 const auto Before=Source();auto Wrong=Cut;Wrong.SurfaceHost.SurfaceGuid=FGuid::NewGuid();TestFalse(TEXT("Wrong slab surface cannot update"),S->UpdateCutOperation(Wrong));TestEqual(TEXT("Failed host update preserves author source"),Source(),Before);
 const TArray<FVector> Small{{-50,-50,0},{-30,-50,0},{-30,50,0},{-50,50,0}};TestFalse(TEXT("Candidate outline must still contain bound source"),S->SetSlabOutline(Small,{}));TestTrue(TEXT("Failed outline retains cut geometry"),FMath::Abs(TopArea()-9400)<0.01);
 TestTrue(TEXT("Bound slab undo"),GEditor->UndoTransaction());TestTrue(TEXT("Undo restores solid top"),S->CutOperations.IsEmpty()&&FMath::Abs(TopArea()-10000)<0.01);TestTrue(TEXT("Bound slab redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo original author contract"),Source(),Before);
 {FScopedTransaction T(NSLOCTEXT("EHBTests","DeleteBoundSlabOpening","Delete bound slab opening"));TestTrue(TEXT("Deleting source restores slab"),S->RemoveCutOperation(Cut.OperationGuid));}TestTrue(TEXT("Delete restores full top"),FMath::Abs(TopArea()-10000)<0.01);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPreparedEndOpeningTest,"EHB.Surfaces.PreparedEndOpening",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPreparedEndOpeningTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);W->Destroy();B->Destroy();};
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 const double OriginalCapArea=WallOpeningSources128::MeshArea(W->CapMeshComponent);
 for(int32 Side=0;Side<2;++Side)for(bool End:{false,true})
 {
  const auto Cut=SurfaceOpening129::Make(Hosts[Side],End?480:0,60);FEHBPreparedWallOpening Candidate;
  if(!TestTrue(*Status.ToString(),W->PrepareSurfaceOpening({Cut},Candidate,Status)))return false;
  auto Area=[](const FEHBWallJunctionMesh& M){double Sum=0;for(int32 I=0;I<M.Triangles.Num();I+=3)Sum+=FVector::CrossProduct(M.Vertices[M.Triangles[I+1]]-M.Vertices[M.Triangles[I]],M.Vertices[M.Triangles[I+2]]-M.Vertices[M.Triangles[I]]).Size()*0.5;return Sum;};
  TestTrue(TEXT("End cut removes600 from both sides"),FMath::Abs(Area(Candidate.Left)-(500*260-600))<0.01&&FMath::Abs(Area(Candidate.Right)-(500*260-600))<0.01);
  TestTrue(TEXT("End exit absent and remaining reveal present"),FMath::Abs(Area(Candidate.Caps)-(OriginalCapArea-30*24+2*70*24))<0.02);
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
  const auto R=UEHBBuildingToolset::SetWallOpenings(W,{Cut},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));if(!TestTrue(*R.Message,R.bSucceeded))return false;
  TestTrue(TEXT("End command actual caps match prepared"),FMath::Abs(WallOpeningSources128::MeshArea(W->CapMeshComponent)-Area(Candidate.Caps))<0.02);
  TestTrue(TEXT("End command undo"),GEditor->UndoTransaction());TestTrue(TEXT("Undo clears authored end opening"),W->CutOperations.IsEmpty());TestTrue(TEXT("End command redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo restores opening identity"),W->CutOperations[0].OperationGuid,Cut.OperationGuid);GEditor->UndoTransaction();
 }
 auto Bottom=SurfaceOpening129::Make(Hosts[0],200,0);FEHBPreparedWallOpening Boundary;TestTrue(TEXT("Height boundary provides candidate contact plan"),W->PrepareSurfaceOpening({Bottom},Boundary,Status));TestTrue(TEXT("Height boundary is flagged for dependency checks"),Boundary.bTouchesHeightBoundary);
 auto* Pillar=World->SpawnActor<AEHB_Pillar>(Params);ON_SCOPE_EXIT{Pillar->Destroy();};Pillar->AttachToBuilding(B,FTransform(FVector(-250,0,300)));W->StartPillarGuid=Pillar->ElementGuid;W->bGenerateLinkedPillarEndCaps=true;
 W->SetPillarConnectionFacePoints(Pillar,W->GetElementLocalTransform().TransformPosition(FVector(-230,12,0)),W->GetElementLocalTransform().TransformPosition(FVector(-270,-12,0)));W->RebuildWallMesh();
 if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;const auto& Host=Hosts[0].GetAreaCm2()>Hosts[1].GetAreaCm2()?Hosts[0]:Hosts[1];
 const FVector Chart=Host.PlaneToBuilding.InverseTransformPosition(W->GetElementLocalTransform().TransformPosition(FVector(-260,0,95)));
 const auto Cut=SurfaceOpening129::Make(Host,Chart.X-10,Chart.Y-15);const double BaseCap=WallOpeningSources128::MeshArea(W->CapMeshComponent),BaseSides=WallOpeningSources128::MeshArea(W->LeftWallMeshComponent)+WallOpeningSources128::MeshArea(W->RightWallMeshComponent);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);const auto Miter=UEHBBuildingToolset::SetWallOpenings(W,{Cut},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));if(!TestTrue(*Miter.Message,Miter.bSucceeded))return false;
 TestTrue(TEXT("Miter cut reaches only longer side"),FMath::Abs(WallOpeningSources128::MeshArea(W->LeftWallMeshComponent)+WallOpeningSources128::MeshArea(W->RightWallMeshComponent)-BaseSides+600)<0.02);
 TestTrue(TEXT("Miter command clips cap and adds partial thickness reveals"),FMath::Abs(WallOpeningSources128::MeshArea(W->CapMeshComponent)-BaseCap+FMath::Sqrt(40.0*40+24.0*24)*15-1200)<0.03);
 TestTrue(TEXT("Miter opening undo"),GEditor->UndoTransaction());TestTrue(TEXT("Miter undo cap area"),FMath::Abs(WallOpeningSources128::MeshArea(W->CapMeshComponent)-BaseCap)<0.02);TestTrue(TEXT("Miter opening redo"),GEditor->RedoTransaction());TestEqual(TEXT("Miter redo source identity"),W->CutOperations[0].OperationGuid,Cut.OperationGuid);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRevealMeasureTest,"EHB.Surfaces.RevealAreaMeasure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRevealMeasureTest::RunTest(const FString& Parameters)
{
 FEHBWallOpeningMeasureDomain D;D.EndLeft=D.EndRight=100;D.Height=100;D.Thickness=10;double Area=0;FName Status;
 auto Rect=[](double X,double Z,double W,double H){return TArray<FVector2d>{{X,Z},{X+W,Z},{X+W,Z+H},{X,Z+H}};};
 auto Check=[&](TArray<FVector2d> Loop,double Expected){for(int32 Order=0;Order<2;++Order){if(!TestTrue(TEXT("Reveal strip integral succeeds"),FEHBWallOpeningMeasure::RevealArea(D,{Loop},Area,Status)))return false;TestTrue(*FString::Printf(TEXT("Analytic reveal area %.3f matches %.3f"),Area,Expected),FMath::Abs(Area-Expected)<0.000001);Algo::Reverse(Loop);}return true;};
 Check(Rect(20,20,20,30),1000);Check(Rect(20,0,20,30),800);Check(Rect(20,-10,20,120),2000);Check(Rect(0,20,20,30),700);Check(Rect(-20,20,20,30),0);
 D.StartLeft=10;Check(Rect(0,20,5,30),175);
 TestTrue(TEXT("No opening boundaries have zero reveal"),FEHBWallOpeningMeasure::RevealArea(D,{},Area,Status));TestEqual(TEXT("Empty area"),Area,0.0);
 D.Height=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Invalid domain refused"),FEHBWallOpeningMeasure::RevealArea(D,{Rect(20,20,20,30)},Area,Status));TestEqual(TEXT("Failure clears result"),Area,0.0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPillarBottomContactTest,"EHB.Surfaces.PillarBottomContactSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPillarBottomContactTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* P=World->SpawnActor<AEHB_Pillar>(Params);ON_SCOPE_EXIT{P->Destroy();B->Destroy();};
 B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(900,-700,50)));P->AttachToBuilding(B,FTransform::Identity);
 const FTransform Local(FRotator(0,23,0),FVector(100,200,330),FVector(2,3,1));P->ConfigureAsPolygonPillar(300,20,30,Local);P->PillarMeshComponent->ClearAllMeshSections();
 TArray<FEHBFloorFinishRegion> Bottom;FName Status;if(!TestTrue(TEXT("Column bottom comes from structural source"),FEHBFloorContactGeometry::CapturePillarBottom(P,Bottom,Status)))return false;
 FEHBFloorSupportSurface Top;Top.OuterPolygon={{-1000,-1000,330},{1000,-1000,330},{1000,1000,330},{-1000,1000,330}};double Area=0;
 TestTrue(TEXT("Structural column bottom contact solves"),FEHBFloorContactGeometry::MeasureArea(Bottom,{Top},Area,Status));TestTrue(TEXT("Rotated scaled bottom area"),FMath::Abs(Area-3600)<0.2);for(const auto& R:Bottom)for(const auto& V:R.OuterPolygon)TestTrue(TEXT("Actual underside elevation"),FMath::Abs(V.Z-330)<0.001);
 P->ShapeType=EEHBPillarShapeType::StaticMesh;TestFalse(TEXT("Sampled column bottom needs its own domain"),FEHBFloorContactGeometry::CapturePillarBottom(P,Bottom,Status));TestTrue(TEXT("Unsupported domain clears output"),Bottom.IsEmpty());P->ShapeType=EEHBPillarShapeType::Polygon;
 P->SetElementLocalTransform(FTransform(FRotator(10,23,0),FVector(100,200,330)));TestFalse(TEXT("Tilted column bottom refuses planar solve"),FEHBFloorContactGeometry::CapturePillarBottom(P,Bottom,Status));P->SetElementLocalTransform(Local);
 P->Width=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Invalid column source refuses"),FEHBFloorContactGeometry::CapturePillarBottom(P,Bottom,Status));TestTrue(TEXT("Invalid source exposes no old footprint"),Bottom.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFloorValuePlanTest,"EHB.Surfaces.FloorAssignmentValuePlan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFloorValuePlanTest::RunTest(const FString& Parameters)
{
 FEHBFloorAssignmentState A;A.ElementGuid=FGuid::NewGuid();A.Type=EEHBBuildingElementType::Pillar;A.FloorIndex=1;A.Role=EEHBBuildingFloorElementRole::FloorBody;A.Policy=EEHBFloorAssignmentPolicy::Explicit;A.Source=EEHBFloorAssignmentSource::Explicit;
 auto B=A;B.ElementGuid=FGuid::NewGuid();B.FloorIndex=3;
 FEHBFloorAssignmentState Slab;Slab.ElementGuid=FGuid::NewGuid();Slab.Type=EEHBBuildingElementType::FoundationAndFloor;
 FEHBFloorAssignmentState Upper;Upper.ElementGuid=FGuid::NewGuid();Upper.Type=EEHBBuildingElementType::Pillar;
 auto Edge=[](FGuid From,FGuid To){FEHBElementRelation R;R.RelationGuid=FGuid::NewGuid();R.Type=EEHBElementRelationType::StructuralSupport;R.Source=FEHBElementRelationEndpoint::MakeElement(From);R.Target=FEHBElementRelationEndpoint::MakeElement(To);R.bAffectsFloorAssignment=true;return R;};
 TArray<FEHBFloorAssignmentState> Input{Upper,B,Slab,A},Output;TArray<FEHBElementRelation> Relations{Edge(A.ElementGuid,Slab.ElementGuid),Edge(B.ElementGuid,Slab.ElementGuid),Edge(Slab.ElementGuid,Upper.ElementGuid)};FName Status;
 if(!TestTrue(TEXT("Pure floor plan converges"),FEHBFloorAssignmentPlan::Build(Input,Relations,Output,Status)))return false;
 auto Find=[&](FGuid Id)->const FEHBFloorAssignmentState&{return *Output.FindByPredicate([&](const auto& S){return S.ElementGuid==Id;});};
 TestEqual(TEXT("Lowest supporting floor selected"),Find(Slab.ElementGuid).FloorIndex,1);TestTrue(TEXT("Alternate floors retained"),Find(Slab.ElementGuid).Candidates==TArray<int32>({1,3})&&Find(Slab.ElementGuid).Conflict);TestEqual(TEXT("Upper floor propagated"),Find(Upper.ElementGuid).FloorIndex,2);
 TestEqual(TEXT("Preview does not change source"),Input[0].FloorIndex,0);
 auto ShuffledInput=Input;auto ShuffledRelations=Relations;Swap(ShuffledInput[0],ShuffledInput[3]);Swap(ShuffledRelations[0],ShuffledRelations[2]);TArray<FEHBFloorAssignmentState> ShuffledOutput;
 if(!FEHBFloorAssignmentPlan::Build(ShuffledInput,ShuffledRelations,ShuffledOutput,Status))return false;
 TestEqual(TEXT("Ordering preserves result count"),ShuffledOutput.Num(),Output.Num());for(int32 I=0;I<Output.Num()&&I<ShuffledOutput.Num();++I){const auto& L=Output[I];const auto& R=ShuffledOutput[I];TestTrue(TEXT("Input ordering cannot change plan"),L.ElementGuid==R.ElementGuid&&L.FloorIndex==R.FloorIndex&&L.Role==R.Role&&L.Source==R.Source&&L.Candidates==R.Candidates&&L.Conflict==R.Conflict);}
 Input=Output;Relations.RemoveAt(0);if(!FEHBFloorAssignmentPlan::Build(Input,Relations,Output,Status))return false;TestEqual(TEXT("Removing lower support plans alternate floor"),Find(Slab.ElementGuid).FloorIndex,3);TestEqual(TEXT("Alternate floor propagates"),Find(Upper.ElementGuid).FloorIndex,4);
 Input=Output;Relations.RemoveAt(0);if(!FEHBFloorAssignmentPlan::Build(Input,Relations,Output,Status))return false;TestEqual(TEXT("All support removed plans unassignment"),Find(Upper.ElementGuid).Source,EEHBFloorAssignmentSource::Unassigned);
 const auto Duplicate=Input[0];Input.Add(Duplicate);TestFalse(TEXT("Duplicate identity refuses planning"),FEHBFloorAssignmentPlan::Build(Input,Relations,Output,Status));TestTrue(TEXT("Failed plan exposes no partial output"),Output.IsEmpty());
 A.Policy=EEHBFloorAssignmentPolicy::Automatic;A.Source=EEHBFloorAssignmentSource::DerivedFromSupport;B.Policy=A.Policy;B.Source=A.Source;
 TestTrue(TEXT("Unrooted cycle resolves without trusting old derived state"),FEHBFloorAssignmentPlan::Build({A,B},{Edge(A.ElementGuid,B.ElementGuid),Edge(B.ElementGuid,A.ElementGuid)},Output,Status));
 for(const auto& S:Output)TestEqual(TEXT("Unrooted cycle withdraws both assignments"),S.Source,EEHBFloorAssignmentSource::Unassigned);
 A.Role=EEHBBuildingFloorElementRole::FloorCeiling;B.Role=A.Role;
 TestTrue(TEXT("Formerly stable unrooted cycle also resolves"),FEHBFloorAssignmentPlan::Build({A,B},{Edge(A.ElementGuid,B.ElementGuid),Edge(B.ElementGuid,A.ElementGuid)},Output,Status));for(const auto& S:Output)TestEqual(TEXT("Stable cycle is not a support anchor"),S.FloorIndex,0);
 A.Policy=EEHBFloorAssignmentPolicy::Explicit;A.Source=EEHBFloorAssignmentSource::Explicit;A.FloorIndex=MAX_int32-1;B.Role=EEHBBuildingFloorElementRole::FloorBody;
 TestFalse(TEXT("Overflow refuses complete candidate"),FEHBFloorAssignmentPlan::Build({A,B},{Edge(A.ElementGuid,B.ElementGuid)},Output,Status));TestTrue(TEXT("Overflow exposes no partial state"),Output.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSupportWithdrawalTest,"EHB.Surfaces.SupportFloorWithdrawal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSupportWithdrawalTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* Root=World->SpawnActor<AEHB_Pillar>(Params);auto* Slab=World->SpawnActor<AEHB_FloorSlab>(Params);auto* Upper=World->SpawnActor<AEHB_Pillar>(Params);
 ON_SCOPE_EXIT{Upper->Destroy();Slab->Destroy();Root->Destroy();B->Destroy();};
 Root->AttachToBuilding(B,FTransform::Identity);Root->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
 Slab->AttachToBuilding(B,FTransform(FVector(0,0,300)));Slab->SetAutomaticFloorAssignment();Upper->AttachToBuilding(B,FTransform(FVector(0,0,320)));Upper->SetAutomaticFloorAssignment();
 const auto First=B->SetStructuralSupportRelation(Root,Slab,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,300),FVector::UpVector,100,EEHBRelationOrigin::UserAuthored);
 const auto Second=B->SetStructuralSupportRelation(Slab,Upper,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,320),FVector::UpVector,100,EEHBRelationOrigin::UserAuthored);
 if(!First.IsValid()||!Second.IsValid())return false;
 TestEqual(TEXT("Supported upper pillar starts in floor two"),Upper->FloorIndex,2);
 {FScopedTransaction T(NSLOCTEXT("EHBTests","WithdrawSupport","Withdraw Support"));B->RemoveElementRelation(First);B->ResolveAutomaticFloorAssignments();}
 auto Missing=[&](){for(auto* E:TArray<AEHBElementActorBase*>{Slab,Upper}){TestEqual(TEXT("Unrooted derived assignment withdrawn"),E->FloorAssignmentSource,EEHBFloorAssignmentSource::Unassigned);TestEqual(TEXT("Unassigned element has no stale storey"),E->FloorIndex,0);TestTrue(TEXT("Candidates cleared"),E->ConflictingFloorCandidates.IsEmpty());}TestEqual(TEXT("Explicit root remains authored"),Root->FloorIndex,1);};Missing();
 TestTrue(TEXT("Withdraw support undo"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores derived slab floor"),Slab->FloorIndex,1);TestEqual(TEXT("Undo restores propagated pillar floor"),Upper->FloorIndex,2);
 TestTrue(TEXT("Withdraw support redo"),GEditor->RedoTransaction());Missing();
 const bool Dirty=B->GetPackage()->IsDirty();B->GetPackage()->SetDirtyFlag(false);B->ResolveAutomaticFloorAssignments();TestFalse(TEXT("No-op resolution does not dirty package"),B->GetPackage()->IsDirty());B->GetPackage()->SetDirtyFlag(Dirty);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabBottomContactTest,"EHB.Surfaces.SlabBottomContactSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabBottomContactTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* S=World->SpawnActor<AEHB_FloorSlab>(Params);ON_SCOPE_EXIT{S->Destroy();B->Destroy();};
 B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(800,-500,60)));
 const FTransform Local(FRotator(0,23,0),FVector(100,200,330));S->ConfigureDefaultSlab(B,Local,100,10,false);
 FEHBFloorSlabHole Hole;Hole.LocalPolygon={{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0}};
 if(!S->SetSlabOutline(S->LocalTopPolygon,{Hole}))return false;
 S->VisualExpansion=100;S->RebuildSlabMesh();S->MeshComponent->ClearAllMeshSections();
 FEHBFloorSupportSurface Support;for(auto P:S->LocalTopPolygon){P.Z=-10;Support.OuterPolygon.Add(Local.TransformPosition(P));}
 TArray<FEHBFloorFinishRegion> Bottom;FName Status;
 if(!TestTrue(TEXT("Underside captures without render mesh"),FEHBFloorContactGeometry::CaptureSlabBottom(S,Bottom,Status)))return false;
 double Area=0;TestTrue(TEXT("Underside solves real contact"),FEHBFloorContactGeometry::MeasureArea(Bottom,{Support},Area,Status));TestTrue(TEXT("Underside keeps hole and ignores expansion"),FMath::Abs(Area-9600)<0.2);
 for(const auto& R:Bottom)for(const auto& P:R.OuterPolygon)TestTrue(TEXT("Underside uses real thickness elevation"),FMath::Abs(P.Z-320)<0.0001);
 S->bIsFoundation=true;S->bKeepFoundationBottomOnGround=true;TestFalse(TEXT("Variable foundation needs distinct plan"),FEHBFloorContactGeometry::CaptureSlabBottom(S,Bottom,Status));TestTrue(TEXT("Foundation refusal clears output"),Bottom.IsEmpty());S->bIsFoundation=false;
 S->Thickness=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Nonfinite thickness refused"),FEHBFloorContactGeometry::CaptureSlabBottom(S,Bottom,Status));S->Thickness=10;
 S->SetElementLocalTransform(FTransform(FRotator(10,23,0),FVector(100,200,330)));TestFalse(TEXT("Tilted bottom needs distinct plan"),FEHBFloorContactGeometry::CaptureSlabBottom(S,Bottom,Status));TestTrue(TEXT("Tilt refusal clears output"),Bottom.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPreparedOpeningTest,"EHB.Surfaces.PreparedWallOpening",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPreparedOpeningTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);
 ON_SCOPE_EXIT{EHBWallOpeningPreparation::FailurePhase=0;W->Destroy();B->Destroy();};
 B->SetActorTransform(FTransform(FRotator(0,37,0),FVector(800,-500,60)));
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=2)return false;
 W->CutOperations={SurfaceOpening129::Make(Hosts[0],240,60)};W->RebuildWallMesh();
 const auto Before=Source(W);const double Left=MeshArea(W->LeftWallMeshComponent),Right=MeshArea(W->RightWallMeshComponent),Caps=MeshArea(W->CapMeshComponent);
 const int32 Graph=B->RelationshipGraphRevision,Geometry=B->GetElementGeometryRevision(W->ElementGuid);const bool Dirty=W->GetPackage()->IsDirty();
 auto Unchanged=[&](){TestEqual(TEXT("Authored source untouched"),Source(W),Before);TestEqual(TEXT("Left component untouched"),MeshArea(W->LeftWallMeshComponent),Left);TestEqual(TEXT("Right component untouched"),MeshArea(W->RightWallMeshComponent),Right);TestEqual(TEXT("Cap component untouched"),MeshArea(W->CapMeshComponent),Caps);TestEqual(TEXT("Graph unchanged"),B->RelationshipGraphRevision,Graph);TestEqual(TEXT("Geometry revision unchanged"),B->GetElementGeometryRevision(W->ElementGuid),Geometry);TestEqual(TEXT("Package dirty unchanged"),W->GetPackage()->IsDirty(),Dirty);};
 FEHBPreparedWallOpening Prepared;
 for(int32 Side=0;Side<2;++Side)
 {
  auto Cut=SurfaceOpening129::Make(Hosts[Side],200,70);Cut.Source.LocalTransform=FTransform(FRotator(0,13,0));
  if(!TestTrue(*Status.ToString(),W->PrepareSurfaceOpening({Cut},Prepared,Status)))return false;
  TestTrue(TEXT("Candidate actually generated"),Prepared.Left.Triangles.Num()>6&&Prepared.Right.Triangles.Num()>6);
  TestTrue(TEXT("Candidate removed area"),FMath::Abs(Prepared.RemovedArea-600)<0.1);
  TestTrue(TEXT("Candidate preserves operation identity"),Prepared.Sources[0].OperationGuid==Cut.OperationGuid);
  TestTrue(TEXT("Candidate supplies contact geometry without publication"),!Prepared.HorizontalTops.IsEmpty());
  Unchanged();
 }
 const auto Cut=SurfaceOpening129::Make(Hosts[0],240,60);
 for(int32 Phase:{1,2})
 {
  EHBWallOpeningPreparation::FailurePhase=Phase;
  TestFalse(TEXT("Injected generation/uncut result refused"),W->PrepareSurfaceOpening({Cut},Prepared,Status));
  TestEqual(TEXT("Precise generation failure"),Status,FName(Phase==1?TEXT("OpeningCandidateGenerationFailed"):TEXT("OpeningCandidateAreaMismatch")));
  TestTrue(TEXT("Failed output cleared"),Prepared.Sources.IsEmpty()&&Prepared.Left.Vertices.IsEmpty());Unchanged();
 }
 auto Bad=Cut;Bad.SurfaceHost.SurfaceGuid=FGuid::NewGuid();TestFalse(TEXT("Unknown host fails without writes"),W->PrepareSurfaceOpening({Bad},Prepared,Status));Unchanged();
 TestFalse(TEXT("Duplicate operation identity is rejected"),W->PrepareSurfaceOpening({Cut,Cut},Prepared,Status));
 auto Edge=SurfaceOpening129::Make(Hosts[0],0,60);TestTrue(TEXT("End boundary uses clipped cap and analytic reveal plan"),W->PrepareSurfaceOpening({Edge},Prepared,Status));TestTrue(TEXT("End boundary retains measured removal"),FMath::Abs(Prepared.RemovedArea-600)<0.01);
 {FEHBChangeNotificationBatch Batch(*B);TestFalse(TEXT("No preview against unpublished source"),W->PrepareSurfaceOpening({Cut},Prepared,Status));Batch.Rollback();}
 TestTrue(TEXT("Removal preview prepares uncut geometry without removing old opening"),W->PrepareSurfaceOpening({},Prepared,Status));TestEqual(TEXT("Removal area"),Prepared.RemovedArea,0.0);Unchanged();
 W->LeftSurfaceStyle.SourceType=EEHBWallSurfaceSourceType::SampledMesh;TestFalse(TEXT("Sampled style never silently simplified"),W->PrepareSurfaceOpening({Cut},Prepared,Status));W->LeftSurfaceStyle.SourceType=EEHBWallSurfaceSourceType::Simple;
 GEditor->SelectNone(false,true,false);
 TestFalse(TEXT("Tool preview requires explicit selection"),UEHBBuildingToolset::PreviewWallOpenings(W,{Cut},Graph,Geometry,Prepared).bSucceeded);
 for(AActor* Selected:TArray<AActor*>{B,W})
 {
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(Selected,true,false);
  TestTrue(TEXT("Tool preview accepts selected wall or building"),UEHBBuildingToolset::PreviewWallOpenings(W,{Cut},Graph,Geometry,Prepared).bSucceeded);
  TestFalse(TEXT("Stale graph rejected"),UEHBBuildingToolset::PreviewWallOpenings(W,{Cut},Graph+1,Geometry,Prepared).bSucceeded);
  TestFalse(TEXT("Stale geometry rejected"),UEHBBuildingToolset::PreviewWallOpenings(W,{Cut},Graph,Geometry+1,Prepared).bSucceeded);
  TestTrue(TEXT("Stale result clears output"),Prepared.Left.Vertices.IsEmpty());Unchanged();
 }
 GEditor->SelectNone(false,true,false);
 // Preparation has no generated-component dependency.
 for(auto* C:{W->LeftWallMeshComponent.Get(),W->RightWallMeshComponent.Get(),W->CapMeshComponent.Get()})if(C)C->DestroyComponent();
 W->LeftWallMeshComponent=nullptr;W->RightWallMeshComponent=nullptr;W->CapMeshComponent=nullptr;
 TestTrue(TEXT("Renderer-free candidate generated"),W->PrepareSurfaceOpening({Cut},Prepared,Status));TestEqual(TEXT("Renderer-free source retained"),Source(W),Before);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPreparedContactTest,"EHB.Surfaces.PreparedContactCapture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPreparedContactTest::RunTest(const FString& Parameters)
{
 FEHBWallJunctionMesh Mesh;Mesh.Vertices={{0,0,20},{10,0,20},{0,10,20}};Mesh.Normals.Init(FVector::UpVector,3);Mesh.Triangles={0,1,2};
 const FTransform Building(FRotator(0,37,0),FVector(800,-500,60));
 const FTransform Local(FRotator::ZeroRotator,FVector(50,60,300));const auto World=Local*Building;
 TArray<FEHBFloorSupportSurface> Tops;FName Status;
 if(!TestTrue(TEXT("Value-only candidate contact capture"),FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Mesh,World,Building,Tops,Status)))return false;
 TestEqual(TEXT("One candidate contact triangle"),Tops.Num(),1);
 if(Tops.Num()!=1)return false;
 for(int32 I=0;I<3;++I)TestTrue(TEXT("Candidate is in building-local frame"),Tops[0].OuterPolygon[I].Equals(Local.TransformPosition(Mesh.Vertices[I]),0.00001));
 FEHBFloorFinishRegion Floor;Floor.OuterPolygon={{50,60,320},{60,60,320},{60,70,320},{50,70,320}};double Area=0;
 TestTrue(TEXT("Candidate participates in real floor contact solve"),FEHBFloorContactGeometry::MeasureArea({Floor},Tops,Area,Status));TestTrue(TEXT("Candidate coverage area"),FMath::Abs(Area-50)<0.01);
 Mesh.Triangles.Add(0);TestFalse(TEXT("Incomplete candidate triangle refused"),FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Mesh,World,Building,Tops,Status));TestTrue(TEXT("Failure never leaks preceding candidate"),Tops.IsEmpty());Mesh.Triangles.Pop();
 Mesh.Triangles[2]=99;TestFalse(TEXT("Invalid candidate index refused"),FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Mesh,World,Building,Tops,Status));TestTrue(TEXT("Index failure clears output"),Tops.IsEmpty());Mesh.Triangles[2]=2;
 TestFalse(TEXT("Singular candidate frame refused"),FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Mesh,World,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),Tops,Status));
 Mesh.Normals.Init(FVector::ForwardVector,3);TestTrue(TEXT("No upward support is a valid empty result"),FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Mesh,World,Building,Tops,Status));TestTrue(TEXT("Vertical normals cannot support floor"),Tops.IsEmpty());
 return true;
}
