IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabOutlineEditWriteTest,"EHBValidation.Persistence.WriteSlabOutlineEdit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabOutlineEditWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("SlabOutlineEdit"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FTransientTopologyFixture Fixture;if(!Fixture.Create(World,RF_Transactional))return false;auto* B=Fixture.Building;B->SetActorRotation(FRotator(0,37,0));
 auto* S=World->SpawnActor<AEHB_FloorSlab>();if(!S)return false;Fixture.Actors.Add(S);S->ConfigureDefaultSlab(B,FTransform(FRotator(0,17,0),FVector(0,0,300)),500,20,false);S->VisualExpansion=0;S->LocalTopPolygon={{0,0,0},{500,0,0},{500,500,0},{0,500,0}};
 FEHBCutOperation Cut;for(const auto& P:TArray<FVector>{{200,-100,0},{220,-100,0},{220,600,0},{200,600,0}})Cut.Source.ExplicitPolygon.Points.AddDefaulted_GetRef().LocalPosition=P;Cut.EnsureGuids();if(!S->AddCutOperation(Cut))return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);for(double X:{100.,400.}){const auto R=UEHBBuildingToolset::AddFloorSlabRectangularHole(S,FVector(X,100,0),50,50,0);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
 if(!TestTrue(TEXT("Persistent fixture keeps building attachment before save"),S->GetAttachParentActor()==B))return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.SlabOutlineEdit.v1"));Data->SetStringField(TEXT("building"),B->BuildingGuid.ToString());Data->SetStringField(TEXT("slab"),S->ElementGuid.ToString());Data->SetStringField(TEXT("surface"),S->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")).ToString());
 TArray<TSharedPtr<FJsonValue>> Holes;for(const auto& H:S->LocalHoles){FString Json;FJsonObjectConverter::UStructToJsonObjectString(H,Json);Holes.Add(MakeShared<FJsonValueString>(Json));}Data->SetArrayField(TEXT("holes"),Holes);FString CutJson;FJsonObjectConverter::UStructToJsonObjectString(S->CutOperations[0],CutJson);Data->SetStringField(TEXT("cut"),CutJson);
 for(auto* Actor:Fixture.Actors)Actor->ClearFlags(RF_Transient);if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;Fixture.Actors.Reset();FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSlabOutlineEditReadTest,"EHBValidation.Persistence.ReadSlabOutlineEdit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSlabOutlineEditReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("SlabOutlineEdit"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.SlabOutlineEdit.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Loaded outline edit map"),World->GetOutermost()->GetName(),Map);FGuid BuildingId,SlabId;FGuid::Parse(Data->GetStringField(TEXT("building")),BuildingId);FGuid::Parse(Data->GetStringField(TEXT("slab")),SlabId);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==BuildingId)B=*It;if(!B)return false;
 auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(SlabId));if(!S||S->LocalHoles.Num()!=2||S->CutOperations.Num()!=1)return false;
 for(int32 I=0;I<2;++I){FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->LocalHoles[I],Actual);TestEqual(TEXT("Exact cold hole source"),Actual,Data->GetArrayField(TEXT("holes"))[I]->AsString());}FString Cut;FJsonObjectConverter::UStructToJsonObjectString(S->CutOperations[0],Cut);TestEqual(TEXT("Exact cold cut source"),Cut,Data->GetStringField(TEXT("cut")));TestEqual(TEXT("Stable surface identity"),S->FindLogicalSurfaceIdentity(TEXT("Slab.Surface")).ToString(),Data->GetStringField(TEXT("surface")));
 if(!S->RebuildSlabMesh())return false;TArray<FEHBPlanarSurfaceRegion> Regions;if(!S->BuildEffectiveDisplayRegions(Regions))return false;TestEqual(TEXT("Two cold fragments"),Regions.Num(),2);for(const auto& R:Regions)TestEqual(TEXT("Each fragment retains hole"),R.HoleLoops.Num(),1);TestTrue(TEXT("Complete cold area"),FMath::Abs(CutSlabTopArea(S)-235000)<0.01);
 TArray<FEHBLogicalSurfaceDefinition> Logical;FName LogicalStatus;
 const bool bColdLogical=S->QueryLogicalBaseSurfaces(Logical,LogicalStatus);
 if(!TestTrue(*FString::Printf(TEXT("Cold logical base query: %s parent=%s owner=%s"),*LogicalStatus.ToString(),*GetNameSafe(S->GetAttachParentActor()),*GetNameSafe(S->OwningBuilding.Get())),bColdLogical))return false;
 TestEqual(TEXT("External cut excluded from authored base"),Logical[0].GetAreaCm2(),245000.0);
 TArray<FEHBLogicalSurfaceRegion> CandidateDisplay;
 if(!TestTrue(TEXT("Cold candidate display includes committed cuts"),S->BuildCandidateDisplayRegions(S->LocalTopPolygon,S->LocalHoles,S->CutOperations,CandidateDisplay,LogicalStatus)))return false;
 FEHBLogicalSurfaceDefinition Display;Display.Regions=CandidateDisplay;TestEqual(TEXT("Cold candidate display keeps both pieces"),CandidateDisplay.Num(),2);TestEqual(TEXT("Cold candidate equals actual top area"),Display.GetAreaCm2(),235000.0);
 const auto SavedHoles=S->LocalHoles;const auto SurfaceId=Logical[0].SurfaceGuid;
 S->LocalHoles.AddDefaulted_GetRef().LocalPolygon={{200,-100,0},{220,-100,0},{220,600,0},{200,600,0}};
 if(!TestTrue(TEXT("Cold source spanning hole normalizes"),S->QueryLogicalBaseSurfaces(Logical,LogicalStatus)))return false;
 TestEqual(TEXT("Cold logical source fragments"),Logical[0].Regions.Num(),2);TestEqual(TEXT("Cold normalized area"),Logical[0].GetAreaCm2(),235000.0);TestEqual(TEXT("Cold normalization preserves identity"),Logical[0].SurfaceGuid,SurfaceId);
 S->LocalHoles=SavedHoles;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Before=SlabEditState118(S);
 TestFalse(TEXT("Cold gap crossing rejected"),UEHBBuildingToolset::AddFloorSlabRectangularHole(S,FVector(210,300,0),80,50,0).bSucceeded);TestFalse(TEXT("Cold invalid hole corner rejected"),S->UpdateCornerWorldLocation(0,0,S->GetActorTransform().TransformPosition(FVector(1.e12,0,0))));TestEqual(TEXT("Cold failed edits preserve state"),SlabEditState118(S),Before);
 const auto Added=UEHBBuildingToolset::AddFloorSlabCircularHole(S,FVector(100,350,0),40,12);if(!TestTrue(*Added.Message,Added.bSucceeded))return false;TestEqual(TEXT("Cold continue opening"),S->LocalHoles.Num(),3);const auto After=SlabEditState118(S);GEditor->UndoTransaction();TestEqual(TEXT("Cold hole undo"),SlabEditState118(S),Before);GEditor->RedoTransaction();TestEqual(TEXT("Cold hole redo"),SlabEditState118(S),After);GEditor->UndoTransaction(false);GEditor->SelectNone(false,true,false);return true;
}
