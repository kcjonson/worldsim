#include "assets/WorkConfig.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"
#include "assets/MemoryConfig.h"
#include "assets/MoodConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/WorkTypeRegistry.h"

#include <cassert>

namespace assets {

	std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot, const CreatureConfigFiles& human) {
		assert(human.errors.empty() && "loadWorkConfig takes a human folder with no errors; its errors are the load's");
		assert(human.mood.size() == 1 && human.memory.size() == 1 && "loadWorkConfig reads the human's one <Mood> and one <Memory> file");
		ActionTypeRegistry::Get().clear();
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

		const std::filesystem::path actionTypes = "legacy/action-types.xml";
		if (!ActionTypeRegistry::Get().loadFromFile(configRoot, actionTypes)) {
			return loadFailure(actionTypes, ActionTypeRegistry::Get().problems());
		}

		const std::filesystem::path workTypes = "legacy/work-types.xml";
		if (!WorkTypeRegistry::Get().loadFromFile(configRoot, workTypes)) {
			return loadFailure(workTypes, WorkTypeRegistry::Get().problems());
		}

		// The arbitration folder holds the one file that declares the ladder and the bonuses, root <arbitration>.
		const std::filesystem::path arbitrationFolder = configRoot / "arbitration";
		const ConfigFiles			arbitration = configFilesIn(arbitrationFolder);
		if (arbitration.error) {
			return {"[arbitration] " + *arbitration.error};
		}
		if (arbitration.files.size() != 1) {
			std::string names;
			for (const std::filesystem::path& file : arbitration.files) {
				names += (names.empty() ? "" : ", ") + configFileName(file, arbitrationFolder);
			}
			return {
				"[arbitration] arbitration: " + std::to_string(arbitration.files.size()) +
				" files; one file declares the ladder and the bonuses" + (names.empty() ? std::string() : " (" + names + ")")
			};
		}
		const std::filesystem::path priorityTuning = configFileName(arbitration.files.front(), arbitrationFolder);
		if (!PriorityConfig::Get().loadFromFile(arbitrationFolder, priorityTuning)) {
			std::vector<std::string> problems = loadFailure(priorityTuning, PriorityConfig::Get().problems());
			for (std::string& problem : problems) {
				problem.insert(0, "[arbitration] ");
			}
			return problems;
		}

		// The decision ranks every option on a rung it names; a ladder without one of them, or with them out of order,
		// never reaches play.
		if (!PriorityConfig::Get().decisionRungProblems().empty()) {
			std::vector<std::string> problems = PriorityConfig::Get().decisionRungProblems();
			for (std::string& problem : problems) {
				problem.insert(0, "[arbitration] ");
			}
			return problems;
		}

		const std::filesystem::path construction = "construction";
		if (!ConstructionRegistry::Get().load((configRoot / construction).string())) {
			return loadFailure(construction);
		}

		const std::filesystem::path mood = configFileName(human.mood.front(), human.folder);
		if (!MoodConfig::Get().loadFromFile(human.folder, mood)) {
			return loadFailure(mood, MoodConfig::Get().problems());
		}

		const std::filesystem::path memory = configFileName(human.memory.front(), human.folder);
		if (!MemoryConfig::Get().loadFromFile(human.folder, memory)) {
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
