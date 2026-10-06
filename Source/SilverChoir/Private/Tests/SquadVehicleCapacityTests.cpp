#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Squads/SquadStructs.h"
#include "Engine/Texture2D.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/StrongObjectPtr.h"

namespace SquadVehicleTests
{
    FVehicleData MakeVehicle(FName Tile, int32 Capacity)
    {
        FVehicleData Vehicle;
        Vehicle.VehicleId = FGuid::NewGuid();
        Vehicle.Profile.VehicleName = FText::FromString(TEXT("Capacity test vehicle"));
        Vehicle.Attributes.PassengerCapacity = Capacity;
        Vehicle.RuntimeData.TileId = Tile;
        Vehicle.RuntimeData.CurrentFuel = 25.f;
        return Vehicle;
    }

    struct FFixture
    {
        TStrongObjectPtr<UPlayerUnitManagerBase> Units{NewObject<UPlayerUnitManagerBase>()};
        TStrongObjectPtr<UPlayerSquadManagerBase> Squads{NewObject<UPlayerSquadManagerBase>()};
        FText Error;
        FFixture() { Squads->Initialize(Units.Get()); }
        ~FFixture() { Squads->Shutdown(); }
        TArray<FGuid> AddUnits(FName Tile, int32 Count)
        {
            TArray<FUnitData> Data;
            TArray<FGuid> Ids;
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FUnitData Unit;
                Unit.UnitId = FGuid::NewGuid();
                Unit.RuntimeData.TileId = Tile;
                Ids.Add(Unit.UnitId);
                Data.Add(Unit);
            }
            if (!Units->LoadUnitData(Data, Error)) Ids.Reset();
            return Ids;
        }
        FGuid AddSquad(FName Tile, const TCHAR* Name)
        {
            FGuid Id;
            Squads->CreateSquad(FText::FromString(Name), nullptr, Tile, Id, Error);
            return Id;
        }
        FVehicleData AddVehicle(FName Tile, int32 Capacity)
        {
            FVehicleData Vehicle = MakeVehicle(Tile, Capacity);
            if (!Units->LoadVehicleData({Vehicle}, Error)) Vehicle.VehicleId.Invalidate();
            return Vehicle;
        }
    };

    bool HasExactly(const TArray<FGuid>& Actual, const TArray<FGuid>& Expected)
    {
        if (Actual.Num() != Expected.Num()) return false;
        for (const FGuid Id : Expected) if (!Actual.Contains(Id)) return false;
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadCapacityValueTest, "SilverChoir.Player.Squads.Capacity.ValueRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSquadCapacityValueTest::RunTest(const FString& Parameters)
{
    using namespace SquadVehicleTests;
    FSquadData Squad;
    TestFalse(TEXT("New squad has no assigned vehicle"), Squad.HasAssignedVehicle());
    TestEqual(TEXT("Squad without vehicle has four seats"), Squad.GetMaxMemberCount(), 4);
    Squad.AssignedVehicle.Attributes.PassengerCapacity = 99;
    TestEqual(TEXT("Vehicle fields without an identity do not expand capacity"), Squad.GetMaxMemberCount(), 4);
    for (int32 Index = 0; Index < 6; ++Index) Squad.MemberUnitIds.Add(FGuid::NewGuid());
    const TArray<FGuid> OriginalIds = Squad.MemberUnitIds;
    Squad.CaptainUnitId = OriginalIds[5];
    Squad.AssignedVehicle = MakeVehicle(TEXT("Capacity_Value"), 3);
    TestTrue(TEXT("Valid vehicle identity marks the squad assigned"), Squad.HasAssignedVehicle());
    TestEqual(TEXT("Vehicle seats replace default capacity"), Squad.GetMaxMemberCount(), 3);
    TestTrue(TEXT("Trimming reports exactly the surplus non-captains"), HasExactly(Squad.TrimMembersToCapacity(), {OriginalIds[2], OriginalIds[3], OriginalIds[4]}));
    TestTrue(TEXT("Trim retains captain and earliest members in joining order"), Squad.MemberUnitIds == TArray<FGuid>({OriginalIds[0], OriginalIds[1], OriginalIds[5]}));
    TestEqual(TEXT("Trim retains the late-joining captain"), Squad.CaptainUnitId, OriginalIds[5]);
    TestTrue(TEXT("An already fitting squad needs no further eviction"), Squad.TrimMembersToCapacity().IsEmpty());
    Squad.AssignedVehicle.Attributes.PassengerCapacity = 0;
    TestEqual(TEXT("Zero-seat vehicle has no minimum fallback"), Squad.GetMaxMemberCount(), 0);
    TestTrue(TEXT("Zero seats remove every remaining member"), HasExactly(Squad.TrimMembersToCapacity(), {OriginalIds[0], OriginalIds[1], OriginalIds[5]}));
    TestTrue(TEXT("Zero seats leave no members or captain"), Squad.MemberUnitIds.IsEmpty() && !Squad.CaptainUnitId.IsValid());
    Squad.AssignedVehicle.Attributes.PassengerCapacity = -1;
    TestEqual(TEXT("Raw invalid capacity is defensively clamped by the getter"), Squad.GetMaxMemberCount(), 0);
    Squad.AssignedVehicle = FVehicleData();
    TestEqual(TEXT("Clearing vehicle restores four seats"), Squad.GetMaxMemberCount(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadVehicleAssignmentTest, "SilverChoir.Player.Squads.Capacity.AssignmentAndTransfers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSquadVehicleAssignmentTest::RunTest(const FString& Parameters)
{
    using namespace SquadVehicleTests;
    FFixture Fixture;
    const FName Tile(TEXT("Capacity_Assignment"));
    const TArray<FGuid> Ids = Fixture.AddUnits(Tile, 7);
    if (!TestEqual(TEXT("Seven canonical units created"), Ids.Num(), 7)) return false;
    const FGuid Target = Fixture.AddSquad(Tile, TEXT("Target"));
    const FGuid Donor = Fixture.AddSquad(Tile, TEXT("Donor"));
    auto* Squads = Fixture.Squads.Get();
    for (int32 Index = 0; Index < 4; ++Index)
        TestTrue(TEXT("Four members fit an unmotorized squad"), Squads->AddUnitToSquad(Ids[Index], Target, Fixture.Error));
    TestTrue(TEXT("Donor captain joins"), Squads->AddUnitToSquad(Ids[4], Donor, Fixture.Error));
    TestTrue(TEXT("Donor second member joins"), Squads->AddUnitToSquad(Ids[5], Donor, Fixture.Error));
    FSquadData DonorBefore;
    Squads->GetSquad(Donor, DonorBefore);
    TestFalse(TEXT("Transfer to a full squad is rejected"), Squads->AddUnitToSquad(Ids[4], Target, Fixture.Error));
    FSquadData Found;
    Squads->GetSquad(Donor, Found);
    TestTrue(TEXT("Failed full-squad transfer retains source roster and captain"), Found.MatchesSnapshot(DonorBefore));
    TestTrue(TEXT("Failed transfer retains source reverse membership"), Squads->GetUnitSquad(Ids[4], Found) && Found.SquadId == Donor);
    TestTrue(TEXT("Repeated join remains valid at capacity"), Squads->AddUnitToSquad(Ids[0], Target, Fixture.Error));
    FSquadData OriginalTarget;
    Squads->GetSquad(Target, OriginalTarget);
    FSquadData OverfullDraft = OriginalTarget;
    OverfullDraft.MemberUnitIds.Add(Ids[4]);
    FGuid FailedCommitId = FGuid::NewGuid();
    TestFalse(TEXT("Over-capacity draft commit rejects the whole transfer"), Squads->CommitSquadDraft(OverfullDraft, OriginalTarget, false, FailedCommitId, Fixture.Error));
    TestFalse(TEXT("Rejected capacity commit clears its output identity"), FailedCommitId.IsValid());
    Squads->GetSquad(Donor, Found);
    TestTrue(TEXT("Over-capacity draft does not strip its source squad"), Found.MatchesSnapshot(DonorBefore));
    TArray<FGuid> Removed;
    const FVehicleData SixSeat = Fixture.AddVehicle(Tile, 6);
    TestTrue(TEXT("Assign six-seat vehicle"), Squads->SetSquadVehicle(Target, SixSeat, Removed, Fixture.Error));
    TestTrue(TEXT("Expanding capacity evicts nobody"), Removed.IsEmpty());
    TestEqual(TEXT("Manager reports six seats"), Squads->GetSquadMaxMemberCount(Target), 6);
    TestTrue(TEXT("Formerly rejected member now transfers"), Squads->AddUnitToSquad(Ids[4], Target, Fixture.Error));
    TestTrue(TEXT("Sixth member transfers"), Squads->AddUnitToSquad(Ids[5], Target, Fixture.Error));
    TestTrue(TEXT("Late-joining member becomes captain"), Squads->SetSquadCaptain(Target, Ids[5], Fixture.Error));
    TArray<TSharedPtr<FUnitData>> Shared;
    TArray<FDelegateHandle> Handles;
    TMap<FGuid, int32> Notifications;
    for (int32 Index = 0; Index < 6; ++Index)
    {
        Shared.Add(Fixture.Units->GetUnitDataShared(Ids[Index]));
        const FGuid Id = Ids[Index];
        Handles.Add(Shared.Last()->OnDataChanged.AddLambda([&, Id](FGuid) { ++Notifications.FindOrAdd(Id); }));
    }
    const FVehicleData ThreeSeat = Fixture.AddVehicle(Tile, 3);
    TestTrue(TEXT("Smaller vehicle atomically trims the squad"), Squads->SetSquadVehicle(Target, ThreeSeat, Removed, Fixture.Error));
    TestTrue(TEXT("Vehicle shrink reports surplus IDs"), HasExactly(Removed, {Ids[2], Ids[3], Ids[4]}));
    Squads->GetSquad(Target, Found);
    TestTrue(TEXT("Vehicle shrink keeps earliest members and captain"), Found.MemberUnitIds == TArray<FGuid>({Ids[0], Ids[1], Ids[5]}));
    TestEqual(TEXT("Vehicle shrink preserves captain"), Found.CaptainUnitId, Ids[5]);
    for (int32 Index = 0; Index < 6; ++Index)
    {
        TestTrue(TEXT("Eviction never replaces or deletes a canonical unit"), Fixture.Units->GetUnitDataShared(Ids[Index]).Get() == Shared[Index].Get());
        TestEqual(TEXT("Each affected unit is notified once"), Notifications.FindRef(Ids[Index]), 1);
        FSquadData Membership;
        const bool bExpectedMember = Index == 0 || Index == 1 || Index == 5;
        TestTrue(TEXT("Shrink rebuilds each reverse membership"), Squads->GetUnitSquad(Ids[Index], Membership) == bExpectedMember);
        if (bExpectedMember) TestEqual(TEXT("Retained membership points to target"), Membership.SquadId, Target);
        Shared[Index]->OnDataChanged.Remove(Handles[Index]);
    }
    TestTrue(TEXT("Vehicle can expand again"), Squads->SetSquadVehicle(Target, SixSeat, Removed, Fixture.Error));
    for (int32 Index : {2, 3, 4}) TestTrue(TEXT("Evicted members remain available to rejoin"), Squads->AddUnitToSquad(Ids[Index], Target, Fixture.Error));
    TestTrue(TEXT("Clearing vehicle applies default capacity"), Squads->ClearSquadVehicle(Target, Removed, Fixture.Error));
    TestTrue(TEXT("Clear reports only the newest excess members"), HasExactly(Removed, {Ids[3], Ids[4]}));
    Squads->GetSquad(Target, Found);
    TestTrue(TEXT("Clear retains four members including captain"), Found.MemberUnitIds == TArray<FGuid>({Ids[0], Ids[1], Ids[5], Ids[2]}));
    TestFalse(TEXT("Clear removes assigned vehicle record"), Found.HasAssignedVehicle());
    TestTrue(TEXT("Zero-seat vehicle is a valid assignment"), Squads->SetSquadVehicle(Target, Fixture.AddVehicle(Tile, 0), Removed, Fixture.Error));
    TestEqual(TEXT("Zero-seat assignment reports all four evictions"), Removed.Num(), 4);
    Squads->GetSquad(Target, Found);
    TestTrue(TEXT("Zero-seat squad is empty and has no captain"), Found.MemberUnitIds.IsEmpty() && !Found.CaptainUnitId.IsValid());
    TestFalse(TEXT("Nobody may join a zero-seat vehicle"), Squads->AddUnitToSquad(Ids[6], Target, Fixture.Error));
    TestFalse(TEXT("Rejected zero-seat join keeps free unit unassigned"), Squads->GetUnitSquad(Ids[6], Found));
    TestEqual(TEXT("Unknown squad has no capacity"), Squads->GetSquadMaxMemberCount(FGuid::NewGuid()), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadVehicleValidationAndSaveTest, "SilverChoir.Player.Squads.Capacity.ValidationAndSaveGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSquadVehicleValidationAndSaveTest::RunTest(const FString& Parameters)
{
    using namespace SquadVehicleTests;
    FFixture Fixture;
    const FName Tile(TEXT("Capacity_Save"));
    const TArray<FGuid> Ids = Fixture.AddUnits(Tile, 6);
    if (!TestEqual(TEXT("Six canonical units created"), Ids.Num(), 6)) return false;
    auto* Squads = Fixture.Squads.Get();
    const FGuid Target = Fixture.AddSquad(Tile, TEXT("Saved Target"));
    const FGuid Other = Fixture.AddSquad(Tile, TEXT("Other"));
    FVehicleData Vehicle = MakeVehicle(Tile, 6);
    Vehicle.Profile.PreviewImage = NewObject<UTexture2D>();
    TestTrue(TEXT("Register vehicle with its preview in the canonical store"), Fixture.Units->LoadVehicleData({Vehicle}, Fixture.Error));
    const TWeakObjectPtr<UTexture2D> Preview = Vehicle.Profile.PreviewImage;
    TArray<FGuid> Removed;
    TestTrue(TEXT("Initial vehicle assigned"), Squads->SetSquadVehicle(Target, Vehicle, Removed, Fixture.Error));
    for (FGuid Id : Ids) TestTrue(TEXT("Saved vehicle accommodates six members"), Squads->AddUnitToSquad(Id, Target, Fixture.Error));
    TestTrue(TEXT("Last saved member is captain"), Squads->SetSquadCaptain(Target, Ids[5], Fixture.Error));
    FSquadData Original;
    Squads->GetSquad(Target, Original);
    auto Reject = [&](const TCHAR* Label, const FGuid SquadId, const FVehicleData& Invalid)
    {
        Removed = {FGuid::NewGuid()};
        TestFalse(Label, Squads->SetSquadVehicle(SquadId, Invalid, Removed, Fixture.Error));
        TestTrue(TEXT("Rejected assignment clears eviction output"), Removed.IsEmpty());
        FSquadData Current;
        Squads->GetSquad(Target, Current);
        TestTrue(TEXT("Rejected assignment preserves target data and roster"), Current.MatchesSnapshot(Original));
    };
    Reject(TEXT("A vehicle cannot occupy two squads"), Other, Vehicle);
    FVehicleData Invalid = Vehicle;
    Invalid.VehicleId.Invalidate();
    Reject(TEXT("Assigned vehicles require a valid identity"), Target, Invalid);
    Invalid = Fixture.AddVehicle(TEXT("Elsewhere"), 6);
    Reject(TEXT("Vehicle and squad must occupy the same tile"), Target, Invalid);
    Invalid = Fixture.AddVehicle(NAME_None, 6);
    Reject(TEXT("Vehicle tile cannot be unknown"), Target, Invalid);
    Invalid = Vehicle;
    Invalid.Attributes.PassengerCapacity = -1;
    Invalid.RuntimeData.TileId = TEXT("ForgedLocation");
    TestTrue(TEXT("Assignment resolves canonical data instead of accepting forged snapshot fields"),
        Squads->SetSquadVehicle(Target, Invalid, Removed, Fixture.Error));
    FSquadData CanonicalAssignment;
    Squads->GetSquad(Target, CanonicalAssignment);
    TestTrue(TEXT("Forged location/capacity leave the canonical assignment unchanged"), CanonicalAssignment.MatchesSnapshot(Original));
    TestFalse(TEXT("The player store rejects negative seat capacity"), Fixture.Units->LoadVehicleData({Invalid}, Fixture.Error));
    Reject(TEXT("Vehicle cannot be assigned to an unknown squad"), FGuid::NewGuid(), Vehicle);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Stored squad retains its nested vehicle preview resource"), Preview.IsValid());

    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Writer, false);
        Archive.ArIsSaveGame = true;
        FSquadData::StaticStruct()->SerializeItem(Archive, &Original, nullptr);
        TestFalse(TEXT("Squad with vehicle serializes"), Archive.IsError());
    }
    FSquadData Saved;
    {
        FMemoryReader Reader(Bytes, true);
        FObjectAndNameAsStringProxyArchive Archive(Reader, false);
        Archive.ArIsSaveGame = true;
        FSquadData::StaticStruct()->SerializeItem(Archive, &Saved, nullptr);
        TestFalse(TEXT("Squad with vehicle deserializes"), Archive.IsError());
    }
    TestTrue(TEXT("SaveGame round trip preserves full assigned vehicle and roster"), Saved.MatchesSnapshot(Original));
    const auto CanonicalCaptain = Fixture.Units->GetUnitDataShared(Ids[5]);
    FSquadData DuplicateVehicle;
    Squads->GetSquad(Other, DuplicateVehicle);
    DuplicateVehicle.AssignedVehicle = Vehicle;
    TestFalse(TEXT("Save cannot assign one vehicle to two squads"), Squads->LoadSquadData({Saved, DuplicateVehicle}, Fixture.Error));
    FSquadData WrongVehicleTile = Saved;
    WrongVehicleTile.AssignedVehicle.RuntimeData.TileId = TEXT("Capacity_WrongVehicleTile");
    TestTrue(TEXT("Loading resolves vehicle location by ID instead of trusting an old snapshot"), Squads->LoadSquadData({WrongVehicleTile}, Fixture.Error));
    FSquadData Overloaded = Saved;
    Overloaded.AssignedVehicle = FVehicleData();
    FSquadData InvalidTail = Overloaded;
    InvalidTail.MemberUnitIds[4] = FGuid::NewGuid();
    TestFalse(TEXT("Unknown surplus member is checked before load trimming"), Squads->LoadSquadData({InvalidTail}, Fixture.Error));
    InvalidTail = Overloaded;
    InvalidTail.MemberUnitIds[4] = Ids[0];
    TestFalse(TEXT("Duplicate surplus member is checked before load trimming"), Squads->LoadSquadData({InvalidTail}, Fixture.Error));
    DuplicateVehicle.AssignedVehicle = FVehicleData();
    DuplicateVehicle.MemberUnitIds = {Ids[4]};
    DuplicateVehicle.CaptainUnitId = Ids[4];
    TestFalse(TEXT("Cross-squad duplicate surplus member is checked before trimming"), Squads->LoadSquadData({Overloaded, DuplicateVehicle}, Fixture.Error));
    FSquadData Current;
    Squads->GetSquad(Target, Current);
    TestTrue(TEXT("Rejected loads preserve saved vehicle and roster"), Current.MatchesSnapshot(Original));
    TestTrue(TEXT("Over-capacity legacy save is trimmed while loading"), Squads->LoadSquadData({Overloaded}, Fixture.Error));
    Squads->GetSquad(Target, Current);
    TestTrue(TEXT("Legacy save keeps captain and earliest three members"), Current.MemberUnitIds == TArray<FGuid>({Ids[0], Ids[1], Ids[2], Ids[5]}));
    TestTrue(TEXT("Legacy load preserves canonical captain allocation"), Fixture.Units->GetUnitDataShared(Ids[5]).Get() == CanonicalCaptain.Get());
    TestFalse(TEXT("Trimmed saved member has no reverse membership"), Squads->GetUnitSquad(Ids[4], Current));
    TestTrue(TEXT("Restore the six-seat vehicle save"), Squads->LoadSquadData({Saved}, Fixture.Error));
    Squads->GetSquad(Target, Current);
    TestTrue(TEXT("Six-seat save retains all six members and vehicle"), Current.MatchesSnapshot(Saved));
    TestTrue(TEXT("Moving a vehicle squad updates its tile"), Squads->SetSquadTileId(Target, TEXT("Capacity_Moved"), Fixture.Error));
    Squads->GetSquad(Target, Current);
    TestEqual(TEXT("Assigned vehicle travels with its squad"), Current.AssignedVehicle.RuntimeData.TileId, Current.TileId);
    for (FGuid Id : Ids) TestEqual(TEXT("Members and assigned vehicle remain colocated"), Fixture.Units->GetUnitDataShared(Id)->RuntimeData.TileId, Current.TileId);
    return true;
}
#endif
