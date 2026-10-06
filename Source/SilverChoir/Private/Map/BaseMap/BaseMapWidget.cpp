#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Components/PanelWidget.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "UIBasic/BasicButtonWidget.h"

void UBaseMapWidget::SetHeaderTitle(FText Title) { HeaderTitle = Title; if (TitleText) TitleText->SetText(HeaderTitle); }
void UBaseMapWidget::SetCurrencyAmount(int64 Amount) { CurrencyAmount = FMath::Max<int64>(0, Amount); if (CurrencyText) CurrencyText->SetText(FText::AsNumber(CurrencyAmount)); }
void UBaseMapWidget::SetFooterStatus(FText Status) { FooterStatus = Status; if (StatusText) StatusText->SetText(FooterStatus); }
void UBaseMapWidget::SetBackButtonVisible(bool bVisible)
{
    bBackButtonVisible = bVisible;
    if (BackButton) BackButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void UBaseMapWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    SetHeaderTitle(HeaderTitle); SetCurrencyAmount(CurrencyAmount); SetFooterStatus(FooterStatus);
    SetBackButtonVisible(bBackButtonVisible);
}
void UBaseMapWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bEndingSceneUI = false;
    if (BackButton) BackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBackRequested);
}
void UBaseMapWidget::NativeDestruct()
{
    bEndingSceneUI = true;
    if (BackButton) BackButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleBackRequested);
    PendingSceneUI = nullptr;
    if (UBaseSceneWidget* Previous = CurrentSceneUI)
    {
        CurrentSceneUI = nullptr;
        const bool AlreadyUnloading = Previous->Lifecycle == UBaseSceneWidget::ELifecycle::Unloading;
        Previous->Lifecycle = UBaseSceneWidget::ELifecycle::Detached;
        if (!AlreadyUnloading) Previous->UnloadSceneUI();
        Previous->RemoveFromParent();
        Previous->BaseUI = nullptr;
    }
    Super::NativeDestruct();
}
void UBaseMapWidget::HandleBackRequested()
{
    if (bEndingSceneUI || !bBackButtonVisible) return;
    OnReturnToBase();
    OnBackRequested.Broadcast();
}
void UBaseMapWidget::OnReturnToBase_Implementation() {}
UBaseSceneWidget* UBaseMapWidget::ShowSceneUI(TSubclassOf<UBaseSceneWidget> WidgetClass)
{
    return MountSceneUI(WidgetClass, WidgetClass ? WidgetClass->GetDefaultObject<UBaseSceneWidget>()->SceneTag : FGameplayTag());
}
UBaseSceneWidget* UBaseMapWidget::CreateSceneUIByTag(FGameplayTag SceneTag)
{
    if (!SceneTag.IsValid()) return nullptr;
    const auto* Class = SceneUIClasses.Find(SceneTag);
    return Class ? MountSceneUI(*Class, SceneTag) : nullptr;
}
UBaseSceneWidget* UBaseMapWidget::MountSceneUI(TSubclassOf<UBaseSceneWidget> WidgetClass, FGameplayTag Tag)
{
    if (bEndingSceneUI || bSwitchingSceneUI || !SceneContent || !WidgetClass || !GetOwningPlayer()) return nullptr;
    if (WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return nullptr;
    // While an exit animation is running, accept only the same pending request.
    if (CurrentSceneUI && CurrentSceneUI->Lifecycle == UBaseSceneWidget::ELifecycle::Unloading)
        return PendingSceneUI && PendingSceneUI->GetClass() == WidgetClass && PendingSceneUI->SceneTag == Tag ? PendingSceneUI.Get() : nullptr;
    if (CurrentSceneUI && CurrentSceneUI->GetClass() == WidgetClass && CurrentSceneUI->SceneTag == Tag) return CurrentSceneUI;
    TGuardValue<bool> Guard(bSwitchingSceneUI, true);
    UBaseSceneWidget* Next = CreateWidget<UBaseSceneWidget>(GetOwningPlayer(), WidgetClass);
    if (!Next) return nullptr;
    Next->SceneTag = Tag;
    PendingSceneUI = Next;
    if (CurrentSceneUI) RemoveCurrentSceneUI();
    else AttachPendingSceneUI();
    return !bEndingSceneUI && (PendingSceneUI == Next || CurrentSceneUI == Next) ? Next : nullptr;
}
void UBaseMapWidget::AttachPendingSceneUI()
{
    if (bEndingSceneUI || CurrentSceneUI || !PendingSceneUI || !SceneContent) return;
    UBaseSceneWidget* Next = PendingSceneUI;
    PendingSceneUI = nullptr;
    CurrentSceneUI = Next;
    Next->BaseUI = this;
    UPanelSlot* ContentSlot = SceneContent->AddChild(Next);
    if (!ContentSlot || bEndingSceneUI || CurrentSceneUI != Next)
    {
        Next->RemoveFromParent(); Next->BaseUI = nullptr;
        if (CurrentSceneUI == Next) CurrentSceneUI = nullptr;
        return;
    }
    if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(ContentSlot))
    {
        OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
        OverlaySlot->SetVerticalAlignment(VAlign_Fill);
    }
    if (!Next->SceneTitle.IsEmpty()) SetHeaderTitle(Next->SceneTitle);
    Next->Lifecycle = UBaseSceneWidget::ELifecycle::Loading;
    Next->LoadSceneUI();
}
void UBaseMapWidget::ClearSceneUI()
{
    UnloadCurrentSceneUI();
}
void UBaseMapWidget::UnloadCurrentSceneUI()
{
    if (bSwitchingSceneUI) return;
    TGuardValue<bool> Guard(bSwitchingSceneUI, true);
    PendingSceneUI = nullptr;
    RemoveCurrentSceneUI();
}
void UBaseMapWidget::RemoveCurrentSceneUI()
{
    if (CurrentSceneUI && CurrentSceneUI->Lifecycle != UBaseSceneWidget::ELifecycle::Unloading)
    {
        CurrentSceneUI->Lifecycle = UBaseSceneWidget::ELifecycle::Unloading;
        CurrentSceneUI->UnloadSceneUI();
    }
}
void UBaseMapWidget::FinishSceneUIUnload(UBaseSceneWidget* Finished)
{
    if (!Finished || CurrentSceneUI != Finished || bEndingSceneUI) return;
    TGuardValue<bool> Guard(bSwitchingSceneUI, true);
    CurrentSceneUI = nullptr;
    Finished->RemoveFromParent();
    Finished->BaseUI = nullptr;
    AttachPendingSceneUI();
}
