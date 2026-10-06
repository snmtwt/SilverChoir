IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayTopologyWriteTest,"EHBValidation.Persistence.WriteDisplayTopology",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayTopologyWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("DisplayTopology"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)for(int32 Operation=0;Operation<3;++Operation)
 {
  FStyledMergeFixture Setup;const bool Different=Operation!=1;if(!Setup.Create(World,Mode,true,Different,Different)||!InitializeBoundDisplays(Setup))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,Operation*2200,0));FGuid ContinueWall;
  if(Operation==0)
  {
   TArray<FEHBWallCreationEndpoint> Points;if(!MakeDisplaySubdivisionEndpoints(B,Points))return false;const auto R=EHBWallCreationCommand::CommitAnchoredPath(B,Points,false,FEHBWallCreationOptions());if(!TestTrue(*R.Status.ToString(),R.bSucceeded)||!R.PrimaryWall)return false;ContinueWall=R.PrimaryWall->ElementGuid;
  }
  else
  {
   const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;ContinueWall=Model.Walls[0].WallGuid;
  }
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("continueWall"),ContinueWall.ToString());C->SetNumberField(TEXT("operation"),Operation);C->SetNumberField(TEXT("roomCount"),B->GetClosedLoopsByFloor(1).Num());TArray<TSharedPtr<FJsonValue>> Slabs;
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* S=Cast<AEHB_FloorSlab>(E)){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("element"),S->ElementGuid.ToString());FString Data;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Data);V->SetStringField(TEXT("partition"),Data);V->SetNumberField(TEXT("source"),static_cast<int32>(S->OutlineSource));Slabs.Add(MakeShared<FJsonValueObject>(V));}
  C->SetArrayField(TEXT("slabs"),Slabs);Cases.Add(MakeShared<FJsonValueObject>(C));B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Setup.Fixture.Actors.Reset();
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.DisplayTopology.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayTopologyReadTest,"EHBValidation.Persistence.ReadDisplayTopology",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayTopologyReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("DisplayTopology"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.DisplayTopology.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Topology map loaded"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three operations by three binding cases saved"),Data->GetArrayField(TEXT("cases")).Num(),9);
 for(const auto& Case:Data->GetArrayField(TEXT("cases")))
 {
  auto C=Case->AsObject();FGuid Id,Wall;FGuid::Parse(C->GetStringField(TEXT("building")),Id);FGuid::Parse(C->GetStringField(TEXT("continueWall")),Wall);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id)B=*It;if(!B)return false;GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  TestEqual(TEXT("Saved room count restored"),B->GetClosedLoopsByFloor(1).Num(),static_cast<int32>(C->GetNumberField(TEXT("roomCount"))));FEHBSlabDisplayPlan Display;FName Status;
  for(const auto& Item:C->GetArrayField(TEXT("slabs")))
  {
   auto V=Item->AsObject();FGuid Element;FGuid::Parse(V->GetStringField(TEXT("element")),Element);auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Element));if(!S)return false;FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("Cold topology masks exact"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));TestEqual(TEXT("Cold source ownership exact"),static_cast<int32>(S->OutlineSource),static_cast<int32>(V->GetNumberField(TEXT("source"))));TestTrue(TEXT("Cold source provenance"),S->IsRecordedOutlineUnchanged());if(!S->RebuildSlabMesh())return false;auto& P=Display.Slabs.AddDefaulted_GetRef();P.Actor=S;P.Polygon=S->LocalTopPolygon;
  }
  Display.DisplayLayers.Add(Display.Slabs[0].Actor);if(!Display.Prepare(Status)||!TestTrue(*Status.ToString(),Display.Verify(Status)))return false;
  const auto Before=DisplayTopologyState(B);const auto Next=UEHBBuildingToolset::RemoveWalls(B,{Wall},B->RelationshipGraphRevision,false);if(!TestTrue(*FString::Printf(TEXT("Cold topology continuation: %s"),*Next.Message),Next.bSucceeded))return false;TestEqual(TEXT("Cold continuation changes room count"),B->GetClosedLoopsByFloor(1).Num(),C->GetNumberField(TEXT("operation"))==0?2:0);GEditor->UndoTransaction(false);TestEqual(TEXT("Cold continuation undo source/masks"),DisplayTopologyState(B),Before);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,8000,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;
  for(const auto& Item:C->GetArrayField(TEXT("slabs"))){auto V=Item->AsObject();FGuid Old;FGuid::Parse(V->GetStringField(TEXT("element")),Old);auto* S=Cast<AEHB_FloorSlab>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(Old)));if(!S)return false;FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("Cold topology copy masks exact"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));if(!S->RebuildSlabMesh())return false;}GEditor->UndoTransaction(false);
 }
 return true;
}
