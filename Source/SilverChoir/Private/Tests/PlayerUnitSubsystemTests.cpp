#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/SIS_UnitInventoryComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveUnitEquipmentTest,"SilverChoir.Player.SaveEquipmentToUnitData",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FSaveUnitEquipmentTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::Game){World=Context.World();break;}
    auto* Manager=UPlayerUnitLibrary::GetPlayerUnitManager(World);
    if(!TestNotNull(TEXT("Unit manager"),Manager))return false;
    FUnitData Record=UPlayerUnitLibrary::CreateUnitDataFromTemplate(FUnitTemplate());
    Record.RuntimeData.CurrentHealth=37.f;
    FText Error;
    if(!TestTrue(TEXT("Load fixture"),Manager->LoadUnitData({Record},Error)))return false;
    auto Shared=Manager->GetUnitDataShared(Record.UnitId);
    Shared->SetSelected(true);
    AActor* Owner=World->SpawnActor<AActor>();
    if(!TestNotNull(TEXT("Inventory owner"),Owner))return false;
    auto* Inventory=NewObject<USIS_UnitInventoryComponent>(Owner);
    Owner->AddInstanceComponent(Inventory);
    const FGameplayTag Hands=FGameplayTag::RequestGameplayTag(TEXT("Item.Hands"));
    auto Equipped=MakeShared<FSIS_ItemInstance>();Equipped->ItemId=FGuid::NewGuid();
    Equipped->ItemLocationInfo.SlotType=ESIS_SlotType::EquipmentSlot;
    Equipped->ItemLocationInfo.EquipmentSlotType=Hands;
    Equipped->ItemLocationInfo.BelongSlotContainerIndex=1;
    Equipped->MarkItemDataCacheDirty();
    auto Child=MakeShared<FSIS_ItemInstance>();Child->ItemId=FGuid::NewGuid();
    Child->ItemLocationInfo.ParentItemId=Equipped->ItemId;Child->MarkItemDataCacheDirty();
    Equipped->ChildInventoryItemDataPtrList.Add(Child);
    Inventory->EquippedSlotRuntimeDataByType.FindOrAdd(Hands).OccupiedItemByIndex.Add(1,Equipped);
    int Notifications=0;
    const auto Handle=Shared->OnDataChanged.AddLambda([&](FGuid ID)
    {
        ++Notifications;
        FText NestedError;
        TestFalse(TEXT("Reject recursive save from data notification"),UPlayerUnitLibrary::SaveEquipmentToUnitData(World,ID,Inventory,NestedError));
    });
    TestTrue(TEXT("Export live equipment to the authoritative record"),UPlayerUnitLibrary::SaveEquipmentToUnitData(World,Record.UnitId,Inventory,Error));
    TestTrue(TEXT("Success clears error"),Error.IsEmpty());
    TestEqual(TEXT("Nested inventory is preserved"),Shared->InventoryItems.Num(),2);
    if(Shared->InventoryItems.Num()==2)
    {
        TestEqual(TEXT("Equipment identity preserved"),Shared->InventoryItems[0].ItemId,Equipped->ItemId);
        TestEqual(TEXT("Equipment slot preserved"),Shared->InventoryItems[0].ItemLocationInfo.BelongSlotContainerIndex,1);
        TestEqual(TEXT("Nested parent identity preserved"),Shared->InventoryItems[1].ItemLocationInfo.ParentItemId,Equipped->ItemId);
    }
    TestTrue(TEXT("Existing shared references retain their address"),Shared==Manager->GetUnitDataShared(Record.UnitId));
    TestEqual(TEXT("Unrelated health retained"),Shared->RuntimeData.CurrentHealth,37.f);
    TestTrue(TEXT("Selection retained"),Shared->IsSelected());
    TestEqual(TEXT("Notifies once"),Notifications,1);
    TestFalse(TEXT("Null component rejected"),UPlayerUnitLibrary::SaveEquipmentToUnitData(World,Record.UnitId,nullptr,Error));
    TestFalse(TEXT("Unknown unit rejected"),UPlayerUnitLibrary::SaveEquipmentToUnitData(World,FGuid::NewGuid(),Inventory,Error));
    TestFalse(TEXT("Missing world rejected"),UPlayerUnitLibrary::SaveEquipmentToUnitData(nullptr,Record.UnitId,Inventory,Error));
    TestEqual(TEXT("Failed saves preserve equipment"),Shared->InventoryItems.Num(),2);
    Inventory->EquippedSlotRuntimeDataByType.Reset();
    TestTrue(TEXT("Empty inventory is a successful save"),UPlayerUnitLibrary::SaveEquipmentToUnitData(World,Record.UnitId,Inventory,Error));
    TestTrue(TEXT("Empty save clears stale equipment"),Shared->InventoryItems.IsEmpty());
    TestEqual(TEXT("Empty save also notifies"),Notifications,2);
    Shared->OnDataChanged.Remove(Handle);
    Owner->Destroy();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedUnitDataTest,"SilverChoir.Player.SharedUnitData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSharedUnitDataTest::RunTest(const FString& Parameters)
{
    FUnitTemplate Template;
    auto Data=UPlayerUnitLibrary::CreateUnitDataBatch(Template,3);
    TestEqual(TEXT("Batch size"),Data.Num(),3);
    TSet<FGuid> IDs;
    for (const auto& Item:Data) IDs.Add(Item.UnitId);
    TestEqual(TEXT("Unique batch IDs"),IDs.Num(),3);
    TestTrue(TEXT("Negative count produces no units"),UPlayerUnitLibrary::CreateUnitDataBatch(Template,-1).IsEmpty());
    TestEqual(TEXT("Template array"),UPlayerUnitLibrary::CreateUnitDataFromTemplates({Template,Template}).Num(),2);
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct=FUnitTemplate::StaticStruct(); Table->AddRow(TEXT("Unit"),Template);
    FText Error; TArray<FUnitData> FromTable;
    TestTrue(TEXT("Repeated row generates distinct units"),UPlayerUnitLibrary::CreateUnitDataFromTable(Table.Get(),{TEXT("Unit"),TEXT("Unit")},FromTable,Error,TEXT("TestTile")));
    for (const auto& Unit:FromTable) TestEqual(TEXT("All created units receive tile"),Unit.RuntimeData.TileId,FName(TEXT("TestTile")));
    TestTrue(TEXT("Repeated row IDs differ"),FromTable.Num()==2 && FromTable[0].UnitId!=FromTable[1].UnitId);
    TestFalse(TEXT("Missing row rejects batch"),UPlayerUnitLibrary::CreateUnitDataFromTable(Table.Get(),{TEXT("Unit"),TEXT("Missing")},FromTable,Error));
    TestTrue(TEXT("No partial table output"),FromTable.IsEmpty());
    TStrongObjectPtr<UPlayerUnitManagerBase> Manager(NewObject<UPlayerUnitManagerBase>());
    Data[0].Profile.PortraitTexture=NewObject<UTexture2D>();
    TWeakObjectPtr<UTexture2D> Portrait=Data[0].Profile.PortraitTexture;
    TestTrue(TEXT("Load succeeds"),Manager->LoadUnitData(Data,Error));
    auto Shared=Manager->GetUnitDataShared(Data[0].UnitId);
    TestTrue(TEXT("Same allocation for all readers"),Shared.Get()==Manager->GetUnitDataShared(Data[0].UnitId).Get());
    int32 Notifications=0;
    auto Handle=Shared->OnDataChanged.AddLambda([&](FGuid ID){ ++Notifications; TestEqual(TEXT("Event identifies unit"),ID,Shared->UnitId); });
    Shared->Modify([](FUnitData& Unit){ Unit.RuntimeData.CurrentHealth=19.f; });
    TestEqual(TEXT("Shared mutation visible to manager"),Manager->GetUnitDataShared(Data[0].UnitId)->RuntimeData.CurrentHealth,19.f);
    TestEqual(TEXT("Modify notifies once"),Notifications,1);
    {
        TStrongObjectPtr<UUnitDataReference> Reference(NewObject<UUnitDataReference>());
        Reference->Initialize(Shared);
        TestTrue(TEXT("Blueprint reference shares canonical allocation"),Reference->GetSharedData().Get()==Shared.Get());
        TestEqual(TEXT("Blueprint snapshot reads latest value"),Reference->GetSnapshot().RuntimeData.CurrentHealth,19.f);
        Reference->Initialize(nullptr);
        TestFalse(TEXT("Unbound reference has no unit ID"),Reference->GetUnitId().IsValid());
    }
    FUnitData Copy=*Shared;
    TestFalse(TEXT("Snapshots do not copy subscriptions"),Copy.OnDataChanged.IsBound());
    Data[0].RuntimeData.CurrentHealth=23.f;
    TestTrue(TEXT("Update succeeds"),Manager->UpdateUnitData(Data[0].UnitId,Data[0]));
    TestEqual(TEXT("Existing reader sees update"),Shared->RuntimeData.CurrentHealth,23.f);
    Data[0].RuntimeData.CurrentHealth=41.f;
    TestTrue(TEXT("Reload succeeds"),Manager->LoadUnitData({Data[0]},Error));
    TestTrue(TEXT("Reload keeps pointer"),Shared.Get()==Manager->GetUnitDataShared(Data[0].UnitId).Get());
    TestEqual(TEXT("Existing reader sees reload"),Shared->RuntimeData.CurrentHealth,41.f);
    TestEqual(TEXT("Merge retains other records"),Manager->GetUnitDataIDs().Num(),3);
    Data[0].RuntimeData.CurrentHealth=99.f;
    TestFalse(TEXT("Invalid ID fails atomically"),Manager->LoadUnitData({Data[0],FUnitData()},Error));
    TestFalse(TEXT("Duplicate ID fails atomically"),Manager->LoadUnitData({Data[0],Data[0]},Error));
    TestEqual(TEXT("Rejected batches leave data unchanged"),Shared->RuntimeData.CurrentHealth,41.f);
    TestFalse(TEXT("Cannot change unit identity"),Manager->UpdateUnitData(Data[0].UnitId,Data[1]));
    TestEqual(TEXT("Updates retain event subscriptions"),Notifications,3);
    Shared->OnDataChanged.Remove(Handle);
    Shared->NotifyDataChanged();
    TestEqual(TEXT("Explicit unbind stops notifications"),Notifications,3);
    Data.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Shared struct keeps portrait alive"),Portrait.IsValid());
    Manager.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("External shared owner keeps portrait alive"),Portrait.IsValid());
    Shared.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Portrait released with last owner"),Portrait.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitTemplateDataTest,"SilverChoir.Player.UnitTemplateData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FUnitTemplateDataTest::RunTest(const FString& Parameters)
{
    FUnitTemplate Template;
    Template.Profile.CodeName=FText::FromString(TEXT("TestUnit"));
    Template.Attributes.MaxHealth=175.f;
    Template.Attributes.MaxStamina=80.f;
    Template.Attributes.Hacking=42;
    auto* Table=NewObject<UDataTable>(); Table->RowStruct=FUnitTemplate::StaticStruct();
    Table->AddRow(TEXT("TestUnit"),Template);
    const auto* Row=Table->FindRow<FUnitTemplate>(TEXT("TestUnit"),TEXT("UnitTemplateTest"));
    if (!TestNotNull(TEXT("DataTable supports unit template row"),Row)) return false;
    const FUnitData First=UPlayerUnitLibrary::CreateUnitDataFromTemplate(*Row);
    const FUnitData Second=UPlayerUnitLibrary::CreateUnitDataFromTemplate(*Row);
    TestTrue(TEXT("Each generated unit has unique valid ID"),First.UnitId.IsValid() && Second.UnitId.IsValid() && First.UnitId!=Second.UnitId);
    TestEqual(TEXT("Health starts at template maximum"),First.RuntimeData.CurrentHealth,175.f);
    TestEqual(TEXT("Stamina starts at template maximum"),First.RuntimeData.CurrentStamina,80.f);
    TestEqual(TEXT("Attributes copied"),First.Attributes.Hacking,42);
    TestEqual(TEXT("Profile copied"),First.Profile.CodeName.ToString(),FString(TEXT("TestUnit")));
    Template.Attributes.MaxHealth=-5.f; Template.Attributes.Medicine=-2;
    const auto Clamped=UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    TestEqual(TEXT("Invalid health normalized"),Clamped.RuntimeData.CurrentHealth,0.f);
    TestEqual(TEXT("Invalid skill normalized"),Clamped.Attributes.Medicine,0);
    TestEqual(TEXT("Source template remains unchanged"),Template.Attributes.MaxHealth,-5.f);
    TestFalse(TEXT("Default struct does not generate an ID"),FUnitData().UnitId.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerUnitSubsystemTest,"SilverChoir.Player.UnitSubsystem",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPlayerUnitSubsystemTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (const auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::Game) { World=Context.World(); break; }
    if (!TestNotNull(TEXT("Game world"),World)) return false;
    TestTrue(TEXT("Subsystem creates default manager"),UPlayerUnitLibrary::IsPlayerUnitSystemReady(World));
    TestFalse(TEXT("Missing context is safe"),UPlayerUnitLibrary::IsPlayerUnitSystemReady(nullptr));
    AActor* Unit=World->SpawnActor<AActor>();
    AActor* Other=World->SpawnActor<AActor>();
    const FName ID(TEXT("Automation_PlayerUnit"));
    TestFalse(TEXT("Empty ID rejected"),UPlayerUnitLibrary::RegisterPlayerUnit(World,NAME_None,Unit));
    TestFalse(TEXT("Null actor rejected"),UPlayerUnitLibrary::RegisterPlayerUnit(World,ID,nullptr));
    TestTrue(TEXT("Register unit through library"),UPlayerUnitLibrary::RegisterPlayerUnit(World,ID,Unit));
    TestTrue(TEXT("Same registration is idempotent"),UPlayerUnitLibrary::RegisterPlayerUnit(World,ID,Unit));
    TestFalse(TEXT("Duplicate ID does not replace unit"),UPlayerUnitLibrary::RegisterPlayerUnit(World,ID,Other));
    TestTrue(TEXT("Resolve by ID"),UPlayerUnitLibrary::GetPlayerUnit(World,ID)==Unit);
    Unit->Destroy();
    TestNull(TEXT("Destroyed actor is not returned"),UPlayerUnitLibrary::GetPlayerUnit(World,ID));
    TestTrue(TEXT("ID can be reused after destruction"),UPlayerUnitLibrary::RegisterPlayerUnit(World,ID,Other));
    TestTrue(TEXT("Unregister succeeds"),UPlayerUnitLibrary::UnregisterPlayerUnit(World,ID));
    TestTrue(TEXT("Unregister does not destroy actor"),IsValid(Other));
    TestFalse(TEXT("Repeated unregister is harmless"),UPlayerUnitLibrary::UnregisterPlayerUnit(World,ID));
    Other->Destroy();
    return true;
}
#endif
