#include "Map/MainMenu/MainMenuPlayerController.h"

#include "Kismet/KismetSystemLibrary.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_MapLoadingWidget.h"
#include "MTS_MapTransitionHandler.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"

AMainMenuPlayerController::AMainMenuPlayerController()
{
	bShowMouseCursor = true;
}

void AMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController()) { ShowMainMenu(); }
}

UMainMenuWidget* AMainMenuPlayerController::ShowMainMenu()
{
	if (bMenuEndingPlay || !IsLocalController()) { RestoreMenuWorldRendering(); return nullptr; }
	if (!MainMenuWidgetInstance)
	{
		UClass* Class = MainMenuWidgetClass.Get();
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogTemp, Error, TEXT("Main menu requires a Widget Blueprint in MainMenuWidgetClass."));
			RestoreMenuWorldRendering();
			return nullptr;
		}
		MainMenuWidgetInstance = CreateWidget<UMainMenuWidget>(this, Class);
		if (!MainMenuWidgetInstance) { RestoreMenuWorldRendering(); return nullptr; }
	}
	MainMenuWidgetInstance->OnMenuActionRequested.AddUniqueDynamic(this, &ThisClass::RouteMenuAction);
	MainMenuWidgetInstance->OnMenuExitFinished.AddUniqueDynamic(this, &ThisClass::HandleMenuExitFinished);
	if (!MainMenuWidgetInstance->IsInViewport()) { MainMenuWidgetInstance->AddToViewport(); }
	if (!MainMenuWidgetInstance->IsInViewport()) { RestoreMenuWorldRendering(); return nullptr; }
	MainMenuWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UpdateMenuWorldRendering();
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	MainMenuWidgetInstance->FocusFirstButton();
	return MainMenuWidgetInstance;
}

void AMainMenuPlayerController::HideMainMenu()
{
	RestoreMenuWorldRendering();
	if (MainMenuWidgetInstance)
	{
		MainMenuWidgetInstance->CancelPendingInput();
		MainMenuWidgetInstance->OnMenuActionRequested.RemoveDynamic(this, &ThisClass::RouteMenuAction);
		MainMenuWidgetInstance->OnMenuExitFinished.RemoveDynamic(this, &ThisClass::HandleMenuExitFinished);
		MainMenuWidgetInstance->RemoveFromParent();
	}
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AMainMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bMenuEndingPlay = true;
	HideMainMenu();
	MainMenuWidgetInstance = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AMainMenuPlayerController::PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel)
{
	RestoreMenuWorldRendering();
	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
}

void AMainMenuPlayerController::UpdateMenuWorldRendering()
{
	UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient.Get() : nullptr;
	if (bMenuEndingPlay || bRenderWorldBehindMenu || !IsLocalController() || !Viewport
		|| !MainMenuWidgetInstance || !MainMenuWidgetInstance->IsInViewport() || !MainMenuWidgetInstance->IsVisible())
	{
		RestoreMenuWorldRendering();
		return;
	}
	if (bOwnsWorldRenderingState && ManagedMenuViewport.Get() != Viewport) RestoreMenuWorldRendering();
	if (!bOwnsWorldRenderingState)
	{
		ManagedMenuViewport = Viewport;
		bSavedDisableWorldRendering = Viewport->bDisableWorldRendering;
		bOwnsWorldRenderingState = true;
	}
	Viewport->bDisableWorldRendering = true;
}

void AMainMenuPlayerController::RestoreMenuWorldRendering()
{
	if (bOwnsWorldRenderingState)
		if (UGameViewportClient* Viewport = ManagedMenuViewport.Get())
			Viewport->bDisableWorldRendering = bSavedDisableWorldRendering;
	bOwnsWorldRenderingState = false;
	ManagedMenuViewport.Reset();
}

void AMainMenuPlayerController::NotifyMainMenuDetached(const UMainMenuWidget* Menu)
{
	if (Menu == MainMenuWidgetInstance) RestoreMenuWorldRendering();
}

void AMainMenuPlayerController::NotifyMainMenuVisibilityChanged(const UMainMenuWidget* Menu)
{
	if (Menu == MainMenuWidgetInstance) UpdateMenuWorldRendering();
}

void AMainMenuPlayerController::RouteMenuAction(EMainMenuAction Action)
{
	OnMenuActionRequested.Broadcast(Action);
	HandleMenuAction(Action);
}

void AMainMenuPlayerController::HandleMenuAction_Implementation(EMainMenuAction Action)
{
	if (Action == EMainMenuAction::NewGame && MainMenuWidgetInstance)
	{
		MainMenuWidgetInstance->PlayMenuExit(Action);
	}
	if (Action == EMainMenuAction::Quit)
	{
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	}
}

void AMainMenuPlayerController::HandleMenuExitFinished(EMainMenuAction Action)
{
	if (Action != EMainMenuAction::NewGame) { return; }
	// The loading screen/next map must not inherit this menu's viewport override.
	RestoreMenuWorldRendering();
	auto* Transition = GetGameInstance()->GetSubsystem<UMTS_MapTransitionSubsystem>();
	FMTS_MapTransitionRequest Request;
	Request.TargetMap = NewGameMap;
	Request.TransitionHandlerClass = NewGameTransitionHandlerClass;
	Request.bInitializeMapFromHandler = NewGameTransitionHandlerClass != nullptr;
	Request.AutomaticProgressMax = MainMapProgressShare;
	Request.LoadingWidgetClass = LoadingWidgetClass;
	Request.LoadingTitle = NSLOCTEXT("SilverChoir", "Deployment", "正在部署");
	Request.bRequireManualMapReadyNotification = true;
	Request.ProgressSmoothSpeed = 0;
	Request.LoadingCompletionDelaySeconds = 1.f;
	if (!Transition || NewGameMap.IsNull() || !Transition->SwitchMap(Request))
	{
		UE_LOG(LogTemp, Warning, TEXT("Main menu: map transition rejected; restoring menu."));
		if (MainMenuWidgetInstance) { MainMenuWidgetInstance->ResetMenuExit(); MainMenuWidgetInstance->FocusFirstButton(); }
		UpdateMenuWorldRendering();
	}
}
