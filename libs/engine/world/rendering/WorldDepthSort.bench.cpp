// Per-frame merge + stable-sort cost for the world 2.5D depth stream, mirroring
// ZSort.bench.cpp. Measures the CPU work that runs every frame once the visible
// occluders + actors are in hand: derive each entity's anchorY, push a light
// {anchorY, ptr, isAnimated} item, and stable_sort ascending. The spatial-index
// queryRect that feeds this is bounded by frustum culling and is not modelled
// here; the sort is the part that scales with visible-entity count.

#include "world/rendering/WorldDepthSort.h"

#include "assets/placement/SpatialIndex.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

	using engine::assets::PlacedEntity;
	using engine::world::computeAnchorY;
	using engine::world::DepthSortItem;
	using engine::world::sortByAnchorY;

	// Representative visible-entity counts: a normal scene, a busy one, and the
	// observed dense zoom-out (~34k).
	constexpr int kMinEntities = 1000;
	constexpr int kMidEntities = 10000;
	constexpr int kMaxEntities = 34000;

	std::vector<PlacedEntity> makeEntities(int n) {
		std::vector<PlacedEntity> entities(static_cast<size_t>(n));
		std::mt19937			  rng(1234567U);
		std::uniform_real_distribution<float> posDist(0.0F, 2000.0F);
		std::uniform_real_distribution<float> heightDist(0.5F, 4.0F);
		for (auto& e : entities) {
			e.position = {posDist(rng), posDist(rng)};
			e.scale = 1.0F;
			// Store the mesh max-local-Y in rotation just to carry a per-entity
			// value into the timed loop without a mesh; anchorY math is identical.
			e.rotation = heightDist(rng);
		}
		return entities;
	}

} // namespace

static void BM_WorldDepthSort_GatherAndSort(benchmark::State& state) {
	const int				  n = static_cast<int>(state.range(0));
	std::vector<PlacedEntity> entities = makeEntities(n);
	std::vector<DepthSortItem> items;
	items.reserve(static_cast<size_t>(n));

	for (auto _ : state) {
		items.clear();
		int i = 0;
		for (const auto& e : entities) {
			const float anchorY = computeAnchorY(e.position.y, e.rotation, e.scale);
			const bool	animated = (i % 20) == 0; // ~5% actors interleave
			items.push_back({anchorY, &e, animated});
			++i;
		}
		sortByAnchorY(items);
		benchmark::DoNotOptimize(items.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_WorldDepthSort_GatherAndSort)->Arg(kMinEntities)->Arg(kMidEntities)->Arg(kMaxEntities);
