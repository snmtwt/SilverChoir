#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/StrongObjectPtr.h"

namespace SquadDraftTests
{
    // Same canonical unit/squad manager fixture used by PlayerSquadSubsystemTests.
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

        FGuid AddSquad(const TCHAR* Name, const FName TileId, const TArray<FGuid>& Members)
        {
            FGuid Id;
            if (!Squads->CreateSquad(FText::FromString(Name), nullptr, TileId, Id, Error)) return FGuid();
            for (const FGuid UnitId : Members)
                if (!Squads->AddUnitToSquad(UnitId, Id, Error)) return FGuid();
            return Id;
        }
    };

    bool SameSquad(const FSquadData& A, const FSquadData& B)
    {
        return A.SquadId == B.SquadId && A.SquadName.EqualTo(B.SquadName)
            && A.SquadIcon == B.SquadIcon && A.IconSource == B.IconSource
            && A.TileId == B.TileId && A.MemberUnitIds == B.MemberUnitIds
            && A.CaptainUnitId == B.CaptainUnitId
            && FVehicleData::StaticStruct()->CompareScriptStruct(&A.AssignedVehicle, &B.AssignedVehicle, 0);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadDraftCreateTest,
    "SilverChoir.Player.Squads.Drafts.CreateAndReject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadDraftCreateTest::RunTest(const FString& Parameters)
{
    using namespace SquadDraftTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    const FName Tile(TEXT("Draft_Base"));
    const FGuid First = Fixture.AddUnit(Tile);
    const FGuid Second = Fixture.AddUnit(Tile);
    const FGuid Distant = Fixture.AddUnit(TEXT("Draft_Distant"));
    const FGuid Donor = Fixture.AddSquad(TEXT("Donor"), Tile, {First, Second});
    if (!TestTrue(TEXT("Fixture is ready"), Donor.IsValid() && Distant.IsValid())) return false;
    FSquadData DonorBefore;
    Squads->GetSquad(Donor, DonorBefore);
    const auto Canonical = Fixture.Units->GetUnitDataShared(First);
    int32 Notifications = 0;
    const FDelegateHandle Handle = Canonical->OnDataChanged.AddLambda([&](FGuid) { ++Notifications; });

    FSquadData Draft;
    Draft.SquadName = FText::FromString(TEXT("Saved Squad"));
    Draft.TileId = Tile;
    Draft.MemberUnitIds = {First};
    Draft.CaptainUnitId = First;
    Draft.IconSource = ESquadIconSource::CaptainPortrait;
    TestEqual(TEXT("Building a draft creates no squad"), Squads->GetSquadIds().Num(), 1);

    auto Reject = [&](const TCHAR* Description, const FSquadData& Invalid)
    {
        FGuid Result = FGuid::NewGuid();
        TestFalse(Description, Squads->CommitSquadDraft(Invalid, FSquadData(), true, Result, Fixture.Error));
        TestFalse(TEXT("Failure clears the output identity"), Result.IsValid());
        TestFalse(TEXT("Failure explains why it was rejected"), Fixture.Error.IsEmpty());
        TestEqual(TEXT("Rejected creation never leaves a ghost squad"), Squads->GetSquadIds().Num(), 1);
        FSquadData Found;
        TestTrue(TEXT("Rejected creation retains the donor"), Squads->GetSquad(Donor, Found));
        TestTrue(TEXT("Rejected creation preserves every donor field"), SameSquad(Found, DonorBefore));
        TestTrue(TEXT("Rejected creation preserves reverse membership"), Squads->GetUnitSquad(First, Found));
        TestEqual(TEXT("Rejected creation retains the original membership"), Found.SquadId, Donor);
        TestEqual(TEXT("Rejected creation emits no unit notifications"), Notifications, 0);
    };
    FSquadData Invalid = Draft;
    Invalid.SquadId = Donor;
    Reject(TEXT("New drafts cannot supply an existing identity"), Invalid);
    Invalid = Draft;
    Invalid.SquadName = FText::FromString(TEXT(" \t "));
    Reject(TEXT("Blank names are rejected"), Invalid);
    Invalid = Draft;
    Invalid.TileId = NAME_None;
    Reject(TEXT("Missing tiles are rejected"), Invalid);
    Invalid = Draft;
    Invalid.IconSource = static_cast<ESquadIconSource>(255);
    Reject(TEXT("Unknown icon modes are rejected"), Invalid);
    Invalid = Draft;
    Invalid.MemberUnitIds.Add(First);
    Reject(TEXT("Duplicate members are rejected"), Invalid);
    Invalid = Draft;
    Invalid.MemberUnitIds.Add(FGuid());
    Reject(TEXT("Invalid member identities are rejected"), Invalid);
    Invalid = Draft;
    Invalid.MemberUnitIds.Add(FGuid::NewGuid());
    Reject(TEXT("Unknown members reject the entire transfer"), Invalid);
    Invalid = Draft;
    Invalid.MemberUnitIds.Add(Distant);
    Reject(TEXT("A distant member rejects the entire transfer"), Invalid);
    Invalid = Draft;
    Invalid.CaptainUnitId = Second;
    Reject(TEXT("A captain outside the draft is rejected"), Invalid);
    Invalid = Draft;
    Invalid.CaptainUnitId.Invalidate();
    Reject(TEXT("Nonempty drafts require a captain"), Invalid);
    Invalid = Draft;
    Invalid.MemberUnitIds.Reset();
    Reject(TEXT("An empty draft cannot retain a captain"), Invalid);

    FGuid SavedId;
    TestTrue(TEXT("Valid creation commits once"), Squads->CommitSquadDraft(Draft, FSquadData(), true, SavedId, Fixture.Error));
    TestTrue(TEXT("Successful creation assigns a fresh identity"), SavedId.IsValid() && SavedId != Donor);
    TestFalse(TEXT("Saving never writes an identity into the input draft"), Draft.SquadId.IsValid());
    TestEqual(TEXT("Only the successful commit adds a squad"), Squads->GetSquadIds().Num(), 2);
    FSquadData Found;
    Squads->GetSquad(SavedId, Found);
    FSquadData Expected = Draft;
    Expected.SquadId = SavedId;
    TestTrue(TEXT("Creation commits every draft field"), SameSquad(Found, Expected));
    Squads->GetSquad(Donor, Found);
    TestTrue(TEXT("Donor retains only the unselected member"), Found.MemberUnitIds == TArray<FGuid>({Second}));
    TestEqual(TEXT("Transferring the donor captain promotes its remaining member"), Found.CaptainUnitId, Second);
    Squads->GetUnitSquad(First, Found);
    TestEqual(TEXT("Creation commits the reverse membership"), Found.SquadId, SavedId);
    TestEqual(TEXT("A successful commit notifies the canonical unit once"), Notifications, 1);
    TestTrue(TEXT("Creation retains the original shared unit allocation"),
        Squads->GetSquadUnitsShared(SavedId)[0].Get() == Canonical.Get());
    Canonical->OnDataChanged.Remove(Handle);

    FSquadData LastMemberDraft = Draft;
    LastMemberDraft.MemberUnitIds = {Second};
    LastMemberDraft.CaptainUnitId = Second;
    TestTrue(TEXT("The last donor member can transfer in another saved draft"),
        Squads->CommitSquadDraft(LastMemberDraft, FSquadData(), true, SavedId, Fixture.Error));
    Squads->GetSquad(Donor, Found);
    TestTrue(TEXT("Empty donor remains valid and clears its captain"), Found.MemberUnitIds.IsEmpty() && !Found.CaptainUnitId.IsValid());

    FSquadData EmptyDraft;
    EmptyDraft.SquadName = FText::FromString(TEXT("Empty Saved Squad"));
    EmptyDraft.TileId = Tile;
    TestTrue(TEXT("Empty squads may be saved without a captain"),
        Squads->CommitSquadDraft(EmptyDraft, FSquadData(), true, SavedId, Fixture.Error));
    Squads->GetSquad(SavedId, Found);
    TestTrue(TEXT("Empty committed squad has no members or captain"), Found.MemberUnitIds.IsEmpty() && !Found.CaptainUnitId.IsValid());
    SavedId = FGuid::NewGuid();
    TestFalse(TEXT("Library reports a missing world context"),
        UPlayerSquadLibrary::CommitSquadDraft(nullptr, EmptyDraft, FSquadData(), true, SavedId, Fixture.Error));
    TestFalse(TEXT("Missing context also clears the output identity"), SavedId.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadDraftAtomicEditTest,
    "SilverChoir.Player.Squads.Drafts.AtomicEdit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadDraftAtomicEditTest::RunTest(const FString& Parameters)
{
    using namespace SquadDraftTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    const FName Tile(TEXT("Draft_Atomic"));
    const FGuid Removed = Fixture.AddUnit(Tile);
    const FGuid Retained = Fixture.AddUnit(Tile);
    const FGuid Transferred = Fixture.AddUnit(Tile);
    const FGuid DonorRemainder = Fixture.AddUnit(Tile);
    const FGuid Unassigned = Fixture.AddUnit(Tile);
    const FGuid Unrelated = Fixture.AddUnit(Tile);
    const FGuid Target = Fixture.AddSquad(TEXT("Target"), Tile, {Removed, Retained});
    const FGuid Donor = Fixture.AddSquad(TEXT("Donor"), Tile, {Transferred, DonorRemainder});
    const FGuid Other = Fixture.AddSquad(TEXT("Other"), Tile, {Unrelated});
    if (!TestTrue(TEXT("Fixture is ready"), Target.IsValid() && Donor.IsValid() && Other.IsValid())) return false;

    FSquadData Original;
    Squads->GetSquad(Target, Original);
    FSquadData OtherBefore;
    Squads->GetSquad(Other, OtherBefore);
    TStrongObjectPtr<UTexture2D> Icon(NewObject<UTexture2D>());
    FSquadData Draft = Original;
    Draft.SquadName = FText::FromString(TEXT("Saved Target"));
    Draft.SquadIcon = Icon.Get();
    Draft.IconSource = ESquadIconSource::CaptainPortrait;
    Draft.MemberUnitIds = {Transferred, Retained, Unassigned};
    Draft.CaptainUnitId = Transferred;
    FSquadData Found;
    Squads->GetSquad(Target, Found);
    TestTrue(TEXT("Editing a draft preserves the original squad"), SameSquad(Found, Original));
    FSquadData DonorBefore;
    Squads->GetSquad(Donor, DonorBefore);
    FSquadData Invalid = Draft;
    Invalid.MemberUnitIds.Add(FGuid::NewGuid());
    FGuid RejectedId;
    TestFalse(TEXT("A late member validation failure rejects the entire existing squad edit"),
        Squads->CommitSquadDraft(Invalid, Original, false, RejectedId, Fixture.Error));
    Squads->GetSquad(Target, Found);
    TestTrue(TEXT("Failed edit retains target metadata and deselected members"), SameSquad(Found, Original));
    Squads->GetSquad(Donor, Found);
    TestTrue(TEXT("Failed edit retains the donor and its captain"), SameSquad(Found, DonorBefore));
    TestFalse(TEXT("Failed edit leaves selected free members unassigned"), Squads->GetUnitSquad(Unassigned, Found));

    const TArray<FGuid> AffectedIds = {Removed, Retained, Transferred, DonorRemainder, Unassigned};
    TArray<TSharedPtr<FUnitData>> CanonicalUnits;
    TArray<FDelegateHandle> Handles;
    TMap<FGuid, int32> Notifications;
    FGuid Result;
    for (const FGuid UnitId : AffectedIds)
    {
        const auto Canonical = Fixture.Units->GetUnitDataShared(UnitId);
        CanonicalUnits.Add(Canonical);
        Handles.Add(Canonical->OnDataChanged.AddLambda([&, UnitId](FGuid)
        {
            ++Notifications.FindOrAdd(UnitId);
            TestEqual(TEXT("Output identity is assigned before callbacks"), Result, Target);
            FSquadData State;
            TestTrue(TEXT("Target exists during every callback"), Squads->GetSquad(Target, State));
            TestTrue(TEXT("All target fields commit before the first callback"), SameSquad(State, Draft));
            Squads->GetSquad(Donor, State);
            TestTrue(TEXT("Donor roster commits before callbacks"), State.MemberUnitIds == TArray<FGuid>({DonorRemainder}));
            TestEqual(TEXT("Donor captain is repaired before callbacks"), State.CaptainUnitId, DonorRemainder);
            TestFalse(TEXT("Deselected target member is free before callbacks"), Squads->GetUnitSquad(Removed, State));
            for (const FGuid MemberId : Draft.MemberUnitIds)
            {
                TestTrue(TEXT("Every selected member has a reverse index during callbacks"), Squads->GetUnitSquad(MemberId, State));
                TestEqual(TEXT("Selected members all point to the committed target"), State.SquadId, Target);
            }
            Squads->GetSquad(Other, State);
            TestTrue(TEXT("Unrelated squad is untouched during callbacks"), SameSquad(State, OtherBefore));
            FGuid NestedId = FGuid::NewGuid();
            FText NestedError;
            TestFalse(TEXT("Callbacks cannot nest a draft commit"),
                Squads->CommitSquadDraft(Draft, Draft, false, NestedId, NestedError));
            TestFalse(TEXT("Rejected reentry returns no identity"), NestedId.IsValid());
            TestFalse(TEXT("Callbacks cannot mutate membership through older APIs"),
                Squads->RemoveUnitFromSquad(Retained, NestedError));
        }));
    }
    int32 UnrelatedNotifications = 0;
    const auto OtherUnit = Fixture.Units->GetUnitDataShared(Unrelated);
    const FDelegateHandle OtherHandle = OtherUnit->OnDataChanged.AddLambda([&](FGuid) { ++UnrelatedNotifications; });
    TestTrue(TEXT("All draft fields save as one transaction"), Squads->CommitSquadDraft(Draft, Original, false, Result, Fixture.Error));
    TestEqual(TEXT("Editing preserves the squad identity"), Result, Target);
    TestEqual(TEXT("Editing creates no additional squad"), Squads->GetSquadIds().Num(), 3);
    for (int32 Index = 0; Index < AffectedIds.Num(); ++Index)
    {
        TestEqual(TEXT("Each affected canonical unit is notified once"), Notifications.FindRef(AffectedIds[Index]), 1);
        TestTrue(TEXT("All affected units retain their original shared allocation"),
            Fixture.Units->GetUnitDataShared(AffectedIds[Index]).Get() == CanonicalUnits[Index].Get());
        TestEqual(TEXT("Saving never moves unit strategic positions"), CanonicalUnits[Index]->RuntimeData.TileId, Tile);
        CanonicalUnits[Index]->OnDataChanged.Remove(Handles[Index]);
    }
    TestEqual(TEXT("Unrelated units receive no notifications"), UnrelatedNotifications, 0);
    OtherUnit->OnDataChanged.Remove(OtherHandle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadDraftStaleSnapshotTest,
    "SilverChoir.Player.Squads.Drafts.StaleSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadDraftStaleSnapshotTest::RunTest(const FString& Parameters)
{
    using namespace SquadDraftTests;
    const FName Tile(TEXT("Draft_Stale"));
    const TCHAR* Cases[] = {TEXT("name"), TEXT("preset icon"), TEXT("icon source"), TEXT("tile"), TEXT("members"), TEXT("captain"),
        TEXT("vehicle identity"), TEXT("vehicle capacity"), TEXT("vehicle fuel"), TEXT("vehicle movement")};
    for (int32 Case = 0; Case < UE_ARRAY_COUNT(Cases); ++Case)
    {
        FFixture Fixture;
        auto* Squads = Fixture.Squads.Get();
        const FGuid First = Fixture.AddUnit(Tile);
        const FGuid Second = Fixture.AddUnit(Tile);
        const FGuid Target = Fixture.AddSquad(TEXT("Original"), Tile, {First, Second});
        if (!TestTrue(TEXT("Fixture is ready"), Target.IsValid())) return false;
        FVehicleData Vehicle;
        TArray<FGuid> Removed;
        if (Case >= 6)
        {
            Vehicle.VehicleId = FGuid::NewGuid();
            Vehicle.Attributes.PassengerCapacity = 6;
            Vehicle.RuntimeData.TileId = Tile;
            if (!TestTrue(TEXT("Register canonical vehicle before assignment"), Fixture.Units->LoadVehicleData({Vehicle}, Fixture.Error))) return false;
            if (!TestTrue(TEXT("Assign vehicle before taking a draft snapshot"), Squads->SetSquadVehicle(Target, Vehicle, Removed, Fixture.Error))) return false;
        }
        FSquadData Original;
        Squads->GetSquad(Target, Original);
        FSquadData Draft = Original;
        Draft.SquadName = FText::FromString(TEXT("Unsaved edit"));
        TStrongObjectPtr<UTexture2D> Icon(NewObject<UTexture2D>());
        switch (Case)
        {
        case 0: Squads->UpdateSquadInfo(Target, FText::FromString(TEXT("External rename")), nullptr, Fixture.Error); break;
        case 1: Squads->UpdateSquadInfo(Target, Original.SquadName, Icon.Get(), Fixture.Error); break;
        case 2: Squads->SetSquadIconSource(Target, ESquadIconSource::CaptainPortrait, Fixture.Error); break;
        case 3: Squads->SetSquadTileId(Target, TEXT("Draft_Moved"), Fixture.Error); break;
        case 4: Squads->RemoveUnitFromSquad(Second, Fixture.Error); break;
        case 5: Squads->SetSquadCaptain(Target, Second, Fixture.Error); break;
        case 6: Vehicle.VehicleId = FGuid::NewGuid(); break;
        case 7: Vehicle.Attributes.PassengerCapacity = 5; break;
        case 8: Vehicle.RuntimeData.CurrentFuel = 17.f; break;
        case 9: Vehicle.StrategicMovementData.SpeedTilesPerHour = 2.f; break;
        }
        if (Case >= 6)
        {
            TestTrue(TEXT("External vehicle changes update canonical storage"), Fixture.Units->LoadVehicleData({Vehicle}, Fixture.Error));
            TestTrue(TEXT("External vehicle change succeeds before stale-save rejection"), Squads->SetSquadVehicle(Target, Vehicle, Removed, Fixture.Error));
        }
        FSquadData Changed;
        Squads->GetSquad(Target, Changed);
        const auto Canonical = Fixture.Units->GetUnitDataShared(First);
        int32 Notifications = 0;
        const FDelegateHandle Handle = Canonical->OnDataChanged.AddLambda([&](FGuid) { ++Notifications; });
        FGuid Result = FGuid::NewGuid();
        TestFalse(*FString::Printf(TEXT("Stale %s rejects an edit"), Cases[Case]),
            Squads->CommitSquadDraft(Draft, Original, false, Result, Fixture.Error));
        TestFalse(TEXT("Stale rejection returns no identity"), Result.IsValid());
        FSquadData Found;
        Squads->GetSquad(Target, Found);
        TestTrue(TEXT("Stale rejection preserves the live squad exactly"), SameSquad(Found, Changed));
        TestEqual(TEXT("Stale rejection has no partial notifications"), Notifications, 0);
        Canonical->OnDataChanged.Remove(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadDraftReadyUnitsTest,
    "SilverChoir.Player.Squads.Drafts.ReadyUnitsAndIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadDraftReadyUnitsTest::RunTest(const FString& Parameters)
{
    using namespace SquadDraftTests;
    FFixture Fixture;
    auto* Squads = Fixture.Squads.Get();
    const FName Tile(TEXT("Draft_Ready"));
    const FGuid Removed = Fixture.AddUnit(Tile);
    const FGuid Requested = Fixture.AddUnit(Tile);
    const FGuid DonorRemainder = Fixture.AddUnit(Tile);
    const FGuid Target = Fixture.AddSquad(TEXT("Target"), Tile, {Removed});
    const FGuid Donor = Fixture.AddSquad(TEXT("Donor"), Tile, {Requested, DonorRemainder});
    if (!TestTrue(TEXT("Fixture is ready"), Target.IsValid() && Donor.IsValid())) return false;
    FSquadData Original;
    FSquadData DonorBefore;
    Squads->GetSquad(Target, Original);
    Squads->GetSquad(Donor, DonorBefore);
    FSquadData Draft = Original;
    Draft.SquadName = FText::FromString(TEXT("Saved"));
    Draft.MemberUnitIds = {Requested};
    Draft.CaptainUnitId = Requested;

    for (const FGuid BusyId : {Removed, DonorRemainder})
    {
        const auto Busy = Fixture.Units->GetUnitDataShared(BusyId);
        bool bChecked = false;
        const FDelegateHandle Handle = Busy->OnDataChanged.AddLambda([&](FGuid)
        {
            bChecked = true;
            FGuid Result;
            TestFalse(TEXT("Notifying canonical members reject the whole draft even when absent from its new roster"),
                Squads->CommitSquadDraft(Draft, Original, false, Result, Fixture.Error));
        });
        Busy->NotifyDataChanged();
        Busy->OnDataChanged.Remove(Handle);
        TestTrue(TEXT("Canonical readiness guard is exercised"), bChecked);
        FSquadData Found;
        Squads->GetSquad(Target, Found);
        TestTrue(TEXT("Readiness rejection preserves the original target"), SameSquad(Found, Original));
        Squads->GetSquad(Donor, Found);
        TestTrue(TEXT("Readiness rejection preserves the original donor"), SameSquad(Found, DonorBefore));
    }
    FGuid Result;
    FSquadData Invalid = Draft;
    Invalid.SquadId = Donor;
    TestFalse(TEXT("Editing requires matching snapshot identities"),
        Squads->CommitSquadDraft(Invalid, Original, false, Result, Fixture.Error));
    Invalid = Draft;
    Invalid.TileId = TEXT("Draft_ChangedTile");
    TestFalse(TEXT("Editing a draft cannot relocate a squad"),
        Squads->CommitSquadDraft(Invalid, Original, false, Result, Fixture.Error));
    TestTrue(TEXT("Draft saves once canonical notifications finish"),
        Squads->CommitSquadDraft(Draft, Original, false, Result, Fixture.Error));
    TestTrue(TEXT("The saved squad can be removed"), Squads->RemoveSquad(Target, Fixture.Error));
    TestFalse(TEXT("A removed squad cannot be recreated by saving an old draft"),
        Squads->CommitSquadDraft(Draft, Draft, false, Result, Fixture.Error));
    TestEqual(TEXT("Deleted target remains absent after failed edit"), Squads->GetSquadIds().Num(), 1);
    return true;
}
#endif
