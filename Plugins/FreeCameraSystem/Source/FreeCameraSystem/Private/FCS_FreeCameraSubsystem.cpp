#include "FCS_FreeCameraSubsystem.h"

#include "FCS_FreeCameraPawn.h"

bool UFCS_FreeCameraSubsystem::RegisterFreeCamera(AFCS_FreeCameraPawn* CameraPawn)
{
	if (!IsValid(CameraPawn) || CameraPawn->GetWorld() != GetWorld())
	{
		return false;
	}

	FreeCamera = CameraPawn;
	return true;
}

bool UFCS_FreeCameraSubsystem::UnregisterFreeCamera(AFCS_FreeCameraPawn* CameraPawn)
{
	if (!CameraPawn || FreeCamera.Get() != CameraPawn)
	{
		return false;
	}

	FreeCamera.Reset();
	return true;
}

AFCS_FreeCameraPawn* UFCS_FreeCameraSubsystem::GetFreeCamera() const
{
	return FreeCamera.Get();
}

bool UFCS_FreeCameraSubsystem::FindFreeCamera(AFCS_FreeCameraPawn*& OutCameraPawn) const
{
	OutCameraPawn = GetFreeCamera();
	return IsValid(OutCameraPawn);
}

bool UFCS_FreeCameraSubsystem::HasFreeCamera() const
{
	return IsValid(GetFreeCamera());
}
