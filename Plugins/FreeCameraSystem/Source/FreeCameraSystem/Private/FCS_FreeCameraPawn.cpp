#include "FCS_FreeCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "FCS_CameraConfigDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "FCS_FreeCameraSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{

void EnsureCameraMoveSpeeds(FFCS_CameraState& CameraState)
{
	const FFCS_CameraState DefaultState;

	if (CameraState.MoveSpeed <= 0.0f)
	{
		CameraState.MoveSpeed = DefaultState.MoveSpeed;
	}

	if (CameraState.RotationSpeed <= 0.0f)
	{
		CameraState.RotationSpeed = DefaultState.RotationSpeed;
	}

	if (CameraState.ZoomSpeed <= 0.0f)
	{
		CameraState.ZoomSpeed = DefaultState.ZoomSpeed;
	}
}
}

AFCS_FreeCameraPawn::AFCS_FreeCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(SceneRoot);
	SpringArm->TargetArmLength = 2000.0f;
	TargetZoomLength = SpringArm->TargetArmLength;
	SpringArm->SetRelativeRotation(FRotator(-60.0f, 0.0f, 0.0f));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.0f;
	SpringArm->CameraLagMaxDistance = 0.0f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 10.0f;
	SpringArm->bUsePawnControlRotation = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AFCS_FreeCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (!bEnableDefaultInputBindings || !PlayerInputComponent) { return; }
	for (const FKey Key : { EKeys::W, EKeys::S, EKeys::A, EKeys::D, EKeys::Q, EKeys::E })
	{
		FInputKeyBinding Press(FInputChord(Key), IE_Pressed);
		Press.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Key]()
		{
			const bool bRotationKey = Key == EKeys::Q || Key == EKeys::E;
			if (bRotationKey ? !bCameraRotationDisabled : !bCameraMovementDisabled) { HeldKeys.Add(Key); }
		});
		PlayerInputComponent->KeyBindings.Add(Press);
		FInputKeyBinding Release(FInputChord(Key), IE_Released);
		Release.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Key]() { HeldKeys.Remove(Key); });
		PlayerInputComponent->KeyBindings.Add(Release);
	}
	FInputKeyBinding ZoomIn(FInputChord(EKeys::MouseScrollUp), IE_Pressed);
	ZoomIn.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { ZoomCamera(1.0f); });
	PlayerInputComponent->KeyBindings.Add(ZoomIn);
	FInputKeyBinding ZoomOut(FInputChord(EKeys::MouseScrollDown), IE_Pressed);
	ZoomOut.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { ZoomCamera(-1.0f); });
	PlayerInputComponent->KeyBindings.Add(ZoomOut);
	if (bEnableDefaultMouseRotationBindings)
	{
	FInputKeyBinding RotateStart(FInputChord(EKeys::RightMouseButton), IE_Pressed);
	RotateStart.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { TriggerMouseRotate(true); });
	PlayerInputComponent->KeyBindings.Add(RotateStart);
	FInputKeyBinding RotateStop(FInputChord(EKeys::RightMouseButton), IE_Released);
	RotateStop.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { TriggerMouseRotate(false); });
	PlayerInputComponent->KeyBindings.Add(RotateStop);
	}
}

void AFCS_FreeCameraPawn::UnPossessed()
{
	TriggerMouseRotate(false);
	HeldKeys.Reset();
	Super::UnPossessed();
}

void AFCS_FreeCameraPawn::CancelCameraStateMove()
{
	bIsBlendingToCameraState = false;
	PendingCallback.Unbind();
}

void AFCS_FreeCameraPawn::SetCameraMovementDisabled(bool bDisabled)
{
	bCameraMovementDisabled = bDisabled;
	if (bDisabled)
	{
		for (const FKey Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D}) { HeldKeys.Remove(Key); }
		// Drop any manual zoom easing remainder; unlocking must not replay the old scroll input.
		if (SpringArm) { TargetZoomLength = SpringArm->TargetArmLength; }
	}
}

