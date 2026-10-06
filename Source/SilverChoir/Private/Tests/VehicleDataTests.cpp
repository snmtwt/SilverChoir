#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Data/Units/UnitStructs.h"
#include "Data/Vehicles/VehicleDataLibrary.h"
#include "Engine/Texture2D.h"
#include "Object/Vehicle/VehiclePawnBase.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
FVehicleTemplate MakeTestVehicleTemplate(UTexture2D* PreviewImage)
{
    FVehicleTemplate Template;
    Template.Profile.VehicleName = FText::FromString(TEXT("Scout carrier"));
    Template.Profile.Description = FText::FromString(TEXT("Vehicle factory test template"));
    Template.Profile.PreviewImage = PreviewImage;
    Template.EntityData.VehiclePawnClass = AVehiclePawnBase::StaticClass();
    Template.Attributes.MaxDurability = 275.f;
    Template.Attributes.MaxFuel = 63.f;
    Template.Attributes.PassengerCapacity = 5;
    Template.Attributes.MaxCargoWeight = 1750.f;
    Template.StrategicMovementData.SpeedTilesPerHour = 4.f;
    Template.StrategicMovementData.StaminaCostPerTile = 2.f;
    Template.StrategicMovementData.FuelCostPerTile = 8.f;
    return Template;
}

