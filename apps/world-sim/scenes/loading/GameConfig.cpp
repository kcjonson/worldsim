#include "GameConfig.h"

#include <assets/CreatureConfigFiles.h>
#include <assets/WorkConfig.h>

#include <utility>

namespace world_sim {

	GameConfigLoad loadGameConfig(const std::filesystem::path& configRoot, const std::filesystem::path& humanFolder) {
		GameConfigLoad				load;
		assets::CreatureConfigFiles human = assets::creatureConfigFiles(humanFolder);
		if (!human.errors.empty()) {
			load.errors = std::move(human.errors);
			return load;
		}
		load.errors = assets::loadWorkConfig(configRoot, human);
		ecs::ColonistRulesLoad colonistRules = ecs::loadColonistRules(configRoot, human);
		load.errors.insert(load.errors.end(), colonistRules.errors.begin(), colonistRules.errors.end());
		load.rules = std::move(colonistRules.rules);
		return load;
	}

} // namespace world_sim
