#pragma once

#include "../../../Include/RmlUi/Core/EventListener.h"
#include "InputType.h"

namespace Rml {

class Element;

/**
    Native handler for HTML input type="color".
 */
class InputTypeColor : public InputType, private EventListener {
public:
	InputTypeColor(ElementFormControlInput* element);
	virtual ~InputTypeColor();

	String GetValue() const override;
	bool OnAttributeChange(const ElementAttributes& changed_attributes) override;
	void ProcessDefaultAction(Event& event) override;
	bool GetIntrinsicDimensions(Vector2f& dimensions, float& ratio) override;

private:
	void ProcessEvent(Event& event) override;
	void SetColorPickerVisible(bool visible);
	void UpdateColorValue(const String& value);
	Element* FindColorOption(Element* element) const;

	Element* color_value_element = nullptr;
	Element* color_picker_element = nullptr;
	bool color_picker_visible = false;
};

} // namespace Rml
