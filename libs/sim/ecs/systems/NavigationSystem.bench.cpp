// NavigationSystem through its public API on a synthetic forest (nav-mesh-variable-resolution.md
// section 6): 3x3 loaded chunks (1.5 km square), a tree every ~5.2 m (about 600 per 128 m square,
// the game's density), a meandering 8 m river across the middle, and a colony 12 m from a chunk side
// where the river runs about 30 m north. Every scenario has its own NavigationSystem and its own
// placement, so scenarios are independent of each other and of --benchmark_filter.
//
// Ticks go through World::update, which runs update() and then endTick(), where the tick's patches
// start on the workers; the next update() waits for them. The last benchmark argument is the gap
// between ticks in ms, standing in for the rest of a frame: 0 is back to back (the longest waits,
// what a headless run gives), 2 leaves the workers 2 ms before the next update.
//
// Per scenario: the main thread's time in World::update per tick without the wait for the workers
// (main), that wait (wait), each job's duration (the longer of the two meshes' jobs), the ticks from
// the change until isBuilding() is false, and the work the [NavBuild] debug lines report: both
// meshes' job time, of it the structural forest rebuilds, the slowest chunk's ground patch, and the
// meshes' triangle slots.
//
// BM_RealChunksGround loads the Quick Start planet (planets/quickstart.wsplanet, read only) and
// meshes the 25 real chunks around its landing spawn, ground only (WORLDSIM_PLANET names another
// copy of the file, e.g. the main checkout's from a worktree); it is skipped when the file is
// absent.

#include "NavigationSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <utils/Log.h>

