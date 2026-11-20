#include "GameBootstrap.h"

#include <application/App.h>
#include <assets/AssetBootstrap.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetValidator.h>
#include <assets/RecipeRegistry.h>
#include <storage/UserDirectories.h>
#include <world/AppIdentity.h>
#include <world/rendering/GroundTileBank.h>
#include <world/rendering/GroundTileCache.h>

#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace world_sim {

	namespace {
		std::optional<worldview::GroundTileCache>  g_groundCache;
		std::unique_ptr<worldview::GroundTileBank> g_groundTiles;
	} // namespace

	void initializeResources(app::App& app, bool hasSceneArg) {
		// Async only on the normal splash path; a --scene override loads synchronously so
		// the jumped-to scene has assets ready before its onEnter.
		assets::initializeAssetSystem(!hasSceneArg);
		g_groundCache = worldview::GroundTileCache::inUserData(app::userDataDir(worldview::kOrganization, worldview::kGameName));
		g_groundTiles = std::make_unique<worldview::GroundTileBank>(worldview::GroundTileBank::Args{
			.restoreCoordinateSystem = &app.coordinateSystem(), .cache = g_groundCache.has_value() ? &*g_groundCache : nullptr
		});
	}

	void shutdownResources() {
		g_groundTiles.reset();
		g_groundCache.reset();
	}

	worldview::GroundTileBank& groundTiles() {
		assert(g_groundTiles != nullptr && "initializeResources makes the ground tile bank");
		return *g_groundTiles;
	}

	GroundBakeStep stepGroundBake() {
		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		// A dev switch from the splash can reach the loading scene while the library still loads on its worker.
		if (registry.loadProgress().started.load() && !registry.isLoadComplete()) {
			return {};
		}
		const assets::ValidationReport& report = registry.getValidationReport();
		const std::vector<std::string>& recipeProblems = assets::RecipeRegistry::Get().problems();
		if (report.hasErrors() || !recipeProblems.empty()) {
			GroundBakeStep failed{.done = true};
			for (const assets::ValidationIssue& issue : report.issues) {
				if (issue.severity == assets::Severity::Error) {
					failed.errors.push_back((issue.defName.empty() ? std::string("-") : issue.defName) + ": " + issue.message);
				}
			}
			for (const std::string& problem : recipeProblems) {
				failed.errors.push_back("[recipes] " + problem);
			}
			return failed;
		}
		worldview::GroundTileBank& bank = groundTiles();
		if (!bank.ready() && bank.bakeNext()) {
			return {};
		}
		return {.done = true, .errors = bank.errors()};
	}

} // namespace world_sim
