IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningRegionWriteTest,"EHBValidation.Persistence.WriteOpeningRegion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningRegionWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("OpeningRegion"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Mode,true))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));
  for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=10;S->RebuildSlabMesh();S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  const auto Removed=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!Removed.bSucceeded)return false;
  auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[0]));const auto C0=FBox(S->LocalTopPolygon).GetCenter();auto Added=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,C0-FVector(50,0,0),20,20,0);if(!TestTrue(*Added.Message,Added.bSucceeded))return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);FEHBSlabCutterDraft CutterDraft;if(!CutterDraft.Begin(S,Mode==1?EEHBFloorSlabCutterShape::Circle:EEHBFloorSlabCutterShape::Square))return false;CutterDraft.Cutter.Size=20;CutterDraft.Update(S->GetActorTransform().TransformVector(FVector(50,0,0)),FRotator(0,10,0),FVector(0.1,0.1,0));const auto CutAdded=CutterDraft.Execute(false);if(!TestTrue(*CutAdded.Message,CutAdded.bSucceeded))return false;

  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)}){FEHBFinishRegionDrag Drag;Drag.Begin(S,0,1,false,Loop);if(!TestTrue(TEXT("Writer opening drag captured"),Drag.bReady))return false;Drag.Update(S->GetActorTransform().TransformVector(FVector(0,-2,0)));const auto R=Drag.Execute(false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)}){const auto R=EHBSlabOpeningEdit::Execute(S,Loop,2,3,true);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
  auto Polygon=S->LocalTopPolygon;const auto Center=FBox(Polygon).GetCenter();for(auto& V:Polygon){V.X=Center.X+(V.X-Center.X)*0.9;V.Y=Center.Y+(V.Y-Center.Y)*0.9;}
  const auto Edited=UEHBBuildingToolset::EditFinishRegion(B,S->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Polygon,{},false);if(!TestTrue(*Edited.Message,Edited.bSucceeded))return false;
  if(Mode==2)
  {
   TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!S->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=1)return false;
   auto Bound=SurfaceOpening129::Make(Hosts[0],Center.X+40,Center.Y-15);Bound.Source.Height=S->Thickness;
   GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);const auto Applied=UEHBBuildingToolset::SetFloorSlabOpenings(S,S->LocalHoles,{Bound},B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  }
  if(!CheckWallBaseSurfaces(*this,B,false))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("edited"),S->ElementGuid.ToString());TArray<TSharedPtr<FJsonValue>> Slabs;
  for(FGuid Id:Setup.Slabs){auto* A=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("element"),Id.ToString());V->SetStringField(TEXT("surface"),A->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")).ToString());FString State;FJsonObjectConverter::UStructToJsonObjectString(A->DisplayPartition,State);V->SetStringField(TEXT("partition"),State);Slabs.Add(MakeShared<FJsonValueObject>(V));}
  if(Mode==2)
  {
   const auto Before=OpeningRegionState(B);const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,2500,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;TestEqual(TEXT("Writer copy preserves source"),OpeningRegionState(B),Before);
   auto* Target=CastChecked<AEHB_FloorSlab>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(S->ElementGuid)));
   auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("building"),Copy.Building->BuildingGuid.ToString());V->SetStringField(TEXT("element"),Target->ElementGuid.ToString());V->SetStringField(TEXT("surface"),Target->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")).ToString());FString State;FJsonObjectConverter::UStructToJsonObjectString(Target->DisplayPartition,State);V->SetStringField(TEXT("partition"),State);C->SetObjectField(TEXT("copiedBoundSlab"),V);
  }
  C->SetBoolField(TEXT("surfaceBoundCut"),Mode==2);C->SetArrayField(TEXT("slabs"),Slabs);Cases.Add(MakeShared<FJsonValueObject>(C));Setup.Fixture.Actors.Reset();
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.OpeningRegion.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningRegionReadTest,"EHBValidation.Persistence.ReadOpeningRegion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningRegionReadTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBFinishRegionCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("OpeningRegion"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.OpeningRegion.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Opening region map loaded"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("All node binding variants saved"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Item:Data->GetArrayField(TEXT("cases")))
 {
  auto C=Item->AsObject();FGuid Id,Edited;FGuid::Parse(C->GetStringField(TEXT("building")),Id);FGuid::Parse(C->GetStringField(TEXT("edited")),Edited);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id)B=*It;if(!B)return false;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  for(const auto& V:C->GetArrayField(TEXT("slabs"))){auto Expected=V->AsObject();FGuid Element;FGuid::Parse(Expected->GetStringField(TEXT("element")),Element);auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Element));if(!S)return false;FString State;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,State);TestEqual(TEXT("Cold complete masks and source snapshot"),State,CanonicalDisplayPartitionManifest(*this,Expected->GetStringField(TEXT("partition"))));TestEqual(TEXT("Cold semantic surface identity"),S->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")).ToString(),Expected->GetStringField(TEXT("surface")));if(!TestTrue(TEXT("Cold actual source and partition agree"),S->RebuildSlabMesh()))return false;TestTrue(TEXT("Cold source provenance"),S->IsRecordedOutlineUnchanged());}
  auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Edited));if(!S||S->LocalHoles.Num()!=1||S->CutOperations.Num()!=1)return false;
  bool Bound=false;C->TryGetBoolField(TEXT("surfaceBoundCut"),Bound);if(Bound){TestEqual(TEXT("Cold cut remains generic surface opening"),S->CutOperations[0].Stage,EEHBCutStage::SurfaceOpening);TestEqual(TEXT("Cold bound slab surface identity"),S->CutOperations[0].SurfaceHost.SurfaceGuid,S->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")));}
  const TSharedPtr<FJsonObject>* CopyData=nullptr;
  if(C->TryGetObjectField(TEXT("copiedBoundSlab"),CopyData))
  {
   FGuid CopyId,Element;FGuid::Parse((*CopyData)->GetStringField(TEXT("building")),CopyId);FGuid::Parse((*CopyData)->GetStringField(TEXT("element")),Element);AEHBBuildingActorBase* Copy=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==CopyId)Copy=*It;if(!Copy)return false;
   auto* Target=Cast<AEHB_FloorSlab>(Copy->FindElementActorByGuid(Element));if(!Target||Target->CutOperations.Num()!=1)return false;
   const auto& Host=Target->CutOperations[0].SurfaceHost;TestEqual(TEXT("Cold copied building binding"),Host.BuildingGuid,CopyId);TestEqual(TEXT("Cold copied element binding"),Host.ElementGuid,Element);TestEqual(TEXT("Cold copied surface binding"),Host.SurfaceGuid.ToString(),(*CopyData)->GetStringField(TEXT("surface")));TestNotEqual(TEXT("Cold copy has distinct surface"),Host.SurfaceGuid,S->CutOperations[0].SurfaceHost.SurfaceGuid);
   FString State;FJsonObjectConverter::UStructToJsonObjectString(Target->DisplayPartition,State);TestEqual(TEXT("Cold copied partition"),State,CanonicalDisplayPartitionManifest(*this,(*CopyData)->GetStringField(TEXT("partition"))));if(!TestTrue(TEXT("Cold copied slab rebuild"),Target->RebuildSlabMesh()))return false;
   const auto Original=OpeningRegionState(B),Before=OpeningRegionState(Copy);GEditor->SelectNone(false,true,false);GEditor->SelectActor(Target,true,false);
   const auto R=UEHBBuildingToolset::SetFloorSlabOpenings(Target,Target->LocalHoles,{},Copy->RelationshipGraphRevision,Copy->GetElementGeometryRevision(Target->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto After=OpeningRegionState(Copy);TestEqual(TEXT("Cold copied edit leaves original"),OpeningRegionState(B),Original);GEditor->UndoTransaction();TestEqual(TEXT("Cold copied edit undo"),OpeningRegionState(Copy),Before);GEditor->RedoTransaction();TestEqual(TEXT("Cold copied edit redo"),OpeningRegionState(Copy),After);GEditor->UndoTransaction();
  }
  auto Polygon=S->LocalTopPolygon;const auto Center=FBox(Polygon).GetCenter();for(auto& V:Polygon){V.X=Center.X+(V.X-Center.X)*0.95;V.Y=Center.Y+(V.Y-Center.Y)*0.95;}
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(S,true,false);
  {
   const auto BeforeCutter=OpeningRegionState(B);FEHBSlabCutterDraft Draft;if(!TestTrue(TEXT("Cold preview cutter begin"),Draft.Begin(S,EEHBFloorSlabCutterShape::Circle)))return false;Draft.Cutter.Size=20;Draft.Update(S->GetActorTransform().TransformVector(FVector(0,70,0)),FRotator(0,15,0),FVector(0.1,0.1,0));TestTrue(TEXT("Cold cutter preview keeps persistent state"),OpeningRegionState(B)==BeforeCutter&&S->PreviewCutters.IsEmpty());auto Cancel=Draft;Cancel.Cancel();TestFalse(TEXT("Cold cancelled draft rejects commit"),Cancel.Execute(false).bSucceeded);const auto R=Draft.Execute(false);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto AfterCutter=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold cutter commit undo"),OpeningRegionState(B)==BeforeCutter);GEditor->RedoTransaction();TestTrue(TEXT("Cold cutter commit redo"),OpeningRegionState(B)==AfterCutter);GEditor->UndoTransaction();
  }
  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)})
  {
   const auto BeforeVertices=OpeningRegionState(B);const auto Preview=EHBSlabOpeningEdit::Execute(S,Loop,2,3,true,true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestTrue(TEXT("Cold vertex preview read only"),OpeningRegionState(B)==BeforeVertices);
   const auto InsertedResult=EHBSlabOpeningEdit::Execute(S,Loop,2,3,true);if(!TestTrue(*InsertedResult.Message,InsertedResult.bSucceeded))return false;const auto Inserted=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold insertion undo"),OpeningRegionState(B)==BeforeVertices);GEditor->RedoTransaction();TestTrue(TEXT("Cold insertion redo"),OpeningRegionState(B)==Inserted);
   const auto RemovedResult=EHBSlabOpeningEdit::Execute(S,Loop,3,INDEX_NONE,false);if(!TestTrue(*RemovedResult.Message,RemovedResult.bSucceeded))return false;const auto Removed=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold vertex removal undo"),OpeningRegionState(B)==Inserted);GEditor->RedoTransaction();TestTrue(TEXT("Cold vertex removal redo"),OpeningRegionState(B)==Removed);GEditor->UndoTransaction();GEditor->UndoTransaction();TestTrue(TEXT("Cold vertex sequence returns exact source"),OpeningRegionState(B)==BeforeVertices);
  }
  for(int32 Loop:{0,AEHB_FloorSlab::MakeCutOperationLoopIndex(0)})
  {
   const auto DraftBefore=OpeningRegionState(B);FEHBFinishRegionDrag Drag;Drag.Begin(S,0,1,false,Loop);if(!TestTrue(TEXT("Cold opening drag captured"),Drag.bReady))return false;if(AEHB_FloorSlab::IsCutOperationLoopIndex(Loop)&&Drag.Polygon.Num()==32){auto Invalid=Drag;Invalid.Update(S->GetActorTransform().TransformVector(FVector(0,-2,0)));TestFalse(TEXT("Cold self-crossing circle drag refused"),Invalid.Feedback.bSucceeded);TestFalse(TEXT("Invalid circle cannot commit"),Invalid.Execute(false).bSucceeded);TestTrue(TEXT("Invalid circle drag preserves saved building"),OpeningRegionState(B)==DraftBefore);}
   Drag.Update(S->GetActorTransform().TransformVector(FVector(2,0,0)));TestTrue(TEXT("Cold opening drag preview read only"),OpeningRegionState(B)==DraftBefore);auto Cancel=Drag;Cancel.bCancelled=true;TestTrue(TEXT("Cold opening drag cancel"),Cancel.Execute(false).bSucceeded);const auto R=Drag.Execute(false);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto DraftAfter=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold opening drag undo"),OpeningRegionState(B)==DraftBefore);GEditor->RedoTransaction();TestTrue(TEXT("Cold opening drag redo"),OpeningRegionState(B)==DraftAfter);GEditor->UndoTransaction();
  }
  const auto OpeningBefore=OpeningRegionState(B);const auto Deleted=UEHBBuildingToolset::SetFloorSlabOpenings(S,{}, {},B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),false);if(!TestTrue(*Deleted.Message,Deleted.bSucceeded))return false;TestTrue(TEXT("Cold tool deletes hole and cut"),S->LocalHoles.IsEmpty()&&S->CutOperations.IsEmpty());const auto Empty=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold opening deletion undo"),OpeningRegionState(B)==OpeningBefore);GEditor->RedoTransaction();TestTrue(TEXT("Cold opening deletion redo"),OpeningRegionState(B)==Empty);GEditor->UndoTransaction();
  const auto Before=OpeningRegionState(B);auto Edit=[&](bool Preview){return UEHBBuildingToolset::EditFinishRegion(B,S->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(S->ElementGuid),Polygon,{},Preview);};
  const auto Preview=Edit(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;TestTrue(TEXT("Cold preview unchanged"),OpeningRegionState(B)==Before);
  EHBFinishRegionCommand::FailurePhase=2;const auto Failed=Edit(false);TestEqual(TEXT("Cold contact failure reached"),EHBFinishRegionCommand::FailurePhase,0);TestEqual(TEXT("Cold contact edit rollback"),Failed.Message,FString(TEXT("RegionEditFailedRolledBack")));TestTrue(TEXT("Cold rollback exact"),OpeningRegionState(B)==Before);
  const auto Applied=Edit(false);if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;const auto After=OpeningRegionState(B);GEditor->UndoTransaction();TestTrue(TEXT("Cold continued edit undo"),OpeningRegionState(B)==Before);GEditor->RedoTransaction();TestTrue(TEXT("Cold continued edit redo"),OpeningRegionState(B)==After);
  if(!CheckWallBaseSurfaces(*this,B,true))return false;
 }
 return true;
}
