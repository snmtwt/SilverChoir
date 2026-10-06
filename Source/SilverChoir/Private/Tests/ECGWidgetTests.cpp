#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "UIBasic/ECGWidget.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "H5UI_View.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace ECGTest
{
class FRuntime : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TStrongObjectPtr<UCanvasPanel> Root;
    TSharedPtr<SWidget> Preview;
    TArray<UECGWidget*> Monitors;
    double Started=FPlatformTime::Seconds(),At=Started;
    int Stage=0;
    void Capture(const TCHAR* Name)
    {const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/ECG");IFileManager::Get().MakeDirectory(*Dir,true);FScreenshotRequest::RequestScreenshot(Dir/Name,true,false);}
public:
    explicit FRuntime(FAutomationTestBase* T):Test(T){}
    ~FRuntime(){if(Preview.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>50){Test->AddError(TEXT("ECG widget preview timed out"));return true;}
        if(Stage==0)
        {
            APlayerController* PC=nullptr;for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::Game&&Context.World())PC=Context.World()->GetFirstPlayerController();
            if(!PC)return false;
            auto* Class=LoadClass<UECGWidget>(nullptr,TEXT("/Game/System/UIBasic/WBP_ECG.WBP_ECG_C"));if(!Test->TestNotNull(TEXT("Reusable ECG Blueprint exists"),Class))return true;
            Root.Reset(NewObject<UCanvasPanel>(PC));auto* Canvas=Root.Get();
            auto* Background=NewObject<UImage>(Canvas);Background->SetColorAndOpacity(FLinearColor(FColor(4,10,17)));auto* BackSlot=Canvas->AddChildToCanvas(Background);BackSlot->SetAnchors(FAnchors(0,0,1,1));BackSlot->SetOffsets(FMargin(0));
            auto Label=[&](const FString& Text,float X,float Y,int Size,FColor Color)
            {auto* B=NewObject<UTextBlock>(Canvas);B->SetText(FText::FromString(Text));B->SetColorAndOpacity(FLinearColor(Color));B->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size));auto* S=Canvas->AddChildToCanvas(B);S->SetPosition({X,Y});S->SetSize({900,45});};
            Label(TEXT("SILVER CHOIR  /  VITAL SIGNS"),360,160,14,FColor(68,135,156));
            Label(TEXT("心电监测"),360,197,32,FColor(208,234,240));
            Label(TEXT("扫描刷新   /   波形余辉   /   实时参数"),360,253,13,FColor(100,147,162));
            const TCHAR* Titles[]={TEXT("稳定"),TEXT("心率加快"),TEXT("微弱心跳"),TEXT("无生命体征")};
            const float Rates[]={72,144,48,72},Strengths[]={1,1,.25f,0};const FColor Colors[]={FColor(41,220,242),FColor(112,228,170),FColor(247,184,76),FColor(245,94,100)};
            for(int I=0;I<4;++I)
            {
                const float X=360+(I%2)*630.f,Y=350+(I/2)*248.f;
                Label(Titles[I],X,Y,18,Colors[I]);Label(FString::Printf(TEXT("%03.0f BPM   ·   强度 %.2f"),Rates[I],Strengths[I]),X,Y+31,12,FColor(119,155,169));
                auto* ECG=CreateWidget<UECGWidget>(PC,Class);ECG->SetECGParameters(FLinearColor(Colors[I]),Rates[I],Strengths[I]);
                auto* S=Canvas->AddChildToCanvas(ECG);S->SetPosition({X,Y+66});S->SetSize({560,112});Monitors.Add(ECG);
            }
            Preview=Root->TakeWidget();GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(),100);Stage=1;At=FPlatformTime::Seconds();return false;
        }
        if(FPlatformTime::Seconds()-At<2)return false;
        if(Stage==1)
        {
            for(auto* ECG:Monitors)
            {
                auto* View=Cast<UH5UI_View>(ECG->GetWidgetFromName(TEXT("ECGView")));if(!Test->TestNotNull(TEXT("H5UI Designer binding"),View))return true;
                Test->TestEqual(TEXT("Native H5UI page is ready"),View->ViewState,EH5UI_ViewState::Ready);
                FString R,E;Test->TestTrue(TEXT("ECG script is running"),View->ExecuteJavaScript(TEXT("ecgMonitor.elapsed>0"),R,E)&&R==TEXT("true"));
                Test->TestTrue(TEXT("Rendered Canvas pixels are visible"),View->ExecuteJavaScript(TEXT("ecgMonitor.ctx.getImageData(10,10,1,1).data[3]>200"),R,E)&&R==TEXT("true"));
                Test->AddInfo(FString::Printf(TEXT("ECG_PERF %s JS=%.3f ms heap=%lld"),*ECG->GetName(),View->GetPerformanceStats().JavaScriptMilliseconds,View->GetPerformanceStats().JavaScriptHeapBytes));
            }
            Capture(TEXT("01_ECG_States.png"));Stage=2;At=FPlatformTime::Seconds();return false;
        }
        if(Stage==2)
        {
            Monitors[0]->SetECGParameters(FLinearColor(FColor(245,94,100)),160,0);
            auto* View=Cast<UH5UI_View>(Monitors[0]->GetWidgetFromName(TEXT("ECGView")));FString R,E;
            Test->TestTrue(TEXT("Blueprint-callable setter immediately clears old beats"),View->ExecuteJavaScript(TEXT("ecgMonitor.parameters.bpm===160&&ecgMonitor.parameters.intensity===0&&ecgMonitor.samples.every(v=>v===0)"),R,E)&&R==TEXT("true"));
            Stage=3;At=FPlatformTime::Seconds();return false;
        }
        if(Stage==3){Capture(TEXT("02_ECG_LiveUpdate.png"));Stage=4;At=FPlatformTime::Seconds();return false;}
        if(Stage==4)
        {
            for(auto* ECG:Monitors)CastChecked<UCanvasPanelSlot>(ECG->Slot)->SetSize({160,56});
            Stage=5;At=FPlatformTime::Seconds();return false;
        }
        if(Stage==5)
        {
            for(auto* ECG:Monitors){auto* View=CastChecked<UH5UI_View>(ECG->GetWidgetFromName(TEXT("ECGView")));Test->AddInfo(FString::Printf(TEXT("ECG_CARD_PERF JS=%.3f ms submit=%.3f ms"),View->GetPerformanceStats().JavaScriptMilliseconds,View->GetPerformanceStats().SubmitMilliseconds));}
            Stage=6;At=FPlatformTime::Seconds();return false;
        }
        GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());Preview.Reset();Root.Reset();Monitors.Reset();
        Test->AddInfo(TEXT("ECG_WIDGET_OK four independent native Canvas instances, animated scan, Blueprint parameters and immediate flatline"));return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FECGWidgetRuntimeTest,"SilverChoir.UI.ECG.RuntimeFlow",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FECGWidgetRuntimeTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(ECGTest::FRuntime(this));return true;}

