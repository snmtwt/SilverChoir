#include "InputTypeColor.h"
#include "../../../Include/RmlUi/Core/Elements/ElementFormControlInput.h"
#include "../../../Include/RmlUi/Core/Factory.h"
#include "../../../Include/RmlUi/Core/Property.h"
#include <array>
#include <cctype>

namespace Rml {

namespace {

static const std::array<const char*, 20> PaletteColors = {
	"#000000", "#ffffff", "#7f1d1d", "#ef4444", "#f97316", "#facc15", "#84cc16", "#22c55e", "#14b8a6", "#06b6d4",
	"#3b82f6", "#6366f1", "#8b5cf6", "#d946ef", "#ec4899", "#7c2d12", "#57534e", "#64748b", "#1e3a8a", "#14532d",
};

bool IsHexDigit(char character)
{
	return std::isxdigit(static_cast<unsigned char>(character)) != 0;
}

int HexValue(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	return character - 'A' + 10;
}

String NormalizeColorValue(const String& value)
{
	if (value.size() == 4 && value[0] == '#' && IsHexDigit(value[1]) && IsHexDigit(value[2]) && IsHexDigit(value[3]))
	{
		String result = "#";
		for (size_t index = 1; index < value.size(); ++index)
		{
			result.push_back(value[index]);
			result.push_back(value[index]);
		}
		return result;
	}

	if (value.size() == 7 && value[0] == '#')
	{
		for (size_t index = 1; index < value.size(); ++index)
		{
			if (!IsHexDigit(value[index]))
				return "#000000";
		}
		return value;
	}

	return "#000000";
}

Colourb ColorFromValue(const String& value)
{
	const String normalized = NormalizeColorValue(value);
	auto ByteAt = [&normalized](size_t index) { return HexValue(normalized[index]) * 16 + HexValue(normalized[index + 1]); };
	return Colourb(ByteAt(1), ByteAt(3), ByteAt(5));
}

} // namespace

InputTypeColor::InputTypeColor(ElementFormControlInput* input) : InputType(input)
{
	ElementPtr unique_value = Factory::InstanceElement(element, "*", "colorvalue", XMLAttributes());
	color_value_element = unique_value.get();
	element->AppendChild(std::move(unique_value), false);

	ElementPtr unique_picker = Factory::InstanceElement(element, "*", "colorpicker", XMLAttributes());
	color_picker_element = unique_picker.get();
	element->AppendChild(std::move(unique_picker), false);

	for (const char* color : PaletteColors)
	{
		ElementPtr option = Factory::InstanceElement(color_picker_element, "*", "coloroption", XMLAttributes());
		option->SetAttribute("value", color);
		option->SetProperty(PropertyId::BackgroundColor, Property(ColorFromValue(color), Unit::COLOUR));
		color_picker_element->AppendChild(std::move(option), false);
	}

	UpdateColorValue(GetValue());
	element->AddEventListener(EventId::Click, this, true);
	element->AddEventListener(EventId::Blur, this);
}

InputTypeColor::~InputTypeColor()
{
	element->RemoveEventListener(EventId::Click, this, true);
	element->RemoveEventListener(EventId::Blur, this);
	element->RemoveChild(color_value_element);
	element->RemoveChild(color_picker_element);
}

String InputTypeColor::GetValue() const
{
	return NormalizeColorValue(element->GetAttribute<String>("value", "#000000"));
}

bool InputTypeColor::OnAttributeChange(const ElementAttributes& changed_attributes)
{
	if (changed_attributes.find("value") != changed_attributes.end())
	{
		const String value = GetValue();
		if (element->GetAttribute<String>("value", "") != value)
		{
			element->SetAttribute("value", value);
			return true;
		}
		UpdateColorValue(value);
	}

	if (changed_attributes.find("disabled") != changed_attributes.end() && element->IsDisabled())
		SetColorPickerVisible(false);

	return true;
}

void InputTypeColor::ProcessDefaultAction(Event& /*event*/) {}

bool InputTypeColor::GetIntrinsicDimensions(Vector2f& dimensions, float& ratio)
{
	dimensions = Vector2f(40.f, 32.f);
	ratio = 1.f;
	return true;
}

void InputTypeColor::ProcessEvent(Event& event)
{
	if (event == EventId::Blur)
	{
		if (event.GetTargetElement() == element)
			SetColorPickerVisible(false);
		return;
	}

	if (event != EventId::Click || element->IsDisabled())
		return;

	Element* target = event.GetTargetElement();
	if (Element* option = FindColorOption(target))
	{
		const String value = option->GetAttribute<String>("value", "#000000");
		if (value != GetValue())
		{
			element->SetAttribute("value", value);
			element->DispatchEvent(EventId::Change, {{"value", Variant(value)}});
		}
		SetColorPickerVisible(false);
		element->Focus();
		event.StopPropagation();
		return;
	}

	for (Element* current = target; current && current != element; current = current->GetParentNode())
	{
		if (current == color_picker_element)
			return;
	}

	SetColorPickerVisible(!color_picker_visible);
}

void InputTypeColor::SetColorPickerVisible(bool visible)
{
	if (color_picker_visible == visible)
		return;

	color_picker_visible = visible;
	element->SetClass("open", visible);
	element->SetPseudoClass("open", visible);
}

void InputTypeColor::UpdateColorValue(const String& value)
{
	if (color_value_element)
		color_value_element->SetProperty(PropertyId::BackgroundColor, Property(ColorFromValue(value), Unit::COLOUR));
}

Element* InputTypeColor::FindColorOption(Element* candidate) const
{
	for (Element* current = candidate; current && current != element; current = current->GetParentNode())
	{
		if (current->GetParentNode() == color_picker_element && current->GetTagName() == "coloroption")
			return current;
	}
	return nullptr;
}

} // namespace Rml
