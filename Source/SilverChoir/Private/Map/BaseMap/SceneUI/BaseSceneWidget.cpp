#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Map/BaseMap/BaseMapWidget.h"

// Preserve existing Blueprint event implementations when no new override is supplied.
void UBaseSceneWidget::LoadSceneUI_Implementation() { OnSceneUIOpened(); }
void UBaseSceneWidget::UnloadSceneUI_Implementation() { OnSceneUIClosed(); }

void UBaseSceneWidget::NotifyLoadCompleted()
{
    if (Lifecycle != ELifecycle::Loading) return;
    Lifecycle = ELifecycle::Loaded;
    OnLoadCompleted.Broadcast();
}
void UBaseSceneWidget::NotifyUnloadCompleted()
{
    if (Lifecycle != ELifecycle::Unloading) return;
    Lifecycle = ELifecycle::Detached;
    UBaseMapWidget* Host = BaseUI;
    if (Host)
    {
        TGuardValue<bool> Guard(Host->bSwitchingSceneUI, true);
        OnUnloadCompleted.Broadcast();
        Host->FinishSceneUIUnload(this);
    }
    else OnUnloadCompleted.Broadcast();
}
