// VisionSystem putting what colonists see into memory and dropping what they see is gone, the vision
// half of the decision paths behavior-data-migration.md moves to data, timed on the hand-written code.
// No nav system and no walls, so every sight is the plain disc: SightForest.bench.cpp times the shape,
// this times memory.
//
// BM_VisionRememberForget: placed flora over a 200 x 100 m stand at range(1) per 1,000 m2, a third each
// trees and berry bushes (both harvestable, so memory keeps them) and ground flora (no capability, so
// memory drops it), on a jittered grid. 80 is the landing's measured tree density (tree-variety.md, D27);
// 400 adds the understory of a dense forest, about 1,100 placed things in a 30 m sight. range(0)
// colonists, 2 m apart across the stand's middle, walk east at 2 m/s with vision passing every 5 frames
// at 60 fps (interval 1 here, 1/12 s a pass), for 600 passes (100 m). Ahead of the first colonist, every
// fourth placed thing within 10 m of his line is taken from the placement index once it is 20 m ahead
// of him, as a harvest would, so the next pass sees it gone and forgets it.
//
// Reports the first pass (every colonist remembering a whole view), the walk's passes (mean, p99,
// worst), what the first colonist sees at the start and remembers at the end, and how many things were
// taken. The run fails if he still remembers one that was taken.

#include "VisionSystem.h"

#include "../World.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <world/chunk/ChunkCoordinate.h>

