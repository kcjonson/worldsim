#pragma once

#include <cstddef>
#include <scene/SceneManager.h>

namespace ui_sandbox {

	/// @brief Scene registration info - each scene exports one of these
	struct SceneInfo {
		const char*			 name;
		engine::SceneFactory factory;
	};

// ============================================================================
// SCENE LIST - Add new scenes here (the ONLY place you need to edit)
// ============================================================================
// clang-format off
#define UI_SANDBOX_SCENES(X) \
	X(Shapes)      \
	X(Arena)       \
	X(Handle)      \
	X(Button)      \
	X(TabBar)      \
	X(TextInput)   \
	X(Grass)       \
	X(VectorPerf)  \
	X(VectorStar)  \
	X(Svg)         \
	X(Clip)        \
	X(Layer)       \
	X(TextShapes)  \
	X(SdfMinimal)  \
	X(InputTest)   \
	X(Tree)
	// clang-format on

	/// @brief Scene types for ui-sandbox application (auto-generated from SCENE_LIST)
	enum class SceneType : std::size_t {
#define SCENE_ENUM(name) name,
		UI_SANDBOX_SCENES(SCENE_ENUM)
#undef SCENE_ENUM
			Count // Must be last
	};

	/// @brief Convert app-specific SceneType to engine::SceneKey
	inline engine::SceneKey toKey(SceneType type) {
		return static_cast<engine::SceneKey>(type);
	}

	/// @brief Initialize SceneManager with all ui-sandbox scenes
	void initializeSceneManager();

} // namespace ui_sandbox