namespace ECGTest
{
/** Reproduces six copies of the authored card, including their nested default ECGs. */
class FSeededInstances : public IAutomationLatentCommand
{
public:
    explicit FSeededInstances(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FSeededInstances() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60.)
        {
            Test->AddError(FString::Printf(TEXT("Seeded ECG card preview timed out at stage %d"), Stage));
            return true;
        }
        if (Stage == 0)
        {
            if (!GEngine || !GEngine->GameViewport) return false;
            APlayerController* PC = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Context.World()->GetFirstPlayerController();
            if (!PC) return false;
            if (!CreatePreview(PC)) return true;
            Advance();
            return false;
        }

        switch (Stage)
        {
        case 1:
            if (FPlatformTime::Seconds() - StageStarted < 2. || !MonitorsReady()) return false;
            // Freeze all six during one game-thread update, so screenshot and
            // state comparisons cannot be explained by different capture times.
            for (auto* Card : Cards)
                Evaluate(Card, TEXT("ecgMonitor.stop();ecgMonitor.draw();true"));
            ValidateAutomaticSeeds();
            Capture(TEXT("03_ECG_SeededCards_Frozen.png"));
            Advance();
            return false;
        case 2:
            if (!CaptureFinished()) return false;
            ValidateRefreshAndFixedSeeds();
            for (auto* Card : Cards)
            {
                Card->SetMemberVitals(1.f, .8f, .7f);
                Card->ECGMonitor->SetAnimationSeed(0);
                Evaluate(Card, TEXT("ecgMonitor.start();true"));
            }
            Advance();
            return false;
        case 3:
            if (FPlatformTime::Seconds() - StageStarted < 1.) return false;
            Capture(TEXT("03_ECG_SeededCards.png"));
            Advance();
            return false;
        case 4:
            if (!CaptureFinished()) return false;
            if (!Test->HasAnyErrors())
                Test->AddInfo(TEXT("ECG_SEEDED_INSTANCES_OK six default nested cards have independent seeds and phases; refresh preserves history; fixed seeds reproduce; zero intensity remains flat"));
            Test->AddInfo(FString::Printf(TEXT("Seeded ECG card screenshot: %s"), *ScreenshotPath));
            Cleanup();
            return true;
        default:
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UCanvasPanel> Root;
    TSharedPtr<SWidget> Preview;
    TArray<UBattlePersonnelCardWidget*> Cards;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;
    double Started = FPlatformTime::Seconds();
    double StageStarted = Started;
    int32 Stage = 0;

    void Advance() { ++Stage; StageStarted = FPlatformTime::Seconds(); }

    void Cleanup()
    {
        if (Preview.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) Root->ClearChildren();
        Cards.Reset();
        Root.Reset();
    }

    bool CreatePreview(APlayerController* PC)
    {
        UClass* CardClass = LoadClass<UBattlePersonnelCardWidget>(nullptr,
            TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片.WBP_人员卡片_C"));
        if (!Test->TestNotNull(TEXT("Authored personnel card Blueprint exists"), CardClass)) return false;
        Root.Reset(NewObject<UCanvasPanel>(PC));
        auto* Background = NewObject<UImage>(Root.Get());
        Background->SetColorAndOpacity(FLinearColor(FColor(5, 12, 19)));
        auto* BackSlot = Root->AddChildToCanvas(Background);
        BackSlot->SetAnchors(FAnchors(0, 0, 1, 1));
        BackSlot->SetOffsets(FMargin(0));
        const FVector2D ViewSize = GEngine->GameViewport->Viewport
            ? FVector2D(GEngine->GameViewport->Viewport->GetSizeXY()) : FVector2D(1920, 1080);
        const float X = FMath::Max(12.f, (float(ViewSize.X) - 6.f * 234.f - 5.f * 8.f) * .5f);
        const float Y = FMath::Max(150.f, float(ViewSize.Y) * .45f);
        auto* Label = NewObject<UTextBlock>(Root.Get());
        Label->SetText(FText::FromString(TEXT("SILVER CHOIR / ECG  ·  6 INDEPENDENT MONITORS  ·  72 BPM")));
        Label->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 18));
        Label->SetColorAndOpacity(FLinearColor(FColor(121, 179, 199)));
        auto* LabelSlot = Root->AddChildToCanvas(Label);
        LabelSlot->SetPosition({X, Y - 58.f});
        LabelSlot->SetSize({1500.f, 38.f});
        for (int32 Index = 0; Index < 6; ++Index)
        {
            auto* Card = CreateWidget<UBattlePersonnelCardWidget>(PC, CardClass);
            if (!Test->TestNotNull(TEXT("Default personnel-card instance"), Card)) return false;
            // No SetMember and no per-card ECG overrides: this is the user's
            // six identical Blueprint templates, not specially seeded fixtures.
            auto* Slot = Root->AddChildToCanvas(Card);
            Slot->SetPosition({X + Index * 242.f, Y});
            Slot->SetAutoSize(true);
            Cards.Add(Card);
        }
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 100);
        for (auto* Card : Cards)
            if (!Test->TestNotNull(TEXT("Nested ECG binding"), Card->ECGMonitor.Get())) return false;
        return true;
    }

