#pragma once

#include <scene/SceneManager.h>
#include <cstddef>

namespace world_sim {

/// @brief Scene registration info - each scene exports one of these
struct SceneInfo {
	const char* name;
	engine::SceneFactory factory;
};

// ============================================================================
// SCENE LIST - Add new scenes here (the ONLY place you need to edit)
// ============================================================================
// clang-format off
#define WORLD_SIM_SCENES(X) \
	X(Splash)       \
	X(MainMenu)     \
	X(Game)         \
	X(Settings)     \
	X(WorldCreator)
// clang-format on

/// @brief Scene types for world-sim application (auto-generated from SCENE_LIST)
enum class SceneType : std::size_t {
#define SCENE_ENUM(name) name,
	WORLD_SIM_SCENES(SCENE_ENUM)
#undef SCENE_ENUM
	Count // Must be last
};

/// @brief Convert app-specific SceneType to engine::SceneKey
inline engine::SceneKey toKey(SceneType type) {
	return static_cast<engine::SceneKey>(type);
}

/// @brief Initialize SceneManager with all world-sim scenes
void initializeSceneManager();

} // namespace world_sim
