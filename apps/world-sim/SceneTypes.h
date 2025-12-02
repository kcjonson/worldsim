#pragma once

#include <scene/SceneManager.h>
#include <cstddef>

namespace world_sim {

/// @brief Scene types for world-sim application
/// Each scene declares its own human-readable name via static kSceneName member
enum class SceneType : std::size_t {
	Splash = 0,
	MainMenu,
	Game,
	Settings,
	WorldCreator,
	Count // Must be last
};

/// @brief Convert app-specific SceneType to engine::SceneKey
inline engine::SceneKey toKey(SceneType type) {
	return static_cast<engine::SceneKey>(type);
}

/// @brief Initialize SceneManager with all world-sim scenes
void initializeSceneManager();

} // namespace world_sim
