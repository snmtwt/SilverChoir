#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Squads/SquadStructs.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/StrongObjectPtr.h"

namespace PlayerSquadTests
{
    struct FFixture
    {
        TStrongObjectPtr<UPlayerUnitManagerBase> Units{NewObject<UPlayerUnitManagerBase>()};
        TStrongObjectPtr<UPlayerSquadManagerBase> Squads{NewObject<UPlayerSquadManagerBase>()};
        FText Error;

        FFixture() { Squads->Initialize(Units.Get()); }
        ~FFixture() { Squads->Shutdown(); }

        FGuid AddUnit(const FName TileId)
        {
            FUnitData Unit;
            Unit.UnitId = FGuid::NewGuid();
            Unit.RuntimeData.TileId = TileId;
            return Units->LoadUnitData({Unit}, Error) ? Unit.UnitId : FGuid();
        }

        FGuid AddSquad(const TCHAR* Name, const FName TileId)
        {
            FGuid Id;
            Squads->CreateSquad(FText::FromString(Name), nullptr, TileId, Id, Error);
            return Id;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerSquadMembershipTest,
    "SilverChoir.Player.Squads.Membership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSquadMembershipTest::RunTest(const FString& Parameters)
{
    using namespace PlayerSquadTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    auto* Units = Fixture.Units.Get();
    auto& Error = Fixture.Error;
    const FName BaseTile(TEXT("SquadTest_Base"));
    const FName BattleTile(TEXT("SquadTest_Battle"));
    TestTrue(TEXT("Manager becomes ready with a unit manager"), Squads->IsReady());

    const FGuid FirstUnit = Fixture.AddUnit(BaseTile);
    const FGuid SecondUnit = Fixture.AddUnit(BaseTile);
    const FGuid DistantUnit = Fixture.AddUnit(BattleTile);
    const FGuid UnplacedUnit = Fixture.AddUnit(NAME_None);
    const FGuid Alpha = Fixture.AddSquad(TEXT("Alpha"), BaseTile);
    const FGuid Bravo = Fixture.AddSquad(TEXT("Bravo"), BaseTile);
    const FGuid DistantSquad = Fixture.AddSquad(TEXT("Distant"), BattleTile);
    if (!TestTrue(TEXT("Fixture records were created"), FirstUnit.IsValid() && SecondUnit.IsValid()
        && DistantUnit.IsValid() && UnplacedUnit.IsValid() && Alpha.IsValid()
        && Bravo.IsValid() && DistantSquad.IsValid())) return false;

    FGuid RejectedId;
    TestFalse(TEXT("A squad must have a known tile"), Squads->CreateSquad(
        FText::FromString(TEXT("Unplaced")), nullptr, NAME_None, RejectedId, Error));
    TestFalse(TEXT("Unknown unit cannot join"), Squads->AddUnitToSquad(FGuid::NewGuid(), Alpha, Error));
    TestFalse(TEXT("Unit without a tile cannot join"), Squads->AddUnitToSquad(UnplacedUnit, Alpha, Error));
    TestFalse(TEXT("Unit on a different tile cannot join"), Squads->AddUnitToSquad(DistantUnit, Alpha, Error));
    TestTrue(TEXT("Removing an unassigned known unit is harmless"), Squads->RemoveUnitFromSquad(FirstUnit, Error));

    const TSharedPtr<FUnitData> FirstData = Units->GetUnitDataShared(FirstUnit);
    int32 UnitNotifications = 0;
    const FDelegateHandle ChangeHandle = FirstData->OnDataChanged.AddLambda(
        [&UnitNotifications](FGuid) { ++UnitNotifications; });
    TestTrue(TEXT("Unit joins a colocated squad"), Squads->AddUnitToSquad(FirstUnit, Alpha, Error));
    TestTrue(TEXT("Joining notifies existing unit consumers"), UnitNotifications > 0);
    TestTrue(TEXT("Repeated join succeeds"), Squads->AddUnitToSquad(FirstUnit, Alpha, Error));

    FSquadData Found;
    TestTrue(TEXT("Membership is discoverable by unit ID"), Squads->GetUnitSquad(FirstUnit, Found));
    TestEqual(TEXT("Reverse lookup resolves Alpha"), Found.SquadId, Alpha);
    TestEqual(TEXT("Repeated joins do not duplicate members"), Found.MemberUnitIds.Num(), 1);
    const TArray<TSharedPtr<FUnitData>> SharedMembers = Squads->GetSquadUnitsShared(Alpha);
    if (TestEqual(TEXT("One canonical unit is returned"), SharedMembers.Num(), 1))
    {
        TestTrue(TEXT("Squad lookup returns the unit manager allocation"), SharedMembers[0].Get() == FirstData.Get());
        SharedMembers[0]->Modify([](FUnitData& Unit) { Unit.RuntimeData.CurrentHealth = 37.f; });
        TestEqual(TEXT("Edits through squad lookup are visible to all consumers"),
            FirstData->RuntimeData.CurrentHealth, 37.f);
    }

    TestTrue(TEXT("Joining another colocated squad transfers membership"), Squads->AddUnitToSquad(FirstUnit, Bravo, Error));
    Squads->GetSquad(Alpha, Found);
    TestTrue(TEXT("Transferred unit leaves the original squad"), Found.MemberUnitIds.IsEmpty());
    TestFalse(TEXT("A location mismatch rejects transfer"), Squads->AddUnitToSquad(FirstUnit, DistantSquad, Error));
    TestFalse(TEXT("An unknown destination rejects transfer"), Squads->AddUnitToSquad(FirstUnit, FGuid::NewGuid(), Error));
    TestTrue(TEXT("Failed transfers preserve the original membership"), Squads->GetUnitSquad(FirstUnit, Found));
    TestEqual(TEXT("Failed transfers leave the unit in Bravo"), Found.SquadId, Bravo);
    TestEqual(TEXT("Failed transfers do not duplicate membership"), Found.MemberUnitIds.Num(), 1);

    FUnitData Reload = *FirstData;
    Reload.RuntimeData.CurrentHealth = 61.f;
    TestTrue(TEXT("Existing unit can be reloaded"), Units->LoadUnitData({Reload}, Error));
    TestTrue(TEXT("Reload retains the canonical pointer"), Units->GetUnitDataShared(FirstUnit).Get() == FirstData.Get());
    TestTrue(TEXT("Reload on the same tile retains squad membership"), Squads->GetUnitSquad(FirstUnit, Found));
    TestEqual(TEXT("Reload retains the correct squad"), Found.SquadId, Bravo);
    TestEqual(TEXT("Earlier consumers observe the reload"), FirstData->RuntimeData.CurrentHealth, 61.f);

    TestTrue(TEXT("Second member joins Bravo"), Squads->AddUnitToSquad(SecondUnit, Bravo, Error));
    bool bReceivedMoveNotification = false;
    bool bReentryAttempted = false;
    const FDelegateHandle MoveHandle = FirstData->OnDataChanged.AddLambda([&](FGuid)
    {
        bReceivedMoveNotification = true;
        FSquadData MovingSquad;
        TestTrue(TEXT("Squad remains queryable during a move notification"), Squads->GetSquad(Bravo, MovingSquad));
        TestEqual(TEXT("Squad position is updated before notification"), MovingSquad.TileId, BattleTile);
        TestEqual(TEXT("First member position is updated before notification"), FirstData->RuntimeData.TileId, BattleTile);
        TestEqual(TEXT("Other member position is updated before notification"),
            Units->GetUnitDataShared(SecondUnit)->RuntimeData.TileId, BattleTile);
        bReentryAttempted = true;
        FText NestedError;
        TestFalse(TEXT("Notification callbacks cannot mutate squad membership reentrantly"),
            Squads->RemoveUnitFromSquad(SecondUnit, NestedError));
    });
    TestTrue(TEXT("Squad relocation moves all members"), Squads->SetSquadTileId(Bravo, BattleTile, Error));
    TestTrue(TEXT("Move notifies unit consumers"), bReceivedMoveNotification);
    TestTrue(TEXT("Reentry guard was exercised"), bReentryAttempted);
    FirstData->OnDataChanged.Remove(MoveHandle);
    TestFalse(TEXT("Relocating to an unknown tile is rejected"), Squads->SetSquadTileId(Bravo, NAME_None, Error));
    TestTrue(TEXT("Reentrant removal did not remove the second member"), Squads->GetUnitSquad(SecondUnit, Found));
    TestEqual(TEXT("Rejected operations retain the destination tile"), FirstData->RuntimeData.TileId, BattleTile);

    bool bExternalNotificationChecked = false;
    const FDelegateHandle ExternalChangeHandle = FirstData->OnDataChanged.AddLambda([&](FGuid)
    {
        bExternalNotificationChecked = true;
        FText NestedError;
        TestFalse(TEXT("Direct unit notifications also reject reentrant squad edits"),
            Squads->UpdateSquadInfo(Bravo, FText::FromString(TEXT("Reentrant")), nullptr, NestedError));
    });
    FirstData->Modify([](FUnitData& Unit) { Unit.RuntimeData.CurrentHealth = 58.f; });
    FirstData->OnDataChanged.Remove(ExternalChangeHandle);
    TestTrue(TEXT("The direct notification guard was exercised"), bExternalNotificationChecked);

    const int32 BeforeInfoChange = UnitNotifications;
    TestTrue(TEXT("Squad name can be changed"), Squads->UpdateSquadInfo(Bravo, FText::FromString(TEXT("Bravo Updated")), nullptr, Error));
    TestTrue(TEXT("Squad metadata change notifies dependent unit UI"), UnitNotifications > BeforeInfoChange);
    TestTrue(TEXT("Member removal succeeds"), Squads->RemoveUnitFromSquad(FirstUnit, Error));
    TestFalse(TEXT("Removed member has no squad"), Squads->GetUnitSquad(FirstUnit, Found));
    TestTrue(TEXT("Member removal does not delete canonical unit data"), Units->GetUnitDataShared(FirstUnit).Get() == FirstData.Get());
    TestTrue(TEXT("Squad deletion succeeds"), Squads->RemoveSquad(Bravo, Error));
    TestFalse(TEXT("Deleting a squad frees remaining members"), Squads->GetUnitSquad(SecondUnit, Found));
    TestTrue(TEXT("Deleting a squad preserves its units"), Units->GetUnitDataShared(SecondUnit).IsValid());

    FirstData->OnDataChanged.Remove(ChangeHandle);
    const int32 AfterUnsubscribe = UnitNotifications;
    FirstData->NotifyDataChanged();
    TestEqual(TEXT("Consumers can remove their subscription"), UnitNotifications, AfterUnsubscribe);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerSquadPersistenceTest,
    "SilverChoir.Player.Squads.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSquadPersistenceTest::RunTest(const FString& Parameters)
{
    using namespace PlayerSquadTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    auto* Units = Fixture.Units.Get();
    auto& Error = Fixture.Error;
    const FName Tile(TEXT("SquadTest_Load"));
    const FGuid FirstUnit = Fixture.AddUnit(Tile);
    const FGuid SecondUnit = Fixture.AddUnit(Tile);
    const FGuid OldSquad = Fixture.AddSquad(TEXT("Old Squad"), Tile);
    if (!TestTrue(TEXT("Fixture was created"), FirstUnit.IsValid() && SecondUnit.IsValid() && OldSquad.IsValid())) return false;
    TestTrue(TEXT("Initial membership is established"), Squads->AddUnitToSquad(FirstUnit, OldSquad, Error));
    const auto CanonicalUnit = Units->GetUnitDataShared(FirstUnit);

    FSquadData Replacement;
    Replacement.SquadId = FGuid::NewGuid();
    Replacement.SquadName = FText::FromString(TEXT("Restored Squad"));
    Replacement.TileId = Tile;
    Replacement.MemberUnitIds = {FirstUnit, SecondUnit};
    FSquadData Invalid = Replacement;
    Invalid.SquadId = FGuid::NewGuid();
    Invalid.MemberUnitIds = {FGuid::NewGuid()};
    TestFalse(TEXT("Missing units reject a whole saved batch"), Squads->LoadSquadData({Replacement, Invalid}, Error));
    FSquadData Found;
    TestTrue(TEXT("Failed load retains previous membership"), Squads->GetUnitSquad(FirstUnit, Found));
    TestEqual(TEXT("Failed load retains previous squad ID"), Found.SquadId, OldSquad);
    TestEqual(TEXT("Failed load does not add any squad"), Squads->GetSquadIds().Num(), 1);

    Invalid = Replacement;
    Invalid.SquadId = FGuid::NewGuid();
    TestFalse(TEXT("A unit cannot be restored into two squads"), Squads->LoadSquadData({Replacement, Invalid}, Error));
    Invalid = Replacement;
    Invalid.MemberUnitIds.Add(FirstUnit);
    TestFalse(TEXT("Duplicate members in one saved squad are rejected"), Squads->LoadSquadData({Invalid}, Error));
    TestFalse(TEXT("Duplicate squad IDs in one saved batch are rejected"), Squads->LoadSquadData({Replacement, Replacement}, Error));
    Invalid = Replacement;
    Invalid.TileId = TEXT("OtherTile");
    TestFalse(TEXT("Saved squad and member locations must agree"), Squads->LoadSquadData({Invalid}, Error));
    Invalid = Replacement;
    Invalid.TileId = NAME_None;
    TestFalse(TEXT("Saved squads cannot have an unknown location"), Squads->LoadSquadData({Invalid}, Error));
    Invalid = Replacement;
    Invalid.SquadId.Invalidate();
    TestFalse(TEXT("Saved squads require a stable identity"), Squads->LoadSquadData({Invalid}, Error));
    TestTrue(TEXT("All rejected batches preserve original squad"), Squads->GetSquad(OldSquad, Found));

    Replacement.SquadIcon = NewObject<UTexture2D>();
    const TWeakObjectPtr<UTexture2D> Icon = Replacement.SquadIcon;
    TestTrue(TEXT("Valid saved batch is restored"), Squads->LoadSquadData({Replacement}, Error));
    TestFalse(TEXT("Successful restore replaces previous squad records"), Squads->GetSquad(OldSquad, Found));
    TestTrue(TEXT("Successful restore builds reverse lookup"), Squads->GetUnitSquad(FirstUnit, Found));
    TestEqual(TEXT("Restored reverse lookup points at saved squad"), Found.SquadId, Replacement.SquadId);
    TestEqual(TEXT("Restored roster includes both units"), Found.MemberUnitIds.Num(), 2);
    TestTrue(TEXT("Restore never replaces canonical unit storage"), Units->GetUnitDataShared(FirstUnit).Get() == CanonicalUnit.Get());
    TestEqual(TEXT("Squad snapshots contain every restored record"), Squads->GetAllSquads().Num(), 1);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Stored squad keeps its icon alive"), Icon.IsValid());

    TestTrue(TEXT("Empty save clears all squads"), Squads->LoadSquadData({}, Error));
    TestTrue(TEXT("All squad records are removed"), Squads->GetSquadIds().IsEmpty());
    TestFalse(TEXT("Empty save clears reverse membership"), Squads->GetUnitSquad(FirstUnit, Found));
    TestTrue(TEXT("Empty squad save preserves the authoritative unit record"), Units->GetUnitDataShared(FirstUnit).Get() == CanonicalUnit.Get());
    Fixture.Squads->Shutdown();
    TestFalse(TEXT("Shutdown marks squad manager unready"), Squads->IsReady());
    CanonicalUnit->Modify([](FUnitData& Unit) { Unit.RuntimeData.CurrentHealth = 17.f; });
    TestEqual(TEXT("Unit consumers remain usable after squad shutdown"), CanonicalUnit->RuntimeData.CurrentHealth, 17.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerSquadSubsystemTest,
    "SilverChoir.Player.Squads.Subsystem",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSquadSubsystemTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::Game)
        {
            World = Context.World();
            break;
        }
    }
    if (!TestNotNull(TEXT("A running game world is available"), World)) return false;
    if (!TestTrue(TEXT("Game instance initializes the squad subsystem and its unit dependency"),
        UPlayerSquadLibrary::IsPlayerSquadSystemReady(World))) return false;
    TestNotNull(TEXT("Blueprint library resolves the squad subsystem"), UPlayerSquadLibrary::GetPlayerSquadSubsystem(World));
    TestNotNull(TEXT("Blueprint library resolves the squad manager"), UPlayerSquadLibrary::GetPlayerSquadManager(World));
    TestFalse(TEXT("Absent world context is safe"), UPlayerSquadLibrary::IsPlayerSquadSystemReady(nullptr));
    TestTrue(TEXT("Absent context returns no unit references"), UPlayerSquadLibrary::GetSquadUnitReferences(nullptr, FGuid::NewGuid()).IsEmpty());

    auto* UnitManager = UPlayerUnitLibrary::GetPlayerUnitManager(World);
    if (!TestNotNull(TEXT("Unit dependency has a live manager"), UnitManager)) return false;
    FUnitData Unit;
    Unit.UnitId = FGuid::NewGuid();
    Unit.RuntimeData.TileId = TEXT("SquadTest_World");
    FText Error;
    if (!TestTrue(TEXT("Canonical unit is loaded in this game instance"), UnitManager->LoadUnitData({Unit}, Error))) return false;
    FGuid SquadId;
    if (!TestTrue(TEXT("Blueprint library creates a squad"), UPlayerSquadLibrary::CreateSquad(
        World, FText::FromString(TEXT("Automation Squad")), nullptr, Unit.RuntimeData.TileId, SquadId, Error))) return false;
    TestTrue(TEXT("Blueprint library joins the unit"), UPlayerSquadLibrary::AddUnitToSquad(World, Unit.UnitId, SquadId, Error));

    const auto CanonicalUnit = UnitManager->GetUnitDataShared(Unit.UnitId);
    const auto NativeMembers = UPlayerSquadLibrary::GetSquadUnitsShared(World, SquadId);
    if (TestEqual(TEXT("Native library exposes one member"), NativeMembers.Num(), 1))
    {
        TestTrue(TEXT("Native lookup uses canonical storage"), NativeMembers[0].Get() == CanonicalUnit.Get());
    }
    const auto References = UPlayerSquadLibrary::GetSquadUnitReferences(World, SquadId);
    if (TestEqual(TEXT("Blueprint library exposes one shared reference"), References.Num(), 1)
        && TestNotNull(TEXT("Blueprint reference exists"), References[0]))
    {
        TStrongObjectPtr<UUnitDataReference> Reference(References[0]);
        TestTrue(TEXT("Blueprint and native consumers share the same allocation"), Reference->GetSharedData().Get() == CanonicalUnit.Get());
        CanonicalUnit->Modify([](FUnitData& Data) { Data.RuntimeData.CurrentStamina = 42.f; });
        TestEqual(TEXT("Blueprint reference observes current shared data"), Reference->GetSnapshot().RuntimeData.CurrentStamina, 42.f);
    }
    FSquadData Found;
    TestTrue(TEXT("Blueprint reverse lookup succeeds"), UPlayerSquadLibrary::GetUnitSquad(World, Unit.UnitId, Found));
    TestEqual(TEXT("Blueprint reverse lookup resolves the created squad"), Found.SquadId, SquadId);
    TestEqual(TEXT("Blueprint library reports four seats without a vehicle"), UPlayerSquadLibrary::GetSquadMaxMemberCount(World, SquadId), 4);
    FVehicleData Vehicle;
    Vehicle.VehicleId = FGuid::NewGuid();
    Vehicle.RuntimeData.TileId = Unit.RuntimeData.TileId;
    Vehicle.Attributes.PassengerCapacity = 0;
    TestTrue(TEXT("Vehicle is registered in canonical player storage"), UnitManager->LoadVehicleData({Vehicle}, Error));
    TArray<FGuid> Removed = {FGuid::NewGuid()};
    TestFalse(TEXT("Missing context cannot assign a squad vehicle"), UPlayerSquadLibrary::SetSquadVehicle(nullptr, SquadId, Vehicle, Removed, Error));
    TestTrue(TEXT("Missing context clears removed member output"), Removed.IsEmpty());
    TestEqual(TEXT("Missing context has no squad capacity"), UPlayerSquadLibrary::GetSquadMaxMemberCount(nullptr, SquadId), 0);
    TestTrue(TEXT("Blueprint library assigns a zero-seat vehicle"), UPlayerSquadLibrary::SetSquadVehicle(World, SquadId, Vehicle, Removed, Error));
    TestTrue(TEXT("Blueprint reverse lookup resolves vehicle ownership"), UPlayerSquadLibrary::GetVehicleSquad(World, Vehicle.VehicleId, Found));
    TestEqual(TEXT("Vehicle reverse index resolves the assigned squad"), Found.SquadId, SquadId);
    TestTrue(TEXT("Native library exposes the canonical vehicle pointer"),
        UPlayerSquadLibrary::GetSquadVehicleShared(World, SquadId).Get() == UnitManager->GetVehicleDataShared(Vehicle.VehicleId).Get());
    TestTrue(TEXT("Blueprint assignment reports evicted members"), Removed == TArray<FGuid>({Unit.UnitId}));
    TestEqual(TEXT("Blueprint capacity follows vehicle seats"), UPlayerSquadLibrary::GetSquadMaxMemberCount(World, SquadId), 0);
    TestTrue(TEXT("Vehicle eviction keeps the canonical unit allocation"), UnitManager->GetUnitDataShared(Unit.UnitId).Get() == CanonicalUnit.Get());
    TestTrue(TEXT("Blueprint library clears the squad vehicle"), UPlayerSquadLibrary::ClearSquadVehicle(World, SquadId, Removed, Error));
    TestFalse(TEXT("Clear releases vehicle reverse index"), UPlayerSquadLibrary::GetVehicleSquad(World, Vehicle.VehicleId, Found));
    TestTrue(TEXT("Clearing an empty vehicle squad evicts nobody"), Removed.IsEmpty());
    TestEqual(TEXT("Clearing vehicle restores four seats"), UPlayerSquadLibrary::GetSquadMaxMemberCount(World, SquadId), 4);
    TestTrue(TEXT("Unit can rejoin after restoring default capacity"), UPlayerSquadLibrary::AddUnitToSquad(World, Unit.UnitId, SquadId, Error));
    TestTrue(TEXT("Blueprint library can remove the test squad"), UPlayerSquadLibrary::RemoveSquad(World, SquadId, Error));
    TestFalse(TEXT("Removing the squad clears reverse lookup"), UPlayerSquadLibrary::GetUnitSquad(World, Unit.UnitId, Found));
    TestTrue(TEXT("Temporary vehicle can be removed after squad cleanup"), UnitManager->RemoveVehicleData(Vehicle.VehicleId, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerSquadCaptainAndIconTest,
    "SilverChoir.Player.Squads.CaptainAndIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSquadCaptainAndIconTest::RunTest(const FString& Parameters)
{
    using namespace PlayerSquadTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    auto* Units = Fixture.Units.Get();
    auto& Error = Fixture.Error;
    const FName Tile(TEXT("SquadTest_Captain"));
    const FGuid First = Fixture.AddUnit(Tile);
    const FGuid Second = Fixture.AddUnit(Tile);
    const FGuid Third = Fixture.AddUnit(Tile);
    const FGuid Alpha = Fixture.AddSquad(TEXT("Alpha"), Tile);
    const FGuid Bravo = Fixture.AddSquad(TEXT("Bravo"), Tile);
    const FGuid Distant = Fixture.AddSquad(TEXT("Distant"), TEXT("OtherTile"));
    TestTrue(TEXT("Manager subscribes to canonical unit changes"),
        Units->OnUnitDataChanged.Contains(Squads, TEXT("HandleUnitDataChanged")));

    FSquadData Found;
    Squads->GetSquad(Alpha, Found);
    TestFalse(TEXT("An empty squad has no captain"), Found.CaptainUnitId.IsValid());
    TestTrue(TEXT("First member joins"), Squads->AddUnitToSquad(First, Alpha, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("First member automatically becomes captain"), Found.CaptainUnitId, First);
    TestTrue(TEXT("Second member joins"), Squads->AddUnitToSquad(Second, Alpha, Error));
    TestTrue(TEXT("Third member joins"), Squads->AddUnitToSquad(Third, Alpha, Error));
    TestFalse(TEXT("An outsider cannot become captain"), Squads->SetSquadCaptain(Alpha, FGuid::NewGuid(), Error));
    TestFalse(TEXT("A nonempty squad cannot clear its captain"), Squads->SetSquadCaptain(Alpha, FGuid(), Error));
    TestTrue(TEXT("A member can be promoted"), Squads->SetSquadCaptain(Alpha, Second, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Promotion selects the requested unit"), Found.CaptainUnitId, Second);
    TestFalse(TEXT("Captain cannot transfer to another tile"), Squads->AddUnitToSquad(Second, Distant, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Rejected transfer preserves the captain"), Found.CaptainUnitId, Second);

    TStrongObjectPtr<UTexture2D> Preset(NewObject<UTexture2D>());
    TStrongObjectPtr<UTexture2D> Portrait(NewObject<UTexture2D>());
    TStrongObjectPtr<UTexture2D> UpdatedPortrait(NewObject<UTexture2D>());
    TestTrue(TEXT("Preset icon can be assigned"), Squads->UpdateSquadInfo(Alpha, Found.SquadName, Preset.Get(), Error));
    TestTrue(TEXT("Preset mode resolves the configured icon"), Squads->GetSquadIcon(Alpha) == Preset.Get());
    TestTrue(TEXT("Captain portrait mode can be selected"), Squads->SetSquadIconSource(Alpha, ESquadIconSource::CaptainPortrait, Error));
    TestTrue(TEXT("Missing captain portrait falls back to preset"), Squads->GetSquadIcon(Alpha) == Preset.Get());

    const auto CanonicalCaptain = Units->GetUnitDataShared(Second);
    CanonicalCaptain->Modify([&](FUnitData& Unit) { Unit.Profile.PortraitTexture = Portrait.Get(); });
    TestTrue(TEXT("Portrait mode resolves the original shared unit's portrait"), Squads->GetSquadIcon(Alpha) == Portrait.Get());
    CanonicalCaptain->Modify([&](FUnitData& Unit) { Unit.Profile.PortraitTexture = UpdatedPortrait.Get(); });
    TestTrue(TEXT("Portrait changes are read live without a copied unit record"), Squads->GetSquadIcon(Alpha) == UpdatedPortrait.Get());
    Squads->GetSquad(Alpha, Found);
    TestTrue(TEXT("Dynamic portraits never overwrite the saved preset"), Found.SquadIcon == Preset.Get());
    TestTrue(TEXT("Squad queries retain the canonical allocation"), Squads->GetSquadUnitsShared(Alpha)[1].Get() == CanonicalCaptain.Get());

    bool bTestedReentry = false;
    const FDelegateHandle Reentry = CanonicalCaptain->OnDataChanged.AddLambda([&](FGuid)
    {
        bTestedReentry = true;
        FText NestedError;
        TestFalse(TEXT("Unit callbacks cannot change the captain reentrantly"), Squads->SetSquadCaptain(Alpha, First, NestedError));
        TestFalse(TEXT("Unit callbacks cannot change the icon mode reentrantly"), Squads->SetSquadIconSource(Alpha, ESquadIconSource::Preset, NestedError));
    });
    CanonicalCaptain->NotifyDataChanged();
    CanonicalCaptain->OnDataChanged.Remove(Reentry);
    TestTrue(TEXT("Reentry checks were exercised"), bTestedReentry);

    TestTrue(TEXT("Captain can transfer after validation"), Squads->AddUnitToSquad(Second, Bravo, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Original squad appoints the earliest remaining member"), Found.CaptainUnitId, First);
    Squads->GetSquad(Bravo, Found);
    TestEqual(TEXT("First member becomes destination captain"), Found.CaptainUnitId, Second);
    TestTrue(TEXT("Original squad portrait follows its replacement captain"), Squads->GetSquadIcon(Alpha) == Preset.Get());
    TestTrue(TEXT("Removing a captain succeeds"), Squads->RemoveUnitFromSquad(First, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Next remaining member is promoted"), Found.CaptainUnitId, Third);
    TestTrue(TEXT("Last member can leave"), Squads->RemoveUnitFromSquad(Third, Error));
    Squads->GetSquad(Alpha, Found);
    TestFalse(TEXT("Empty squad clears captain identity"), Found.CaptainUnitId.IsValid());

    FSquadData Saved;
    Saved.SquadId = Alpha;
    Saved.SquadName = FText::FromString(TEXT("Restored Alpha"));
    Saved.TileId = Tile;
    Saved.MemberUnitIds = {First, Third};
    Saved.SquadIcon = Preset.Get();
    Saved.IconSource = ESquadIconSource::CaptainPortrait;
    FSquadData Invalid = Saved;
    Invalid.CaptainUnitId = Second;
    TestFalse(TEXT("Saved captain outside the roster rejects the full load"), Squads->LoadSquadData({Invalid}, Error));
    Squads->GetSquad(Bravo, Found);
    TestEqual(TEXT("Rejected save leaves prior captain intact"), Found.CaptainUnitId, Second);
    Invalid = Saved;
    Invalid.IconSource = static_cast<ESquadIconSource>(255);
    TestFalse(TEXT("Saved invalid icon modes are rejected"), Squads->LoadSquadData({Invalid}, Error));
    TestTrue(TEXT("Old saves without a captain still load"), Squads->LoadSquadData({Saved}, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Old saves appoint the first roster member"), Found.CaptainUnitId, First);
    TestTrue(TEXT("Old save icon selection is preserved"), Found.IconSource == ESquadIconSource::CaptainPortrait);
    Saved.CaptainUnitId = Third;
    TestTrue(TEXT("Saves preserve an explicit valid captain"), Squads->LoadSquadData({Saved}, Error));
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Explicit captain survives restore"), Found.CaptainUnitId, Third);
    TestTrue(TEXT("Clearing all squads succeeds"), Squads->LoadSquadData({}, Error));
    TestNull(TEXT("Removed squad resolves no icon"), Squads->GetSquadIcon(Alpha));
    TestNull(TEXT("Library missing context resolves no icon"), UPlayerSquadLibrary::GetSquadIcon(nullptr, Alpha));

    Squads->Shutdown();
    TestFalse(TEXT("Shutdown removes the unit change subscription"),
        Units->OnUnitDataChanged.Contains(Squads, TEXT("HandleUnitDataChanged")));
    Squads->Initialize(Units);
    TestTrue(TEXT("Reinitializing installs one live subscription"),
        Units->OnUnitDataChanged.GetAllObjects().FilterByPredicate(
            [Squads](const UObject* Object) { return Object == Squads; }).Num() == 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerSquadCanonicalVehicleTest,
    "SilverChoir.Player.Squads.CanonicalVehicleBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSquadCanonicalVehicleTest::RunTest(const FString& Parameters)
{
    using namespace PlayerSquadTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    auto* Units = Fixture.Units.Get();
    auto& Error = Fixture.Error;
    const FName Tile(TEXT("VehicleBinding_Base"));
    const FGuid Alpha = Fixture.AddSquad(TEXT("Alpha"), Tile);
    const FGuid Bravo = Fixture.AddSquad(TEXT("Bravo"), Tile);
    FVehicleData First;
    First.VehicleId = FGuid::NewGuid();
    First.RuntimeData.TileId = Tile;
    First.Attributes.PassengerCapacity = 6;
    FVehicleData Second = First;
    Second.VehicleId = FGuid::NewGuid();
    TArray<FGuid> Removed;
    TestFalse(TEXT("Unregistered vehicles cannot be assigned"), Squads->SetSquadVehicle(Alpha, First, Removed, Error));
    TestTrue(TEXT("Canonical vehicles load"), Units->LoadVehicleData({First, Second}, Error));
    const auto Canonical = Units->GetVehicleDataShared(First.VehicleId);
    TestTrue(TEXT("Assign first canonical vehicle"), Squads->SetSquadVehicle(Alpha, First, Removed, Error));
    TestTrue(TEXT("Assign second canonical vehicle"), Squads->SetSquadVehicle(Bravo, Second, Removed, Error));
    TestTrue(TEXT("Squad shares the canonical vehicle allocation"), Squads->GetSquadVehicleShared(Alpha).Get() == Canonical.Get());
    FSquadData Found;
    TestTrue(TEXT("Vehicle has a reverse index"), Squads->GetVehicleSquad(First.VehicleId, Found));
    TestEqual(TEXT("Reverse index has the correct squad"), Found.SquadId, Alpha);
    TestFalse(TEXT("A second squad cannot claim an occupied vehicle"), Squads->SetSquadVehicle(Bravo, First, Removed, Error));

    FVehicleData Forged = First;
    Forged.Attributes.PassengerCapacity = 99;
    Forged.RuntimeData.TileId = TEXT("Forged_Tile");
    TestTrue(TEXT("Assignment only reads the input vehicle identity"), Squads->SetSquadVehicle(Alpha, Forged, Removed, Error));
    TestEqual(TEXT("Forged seats never replace canonical capacity"), Squads->GetSquadMaxMemberCount(Alpha), 6);
    TestEqual(TEXT("Forged tile never moves canonical vehicle"), Canonical->RuntimeData.TileId, Tile);

    TArray<FGuid> AlphaMembers;
    TArray<FGuid> BravoMembers;
    for (int32 Index = 0; Index < 6; ++Index)
    {
        AlphaMembers.Add(Fixture.AddUnit(Tile));
        BravoMembers.Add(Fixture.AddUnit(Tile));
        TestTrue(TEXT("Alpha member joins"), Squads->AddUnitToSquad(AlphaMembers.Last(), Alpha, Error));
        TestTrue(TEXT("Bravo member joins"), Squads->AddUnitToSquad(BravoMembers.Last(), Bravo, Error));
    }
    TestTrue(TEXT("Last member becomes Alpha captain"), Squads->SetSquadCaptain(Alpha, AlphaMembers.Last(), Error));
    int32 BatchChecks = 0;
    const auto Evicted = Units->GetUnitDataShared(AlphaMembers[4]);
    const FDelegateHandle BatchHandle = Evicted->OnDataChanged.AddLambda([&](FGuid)
    {
        ++BatchChecks;
        FSquadData A, B;
        Squads->GetSquad(Alpha, A);
        Squads->GetSquad(Bravo, B);
        TestEqual(TEXT("Batch reconciles first squad before callbacks"), A.MemberUnitIds.Num(), 2);
        TestEqual(TEXT("Batch reconciles second squad before callbacks"), B.MemberUnitIds.Num(), 3);
        TestFalse(TEXT("Batch callbacks cannot remove a vehicle reentrantly"), Units->RemoveVehicleData(Second.VehicleId, Error));
    });
    First.Attributes.PassengerCapacity = 2;
    Second.Attributes.PassengerCapacity = 3;
    TestTrue(TEXT("Canonical batch capacity update succeeds"), Units->LoadVehicleData({First, Second}, Error));
    Evicted->OnDataChanged.Remove(BatchHandle);
    TestEqual(TEXT("Batch observer ran exactly once"), BatchChecks, 1);
    TestTrue(TEXT("Reload preserves canonical allocation"), Units->GetVehicleDataShared(First.VehicleId).Get() == Canonical.Get());
    Squads->GetSquad(Alpha, Found);
    TestTrue(TEXT("Capacity trim retains captain and earliest member"),
        Found.MemberUnitIds == TArray<FGuid>({AlphaMembers[0], AlphaMembers[5]}));
    TestFalse(TEXT("Evicted member index is released"), Squads->GetUnitSquad(AlphaMembers[4], Found));

    const FName Destination(TEXT("VehicleBinding_Moved"));
    TestTrue(TEXT("Squad moves together with its canonical vehicle"), Squads->SetSquadTileId(Alpha, Destination, Error));
    TestEqual(TEXT("Shared vehicle sees the squad destination"), Canonical->RuntimeData.TileId, Destination);
    TestEqual(TEXT("Shared members see the squad destination"), Units->GetUnitDataShared(AlphaMembers[0])->RuntimeData.TileId, Destination);
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Assigned snapshot follows canonical tile"), Found.AssignedVehicle.RuntimeData.TileId, Destination);

    Canonical->Modify([](FVehicleData& Data) { Data.RuntimeData.TileId = TEXT("VehicleBinding_Detached"); });
    TestFalse(TEXT("Direct vehicle movement releases its previous squad"), Squads->GetVehicleSquad(First.VehicleId, Found));
    TestEqual(TEXT("Detached squad restores four seats"), Squads->GetSquadMaxMemberCount(Alpha), 4);
    Squads->GetSquad(Alpha, Found);
    TestEqual(TEXT("Direct vehicle move never teleports the squad"), Found.TileId, Destination);
    TestTrue(TEXT("Removing an assigned vehicle succeeds"), Units->RemoveVehicleData(Second.VehicleId, Error));
    TestFalse(TEXT("Removed vehicle leaves no reverse index"), Squads->GetVehicleSquad(Second.VehicleId, Found));
    TestEqual(TEXT("Removed vehicle restores default capacity"), Squads->GetSquadMaxMemberCount(Bravo), 4);

    Canonical->Modify([&](FVehicleData& Data) { Data.RuntimeData.TileId = Destination; });
    TestTrue(TEXT("Free vehicle can be assigned again"), Squads->SetSquadVehicle(Alpha, *Canonical, Removed, Error));
    const TArray<FSquadData> Saved = Squads->GetAllSquads();
    TestTrue(TEXT("Clearing squads succeeds"), Squads->LoadSquadData({}, Error));
    TestFalse(TEXT("Clearing squads frees vehicles without deleting them"), Squads->GetVehicleSquad(First.VehicleId, Found));
    TestTrue(TEXT("Vehicle allocation survives clearing squads"), Units->GetVehicleDataShared(First.VehicleId).Get() == Canonical.Get());
    TestTrue(TEXT("Squad restore rebuilds vehicle reverse index"), Squads->LoadSquadData(Saved, Error));
    TestTrue(TEXT("Restored vehicle resolves its owner"), Squads->GetVehicleSquad(First.VehicleId, Found));
    TestEqual(TEXT("Restored vehicle belongs to Alpha"), Found.SquadId, Alpha);
    TestTrue(TEXT("Disbanding releases assigned vehicle"), Squads->RemoveSquad(Alpha, Error));
    TestFalse(TEXT("Disbanded vehicle has no owner"), Squads->GetVehicleSquad(First.VehicleId, Found));
    return true;
}
#endif
