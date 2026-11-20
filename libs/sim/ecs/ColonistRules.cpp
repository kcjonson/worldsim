#include "ColonistRules.h"

#include <conditions/SimTerms.h>

#include <assets/AttributeConfig.h>
#include <assets/ConditionConfig.h>
#include <assets/NeedsConfig.h>
#include <assets/ThoughtConfig.h>

#include <utils/Log.h>

#include <cassert>

namespace ecs {

	namespace {
		void addErrors(std::vector<std::string>& errors, const char* folder, const std::vector<std::string>& found) {
			for (const std::string& error : found) {
				errors.push_back(std::string("[") + folder + "] " + error);
			}
		}
	} // namespace

	std::vector<std::string>
	loadAttributeRolls(AttributeRolls& rolls, const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder) {
		std::vector<std::string_view> names;
		for (const AttributeField& field : kAttributeFields) {
			names.push_back(field.id);
		}
		assets::AttributeConfigLoad config = assets::loadAttributeConfig(files, folder, names);
		if (!config.errors.empty()) {
			return config.errors;
		}
		for (std::size_t index = 0; index < config.ranges.size(); ++index) {
			rolls.ranges[index] = std::move(config.ranges[index]);
		}
		LOG_INFO(Engine, "Loaded %zu attribute ranges from %s", config.ranges.size(), folder.generic_string().c_str());
		return {};
	}

	std::vector<std::string>
	loadConditionVocabulary(conditions::ConditionVocabulary& vocabulary, const std::filesystem::path& folder, const assets::TraitRegistry& traits) {
		const assets::ConditionConfigLoad config = assets::loadConditionConfig(folder);
		if (!config.errors.empty()) {
			return config.errors;
		}
		std::vector<std::string> errors = conditions::buildSimVocabulary(vocabulary, config.config, traits);
		if (errors.empty()) {
			LOG_INFO(Engine, "Loaded condition vocabulary from %s: %zu terms", folder.generic_string().c_str(), vocabulary.termCount());
		}
		return errors;
	}

	std::vector<std::string> loadThoughtTypes(ThoughtTypes& types, const std::filesystem::path& folder) {
		const assets::ThoughtConfigLoad config = assets::loadThoughtConfig(folder);
		if (!config.errors.empty()) {
			return config.errors;
		}
		std::vector<std::string> errors = types.load(config.config);
		if (errors.empty()) {
			LOG_INFO(
				Engine,
				"Loaded thought config from %s: %zu feelings, %zu thought types, %zu sources",
				folder.generic_string().c_str(),
				config.config.feelings.size(),
				types.typeCount(),
				types.sources().size()
			);
		}
		return errors;
	}

	std::vector<std::string>
	loadNeedLevels(NeedLevels& levels, const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder) {
		const assets::NeedsConfigLoad config = assets::loadNeedsConfig(files, folder);
		if (!config.errors.empty()) {
			return config.errors;
		}
		std::vector<std::string> errors = levels.load(config.config, folder);
		if (errors.empty()) {
			LOG_INFO(Engine, "Loaded need levels from %s: %zu needs", folder.generic_string().c_str(), config.config.needs.size());
		}
		return errors;
	}

	ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot, const assets::CreatureConfigFiles& human) {
		assert(human.errors.empty() && "loadColonistRules takes a human folder with no errors; its errors are the load's");
		ColonistRulesLoad load;
		load.rules.traits = std::make_unique<assets::TraitRegistry>();
		load.rules.attributes = std::make_unique<AttributeRolls>();
		load.rules.vocabulary = std::make_unique<conditions::ConditionVocabulary>();
		load.rules.thoughtTypes = std::make_unique<ThoughtTypes>(*load.rules.vocabulary);
		load.rules.needLevels = std::make_unique<NeedLevels>();

		addErrors(load.errors, "traits", load.rules.traits->loadFromFiles(human.traits, human.folder));
		if (!load.errors.empty()) {
			return load;
		}
		const assets::TraitRegistry& traits = *load.rules.traits;
		LOG_INFO(Engine, "Loaded %zu traits from %s", traits.traits().size(), human.folder.generic_string().c_str());
		addErrors(load.errors, "attributes", loadAttributeRolls(*load.rules.attributes, human.attributes, human.folder));
		if (!load.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "needs", loadNeedLevels(*load.rules.needLevels, human.needs, human.folder));
		if (!load.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "conditions", loadConditionVocabulary(*load.rules.vocabulary, configRoot / "conditions", traits));
		if (!load.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "thoughts", loadThoughtTypes(*load.rules.thoughtTypes, configRoot / "thoughts"));
		return load;
	}

} // namespace ecs
