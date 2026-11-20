#include "assets/WorkConfig.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"
#include "assets/MemoryConfig.h"
#include "assets/MoodConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkTypeRegistry.h"

namespace assets {

	std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot) {
		ActionTypeRegistry::Get().clear();
		TaskChainRegistry::Get().clear();
		WorkTypeRegistry::Get().clear();
		PriorityConfig::Get().clear();
		ConstructionRegistry::Get().clear();
		MoodConfig::Get().clear();
		MemoryConfig::Get().clear();
		ConfigValidator::clearErrors();

		const auto loadFailure = [](const std::filesystem::path& file, const std::vector<std::string>& problems = {}) {
			if (!problems.empty()) {
				return problems;
			}
			return std::vector<std::string>{"Failed to load " + file.generic_string()};
		};

		const std::filesystem::path actionTypes = "actions/action-types.xml";
		if (!ActionTypeRegistry::Get().loadFromFile(configRoot, actionTypes)) {
			return loadFailure(actionTypes, ActionTypeRegistry::Get().problems());
		}

		const std::filesystem::path taskChains = "work/task-chains.xml";
		if (!TaskChainRegistry::Get().loadFromFile(configRoot, taskChains)) {
			return loadFailure(taskChains, TaskChainRegistry::Get().problems());
		}

		const std::filesystem::path workTypes = "work/work-types.xml";
		if (!WorkTypeRegistry::Get().loadFromFile(configRoot, workTypes)) {
			return loadFailure(workTypes, WorkTypeRegistry::Get().problems());
		}

		const std::filesystem::path priorityTuning = "work/priority-tuning.xml";
		if (!PriorityConfig::Get().loadFromFile(configRoot, priorityTuning)) {
			return loadFailure(priorityTuning, PriorityConfig::Get().problems());
		}

		// Every task type the AI can evaluate must have an explicit arbitration tier, or the
		// (tier, score) ordering would put a new type in an unintended tier. Fail loud instead.
		const std::vector<std::string> requiredTaskTiers(kTaskTypeNames.begin(), kTaskTypeNames.end());
		std::vector<std::string>	   missingTiers;
		if (!PriorityConfig::Get().validateTaskTiers(requiredTaskTiers, missingTiers)) {
			std::string joined;
			for (const auto& name : missingTiers) {
				joined += (joined.empty() ? "" : ", ") + name;
			}
			return {priorityTuning.generic_string() + " is missing an arbitration tier for task type(s): " + joined};
		}

		const std::filesystem::path construction = "construction";
		if (!ConstructionRegistry::Get().load((configRoot / construction).string())) {
			return loadFailure(construction);
		}

		const std::filesystem::path mood = "colonists/mood.xml";
		if (!MoodConfig::Get().loadFromFile(configRoot, mood)) {
			return loadFailure(mood, MoodConfig::Get().problems());
		}

		const std::filesystem::path memory = "colonists/memory.xml";
		if (!MemoryConfig::Get().loadFromFile(configRoot, memory)) {
			return loadFailure(memory, MemoryConfig::Get().problems());
		}

		if (ConfigValidator::validateAll()) {
			return {};
		}
		std::vector<std::string> errors;
		for (const auto& error : ConfigValidator::getErrors()) {
			std::string message = "[" + error.source + "] " + error.message;
			if (!error.context.empty()) {
				message += " (" + error.context + ")";
			}
			errors.push_back(std::move(message));
		}
		if (errors.empty()) {
			errors.emplace_back("Config validation failed");
		}
		return errors;
	}

} // namespace assets
