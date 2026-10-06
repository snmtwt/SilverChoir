#include "Tests/GTS_AutomationTestTypes.h"

#include "GTS_TimeManager.h"

void UGTS_AutomationTimeTask::OnTimeReached_Implementation(FDateTime ScheduledTime, FDateTime CurrentTime)
{
    ++CallCount;
    LastScheduledTime = ScheduledTime;
    LastCurrentTime = CurrentTime;
    if (OnInvoked) OnInvoked(this, ScheduledTime, CurrentTime);
}

void UGTS_AutomationTimeObserver::HandleTime(FDateTime Time)
{
    TimeEvents.Add(Time);
    if (OnTimeEvent) OnTimeEvent(Time);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "GTS_TimeLibrary.h"
#include "GTS_TimeSubsystem.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace GTSTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FDateTime Epoch(2049, 1, 1, 0, 0, 0);

    struct FFixture
    {
        TStrongObjectPtr<UGTS_TimeManager> Manager{NewObject<UGTS_TimeManager>()};
        FFixture(FDateTime InitialTime = Epoch, double Scale = 1., bool bPaused = false, int32 Budget = 128)
        {
            Manager->Initialize(nullptr, InitialTime, Scale, bPaused, Budget);
        }
        ~FFixture() { Manager->Shutdown(); }
        UGTS_AutomationTimeTask* Task() const
        {
            return Cast<UGTS_AutomationTimeTask>(Manager->CreateTimeTask(UGTS_AutomationTimeTask::StaticClass()));
        }
    };

    FDateTime After(double Seconds) { return Epoch + FTimespan::FromSeconds(Seconds); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSCalendarTest, "GameTimeSystem.Clock.CalendarAndFractions", GTSTests::Flags)
bool FGTSCalendarTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F(FDateTime(2048, 2, 28, 23, 59, 59));
    auto* M = F.Manager.Get();
    TestTrue(TEXT("Clock initialized without requiring an editor world"), M->IsInitialized());
    M->AdvanceTime(1.);
    TestEqual(TEXT("Leap year enters February 29"), M->GetCurrentTime(), FDateTime(2048, 2, 29));
    M->AdvanceTime(86400.);
    TestEqual(TEXT("Leap day rolls into March"), M->GetCurrentTime(), FDateTime(2048, 3, 1));
    TestTrue(TEXT("Explicitly set a non-leap-year boundary"), M->SetCurrentTime(FDateTime(2049, 2, 28, 23, 59, 59)));
    M->AdvanceTime(1.);
    TestEqual(TEXT("Non-leap year skips February 29"), M->GetCurrentTime(), FDateTime(2049, 3, 1));
    M->SetCurrentTime(FDateTime(2049, 12, 31, 23, 59, 59));
    M->AdvanceTime(1.);
    TestEqual(TEXT("Calendar crosses the year boundary"), M->GetCurrentTime(), FDateTime(2050, 1, 1));

    M->SetCurrentTime(Epoch);
    TestTrue(TEXT("Fractional scale accepted"), M->SetTimeScale(.25));
    for (int32 Index = 0; Index < 40; ++Index) M->AdvanceTime(.1);
    TestTrue(TEXT("Fractional frame deltas preserve a full scaled second"),
        FMath::Abs((M->GetCurrentTime() - After(1.)).GetTicks()) <= 1);
    M->SetCurrentTime(Epoch);
    M->SetTimeScale(1.);
    for (int32 Index = 0; Index < 4; ++Index) M->AdvanceTime(0.000000025);
    TestEqual(TEXT("Sub-tick fractions accumulate instead of being discarded each frame"),
        (M->GetCurrentTime() - Epoch).GetTicks(), int64(1));
    M->SetCurrentTime(Epoch);
    M->AdvanceTime(0.000000025);
    M->SetCurrentTime(Epoch);
    M->AdvanceTime(0.000000075);
    TestEqual(TEXT("Explicit time assignment resets the previous fractional remainder"),
        (M->GetCurrentTime() - Epoch).GetTicks(), int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSClockValidationTest, "GameTimeSystem.Clock.PauseScaleAndValidation", GTSTests::Flags)
bool FGTSClockValidationTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    auto* M = F.Manager.Get();
    TStrongObjectPtr<UGTS_AutomationTimeObserver> O(NewObject<UGTS_AutomationTimeObserver>());
    M->OnTimeChanged.AddDynamic(O.Get(), &UGTS_AutomationTimeObserver::HandleTime);
    M->OnTimeScaleChanged.AddDynamic(O.Get(), &UGTS_AutomationTimeObserver::HandleScale);
    M->OnTimePausedChanged.AddDynamic(O.Get(), &UGTS_AutomationTimeObserver::HandlePaused);
    M->AdvanceTime(.2);
    TestEqual(TEXT("Subsecond automatic progress emits no redundant clock event"), O->TimeEvents.Num(), 0);
    M->AdvanceTime(.8);
    TestEqual(TEXT("Crossing a second emits one time notification"), O->TimeEvents.Num(), 1);
    TestEqual(TEXT("Time notification includes the actual new date"), O->TimeEvents.Last(), After(1.));
    TestTrue(TEXT("Set time notifies immediately"), M->SetCurrentTime(After(2.25)));
    TestEqual(TEXT("Explicit subsecond setting notifies"), O->TimeEvents.Last(), After(2.25));
    TestTrue(TEXT("Set valid scale"), M->SetTimeScale(60.));
    M->SetTimeScale(60.);
    TestEqual(TEXT("Unchanged scale does not repeat notifications"), O->ScaleEvents.Num(), 1);
    M->SetTimePaused(true);
    M->SetTimePaused(true);
    TestEqual(TEXT("Unchanged pause does not repeat notifications"), O->PauseEvents.Num(), 1);
    const FDateTime PausedTime = M->GetCurrentTime();
    M->AdvanceTime(300.);
    TestEqual(TEXT("Explicit pause freezes calendar"), M->GetCurrentTime(), PausedTime);
    auto* Due = F.Task();
    if (!TestNotNull(TEXT("Create task while clock paused"), Due)) return false;
    const FGuid DueId = M->RegisterTimeTask(Due, Epoch);
    M->AdvanceTime(0.);
    TestEqual(TEXT("Pause also stops already-due callbacks"), Due->CallCount, 0);
    TestTrue(TEXT("Paused due task stays registered"), M->IsTimeTaskRegistered(DueId));
    M->SetTimeScale(0.);
    M->SetTimePaused(false);
    M->AdvanceTime(30.);
    TestEqual(TEXT("Zero scale freezes the date"), M->GetCurrentTime(), PausedTime);
    TestEqual(TEXT("Zero scale permits overdue task dispatch when not paused"), Due->CallCount, 1);

    const double InvalidValues[] = {-1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
    for (double Invalid : InvalidValues)
    {
        TestFalse(TEXT("Negative and nonfinite scale values are rejected"), M->SetTimeScale(Invalid));
        TestEqual(TEXT("Rejected scale retains previous value"), M->GetTimeScale(), 0.);
        M->AdvanceTime(Invalid);
        TestEqual(TEXT("Invalid real delta does not alter date"), M->GetCurrentTime(), PausedTime);
    }
    const FDateTime Unsupported[] = {FDateTime(int64(-1)), FDateTime(FDateTime::MaxValue().GetTicks() + 1)};
    for (FDateTime Invalid : Unsupported)
    {
        TestFalse(TEXT("Out-of-range calendar values are rejected"), M->SetCurrentTime(Invalid));
        TestEqual(TEXT("Rejected time preserves valid current time"), M->GetCurrentTime(), PausedTime);
    }
    M->Shutdown();
    TestFalse(TEXT("Shutdown marks clock uninitialized"), M->IsInitialized());
    TestFalse(TEXT("Inactive manager rejects time changes"), M->SetCurrentTime(Epoch));
    TestFalse(TEXT("Inactive manager rejects scale changes"), M->SetTimeScale(1.));
    TestNull(TEXT("Inactive manager cannot create tasks"), F.Task());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSClockOverflowTest, "GameTimeSystem.Clock.MaximumDateClamp", GTSTests::Flags)
bool FGTSClockOverflowTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    const FDateTime Maximum = FDateTime::MaxValue();
    FFixture F(Maximum - FTimespan::FromSeconds(1.), 1.);
    auto* M = F.Manager.Get();
    auto* Due = F.Task();
    if (!TestNotNull(TEXT("Create max-date task"), Due)) return false;
    TestTrue(TEXT("Maximum supported date is a valid deadline"), M->RegisterTimeTask(Due, Maximum).IsValid());
    TestTrue(TEXT("Finite maximum double scale is accepted"), M->SetTimeScale(std::numeric_limits<double>::max()));
    M->AdvanceTime(std::numeric_limits<double>::max());
    TestEqual(TEXT("Multiplication overflow clamps at the maximum supported calendar date"), M->GetCurrentTime(), Maximum);
    TestTrue(TEXT("Clock pauses after reaching calendar capacity"), M->IsTimePaused());
    TestEqual(TEXT("Reached maximum-date deadline fires before automatic pause"), Due->CallCount, 1);
    TestEqual(TEXT("Callback receives clamped current time"), Due->LastCurrentTime, Maximum);
    M->AdvanceTime(1.);
    TestEqual(TEXT("Overflow never wraps into a negative or early date"), M->GetCurrentTime(), Maximum);
    TestEqual(TEXT("Maximum-date task fires once"), Due->CallCount, 1);

    FFixture Tail(Maximum - FTimespan::FromSeconds(1.), 1., false, 1);
    auto* TailFirst = Tail.Task();
    auto* TailSecond = Tail.Task();
    if (!TestNotNull(TEXT("Create first tail task"), TailFirst) || !TestNotNull(TEXT("Create second tail task"), TailSecond)) return false;
    Tail.Manager->RegisterTimeTask(TailFirst, Maximum);
    Tail.Manager->RegisterTimeTask(TailSecond, Maximum);
    Tail.Manager->AdvanceTime(100.);
    TestEqual(TEXT("Calendar-limit dispatch respects its normal callback budget"), TailFirst->CallCount + TailSecond->CallCount, 1);
    TestFalse(TEXT("Automatic pause waits until due tail backlog drains"), Tail.Manager->IsTimePaused());
    Tail.Manager->AdvanceTime(0.);
    TestEqual(TEXT("Tail task is not stranded at the calendar limit"), TailSecond->CallCount, 1);
    TestTrue(TEXT("Calendar-limit clock pauses when all tail callbacks finish"), Tail.Manager->IsTimePaused());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTaskOrderTest, "GameTimeSystem.Tasks.DeadlineOrderAndOnceOnly", GTSTests::Flags)