void AFCS_FreeCameraPawn::SetCameraRotationDisabled(bool bDisabled)
{
	bCameraRotationDisabled = bDisabled;
	if (bDisabled)
	{
		HeldKeys.Remove(EKeys::Q);
		HeldKeys.Remove(EKeys::E);
		TriggerMouseRotate(false); // Restore the cursor even when disabled during a drag.
	}
}

void AFCS_FreeCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyCameraConfig();

	if (UWorld* World = GetWorld())
	{
		if (UFCS_FreeCameraSubsystem* CoreSubsystem = World->GetSubsystem<UFCS_FreeCameraSubsystem>())
		{
			CoreSubsystem->RegisterFreeCamera(this);
		}
	}
}

void AFCS_FreeCameraPawn::ApplyCameraConfig()
{
	TriggerMouseRotate(false);
	CancelCameraStateMove();
	float InitialTargetArmLength = 2000.0f;
	float InitialPitch = -60.0f;
	float NewMinZoomLength = 0.0f;
	float NewMaxZoomLength = 5000.0f;
	float NewMinPitch = -80.0f;
	float NewMaxPitch = -25.0f;
	FVector2D NewMinCameraBounds(-10000.0f, -10000.0f);
	FVector2D NewMaxCameraBounds(10000.0f, 10000.0f);
	bool bIgnoreCameraArmCollision = true;
	bool bEnableSpringArmCameraLag = true;
	float NewCameraLagSpeed = 10.0f;
	float NewCameraLagMaxDistance = 0.0f;
	bool bEnableSpringArmCameraRotationLag = true;
	float NewCameraRotationLagSpeed = 10.0f;

	bUseZoomInterpolatedMoveSpeed = true;
	MoveSpeed = 3000.0f;
	MinMoveSpeed = 1000.0f;
	MaxMoveSpeed = 5000.0f;
	EdgeMoveSpeedScale = 1.0f;
	RotationSpeed = 100.0f;
	ZoomStep = 200.0f;
	bEnableZoomEase = true;
	ZoomEaseSpeed = 10.0f;
	bEnableEdgeScroll = false;
	EdgeScrollThreshold = 20.0f;
	bUseCameraBounds = false;
	MouseYawSpeed = 0.2f;
	MousePitchSpeed = 0.2f;
	bHideCursorWhenRotate = false;

	if (CameraConfig)
	{
		InitialTargetArmLength = CameraConfig->InitialTargetArmLength;
		InitialPitch = CameraConfig->InitialPitch;
		bUseZoomInterpolatedMoveSpeed = CameraConfig->bUseZoomInterpolatedMoveSpeed;
		MoveSpeed = FMath::Max(CameraConfig->MoveSpeed, 0.0f);
		MinMoveSpeed = CameraConfig->MinMoveSpeed;
		MaxMoveSpeed = CameraConfig->MaxMoveSpeed;
		EdgeMoveSpeedScale = FMath::Max(CameraConfig->EdgeMoveSpeedScale, 0.0f);
		RotationSpeed = FMath::Max(CameraConfig->RotationSpeed, 0.0f);
		ZoomStep = FMath::Max(CameraConfig->ZoomStep, 0.0f);
		bEnableZoomEase = CameraConfig->bEnableZoomEase;
		ZoomEaseSpeed = FMath::Max(CameraConfig->ZoomEaseSpeed, 0.0f);
		NewMinZoomLength = CameraConfig->MinZoomLength;
		NewMaxZoomLength = CameraConfig->MaxZoomLength;
		bEnableEdgeScroll = CameraConfig->bEnableEdgeScroll;
		EdgeScrollThreshold = FMath::Max(CameraConfig->EdgeScrollThreshold, 0.0f);
		bUseCameraBounds = CameraConfig->bUseCameraBounds;
		NewMinCameraBounds = CameraConfig->MinCameraBounds;
		NewMaxCameraBounds = CameraConfig->MaxCameraBounds;
		MouseYawSpeed = FMath::Max(CameraConfig->MouseYawSpeed, 0.0f);
		MousePitchSpeed = FMath::Max(CameraConfig->MousePitchSpeed, 0.0f);
		NewMinPitch = CameraConfig->MinPitch;
		NewMaxPitch = CameraConfig->MaxPitch;
		bHideCursorWhenRotate = CameraConfig->bHideCursorWhenRotate;
		bIgnoreCameraArmCollision = CameraConfig->bIgnoreCameraArmCollision;
		bEnableSpringArmCameraLag = CameraConfig->bEnableCameraLag;
		NewCameraLagSpeed = FMath::Max(CameraConfig->CameraLagSpeed, 0.0f);
		NewCameraLagMaxDistance = FMath::Max(CameraConfig->CameraLagMaxDistance, 0.0f);
		bEnableSpringArmCameraRotationLag = CameraConfig->bEnableCameraRotationLag;
		NewCameraRotationLagSpeed = FMath::Max(CameraConfig->CameraRotationLagSpeed, 0.0f);
	}

	MinZoomLength = FMath::Max(0.0f, FMath::Min(NewMinZoomLength, NewMaxZoomLength));
	MaxZoomLength = FMath::Max(0.0f, FMath::Max(NewMinZoomLength, NewMaxZoomLength));
	const float NewMinMoveSpeed = FMath::Max(FMath::Min(MinMoveSpeed, MaxMoveSpeed), 0.0f);
	const float NewMaxMoveSpeed = FMath::Max(FMath::Max(MinMoveSpeed, MaxMoveSpeed), 0.0f);
	MinMoveSpeed = NewMinMoveSpeed;
	MaxMoveSpeed = NewMaxMoveSpeed;
	MinPitch = FMath::Min(NewMinPitch, NewMaxPitch);
	MaxPitch = FMath::Max(NewMinPitch, NewMaxPitch);
	MinCameraBounds = FVector2D(
		FMath::Min(NewMinCameraBounds.X, NewMaxCameraBounds.X),
		FMath::Min(NewMinCameraBounds.Y, NewMaxCameraBounds.Y)
	);
	MaxCameraBounds = FVector2D(
		FMath::Max(NewMinCameraBounds.X, NewMaxCameraBounds.X),
		FMath::Max(NewMinCameraBounds.Y, NewMaxCameraBounds.Y)
	);

	if (SpringArm)
	{
		SpringArm->bDoCollisionTest = !bIgnoreCameraArmCollision;
		SpringArm->bEnableCameraLag = bEnableSpringArmCameraLag;
		SpringArm->CameraLagSpeed = NewCameraLagSpeed;
		SpringArm->CameraLagMaxDistance = NewCameraLagMaxDistance;
		SpringArm->bEnableCameraRotationLag = bEnableSpringArmCameraRotationLag;
		SpringArm->CameraRotationLagSpeed = NewCameraRotationLagSpeed;
		if (!bCameraMovementDisabled)
		{
			SpringArm->TargetArmLength = FMath::Clamp(InitialTargetArmLength, MinZoomLength, MaxZoomLength);
		}
		TargetZoomLength = SpringArm->TargetArmLength;

		if (!bCameraRotationDisabled)
		{
			FRotator ArmRot = SpringArm->GetRelativeRotation();
			ArmRot.Pitch = FMath::Clamp(InitialPitch, MinPitch, MaxPitch);
			SpringArm->SetRelativeRotation(ArmRot);
		}
	}

	if (!bCameraRotationDisabled) { ClampSpringArmPitch(); }
	if (!bCameraMovementDisabled) { ClampCameraLocation(); }
}

