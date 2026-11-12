// NavigationSystem through its public API on a synthetic forest (nav-mesh-variable-resolution.md
// section 6): 3x3 loaded chunks (1.5 km square), a meandering 8 m river across the middle, and a
// colony 12 m from a chunk side where the river runs about 30 m north. The forest is the game's
// since the forest-floor change (PR 365): a tree every ~3.5 m (about 80 per 1,000 m2, 1,300 per
// 128 m square), each sapling, mature or veteran by the oak's weights (0.15, 0.6, 0.25) with that
// class's trunk rect (half extents 0.125, 0.225, 0.36 m). WORLDSIM_BENCH_FOREST=sparse runs the
// forest every number before that change was measured on: a tree every ~5.2 m (about 600 per 128 m
// square), all with a 0.3 m half extent. Every scenario has its own NavigationSystem and its own
// placement, so scenarios are independent of each other and of --benchmark_filter.
//
// Ticks go through World::update, which runs update() and then endTick(), where the tick's patches
// start on the workers; the next update() waits for them. The last benchmark argument is the gap
// between ticks in ms, standing in for the rest of a frame: 0 is back to back (the longest waits,
// what a headless run gives), 2 leaves the workers 2 ms before the next update.
//
// Per scenario: the main thread's time in World::update per tick without the wait for the workers
// (main), that wait (wait), each patch job's duration, the ticks from the change until isBuilding()
// is false, and the work the [NavBuild] debug lines report: the jobs' time, of it the width
// refreshes, the slowest chunk's ground patch, and the mesh's triangle slots.
//
// BM_RealChunksGround loads the Quick Start planet (planets/quickstart.wsplanet, read only) and
// meshes the 25 real chunks around its landing spawn, ground only (WORLDSIM_PLANET names another
// copy of the file, e.g. the main checkout's from a worktree); it is skipped when the file is
// absent.

#include "NavigationSystem.h"

#include "AIDecisionSystem.h"
#include "MovementSystem.h"
#include "PhysicsSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/SourceReachMemo.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/RecipeRegistry.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>
#include <nav/NavCoords.h>
#include <nav/NavInputBuilder.h>
#include <nav/PatchableMesh.h>

#include <utils/Log.h>

#include <world/BiomeWeights.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/GeneratedWorldSampler.h>
#include <world/chunk/IWorldSampler.h>
#include <world/chunk/TerrainPolygonQuery.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>
#include <worldgen/sampling/SpawnSite.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <malloc.h>
#include <windows.h>
#include <psapi.h>
#endif

namespace {

	using Clock = std::chrono::steady_clock;

