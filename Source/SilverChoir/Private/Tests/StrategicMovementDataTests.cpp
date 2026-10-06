#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Common/StrategicMovementStructs.h"
#include "Data/Units/UnitStructs.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/CoreRedirects.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
FStrategicMovementData MakeTestMovement(float Offset = 0.f)
{
    FStrategicMovementData Data;
    Data.SpeedTilesPerHour = 2.5f + Offset;
    Data.StaminaCostPerTile = 7.f + Offset;
    Data.FuelCostPerTile = 3.f + Offset;
    return Data;
}

void TestMovementEquals(FAutomationTestBase& Test, const TCHAR* Label,
    const FStrategicMovementData& Actual, const FStrategicMovementData& Expected)
{
    Test.TestEqual(FString::Printf(TEXT("%s: tiles per hour"), Label), Actual.SpeedTilesPerHour, Expected.SpeedTilesPerHour);
    Test.TestEqual(FString::Printf(TEXT("%s: stamina cost per tile"), Label), Actual.StaminaCostPerTile, Expected.StaminaCostPerTile);
    Test.TestEqual(FString::Printf(TEXT("%s: fuel cost per tile"), Label), Actual.FuelCostPerTile, Expected.FuelCostPerTile);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrategicMovementReflectionTest, "SilverChoir.Data.StrategicMovement.ReflectionAndSaveGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrategicMovementReflectionTest::RunTest(const FString& Parameters)
{
    UScriptStruct* MovementStruct = FStrategicMovementData::StaticStruct();
#if WITH_METADATA
    TestTrue(TEXT("Strategic movement is exposed as a Blueprint struct"), MovementStruct->HasMetaData(TEXT("BlueprintType")));
#endif
    for (const FName FieldName : { FName(TEXT("SpeedTilesPerHour")), FName(TEXT("StaminaCostPerTile")), FName(TEXT("FuelCostPerTile")) })
    {
        const FFloatProperty* Property = FindFProperty<FFloatProperty>(MovementStruct, FieldName);
        if (!TestNotNull(FString::Printf(TEXT("%s is a reflected float"), *FieldName.ToString()), Property)) { continue; }
        TestTrue(FString::Printf(TEXT("%s is saved"), *FieldName.ToString()), Property->HasAnyPropertyFlags(CPF_SaveGame));
        TestTrue(FString::Printf(TEXT("%s is Blueprint accessible"), *FieldName.ToString()), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
        TestFalse(FString::Printf(TEXT("%s can be written in Blueprint"), *FieldName.ToString()), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
    }
    const FStructProperty* InstanceProperty = FindFProperty<FStructProperty>(FUnitData::StaticStruct(), TEXT("StrategicMovementData"));
    const FStructProperty* TemplateProperty = FindFProperty<FStructProperty>(FUnitTemplate::StaticStruct(), TEXT("StrategicMovementData"));
    if (TestNotNull(TEXT("Unit instance reflects strategic movement"), InstanceProperty))
    {
        TestTrue(TEXT("Unit instance uses the common movement struct"), InstanceProperty->Struct == MovementStruct);
        TestTrue(TEXT("Unit instance saves nested movement data"), InstanceProperty->HasAnyPropertyFlags(CPF_SaveGame));
        TestTrue(TEXT("Unit instance exposes movement to Blueprint"), InstanceProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
    }
    if (TestNotNull(TEXT("Unit template reflects strategic movement"), TemplateProperty))
    {
        TestTrue(TEXT("Unit template uses the same movement struct"), TemplateProperty->Struct == MovementStruct);
        TestTrue(TEXT("Unit template exposes movement to Blueprint"), TemplateProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
    }

    const FCoreRedirectObjectName OldStructName(FString(TEXT("/Script/SilverChoir.TacticalMovementData")));
    const FCoreRedirectObjectName NewStructName(FString(TEXT("/Script/SilverChoir.StrategicMovementData")));
    TestEqual(TEXT("Legacy movement struct resolves to the strategic struct"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct, OldStructName).ToString(), NewStructName.ToString());
    for (const TCHAR* OwnerName : { TEXT("UnitData"), TEXT("UnitTemplate"), TEXT("VehicleData"), TEXT("VehicleTemplate") })
    {
        const FCoreRedirectObjectName OldPropertyName(FString::Printf(TEXT("/Script/SilverChoir.%s.TacticalMovementData"), OwnerName));
        const FCoreRedirectObjectName NewPropertyName(FString::Printf(TEXT("/Script/SilverChoir.%s.StrategicMovementData"), OwnerName));
        TestEqual(FString::Printf(TEXT("%s redirects its legacy movement property"), OwnerName),
            FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Property, OldPropertyName).ToString(), NewPropertyName.ToString());
    }
    // Legacy local-movement values use different units and must not populate tile-travel parameters.
    for (const FName LegacyField : { FName(TEXT("MaxSpeed")), FName(TEXT("Acceleration")), FName(TEXT("Deceleration")),
        FName(TEXT("TurnRate")), FName(TEXT("AcceptanceRadius")) })
    {
        TestNull(FString::Printf(TEXT("Strategic movement has no legacy %s field"), *LegacyField.ToString()),
            FindFProperty<FProperty>(MovementStruct, LegacyField));
        for (const TCHAR* StructName : { TEXT("TacticalMovementData"), TEXT("StrategicMovementData") })
        {
            const FCoreRedirectObjectName LegacyPropertyName(FString::Printf(TEXT("/Script/SilverChoir.%s.%s"), StructName, *LegacyField.ToString()));
            const FCoreRedirectObjectName RedirectedPropertyName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Property, LegacyPropertyName);
            TestEqual(FString::Printf(TEXT("%s.%s is not redirected into a strategic numeric field"), StructName, *LegacyField.ToString()),
                RedirectedPropertyName.ObjectName, LegacyField);
        }
    }

    FUnitData Original;
    Original.UnitId = FGuid::NewGuid();
    Original.StrategicMovementData = MakeTestMovement();
    Original.RuntimeData.TileId = TEXT("StrategicMovement_SaveGameTile");
    Original.RuntimeData.bCannotWalkStrategically = true;
    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Writer, false);
        Archive.ArIsSaveGame = true;
        FUnitData::StaticStruct()->SerializeItem(Archive, &Original, nullptr);
        TestFalse(TEXT("Saving unit movement succeeds"), Archive.IsError());
    }
    FUnitData Restored;
    {
        FMemoryReader Reader(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Reader, false);
        Archive.ArIsSaveGame = true;
        FUnitData::StaticStruct()->SerializeItem(Archive, &Restored, nullptr);
        TestFalse(TEXT("Loading unit movement succeeds"), Archive.IsError());
    }
    TestEqual(TEXT("SaveGame preserves unit identity"), Restored.UnitId, Original.UnitId);
    TestMovementEquals(*this, TEXT("SaveGame round trip"), Restored.StrategicMovementData, Original.StrategicMovementData);
    TestEqual(TEXT("Strategic position remains independent"), Restored.RuntimeData.TileId, Original.RuntimeData.TileId);
    TestTrue(TEXT("Strategic walking restriction is saved independently"), Restored.RuntimeData.bCannotWalkStrategically);
    TestFalse(TEXT("SaveGame does not create event subscriptions"), Restored.OnDataChanged.IsBound());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrategicMovementUnitTemplateTest, "SilverChoir.Data.StrategicMovement.UnitTemplate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrategicMovementUnitTemplateTest::RunTest(const FString& Parameters)
{
    const FStrategicMovementData Defaults;
    TestEqual(TEXT("Default march speed is one tile per hour"), Defaults.SpeedTilesPerHour, 1.f);
    TestEqual(TEXT("Default march has no stamina cost"), Defaults.StaminaCostPerTile, 0.f);
    TestEqual(TEXT("Default march has no fuel cost"), Defaults.FuelCostPerTile, 0.f);
    FUnitTemplate Template;
    Template.StrategicMovementData = MakeTestMovement();
    const FUnitData First = UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    const FUnitData Second = UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    TestMovementEquals(*this, TEXT("Template creates non-default movement values"), First.StrategicMovementData, Template.StrategicMovementData);
    TestMovementEquals(*this, TEXT("Repeated template generation keeps movement values"), Second.StrategicMovementData, Template.StrategicMovementData);
    TestTrue(TEXT("Movement configuration does not share generated identities"), First.UnitId.IsValid() && Second.UnitId.IsValid() && First.UnitId != Second.UnitId);
    TestTrue(TEXT("Template generation does not assign a strategic tile"), First.RuntimeData.TileId.IsNone());
    TestFalse(TEXT("Positive march speed does not add a runtime walking restriction"), First.RuntimeData.bCannotWalkStrategically);

    for (const float InvalidValue : { -100.f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
    {
        Template.StrategicMovementData.SpeedTilesPerHour = InvalidValue;
        Template.StrategicMovementData.StaminaCostPerTile = InvalidValue;
        Template.StrategicMovementData.FuelCostPerTile = InvalidValue;
        const FUnitData Sanitized = UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
        TestEqual(TEXT("Invalid march speed becomes zero"), Sanitized.StrategicMovementData.SpeedTilesPerHour, 0.f);
        TestEqual(TEXT("Invalid stamina cost becomes zero"), Sanitized.StrategicMovementData.StaminaCostPerTile, 0.f);
        TestEqual(TEXT("Invalid fuel cost becomes zero"), Sanitized.StrategicMovementData.FuelCostPerTile, 0.f);
        const auto SourceUnchanged = [InvalidValue](float Value)
        {
            return FMath::IsNaN(InvalidValue) ? FMath::IsNaN(Value) : Value == InvalidValue;
        };
        TestTrue(TEXT("Sanitization leaves source march speed unchanged"), SourceUnchanged(Template.StrategicMovementData.SpeedTilesPerHour));
        TestTrue(TEXT("Sanitization leaves source stamina cost unchanged"), SourceUnchanged(Template.StrategicMovementData.StaminaCostPerTile));
        TestTrue(TEXT("Sanitization leaves source fuel cost unchanged"), SourceUnchanged(Template.StrategicMovementData.FuelCostPerTile));
    }

    Template.StrategicMovementData.SpeedTilesPerHour = 0.f;
    Template.StrategicMovementData.StaminaCostPerTile = 0.f;
    Template.StrategicMovementData.FuelCostPerTile = 0.f;
    const FUnitData Stationary = UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    TestMovementEquals(*this, TEXT("Zero march speed and costs are preserved"), Stationary.StrategicMovementData, Template.StrategicMovementData);
    TestEqual(TEXT("A template that cannot march is not assigned the default speed"), Stationary.StrategicMovementData.SpeedTilesPerHour, 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrategicMovementSharedUnitTest, "SilverChoir.Data.StrategicMovement.SharedUnitUpdates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrategicMovementSharedUnitTest::RunTest(const FString& Parameters)
{
    FUnitTemplate Template;
    Template.StrategicMovementData = MakeTestMovement();
    FUnitData Initial = UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    Initial.RuntimeData.TileId = TEXT("StrategicMovement_SharedTile");
    Initial.RuntimeData.bCannotWalkStrategically = true;
    TStrongObjectPtr<UPlayerUnitManagerBase> Manager(NewObject<UPlayerUnitManagerBase>());
    FText Error;
    if (!TestTrue(TEXT("Load unit with movement parameters"), Manager->LoadUnitData({ Initial }, Error))) { return false; }
    TSharedPtr<FUnitData> Shared = Manager->GetUnitDataShared(Initial.UnitId);
    if (!TestTrue(TEXT("Shared unit exists"), Shared.IsValid())) { return false; }
    TestMovementEquals(*this, TEXT("Initial shared allocation copies movement"), Shared->StrategicMovementData, Template.StrategicMovementData);

    TArray<FStrategicMovementData> NotifiedValues;
    const FDelegateHandle Handle = Shared->OnDataChanged.AddLambda([&](FGuid UnitId)
    {
        TestEqual(TEXT("Movement notification retains unit identity"), UnitId, Initial.UnitId);
        NotifiedValues.Add(Shared->StrategicMovementData);
    });
    FUnitData Copy(*Shared);
    TestMovementEquals(*this, TEXT("Copy construction preserves movement"), Copy.StrategicMovementData, Initial.StrategicMovementData);
    TestFalse(TEXT("Copy construction does not copy subscriptions"), Copy.OnDataChanged.IsBound());
    FUnitData Assigned;
    int32 AssignmentNotifications = 0;
    Assigned.OnDataChanged.AddLambda([&](FGuid) { ++AssignmentNotifications; });
    Assigned = *Shared;
    TestMovementEquals(*this, TEXT("Assignment preserves movement"), Assigned.StrategicMovementData, Initial.StrategicMovementData);
    TestEqual(TEXT("Assignment itself does not notify"), AssignmentNotifications, 0);
    Assigned.NotifyDataChanged();
    TestEqual(TEXT("Assignment preserves destination subscription"), AssignmentNotifications, 1);
    TestTrue(TEXT("Assignment does not copy source subscriptions"), NotifiedValues.IsEmpty());

    const FStrategicMovementData ModifiedMovement = MakeTestMovement(1.f);
    Shared->Modify([&](FUnitData& Unit) { Unit.StrategicMovementData = ModifiedMovement; });
    TestEqual(TEXT("Modify emits one movement notification"), NotifiedValues.Num(), 1);
    TestMovementEquals(*this, TEXT("Modify reaches the canonical record"), Manager->GetUnitDataShared(Initial.UnitId)->StrategicMovementData, ModifiedMovement);
    TestMovementEquals(*this, TEXT("Earlier snapshot remains a value copy"), Copy.StrategicMovementData, Initial.StrategicMovementData);

    FUnitData Update = *Shared;
    Update.StrategicMovementData = MakeTestMovement(2.f);
    TestTrue(TEXT("Update movement succeeds"), Manager->UpdateUnitData(Initial.UnitId, Update));
    TestTrue(TEXT("Update retains canonical allocation"), Shared.Get() == Manager->GetUnitDataShared(Initial.UnitId).Get());
    TestMovementEquals(*this, TEXT("Existing reader sees update"), Shared->StrategicMovementData, Update.StrategicMovementData);
    TestEqual(TEXT("Update emits one movement notification"), NotifiedValues.Num(), 2);

    FUnitData Reload = Update;
    Reload.StrategicMovementData = MakeTestMovement(3.f);
    TestTrue(TEXT("Reload movement succeeds"), Manager->LoadUnitData({ Reload }, Error));
    TestTrue(TEXT("Reload retains canonical allocation"), Shared.Get() == Manager->GetUnitDataShared(Initial.UnitId).Get());
    TestMovementEquals(*this, TEXT("Existing reader sees reload"), Shared->StrategicMovementData, Reload.StrategicMovementData);
    TestEqual(TEXT("Reload emits one movement notification"), NotifiedValues.Num(), 3);
    if (NotifiedValues.Num() == 3)
    {
        TestMovementEquals(*this, TEXT("Modify notification sees complete new movement"), NotifiedValues[0], ModifiedMovement);
        TestMovementEquals(*this, TEXT("Update notification sees complete new movement"), NotifiedValues[1], Update.StrategicMovementData);
        TestMovementEquals(*this, TEXT("Reload notification sees complete new movement"), NotifiedValues[2], Reload.StrategicMovementData);
    }
    TStrongObjectPtr<UUnitDataReference> Reference(NewObject<UUnitDataReference>());
    Reference->Initialize(Shared);
    TestTrue(TEXT("Blueprint reference retains canonical allocation"), Reference->GetSharedData().Get() == Shared.Get());
    TestMovementEquals(*this, TEXT("Blueprint snapshot contains latest movement"), Reference->GetSnapshot().StrategicMovementData, Reload.StrategicMovementData);
    TestEqual(TEXT("Movement edits preserve strategic tile"), Shared->RuntimeData.TileId, Initial.RuntimeData.TileId);
    TestTrue(TEXT("Movement edits preserve strategic walking restriction"), Shared->RuntimeData.bCannotWalkStrategically);
    Shared->OnDataChanged.Remove(Handle);
    Shared->NotifyDataChanged();
    TestEqual(TEXT("Explicit unbind stops movement notifications"), NotifiedValues.Num(), 3);
    Reference->Initialize(nullptr);
    return true;
}
#endif
