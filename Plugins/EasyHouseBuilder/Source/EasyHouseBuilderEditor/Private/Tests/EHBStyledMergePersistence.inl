namespace
{
 // UE may print a near-zero derived attachment component as -0.000000 after
 // loading a translated/rotated building. Canonicalize only that numeric zero
 // spelling; keep every nonzero component and all other snapshot fields exact.
 FString StyledPersistenceSnapshot(const FString& Text)
 {
  TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data))return Text;
  for(const auto& V:Data->GetArrayField(TEXT("junctions")))
  {
   const auto J=V->AsObject();TArray<FString> Groups;J->GetStringField(TEXT("pose")).ParseIntoArray(Groups,TEXT("|"),false);
   for(auto& Group:Groups){TArray<FString> Components;Group.ParseIntoArray(Components,TEXT(","),false);for(auto& Component:Components)if(Component==TEXT("-0.000000"))Component=TEXT("0.000000");Group=FString::Join(Components,TEXT(","));}
   J->SetStringField(TEXT("pose"),FString::Join(Groups,TEXT("|")));
  }
  FString Result;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Result));return Result;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStyledMergeWriteTest,"EHBValidation.Persistence.WriteStyledMerge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStyledMergeWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("StyledMerge")))return false;
 if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite styled partition evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<AEHBBuildingActorBase*> Buildings;TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Mode,true))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));
  const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded)||!CheckStyledMerge(*this,B,Setup.Floors,Setup.Slabs,Setup.Materials))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("node"),Setup.Node.ToString());TArray<TSharedPtr<FJsonValue>> Elements;
  for(const auto& Pair:Setup.Materials)
  {auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("id"),Pair.Key.ToString());E->SetStringField(TEXT("material"),Pair.Value.ToSoftObjectPath().ToString());E->SetBoolField(TEXT("floor"),Setup.Floors.Contains(Pair.Key));Elements.Add(MakeShared<FJsonValueObject>(E));}
  C->SetArrayField(TEXT("elements"),Elements);Cases.Add(MakeShared<FJsonValueObject>(C));Buildings.Add(B);
  B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Setup.Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save actual different-material partitions and generated seam"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 for(int32 I=0;I<Buildings.Num();++I){Cases[I]->AsObject()->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(Buildings[I]));Cases[I]->AsObject()->SetStringField(TEXT("receipt"),LiveNodeReceipt(Buildings[I]));}
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.StyledMerge.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save styled partition manifest"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBStyledMergeReadTest,"EHBValidation.Persistence.ReadStyledMerge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBStyledMergeReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("StyledMerge"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.StyledMerge.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold styled map current"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three styled binding modes"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Value:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=Value->AsObject();auto Id=[&](const TCHAR* Key){FGuid V;FGuid::Parse(C->GetStringField(Key),V);return V;};AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id(TEXT("building")))B=*It;if(!B)return false;
  const auto Before=LiveNodeAuthoritySnapshot(B);TestEqual(TEXT("Cold exact styled geometry and sources (canonical numeric zero)"),StyledPersistenceSnapshot(Before),StyledPersistenceSnapshot(C->GetStringField(TEXT("state"))));TestEqual(TEXT("Cold styled receipt"),LiveNodeReceipt(B),C->GetStringField(TEXT("receipt")));GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  TArray<FGuid> Floors,Slabs;TMap<FGuid,TSoftObjectPtr<UMaterialInterface>> Materials;
  for(const auto& V:C->GetArrayField(TEXT("elements"))){const auto E=V->AsObject();FGuid Element;FGuid::Parse(E->GetStringField(TEXT("id")),Element);(E->GetBoolField(TEXT("floor"))?Floors:Slabs).Add(Element);Materials.Add(Element,TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(E->GetStringField(TEXT("material")))));}
  if(!TestEqual(TEXT("Cold two floor partitions"),Floors.Num(),2)||!TestEqual(TEXT("Cold two slab partitions"),Slabs.Num(),2)||!CheckStyledMerge(*this,B,Floors,Slabs,Materials))return false;
  for(FGuid Element:{Slabs[0],Floors[0]})
  {
   auto P=EHBFinishRegionCommand::GetPolygon(B->FindElementActorByGuid(Element));const auto Center=FBox(P).GetCenter();for(auto& V:P){V.X=Center.X+(V.X-Center.X)*0.95;V.Y=Center.Y+(V.Y-Center.Y)*0.95;}
   const auto R=UEHBBuildingToolset::EditFinishRegion(B,Element,B->RelationshipGraphRevision,B->GetElementGeometryRevision(Element),P,{},false);if(!TestTrue(*R.Message,R.bSucceeded))return false;const auto After=LiveNodeAuthoritySnapshot(B);
   TestTrue(TEXT("Cold partition resize undo"),GEditor->UndoTransaction());TestEqual(TEXT("Cold partition undo exact"),LiveNodeAuthoritySnapshot(B),Before);TestTrue(TEXT("Cold partition resize redo"),GEditor->RedoTransaction());TestEqual(TEXT("Cold partition redo exact"),LiveNodeAuthoritySnapshot(B),After);GEditor->UndoTransaction(false);
  }
  const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id(TEXT("node"));});if(!N)return false;FEHBNodeMoveRequest M;M.NodeGuid=N->NodeGuid;M.ExpectedPosition=N->LocalTransform.GetLocation();M.TargetPosition=M.ExpectedPosition+FVector(-25,0,0);
  const auto Moved=EHBNodeAuthorityEditing::ExecuteMoves(B,{M},{{N->NodeGuid,N->GeometryRevision}},false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold styled wall move undo"),LiveNodeAuthoritySnapshot(B),Before);
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3200,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;GEditor->UndoTransaction(false);TestEqual(TEXT("Cold styled source survives copy"),LiveNodeAuthoritySnapshot(B),Before);
  if(!CheckStyledMerge(*this,B,Floors,Slabs,Materials))return false;
 }
 GEditor->SelectNone(false,true,false);return true;
}
