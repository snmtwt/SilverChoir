#include "Core/EHBRoomExplorationSaveGame.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomExplorationSaveTest,"EHB.Runtime.ExplorationSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomExplorationSaveTest::RunTest(const FString& Parameters)
{
 auto* Rooms=GEditor->GetEditorWorldContext().World()->GetSubsystem<UEHBRoomRuntimeSubsystem>();if(!Rooms)return false;
 auto* Original=Rooms->CreateExplorationSave();FName Status;
 const FString Slot=TEXT("EHB_Test_Exploration_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 ON_SCOPE_EXIT{Rooms->RestoreExplorationSave(Original,true,Status);UGameplayStatics::DeleteGameInSlot(Slot,0);};
 const FName A=TEXT("EHB_Save_A"),B=TEXT("EHB_Save_B"),Extra=TEXT("EHB_Save_Extra");
 FEHBRoomAddress First;First.BuildingGuid=FGuid::NewGuid();First.RoomGuid=FGuid::NewGuid();auto Second=First;Second.RoomGuid=FGuid::NewGuid();
 Rooms->MarkExplored(A,First);Rooms->MarkExplored(B,Second);
 if(!TestTrue(TEXT("Actual platform slot save"),Rooms->SaveExplorationToSlot(Slot,0,Status)))return false;
 Rooms->ClearExploration(A);Rooms->ClearExploration(B);Rooms->MarkExplored(Extra,First);
 if(!TestTrue(TEXT("Merge slot load"),Rooms->LoadExplorationFromSlot(Slot,0,false,Status)))return false;
 TestTrue(TEXT("Merge keeps unsaved observer"),Rooms->IsExplored(Extra,First));TestTrue(TEXT("Saved groups restored"),Rooms->IsExplored(A,First)&&Rooms->IsExplored(B,Second));
 if(!TestTrue(TEXT("Replace slot load"),Rooms->LoadExplorationFromSlot(Slot,0,true,Status)))return false;
 TestFalse(TEXT("Replace drops unsaved group"),Rooms->IsExplored(Extra,First));TestFalse(TEXT("No cross-observer leak"),Rooms->IsExplored(A,Second));
 auto State=[&](){FString S;for(const auto& E:Rooms->CreateExplorationSave()->Observers){S+=E.Observer.ToString();for(const auto& R:E.Rooms)S+=R.BuildingGuid.ToString()+R.RoomGuid.ToString();}return S;};const auto Before=State();
 TestFalse(TEXT("Missing slot fails"),Rooms->LoadExplorationFromSlot(Slot+TEXT("_missing"),0,true,Status));TestEqual(TEXT("Missing load leaves live state"),State(),Before);
 TestFalse(TEXT("Invalid slot fails"),Rooms->SaveExplorationToSlot(TEXT("../invalid"),0,Status));TestEqual(TEXT("Invalid save leaves live state"),State(),Before);
 auto* Invalid=Rooms->CreateExplorationSave();Invalid->SchemaVersion=99;
 TestFalse(TEXT("Unknown schema fails"),Rooms->RestoreExplorationSave(Invalid,true,Status));TestEqual(TEXT("Version failure leaves all observers"),State(),Before);
 if(!UGameplayStatics::SaveGameToSlot(Invalid,Slot,0))return false;
 TestFalse(TEXT("Unknown version from actual slot fails"),Rooms->LoadExplorationFromSlot(Slot,0,true,Status));TestEqual(TEXT("Invalid stored version never clears state"),State(),Before);
 Invalid->SchemaVersion=1;auto& Bad=Invalid->Observers.AddDefaulted_GetRef();Bad.Observer=TEXT("BadLateRecord");Bad.Rooms.AddDefaulted();
 TestFalse(TEXT("Bad trailing record fails atomically"),Rooms->RestoreExplorationSave(Invalid,true,Status));TestEqual(TEXT("Late invalid record publishes nothing"),State(),Before);
 Invalid->Observers.Pop();const auto Duplicate=Invalid->Observers[0];Invalid->Observers.Add(Duplicate);TestFalse(TEXT("Duplicate observer rejected"),Rooms->RestoreExplorationSave(Invalid,false,Status));TestEqual(TEXT("Duplicate failure leaves all groups"),State(),Before);
 auto* Empty=NewObject<UEHBRoomExplorationSaveGame>();TestTrue(TEXT("Valid empty replace clears deliberately"),Rooms->RestoreExplorationSave(Empty,true,Status));TestFalse(TEXT("Empty replace clears saved room"),Rooms->IsExplored(A,First));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomExplorationWriteTest,"EHBValidation.Persistence.WriteRoomExploration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomExplorationWriteTest::RunTest(const FString& Parameters)
{
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("RoomExploration"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);FStyledMergeFixture Fixture;if(!Fixture.Create(World,2,true))return false;auto* B=Fixture.Building();
 auto* Rooms=World->GetSubsystem<UEHBRoomRuntimeSubsystem>();if(!Rooms)return false;
 const auto Loops=B->GetClosedLoopsByFloor(1);if(Loops.Num()!=2)return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.RoomExploration.v1"));Data->SetStringField(TEXT("building"),B->BuildingGuid.ToString());
 const FString Slot=TEXT("EHB_Exploration_")+FPackageName::GetShortName(Map);Data->SetStringField(TEXT("slot"),Slot);
 TArray<TSharedPtr<FJsonValue>> Records;
 for(int32 I=0;I<Loops.Num();++I)
 {
  FEHBRoomAddress Address;Address.BuildingGuid=B->BuildingGuid;Address.RoomGuid=Loops[I].LoopGuid;const FName Observer=I==0?TEXT("PlayerA"):TEXT("TeamB");Rooms->MarkExplored(Observer,Address);
  FEHBNodeRoomBoundary Boundary;if(!B->TryGetRoomBoundary(Address.RoomGuid,1,Boundary))return false;FVector Center=FVector::ZeroVector;
  for(FGuid Id:Boundary.NodeGuids){const auto* N=B->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});if(!N)return false;Center+=N->LocalTransform.GetLocation();}
  Center=B->GetActorTransform().TransformPosition(Center/Boundary.NodeGuids.Num()+FVector(0,0,50));const auto Found=Rooms->QueryLocation(Center,{});if(!TestEqual(TEXT("Writer actual room"),Found.Identity.RoomGuid,Address.RoomGuid))return false;
  auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("observer"),Observer.ToString());R->SetStringField(TEXT("room"),Address.RoomGuid.ToString());R->SetStringField(TEXT("position"),Center.ToString());Records.Add(MakeShared<FJsonValueObject>(R));
 }
 FName Status;if(!TestTrue(*Status.ToString(),Rooms->SaveExplorationToSlot(Slot,0,Status)))return false;
 Fixture.Fixture.Actors.Reset();if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;
 Data->SetArrayField(TEXT("records"),Records);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRoomExplorationReadTest,"EHBValidation.Persistence.ReadRoomExploration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRoomExplorationReadTest::RunTest(const FString& Parameters)
{
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("RoomExploration"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.RoomExploration.v1"))return false;
 auto* World=GEditor->GetEditorWorldContext().World();auto* Rooms=World->GetSubsystem<UEHBRoomRuntimeSubsystem>();if(!Rooms)return false;
 FName Status;if(!TestTrue(TEXT("Independent reader loads platform save"),Rooms->LoadExplorationFromSlot(Data->GetStringField(TEXT("slot")),0,true,Status)))return false;
 FGuid BuildingId;if(!FGuid::Parse(Data->GetStringField(TEXT("building")),BuildingId))return false;
 for(const auto& Item:Data->GetArrayField(TEXT("records")))
 {
  const auto R=Item->AsObject();FEHBRoomAddress Address;Address.BuildingGuid=BuildingId;if(!FGuid::Parse(R->GetStringField(TEXT("room")),Address.RoomGuid))return false;const FName Observer(*R->GetStringField(TEXT("observer")));
  TestTrue(TEXT("Cold exploration retains stable identity"),Rooms->IsExplored(Observer,Address));TestFalse(TEXT("Cold observers remain isolated"),Rooms->IsExplored(Observer==TEXT("PlayerA")?FName(TEXT("TeamB")):FName(TEXT("PlayerA")),Address));
  FVector Position;if(!Position.InitFromString(R->GetStringField(TEXT("position"))))return false;
  const auto Found=Rooms->QueryLocation(Position,{});TestEqual(TEXT("Cold world registry finds actual room"),Found.Status,EEHBRoomLocationStatus::Inside);TestTrue(TEXT("Cold room address matches save"),Found.Identity==Address);
 }
 return true;
}
