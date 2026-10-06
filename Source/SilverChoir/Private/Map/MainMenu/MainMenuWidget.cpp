#include "Map/MainMenu/MainMenuWidget.h"
#include "Map/MainMenu/MainMenuPlayerController.h"

#include "H5UI_View.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UIBasic/MenuExitSequence.h"

UMainMenuWidget::UMainMenuWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UMainMenuWidget::ConfigureBackground()
{
	if (!BackgroundView) { return; }
	BackgroundView->URL = BackgroundURL;
	BackgroundView->bAutoLoad = false;
	BackgroundView->bReceiveInput = false;
	BackgroundView->bConsumeInput = false;
	BackgroundView->bEnableJavaScript = false;
	BackgroundView->bEnableBrowserSubviews = false;
	BackgroundView->TargetFrameRate = FMath::Clamp(BackgroundFrameRate, 1, 120);
	BackgroundView->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMainMenuWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ConfigureBackground();
	if (BackgroundView && IsDesignTime())
	{
		BackgroundView->SynchronizeProperties();
		BackgroundView->LoadURL(BackgroundURL);
	}
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ConfigureBackground();
	if (BackgroundView)
	{
		BackgroundView->SynchronizeProperties();
		if (!IsDesignTime()) { BackgroundView->LoadURL(BackgroundURL); }
	}
	if (NewGameButton) { NewGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleNewGame); }
	if (LoadGameButton) { LoadGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLoadGame); }
	if (SettingsButton) { SettingsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSettings); }
	if (QuitButton) { QuitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuit); }
}

void UMainMenuWidget::SetVisibility(ESlateVisibility InVisibility)
{
	Super::SetVisibility(InVisibility);
	if (auto* Controller = Cast<AMainMenuPlayerController>(GetOwningPlayer()))
		Controller->NotifyMainMenuVisibilityChanged(this);
}

void UMainMenuWidget::RemoveFromParent()
{
	Super::RemoveFromParent();
	// NativeDestruct can be delayed by an external Slate reference. Restore the
	// viewport immediately when the UMG widget is explicitly detached instead.
	if (!IsInViewport() && !GetParent())
		if (auto* Controller = Cast<AMainMenuPlayerController>(GetOwningPlayer()))
			Controller->NotifyMainMenuDetached(this);
}

void UMainMenuWidget::NativeDestruct()
{
	ResetMenuExit();
	LastContentSize = FVector2D::ZeroVector;
	CancelPendingInput();
	if (BackgroundView) { BackgroundView->Close(); }
	if (NewGameButton) { NewGameButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNewGame); }
	if (LoadGameButton) { LoadGameButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLoadGame); }
	if (SettingsButton) { SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSettings); }
	if (QuitButton) { QuitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleQuit); }
	if (auto* Controller = Cast<AMainMenuPlayerController>(GetOwningPlayer())) Controller->NotifyMainMenuDetached(this);
	Super::NativeDestruct();
}

void UMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TickMenuExit(MyGeometry, InDeltaTime);
	if (!ContentFrame) { return; }
	const FVector2D Available = MyGeometry.GetLocalSize();
	if (Available.X <= 0 || Available.Y <= 0) { return; }
	// Constrain composition spacing on 21:9 / 32:9 without scaling up controls.
	// Child anchors, sizes and artwork remain authored in the Widget Blueprint.
	const FVector2D ContentSize(FMath::Min(Available.X, Available.Y * 16.0 / 9.0), Available.Y);
	if (!ContentSize.Equals(LastContentSize, 0.5))
	{
		if (UCanvasPanelSlot* ContentSlot = Cast<UCanvasPanelSlot>(ContentFrame->Slot))
		{
			ContentSlot->SetSize(ContentSize);
			LastContentSize = ContentSize;
		}
	}
}

void UMainMenuWidget::FocusFirstButton()
{
	if (NewGameButton && GetOwningPlayer()) { NewGameButton->SetUserFocus(GetOwningPlayer()); }
}

void UMainMenuWidget::CancelPendingInput()
{
	for (UBasicButtonWidget* Button : {NewGameButton.Get(), LoadGameButton.Get(), SettingsButton.Get(), QuitButton.Get()})
	{
		if (Button) { Button->CancelPendingClick(); }
	}
}

