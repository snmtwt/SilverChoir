#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GTS_TimeLibrary.h"
#include "GTS_TimeManager.h"
#include "GTS_TimeSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace GTSTimeTextTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FText Format(const TCHAR* Value) { return FText::FromString(Value); }

    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
        TWeakObjectPtr<UWorld> World;
        bool bShutdown = false;

        FFixture()
        {
            Instance->InitializeStandalone();
            World = Instance->GetWorld();
        }
        ~FFixture() { Shutdown(); }
        UGTS_TimeSubsystem* Subsystem() const { return Instance->GetSubsystem<UGTS_TimeSubsystem>(); }
        UGTS_TimeManager* Manager() const { return Subsystem() ? Subsystem()->GetManager() : nullptr; }
        void Shutdown()
        {
            if (bShutdown) return;
            bShutdown = true;
            Instance->Shutdown();
            if (World.IsValid())
            {
                GEngine->DestroyWorldContext(World.Get());
                World->DestroyWorld(false);
            }
        }
        UTextBlock* Text() const { return NewObject<UTextBlock>(Instance.Get()); }
        bool IsReady() const { return World.IsValid() && Subsystem() && Subsystem()->IsReady(); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTimeTextFormatTest, "GameTimeSystem.Text.FormatPlaceholders", GTSTimeTextTests::Flags)