#include <benchmark/benchmark.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

	using Clock = std::chrono::steady_clock;

	constexpr float kPassSeconds	= 5.0F / 60.0F;
	constexpr float kWalkMps		= 2.0F;
	constexpr int	kPasses			= 600;
	constexpr float kStandWestM		= -50.0F;
	constexpr float kStandEastM		= 150.0F;
	constexpr float kStandHalfDepthM = 50.0F;
	constexpr float kHarvestBandM	= 10.0F;
	constexpr float kHarvestAheadM	= 20.0F;

	constexpr std::array<const char*, 3> kFloraDefs{"BenchVision_Tree", "BenchVision_Bush", "BenchVision_Flower"};
	constexpr std::size_t				 kFlowerKind = 2;

	std::uint32_t hashU32(std::uint32_t value) {
		value ^= value >> 16U;
		value *= 0x7feb352dU;
		value ^= value >> 15U;
		value *= 0x846ca68bU;
		value ^= value >> 16U;
		return value;
	}

	double atPercentile(std::vector<double> values, double percentile) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(percentile * static_cast<double>(values.size())))];
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double value : values) {
			sum += value;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	void registerFlora() {
		static const bool registered = [] {
			auto& registry = assets::AssetRegistry::Get();
			for (std::size_t kind = 0; kind < kFloraDefs.size(); ++kind) {
				assets::AssetDefinition flora;
				flora.defName = kFloraDefs[kind];
				flora.label	  = kFloraDefs[kind];
				if (kind != kFlowerKind) {
					flora.capabilities.harvestable				 = assets::HarvestableCapability{};
					flora.capabilities.harvestable->yieldDefName = "BenchVision_Yield";
				}
				registry.registerTestDefinition(std::move(flora));
			}
			return true;
		}();
		(void)registered;
	}

	struct Placed {
		glm::vec2	position;
		const char* defName;
	};

	// The stand at `perThousandSquareMeters` into `executor`, with the processed chunks vision gates on:
	// one thing in each cell of a square grid of that density, jittered up to a quarter cell, so no two
	// stand nearer than half a cell, as placement spaces flora. Reconciliation takes a thing of the same
	// kind within 0.2 m of a remembered one for it, so random points would hide some takes.
	std::vector<Placed> placeStand(
		double perThousandSquareMeters, assets::PlacementExecutor& executor, std::unordered_set<world::ChunkCoordinate>& processed
	) {
		const auto	  spacing = static_cast<float>(std::sqrt(1000.0 / perThousandSquareMeters));
		std::uint32_t seed	  = 61;
		const auto	  jitter  = [&seed, spacing] {
			   seed = hashU32(seed + 0x9e3779b9U);
			   return (static_cast<float>(seed & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU) - 0.5F) * 0.5F * spacing;
		};
		std::vector<Placed> placed;
		for (float y = -kStandHalfDepthM + spacing / 2.0F; y < kStandHalfDepthM; y += spacing) {
			for (float x = kStandWestM + spacing / 2.0F; x < kStandEastM; x += spacing) {
				const float jitterX = jitter();
				placed.push_back({{x + jitterX, y + jitter()}, kFloraDefs[placed.size() % kFloraDefs.size()]});
			}
		}

		std::unordered_map<world::ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
		for (const Placed& thing : placed) {
			const world::ChunkCoordinate	   coord  = world::worldToChunk({thing.position.x, thing.position.y});
			assets::AsyncChunkPlacementResult& result = byChunk[coord];
			result.coord							  = coord;
			assets::PlacedEntity entity;
			entity.defName	= thing.defName;
			entity.position = thing.position;
			result.spatialIndex.insert(entity);
		}
		for (auto& [coord, result] : byChunk) {
			executor.storeChunkResult(std::move(result));
			processed.insert(coord);
		}
		return placed;
	}

	void BM_VisionRememberForget(benchmark::State& state) {
		const int	 colonistCount = static_cast<int>(state.range(0));
		const double density	   = static_cast<double>(state.range(1));
		registerFlora();
		auto& registry = assets::AssetRegistry::Get();

		for (auto iteration : state) {
			assets::PlacementExecutor				   executor(registry);
			std::unordered_set<world::ChunkCoordinate> processed;
			const std::vector<Placed>				   placed = placeStand(density, executor, processed);

			ecs::World			world;
			ecs::VisionSystem& vision = world.registerSystem<ecs::VisionSystem>();
			vision.setPlacementData(&executor, &processed);
			vision.setUpdateInterval(1);

			std::vector<ecs::EntityID> colonists;
			for (int index = 0; index < colonistCount; ++index) {
				const float			side	 = index % 2 == 0 ? 1.0F : -1.0F;
				const float			y		 = side * static_cast<float>((index + 1) / 2) * 2.0F;
				const ecs::EntityID colonist = world.createEntity();
				world.addComponent<ecs::Position>(colonist, ecs::Position{{0.0F, y}});
				world.addComponent<ecs::MovementTarget>(colonist, ecs::MovementTarget{{kStandEastM, y}, kWalkMps, true});
				world.addComponent<ecs::Memory>(colonist, ecs::Memory{});
				world.getComponent<ecs::Memory>(colonist)->owner = colonist;
				world.addComponent<ecs::Knowledge>(colonist, ecs::Knowledge{});
				colonists.push_back(colonist);
			}
			const ecs::EntityID lead = colonists.front();

			std::size_t inView = 0;
			for (const Placed& thing : placed) {
				inView += glm::dot(thing.position, thing.position) <= ecs::kDefaultSightRadius * ecs::kDefaultSightRadius ? 1 : 0;
			}
			std::vector<Placed> harvests;
			for (std::size_t index = 0; index < placed.size(); ++index) {
				const Placed& thing = placed[index];
				if (index % 4 == 0 && thing.position.x > kHarvestAheadM && std::abs(thing.position.y) <= kHarvestBandM) {
					harvests.push_back(thing);
				}
			}
			std::sort(harvests.begin(), harvests.end(), [](const Placed& first, const Placed& second) { return first.position.x < second.position.x; });

			std::vector<double> passUs;
			passUs.reserve(kPasses);
			double		firstPassUs = 0.0;
			std::size_t nextHarvest = 0;
			for (int pass = 0; pass <= kPasses; ++pass) {
				const float walked = kWalkMps * kPassSeconds * static_cast<float>(pass);
				for (const ecs::EntityID colonist : colonists) {
					world.getComponent<ecs::Position>(colonist)->value.x = walked;
				}
				while (nextHarvest < harvests.size() && harvests[nextHarvest].position.x <= walked + kHarvestAheadM) {
					const Placed& taken = harvests[nextHarvest++];
					executor.removeEntity(world::worldToChunk({taken.position.x, taken.position.y}), taken.position, taken.defName);
				}
				const Clock::time_point start = Clock::now();
				vision.update(kPassSeconds);
				const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
				if (pass == 0) {
					firstPassUs = elapsed;
				} else {
					passUs.push_back(elapsed);
				}
			}

			const ecs::Memory& memory			= *world.getComponent<ecs::Memory>(lead);
			std::size_t		   stillRemembered = 0;
			for (std::size_t index = 0; index < nextHarvest; ++index) {
				stillRemembered += memory.knowsWorldEntity(harvests[index].position, registry.getDefNameId(harvests[index].defName)) ? 1 : 0;
			}
			if (stillRemembered > 0) {
				state.SkipWithError("the lead colonist still remembers something taken in his sight");
				break;
			}
			state.counters["colonists"]	   = colonistCount;
			state.counters["perThousandM2"] = density;
			state.counters["inView"]	   = static_cast<double>(inView);
			state.counters["firstPassUs"]  = firstPassUs;
			state.counters["passMeanUs"]   = meanOf(passUs);
			state.counters["passP99Us"]	   = atPercentile(passUs, 0.99);
			state.counters["passMaxUs"]	   = atPercentile(passUs, 1.0);
			state.counters["remembered"]   = static_cast<double>(memory.worldEntityCount());
			state.counters["taken"]		   = static_cast<double>(nextHarvest);
			benchmark::DoNotOptimize(iteration);
		}
	}

} // namespace

BENCHMARK(BM_VisionRememberForget)->ArgsProduct({{1, 8}, {80, 400}})->Iterations(1)->Unit(benchmark::kMillisecond);
