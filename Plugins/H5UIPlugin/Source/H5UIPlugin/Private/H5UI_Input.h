#pragma once

#include "Input/Events.h"
#include "RmlUi/Core/Input.h"

namespace H5UI_Input
{
	Rml::Input::KeyIdentifier TranslateKey(const FKey& Key);
	int32 GetModifiers(const FInputEvent& Event);
	int32 GetMouseButtonIndex(const FKey& Key);
}
