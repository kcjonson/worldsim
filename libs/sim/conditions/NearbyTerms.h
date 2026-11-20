#pragma once

#include <assets/ConditionConfig.h>

#include <string>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// near(kind, radius) and count_near(kind, radius) from what the colonist remembers, measured from where
	/// he stands, for each kind `config` maps to a capability or to a mover definition. A mover counts when
	/// it was seen within assets::MemoryConfig's recent-sighting window, at its last-seen position. A kind
	/// with no mapping reads Unknown. Refusals go to `errors`.
	void provideNearbyTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors);

} // namespace conditions
