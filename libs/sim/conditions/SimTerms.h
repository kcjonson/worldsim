#pragma once

#include <assets/ConditionConfig.h>

#include <string>
#include <vector>

namespace conditions {

	class ConditionVocabulary;

	/// Build the vocabulary the sim answers: declare `config`, provide every term the sim has (colonist,
	/// nearby, and time terms), and freeze it. Returns every refusal; empty means it is ready to parse.
	[[nodiscard]] std::vector<std::string> buildSimVocabulary(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config);

} // namespace conditions
