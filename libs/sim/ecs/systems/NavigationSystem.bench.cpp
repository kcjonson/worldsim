// NavigationSystem through its public API on a synthetic forest (nav-mesh-variable-resolution.md
// section 6): 3x3 loaded chunks (1.5 km square), a tree every ~5.2 m (about 600 per 128 m square,
// the game's density), a meandering 8 m river across the middle, and a colony near the corner
// where four chunks meet. Each run paces ticks at 60 Hz, so work the system hands to another
// thread runs in the frame time it would have in the game.
//
// Per scenario: the main-thread time inside update() per tick (mean, p99, max, in us), the ticks
// from the change until isBuilding() is false, and the mesh work the system reports through its
// [NavBuild] debug lines (a whole-region buildNavMesh, or a tick's patches), summed. The patch
// lines also split nav-mesh from structural-mesh time and carry the largest triangle slot count.
//
// The scenarios run in order on the same synthetic world, one NavigationSystem each.

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
#include <world/chunk/IWorldSampler.h>

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
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

	using Clock = std::chrono::steady_clock;

	constexpr float	 kTickSeconds = 1.0F / 60.0F;
	constexpr double kTreeSpacingM = 5.22; // 128 m square / 600 trees
	constexpr float	 kWorldMinM	   = 0.0F;
	constexpr float	 kWorldMaxM	   = 1536.0F; // chunks 0..2 on each axis
	constexpr float	 kAgentRadius  = 0.3F;
	const glm::vec2	 kColony{500.0F, 555.0F}; // the river runs about 30 m north
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
				const double y0 = riverY(x);
				const double y1 = riverY(x + 20.0);
				const double len = std::hypot(20.0, y1 - y0);
				segments.push_back({.x0 = x, .y0 = y0, .x1 = x + 20.0, .y1 = y1, .halfWidth0 = 4.0F, .halfWidth1 = 4.0F, .s0 = s, .s1 = s + len});
				s += len;
			}
		}
		[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
			world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
			r.riverSegments = segments;
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

	// Sums what the [NavBuild] debug lines report, from any thread.
	struct BuildLog {
		std::mutex mutex;
		double	   meshMs	  = 0.0; // buildNavMesh, or a tick's patches (both meshes)
		double	   navMs	  = 0.0;
		double	   structMs	  = 0.0;
		double	   maxGroundMs = 0.0;
		int		   builds	  = 0;
		long long  maxSlots	  = 0;

		void reset() {
			const std::lock_guard lock(mutex);
			meshMs = navMs = structMs = maxGroundMs = 0.0;
			builds = 0;
			maxSlots = 0;
		}

		static double numberBefore(const char* text, const char* tag) {
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
			if (const char* at = std::strstr(m, "buildNavMesh "); at != nullptr) {
				self->meshMs += std::atof(at + std::strlen("buildNavMesh "));
				++self->builds;
			} else if (std::strstr(m, "[NavBuild] patch ") != nullptr) {
				const double nav = numberBefore(m, "nav ");
				const double st	 = numberBefore(m, "structural ");
				self->navMs += nav;
				self->structMs += st;
				self->meshMs += nav + st;
				self->maxSlots = std::max(self->maxSlots, static_cast<long long>(numberBefore(m, "slots ")));
				++self->builds;
			} else if (std::strstr(m, "[NavBuild] ground ") != nullptr) {
				self->maxGroundMs = std::max(self->maxGroundMs, numberBefore(m, "nav ") + numberBefore(m, "structural "));
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
	};

	// The world every scenario shares: chunks, placed trees, and the registries.
	struct Forest {
		std::unique_ptr<world::ChunkManager>			 chunks;
		std::unique_ptr<assets::PlacementExecutor>		 placement;
		std::unordered_set<world::ChunkCoordinate>		 processed;
		std::vector<Tree>								 trees;

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

			placement = std::make_unique<assets::PlacementExecutor>(assets::AssetRegistry::Get());
			std::unordered_map<world::ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
			const int cells = static_cast<int>((kWorldMaxM - kWorldMinM) / kTreeSpacingM);
			for (int j = 0; j < cells; ++j) {
				for (int i = 0; i < cells; ++i) {
					const std::uint32_t h  = hashU32(static_cast<std::uint32_t>(j * 4096 + i));
					const float			jx = (static_cast<float>(h & 0xFFFFU) / 65535.0F - 0.5F) * 3.0F;
					const float			jy = (static_cast<float>(h >> 16U) / 65535.0F - 0.5F) * 3.0F;
					const glm::vec2		p{kWorldMinM + (static_cast<float>(i) + 0.5F) * static_cast<float>(kTreeSpacingM) + jx,
										  kWorldMinM + (static_cast<float>(j) + 0.5F) * static_cast<float>(kTreeSpacingM) + jy};
					if (std::abs(p.y - riverY(p.x)) < 6.0F) {
						continue;
					}
					const world::ChunkCoordinate coord = world::worldToChunk({p.x, p.y});
					assets::AsyncChunkPlacementResult& r = byChunk[coord];
					r.coord								 = coord;
					assets::PlacedEntity e;
					e.defName  = kTreeDef;
					e.position = p;
					r.spatialIndex.insert(e);
					trees.push_back({coord, p});
				}
			}
			for (auto& [coord, result] : byChunk) {
				processed.insert(coord);
				placement->storeChunkResult(std::move(result));
			}
		}

		// The tree nearest `p`, removed from placement.
		void fellNearest(glm::vec2 p) {
			auto best = std::min_element(trees.begin(), trees.end(), [p](const Tree& a, const Tree& b) {
				const glm::vec2 da = a.position - p;
				const glm::vec2 db = b.position - p;
				return da.x * da.x + da.y * da.y < db.x * db.x + db.y * db.y;
			});
			placement->removeEntity(best->coord, best->position, kTreeDef);
			trees.erase(best);
		}
	};

	Forest& forest() {
		static Forest f;
		return f;
	}

	struct TickStats {
		std::vector<double> us;
		int					settledAt = -1;

		void report(benchmark::State& state) const {
			std::vector<double> sorted = us;
			std::sort(sorted.begin(), sorted.end());
			double sum = 0.0;
			for (double v : sorted) {
				sum += v;
			}
			const std::size_t n = sorted.size();
			state.counters["ticks"]	   = static_cast<double>(n);
			state.counters["meanUs"]   = n > 0 ? sum / static_cast<double>(n) : 0.0;
			state.counters["p99Us"]	   = n > 0 ? sorted[std::min(n - 1, (n * 99) / 100)] : 0.0;
			state.counters["maxUs"]	   = n > 0 ? sorted.back() : 0.0;
			state.counters["settleAt"] = static_cast<double>(settledAt);
			BuildLog& log			   = buildLog();
			const std::lock_guard lock(log.mutex);
			state.counters["meshMs"]	  = log.meshMs;
			state.counters["navMs"]		  = log.navMs;
			state.counters["structMs"]	  = log.structMs;
			state.counters["builds"]	  = static_cast<double>(log.builds);
			state.counters["maxGroundMs"] = log.maxGroundMs;
			state.counters["maxSlots"]	  = static_cast<double>(log.maxSlots);
		}
	};

	// One NavigationSystem over the shared forest.
	struct Rig {
		ecs::World						world;
		ecs::NavigationSystem&			nav;
		construction::ConstructionWorld construction;
		std::vector<ecs::EntityID>		movers;
		Clock::time_point				nextTick = Clock::now();

		Rig()
			: nav(world.registerSystem<ecs::NavigationSystem>()) {
			Forest& f = forest();
			nav.setChunkManager(f.chunks.get());
			nav.setPlacementData(f.placement.get(), &f.processed);
			nav.setConstructionWorld(&construction);
		}

		void addMover(glm::vec2 at) {
			const ecs::EntityID e = world.createEntity();
			world.addComponent<ecs::Position>(e, ecs::Position{at});
			world.addComponent<ecs::Colonist>(e, ecs::Colonist{"Bench"});
			movers.push_back(e);
		}

		void moveMover(std::size_t i, glm::vec2 by) { world.getComponent<ecs::Position>(movers[i])->value += by; }

		// One timed update, then wait out the rest of the 60 Hz frame.
		void tick(TickStats& stats) {
			const Clock::time_point start = Clock::now();
			nav.update(kTickSeconds);
			const Clock::time_point end = Clock::now();
			stats.us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
			if (stats.settledAt < 0 && !nav.isBuilding()) {
				stats.settledAt = static_cast<int>(stats.us.size());
			}
			nextTick += std::chrono::microseconds(16667);
			while (Clock::now() < nextTick) {
				std::this_thread::yield();
			}
		}

		void settle() {
			nav.finishPendingBuilds();
			nextTick = Clock::now();
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

	void colony(Rig& rig) {
		const glm::vec2 offsets[] = {{0.0F, 0.0F}, {6.0F, 3.0F}, {-5.0F, 4.0F}, {3.0F, -6.0F}, {-8.0F, -3.0F}};
		for (const glm::vec2 o : offsets) {
			rig.addMover(kColony + o);
		}
		rig.nav.setViewportRect(kColony, kViewHalf);
	}

	void begin(benchmark::State& state) {
		(void)state;
		forest();
		buildLog().reset();
		foundation::Logger::setSink(&BuildLog::sink, &buildLog());
	}

	void end() { foundation::Logger::setSink(nullptr, nullptr); }

	void BM_ColdSettle(benchmark::State& state) {
		for (auto _ : state) {
			begin(state);
			Rig rig;
			colony(rig);
			TickStats stats;
			rig.ticksUntilSettled(stats, 6000);
			end();
			stats.report(state);
			std::size_t waterRings = 0;
			std::size_t waterVerts = 0;
			for (const world::Chunk* chunk : forest().chunks->getLoadedChunks()) {
				for (const world::TerrainRing& ring : chunk->terrainPolygons().navRings) {
					waterRings += ring.blocksMovement ? 1 : 0;
					waterVerts += ring.blocksMovement ? ring.ring.size() : 0;
				}
			}
			state.counters["trees"]		 = static_cast<double>(forest().trees.size());
			state.counters["waterRings"] = static_cast<double>(waterRings);
			state.counters["waterVerts"] = static_cast<double>(waterVerts);
		}
	}

	void BM_Steady(benchmark::State& state) {
		for (auto _ : state) {
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
			TickStats stats;
			rig.ticks(stats, 600);
			end();
			stats.report(state);
		}
	}

	void BM_FellTree(benchmark::State& state) {
		for (auto _ : state) {
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
			forest().fellNearest(kColony + glm::vec2{5.0F, 5.0F});
			TickStats stats;
			rig.ticks(stats, 120);
			end();
			stats.report(state);
		}
	}

	void BM_BuildWall(benchmark::State& state) {
		const bool door = state.range(0) != 0;
		for (auto _ : state) {
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
			rig.wall(kColony + glm::vec2{12.0F, 8.0F}, kColony + glm::vec2{20.0F, 8.0F}, door);
			TickStats stats;
			rig.ticks(stats, 120);
			end();
			stats.report(state);
		}
	}

	// range(0): metres per second; range(1): 1 to sweep back and forth over 200 m.
	void BM_Pan(benchmark::State& state) {
		const float speed	= static_cast<float>(state.range(0));
		const bool	sweep	= state.range(1) != 0;
		const int	count	= speed < 10.0F ? 1200 : 600;
		for (auto _ : state) {
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
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
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
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
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			begin(state);
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
			forest();
			Rig rig;
			crowd(rig);
			if (walk) {
				rig.settle();
			}
			begin(state);
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

	// Fixed start/goal pairs in the colony's view, once settled. Prints each pair's verdict and
	// length so two builds can be compared pair by pair.
	void BM_RequestPath(benchmark::State& state) {
		for (auto _ : state) {
			forest();
			Rig rig;
			colony(rig);
			rig.settle();
			std::vector<std::pair<glm::vec2, glm::vec2>> pairs;
			std::uint32_t								 seed = 5;
			auto unit = [&seed] {
				seed = hashU32(seed + 0x9e3779b9U);
				return static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
			};
			for (int i = 0; i < 64; ++i) {
				const glm::vec2 a = kColony + glm::vec2{(unit() - 0.5F) * 70.0F, (unit() - 0.5F) * 40.0F};
				const glm::vec2 b = kColony + glm::vec2{(unit() - 0.5F) * 70.0F, (unit() - 0.5F) * 40.0F};
				pairs.emplace_back(a, b);
			}
			int	   ok		   = 0;
			double firstUs	   = 0.0;
			double warmUs	   = 0.0;
			double lengthSum   = 0.0;
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
						std::printf("pair %2zu %s %.3f m\n", i, path.has_value() ? "ok  " : "fail", len);
					}
				}
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
				(round == 0 ? firstUs : warmUs) += us;
			}
			state.counters["pairs"]			  = static_cast<double>(pairs.size());
			state.counters["ok"]			  = ok;
			state.counters["firstRoundUs"]	  = firstUs;
			state.counters["warmRoundUs"]	  = warmUs / 10.0;
			state.counters["lengthSumM"]	  = lengthSum;
			state.counters["nodesFirstRound"] = static_cast<double>(rig.nav.navQueryStats().totalNodesExpanded) / 11.0;
		}
	}

	// WOR-644: two colonists 160 m apart on connected ground, the view elsewhere.
	void BM_TwoGroupsApart(benchmark::State& state) {
		for (auto _ : state) {
			forest();
			Rig				rig;
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

} // namespace

BENCHMARK(BM_ColdSettle)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Steady)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FellTree)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BuildWall)->Arg(0)->Arg(1)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Pan)->Args({2, 0})->Args({83, 0})->Args({83, 1})->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ZoomSweep)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ColonistsWalk)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Crowd20)->Arg(0)->Arg(1)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RequestPath)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TwoGroupsApart)->Iterations(1)->Unit(benchmark::kMillisecond);
