#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Vehicles/VehicleDataLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FVehicleTemplate VehicleStoreTemplate(const TCHAR* Name, int32 Seats)
{
    FVehicleTemplate Template;
    Template.Profile.VehicleName = FText::FromString(Name);
    Template.Attributes.PassengerCapacity = Seats;
    Template.Attributes.MaxDurability = 230.f;
    Template.Attributes.MaxFuel = 75.f;
    Template.StrategicMovementData.SpeedTilesPerHour = 2.5f;
    return Template;
}

UDataTable* MakeVehicleStoreTable()
{
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FVehicleTemplate::StaticStruct();
    // Insert deliberately out of lexical order. Full-table loading promises deterministic order.
    Table->AddRow(TEXT("ZuluSix"), VehicleStoreTemplate(TEXT("Six seat transport"), 6));
    Table->AddRow(TEXT("AlphaTwo"), VehicleStoreTemplate(TEXT("Two seat coupe"), 2));
    Table->AddRow(TEXT("MikeFour"), VehicleStoreTemplate(TEXT("Four seat saloon"), 4));
    return Table;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVehicleSharedDataTest, "SilverChoir.Player.Vehicles.SharedData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVehicleSharedDataTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UPlayerUnitManagerBase> Manager(NewObject<UPlayerUnitManagerBase>());
    FVehicleData First = UVehicleDataLibrary::CreateVehicleDataFromTemplate(VehicleStoreTemplate(TEXT("First"), 4), TEXT("VehicleTileA"));
    FVehicleData Second = UVehicleDataLibrary::CreateVehicleDataFromTemplate(VehicleStoreTemplate(TEXT("Second"), 6), TEXT("VehicleTileB"));
    FVehicleData Third = UVehicleDataLibrary::CreateVehicleDataFromTemplate(VehicleStoreTemplate(TEXT("Third"), 2), TEXT("VehicleTileA"));
    First.Profile.PreviewImage = NewObject<UTexture2D>();
    TWeakObjectPtr<UTexture2D> Image = First.Profile.PreviewImage;
    FText Error = FText::FromString(TEXT("Stale error"));
    TestTrue(TEXT("Initial vehicle load succeeds"), Manager->LoadVehicleData({First, Second}, Error));
    TestTrue(TEXT("Successful load clears error"), Error.IsEmpty());
    TSharedPtr<FVehicleData> Shared = Manager->GetVehicleDataShared(First.VehicleId);
    if (!TestTrue(TEXT("Loaded vehicle has a shared record"), Shared.IsValid())) return false;
    TestTrue(TEXT("Every reader receives the canonical allocation"), Shared.Get() == Manager->GetVehicleDataShared(First.VehicleId).Get());
    TestFalse(TEXT("Unknown ID does not create a vehicle"), Manager->GetVehicleDataShared(FGuid::NewGuid()).IsValid());
    FVehicleData Snapshot = First;
    TestFalse(TEXT("Missing snapshot reports failure"), Manager->GetVehicleDataSnapshot(FGuid::NewGuid(), Snapshot));
    TestFalse(TEXT("Missing snapshot clears stale identity"), Snapshot.VehicleId.IsValid());
    TestEqual(TEXT("Querying never changes store size"), Manager->GetVehicleDataIDs().Num(), 2);

    int32 Notifications = 0;
    bool bInspectBatch = false;
    bool bReconciledForReload = false;
    bool bInspectDirectMutation = false;
    bool bReconciledForDirectMutation = false;
    bool bNestedModifyRan = false;
    bool bOtherVehicleModifyRan = false;
    const FGuid OriginalId = First.VehicleId;
    FDelegateHandle Reconciled = Manager->OnVehicleDataReconcile.AddLambda([&](const TArray<FGuid>& Ids)
    {
        TestTrue(TEXT("Association reconciliation holds the vehicle write guard"), Manager->IsUpdatingVehicleData());
        if (bInspectDirectMutation) bReconciledForDirectMutation = true;
        if (bInspectBatch)
        {
            bReconciledForReload = true;
            TestTrue(TEXT("Batch association reconciliation sees all committed records"),
                Manager->GetVehicleDataShared(OriginalId).IsValid() && Manager->GetVehicleDataShared(Third.VehicleId).IsValid());
        }
    });
    FDelegateHandle Changed = Shared->OnDataChanged.AddLambda([&](FGuid Id)
    {
        ++Notifications;
        TestEqual(TEXT("Notification identifies the vehicle"), Id, OriginalId);
        TestTrue(TEXT("Notification marks the shared record busy"), Shared->IsNotifyingDataChanged());
        Shared->Modify([&](FVehicleData& Data) { bNestedModifyRan = true; });
        if (bInspectDirectMutation)
        {
            TestTrue(TEXT("Direct shared mutation reconciles before consumer callbacks"), bReconciledForDirectMutation);
            TestTrue(TEXT("Direct shared mutation locks manager for all consumer callbacks"), Manager->IsUpdatingVehicleData());
            FText NestedError;
            TestFalse(TEXT("Direct mutation callback cannot reenter manager load"), Manager->LoadVehicleData({Second}, NestedError));
            TestFalse(TEXT("Direct mutation callback cannot remove another record"), Manager->RemoveVehicleData(Second.VehicleId, NestedError));
            if (const auto Other = Manager->GetVehicleDataShared(Second.VehicleId))
                Other->Modify([&](FVehicleData& Data) { bOtherVehicleModifyRan = true; });
        }
        if (bInspectBatch)
        {
            TestTrue(TEXT("Association reconciliation precedes consumer notification"), bReconciledForReload);
            TestTrue(TEXT("All batch records exist before the first notification"), Manager->GetVehicleDataShared(Third.VehicleId).IsValid());
            FText NestedError;
            TestFalse(TEXT("Notification cannot reenter batch load"), Manager->LoadVehicleData({Second}, NestedError));
            TestFalse(TEXT("Rejected reentrant load explains failure"), NestedError.IsEmpty());
            TestFalse(TEXT("Notification cannot reenter update"), Manager->UpdateVehicleData(Second.VehicleId, Second));
        }
    });
    bInspectDirectMutation = true;
    Shared->Modify([](FVehicleData& Data)
    {
        Data.RuntimeData.CurrentFuel = 19.f;
        Data.VehicleId = FGuid::NewGuid();
    });
    bInspectDirectMutation = false;
    TestFalse(TEXT("Direct mutation releases manager write guard on return"), Manager->IsUpdatingVehicleData());
    TestEqual(TEXT("Modify preserves identity"), Shared->VehicleId, OriginalId);
    TestEqual(TEXT("All readers see mutation"), Manager->GetVehicleDataShared(OriginalId)->RuntimeData.CurrentFuel, 19.f);
    TestEqual(TEXT("Modify notifies once"), Notifications, 1);
    TestFalse(TEXT("Nested Modify is suppressed"), bNestedModifyRan);
    TestFalse(TEXT("Notification cannot mutate another canonical vehicle through its shared pointer"), bOtherVehicleModifyRan);
    TestTrue(TEXT("Snapshot reads current shared value"), Manager->GetVehicleDataSnapshot(OriginalId, Snapshot));
    TestFalse(TEXT("Snapshot does not copy subscriptions"), Snapshot.OnDataChanged.IsBound());
    Snapshot.RuntimeData.CurrentFuel = 999.f;
    TestEqual(TEXT("Editing transport snapshot does not mutate shared store"), Shared->RuntimeData.CurrentFuel, 19.f);

    FVehicleData ChangedFirst = First;
    ChangedFirst.RuntimeData.CurrentFuel = 41.f;
    FVehicleData InvalidSeats = Third;
    InvalidSeats.Attributes.PassengerCapacity = -1;
    TestFalse(TEXT("Invalid ID rejects complete batch"), Manager->LoadVehicleData({ChangedFirst, Third, FVehicleData()}, Error));
    TestFalse(TEXT("Invalid batch explains failure"), Error.IsEmpty());
    TestFalse(TEXT("Duplicate ID rejects complete batch"), Manager->LoadVehicleData({ChangedFirst, Third, ChangedFirst}, Error));
    TestFalse(TEXT("Negative seats reject complete batch"), Manager->LoadVehicleData({ChangedFirst, InvalidSeats}, Error));
    TestEqual(TEXT("Rejected batches retain original data"), Shared->RuntimeData.CurrentFuel, 19.f);
    TestFalse(TEXT("Rejected batches create no partial record"), Manager->GetVehicleDataShared(Third.VehicleId).IsValid());
    TestEqual(TEXT("Rejected batches do not notify"), Notifications, 1);
    TestFalse(TEXT("Update cannot change vehicle identity"), Manager->UpdateVehicleData(OriginalId, Second));
    TestFalse(TEXT("Update cannot implicitly create a missing record"), Manager->UpdateVehicleData(Third.VehicleId, Third));

    bInspectBatch = true;
    TestTrue(TEXT("Valid batch appends and updates together"), Manager->LoadVehicleData({ChangedFirst, Third}, Error));
    bInspectBatch = false;
    TestEqual(TEXT("Existing record notified once for reload"), Notifications, 2);
    TestTrue(TEXT("Reload preserves pointer identity"), Shared.Get() == Manager->GetVehicleDataShared(OriginalId).Get());
    TestEqual(TEXT("Existing reader observes reloaded fields"), Shared->RuntimeData.CurrentFuel, 41.f);
    TestEqual(TEXT("Append retains other existing records"), Manager->GetVehicleDataIDs().Num(), 3);
    ChangedFirst.RuntimeData.CurrentFuel = 52.f;
    TestTrue(TEXT("Explicit update succeeds"), Manager->UpdateVehicleData(OriginalId, ChangedFirst));
    TestEqual(TEXT("Update preserves reader and subscriptions"), Shared->RuntimeData.CurrentFuel, 52.f);
    TestEqual(TEXT("Update sends one notification"), Notifications, 3);
    TestTrue(TEXT("Empty load is a valid no-op"), Manager->LoadVehicleData({}, Error));
    TestEqual(TEXT("Empty load does not remove records"), Manager->GetVehicleDataIDs().Num(), 3);
    TestEqual(TEXT("Tile filter includes only current tile vehicles"), Manager->GetVehicleDataAtTile(TEXT("VehicleTileA")).Num(), 2);
    Second.RuntimeData.TileId = NAME_None;
    TestTrue(TEXT("Unpositioned vehicle can be restored"), Manager->LoadVehicleData({Second}, Error));
    TestEqual(TEXT("Unpositioned vehicle remains in canonical store"), Manager->GetVehicleDataIDs().Num(), 3);
    TestTrue(TEXT("Unspecified query tile never exposes unpositioned vehicles"), Manager->GetVehicleDataAtTile(NAME_None).IsEmpty());
    TestTrue(TEXT("Moved record no longer matches previous tile"), Manager->GetVehicleDataAtTile(TEXT("VehicleTileB")).IsEmpty());

    Shared->OnDataChanged.Remove(Changed);
    Shared->NotifyDataChanged();
    TestEqual(TEXT("Removing listener stops notifications"), Notifications, 3);
    First = ChangedFirst = Snapshot = FVehicleData();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Canonical struct traces its preview texture"), Image.IsValid());
    TestTrue(TEXT("Vehicle can be removed explicitly"), Manager->RemoveVehicleData(OriginalId, Error));
    TestFalse(TEXT("Removed vehicle is no longer queryable"), Manager->GetVehicleDataShared(OriginalId).IsValid());
    TestFalse(TEXT("Repeated remove is a safe failure"), Manager->RemoveVehicleData(OriginalId, Error));
    Manager->OnVehicleDataReconcile.Remove(Reconciled);
    Manager.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("External shared owner keeps resources alive after manager destruction"), Image.IsValid());
    Shared.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Last shared owner releases its preview texture"), Image.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVehicleTemplateLoadingTest, "SilverChoir.Player.Vehicles.TemplateTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVehicleTemplateLoadingTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UPlayerUnitManagerBase> Manager(NewObject<UPlayerUnitManagerBase>());
    TStrongObjectPtr<UDataTable> Table(MakeVehicleStoreTable());
    TArray<FGuid> Created;
    FText Error = FText::FromString(TEXT("Stale error"));
    const FName Tile(TEXT("FullVehicleTableTile"));
    TestTrue(TEXT("Full table creates and loads every row"), Manager->LoadVehicleTemplates(Table.Get(), Tile, Created, Error));
    TestTrue(TEXT("Successful full-table load clears stale error"), Error.IsEmpty());
    if (!TestEqual(TEXT("Every row creates one vehicle"), Created.Num(), 3)) return false;
    const TArray<FName> ExpectedRows = {TEXT("AlphaTwo"), TEXT("MikeFour"), TEXT("ZuluSix")};
    const TArray<int32> ExpectedSeats = {2, 4, 6};
    TSet<FGuid> AllIds;
    for (int32 Index = 0; Index < Created.Num(); ++Index)
    {
        TestTrue(TEXT("Generated ID is valid"), Created[Index].IsValid());
        AllIds.Add(Created[Index]);
        const auto Data = Manager->GetVehicleDataShared(Created[Index]);
        if (!TestTrue(TEXT("Returned ID resolves to canonical record"), Data.IsValid())) continue;
        TestEqual(TEXT("Rows are loaded in deterministic lexical order"), Data->SourceTemplateRow, ExpectedRows[Index]);
        TestEqual(TEXT("Correct seat capacity copied"), Data->Attributes.PassengerCapacity, ExpectedSeats[Index]);
        TestEqual(TEXT("All created vehicles receive supplied tile"), Data->RuntimeData.TileId, Tile);
        TestEqual(TEXT("Factory initializes full durability"), Data->RuntimeData.CurrentDurability, 230.f);
        TestEqual(TEXT("Factory initializes full fuel"), Data->RuntimeData.CurrentFuel, 75.f);
        TestEqual(TEXT("Strategic movement survives loading"), Data->StrategicMovementData.SpeedTilesPerHour, 2.5f);
    }
    TestEqual(TEXT("Full-table identities are unique"), AllIds.Num(), 3);
    const auto First = Manager->GetVehicleDataShared(Created[0]);
    if (!TestTrue(TEXT("First generated vehicle remains available"), First.IsValid())) return false;
    First->Modify([](FVehicleData& Data) { Data.RuntimeData.CurrentFuel = 9.f; });
    TestTrue(TEXT("Explicit repeated full-table request creates more vehicles"), Manager->LoadVehicleTemplates(Table.Get(), Tile, Created, Error));
    for (FGuid Id : Created) TestFalse(TEXT("Repeated request never reuses a prior ID"), AllIds.Contains(Id));
    TestEqual(TEXT("Repeated request appends rather than replacing"), Manager->GetVehicleDataIDs().Num(), 6);
    TestEqual(TEXT("Repeated request preserves original runtime state"), First->RuntimeData.CurrentFuel, 9.f);

    const int32 CountBeforeFailure = Manager->GetVehicleDataIDs().Num();
    TestFalse(TEXT("Full-table load requires a strategic position"), Manager->LoadVehicleTemplates(Table.Get(), NAME_None, Created, Error));
    TestTrue(TEXT("Missing tile clears stale output IDs"), Created.IsEmpty());
    TestFalse(TEXT("Missing tile explains failure"), Error.IsEmpty());
    TStrongObjectPtr<UDataTable> WrongTable(NewObject<UDataTable>());
    WrongTable->RowStruct = FUnitTemplate::StaticStruct();
    Created.Add(FGuid::NewGuid());
    TestFalse(TEXT("Unit table cannot load as vehicle table"), Manager->LoadVehicleTemplates(WrongTable.Get(), Tile, Created, Error));
    TestTrue(TEXT("Wrong table clears stale output IDs"), Created.IsEmpty());
    TestEqual(TEXT("Failed full-table requests do not mutate store"), Manager->GetVehicleDataIDs().Num(), CountBeforeFailure);

    UPlayerUnitSettings* Settings = GetMutableDefault<UPlayerUnitSettings>();
    {
        TGuardValue<TSoftObjectPtr<UDataTable>> RestoreSetting(Settings->VehicleTemplateTable, TSoftObjectPtr<UDataTable>(Table.Get()));
        TestTrue(TEXT("Null table uses configured template table"), Manager->LoadVehicleTemplates(nullptr, Tile, Created, Error));
        TestEqual(TEXT("Configured table loads every row"), Created.Num(), 3);
    }
    const int32 CountBeforeMissingSettings = Manager->GetVehicleDataIDs().Num();
    {
        TGuardValue<TSoftObjectPtr<UDataTable>> RestoreSetting(Settings->VehicleTemplateTable, TSoftObjectPtr<UDataTable>());
        TestFalse(TEXT("Missing explicit and configured table fails"), Manager->LoadVehicleTemplates(nullptr, Tile, Created, Error));
        TestTrue(TEXT("Missing configuration returns no stale IDs"), Created.IsEmpty());
        TestFalse(TEXT("Missing configuration explains failure"), Error.IsEmpty());
    }
    TestEqual(TEXT("Missing configuration does not change store"), Manager->GetVehicleDataIDs().Num(), CountBeforeMissingSettings);
    TStrongObjectPtr<UDataTable> EmptyTable(NewObject<UDataTable>());
    EmptyTable->RowStruct = FVehicleTemplate::StaticStruct();
    Created.Add(FGuid::NewGuid());
    TestTrue(TEXT("Empty valid table is a successful no-op"), Manager->LoadVehicleTemplates(EmptyTable.Get(), Tile, Created, Error));
    TestTrue(TEXT("Empty table clears stale output IDs"), Created.IsEmpty());
    TestTrue(TEXT("Empty table clears previous error"), Error.IsEmpty());
    TestEqual(TEXT("Empty table creates no records"), Manager->GetVehicleDataIDs().Num(), CountBeforeMissingSettings);
    TestEqual(TEXT("Source template table still has all rows"), Table->GetRowNames().Num(), 3);
    const auto* Source = Table->FindRow<FVehicleTemplate>(TEXT("AlphaTwo"), TEXT("VehicleStoreTest"), false);
    if (TestNotNull(TEXT("Original source row remains available"), Source))
        TestEqual(TEXT("Runtime edits never mutate template data"), Source->Attributes.MaxFuel, 75.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVehicleLibraryLoadingTest, "SilverChoir.Player.Vehicles.Library",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVehicleLibraryLoadingTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
    if (!TestNotNull(TEXT("Game world is available"), World)) return false;
    UPlayerUnitManagerBase* Manager = UPlayerUnitLibrary::GetPlayerUnitManager(World);
    if (!TestNotNull(TEXT("Library resolves the player unit manager"), Manager)) return false;
    const int32 InitialCount = Manager->GetVehicleDataIDs().Num();
    TStrongObjectPtr<UDataTable> Table(MakeVehicleStoreTable());
    TArray<FGuid> Created = {FGuid::NewGuid()};
    FText Error;
    TestFalse(TEXT("Null world context is a safe failure"), UPlayerUnitLibrary::LoadPlayerVehiclesFromTemplateTable(nullptr, Table.Get(), TEXT("TestVehicleLibrary"), Created, Error));
    TestTrue(TEXT("Null context clears stale output IDs"), Created.IsEmpty());
    TestFalse(TEXT("Null context explains failure"), Error.IsEmpty());
    TestTrue(TEXT("Blueprint library loads full table into active subsystem"),
        UPlayerUnitLibrary::LoadPlayerVehiclesFromTemplateTable(World, Table.Get(), TEXT("TestVehicleLibrary"), Created, Error));
    TestEqual(TEXT("Library creates one instance of every template"), Created.Num(), 3);
    for (FGuid Id : Created)
    {
        const auto Data = Manager->GetVehicleDataShared(Id);
        if (TestTrue(TEXT("Library output resolves through canonical manager"), Data.IsValid()))
            TestEqual(TEXT("Library applies caller position"), Data->RuntimeData.TileId, FName(TEXT("TestVehicleLibrary")));
    }
    // These test records never enter a squad. Remove them so other client UI tests see
    // exactly the original vehicle collection and no permanent fixture data.
    for (FGuid Id : Created) TestTrue(TEXT("Library fixture record removed"), Manager->RemoveVehicleData(Id, Error));
    TestEqual(TEXT("Library test leaves the live player store unchanged"), Manager->GetVehicleDataIDs().Num(), InitialCount);
    return true;
}
#endif
