#include "UIBasic/ECGWidget.h"
#include "H5UI_View.h"

namespace {float ECGValue(float V,float Default,float Max){return FMath::IsFinite(V)?FMath::Clamp(V,0.f,Max):Default;}}
void UECGWidget::SetECGParameters(FLinearColor Color,float BPM,float Intensity)
{WaveColor=Color;HeartRateBPM=BPM;HeartbeatIntensity=Intensity;RefreshECG();}
void UECGWidget::SetWaveColor(FLinearColor Color){WaveColor=Color;RefreshECG();}
void UECGWidget::SetHeartRate(float BPM){HeartRateBPM=BPM;RefreshECG();}
void UECGWidget::SetHeartbeatIntensity(float Intensity){HeartbeatIntensity=Intensity;RefreshECG();}
void UECGWidget::SetAnimationSeed(int32 Seed){AnimationSeed=Seed;RefreshECG();}
void UECGWidget::ResolveAnimationSeed()
{
    if(ResolvedAnimationSeed!=0&&LastRequestedAnimationSeed==AnimationSeed)return;
    LastRequestedAnimationSeed=AnimationSeed;
    ResolvedAnimationSeed=AnimationSeed!=0?AnimationSeed:static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    if(ResolvedAnimationSeed==0)ResolvedAnimationSeed=1;
}
void UECGWidget::ConfigureView()
{
    if(!ECGView)return;
    ECGView->URL=TEXT("coui://uiresources/ECG/ecg.html");ECGView->bAutoLoad=false;
    ECGView->bEnableJavaScript=true;ECGView->bEnableBrowserSubviews=false;
    ECGView->bReceiveInput=false;ECGView->bConsumeInput=false;ECGView->TargetFrameRate=60;
    ECGView->SetVisibility(ESlateVisibility::HitTestInvisible);
    ECGView->OnReadyForBindings.AddUniqueDynamic(this,&ThisClass::HandleReady);
}
void UECGWidget::HandleReady(){RefreshECG();}
void UECGWidget::RefreshECG()
{
    HeartRateBPM=ECGValue(HeartRateBPM,72,300);HeartbeatIntensity=ECGValue(HeartbeatIntensity,0,2);
    WaveColor=FLinearColor(ECGValue(WaveColor.R,0,1),ECGValue(WaveColor.G,0,1),ECGValue(WaveColor.B,0,1),ECGValue(WaveColor.A,1,1));
    SweepSeconds=FMath::Max(1.f,ECGValue(SweepSeconds,3.2f,10));GlowStrength=ECGValue(GlowStrength,1,2);
    ResolveAnimationSeed();
    if(!ECGView)return;
    const FColor C=WaveColor.ToFColorSRGB();
    const FString Data=FString::Printf(TEXT("{\"color\":\"#%02x%02x%02x\",\"opacity\":%.5f,\"bpm\":%.5f,\"intensity\":%.5f,\"sweepSeconds\":%.5f,\"glow\":%.5f,\"grid\":%s,\"background\":%s,\"seed\":%d}"),C.R,C.G,C.B,WaveColor.A,HeartRateBPM,HeartbeatIntensity,SweepSeconds,GlowStrength,bShowGrid?TEXT("true"):TEXT("false"),bShowBackground?TEXT("true"):TEXT("false"),ResolvedAnimationSeed);
    ECGView->SetDataString(TEXT("ecgParameters"),Data);
    if(ECGView->ViewState==EH5UI_ViewState::Ready)ECGView->DispatchHtmlEvent(TEXT("ECGParameters"),Data);
}
void UECGWidget::SynchronizeProperties(){Super::SynchronizeProperties();ConfigureView();RefreshECG();}
void UECGWidget::NativePreConstruct()
{
    Super::NativePreConstruct();ConfigureView();RefreshECG();
    if(ECGView&&IsDesignTime()){ECGView->SynchronizeProperties();ECGView->LoadURL(ECGView->URL);}
}
void UECGWidget::NativeConstruct()
{Super::NativeConstruct();ConfigureView();RefreshECG();if(ECGView){ECGView->SynchronizeProperties();ECGView->LoadURL(ECGView->URL);}}
void UECGWidget::NativeDestruct()
{if(ECGView){ECGView->OnReadyForBindings.RemoveDynamic(this,&ThisClass::HandleReady);ECGView->Close();}Super::NativeDestruct();}
