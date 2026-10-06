#include "Core/EHBDisplayPartition.h"

namespace
{
 FString CanonicalCutOperationManifest(FAutomationTestBase& Test,const FString& Json)
 {
  FEHBCutOperation Value;FString Result;
  if(!FJsonObjectConverter::JsonObjectStringToUStruct(Json,&Value,0,0)){Test.AddError(TEXT("Invalid cut operation manifest"));return Result;}
  FJsonObjectConverter::UStructToJsonObjectString(Value,Result);return Result;
 }
 FString CanonicalDisplayPartitionManifest(FAutomationTestBase& Test,const FString& Json)
 {
  // Older manifests omit fields introduced by later source-contract versions.
  // Decode defaults, then compare every field exactly in the current schema.
  FEHBSlabDisplayPartition Value;FString Result;
  if(!FJsonObjectConverter::JsonObjectStringToUStruct(Json,&Value,0,0))
  {Test.AddError(TEXT("Invalid display partition manifest"));return Result;}
  FJsonObjectConverter::UStructToJsonObjectString(Value,Result);return Result;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayPartitionWriteTest,"EHBValidation.Persistence.WriteDisplayPartition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayPartitionWriteTest::RunTest(const FString& Parameters)
{
 FString RequestedMap;FParse::Value(FCommandLine::Get(),TEXT("EHBPersistenceMap="),RequestedMap);const bool FromRemoval=RequestedMap.StartsWith(TEXT("/Game/EHB_Refactor_Validation/GeneratedDisplayRemoval_"));
 FString Map,Path;if(!PersistencePaths(Map,Path,FromRemoval?TEXT("DisplayRemoval"):TEXT("DisplayPartition"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!TestTrue(*FString::Printf(TEXT("Create display fixture%d"),Mode),Setup.Create(World,Mode,true)))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));
  if(FromRemoval)for(FGuid Id:Setup.Slabs){auto* S=CastChecked<AEHB_FloorSlab>(B->FindElementActorByGuid(Id));S->VisualExpansion=10;if(!S->RebuildSlabMesh())return false;S->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);}
  const auto Removal=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*Removal.Message,Removal.bSucceeded))return false;
  TArray<FEHBDisplayPartitionInput> Inputs;
  for(int32 I=0;!FromRemoval&&I<Setup.Slabs.Num();++I)
  {
   auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[I]));if(!S)return false;S->VisualExpansion=10;TArray<FEHBLogicalSurfaceDefinition> Base;FName Status;TArray<FVector> Expanded;
   const bool Queried=S->QueryLogicalBaseSurfaces(Base,Status);if(!TestTrue(*FString::Printf(TEXT("Query display fixture%d slab%d %s"),Mode,I,*Status.ToString()),Queried)||!TestTrue(TEXT("Expand source outline"),S->BuildEffectiveOuterPolygon(Expanded)))return false;
   auto& In=Inputs.AddDefaulted_GetRef();In.Base=Base[0];In.Priority=I;auto& R=In.RequestedDisplay.AddDefaulted_GetRef();for(const auto& V:Expanded)R.Boundary.Add({V.X,V.Y});
  }
  TArray<FEHBLogicalSurfaceDefinition> Result;FName Status;if(!FromRemoval){const bool Planned=FEHBDisplayPartition::Build(Inputs,Result,Status);if(!TestTrue(*FString::Printf(TEXT("Plan display fixture%d: %s"),Mode,*Status.ToString()),Planned))return false;}
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());TArray<TSharedPtr<FJsonValue>> Slabs;
  for(int32 I=0;I<Setup.Slabs.Num();++I)
  {
   auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Setup.Slabs[I]));if(!S)return false;FEHBSlabDisplayPartition P;
   if(FromRemoval){P=S->DisplayPartition;if(!TestTrue(TEXT("Real removal supplied persisted allocation"),P.IsActive()))return false;}else{P.SourcePolygon=S->LocalTopPolygon;P.SourceExpansion=S->VisualExpansion;P.Priority=I;P.Regions=Result[I].Regions;}
   if(!FromRemoval&&!TestTrue(TEXT("Persist actual planned slab"),S->SetPartitionedSlabOutline(P.SourcePolygon,P)))
   {
    FEHBDisplayPartitionInput Probe;Probe.Base.ElementGuid=S->ElementGuid;Probe.Base.SurfaceGuid=S->FindLogicalSurfaceIdentity(TEXT("Slab.Surface"));Probe.Base.SourceName=TEXT("Slab.Surface");auto& R=Probe.Base.Regions.AddDefaulted_GetRef();for(const auto& V:P.SourcePolygon)R.Boundary.Add({V.X,V.Y});Probe.RequestedDisplay=P.Regions;TArray<FEHBLogicalSurfaceDefinition> Checked;FName Why;const bool Valid=FEHBDisplayPartition::Build({Probe},Checked,Why,true);
    FString Json;FJsonObjectConverter::UStructToJsonObjectString(P,Json);AddError(FString::Printf(TEXT("Partition mode%d index%d regions%d holes%d cuts%d previews%d foundation%d top%.9f standalone%d status%s data%s"),Mode,I,P.Regions.Num(),S->LocalHoles.Num(),S->CutOperations.Num(),S->PreviewCutters.Num(),S->bIsFoundation,S->GetTopZ(),Valid,*Why.ToString(),*Json));return false;
   }if(!FromRemoval)S->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
   auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("element"),S->ElementGuid.ToString());FString Json;FJsonObjectConverter::UStructToJsonObjectString(P,Json);V->SetStringField(TEXT("partition"),Json);FEHBLogicalSurfaceDefinition Display;Display.Regions=P.Regions;V->SetNumberField(TEXT("displayArea"),Display.GetAreaCm2());Slabs.Add(MakeShared<FJsonValueObject>(V));
  }
  C->SetArrayField(TEXT("slabs"),Slabs);Cases.Add(MakeShared<FJsonValueObject>(C));B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Setup.Fixture.Actors.Reset();
 }
 if(!TestTrue(TEXT("Save native display map"),UEditorLoadingAndSavingUtils::SaveMap(World,Map)))return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.DisplayPartition.v1"));Data->SetBoolField(TEXT("positiveExpansionRemoval"),FromRemoval);Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBDisplayPartitionReadTest,"EHBValidation.Persistence.ReadDisplayPartition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBDisplayPartitionReadTest::RunTest(const FString& Parameters)
{
 FString RequestedMap;FParse::Value(FCommandLine::Get(),TEXT("EHBPersistenceMap="),RequestedMap);const bool FromRemoval=RequestedMap.StartsWith(TEXT("/Game/EHB_Refactor_Validation/GeneratedDisplayRemoval_"));
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,FromRemoval?TEXT("DisplayRemoval"):TEXT("DisplayPartition"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.DisplayPartition.v1"))return false;
 bool WasRemoval=false;Data->TryGetBoolField(TEXT("positiveExpansionRemoval"),WasRemoval);if(!TestEqual(TEXT("Saved fixture creation path is explicit"),WasRemoval,FromRemoval))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold display map loaded"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three binding cases"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Case:Data->GetArrayField(TEXT("cases")))
 {
  auto C=Case->AsObject();FGuid Id;FGuid::Parse(C->GetStringField(TEXT("building")),Id);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id)B=*It;if(!B)return false;
  for(const auto& Item:C->GetArrayField(TEXT("slabs")))
  {
   auto V=Item->AsObject();FGuid Element;FGuid::Parse(V->GetStringField(TEXT("element")),Element);auto* S=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Element));if(!S)return false;
   FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("All saved display fields exact"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));TestTrue(TEXT("Cold provenance includes allocation"),S->IsRecordedOutlineUnchanged());
   if(!TestTrue(TEXT("Cold allocation rebuilds mesh"),S->RebuildSlabMesh()))return false;
   FName Status;TArray<FEHBFloorSupportSurface> Tops;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(S,Tops,Status))return false;TArray<FEHBFloorFinishRegion> Regions;for(const auto& T:Tops){auto& R=Regions.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;}double Area=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions,Tops,Area,Status))return false;
   TArray<FEHBFloorFinishRegion> Expected;TArray<FEHBFloorSupportSurface> ExpectedTops;
   for(const auto& D:S->DisplayPartition.Regions){auto& R=Expected.AddDefaulted_GetRef();for(const auto& P:D.Boundary)R.OuterPolygon.Add(S->GetElementLocalTransform().TransformPosition(FVector(P.X,P.Y,S->GetTopZ())));for(const auto& H:D.Holes){auto& Q=R.Holes.AddDefaulted_GetRef();for(const auto& P:H.Vertices)Q.LocalPolygon.Add(S->GetElementLocalTransform().TransformPosition(FVector(P.X,P.Y,S->GetTopZ())));}auto& T=ExpectedTops.AddDefaulted_GetRef();T.OuterPolygon=R.OuterPolygon;T.Holes=R.Holes;}
   double ExpectedArea=0,Intersection=0;if(!FEHBFloorContactGeometry::MeasureArea(Expected,ExpectedTops,ExpectedArea,Status)||!FEHBFloorContactGeometry::MeasureArea(Expected,Tops,Intersection,Status))return false;
   TestTrue(*FString::Printf(TEXT("Cold actual display matches persisted domain on same building grid: actual%.6f expected%.6f intersection%.6f"),Area,ExpectedArea,Intersection),FMath::IsNearlyEqual(Area,ExpectedArea,0.01)&&FMath::IsNearlyEqual(Area,Intersection,0.01));
  }
  auto VerifyEditable=[&](AEHBBuildingActorBase* Building,FGuid Element)
  {
   GEditor->SelectNone(false,true,false);GEditor->SelectActor(Building,true,false);
   auto* Slab=Cast<AEHB_FloorSlab>(Building->FindElementActorByGuid(Element));if(!Slab)return false;
   const auto Before=LiveNodeAuthoritySnapshot(Building);FString MasksBefore;FJsonObjectConverter::UStructToJsonObjectString(Slab->DisplayPartition,MasksBefore);
   auto Polygon=Slab->LocalTopPolygon;const auto Center=FBox(Polygon).GetCenter();for(auto& P:Polygon){P.X=Center.X+(P.X-Center.X)*0.9;P.Y=Center.Y+(P.Y-Center.Y)*0.9;}
   const auto R=UEHBBuildingToolset::EditFinishRegion(Building,Element,Building->RelationshipGraphRevision,Building->GetElementGeometryRevision(Element),Polygon,{},false);
   if(!TestTrue(*FString::Printf(TEXT("Cold/copied partition edit: %s"),*R.Message),R.bSucceeded))return false;
   TestEqual(TEXT("Cold/copied edit updates partition source"),Slab->DisplayPartition.SourcePolygon,Polygon);
   GEditor->UndoTransaction(false);TestEqual(TEXT("Cold/copied resize undo source"),LiveNodeAuthoritySnapshot(Building),Before);
   FString MasksAfter;FJsonObjectConverter::UStructToJsonObjectString(Slab->DisplayPartition,MasksAfter);TestEqual(TEXT("Cold/copied resize undo partition"),MasksAfter,MasksBefore);return true;
  };
  FGuid First;FGuid::Parse(C->GetArrayField(TEXT("slabs"))[0]->AsObject()->GetStringField(TEXT("element")),First);if(!VerifyEditable(B,First))return false;
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3200,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded))return false;
  for(const auto& Item:C->GetArrayField(TEXT("slabs"))){auto V=Item->AsObject();FGuid Old;FGuid::Parse(V->GetStringField(TEXT("element")),Old);const auto* New=Copy.IdentityDraft.ElementGuids.Find(Old);if(!New)return false;auto* S=Cast<AEHB_FloorSlab>(Copy.Building->FindElementActorByGuid(*New));if(!S)return false;FString Actual;FJsonObjectConverter::UStructToJsonObjectString(S->DisplayPartition,Actual);TestEqual(TEXT("Copy preserves authored display priority and local data"),Actual,CanonicalDisplayPartitionManifest(*this,V->GetStringField(TEXT("partition"))));TestTrue(TEXT("Copied partition rebuilds"),S->RebuildSlabMesh());}
  if(!VerifyEditable(Copy.Building,Copy.IdentityDraft.ElementGuids.FindChecked(First)))return false;
  GEditor->UndoTransaction(false);
 }
 return true;
}
