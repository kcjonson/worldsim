#include "GameBootstrap.h"

#include <application/App.h>
#include <assets/AssetBootstrap.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetValidator.h>
#include <storage/UserDirectories.h>
#include <world/AppIdentity.h>
#include <world/rendering/GroundTileBank.h>
#include <world/rendering/GroundTileCache.h>

#include <cassert>
#include <memory>
#include <optional>

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
		const assets::ValidationReport& report = assets::AssetRegistry::Get().getValidationReport();
		if (report.hasErrors()) {
			GroundBakeStep failed{.done = true};
			for (const assets::ValidationIssue& issue : report.issues) {
				if (issue.severity == assets::Severity::Error) {
					failed.errors.push_back((issue.defName.empty() ? std::string("-") : issue.defName) + ": " + issue.message);
				}
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