    bool MonitorsReady() const
    {
        for (auto* Card : Cards)
        {
            const auto* View = Cast<UH5UI_View>(Card->ECGMonitor->GetWidgetFromName(TEXT("ECGView")));
            if (!View || View->ViewState != EH5UI_ViewState::Ready) return false;
        }
        return true;
    }

    FString Evaluate(UBattlePersonnelCardWidget* Card, const FString& Script)
    {
        auto* View = Cast<UH5UI_View>(Card->ECGMonitor->GetWidgetFromName(TEXT("ECGView")));
        FString Result, Error;
        if (!View || !View->ExecuteJavaScript(Script, Result, Error))
            Test->AddError(FString::Printf(TEXT("ECG seed JavaScript check failed: %s; %s"), *Script, *Error));
        return Result;
    }

    FString Snapshot(UBattlePersonnelCardWidget* Card)
    {
        return Evaluate(Card, TEXT("JSON.stringify([ecgMonitor.parameters.seed,ecgMonitor.elapsed,ecgMonitor.phase,ecgMonitor.head,Array.from(ecgMonitor.samples)])"));
    }

    void ValidateAutomaticSeeds()
    {
        TSet<FString> Seeds, Phases, Heads;
        const float BPM = Cards[0]->ECGMonitor->HeartRateBPM;
        const float Intensity = Cards[0]->ECGMonitor->HeartbeatIntensity;
        const FLinearColor Color = Cards[0]->ECGMonitor->WaveColor;
        for (auto* Card : Cards)
        {
            auto* ECG = Card->ECGMonitor.Get();
            Test->TestEqual(TEXT("Authored ECG uses automatic animation seed"), ECG->AnimationSeed, 0);
            Test->TestEqual(TEXT("All six cards retain the same requested heart rate"), ECG->HeartRateBPM, BPM);
            Test->TestEqual(TEXT("All six cards retain the same heartbeat intensity"), ECG->HeartbeatIntensity, Intensity);
            Test->TestTrue(TEXT("All six cards retain the same waveform color"), ECG->WaveColor.Equals(Color));
            Test->TestTrue(TEXT("Card remains at its authored 234x148 size"),
                Card->GetCachedGeometry().GetLocalSize().Equals(FVector2f(234.f, 148.f), .1f));
            Test->TestEqual(TEXT("C++ sends a resolved nonzero integer seed to the nested H5UI view"),
                Evaluate(Card, TEXT("Number.isInteger(ecgMonitor.parameters.seed)&&ecgMonitor.parameters.seed!==0")), FString(TEXT("true")));
            Test->TestEqual(TEXT("Automatic desynchronization preserves BPM and intensity in JS"),
                Evaluate(Card, FString::Printf(TEXT("ecgMonitor.parameters.bpm===%.5f&&ecgMonitor.parameters.intensity===%.5f"), BPM, Intensity)), FString(TEXT("true")));
            Seeds.Add(Evaluate(Card, TEXT("ecgMonitor.parameters.seed")));
            Phases.Add(Evaluate(Card, TEXT("ecgMonitor.phase")));
            Heads.Add(Evaluate(Card, TEXT("ecgMonitor.head")));
        }
        Test->TestEqual(TEXT("Six default nested ECGs have six distinct automatic seeds"), Seeds.Num(), 6);
        Test->TestTrue(TEXT("Default heartbeat phases are not all synchronized"), Phases.Num() > 1);
        Test->TestTrue(TEXT("Default sweep heads are not all synchronized"), Heads.Num() > 1);
    }

