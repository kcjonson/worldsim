#pragma once

#include <assets/ConditionConfig.h>
#include <assets/TraitRegistry.h>

#include <string>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// Build the vocabulary the sim answers: declare `config`, provide every term the sim has (colonist,
	/// nearby, and time terms, with trait names from `traits`), and freeze it. Returns every refusal; empty
	/// means it is ready to parse.
	[[nodiscard]] std::vector<std::string>
	buildSimVocabulary(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, const assets::TraitRegistry& traits);

} // namespace conditions