bool FGTSTaskOrderTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    auto* M = F.Manager.Get();
    TArray<int32> Order;
    TArray<FGuid> Registrations;
    TArray<UGTS_AutomationTimeTask*> Tasks;
    const int32 Seconds[] = {30, 10, 10, 20};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Task = F.Task();
        if (!TestNotNull(TEXT("Create ordered task"), Task)) return false;
        Task->OnInvoked = [&, Index](UGTS_AutomationTimeTask*, FDateTime, FDateTime) { Order.Add(Index); };
        Registrations.Add(M->RegisterTimeTask(Task, After(Seconds[Index])));
        Tasks.Add(Task);
    }
    M->AdvanceTime(9.);
    TestTrue(TEXT("No callback fires before its deadline"), Order.IsEmpty());
    M->AdvanceTime(1.);
    TestTrue(TEXT("Equal deadlines fire in registration order"), Order == TArray<int32>({1, 2}));
    M->AdvanceTime(35.);
    TestTrue(TEXT("Skipped deadlines fire by scheduled time"), Order == TArray<int32>({1, 2, 3, 0}));
    TestEqual(TEXT("Skipped task receives its original deadline"), Tasks[3]->LastScheduledTime, After(20.));
    TestEqual(TEXT("Skipped task sees post-jump calendar time"), Tasks[3]->LastCurrentTime, After(45.));
    TestEqual(TEXT("Finished tasks leave no pending registrations"), M->GetPendingTaskCount(), 0);
    for (const FGuid Id : Registrations) TestFalse(TEXT("Completed registration cannot be canceled"), M->CancelTimeTask(Id));
    M->SetCurrentTime(Epoch);
    M->AdvanceTime(60.);
    TestEqual(TEXT("Rewinding date never resurrects completed tasks"), Order.Num(), 4);
    for (auto* Task : Tasks) TestEqual(TEXT("Each registered task fires once"), Task->CallCount, 1);

    auto* Overdue = F.Task();
    auto* Jumped = F.Task();
    if (!TestNotNull(TEXT("Create overdue task"), Overdue) || !TestNotNull(TEXT("Create jumped task"), Jumped)) return false;
    M->RegisterTimeTask(Overdue, Epoch);
    TestEqual(TEXT("Past-date registration never invokes synchronously"), Overdue->CallCount, 0);
    M->RegisterTimeTask(Jumped, After(100.));
    M->SetCurrentTime(After(200.));
    TestEqual(TEXT("Setting time forwards does not recursively run tasks"), Jumped->CallCount, 0);
    M->AdvanceTime(0.);
    TestEqual(TEXT("Overdue task runs on next advancement"), Overdue->CallCount, 1);
    TestEqual(TEXT("Forward-set skipped deadline runs on next advancement"), Jumped->CallCount, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTaskRegistrationTest, "GameTimeSystem.Tasks.RegistrationAndCancellation", GTSTests::Flags)
bool FGTSTaskRegistrationTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    FFixture Other;
    auto* M = F.Manager.Get();
    TestNull(TEXT("Abstract task class cannot be instantiated"), M->CreateTimeTask(UGTS_TimeTask::StaticClass()));
    TestNull(TEXT("Null task class cannot be instantiated"), M->CreateTimeTask(nullptr));
    TestFalse(TEXT("Null task registration rejected"), M->RegisterTimeTask(nullptr, Epoch).IsValid());
    TestFalse(TEXT("Class default task registration rejected"),
        M->RegisterTimeTask(GetMutableDefault<UGTS_AutomationTimeTask>(), Epoch).IsValid());
    auto* Foreign = Other.Task();
    auto* Unmanaged = NewObject<UGTS_AutomationTimeTask>();
    auto* Task = F.Task();
    if (!TestNotNull(TEXT("Create registerable task"), Task) || !TestNotNull(TEXT("Create foreign task"), Foreign)) return false;
    TestFalse(TEXT("Tasks belong to the manager that created them"), M->RegisterTimeTask(Foreign, Epoch).IsValid());
    TestFalse(TEXT("Arbitrary NewObject cannot bypass manager ownership"), M->RegisterTimeTask(Unmanaged, Epoch).IsValid());
    TestEqual(TEXT("Task exposes creating manager"), Task->GetTimeManager(), M);
    TestFalse(TEXT("Negative task deadline rejected"), M->RegisterTimeTask(Task, FDateTime(int64(-1))).IsValid());
    TestFalse(TEXT("Out-of-range task deadline rejected"),
        M->RegisterTimeTask(Task, FDateTime(FDateTime::MaxValue().GetTicks() + 1)).IsValid());
    const FGuid FirstId = M->RegisterTimeTask(Task, After(10.));
    TestTrue(TEXT("Valid task creates a registration identity"), FirstId.IsValid());
    TestFalse(TEXT("Duplicate pending task is rejected"), M->RegisterTimeTask(Task, After(20.)).IsValid());
    TestEqual(TEXT("Duplicate rejection does not duplicate pending work"), M->GetPendingTaskCount(), 1);
    TestFalse(TEXT("Random cancellation is harmless"), M->CancelTimeTask(FGuid::NewGuid()));
    TestFalse(TEXT("Invalid cancellation is harmless"), M->CancelTimeTask(FGuid()));
    TestTrue(TEXT("Cancel valid task succeeds"), M->CancelTimeTask(FirstId));
    TestFalse(TEXT("Cancellation is once-only"), M->CancelTimeTask(FirstId));
    M->AdvanceTime(30.);
    TestEqual(TEXT("Canceled task is never invoked"), Task->CallCount, 0);
    const FGuid SecondId = M->RegisterTimeTask(Task, Epoch);
    TestTrue(TEXT("Canceled task object may be registered again with a fresh identity"), SecondId.IsValid() && SecondId != FirstId);
    M->AdvanceTime(0.);
    TestEqual(TEXT("Re-registered task invokes once"), Task->CallCount, 1);
    TestTrue(TEXT("Completed task can also be registered again"), M->RegisterTimeTask(Task, Epoch).IsValid());
    M->AdvanceTime(0.);
    TestEqual(TEXT("New registration produces one additional callback"), Task->CallCount, 2);

    TArray<FGuid> CanceledHeapEntries;
    TArray<int32> RemainingOrder;
    for (int32 Index = 0; Index < 90; ++Index)
    {
        auto* Entry = F.Task();
        if (!TestNotNull(TEXT("Create cancellation-compaction task"), Entry)) return false;
        Entry->OnInvoked = [&, Index](UGTS_AutomationTimeTask*, FDateTime, FDateTime) { RemainingOrder.Add(Index); };
        CanceledHeapEntries.Add(M->RegisterTimeTask(Entry, After(100.)));
    }
    for (int32 Index = 0; Index < 85; ++Index) M->CancelTimeTask(CanceledHeapEntries[Index]);
    M->AdvanceTime(100.);
    TestTrue(TEXT("Heap compaction preserves only live registrations in stable order"), RemainingOrder == TArray<int32>({85, 86, 87, 88, 89}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTaskReentrancyTest, "GameTimeSystem.Tasks.ReentrancyAndDeferredWork", GTSTests::Flags)
bool FGTSTaskReentrancyTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    auto* M = F.Manager.Get();
    auto* First = F.Task();
    auto* Canceled = F.Task();
    auto* Spawned = F.Task();
    if (!TestNotNull(TEXT("First task"), First) || !TestNotNull(TEXT("Canceled task"), Canceled)
        || !TestNotNull(TEXT("Spawned task"), Spawned)) return false;
    const FGuid FirstId = M->RegisterTimeTask(First, After(1.));
    const FGuid CanceledId = M->RegisterTimeTask(Canceled, After(2.));
    TArray<int32> Order;
    First->OnInvoked = [&](UGTS_AutomationTimeTask* Self, FDateTime, FDateTime)
    {
        Order.Add(1);
        TestFalse(TEXT("Task is removed from registration before its callback"), M->IsTimeTaskRegistered(FirstId));
        if (Self->CallCount == 1)
        {
            TestTrue(TEXT("Callback can cancel another due task"), M->CancelTimeTask(CanceledId));
            TestTrue(TEXT("Callback can register a new already-due task"), M->RegisterTimeTask(Spawned, Epoch).IsValid());
            TestTrue(TEXT("Callback can reschedule itself"), M->RegisterTimeTask(Self, Epoch).IsValid());
            const FDateTime AtCallback = M->GetCurrentTime();
            M->AdvanceTime(100.);
            TestEqual(TEXT("Reentrant advancement cannot advance date or dispatch recursively"), M->GetCurrentTime(), AtCallback);
            TestFalse(TEXT("Reentrant setting cannot change the active dispatch date"), M->SetCurrentTime(After(1000.)));
            TestEqual(TEXT("New work does not run on the registering callback stack"), Spawned->CallCount, 0);
        }
    };
    Spawned->OnInvoked = [&](UGTS_AutomationTimeTask*, FDateTime, FDateTime) { Order.Add(3); };
    M->AdvanceTime(10.);
    TestTrue(TEXT("Only original non-canceled work runs in the current advancement"), Order == TArray<int32>({1}));
    TestEqual(TEXT("Canceled due task is skipped"), Canceled->CallCount, 0);
    TestEqual(TEXT("Newly registered and self-rescheduled tasks remain pending"), M->GetPendingTaskCount(), 2);
    M->AdvanceTime(0.);
    TestTrue(TEXT("Deferred tasks run next advancement in insertion order"), Order == TArray<int32>({1, 3, 1}));
    TestEqual(TEXT("Self-reschedule did not recurse"), First->CallCount, 2);
    TestEqual(TEXT("Deferred callbacks retain their real requested deadline"), Spawned->LastScheduledTime, Epoch);
    TestEqual(TEXT("Completed reentrant work drains the registry"), M->GetPendingTaskCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTaskBudgetTest, "GameTimeSystem.Tasks.BudgetAndFairness", GTSTests::Flags)
bool FGTSTaskBudgetTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F(Epoch, 1., false, 2);
    auto* M = F.Manager.Get();
    TArray<int32> Order;
    for (int32 Index = 0; Index < 7; ++Index)
    {
        auto* Task = F.Task();
        if (!TestNotNull(TEXT("Create budgeted task"), Task)) return false;
        Task->OnInvoked = [&, Index](UGTS_AutomationTimeTask*, FDateTime, FDateTime) { Order.Add(Index); };
        M->RegisterTimeTask(Task, After(1.));
    }
    M->AdvanceTime(100.);
    TestTrue(TEXT("First callback budget runs oldest two registrations"), Order == TArray<int32>({0, 1}));
    M->AdvanceTime(0.);
    TestTrue(TEXT("Second advancement continues without duplicates or starvation"), Order == TArray<int32>({0, 1, 2, 3}));
    M->AdvanceTime(0.);
    M->AdvanceTime(0.);
    TestTrue(TEXT("Budgeted backlog drains in stable order"), Order == TArray<int32>({0, 1, 2, 3, 4, 5, 6}));
    TestEqual(TEXT("Backlog leaves no pending work"), M->GetPendingTaskCount(), 0);

    FFixture One(Epoch, 1., false, 1);
    auto* Repeater = One.Task();
    auto* Waiting = One.Task();
    if (!TestNotNull(TEXT("Create self-rescheduling task"), Repeater) || !TestNotNull(TEXT("Create waiting task"), Waiting)) return false;
    Repeater->OnInvoked = [&](UGTS_AutomationTimeTask* Self, FDateTime, FDateTime)
    {
        if (Self->CallCount < 3) One.Manager->RegisterTimeTask(Self, Epoch);
    };
    One.Manager->RegisterTimeTask(Repeater, Epoch);
    One.Manager->RegisterTimeTask(Waiting, After(1.));
    One.Manager->AdvanceTime(10.);
    TestEqual(TEXT("Budget one invokes only the first task"), Repeater->CallCount, 1);
    One.Manager->AdvanceTime(0.);
    TestEqual(TEXT("A callback cannot starve older due work by re-registering an earlier deadline"), Waiting->CallCount, 1);
    TestEqual(TEXT("Self-reschedule waits behind previously due work"), Repeater->CallCount, 1);
    One.Manager->AdvanceTime(0.);
    TestEqual(TEXT("Deferred self-reschedule eventually runs"), Repeater->CallCount, 2);
    TestEqual(TEXT("Fairness ordering does not change the callback's requested deadline"), Repeater->LastScheduledTime, Epoch);

    FFixture Clamped(Epoch, 1., false, 0);
    auto* AtLeastOne = Clamped.Task();
    if (!TestNotNull(TEXT("Create minimum-budget task"), AtLeastOne)) return false;
    Clamped.Manager->RegisterTimeTask(AtLeastOne, Epoch);
    Clamped.Manager->AdvanceTime(0.);
    TestEqual(TEXT("Zero callback budget is clamped so work can progress"), AtLeastOne->CallCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSCallbackControlTest, "GameTimeSystem.Tasks.CallbackPauseAndShutdown", GTSTests::Flags)
bool FGTSCallbackControlTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    auto* M = F.Manager.Get();
    auto* Pause = F.Task();
    auto* Waiting = F.Task();
    if (!TestNotNull(TEXT("Pause task"), Pause) || !TestNotNull(TEXT("Waiting task"), Waiting)) return false;
    Pause->OnInvoked = [M](UGTS_AutomationTimeTask*, FDateTime, FDateTime) { M->SetTimePaused(true); };
    M->RegisterTimeTask(Pause, Epoch);
    const FGuid WaitingId = M->RegisterTimeTask(Waiting, After(1.));
    M->AdvanceTime(10.);
    TestEqual(TEXT("Pause callback executed"), Pause->CallCount, 1);
    TestEqual(TEXT("Pausing from callback stops the remaining dispatch"), Waiting->CallCount, 0);
    TestTrue(TEXT("Paused backlog is retained"), M->IsTimeTaskRegistered(WaitingId));
    M->SetTimePaused(false);
    M->AdvanceTime(0.);
    TestEqual(TEXT("Unpause resumes overdue work"), Waiting->CallCount, 1);

    auto* Stop = F.Task();
    auto* Dropped = F.Task();
    auto* Deferred = F.Task();
    if (!TestNotNull(TEXT("Shutdown task"), Stop) || !TestNotNull(TEXT("Dropped task"), Dropped)
        || !TestNotNull(TEXT("Deferred task"), Deferred)) return false;
    Stop->OnInvoked = [&, M](UGTS_AutomationTimeTask*, FDateTime, FDateTime)
    {
        M->RegisterTimeTask(Deferred, Epoch);
        M->Shutdown();
    };
    M->RegisterTimeTask(Stop, Epoch);
    M->RegisterTimeTask(Dropped, Epoch);
    M->AdvanceTime(0.);
    TestFalse(TEXT("Shutdown from callback ends manager lifecycle"), M->IsInitialized());
    TestEqual(TEXT("Shutdown clears pending and newly deferred registrations"), M->GetPendingTaskCount(), 0);
    TestEqual(TEXT("Later due task is not called after shutdown"), Dropped->CallCount, 0);
    TestEqual(TEXT("Deferred task is not called after shutdown"), Deferred->CallCount, 0);
    M->AdvanceTime(100.);
    TestEqual(TEXT("Inactive advancement does not revive callbacks"), Stop->CallCount, 1);

    FFixture Notification;
    TStrongObjectPtr<UGTS_AutomationTimeObserver> Observer(NewObject<UGTS_AutomationTimeObserver>());
    Observer->OnTimeEvent = [&](FDateTime)
    {
        TestFalse(TEXT("Time-change notification cannot recursively set time"), Notification.Manager->SetCurrentTime(After(99.)));
        Notification.Manager->Shutdown();
    };
    Notification.Manager->OnTimeChanged.AddDynamic(Observer.Get(), &UGTS_AutomationTimeObserver::HandleTime);
    Notification.Manager->AdvanceTime(1.);
    TestFalse(TEXT("Shutdown during time notification is safe"), Notification.Manager->IsInitialized());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTaskLifetimeTest, "GameTimeSystem.Tasks.GarbageCollectionOwnership", GTSTests::Flags)
