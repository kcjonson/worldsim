#pragma once

// The rules from data that colonists are made and think by: the trait definitions, the attribute ranges,
// the need levels colonists' needs are built from, the condition vocabulary (declared from its own config,
// then frozen) and the thought types written in it. Loaded once per game.

#include "NeedLevels.h"
#include "ThoughtTypes.h"
#include "components/Attributes.h"

#include <conditions/ConditionVocabulary.h>

#include <assets/ConfigFiles.h>
#include <assets/CreatureConfigFiles.h>
#include <assets/TraitRegistry.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ecs {

	/// Held by pointer so what holds a part (the types their vocabulary, the systems their types, the Bio tab
	/// the traits) keeps it wherever the rules are moved.
	struct ColonistRules {
		std::unique_ptr<assets::TraitRegistry>			 traits;
		std::unique_ptr<AttributeRolls>					 attributes;
		std::unique_ptr<conditions::ConditionVocabulary> vocabulary;
		std::unique_ptr<ThoughtTypes>					 thoughtTypes;
		std::unique_ptr<NeedLevels>						 needLevels;
	};

	struct ColonistRulesLoad {
		ColonistRules			 rules;
		std::vector<std::string> errors;
	};

	/// Fill `rolls` from the attribute config in `found` (its files named relative to `folder`), one range for
	/// every attribute in kAttributeFields. An empty error list means it is ready; that is logged with the
	/// attribute count.
	[[nodiscard]] std::vector<std::string>
	loadAttributeRolls(AttributeRolls& rolls, const assets::ConfigFiles& found, const std::filesystem::path& folder);

	/// Declare `vocabulary` from the condition config in `folder`, answer it with the sim's terms (trait
	/// names from `traits`), and freeze it. An empty error list means it is ready; that is logged with its
	/// term count.
	[[nodiscard]] std::vector<std::string>
	loadConditionVocabulary(conditions::ConditionVocabulary& vocabulary, const std::filesystem::path& folder, const assets::TraitRegistry& traits);

	/// Build `types` from the thought config in `folder`. An empty error list means they are ready; that is
	/// logged with the feeling, thought type, and source counts.
	[[nodiscard]] std::vector<std::string> loadThoughtTypes(ThoughtTypes& types, const std::filesystem::path& folder);

	/// Build `levels` from the need config in `found` (its files named relative to `folder`). An empty error
	/// list means they are ready; that is logged with the need count.
	[[nodiscard]] std::vector<std::string>
	loadNeedLevels(NeedLevels& levels, const assets::ConfigFiles& found, const std::filesystem::path& folder);

	/// Report `human`'s errors finding its files, then load the traits, the attribute ranges, and the need
	/// levels from `human`'s <Traits>, <Attributes>, and <Needs> files, build the vocabulary from
	/// `configRoot`/conditions, then the thought types from `configRoot`/thoughts, stopping at the first stage
	/// with errors. Errors name their stage; an empty list means all are ready. Every part of the rules exists
	/// either way; a stage that failed or never ran is empty.
	[[nodiscard]] ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot, const assets::CreatureConfigFiles& human);

} // namespace ecs
