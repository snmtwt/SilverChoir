#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "H5UI_View.h"
#include "Engine/Texture2D.h"

class FBaseShellScreenshot : public IAutomationLatentCommand
{
    TWeakObjectPtr<UBaseMapWidget> Shell;
    double Start=FPlatformTime::Seconds();
    bool bCaptured=false;
    bool bCapturedSecond=false;
    FAutomationTestBase* Test;
public:
    FBaseShellScreenshot(UBaseMapWidget* Widget, FAutomationTestBase* InTest):Shell(Widget),Test(InTest){}
    bool Update() override
    {
        if (!Shell.IsValid()) return true;
        const double Elapsed=FPlatformTime::Seconds()-Start;
        if (Elapsed>3 && !bCaptured)
        {
            FString Tag=TEXT("BaseShell");
            FParse::Value(FCommandLine::Get(),TEXT("BaseUICaptureTag="),Tag);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/BaseUI")/(FPaths::MakeValidFileName(Tag)+TEXT(".png")),true,false);
            bCaptured=true;
            for (const FName Name : {FName(TEXT("HeaderEffects")),FName(TEXT("FooterEffects"))})
            {
                auto* View=Cast<UH5UI_View>(Shell->GetWidgetFromName(Name));
                if (Test->TestNotNull(TEXT("Native H5 decoration"),View))
                {
                    const auto Stats=View->GetPerformanceStats();
                    Test->TestTrue(TEXT("H5 decoration submits geometry"),Stats.DrawBatches>0);
                    Test->TestTrue(TEXT("H5 view covers only the small bar"),Stats.ViewSize.Y<=100);
                    Test->TestFalse(TEXT("No CEF needed"),View->bEnableBrowserSubviews);
                }
            }
        }
        if (Elapsed>5 && !bCapturedSecond)
        {
            FString Tag=TEXT("BaseShell");
            FParse::Value(FCommandLine::Get(),TEXT("BaseUICaptureTag="),Tag);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/BaseUI")/(FPaths::MakeValidFileName(Tag)+TEXT("_later.png")),true,false);
            bCapturedSecond=true;
        }
        if (Elapsed>6) { Shell->RemoveFromParent(); return true; }
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseUIShellTest,"SilverChoir.BaseUI.Shell",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FBaseUIShellTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (const auto& Context:GEngine->GetWorldContexts())
        if (Context.WorldType==EWorldType::Game) { World=Context.World(); break; }
    if (!TestNotNull(TEXT("Game world"),World)) return false;
    auto* PC=World->GetFirstPlayerController();
    auto* Class=LoadClass<UBaseMapWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C"));
    if (!TestNotNull(TEXT("Shell class"),Class) || !TestNotNull(TEXT("Player"),PC)) return false;
    auto* Shell=CreateWidget<UBaseMapWidget>(PC,Class);
    Shell->SetCurrencyAmount(128450); // Exercise data updates before widget construction.
    Shell->AddToViewport(10000);
    if (!TestNotNull(TEXT("Scene content binding"),Shell->SceneContent.Get())) return false;
    const TCHAR* Rooms[]={TEXT("WBP_SquadMeetingRoom"),TEXT("WBP_CommanderOffice"),TEXT("WBP_OperationsCommandRoom"),TEXT("WBP_PersonnelPreparationRoom")};
    UBaseSceneWidget* Previous=nullptr;
    for (const TCHAR* Name:Rooms)
    {
        UClass* RoomClass=LoadClass<UBaseSceneWidget>(nullptr,*(FString(TEXT("/Game/System/Map/BaseMap/UI/SceneUI/"))+FString(Name).RightChop(4)+TEXT("/")+Name+TEXT(".")+Name+TEXT("_C")));
        if (!TestNotNull(TEXT("Room class"),RoomClass)) return false;
        const FGameplayTag Tag=RoomClass->GetDefaultObject<UBaseSceneWidget>()->SceneTag;
        UBaseSceneWidget* Current=Shell->CreateSceneUIByTag(Tag);
        if (!TestNotNull(TEXT("Room instance"),Current)) return false;
        if (Previous && Shell->GetCurrentSceneUI()==Previous)
        {
            TestTrue(TEXT("Exit animation keeps old room current"),Shell->GetCurrentSceneUI()==Previous);
            TestNotNull(TEXT("Exit animation keeps old room mounted"),Previous->GetParent());
            TestNull(TEXT("Replacement waits offscreen"),Current->GetParent());
            TestTrue(TEXT("Repeated pending request reuses next room"),Shell->CreateSceneUIByTag(Tag)==Current);
            Previous->NotifyLoadCompleted(); // A late entry animation must not interrupt unloading.
            Previous->NotifyUnloadCompleted();
            Previous->NotifyUnloadCompleted(); // Completion must be idempotent.
        }
        else if (Previous)
        {
            // Rooms without exit animation may finish their unload synchronously.
            TestTrue(TEXT("Synchronous exit mounts the requested replacement"),Shell->GetCurrentSceneUI()==Current);
            TestNull(TEXT("Synchronous exit detaches the old room"),Previous->GetParent());
            TestNull(TEXT("Synchronous exit releases the old host"),Previous->BaseUI.Get());
        }
        Current->NotifyUnloadCompleted(); // Cannot unload before an unload request.
        TestTrue(TEXT("Premature unload notification ignored"),Shell->GetCurrentSceneUI()==Current);
        Current->NotifyLoadCompleted();
        Current->NotifyLoadCompleted();
        TestTrue(TEXT("Scene tag configured"),Current->SceneTag.IsValid());
        TestTrue(TEXT("Room owns shell reference"),Current->BaseUI==Shell);
        TestTrue(TEXT("Getter exposes current scene"),Shell->GetCurrentSceneUI()==Current);
        TestNull(TEXT("Invalid tag rejected"),Shell->CreateSceneUIByTag(FGameplayTag()));
        TestTrue(TEXT("Invalid tag preserves current UI"),Shell->GetCurrentSceneUI()==Current);
        TestEqual(TEXT("Only one room is mounted"),Shell->SceneContent->GetChildrenCount(),1);
        TestTrue(TEXT("Same class reuses instance"),Shell->ShowSceneUI(RoomClass)==Current);
        TestTrue(TEXT("Same tag reuses instance"),Shell->CreateSceneUIByTag(Tag)==Current);
        if (Previous) { TestNull(TEXT("Old room released shell"),Previous->BaseUI.Get()); TestNull(TEXT("Old room detached"),Previous->GetParent()); }
        Previous=Current;
    }
    Shell->UnloadCurrentSceneUI();
    Shell->UnloadCurrentSceneUI(); // Repeated teardown is safe.
    TestTrue(TEXT("Unload waits for manual notification"),Shell->CurrentSceneUI==Previous);
    Previous->NotifyUnloadCompleted();
    TestNull(TEXT("Clear releases current room"),Shell->CurrentSceneUI.Get());
    TestEqual(TEXT("Clear empties content"),Shell->SceneContent->GetChildrenCount(),0);
    auto* Amount=Cast<UTextBlock>(Shell->GetWidgetFromName(TEXT("CurrencyText")));
    if (TestNotNull(TEXT("Currency text binding"),Amount))
    {
        TestEqual(TEXT("Preconstruct currency is retained"),Amount->GetText().ToString(),FText::AsNumber(int64(128450)).ToString());
        Shell->SetCurrencyAmount(-1);
        TestEqual(TEXT("Negative currency clamped"),Amount->GetText().ToString(),FText::AsNumber(int64(0)).ToString());
        Shell->SetCurrencyAmount(128450);
    }
    auto* Icon=LoadObject<UTexture2D>(nullptr,TEXT("/Game/System/UIBasic/Textures/T_ValCurrency"));
    if (TestNotNull(TEXT("Currency icon"),Icon))
        TestTrue(TEXT("Icon has downsampled mip levels"),Icon->GetNumMips()>1);
    ADD_LATENT_AUTOMATION_COMMAND(FBaseShellScreenshot(Shell,this));
    return true;
}
#endif
