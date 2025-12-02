#pragma once

#include <scene/SceneManager.h>
#include <cstddef>

namespace ui_sandbox {

/// @brief Scene types for ui-sandbox application
/// Each scene declares its own human-readable name via static kSceneName member
enum class SceneType : std::size_t {
	Shapes = 0,
	Arena,
	Handle,
	Button,
	TextInput,
	Grass,
	VectorPerf,
	VectorStar,
	Svg,
	Clip,
	Layer,
	TextShapes,
	SdfMinimal,
	InputTest,
	Tree,
	Count // Must be last
};

/// @brief Convert app-specific SceneType to engine::SceneKey
inline engine::SceneKey toKey(SceneType type) {
	return static_cast<engine::SceneKey>(type);
}

/// @brief Initialize SceneManager with all ui-sandbox scenes
void initializeSceneManager();

} // namespace ui_sandbox