void AFCS_FreeCameraPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TriggerMouseRotate(false);
	CancelCameraStateMove();
	HeldKeys.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UFCS_FreeCameraSubsystem* CoreSubsystem = World->GetSubsystem<UFCS_FreeCameraSubsystem>())
		{
			CoreSubsystem->UnregisterFreeCamera(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFCS_FreeCameraPawn::CopyCurrentCameraStateToClipboard()
{
	FString CameraStateText;
	if (!TryCopyCurrentCameraStateToClipboard(CameraStateText))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to copy current camera state to clipboard."));
	}
}



bool AFCS_FreeCameraPawn::TryCopyCurrentCameraStateToClipboard(FString& OutCameraStateText) const
{
	OutCameraStateText.Reset();

	const FFCS_CameraState CurrentCameraState = GetCurrentCameraState();
	FFCS_CameraState::StaticStruct()->ExportText(
		OutCameraStateText,
		&CurrentCameraState,
		nullptr,
		nullptr,
		0,
		nullptr
	);

	if (OutCameraStateText.IsEmpty())
	{
		return false;
	}

	FPlatformApplicationMisc::ClipboardCopy(*OutCameraStateText);
	return true;
}



void AFCS_FreeCameraPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	APlayerController* PC = GetOwningPlayerController();
	// 清除 UI 输入模式切换、窗口失焦等情况下未收到 Released 的按键。
	if (PC && bEnableDefaultInputBindings)
	{
		for (auto It = HeldKeys.CreateIterator(); It; ++It)
		{
			if (!PC->IsInputKeyDown(*It)) { It.RemoveCurrent(); }
		}
		if (bEnableDefaultMouseRotationBindings && bMouseRotateMode && !PC->IsInputKeyDown(EKeys::RightMouseButton)) { TriggerMouseRotate(false); }
	}

	if (bIsBlendingToCameraState)
	{
		UpdateCameraStateBlend(DeltaTime);
		return;
	}

	if (PC && bEnableDefaultInputBindings && !PC->IsMoveInputIgnored())
	{
		MoveForward(float(HeldKeys.Contains(EKeys::W)) - float(HeldKeys.Contains(EKeys::S)));
		MoveRight(float(HeldKeys.Contains(EKeys::D)) - float(HeldKeys.Contains(EKeys::A)));
		RotateCamera(float(HeldKeys.Contains(EKeys::E)) - float(HeldKeys.Contains(EKeys::Q)));
	}

	if (bMouseRotateMode && !bCameraRotationDisabled)
	{
		HandleMouseRotate();
	}

	UpdateZoomEase(DeltaTime);

	if (bEnableEdgeScroll && !bMouseRotateMode && !bCameraMovementDisabled)
	{
		HandleEdgeScroll(DeltaTime);
	}
}

