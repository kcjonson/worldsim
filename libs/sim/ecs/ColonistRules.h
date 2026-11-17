#pragma once

// The rules from data that colonists' minds run on: the condition vocabulary, built and frozen first,
// the thought types written in it, and the need levels colonists' needs are built from. Loaded once per game.

#include "NeedLevels.h"
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
		std::unique_ptr<NeedLevels>						 needLevels;
	};

	struct ColonistRulesLoad {
		ColonistRules			 rules;
		std::vector<std::string> errors;
	};

	/// Declare `vocabulary` from the condition config in `folder`, answer it with the sim's terms, and
	/// freeze it. An empty error list means it is ready; that is logged with its term count.
	[[nodiscard]] std::vector<std::string> loadConditionVocabulary(conditions::ConditionVocabulary& vocabulary, const std::filesystem::path& folder);

	/// Build `types` from the thought config in `folder`. An empty error list means they are ready; that is
	/// logged with the feeling, thought type, and source counts.
	[[nodiscard]] std::vector<std::string> loadThoughtTypes(ThoughtTypes& types, const std::filesystem::path& folder);

	/// Build `levels` from the need config in `folder`. An empty error list means they are ready; that is
	/// logged with the need count.
	[[nodiscard]] std::vector<std::string> loadNeedLevels(NeedLevels& levels, const std::filesystem::path& folder);

	/// Build the vocabulary from `configRoot`/conditions, then the thought types from `configRoot`/thoughts,
	/// and the need levels from `configRoot`/colonists/needs. Errors name their folder; an empty list
	/// means all are ready.
	[[nodiscard]] ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot);

} // namespace ecs
