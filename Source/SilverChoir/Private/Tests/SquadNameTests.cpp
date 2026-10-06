#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Vehicles/VehicleStructs.h"
#include "Engine/DataTable.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSettings.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadNameTemplateTest, "SilverChoir.Player.Squads.Names.TemplateOrderAndReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadNameTemplateTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UPlayerUnitManagerBase> Units(NewObject<UPlayerUnitManagerBase>());
    TStrongObjectPtr<UPlayerSquadManagerBase> Squads(NewObject<UPlayerSquadManagerBase>());
    Squads->Initialize(Units.Get());
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct = FSquadNameTemplate::StaticStruct();
    auto AddName = [&](FName Row, int32 Order, const TCHAR* Name)
    {
        FSquadNameTemplate Template;
        Template.SortOrder = Order;
        Template.SquadName = FText::FromString(Name);
        Table->AddRow(Row, Template);
    };
    AddName(TEXT("Whitespace"), -5, TEXT(" \t "));
    AddName(TEXT("Zulu"), 10, TEXT("  银翼  "));
    AddName(TEXT("Alpha"), 10, TEXT(" 守望 "));
    AddName(TEXT("Duplicate"), 20, TEXT("银翼"));
    AddName(TEXT("Final"), 30, TEXT("暮鸦"));
    UPlayerSquadSettings* Settings = GetMutableDefault<UPlayerSquadSettings>();
    TGuardValue<TSoftObjectPtr<UDataTable>> RestoreSetting(Settings->SquadNameTable, TSoftObjectPtr<UDataTable>(Table.Get()));
    FText Error;
    auto Create = [&](const TCHAR* Name)
    {
        FGuid Id;
        TestTrue(TEXT("Create a named squad"), Squads->CreateSquad(FText::FromString(Name), nullptr, TEXT("NameTestTile"), Id, Error));
        return Id;
    };
    TestEqual(TEXT("Order then row name selects first trimmed valid name"), Squads->GetNextSquadName().ToString(), FString(TEXT("守望")));
    TestEqual(TEXT("Repeated preview does not advance the name"), Squads->GetNextSquadName().ToString(), FString(TEXT("守望")));
    TestTrue(TEXT("Name preview never creates a squad"), Squads->GetSquadIds().IsEmpty());
    const FGuid First = Create(TEXT("守望"));
    TestEqual(TEXT("Used first name advances to next configured name"), Squads->GetNextSquadName().ToString(), FString(TEXT("银翼")));
    Create(TEXT("银翼"));
    TestEqual(TEXT("Duplicate trimmed names do not consume another choice"), Squads->GetNextSquadName().ToString(), FString(TEXT("暮鸦")));
    Create(TEXT("暮鸦"));
    TestEqual(TEXT("Exhausted table continues after distinct valid configured names"), Squads->GetNextSquadName().ToString(), FString(TEXT("第4小队")));
    TestEqual(TEXT("Repeated fallback preview does not consume a number"), Squads->GetNextSquadName().ToString(), FString(TEXT("第4小队")));
    Create(TEXT("第4小队"));
    Create(TEXT("第5小队"));
    TestEqual(TEXT("Number search skips all official collisions"), Squads->GetNextSquadName().ToString(), FString(TEXT("第6小队")));
    TestTrue(TEXT("Remove first official squad"), Squads->RemoveSquad(First, Error));
    TestEqual(TEXT("Freed configured name becomes available again"), Squads->GetNextSquadName().ToString(), FString(TEXT("守望")));
    TestEqual(TEXT("Reading names never changes template rows"), Table->GetRowNames().Num(), 5);
    Squads->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadNameFallbackTest, "SilverChoir.Player.Squads.Names.Fallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadNameFallbackTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UPlayerUnitManagerBase> Units(NewObject<UPlayerUnitManagerBase>());
    TStrongObjectPtr<UPlayerSquadManagerBase> Squads(NewObject<UPlayerSquadManagerBase>());
    Squads->Initialize(Units.Get());
    UPlayerSquadSettings* Settings = GetMutableDefault<UPlayerSquadSettings>();
    TGuardValue<TSoftObjectPtr<UDataTable>> RestoreSetting(Settings->SquadNameTable, TSoftObjectPtr<UDataTable>());
    TestEqual(TEXT("Missing table starts numbered squads at one"), Squads->GetNextSquadName().ToString(), FString(TEXT("第1小队")));
    FGuid Id;
    FText Error;
    TestTrue(TEXT("Create fallback collision"), Squads->CreateSquad(FText::FromString(TEXT("第1小队")), nullptr, TEXT("NameTestTile"), Id, Error));
    TestEqual(TEXT("Fallback skips official names"), Squads->GetNextSquadName().ToString(), FString(TEXT("第2小队")));
    TStrongObjectPtr<UDataTable> Empty(NewObject<UDataTable>());
    Empty->RowStruct = FSquadNameTemplate::StaticStruct();
    Settings->SquadNameTable = Empty.Get();
    TestEqual(TEXT("Empty table uses collision-safe fallback"), Squads->GetNextSquadName().ToString(), FString(TEXT("第2小队")));
    FSquadNameTemplate Blank;
    Blank.SquadName = FText::FromString(TEXT(" \n \t "));
    Empty->AddRow(TEXT("Blank"), Blank);
    TestEqual(TEXT("Only-whitespace table also uses fallback"), Squads->GetNextSquadName().ToString(), FString(TEXT("第2小队")));
    TStrongObjectPtr<UDataTable> Wrong(NewObject<UDataTable>());
    Wrong->RowStruct = FVehicleTemplate::StaticStruct();
    Settings->SquadNameTable = Wrong.Get();
    TestEqual(TEXT("Wrong table structure safely falls back"), Squads->GetNextSquadName().ToString(), FString(TEXT("第2小队")));
    Squads->Shutdown();
    return true;
}
#endif
