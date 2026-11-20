#include "scenes/shared/LandingSiteSurvey.h"

#include <worldgen/data/PlanetParams.h>
#include <worldgen/data/WorldData.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace world_sim {
	namespace {

		constexpr uint32_t kSubdivision = 24;

		// Grassland in |lat| < 60, |lon| < 25, ocean elsewhere, with a river along the equator from lon -10 eastward.
		std::shared_ptr<worldgen::GeneratedWorld> riverWorld() {
			auto world = std::make_shared<worldgen::GeneratedWorld>();
			world->params.gridSubdivision = kSubdivision;
			world->derived = worldgen::derive(world->params);
			world->grid = std::make_shared<worldgen::SphereGrid>(kSubdivision);
			world->data.allocate(world->grid->tileCount());
			world->validFields = static_cast<uint32_t>(worldgen::WorldField::Elevation) | static_cast<uint32_t>(worldgen::WorldField::Biome) |
								 static_cast<uint32_t>(worldgen::WorldField::Flags) | static_cast<uint32_t>(worldgen::WorldField::FlowAccum) |
								 static_cast<uint32_t>(worldgen::WorldField::Downhill) |
								 static_cast<uint32_t>(worldgen::WorldField::TemperatureMean) |
								 static_cast<uint32_t>(worldgen::WorldField::TemperatureRange) |
								 static_cast<uint32_t>(worldgen::WorldField::Precipitation);
			for (worldgen::TileId t = 0; t < world->grid->tileCount(); ++t) {
				double lat = 0.0;
				double lon = 0.0;
				world->grid->latLonOf(t, lat, lon);
				const bool land = std::abs(lat) < 60.0 && std::abs(lon) < 25.0;
				world->data.elevation[t] = land ? 500.0F : -3000.0F;
				world->data.biome[t] = static_cast<uint8_t>(land ? worldgen::Biome::TemperateGrassland : worldgen::Biome::Ocean);
				world->data.flags[t] = land ? 0 : worldgen::kFlagOcean;
				world->data.temperatureMean[t] = 150;
				world->data.temperatureRange[t] = 100;
				world->data.precipitation[t] = 900;
			}
			worldgen::TileId current = world->grid->fromLatLon(0.0, -10.0);
			const worldgen::TileId mouth = world->grid->fromLatLon(0.0, 20.0);
			const worldgen::Vec3d  target = world->grid->tileCenter(mouth);
			float				   flow = 90.0F;
			while (current != mouth) {
				std::array<worldgen::TileId, 6> neighbours{};
				const uint32_t					count = world->grid->neighbors(current, neighbours);
				uint32_t						best = 0;
				double							bestDot = -2.0;
				for (uint32_t i = 0; i < count; ++i) {
					const worldgen::Vec3d centre = world->grid->tileCenter(neighbours[i]);
					const double		  dot = centre.x * target.x + centre.y * target.y + centre.z * target.z;
					if (dot > bestDot) {
						bestDot = dot;
						best = i;
					}
				}
				world->data.flags[current] |= worldgen::kFlagRiver;
				world->data.flowAccum[current] = flow;
				world->data.downhill[current] = static_cast<uint8_t>(best);
				flow += 15.0F;
				current = neighbours[best];
			}
			return world;
		}

		worldgen::BiomeProvisionTable grasslandProvisions() {
			worldgen::BiomeProvisionTable provisions{};
			provisions[static_cast<std::size_t>(worldgen::Biome::TemperateGrassland)] = {300.0F, 300.0F};
			return provisions;
		}

		template <typename Ready> bool waitFor(Ready ready) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
			while (!ready()) {
				if (std::chrono::steady_clock::now() > deadline) {
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return true;
		}

	} // namespace

	TEST(LandingSiteSurvey, FindsSitesOnceThenResolvesTheNewestPick) {
		const std::shared_ptr<const worldgen::GeneratedWorld> world = riverWorld();
		LandingSiteSurvey									  survey;
		survey.start(world, grasslandProvisions(), worldgen::SuggestedSiteQuery{});

		std::optional<std::vector<worldgen::LandingSite>> sites;
		ASSERT_TRUE(waitFor([&] { return (sites = survey.takeSites()).has_value(); }));
		ASSERT_FALSE(sites->empty());
		EXPECT_FALSE(survey.surveying());
		EXPECT_FALSE(survey.takeSites().has_value()) << "the sites are handed back once";

		const worldgen::LatLon stale{1.0, 1.0};
		const worldgen::LatLon newest = sites->front().origin;
		survey.resolve(stale);
		survey.resolve(newest);
		std::optional<LandingSiteSurvey::ResolvedPick> resolved;
		ASSERT_TRUE(waitFor([&] { return !survey.resolving() && (resolved = survey.takeResolved()).has_value(); }));
		EXPECT_EQ(resolved->pick.latDeg, newest.latDeg);
		EXPECT_EQ(resolved->pick.lonDeg, newest.lonDeg);
		EXPECT_EQ(resolved->landing.tile, sites->front().tile);
	}

	TEST(LandingSiteSurvey, CancelStopsAScanInProgress) {
		LandingSiteSurvey survey;
		survey.start(riverWorld(), grasslandProvisions(), worldgen::SuggestedSiteQuery{});
		survey.resolve({0.0, 0.0});
		survey.cancel();
		EXPECT_FALSE(survey.surveying());
		EXPECT_FALSE(survey.resolving());
		EXPECT_FALSE(survey.takeSites().has_value());
	}

} // namespace world_sim
