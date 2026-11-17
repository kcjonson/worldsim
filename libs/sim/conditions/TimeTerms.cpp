#include "TimeTerms.h"

#include "ConditionVocabulary.h"

#include <ecs/World.h>
#include <ecs/systems/TimeSystem.h>

namespace conditions {

	void provideTimeTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
		const auto hourRefused = vocabulary.provide("hour", ValueType::Number, {}, [](const TermCall& call) {
			const ecs::TimeSystem* time = call.context.world.tryGetSystem<ecs::TimeSystem>();
			return time != nullptr ? TermValue::fromNumber(time->timeOfDay()) : TermValue::unknown();
		});
		if (hourRefused.has_value()) {
			errors.push_back(*hourRefused);
		}

		if (!config.night.has_value()) {
			errors.emplace_back("is_night needs <Night> hours in the condition vocabulary");
			return;
		}
		const auto nightRefused = vocabulary.provide("is_night", ValueType::Truth, {}, [night = *config.night](const TermCall& call) {
			const ecs::TimeSystem* time = call.context.world.tryGetSystem<ecs::TimeSystem>();
			if (time == nullptr) {
				return TermValue::unknown();
			}
			const float hour = time->timeOfDay();
			const bool	wrapsMidnight = night.start > night.end;
			return TermValue::fromTruth(
				wrapsMidnight ? (hour >= night.start || hour < night.end) : (hour >= night.start && hour < night.end)
			);
		});
		if (nightRefused.has_value()) {
			errors.push_back(*nightRefused);
		}
	}

} // namespace conditions
