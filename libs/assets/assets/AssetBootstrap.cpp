#include "assets/AssetBootstrap.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>
#include <utils/ResourcePath.h>

namespace assets {

	void loadAssetSystem(const AssetSources& sources, bool async) {
		AssetRegistry& definitions = AssetRegistry::Get();

		// Definitions first: clearing them joins a load still on the worker, which may be loading recipes.
		definitions.clear();
		RecipeRegistry::Get().clear();
		definitions.setSharedScriptsPath(sources.sharedScripts);

		// Recipes resolve their def-name ids against the definition index, so they load after the
		// definitions in both paths: on the worker before it reports done, or after the blocking load.
		auto loadRecipes = [recipesPath = sources.recipes]() {
			if (!recipesPath.empty()) {
				RecipeRegistry::Get().loadRecipesFromFolder(recipesPath);
			}
		};

		if (async) {
			definitions.beginLoadAsync(sources.definitions, loadRecipes);
			LOG_INFO(Engine, "Asset loading started asynchronously from %s", sources.definitions.c_str());
		} else {
			const size_t loaded = definitions.loadDefinitionsFromFolder(sources.definitions);
			LOG_INFO(Engine, "Loaded %zu asset definitions from %s", loaded, sources.definitions.c_str());
			loadRecipes();
		}
	}

	void initializeAssetSystem(bool async, const std::string& assetsRootPath) {
		LOG_INFO(Engine, "Initializing asset system from %s", assetsRootPath.c_str());

		const std::string definitionsPath = Foundation::findResourceString(assetsRootPath);
		if (definitionsPath.empty()) {
			LOG_WARNING(Engine, "Assets root not found: %s", assetsRootPath.c_str());
			return;
		}

		loadAssetSystem(
			{.definitions = definitionsPath,
			 .sharedScripts = Foundation::findResourceString("assets/shared/scripts"),
			 .recipes = Foundation::findResourceString("assets/recipes")},
			async
		);
	}

} // namespace assets