bool FGTSTaskLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    FFixture F;
    auto* M = F.Manager.Get();
    auto* Task = F.Task();
    if (!TestNotNull(TEXT("Create managed lifetime task"), Task)) return false;
    TWeakObjectPtr<UGTS_AutomationTimeTask> Weak = Task;
    const FGuid Id = M->RegisterTimeTask(Task, After(100.));
    Task = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Registration owns pending UObject task across GC"), Weak.IsValid());
    TestTrue(TEXT("Ownership survives without a caller-side strong reference"), M->IsTimeTaskRegistered(Id));
    M->CancelTimeTask(Id);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Cancellation releases the task for GC"), Weak.IsValid());

    auto* Completing = F.Task();
    if (!TestNotNull(TEXT("Create task collected during callback"), Completing)) return false;
    TWeakObjectPtr<UGTS_AutomationTimeTask> CompletedWeak = Completing;
    bool bAliveWithinCallback = false;
    Completing->OnInvoked = [&](UGTS_AutomationTimeTask*, FDateTime, FDateTime)
    {
        CollectGarbage(RF_NoFlags);
        bAliveWithinCallback = CompletedWeak.IsValid();
    };
    M->RegisterTimeTask(Completing, Epoch);
    Completing = nullptr;
    M->AdvanceTime(0.);
    TestTrue(TEXT("Dispatch retains task for entire callback even after removing registration"), bAliveWithinCallback);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Completed task has no leaked manager reference"), CompletedWeak.IsValid());

    auto* Pending = F.Task();
    if (!TestNotNull(TEXT("Create shutdown-owned task"), Pending)) return false;
    TWeakObjectPtr<UGTS_AutomationTimeTask> ShutdownWeak = Pending;
    M->RegisterTimeTask(Pending, After(100.));
    Pending = nullptr;
    M->Shutdown();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Shutdown releases pending tasks"), ShutdownWeak.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSLibraryValidationTest, "GameTimeSystem.Library.DateValidationAndMissingContext", GTSTests::Flags)
