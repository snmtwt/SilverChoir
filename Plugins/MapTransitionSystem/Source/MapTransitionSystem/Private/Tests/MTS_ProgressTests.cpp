#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MTS_MapTransitionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "UObject/StrongObjectPtr.h"

class FMTSWaitForDelayedClose : public IAutomationLatentCommand
{
public:
	FMTSWaitForDelayedClose(FAutomationTestBase* InTest, UMTS_MapTransitionSubsystem* InAuto, UMTS_MapTransitionSubsystem* InManual)
		: Test(InTest), Auto(InAuto), Manual(InManual), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 10)
		{
			Test->AddError(TEXT("Timed out waiting for delayed map completion"));
			return true;
		}
		if (!bNotified)
		{
			if (Auto->IsTransitionInProgress()) { return false; }
			Test->TestEqual(TEXT("Auto timer closes below 100 percent"), Auto->GetCurrentPayload().Progress, 0.8f);
			Test->TestTrue(TEXT("Manual mode remains open beyond delay without notification"), Manual->IsTransitionInProgress());
			Manual->NotifyMapLoadCompleted(TEXT("TimerTest"));
			Manual->SetTransitionProgress(1.0f, FText());
			Test->TestTrue(TEXT("100 percent after notification does not skip delay"), Manual->IsTransitionInProgress());
			bNotified = true;
			return false;
		}
		if (Manual->IsTransitionInProgress())
		{
			// Repeated notifications must not reset the countdown.
			Manual->NotifyMapLoadCompleted(TEXT("RepeatedTimerTest"));
			return false;
		}
		Test->TestEqual(TEXT("Notified timer completes"), Manual->GetCurrentPayload().Phase, EMTS_MapTransitionPhase::Completed);
		return true;
	}
