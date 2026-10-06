#include "PropertyParserAnimation.h"
#include "../../Include/RmlUi/Core/PropertyDefinition.h"
#include "../../Include/RmlUi/Core/PropertyIdSet.h"
#include "../../Include/RmlUi/Core/StringUtilities.h"
#include "../../Include/RmlUi/Core/StyleSheetSpecification.h"
#include "PropertyShorthandDefinition.h"

namespace Rml {

enum class KeywordType { None, Tween, All, Alternate, Reverse, AlternateReverse, Normal, Infinite, Paused, Running };

struct Keyword {
	KeywordType type;
	Tween tween;
	Keyword(Tween tween) : type(KeywordType::Tween), tween(tween) {}
	Keyword(KeywordType type) : type(type) {}

	bool ValidTransition() const { return type == KeywordType::None || type == KeywordType::Tween || type == KeywordType::All; }
	bool ValidAnimation() const
	{
		return type == KeywordType::None || type == KeywordType::Tween || type == KeywordType::Alternate || type == KeywordType::Infinite ||
			type == KeywordType::Paused || type == KeywordType::Running || type == KeywordType::Normal || type == KeywordType::Reverse ||
			type == KeywordType::AlternateReverse;
	}
};

struct PropertyParserAnimationData {
	const UnorderedMap<String, Keyword> keywords = {
		{"none", {KeywordType::None}},
		{"all", {KeywordType::All}},
		{"alternate", {KeywordType::Alternate}},
		{"reverse", {KeywordType::Reverse}},
		{"alternate-reverse", {KeywordType::AlternateReverse}},
		{"normal", {KeywordType::Normal}},
		{"infinite", {KeywordType::Infinite}},
		{"paused", {KeywordType::Paused}},
		{"running", {KeywordType::Running}},
		{"linear", {Tween{Tween::Linear, Tween::InOut}}},
		{"ease", {Tween{Tween::Quadratic, Tween::InOut}}},
		{"ease-in", {Tween{Tween::Quadratic, Tween::In}}},
		{"ease-out", {Tween{Tween::Quadratic, Tween::Out}}},
		{"ease-in-out", {Tween{Tween::Quadratic, Tween::InOut}}},

		{"back-in", {Tween{Tween::Back, Tween::In}}},
		{"back-out", {Tween{Tween::Back, Tween::Out}}},
		{"back-in-out", {Tween{Tween::Back, Tween::InOut}}},

		{"bounce-in", {Tween{Tween::Bounce, Tween::In}}},
		{"bounce-out", {Tween{Tween::Bounce, Tween::Out}}},
		{"bounce-in-out", {Tween{Tween::Bounce, Tween::InOut}}},

		{"circular-in", {Tween{Tween::Circular, Tween::In}}},
		{"circular-out", {Tween{Tween::Circular, Tween::Out}}},
		{"circular-in-out", {Tween{Tween::Circular, Tween::InOut}}},

		{"cubic-in", {Tween{Tween::Cubic, Tween::In}}},
		{"cubic-out", {Tween{Tween::Cubic, Tween::Out}}},
		{"cubic-in-out", {Tween{Tween::Cubic, Tween::InOut}}},

		{"elastic-in", {Tween{Tween::Elastic, Tween::In}}},
		{"elastic-out", {Tween{Tween::Elastic, Tween::Out}}},
		{"elastic-in-out", {Tween{Tween::Elastic, Tween::InOut}}},

		{"exponential-in", {Tween{Tween::Exponential, Tween::In}}},
		{"exponential-out", {Tween{Tween::Exponential, Tween::Out}}},
		{"exponential-in-out", {Tween{Tween::Exponential, Tween::InOut}}},

		{"linear-in", {Tween{Tween::Linear, Tween::In}}},
		{"linear-out", {Tween{Tween::Linear, Tween::Out}}},
		{"linear-in-out", {Tween{Tween::Linear, Tween::InOut}}},

		{"quadratic-in", {Tween{Tween::Quadratic, Tween::In}}},
		{"quadratic-out", {Tween{Tween::Quadratic, Tween::Out}}},
		{"quadratic-in-out", {Tween{Tween::Quadratic, Tween::InOut}}},

		{"quartic-in", {Tween{Tween::Quartic, Tween::In}}},
		{"quartic-out", {Tween{Tween::Quartic, Tween::Out}}},
		{"quartic-in-out", {Tween{Tween::Quartic, Tween::InOut}}},

		{"quintic-in", {Tween{Tween::Quintic, Tween::In}}},
		{"quintic-out", {Tween{Tween::Quintic, Tween::Out}}},
		{"quintic-in-out", {Tween{Tween::Quintic, Tween::InOut}}},

		{"sine-in", {Tween{Tween::Sine, Tween::In}}},
		{"sine-out", {Tween{Tween::Sine, Tween::Out}}},
		{"sine-in-out", {Tween{Tween::Sine, Tween::InOut}}},
	};
};

ControlledLifetimeResource<PropertyParserAnimationData> PropertyParserAnimation::parser_data;

// CSS time tokens may use seconds or milliseconds. Do not interpret a partial
// numeric prefix (such as '240ms') as an iteration count or reverse factor.
static bool ParseAnimationNumber(const String& token, float& number, bool& is_time)
{
	int count = 0;
	is_time = false;
	if (sscanf(token.c_str(), "%f%n", &number, &count) != 1 || count <= 0)
		return false;
	const String unit = StringUtilities::ToLower(token.substr(count));
	if (unit == "ms")
	{
		number *= 0.001f;
		is_time = true;
	}
	else if (unit == "s")
		is_time = true;
	else if (!unit.empty())
		return false;
	return true;
}

void PropertyParserAnimation::Initialize()
{
	parser_data.Initialize();
}

void PropertyParserAnimation::Shutdown()
{
	parser_data.Shutdown();
}

PropertyParserAnimation::PropertyParserAnimation(Type type) : type(type) {}

bool PropertyParserAnimation::ParseValue(Property& property, const String& value, const ParameterMap& /*parameters*/) const
{
	StringList list_of_values;
	StringUtilities::ExpandString(list_of_values, value, ',');

	bool result = false;
	if (type == ANIMATION_PARSER)
	{
		result = ParseAnimation(property, list_of_values);
	}
	else if (type == TRANSITION_PARSER)
	{
		result = ParseTransition(property, list_of_values);
	}

	return result;
}

bool PropertyParserAnimation::ParseAnimation(Property& property, const StringList& animation_values)
{
	AnimationList animation_list;

	for (const String& single_animation_value : animation_values)
	{
		Animation animation;

		StringList arguments;
		StringUtilities::ExpandString(arguments, single_animation_value, ' ');

		bool duration_found = false;
		bool delay_found = false;
		bool num_iterations_found = false;

		for (auto& argument : arguments)
		{
			if (argument.empty())
				continue;

			// See if we have a <keyword> or <tween> specifier as defined in keywords
			auto it = parser_data->keywords.find(StringUtilities::ToLower(argument));
			if (it != parser_data->keywords.end() && it->second.ValidAnimation())
			{
				switch (it->second.type)
				{
				case KeywordType::None:
				{
					if (animation_list.size() > 0) // The none keyword can not be part of multiple definitions
						return false;
					property = Property{AnimationList{}, Unit::ANIMATION};
					return true;
				}
				break;
				case KeywordType::Tween: animation.tween = it->second.tween; break;
				case KeywordType::Alternate: animation.alternate = true; animation.reverse = false; break;
				case KeywordType::Reverse: animation.reverse = true; animation.alternate = false; break;
				case KeywordType::AlternateReverse: animation.reverse = true; animation.alternate = true; break;
				case KeywordType::Normal: animation.reverse = false; animation.alternate = false; break;
				case KeywordType::Infinite:
					if (num_iterations_found)
						return false;
					animation.num_iterations = -1;
					num_iterations_found = true;
					break;
				case KeywordType::Paused: animation.paused = true; break;
				case KeywordType::Running: animation.paused = false; break;
				default: break;
				}
			}
			else
			{
				// Either <duration>, <delay>, <num_iterations> or a <keyframes-name>
				float number = 0.0f;
				bool is_time = false;

				if (ParseAnimationNumber(argument, number, is_time))
				{
					if (is_time)
					{
						// Duration or delay was assigned
						if (!duration_found)
						{
							duration_found = true;
							animation.duration = number;
						}
						else if (!delay_found)
						{
							delay_found = true;
							animation.delay = number;
						}
						else
							return false;
					}
					else
					{
						// No 's' unit means num_iterations was found
						if (!num_iterations_found)
						{
							animation.num_iterations = Math::RoundToInteger(number);
							num_iterations_found = true;
						}
						else
							return false;
					}
				}
				else
				{
					// Must be an animation name
					if (!animation.name.empty())
						return false;
					animation.name = argument;
				}
			}
		}

		// Validate the parsed transition
		if (animation.name.empty() || animation.duration <= 0.0f || (animation.num_iterations < -1 || animation.num_iterations == 0))
		{
			return false;
		}

		animation_list.push_back(std::move(animation));
	}

	property.value = std::move(animation_list);
	property.unit = Unit::ANIMATION;

	return true;
}

bool PropertyParserAnimation::ParseTransition(Property& property, const StringList& transition_values)
{
	TransitionList transition_list{false, false, {}};

	for (const String& single_transition_value : transition_values)
	{
		Transition transition;
		PropertyIdSet target_property_ids;

		StringList arguments;
		StringUtilities::ExpandString(arguments, single_transition_value, ' ');

		bool duration_found = false;
		bool delay_found = false;
		bool reverse_adjustment_factor_found = false;

		for (auto& argument : arguments)
		{
			if (argument.empty())
				continue;

			// See if we have a <keyword> or <tween> specifier as defined in keywords
			auto it = parser_data->keywords.find(argument);
			if (it != parser_data->keywords.end() && it->second.ValidTransition())
			{
				if (it->second.type == KeywordType::None)
				{
					if (transition_list.transitions.size() > 0) // The none keyword can not be part of multiple definitions
						return false;
					property = Property{TransitionList{true, false, {}}, Unit::TRANSITION};
					return true;
				}
				else if (it->second.type == KeywordType::All)
				{
					if (transition_list.transitions.size() > 0) // The all keyword can not be part of multiple definitions
						return false;
					transition_list.all = true;
				}
				else if (it->second.type == KeywordType::Tween)
				{
					transition.tween = it->second.tween;
				}
			}
			else
			{
				// Either <duration>, <delay> or a <property name>
				float number = 0.0f;
				bool is_time = false;

				if (ParseAnimationNumber(argument, number, is_time))
				{
					if (is_time)
					{
						// Duration or delay was assigned
						if (!duration_found)
						{
							duration_found = true;
							transition.duration = number;
						}
						else if (!delay_found)
						{
							delay_found = true;
							transition.delay = number;
						}
						else
							return false;
					}
					else
					{
						// No 's' unit means reverse adjustment factor was found
						if (!reverse_adjustment_factor_found)
						{
							reverse_adjustment_factor_found = true;
							transition.reverse_adjustment_factor = number;
						}
						else
							return false;
					}
				}
				else
				{
					// Must be a property name or shorthand, expand now
					if (auto shorthand = StyleSheetSpecification::GetShorthand(argument))
					{
						PropertyIdSet underlying_properties = StyleSheetSpecification::GetShorthandUnderlyingProperties(shorthand->id);
						target_property_ids |= underlying_properties;
					}
					else if (auto definition = StyleSheetSpecification::GetProperty(argument))
					{
						// Single property
						target_property_ids.Insert(definition->GetId());
					}
					else
					{
						// Unknown property name
						return false;
					}
				}
			}
		}

		// Validate the parsed transition
		if ((transition_list.all && !target_property_ids.Empty())    //
			|| (!transition_list.all && target_property_ids.Empty()) //
			|| transition.duration <= 0.0f                           //
			|| transition.reverse_adjustment_factor < 0.0f           //
			|| transition.reverse_adjustment_factor > 1.0f           //
		)
		{
			return false;
		}

		if (transition_list.all)
		{
			transition.id = PropertyId::Invalid;
			transition_list.transitions.push_back(transition);
		}
		else
		{
			for (const PropertyId id : target_property_ids)
			{
				transition.id = id;
				transition_list.transitions.push_back(transition);
			}
		}
	}

	property.value = std::move(transition_list);
	property.unit = Unit::TRANSITION;

	return true;
}

} // namespace Rml
