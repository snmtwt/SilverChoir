#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_SubMapSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreamingDynamic.h"
#include "UObject/StrongObjectPtr.h"

class FMTSWaitBatch : public IAutomationLatentCommand
{
public:
    FMTSWaitBatch(FAutomationTestBase* T, UMTS_MapTransitionSubsystem* S) : Test(T), System(S), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        auto* Maps = System->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
        FMTS_SubMapInfo A, B;
        const bool FoundA = Maps->GetSubMapByKey(TEXT("BatchA"), A);
        const bool FoundB = Maps->GetSubMapByKey(TEXT("BatchB"), B);
        if (FPlatformTime::Seconds() - Start > 30)
        {
            Test->AddError(TEXT("Batch lifecycle timed out"));
            System->ReportMapInitializationFailure(FText::FromString(TEXT("Test timeout")));
            return true;
        }
        if (bCleaning) { return !FoundA && !FoundB; }
        if (bNotified)
        {
            if (System->IsTransitionInProgress()) { return false; }
            Test->TestEqual(TEXT("Notification completes transition"), System->GetCurrentPayload().Phase, EMTS_MapTransitionPhase::Completed);
            Test->TestTrue(TEXT("Completed maps remain registered"), FoundA && FoundB);
            Maps->UnloadSubMapByID(TEXT("BatchA")); Maps->UnloadSubMapByID(TEXT("BatchB"));
            bCleaning = true; return false;
        }
        if (!FoundA || !FoundB || A.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed || B.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed) { return false; }
        Test->TestTrue(TEXT("Distinct instances for same map"), A.StreamingLevel != B.StreamingLevel);
        Test->TestTrue(TEXT("Placement reaches actual streaming instance"), B.StreamingLevel->LevelTransform.GetLocation().Equals(FVector(10000, 2000, 300)));
        Test->TestTrue(TEXT("Rotation reaches streaming instance"), B.StreamingLevel->LevelTransform.Rotator().Equals(FRotator(0,45,0)));
        Test->TestEqual(TEXT("All children reach unified 100 percent"), System->GetCurrentPayload().Progress, 1.f);
        Test->TestTrue(TEXT("Still waits for explicit notification"), System->IsTransitionInProgress());
        System->NotifyMapLoadCompleted(TEXT("BatchReady"));
        bNotified = true; return false;
    }
private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UMTS_MapTransitionSubsystem> System;
    double Start;
    bool bCleaning = false;
    bool bNotified = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMTSBatchTest, "MapTransitionSystem.SubMaps.BatchConfigs", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMTSBatchTest::RunTest(const FString& Parameters)
{
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
        auto* System = NewObject<UMTS_MapTransitionSubsystem>(Context.World()->GetGameInstance());
        System->bTransitionInProgress = true;
        System->ActiveRequest.bRequireManualMapReadyNotification = true;
        System->ActiveRequest.AutomaticProgressMax = .3f;
        System->ActiveRequest.ProgressSmoothSpeed = 0;
        System->ActiveRequest.LoadingCompletionDelaySeconds = 0;
        System->CurrentPayload.Phase = EMTS_MapTransitionPhase::WaitingForMapReady;
        FMTS_SubMapLoadConfig A; A.MapID=TEXT("BatchA"); A.Weight=2;
        A.MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
        FText Error;
        TestFalse(TEXT("Duplicate IDs reject entire batch"), System->LoadSubMaps({A,A},Error));
        TestFalse(TEXT("Invalid batch leaves no progress plan"), System->HasSubMapProgressPlan());
        FMTS_SubMapInfo Info;
        TestFalse(TEXT("Invalid batch loads nothing"), Context.World()->GetSubsystem<UMTS_SubMapSubsystem>()->GetSubMapByKey(A.MapID,Info));
        auto B=A; B.MapID=TEXT("BatchB"); B.Weight=0;
        TestFalse(TEXT("Zero weight rejected"), System->LoadSubMaps({A,B},Error));
        B.Weight=1; B.Location=FVector(10000,2000,300); B.Rotation=FRotator(0,45,0);
        if (!TestTrue(TEXT("Valid batch accepted"), System->LoadSubMaps({A,B},Error))) { return false; }
        TestEqual(TEXT("Weight A retained"), System->SubMapWeights.FindRef(A.MapID),2.f);
        TestEqual(TEXT("Weight B retained"), System->SubMapWeights.FindRef(B.MapID),1.f);
        TestFalse(TEXT("Concurrent batch rejected"), System->LoadSubMaps({A,B},Error));
        ADD_LATENT_AUTOMATION_COMMAND(FMTSWaitBatch(this,System)); return true;
    }
    AddError(TEXT("Requires game world")); return false;
}
#endif
