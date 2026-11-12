#pragma once

#include <string>

namespace assets {

	/// Find the assets root and shared Lua scripts, load definitions into AssetRegistry
	/// (on a worker thread when `async`, otherwise blocking), then load recipes into
	/// RecipeRegistry against the finished definition index. When `async`, recipes are
	/// readable once AssetRegistry::isLoadComplete() is true.
	void initializeAssetSystem(bool async, const std::string& assetsRootPath = "assets/world");

} // namespace assets