void AFCS_FreeCameraPawn::MoveForward(float Value)
{
	if (bCameraMovementDisabled || bIsBlendingToCameraState || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	AddCameraMovement(Forward, Value, GetCurrentMoveSpeed());
}

void AFCS_FreeCameraPawn::MoveRight(float Value)
{
	if (bCameraMovementDisabled || bIsBlendingToCameraState || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddCameraMovement(Right, Value, GetCurrentMoveSpeed());
}

void AFCS_FreeCameraPawn::RotateCamera(float Value)
{
	if (bCameraRotationDisabled || bIsBlendingToCameraState || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	const float YawDelta = Value * RotationSpeed * DeltaTime;
	AddActorWorldRotation(FRotator(0.0f, YawDelta, 0.0f));
}

void AFCS_FreeCameraPawn::RotatePitch(float Value)
{
	if (bCameraRotationDisabled || bIsBlendingToCameraState || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	const float PitchDelta = Value * RotationSpeed * DeltaTime;

	FRotator ArmRot = SpringArm->GetRelativeRotation();
	ArmRot.Pitch = FMath::Clamp(ArmRot.Pitch + PitchDelta, MinPitch, MaxPitch);
	SpringArm->SetRelativeRotation(ArmRot);
}

void AFCS_FreeCameraPawn::ZoomCamera(float Value)
{
	if (bCameraMovementDisabled || bIsBlendingToCameraState || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float BaseArmLength = bEnableZoomEase ? TargetZoomLength : SpringArm->TargetArmLength;
	TargetZoomLength = FMath::Clamp(BaseArmLength - (Value * ZoomStep), MinZoomLength, MaxZoomLength);

	if (!bEnableZoomEase || ZoomEaseSpeed <= 0.0f)
	{
		SpringArm->TargetArmLength = TargetZoomLength;
	}
}

void AFCS_FreeCameraPawn::SetEdgeScrollEnabled(bool bEnabled)
{
	bEnableEdgeScroll = bEnabled;
}

void AFCS_FreeCameraPawn::SetCameraPitch(float NewPitch)
{
	if (bCameraRotationDisabled) { return; }
	FRotator ArmRot = SpringArm->GetRelativeRotation();
	ArmRot.Pitch = FMath::Clamp(NewPitch, MinPitch, MaxPitch);
	SpringArm->SetRelativeRotation(ArmRot);
}

void AFCS_FreeCameraPawn::SetPitchLimits(float InMinPitch, float InMaxPitch)
{
	MinPitch = FMath::Min(InMinPitch, InMaxPitch);
	MaxPitch = FMath::Max(InMinPitch, InMaxPitch);
	if (!bCameraRotationDisabled) { ClampSpringArmPitch(); }
}

void AFCS_FreeCameraPawn::TriggerMouseRotate(bool bEnable)
{
	if (!bEnable)
	{
		if (bMouseRotateMode && RotationController.IsValid())
		{
			RotationController->bShowMouseCursor = bCachedShowMouseCursor;
		}
		bMouseRotateMode = false;
		bMouseRotateInitialized = false;
		RotationController.Reset();
		return;
	}
	if (bCameraRotationDisabled) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	if (bEnable == bMouseRotateMode)
	{
		return;
	}

	bMouseRotateMode = bEnable;

	if (bEnable)
	{
		bCachedShowMouseCursor = PC->bShowMouseCursor;
		RotationController = PC;

		if (bHideCursorWhenRotate)
		{
			PC->bShowMouseCursor = false;
		}

		float MouseX = 0.0f;
		float MouseY = 0.0f;
		FVector2D ViewportSize = FVector2D::ZeroVector;

		if (GetMousePositionInViewport(PC, MouseX, MouseY, ViewportSize))
		{
			LastMouseScreenPosition = FVector2D(MouseX, MouseY);
			bMouseRotateInitialized = true;
		}
		else
		{
			bMouseRotateInitialized = false;
		}
	}
	else
	{
		PC->bShowMouseCursor = bCachedShowMouseCursor;
		bMouseRotateInitialized = false;
	}
}

void AFCS_FreeCameraPawn::RotateCameraByMouseDelta(float DeltaX, float DeltaY)
{
	if (bCameraRotationDisabled || bIsBlendingToCameraState || (FMath::IsNearlyZero(DeltaX) && FMath::IsNearlyZero(DeltaY)))
	{
		return;
	}

	const float YawDelta = DeltaX * MouseYawSpeed;
	AddActorWorldRotation(FRotator(0.0f, YawDelta, 0.0f));

	FRotator ArmRot = SpringArm->GetRelativeRotation();
	ArmRot.Pitch = FMath::Clamp(ArmRot.Pitch + (-DeltaY * MousePitchSpeed), MinPitch, MaxPitch);
	SpringArm->SetRelativeRotation(ArmRot);
}

float AFCS_FreeCameraPawn::GetCurrentMoveSpeed() const
{
	if (!bUseZoomInterpolatedMoveSpeed || !SpringArm || FMath::IsNearlyEqual(MinZoomLength, MaxZoomLength))
	{
		return MoveSpeed;
	}

	const float ZoomAlpha = FMath::GetRangePct(MinZoomLength, MaxZoomLength, SpringArm->TargetArmLength);
	return FMath::Lerp(MinMoveSpeed, MaxMoveSpeed, FMath::Clamp(ZoomAlpha, 0.0f, 1.0f));
}

void AFCS_FreeCameraPawn::AddCameraMovement(const FVector& WorldDirection, float ScaleValue, float Speed)
{
	if (bCameraMovementDisabled || FMath::IsNearlyZero(ScaleValue) || WorldDirection.IsNearlyZero())
	{
		return;
	}

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	const FVector DeltaMove = WorldDirection * ScaleValue * Speed * DeltaTime;

	FVector NewLocation = GetActorLocation() + DeltaMove;
	NewLocation.Z = GetActorLocation().Z;
	SetActorLocation(NewLocation);

	ClampCameraLocation();
}

void AFCS_FreeCameraPawn::HandleEdgeScroll(float DeltaTime)
{
	if (bCameraMovementDisabled) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	FVector2D ViewportSize = FVector2D::ZeroVector;

	if (!GetMousePositionInViewport(PC, MouseX, MouseY, ViewportSize))
	{
		return;
	}

	FVector MoveDirection = FVector::ZeroVector;
	const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	if (MouseY <= EdgeScrollThreshold)
	{
		MoveDirection += Forward;
	}
	else if (MouseY >= ViewportSize.Y - EdgeScrollThreshold)
	{
		MoveDirection -= Forward;
	}

	if (MouseX <= EdgeScrollThreshold)
	{
		MoveDirection -= Right;
	}
	else if (MouseX >= ViewportSize.X - EdgeScrollThreshold)
	{
		MoveDirection += Right;
	}

	if (!MoveDirection.IsNearlyZero())
	{
		MoveDirection.Normalize();

		FVector NewLocation = GetActorLocation() + MoveDirection * GetCurrentMoveSpeed() * EdgeMoveSpeedScale * DeltaTime;
		NewLocation.Z = GetActorLocation().Z;
		SetActorLocation(NewLocation);

		ClampCameraLocation();
	}
}

void AFCS_FreeCameraPawn::HandleMouseRotate()
{
	if (bCameraRotationDisabled) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	if (bHideCursorWhenRotate)
	{
		float DeltaX = 0.0f;
		float DeltaY = 0.0f;
		PC->GetInputMouseDelta(DeltaX, DeltaY);
		RotateCameraByMouseDelta(DeltaX, -DeltaY);
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	FVector2D ViewportSize = FVector2D::ZeroVector;

	if (!GetMousePositionInViewport(PC, MouseX, MouseY, ViewportSize))
	{
		bMouseRotateInitialized = false;
		return;
	}

	const FVector2D CurrentMousePos(MouseX, MouseY);

	if (!bMouseRotateInitialized)
	{
		LastMouseScreenPosition = CurrentMousePos;
		bMouseRotateInitialized = true;
		return;
	}

	const FVector2D MouseDelta = CurrentMousePos - LastMouseScreenPosition;
	LastMouseScreenPosition = CurrentMousePos;

	if (!MouseDelta.IsNearlyZero())
	{
		RotateCameraByMouseDelta(MouseDelta.X, MouseDelta.Y);
	}
}

void AFCS_FreeCameraPawn::UpdateZoomEase(float DeltaTime)
{
	if (bCameraMovementDisabled || !bEnableZoomEase || !SpringArm)
	{
		return;
	}

	if (FMath::IsNearlyEqual(SpringArm->TargetArmLength, TargetZoomLength, 0.1f))
	{
		SpringArm->TargetArmLength = TargetZoomLength;
		return;
	}

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength,
		TargetZoomLength,
		DeltaTime,
		ZoomEaseSpeed
	);
}

void AFCS_FreeCameraPawn::ClampCameraLocation()
{
	if (!bUseCameraBounds)
	{
		return;
	}

	FVector Location = GetActorLocation();
	Location.X = FMath::Clamp(Location.X, MinCameraBounds.X, MaxCameraBounds.X);
	Location.Y = FMath::Clamp(Location.Y, MinCameraBounds.Y, MaxCameraBounds.Y);
	SetActorLocation(Location);
}

void AFCS_FreeCameraPawn::ClampSpringArmPitch()
{
	FRotator ArmRot = SpringArm->GetRelativeRotation();
	ArmRot.Pitch = FMath::Clamp(ArmRot.Pitch, MinPitch, MaxPitch);
	SpringArm->SetRelativeRotation(ArmRot);
}

APlayerController* AFCS_FreeCameraPawn::GetOwningPlayerController() const
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && PC->IsLocalController() ? PC : nullptr;
}


bool AFCS_FreeCameraPawn::GetMousePositionInViewport(
	APlayerController* PC,
	float& MouseX,
	float& MouseY,
	FVector2D& ViewportSize) const
{
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	if (!PC->GetMousePosition(MouseX, MouseY)) { return false; }
	int32 Width = 0;
	int32 Height = 0;
	PC->GetViewportSize(Width, Height);
	const FVector2D Size(Width, Height);

	if (Size.X <= 0.0f || Size.Y <= 0.0f || MouseX < 0 || MouseY < 0 || MouseX > Size.X || MouseY > Size.Y)
	{
		return false;
	}

	ViewportSize = Size;
	return true;
}

FFCS_CameraState AFCS_FreeCameraPawn::GetCurrentCameraState() const
{
	FFCS_CameraState Result;
	Result.Location = GetActorLocation();
	Result.Yaw = GetActorRotation().Yaw;
	Result.Pitch = SpringArm ? SpringArm->GetRelativeRotation().Pitch : 0.0f;
	Result.TargetArmLength = SpringArm ? SpringArm->TargetArmLength : 0.0f;
	return Result;
}

void AFCS_FreeCameraPawn::ApplyCameraState(const FFCS_CameraState& InCameraState)
{
	SetActorLocation(InCameraState.Location);

	FRotator NewActorRotation = GetActorRotation();
	NewActorRotation.Yaw = InCameraState.Yaw;
	SetActorRotation(NewActorRotation);

	if (SpringArm)
	{
		FRotator ArmRot = SpringArm->GetRelativeRotation();
		ArmRot.Pitch = FMath::Clamp(InCameraState.Pitch, MinPitch, MaxPitch);
		SpringArm->SetRelativeRotation(ArmRot);

		SpringArm->TargetArmLength = FMath::Clamp(InCameraState.TargetArmLength, MinZoomLength, MaxZoomLength);
		TargetZoomLength = SpringArm->TargetArmLength;
	}

	ClampCameraLocation();
	ClampSpringArmPitch();
}

void AFCS_FreeCameraPawn::MoveToCameraState(
	const FFCS_CameraState& InCameraState,
	FFCS_OnCameraMoveFinished MoveFinished)
{
	// Scripted scene transitions intentionally bypass manual control gates, including their callback.
	const FFCS_CameraState CurrentState = GetCurrentCameraState();
	CameraStateBlendStart = CurrentState;
	CameraStateBlendTarget = InCameraState;

	EnsureCameraMoveSpeeds(CameraStateBlendTarget);

	if (SpringArm)
	{
		CameraStateBlendTarget.Pitch = FMath::Clamp(CameraStateBlendTarget.Pitch, MinPitch, MaxPitch);
		CameraStateBlendTarget.TargetArmLength = FMath::Clamp(CameraStateBlendTarget.TargetArmLength, MinZoomLength, MaxZoomLength);
	}

	if (bUseCameraBounds)
	{
		CameraStateBlendTarget.Location.X = FMath::Clamp(CameraStateBlendTarget.Location.X, MinCameraBounds.X, MaxCameraBounds.X);
		CameraStateBlendTarget.Location.Y = FMath::Clamp(CameraStateBlendTarget.Location.Y, MinCameraBounds.Y, MaxCameraBounds.Y);
	}

	bIsBlendingToCameraState = true;
	CameraStateBlendElapsed = 0.0f;
	PendingCallback = MoveFinished;
}

bool AFCS_FreeCameraPawn::SetCameraStateInstant(const FFCS_CameraState& InCameraState, bool bRestoreSpringArmLag)
{
	// Reject malformed targets before cancelling a valid in-flight transition.
	if (!IsValid(SpringArm) || !SpringArm->IsRegistered() || InCameraState.Location.ContainsNaN()
		|| !FMath::IsFinite(InCameraState.Yaw) || !FMath::IsFinite(InCameraState.Pitch)
		|| !FMath::IsFinite(InCameraState.TargetArmLength)) return false;

	CancelCameraStateMove();
	TriggerMouseRotate(false);
	HeldKeys.Reset();
	ConsumeMovementInputVector();
	const bool bPreviousLocationLag = SpringArm->bEnableCameraLag;
	const bool bPreviousRotationLag = SpringArm->bEnableCameraRotationLag;
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = false;
	ApplyCameraState(InCameraState); // Also synchronizes TargetZoomLength; no old scroll ease survives.
	CameraStateBlendStart = CameraStateBlendTarget = GetCurrentCameraState();
	CameraStateBlendElapsed = 0.0f;
	if (auto* PC = GetOwningPlayerController())
	{
		// This rig normally uses relative arm rotation; keep control-rotation based configurations in sync too.
		PC->SetControlRotation(SpringArm->GetComponentRotation());
		if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
	}
	// Force the native arm to update its socket and lag history now, without waiting for a world tick.
	// Keep the configured collision test: snapping must not silently disable camera collision.
	SpringArm->TickComponent(0.0f, LEVELTICK_All, nullptr);
	if (bRestoreSpringArmLag)
	{
		SpringArm->bEnableCameraLag = bPreviousLocationLag;
		SpringArm->bEnableCameraRotationLag = bPreviousRotationLag;
	}
	return true;
}

void AFCS_FreeCameraPawn::UpdateCameraStateBlend(float DeltaTime)
{
	if (!bIsBlendingToCameraState)
	{
		return;
	}

	const FFCS_CameraState CurrentState = GetCurrentCameraState();
	FFCS_CameraState NewState = CurrentState;
	CameraStateBlendElapsed += DeltaTime;

	NewState.Location = FMath::VInterpConstantTo(
		CurrentState.Location,
		CameraStateBlendTarget.Location,
		DeltaTime,
		CameraStateBlendTarget.MoveSpeed
	);

	NewState.Yaw = FMath::FixedTurn(
		CurrentState.Yaw,
		CameraStateBlendTarget.Yaw,
		CameraStateBlendTarget.RotationSpeed * DeltaTime
	);

	NewState.Pitch = FMath::FInterpConstantTo(
		CurrentState.Pitch,
		CameraStateBlendTarget.Pitch,
		DeltaTime,
		CameraStateBlendTarget.RotationSpeed
	);

	NewState.TargetArmLength = FMath::FInterpConstantTo(
		CurrentState.TargetArmLength,
		CameraStateBlendTarget.TargetArmLength,
		DeltaTime,
		CameraStateBlendTarget.ZoomSpeed
	);

	ApplyCameraState(NewState);

	const FFCS_CameraState AppliedState = GetCurrentCameraState();
	const bool bLocationDone = FVector::Dist(AppliedState.Location, CameraStateBlendTarget.Location) < 5.0f;
	const bool bYawDone = FMath::Abs(FMath::FindDeltaAngleDegrees(AppliedState.Yaw, CameraStateBlendTarget.Yaw)) < 0.5f;
	const bool bPitchDone = FMath::IsNearlyEqual(AppliedState.Pitch, CameraStateBlendTarget.Pitch, 0.5f);
	const bool bZoomDone = FMath::IsNearlyEqual(AppliedState.TargetArmLength, CameraStateBlendTarget.TargetArmLength, 1.0f);

	if (bLocationDone && bYawDone && bPitchDone && bZoomDone)
	{
		bIsBlendingToCameraState = false;
		ApplyCameraState(CameraStateBlendTarget);

		FFCS_OnCameraMoveFinished FinishedCallback = PendingCallback;
		PendingCallback.Unbind();

		if (FinishedCallback.IsBound())
		{
			FinishedCallback.Execute();
		}
	}
}
