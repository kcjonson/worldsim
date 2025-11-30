// Clipping System Performance Benchmarks
//
// These benchmarks measure the overhead of the shader-based clipping system.
// The fast path (ClipRect with ClipMode::Inside) uses per-vertex clip bounds
// that are evaluated in the fragment shader, preserving full batching.

#include "graphics/ClipTypes.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <stack>

using namespace Foundation;

// ============================================================================
// Helper Functions (copied from Primitives.cpp for isolated benchmarking)
// ============================================================================

// Compute Vec4 bounds from ClipSettings
static Vec4 ComputeClipBounds(const ClipSettings& settings) {
	if (const auto* clipRect = std::get_if<ClipRect>(&settings.shape)) {
		if (clipRect->bounds.has_value()) {
			const auto& rect = clipRect->bounds.value();
			return Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
		}
		return Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	if (const auto* rr = std::get_if<ClipRoundedRect>(&settings.shape)) {
		if (rr->bounds.has_value()) {
			const auto& rect = rr->bounds.value();
			return Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
		}
	}

	if (const auto* circle = std::get_if<ClipCircle>(&settings.shape)) {
		float minX = circle->center.x - circle->radius;
		float minY = circle->center.y - circle->radius;
		float maxX = circle->center.x + circle->radius;
		float maxY = circle->center.y + circle->radius;
		return Vec4(minX, minY, maxX, maxY);
	}

	if (const auto* path = std::get_if<ClipPath>(&settings.shape)) {
		if (!path->vertices.empty()) {
			float minX = path->vertices[0].x;
			float minY = path->vertices[0].y;
			float maxX = minX;
			float maxY = minY;
			for (const auto& v : path->vertices) {
				minX = std::min(minX, v.x);
				minY = std::min(minY, v.y);
				maxX = std::max(maxX, v.x);
				maxY = std::max(maxY, v.y);
			}
			return Vec4(minX, minY, maxX, maxY);
		}
	}

	return Vec4(0.0F, 0.0F, 0.0F, 0.0F);
}

// Intersect two clip bounds
static Vec4 IntersectClipBounds(const Vec4& a, const Vec4& b) {
	bool aEmpty = (a.z <= a.x || a.w <= a.y);
	bool bEmpty = (b.z <= b.x || b.w <= b.y);

	if (aEmpty)
		return b;
	if (bEmpty)
		return a;

	float minX = std::max(a.x, b.x);
	float minY = std::max(a.y, b.y);
	float maxX = std::min(a.z, b.z);
	float maxY = std::min(a.w, b.w);

	if (maxX <= minX || maxY <= minY) {
		return Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	return Vec4(minX, minY, maxX, maxY);
}

// ============================================================================
// Clip Stack Simulation (mirrors Primitives.cpp implementation)
// ============================================================================

struct ClipStackEntry {
	ClipSettings settings;
	Vec4		 bounds;
};

// ============================================================================
// Benchmark: ClipRect Bounds Computation
// ============================================================================

static void BM_ComputeClipBoundsRect(benchmark::State& state) {
	ClipSettings settings;
	settings.shape = ClipRect{.bounds = Rect(100.0F, 100.0F, 400.0F, 300.0F)};

	for (auto _ : state) {
		Vec4 bounds = ComputeClipBounds(settings);
		benchmark::DoNotOptimize(bounds);
	}
}
BENCHMARK(BM_ComputeClipBoundsRect);

// ============================================================================
// Benchmark: ClipCircle Bounds Computation
// ============================================================================

static void BM_ComputeClipBoundsCircle(benchmark::State& state) {
	ClipSettings settings;
	settings.shape = ClipCircle{.center = Vec2(300.0F, 200.0F), .radius = 150.0F};

	for (auto _ : state) {
		Vec4 bounds = ComputeClipBounds(settings);
		benchmark::DoNotOptimize(bounds);
	}
}
BENCHMARK(BM_ComputeClipBoundsCircle);

// ============================================================================
// Benchmark: ClipPath Bounds Computation (8-vertex polygon)
// ============================================================================

static void BM_ComputeClipBoundsPath(benchmark::State& state) {
	ClipSettings settings;
	ClipPath	 path;
	// Octagon-like shape
	path.vertices = {
		Vec2(150.0F, 100.0F),
		Vec2(250.0F, 100.0F),
		Vec2(300.0F, 150.0F),
		Vec2(300.0F, 250.0F),
		Vec2(250.0F, 300.0F),
		Vec2(150.0F, 300.0F),
		Vec2(100.0F, 250.0F),
		Vec2(100.0F, 150.0F)
	};
	settings.shape = path;

	for (auto _ : state) {
		Vec4 bounds = ComputeClipBounds(settings);
		benchmark::DoNotOptimize(bounds);
	}
}
BENCHMARK(BM_ComputeClipBoundsPath);

// ============================================================================
// Benchmark: Clip Bounds Intersection
// ============================================================================

static void BM_IntersectClipBounds(benchmark::State& state) {
	Vec4 a(100.0F, 100.0F, 500.0F, 400.0F);
	Vec4 b(200.0F, 150.0F, 600.0F, 350.0F);

	for (auto _ : state) {
		Vec4 result = IntersectClipBounds(a, b);
		benchmark::DoNotOptimize(result);
	}
}
BENCHMARK(BM_IntersectClipBounds);

// ============================================================================
// Benchmark: Push/Pop Single Clip Region
// ============================================================================

static void BM_PushPopSingleClip(benchmark::State& state) {
	std::stack<ClipStackEntry> clipStack;
	ClipSettings			   settings;
	settings.shape = ClipRect{.bounds = Rect(100.0F, 100.0F, 400.0F, 300.0F)};

	for (auto _ : state) {
		// Push
		Vec4 bounds = ComputeClipBounds(settings);
		clipStack.push({settings, bounds});

		// Pop
		clipStack.pop();

		benchmark::DoNotOptimize(clipStack.empty());
	}
}
BENCHMARK(BM_PushPopSingleClip);

// ============================================================================
// Benchmark: Nested Clip Regions (common UI pattern)
// ============================================================================

static void BM_NestedClipRegions(benchmark::State& state) {
	int depth = state.range(0);

	for (auto _ : state) {
		std::stack<ClipStackEntry> clipStack;

		// Push nested clips (simulating UI hierarchy: Window > Panel > Card > Content)
		for (int i = 0; i < depth; ++i) {
			ClipSettings settings;
			float		 margin = static_cast<float>(i * 20);
			settings.shape = ClipRect{.bounds = Rect(margin, margin, 800.0F - 2.0F * margin, 600.0F - 2.0F * margin)};

			Vec4 bounds = ComputeClipBounds(settings);
			if (!clipStack.empty()) {
				bounds = IntersectClipBounds(clipStack.top().bounds, bounds);
			}
			clipStack.push({settings, bounds});
		}

		// Pop all clips
		while (!clipStack.empty()) {
			clipStack.pop();
		}

		benchmark::DoNotOptimize(clipStack.empty());
	}

	state.SetItemsProcessed(state.iterations() * depth * 2); // push + pop operations
}
BENCHMARK(BM_NestedClipRegions)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

// ============================================================================
// Benchmark: Rapid Clip Switching (scrollable list pattern)
// ============================================================================

static void BM_RapidClipSwitching(benchmark::State& state) {
	int itemCount = state.range(0);

	// Pre-create clip settings for each item
	std::vector<ClipSettings> itemClips;
	itemClips.reserve(itemCount);
	for (int i = 0; i < itemCount; ++i) {
		ClipSettings settings;
		settings.shape = ClipRect{.bounds = Rect(0.0F, static_cast<float>(i * 50), 400.0F, 50.0F)};
		itemClips.push_back(settings);
	}

	// Parent container clip
	ClipSettings containerClip;
	containerClip.shape = ClipRect{.bounds = Rect(0.0F, 0.0F, 400.0F, 300.0F)};

	for (auto _ : state) {
		std::stack<ClipStackEntry> clipStack;

		// Push container clip
		Vec4 containerBounds = ComputeClipBounds(containerClip);
		clipStack.push({containerClip, containerBounds});

		// Simulate rendering each list item (push item clip, "render", pop)
		for (int i = 0; i < itemCount; ++i) {
			// Push item clip
			Vec4 itemBounds = ComputeClipBounds(itemClips[i]); // NOLINT
			itemBounds = IntersectClipBounds(containerBounds, itemBounds);
			clipStack.push({itemClips[i], itemBounds}); // NOLINT

			// Simulate "rendering" by preventing optimization
			benchmark::DoNotOptimize(itemBounds);

			// Pop item clip
			clipStack.pop();
		}

		// Pop container clip
		clipStack.pop();
	}

	state.SetItemsProcessed(state.iterations() * itemCount);
}
BENCHMARK(BM_RapidClipSwitching)->Arg(10)->Arg(50)->Arg(100)->Arg(200);

// ============================================================================
// Benchmark: Clip Bounds Check (fragment shader simulation)
// ============================================================================

static void BM_FragmentClipCheck(benchmark::State& state) {
	int fragmentCount = state.range(0);

	Vec4 clipBounds(100.0F, 100.0F, 500.0F, 400.0F);

	// Generate test fragment positions (some inside, some outside)
	std::vector<Vec2> fragments;
	fragments.reserve(fragmentCount);
	for (int i = 0; i < fragmentCount; ++i) {
		float x = static_cast<float>(i % 640);
		float y = static_cast<float>((i / 640) % 480);
		fragments.emplace_back(x, y);
	}

	for (auto _ : state) {
		int visibleCount = 0;
		for (const auto& frag : fragments) {
			// This mirrors the fragment shader clip test
			bool inside = (frag.x >= clipBounds.x && frag.x <= clipBounds.z && frag.y >= clipBounds.y && frag.y <= clipBounds.w);
			if (inside) {
				++visibleCount;
			}
		}
		benchmark::DoNotOptimize(visibleCount);
	}

	state.SetItemsProcessed(state.iterations() * fragmentCount);
}
BENCHMARK(BM_FragmentClipCheck)->Arg(1000)->Arg(10000)->Arg(100000);
