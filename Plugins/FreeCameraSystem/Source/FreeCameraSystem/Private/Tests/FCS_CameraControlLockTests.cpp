#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FCS_CameraConfigDataAsset.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_FreeCameraSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include <limits>

namespace
{
struct FCameraControlLockFixture
{
	UWorld* World = nullptr;
	UFCS_FreeCameraSubsystem* Subsystem = nullptr;
	AFCS_FreeCameraPawn* Pawn = nullptr;
	AActor* CallbackProbe = nullptr;
	UFCS_CameraConfigDataAsset* Config = nullptr;
	TWeakObjectPtr<AFCS_FreeCameraPawn> PreviousCamera;
	APlayerController* Controller = nullptr;
	TWeakObjectPtr<APawn> PreviousPawn;
	UPlayerInput* PreviousPlayerInput = nullptr;
	UInputComponent* Input = nullptr;
	bool bPreviousCursor = false;
	bool bHadMousePosition = false;
	float PreviousMouseX = 0.0f;
	float PreviousMouseY = 0.0f;
	int32 RemovedMoveIgnoreCount = 0;

	bool Initialize(FAutomationTestBase& Test)
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		if (!Test.TestNotNull(TEXT("Game world available"), World)) { return false; }
		Subsystem = World->GetSubsystem<UFCS_FreeCameraSubsystem>();
		if (!Test.TestNotNull(TEXT("Camera subsystem available"), Subsystem)) { return false; }
		PreviousCamera = Subsystem->GetFreeCamera();
		Pawn = World->SpawnActor<AFCS_FreeCameraPawn>();
		if (!Test.TestNotNull(TEXT("Camera spawned"), Pawn)) { return false; }
		Config = NewObject<UFCS_CameraConfigDataAsset>(Pawn);
		Config->bUseZoomInterpolatedMoveSpeed = false;
		Config->MoveSpeed = 1000.0f;
		Config->bEnableZoomEase = false;
		Config->bEnableEdgeScroll = false;
		Config->bUseCameraBounds = false;
		Config->bHideCursorWhenRotate = true;
		Pawn->CameraConfig = Config;
		Pawn->ApplyCameraConfig();
		Pawn->SetActorLocation(FVector(0.0f, 0.0f, 123.0f));
		Pawn->SetActorRotation(FRotator::ZeroRotator);
		return Test.TestTrue(TEXT("Fixture camera registered"), Subsystem->GetFreeCamera() == Pawn);
	}

	bool Possess(FAutomationTestBase& Test)
	{
		Controller = World->GetFirstPlayerController();
		if (!Test.TestNotNull(TEXT("Player controller available"), Controller)) { return false; }
		PreviousPawn = Controller->GetPawn();
		PreviousPlayerInput = Controller->PlayerInput;
		bPreviousCursor = Controller->bShowMouseCursor;
		bHadMousePosition = Controller->GetMousePosition(PreviousMouseX, PreviousMouseY);
		Controller->Possess(Pawn);
		if (!Test.TestTrue(TEXT("Test camera possessed"), Controller->GetPawn() == Pawn)) { return false; }
		// Keep real keyboard state untouched, and run only this camera's default bindings.
		Controller->PlayerInput = NewObject<UPlayerInput>(Controller);
		Input = NewObject<UInputComponent>(Pawn);
		Pawn->SetupPlayerInputComponent(Input);
		while (Controller->IsMoveInputIgnored() && RemovedMoveIgnoreCount < 256)
		{
			Controller->SetIgnoreMoveInput(false);
			++RemovedMoveIgnoreCount;
		}
		return Test.TestFalse(TEXT("Movement input enabled for keyboard checks"), Controller->IsMoveInputIgnored());
	}

