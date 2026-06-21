#pragma once

// Scene registry for the Asset Manager app. The X-macro is the single source of
// truth for the scene list (mirrors the ui-sandbox pattern).

#include <scene/SceneManager.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace asset_manager {

	struct SceneInfo {
		const char*			 name;
		engine::SceneFactory factory;
	};

#define ASSET_MANAGER_SCENES(X) \
	X(Browser)

	enum class SceneType : std::size_t {
#define SCENE_ENUM(name) name,
		ASSET_MANAGER_SCENES(SCENE_ENUM)
#undef SCENE_ENUM
			Count
	};

	inline engine::SceneKey toKey(SceneType type) {
		return static_cast<engine::SceneKey>(type);
	}

	void initializeSceneManager();

} // namespace asset_manager
