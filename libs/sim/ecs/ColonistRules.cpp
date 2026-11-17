#include "ColonistRules.h"

#include <conditions/SimTerms.h>

#include <assets/ConditionConfig.h>
#include <assets/NeedsConfig.h>
#include <assets/ThoughtConfig.h>

#include <utils/Log.h>

namespace ecs {

	namespace {
		void addErrors(std::vector<std::string>& errors, const char* folder, const std::vector<std::string>& found) {
			for (const std::string& error : found) {
				errors.push_back(std::string("[") + folder + "] " + error);
			}
		}
	} // namespace

	std::vector<std::string> loadConditionVocabulary(conditions::ConditionVocabulary& vocabulary, const std::filesystem::path& folder) {
		const assets::ConditionConfigLoad config = assets::loadConditionConfig(folder);
		if (!config.errors.empty()) {
			return config.errors;
		}
		std::vector<std::string> errors = conditions::buildSimVocabulary(vocabulary, config.config);
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

	std::vector<std::string> loadNeedLevels(NeedLevels& levels, const std::filesystem::path& folder) {
		const assets::NeedsConfigLoad config = assets::loadNeedsConfig(folder);
		if (!config.errors.empty()) {
			return config.errors;
		}
		std::vector<std::string> errors = levels.load(config.config, folder);
		if (errors.empty()) {
			LOG_INFO(Engine, "Loaded need levels from %s: %zu needs", folder.generic_string().c_str(), config.config.needs.size());
		}
		return errors;
	}

	ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot) {
		ColonistRulesLoad load;
		load.rules.vocabulary = std::make_unique<conditions::ConditionVocabulary>();
		load.rules.thoughtTypes = std::make_unique<ThoughtTypes>(*load.rules.vocabulary);
		load.rules.needLevels = std::make_unique<NeedLevels>();

		addErrors(load.errors, "conditions", loadConditionVocabulary(*load.rules.vocabulary, configRoot / "conditions"));
		if (!load.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "thoughts", loadThoughtTypes(*load.rules.thoughtTypes, configRoot / "thoughts"));
		addErrors(load.errors, "colonists", loadNeedLevels(*load.rules.needLevels, configRoot / "colonists" / "needs"));
		return load;
	}

} // namespace ecs
