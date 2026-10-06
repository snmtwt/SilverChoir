IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayFollowingWriteTest,"EHBValidation.Persistence.WriteDisplayFollowing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayFollowingWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("DisplayFollowing"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Mode,true)||!InitializeBoundDisplays(Setup))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));
  FEHBWallNodeModel Model;if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Model).bSucceeded)return false;const auto* W=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Setup.Shared;});if(!W)return false;const FGuid Node=W->StartNodeGuid;
  const auto Moved=MoveBoundDisplayNode(B,Node);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("node"),Node.ToString());TArray<TSharedPtr<FJsonValue>> Slabs;int32 Multi=0;
  for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("element"),Id.ToString());FString Json;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Json);V->SetStringField(TEXT("partition"),Json);Slabs.Add(MakeShared<FJsonValueObject>(V));if(S->DisplayPartition.Regions.Num()>1)++Multi;}
  if(Mode==2&&!TestTrue(TEXT("Saved followed fixture includes multi-region slab"),Multi>0))return false;
  C->SetArrayField(TEXT("slabs"),Slabs);Cases.Add(MakeShared<FJsonValueObject>(C));B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Setup.Fixture.Actors.Reset();
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.DisplayFollowing.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayFollowingReadTest,"EHBValidation.Persistence.ReadDisplayFollowing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayFollowingReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("DisplayFollowing"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.DisplayFollowing.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Followed map loaded"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Saved binding case count"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Case:Data->GetArrayField(TEXT("cases")))
 {
  auto C=Case->AsObject();FGuid Id,Node;FGuid::Parse(C->GetStringField(TEXT("building")),Id);FGuid::Parse(C->GetStringField(TEXT("node")),Node);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id)B=*It;if(!B)return false;
  FEHBSlabDisplayPlan Display;FName Status;
  for(const auto& Item:C->GetArrayField(TEXT("slabs")))
  {
   const auto V=Item->AsObject();FGuid Element;FGuid::Parse(V->GetStringField(TEXT("element")),Element);auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Element));if(!S)return false;
   FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("Cold followed masks preserved exactly"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));TestEqual(TEXT("Cold followed slab room ownership"),S->OutlineSource,EEHBOutlineSource::RoomBoundary);TestTrue(TEXT("Cold followed source provenance"),S->IsRecordedOutlineUnchanged());if(!S->RebuildSlabMesh())return false;
   auto& P=Display.Slabs.AddDefaulted_GetRef();P.Actor=S;P.Polygon=S->LocalTopPolygon;
   TArray<UEHBGeneratedMeshComponent*> Meshes;S->GetGeneratedMeshComponents(Meshes);auto* Surface=Meshes.IsEmpty()?nullptr:Cast<UEHBPlanarSurfaceComponent>(Meshes[0]);if(!Surface)return false;if(S->DisplayPartition.IsActive())TestEqual(TEXT("Cold component keeps every fragment"),Surface->GetPlanarSurfaceRegions().Num(),S->DisplayPartition.Regions.Num());
  }
  Display.DisplayLayers.Add(Display.Slabs[0].Actor);if(!Display.Prepare(Status)||!TestTrue(*Status.ToString(),Display.Verify(Status)))return false;
  const auto Before=LiveNodeAuthoritySnapshot(B);const auto Again=MoveBoundDisplayNode(B,Node);if(!TestTrue(*FString::Printf(TEXT("Cold second wall move: %s"),*Again.Message),Again.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold second move undo"),LiveNodeAuthoritySnapshot(B),Before);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,4000,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;
  for(const auto& Item:C->GetArrayField(TEXT("slabs"))){auto V=Item->AsObject();FGuid Old;FGuid::Parse(V->GetStringField(TEXT("element")),Old);auto* S=Cast<AEHB_FloorSlab>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(Old)));if(!S)return false;FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("Cold copy keeps all fragment data"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));if(!S->RebuildSlabMesh())return false;}GEditor->UndoTransaction(false);
 }
 return true;
}
