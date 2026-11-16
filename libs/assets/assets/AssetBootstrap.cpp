#include "assets/AssetBootstrap.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>
#include <utils/ResourcePath.h>

namespace assets {

	void initializeAssetSystem(bool async, const std::string& assetsRootPath) {
		LOG_INFO(Engine, "Initializing asset system from %s", assetsRootPath.c_str());

		// Find the assets root folder
		std::string fullPath = Foundation::findResourceString(assetsRootPath);
		if (fullPath.empty()) {
			LOG_WARNING(Engine, "Assets root not found: %s", assetsRootPath.c_str());
			return;
		}

		// Set shared scripts path (for @shared/ prefix resolution)
		std::string sharedPath = Foundation::findResourceString("assets/shared/scripts");
		if (!sharedPath.empty()) {
			AssetRegistry::Get().setSharedScriptsPath(sharedPath);
			LOG_INFO(Engine, "Set shared scripts path: %s", sharedPath.c_str());
		}

		// Recipes resolve their def-name ids against the definition index, so they load after the
		// definitions in both paths: on the worker before it reports done, or after the blocking load.
		auto loadRecipes = [recipesPath = Foundation::findResourceString("assets/recipes")]() {
			if (!recipesPath.empty()) {
				RecipeRegistry::Get().loadRecipesFromFolder(recipesPath);
			}
		};

		if (async) {
			AssetRegistry::Get().beginLoadAsync(fullPath, loadRecipes);
			LOG_INFO(Engine, "Asset loading started asynchronously from %s", fullPath.c_str());
		} else {
			size_t loaded = AssetRegistry::Get().loadDefinitionsFromFolder(fullPath);
			LOG_INFO(Engine, "Loaded %zu asset definitions from %s", loaded, fullPath.c_str());
			loadRecipes();
		}
	}

} // namespace assets
