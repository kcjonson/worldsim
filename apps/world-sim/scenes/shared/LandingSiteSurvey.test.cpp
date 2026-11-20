#include "scenes/shared/LandingSiteSurvey.h"

#include <worldgen/sampling/RiverWorld.test.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

namespace world_sim {
	namespace {

		// Grassland in |lat| < 60, |lon| < 25, ocean elsewhere, with a river along the equator from lon -10 eastward.
		std::shared_ptr<worldgen::GeneratedWorld> riverWorld() {
			auto world = worldgen::test_support::grasslandWorld(worldgen::test_support::GrasslandBlock{});
			worldgen::test_support::carveRiver(*world, world->grid->fromLatLon(0.0, -10.0), world->grid->fromLatLon(0.0, 20.0));
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

	// Whatever the worker was doing, a cancel leaves no survey, no pick waiting, and no sites to take.
	TEST(LandingSiteSurvey, CancelLeavesNothingRunningOrToTake) {
		LandingSiteSurvey survey;
		survey.start(riverWorld(), grasslandProvisions(), worldgen::SuggestedSiteQuery{});
		survey.resolve({0.0, 0.0});
		survey.cancel();
		EXPECT_FALSE(survey.surveying());
		EXPECT_FALSE(survey.resolving());
		EXPECT_FALSE(survey.takeSites().has_value());
	}

} // namespace world_sim