bool FGTSLibraryValidationTest::RunTest(const FString& Parameters)
{
    FDateTime Date;
    TestTrue(TEXT("Library constructs a valid leap date"), UGTS_TimeLibrary::MakeGameDateTime(2048, 2, 29, Date, 23, 59, 59));
    TestEqual(TEXT("Date parts are preserved"), Date, FDateTime(2048, 2, 29, 23, 59, 59));
    const int32 Invalid[][6] = {{2049, 2, 29, 0, 0, 0}, {2049, 4, 31, 0, 0, 0}, {0, 1, 1, 0, 0, 0},
        {10000, 1, 1, 0, 0, 0}, {2049, 0, 1, 0, 0, 0}, {2049, 13, 1, 0, 0, 0}, {2049, 1, 0, 0, 0, 0},
        {2049, 1, 1, 24, 0, 0}, {2049, 1, 1, 0, 60, 0}, {2049, 1, 1, 0, 0, 60}};
    for (const auto& Parts : Invalid)
    {
        TestFalse(TEXT("Invalid calendar parts return false without asserting"),
            UGTS_TimeLibrary::MakeGameDateTime(Parts[0], Parts[1], Parts[2], Date, Parts[3], Parts[4], Parts[5]));
        TestEqual(TEXT("Rejected date resets output"), Date, FDateTime::MinValue());
    }
    TestNull(TEXT("Missing context has no subsystem"), UGTS_TimeLibrary::GetTimeSubsystem(nullptr));
    TestNull(TEXT("Missing context has no manager"), UGTS_TimeLibrary::GetTimeManager(nullptr));
    TestFalse(TEXT("Missing context cannot set time"), UGTS_TimeLibrary::SetCurrentTime(nullptr, GTSTests::Epoch));
    TestFalse(TEXT("Missing context cannot set date parts"), UGTS_TimeLibrary::SetCurrentTimeFromParts(nullptr, 2049, 1, 1));
    TestFalse(TEXT("Missing context cannot set scale"), UGTS_TimeLibrary::SetTimeScale(nullptr, 1.));
    TestNull(TEXT("Missing context cannot create tasks"), UGTS_TimeLibrary::CreateTimeTask(nullptr, UGTS_AutomationTimeTask::StaticClass()));
    TestFalse(TEXT("Missing context cannot register tasks"), UGTS_TimeLibrary::RegisterTimeTask(nullptr, nullptr, GTSTests::Epoch).IsValid());
    TestFalse(TEXT("Missing context cannot cancel tasks"), UGTS_TimeLibrary::CancelTimeTask(nullptr, FGuid::NewGuid()));
    TestEqual(TEXT("Missing context exposes zero pending work"), UGTS_TimeLibrary::GetPendingTaskCount(nullptr), 0);

    const double InvalidScales[] = {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    for (double Scale : InvalidScales)
    {
        GTSTests::FFixture InvalidManager(GTSTests::Epoch, Scale);
        TestFalse(TEXT("Invalid initial scale leaves manager uninitialized"), InvalidManager.Manager->IsInitialized());
    }
    GTSTests::FFixture InvalidDate(FDateTime(int64(-1)));
    TestFalse(TEXT("Invalid initial date leaves manager uninitialized"), InvalidDate.Manager->IsInitialized());
    return true;
}

namespace GTSTests
{
    struct FRuntimeFixture
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<UGTS_TimeManager> Manager;
        TWeakObjectPtr<AWorldSettings> WorldSettings;
        TStrongObjectPtr<UGTS_AutomationTimeTask> Task;
        TStrongObjectPtr<UTextBlock> TimeText;
        FGuid Registration;
        FDateTime OriginalTime;
        double OriginalScale = 1.;
        bool bOriginalPaused = false;
        float OriginalDilation = 1.f;
        bool bRestoreState = false;
        ~FRuntimeFixture()
        {
            if (Manager.IsValid() && bRestoreState)
            {
                if (World.IsValid() && TimeText.IsValid()) UGTS_TimeLibrary::UnregisterTimeTextBlock(World.Get(), TimeText.Get());
                Manager->CancelTimeTask(Registration);
                Manager->SetTimePaused(true);
                Manager->SetCurrentTime(OriginalTime);
                Manager->SetTimeScale(OriginalScale);
                Manager->SetTimePaused(bOriginalPaused);
            }
            if (WorldSettings.IsValid() && bRestoreState) WorldSettings->SetTimeDilation(OriginalDilation);
        }
    };

    class FVerifyRuntimeClock final : public IAutomationLatentCommand
    {
        TSharedRef<FRuntimeFixture> Fixture;
        FAutomationTestBase* Test;
        int32 Step = 0;
        double Started = FPlatformTime::Seconds();
        FDateTime PausedAt;
    public:
        FVerifyRuntimeClock(TSharedRef<FRuntimeFixture> InFixture, FAutomationTestBase* InTest)
            : Fixture(InFixture), Test(InTest) {}
        bool Update() override
        {
            auto* World = Fixture->World.Get();
            auto* M = Fixture->Manager.Get();
            if (!World || !M) { Test->AddError(TEXT("Runtime time subsystem disappeared.")); return true; }
            if (FPlatformTime::Seconds() - Started > 8.)
            { Test->AddError(TEXT("Actual core ticker did not execute the calendar task within eight seconds.")); return true; }
            if (Step == 0)
            {
                if (Fixture->Task->CallCount == 0) return false;
                Test->TestEqual(TEXT("Actual core ticker invokes the task exactly once"), Fixture->Task->CallCount, 1);
                Test->TestTrue(TEXT("Actual ticker advances scaled calendar to the task deadline"), Fixture->Task->LastCurrentTime >= After(4.));
                Test->TestEqual(TEXT("Task reports original requested deadline"), Fixture->Task->LastScheduledTime, After(4.));
                Test->TestEqual(TEXT("Task object provides the running world context"), Fixture->Task->GetWorld(), World);
                Test->TestEqual(TEXT("Task can call world-context library nodes"), UGTS_TimeLibrary::GetTimeManager(Fixture->Task.Get()), M);
                Test->TestFalse(TEXT("Completed task registration is absent through the library"),
                    UGTS_TimeLibrary::IsTimeTaskRegistered(World, Fixture->Registration));
                Test->TestEqual(TEXT("Actual ticker updates the registered time TextBlock"), Fixture->TimeText->GetText().ToString(),
                    UGTS_TimeLibrary::FormatCurrentTime(World, FText::FromString(TEXT("{Hour}:{Minute}:{Second}"))).ToString());
                UGTS_TimeLibrary::SetTimePaused(World, true);
                PausedAt = M->GetCurrentTime();
                Step = 1;
                Started = FPlatformTime::Seconds();
                return false;
            }
            if (FPlatformTime::Seconds() - Started < .2) return false;
            Test->TestTrue(TEXT("Pause getter reflects library setter"), UGTS_TimeLibrary::IsTimePaused(World));
            Test->TestEqual(TEXT("Clock remains frozen across actual core ticker frames while paused"), M->GetCurrentTime(), PausedAt);
            Test->TestEqual(TEXT("Paused runtime text retains the formatted current date"), Fixture->TimeText->GetText().ToString(),
                UGTS_TimeLibrary::FormatCurrentTime(World, FText::FromString(TEXT("{Hour}:{Minute}:{Second}"))).ToString());
            Test->TestEqual(TEXT("No repeat callback on later runtime frames"), Fixture->Task->CallCount, 1);
            Test->TestTrue(TEXT("Blueprint date-parts setter resolves actual game world"),
                UGTS_TimeLibrary::SetCurrentTimeFromParts(World, 2048, 2, 29, 12, 34, 56));
            Test->TestEqual(TEXT("Library date getter returns the set date"),
                UGTS_TimeLibrary::GetCurrentTime(World), FDateTime(2048, 2, 29, 12, 34, 56));
            Test->TestEqual(TEXT("Explicit setting refreshes runtime TextBlock while paused"),
                Fixture->TimeText->GetText().ToString(), FString(TEXT("12:34:56")));
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSRuntimeIntegrationTest, "GameTimeSystem.Runtime.GameInstanceTickerAndLibrary",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGTSRuntimeIntegrationTest::RunTest(const FString& Parameters)
{
    using namespace GTSTests;
    auto Fixture = MakeShared<FRuntimeFixture>();
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World()) { Fixture->World = Context.World(); break; }
    auto* World = Fixture->World.Get();
    if (!TestNotNull(TEXT("Test requires a real standalone game world"), World)) return false;
    auto* Subsystem = UGTS_TimeLibrary::GetTimeSubsystem(World);
    if (!TestNotNull(TEXT("GameInstance creates time subsystem"), Subsystem)
        || !TestTrue(TEXT("Game time subsystem reports ready"), Subsystem->IsReady())) return false;
    Fixture->Manager = UGTS_TimeLibrary::GetTimeManager(World);
    auto* M = Fixture->Manager.Get();
    if (!TestNotNull(TEXT("World-context library returns its manager"), M)) return false;
    TestEqual(TEXT("Manager supplies current game world"), M->GetWorld(), World);
    Fixture->WorldSettings = World->GetWorldSettings();
    if (!TestNotNull(TEXT("Runtime world settings exist"), Fixture->WorldSettings.Get())
        || !TestFalse(TEXT("Runtime fixture requires an unpaused world"), World->IsPaused())) return false;
    Fixture->OriginalTime = M->GetCurrentTime();
    Fixture->OriginalScale = M->GetTimeScale();
    Fixture->bOriginalPaused = M->IsTimePaused();
    Fixture->OriginalDilation = Fixture->WorldSettings->TimeDilation;
    Fixture->bRestoreState = true;
    UGTS_TimeLibrary::SetTimePaused(World, true);
    TestTrue(TEXT("Library accepts initial test date"), UGTS_TimeLibrary::SetCurrentTime(World, Epoch));
    TestTrue(TEXT("Library accepts a scaled calendar"), UGTS_TimeLibrary::SetTimeScale(World, 120.));
    Fixture->TimeText.Reset(NewObject<UTextBlock>(World->GetGameInstance()));
    TestTrue(TEXT("Library registers runtime clock display"), UGTS_TimeLibrary::RegisterTimeTextBlock(World, Fixture->TimeText.Get(),
        FText::FromString(TEXT("{Hour}:{Minute}:{Second}"))));
    TestEqual(TEXT("Runtime display initializes immediately"), Fixture->TimeText->GetText().ToString(), FString(TEXT("00:00:00")));
    TestEqual(TEXT("Calendar scale does not write engine time dilation"), Fixture->WorldSettings->TimeDilation, Fixture->OriginalDilation);
    // World time is slowed by 1000x. A calendar incorrectly driven by dilated world delta
    // cannot reach this deadline inside the eight-second test window.
    Fixture->WorldSettings->SetTimeDilation(.001f);
    Fixture->Task.Reset(Cast<UGTS_AutomationTimeTask>(UGTS_TimeLibrary::CreateTimeTask(World, UGTS_AutomationTimeTask::StaticClass())));
    if (!TestNotNull(TEXT("Blueprint library creates concrete callback object"), Fixture->Task.Get())) return false;
    Fixture->Registration = UGTS_TimeLibrary::RegisterTimeTask(World, Fixture->Task.Get(), After(4.));
    if (!TestTrue(TEXT("Blueprint library registers future callback"), Fixture->Registration.IsValid())) return false;
    TestTrue(TEXT("Library finds pending registration"), UGTS_TimeLibrary::IsTimeTaskRegistered(World, Fixture->Registration));
    UGTS_TimeLibrary::SetTimePaused(World, false);
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyRuntimeClock(Fixture, this));
    return true;
}
#endif
