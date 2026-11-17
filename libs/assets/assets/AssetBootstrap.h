#pragma once

#include <string>

namespace assets {

	/// The folders one load of the asset system reads.
	struct AssetSources {
		std::string definitions;   // asset definitions, <Name>/<Name>.xml
		std::string sharedScripts; // Lua behind the @shared/ prefix; empty for none
		std::string recipes;	   // recipe XML; empty for none
	};

	/// Replace what AssetRegistry and RecipeRegistry hold with `sources`: clear both, load the
	/// definitions and build their id index (on a worker thread when `async`, otherwise blocking),
	/// then load the recipes against that index. The apps' startup and the asset manager's Reload
	/// go through here, since a definition's interned id is only good for the index it was read
	/// from, and recipes hold ids.
	/// When `async`, both registries are readable once AssetRegistry::isLoadComplete() is true.
	void loadAssetSystem(const AssetSources& sources, bool async);

	/// Find the shipped assets root, shared Lua scripts and recipes, and load them.
	void initializeAssetSystem(bool async, const std::string& assetsRootPath = "assets/world");

} // namespace assets
