#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MTS_SubMapBlueprintLibrary.h"
#include "MTS_SubMapTestHandler.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MTS_RegionTest,"MTS.Automation.Region");
namespace MTSOperationTests
{
UMTS_SubMapDataAsset* Asset(FName ID,bool Region=true)
{
    auto* A=NewObject<UMTS_SubMapDataAsset>();A->MapID=ID;A->MapName=ID;
    A->MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
    if(Region)A->MapTags.AddTag(TAG_MTS_RegionTest);return A;
}
class FOperation : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ULevelStreamingDynamic> Base;
    TStrongObjectPtr<UMTS_SubMapTestHandler> Handler;
    double Started=FPlatformTime::Seconds(),StepStarted=Started;
    int32 Phase=0;
    bool bOriginalLook=false;
public:
    FOperation(FAutomationTestBase* T,UWorld* W):Test(T),World(W){}
    bool Update() override
    {
        if(!World.IsValid()){Test->AddError(TEXT("World disappeared"));return true;}
        auto* S=World->GetSubsystem<UMTS_SubMapSubsystem>();auto* PC=World->GetFirstPlayerController();
        if(FPlatformTime::Seconds()-Started>60)
        {UGameplayStatics::SetGamePaused(World.Get(),false);Test->AddError(FString::Printf(TEXT("Operation timed out at %d"),Phase));return true;}
        FText Error;FMTS_SubMapInfo BaseInfo,Old,Target;
        S->GetSubMapByKey(TEXT("PersistentBase"),BaseInfo);
        if(Phase>0 && Phase<6)
        {
            if(!Test->TestTrue(TEXT("Base instance survives every replacement"),BaseInfo.StreamingLevel==Base.Get() && BaseInfo.State==EMTS_SubMapState::Loaded))return true;
        }
        switch(Phase)
        {
        case 0:
        {
            if(BaseInfo.LoadingPayload.Phase!=EMTS_MapTransitionPhase::Completed || !S->GetSubMapByKey(TEXT("OldRegion"),Old) || Old.LoadingPayload.Phase!=EMTS_MapTransitionPhase::Completed)return false;
            Base=BaseInfo.StreamingLevel;
            Test->TestEqual(TEXT("Tag query excludes base"),S->GetSubMapIDsByTag(TAG_MTS_RegionTest),TArray<FName>{TEXT("OldRegion")});
            Handler.Reset(CastChecked<UMTS_SubMapTestHandler>(S->CreateSubMapHandler(UMTS_SubMapTestHandler::StaticClass())));
            Handler->MustBeUnloadedBeforePreload=TEXT("OldRegion");
            FMTS_SubMapLoadRequest Request;Request.MapAsset=Asset(TEXT("NewRegion"));Request.LoadingWidgetClass=UMTS_SubMapTestWidget::StaticClass();Request.ProgressSmoothSpeed=10;Request.CompletionDelay=.05f;
            Request.Location=FVector(110,200,30);Request.Rotation=FRotator(0,90,0);
            Test->TestFalse(TEXT("Unknown unload ID rejected before old region touched"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),{TEXT("Missing")},Error));
            S->GetSubMapByKey(TEXT("OldRegion"),Old);Test->TestEqual(TEXT("Rejected request preserves old region"),Old.State,EMTS_SubMapState::Loaded);
            Test->TestTrue(TEXT("Rejected request leaves handler reusable"),Handler->GetMapID().IsNone());
            bOriginalLook=PC->IsLookInputIgnored();PC->SetIgnoreLookInput(true);
            Test->TestTrue(TEXT("Object handler replacement accepted"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),S->GetSubMapIDsByTag(TAG_MTS_RegionTest),Error));
            Test->TestEqual(TEXT("Handler gets ID before streaming"),Handler->GetMapID(),FName(TEXT("NewRegion")));
            Test->TestEqual(TEXT("No preload before old region removal"),Handler->PreloadCalls,0);
            Test->TestTrue(TEXT("UI blocks input"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
            Test->TestNotNull(TEXT("Loading widget created immediately"),S->GetSubMapLoadingWidget(TEXT("NewRegion")));
            Test->TestTrue(TEXT("Foreground request is busy"),S->IsSubMapTransitionInProgress());
            Test->TestFalse(TEXT("Duplicate ID cannot replace a running request"),S->LoadSubMapByHandlerObject(Request,S->CreateSubMapHandler(UMTS_SubMapTestHandler::StaticClass()),{},Error));
            Request.MapAsset=Asset(TEXT("OtherRegion"));
            Test->TestFalse(TEXT("Concurrent foreground request rejected"),S->LoadSubMapByHandlerObject(Request,S->CreateSubMapHandler(UMTS_SubMapTestHandler::StaticClass()),{},Error));
            Phase=1;return false;
        }
        case 1:
            if(Handler->LoadedCalls==0)return false;
            Test->TestTrue(TEXT("Old region fully removed before preload"),Handler->bPreviousMapWasRemoved);
            Test->TestEqual(TEXT("One preload"),Handler->PreloadCalls,1);
            Test->TestTrue(TEXT("Supplied handler is used unchanged"),S->GetSubMapHandler(TEXT("NewRegion"))==Handler.Get());
            Test->TestFalse(TEXT("Visible map is not yet initialized"),Handler->IsSubMapReady());
            {
                FTransform WorldEntry;
                Test->TestTrue(TEXT("Native world entry helper resolves loaded instance"),Handler->GetWorldTransform(FTransform(FVector(10,0,5)),WorldEntry));
                Test->TestTrue(TEXT("World entry composes rotation and translation"),WorldEntry.GetLocation().Equals(FVector(110,210,35),.01));
            }
            Test->TestTrue(TEXT("Manual 100 percent accepted"),Handler->UpdateLoadingProgress(1,FText::FromString(TEXT("waiting"))));
            S->Tick(0);
            Test->TestEqual(TEXT("100 percent alone does not signal ready"),Handler->ReadyCalls,0);
            Test->TestNotNull(TEXT("Manual initialization retains the screen"),S->GetSubMapLoadingWidget(TEXT("NewRegion")));
            UGameplayStatics::SetGamePaused(World.Get(),true);
            Test->TestTrue(TEXT("Handler explicitly finishes"),Handler->FinishLoading());
            Phase=2;return false;
        case 2:
            if(S->IsSubMapTransitionInProgress())return false;
            Test->TestTrue(TEXT("Presentation closes while world is paused"),UGameplayStatics::IsGamePaused(World.Get()));
            UGameplayStatics::SetGamePaused(World.Get(),false);
            Test->TestNull(TEXT("Loading widget reference released"),S->GetSubMapLoadingWidget(TEXT("NewRegion")));
            Test->TestFalse(TEXT("Owned move-input lock released"),PC->IsMoveInputIgnored());
            Test->TestTrue(TEXT("Preexisting look-input lock retained"),PC->IsLookInputIgnored());PC->SetIgnoreLookInput(false);
            Test->TestEqual(TEXT("Look lock restored to original state"),PC->IsLookInputIgnored(),bOriginalLook);
            Test->TestEqual(TEXT("Ready event once"),Handler->ReadyCalls,1);
            Test->TestTrue(TEXT("Native readiness helper sees completed instance"),Handler->IsSubMapReady());
            Test->TestEqual(TEXT("Native resources remain during residency"),Handler->NativeCleanupCalls,0);
            Test->TestFalse(TEXT("Duplicate ready rejected"),Handler->FinishLoading());
            {
                TWeakObjectPtr<UMTS_SubMapTestHandler> Weak=Handler.Get();Handler.Reset();CollectGarbage(RF_NoFlags);
                Test->TestTrue(TEXT("Subsystem owns completed handler through GC"),Weak.IsValid() && S->GetSubMapHandler(TEXT("NewRegion"))==Weak.Get());Handler.Reset(Weak.Get());
            }
            Test->TestTrue(TEXT("Handler can unload itself"),Handler->UnloadMap());
            Test->TestEqual(TEXT("Unloading event once"),Handler->UnloadingCalls,1);
            Test->TestTrue(TEXT("Native cleanup precedes unload event without a parent call"),Handler->bNativeCleanupBeforeUnload);
            Test->TestTrue(TEXT("Handler queryable until removal"),S->GetSubMapHandler(TEXT("NewRegion"))==Handler.Get());
            Phase=3;return false;
        case 3:
            if(S->GetSubMapByKey(TEXT("NewRegion"),Target))return false;
            Test->TestEqual(TEXT("Unloaded event once"),Handler->UnloadedCalls,1);
            Test->TestTrue(TEXT("Handler query works inside final cleanup event"),Handler->bQueryableDuringUnload);
            Test->TestNull(TEXT("Handler released from registry after unload"),S->GetSubMapHandler(TEXT("NewRegion")));
            Test->TestFalse(TEXT("Detached handler info lookup fails"),Handler->GetSubMapInfo(Target));
            Test->TestTrue(TEXT("Failed info lookup clears output"),Target.MapID.IsNone());
            Test->TestEqual(TEXT("Native cleanup runs exactly once across unload stages"),Handler->NativeCleanupCalls,1);
            Test->TestFalse(TEXT("Detached handler cannot affect a new load"),Handler->UnloadMap());
            {
                FMTS_SubMapLoadRequest Request;Request.MapAsset=Asset(TEXT("NewRegion"));Request.LoadingWidgetClass=UMTS_SubMapTestWidget::StaticClass();
                Test->TestFalse(TEXT("Used handler cannot be registered again"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),{},Error));
                Handler.Reset(CastChecked<UMTS_SubMapTestHandler>(S->CreateSubMapHandler(UMTS_SubMapTestHandler::StaticClass())));
                Test->TestTrue(TEXT("Fresh handler can reuse unloaded ID"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),{},Error));
                Test->TestTrue(TEXT("Cancel new load"),Handler->FailLoading(FText::FromString(TEXT("test initialization failure"))));
                Test->TestEqual(TEXT("Failure callback once"),Handler->FailureCalls,1);
                Test->TestTrue(TEXT("Native cleanup precedes failure callback"),Handler->bNativeCleanupBeforeFailure);
                Test->TestEqual(TEXT("Failure is available without Blueprint storage"),Handler->GetLastHandlerError().ToString(),FString(TEXT("test initialization failure")));
                Test->TestFalse(TEXT("Failure immediately clears input blocker"),S->IsSubMapTransitionInProgress());
                Test->TestNull(TEXT("Failure releases presentation"),S->GetSubMapLoadingWidget(TEXT("NewRegion")));
            }
            Phase=4;return false;
        case 4:
            if(S->GetSubMapByKey(TEXT("NewRegion"),Target))return false;
            Test->TestEqual(TEXT("Cancellation completes unload cleanup once"),Handler->UnloadedCalls,1);
            Test->TestEqual(TEXT("No duplicate failure"),Handler->FailureCalls,1);
            {
                FMTS_SubMapLoadRequest Request;Request.MapAsset=Asset(TEXT("AutoRegion"));Request.bRequireManualReady=false;Request.AutomaticProgressMax=.3f;
                Handler.Reset(CastChecked<UMTS_SubMapTestHandler>(S->CreateSubMapHandler(UMTS_SubMapTestHandler::StaticClass())));
                Test->TestTrue(TEXT("Automatic request with reserved progress accepted"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),{},Error));
            }
            Phase=5;return false;
        case 5:
            if(S->IsSubMapTransitionInProgress() || Handler->ReadyCalls==0)return false;
            Test->TestEqual(TEXT("Explicit automatic mode completes regardless of visual cap"),Handler->ReadyCalls,1);
            Handler->UnloadMap();S->UnloadSubMapByID(TEXT("PersistentBase"));Phase=6;return false;
        default:
            if(!S->GetSubMaps().IsEmpty())return false;
            Test->AddInfo(TEXT("SUBMAP_OBJECT_LIFECYCLE_OK replacement, base retained, explicit ready, paused presentation, GC retention, unload and cancellation"));return true;
        }
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMTSObjectLifecycleTest,"MapTransitionSystem.SubMaps.ObjectHandlerLifecycle",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FMTSObjectLifecycleTest::RunTest(const FString&)
{
    UWorld* W=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game){W=C.World();break;}
    if(!TestNotNull(TEXT("Game world"),W))return false;
    auto* S=W->GetSubsystem<UMTS_SubMapSubsystem>();if(!TestTrue(TEXT("Empty initial registry"),S->GetSubMaps().IsEmpty()))return false;
    FText Error;TestTrue(TEXT("Load persistent base"),S->LoadSubMap(MTSOperationTests::Asset(TEXT("PersistentBase"),false),FVector::ZeroVector,FRotator::ZeroRotator,Error));
    TestTrue(TEXT("Load previous tile region"),S->LoadSubMap(MTSOperationTests::Asset(TEXT("OldRegion")),FVector(10000,0,0),FRotator::ZeroRotator,Error));
    ADD_LATENT_AUTOMATION_COMMAND(MTSOperationTests::FOperation(this,W));return true;
}
#endif
