#include "UIBasic/PanelAnimationTick.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"

FPanelAnimationTick::~FPanelAnimationTick()
{
    Stop();
}

void FPanelAnimationTick::Start(UUserWidget* Widget)
{
    if(Owner.Get()==Widget&&PostTickHandle.IsValid())return;
    Stop();
    if(!IsValid(Widget)||!Widget->IsConstructed()||!Widget->IsVisible()
        ||!Widget->IsAnyAnimationPlaying()||!FSlateApplication::IsInitialized())return;
    Owner=Widget;
    UUMGSequenceTickManager::Get(Widget)->OnWidgetTicked(Widget);
    PostTickHandle=FSlateApplication::Get().OnPostTick().AddWeakLambda(Widget,
        [this](float DeltaSeconds){KeepAnimationEligible(DeltaSeconds);});
}

void FPanelAnimationTick::KeepAnimationEligible(float DeltaSeconds)
{
    UUserWidget* Widget=Owner.Get();
    if(!Widget||!Widget->IsConstructed()||!Widget->IsVisible()||!Widget->IsAnyAnimationPlaying())
    {
        Stop();
        return;
    }
    // Clipping skips NativeTick, which normally grants this eligibility. The
    // UMG manager still advances time exactly once on its next Slate pre-tick.
    UUMGSequenceTickManager::Get(Widget)->OnWidgetTicked(Widget);
}

void FPanelAnimationTick::Stop()
{
    if(PostTickHandle.IsValid()&&FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(PostTickHandle);
    PostTickHandle.Reset();
    Owner.Reset();
}
