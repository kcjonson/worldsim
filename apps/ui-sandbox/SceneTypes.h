#pragma once

#include <scene/SceneStack.h>

namespace ui_sandbox {

	/// @brief Scene registration info - each scene exports one of these
	struct SceneInfo {
		const char*				 name;
		app::SceneStack::Factory factory;
	};

// ============================================================================
// SCENE LIST - Add new scenes here (the ONLY place you need to edit)
// ============================================================================
// clang-format off
#define UI_SANDBOX_SCENES(X) \
	X(Arena)         \
	X(Handle)        \
	X(Grass)         \
	X(VectorPerf)    \
	X(Svg)           \
	X(Tree)          \
	X(Planet)        \
	X(Gallery)       \
	X(GalleryDialog)
	// clang-format on

	namespace scenes {
#define SCENE_EXTERN(scene) extern const SceneInfo scene;
		UI_SANDBOX_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
	} // namespace scenes

	/// @brief Add every scene above to the stack under its name
	void addScenes(app::SceneStack& stack);

} // namespace ui_sandbox
