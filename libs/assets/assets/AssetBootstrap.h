#pragma once

#include <utils/ResourcePath.h>

#include <string>

namespace assets {

	/// The folders one load of the asset system reads.
	struct AssetSources {
		std::string definitions;   // asset definitions, <Name>/<Name>.xml
		std::string sharedScripts; // Lua behind the @shared/ prefix; empty for none
		std::string recipes;	   // recipe XML; empty for none
		// The config root the view config (ViewConfig.h) is read from: the shipped assets/config unless a caller
		// names another; empty when it can't be found, which leaves the view config unloaded and its parts defs
		// unread.
		std::string config = Foundation::findResourceString("assets/config");
	};

	/// Replace what AssetRegistry and RecipeRegistry hold with `sources`: read the view config, clear both, load the
	/// definitions and build their id index (on a worker thread when `async`, otherwise blocking),
	/// then load the recipes against that index. The apps' startup and the asset manager's Reload
	/// go through here, since a definition's interned id is only good for the index it was read
	/// from, and recipes hold ids.
	/// When `async`, both registries are readable once AssetRegistry::isLoadComplete() is true.
	void loadAssetSystem(const AssetSources& sources, bool async);

	/// Find the shipped assets root, shared Lua scripts and recipes, and load them.
	void initializeAssetSystem(bool async, const std::string& assetsRootPath = "assets/world");

} // namespace assets
