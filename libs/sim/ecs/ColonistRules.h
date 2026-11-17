#pragma once

// The rules from data that colonists' minds run on: the trait definitions, the condition vocabulary built
// from them and frozen, and the thought types written in it. Loaded once per game.

#include "ThoughtTypes.h"

#include <conditions/ConditionVocabulary.h>

#include <assets/TraitRegistry.h>

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

	/// Declare `vocabulary` from the condition config in `folder`, answer it with the sim's terms (trait
	/// names from `traits`), and freeze it. An empty error list means it is ready; that is logged with its
	/// term count.
	[[nodiscard]] std::vector<std::string>
	loadConditionVocabulary(conditions::ConditionVocabulary& vocabulary, const std::filesystem::path& folder, const assets::TraitRegistry& traits);

	/// Build `types` from the thought config in `folder`. An empty error list means they are ready; that is
	/// logged with the feeling and thought type counts.
	[[nodiscard]] std::vector<std::string> loadThoughtTypes(ThoughtTypes& types, const std::filesystem::path& folder);

	/// Load the game's assets::TraitRegistry from `configRoot`/colonists/traits, build the vocabulary from
	/// `configRoot`/conditions, then the thought types from `configRoot`/thoughts, stopping at the first
	/// stage with errors. Errors name their folder; an empty list means all are ready.
	[[nodiscard]] ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot);

} // namespace ecs