	void SendKey(const FKey& Key, EInputEvent Event)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
		const TArray<UInputComponent*> InputStack = { Input };
		Controller->PlayerInput->ProcessInputStack(InputStack, 0.05f, false);
	}

	void Tick(float DeltaTime = 0.05f) const
	{
		Pawn->TickActor(DeltaTime, LEVELTICK_All, Pawn->PrimaryActorTick);
	}

	~FCameraControlLockFixture()
	{
		if (IsValid(Controller))
		{
			if (Controller->GetPawn() == Pawn) { Controller->UnPossess(); }
			Controller->PlayerInput = PreviousPlayerInput;
			if (PreviousPawn.IsValid()) { Controller->Possess(PreviousPawn.Get()); }
			Controller->bShowMouseCursor = bPreviousCursor;
			for (int32 Index = 0; Index < RemovedMoveIgnoreCount; ++Index) { Controller->SetIgnoreMoveInput(true); }
			if (bHadMousePosition) { Controller->SetMouseLocation(FMath::RoundToInt(PreviousMouseX), FMath::RoundToInt(PreviousMouseY)); }
		}
		if (IsValid(Pawn)) { Pawn->Destroy(); }
		if (IsValid(CallbackProbe)) { CallbackProbe->Destroy(); }
		if (IsValid(Subsystem) && PreviousCamera.IsValid()) { Subsystem->RegisterFreeCamera(PreviousCamera.Get()); }
	}
};

