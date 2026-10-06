#include "H5UI_Input.h"

#include "InputCoreTypes.h"

namespace H5UI_Input
{
	Rml::Input::KeyIdentifier TranslateKey(const FKey& Key)
	{
		using namespace Rml::Input;

		if (Key == EKeys::BackSpace) return KI_BACK;
		if (Key == EKeys::Tab) return KI_TAB;
		if (Key == EKeys::Enter) return KI_RETURN;
		if (Key == EKeys::Escape) return KI_ESCAPE;
		if (Key == EKeys::SpaceBar) return KI_SPACE;
		if (Key == EKeys::PageUp) return KI_PRIOR;
		if (Key == EKeys::PageDown) return KI_NEXT;
		if (Key == EKeys::End) return KI_END;
		if (Key == EKeys::Home) return KI_HOME;
		if (Key == EKeys::Left) return KI_LEFT;
		if (Key == EKeys::Up) return KI_UP;
		if (Key == EKeys::Right) return KI_RIGHT;
		if (Key == EKeys::Down) return KI_DOWN;
		if (Key == EKeys::Insert) return KI_INSERT;
		if (Key == EKeys::Delete) return KI_DELETE;

		static const TMap<FKey, KeyIdentifier> KeyMap = {
			{EKeys::Zero, KI_0}, {EKeys::One, KI_1}, {EKeys::Two, KI_2}, {EKeys::Three, KI_3}, {EKeys::Four, KI_4},
			{EKeys::Five, KI_5}, {EKeys::Six, KI_6}, {EKeys::Seven, KI_7}, {EKeys::Eight, KI_8}, {EKeys::Nine, KI_9},
			{EKeys::A, KI_A}, {EKeys::B, KI_B}, {EKeys::C, KI_C}, {EKeys::D, KI_D}, {EKeys::E, KI_E},
			{EKeys::F, KI_F}, {EKeys::G, KI_G}, {EKeys::H, KI_H}, {EKeys::I, KI_I}, {EKeys::J, KI_J},
			{EKeys::K, KI_K}, {EKeys::L, KI_L}, {EKeys::M, KI_M}, {EKeys::N, KI_N}, {EKeys::O, KI_O},
			{EKeys::P, KI_P}, {EKeys::Q, KI_Q}, {EKeys::R, KI_R}, {EKeys::S, KI_S}, {EKeys::T, KI_T},
			{EKeys::U, KI_U}, {EKeys::V, KI_V}, {EKeys::W, KI_W}, {EKeys::X, KI_X}, {EKeys::Y, KI_Y}, {EKeys::Z, KI_Z},
			{EKeys::F1, KI_F1}, {EKeys::F2, KI_F2}, {EKeys::F3, KI_F3}, {EKeys::F4, KI_F4},
			{EKeys::F5, KI_F5}, {EKeys::F6, KI_F6}, {EKeys::F7, KI_F7}, {EKeys::F8, KI_F8},
			{EKeys::F9, KI_F9}, {EKeys::F10, KI_F10}, {EKeys::F11, KI_F11}, {EKeys::F12, KI_F12}
		};

		if (const KeyIdentifier* Identifier = KeyMap.Find(Key))
		{
			return *Identifier;
		}
		return KI_UNKNOWN;
	}

	int32 GetModifiers(const FInputEvent& Event)
	{
		int32 Modifiers = 0;
		Modifiers |= Event.IsControlDown() ? Rml::Input::KM_CTRL : 0;
		Modifiers |= Event.IsShiftDown() ? Rml::Input::KM_SHIFT : 0;
		Modifiers |= Event.IsAltDown() ? Rml::Input::KM_ALT : 0;
		Modifiers |= Event.IsCommandDown() ? Rml::Input::KM_META : 0;
		return Modifiers;
	}

	int32 GetMouseButtonIndex(const FKey& Key)
	{
		if (Key == EKeys::LeftMouseButton) return 0;
		if (Key == EKeys::RightMouseButton) return 1;
		if (Key == EKeys::MiddleMouseButton) return 2;
		return INDEX_NONE;
	}
}
