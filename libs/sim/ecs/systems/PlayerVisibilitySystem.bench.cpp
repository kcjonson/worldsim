// PlayerVisibilitySystem's main-thread time a tick (player-visibility.md 6.5, 6.6): colonists walking
// in open ground from the landing, and in a base beyond it (BM_PlayerVisibilityBase, below), at 1x
// and 10x. Each walk run is 36,000 ticks, ten minutes of frames at
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

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <benchmark/benchmark.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

	struct TickTimes {
		double mean = 0.0;
		double p99	= 0.0;
		double most = 0.0;
	};

	TickTimes timesOf(const std::vector<double>& tickMs) {
		std::vector<double> sorted = tickMs;
		std::sort(sorted.begin(), sorted.end());
		double sum = 0.0;
		for (const double value : tickMs) {
			sum += value;
		}
		return {sum / static_cast<double>(tickMs.size()), sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))], sorted.back()};
	}

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
			std::size_t mostPending = 0;
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
				mostPending = std::max(mostPending, visibility.seenArea().shapesPending());
			}

			const TickTimes times = timesOf(tickMs);
			std::fprintf(stderr, "[PlayerVisibility] %d colonists at %.0fx: tick ms mean %.4f p99 %.4f max %.3f; rings %zu, dropped %zu, pending at most %zu, seen %.0f m2\n",
						 colonists, static_cast<double>(speed), times.mean, times.p99, times.most, visibility.seenArea().ringsRegistered(),
						 visibility.seenArea().shapesDropped(), mostPending, visibility.seenArea().committedSquareMeters());
			state.counters["meanMs"] = times.mean;
			state.counters["p99Ms"]	 = times.p99;
			state.counters["maxMs"]	 = times.most;
			state.counters["rings"]	 = static_cast<double>(visibility.seenArea().ringsRegistered());
			benchmark::DoNotOptimize(iteration);
		}
	}

	// --- A base beyond the landing area ------------------------------------------------------
	//
	// Eight rooms in two rows (columns 5, 8, 4 and 6.5 m, rows 6 and 4.5 m), Wood walls of the Standard
	// preset with a 1 m gap in the middle of every inner wall and of the two west and the two south
	// outer walls of the end rooms, 250 m east of a landing of 150 m: the record starts with nothing
	// of it. Colonists walk at 2 m/s (20 m/s at 10x) from room to room through the gaps, and one walk
	// in four goes out of the west gap of the first room to a point 3 to 40 m out and back. Walls
	// block sight and seen walls join the record, as in the game. Same timing as the walk.

	constexpr std::array<float, 5> kColumnsAt{0.0F, 5.0F, 13.0F, 17.0F, 23.5F};
	constexpr std::array<float, 3> kRowsAt{0.0F, 6.0F, 10.5F};
	constexpr glm::vec2			   kBaseOrigin{250.0F, -5.0F};
	constexpr float				   kGapHalfMeters = 0.5F;

	glm::vec2 roomCentre(std::size_t column, std::size_t row) {
		return kBaseOrigin + glm::vec2{(kColumnsAt[column] + kColumnsAt[column + 1]) / 2.0F, (kRowsAt[row] + kRowsAt[row + 1]) / 2.0F};
	}

	geometry::Vec2i64 millimetres(glm::vec2 meters) { return {std::llround(meters.x * 1000.0F), std::llround(meters.y * 1000.0F)}; }

	// A wall from `from` to `to` (metres from the base's origin), with a gap in its middle when `gap`.
	void buildWall(construction::ConstructionWorld& walls, glm::vec2 from, glm::vec2 to, bool gap) {
		auto commit = [&walls](glm::vec2 start, glm::vec2 end) {
			const construction::SegmentCommitResult result =
				walls.commitSegment(millimetres(kBaseOrigin + start), millimetres(kBaseOrigin + end), "Wood", "Standard", construction::kInvalidFoundation);
			for (const construction::SegmentId segment : result.createdSegments) {
				walls.setSegmentState(segment, construction::FoundationState::Built);
			}
		};
		if (!gap) {
			commit(from, to);
			return;
		}
		const glm::vec2 middle = (from + to) / 2.0F;
		const glm::vec2 along  = glm::normalize(to - from) * kGapHalfMeters;
		commit(from, middle - along);
		commit(middle + along, to);
	}

	void buildBase(construction::ConstructionWorld& walls) {
		const std::size_t columns = kColumnsAt.size() - 1;
		const std::size_t rows	  = kRowsAt.size() - 1;
		for (std::size_t line = 0; line <= columns; ++line) {
			for (std::size_t row = 0; row < rows; ++row) {
				const bool inner = line > 0 && line < columns;
				buildWall(walls, {kColumnsAt[line], kRowsAt[row]}, {kColumnsAt[line], kRowsAt[row + 1]}, inner || (line == 0));
			}
		}
		for (std::size_t line = 0; line <= rows; ++line) {
			for (std::size_t column = 0; column < columns; ++column) {
				const bool inner = line > 0 && line < rows;
				buildWall(walls, {kColumnsAt[column], kRowsAt[line]}, {kColumnsAt[column + 1], kRowsAt[line]}, inner || (line == 0 && (column == 0 || column == columns - 1)));
			}
		}
	}

	struct Resident {
		EntityID			   colonist = 0;
		std::size_t			   column	= 0;
		std::size_t			   row		= 0;
		std::vector<glm::vec2> route;
	};

	// The next walk: to a random room, a column at a time then a row, each step through the gap in the
	// wall between; or, one in four, out of the west gap of the first room and back.
	void planWalk(Resident& resident, std::mt19937& random) {
		std::uniform_real_distribution<float> unit(0.0F, 1.0F);
		const std::size_t					  columns = kColumnsAt.size() - 1;
		const std::size_t					  rows	  = kRowsAt.size() - 1;
		const bool							  outside = random() % 4 == 0;
		const std::size_t					  toColumn = outside ? 0 : random() % columns;
		const std::size_t					  toRow	   = outside ? 0 : random() % rows;
		while (resident.column != toColumn) {
			const std::size_t next = resident.column < toColumn ? resident.column + 1 : resident.column - 1;
			const float		  wallX = kColumnsAt[std::max(resident.column, next)];
			resident.route.push_back(kBaseOrigin + glm::vec2{wallX, (kRowsAt[resident.row] + kRowsAt[resident.row + 1]) / 2.0F});
			resident.column = next;
			resident.route.push_back(roomCentre(resident.column, resident.row));
		}
		while (resident.row != toRow) {
			const std::size_t next	= resident.row < toRow ? resident.row + 1 : resident.row - 1;
			const float		  wallY = kRowsAt[std::max(resident.row, next)];
			resident.route.push_back(kBaseOrigin + glm::vec2{(kColumnsAt[resident.column] + kColumnsAt[resident.column + 1]) / 2.0F, wallY});
			resident.row = next;
			resident.route.push_back(roomCentre(resident.column, resident.row));
		}
		if (outside) {
			const glm::vec2 gap = kBaseOrigin + glm::vec2{0.0F, (kRowsAt[0] + kRowsAt[1]) / 2.0F};
			const float		out = 3.0F + 37.0F * unit(random);
			const float		across = (unit(random) - 0.5F) * 40.0F;
			resident.route.push_back(gap);
			resident.route.push_back(gap + glm::vec2{-out, across});
			resident.route.push_back(gap);
			resident.route.push_back(roomCentre(0, 0));
		}
		const float jitterX = unit(random) - 0.5F;
		const float jitterY = unit(random) - 0.5F;
		resident.route.push_back(roomCentre(resident.column, resident.row) + glm::vec2{jitterX, jitterY});
	}

	std::filesystem::path projectRoot() {
		std::filesystem::path path = __FILE__;
		return path.parent_path().parent_path().parent_path().parent_path().parent_path();
	}

	void BM_PlayerVisibilityBase(benchmark::State& state) {
		const int		colonists = static_cast<int>(state.range(0));
		const float		speed	  = static_cast<float>(state.range(1));
		constexpr float kTick	  = 1.0F / 60.0F;
		constexpr int	kTicks	  = 60 * 60 * 10;
		constexpr float kWalkMps  = 2.0F;

		assets::ConstructionRegistry::Get().clear();
		if (!assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string())) {
			state.SkipWithError("no construction registry");
			return;
		}
		for (auto iteration : state) {
			construction::ConstructionWorld walls;
			buildBase(walls);
			World					world;
			VisionSystem&			vision	   = world.registerSystem<VisionSystem>();
			PlayerVisibilitySystem& visibility = world.registerSystem<PlayerVisibilitySystem>();
			vision.setConstructionWorld(&walls);
			visibility.setConstructionWorld(&walls);
			visibility.revealLanding({0.0F, 0.0F}, 150.0F);
			std::mt19937 random(31);

			std::vector<Resident> residents;
			const std::size_t	  rooms = (kColumnsAt.size() - 1) * (kRowsAt.size() - 1);
			for (int i = 0; i < colonists; ++i) {
				Resident resident;
				resident.colonist = world.createEntity();
				resident.column	  = static_cast<std::size_t>(i) % rooms % (kColumnsAt.size() - 1);
				resident.row	  = static_cast<std::size_t>(i) % rooms / (kColumnsAt.size() - 1);
				world.addComponent<Position>(resident.colonist, Position{roomCentre(resident.column, resident.row)});
				world.addComponent<Colonist>(resident.colonist, Colonist{"Resident"});
				world.addComponent<Memory>(resident.colonist, Memory{});
				residents.push_back(std::move(resident));
			}

			std::vector<double> tickMs;
			tickMs.reserve(kTicks);
			std::size_t mostPending = 0;
			// The slowest tick: its time, which tick, and what it registered and queued.
			struct SlowestTick {
				double		ms		   = 0.0;
				int			tick	   = 0;
				std::size_t rings	   = 0;
				std::size_t footprints = 0;
			};
			SlowestTick slowest;
			for (int tick = 0; tick < kTicks; ++tick) {
				for (Resident& resident : residents) {
					glm::vec2& at		 = world.getComponent<Position>(resident.colonist)->value;
					float	   remaining = kWalkMps * speed * kTick;
					while (remaining > 0.0F) {
						if (resident.route.empty()) {
							planWalk(resident, random);
						}
						const glm::vec2 toward	 = resident.route.front() - at;
						const float		distance = glm::length(toward);
						if (distance <= remaining) {
							at = resident.route.front();
							resident.route.erase(resident.route.begin());
							remaining -= distance;
						} else {
							at += toward / distance * remaining;
							remaining = 0.0F;
						}
					}
				}
				vision.update(kTick);
				const std::size_t ringsBefore	   = visibility.seenArea().ringsRegistered();
				const std::size_t footprintsBefore = visibility.footprintsEntered();
				const auto		  start			   = std::chrono::steady_clock::now();
				visibility.update(kTick);
				tickMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
				mostPending = std::max(mostPending, visibility.seenArea().shapesPending());
				if (tickMs.back() > slowest.ms) {
					slowest = {tickMs.back(), tick, visibility.seenArea().ringsRegistered() - ringsBefore, visibility.footprintsEntered() - footprintsBefore};
				}
			}

			const TickTimes times = timesOf(tickMs);
			std::fprintf(stderr,
						 "[PlayerVisibility base] %d colonists at %.0fx: tick ms mean %.4f p99 %.4f max %.3f (tick %d: %zu rings registered, %zu footprints "
						 "queued); rings %zu, dropped %zu, skipped %zu, footprints %zu, pending at most %zu and %zu at the end, seen %.0f m2\n",
						 colonists, static_cast<double>(speed), times.mean, times.p99, times.most, slowest.tick, slowest.rings, slowest.footprints,
						 visibility.seenArea().ringsRegistered(), visibility.seenArea().shapesDropped(), visibility.seenArea().shapesSkipped(),
						 visibility.footprintsEntered(), mostPending, visibility.seenArea().shapesPending(), visibility.seenArea().committedSquareMeters());
			state.counters["meanMs"]  = times.mean;
			state.counters["p99Ms"]	  = times.p99;
			state.counters["maxMs"]	  = times.most;
			state.counters["rings"]	  = static_cast<double>(visibility.seenArea().ringsRegistered());
			state.counters["pending"] = static_cast<double>(mostPending);
			benchmark::DoNotOptimize(iteration);
		}
		assets::ConstructionRegistry::Get().clear();
	}

} // namespace

BENCHMARK(BM_PlayerVisibilityBase)->Args({10, 1})->Args({50, 1})->Args({10, 10})->Args({50, 10})->Iterations(1)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_PlayerVisibilityWalk)
	->Args({1, 1})
	->Args({10, 1})
	->Args({50, 1})
	->Args({1, 10})
	->Args({10, 10})
	->Args({50, 10})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
