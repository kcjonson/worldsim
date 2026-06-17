// Z-order draw-queue performance benchmarks.
//
// Measures the CPU cost that the per-draw-call group queue + z-sort adds to the
// batch flush, in isolation (no GL; the actual glDrawElements work is unchanged
// by reordering). Three cases at 10k / 20k draw calls:
//   - Baseline: current behaviour (build indices, upload as-is, no groups/sort).
//   - FastPath: new common case (build one group record per draw; no explicit z,
//     so no sort and indices are used as-is).
//   - Sorted:   new worst-ish case (groups built; ~2% carry an explicit z, so the
//     groups are stable-sorted and the emit-order index list is rebuilt).
// The Baseline-vs-FastPath gap is the always-on cost; Baseline-vs-Sorted is the
// cost only paid on frames where a popup (explicit z) is open.

#include <algorithm>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

	struct Group {
		std::uint32_t indexStart;
		std::uint32_t indexCount;
		float		  zIndex;
	};

	inline void pushQuad(std::vector<std::uint32_t>& indices, std::uint32_t base) {
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

} // namespace

// Current behaviour: indices accumulated in submission order, uploaded as-is.
static void BM_ZQueue_Baseline(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	indices.reserve(static_cast<size_t>(n) * 6);

	for (auto _ : state) {
		indices.clear();
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			pushQuad(indices, base);
			base += 4;
		}
		benchmark::DoNotOptimize(indices.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_Baseline)->Arg(10000)->Arg(20000);

// New, common case: one POD group per draw; no explicit z -> fast path, no sort,
// indices used as-is. Delta vs Baseline = the group records.
static void BM_ZQueue_FastPath(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	std::vector<Group>		   groups;
	indices.reserve(static_cast<size_t>(n) * 6);
	groups.reserve(static_cast<size_t>(n));

	for (auto _ : state) {
		indices.clear();
		groups.clear();
		bool		  anyExplicitZ = false;
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			const auto start = static_cast<std::uint32_t>(indices.size());
			pushQuad(indices, base);
			groups.push_back({start, 6, 0.0F});
			base += 4;
		}
		// Flush fast path: nothing explicit -> upload indices unchanged.
		if (anyExplicitZ) {
			std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.zIndex < b.zIndex; });
		}
		benchmark::DoNotOptimize(indices.data());
		benchmark::DoNotOptimize(groups.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_FastPath)->Arg(10000)->Arg(20000);

// New, popup-open case: ~2% of draws carry an explicit z, so groups are
// stable-sorted and the emit-order index list is rebuilt.
static void BM_ZQueue_Sorted(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	std::vector<Group>		   groups;
	std::vector<std::uint32_t> emit;
	indices.reserve(static_cast<size_t>(n) * 6);
	groups.reserve(static_cast<size_t>(n));
	emit.reserve(static_cast<size_t>(n) * 6);

	for (auto _ : state) {
		indices.clear();
		groups.clear();
		emit.clear();
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			const auto start = static_cast<std::uint32_t>(indices.size());
			pushQuad(indices, base);
			const float z = (i % 50 == 0) ? 1000.0F : 0.0F; // ~2% explicit
			groups.push_back({start, 6, z});
			base += 4;
		}
		std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.zIndex < b.zIndex; });
		for (const Group& g : groups) {
			for (std::uint32_t k = 0; k < g.indexCount; ++k) {
				emit.push_back(indices[g.indexStart + k]);
			}
		}
		benchmark::DoNotOptimize(emit.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_Sorted)->Arg(10000)->Arg(20000);
