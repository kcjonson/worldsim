#pragma once

#include <assets/ConditionConfig.h>

#include <string>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// near(kind, radius) and count_near(kind, radius) from what the colonist remembers, measured from where
	/// he stands, for each kind `config` maps to a capability or to remembered colonists. A kind with no
	/// mapping reads Unknown. Refusals go to `errors`.
	void provideNearbyTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors);

} // namespace conditions