#include <world/BiomeWeights.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/GeneratedWorldSampler.h>
#include <world/chunk/IWorldSampler.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>
#include <worldgen/sampling/SpawnSite.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

	using Clock = std::chrono::steady_clock;

	constexpr float	 kTickSeconds  = 1.0F / 60.0F;
	constexpr double kTreeSpacingM = 5.22; // 128 m square / 600 trees
	constexpr float	 kWorldMinM	   = 0.0F;
	constexpr float	 kWorldMaxM	   = 1536.0F; // chunks 0..2 on each axis
	constexpr float	 kAgentRadius  = 0.3F;
	const glm::vec2	 kColony{500.0F, 555.0F};		// 12 m from the x = 512 chunk side; the river is ~30 m north
	const glm::vec2	 kChunkCentreColony{256.0F, 256.0F}; // the middle of chunk (0, 0), far from every seam
	// A 1920x1080 view at 30 px/m, half extents times kViewportMargin.
	const glm::vec2 kViewHalf{32.0F * ecs::NavigationSystem::kViewportMargin, 18.0F * ecs::NavigationSystem::kViewportMargin};
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
		std::vector<double> jobMs; // per job, the longer of the two meshes' jobs
		std::vector<double> groundMs;
		std::vector<double> forestMs;
		std::vector<double> forestSlots; // structural slots when each forest rebuild ran
		double				navMs	   = 0.0;
		double				structMs   = 0.0;
		double				waitMs	   = 0.0; // since the last takeWait
		long long			maxSlots   = 0;
		long long			maxStructuralSlots = 0;

		void reset() {
			const std::lock_guard lock(mutex);
			jobMs.clear();
			groundMs.clear();
			forestMs.clear();
			forestSlots.clear();
			navMs = structMs = waitMs = 0.0;
			maxSlots = maxStructuralSlots = 0;
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
				const double nav = numberAfter(m, "job nav ");
				const double st	 = numberAfter(m, "structural ");
				self->navMs += nav;
				self->structMs += st;
				self->jobMs.push_back(std::max(nav, st));
				self->waitMs += numberAfter(m, "wait ");
				const double forests = numberAfter(m, "forests ");
				const auto	 structuralSlots = static_cast<long long>(numberAfter(m, "structuralSlots "));
				if (st > 0.0) {
					self->forestMs.push_back(forests);
					self->forestSlots.push_back(static_cast<double>(structuralSlots));
				}
				self->maxSlots			 = std::max(self->maxSlots, static_cast<long long>(numberAfter(m, "slots ")));
				self->maxStructuralSlots = std::max(self->maxStructuralSlots, structuralSlots);
			} else if (std::strstr(m, "[NavBuild] ground ") != nullptr) {
				self->groundMs.push_back(numberAfter(m, " nav ") + numberAfter(m, "structural "));
			}
		}

		// Forest rebuild time per thousand structural slots, least squares through the origin.
		double forestUsPerKiloSlot() const {
			double xy = 0.0;
			double xx = 0.0;
			for (std::size_t i = 0; i < forestMs.size(); ++i) {
				xy += forestSlots[i] * forestMs[i];
				xx += forestSlots[i] * forestSlots[i];
			}
			return xx > 0.0 ? 1e6 * xy / xx : 0.0;
		}
	};

	BuildLog& buildLog() {
		static BuildLog log;
		return log;
	}

	struct Tree {
		world::ChunkCoordinate coord;
		glm::vec2			   position;
	};

	// The world every scenario shares: chunks, where the trees stand, and the registries.
	struct Forest {
		std::unique_ptr<world::ChunkManager> chunks;
		std::vector<Tree>					 trees;

		Forest() {
			foundation::Logger::initialize();
			assets::ConstructionRegistry::Get().clear();
			assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string());

			assets::AssetDefinition tree;
			tree.defName					 = kTreeDef;
			tree.collision.type				 = assets::CollisionShapeType::Rect;
			tree.collision.halfExtentsMeters = {0.3F, 0.3F};
			assets::AssetRegistry::Get().registerTestDefinition(tree);

			chunks = std::make_unique<world::ChunkManager>(std::make_unique<ForestRiverSampler>());
			chunks->setLoadRadius(1);
			chunks->setUnloadRadius(3);
			chunks->update({768.0F, 768.0F});
			chunks->finishPendingGeneration();

			const int cells = static_cast<int>((kWorldMaxM - kWorldMinM) / kTreeSpacingM);
			for (int j = 0; j < cells; ++j) {
				for (int i = 0; i < cells; ++i) {
					const std::uint32_t h  = hashU32(static_cast<std::uint32_t>(j * 4096 + i));
					const float			jx = (static_cast<float>(h & 0xFFFFU) / 65535.0F - 0.5F) * 3.0F;
					const float			jy = (static_cast<float>(h >> 16U) / 65535.0F - 0.5F) * 3.0F;
					const glm::vec2		p{kWorldMinM + (static_cast<float>(i) + 0.5F) * static_cast<float>(kTreeSpacingM) + jx,
										  kWorldMinM + (static_cast<float>(j) + 0.5F) * static_cast<float>(kTreeSpacingM) + jy};
					if (std::abs(p.y - riverY(p.x)) >= 6.0F) {
						trees.push_back({world::worldToChunk({p.x, p.y}), p});
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
				e.defName  = kTreeDef;
				e.position = t.position;
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
			state.counters["meshMs"]		  = log.navMs + log.structMs;
			state.counters["navMs"]			  = log.navMs;
			state.counters["structMs"]		  = log.structMs;
			state.counters["forestMsSum"]	  = meanOf(log.forestMs) * static_cast<double>(log.forestMs.size());
			state.counters["forestUsPerKSlot"] = log.forestUsPerKiloSlot();
			state.counters["groundMaxMs"]	  = atPercentile(log.groundMs, 1.0);
			state.counters["slots"]			  = static_cast<double>(log.maxSlots);
			state.counters["structSlots"]	  = static_cast<double>(log.maxStructuralSlots);
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

		void addMover(glm::vec2 at) {
			const ecs::EntityID e = world.createEntity();
			world.addComponent<ecs::Position>(e, ecs::Position{at});
			world.addComponent<ecs::Colonist>(e, ecs::Colonist{"Bench"});
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

	void colony(Rig& rig, glm::vec2 at = kColony) {
		const glm::vec2 offsets[] = {{0.0F, 0.0F}, {6.0F, 3.0F}, {-5.0F, 4.0F}, {3.0F, -6.0F}, {-8.0F, -3.0F}};
		for (const glm::vec2 o : offsets) {
			rig.addMover(at + o);
		}
		rig.nav.setViewportRect(at, kViewHalf);
	}

	void begin() {
		buildLog().reset();
		foundation::Logger::setSink(&BuildLog::sink, &buildLog());
	}

	void end() { foundation::Logger::setSink(nullptr, nullptr); }

	// All nine chunks' ground and the colony's flora from nothing: the old system meshed one region
	// here, so this is different work from its row, not the same work done two ways.
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

	// range(0): metres per second; range(1): 1 to sweep back and forth over 200 m.
	void BM_Pan(benchmark::State& state) {
		const float speed = static_cast<float>(state.range(0));
		const bool	sweep = state.range(1) != 0;
		const int	count = speed < 10.0F ? 1200 : 600;
		for (auto _ : state) {
			Rig rig(state.range(2));
			colony(rig);
			rig.settle();
			begin();
			TickStats stats;
			float	  offset = 0.0F;
			float	  dir	 = 1.0F;
			for (int i = 0; i < count; ++i) {
				offset += dir * speed * kTickSeconds;
				if (sweep && (offset > 200.0F || offset < 0.0F)) {
					dir = -dir;
				}
				rig.nav.setViewportRect(kColony + glm::vec2{offset, 0.0F}, kViewHalf);
				rig.tick(stats);
			}
			end();
			stats.report(state);
		}
	}

	void BM_ZoomSweep(benchmark::State& state) {
		for (auto _ : state) {
			Rig rig(state.range(0));
			colony(rig);
			rig.settle();
			begin();
			TickStats stats;
			for (int i = 0; i < 600; ++i) {
				const float t = static_cast<float>(i) / 600.0F;
				const float k = 0.5F + 3.0F * (t < 0.5F ? 2.0F * t : 2.0F - 2.0F * t);
				rig.nav.setViewportRect(kColony, kViewHalf * k);
				rig.tick(stats);
			}
			end();
			stats.report(state);
		}
	}

	// Every mover walks 100 m east at 4 m/s, the view staying on the colony.
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
		rig.nav.setViewportRect(kColony, kViewHalf);
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

	// Fixed start/goal pairs in a colony's view, once settled. range(0): 0 the colony by the chunk
	// side (the rows the old system has), 1 a colony in a chunk's middle. Prints each pair's verdict
	// and length so two builds can be compared pair by pair.
	void BM_RequestPath(benchmark::State& state) {
		const glm::vec2 at = state.range(0) == 0 ? kColony : kChunkCentreColony;
		for (auto _ : state) {
			Rig rig(state.range(1));
			colony(rig, at);
			rig.settle();
			std::vector<std::pair<glm::vec2, glm::vec2>> pairs;
			std::uint32_t								 seed = 5;
			auto										 unit = [&seed] {
				   seed = hashU32(seed + 0x9e3779b9U);
				   return static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
			};
			for (int i = 0; i < 64; ++i) {
				const glm::vec2 a = at + glm::vec2{(unit() - 0.5F) * 70.0F, (unit() - 0.5F) * 40.0F};
				const glm::vec2 b = at + glm::vec2{(unit() - 0.5F) * 70.0F, (unit() - 0.5F) * 40.0F};
				pairs.emplace_back(a, b);
			}
			int			  ok		= 0;
			double		  firstUs	= 0.0;
			double		  warmUs	= 0.0;
			double		  lengthSum = 0.0;
			std::uint64_t firstNodes = 0;
			rig.nav.resetNavQueryStats();
			for (int round = 0; round < 11; ++round) {
				const Clock::time_point start = Clock::now();
				for (std::size_t i = 0; i < pairs.size(); ++i) {
					const auto path = rig.nav.requestPath(pairs[i].first, pairs[i].second, kAgentRadius);
					if (round == 0) {
						double len = 0.0;
						if (path.has_value()) {
							++ok;
							for (std::size_t k = 1; k < path->size(); ++k) {
								len += std::hypot((*path)[k].x - (*path)[k - 1].x, (*path)[k].y - (*path)[k - 1].y);
							}
							lengthSum += len;
						}
						std::printf("pair %d.%2zu %s %.3f m\n", static_cast<int>(state.range(0)), i, path.has_value() ? "ok  " : "fail", len);
					}
				}
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
				(round == 0 ? firstUs : warmUs) += us;
				if (round == 0) {
					firstNodes = rig.nav.navQueryStats().totalNodesExpanded;
				}
			}
			// What requestPath does around pathThrough, timed alone over the same pairs: the structural
			// pre-check (a locate and a forest climb at each end) and one locate on the nav mesh.
			const Clock::time_point checkStart = Clock::now();
			int						reachable  = 0;
			for (int round = 0; round < 10; ++round) {
				for (const auto& [a, b] : pairs) {
					reachable += rig.nav.reaches(rig.nav.reachFrom(a, kAgentRadius), b) ? 1 : 0;
				}
			}
			state.counters["precheckRoundUs"] = std::chrono::duration<double, std::micro>(Clock::now() - checkStart).count() / 10.0;
			const Clock::time_point locateStart = Clock::now();
			for (int round = 0; round < 10; ++round) {
				for (const auto& [a, b] : pairs) {
					reachable += rig.nav.inSimArea(b) ? 0 : 1;
				}
			}
			state.counters["locateRoundUs"] = std::chrono::duration<double, std::micro>(Clock::now() - locateStart).count() / 10.0;
			benchmark::DoNotOptimize(reachable);
			state.counters["pairs"]			  = static_cast<double>(pairs.size());
			state.counters["ok"]			  = ok;
			state.counters["firstRoundUs"]	  = firstUs;
			state.counters["warmRoundUs"]	  = warmUs / 10.0;
			state.counters["lengthSumM"]	  = lengthSum;
			state.counters["nodesFirstRound"] = static_cast<double>(firstNodes);
		}
	}

	// WOR-644: two colonists 160 m apart on connected ground, the view elsewhere.
	void BM_TwoGroupsApart(benchmark::State& state) {
		for (auto _ : state) {
			Rig				rig(state.range(0));
			const glm::vec2 a{300.0F, 470.0F};
			const glm::vec2 b{460.0F, 470.0F};
			rig.addMover(a);
			rig.addMover(b);
			rig.nav.setViewportRect({900.0F, 300.0F}, kViewHalf);
			rig.settle();
			const Clock::time_point start = Clock::now();
			const auto				path  = rig.nav.requestPath(a + glm::vec2{1.0F, 0.0F}, b + glm::vec2{1.0F, 0.0F}, kAgentRadius);
			state.counters["pathUs"]	  = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
			state.counters["routes"]	  = path.has_value() ? 1.0 : 0.0;
			state.counters["reaches"] =
				rig.nav.reaches(rig.nav.reachFrom(a + glm::vec2{1.0F, 0.0F}, kAgentRadius), b + glm::vec2{1.0F, 0.0F}) ? 1.0 : 0.0;
		}
	}

	// A wall built once every loaded chunk's ground is meshed: its job carries the structural forest
	// rebuild over all of it. range(0) is the chunk load radius around the forest's middle: 2 meshes
	// 25 chunks, 5 meshes 121 (the most the system holds is the loaded chunks plus
	// kKeptUnloadedChunks).
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

	// The 25 real chunks around the Quick Start landing spawn, ground only, ticked until settled.
	void BM_RealChunksGround(benchmark::State& state) {
		// A worktree has no planets/ of its own unless it is junctioned; WORLDSIM_PLANET names the file.
		const char*					planetEnv  = std::getenv("WORLDSIM_PLANET");
		const std::filesystem::path planetPath = planetEnv != nullptr ? std::filesystem::path(planetEnv) : projectRoot() / "planets" / "quickstart.wsplanet";
		if (!std::filesystem::exists(planetPath)) {
			state.SkipWithMessage("no planets/quickstart.wsplanet");
			return;
		}
		static const std::shared_ptr<const worldgen::GeneratedWorld> planet = worldgen::loadPlanet(planetPath);
		if (!planet) {
			state.SkipWithError("planets/quickstart.wsplanet did not load");
			return;
		}
		const worldgen::LatLon	  site	= worldgen::findDefaultLandingSite(*planet, worldgen::Biome::TemperateDeciduousForest);
		const worldgen::SpawnSite spawn = worldgen::findRiverbankSpawn(planet, site.latDeg, site.lonDeg);
		const glm::vec2			  at{static_cast<float>(spawn.xMeters), static_cast<float>(spawn.yMeters)};
		world::ChunkManager		  chunks(std::make_unique<world::GeneratedWorldSampler>(planet, site.latDeg, site.lonDeg));
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
			rig.nav.setViewportRect(at, kViewHalf);
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

} // namespace

// The last argument of every row is the gap between ticks in ms.
BENCHMARK(BM_ColdSettleNineChunks)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Steady)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FellTree)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BuildWall)->Args({0, 0})->Args({0, 2})->Args({1, 0})->Args({1, 2})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Pan)
	->Args({2, 0, 0})
	->Args({2, 0, 2})
	->Args({83, 0, 0})
	->Args({83, 0, 2})
	->Args({83, 1, 0})
	->Args({83, 1, 2})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ZoomSweep)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ColonistsWalk)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Crowd20)->Args({0, 0})->Args({0, 2})->Args({1, 0})->Args({1, 2})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RequestPath)->Args({0, 0})->Args({1, 0})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TwoGroupsApart)->Arg(0)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WallEditMeshedChunks)->Args({2, 0})->Args({5, 0})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RealChunksGround)->Arg(0)->Arg(2)->Iterations(1)->Unit(benchmark::kMillisecond);
