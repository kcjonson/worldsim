#include "ColonistRules.h"

#include <conditions/SimTerms.h>

#include <assets/ConditionConfig.h>
#include <assets/ThoughtConfig.h>

namespace ecs {

	namespace {
		void addErrors(std::vector<std::string>& errors, const char* folder, const std::vector<std::string>& found) {
			for (const std::string& error : found) {
				errors.push_back(std::string("[") + folder + "] " + error);
			}
		}
	} // namespace

	ColonistRulesLoad loadColonistRules(const std::filesystem::path& configRoot) {
		ColonistRulesLoad load;
		load.rules.vocabulary = std::make_unique<conditions::ConditionVocabulary>();
		load.rules.thoughtTypes = std::make_unique<ThoughtTypes>(*load.rules.vocabulary);

		const assets::ConditionConfigLoad conditionConfig = assets::loadConditionConfig(configRoot / "conditions");
		addErrors(load.errors, "conditions", conditionConfig.errors);
		if (!conditionConfig.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "conditions", conditions::buildSimVocabulary(*load.rules.vocabulary, conditionConfig.config));
		if (!load.errors.empty()) {
			return load;
		}

		const assets::ThoughtConfigLoad thoughtConfig = assets::loadThoughtConfig(configRoot / "thoughts");
		addErrors(load.errors, "thoughts", thoughtConfig.errors);
		if (!thoughtConfig.errors.empty()) {
			return load;
		}
		addErrors(load.errors, "thoughts", load.rules.thoughtTypes->load(thoughtConfig.config));
		return load;
	}

} // namespace ecs