void TestStateUnchanged(FAutomationTestBase& Test, const TCHAR* Label, const FFCS_CameraState& Before, const FFCS_CameraState& After)
{
	Test.TestTrue(FString::Printf(TEXT("%s: location unchanged"), Label), Before.Location.Equals(After.Location));
	Test.TestTrue(FString::Printf(TEXT("%s: yaw unchanged"), Label), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Before.Yaw, After.Yaw)));
	Test.TestEqual(FString::Printf(TEXT("%s: pitch unchanged"), Label), After.Pitch, Before.Pitch);
	Test.TestEqual(FString::Printf(TEXT("%s: zoom unchanged"), Label), After.TargetArmLength, Before.TargetArmLength);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFCSInstantCameraTest, "FreeCameraSystem.Camera.InstantState",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFCSInstantCameraTest::RunTest(const FString&)
{
    FCameraControlLockFixture Fixture;
    if(!Fixture.Initialize(*this) || !Fixture.Possess(*this)) return false;
    auto* Pawn=Fixture.Pawn;
    auto* Arm=Pawn->FindComponentByClass<USpringArmComponent>();
    auto* Camera=Pawn->FindComponentByClass<UCameraComponent>();
    if(!TestNotNull(TEXT("Spring arm"),Arm) || !TestNotNull(TEXT("Camera"),Camera))return false;
    Fixture.Config->bEnableZoomEase=true;
    Pawn->ApplyCameraConfig();
    Arm->TickComponent(0.0f,LEVELTICK_All,nullptr);
    Pawn->ZoomCamera(3.0f);
    FFCS_CameraState Target=Pawn->GetCurrentCameraState();
    Target.Location=FVector(3000,700,-20000);Target.Yaw=125;Target.Pitch=-70;Target.TargetArmLength=1400;
    // Instant application must not use the state speeds.
    Target.MoveSpeed=1;Target.RotationSpeed=1;Target.ZoomSpeed=1;
    Fixture.CallbackProbe=Fixture.World->SpawnActor<AActor>();
    FFCS_OnCameraMoveFinished Callback;Callback.BindDynamic(Fixture.CallbackProbe,&AActor::K2_DestroyActor);
    Pawn->MoveToCameraState(Target,Callback);
    FFCS_CameraState Invalid=Target;Invalid.Pitch=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Invalid target rejected before cancellation"),Pawn->SetCameraStateInstant(Invalid));
    TestTrue(TEXT("Rejected snap retains old transition"),Pawn->IsMovingToCameraState() && Pawn->PendingCallback.IsBound());
    TestFalse(TEXT("No world is safe"),UFCS_FreeCameraBlueprintLibrary::SetFreeCameraStateInstant(nullptr,Target));
    const bool bWasPaused=UGameplayStatics::IsGamePaused(Fixture.World);
    UGameplayStatics::SetGamePaused(Fixture.World,true);
    TestTrue(TEXT("Instant node works while world is paused"),UFCS_FreeCameraBlueprintLibrary::SetFreeCameraStateInstant(Fixture.World,Target,true));
    const FVector Expected=Target.Location-FRotator(Target.Pitch,Target.Yaw,0).Vector()*Target.TargetArmLength;
    TestTrue(TEXT("Rendered camera component reaches target synchronously"),Camera->GetComponentLocation().Equals(Expected,.01));
    TestTrue(TEXT("Rotation reaches target synchronously"),Camera->GetComponentRotation().Equals(FRotator(Target.Pitch,Target.Yaw,0),.01));
    TestFalse(TEXT("Old move cancelled"),Pawn->IsMovingToCameraState());
    TestFalse(TEXT("Old completion delegate released"),Pawn->PendingCallback.IsBound());
    TestFalse(TEXT("Cancelled move never fires completion"),Fixture.CallbackProbe->IsActorBeingDestroyed());
    TestTrue(TEXT("Optional restore retains original lag flags"),Arm->bEnableCameraLag && Arm->bEnableCameraRotationLag);
    TestTrue(TEXT("Camera manager is notified of a cut"),Fixture.Controller->PlayerCameraManager->bGameCameraCutThisFrame);
    Arm->TickComponent(.016f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Restored lag does not pull camera towards old location"),Camera->GetComponentLocation().Equals(Expected,.01));
    UGameplayStatics::SetGamePaused(Fixture.World,bWasPaused);
    Fixture.Tick(.1f);
    TestEqual(TEXT("Old zoom ease does not resume"),Pawn->GetCurrentCameraState().TargetArmLength,Target.TargetArmLength);
    Pawn->SetCameraMovementDisabled(true);Pawn->SetCameraRotationDisabled(true);
    Target.Location=FVector(-50,25,-18000);Target.Yaw=-80;
    TestTrue(TEXT("Scripted snap bypasses manual control locks"),Pawn->SetCameraStateInstant(Target));
    TestTrue(TEXT("Snap preserves manual control locks"),Pawn->IsCameraMovementDisabled() && Pawn->IsCameraRotationDisabled());
    TestFalse(TEXT("Default leaves position lag off"),Arm->bEnableCameraLag);
    TestFalse(TEXT("Default leaves rotation lag off"),Arm->bEnableCameraRotationLag);
    TestTrue(TEXT("Second large snap is also immediate"),Camera->GetComponentLocation().Equals(Target.Location-FRotator(Target.Pitch,Target.Yaw,0).Vector()*Target.TargetArmLength,.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFCSCameraControlLocksTest, "FreeCameraSystem.Camera.ControlLocks",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFCSCameraControlLocksTest::RunTest(const FString& Parameters)
{
	FCameraControlLockFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	AFCS_FreeCameraPawn* Pawn = Fixture.Pawn;
	UWorld* World = Fixture.World;
	UFCS_CameraConfigDataAsset* Config = Fixture.Config;
	using Library = UFCS_FreeCameraBlueprintLibrary;

	TestFalse(TEXT("Movement initially unlocked"), Pawn->IsCameraMovementDisabled());
	TestFalse(TEXT("Rotation initially unlocked"), Pawn->IsCameraRotationDisabled());
	const TArray<const UObject*> InvalidContexts = { nullptr, NewObject<UFCS_CameraConfigDataAsset>() };
	for (const UObject* InvalidContext : InvalidContexts)
	{
		TestFalse(TEXT("Invalid context cannot lock movement"), Library::SetFreeCameraMovementDisabled(InvalidContext));
		TestFalse(TEXT("Invalid context cannot lock rotation"), Library::SetFreeCameraRotationDisabled(InvalidContext));
		TestFalse(TEXT("Invalid context movement query is safe"), Library::IsFreeCameraMovementDisabled(InvalidContext));
		TestFalse(TEXT("Invalid context rotation query is safe"), Library::IsFreeCameraRotationDisabled(InvalidContext));
	}
	Fixture.Subsystem->UnregisterFreeCamera(Pawn);
	TestFalse(TEXT("World without registered camera cannot lock movement"), Library::SetFreeCameraMovementDisabled(World));
	TestFalse(TEXT("World without registered camera cannot lock rotation"), Library::SetFreeCameraRotationDisabled(World));
	TestFalse(TEXT("World without registered camera movement query is safe"), Library::IsFreeCameraMovementDisabled(World));
	TestFalse(TEXT("World without registered camera rotation query is safe"), Library::IsFreeCameraRotationDisabled(World));
	Fixture.Subsystem->RegisterFreeCamera(Pawn);

	TestTrue(TEXT("Library locks movement with default true argument"), Library::SetFreeCameraMovementDisabled(World));
	TestTrue(TEXT("Movement state exposed by pawn"), Pawn->IsCameraMovementDisabled());
	TestTrue(TEXT("Movement state exposed by library"), Library::IsFreeCameraMovementDisabled(World));
	TestFalse(TEXT("Movement lock leaves rotation unlocked"), Library::IsFreeCameraRotationDisabled(World));
	const FFCS_CameraState MovementLocked = Pawn->GetCurrentCameraState();
	Pawn->MoveForward(1.0f);
	Pawn->MoveRight(1.0f);
	Pawn->ZoomCamera(1.0f);
	TestFalse(TEXT("Library forward rejected while locked"), Library::MoveFreeCameraForward(World, 1.0f));
	TestFalse(TEXT("Library strafe rejected while locked"), Library::MoveFreeCameraRight(World, 1.0f));
	TestFalse(TEXT("Library zoom rejected while locked"), Library::ZoomFreeCamera(World, 1.0f));
	Fixture.Tick();
	TestStateUnchanged(*this, TEXT("Blocked movement commands"), MovementLocked, Pawn->GetCurrentCameraState());
	TestTrue(TEXT("Mouse rotation remains available with movement locked"), Library::RotateFreeCameraByMouseDelta(World, 20.0f, 10.0f));
	TestFalse(TEXT("Movement lock permits yaw changes"), FMath::IsNearlyEqual(Pawn->GetCurrentCameraState().Yaw, MovementLocked.Yaw));
	TestFalse(TEXT("Movement lock permits pitch changes"), FMath::IsNearlyEqual(Pawn->GetCurrentCameraState().Pitch, MovementLocked.Pitch));

	Pawn->SetCameraMovementDisabled(false);
	TestTrue(TEXT("Library locks rotation with default true argument"), Library::SetFreeCameraRotationDisabled(World));
	TestTrue(TEXT("Rotation state exposed by pawn"), Pawn->IsCameraRotationDisabled());
	TestTrue(TEXT("Rotation state exposed by library"), Library::IsFreeCameraRotationDisabled(World));
	TestFalse(TEXT("Rotation lock leaves movement unlocked"), Library::IsFreeCameraMovementDisabled(World));
	const FFCS_CameraState RotationLocked = Pawn->GetCurrentCameraState();
	Pawn->RotateCamera(1.0f);
	Pawn->RotatePitch(1.0f);
	Pawn->SetCameraPitch(-30.0f);
	Pawn->RotateCameraByMouseDelta(100.0f, 100.0f);
	Pawn->TriggerMouseRotate(true);
	Pawn->SetPitchLimits(-50.0f, -40.0f);
	TestFalse(TEXT("Library yaw rejected while locked"), Library::RotateFreeCamera(World, 1.0f));
	TestFalse(TEXT("Library pitch rotation rejected while locked"), Library::RotateFreeCameraPitch(World, 1.0f));
	TestFalse(TEXT("Library pitch setter rejected while locked"), Library::SetFreeCameraPitch(World, -30.0f));
	TestFalse(TEXT("Library mouse delta rejected while locked"), Library::RotateFreeCameraByMouseDelta(World, 100.0f, 100.0f));
	TestFalse(TEXT("Library rotate start rejected while locked"), Library::TriggerFreeCameraMouseRotate(World, true));
	TestTrue(TEXT("Library rotate stop remains available while locked"), Library::TriggerFreeCameraMouseRotate(World, false));
	TestTrue(TEXT("Pitch limits remain configurable while locked"), Library::SetFreeCameraPitchLimits(World, -45.0f, -35.0f));
	TestStateUnchanged(*this, TEXT("Blocked rotation commands and limit clamp"), RotationLocked, Pawn->GetCurrentCameraState());
	TestTrue(TEXT("Forward movement remains available with rotation locked"), Library::MoveFreeCameraForward(World, 1.0f));
	TestTrue(TEXT("Strafe remains available with rotation locked"), Library::MoveFreeCameraRight(World, 1.0f));
	TestTrue(TEXT("Zoom remains available with rotation locked"), Library::ZoomFreeCamera(World, 1.0f));
	TestFalse(TEXT("Rotation lock permits location changes"), Pawn->GetActorLocation().Equals(RotationLocked.Location));
	TestTrue(TEXT("Rotation lock permits zoom changes"), Pawn->GetCurrentCameraState().TargetArmLength < RotationLocked.TargetArmLength);
	TestTrue(TEXT("Library unlocks rotation"), Library::SetFreeCameraRotationDisabled(World, false));
	TestTrue(TEXT("Pitch setter works after unlocking"), Library::SetFreeCameraPitch(World, -80.0f));
	TestEqual(TEXT("Limits configured while locked apply to later rotation"), Pawn->GetCurrentCameraState().Pitch, -45.0f);
	Pawn->SetPitchLimits(-80.0f, -25.0f);

	// Reloading configuration must not move dimensions protected by either lock.
	Pawn->SetActorLocation(FVector(500.0f, 600.0f, 123.0f));
	Pawn->SetCameraMovementDisabled(true);
	Pawn->SetCameraRotationDisabled(true);
	const FFCS_CameraState ConfigLocked = Pawn->GetCurrentCameraState();
	Config->bUseCameraBounds = true;
	Config->MinCameraBounds = FVector2D(-10.0f, -10.0f);
	Config->MaxCameraBounds = FVector2D(10.0f, 10.0f);
	Config->InitialTargetArmLength = 400.0f;
	Config->MaxZoomLength = 500.0f;
	Config->InitialPitch = -30.0f;
	Config->MinPitch = -35.0f;
	Config->MaxPitch = -25.0f;
	Pawn->ApplyCameraConfig();
	Fixture.Tick();
	TestStateUnchanged(*this, TEXT("Configuration while both dimensions locked"), ConfigLocked, Pawn->GetCurrentCameraState());
	Pawn->SetCameraRotationDisabled(false);
	Pawn->ApplyCameraConfig();
	TestEqual(TEXT("Config applies unlocked pitch"), Pawn->GetCurrentCameraState().Pitch, -30.0f);
	TestTrue(TEXT("Config keeps locked location"), Pawn->GetActorLocation().Equals(ConfigLocked.Location));
	TestEqual(TEXT("Config keeps locked zoom"), Pawn->GetCurrentCameraState().TargetArmLength, ConfigLocked.TargetArmLength);
	Pawn->SetCameraRotationDisabled(true);
	Pawn->SetCameraMovementDisabled(false);
	Config->InitialPitch = -25.0f;
	Pawn->ApplyCameraConfig();
	TestEqual(TEXT("Config keeps locked pitch when movement is free"), Pawn->GetCurrentCameraState().Pitch, -30.0f);
	TestTrue(TEXT("Config applies unlocked location bounds"), Pawn->GetActorLocation().Equals(FVector(10.0f, 10.0f, 123.0f)));
	TestEqual(TEXT("Config applies unlocked zoom"), Pawn->GetCurrentCameraState().TargetArmLength, 400.0f);

	Pawn->SetCameraRotationDisabled(false);
	Config->bUseCameraBounds = false;
	Config->MinPitch = -80.0f;
	Config->MaxPitch = -25.0f;
	Config->MaxZoomLength = 5000.0f;
	Config->InitialTargetArmLength = 2000.0f;
	Config->bEnableZoomEase = true;
	Pawn->ApplyCameraConfig();
	Pawn->ZoomCamera(1.0f);
	Fixture.Tick(0.025f);
	const float PartlyEasedZoom = Pawn->GetCurrentCameraState().TargetArmLength;
	TestTrue(TEXT("Zoom easing was active before locking"), PartlyEasedZoom < 2000.0f && PartlyEasedZoom > 1800.0f);
	Pawn->SetCameraMovementDisabled(true);
	Pawn->ZoomCamera(-5.0f);
	Fixture.Tick(0.2f);
	TestEqual(TEXT("Movement lock stops outstanding zoom easing"), Pawn->GetCurrentCameraState().TargetArmLength, PartlyEasedZoom);
	TestTrue(TEXT("Library unlocks movement"), Library::SetFreeCameraMovementDisabled(World, false));
	Fixture.Tick(0.2f);
	TestEqual(TEXT("Unlock does not resume old or blocked zoom input"), Pawn->GetCurrentCameraState().TargetArmLength, PartlyEasedZoom);
	Pawn->ZoomCamera(1.0f);
	Fixture.Tick();
	TestTrue(TEXT("Fresh zoom works after unlocking"), Pawn->GetCurrentCameraState().TargetArmLength < PartlyEasedZoom);

	FFCS_CameraState Target = Pawn->GetCurrentCameraState();
	Target.Location += FVector(500.0f, 300.0f, 25.0f);
	Target.Yaw = 80.0f;
	Target.Pitch = -70.0f;
	Target.TargetArmLength = 1200.0f;
	Fixture.CallbackProbe = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Completion callback probe spawned"), Fixture.CallbackProbe)) { return false; }
	FFCS_OnCameraMoveFinished Callback;
	Callback.BindDynamic(Fixture.CallbackProbe, &AActor::K2_DestroyActor);
	Pawn->MoveToCameraState(Target, Callback);
	Pawn->SetCameraMovementDisabled(true);
	Pawn->SetCameraRotationDisabled(true);
	TestTrue(TEXT("Locking does not cancel an active scripted move"), Pawn->IsMovingToCameraState());
	TestTrue(TEXT("Locking preserves the pending completion callback"), Pawn->PendingCallback.IsBound());
	TestEqual(TEXT("Locking preserves the callback target function"), Pawn->PendingCallback.GetFunctionName(), Callback.GetFunctionName());
	TestFalse(TEXT("Locking does not fire the completion callback early"), Fixture.CallbackProbe->IsActorBeingDestroyed());
	// Starting a transition through the library is also supported while both locks are set.
	TestTrue(TEXT("Library starts scripted move while locked"), Library::MoveFreeCameraToState(World, Target, Callback));
	for (int32 Step = 0; Step < 100 && Pawn->IsMovingToCameraState(); ++Step)
	{
		const FFCS_CameraState BeforeInput = Pawn->GetCurrentCameraState();
		Pawn->MoveForward(1.0f);
		Pawn->MoveRight(1.0f);
		Pawn->ZoomCamera(1.0f);
		Pawn->RotateCamera(1.0f);
		Pawn->RotatePitch(1.0f);
		Pawn->SetCameraPitch(-25.0f);
		Pawn->RotateCameraByMouseDelta(100.0f, 100.0f);
		TestFalse(TEXT("Manual library movement remains blocked during transition"), Library::MoveFreeCameraForward(World, 1.0f));
		TestFalse(TEXT("Manual library rotation remains blocked during transition"), Library::RotateFreeCameraByMouseDelta(World, 100.0f, 100.0f));
		TestStateUnchanged(*this, TEXT("Manual input during scripted move"), BeforeInput, Pawn->GetCurrentCameraState());
		Fixture.Tick(0.1f);
	}
	TestFalse(TEXT("Scripted move finishes with both locks set"), Pawn->IsMovingToCameraState());
	TestStateUnchanged(*this, TEXT("Scripted move reaches every target dimension"), Target, Pawn->GetCurrentCameraState());
	TestTrue(TEXT("Scripted move executes the completion callback while locked"), Fixture.CallbackProbe->IsActorBeingDestroyed());
	TestFalse(TEXT("Completion clears pending callback"), Pawn->PendingCallback.IsBound());
	TestTrue(TEXT("Automatic move preserves movement lock"), Pawn->IsCameraMovementDisabled());
	TestTrue(TEXT("Automatic move preserves rotation lock"), Pawn->IsCameraRotationDisabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFCSCameraDefaultInputLocksTest, "FreeCameraSystem.Camera.DefaultInputLocks",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFCSCameraDefaultInputLocksTest::RunTest(const FString& Parameters)
{
	FCameraControlLockFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Possess(*this)) { return false; }
	AFCS_FreeCameraPawn* Pawn = Fixture.Pawn;
	APlayerController* PC = Fixture.Controller;
	TestEqual(TEXT("Default key bindings installed"), Fixture.Input->KeyBindings.Num(), 16);
	Fixture.SendKey(EKeys::W, IE_Pressed);
	Fixture.SendKey(EKeys::E, IE_Pressed);
	Fixture.Tick();
	TestTrue(TEXT("Held W moves the camera"), Pawn->GetActorLocation().X > 0.0f);
	TestTrue(TEXT("Held E rotates the camera"), Pawn->GetActorRotation().Yaw > 0.0f);
	Pawn->SetCameraMovementDisabled(true);
	const FFCS_CameraState MovementLocked = Pawn->GetCurrentCameraState();
	Fixture.SendKey(EKeys::D, IE_Pressed);
	Fixture.SendKey(EKeys::MouseScrollUp, IE_Pressed);
	Fixture.SendKey(EKeys::MouseScrollUp, IE_Released);
	Fixture.Tick();
	TestTrue(TEXT("Movement lock blocks held W and newly pressed D"), Pawn->GetActorLocation().Equals(MovementLocked.Location));
	TestEqual(TEXT("Movement lock blocks the default scroll binding"), Pawn->GetCurrentCameraState().TargetArmLength, MovementLocked.TargetArmLength);
	TestFalse(TEXT("Movement lock preserves held E rotation"), FMath::IsNearlyEqual(Pawn->GetActorRotation().Yaw, MovementLocked.Yaw));
	Pawn->SetCameraMovementDisabled(false);
	Fixture.Tick();
	TestTrue(TEXT("Unlock does not reuse old W or locked D presses"), Pawn->GetActorLocation().Equals(MovementLocked.Location));
	Fixture.SendKey(EKeys::W, IE_Released);
	Fixture.SendKey(EKeys::D, IE_Released);
	Fixture.SendKey(EKeys::W, IE_Pressed);
	Fixture.Tick();
	TestFalse(TEXT("Fresh W press works after unlocking"), Pawn->GetActorLocation().Equals(MovementLocked.Location));
	Pawn->SetCameraRotationDisabled(true);
	const FFCS_CameraState RotationLocked = Pawn->GetCurrentCameraState();
	Fixture.Tick();
	TestEqual(TEXT("Rotation lock blocks held E"), Pawn->GetCurrentCameraState().Yaw, RotationLocked.Yaw);
	TestFalse(TEXT("Rotation lock preserves held W movement"), Pawn->GetActorLocation().Equals(RotationLocked.Location));
	Pawn->SetCameraRotationDisabled(false);
	Fixture.Tick();
	TestEqual(TEXT("Unlock does not reuse old E press"), Pawn->GetCurrentCameraState().Yaw, RotationLocked.Yaw);
	Fixture.SendKey(EKeys::E, IE_Released);
	Pawn->SetCameraRotationDisabled(true);
	Fixture.SendKey(EKeys::Q, IE_Pressed);
	Fixture.Tick();
	TestEqual(TEXT("Rotation lock blocks new Q press"), Pawn->GetCurrentCameraState().Yaw, RotationLocked.Yaw);
	Pawn->SetCameraRotationDisabled(false);
	Fixture.Tick();
	TestEqual(TEXT("Unlock does not reuse locked Q press"), Pawn->GetCurrentCameraState().Yaw, RotationLocked.Yaw);
	Fixture.SendKey(EKeys::W, IE_Released);
	Fixture.SendKey(EKeys::Q, IE_Released);
	Fixture.SendKey(EKeys::Q, IE_Pressed);
	Fixture.Tick();
	TestFalse(TEXT("Fresh Q press works after unlocking"), FMath::IsNearlyEqual(Pawn->GetActorRotation().Yaw, RotationLocked.Yaw));
	Fixture.SendKey(EKeys::Q, IE_Released);

	PC->bShowMouseCursor = true;
	Fixture.SendKey(EKeys::RightMouseButton, IE_Pressed);
	TestTrue(TEXT("Default rotate press enables mouse rotation"), Pawn->bMouseRotateMode);
	TestFalse(TEXT("Mouse rotation hides cursor"), PC->bShowMouseCursor);
	Pawn->SetCameraMovementDisabled(true);
	TestTrue(TEXT("Movement lock preserves mouse rotation mode"), Pawn->bMouseRotateMode);
	Pawn->SetCameraRotationDisabled(true);
	TestFalse(TEXT("Rotation lock immediately exits mouse rotation"), Pawn->bMouseRotateMode);
	TestTrue(TEXT("Rotation lock immediately restores cursor"), PC->bShowMouseCursor);
	Fixture.SendKey(EKeys::RightMouseButton, IE_Released);
	Fixture.SendKey(EKeys::RightMouseButton, IE_Pressed);
	TestFalse(TEXT("Locked rotate press cannot re-enter rotation"), Pawn->bMouseRotateMode);
	Pawn->SetCameraRotationDisabled(false);
	Fixture.Tick();
	TestFalse(TEXT("Unlock does not reuse a locked mouse press"), Pawn->bMouseRotateMode);
	Fixture.SendKey(EKeys::RightMouseButton, IE_Released);
	Fixture.SendKey(EKeys::RightMouseButton, IE_Pressed);
	TestTrue(TEXT("Fresh mouse press works after unlocking"), Pawn->bMouseRotateMode);
	Fixture.SendKey(EKeys::RightMouseButton, IE_Released);
	TestTrue(TEXT("Mouse release restores cursor"), PC->bShowMouseCursor);
	Pawn->SetCameraMovementDisabled(false);

	int32 Width = 0;
	int32 Height = 0;
	PC->GetViewportSize(Width, Height);
	if (Width > 40 && Height > 40)
	{
		PC->SetMouseLocation(1, Height / 2);
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (PC->GetMousePosition(MouseX, MouseY) && MouseX >= 0.0f && MouseX < 20.0f && MouseY > 20.0f && MouseY < Height - 20.0f)
		{
			Pawn->SetEdgeScrollEnabled(true);
			Pawn->SetCameraRotationDisabled(true);
			const FVector BeforeEdgeScroll = Pawn->GetActorLocation();
			Fixture.Tick();
			TestFalse(TEXT("Rotation lock leaves edge scrolling available"), Pawn->GetActorLocation().Equals(BeforeEdgeScroll));
			Pawn->SetCameraMovementDisabled(true);
			const FVector EdgeLocked = Pawn->GetActorLocation();
			Fixture.Tick();
			TestTrue(TEXT("Movement lock blocks edge scrolling"), Pawn->GetActorLocation().Equals(EdgeLocked));
			Pawn->SetCameraMovementDisabled(false);
			Fixture.Tick();
			TestFalse(TEXT("Unlock restores previously enabled edge scrolling"), Pawn->GetActorLocation().Equals(EdgeLocked));
			Pawn->SetEdgeScrollEnabled(false);
			Pawn->SetCameraMovementDisabled(true);
			Pawn->SetCameraMovementDisabled(false);
			const FVector EdgeDisabled = Pawn->GetActorLocation();
			Fixture.Tick();
			TestTrue(TEXT("Lock cycle preserves disabled edge scrolling too"), Pawn->GetActorLocation().Equals(EdgeDisabled));
		}
		else { AddInfo(TEXT("Edge scrolling checks skipped: viewport did not accept the test mouse position.")); }
	}
	else { AddInfo(TEXT("Edge scrolling checks skipped: no usable rendered viewport.")); }
	return true;
}
#endif
