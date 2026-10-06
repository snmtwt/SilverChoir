#include "MovementMode/HMS_FallingMode.h"

UHMS_FallingMode::UHMS_FallingMode()
{
	bCancelVerticalSpeedOnLanding = true;
	AirControlPercentage = 0.5f;
	FallingDeceleration = 0.0f;
	FallingLateralFriction = 0.0f;
	OverTerminalSpeedFallingDeceleration = 800.0f;
	TerminalMovementPlaneSpeed = 1500.0f;
	bShouldClampTerminalVerticalSpeed = true;
	VerticalFallingDeceleration = 4000.0f;
	TerminalVerticalSpeed = 2000.0f;
}
