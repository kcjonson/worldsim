#include "Tessellator.h"
#include "Types.h"
#include "utils/Log.h"

#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

namespace {

	using renderer::TessellatedMesh;
	using renderer::Tessellator;
	using renderer::VectorPath;

	constexpr float kPi = 3.14159265358979F;

	// Regular convex n-gon: exercises the convex-fan fast path.
	VectorPath makeRegularNGon(int n, float radius = 100.0F) {
		VectorPath p;
		p.isClosed = true;
		p.vertices.reserve(static_cast<size_t>(n));
		for (int i = 0; i < n; ++i) {
			const float a = (static_cast<float>(i) / static_cast<float>(n)) * 2.0F * kPi;
			p.vertices.push_back({radius * std::cos(a), radius * std::sin(a)});
		}
		return p;
	}

	// Wobbly star-shaped-from-center loop: concave but simple (non-self-intersecting),
	// like an organic flora-canopy outline. Exercises the ear-clipping path (O(n^2)).
	VectorPath makeWobblyBlob(int n, int lobes = 7, float base = 100.0F, float amp = 0.32F) {
		VectorPath p;
		p.isClosed = true;
		p.vertices.reserve(static_cast<size_t>(n));
		for (int i = 0; i < n; ++i) {
			const float a = (static_cast<float>(i) / static_cast<float>(n)) * 2.0F * kPi;
			const float r = base * (1.0F + (amp * std::sin(static_cast<float>(lobes) * a)));
			p.vertices.push_back({r * std::cos(a), r * std::sin(a)});
		}
		return p;
	}

	void runTess(benchmark::State& state, const VectorPath& path) {
		// Silence per-call tessellator debug logging so it doesn't dominate the timing.
		foundation::Logger::setLevel(foundation::LogCategory::Renderer, foundation::LogLevel::Warning);

		Tessellator tess;
		size_t		tris = 0;
		for (auto _ : state) {
			TessellatedMesh mesh;
			const bool		ok = tess.Tessellate(path, mesh);
			benchmark::DoNotOptimize(ok);
			benchmark::DoNotOptimize(mesh.indices.data());
			tris = mesh.getTriangleCount();
		}
		state.counters["tris"] = static_cast<double>(tris);
		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(path.vertices.size()));
	}

} // namespace

// Convex n-gon (fan fast path).
static void BM_TessConvexNGon(benchmark::State& state) {
	const VectorPath path = makeRegularNGon(static_cast<int>(state.range(0)));
	runTess(state, path);
}
BENCHMARK(BM_TessConvexNGon)->RangeMultiplier(2)->Range(8, 512);

// Concave organic blob (ear-clipping path): the realistic baseline; watch O(n^2) at high counts.
static void BM_TessConcaveBlob(benchmark::State& state) {
	const VectorPath path = makeWobblyBlob(static_cast<int>(state.range(0)));
	runTess(state, path);
}
BENCHMARK(BM_TessConcaveBlob)->RangeMultiplier(2)->Range(16, 512);

// 5-point star outline (10 verts), concave non-self-intersecting.
static void BM_TessStar5(benchmark::State& state) {
	VectorPath p;
	p.isClosed = true;
	for (int i = 0; i < 10; ++i) {
		const float a = ((static_cast<float>(i) / 10.0F) * 2.0F * kPi) - (kPi / 2.0F);
		const float r = (i % 2 == 0) ? 100.0F : 42.0F;
		p.vertices.push_back({r * std::cos(a), r * std::sin(a)});
	}
	runTess(state, p);
}
BENCHMARK(BM_TessStar5);
