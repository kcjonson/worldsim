#pragma once

#include <assets/ConditionConfig.h>

#include <string>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// hour (0-24) and is_night, by `config`'s night hours, from the world's TimeSystem; both are Unknown
	/// in a world without one. Refusals go to `errors`.
	void provideTimeTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors);

} // namespace conditions