    void ValidateRefreshAndFixedSeeds()
    {
        for (auto* Card : Cards)
        {
            const FString Before = Snapshot(Card);
            Card->ECGMonitor->RefreshECG();
            Test->TestEqual(TEXT("Ordinary ECG refresh preserves seed, elapsed, phase, head and samples"), Snapshot(Card), Before);
            Card->SetMemberVitals(1.f, .8f, .7f);
            Test->TestEqual(TEXT("Personnel vitals update preserves ECG animation history"), Snapshot(Card), Before);
            Card->ECGMonitor->SetAnimationSeed(0);
            Test->TestEqual(TEXT("Repeated automatic-seed assignment preserves animation history"), Snapshot(Card), Before);
        }

        // Negative int32 also exercises the signed C++ -> JSON -> JS seed path.
        constexpr int32 FixedSeed = -20261003;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Cards[Index]->ECGMonitor->SetAnimationSeed(FixedSeed);
            Test->TestEqual(TEXT("Fixed signed seed arrives intact in JavaScript"),
                Evaluate(Cards[Index], TEXT("ecgMonitor.parameters.seed")), FString::FromInt(FixedSeed));
        }
        Test->TestEqual(TEXT("Matching fixed seeds reproduce elapsed, heartbeat phase, sweep head and waveform history"),
            Snapshot(Cards[0]), Snapshot(Cards[1]));
        for (int32 Index = 0; Index < 2; ++Index)
            Evaluate(Cards[Index], TEXT("ecgMonitor.advance(0.125);ecgMonitor.draw();true"));
        Test->TestEqual(TEXT("Matching fixed seeds advance deterministically"), Snapshot(Cards[0]), Snapshot(Cards[1]));
        const FString FixedBefore = Snapshot(Cards[0]);
        Cards[0]->ECGMonitor->SetAnimationSeed(FixedSeed);
        Cards[0]->ECGMonitor->RefreshECG();
        Test->TestEqual(TEXT("Repeated fixed-seed assignment does not restart an existing animation"), Snapshot(Cards[0]), FixedBefore);

        Cards[0]->SetMemberVitals(0.f, 0.f, 0.f);
        Test->TestEqual(TEXT("Zero health immediately clears seeded waveform to a flat line"),
            Evaluate(Cards[0], TEXT("ecgMonitor.parameters.intensity===0&&ecgMonitor.samples.every(v=>v===0)")), FString(TEXT("true")));
        Evaluate(Cards[0], TEXT("ecgMonitor.advance(0.2);true"));
        Test->TestEqual(TEXT("Seeded dead monitor stays flat as its sweep advances"),
            Evaluate(Cards[0], TEXT("ecgMonitor.samples.every(v=>v===0)")), FString(TEXT("true")));
    }

    void Capture(const TCHAR* Name)
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/ECG");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = Directory / Name;
        PreviousScreenshotTime = IFileManager::Get().GetTimeStamp(*ScreenshotPath);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    }

    bool CaptureFinished() const
    {
        return !FScreenshotRequest::IsScreenshotRequested()
            && IFileManager::Get().FileSize(*ScreenshotPath) > 0
            && IFileManager::Get().GetTimeStamp(*ScreenshotPath) != PreviousScreenshotTime;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FECGSeededInstancesTest,"SilverChoir.UI.ECG.SeededInstances",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FECGSeededInstancesTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(ECGTest::FSeededInstances(this));return true;}
#endif
