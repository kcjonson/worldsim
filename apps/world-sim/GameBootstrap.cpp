#include "GameBootstrap.h"

#include <application/App.h>
#include <assets/AssetBootstrap.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetValidator.h>
#include <world/rendering/GroundTileBank.h>

#include <cassert>
#include <memory>

namespace world_sim {

	namespace {
		std::unique_ptr<worldview::GroundTileBank> g_groundTiles;
	} // namespace

	void initializeResources(app::App& app, bool hasSceneArg) {
		// Async only on the normal splash path; a --scene override loads synchronously so
		// the jumped-to scene has assets ready before its onEnter.
		assets::initializeAssetSystem(!hasSceneArg);
		g_groundTiles = std::make_unique<worldview::GroundTileBank>(
			worldview::GroundTileBank::Args{.restoreCoordinateSystem = &app.coordinateSystem()}
		);
	}

	void shutdownResources() {
		g_groundTiles.reset();
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