bool FGTSTimeTextFormatTest::RunTest(const FString& Parameters)
{
    using namespace GTSTimeTextTests;
    const FDateTime Time(2024, 2, 29, 3, 4, 5);
    TestEqual(TEXT("All numeric fields have stable zero padding"), UGTS_TimeSubsystem::FormatTimeText(Time,
        Format(TEXT("{Year}-{Month}-{Day} {Hour}:{Minute}:{Second}"))).ToString(), FString(TEXT("2024-02-29 03:04:05")));
    TestEqual(TEXT("Date template preserves surrounding labels and punctuation"), UGTS_TimeSubsystem::FormatTimeText(Time,
        Format(TEXT("基地时间  {Year}年{Month}月{Day}日\n{Hour}时{Minute}分"))).ToString(), FString(TEXT("基地时间  2024年02月29日\n03时04分")));
    TestEqual(TEXT("Templates may repeat or omit calendar fields"), UGTS_TimeSubsystem::FormatTimeText(Time,
        Format(TEXT("{Minute}/{Hour}/{Minute}"))).ToString(), FString(TEXT("04/03/04")));
    TestEqual(TEXT("Year is padded to four digits too"), UGTS_TimeSubsystem::FormatTimeText(FDateTime(9, 1, 2),
        Format(TEXT("{Year}-{Month}-{Day}"))).ToString(), FString(TEXT("0009-01-02")));
    TestEqual(TEXT("Weekday uses the declared localized Chinese text"), UGTS_TimeSubsystem::FormatTimeText(Time,
        Format(TEXT("{Weekday}"))).ToString(), NSLOCTEXT("GTS", "Thursday", "星期四").ToString());
    TestTrue(TEXT("Empty template explicitly produces empty text"), UGTS_TimeSubsystem::FormatTimeText(Time, FText::GetEmpty()).IsEmpty());
    TestEqual(TEXT("Literal template is valid"), UGTS_TimeSubsystem::FormatTimeText(Time, Format(TEXT("本地终端"))).ToString(), FString(TEXT("本地终端")));
    TestTrue(TEXT("Invalid negative calendar cannot be formatted"), UGTS_TimeSubsystem::FormatTimeText(FDateTime(int64(-1)),
        Format(TEXT("{Year}"))).IsEmpty());
    TestTrue(TEXT("Date above supported calendar cannot be formatted"), UGTS_TimeSubsystem::FormatTimeText(
        FDateTime(FDateTime::MaxValue().GetTicks() + 1), Format(TEXT("{Year}"))).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTimeTextBindingTest, "GameTimeSystem.Text.RegisterAndRefresh", GTSTimeTextTests::Flags)
bool FGTSTimeTextBindingTest::RunTest(const FString& Parameters)
{
    using namespace GTSTimeTextTests;
    FFixture F;
    if (!TestTrue(TEXT("Standalone GameInstance initializes a ready time subsystem"), F.IsReady())) return false;
    auto* Sub = F.Subsystem();
    auto* M = F.Manager();
    M->SetTimePaused(false);
    M->SetTimeScale(1.);
    M->SetCurrentTime(FDateTime(2024, 2, 28, 23, 59, 59));
    TStrongObjectPtr<UTextBlock> Date(F.Text());
    TStrongObjectPtr<UTextBlock> Clock(F.Text());
    const FText DateFormat = Format(TEXT("{Year}/{Month}/{Day}"));
    const FText ClockFormat = Format(TEXT("{Hour}:{Minute}:{Second}"));
    TestEqual(TEXT("Widget inherits its GameInstance world context"), Date->GetWorld(), F.World.Get());
    TestTrue(TEXT("Library registers first TextBlock"), UGTS_TimeLibrary::RegisterTimeTextBlock(F.World.Get(), Date.Get(), DateFormat));
    TestTrue(TEXT("Subsystem registers a second independently formatted TextBlock"), Sub->RegisterTimeTextBlock(Clock.Get(), ClockFormat));
    TestEqual(TEXT("Date is rendered immediately without waiting for a frame"), Date->GetText().ToString(), FString(TEXT("2024/02/28")));
    TestEqual(TEXT("Clock has its own immediate format"), Clock->GetText().ToString(), FString(TEXT("23:59:59")));
    TestEqual(TEXT("Both registrations exist"), Sub->GetRegisteredTimeTextBlockCount(), 2);
    TestTrue(TEXT("Registration query resolves through the library"), UGTS_TimeLibrary::IsTimeTextBlockRegistered(F.World.Get(), Date.Get()));
    M->AdvanceTime(.25);
    TestEqual(TEXT("Subsecond progress does not change whole-second display"), Clock->GetText().ToString(), FString(TEXT("23:59:59")));
    M->AdvanceTime(.75);
    TestEqual(TEXT("Rollover updates registered date"), Date->GetText().ToString(), FString(TEXT("2024/02/29")));
    TestEqual(TEXT("Minute, hour and day rollover updates registered clock"), Clock->GetText().ToString(), FString(TEXT("00:00:00")));
    M->AdvanceTime(86400.);
    TestEqual(TEXT("Leap-day transition updates without rebinding"), Date->GetText().ToString(), FString(TEXT("2024/03/01")));

    M->SetTimePaused(true);
    M->AdvanceTime(12345.);
    TestEqual(TEXT("Paused clock does not advance bound date"), Date->GetText().ToString(), FString(TEXT("2024/03/01")));
    TestTrue(TEXT("Explicit date can still be changed while paused"),
        UGTS_TimeLibrary::SetCurrentTimeFromParts(F.World.Get(), 2049, 12, 31, 18, 7, 9));
    TestEqual(TEXT("Explicit paused setting refreshes every date field immediately"), Date->GetText().ToString(), FString(TEXT("2049/12/31")));
    TestEqual(TEXT("Explicit paused setting refreshes the other format immediately"), Clock->GetText().ToString(), FString(TEXT("18:07:09")));
    TestEqual(TEXT("Pure formatting node uses the same clock and template"),
        UGTS_TimeLibrary::FormatCurrentTime(F.World.Get(), ClockFormat).ToString(), Clock->GetText().ToString());
    TestTrue(TEXT("Duplicate widget registration updates its format"),
        UGTS_TimeLibrary::RegisterTimeTextBlock(F.World.Get(), Clock.Get(), Format(TEXT("{Day}日 {Hour}:{Minute}"))));
    TestEqual(TEXT("Duplicate widget does not add a second binding"), Sub->GetRegisteredTimeTextBlockCount(), 2);
    TestEqual(TEXT("Changed format appears immediately"), Clock->GetText().ToString(), FString(TEXT("31日 18:07")));
    TestTrue(TEXT("Empty format is a legitimate binding"), Sub->RegisterTimeTextBlock(Clock.Get(), FText::GetEmpty()));
    TestTrue(TEXT("Empty format clears previous visible text"), Clock->GetText().IsEmpty());
    Sub->RegisterTimeTextBlock(Clock.Get(), ClockFormat);
    const FText LastClock = Clock->GetText();
    TestTrue(TEXT("Library unregisters existing text"), UGTS_TimeLibrary::UnregisterTimeTextBlock(F.World.Get(), Clock.Get()));
    TestFalse(TEXT("Canceled registration cannot be found"), Sub->IsTimeTextBlockRegistered(Clock.Get()));
    TestFalse(TEXT("Repeated unregister is harmless"), Sub->UnregisterTimeTextBlock(Clock.Get()));
    M->SetCurrentTime(FDateTime(2050, 1, 1, 1, 2, 3));
    TestTrue(TEXT("Unregistered text keeps its last display"), Clock->GetText().EqualTo(LastClock));
    TestEqual(TEXT("Remaining widget continues refreshing"), Date->GetText().ToString(), FString(TEXT("2050/01/01")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTimeTextValidationTest, "GameTimeSystem.Text.ValidationAndWorldIsolation", GTSTimeTextTests::Flags)
bool FGTSTimeTextValidationTest::RunTest(const FString& Parameters)
{
    using namespace GTSTimeTextTests;
    FFixture F;
    FFixture Other;
    if (!TestTrue(TEXT("Two independent GameInstances initialize"), F.IsReady() && Other.IsReady())) return false;
    auto* Sub = F.Subsystem();
    TStrongObjectPtr<UTextBlock> Own(F.Text());
    TStrongObjectPtr<UTextBlock> Foreign(Other.Text());
    TStrongObjectPtr<UTextBlock> Worldless(NewObject<UTextBlock>());
    const FText Pattern = Format(TEXT("{Year}"));
    TestFalse(TEXT("Null text registration rejected"), Sub->RegisterTimeTextBlock(nullptr, Pattern));
    TestFalse(TEXT("CDO cannot be registered as display instance"), Sub->RegisterTimeTextBlock(GetMutableDefault<UTextBlock>(), Pattern));
    TestFalse(TEXT("Another GameInstance's text is rejected"), Sub->RegisterTimeTextBlock(Foreign.Get(), Pattern));
    TestEqual(TEXT("Rejected widgets do not enter the registry"), Sub->GetRegisteredTimeTextBlockCount(), 0);
    TestFalse(TEXT("Missing library context rejects registration"), UGTS_TimeLibrary::RegisterTimeTextBlock(nullptr, Own.Get(), Pattern));
    TestFalse(TEXT("Missing library context cannot unregister"), UGTS_TimeLibrary::UnregisterTimeTextBlock(nullptr, Own.Get()));
    TestFalse(TEXT("Missing library context cannot report registration"), UGTS_TimeLibrary::IsTimeTextBlockRegistered(nullptr, Own.Get()));
    TestTrue(TEXT("Missing context formatting gives empty text"), UGTS_TimeLibrary::FormatCurrentTime(nullptr, Pattern).IsEmpty());
    TestFalse(TEXT("Wrong explicit context rejects another instance's widget"),
        UGTS_TimeLibrary::RegisterTimeTextBlock(Other.World.Get(), Own.Get(), Pattern));
    TestTrue(TEXT("Worldless transient TextBlock is allowed for native consumers"), Sub->RegisterTimeTextBlock(Worldless.Get(), Pattern));
    TestTrue(TEXT("Own text registration succeeds after rejected attempts"), Sub->RegisterTimeTextBlock(Own.Get(), Pattern));
    TestFalse(TEXT("Foreign subsystem cannot cancel this registration"),
        UGTS_TimeLibrary::UnregisterTimeTextBlock(Other.World.Get(), Own.Get()));
    TestTrue(TEXT("Wrong-context cancellation leaves owner binding intact"), Sub->IsTimeTextBlockRegistered(Own.Get()));
    TestFalse(TEXT("Null text cancellation is safe"), Sub->UnregisterTimeTextBlock(nullptr));
    TestFalse(TEXT("Null text query is safe"), Sub->IsTimeTextBlockRegistered(nullptr));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGTSTimeTextLifetimeTest, "GameTimeSystem.Text.WeakLifetimeAndDeinitialize", GTSTimeTextTests::Flags)
bool FGTSTimeTextLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace GTSTimeTextTests;
    FFixture F;
    if (!TestTrue(TEXT("Lifetime fixture initializes subsystem"), F.IsReady())) return false;
    TStrongObjectPtr<UGTS_TimeSubsystem> Sub(F.Subsystem());
    TStrongObjectPtr<UGTS_TimeManager> M(F.Manager());
    TStrongObjectPtr<UTextBlock> Retained(F.Text());
    const FText Pattern = Format(TEXT("{Hour}:{Minute}:{Second}"));
    M->SetCurrentTime(FDateTime(2049, 1, 1, 2, 3, 4));
    TestTrue(TEXT("Retained text can be registered"), Sub->RegisterTimeTextBlock(Retained.Get(), Pattern));
    UTextBlock* Disposable = F.Text();
    const TWeakObjectPtr<UTextBlock> WeakText = Disposable;
    TestTrue(TEXT("Disposable text can be registered"), Sub->RegisterTimeTextBlock(Disposable, Pattern));
    Disposable = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Time bindings do not keep discarded UI alive"), WeakText.IsValid());
    TestEqual(TEXT("Registry count excludes dead weak bindings immediately"), Sub->GetRegisteredTimeTextBlockCount(), 1);
    TestTrue(TEXT("Clock update safely prunes dead text references"), M->SetCurrentTime(FDateTime(2049, 1, 1, 5, 6, 7)));
    TestEqual(TEXT("Remaining live text still updates after GC"), Retained->GetText().ToString(), FString(TEXT("05:06:07")));
    const FText BeforeShutdown = Retained->GetText();
    F.Shutdown();
    TestFalse(TEXT("Deinitialized subsystem is no longer ready"), Sub->IsReady());
    TestEqual(TEXT("Deinitialization clears all display registrations"), Sub->GetRegisteredTimeTextBlockCount(), 0);
    TestFalse(TEXT("Deinitialized subsystem reports no lingering binding"), Sub->IsTimeTextBlockRegistered(Retained.Get()));
    TestFalse(TEXT("Deinitialized subsystem rejects new display registrations"), Sub->RegisterTimeTextBlock(Retained.Get(), Pattern));
    TestTrue(TEXT("Deinitialization retains the widget's last text"), Retained->GetText().EqualTo(BeforeShutdown));
    TestFalse(TEXT("Deinitialization stops the old manager's time events"), M->SetCurrentTime(FDateTime(2050, 1, 1)));
    return true;
}
#endif
