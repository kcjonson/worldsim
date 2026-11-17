#pragma once

// The rules from data that colonists' minds run on: the condition vocabulary, built and frozen first,
// and the thought types written in it. Loaded once per game.

#include "ThoughtTypes.h"

#include <conditions/ConditionVocabulary.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ecs {

	/// Held by pointer so the types keep their vocabulary and the systems their types wherever the
	/// rules are moved.
	struct ColonistRules {
		std::unique_ptr<conditions::ConditionVocabulary> vocabulary;
		std::unique_ptr<ThoughtTypes>					 thoughtTypes;
	};

	struct ColonistRulesLoad {
		ColonistRules			 rules;
		std::vector<std::string> errors;
	};

	/// Build the vocabulary from `configRoot`/conditions, then the thought types from `configRoot`/thoughts.
	/// Errors name their folder; an empty list means both are ready.
	[[nodiscard]] ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot);

} // namespace ecs
