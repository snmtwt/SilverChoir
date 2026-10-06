#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "SubSystem/GameMapTransitionSystem/GameMainMapSubMapHandler.h"
#include "SubSystem/GameMapTransitionSystem/GameMapSubMapHandler.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "SMS_SceneLibrary.h"
#include "MTS_SubMapHandler.h"
#include "MTS_MapLoadingWidget.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_SubMapSubsystem.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_CameraConfigDataAsset.h"

namespace OperationsSubMapEntry
{
class FFlow : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TWeakObjectPtr<UGameInstance> GI;
    TWeakObjectPtr<ULevelStreamingDynamic> Base,FirstTile;
    TStrongObjectPtr<UMTS_SubMapHandler> Handler;
    FGuid Squad;int32 Stage=0;double Start=FPlatformTime::Seconds();float MinPitch=0,MaxPitch=0;
    FVector LocationBeforeUnload;
    TWeakObjectPtr<AFCS_FreeCameraPawn> TestCamera;
    bool bOriginalEdgeScroll = false;
    FGameplayTag RoomTag=FGameplayTag::RequestGameplayTag(TEXT("GameScene.OperationsCommandRoom"));
public:
    FFlow(FAutomationTestBase* T,UGameInstance* Instance):Test(T),GI(Instance){}
    ~FFlow() { if(TestCamera.IsValid())TestCamera->SetEdgeScrollEnabled(bOriginalEdgeScroll); }
    bool Update() override
    {
        auto* W=GI.IsValid()?GI->GetWorld():nullptr;if(!W)return false;
        if(auto* Camera=UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(W))
        {
            if(!TestCamera.IsValid())
            {
                TestCamera=Camera;
                bOriginalEdgeScroll=Camera->GetCameraConfig() && Camera->GetCameraConfig()->bEnableEdgeScroll;
            }
            // Offscreen runs must not turn the user's desktop cursor into camera movement.
            Camera->SetEdgeScrollEnabled(false);
        }
        auto* PC=Cast<AGameMainMapPlayerController>(W->GetFirstPlayerController());auto* State=W->GetGameState<AGameMainMapGameState>();
        auto* Mode=W->GetAuthGameMode<AGameMainMapGameMode>();auto* S=W->GetSubsystem<UMTS_SubMapSubsystem>();auto* Scenes=USMS_SceneLibrary::GetSceneManager(W);
        if(FPlatformTime::Seconds()-Start>150){Test->AddError(FString::Printf(TEXT("Entry timeout stage=%d map=%s busy=%d"),Stage,State?*State->ActiveMapID.ToString():TEXT("none"),S->IsSubMapTransitionInProgress()));UGameplayStatics::SetGamePaused(W,false);return true;}
        if(!PC || !State || !Mode || !Scenes)return false;
        FMTS_SubMapInfo B,T,Old;S->GetSubMapByKey(TEXT("Base"),B);S->GetSubMapByKey(TEXT("T5"),T);
        FText Error;
        switch(Stage)
        {
        case 0:
        {
            if(!State->IsBaseMap() || GI->GetSubsystem<UMTS_MapTransitionSubsystem>()->IsTransitionInProgress() || Scenes->GetTransitionState()!=ESMS_SceneTransitionState::Idle)return false;
            const FTransform BeforeRejectedEntry=PC->GetPawn()->GetActorTransform();
            Test->AddExpectedError(TEXT("ActivateLoadedMap: MapType must be Base or Battle"),EAutomationExpectedErrorFlags::Contains,2);
            Test->TestFalse(TEXT("Omitting MapType produces an error even for the known Base ID"),Mode->ActivateLoadedMap(TEXT("Base"),FTransform::Identity));
            Test->TestFalse(TEXT("Invalid enum values produce the same explicit-type error"),Mode->ActivateLoadedMap(TEXT("Base"),FTransform::Identity,static_cast<EGameMainMapType>(255)));
            Test->TestFalse(TEXT("Activation error is available to Blueprint"),State->LastError.IsEmpty());
            Test->TestTrue(TEXT("Rejected activation keeps the active ID, mode, input guard and player transform"),State->ActiveMapID==TEXT("Base") && State->IsBaseMap() && !State->bSwitchingMap && PC->GetPawn()->GetActorTransform().Equals(BeforeRejectedEntry));
            Base=B.StreamingLevel;auto* Camera=UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(W);Camera->GetPitchLimits(MinPitch,MaxPitch);
            Test->TestTrue(TEXT("Create resident squad for real entry button"),UPlayerSquadLibrary::GetPlayerSquadManager(W)->CreateSquad(FText::FromString(TEXT("T5 entry fixture")),nullptr,TEXT("T5"),Squad,Error));
            // Load a previous region with the same classification used by the Blueprint query.
            auto* Asset=NewObject<UMTS_SubMapDataAsset>();Asset->MapID=TEXT("EntryOldRegion");Asset->MapName=Asset->MapID;
            Asset->MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));Asset->MapTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Map.Region.Tile")));
            Test->TestTrue(TEXT("Load previous tile fixture"),S->LoadSubMap(Asset,FVector(200000,0,0),FRotator::ZeroRotator,Error));
            Test->TestTrue(TEXT("Enter operations room"),Scenes->SwitchScene(RoomTag));Stage=1;return false;
        }
        case 1:
        {
            if(Scenes->GetCurrentSceneTag()!=RoomTag || Scenes->GetTransitionState()!=ESMS_SceneTransitionState::Idle || !PC->BaseWidget)return false;
            auto* Room=Cast<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI());if(!Room || !Room->IsSceneUILoaded())return false;
            if(!S->GetSubMapByKey(TEXT("EntryOldRegion"),Old) || Old.LoadingPayload.Phase!=EMTS_MapTransitionPhase::Completed)return false;
            Test->TestEqual(TEXT("Default T5 sees resident squad"),Room->GetSelectedTileSquadCount(),1);
            Test->TestTrue(TEXT("Pause from actual room"),Room->PauseTime());
            Test->TestTrue(TEXT("Request real Blueprint entry while paused"),Room->RequestEnterSelectedTile());
            Handler.Reset(S->GetSubMapHandler(TEXT("T5")));
            if(!Test->TestNotNull(TEXT("T5 object registered"),Handler.Get()))return true;
            Test->TestEqual(TEXT("Actual Blueprint handler"),Handler->GetClass()->GetName(),FString(TEXT("BP_SubMapHandler_T5_C")));
            Test->TestNotNull(TEXT("T5 uses reusable native handler parent"),Cast<UGameMainMapSubMapHandler>(Handler.Get()));
            Test->TestNotNull(TEXT("T5 lifecycle is migrated into game-map native handler"),Cast<UGameMapSubMapHandler>(Handler.Get()));
            const auto* Property=FindFProperty<FArrayProperty>(Handler->GetClass(),TEXT("SquadIds"));
            Test->TestTrue(TEXT("Squad IDs assigned before load"),Property && *Property->ContainerPtrToValuePtr<TArray<FGuid>>(Handler.Get())==TArray<FGuid>{Squad});
            auto* Loading=S->GetSubMapLoadingWidget(TEXT("T5"));
            if(Test->TestNotNull(TEXT("Actual loading UI present"),Loading))Test->TestEqual(TEXT("Reuses requested loading widget"),Loading->GetClass()->GetName(),FString(TEXT("WBP_MenuLoading_C")));
            auto* Query=PC->FindFunction(TEXT("CommandMap_IsRoomReady"));FStructOnScope Params(Query);PC->ProcessEvent(Query,Params.GetStructMemory());
            Test->TestFalse(TEXT("Loading disables sandbox operations"),FindFProperty<FBoolProperty>(Query,TEXT("ReturnValue"))->GetPropertyValue_InContainer(Params.GetStructMemory()));
            Stage=2;return false;
        }
        case 2:
        {
            if(S->IsSubMapTransitionInProgress() || State->ActiveMapID!=TEXT("T5") || !State->IsBattleMap())return false;
            Test->TestFalse(TEXT("Old tile removed"),S->GetSubMapByKey(TEXT("EntryOldRegion"),Old));
            Test->TestTrue(TEXT("Base is the original loaded visible instance"),B.StreamingLevel==Base.Get() && B.bIsVisible);
            Test->TestTrue(TEXT("Same handler retained"),S->GetSubMapHandler(TEXT("T5"))==Handler.Get());
            Test->TestEqual(TEXT("Initialization completed before activation"),T.LoadingPayload.Phase,EMTS_MapTransitionPhase::Completed);
            Test->TestFalse(TEXT("Room-owned simulation pause released"),UGameplayStatics::IsGamePaused(W));
            float PitchA=0,PitchB=0;UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(W)->GetPitchLimits(PitchA,PitchB);
            Test->TestEqual(TEXT("Temporary room pitch limit restored"),PitchA,MinPitch);
            Test->TestNotNull(TEXT("Battle UI switched"),PC->BattleWidget.Get());Test->TestNull(TEXT("Base UI removed after exit"),PC->BaseWidget.Get());
            Test->TestTrue(TEXT("Native T5 lifecycle initializes the actual squad roster"),PC->BattleWidget && PC->BattleWidget->bRosterInitialized && !PC->BattleWidget->bUsingTestData && PC->BattleWidget->SelectedSquadId==Squad);
            if (auto* GameHandler=Cast<UGameMapSubMapHandler>(Handler.Get()))
                Test->TestFalse(TEXT("Authored T5 squad spawner registered through migrated Blueprint call"),GameHandler->GetRegisteredSquadSpawners().IsEmpty());
            auto* EntryProperty=FindFProperty<FStructProperty>(Handler->GetClass(),TEXT("LocalEntryTransform"));
            FTransform ExpectedEntry;
            Test->TestTrue(TEXT("Entry helper uses authored configuration"),EntryProperty && Handler->GetWorldTransform(*EntryProperty->ContainerPtrToValuePtr<FTransform>(Handler.Get()),ExpectedEntry));
            Test->TestTrue(FString::Printf(TEXT("Player uses configured entry: actual=%s expected=%s"),*PC->GetPawn()->GetActorLocation().ToString(),*ExpectedEntry.GetLocation().ToString()),PC->GetPawn()->GetActorLocation().Equals(ExpectedEntry.GetLocation(),5));
            Test->TestFalse(TEXT("Scene subscription removed after successful entry"),CastChecked<UGameMainMapSubMapHandler>(Handler.Get())->IsWaitingForEntryScene());
            Test->TestTrue(TEXT("Explicit successful activation clears the earlier validation error"),State->LastError.IsEmpty());
            FirstTile=T.StreamingLevel;
            // Test fixture repositioning only; no production return-to-base workflow is installed.
            Test->TestTrue(TEXT("Explicitly activate the loaded Base fixture for resident revisit"),Mode->ActivateLoadedMap(TEXT("Base"),B.Transform,EGameMainMapType::Base));
            Test->TestTrue(TEXT("Reenter room"),Scenes->SwitchScene(RoomTag));Stage=3;return false;
        }
        case 3:
        {
            if(Scenes->GetCurrentSceneTag()!=RoomTag || Scenes->GetTransitionState()!=ESMS_SceneTransitionState::Idle || !PC->BaseWidget)return false;
            auto* Room=Cast<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI());if(!Room || !Room->IsSceneUILoaded())return false;
            Test->TestTrue(TEXT("Revisit T5 using existing handler"),Room->RequestEnterSelectedTile());Stage=4;return false;
        }
        case 4:
            if(!State->IsBattleMap() || State->ActiveMapID!=TEXT("T5"))return false;
            Test->TestTrue(TEXT("Revisit does not duplicate streamed level"),T.StreamingLevel==FirstTile.Get());
            Test->TestTrue(TEXT("Revisit keeps handler object"),S->GetSubMapHandler(TEXT("T5"))==Handler.Get());
            Test->TestTrue(TEXT("Resident revisit initializes the newly recreated battle UI roster"),PC->BattleWidget && PC->BattleWidget->bRosterInitialized && PC->BattleWidget->SelectedSquadId==Squad);
            UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(W)->SetEdgeScrollEnabled(false); // Ignore desktop cursor position in the offscreen fixture.
            LocationBeforeUnload=PC->GetPawn()->GetActorLocation();
            Test->TestTrue(TEXT("Unload through handler"),Handler->UnloadMap());Stage=5;return false;
        case 5:
        {
            if(S->GetSubMapByKey(TEXT("T5"),T))return false;
            Test->TestTrue(TEXT("Plugin unload does not implicitly activate Base"),State->IsBattleMap() && State->ActiveMapID==TEXT("T5"));
            Test->TestTrue(TEXT("Plugin unload does not teleport the player"),PC->GetPawn()->GetActorLocation().Equals(LocationBeforeUnload,1));
            Test->TestTrue(TEXT("Base never reloaded"),B.StreamingLevel==Base.Get());
            Test->TestNull(TEXT("Unloaded handler removed by ID"),S->GetSubMapHandler(TEXT("T5")));
            Test->TestNull(TEXT("Unloading no longer recreates Base UI"),PC->BaseWidget.Get());
            Test->TestNotNull(TEXT("UI switching remains explicitly owned by activation"),PC->BattleWidget.Get());
            Test->TestFalse(TEXT("No residual input lock"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
            Test->TestTrue(TEXT("Restore Base fixture explicitly for cancellation checks"),Mode->ActivateLoadedMap(TEXT("Base"),B.Transform,EGameMainMapType::Base));
            FMTS_SubMapLoadRequest Request;
            Request.MapAsset=NewObject<UMTS_SubMapDataAsset>();Request.MapAsset->MapID=TEXT("CancelWaitRegion");Request.MapAsset->MapName=TEXT("CancelWaitRegion");
            Request.MapAsset->MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
            Handler.Reset(S->CreateSubMapHandler(UGameMainMapSubMapHandler::StaticClass()));
            Test->TestTrue(TEXT("Load native handler cancellation fixture"),S->LoadSubMapByHandlerObject(Request,Handler.Get(),{},Error));
            Stage=6;return false;
        }
        case 6:
        {
            FMTS_SubMapInfo Pending;if(!Handler->GetSubMapInfo(Pending) || Pending.State!=EMTS_SubMapState::Loaded)return false;
            auto* Native=CastChecked<UGameMainMapSubMapHandler>(Handler.Get());
            Test->TestFalse(TEXT("Invalid scene tag rejected"),Native->WaitForEntryScene(FGameplayTag(),Error));
            Test->TestFalse(TEXT("Invalid scene exposes a useful error"),Error.IsEmpty());
            Test->TestTrue(TEXT("Start an asynchronous scene wait"),Native->WaitForEntryScene(RoomTag,Error));
            Test->TestTrue(TEXT("Native parent owns pending subscription"),Native->IsWaitingForEntryScene());
            Test->TestTrue(TEXT("Repeated same scene wait is idempotent"),Native->WaitForEntryScene(RoomTag,Error));
            Test->TestFalse(TEXT("Competing scene wait rejected"),Native->WaitForEntryScene(FGameplayTag::RequestGameplayTag(TEXT("GameScene.BaseOverview")),Error));
            Test->TestTrue(TEXT("Cancel handler while scene is still moving"),Native->UnloadMap());
            Test->TestFalse(TEXT("Native cleanup unbinds immediately, no Blueprint parent call needed"),Native->IsWaitingForEntryScene());
            Stage=7;return false;
        }
        case 7:
            if(S->GetSubMapByKey(TEXT("CancelWaitRegion"),T) || Scenes->GetTransitionState()!=ESMS_SceneTransitionState::Idle)return false;
            Test->TestTrue(TEXT("Late scene notification cannot activate cancelled region"),State->IsBaseMap() && State->ActiveMapID==TEXT("Base"));
            Test->TestFalse(TEXT("Stale handler activation rejected"),CastChecked<UGameMainMapSubMapHandler>(Handler.Get())->ActivateThisMap(FTransform::Identity,EGameMainMapType::Battle,Error));
            Test->TestTrue(TEXT("Restore overview after cancellation fixture"),Scenes->SwitchScene(FGameplayTag::RequestGameplayTag(TEXT("GameScene.BaseOverview"))));
            Stage=8;return false;
        default:
            if(Scenes->GetTransitionState()!=ESMS_SceneTransitionState::Idle)return false;
            Test->AddInfo(TEXT("OPERATIONS_SUBMAP_ENTRY_OK explicit map type rejection, Handler loading, paused entry, old region replacement, base retained, resident revisit, plugin unload without automatic return, scene wait cancellation"));return true;
        }
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOperationsSubMapEntryTest,"SilverChoir.BaseUI.OperationsCommandRoom.SubMapEntry.RuntimeFlow",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FOperationsSubMapEntryTest::RunTest(const FString&)
{
    for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game && C.World())
        if(auto* Menu=Cast<AMainMenuPlayerController>(C.World()->GetFirstPlayerController()))
        {auto* GI=C.World()->GetGameInstance();Menu->HandleMenuAction(EMainMenuAction::NewGame);ADD_LATENT_AUTOMATION_COMMAND(OperationsSubMapEntry::FFlow(this,GI));return true;}
    AddError(TEXT("Run alone from MainMenu -game"));return false;
}
#endif