private:
	FAutomationTestBase* Test;
	TStrongObjectPtr<UMTS_MapTransitionSubsystem> Auto;
	TStrongObjectPtr<UMTS_MapTransitionSubsystem> Manual;
	double Started;
	bool bNotified = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMTSProgressLimitTest, "MapTransitionSystem.Progress.AutomaticLimit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMTSProgressLimitTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMTS_MapTransitionSubsystem* System = NewObject<UMTS_MapTransitionSubsystem>(GameInstance);
	System->ActiveRequest.AutomaticProgressMax = 0.8f;
	System->ActiveRequest.ProgressSmoothSpeed = 0.0f;
	System->ActiveRequest.LoadingCompletionDelaySeconds = 2.0f;
	System->bTransitionInProgress = true;
	System->SetTransitionPhase(EMTS_MapTransitionPhase::OpeningMap, 0.5f, FText());
	TestEqual(TEXT("Automatic milestones scale into reserved range"), System->GetCurrentPayload().Progress, 0.4f);
	FMTS_MapTransitionRequest CompetingRequest;
	TestFalse(TEXT("Competing transition rejected"), System->SwitchMap(CompetingRequest));
	TestTrue(TEXT("Competing request does not destroy active transition"), System->IsTransitionInProgress());
	System->SetTransitionProgress(0.95f, FText());
	TestEqual(TEXT("Manual progress exceeds cap even with completion delay"), System->GetCurrentPayload().Progress, 0.95f);
	System->SetTransitionProgress(0.1f, FText());
	TestEqual(TEXT("Progress never regresses"), System->GetCurrentPayload().Progress, 0.95f);
	System->CompleteTransition();
	TestFalse(TEXT("Completion below 100 percent closes transition"), System->IsTransitionInProgress());
	TestEqual(TEXT("Completion preserves manual progress above automatic cap"), System->GetCurrentPayload().Progress, 0.95f);

	// A worldless subsystem makes next-tick readiness synchronous, isolating the gates from travel.
	System->CurrentPayload = FMTS_MapTransitionPayload();
	System->TargetProgress = 0;
	System->bTransitionInProgress = true;
	System->ActiveRequest.LoadingCompletionDelaySeconds = 0;
	System->ActiveRequest.bRequireManualMapReadyNotification = false;
	System->HandlePostLoadMapWithWorld(nullptr);
	TestFalse(TEXT("Automatic readiness closes below 100 percent"), System->IsTransitionInProgress());
	TestEqual(TEXT("Automatic completion stops at cap"), System->GetCurrentPayload().Progress, 0.8f);
	TestEqual(TEXT("Automatic completion exposes completed phase"), System->GetCurrentPayload().Phase, EMTS_MapTransitionPhase::Completed);

	System->CurrentPayload = FMTS_MapTransitionPayload();
	System->TargetProgress = 0;
	System->bTransitionInProgress = true;
	System->ActiveRequest.bRequireManualMapReadyNotification = true;
	System->HandlePostLoadMapWithWorld(nullptr);
	TestTrue(TEXT("Manual readiness waits for notification"), System->IsTransitionInProgress());
	System->SetTransitionProgress(1.0f, FText());
	TestTrue(TEXT("100 percent cannot replace notification"), System->IsTransitionInProgress());
	System->NotifyMapLoadCompleted(TEXT("ProgressTest"));
	TestFalse(TEXT("Notification permits completion"), System->IsTransitionInProgress());

	System->CurrentPayload = FMTS_MapTransitionPayload();
	System->TargetProgress = 0;
	System->bTransitionInProgress = true;
	System->HandlePostLoadMapWithWorld(nullptr);
	System->NotifyMapLoadCompleted(TEXT("BelowCapTest"));
	TestFalse(TEXT("Notification closes without manual 100 percent"), System->IsTransitionInProgress());
	TestEqual(TEXT("Notified completion still respects cap"), System->GetCurrentPayload().Progress, 0.8f);
	UGameInstance* LiveGameInstance = nullptr;
	System->CurrentPayload = FMTS_MapTransitionPayload();
	System->TargetProgress = 0;
	System->bTransitionInProgress = true;
	System->bMapReadyReceived = false;
	System->ActiveRequest.AutomaticProgressMax = .3f;
	System->SubMapWeights = {{TEXT("Base"), 1.f}, {TEXT("Battle"), 1.f}};
	FMTS_MapTransitionPayload Child;
	Child.Phase = EMTS_MapTransitionPhase::Completed;
	Child.Progress = 1;
	System->HandleSubMapProgress(TEXT("Base"), Child);
	TestTrue(TEXT("First of two equal stages reaches 65 percent"), FMath::IsNearlyEqual(System->GetCurrentPayload().Progress, .65f));
	System->MarkMapReady(TEXT("EarlyReady"));
	TestTrue(TEXT("Early readiness cannot close incomplete plan"), System->IsTransitionInProgress());
	Child.Phase = EMTS_MapTransitionPhase::OpeningMap;
	Child.Progress = .5f;
	System->HandleSubMapProgress(TEXT("Battle"), Child);
	TestTrue(TEXT("Second stage continues at 82.5 percent"), FMath::IsNearlyEqual(System->GetCurrentPayload().Progress, .825f));
	Child.Progress = .1f;
	System->HandleSubMapProgress(TEXT("Battle"), Child);
	TestTrue(TEXT("Child regressions do not move total backwards"), FMath::IsNearlyEqual(System->GetCurrentPayload().Progress, .825f));
	Child.Progress = 1;
	System->HandleSubMapProgress(TEXT("Battle"), Child);
	TestTrue(TEXT("Numeric 100 alone cannot complete stage"), System->IsTransitionInProgress());
	Child.Phase = EMTS_MapTransitionPhase::Completed;
	System->HandleSubMapProgress(TEXT("Battle"), Child);
	TestFalse(TEXT("All completed stages release deferred readiness"), System->IsTransitionInProgress());
	TestEqual(TEXT("Combined completion ends at 100 percent"), System->GetCurrentPayload().Progress, 1.f);
	TestFalse(TEXT("Completed plan clears subscriptions and state"), System->HasSubMapProgressPlan());
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World())
		{
			LiveGameInstance = Context.World()->GetGameInstance();
			break;
		}
	}
	if (!TestNotNull(TEXT("Game world available for real delay timers"), LiveGameInstance)) { return false; }
	UMTS_MapTransitionSubsystem* Auto = NewObject<UMTS_MapTransitionSubsystem>(LiveGameInstance);
	UMTS_MapTransitionSubsystem* Manual = NewObject<UMTS_MapTransitionSubsystem>(LiveGameInstance);
	for (UMTS_MapTransitionSubsystem* Delayed : { Auto, Manual })
	{
		Delayed->ActiveRequest.AutomaticProgressMax = 0.8f;
		Delayed->ActiveRequest.ProgressSmoothSpeed = 0;
		Delayed->ActiveRequest.LoadingCompletionDelaySeconds = 0.3f;
		Delayed->ActiveRequest.bRequireManualMapReadyNotification = Delayed == Manual;
		Delayed->bTransitionInProgress = true;
		Delayed->HandlePostLoadMapWithWorld(LiveGameInstance->GetWorld());
		TestTrue(TEXT("Readiness does not skip nonzero delay"), Delayed->IsTransitionInProgress());
	}
	Manual->SetTransitionProgress(1.0f, FText());
	ADD_LATENT_AUTOMATION_COMMAND(FMTSWaitForDelayedClose(this, Auto, Manual));
	return true;
}
#endif
