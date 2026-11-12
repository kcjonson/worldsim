#include "assets/WorkConfig.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"
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
		ConfigValidator::clearErrors();

		auto loadFailure = [](const std::filesystem::path& path) {
			return std::vector<std::string>{"Failed to load " + path.generic_string()};
		};

		const auto actionTypes = configRoot / "actions" / "action-types.xml";
		if (!ActionTypeRegistry::Get().loadFromFile(actionTypes.string())) {
			return loadFailure(actionTypes);
		}

		const auto taskChains = configRoot / "work" / "task-chains.xml";
		if (!TaskChainRegistry::Get().loadFromFile(taskChains.string())) {
			return loadFailure(taskChains);
		}

		const auto workTypes = configRoot / "work" / "work-types.xml";
		if (!WorkTypeRegistry::Get().loadFromFile(workTypes.string())) {
			return loadFailure(workTypes);
		}

		const auto priorityTuning = configRoot / "work" / "priority-tuning.xml";
		if (!PriorityConfig::Get().loadFromFile(priorityTuning.string())) {
			return loadFailure(priorityTuning);
		}

		// Every task type the AI can evaluate must have an explicit arbitration tier, or the
		// (tier, score) ordering would put a new type in an unintended tier. Fail loud instead.
		static const std::vector<std::string> kRequiredTaskTiers = {
			"FulfillNeed", "Harvest", "Craft", "Haul", "PlacePackaged", "Build", "Deconstruct", "Wander"
		};
		std::vector<std::string> missingTiers;
		if (!PriorityConfig::Get().validateTaskTiers(kRequiredTaskTiers, missingTiers)) {
			std::string joined;
			for (const auto& name : missingTiers) {
				joined += (joined.empty() ? "" : ", ") + name;
			}
			return {"priority-tuning.xml is missing an arbitration tier for task type(s): " + joined};
		}

		const auto construction = configRoot / "construction";
		if (!ConstructionRegistry::Get().load(construction.string())) {
			return loadFailure(construction);
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
