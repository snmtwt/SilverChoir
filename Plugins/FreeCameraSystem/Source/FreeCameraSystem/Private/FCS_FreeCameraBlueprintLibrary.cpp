#include "FCS_FreeCameraBlueprintLibrary.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FCS_FreeCameraSubsystem.h"

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraMovementDisabled(const UObject* WorldContextObject, bool bDisabled)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn)) { return false; }
	CameraPawn->SetCameraMovementDisabled(bDisabled);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraRotationDisabled(const UObject* WorldContextObject, bool bDisabled)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn)) { return false; }
	CameraPawn->SetCameraRotationDisabled(bDisabled);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::IsFreeCameraMovementDisabled(const UObject* WorldContextObject)
{
	const AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	return IsValid(CameraPawn) && CameraPawn->IsCameraMovementDisabled();
}

bool UFCS_FreeCameraBlueprintLibrary::IsFreeCameraRotationDisabled(const UObject* WorldContextObject)
{
	const AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	return IsValid(CameraPawn) && CameraPawn->IsCameraRotationDisabled();
}

UFCS_FreeCameraSubsystem* UFCS_FreeCameraBlueprintLibrary::GetFreeCameraSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;

	return World ? World->GetSubsystem<UFCS_FreeCameraSubsystem>() : nullptr;
}

AFCS_FreeCameraPawn* UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(const UObject* WorldContextObject)
{
	const UFCS_FreeCameraSubsystem* CoreSubsystem = GetFreeCameraSubsystem(WorldContextObject);
	return CoreSubsystem ? CoreSubsystem->GetFreeCamera() : nullptr;
}

bool UFCS_FreeCameraBlueprintLibrary::FindFreeCamera(
	const UObject* WorldContextObject,
	AFCS_FreeCameraPawn*& OutCameraPawn)
{
	OutCameraPawn = GetFreeCamera(WorldContextObject);
	return IsValid(OutCameraPawn);
}

bool UFCS_FreeCameraBlueprintLibrary::MoveFreeCameraForward(const UObject* WorldContextObject, float Value)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraMovementDisabled())
	{
		return false;
	}

	CameraPawn->MoveForward(Value);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::MoveFreeCameraRight(const UObject* WorldContextObject, float Value)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraMovementDisabled())
	{
		return false;
	}

	CameraPawn->MoveRight(Value);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::RotateFreeCamera(const UObject* WorldContextObject, float Value)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraRotationDisabled())
	{
		return false;
	}

	CameraPawn->RotateCamera(Value);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::RotateFreeCameraPitch(const UObject* WorldContextObject, float Value)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraRotationDisabled())
	{
		return false;
	}

	CameraPawn->RotatePitch(Value);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::ZoomFreeCamera(const UObject* WorldContextObject, float Value)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraMovementDisabled())
	{
		return false;
	}

	CameraPawn->ZoomCamera(Value);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraEdgeScrollEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn))
	{
		return false;
	}

	CameraPawn->SetEdgeScrollEnabled(bEnabled);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraPitch(const UObject* WorldContextObject, float NewPitch)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraRotationDisabled())
	{
		return false;
	}

	CameraPawn->SetCameraPitch(NewPitch);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraPitchLimits(
	const UObject* WorldContextObject,
	float MinPitch,
	float MaxPitch)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn))
	{
		return false;
	}

	CameraPawn->SetPitchLimits(MinPitch, MaxPitch);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::TriggerFreeCameraMouseRotate(
	const UObject* WorldContextObject,
	bool bEnable)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || (bEnable && CameraPawn->IsCameraRotationDisabled()))
	{
		return false;
	}

	CameraPawn->TriggerMouseRotate(bEnable);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::RotateFreeCameraByMouseDelta(
	const UObject* WorldContextObject,
	float DeltaX,
	float DeltaY)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn) || CameraPawn->IsCameraRotationDisabled())
	{
		return false;
	}

	CameraPawn->RotateCameraByMouseDelta(DeltaX, DeltaY);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::GetFreeCameraState(
	const UObject* WorldContextObject,
	FFCS_CameraState& OutCameraState)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn))
	{
		OutCameraState = FFCS_CameraState();
		return false;
	}

	OutCameraState = CameraPawn->GetCurrentCameraState();
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::MoveFreeCameraToState(
	const UObject* WorldContextObject,
	const FFCS_CameraState& CameraState,
	FFCS_OnCameraMoveFinished MoveFinished)
{
	AFCS_FreeCameraPawn* CameraPawn = GetFreeCamera(WorldContextObject);
	if (!IsValid(CameraPawn))
	{
		return false;
	}

	CameraPawn->MoveToCameraState(CameraState, MoveFinished);
	return true;
}

bool UFCS_FreeCameraBlueprintLibrary::SetFreeCameraStateInstant(
	const UObject* WorldContextObject, const FFCS_CameraState& CameraState, bool bRestoreSpringArmLag)
{
	auto* Pawn = GetFreeCamera(WorldContextObject);
	return IsValid(Pawn) && Pawn->SetCameraStateInstant(CameraState, bRestoreSpringArmLag);
}
