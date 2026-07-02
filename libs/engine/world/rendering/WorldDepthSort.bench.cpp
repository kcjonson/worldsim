// Per-frame CPU cost of the world 2.5D depth stream. Two things are measured:
//
//  1. BM_WorldDepthSort_GatherAndSort - the merge + stable_sort baseline (mirrors
//     ZSort.bench.cpp): derive an anchorY, push a light {anchorY, ptr, animated}
//     item, stable_sort ascending. This is the part that scales with visible count.
//
//  2. The gather itself, old vs new, to quantify the perf-hardening change that
//     moved per-entity classification out of the frame. The OLD gather walked every
//     visible static doing string-keyed lookups (groundcover set -> template map ->
//     extent map) before it could even decide to skip short flora; the NEW gather
//     reads a precomputed anchorY + isTallOccluder off each PlacedEntity. Both feed
//     the same stable_sort, so the delta is purely the per-entity lookup work the
//     baked/placement path now eliminates. Contrasted at 1k / 10k / 34k statics.

#include "world/rendering/WorldDepthSort.h"

#include "assets/placement/SpatialIndex.h"

#include <vector/Types.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

	using engine::assets::PlacedEntity;
	using engine::world::computeAnchorY;
	using engine::world::computeStaticDepthAttribs;
	using engine::world::DepthSortItem;
	using engine::world::kShortFloraMaxHeight;
	using engine::world::MeshYExtent;
	using engine::world::meshYExtent;
	using engine::world::sortByAnchorY;
	using engine::world::StaticDepthAttribs;

	// Representative visible-entity counts: a normal scene, a busy one, and the
	// observed dense zoom-out (~34k).
	constexpr int kMinEntities = 1000;
	constexpr int kMidEntities = 10000;
	constexpr int kMaxEntities = 34000;

	std::vector<PlacedEntity> makeEntities(int n) {
		std::vector<PlacedEntity>			  entities(static_cast<size_t>(n));
		std::mt19937						  rng(1234567U);
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

	// A handful of template "defs" with distinct Y extents: two tall occluders, one
	// short flora, one groundcover, the mix a dense zoomed-out scene actually holds.
	struct TemplateDef {
		std::string				  name;
		renderer::TessellatedMesh mesh;
		bool					  groundcover = false;
	};

	renderer::TessellatedMesh makeTri(float minY, float maxY) {
		renderer::TessellatedMesh m;
		m.vertices.emplace_back(-0.5F, minY);
		m.vertices.emplace_back(0.5F, minY);
		m.vertices.emplace_back(0.0F, maxY);
		return m;
	}

	// A scene wiring the entities to the old gather's per-entity lookup tables:
	// name -> template mesh, mesh -> Y extent, and the groundcover name set. The new
	// gather needs none of these; it reads the precomputed fields on each entity.
	struct Scene {
		std::vector<TemplateDef>														 templates;
		std::vector<PlacedEntity>														 entities;
		std::unordered_map<std::string, const renderer::TessellatedMesh*>				 templateByName;
		std::unordered_map<const renderer::TessellatedMesh*, MeshYExtent>				 extentByMesh;
		std::unordered_set<std::string>													 groundcoverNames;
	};

	Scene makeScene(int n) {
		Scene s;
		s.templates.push_back({"Tree", makeTri(-3.0F, 0.5F), false});			  // tall
		s.templates.push_back({"Bush", makeTri(-1.4F, 0.2F), false});			  // tall
		s.templates.push_back({"Grass", makeTri(-0.3F, 0.1F), false});			  // short flora
		s.templates.push_back({"Groundcover_Grass", makeTri(-0.2F, 0.05F), true}); // groundcover
		for (auto& t : s.templates) {
			s.templateByName[t.name] = &t.mesh;
			s.extentByMesh[&t.mesh] = meshYExtent(&t.mesh);
			if (t.groundcover) {
				s.groundcoverNames.insert(t.name);
			}
		}

		s.entities.resize(static_cast<size_t>(n));
		std::mt19937						  rng(1234567U);
		std::uniform_real_distribution<float> posDist(0.0F, 2000.0F);
		std::uniform_int_distribution<int>	  tmplDist(0, static_cast<int>(s.templates.size()) - 1);
		for (auto& e : s.entities) {
			const auto& t = s.templates[static_cast<size_t>(tmplDist(rng))];
			e.defName = t.name;
			e.position = {posDist(rng), posDist(rng)};
			e.scale = 1.0F;
			// Precompute exactly what PlacementExecutor now stamps at placement time.
			const StaticDepthAttribs d = computeStaticDepthAttribs(e.position.y, s.extentByMesh.at(&t.mesh), e.scale, t.groundcover);
			e.anchorY = d.anchorY;
			e.isTallOccluder = d.isTallOccluder;
		}
		return s;
	}

} // namespace

static void BM_WorldDepthSort_GatherAndSort(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<PlacedEntity>  entities = makeEntities(n);
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

// NEW gather: read the precomputed anchorY + isTallOccluder, skip non-tall, push, sort.
static void BM_WorldDepthGather_PrecomputedRead(benchmark::State& state) {
	Scene					   scene = makeScene(static_cast<int>(state.range(0)));
	std::vector<DepthSortItem> items;
	items.reserve(scene.entities.size());

	for (auto _ : state) {
		items.clear();
		for (const auto& e : scene.entities) {
			if (!e.isTallOccluder) {
				continue;
			}
			items.push_back({e.anchorY, &e, false});
		}
		sortByAnchorY(items);
		benchmark::DoNotOptimize(items.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(scene.entities.size()));
}
BENCHMARK(BM_WorldDepthGather_PrecomputedRead)->Arg(kMinEntities)->Arg(kMidEntities)->Arg(kMaxEntities);

// OLD gather: per-entity string-keyed lookups (groundcover set -> template map ->
// extent map) + height calc before it can skip short flora, then anchorY + push + sort.
static void BM_WorldDepthGather_PerEntityLookup(benchmark::State& state) {
	Scene					   scene = makeScene(static_cast<int>(state.range(0)));
	std::vector<DepthSortItem> items;
	items.reserve(scene.entities.size());

	for (auto _ : state) {
		items.clear();
		for (const auto& e : scene.entities) {
			if (scene.groundcoverNames.count(e.defName) != 0) {
				continue;
			}
			auto it = scene.templateByName.find(e.defName);
			if (it == scene.templateByName.end() || it->second == nullptr) {
				continue;
			}
			const renderer::TessellatedMesh* mesh = it->second;
			const MeshYExtent				 ext = scene.extentByMesh.at(mesh);
			const float						 worldHeight = (ext.maxY - ext.minY) * e.scale;
			if (worldHeight < kShortFloraMaxHeight) {
				continue;
			}
			items.push_back({computeAnchorY(e.position.y, ext.maxY, e.scale), &e, false});
		}
		sortByAnchorY(items);
		benchmark::DoNotOptimize(items.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(scene.entities.size()));
}
BENCHMARK(BM_WorldDepthGather_PerEntityLookup)->Arg(kMinEntities)->Arg(kMidEntities)->Arg(kMaxEntities);