	constexpr float	 kTickSeconds  = 1.0F / 60.0F;
	// The forest's layout, by WORLDSIM_BENCH_FOREST (see the file comment).
	struct ForestLayout {
		double spacingM;
		bool   ageClasses; // the oak's three classes, else one 0.3 m rect
	};
	ForestLayout forestLayout() {
		const char* which = std::getenv("WORLDSIM_BENCH_FOREST");
		if (which != nullptr && std::strcmp(which, "sparse") == 0) {
			return {.spacingM = 5.22, .ageClasses = false}; // 128 m square / 600 trees
		}
		return {.spacingM = 3.54, .ageClasses = true}; // 1,000 m2 / 80 trees
	}
	constexpr float	 kWorldMinM	   = 0.0F;
	constexpr float	 kWorldMaxM	   = 1536.0F; // chunks 0..2 on each axis
	constexpr float	 kAgentRadius  = 0.3F;
	const glm::vec2	 kColony{500.0F, 555.0F};		// 12 m from the x = 512 chunk side; the river is ~30 m north
	const glm::vec2	 kChunkCentreColony{256.0F, 256.0F}; // the middle of chunk (0, 0), far from every seam
	const char*		kTreeDef = "Bench_NavTree";

	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path().parent_path();
	}

	float riverY(double x) { return static_cast<float>(600.0 + 22.0 * std::sin(x / 90.0)); }

	// Grassland cut by one meandering river along y ~ 600 m, 8 m wide.
	class ForestRiverSampler : public world::IWorldSampler {
	  public:
		ForestRiverSampler() {
			double s = 0.0;
			for (double x = -600.0; x < 2200.0; x += 20.0) {
				const double y0	 = riverY(x);
				const double y1	 = riverY(x + 20.0);
				const double len = std::hypot(20.0, y1 - y0);
				segments.push_back({.x0 = x, .y0 = y0, .x1 = x + 20.0, .y1 = y1, .halfWidth0 = 4.0F, .halfWidth1 = 4.0F, .s0 = s, .s1 = s + len});
				s += len;
			}
		}
		[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
			world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
			r.riverSegments			   = segments;
			return r;
		}
		[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 11U; }

	  private:
		std::vector<worldgen::RiverNetwork2D::Segment> segments;
	};

	std::uint32_t hashU32(std::uint32_t x) {
		x ^= x >> 16U;
		x *= 0x7feb352dU;
		x ^= x >> 15U;
		x *= 0x846ca68bU;
		x ^= x >> 16U;
		return x;
	}

	double atPercentile(std::vector<double> values, double p) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(p * static_cast<double>(values.size())))];
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double v : values) {
			sum += v;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	// What the [NavBuild] debug lines report, from any thread.
	struct BuildLog {
		std::mutex			mutex;
		std::vector<double> jobMs; // per job
		std::vector<double> groundMs;
		double				widthsMs = 0.0;
		double				waitMs	 = 0.0; // since the last takeWait
		long long			maxSlots = 0;

		void reset() {
			const std::lock_guard lock(mutex);
			jobMs.clear();
			groundMs.clear();
			widthsMs = waitMs = 0.0;
			maxSlots		  = 0;
		}

		double takeWait() {
			const std::lock_guard lock(mutex);
			return std::exchange(waitMs, 0.0);
		}

		static double numberAfter(const char* text, const char* tag) {
			const char* at = std::strstr(text, tag);
			return at != nullptr ? std::atof(at + std::strlen(tag)) : 0.0;
		}

		static void sink(void* context, const foundation::LogRecord& record) {
			auto*		self = static_cast<BuildLog*>(context);
			const char* m	 = record.message;
			if (std::strstr(m, "[NavBuild]") == nullptr) {
				return;
			}
			const std::lock_guard lock(self->mutex);
			// NAV_BENCH_TRACE=1 prints every line, to see where a scenario's time goes.
			static const bool kTrace = std::getenv("NAV_BENCH_TRACE") != nullptr;
			if (kTrace) {
				std::fprintf(stderr, "%s\n", m);
			}
			if (std::strstr(m, "[NavBuild] job ") != nullptr) {
				self->jobMs.push_back(numberAfter(m, "job "));
				self->widthsMs += numberAfter(m, "widths ");
				self->waitMs += numberAfter(m, "wait ");
				self->maxSlots = std::max(self->maxSlots, static_cast<long long>(numberAfter(m, "slots ")));
			} else if (std::strstr(m, "[NavBuild] ground ") != nullptr) {
				const char* const close = std::strchr(m, ')');
				self->groundMs.push_back(close != nullptr ? std::atof(close + 1) : 0.0);
			}
		}
	};

	BuildLog& buildLog() {
		static BuildLog log;
		return log;
	}

	struct Tree {
		world::ChunkCoordinate coord;
		glm::vec2			   position;
		std::uint8_t		   ageClass = 0;
	};

	// The world every scenario shares: chunks, where the trees stand, and the registries.
	struct Forest {
		std::unique_ptr<world::ChunkManager> chunks;
		std::vector<Tree>					 trees;

		Forest() {
			foundation::Logger::initialize();
			assets::ConstructionRegistry::Get().clear();
			assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string());

			const ForestLayout		layout = forestLayout();
			assets::AssetDefinition tree;
			tree.defName = kTreeDef;
			if (layout.ageClasses) {
				tree.ageClasses = {{.name = "sapling", .weight = 0.15F}, {.name = "mature", .weight = 0.6F}, {.name = "veteran", .weight = 0.25F}};
				const float halfExtents[] = {0.125F, 0.225F, 0.36F};
				for (std::uint8_t a = 0; a < 3; ++a) {
					tree.setCollision(a, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {halfExtents[a], halfExtents[a]}});
				}
			} else {
				tree.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
			}
			assets::AssetRegistry::Get().registerTestDefinition(tree);

			chunks = std::make_unique<world::ChunkManager>(std::make_unique<ForestRiverSampler>());
			chunks->setLoadRadius(1);
			chunks->setUnloadRadius(3);
			chunks->update({768.0F, 768.0F});
			chunks->finishPendingGeneration();

			const int cells = static_cast<int>((kWorldMaxM - kWorldMinM) / layout.spacingM);
			for (int j = 0; j < cells; ++j) {
				for (int i = 0; i < cells; ++i) {
					const std::uint32_t h  = hashU32(static_cast<std::uint32_t>(j * 4096 + i));
					const float			jx = (static_cast<float>(h & 0xFFFFU) / 65535.0F - 0.5F) * 3.0F;
					const float			jy = (static_cast<float>(h >> 16U) / 65535.0F - 0.5F) * 3.0F;
					const glm::vec2		p{kWorldMinM + (static_cast<float>(i) + 0.5F) * static_cast<float>(layout.spacingM) + jx,
										  kWorldMinM + (static_cast<float>(j) + 0.5F) * static_cast<float>(layout.spacingM) + jy};
					if (std::abs(p.y - riverY(p.x)) >= 6.0F) {
						const float	  pick	   = static_cast<float>(hashU32(h) & 0xFFFFU) / 65535.0F;
						const uint8_t ageClass = !layout.ageClasses ? 0 : pick < 0.15F ? 0 : pick < 0.75F ? 1 : 2;
						trees.push_back({world::worldToChunk({p.x, p.y}), p, ageClass});
					}
				}
			}
		}

		// A placement holding every tree, each chunk marked processed.
		std::unique_ptr<assets::PlacementExecutor> place(std::unordered_set<world::ChunkCoordinate>& processed) const {
			auto placement = std::make_unique<assets::PlacementExecutor>(assets::AssetRegistry::Get());
			std::unordered_map<world::ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
			for (const Tree& t : trees) {
				assets::AsyncChunkPlacementResult& r = byChunk[t.coord];
				r.coord								 = t.coord;
				assets::PlacedEntity e;
				e.defName			= kTreeDef;
				e.position			= t.position;
				e.traits.ageClass	= t.ageClass;
				r.spatialIndex.insert(e);
			}
			for (auto& [coord, result] : byChunk) {
				processed.insert(coord);
				placement->storeChunkResult(std::move(result));
			}
			return placement;
		}

		const Tree& nearest(glm::vec2 p) const {
			return *std::min_element(trees.begin(), trees.end(), [p](const Tree& a, const Tree& b) {
				const glm::vec2 da = a.position - p;
				const glm::vec2 db = b.position - p;
				return da.x * da.x + da.y * da.y < db.x * db.x + db.y * db.y;
			});
		}
	};

	const Forest& forest() {
		static const Forest f;
		return f;
	}

	struct TickStats {
		std::vector<double> mainUs;
		std::vector<double> waitUs;
		int					settledAt = -1;

		void report(benchmark::State& state) const {
			state.counters["ticks"]		 = static_cast<double>(mainUs.size());
			state.counters["mainMeanUs"] = meanOf(mainUs);
			state.counters["mainP99Us"]	 = atPercentile(mainUs, 0.99);
			state.counters["mainMaxUs"]	 = atPercentile(mainUs, 1.0);
			state.counters["waitMeanUs"] = meanOf(waitUs);
			state.counters["waitP99Us"]	 = atPercentile(waitUs, 0.99);
			state.counters["waitMaxUs"]	 = atPercentile(waitUs, 1.0);
			state.counters["settleAt"]	 = static_cast<double>(settledAt);
			BuildLog&			  log	 = buildLog();
			const std::lock_guard lock(log.mutex);
			state.counters["jobs"]			  = static_cast<double>(log.jobMs.size());
			state.counters["jobMeanMs"]		  = meanOf(log.jobMs);
			state.counters["jobP99Ms"]		  = atPercentile(log.jobMs, 0.99);
			state.counters["jobMaxMs"]		  = atPercentile(log.jobMs, 1.0);
			state.counters["meshMs"]		  = meanOf(log.jobMs) * static_cast<double>(log.jobMs.size());
			state.counters["widthsMs"]		  = log.widthsMs;
			state.counters["groundMaxMs"]	  = atPercentile(log.groundMs, 1.0);
			state.counters["slots"]			  = static_cast<double>(log.maxSlots);
		}
	};

	// One NavigationSystem over the shared chunks, with a placement of its own.
	struct Rig {
		std::unordered_set<world::ChunkCoordinate> processed;
		std::unique_ptr<assets::PlacementExecutor> placement;
		construction::ConstructionWorld			   construction;
		ecs::World								   world;
		ecs::NavigationSystem&					   nav;
		std::vector<ecs::EntityID>				   movers;
		std::chrono::microseconds				   gap;

		explicit Rig(std::int64_t gapMs, world::ChunkManager* chunks = forest().chunks.get())
			: placement(forest().place(processed)),
			  nav(world.registerSystem<ecs::NavigationSystem>()),
			  gap(std::chrono::microseconds(gapMs * 1000)) {
			nav.setChunkManager(chunks);
			nav.setPlacementData(placement.get(), &processed);
			nav.setConstructionWorld(&construction);
		}

		// A mover meshes the disc of its sight distance plus the buffer around it.
		void addMover(glm::vec2 at, float sightMeters = ecs::kDefaultSightRadius) {
			const ecs::EntityID e = world.createEntity();
			world.addComponent<ecs::Position>(e, ecs::Position{at});
			world.addComponent<ecs::Colonist>(e, ecs::Colonist{"Bench"});
			world.addComponent<ecs::Task>(e, ecs::Task{});
			world.addComponent<ecs::AgentRadius>(e, ecs::AgentRadius{kAgentRadius, 1.0F});
			ecs::Memory memory;
			memory.sightRadius = sightMeters;
			world.addComponent<ecs::Memory>(e, std::move(memory));
			movers.push_back(e);
		}

		void moveMover(std::size_t i, glm::vec2 by) { world.getComponent<ecs::Position>(movers[i])->value += by; }

		// One World::update (update, then endTick), timed without the wait, then the gap.
		void tick(TickStats& stats) {
			// isBuilding() is a query, so it waits for the previous tick's job. It runs here, after the
			// gap, where the update would have waited anyway, and its wait is this tick's.
			buildLog().takeWait();
			if (!stats.mainUs.empty() && stats.settledAt < 0 && !nav.isBuilding()) {
				stats.settledAt = static_cast<int>(stats.mainUs.size());
			}
			const double			queryWaitUs = 1000.0 * buildLog().takeWait();
			const Clock::time_point start		= Clock::now();
			world.update(kTickSeconds);
			const double totalUs	   = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
			const double updateWaitUs = 1000.0 * buildLog().takeWait();
			stats.mainUs.push_back(totalUs - updateWaitUs);
			stats.waitUs.push_back(queryWaitUs + updateWaitUs);
			const Clock::time_point until = Clock::now() + gap;
			while (Clock::now() < until) {
				std::this_thread::yield();
			}
		}

		void ticks(TickStats& stats, int count) {
			for (int i = 0; i < count; ++i) {
				tick(stats);
			}
		}

		void ticksUntilSettled(TickStats& stats, int limit) {
			for (int i = 0; i < limit && stats.settledAt < 0; ++i) {
				tick(stats);
			}
		}

		void settle() { nav.finishPendingBuilds(); }

		// The route by ticket, ticking the world (no gap) until it is answered; `ticks` counts them.
		std::optional<ecs::NavigationSystem::RouteAnswer> route(glm::vec2 a, glm::vec2 b, const geometry::nav::BeliefFilter& belief, int& ticks) {
			const nav::RouteTicket ticket = nav.requestRoute(a, std::array{b}, kAgentRadius, belief);
			for (ticks = 0; ticks < 6000; ++ticks) {
				if (std::optional<ecs::NavigationSystem::RouteAnswer> answer = nav.takeRouteAnswer(ticket)) {
					return answer;
				}
				world.update(kTickSeconds);
			}
			nav.cancelRoute(ticket);
			return std::nullopt;
		}

		void wall(glm::vec2 a, glm::vec2 b, bool door) {
			const auto commit = construction.commitSegment({std::llround(a.x * 1000.0F), std::llround(a.y * 1000.0F)},
														   {std::llround(b.x * 1000.0F), std::llround(b.y * 1000.0F)}, "Wood", "Standard",
														   construction::kInvalidFoundation);
			for (const auto id : commit.createdSegments) {
				construction.setSegmentState(id, construction::FoundationState::Built);
			}
			if (door) {
				const auto op = construction.addOpening(commit.createdSegments.front(), 0.5F, "Door", "Wood");
				construction.setOpeningState(op, construction::FoundationState::Built);
			}
		}
	};

	// A sight distance whose disc, from anywhere in a chunk, meets every chunk within `chunkRadius` of it.
	float sightCovering(std::int32_t chunkRadius) {
		return static_cast<float>(chunkRadius + 1) * world::kChunkSize * 1.4143F - ecs::NavigationSystem::kSightBufferMeters;
	}

	void colony(Rig& rig, glm::vec2 at = kColony) {
		const glm::vec2 offsets[] = {{0.0F, 0.0F}, {6.0F, 3.0F}, {-5.0F, 4.0F}, {3.0F, -6.0F}, {-8.0F, -3.0F}};
		for (const glm::vec2 o : offsets) {
			rig.addMover(at + o);
		}
	}

	void begin() {
		buildLog().reset();
		foundation::Logger::setSink(&BuildLog::sink, &buildLog());
	}

	void end() { foundation::Logger::setSink(nullptr, nullptr); }

	// The colony's discs, ground and placed obstacles, from nothing.
	void BM_ColdSettleNineChunks(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(state.range(0));
			colony(rig);
			begin();
			TickStats stats;
			rig.ticksUntilSettled(stats, 6000);
			end();
			stats.report(state);
		}
	}

	void BM_Steady(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(state.range(0));
			colony(rig);
			rig.settle();
			begin();
			TickStats stats;
			rig.ticks(stats, 600);
			end();
			stats.report(state);
		}
	}

	void BM_FellTree(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(state.range(0));
			colony(rig);
			rig.settle();
			begin();
			const Tree& tree = forest().nearest(kColony + glm::vec2{5.0F, 5.0F});
			rig.placement->removeEntity(tree.coord, tree.position, kTreeDef);
			TickStats stats;
			rig.ticks(stats, 120);
			end();
			stats.report(state);
		}
	}

	void BM_BuildWall(benchmark::State& state) {
		const bool door = state.range(0) != 0;
		for (auto _ : state) {
			Rig rig(state.range(1));
			colony(rig);
			rig.settle();
			begin();
			rig.wall(kColony + glm::vec2{12.0F, 8.0F}, kColony + glm::vec2{20.0F, 8.0F}, door);
			TickStats stats;
			rig.ticks(stats, 120);
			end();
			stats.report(state);
		}
	}

	// range(0) movers on a grid 100 m apart over the forest, their discs settled, then every one walking
	// east at 2 m/s for 1,800 ticks (60 m), its disc's leading edge in dense ground no disc has met
	// for most of it. Reports the main thread's tick (mover tracking, ground and detail gathering),
	// the wait, the patch jobs, and the nav patches the walk took. range(1): gap.
	void BM_WalkingMovers(benchmark::State& state) {
		const auto count = static_cast<int>(state.range(0));
		for (auto _ : state) {
			Rig		  rig(state.range(1));
			const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
			for (int i = 0; i < count; ++i) {
				rig.addMover({60.0F + static_cast<float>(i % side) * 100.0F, 40.0F + static_cast<float>(i / side) * 100.0F});
			}
			rig.settle();
			const std::uint64_t patchesBefore = rig.nav.navQueryStats().navPatches;
			begin();
			TickStats stats;
			for (int t = 0; t < 1800; ++t) {
				for (std::size_t m = 0; m < rig.movers.size(); ++m) {
					rig.moveMover(m, {2.0F * kTickSeconds, 0.0F});
				}
				rig.tick(stats);
			}
			end();
			stats.report(state);
			state.counters["movers"]	  = static_cast<double>(count);
			state.counters["navPatches"] = static_cast<double>(rig.nav.navQueryStats().navPatches - patchesBefore);
		}
	}

	// Every mover walks 100 m east at 4 m/s.
	void BM_ColonistsWalk(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(state.range(0));
			colony(rig);
			rig.settle();
			begin();
			TickStats stats;
			for (int i = 0; i < 1500; ++i) {
				for (std::size_t m = 0; m < rig.movers.size(); ++m) {
					rig.moveMover(m, {4.0F * kTickSeconds, 0.0F});
				}
				rig.tick(stats);
			}
			end();
			stats.report(state);
		}
	}

	void crowd(Rig& rig) {
		const glm::vec2 centres[] = {{380.0F, 380.0F}, {650.0F, 380.0F}, {380.0F, 700.0F}, {650.0F, 700.0F}};
		for (const glm::vec2 c : centres) {
			for (int k = 0; k < 5; ++k) {
				rig.addMover(c + glm::vec2{static_cast<float>(k) * 4.0F - 8.0F, static_cast<float>(k % 2) * 5.0F});
			}
		}
	}

	// range(0): 0 settles from cold and then holds 600 ticks; 1 walks every mover 50 m east.
	void BM_Crowd20(benchmark::State& state) {
		const bool walk = state.range(0) != 0;
		for (auto _ : state) {
			Rig rig(state.range(1));
			crowd(rig);
			if (walk) {
				rig.settle();
			}
			begin();
			TickStats stats;
			const int count = walk ? 750 : 600;
			for (int i = 0; i < count; ++i) {
				if (walk) {
					for (std::size_t m = 0; m < rig.movers.size(); ++m) {
						rig.moveMover(m, {4.0F * kTickSeconds, 0.0F});
					}
				}
				rig.tick(stats);
			}
			end();
			stats.report(state);
		}
	}

	// 20 colonists, the real AI and movement, each walking a move order 80 m east with placed obstacles
	// streaming in ahead of them. Counts the path requests each tick: the replans. range(0): gap.
	void BM_ReplanWalk(benchmark::State& state) {
		for (auto _ : state) {
			Rig					   rig(state.range(0));
			ecs::AIDecisionSystem& ai = rig.world.registerSystem<ecs::AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234U);
			ai.setNavigationSystem(&rig.nav);
			rig.world.registerSystem<ecs::MovementSystem>();
			rig.world.registerSystem<ecs::PhysicsSystem>();
			rig.settle();
			for (int i = 0; i < 20; ++i) {
				const glm::vec2		at = kColony + glm::vec2{static_cast<float>(i % 5) * 4.0F - 8.0F, static_cast<float>(i / 5) * 4.0F - 6.0F};
				const ecs::EntityID e  = rig.world.createEntity();
				rig.world.addComponent<ecs::Position>(e, ecs::Position{at});
				rig.world.addComponent<ecs::Velocity>(e, ecs::Velocity{{0.0F, 0.0F}});
				rig.world.addComponent<ecs::MovementTarget>(e, ecs::MovementTarget{{0.0F, 0.0F}, 2.0F, false});
				ecs::NeedsComponent needs = ecs::NeedsComponent::createDefault();
				for (const ecs::NeedType need : {ecs::NeedType::Hunger, ecs::NeedType::Thirst, ecs::NeedType::Energy, ecs::NeedType::Bladder,
												 ecs::NeedType::Digestion}) {
					needs.get(need).value = 100.0F;
				}
				rig.world.addComponent<ecs::NeedsComponent>(e, needs);
				rig.world.addComponent<ecs::Memory>(e, ecs::Memory{});
				rig.world.addComponent<ecs::Inventory>(e, ecs::Inventory::createForColonist());
				rig.world.addComponent<ecs::Task>(e, ecs::Task{});
				rig.world.addComponent<ecs::DecisionTrace>(e, ecs::DecisionTrace{});
				rig.world.addComponent<ecs::SourceReachMemo>(e, ecs::SourceReachMemo{});
				rig.world.addComponent<ecs::Colonist>(e, ecs::Colonist{"Walker"});
				rig.world.addComponent<ecs::AgentRadius>(e, ecs::AgentRadius{kAgentRadius, 1.0F});
				ai.enterControl(e);
				static_cast<void>(ai.issuePlayerMoveOrder(e, at + glm::vec2{80.0F, 0.0F}));
				rig.movers.push_back(e);
			}
			rig.settle();
			begin();
			TickStats			stats;
			std::vector<double> perTick;
			const std::uint64_t first = rig.nav.routeStats().requests;
			std::uint64_t		last  = first;
			for (int i = 0; i < 600; ++i) {
				rig.tick(stats);
				const std::uint64_t requests = rig.nav.routeStats().requests;
				perTick.push_back(static_cast<double>(requests - last));
				last = requests;
			}
			last -= first;
			end();
			stats.report(state);
			state.counters["replans"]		 = static_cast<double>(last);
			state.counters["replansPerTick"] = meanOf(perTick);
			state.counters["replansMaxTick"] = atPercentile(perTick, 1.0);
		}
	}

	// M1, the route slice: range(0) requests kept pending round the colony (each answered one is
	// replaced by a new one between random points within 35 m of it), 600 ticks back to back. Reports
	// each slice's time on the route worker and its expansions, the main thread's time making the
	// requests (the belief copy and the label check), and the tick as the other scenarios do.
	void BM_RouteSlice(benchmark::State& state) {
		const auto pending = static_cast<std::size_t>(state.range(0));
		for (auto _ : state) {
			Rig rig(0);
			colony(rig);
			rig.settle();
			std::uint32_t seed = 9;
			const auto	  roundColony = [&seed] {
				   seed = hashU32(seed + 0x9e3779b9U);
				   return kColony + glm::vec2{(static_cast<float>(seed & 0xFFFFU) / 65535.0F - 0.5F) * 70.0F,
											  (static_cast<float>(seed >> 16U) / 65535.0F - 0.5F) * 70.0F};
			};
			std::vector<nav::RouteTicket> open;
			std::vector<double>			  requestUs;
			std::vector<double>			  sliceMs;
			std::vector<double>			  sliceExpansions;
			begin();
			TickStats stats;
			for (int t = 0; t < 600; ++t) {
				std::erase_if(open, [&](nav::RouteTicket ticket) { return rig.nav.takeRouteAnswer(ticket).has_value(); });
				const Clock::time_point start = Clock::now();
				while (open.size() < pending) {
					const glm::vec2 a = roundColony();
					open.push_back(rig.nav.requestRoute(a, std::array{roundColony()}, kAgentRadius));
				}
				requestUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
				const std::uint64_t slicesBefore = rig.nav.routeStats().slices;
				rig.tick(stats);
				if (rig.nav.routeStats().slices != slicesBefore) {
					sliceMs.push_back(rig.nav.routeStats().lastSliceMs);
					sliceExpansions.push_back(static_cast<double>(rig.nav.routeStats().lastSliceExpansions));
				}
			}
			end();
			stats.report(state);
			const ecs::NavigationSystem::RouteStats& routes = rig.nav.routeStats();
			state.counters["pending"]		  = static_cast<double>(pending);
			state.counters["slices"]		  = static_cast<double>(routes.slices);
			state.counters["sliceMeanMs"]	  = meanOf(sliceMs);
			state.counters["sliceP99Ms"]	  = atPercentile(sliceMs, 0.99);
			state.counters["sliceMaxMs"]	  = routes.maxSliceMs;
			state.counters["sliceExpMean"]	  = meanOf(sliceExpansions);
			state.counters["sliceExpMax"]	  = static_cast<double>(routes.maxSliceExpansions);
			state.counters["answered"]		  = static_cast<double>(routes.answered);
			state.counters["answeredAtOnce"] = static_cast<double>(routes.answeredAtOnce);
			state.counters["restarts"]		  = static_cast<double>(routes.restarts);
			state.counters["ticksHeld"]		  = static_cast<double>(routes.ticksHeld);
			state.counters["requestUsMean"]  = meanOf(requestUs);
			state.counters["requestUsMax"]	  = atPercentile(requestUs, 1.0);
			for (const nav::RouteTicket ticket : open) {
				rig.nav.cancelRoute(ticket);
			}
		}
	}

	// The build tool's per-frame cursor checks in the forest: isAreaWalkable over a 4 m square
	// footprint and isPolylineWalkable over a 20 m wall line, each sampled every 0.5 m, at 200 spots
	// round the colony. Reports one frame's pair of checks.
	void BM_CursorCheck(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(0);
			colony(rig);
			rig.settle();
			std::uint32_t seed = 31;
			auto		  unit = [&seed] {
				 seed = hashU32(seed + 0x9e3779b9U);
				 return static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
			};
			constexpr int kFrames = 200;
			double		  totalUs = 0.0;
			double		  maxUs	  = 0.0;
			int			  clear	  = 0;
			for (int i = 0; i < kFrames; ++i) {
				const glm::vec2				 at = kColony + glm::vec2{(unit() - 0.5F) * 60.0F, (unit() - 0.5F) * 30.0F};
				const std::vector<glm::vec2> footprint{at, at + glm::vec2{4.0F, 0.0F}, at + glm::vec2{4.0F, 4.0F}, at + glm::vec2{0.0F, 4.0F}};
				const std::vector<glm::vec2> wall{at, at + glm::vec2{20.0F, 0.0F}};
				const Clock::time_point		 start = Clock::now();
				clear += rig.nav.isAreaWalkable(footprint) ? 1 : 0;
				clear += rig.nav.isPolylineWalkable(wall) ? 1 : 0;
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
				totalUs += us;
				maxUs = std::max(maxUs, us);
			}
			state.counters["frameMeanUs"] = totalUs / kFrames;
			state.counters["frameMaxUs"]  = maxUs;
			state.counters["clear"]		  = clear;
		}
	}

	// WOR-644: two colonists 160 m apart on connected ground.
	void BM_TwoGroupsApart(benchmark::State& state) {
		for (auto _ : state) {
			Rig				rig(state.range(0));
			const glm::vec2 a{300.0F, 470.0F};
			const glm::vec2 b{460.0F, 470.0F};
			rig.addMover(a);
			rig.addMover(b);
			rig.settle();
			int		   pathTicks		 = 0;
			const auto answer			 = rig.route(a + glm::vec2{1.0F, 0.0F}, b + glm::vec2{1.0F, 0.0F}, {}, pathTicks);
			state.counters["pathTicks"] = pathTicks;
			state.counters["routes"]	 = answer.has_value() && answer->outcome == nav::RouteOutcome::Routed ? 1.0 : 0.0;
		}
	}

	// A wall built once every loaded chunk's ground is meshed (one mover at the middle whose disc
	// meets them all): its patch job. range(0) is the
	// chunk load radius around the forest's middle: 2 meshes 25 chunks, 5 meshes 121.
	void BM_WallEditMeshedChunks(benchmark::State& state) {
		const auto radius = static_cast<std::int32_t>(state.range(0));
		world::ChunkManager chunks(std::make_unique<ForestRiverSampler>());
		chunks.setLoadRadius(radius);
		chunks.setUnloadRadius(radius + 2);
		chunks.update({768.0F, 768.0F});
		chunks.finishPendingGeneration();
		for (auto _ : state) {
			Rig rig(state.range(1), &chunks);
			rig.nav.setPlacementData(nullptr, nullptr);
			rig.addMover({768.0F, 768.0F}, sightCovering(radius));
			rig.settle();
			begin();
			rig.wall(kColony + glm::vec2{12.0F, 8.0F}, kColony + glm::vec2{20.0F, 8.0F}, false);
			TickStats stats;
			rig.ticks(stats, 10);
			end();
			stats.report(state);
		}
		state.counters["chunks"] = static_cast<double>(chunks.loadedChunkCount());
	}

	struct Landing {
		std::shared_ptr<const worldgen::GeneratedWorld> planet;
		worldgen::LatLon								site;
		glm::vec2										at{0.0F, 0.0F};
	};

	// The Quick Start planet and its landing spawn; null without the file. A worktree has no planets/ of
	// its own unless it is junctioned; WORLDSIM_PLANET names the file.
	const Landing* quickstartLanding() {
		static const std::optional<Landing> landing = []() -> std::optional<Landing> {
			const char*					planetEnv  = std::getenv("WORLDSIM_PLANET");
			const std::filesystem::path planetPath = planetEnv != nullptr ? std::filesystem::path(planetEnv) : projectRoot() / "planets" / "quickstart.wsplanet";
			if (!std::filesystem::exists(planetPath)) {
				return std::nullopt;
			}
			Landing out;
			out.planet = worldgen::loadPlanet(planetPath);
			if (!out.planet) {
				return std::nullopt;
			}
			out.site						= worldgen::findDefaultLandingSite(*out.planet, worldgen::Biome::TemperateDeciduousForest);
			const worldgen::SpawnSite spawn = worldgen::findRiverbankSpawn(out.planet, out.site.latDeg, out.site.lonDeg);
			out.at							= {static_cast<float>(spawn.xMeters), static_cast<float>(spawn.yMeters)};
			return out;
		}();
		return landing ? &*landing : nullptr;
	}

	// The 25 real chunks around the Quick Start landing spawn, ground only, ticked until settled.
	void BM_RealChunksGround(benchmark::State& state) {
		const Landing* landing = quickstartLanding();
		if (landing == nullptr) {
			state.SkipWithMessage("no planet (WORLDSIM_PLANET)");
			return;
		}
		const glm::vec2		at = landing->at;
		world::ChunkManager chunks(std::make_unique<world::GeneratedWorldSampler>(landing->planet, landing->site.latDeg, landing->site.lonDeg));
		chunks.setLoadRadius(2);
		chunks.setUnloadRadius(4);
		chunks.update({at.x, at.y});
		chunks.finishPendingGeneration();
		std::size_t waterRings = 0;
		std::size_t waterVerts = 0;
		for (const world::Chunk* chunk : chunks.getLoadedChunks()) {
			for (const world::TerrainRing& ring : chunk->terrainPolygons().navRings) {
				waterRings += ring.blocksMovement ? 1 : 0;
				waterVerts += ring.blocksMovement ? ring.ring.size() : 0;
			}
		}
		for (auto _ : state) {
			Rig rig(state.range(0), &chunks);
			rig.nav.setPlacementData(nullptr, nullptr);
			rig.addMover(at, sightCovering(2));
			begin();
			TickStats stats;
			rig.ticksUntilSettled(stats, 600);
			end();
			stats.report(state);
		}
		state.counters["chunks"]	 = static_cast<double>(chunks.loadedChunkCount());
		state.counters["waterRings"] = static_cast<double>(waterRings);
		state.counters["waterVerts"] = static_cast<double>(waterVerts);
	}

	// --- What a placed obstacle costs in memory, and how many there are -------------------------
	//
	// BM_PlacedObstacleBytes: NavScale.bench.cpp's dense chunk (a placed obstacle every 3.54 m,
	// half extents by age class) patched into a nav mesh 16 m cell by cell, its bytes split by the
	// mesh's containers. BM_PlacedObstacleSystemBytes: the same density through NavigationSystem,
	// whose own records per obstacle are what its heap grows by beyond its mesh's.
	// BM_PlacedObstacleDensity: the Quick Start chunks round the landing placed as the game places
	// them, the placed obstacles counted per chunk, and the 3 x 3's patched onto its real ground.
	//
	// Container bytes are capacities times element sizes, the allocator's own headers excluded (the
	// allocation count is reported beside them); "heap" is the CRT heap's allocated bytes, "private"
	// the process's committed private bytes. The containers are private members, read through
	// explicit template instantiation, which the language exempts from access checking; nothing here
	// writes them.

	std::int64_t heapAllocated() {
#ifdef _WIN32
		HEAP_SUMMARY summary{};
		summary.cb = sizeof(summary);
		HeapSummary(reinterpret_cast<HANDLE>(_get_heap_handle()), 0, &summary);
		return static_cast<std::int64_t>(summary.cbAllocated);
#else
		return 0;
#endif
	}

	std::int64_t privateBytes() {
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return static_cast<std::int64_t>(counters.PrivateUsage);
#else
		return 0;
#endif
	}

	// Bytes a container holds (capacity times element size), its allocations, and a count where
	// the visit keeps one.
	struct Tally {
		double bytes  = 0.0;
		double allocs = 0.0;
		double count  = 0.0;

		template <typename T> void paged(const geometry::PagedVector<T>& v) {
			constexpr std::size_t kPage = geometry::PagedVector<T>::kPageSize;
			const std::size_t	  pages = (v.size() + kPage - 1) / kPage;
			bytes += static_cast<double>(pages * kPage * sizeof(T) + pages * 2 * sizeof(void*));
			allocs += static_cast<double>(pages) + (pages > 0 ? 2.0 : 0.0);
		}
		template <typename V> void vec(const V& v) {
			if (v.capacity() > 0) {
				bytes += static_cast<double>(v.capacity() * sizeof(typename V::value_type));
				allocs += 1.0;
			}
		}
		// ScratchArray and IncrementalHashMap's slot tables: size() elements in one allocation.
		template <typename S> void block(const S& s) {
			if (s.size() > 0) {
				bytes += static_cast<double>(s.size() * sizeof(s[0]));
				allocs += 1.0;
			}
		}
		void add(const Tally& o) {
			bytes += o.bytes;
			allocs += o.allocs;
			count += o.count;
		}
	};

	template <typename T> struct IsVector : std::false_type {};
	template <typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type {};

	template <typename S> void account(const S& s, Tally& out) { out.block(s); }
	template <typename T, typename A> void account(const std::vector<T, A>& v, Tally& out) { out.vec(v); }
	template <typename T> void account(const geometry::PagedVector<T>& v, Tally& out) {
		out.paged(v);
		if constexpr (IsVector<T>::value) {
			v.forEach([&](const T& e) { out.vec(e); });
		}
	}
	template <typename T, std::size_t N> void account(const std::array<T, N>& a, Tally& out) {
		for (const T& e : a) {
			account(e, out);
		}
	}

	// The containers are private members. A member pointer named in an explicit instantiation's
	// template arguments is exempt from access checking ([temp.spec.general]), so Rob<&X::m, Tag>
	// hands Tag::visit the member of any X it is given; nothing here writes one.
	template <typename C, typename T> C ownerOf(T C::*);
	template <auto Member, typename Tag> struct Rob {
		using Owner = decltype(ownerOf(Member));
		static void call(const void* owner, Tally& out) { Tag::visit(static_cast<const Owner*>(owner)->*Member, out); }
		static inline const bool registered = (Tag::hook = &call, true);
	};