bool UMainMenuWidget::PlayMenuExit(EMainMenuAction SelectedAction)
{
	if (bMenuExiting || bMenuExited) { return false; }
	const auto Order = FMenuExitSequence::Order(static_cast<int32>(SelectedAction));
	if (Order.IsEmpty()) { return false; }
	UBasicButtonWidget* Buttons[] = {NewGameButton, LoadGameButton, SettingsButton, QuitButton};
	for (auto* Button : Buttons) { if (!Button) { return false; } }
	CancelPendingInput();
	ExitItems.Reset();
	for (int32 Index : Order)
	{
		FExitItem Item;
		Item.Button = Buttons[Index];
		Item.Transform = Buttons[Index]->GetRenderTransform();
		Item.Opacity = Buttons[Index]->GetRenderOpacity();
		Item.Visibility = Buttons[Index]->GetVisibility();
		const auto& Geometry = GetCachedGeometry();
		const float Left = Geometry.AbsoluteToLocal(Buttons[Index]->GetCachedGeometry().GetAbsolutePosition()).X;
		Item.Distance = FMath::Max(Buttons[Index]->GetDesiredSize().X + 32.0, Geometry.GetLocalSize().X - Left + 32.0);
		ExitItems.Add(Item);
		Buttons[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	ExitAction = SelectedAction;
	ExitStartFrame = MAX_uint64;
	LastExitTickFrame = MAX_uint64;
	bMenuExiting = true;
	return true;
}

void UMainMenuWidget::TickMenuExit(const FGeometry& Geometry, float DeltaTime)
{
	if (!bMenuExiting || LastExitTickFrame == GFrameCounter) { return; }
	LastExitTickFrame = GFrameCounter;
	if (ExitStartFrame == MAX_uint64) { ExitStartFrame = GFrameCounter; }
	bool bFinished = true;
	for (int32 Index = 0; Index < ExitItems.Num(); ++Index)
	{
		auto& Item = ExitItems[Index];
		auto* Button = Item.Button.Get();
		if (!Button) { continue; }
		if (!FMenuExitSequence::ShouldStart(ExitStartFrame, GFrameCounter, Index)) { bFinished = false; continue; }
		Item.Elapsed += FMath::Max(0.0f, DeltaTime);
		const float Alpha = FMath::Clamp(Item.Elapsed / FMath::Max(.05f, ExitSlideDuration), 0.0f, 1.0f);
		const float Ease = 1.0f - FMath::Pow(1.0f - Alpha, 3.0f);
		auto Transform = Item.Transform;
		// Enough travel for the full viewport, including ultra-wide margins.
		Transform.Translation.X += Item.Distance * Ease;
		Button->SetRenderTransform(Transform);
		Button->SetRenderOpacity(Item.Opacity * (1.0f - Alpha));
		bFinished &= Alpha >= 1;
	}
	if (bFinished)
	{
		bMenuExiting = false;
		bMenuExited = true;
		OnMenuExitFinished.Broadcast(ExitAction);
	}
}

void UMainMenuWidget::ResetMenuExit()
{
	bMenuExiting = false;
	bMenuExited = false;
	for (const auto& Item : ExitItems)
	{
		if (auto* Button = Item.Button.Get())
		{
			Button->SetRenderTransform(Item.Transform);
			Button->SetRenderOpacity(Item.Opacity);
			Button->SetVisibility(Item.Visibility);
		}
	}
	ExitItems.Reset();
}

void UMainMenuWidget::HandleNewGame() { if (!bMenuExiting && !bMenuExited) { OnMenuActionRequested.Broadcast(EMainMenuAction::NewGame); } }
void UMainMenuWidget::HandleLoadGame() { if (!bMenuExiting && !bMenuExited) { OnMenuActionRequested.Broadcast(EMainMenuAction::LoadGame); } }
void UMainMenuWidget::HandleSettings() { if (!bMenuExiting && !bMenuExited) { OnMenuActionRequested.Broadcast(EMainMenuAction::Settings); } }
void UMainMenuWidget::HandleQuit() { if (!bMenuExiting && !bMenuExited) { OnMenuActionRequested.Broadcast(EMainMenuAction::Quit); } }
