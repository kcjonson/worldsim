// Worker-thread cost of the waterline build (terrain-polygons-architecture.md
// section 6: "a few tens of ms on the worker"). Three extended tile fields:
//
//  - StaircaseLake: one lake ~400 m across built from 16 m biome sectors, the shape
//    corner-interpolated biomes produce; ~1.3 km of staircase shore.
//  - Archipelago: water wherever a 40 m value noise exceeds one half, dozens of
//    lakes and islands; the shore-heaviest chunk a real planet would make.
//  - AllOcean: every tile water, so the only ring is the synthetic closure.
//
// Two river chunks from the carved-river test world (RiverNetwork2D geometry
// through GeneratedWorldSampler, the tiles from a generated chunk plus its
// apron), timing the build alone with its channel rings (D7):
//
//  - RiverSource: the headwater, where the trunk starts and its springs and
//    feeders fan out.
//  - RiverTrunk: ~520 m of the ~10 m wide meandering trunk.
//
// Plus Chunk::generate end to end for an all-land and an all-water chunk, to show
// the builder against the rest of generation. Counters report ring vertex counts.

#include "world/chunk/ApronField.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/GeneratedWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"
#include "world/chunk/TerrainPolygonBuilder.h"

#include <random/HashNoise.h>

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

	using engine::world::Biome;
	using engine::world::BiomeWeights;
	using engine::world::Chunk;
	using engine::world::ChunkCoordinate;
	using engine::world::ChunkTerrainPolygons;
	using engine::world::kExtendedSize;
	using engine::world::TerrainPolygonBuilder;
	using engine::world::TileData;

	constexpr uint64_t kSeed = 424242;

	std::vector<TileData> makeTiles(bool (*isWater)(int32_t ex, int32_t ey), Biome water) {
		std::vector<TileData> tiles(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kExtendedSize));
		for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
			for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
				TileData& t		 = tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
				t.primaryBiome	 = isWater(ex, ey) ? water : Biome::TemperateGrassland;
				t.secondaryBiome = t.primaryBiome;
			}
		}
		return tiles;
	}

	bool staircaseLake(int32_t ex, int32_t ey) {
		const double sx = std::floor(ex / 16.0) * 16.0 + 8.0 - 264.0;
		const double sy = std::floor(ey / 16.0) * 16.0 + 8.0 - 264.0;
		return sx * sx + sy * sy < 200.0 * 200.0;
	}

	bool archipelago(int32_t ex, int32_t ey) {
		return foundation::valueNoise3(static_cast<float>(ex) / 40.0F, static_cast<float>(ey) / 40.0F, 0.0F, 7U) > 0.5F;
	}

	bool allWater(int32_t, int32_t) {
		return true;
	}

	void reportCounts(benchmark::State& state, const ChunkTerrainPolygons& polys) {
		size_t ringVerts = 0;
		size_t navVerts	 = 0;
		for (const auto& r : polys.rings) {
			ringVerts += r.ring.size();
		}
		for (const auto& r : polys.navRings) {
			navVerts += r.ring.size();
		}
		state.counters["rings"]		= static_cast<double>(polys.rings.size());
		state.counters["ringVerts"] = static_cast<double>(ringVerts);
		state.counters["navRings"]	= static_cast<double>(polys.navRings.size());
		state.counters["navVerts"]	= static_cast<double>(navVerts);
	}

	void runBuild(benchmark::State& state, bool (*isWater)(int32_t, int32_t), Biome water) {
		const ChunkCoordinate						 coord{3, -2};
		const std::vector<TileData>					 tiles = makeTiles(isWater, water);
		const TerrainPolygonBuilder::ExtendedTileFn fn	  = [&tiles](int32_t ex, int32_t ey) -> const TileData& {
			return tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
		};
		// Channel-free, so only the extended tiles are ever asked.
		const TerrainPolygonBuilder::BiomeWaterFn waterFn = [&fn, coord](int64_t tx, int64_t ty) {
			return engine::world::isBiomeWater(fn(
				static_cast<int32_t>(tx - static_cast<int64_t>(coord.x) * engine::world::kChunkSize + engine::world::kApronTiles),
				static_cast<int32_t>(ty - static_cast<int64_t>(coord.y) * engine::world::kChunkSize + engine::world::kApronTiles)
			));
		};
		ChunkTerrainPolygons polys;
		for (auto _ : state) {
			polys = TerrainPolygonBuilder::build(coord, kSeed, fn, waterFn, {}, {});
			benchmark::DoNotOptimize(polys);
		}
		reportCounts(state, polys);
	}

	void runRiverBuild(benchmark::State& state, ChunkCoordinate coord) {
		const engine::world::river_test::CarvedRiverWorld world = engine::world::river_test::makeCarvedRiverWorld();
		const engine::world::GeneratedWorldSampler			sampler(world.world, world.landingLat, world.landingLon);
		auto chunk = std::make_unique<Chunk>(coord, sampler.sampleChunk(coord), sampler.getWorldSeed());
		chunk->generate();
		const engine::world::ApronField	   apron = engine::world::ApronField::build(
			coord, chunk->biomeData(), chunk->biomeData().rasterHydrology(coord), sampler.getWorldSeed());
		const engine::world::ExtendedTiles extended(*chunk, apron);
		const TerrainPolygonBuilder::ExtendedTileFn fn = [&extended](int32_t ex, int32_t ey) -> const TileData& {
			return extended.at(ex, ey);
		};
		const engine::world::ChunkSampleResult& sample = chunk->biomeData();
		engine::world::NeighborhoodGrids		neighborhood(sample);
		const TerrainPolygonBuilder::BiomeWaterFn waterFn = [&neighborhood, coord](int64_t tx, int64_t ty) {
			return engine::world::isBiomeWater(neighborhood.primaryBiomeAt(coord, tx, ty));
		};
		ChunkTerrainPolygons polys;
		for (auto _ : state) {
			polys = TerrainPolygonBuilder::build(coord, sampler.getWorldSeed(), fn, waterFn, sample.riverSegments, sample.pondBlobs);
			benchmark::DoNotOptimize(polys);
		}
		reportCounts(state, polys);
		state.counters["segments"] = static_cast<double>(sample.riverSegments.size());
	}

	void BM_TerrainPolygonBuilder_StaircaseLake(benchmark::State& state) {
		runBuild(state, staircaseLake, Biome::Lake);
	}

	void BM_TerrainPolygonBuilder_Archipelago(benchmark::State& state) {
		runBuild(state, archipelago, Biome::Lake);
	}

	void BM_TerrainPolygonBuilder_AllOcean(benchmark::State& state) {
		runBuild(state, allWater, Biome::Ocean);
	}

	void BM_TerrainPolygonBuilder_RiverSource(benchmark::State& state) {
		runRiverBuild(state, ChunkCoordinate{0, 0});
	}

	void BM_TerrainPolygonBuilder_RiverTrunk(benchmark::State& state) {
		runRiverBuild(state, ChunkCoordinate{0, -1});
	}

	void runGenerate(benchmark::State& state, Biome biome) {
		for (auto _ : state) {
			auto chunk = std::make_unique<Chunk>(
				ChunkCoordinate{3, -2}, engine::world::makeUniformChunkSampleResult(BiomeWeights::single(biome), 1.0F), kSeed
			);
			chunk->generate();
			benchmark::DoNotOptimize(chunk->terrainPolygons());
		}
	}

	void BM_ChunkGenerate_AllLand(benchmark::State& state) {
		runGenerate(state, Biome::TemperateGrassland);
	}

	void BM_ChunkGenerate_AllWater(benchmark::State& state) {
		runGenerate(state, Biome::Ocean);
	}

	// Sampling (the river and pond gather) plus generation, on the trunk chunk: what
	// the gather margin costs the tile raster, which scans every gathered segment.
	void BM_ChunkSampleAndGenerate_RiverTrunk(benchmark::State& state) {
		const engine::world::river_test::CarvedRiverWorld world = engine::world::river_test::makeCarvedRiverWorld();
		const engine::world::GeneratedWorldSampler			sampler(world.world, world.landingLat, world.landingLon);
		size_t												segments = 0;
		for (auto _ : state) {
			const ChunkCoordinate kTrunk{0, -1};
			auto				  chunk = std::make_unique<Chunk>(kTrunk, sampler.sampleChunk(kTrunk), sampler.getWorldSeed());
			chunk->generate();
			segments = chunk->biomeData().riverSegments.size();
			benchmark::DoNotOptimize(chunk->terrainPolygons());
		}
		state.counters["segments"] = static_cast<double>(segments);
	}

	// The sampling (river and pond gather) alone, the part of the above the
	// gather margin grows directly.
	void BM_ChunkSample_RiverTrunk(benchmark::State& state) {
		const engine::world::river_test::CarvedRiverWorld world = engine::world::river_test::makeCarvedRiverWorld();
		const engine::world::GeneratedWorldSampler			sampler(world.world, world.landingLat, world.landingLon);
		for (auto _ : state) {
			benchmark::DoNotOptimize(sampler.sampleChunk(ChunkCoordinate{0, -1}));
		}
	}

} // namespace

BENCHMARK(BM_TerrainPolygonBuilder_StaircaseLake)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainPolygonBuilder_Archipelago)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainPolygonBuilder_AllOcean)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainPolygonBuilder_RiverSource)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainPolygonBuilder_RiverTrunk)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkGenerate_AllLand)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkGenerate_AllWater)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkSampleAndGenerate_RiverTrunk)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ChunkSample_RiverTrunk)->Unit(benchmark::kMillisecond);