#define NAV_BENCH_TAG(Tag, ...)                                                   \
	struct Tag {                                                                  \
		static inline void (*hook)(const void*, Tally&) = nullptr;                \
		static void visit([[maybe_unused]] const auto& v, [[maybe_unused]] Tally& out) { __VA_ARGS__; } \
	};
#define NAV_BENCH_LEAF(Tag) NAV_BENCH_TAG(Tag, account(v, out))

	template <typename Tag, typename Owner> Tally visit(const Owner& owner) {
		Tally out;
		Tag::hook(&owner, out);
		return out;
	}

	using PM = geometry::nav::PatchableMesh;

	NAV_BENCH_LEAF(RLive)
	NAV_BENCH_LEAF(REdgeSourceIds)
	NAV_BENCH_LEAF(RCoverIds)
	NAV_BENCH_LEAF(RVisitStamp)
	NAV_BENCH_LEAF(RFreshMark)
	NAV_BENCH_LEAF(RWalkMark)
	NAV_BENCH_LEAF(RVertexUses)
	NAV_BENCH_LEAF(RRings)
	NAV_BENCH_TAG(RRingHeap, v.forEach([&](const geometry::nav::RegisteredRing& r) {
		out.vec(r.polygon.ring);
		out.vec(r.sample.ring3);
		out.vec(r.edgeIds);
		out.count += r.live ? 1.0 : 0.0;
	}))
	NAV_BENCH_LEAF(REdgeOf)
	NAV_BENCH_TAG(RPieces, account(v, out); out.count += static_cast<double>(v.size()))
	NAV_BENCH_LEAF(RCoverTags)
	NAV_BENCH_TAG(RWidthSearch, out.block(v.visited); account(v.vertexSeen, out); account(v.vertexObstacle, out); out.vec(v.star))
	NAV_BENCH_LEAF(RFreeRings)
	NAV_BENCH_LEAF(RFreeVertices)
	NAV_BENCH_LEAF(RFreeTriangles)
	NAV_BENCH_LEAF(RFreeEdgeIds)
	NAV_BENCH_LEAF(RFreshWidthTriangles)
	NAV_BENCH_LEAF(RPendingWidthSeeds)
	NAV_BENCH_LEAF(RStaleApexes)
	NAV_BENCH_LEAF(RWidthSeeds)
	NAV_BENCH_LEAF(RSeedBuckets)
	NAV_BENCH_LEAF(RSeedCellRuns)
	NAV_BENCH_LEAF(RSeedCellTable)
	NAV_BENCH_LEAF(RWidthWalk)
	template struct Rob<&PM::live, RLive>;
	template struct Rob<&PM::edgeSourceIds, REdgeSourceIds>;
	template struct Rob<&PM::coverIds, RCoverIds>;
	template struct Rob<&PM::visitStamp, RVisitStamp>;
	template struct Rob<&PM::freshMark, RFreshMark>;
	template struct Rob<&PM::walkMark, RWalkMark>;
	template struct Rob<&PM::vertexUses, RVertexUses>;
	template struct Rob<&PM::rings, RRings>;
	template struct Rob<&PM::rings, RRingHeap>;
	template struct Rob<&PM::edgeOf, REdgeOf>;
	template struct Rob<&PM::pieces, RPieces>;
	template struct Rob<&PM::coverTags, RCoverTags>;
	template struct Rob<&PM::widthSearch, RWidthSearch>;
	template struct Rob<&PM::freeRings, RFreeRings>;
	template struct Rob<&PM::freeVertices, RFreeVertices>;
	template struct Rob<&PM::freeTriangles, RFreeTriangles>;
	template struct Rob<&PM::freeEdgeIds, RFreeEdgeIds>;
	template struct Rob<&PM::freshWidthTriangles, RFreshWidthTriangles>;
	template struct Rob<&PM::pendingWidthSeeds, RPendingWidthSeeds>;
	template struct Rob<&PM::staleApexes, RStaleApexes>;
	template struct Rob<&PM::widthSeeds, RWidthSeeds>;
	template struct Rob<&PM::seedBuckets, RSeedBuckets>;
	template struct Rob<&PM::seedCellRuns, RSeedCellRuns>;
	template struct Rob<&PM::seedCellTable, RSeedCellTable>;
	template struct Rob<&PM::widthWalk, RWidthWalk>;

	// IncrementalHashMap: its live table and the one being drained.
