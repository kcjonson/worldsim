#include "TimeTerms.h"

#include "ConditionVocabulary.h"

#include <ecs/World.h>
#include <ecs/systems/TimeSystem.h>

#include <cassert>

namespace conditions {

	void provideTimeTerms(ConditionVocabulary& vocabulary, NightHours night) {
		[[maybe_unused]] const auto hourRefused = vocabulary.provide(
			{.name = "hour", .result = ValueType::Number}, [](std::span<const TermValue>, const ConditionContext& context) {
				const ecs::TimeSystem* time = context.world.tryGetSystem<ecs::TimeSystem>();
				return time != nullptr ? TermValue::fromNumber(time->timeOfDay()) : TermValue::unknown();
			}
		);
		assert(!hourRefused.has_value());

		[[maybe_unused]] const auto nightRefused = vocabulary.provide(
			{.name = "is_night", .result = ValueType::Boolean}, [night](std::span<const TermValue>, const ConditionContext& context) {
				const ecs::TimeSystem* time = context.world.tryGetSystem<ecs::TimeSystem>();
				if (time == nullptr) {
					return TermValue::unknown();
				}
				const float hour = time->timeOfDay();
				const bool	wrapsMidnight = night.start > night.end;
				const bool	isNight = wrapsMidnight ? (hour >= night.start || hour < night.end) : (hour >= night.start && hour < night.end);
				return TermValue::fromBoolean(isNight);
			}
		);
		assert(!nightRefused.has_value());
	}

} // namespace conditions
