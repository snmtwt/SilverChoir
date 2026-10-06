namespace
{
 double CutSlabTopArea(const AEHB_FloorSlab* Slab)
 {
  double Area=0;const auto* S=Slab->MeshComponent->GetProcMeshSection(0);if(!S)return Area;
  for(int32 I=0;I<S->ProcIndexBuffer.Num();I+=3){const auto A=S->ProcVertexBuffer[S->ProcIndexBuffer[I]].Position,B=S->ProcVertexBuffer[S->ProcIndexBuffer[I+1]].Position,C=S->ProcVertexBuffer[S->ProcIndexBuffer[I+2]].Position;
   if(FMath::Abs(A.Z-Slab->GetTopZ())<0.001&&FMath::Abs(B.Z-A.Z)<0.001&&FMath::Abs(C.Z-A.Z)<0.001)Area+=FVector::CrossProduct(B-A,C-A).Size()*0.5;}
  return Area;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutSlabRegionsWriteTest,"EHBValidation.Persistence.WriteCutSlabRegions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCutSlabRegionsWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("CutSlabRegions"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 auto Rect=[](double A,double B,double C,double D){return TArray<FVector>{{A,B,0},{C,B,0},{C,D,0},{A,D,0}};};
 for(int32 I=0;I<4;++I)
 {
  auto* S=World->SpawnActor<AEHB_FloorSlab>();if(!S)return false;S->SetActorLocation(FVector(I*1000,0,300));S->SetActorRotation(FRotator(0,37,0));S->VisualExpansion=I*5;S->LocalTopPolygon=Rect(0,0,500,500);S->LocalHoles.AddDefaulted_GetRef().LocalPolygon=Rect(300,100,350,150);
  FEHBCutOperation Cut;for(const auto& P:Rect(200,-100,220,600))Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Cut.EnsureGuids();if(!S->AddCutOperation(Cut))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("actor"),S->GetName());C->SetStringField(TEXT("cut"),Cut.OperationGuid.ToString());C->SetNumberField(TEXT("expansion"),I*5);C->SetNumberField(TEXT("regions"),I<2?2:1);C->SetNumberField(TEXT("area"),I==0?237500:I==1?252500:I==2?267900:278400);
  FString Source;FJsonObjectConverter::UStructToJsonObjectString(S->CutOperations[0],Source);C->SetStringField(TEXT("source"),Source);Cases.Add(MakeShared<FJsonValueObject>(C));
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.CutSlabRegions.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBCutSlabRegionsReadTest,"EHBValidation.Persistence.ReadCutSlabRegions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBCutSlabRegionsReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("CutSlabRegions"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.CutSlabRegions.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Loaded cut map"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Four expansion cases"),Data->GetArrayField(TEXT("cases")).Num(),4);
 for(const auto& V:Data->GetArrayField(TEXT("cases")))
 {
  auto C=V->AsObject();AEHB_FloorSlab* S=nullptr;for(TActorIterator<AEHB_FloorSlab> It(World);It;++It)if(It->GetName()==C->GetStringField(TEXT("actor")))S=*It;if(!S||S->CutOperations.Num()!=1)return false;
  FString Source;FJsonObjectConverter::UStructToJsonObjectString(S->CutOperations[0],Source);TestEqual(TEXT("Exact cut source"),Source,CanonicalCutOperationManifest(*this,C->GetStringField(TEXT("source"))));TestEqual(TEXT("Author hole retained"),S->LocalHoles.Num(),1);
  auto Verify=[&](){auto* Surface=Cast<UEHBPlanarSurfaceComponent>(S->MeshComponent);TArray<FEHBPlanarSurfaceRegion> Regions;if(!Surface||!S->BuildEffectiveDisplayRegions(Regions,true,true))return false;TestEqual(TEXT("All cold query pieces"),Regions.Num(),int32(C->GetNumberField(TEXT("regions"))));TestEqual(TEXT("All cold component pieces"),Surface->GetPlanarSurfaceRegions().Num(),Regions.Num());return TestTrue(TEXT("Exact cold triangle area"),FMath::Abs(CutSlabTopArea(S)-C->GetNumberField(TEXT("area")))<0.01);};
  if(!Verify()||!S->RebuildSlabMesh()||!Verify())return false;
  auto Invalid=S->CutOperations[0];Invalid.Source.ExplicitPolygon.Points.Reset();for(const auto& P:TArray<FVector>{{-100,-100,0},{600,-100,0},{600,600,0},{-100,600,0}})Invalid.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Invalid.EnsureGuids();
  TestFalse(TEXT("Cold erase preview rejected"),S->ValidateCutOperations({Invalid}));TestFalse(TEXT("Cold erase update rejected"),S->UpdateCutOperation(Invalid));
  FString AfterFailure;FJsonObjectConverter::UStructToJsonObjectString(S->CutOperations[0],AfterFailure);TestEqual(TEXT("Cold failed command preserves source"),AfterFailure,Source);if(!Verify())return false;
  FGuid Cut;FGuid::Parse(C->GetStringField(TEXT("cut")),Cut);
  {FScopedTransaction Transaction(FText::FromString(TEXT("Cold cut delete")));S->SetFlags(RF_Transactional);S->Modify();for(auto* Component:S->GetComponents()){Component->SetFlags(RF_Transactional);Component->Modify();}if(!S->RemoveCutOperation(Cut))return false;}
  TestEqual(TEXT("Cold source removal"),S->CutOperations.Num(),0);GEditor->UndoTransaction(false);TestEqual(TEXT("Cold undo source"),S->CutOperations.Num(),1);if(!S->RebuildSlabMesh()||!Verify())return false;
 }
 return true;
}