#define NAV_BENCH_MAP(P, ...)                                                                        \
	NAV_BENCH_LEAF(P##Table)                                                                         \
	NAV_BENCH_LEAF(P##Old)                                                                           \
	template struct Rob<&__VA_ARGS__::table, P##Table>;                                              \
	template struct Rob<&__VA_ARGS__::old, P##Old>;                                                  \
	NAV_BENCH_TAG(P, P##Table::hook(&v, out); P##Old::hook(&v, out))

	NAV_BENCH_MAP(RVertexMap, geometry::IncrementalHashMap<geometry::Vec2i64, std::uint32_t, PM::VertexHash>)
	template struct Rob<&PM::vertexAt, RVertexMap>;

	NAV_BENCH_MAP(RGridMap, geometry::IncrementalHashMap<PM::BoxGrid::CellKey, std::int32_t, PM::BoxGrid::CellHash>)
	template struct Rob<&PM::BoxGrid::table, RGridMap>;
	NAV_BENCH_TAG(RGridCells, out.paged(v); for (std::size_t i = 0; i < v.size(); ++i) {
		out.vec(v[i].entries);
		out.count += static_cast<double>(v[i].entries.size());
	})
	template struct Rob<&PM::BoxGrid::cells, RGridCells>;
	NAV_BENCH_LEAF(RGridFreeCells)
	template struct Rob<&PM::BoxGrid::freeCells, RGridFreeCells>;
	NAV_BENCH_LEAF(RGridLevelCells)
	template struct Rob<&PM::BoxGrid::levelCells, RGridLevelCells>;
	// The grid's bytes; count is its entries.
	NAV_BENCH_TAG(RBoxGrid, RGridMap::hook(&v, out); RGridCells::hook(&v, out); RGridFreeCells::hook(&v, out); RGridLevelCells::hook(&v, out))
	NAV_BENCH_TAG(RGrid, RBoxGrid::visit(v, out))
	NAV_BENCH_TAG(RRingGrid, RBoxGrid::visit(v, out))
	NAV_BENCH_TAG(REdgeGrid, RBoxGrid::visit(v, out))
	template struct Rob<&PM::grid, RGrid>;
	template struct Rob<&PM::ringGrid, RRingGrid>;
	template struct Rob<&PM::edgeGrid, REdgeGrid>;

	// InternTable: values (each a vector), per-id hash, ref count and chain, free ids, and the map
	// from hash to first id; count is the id slots.
#define NAV_BENCH_INTERN(P, ...)                                                                                     \
	NAV_BENCH_TAG(P##Values, account(v, out); out.count += static_cast<double>(v.size()))                            \
	NAV_BENCH_LEAF(P##Hashes)                                                                                        \
	NAV_BENCH_LEAF(P##Refs)                                                                                          \
	NAV_BENCH_LEAF(P##SameHash)                                                                                      \
	NAV_BENCH_LEAF(P##FreeIds)                                                                                       \
	template struct Rob<&__VA_ARGS__::values, P##Values>;                                                            \
	template struct Rob<&__VA_ARGS__::hashes, P##Hashes>;                                                            \
	template struct Rob<&__VA_ARGS__::refs, P##Refs>;                                                                \
	template struct Rob<&__VA_ARGS__::sameHash, P##SameHash>;                                                        \
	template struct Rob<&__VA_ARGS__::freeIds, P##FreeIds>;                                                          \
	NAV_BENCH_MAP(P##ByHash, geometry::IncrementalHashMap<std::uint64_t, std::int32_t, __VA_ARGS__::HashOf>)         \
	template struct Rob<&__VA_ARGS__::byHash, P##ByHash>;                                                            \
	NAV_BENCH_TAG(P, P##Values::hook(&v, out); P##Hashes::hook(&v, out); P##Refs::hook(&v, out); P##SameHash::hook(&v, out); \
				  P##FreeIds::hook(&v, out); P##ByHash::hook(&v, out))

	NAV_BENCH_INTERN(RSourceLists, PM::InternTable<std::vector<geometry::nav::EdgeSource>>)
	template struct Rob<&PM::sourceLists, RSourceLists>;
	NAV_BENCH_INTERN(RCovers, PM::InternTable<std::vector<geometry::nav::RingId>>)
	template struct Rob<&PM::covers, RCovers>;

	// NavigationSystem's per-obstacle records: entries and their element bytes (node overhead not
	// included; reportSystem adds it).
	NAV_BENCH_TAG(RSystemContainer, out.count += static_cast<double>(v.size());
				  out.bytes += static_cast<double>(v.size() * sizeof(typename std::remove_cvref_t<decltype(v)>::value_type)))
	NAV_BENCH_TAG(RDetailInMesh, RSystemContainer::visit(v, out))
	NAV_BENCH_TAG(RWantedCells, RSystemContainer::visit(v, out))
	NAV_BENCH_TAG(RWantedChunks, RSystemContainer::visit(v, out))
	template struct Rob<&ecs::NavigationSystem::detailInMesh, RDetailInMesh>;
	template struct Rob<&ecs::NavigationSystem::wantedCells, RWantedCells>;
	template struct Rob<&ecs::NavigationSystem::wantedChunks, RWantedChunks>;
#undef NAV_BENCH_INTERN
#undef NAV_BENCH_MAP
#undef NAV_BENCH_LEAF
#undef NAV_BENCH_TAG

	// The mesh's bytes by container, and the counts that drive them.
	struct MeshBytes {
		std::vector<std::pair<const char*, Tally>> lines;
		std::map<std::string, double>			   counts;

		[[nodiscard]] Tally total() const {
			Tally t;
			for (const auto& [name, line] : lines) {
				t.add(line);
			}
			return t;
		}
	};

	template <typename... Tags> Tally visitAll(const PM& m) {
		Tally out;
		(Tags::hook(&m, out), ...);
		return out;
	}

	MeshBytes meshBytes(const PM& m) {
		MeshBytes out;
		Tally	  t;
		t.paged(m.triangles());
		out.lines.emplace_back("triangle records (NavTriangle)", t);
		out.lines.emplace_back("triangle side arrays (live, edge source ids, cover id)", visitAll<RLive, REdgeSourceIds, RCoverIds>(m));
		Tally stamps = visitAll<RVisitStamp, RFreshMark, RWalkMark>(m);
		Tally width	 = visitAll<RWidthSearch>(m);
		out.lines.emplace_back("per-triangle stamps (visit, fresh, walk) and width search", (stamps.add(width), stamps));
		t = {};
		t.paged(m.vertices());
		t.add(visitAll<RVertexUses>(m));
		out.lines.emplace_back("vertices and use counts", t);
		out.lines.emplace_back("vertex hash", visitAll<RVertexMap>(m));
		out.lines.emplace_back("ring records (RegisteredRing)", visitAll<RRings>(m));
		const Tally ringHeap = visitAll<RRingHeap>(m);
		out.lines.emplace_back("ring outline, sample ring and edge ids (heap)", ringHeap);
		const Tally pieces = visitAll<RPieces>(m);
		Tally		edges  = visitAll<REdgeOf>(m);
		edges.add(pieces);
		out.lines.emplace_back("ring edge index (edge of, pieces)", edges);
		const Tally sources = visitAll<RSourceLists>(m);
		out.lines.emplace_back("edge source lists (interned)", sources);
		Tally covers = visitAll<RCovers>(m);
		covers.add(visitAll<RCoverTags>(m));
		out.lines.emplace_back("covers (interned) and cover tags", covers);
		const Tally grid	 = visitAll<RGrid>(m);
		const Tally ringGrid = visitAll<RRingGrid>(m);
		const Tally edgeGrid = visitAll<REdgeGrid>(m);
		out.lines.emplace_back("triangle box grid", grid);
		out.lines.emplace_back("ring box grid", ringGrid);
		out.lines.emplace_back("ring edge box grid", edgeGrid);
		out.lines.emplace_back("free lists and width refresh queues",
							   visitAll<RFreeRings, RFreeVertices, RFreeTriangles, RFreeEdgeIds, RFreshWidthTriangles, RPendingWidthSeeds, RStaleApexes,
										RWidthSeeds, RSeedBuckets, RSeedCellRuns, RSeedCellTable, RWidthWalk>(m));

		out.counts["triangle slots"]	= static_cast<double>(m.triangles().size());
		out.counts["live triangles"]	= static_cast<double>(m.liveTriangleCount());
		out.counts["vertex slots"]		= static_cast<double>(m.vertices().size());
		out.counts["live rings"]		= ringHeap.count;
		out.counts["edge index ids"]	= pieces.count;
		out.counts["source list ids"]	= sources.count;
		out.counts["cover ids"]			= covers.count;
		out.counts["tri grid entries"]	= grid.count;
		out.counts["ring grid entries"] = ringGrid.count;
		out.counts["edge grid entries"] = edgeGrid.count;
		return out;
	}

	// Per obstacle: each container's growth from `before` to `after`, the total, and what the heap
	// and the process grew by.
	void reportBytes(benchmark::State& state, const char* label, const MeshBytes& before, const MeshBytes& after, double obstacles,
					 double heapDelta, double privateDelta) {
		std::fprintf(stderr, "\n[%s] %.0f placed obstacles; bytes per obstacle by container (allocations per obstacle)\n", label, obstacles);
		for (std::size_t i = 0; i < after.lines.size(); ++i) {
			const double b = (after.lines[i].second.bytes - before.lines[i].second.bytes) / obstacles;
			const double a = (after.lines[i].second.allocs - before.lines[i].second.allocs) / obstacles;
			std::fprintf(stderr, "  %-58s %8.1f  (%.2f)\n", after.lines[i].first, b, a);
		}
		const double accounted = (after.total().bytes - before.total().bytes) / obstacles;
		const double allocs	   = (after.total().allocs - before.total().allocs) / obstacles;
		std::fprintf(stderr, "  %-58s %8.1f  (%.2f)\n", "accounted", accounted, allocs);
		std::fprintf(stderr, "  %-58s %8.1f\n", "CRT heap allocated", heapDelta / obstacles);
		std::fprintf(stderr, "  %-58s %8.1f\n", "process private bytes", privateDelta / obstacles);
		std::fprintf(stderr, "  counts per obstacle:");
		for (const auto& [name, value] : after.counts) {
			std::fprintf(stderr, " %s %.2f;", name.c_str(), (value - before.counts.at(name)) / obstacles);
		}
		std::fprintf(stderr, "\n");
		state.counters["obstacles"]			 = obstacles;
		state.counters["accountedBPerObst"]	 = accounted;
		state.counters["allocsPerObst"]		 = allocs;
		state.counters["heapBPerObst"]		 = heapDelta / obstacles;
		state.counters["privateBPerObst"]	 = privateDelta / obstacles;
	}

	geometry::nav::PatchableMesh navMeshLikeTheSystems() {
		geometry::nav::PatchableMeshArgs args;
		args.maxBoundsEdgeMm = ecs::NavigationSystem::kMaxBoundsEdgeMm;
		args.detailTags		 = {nav::kProvenancePlaced};
		return geometry::nav::PatchableMesh(args);
	}

	std::int64_t cellOf(glm::vec2 p) {
		const auto c = [](float v) { return static_cast<std::int64_t>(std::floor(v / 16.0F)); };
		return c(p.x) * 1000003 + c(p.y);
	}

	// Patches `obstacles` into `mesh` one 16 m cell at a time, as the game does, then refreshes widths.
	void patchByCell(geometry::nav::PatchableMesh& mesh, const std::vector<std::pair<glm::vec2, geometry::nav::NavInputPolygon>>& obstacles) {
		std::map<std::int64_t, geometry::nav::PatchEdits> cells;
		for (const auto& [at, ring] : obstacles) {
			cells[cellOf(at)].adds.push_back(ring);
		}
		for (const auto& [cell, edits] : cells) {
			mesh.applyPatch(edits);
		}
		mesh.refreshWidths();
	}

	// NavScale.bench.cpp's BM_NavScaleDetail chunk, bytes by container.
	void BM_PlacedObstacleBytes(benchmark::State& state) {
		for (auto _ : state) {
			std::vector<std::pair<glm::vec2, geometry::nav::NavInputPolygon>> obstacles;
			const float halfExtents[] = {0.125F + 0.05F, 0.225F + 0.05F, 0.36F + 0.05F};
			for (float x = 1.0F; x < 511.0F; x += 3.54F) {
				for (float y = 1.0F; y < 511.0F; y += 3.54F) {
					const glm::vec2		p{x + std::fmod(y * 0.37F, 1.4F), y + std::fmod(x * 0.53F, 1.4F)};
					const std::uint32_t h = static_cast<std::uint32_t>(p.x * 131.0F + p.y * 7.0F) % 100U;
					const float			e = halfExtents[h < 15 ? 0 : h < 75 ? 1 : 2];
					obstacles.push_back({p,
										 {{nav::toMm(p - glm::vec2{e}), nav::toMm({p.x + e, p.y - e}), nav::toMm(p + glm::vec2{e}), nav::toMm({p.x - e, p.y + e})},
										  true,
										  nav::kProvenancePlaced}});
				}
			}
			geometry::nav::PatchableMesh mesh = navMeshLikeTheSystems();
			mesh.applyPatch({.adds = {nav::chunkSquare({0, 0})}});
			mesh.refreshWidths();
			const MeshBytes	   before		 = meshBytes(mesh);
			const std::int64_t heapBefore	 = heapAllocated();
			const std::int64_t privateBefore = privateBytes();
			patchByCell(mesh, obstacles);
			const std::int64_t heapAfter	= heapAllocated();
			const std::int64_t privateAfter = privateBytes();
			reportBytes(state, "dense synthetic chunk", before, meshBytes(mesh), static_cast<double>(obstacles.size()),
						static_cast<double>(heapAfter - heapBefore), static_cast<double>(privateAfter - privateBefore));
			std::fprintf(stderr, "  sizeof NavTriangle %zu, RegisteredRing %zu, NavInputPolygon %zu, SampleRing %zu\n", sizeof(geometry::nav::NavTriangle),
						 sizeof(geometry::nav::RegisteredRing), sizeof(geometry::nav::NavInputPolygon), sizeof(geometry::nav::SampleRing));
		}
	}

	// The system at the bench forest's density: the colony settled, then its first mover walked 800 m
	// east 40 m a settle at a time. What the heap grew by, less the mesh's container growth, is the
	// system's own records (and its jobs' retained capacity), per placed obstacle that entered.
	void BM_PlacedObstacleSystemBytes(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(0);
			colony(rig);
			TickStats warm;
			rig.ticksUntilSettled(warm, 20000);
			const auto		   detailCount	 = [&] { return visit<RDetailInMesh>(rig.nav).count; };
			const double	   d0			 = detailCount();
			const MeshBytes	   nav0			 = meshBytes(rig.nav.navMesh());
			const std::int64_t heapBefore	 = heapAllocated();
			const std::int64_t privateBefore = privateBytes();
			for (float dx = 40.0F; dx <= 800.0F; dx += 40.0F) {
				rig.moveMover(0, {40.0F, 0.0F});
				TickStats stats;
				rig.ticksUntilSettled(stats, 20000);
			}
			const double	   d1			= detailCount();
			const MeshBytes	   nav1			= meshBytes(rig.nav.navMesh());
			const double	   heapDelta	= static_cast<double>(heapAllocated() - heapBefore);
			const double	   added		= d1 - d0;
			reportBytes(state, "NavigationSystem nav mesh, bench forest", nav0, nav1, added, heapDelta, static_cast<double>(privateBytes() - privateBefore));
			const double meshDelta = nav1.total().bytes - nav0.total().bytes;
			const Tally	 inMesh	   = visit<RDetailInMesh>(rig.nav);
			std::fprintf(stderr,
						 "  heap growth beyond the mesh's containers %.1f B per obstacle\n"
						 "  detailInMesh %.0f entries of %.0f B each (a std::map node adds 4 pointers); wantedCells %.0f; wantedChunks %.0f\n",
						 (heapDelta - meshDelta) / added, inMesh.count, inMesh.bytes / inMesh.count,
						 visit<RWantedCells>(rig.nav).count, visit<RWantedChunks>(rig.nav).count);
			state.counters["systemBeyondMeshesBPerObst"] = (heapDelta - meshDelta) / added;
		}
	}

	// The 5 x 5 chunks round the landing placed by PlacementExecutor exactly as AsyncChunkProcessor's
	// task does (nearest the landing first, each stored before the next so cross-chunk rules see it),
	// with the real asset definitions. Per chunk: placed obstacles (entities with a blocking collision
	// ring, nav::placedObstacleRing), water share on a 2 m grid, and its 16 m cells holding none.
	void BM_PlacedObstacleDensity(benchmark::State& state) {
		const Landing* landing = quickstartLanding();
		if (landing == nullptr) {
			state.SkipWithMessage("no planet (WORLDSIM_PLANET)");
			return;
		}
		assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		registry.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");
		registry.loadDefinitionsFromFolder((projectRoot() / "assets" / "world").string());
		for (auto _ : state) {
			assets::PlacementExecutor placement(registry);
			placement.initialize();
			world::ChunkManager chunks(std::make_unique<world::GeneratedWorldSampler>(landing->planet, landing->site.latDeg, landing->site.lonDeg));
			chunks.setLoadRadius(2);
			chunks.setUnloadRadius(4);
			chunks.update({landing->at.x, landing->at.y});
			chunks.finishPendingGeneration();
			const world::ChunkCoordinate home = world::worldToChunk({landing->at.x, landing->at.y});

			struct Counted {
				int	   ring			  = 0; // Chebyshev distance from the landing chunk
				int	   obstacles	  = 0;
				int	   entities		  = 0;
				double waterShare	  = 0.0;
				int	   emptyCells	  = 0; // of 1,024
				int	   waterCells	  = 0; // all water
				int	   emptyLandCells = 0; // no obstacle, not all water
			};
			std::vector<Counted>															  counted;
			std::vector<std::pair<glm::vec2, geometry::nav::NavInputPolygon>>				  nearObstacles; // the 3 x 3's
			std::vector<geometry::nav::NavInputPolygon>										  nearGround;
			for (int r = 0; r <= 2; ++r) {
				for (int dy = -r; dy <= r; ++dy) {
					for (int dx = -r; dx <= r; ++dx) {
						if (std::max(std::abs(dx), std::abs(dy)) != r) {
							continue;
						}
						const world::ChunkCoordinate coord{home.x + dx, home.y + dy};
						const world::Chunk*			 chunk = chunks.getChunk(coord);
						if (chunk == nullptr || !chunk->isReady()) {
							state.SkipWithError("chunk not ready");
							return;
						}
						const assets::ChunkDataSnapshot	 data = assets::captureChunkData(chunk);
						const world::TerrainPolygonQuery water(*data.terrainPolygons);
						const world::RenderTileView		 land = world::chunkRenderTiles(data.coord, data.renderTiles);
						assets::ChunkPlacementContext	 ctx;
						ctx.coord		= data.coord;
						ctx.worldSeed	= data.worldSeed;
						ctx.getBiome	= [&data](uint16_t x, uint16_t y) { return data.biomes[y * world::kChunkSize + x]; };
						ctx.isWater		= [&water](glm::vec2 pos) { return water.isInsideWater(geometry::quantize(pos)); };
						ctx.landSurface = [&land, &data](glm::vec2 pos) {
							return world::evaluateSurfaceField(land, world::tilePointOf(pos.x, pos.y), data.landParams).surface;
						};
						assets::AsyncChunkPlacementResult result = placement.computeChunkEntities(ctx, &placement);

						const glm::vec2		origin{static_cast<float>(coord.x) * 512.0F, static_cast<float>(coord.y) * 512.0F};
						std::vector<int>	perCell(1024, 0);
						std::vector<int>	waterPerCell(1024, 0);
						Counted				c;
						c.ring	   = r;
						c.entities = static_cast<int>(result.entities.size());
						for (const assets::PlacedEntity& e : result.entities) {
							std::optional<geometry::nav::NavInputPolygon> ring = nav::placedObstacleRing(e, registry);
							if (!ring.has_value()) {
								continue;
							}
							++c.obstacles;
							const int cx = std::clamp(static_cast<int>((e.position.x - origin.x) / 16.0F), 0, 31);
							const int cy = std::clamp(static_cast<int>((e.position.y - origin.y) / 16.0F), 0, 31);
							++perCell[static_cast<std::size_t>(cy * 32 + cx)];
							if (r <= 1) {
								nearObstacles.emplace_back(e.position, std::move(*ring));
							}
						}
						int waterSamples = 0;
						for (int j = 0; j < 256; ++j) {
							for (int i = 0; i < 256; ++i) {
								if (water.isInsideWater(geometry::quantize(origin + glm::vec2{static_cast<float>(i) * 2.0F + 1.0F, static_cast<float>(j) * 2.0F + 1.0F}))) {
									++waterSamples;
									++waterPerCell[static_cast<std::size_t>((j / 8) * 32 + i / 8)];
								}
							}
						}
						c.waterShare = static_cast<double>(waterSamples) / 65536.0;
						for (std::size_t k = 0; k < 1024; ++k) {
							c.emptyCells += perCell[k] == 0 ? 1 : 0;
							c.waterCells += waterPerCell[k] == 64 ? 1 : 0;
							c.emptyLandCells += perCell[k] == 0 && waterPerCell[k] < 64 ? 1 : 0;
						}
						std::fprintf(stderr, "[density] chunk %+d %+d: %d placed obstacles (%d entities), water %.1f%%, %.4f per m2 (%.4f per land m2), "
											 "cells: %d empty, %d all water, %d land with none\n",
									 dx, dy, c.obstacles, c.entities, 100.0 * c.waterShare, c.obstacles / 262144.0,
									 c.waterShare < 1.0 ? c.obstacles / (262144.0 * (1.0 - c.waterShare)) : 0.0, c.emptyCells, c.waterCells, c.emptyLandCells);
						counted.push_back(c);
						if (r <= 1) {
							nearGround.push_back(nav::chunkSquare(coord));
							nav::appendChunkWater(chunk->terrainPolygons(), nearGround);
						}
						placement.storeChunkResult(std::move(result));
					}
				}
			}
			for (int r = 0; r <= 2; ++r) {
				double n = 0.0, obstacles = 0.0, water = 0.0, empty = 0.0, emptyLand = 0.0, waterCells = 0.0;
				double lo = 1e9, hi = 0.0;
				for (const Counted& c : counted) {
					if (c.ring > r) {
						continue;
					}
					n += 1.0;
					obstacles += c.obstacles;
					water += c.waterShare;
					empty += c.emptyCells;
					emptyLand += c.emptyLandCells;
					waterCells += c.waterCells;
					lo = std::min(lo, c.obstacles / 262144.0);
					hi = std::max(hi, c.obstacles / 262144.0);
				}
				const int side = 2 * r + 1;
				std::fprintf(stderr, "[density] %dx%d: %.0f placed obstacles, %.4f per m2 (chunk min %.4f, max %.4f), %.4f per land m2; water %.1f%%; "
									 "cells with none %.1f%% (all water %.1f%%, land %.1f%%)\n",
							 side, side, obstacles, obstacles / (n * 262144.0), lo, hi, obstacles / (262144.0 * (n - water)), 100.0 * water / n,
							 100.0 * empty / (n * 1024.0), 100.0 * waterCells / (n * 1024.0), 100.0 * emptyLand / (n * 1024.0));
				const std::string key = std::to_string(side) + "x" + std::to_string(side);
				state.counters[key + "PerM2"]		= obstacles / (n * 262144.0);
				state.counters[key + "MinPerM2"]	= lo;
				state.counters[key + "MaxPerM2"]	= hi;
				state.counters[key + "WaterShare"]	= water / n;
			}

			// The 3 x 3's placed obstacles on their real ground, as the game patches them.
			geometry::nav::PatchableMesh mesh = navMeshLikeTheSystems();
			mesh.applyPatch({.adds = nearGround});
			mesh.refreshWidths();
			const MeshBytes	   before		 = meshBytes(mesh);
			const std::int64_t heapBefore	 = heapAllocated();
			const std::int64_t privateBefore = privateBytes();
			patchByCell(mesh, nearObstacles);
			const std::int64_t heapAfter	= heapAllocated();
			const std::int64_t privateAfter = privateBytes();
			reportBytes(state, "Quick Start 3x3, real ground and placement", before, meshBytes(mesh), static_cast<double>(nearObstacles.size()),
						static_cast<double>(heapAfter - heapBefore), static_cast<double>(privateAfter - privateBefore));
		}
	}

} // namespace

// The last argument of every row is the gap between ticks in ms.
BENCHMARK(BM_ColdSettleNineChunks)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Steady)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FellTree)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BuildWall)->Args({0, 0})->Args({0, 2})->Args({1, 0})->Args({1, 2})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WalkingMovers)->Args({20, 0})->Args({200, 0})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ColonistsWalk)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Crowd20)->Args({0, 0})->Args({0, 2})->Args({1, 0})->Args({1, 2})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ReplanWalk)->Arg(0)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RouteSlice)->Arg(1)->Arg(20)->Arg(200)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CursorCheck)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TwoGroupsApart)->Arg(0)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WallEditMeshedChunks)->Args({2, 0})->Args({5, 0})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RealChunksGround)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlacedObstacleBytes)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlacedObstacleSystemBytes)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlacedObstacleDensity)->Iterations(1)->Unit(benchmark::kMillisecond);
