#include "QuickStartLanding.bench.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/BiomeProvisions.h>
#include <worldgen/io/PlanetIO.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace bench {

	std::filesystem::path projectRoot() {
		std::filesystem::path path = __FILE__;
		return path.parent_path().parent_path().parent_path().parent_path().parent_path();
	}

	namespace {

		struct Outcome {
			std::optional<QuickStartLanding> landing;
			std::string						 skip;
			std::string						 error;
		};

		Outcome findQuickStartLanding() {
			const char*					planetEnv = std::getenv("WORLDSIM_PLANET");
			const std::filesystem::path planetPath =
				planetEnv != nullptr ? std::filesystem::path(planetEnv) : projectRoot() / "planets" / "quickstart.wsplanet";
			if (!std::filesystem::exists(planetPath)) {
				return {.skip = "no planet (WORLDSIM_PLANET)"};
			}
			QuickStartLanding found;
			found.planet = worldgen::loadPlanet(planetPath);
			if (!found.planet) {
				return {.error = "the planet at " + planetPath.string() + " failed to load"};
			}
			assets::AssetRegistry& registry = assets::AssetRegistry::Get();
			registry.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");
			const std::filesystem::path worldAssets = projectRoot() / "assets" / "world";
			if (registry.loadDefinitionsFromFolder(worldAssets.string()) == 0) {
				return {.error = "no asset definitions loaded from " + worldAssets.string()};
			}
			const std::vector<worldgen::LandingSite> sites = worldgen::findSuggestedLandingSites(
				found.planet, assets::biomeProvisionsFromDefinitions(registry), worldgen::quickStartSiteQuery()
			);
			if (sites.empty()) {
				return {.error = "the planet has no Quick Start landing site"};
			}
			found.landing = sites.front();
			found.drop = {static_cast<float>(found.landing.spawn.xMeters), static_cast<float>(found.landing.spawn.yMeters)};
			return {.landing = std::move(found)};
		}

	} // namespace

	const QuickStartLanding* quickStartLanding(benchmark::State& state) {
		static const Outcome outcome = findQuickStartLanding();
		if (!outcome.skip.empty()) {
			state.SkipWithMessage(outcome.skip);
		} else if (!outcome.error.empty()) {
			state.SkipWithError(outcome.error);
		}
		return outcome.landing ? &*outcome.landing : nullptr;
	}

} // namespace bench