void TestVehicleConfiguration(FAutomationTestBase& Test, const FVehicleData& Data, const FVehicleTemplate& Template)
{
    Test.TestEqual(TEXT("Vehicle name copied"), Data.Profile.VehicleName.ToString(), Template.Profile.VehicleName.ToString());
    Test.TestEqual(TEXT("Vehicle description copied"), Data.Profile.Description.ToString(), Template.Profile.Description.ToString());
    Test.TestTrue(TEXT("Preview image reference copied"), Data.Profile.PreviewImage == Template.Profile.PreviewImage);
    Test.TestTrue(TEXT("Vehicle entity class copied"), Data.EntityData.VehiclePawnClass == Template.EntityData.VehiclePawnClass);
    Test.TestEqual(TEXT("Maximum durability copied"), Data.Attributes.MaxDurability, Template.Attributes.MaxDurability);
    Test.TestEqual(TEXT("Maximum fuel copied"), Data.Attributes.MaxFuel, Template.Attributes.MaxFuel);
    Test.TestEqual(TEXT("Passenger capacity copied"), Data.Attributes.PassengerCapacity, Template.Attributes.PassengerCapacity);
    Test.TestEqual(TEXT("Cargo limit copied"), Data.Attributes.MaxCargoWeight, Template.Attributes.MaxCargoWeight);
    Test.TestEqual(TEXT("Strategic tiles per hour copied"), Data.StrategicMovementData.SpeedTilesPerHour, Template.StrategicMovementData.SpeedTilesPerHour);
    Test.TestEqual(TEXT("Strategic stamina cost per tile copied"), Data.StrategicMovementData.StaminaCostPerTile, Template.StrategicMovementData.StaminaCostPerTile);
    Test.TestEqual(TEXT("Strategic fuel cost per tile copied"), Data.StrategicMovementData.FuelCostPerTile, Template.StrategicMovementData.FuelCostPerTile);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleTemplateFactoryTest, "SilverChoir.Vehicle.Data.TemplateFactory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVehicleTemplateFactoryTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UTexture2D> PreviewImage(NewObject<UTexture2D>());
    FVehicleTemplate Template = MakeTestVehicleTemplate(PreviewImage.Get());
    const FName TileId(TEXT("VehicleFactoryTile"));
    const FVehicleData First = UVehicleDataLibrary::CreateVehicleDataFromTemplate(Template, TileId);
    const FVehicleData Second = UVehicleDataLibrary::CreateVehicleDataFromTemplate(Template);
    TestTrue(TEXT("Each factory call allocates an independent valid identity"), First.VehicleId.IsValid() && Second.VehicleId.IsValid() && First.VehicleId != Second.VehicleId);
    TestFalse(TEXT("Default data construction does not allocate identity"), FVehicleData().VehicleId.IsValid());
    TestVehicleConfiguration(*this, First, Template);
    TestEqual(TEXT("Durability starts at the template maximum"), First.RuntimeData.CurrentDurability, Template.Attributes.MaxDurability);
    TestEqual(TEXT("Fuel starts at the template maximum"), First.RuntimeData.CurrentFuel, Template.Attributes.MaxFuel);
    TestEqual(TEXT("Factory applies strategic tile"), First.RuntimeData.TileId, TileId);
    TestTrue(TEXT("Omitted tile remains unassigned"), Second.RuntimeData.TileId.IsNone());
    TestTrue(TEXT("Direct template creation has no source row"), First.SourceTemplateRow.IsNone());
    const FVehicleData Unconfigured = UVehicleDataLibrary::CreateVehicleDataFromTemplate(FVehicleTemplate());
    TestTrue(TEXT("Unconfigured optional resources do not prevent creation"), Unconfigured.VehicleId.IsValid());
    TestNull(TEXT("Preview image may be absent"), Unconfigured.Profile.PreviewImage.Get());
    TestNull(TEXT("Vehicle class may be absent"), Unconfigured.EntityData.VehiclePawnClass.Get());

    TArray<FVehicleData> Batch;
    FText Error = FText::FromString(TEXT("Stale error"));
    TestTrue(TEXT("Batch creation succeeds"), UVehicleDataLibrary::CreateVehicleDataBatch(Template, 3, Batch, Error, TileId));
    TestEqual(TEXT("Batch returns requested count"), Batch.Num(), 3);
    TestTrue(TEXT("Successful batch clears stale error"), Error.IsEmpty());
    TSet<FGuid> Ids;
    for (const FVehicleData& Vehicle : Batch)
    {
        TestTrue(TEXT("Every batch identity is valid"), Vehicle.VehicleId.IsValid());
        Ids.Add(Vehicle.VehicleId);
        TestVehicleConfiguration(*this, Vehicle, Template);
        TestEqual(TEXT("Batch applies tile to every vehicle"), Vehicle.RuntimeData.TileId, TileId);
        TestEqual(TEXT("Batch initializes full durability"), Vehicle.RuntimeData.CurrentDurability, Template.Attributes.MaxDurability);
        TestEqual(TEXT("Batch initializes full fuel"), Vehicle.RuntimeData.CurrentFuel, Template.Attributes.MaxFuel);
        TestTrue(TEXT("Template batch has no source row"), Vehicle.SourceTemplateRow.IsNone());
    }
    TestEqual(TEXT("Batch identities are unique"), Ids.Num(), 3);
    TestFalse(TEXT("Negative count is rejected"), UVehicleDataLibrary::CreateVehicleDataBatch(Template, -1, Batch, Error));
    TestTrue(TEXT("Rejected count clears previous output"), Batch.IsEmpty());
    TestFalse(TEXT("Rejected count explains failure"), Error.IsEmpty());
    Batch.Add(First);
    TestTrue(TEXT("Zero count succeeds"), UVehicleDataLibrary::CreateVehicleDataBatch(Template, 0, Batch, Error));
    TestTrue(TEXT("Zero count clears previous output"), Batch.IsEmpty());
    TestTrue(TEXT("Zero count clears previous error"), Error.IsEmpty());

    Template.Attributes.MaxDurability = -5.f;
    Template.Attributes.MaxFuel = std::numeric_limits<float>::quiet_NaN();
    Template.Attributes.PassengerCapacity = -2;
    Template.Attributes.MaxCargoWeight = std::numeric_limits<float>::infinity();
    Template.StrategicMovementData.SpeedTilesPerHour = -10.f;
    Template.StrategicMovementData.StaminaCostPerTile = std::numeric_limits<float>::quiet_NaN();
    Template.StrategicMovementData.FuelCostPerTile = std::numeric_limits<float>::infinity();
    const FVehicleData Sanitized = UVehicleDataLibrary::CreateVehicleDataFromTemplate(Template, TileId);
    TestEqual(TEXT("Negative durability is normalized"), Sanitized.Attributes.MaxDurability, 0.f);
    TestEqual(TEXT("NaN fuel is normalized"), Sanitized.Attributes.MaxFuel, 0.f);
    TestEqual(TEXT("Negative passenger capacity is normalized"), Sanitized.Attributes.PassengerCapacity, 0);
    TestEqual(TEXT("Infinite cargo limit is normalized"), Sanitized.Attributes.MaxCargoWeight, 0.f);
    TestEqual(TEXT("Current durability uses sanitized maximum"), Sanitized.RuntimeData.CurrentDurability, 0.f);
    TestEqual(TEXT("Current fuel uses sanitized maximum"), Sanitized.RuntimeData.CurrentFuel, 0.f);
    TestEqual(TEXT("Negative march speed is normalized"), Sanitized.StrategicMovementData.SpeedTilesPerHour, 0.f);
    TestEqual(TEXT("NaN stamina cost per tile is normalized"), Sanitized.StrategicMovementData.StaminaCostPerTile, 0.f);
    TestEqual(TEXT("Infinite fuel cost per tile is normalized"), Sanitized.StrategicMovementData.FuelCostPerTile, 0.f);
    TestEqual(TEXT("Source durability remains unchanged"), Template.Attributes.MaxDurability, -5.f);
    TestTrue(TEXT("Source fuel remains NaN"), FMath::IsNaN(Template.Attributes.MaxFuel));
    TestEqual(TEXT("Source passenger capacity remains unchanged"), Template.Attributes.PassengerCapacity, -2);
    TestTrue(TEXT("Source cargo limit remains infinite"), Template.Attributes.MaxCargoWeight == std::numeric_limits<float>::infinity());
    TestEqual(TEXT("Source march speed remains unchanged"), Template.StrategicMovementData.SpeedTilesPerHour, -10.f);
    TestTrue(TEXT("Source stamina cost remains NaN"), FMath::IsNaN(Template.StrategicMovementData.StaminaCostPerTile));
    TestTrue(TEXT("Source fuel cost remains infinite"), Template.StrategicMovementData.FuelCostPerTile == std::numeric_limits<float>::infinity());
    TestTrue(TEXT("Sanitization preserves preview resource"), Sanitized.Profile.PreviewImage == Template.Profile.PreviewImage);
    TestTrue(TEXT("Sanitization preserves entity class"), Sanitized.EntityData.VehiclePawnClass == Template.EntityData.VehiclePawnClass);
    Template.StrategicMovementData = Sanitized.StrategicMovementData;
    const FVehicleData Stationary = UVehicleDataLibrary::CreateVehicleDataFromTemplate(Template, TileId);
    TestEqual(TEXT("A vehicle that cannot march retains zero speed"), Stationary.StrategicMovementData.SpeedTilesPerHour, 0.f);
    TestEqual(TEXT("Zero stamina cost remains valid"), Stationary.StrategicMovementData.StaminaCostPerTile, 0.f);
    TestEqual(TEXT("Zero fuel cost remains valid"), Stationary.StrategicMovementData.FuelCostPerTile, 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleTableFactoryTest, "SilverChoir.Vehicle.Data.TableFactory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVehicleTableFactoryTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UTexture2D> PreviewImage(NewObject<UTexture2D>());
    const FVehicleTemplate Scout = MakeTestVehicleTemplate(PreviewImage.Get());
    FVehicleTemplate Truck = Scout;
    Truck.Profile.VehicleName = FText::FromString(TEXT("Supply truck"));
    Truck.Attributes.MaxCargoWeight = 4500.f;
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct = FVehicleTemplate::StaticStruct();
    Table->AddRow(TEXT("Scout"), Scout);
    Table->AddRow(TEXT("Truck"), Truck);
    const FName TileId(TEXT("VehicleTableTile"));
    TArray<FVehicleData> Vehicles;
    FText Error = FText::FromString(TEXT("Stale table error"));
    TestTrue(TEXT("Explicit repeated rows create vehicles"), UVehicleDataLibrary::CreateVehicleDataFromTable(Table.Get(), { TEXT("Truck"), TEXT("Scout"), TEXT("Scout") }, Vehicles, Error, TileId));
    TestTrue(TEXT("Successful table creation clears stale error"), Error.IsEmpty());
    if (TestEqual(TEXT("One vehicle returned for each requested row"), Vehicles.Num(), 3))
    {
        TestVehicleConfiguration(*this, Vehicles[0], Truck);
        TestVehicleConfiguration(*this, Vehicles[1], Scout);
        TestVehicleConfiguration(*this, Vehicles[2], Scout);
        TestEqual(TEXT("First source row preserves request order"), Vehicles[0].SourceTemplateRow, FName(TEXT("Truck")));
        TestEqual(TEXT("Second source row is recorded"), Vehicles[1].SourceTemplateRow, FName(TEXT("Scout")));
        TestEqual(TEXT("Repeated source row is recorded"), Vehicles[2].SourceTemplateRow, FName(TEXT("Scout")));
        TSet<FGuid> Ids;
        for (const FVehicleData& Vehicle : Vehicles)
        {
            TestTrue(TEXT("Table creates valid identity"), Vehicle.VehicleId.IsValid());
            Ids.Add(Vehicle.VehicleId);
            TestEqual(TEXT("Table applies tile to all vehicles"), Vehicle.RuntimeData.TileId, TileId);
        }
        TestEqual(TEXT("Repeated rows allocate different identities"), Ids.Num(), 3);
        Vehicles[1].RuntimeData.CurrentFuel = 1.f;
        Vehicles[1].StrategicMovementData.SpeedTilesPerHour = 1.f;
        TestEqual(TEXT("Repeated rows have independent runtime state"), Vehicles[2].RuntimeData.CurrentFuel, Scout.Attributes.MaxFuel);
        TestEqual(TEXT("Repeated rows have independent movement values"), Vehicles[2].StrategicMovementData.SpeedTilesPerHour, Scout.StrategicMovementData.SpeedTilesPerHour);
    }
    const FVehicleTemplate* StoredScout = Table->FindRow<FVehicleTemplate>(TEXT("Scout"), TEXT("VehicleTableTest"), false);
    if (TestNotNull(TEXT("Factory preserves source table row"), StoredScout))
    {
        TestEqual(TEXT("Factory and instance edits leave source fuel unchanged"), StoredScout->Attributes.MaxFuel, Scout.Attributes.MaxFuel);
        TestEqual(TEXT("Factory and instance edits leave source movement unchanged"), StoredScout->StrategicMovementData.SpeedTilesPerHour, Scout.StrategicMovementData.SpeedTilesPerHour);
    }

    TestFalse(TEXT("Missing later row rejects entire request"), UVehicleDataLibrary::CreateVehicleDataFromTable(Table.Get(), { TEXT("Scout"), TEXT("Missing") }, Vehicles, Error));
    TestTrue(TEXT("Missing row returns no partial vehicles"), Vehicles.IsEmpty());
    TestFalse(TEXT("Missing row explains failure"), Error.IsEmpty());
    Vehicles.Add(UVehicleDataLibrary::CreateVehicleDataFromTemplate(Scout));
    TestFalse(TEXT("Empty row name is rejected"), UVehicleDataLibrary::CreateVehicleDataFromTable(Table.Get(), { TEXT("Scout"), NAME_None }, Vehicles, Error));
    TestTrue(TEXT("Empty row name clears existing output"), Vehicles.IsEmpty());
    Vehicles.Add(UVehicleDataLibrary::CreateVehicleDataFromTemplate(Scout));
    TestFalse(TEXT("Null table is rejected"), UVehicleDataLibrary::CreateVehicleDataFromTable(nullptr, { TEXT("Scout") }, Vehicles, Error));
    TestTrue(TEXT("Null table clears existing output"), Vehicles.IsEmpty());
    TestFalse(TEXT("Null table explains failure"), Error.IsEmpty());
    TStrongObjectPtr<UDataTable> WrongTable(NewObject<UDataTable>());
    WrongTable->RowStruct = FUnitTemplate::StaticStruct();
    Vehicles.Add(UVehicleDataLibrary::CreateVehicleDataFromTemplate(Scout));
    TestFalse(TEXT("Unit template table cannot create vehicles"), UVehicleDataLibrary::CreateVehicleDataFromTable(WrongTable.Get(), { TEXT("Scout") }, Vehicles, Error));
    TestTrue(TEXT("Wrong row struct clears existing output"), Vehicles.IsEmpty());
    TestFalse(TEXT("Wrong row struct explains failure"), Error.IsEmpty());
    Vehicles.Add(UVehicleDataLibrary::CreateVehicleDataFromTemplate(Scout));
    TestTrue(TEXT("Empty explicit row list succeeds"), UVehicleDataLibrary::CreateVehicleDataFromTable(Table.Get(), {}, Vehicles, Error));
    TestTrue(TEXT("Empty list creates no vehicles instead of reading the entire table"), Vehicles.IsEmpty());
    TestTrue(TEXT("Empty successful list clears previous error"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleDataSaveGameTest, "SilverChoir.Vehicle.Data.ReflectionAndSaveGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDataSaveGameTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Vehicle templates are DataTable rows"), FVehicleTemplate::StaticStruct()->IsChildOf(FTableRowBase::StaticStruct()));
    TestNull(TEXT("Vehicle template has no instance identity"), FindFProperty<FProperty>(FVehicleTemplate::StaticStruct(), TEXT("VehicleId")));
    TestNull(TEXT("Vehicle template has no runtime state"), FindFProperty<FProperty>(FVehicleTemplate::StaticStruct(), TEXT("RuntimeData")));
    for (UScriptStruct* Struct : { FVehicleData::StaticStruct(), FVehicleTemplate::StaticStruct(), FVehicleProfile::StaticStruct(),
        FVehicleAttributes::StaticStruct(), FVehicleEntityData::StaticStruct(), FVehicleRuntimeData::StaticStruct() })
    {
#if WITH_METADATA
        TestTrue(FString::Printf(TEXT("%s is a Blueprint struct"), *Struct->GetName()), Struct->HasMetaData(TEXT("BlueprintType")));
#endif
        for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            TestTrue(FString::Printf(TEXT("%s.%s participates in SaveGame"), *Struct->GetName(), *It->GetName()), It->HasAnyPropertyFlags(CPF_SaveGame));
        }
    }
    const FStructProperty* MovementProperty = FindFProperty<FStructProperty>(FVehicleData::StaticStruct(), TEXT("StrategicMovementData"));
    if (TestNotNull(TEXT("Vehicle exposes strategic movement"), MovementProperty))
    {
        TestTrue(TEXT("Vehicle and unit use the common movement struct"), MovementProperty->Struct == FStrategicMovementData::StaticStruct());
    }
    const UClass* VehicleClass = AVehiclePawnBase::StaticClass();
    TestTrue(TEXT("Vehicle base is an abstract Pawn"), VehicleClass->HasAnyClassFlags(CLASS_Abstract) && VehicleClass->IsChildOf(APawn::StaticClass()));
    const AVehiclePawnBase* Defaults = GetDefault<AVehiclePawnBase>();
    TestFalse(TEXT("Vehicle Pawn starts without a data identity"), Defaults->GetVehicleId().IsValid());
    TestNotNull(TEXT("Vehicle Pawn has a default root component"), Defaults->SceneRoot.Get());
    TestTrue(TEXT("Vehicle scene component is its root"), Defaults->GetRootComponent() == Defaults->SceneRoot);

    TStrongObjectPtr<UTexture2D> PreviewImage(NewObject<UTexture2D>());
    const FVehicleTemplate Template = MakeTestVehicleTemplate(PreviewImage.Get());
    FVehicleData Original = UVehicleDataLibrary::CreateVehicleDataFromTemplate(Template, TEXT("VehicleSaveTile"));
    Original.SourceTemplateRow = TEXT("SavedScout");
    Original.RuntimeData.CurrentDurability = 57.f;
    Original.RuntimeData.CurrentFuel = 12.f;
    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Writer, false);
        Archive.ArIsSaveGame = true;
        FVehicleData::StaticStruct()->SerializeItem(Archive, &Original, nullptr);
        TestFalse(TEXT("Vehicle SaveGame serialization succeeds"), Archive.IsError());
    }
    FVehicleData Restored;
    {
        FMemoryReader Reader(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Reader, false);
        Archive.ArIsSaveGame = true;
        FVehicleData::StaticStruct()->SerializeItem(Archive, &Restored, nullptr);
        TestFalse(TEXT("Vehicle SaveGame deserialization succeeds"), Archive.IsError());
    }
    TestEqual(TEXT("Vehicle save preserves identity"), Restored.VehicleId, Original.VehicleId);
    TestEqual(TEXT("Vehicle save preserves source template row"), Restored.SourceTemplateRow, Original.SourceTemplateRow);
    TestVehicleConfiguration(*this, Restored, Template);
    TestEqual(TEXT("Vehicle save preserves strategic tile"), Restored.RuntimeData.TileId, Original.RuntimeData.TileId);
    TestEqual(TEXT("Vehicle save preserves spent durability"), Restored.RuntimeData.CurrentDurability, Original.RuntimeData.CurrentDurability);
    TestEqual(TEXT("Vehicle save preserves spent fuel"), Restored.RuntimeData.CurrentFuel, Original.RuntimeData.CurrentFuel);
    return true;
}
#endif
