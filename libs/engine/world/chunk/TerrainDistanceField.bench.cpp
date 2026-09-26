// Worker-thread cost and memory of the distance-field bake
// (terrain-polygons-architecture.md 10.4 and section 6). The rings are built
// once outside the timed loop; only TerrainDistanceField::bake is timed.
//
//  - AllLand: no rings, no thalwegs.
//  - StaircaseLake: the ~400 m lake of the waterline bench, ~1.3 km of shore.
//  - Archipelago: water wherever a 40 m value noise exceeds one half.
//  - RiverSource: the carved-river world's headwater chunk, where the trunk
//    starts and its springs and feeders fan out.
//  - WideRiver: a hand-built 110 m river (hw 55 m, the widest the network
//    makes) straight through the chunk, the worst case for the thalweg ratio.
//
// Counters report the bytes each texture holds and the near tile count.

#include "world/chunk/Chunk.h"
#include "world/chunk/GeneratedWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"
#include "world/chunk/TerrainDistanceField.h"
#include "world/chunk/TerrainPolygonBuilder.h"

#include <random/HashNoise.h>

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

	using engine::world::Biome;
	using engine::world::ChunkCoordinate;
	using engine::world::ChunkTerrainPolygons;
	using engine::world::kExtendedSize;
	using engine::world::TerrainDistanceField;
	using engine::world::TerrainPolygonBuilder;
	using engine::world::TileData;

	constexpr uint64_t	  kSeed = 424242;
	const ChunkCoordinate kCoord{3, -2};

	ChunkTerrainPolygons buildFromTiles(bool (*isWater)(int32_t ex, int32_t ey)) {
		std::vector<TileData> tiles(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kExtendedSize));
		for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
			for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
				TileData& t		 = tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
				t.primaryBiome	 = isWater(ex, ey) ? Biome::Lake : Biome::TemperateGrassland;
				t.secondaryBiome = t.primaryBiome;
			}
		}
		const TerrainPolygonBuilder::ExtendedTileFn fn = [&tiles](int32_t ex, int32_t ey) -> const TileData& {
			return tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
		};
		return TerrainPolygonBuilder::build(kCoord, kSeed, fn, {}, {});
	}

	bool staircaseLake(int32_t ex, int32_t ey) {
		const double sx = std::floor(ex / 16.0) * 16.0 + 8.0 - 264.0;
		const double sy = std::floor(ey / 16.0) * 16.0 + 8.0 - 264.0;
		return sx * sx + sy * sy < 200.0 * 200.0;
	}

	bool archipelago(int32_t ex, int32_t ey) {
		return foundation::valueNoise3(static_cast<float>(ex) / 40.0F, static_cast<float>(ey) / 40.0F, 0.0F, 7U) > 0.5F;
	}

	void runBake(benchmark::State& state, const ChunkTerrainPolygons& polys, ChunkCoordinate coord) {
		TerrainDistanceField field;
		for (auto _ : state) {
			field = TerrainDistanceField::bake(polys, coord);
			benchmark::DoNotOptimize(field);
		}
		size_t ringVerts = 0;
		for (const auto& r : polys.rings) {
			ringVerts += r.ring.size();
		}
		state.counters["ringVerts"]	 = static_cast<double>(ringVerts);
		state.counters["thalwegs"]	 = static_cast<double>(polys.thalwegs.size());
		state.counters["nearTiles"]	 = static_cast<double>(field.nearTileCount());
		state.counters["nearKB"]	 = static_cast<double>(field.nearBytes()) / 1024.0;
		state.counters["farKB"]		 = static_cast<double>(field.farBytes()) / 1024.0;
		state.counters["profileKB"]	 = static_cast<double>(field.shoreProfileBytes()) / 1024.0;
		state.counters["frameKB"]	 = static_cast<double>(field.channelFrameBytes()) / 1024.0;
		state.counters["totalKB"] = static_cast<double>(field.nearBytes() + field.farBytes() + field.shoreProfileBytes() + field.channelFrameBytes()) /
									1024.0;
	}

	void BM_TerrainDistanceField_AllLand(benchmark::State& state) {
		runBake(state, ChunkTerrainPolygons{}, kCoord);
	}

	void BM_TerrainDistanceField_StaircaseLake(benchmark::State& state) {
		runBake(state, buildFromTiles(staircaseLake), kCoord);
	}

	void BM_TerrainDistanceField_Archipelago(benchmark::State& state) {
		runBake(state, buildFromTiles(archipelago), kCoord);
	}

	void BM_TerrainDistanceField_RiverSource(benchmark::State& state) {
		const ChunkCoordinate								coord{0, 0};
		const engine::world::river_test::CarvedRiverWorld world = engine::world::river_test::makeCarvedRiverWorld();
		const engine::world::GeneratedWorldSampler			sampler(world.world, world.landingLat, world.landingLon);
		auto chunk = std::make_unique<engine::world::Chunk>(coord, sampler.sampleChunk(coord), sampler.getWorldSeed());
		chunk->generate();
		runBake(state, chunk->terrainPolygons(), coord);
	}

	void BM_TerrainDistanceField_WideRiver(benchmark::State& state) {
		constexpr double kHalfWidthM = 55.0;
		constexpr double kCenterY	 = -2.0 * 512.0 + 250.0;
		const double	 x0			 = 3.0 * 512.0 - 8.0;
		const double	 x1			 = 4.0 * 512.0 + 8.0;
		auto			 mm			 = [](double m) { return std::llround(m * 1000.0); };

		ChunkTerrainPolygons		   polys;
		engine::world::TerrainRing ring;
		for (double x = x0; x < x1; x += 0.5) {
			ring.ring.push_back({mm(x), mm(kCenterY - kHalfWidthM)});
		}
		for (double x = x1; x > x0; x -= 0.5) {
			ring.ring.push_back({mm(x), mm(kCenterY + kHalfWidthM)});
		}
		ring.profiles.resize(ring.ring.size());
		ring.kind		 = engine::world::TerrainRingKind::Channel;
		ring.water		 = engine::world::WaterKind::River;
		ring.holeCapable = false;
		polys.rings.push_back(std::move(ring));

		engine::world::ThalwegPath thalweg;
		for (double x = x0; x <= x1; x += 0.5) {
			thalweg.points.push_back({mm(x), mm(kCenterY + 3.0 * std::sin(x / 40.0))});
			thalweg.halfWidthM.push_back(static_cast<float>(kHalfWidthM));
			thalweg.widthRatio.push_back(1.0F);
			thalweg.curvature.push_back(0.0F);
			thalweg.arcLengthM.push_back(x - x0);
		}
		polys.thalwegs.push_back(std::move(thalweg));
		runBake(state, polys, kCoord);
	}

} // namespace

BENCHMARK(BM_TerrainDistanceField_AllLand)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainDistanceField_StaircaseLake)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainDistanceField_Archipelago)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainDistanceField_RiverSource)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TerrainDistanceField_WideRiver)->Unit(benchmark::kMillisecond);
