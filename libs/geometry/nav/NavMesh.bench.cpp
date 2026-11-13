// Cost of buildNavMesh on real game inputs: the dumped quickstart regions as they
// are, the same regions with every placed obstacle removed, an obstacle-density sweep and an
// area sweep over controlDump, and a split of the arrangement and constrained
// Delaunay stages from the rest of the build.

#include "nav/NavMesh.h"
#include "nav/NavMeshDumpRings.test.h"
#include "nav/NavMeshRealRings.test.h"

#include "arrangement/Arrangement.h"
#include "triangulation/ConstrainedDelaunay.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

	using geometry::InputSegment;
	using geometry::Vec2i64;
	using geometry::nav::buildNavMesh;
	using geometry::nav::kNoOpening;
	using geometry::nav::NavInputPolygon;
	using geometry::nav::NavMesh;
	using geometry::nav::NavMeshInput;

	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenancePlaced	 = -2;
	constexpr std::int64_t kProvenanceBorder = -3;

	enum class Fixture { ManyObstacles, Control, RealRings };

	NavMeshInput dumpInput(const std::vector<geometry::nav::testdata::DumpRing>& rings) {
		NavMeshInput in;
		for (const geometry::nav::testdata::DumpRing& r : rings) {
			in.polygons.push_back(NavInputPolygon{r.ring, r.blocked, r.provenanceId, kNoOpening, r.holeCapable});
		}
		return in;
	}

	NavMeshInput realRingsInput() {
		namespace td = geometry::nav::testdata;
		NavMeshInput in;
		in.polygons.push_back(NavInputPolygon{td::realBorderRing(), false, kProvenanceBorder});
		in.polygons.push_back(NavInputPolygon{td::realWaterRing(), true, kProvenanceWater, kNoOpening, true});
		for (const std::vector<Vec2i64>& obstacle : td::realObstacleRings()) {
			in.polygons.push_back(NavInputPolygon{obstacle, true, kProvenancePlaced});
		}
		return in;
	}

	NavMeshInput fixtureInput(Fixture f) {
		switch (f) {
			case Fixture::ManyObstacles:
				return dumpInput(geometry::nav::testdata::manyObstaclesDump());
			case Fixture::Control:
				return dumpInput(geometry::nav::testdata::controlDump());
			case Fixture::RealRings:
				return realRingsInput();
		}
		return {};
	}

	std::int64_t obstacleCount(const NavMeshInput& in) {
		return std::count_if(in.polygons.begin(), in.polygons.end(), [](const NavInputPolygon& p) {
			return p.provenanceId == kProvenancePlaced;
		});
	}

	// Border and water kept, then only the first `keep` placed obstacle rings in input order.
	NavMeshInput keepObstacles(const NavMeshInput& in, std::int64_t keep) {
		NavMeshInput out;
		std::int64_t kept = 0;
		for (const NavInputPolygon& p : in.polygons) {
			if (p.provenanceId == kProvenancePlaced) {
				if (kept >= keep) {
					continue;
				}
				++kept;
			}
			out.polygons.push_back(p);
		}
		return out;
	}

	// A k x k mosaic of the input: every non-border ring copied into each tile,
	// translated by the border's side length, under one border spanning the mosaic.
	NavMeshInput tileInput(const NavMeshInput& in, std::int64_t k) {
		const NavInputPolygon* border = nullptr;
		for (const NavInputPolygon& p : in.polygons) {
			if (!p.blocked) {
				border = &p;
				break;
			}
		}
		Vec2i64 lo = border->ring.front();
		Vec2i64 hi = border->ring.front();
		for (const Vec2i64& v : border->ring) {
			lo = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
			hi = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
		}
		const std::int64_t side = hi.x - lo.x;

		NavMeshInput out;
		out.polygons.push_back(NavInputPolygon{
			{{lo.x, lo.y}, {lo.x + k * side, lo.y}, {lo.x + k * side, lo.y + k * side}, {lo.x, lo.y + k * side}},
			false,
			border->provenanceId});
		for (std::int64_t j = 0; j < k; ++j) {
			for (std::int64_t i = 0; i < k; ++i) {
				const Vec2i64 offset{i * side, j * side};
				for (const NavInputPolygon& p : in.polygons) {
					if (&p == border) {
						continue;
					}
					NavInputPolygon moved = p;
					for (Vec2i64& v : moved.ring) {
						v = v + offset;
					}
					out.polygons.push_back(std::move(moved));
				}
			}
		}
		return out;
	}

	// buildNavMesh's step 1 input: every ring edge tagged with its ring's provenance.
	std::vector<InputSegment> navSegments(const NavMeshInput& in) {
		std::vector<InputSegment> segments;
		for (const NavInputPolygon& poly : in.polygons) {
			const std::size_t n = poly.ring.size();
			if (n < 3) {
				continue;
			}
			for (std::size_t i = 0; i < n; ++i) {
				segments.push_back({poly.ring[i], poly.ring[(i + 1) % n], poly.provenanceId});
			}
		}
		return segments;
	}

	void setInputCounters(benchmark::State& state, const NavMeshInput& in) {
		state.counters["rings"] = static_cast<double>(in.polygons.size());
		state.counters["obstacles"] = static_cast<double>(obstacleCount(in));
	}

	void runBuild(benchmark::State& state, const NavMeshInput& in) {
		std::size_t triangles = 0;
		for (auto _ : state) {
			NavMesh mesh = buildNavMesh(in);
			triangles	 = mesh.triangles.size();
			benchmark::DoNotOptimize(mesh);
		}
		setInputCounters(state, in);
		state.counters["tris"] = static_cast<double>(triangles);
	}

	void BM_BuildNavMesh(benchmark::State& state, Fixture f) {
		runBuild(state, fixtureInput(f));
	}

	void BM_BuildNavMeshTerrainOnly(benchmark::State& state, Fixture f) {
		runBuild(state, keepObstacles(fixtureInput(f), 0));
	}

	void BM_ControlObstacleDensity(benchmark::State& state) {
		runBuild(state, keepObstacles(fixtureInput(Fixture::Control), state.range(0)));
	}

	void densityArgs(benchmark::internal::Benchmark* b) {
		for (const std::int64_t n : {0, 64, 128, 256, 512}) {
			b->Arg(n);
		}
		b->Arg(obstacleCount(fixtureInput(Fixture::Control)));
	}

	void BM_ControlAreaTiled(benchmark::State& state) {
		runBuild(state, tileInput(fixtureInput(Fixture::Control), state.range(0)));
	}

	void BM_ControlStageArrangement(benchmark::State& state, bool terrainOnly) {
		const NavMeshInput				in		 = keepObstacles(fixtureInput(Fixture::Control), terrainOnly ? 0 : INT64_MAX);
		const std::vector<InputSegment> segments = navSegments(in);
		std::size_t						vertices = 0;
		for (auto _ : state) {
			geometry::Arrangement arrangement = geometry::buildArrangement(segments);
			vertices						  = arrangement.vertices.size();
			benchmark::DoNotOptimize(arrangement);
		}
		setInputCounters(state, in);
		state.counters["verts"] = static_cast<double>(vertices);
	}

	void BM_ControlStageArrangementCdt(benchmark::State& state, bool terrainOnly) {
		const NavMeshInput				in		 = keepObstacles(fixtureInput(Fixture::Control), terrainOnly ? 0 : INT64_MAX);
		const std::vector<InputSegment> segments = navSegments(in);
		std::size_t						triangles = 0;
		for (auto _ : state) {
			const geometry::Arrangement				  arrangement = geometry::buildArrangement(segments);
			std::vector<std::array<std::uint32_t, 2>> constraints;
			constraints.reserve(arrangement.edges.size());
			for (const geometry::ArrangementEdge& ae : arrangement.edges) {
				constraints.push_back({static_cast<std::uint32_t>(ae.from), static_cast<std::uint32_t>(ae.to)});
			}
			geometry::ConstrainedDelaunay cdt = geometry::buildConstrainedDelaunay(arrangement.vertices, constraints);
			triangles						  = cdt.triangles.size();
			benchmark::DoNotOptimize(cdt);
		}
		setInputCounters(state, in);
		state.counters["cdtTris"] = static_cast<double>(triangles);
	}

} // namespace

BENCHMARK_CAPTURE(BM_BuildNavMesh, manyObstacles, Fixture::ManyObstacles)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_BuildNavMesh, control, Fixture::Control)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_BuildNavMesh, realRings, Fixture::RealRings)->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_BuildNavMeshTerrainOnly, manyObstacles, Fixture::ManyObstacles)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_BuildNavMeshTerrainOnly, control, Fixture::Control)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_BuildNavMeshTerrainOnly, realRings, Fixture::RealRings)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_ControlObstacleDensity)->Apply(densityArgs)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_ControlAreaTiled)->DenseRange(1, 4)->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_ControlStageArrangement, full, false)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ControlStageArrangement, terrainOnly, true)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ControlStageArrangementCdt, full, false)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ControlStageArrangementCdt, terrainOnly, true)->Unit(benchmark::kMillisecond);
