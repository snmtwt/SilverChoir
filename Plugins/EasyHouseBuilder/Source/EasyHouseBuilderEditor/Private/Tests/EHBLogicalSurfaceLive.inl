#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Core/EHBLogicalSurfaceAdjacency.h"
namespace
{
 FString LogicalSurfaceIdentitySnapshot(AEHBBuildingActorBase* B)
 {
  auto Elements=B->QueryElements(FEHBElementQuery());Elements.Sort([](const auto& A,const auto& C){return A.ElementGuid<C.ElementGuid;});FString Result;
  for(auto* E:Elements)
  {
   auto Ids=E->LogicalSurfaceIdentities;Ids.Sort([](const auto& A,const auto& C){return A.Name==C.Name?A.SubIndex<C.SubIndex:A.Name.LexicalLess(C.Name);});FString Text;Result+=E->ElementGuid.ToString()+E->LogicalSurfaceIdentityOwner.ToString();
   for(const auto& I:Ids){FJsonObjectConverter::UStructToJsonObjectString(I,Text);Result+=Text;}
  }
  return Result;
 }
 bool CheckLogicalSurfaceCopies(FAutomationTestBase& Test,AEHBBuildingActorBase* Source,const FEHBBuildingCopyResult& Copy)
 {
  TSet<FGuid> OldIds;for(auto* E:Source->QueryElements(FEHBElementQuery()))for(const auto& I:E->LogicalSurfaceIdentities)OldIds.Add(I.SurfaceGuid);
  TSet<FGuid> NewIds;
  for(auto* E:Source->QueryElements(FEHBElementQuery()))
  {
   auto* C=Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(E->ElementGuid));if(!C)return false;
   Test.TestEqual(TEXT("Copy preserves all semantic ports"),C->LogicalSurfaceIdentities.Num(),E->LogicalSurfaceIdentities.Num());Test.TestEqual(TEXT("Copy identity table belongs to copied element"),C->LogicalSurfaceIdentityOwner,C->ElementGuid);
   for(const auto& I:E->LogicalSurfaceIdentities){const auto Id=C->FindLogicalSurfaceIdentity(I.Name,I.SubIndex);Test.TestTrue(TEXT("Each copied surface ID is new and unique"),Id.IsValid()&&!OldIds.Contains(Id)&&!NewIds.Contains(Id));NewIds.Add(Id);}
   TInlineComponentArray<UEHBArchitecturalSurfaceComponent*> Meshes(C);for(auto* M:Meshes)if(!M->SurfaceName.IsNone())Test.TestEqual(TEXT("Copied renderer consumes owned surface ID"),M->SurfaceGuid,C->FindLogicalSurfaceIdentity(M->SurfaceName,M->SurfaceSubIndex));
  }
  return true;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSurfaceLiveTest,"EHB.Surfaces.LogicalLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSurfaceLiveTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{EHBBuildingCopy::FailAfterImport=false;EHBBuildingCopy::FailAfterApply=false;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;auto* B=Setup.Building();const auto Before=LogicalSurfaceIdentitySnapshot(B);FName Status;
  auto* Floor=B->FindElementActorByGuid(Setup.Floors[0]);TArray<FEHBLogicalSurfaceDefinition> Surfaces;
  {FEHBChangeNotificationBatch Batch(*B);TestFalse(TEXT("Read refuses unpublished edit"),Floor->QueryLogicalBaseSurfaces(Surfaces,Status));TestTrue(TEXT("Refused logical read has no partial output"),Surfaces.IsEmpty());Batch.Rollback();}
  TestTrue(TEXT("Pure logical query outside edit"),Floor->QueryLogicalBaseSurfaces(Surfaces,Status));TestEqual(TEXT("Query does not rewrite identities"),LogicalSurfaceIdentitySnapshot(B),Before);
  const auto Removal=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*Removal.Message,Removal.bSucceeded))return false;
  const auto After=LogicalSurfaceIdentitySnapshot(B);
  TArray<FEHBLogicalSurfaceDefinition> SlabSurfaces;for(FGuid Id:Setup.Slabs){TArray<FEHBLogicalSurfaceDefinition> Values;if(!B->FindElementActorByGuid(Id)->QueryLogicalBaseSurfaces(Values,Status))return false;SlabSurfaces.Append(Values);}
  TArray<FEHBLogicalSurfaceSharedEdge> Shared;const bool Adjacent=FEHBLogicalSurfaceAdjacency::Build(SlabSurfaces,Shared,Status);if(!TestTrue(*FString::Printf(TEXT("Actual material partition adjacency: %s"),*Status.ToString()),Adjacent))return false;double SharedLength=0;for(const auto& Edge:Shared){SharedLength+=(Edge.End-Edge.Start).Size();TestTrue(TEXT("Shared edge names both actual slab elements"),Setup.Slabs.Contains(Edge.A.ElementGuid)&&Setup.Slabs.Contains(Edge.B.ElementGuid)&&Edge.A.ElementGuid!=Edge.B.ElementGuid);}TestTrue(TEXT("Actual merged net seam has 480 cm shared length"),FMath::IsNearlyEqual(SharedLength,480.0,0.001));
  for(FGuid Id:Setup.Floors){TArray<FEHBLogicalSurfaceDefinition> Value;if(!B->FindElementActorByGuid(Id)->QueryLogicalBaseSurfaces(Value,Status))return false;TestEqual(TEXT("Material partition retains floor surface ID"),Value[0].SurfaceGuid,B->FindElementActorByGuid(Id)->FindLogicalSurfaceIdentity(TEXT("Floor.Top")));}
  TestTrue(TEXT("Undo surface migration with wall deletion"),GEditor->UndoTransaction());TestEqual(TEXT("Undo restores identity table exactly"),LogicalSurfaceIdentitySnapshot(B),Before);TestTrue(TEXT("Redo surface migration"),GEditor->RedoTransaction());TestEqual(TEXT("Redo identity table exact"),LogicalSurfaceIdentitySnapshot(B),After);
  for(int32 Phase=0;Phase<2;++Phase)
  {
   EHBBuildingCopy::FailAfterImport=Phase==0;EHBBuildingCopy::FailAfterApply=Phase==1;const auto Failed=EHBBuildingCopy::Execute(B,FVector(3200,0,0));TestEqual(TEXT("Copied surface failure is rolled back"),Failed.Status,FName(TEXT("BuildingCopyFailedRolledBack")));TestEqual(TEXT("Failed copy preserves all source surface IDs"),LogicalSurfaceIdentitySnapshot(B),After);
  }
  const auto Copy=EHBBuildingCopy::Execute(B,FVector(3200,0,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded)||!CheckLogicalSurfaceCopies(*this,B,Copy))return false;Setup.Fixture.Actors.Append(Copy.Actors);const auto Copied=LogicalSurfaceIdentitySnapshot(Copy.Building);
  TestTrue(TEXT("Undo logical surface copy"),GEditor->UndoTransaction());TestEqual(TEXT("Copy undo leaves source IDs exact"),LogicalSurfaceIdentitySnapshot(B),After);TestTrue(TEXT("Redo logical surface copy"),GEditor->RedoTransaction());TestEqual(TEXT("Copy redo retains reserved surface IDs"),LogicalSurfaceIdentitySnapshot(Copy.Building),Copied);GEditor->UndoTransaction(false);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSurfaceWriteTest,"EHBValidation.Persistence.WriteLogicalSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSurfaceWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("LogicalSurface")))return false;if(FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path)){AddError(TEXT("Refusing to overwrite logical surface evidence"));return false;}
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<AEHBBuildingActorBase*> Buildings;TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Mode,true))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));const auto R=UEHBBuildingToolset::RemoveWalls(B,{Setup.Shared},B->RelationshipGraphRevision,false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());Cases.Add(MakeShared<FJsonValueObject>(C));Buildings.Add(B);B->ClearFlags(RF_Transient);for(auto* E:B->QueryElements(FEHBElementQuery()))E->ClearFlags(RF_Transient);Setup.Fixture.Actors.Reset();
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;
 for(int32 I=0;I<Buildings.Num();++I)
 {
  auto* B=Buildings[I];auto C=Cases[I]->AsObject();C->SetStringField(TEXT("identities"),LogicalSurfaceIdentitySnapshot(B));C->SetStringField(TEXT("state"),LiveNodeAuthoritySnapshot(B));TArray<TSharedPtr<FJsonValue>> Definitions;
  for(auto* E:B->QueryElements(FEHBElementQuery()))if(E->IsA<AEHB_Floor>()||E->IsA<AEHB_FloorSlab>()){TArray<FEHBLogicalSurfaceDefinition> Values;FName Status;if(!E->QueryLogicalBaseSurfaces(Values,Status))return false;for(const auto& V:Values)Definitions.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(V)));}C->SetArrayField(TEXT("surfaces"),Definitions);
 }
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.LogicalSurface.v1"));Data->SetArrayField(TEXT("cases"),Cases);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Save logical identity and plane manifest"),FFileHelper::SaveStringToFile(Json,*Path));GEditor->SelectNone(false,true,false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalSurfaceReadTest,"EHBValidation.Persistence.ReadLogicalSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalSurfaceReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("LogicalSurface"))||!FFileHelper::LoadFileToString(Json,*Path))return false;TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.LogicalSurface.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold logical surface map current"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("Three binding modes persisted"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& V:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=V->AsObject();FGuid Id;FGuid::Parse(C->GetStringField(TEXT("building")),Id);AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Id)B=*It;if(!B)return false;
  TestEqual(TEXT("All semantic surface IDs survive save and reload"),LogicalSurfaceIdentitySnapshot(B),C->GetStringField(TEXT("identities")));TestEqual(TEXT("Persisted underlying geometry remains exact"),StyledPersistenceSnapshot(LiveNodeAuthoritySnapshot(B)),StyledPersistenceSnapshot(C->GetStringField(TEXT("state"))));TestEqual(TEXT("Four persisted logical floor/slab surfaces"),C->GetArrayField(TEXT("surfaces")).Num(),4);
  for(const auto& S:C->GetArrayField(TEXT("surfaces")))
  {
   FEHBLogicalSurfaceDefinition Saved;if(!FJsonObjectConverter::JsonObjectToUStruct(S->AsObject().ToSharedRef(),&Saved))return false;auto* E=B->FindElementActorByGuid(Saved.ElementGuid);if(!E)return false;TArray<FEHBLogicalSurfaceDefinition> Values;FName Status;if(!E->QueryLogicalBaseSurfaces(Values,Status)||Values.Num()!=1)return false;
   auto Actual=Values[0];TestTrue(TEXT("Cold rigid plane within 1e-9 engine transform tolerance"),Actual.PlaneToBuilding.Equals(Saved.PlaneToBuilding,1.e-9));Actual.PlaneToBuilding=Saved.PlaneToBuilding;FString Expected,Current;FJsonObjectConverter::UStructToJsonObjectString(Saved,Expected);FJsonObjectConverter::UStructToJsonObjectString(Actual,Current);TestEqual(TEXT("All other logical surface fields exact"),Current,Expected);
  }
  const auto Before=LogicalSurfaceIdentitySnapshot(B);GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,3200,0));if(!TestTrue(*Copy.Status.ToString(),Copy.bSucceeded)||!CheckLogicalSurfaceCopies(*this,B,Copy))return false;const auto Copied=LogicalSurfaceIdentitySnapshot(Copy.Building);GEditor->UndoTransaction();GEditor->RedoTransaction();TestEqual(TEXT("Cold copy redo retains mapped IDs"),LogicalSurfaceIdentitySnapshot(Copy.Building),Copied);GEditor->UndoTransaction(false);TestEqual(TEXT("Cold source identity remains unchanged"),LogicalSurfaceIdentitySnapshot(B),Before);
 }
 GEditor->SelectNone(false,true,false);return true;
}
