// ChunkManager::update in steady state, where nothing loads or unloads, as the
// simulation anchors grow (nav-mesh-variable-resolution.md 4.5). The camera uses the
// game's radii (load 1, unload 2) and sits on the colony; every chunk the camera and
// anchors reach is generated before the timed loop.
//
//  - Clustered: every anchor within 40 m of the colony, the realistic case.
//  - Scattered: anchors spread over the 1.5 km square of the camera's 3x3 chunks, so
//    nearly every anchor covers a different set of chunks and the reduction sorts
//    the most coordinates it can for that count.
//
// ChunkMemory reports what one loaded chunk holds on the CPU (fixed arrays plus its
// terrain rings and distance field) for an all-land, a lake-shore, and a river chunk.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/GeneratedWorldSampler.h"
#include "world/chunk/LakeShoreTestWorld.h"
#include "world/chunk/MockWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

	using world::ChunkManager;
	using world::WorldPosition;

	constexpr uint64_t kSeed = 424242;
	const WorldPosition kColony{256.0F, 256.0F};

	/// Deterministic [0, 1) sequence so every run times the same anchors.
	float unitRandom(uint32_t& state) {
		state = state * 1664525U + 1013904223U;
		return static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
	}

	std::vector<WorldPosition> clusteredAnchors(int64_t count) {
		uint32_t				   state = 1;
		std::vector<WorldPosition> anchors;
		for (int64_t i = 0; i < count; ++i) {
			anchors.emplace_back(kColony.x - 40.0F + 80.0F * unitRandom(state), kColony.y - 40.0F + 80.0F * unitRandom(state));
		}
		return anchors;
	}

	std::vector<WorldPosition> scatteredAnchors(int64_t count) {
		uint32_t				   state = 2;
		std::vector<WorldPosition> anchors;
		for (int64_t i = 0; i < count; ++i) {
			anchors.emplace_back(-512.0F + 1536.0F * unitRandom(state), -512.0F + 1536.0F * unitRandom(state));
		}
		return anchors;
	}

	void runSteadyUpdate(benchmark::State& state, const std::vector<WorldPosition>& anchors) {
		ChunkManager manager(std::make_unique<world::MockWorldSampler>(kSeed));
		manager.setLoadRadius(1);
		manager.setUnloadRadius(2);
		manager.update(kColony, anchors);
		manager.finishPendingGeneration();
		manager.update(kColony, anchors);
		const size_t chunks = manager.loadedChunkCount();

		for (auto _ : state) {
			manager.update(kColony, anchors);
		}

		if (manager.loadedChunkCount() != chunks) {
			state.SkipWithError("chunks loaded or unloaded inside the timed loop");
		}
		state.counters["chunks"] = static_cast<double>(chunks);
	}

	void BM_ChunkManagerUpdate_Clustered(benchmark::State& state) {
		runSteadyUpdate(state, clusteredAnchors(state.range(0)));
	}

	void BM_ChunkManagerUpdate_Scattered(benchmark::State& state) {
		runSteadyUpdate(state, scatteredAnchors(state.range(0)));
	}

	size_t ringBytes(const std::vector<world::TerrainRing>& rings) {
		size_t bytes = rings.capacity() * sizeof(world::TerrainRing);
		for (const world::TerrainRing& r : rings) {
			bytes += r.ring.capacity() * sizeof(geometry::Vec2i64) + r.profiles.capacity() * sizeof(world::ShoreProfile);
		}
		return bytes;
	}

	size_t polygonBytes(const world::ChunkTerrainPolygons& p) {
		size_t bytes = sizeof(world::ChunkTerrainPolygons) + ringBytes(p.rings) + ringBytes(p.navRings);
		bytes += p.thalwegs.capacity() * sizeof(world::ThalwegPath);
		for (const world::ThalwegPath& t : p.thalwegs) {
			bytes += t.points.capacity() * sizeof(geometry::Vec2i64) + t.arcLengthM.capacity() * sizeof(double) +
					 (t.halfWidthM.capacity() + t.widthRatio.capacity() + t.curvature.capacity()) * sizeof(float);
		}
		const world::TerrainEdgeIndex& e = p.edgeIndex;
		bytes += e.cellStart.capacity() * sizeof(uint32_t) + e.edges.capacity() * sizeof(world::TerrainEdgeIndex::Edge) +
				 e.cellWater.capacity() * sizeof(world::TerrainEdgeIndex::CellWater) + e.shoreNearby.capacity();
		bytes += p.shorePoints.capacity() * sizeof(geometry::Vec2i64) + p.barTiles.words.capacity() * sizeof(uint64_t);
		return bytes;
	}

	void runChunkMemory(benchmark::State& state, world::ChunkCoordinate coord, const world::ChunkSampleResult& sample, uint64_t seed) {
		std::unique_ptr<world::Chunk> chunk;
		for (auto _ : state) {
			chunk = std::make_unique<world::Chunk>(coord, sample, seed);
			chunk->generate();
			benchmark::DoNotOptimize(chunk.get());
		}
		const world::TerrainDistanceField& sdf = chunk->terrainDistanceField();
		const size_t sdfBytes = sdf.nearBytes() + sdf.farBytes() + sdf.shoreProfileBytes() + sdf.channelFrameBytes();
		const size_t polyBytes = polygonBytes(chunk->terrainPolygons());
		state.counters["fixedKB"] = static_cast<double>(sizeof(world::Chunk)) / 1024.0;
		state.counters["polygonKB"] = static_cast<double>(polyBytes) / 1024.0;
		state.counters["sdfKB"] = static_cast<double>(sdfBytes) / 1024.0;
		state.counters["totalKB"] = static_cast<double>(sizeof(world::Chunk) + polyBytes + sdfBytes) / 1024.0;
	}

	void BM_ChunkMemory_AllLand(benchmark::State& state) {
		const world::MockWorldSampler sampler(kSeed);
		const world::ChunkCoordinate  coord{0, 0};
		runChunkMemory(state, coord, sampler.sampleChunk(coord), kSeed);
	}

	void BM_ChunkMemory_LakeShore(benchmark::State& state) {
		runChunkMemory(state, world::shore_test::kLakeShoreChunk, world::shore_test::lakeShoreSample(), kSeed);
	}

	void BM_ChunkMemory_RiverSource(benchmark::State& state) {
		const world::ChunkCoordinate				coord{0, 0};
		const world::river_test::CarvedRiverWorld	riverWorld = world::river_test::makeCarvedRiverWorld();
		const world::GeneratedWorldSampler			sampler(riverWorld.world, riverWorld.landingLat, riverWorld.landingLon);
		runChunkMemory(state, coord, sampler.sampleChunk(coord), sampler.getWorldSeed());
	}

} // namespace

BENCHMARK(BM_ChunkManagerUpdate_Clustered)->Arg(0)->Arg(10)->Arg(100)->Arg(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ChunkManagerUpdate_Scattered)->Arg(0)->Arg(10)->Arg(100)->Arg(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ChunkMemory_AllLand)->Iterations(3)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkMemory_LakeShore)->Iterations(3)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkMemory_RiverSource)->Iterations(3)->Unit(benchmark::kMillisecond);
