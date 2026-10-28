#pragma once

#include <app/UiScene.h>

namespace app {
	class SceneStack;
} // namespace app

namespace ui_sandbox {

	/// @brief Scene registration info - each scene exports one of these
	struct SceneInfo {
		const char*			   name;
		engine::UiSceneFactory factory;
	};

// ============================================================================
// SCENE LIST - Add new scenes here (the ONLY place you need to edit)
// ============================================================================
// clang-format off
#define UI_SANDBOX_SCENES(X) \
	X(Arena)       \
	X(Handle)      \
	X(Button)      \
	X(TabBar)      \
	X(TextInput)   \
	X(Grass)       \
	X(VectorPerf)  \
	X(Svg)         \
	X(Clip)        \
	X(Layer)       \
	X(TextShapes)  \
	X(Tree)        \
	X(Layout)      \
	X(Scroll)      \
	X(Icon)        \
	X(TreeView_)   \
	X(Dropdown)    \
	X(Toast_)      \
	X(Dialog_)     \
	X(Tooltip_)    \
	X(ContextMenu_) \
	X(TextWrap) \
	X(Planet)  \
	X(Salvage) \
	X(SalvageDialog)
	// clang-format on

	namespace scenes {
#define SCENE_EXTERN(scene) extern const SceneInfo scene;
		UI_SANDBOX_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
	} // namespace scenes

	/// @brief Add every scene above to the stack under its name
	void addScenes(app::SceneStack& stack);

} // namespace ui_sandbox
