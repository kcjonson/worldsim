// Per-frame CPU cost of the world 2.5D depth stream: WorldDepthGather::gather on a view whose chunks
// don't change between frames, so each frame culls every chunk's cached sorted statics, sorts the
// dynamic entities, and merges the stretches. Half the statics are tall occluders, the stream's
// share; the rest are short flora the gather skips. Run at 1k, 10k, and 34k statics (the observed
// dense zoom-out), over a 4 x 4 block of chunks, with 64 actors.

#include "assets/placement/WorldDepthSort.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "assets/placement/RenderContext.h"
#include "world/camera/WorldCamera.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

namespace {

	using assets::PlacedEntity;

	constexpr int kMinEntities = 1000;
	constexpr int kMidEntities = 10000;
	constexpr int kMaxEntities = 34000;

	constexpr int	kChunksPerSide = 4;
	constexpr float kChunkWidth = 25.0F; // a 100 x 60 m block, inside an 1920 x 1080 view at 16 px/m
	constexpr float kChunkHeight = 15.0F;
	constexpr int	kActors = 64;

	struct Scene {
		assets::PlacementExecutor				   executor{assets::AssetRegistry::Get()};
		std::unordered_set<world::ChunkCoordinate> processed;
		std::vector<PlacedEntity>				   actors;
		world::WorldCamera						   camera;
	};

	void fill(Scene& scene, int statics) {
		std::mt19937						  rng(1234567U);
		std::uniform_real_distribution<float> unit(0.0F, 1.0F);
		std::uniform_real_distribution<float> foot(0.0F, 0.6F);
		const int							  perChunk = statics / (kChunksPerSide * kChunksPerSide);
		for (int cy = 0; cy < kChunksPerSide; ++cy) {
			for (int cx = 0; cx < kChunksPerSide; ++cx) {
				const float			  x0 = (static_cast<float>(cx - (kChunksPerSide / 2))) * kChunkWidth;
				const float			  y0 = (static_cast<float>(cy - (kChunksPerSide / 2))) * kChunkHeight;
				assets::SpatialIndex index;
				for (int i = 0; i < perChunk; ++i) {
					PlacedEntity entity;
					entity.defName = "Tree";
					entity.position = {x0 + (unit(rng) * kChunkWidth), y0 + (unit(rng) * kChunkHeight)};
					entity.anchorY = entity.position.y + foot(rng);
					entity.isTallOccluder = (i % 2) == 0;
					index.insert(entity);
				}
				assets::AsyncChunkPlacementResult result;
				result.coord = {cx - (kChunksPerSide / 2), cy - (kChunksPerSide / 2)};
				result.spatialIndex = std::move(index);
				scene.executor.storeChunkResult(std::move(result));
				scene.processed.insert({cx - (kChunksPerSide / 2), cy - (kChunksPerSide / 2)});
			}
		}
		scene.actors.resize(kActors);
		for (PlacedEntity& actor : scene.actors) {
			actor.position = {(unit(rng) - 0.5F) * 100.0F, (unit(rng) - 0.5F) * 60.0F};
			actor.anchorY = actor.position.y + 0.9F;
		}
		scene.camera.setPosition({0.0F, 0.0F});
	}

} // namespace

static void BM_WorldDepthGather_SteadyView(benchmark::State& state) {
	Scene scene;
	fill(scene, static_cast<int>(state.range(0)));
	const assets::RenderContext		   ctx{scene.executor, scene.processed, &scene.actors, scene.camera, 1920, 1080, 16.0F, 1};
	assets::WorldDepthGather		   gather;
	std::vector<assets::DepthSortItem> items;
	gather.gather(ctx, items); // builds each chunk's sorted statics once, as the first frame over a view does

	for (auto _ : state) {
		gather.gather(ctx, items);
		benchmark::DoNotOptimize(items.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_WorldDepthGather_SteadyView)->Arg(kMinEntities)->Arg(kMidEntities)->Arg(kMaxEntities);
