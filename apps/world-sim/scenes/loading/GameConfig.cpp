#include "GameConfig.h"

#include <assets/AssetRegistry.h>
#include <assets/CreatureConfigFiles.h>
#include <assets/EntryLoader.h>
#include <assets/PriorityConfig.h>
#include <assets/TraitRegistry.h>
#include <assets/WorkConfig.h>
#include <conditions/ConditionVocabulary.h>
#include <ecs/ThoughtTypes.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace world_sim {

	GameConfigLoad loadGameConfig(const std::filesystem::path& configRoot, const std::filesystem::path& creaturesRoot) {
		GameConfigLoad load;

		std::vector<std::filesystem::path> folders;
		std::error_code					   error;
		for (std::filesystem::directory_iterator entry(creaturesRoot, error), end; !error && entry != end; entry.increment(error)) {
			std::error_code typeError;
			if (entry->is_directory(typeError)) {
				folders.push_back(entry->path());
			}
		}
		if (error) {
			load.errors.push_back("[creatures] " + creaturesRoot.generic_string() + ": " + error.message());
			return load;
		}
		std::ranges::sort(folders);

		std::vector<assets::CreatureConfigFiles> creatures;
		for (const std::filesystem::path& folder : folders) {
			assets::CreatureConfigFiles files = assets::creatureConfigFiles(folder);
			load.errors.insert(load.errors.end(), files.errors.begin(), files.errors.end());
			creatures.push_back(std::move(files));
		}
		if (!load.errors.empty()) {
			return load;
		}
		const auto human =
			std::ranges::find_if(creatures, [](const assets::CreatureConfigFiles& files) { return files.folder.filename() == "Human"; });
		if (human == creatures.end()) {
			load.errors.push_back("[creatures] creatures/Human: missing; the human's folder holds its definition and config");
			return load;
		}
		// The human reads all five kinds; a creature folder in general need not have them.
		load.errors = assets::missingKinds(*human);
		if (!load.errors.empty()) {
			return load;
		}

		load.errors = assets::loadWorkConfig(configRoot, *human);
		const bool workConfigLoaded = load.errors.empty();
		if (workConfigLoaded) {
			const assets::AssetRegistry& definitions = assets::AssetRegistry::Get();
			const auto					 definitionId = [&definitions](std::string_view name) -> std::optional<uint32_t> {
				  const uint32_t id = definitions.getDefNameId(std::string(name));
				  return definitions.getDefinitionById(id) == nullptr ? std::nullopt : std::optional<uint32_t>(id);
			};
			// A work entry's rung names a rung of the ladder the arbitration file just declared.
			assets::EntrySources sources{.configRoot = configRoot, .creatures = creatures, .rungs = assets::PriorityConfig::Get().rungs()};
			sources.definitionId = definitionId;
			assets::EntryLoad entries = assets::loadEntries(sources);
			load.errors = std::move(entries.errors);
			load.entries = std::move(entries.library);
		}
		const bool			   entriesLoaded = workConfigLoaded && load.errors.empty();
		ecs::ColonistRulesLoad colonistRules = ecs::loadColonistRules(configRoot, *human);
		load.errors.insert(load.errors.end(), colonistRules.errors.begin(), colonistRules.errors.end());
		load.rules = std::move(colonistRules.rules);

		// Entries name traits, kinds, and thought types, which the colonist rules load after them.
		if (entriesLoaded && colonistRules.errors.empty()) {
			const ecs::ColonistRules&	  rules = load.rules;
			const std::optional<uint32_t> kindSet = rules.vocabulary->findSymbolSet("kind");
			const assets::EntryNameSets	  names{
				  .isTrait = [&rules](std::string_view name) { return rules.traits->find(name) != nullptr; },
				  .isKind =
					  [&rules, kindSet](std::string_view name) {
						  return kindSet && std::ranges::any_of(rules.vocabulary->symbolSet(*kindSet), [name](const auto& symbol) {
									 return symbol.name == name;
								 });
					  },
				  .isThought = [&rules](std::string_view name) { return rules.thoughtTypes->findType(name).has_value(); },
			  };
			std::vector<std::string> nameErrors = assets::checkEntryNames(load.entries, names);
			load.errors.insert(load.errors.end(), nameErrors.begin(), nameErrors.end());
		}
		if (!load.errors.empty()) {
			load.entries = assets::EntryLibrary{};
		}
		return load;
	}

} // namespace world_sim
