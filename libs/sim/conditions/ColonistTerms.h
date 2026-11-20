#pragma once

#include <assets/ConditionConfig.h>
#include <assets/TraitRegistry.h>

#include <string>
#include <string_view>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// An engine name as conditions write it: "FulfillNeed" and "Work Ethic" are fulfill_need and work_ethic.
	[[nodiscard]] std::string conditionName(std::string_view engineName);

	/// need(kind) (0-100), attribute(kind) (0-20), trait(id), current_task(kind) and mid_craft, from the
	/// colonist's own components. trait takes the ids in `traits`. current_task takes a task type or a task
	/// group from `config`. Refusals go to `errors`.
	void provideColonistTerms(
		ConditionVocabulary&		   vocabulary,
		const assets::ConditionConfig& config,
		const assets::TraitRegistry&   traits,
		std::vector<std::string>&	   errors
	);

} // namespace conditions
