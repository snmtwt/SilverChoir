#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "MTS_MapTransitionSubsystem.h"
#include "MTS_MapTransitionHandler.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "CoreGlobals.h"

class FWaitForMenuTravel : public IAutomationLatentCommand
{
public:
	FWaitForMenuTravel(FAutomationTestBase* InTest, UGameInstance* Instance, double InTimeoutSeconds = 30)
		: Test(InTest), GameInstance(Instance), Start(FPlatformTime::Seconds()), TimeoutSeconds(InTimeoutSeconds) {}
	bool Update() override
	{
		if (!GameInstance.IsValid()) { Test->AddError(TEXT("GameInstance lost during travel")); return true; }
		if (FPlatformTime::Seconds() - Start > TimeoutSeconds) { Test->AddError(TEXT("Menu-to-base transition timed out")); return true; }
		auto* World = GameInstance->GetWorld();
		auto* Transition = GameInstance->GetSubsystem<UMTS_MapTransitionSubsystem>();
		if (ReadyObserved == 0 && Transition->IsTransitionInProgress() && !Transition->GetCurrentPayload().ReadySource.IsNone()) { ReadyObserved = FPlatformTime::Seconds(); }
		if (auto* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr)
		{
			if (Transition->IsTransitionInProgress() || State->ActiveMapID != TEXT("Base")) { return false; }
			Test->TestTrue(TEXT("Activation updates GameState to Base"), State->IsBaseMap() && !State->IsBattleMap());
			auto* Player = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
			if (!Test->TestNotNull(TEXT("Main map controller"), Player)) { return true; }
			auto* Viewport = Player->GetLocalPlayer() ? Player->GetLocalPlayer()->ViewportClient.Get() : nullptr;
			if (Test->TestNotNull(TEXT("Base map viewport survives travel"), Viewport))
				Test->TestFalse(TEXT("Menu world-rendering suppression is restored before entering Base"), bool(Viewport->bDisableWorldRendering));
			Test->TestTrue(TEXT("Activation shows only Base UI"), Player->BaseWidget && Player->BaseWidget->IsInViewport() && !Player->BattleWidget);
			auto* PreviousBase = Player->BaseWidget.Get();
			Test->TestTrue(TEXT("Battle UI switch accepted"), Player->SwitchMapUI(EGameMainMapType::Battle));
			Test->TestTrue(TEXT("UI switch removes Base and displays Battle"), !Player->BaseWidget && Player->BattleWidget && Player->BattleWidget->IsInViewport() && (!PreviousBase || !PreviousBase->IsInViewport()));
			Test->TestTrue(TEXT("Display alone does not change gameplay state"), State->IsBaseMap());
			Test->TestTrue(TEXT("Base UI can be restored"), Player->SwitchMapUI(EGameMainMapType::Base));
			Test->TestEqual(TEXT("Loading lifecycle completed"), Transition->GetCurrentPayload().Phase, EMTS_MapTransitionPhase::Completed);
			Test->TestTrue(TEXT("Loading remains visible for one second after notification"), ReadyObserved > 0 && FPlatformTime::Seconds() - ReadyObserved >= .9);
			const auto Request = Transition->GetActiveTransitionRequest();
			Test->TestEqual(TEXT("Requested close delay is one second"), Request.LoadingCompletionDelaySeconds, 1.f);
			Test->TestTrue(TEXT("Handler owns initial base load"), Request.bInitializeMapFromHandler);
			Test->TestEqual(TEXT("Main map occupies first 30 percent"), Request.AutomaticProgressMax, .3f);
			Test->TestEqual(TEXT("Base brings combined loading to 100 percent"), Transition->GetCurrentPayload().Progress, 1.f);
			Test->TestTrue(TEXT("NewGame Blueprint handler selected"), Request.TransitionHandlerClass && Request.TransitionHandlerClass->GetName() == TEXT("BP_MapTransitionHandler_NewGame_C"));
			Test->TestEqual(TEXT("Loading screen waits for base"), Transition->GetCurrentPayload().ReadySource, FName(TEXT("GameMainMap.BaseReady")));
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TWeakObjectPtr<UGameInstance> GameInstance;
	double Start;
	double TimeoutSeconds;
	double ReadyObserved = 0;
};

/** Runs after the real NewGame Blueprint transition; does not modify map assets. */
class FWaitForBaseSandboxScene : public IAutomationLatentCommand
{
public:
	FWaitForBaseSandboxScene(FAutomationTestBase* InTest, UGameInstance* Instance)
		: Test(InTest), GameInstance(Instance), Started(FPlatformTime::Seconds()),
		bVisualReview(FParse::Param(FCommandLine::Get(), TEXT("SandboxVisualReview"))) {}
	~FWaitForBaseSandboxScene() override
	{
		if (ReviewPlayer.IsValid() && OriginalView.IsValid()) { ReviewPlayer->SetViewTarget(OriginalView.Get()); }
		if (ReviewCamera.IsValid()) { ReviewCamera->Destroy(); }
	}
	bool Update() override
	{
		if (!GameInstance.IsValid()) { Test->AddError(TEXT("Sandbox review lost GameInstance")); return true; }
		if (FPlatformTime::Seconds() - Started > 180) { Test->AddError(TEXT("Sandbox scene review timed out")); return true; }
		UWorld* World = GameInstance->GetWorld();
		AGameMainMapGameState* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
		if (!State || State->ActiveMapID != TEXT("Base") || !State->IsBaseMap()) { return false; }
		if (Stage == 0)
		{
			ABaseSandboxMap* Sandbox = nullptr;
			int32 SandboxCount = 0;
			for (TActorIterator<ABaseSandboxMap> It(World); It; ++It) { Sandbox = *It; ++SandboxCount; }
			if (!Test->TestEqual(TEXT("Streamed Base contains exactly one sandbox actor"), SandboxCount, 1)) { return true; }
			bool bValid = true;
			int32 OwnedTiles = 0, LevelTiles = 0, AttachedTiles = 0;
			for (TActorIterator<AGSMTile3D> It(World); It; ++It)
			{
				if (It->GetLevel() == Sandbox->GetLevel()) { ++LevelTiles; }
				if (It->GetOwner() == Sandbox)
				{
					++OwnedTiles;
					if (It->GetAttachParentActor() == Sandbox && It->GetLevel() == Sandbox->GetLevel()) { ++AttachedTiles; }
				}
			}
			bValid &= Test->TestEqual(TEXT("Sandbox owns 336 runtime tiles"), OwnedTiles, 336);
			bValid &= Test->TestEqual(TEXT("All 336 tiles attach to sandbox in its streamed level"), AttachedTiles, 336);
			bValid &= Test->TestEqual(TEXT("Base has no orphaned serialized preview tiles"), LevelTiles, 336);
			UGSMMapSubsystem* Subsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>();
			UGSMMapData* Data = Sandbox->GetMapData();
			bValid &= Test->TestTrue(TEXT("Sandbox runtime data is valid"), Data && Data->IsMapDataValid());
			bValid &= Test->TestTrue(TEXT("Sandbox data is independently registered by GUID"),
				Subsystem && Subsystem->GetMapDataByGuid(Sandbox->GetMapGuid()) == Data && Data
				&& Subsystem->GetDefaultMapData() != Data);
			UStaticMeshComponent* Terrain = Sandbox->GetMapTerrainMeshComponent();
			bValid &= Test->TestTrue(TEXT("Sandbox terrain component and authored mesh survive streaming"),
				Terrain && Terrain->GetStaticMesh()
				&& Terrain->GetStaticMesh()->GetPathName() == TEXT("/Game/Meshs/Map/SM_SandboxMap.SM_SandboxMap")
				&& Terrain->IsVisible() && !Terrain->bHiddenInGame);
			if (Terrain)
			{
				bValid &= Test->TestEqual(TEXT("Terrain retains two material slots"), Terrain->GetNumMaterials(), 2);
				const TCHAR* ExpectedMasters[] = {
					TEXT("/Game/System/Map/BaseMap/Sandbox/Materials/M_BaseSandboxTerrain.M_BaseSandboxTerrain"),
					TEXT("/Game/System/Map/BaseMap/Sandbox/Materials/M_BaseSandboxWater.M_BaseSandboxWater") };
				for (int32 Slot = 0; Slot < 2; ++Slot)
				{
					UMaterialInterface* Material = Terrain->GetMaterial(Slot);
					bValid &= Test->TestTrue(FString::Printf(TEXT("Sandbox material slot %d retains its bounds-clipping master"), Slot),
						Material && Material->GetMaterial() && Material->GetMaterial()->GetPathName() == ExpectedMasters[Slot]);
				}
			}
			if (!bValid || !bVisualReview) { return true; }
			if (FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
			{
				Test->AddError(TEXT("-SandboxVisualReview requires rendering; use -RenderOffscreen without -nullrhi"));
				return true;
			}
			AGameMainMapPlayerController* Player = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
			if (!Test->TestNotNull(TEXT("Visual review has main map controller"), Player)) { return true; }
			ReviewPlayer = Player;
			OriginalView = Player->GetViewTarget();
			ReviewCamera = World->SpawnActor<ACameraActor>();
			if (!Test->TestNotNull(TEXT("Transient sandbox review camera"), ReviewCamera.Get())) { return true; }
			const FTransform Transform = Sandbox->GetActorTransform();
			const FVector Location = Transform.TransformPosition(FVector(-60, 660, 365));
			const FVector Target = Transform.TransformPosition(FVector(0, 0, 10));
			ReviewCamera->SetActorLocationAndRotation(Location, (Target - Location).Rotation());
			ReviewCamera->GetCameraComponent()->SetFieldOfView(74.f);
			Player->SetViewTarget(ReviewCamera.Get());
			Stage = 1;
			StageStarted = FPlatformTime::Seconds();
			StageFrame = GFrameCounter;
			return false;
		}
		if (Stage == 1)
		{
			if (FPlatformTime::Seconds() - StageStarted < 10 || GFrameCounter < StageFrame + 60) { return false; }
			// Diagnostic commands are explicitly opt-in and are absent from normal tests.
			GEngine->Exec(World, TEXT("D3D12.DumpRayTracingGeometries all Sandbox"));
			GEngine->Exec(World, TEXT("D3D12.DumpRayTracingGeometriesToCSV"));
			ScreenshotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BaseSandboxSetup/runtime_base.png"));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
			if (IFileManager::Get().FileExists(*ScreenshotPath) && !IFileManager::Get().Delete(*ScreenshotPath))
			{
				Test->AddError(TEXT("Cannot replace previous sandbox review screenshot"));
				return true;
			}
			FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
			Stage = 2;
			StageFrame = GFrameCounter;
			return false;
		}
		if (GFrameCounter < StageFrame + 3 || FScreenshotRequest::IsScreenshotRequested()) { return false; }
		if (!Test->TestTrue(TEXT("Rendered sandbox screenshot was written"), IFileManager::Get().FileSize(*ScreenshotPath) > 1024)) { return true; }
		Test->AddInfo(FString::Printf(TEXT("BASE_SANDBOX_VISUAL_REVIEW_OK %s; D3D12 geometry CSV is in Saved/Profiling"), *ScreenshotPath));
		return true;
	}
private:
	FAutomationTestBase* Test;
	TWeakObjectPtr<UGameInstance> GameInstance;
	TWeakObjectPtr<AGameMainMapPlayerController> ReviewPlayer;
	TWeakObjectPtr<AActor> OriginalView;
	TWeakObjectPtr<ACameraActor> ReviewCamera;
	double Started;
	double StageStarted = 0;
	uint64 StageFrame = 0;
	bool bVisualReview = false;
	int32 Stage = 0;
	FString ScreenshotPath;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuWorldRenderingScopeTest, "SilverChoir.MenuTravel.WorldRenderingScope", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMenuWorldRenderingScopeTest::RunTest(const FString& Parameters)
{
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::Game || !Context.World()) continue;
		auto* Controller = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController());
		if (!Controller) continue;
		auto* Viewport = Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->ViewportClient.Get() : nullptr;
		if (!TestNotNull(TEXT("Menu rendering test has a local viewport"), Viewport)) return false;
		const bool bOriginalRenderBehindMenu = Controller->bRenderWorldBehindMenu;
		Controller->HideMainMenu();
		const bool bOriginalWorldDisabled = Viewport->bDisableWorldRendering;
		Controller->bRenderWorldBehindMenu = false;
		Viewport->bDisableWorldRendering = false;

		auto* Menu = Controller->ShowMainMenu();
		TestNotNull(TEXT("Menu can be displayed"), Menu);
		TestTrue(TEXT("Visible pure UI menu skips world rendering"), bool(Viewport->bDisableWorldRendering));
		TestTrue(TEXT("Repeated show returns the same menu"), Controller->ShowMainMenu() == Menu);
		Controller->HideMainMenu();
		TestFalse(TEXT("Repeated show does not overwrite the original false state"), bool(Viewport->bDisableWorldRendering));
		Controller->ShowMainMenu();
		TestTrue(TEXT("Reopening acquires the viewport override again"), bool(Viewport->bDisableWorldRendering));
		Controller->HideMainMenu();
		TestFalse(TEXT("Hide restores after reopening"), bool(Viewport->bDisableWorldRendering));

		Menu = Controller->ShowMainMenu();
		if (Menu)
		{
			Menu->SetVisibility(ESlateVisibility::Hidden);
			TestFalse(TEXT("Blueprint Hidden visibility restores world rendering"), bool(Viewport->bDisableWorldRendering));
			Menu->SetVisibility(ESlateVisibility::Visible);
			TestTrue(TEXT("Visible menu reacquires the viewport override"), bool(Viewport->bDisableWorldRendering));
			Menu->SetVisibility(ESlateVisibility::Collapsed);
			TestFalse(TEXT("Blueprint Collapsed visibility restores world rendering"), bool(Viewport->bDisableWorldRendering));
			// A different system may change the viewport while the menu is hidden.
			Viewport->bDisableWorldRendering = true;
			Menu->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Menu->SetVisibility(ESlateVisibility::Hidden);
			TestTrue(TEXT("Reshow preserves the newly acquired original true state"), bool(Viewport->bDisableWorldRendering));
			Viewport->bDisableWorldRendering = false;
			Menu->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		Controller->HideMainMenu();
		TestFalse(TEXT("Visibility round-trip restores the latest false state"), bool(Viewport->bDisableWorldRendering));

		Viewport->bDisableWorldRendering = true;
		Controller->ShowMainMenu();
		Controller->ShowMainMenu();
		Controller->HideMainMenu();
		TestTrue(TEXT("An existing true state is preserved, not forced false"), bool(Viewport->bDisableWorldRendering));

		Viewport->bDisableWorldRendering = false;
		Menu = Controller->ShowMainMenu();
		if (Menu)
		{
			// Keep Slate alive to prove restoration does not wait for NativeDestruct.
			TSharedPtr<SWidget> RetainedSlate = Menu->TakeWidget();
			Menu->RemoveFromParent();
			TestFalse(TEXT("External RemoveFromParent immediately restores world rendering"), bool(Viewport->bDisableWorldRendering));
			TestFalse(TEXT("Externally removed menu is no longer in viewport"), Menu->IsInViewport());
			RetainedSlate.Reset();
		}
		Controller->HideMainMenu();

		Controller->bRenderWorldBehindMenu = true;
		Controller->ShowMainMenu();
		TestFalse(TEXT("3D menu option keeps world rendering enabled"), bool(Viewport->bDisableWorldRendering));
		Controller->HideMainMenu();
		TestFalse(TEXT("3D menu hide does not change viewport state"), bool(Viewport->bDisableWorldRendering));

		Controller->bRenderWorldBehindMenu = bOriginalRenderBehindMenu;
		Viewport->bDisableWorldRendering = bOriginalWorldDisabled;
		Controller->ShowMainMenu();
		AddInfo(TEXT("MENU_WORLD_RENDERING_SCOPE_OK show/hide/reopen/visibility/prior-true/external-remove/3D-option"));
		return true;
	}
	AddError(TEXT("Run from MainMenu -game"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuTravelTest, "SilverChoir.MenuTravel.StartAction", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMenuTravelTest::RunTest(const FString& Parameters)
{
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
		auto* Controller = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController());
		if (!Controller) { continue; }
		auto* Menu = Controller->GetMainMenuWidget();
		if (!TestNotNull(TEXT("Menu instance exists"), Menu)) { return false; }
		Controller->HandleMenuAction(EMainMenuAction::NewGame);
		TestTrue(TEXT("Start action begins exit before travel"), Menu->bMenuExiting);
		TestFalse(TEXT("Duplicate exit rejected"), Menu->PlayMenuExit(EMainMenuAction::NewGame));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForMenuTravel(this, Context.World()->GetGameInstance()));
		return true;
	}
	AddError(TEXT("Run from MainMenu -game")); return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuSandboxSceneTest, "SilverChoir.MenuTravel.SandboxScene", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMenuSandboxSceneTest::RunTest(const FString& Parameters)
{
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
		auto* Controller = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController());
		if (!Controller) { continue; }
		if (!TestNotNull(TEXT("Menu instance exists before sandbox regression"), Controller->GetMainMenuWidget())) { return false; }
		UGameInstance* Instance = Context.World()->GetGameInstance();
		Controller->HandleMenuAction(EMainMenuAction::NewGame);
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForMenuTravel(this, Instance, 120));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForBaseSandboxScene(this, Instance));
		return true;
	}
	AddError(TEXT("Run SandboxScene alone from MainMenu -game; add -SandboxVisualReview for screenshot and D3D12 memory diagnostics"));
	return false;
}
#endif
