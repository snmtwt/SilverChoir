#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_CameraConfigDataAsset.h"
#include "FCS_FreeCameraSubsystem.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InputComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFCSCameraTest, "FreeCameraSystem.Camera.ConfigMovementAndLifecycle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFCSCameraTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
	}
	if (!TestNotNull(TEXT("Game world available"), World)) { return false; }
	UFCS_FreeCameraSubsystem* Subsystem = World->GetSubsystem<UFCS_FreeCameraSubsystem>();
	if (!TestNotNull(TEXT("Standalone camera subsystem created"), Subsystem)) { return false; }
	AFCS_FreeCameraPawn* Pawn = World->SpawnActor<AFCS_FreeCameraPawn>();
	if (!TestNotNull(TEXT("Camera spawned"), Pawn)) { return false; }
	TestTrue(TEXT("BeginPlay registers camera"), Subsystem->GetFreeCamera() == Pawn);
	TestTrue(TEXT("Blueprint library resolves same camera"), UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World) == Pawn);
	TestNull(TEXT("Invalid blueprint context safe"), UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(nullptr));
	TestTrue(TEXT("Pawn does not steal player possession"), Pawn->AutoPossessPlayer == EAutoReceiveInput::Disabled);

	UFCS_CameraConfigDataAsset* Config = NewObject<UFCS_CameraConfigDataAsset>(Pawn);
	Config->bUseZoomInterpolatedMoveSpeed = false;
	Config->MoveSpeed = 1000.0f;
	Config->bUseCameraBounds = true;
	Config->MinCameraBounds = FVector2D(100, 200);
	Config->MaxCameraBounds = FVector2D(-100, -200);
	Config->MinPitch = -20;
	Config->MaxPitch = -80;
	Config->MinZoomLength = 3000;
	Config->MaxZoomLength = 1000;
	Config->InitialTargetArmLength = 4000;
	Config->bEnableZoomEase = false;
	Config->bHideCursorWhenRotate = true;
	Pawn->CameraConfig = Config;
	Pawn->SetActorLocation(FVector(500, 500, 123));
	Pawn->ApplyCameraConfig();
	TestTrue(TEXT("Reversed bounds normalized and applied"), Pawn->GetActorLocation().Equals(FVector(100, 200, 123)));
	TestEqual(TEXT("Initial zoom clamped to normalized range"), Pawn->GetCurrentCameraState().TargetArmLength, 3000.0f);
	Pawn->ZoomCamera(100);
	TestEqual(TEXT("Zoom in clamped"), Pawn->GetCurrentCameraState().TargetArmLength, 1000.0f);
	Pawn->ZoomCamera(-100);
	TestEqual(TEXT("Zoom out clamped"), Pawn->GetCurrentCameraState().TargetArmLength, 3000.0f);
	Pawn->SetCameraPitch(90);
	TestEqual(TEXT("Pitch max clamped"), Pawn->GetCurrentCameraState().Pitch, -20.0f);
	Pawn->SetCameraPitch(-90);
	TestEqual(TEXT("Pitch min clamped"), Pawn->GetCurrentCameraState().Pitch, -80.0f);
	Pawn->SetActorLocation(FVector(0, 0, 123));
	Pawn->SetActorRotation(FRotator::ZeroRotator);
	Pawn->MoveForward(1);
	TestTrue(TEXT("Forward movement preserves height"), Pawn->GetActorLocation().X > 0 && Pawn->GetActorLocation().Z == 123);
	Pawn->MoveRight(1);
	TestTrue(TEXT("Right movement follows yaw plane"), Pawn->GetActorLocation().Y > 0);
	Pawn->RotateCameraByMouseDelta(50, 0);
	TestTrue(TEXT("Mouse delta uses configured sensitivity"), FMath::IsNearlyEqual(Pawn->GetActorRotation().Yaw, 10.0f));

	Config->bEnableZoomEase = true;
	Config->InitialTargetArmLength = 2000;
	Pawn->ApplyCameraConfig();
	Pawn->ZoomCamera(1);
	TestEqual(TEXT("Eased zoom does not jump immediately"), Pawn->GetCurrentCameraState().TargetArmLength, 2000.0f);
	Pawn->TickActor(0.05f, LEVELTICK_All, Pawn->PrimaryActorTick);
	TestTrue(TEXT("Eased zoom advances toward target"), Pawn->GetCurrentCameraState().TargetArmLength < 2000 && Pawn->GetCurrentCameraState().TargetArmLength > 1800);

	FFCS_CameraState Target;
	Target.Location = FVector(-50, -100, 321);
	Target.Yaw = -170;
	Target.Pitch = -45;
	Target.TargetArmLength = 1500;
	Target.MoveSpeed = 1000;
	Pawn->MoveToCameraState(Target, FFCS_OnCameraMoveFinished());
	for (int32 Step = 0; Step < 100 && Pawn->IsMovingToCameraState(); ++Step)
	{
		Pawn->TickActor(0.1f, LEVELTICK_All, Pawn->PrimaryActorTick);
	}
	TestFalse(TEXT("Camera transition finishes"), Pawn->IsMovingToCameraState());
	const FFCS_CameraState Result = Pawn->GetCurrentCameraState();
	TestTrue(TEXT("Transition reaches location"), Result.Location.Equals(Target.Location));
	TestTrue(TEXT("Transition reaches yaw"), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Result.Yaw, Target.Yaw)));
	TestEqual(TEXT("Transition reaches pitch"), Result.Pitch, Target.Pitch);
	TestEqual(TEXT("Transition reaches zoom"), Result.TargetArmLength, Target.TargetArmLength);
	Target.Location = FVector(500, 500, 321);
	Pawn->MoveToCameraState(Target, FFCS_OnCameraMoveFinished());
	Pawn->CancelCameraStateMove();
	TestFalse(TEXT("Transition can be cancelled"), Pawn->IsMovingToCameraState());

	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("Default keyboard, scroll and rotate bindings installed"), Input->KeyBindings.Num(), 16);
	Pawn->bEnableDefaultInputBindings = false;
	UInputComponent* CustomInput = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(CustomInput);
	TestEqual(TEXT("Custom input can opt out of all default bindings"), CustomInput->KeyBindings.Num(), 0);

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		APawn* PreviousPawn = PC->GetPawn();
		const bool bPreviousCursor = PC->bShowMouseCursor;
		PC->Possess(Pawn);
		PC->bShowMouseCursor = true;
		Pawn->TriggerMouseRotate(true);
		TestFalse(TEXT("Rotate hides cursor when configured"), PC->bShowMouseCursor);
		PC->UnPossess();
		TestTrue(TEXT("Unpossess restores cursor"), PC->bShowMouseCursor);
		TestFalse(TEXT("Unpossess clears rotation mode"), Pawn->bMouseRotateMode);
		if (IsValid(PreviousPawn)) { PC->Possess(PreviousPawn); }
		PC->bShowMouseCursor = bPreviousCursor;
	}
	else { AddError(TEXT("No player controller for possession lifecycle checks")); }
	Pawn->Destroy();
	TestNull(TEXT("EndPlay unregisters camera"), Subsystem->GetFreeCamera());
	return true;
}
#endif
