// PlayerVisibilitySystem's main-thread time a tick (player-visibility.md 6, step 3): colonists walking
// in open ground from the landing, at 1x and 10x. Each run is 36,000 ticks, ten minutes of frames at
// 60 a second: the landing polygon goes in first, then every colonist walks at 2 m/s (20 m/s at 10x)
// on headings that change by up to 60 degrees every 2 to 15 s, turned back toward the drop past
// 400 m from it. Vision runs every 5th tick as in the game; only PlayerVisibilitySystem::update is
// timed. Arguments: colonists, game speed.

#include "PlayerVisibilitySystem.h"

#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include <benchmark/benchmark.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

namespace {

	using namespace ecs;

	struct Walker {
		EntityID colonist  = 0;
		float	 heading   = 0.0F;
		float	 untilTurn = 0.0F;
	};

	void BM_PlayerVisibilityWalk(benchmark::State& state) {
		const int	colonists = static_cast<int>(state.range(0));
		const float speed	  = static_cast<float>(state.range(1));
		constexpr float kTick		   = 1.0F / 60.0F;
		constexpr int	kTicks		   = 60 * 60 * 10;
		constexpr float kWalkMps	   = 2.0F;
		constexpr float kTurnBackMeters = 400.0F;

		for (auto iteration : state) {
			World					world;
			VisionSystem&			vision	   = world.registerSystem<VisionSystem>();
			PlayerVisibilitySystem& visibility = world.registerSystem<PlayerVisibilitySystem>();
			std::mt19937			random(29);
			std::uniform_real_distribution<float> unit(0.0F, 1.0F);
			visibility.revealLanding({0.0F, 0.0F}, 150.0F);

			std::vector<Walker> walkers;
			for (int i = 0; i < colonists; ++i) {
				Walker walker;
				walker.colonist = world.createEntity();
				const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(colonists);
				world.addComponent<Position>(walker.colonist, Position{glm::vec2{std::cos(angle), std::sin(angle)} * 3.0F});
				world.addComponent<Colonist>(walker.colonist, Colonist{"Walker"});
				world.addComponent<Memory>(walker.colonist, Memory{});
				walker.heading = angle;
				walkers.push_back(walker);
			}

			std::vector<double> tickMs;
			tickMs.reserve(kTicks);
			for (int tick = 0; tick < kTicks; ++tick) {
				for (Walker& walker : walkers) {
					glm::vec2& at = world.getComponent<Position>(walker.colonist)->value;
					walker.untilTurn -= kTick * speed;
					if (walker.untilTurn <= 0.0F) {
						const float turn  = (unit(random) - 0.5F) * 2.0F * std::numbers::pi_v<float> / 3.0F;
						const float pause = 2.0F + 13.0F * unit(random);
						walker.heading += turn;
						walker.untilTurn = pause;
						if (glm::length(at) > kTurnBackMeters) {
							walker.heading = std::atan2(-at.y, -at.x) + turn / 2.0F;
						}
					}
					at += glm::vec2{std::cos(walker.heading), std::sin(walker.heading)} * (kWalkMps * speed * kTick);
				}
				vision.update(kTick);
				const auto start = std::chrono::steady_clock::now();
				visibility.update(kTick);
				tickMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			}

			std::vector<double> sorted = tickMs;
			std::sort(sorted.begin(), sorted.end());
			double sum = 0.0;
			for (const double value : tickMs) {
				sum += value;
			}
			const double mean = sum / static_cast<double>(tickMs.size());
			const double p99  = sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))];
			const double most = sorted.back();
			std::fprintf(stderr, "[PlayerVisibility] %d colonists at %.0fx: tick ms mean %.4f p99 %.4f max %.3f; rings %zu, dropped %zu, seen %.0f m2\n",
						 colonists, static_cast<double>(speed), mean, p99, most, visibility.seenArea().ringsRegistered(),
						 visibility.seenArea().shapesDropped(), visibility.seenArea().committedSquareMeters());
			state.counters["meanMs"] = mean;
			state.counters["p99Ms"]	 = p99;
			state.counters["maxMs"]	 = most;
			state.counters["rings"]	 = static_cast<double>(visibility.seenArea().ringsRegistered());
			benchmark::DoNotOptimize(iteration);
		}
	}

} // namespace

BENCHMARK(BM_PlayerVisibilityWalk)
	->Args({1, 1})
	->Args({10, 1})
	->Args({50, 1})
	->Args({1, 10})
	->Args({10, 10})
	->Args({50, 10})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
