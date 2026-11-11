// Cost of a PatchableMesh patch on real game inputs: one tree in and out, a footprint
// moving over open ground and through trees, trees on the shoreline, rings across a
// long bounds edge, detail entering an area, a first build, and point location.
//
// Every patch row times only the applyPatch call (plus refreshWidths in the _refreshed
// rows), with a steady clock around it and UseManualTime, so the untimed setup between
// patches (taking a tree out before timing its add, building the mesh) never reaches
// the reported time.

#include "nav/NavMesh.h"
#include "nav/NavMeshDumpRings.test.h"
#include "nav/NavMeshRealRings.test.h"
#include "nav/PatchableMesh.h"
#include "nav/PathQuery.h"

#include "predicates/Predicates.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using namespace geometry::nav;

	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenanceTree	 = -2;
	constexpr std::int64_t kProvenanceBorder = -3;

	enum class Fixture { ManyTrees, Control, RealRings };

	struct Rings {
		std::vector<NavInputPolygon> base; // bounds and water
		std::vector<NavInputPolygon> trees;
	};

	Rings fixtureRings(Fixture f) {
		namespace td = geometry::nav::testdata;
		Rings rings;
		if (f == Fixture::RealRings) {
			rings.base.push_back(NavInputPolygon{td::realBorderRing(), false, kProvenanceBorder});
			rings.base.push_back(NavInputPolygon{td::realWaterRing(), true, kProvenanceWater, kNoOpening, true});
			for (const std::vector<Vec2i64>& tree : td::realTreeRings()) {
				rings.trees.push_back(NavInputPolygon{tree, true, kProvenanceTree});
			}
			return rings;
		}
		for (const td::DumpRing& r : f == Fixture::ManyTrees ? td::manyTreesDump() : td::controlDump()) {
			NavInputPolygon poly{r.ring, r.blocked, r.provenanceId, kNoOpening, r.holeCapable};
			(r.provenanceId == kProvenanceTree ? rings.trees : rings.base).push_back(std::move(poly));
		}
		return rings;
	}

	void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& lo, Vec2i64& hi) {
		lo = ring.front();
		hi = ring.front();
		for (const Vec2i64& p : ring) {
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
		}
	}

	bool boxesNear(const std::vector<Vec2i64>& a, const std::vector<Vec2i64>& b, std::int64_t margin) {
		Vec2i64 aLo;
		Vec2i64 aHi;
		Vec2i64 bLo;
		Vec2i64 bHi;
		boxOf(a, aLo, aHi);
		boxOf(b, bLo, bHi);
		return aLo.x - margin <= bHi.x && bLo.x <= aHi.x + margin && aLo.y - margin <= bHi.y && bLo.y <= aHi.y + margin;
	}

	bool ringsCross(const std::vector<Vec2i64>& a, const std::vector<Vec2i64>& b) {
		for (std::size_t i = 0; i < a.size(); ++i) {
			for (std::size_t j = 0; j < b.size(); ++j) {
				if (geometry::intersectSegments(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()]).relation !=
					geometry::SegmentRelation::Disjoint) {
					return true;
				}
			}
		}
		return false;
	}

	std::vector<Vec2i64> translated(std::vector<Vec2i64> ring, Vec2i64 by) {
		for (Vec2i64& p : ring) {
			p = p + by;
		}
		return ring;
	}

	std::vector<Vec2i64> rect(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	// A mover footprint: an octagon about 0.6 m across.
	std::vector<Vec2i64> footprint(Vec2i64 c) {
		return {{c.x + 300, c.y - 124}, {c.x + 300, c.y + 124}, {c.x + 124, c.y + 300}, {c.x - 124, c.y + 300},
				{c.x - 300, c.y + 124}, {c.x - 300, c.y - 124}, {c.x - 124, c.y - 300}, {c.x + 124, c.y - 300}};
	}

	template <typename Work> double secondsOf(Work work) {
		const auto start = std::chrono::steady_clock::now();
		work();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	struct PatchTally {
		double freed   = 0;
		double created = 0;
		double passes  = 0;
		double patches = 0;

		void add(const PatchReport& r) {
			freed += static_cast<double>(r.freedTriangles.size());
			created += static_cast<double>(r.newTriangles.size());
			passes += r.passes;
			patches += 1;
		}

		void report(benchmark::State& state) const {
			const double n			= std::max(1.0, patches);
			state.counters["freed"]	  = freed / n;
			state.counters["created"] = created / n;
			state.counters["passes"]  = passes / n;
		}
	};

	// Widths lag behind a patch until refreshWidths. A row either times the patch alone
	// and refreshes untimed, or times both, which is what keeping widths eager would cost.
	enum class Refresh { Untimed, Timed };

	PatchReport untimedPatch(PatchableMesh& mesh, const PatchEdits& edits) {
		PatchReport report = mesh.applyPatch(edits);
		mesh.refreshWidths();
		return report;
	}

	PatchReport timedPatch(benchmark::State& state, PatchableMesh& mesh, const PatchEdits& edits, Refresh refresh = Refresh::Untimed) {
		PatchReport report;
		state.SetIterationTime(secondsOf([&] {
			report = mesh.applyPatch(edits);
			if (refresh == Refresh::Timed) {
				mesh.refreshWidths();
			}
		}));
		mesh.refreshWidths();
		return report;
	}

	PatchEdits adding(std::vector<NavInputPolygon> rings) {
		PatchEdits edits;
		edits.adds = std::move(rings);
		return edits;
	}

	PatchEdits removing(RingId ring) {
		PatchEdits edits;
		edits.removes.push_back(ring);
		return edits;
	}

	PatchEdits moving(RingId ring, std::vector<Vec2i64> outline) {
		PatchEdits edits;
		edits.moves.push_back({ring, std::move(outline)});
		return edits;
	}

	// The fixture fully loaded, each tree under its current id.
	struct Loaded {
		explicit Loaded(std::int64_t maxBoundsEdgeMm = 0) : mesh({.maxBoundsEdgeMm = maxBoundsEdgeMm}) {}

		Rings						 rings;
		PatchableMesh				 mesh;
		std::vector<RingId>			 treeIds;
		std::vector<NavInputPolygon> all;
	};

	void load(Loaded& l, Rings rings) {
		l.rings = std::move(rings);
		l.all	= l.rings.base;
		l.all.insert(l.all.end(), l.rings.trees.begin(), l.rings.trees.end());
		const PatchReport r = untimedPatch(l.mesh, adding(l.all));
		l.treeIds.assign(r.added.begin() + static_cast<std::ptrdiff_t>(l.rings.base.size()), r.added.end());
	}

	// Trees no other ring comes within 50 mm of.
	std::vector<std::size_t> loneTrees(const Loaded& l) {
		std::vector<std::size_t> out;
		for (std::size_t i = 0; i < l.rings.trees.size(); ++i) {
			bool alone = true;
			for (std::size_t j = 0; j < l.all.size() && alone; ++j) {
				const NavInputPolygon& other = l.all[j];
				if (&other.ring == &l.all[l.rings.base.size() + i].ring) {
					continue;
				}
				alone = other.provenanceId == kProvenanceBorder ? !ringsCross(l.rings.trees[i].ring, other.ring)
																: !boxesNear(l.rings.trees[i].ring, other.ring, 50);
			}
			if (alone) {
				out.push_back(i);
			}
		}
		return out;
	}

	std::vector<std::size_t> shorelineTrees(const Loaded& l) {
		std::vector<std::size_t> out;
		for (std::size_t i = 0; i < l.rings.trees.size(); ++i) {
			for (const NavInputPolygon& base : l.rings.base) {
				if (base.provenanceId == kProvenanceWater && ringsCross(l.rings.trees[i].ring, base.ring)) {
					out.push_back(i);
					break;
				}
			}
		}
		return out;
	}

	// Time putting each chosen tree back after taking it out, or taking it out.
	void cycleTrees(benchmark::State& state, Loaded& l, const std::vector<std::size_t>& chosen, bool timeAdd, Refresh refresh) {
		PatchTally	tally;
		std::size_t next = 0;
		for (auto _ : state) {
			const std::size_t i = chosen[next++ % chosen.size()];
			if (timeAdd) {
				untimedPatch(l.mesh, removing(l.treeIds[i]));
				const PatchReport r = timedPatch(state, l.mesh, adding({l.rings.trees[i]}), refresh);
				l.treeIds[i]		= r.added.front();
				tally.add(r);
			} else {
				tally.add(timedPatch(state, l.mesh, removing(l.treeIds[i]), refresh));
				l.treeIds[i] = untimedPatch(l.mesh, adding({l.rings.trees[i]})).added.front();
			}
		}
		tally.report(state);
		state.counters["trees"] = static_cast<double>(chosen.size());
	}

	void BM_PatchLoneTree(benchmark::State& state, Fixture f, bool add, Refresh refresh) {
		Loaded l;
		load(l, fixtureRings(f));
		cycleTrees(state, l, loneTrees(l), add, refresh);
	}

	void BM_PatchShorelineTree(benchmark::State& state, Fixture f, bool add) {
		Loaded l;
		load(l, fixtureRings(f));
		const std::vector<std::size_t> chosen = shorelineTrees(l);
		if (chosen.empty()) {
			state.SkipWithError("no tree overlaps the shoreline");
			return;
		}
		cycleTrees(state, l, chosen, add, Refresh::Untimed);
	}

	// Footprint positions 0.3 m apart along a horizontal line, forward then back.
	std::vector<Vec2i64> pathAlong(Vec2i64 from, std::int64_t steps) {
		std::vector<Vec2i64> out;
		for (std::int64_t i = 0; i < steps; ++i) {
			out.push_back({from.x + i * 300, from.y});
		}
		for (std::int64_t i = steps - 2; i > 0; --i) {
			out.push_back(out[static_cast<std::size_t>(i)]);
		}
		return out;
	}

	std::int64_t overlapsAlong(const Loaded& l, Vec2i64 from, std::int64_t steps) {
		std::int64_t hits = 0;
		for (std::int64_t i = 0; i < steps; ++i) {
			const std::vector<Vec2i64> fp = footprint({from.x + i * 300, from.y});
			for (const NavInputPolygon& r : l.all) {
				if (r.provenanceId != kProvenanceBorder && boxesNear(fp, r.ring, 0)) {
					++hits;
				}
			}
		}
		return hits;
	}

	// A 20 m line over open ground (no ring near any step), or the line through the
	// densest trees, both inside the border.
	std::vector<Vec2i64> footprintPath(const Loaded& l, bool throughTrees) {
		constexpr std::int64_t kSteps = 67;
		Vec2i64				   lo;
		Vec2i64				   hi;
		boxOf(l.rings.base.front().ring, lo, hi);
		Vec2i64		 best{};
		std::int64_t bestHits = throughTrees ? -1 : INT64_MAX;
		for (const NavInputPolygon& tree : l.rings.trees) {
			const Vec2i64 from{tree.ring.front().x - 10000, tree.ring.front().y + (throughTrees ? 0 : 3000)};
			if (from.x - 400 < lo.x || from.x + kSteps * 300 + 400 > hi.x || from.y - 400 < lo.y || from.y + 400 > hi.y) {
				continue;
			}
			const std::int64_t hits = overlapsAlong(l, from, kSteps);
			if (throughTrees ? hits > bestHits : hits < bestHits) {
				bestHits = hits;
				best	 = from;
			}
			if (!throughTrees && hits == 0) {
				break;
			}
		}
		return pathAlong(best, kSteps);
	}

	void runFootprint(benchmark::State& state, PatchableMesh& mesh, const std::vector<Vec2i64>& path, Refresh refresh) {
		const RingId mover = untimedPatch(mesh, adding({NavInputPolygon{footprint(path.front()), true, 7}})).added.front();
		PatchTally	 tally;
		std::size_t	 next = 1;
		for (auto _ : state) {
			tally.add(timedPatch(state, mesh, moving(mover, footprint(path[next++ % path.size()])), refresh));
		}
		tally.report(state);
	}

	void BM_PatchFootprintMove(benchmark::State& state, Fixture f, bool throughTrees, Refresh refresh) {
		Loaded l;
		load(l, fixtureRings(f));
		runFootprint(state, l.mesh, footprintPath(l, throughTrees), refresh);
	}

	// --- Across a long bounds edge --------------------------------------------------

	// Two chunk squares of side `side` sharing x = 0, with the fixture's trees tiled
	// into a band 20 m wide along the shared side at the fixture's density.
	Rings sharedSideRings(std::int64_t side) {
		const Rings control = fixtureRings(Fixture::Control);
		Vec2i64		lo;
		Vec2i64		hi;
		boxOf(control.base.front().ring, lo, hi);
		Rings rings;
		rings.base.push_back(NavInputPolygon{rect(-side, 0, 0, side), false, kProvenanceBorder});
		rings.base.push_back(NavInputPolygon{rect(0, 0, side, side), false, kProvenanceBorder});
		const std::int64_t fixtureSide = hi.x - lo.x;
		const std::int64_t bandWidth   = 20000;
		for (std::int64_t y0 = 0; y0 < side; y0 += fixtureSide) {
			for (const NavInputPolygon& tree : control.trees) {
				const Vec2i64 at = tree.ring.front() - lo;
				if (at.x >= bandWidth || at.y + y0 + 1000 >= side) {
					continue;
				}
				rings.trees.push_back(NavInputPolygon{translated(tree.ring, Vec2i64{-lo.x - bandWidth / 2, -lo.y + y0}), true, kProvenanceTree});
			}
		}
		return rings;
	}

	Rings borderRings(Fixture f, std::int64_t side) {
		return side == 0 ? fixtureRings(f) : sharedSideRings(side);
	}

	// x of the long side the rings cross: the fixture border's west side, or the shared side.
	std::int64_t crossingX(const Loaded& l, std::int64_t side) {
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(l.rings.base.front().ring, lo, hi);
		return side == 0 ? lo.x : 0;
	}

	// A 0.6 m tree rect straddling the side at a height no other ring is near.
	std::vector<Vec2i64> straddlingRect(const Loaded& l, std::int64_t x, std::int64_t yLo, std::int64_t yHi) {
		for (std::int64_t y = yLo + 5000; y < yHi - 5000; y += 997) {
			const std::vector<Vec2i64> r = rect(x - 300, y, x + 300, y + 600);
			bool					   clear = true;
			for (std::size_t i = l.rings.base.size(); i < l.all.size() && clear; ++i) {
				clear = !boxesNear(r, l.all[i].ring, 200);
			}
			for (const NavInputPolygon& b : l.rings.base) {
				clear = clear && (b.provenanceId == kProvenanceBorder || !boxesNear(r, b.ring, 200));
			}
			if (clear) {
				return r;
			}
		}
		return rect(x - 300, yLo + 5000, x + 300, yLo + 5600);
	}

	// what: 0 add a straddling tree, 1 remove it, 2 a footprint walking across. Bounds
	// edges are split to state.range(0) mm when it is not 0.
	void BM_PatchAcrossBounds(benchmark::State& state, Fixture f, std::int64_t side, int what) {
		Loaded l(state.range(0));
		load(l, borderRings(f, side));
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(l.rings.base.front().ring, lo, hi);
		const std::int64_t x = crossingX(l, side);
		if (what == 2) {
			const Vec2i64		 from{x - 3000, (lo.y + hi.y) / 2 + 211};
			runFootprint(state, l.mesh, pathAlong(from, 21), Refresh::Untimed);
			return;
		}
		const NavInputPolygon tree{straddlingRect(l, x, lo.y, hi.y), true, kProvenanceTree};
		PatchTally			  tally;
		RingId				  id = kNoRing;
		for (auto _ : state) {
			if (what == 0) {
				const PatchReport r = timedPatch(state, l.mesh, adding({tree}));
				id					= r.added.front();
				tally.add(r);
				untimedPatch(l.mesh, removing(id));
			} else {
				id = untimedPatch(l.mesh, adding({tree})).added.front();
				tally.add(timedPatch(state, l.mesh, removing(id)));
			}
		}
		tally.report(state);
		state.counters["trees"] = static_cast<double>(l.rings.trees.size());
	}

	void boundsSplits(benchmark::internal::Benchmark* b) {
		for (const std::int64_t maxEdge : {0, 32000, 16000, 8000}) {
			b->Arg(maxEdge);
		}
	}

	// --- Detail entering an area, and first builds ---------------------------------

	// Empty mesh, bounds and water in untimed, then every tree in batches of `batch`
	// (0: all in one patch), the batches' times summed.
	void BM_PatchTreesEnter(benchmark::State& state, Fixture f, std::size_t batch, Refresh refresh) {
		const Rings rings = fixtureRings(f);
		PatchTally	tally;
		for (auto _ : state) {
			PatchableMesh mesh;
			untimedPatch(mesh, adding(rings.base));
			const std::size_t step	= batch == 0 ? rings.trees.size() : batch;
			double			  total = 0;
			for (std::size_t at = 0; at < rings.trees.size(); at += step) {
				const std::size_t end = std::min(rings.trees.size(), at + step);
				const PatchEdits  edits = adding({rings.trees.begin() + static_cast<std::ptrdiff_t>(at), rings.trees.begin() + static_cast<std::ptrdiff_t>(end)});
				PatchReport		  r;
				total += secondsOf([&] {
					r = mesh.applyPatch(edits);
					if (refresh == Refresh::Timed) {
						mesh.refreshWidths();
					}
				});
				mesh.refreshWidths();
				tally.add(r);
			}
			state.SetIterationTime(total);
		}
		tally.report(state);
	}

	// A k x k mosaic of control under one border (as NavMesh.bench's area sweep).
	Rings tiledControl(std::int64_t k) {
		const Rings control = fixtureRings(Fixture::Control);
		Vec2i64		lo;
		Vec2i64		hi;
		boxOf(control.base.front().ring, lo, hi);
		const std::int64_t side = hi.x - lo.x;
		Rings			   rings;
		rings.base.push_back(NavInputPolygon{rect(lo.x, lo.y, lo.x + k * side, lo.y + k * side), false, kProvenanceBorder});
		for (std::int64_t j = 0; j < k; ++j) {
			for (std::int64_t i = 0; i < k; ++i) {
				const Vec2i64 offset{i * side, j * side};
				for (std::size_t b = 1; b < control.base.size(); ++b) {
					NavInputPolygon moved = control.base[b];
					moved.ring			  = translated(moved.ring, offset);
					rings.base.push_back(std::move(moved));
				}
				for (const NavInputPolygon& tree : control.trees) {
					NavInputPolygon moved = tree;
					moved.ring			  = translated(moved.ring, offset);
					rings.trees.push_back(std::move(moved));
				}
			}
		}
		return rings;
	}

	void runFirstBuild(benchmark::State& state, const Rings& rings) {
		std::vector<NavInputPolygon> all = rings.base;
		all.insert(all.end(), rings.trees.begin(), rings.trees.end());
		const PatchEdits edits = adding(all);
		std::size_t		 tris  = 0;
		for (auto _ : state) {
			PatchableMesh mesh;
			timedPatch(state, mesh, edits);
			tris = static_cast<std::size_t>(mesh.liveTriangleCount());
		}
		state.counters["tris"] = static_cast<double>(tris);
	}

	void BM_PatchFirstBuild(benchmark::State& state, Fixture f) {
		runFirstBuild(state, fixtureRings(f));
	}

	void BM_PatchFirstBuildTiled(benchmark::State& state) {
		runFirstBuild(state, tiledControl(state.range(0)));
	}

	// --- Point location -------------------------------------------------------------

	std::vector<Vec2i64> randomPoints(const Rings& rings, std::size_t n) {
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(rings.base.front().ring, lo, hi);
		std::mt19937		 rng(5);
		std::vector<Vec2i64> out;
		for (std::size_t i = 0; i < n; ++i) {
			out.push_back({lo.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.x - lo.x)),
						   lo.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.y - lo.y))});
		}
		return out;
	}

	constexpr std::size_t kLocatePoints = 4096;

	void BM_LocatePatchable(benchmark::State& state, Fixture f) {
		Loaded l;
		load(l, fixtureRings(f));
		const std::vector<Vec2i64> points = randomPoints(l.rings, kLocatePoints);
		for (auto _ : state) {
			for (const Vec2i64& p : points) {
				benchmark::DoNotOptimize(l.mesh.locate(p));
			}
		}
		state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kLocatePoints));
	}

	void BM_LocateBuilt(benchmark::State& state, Fixture f) {
		const Rings	 rings = fixtureRings(f);
		NavMeshInput in;
		in.polygons = rings.base;
		in.polygons.insert(in.polygons.end(), rings.trees.begin(), rings.trees.end());
		const NavMesh			   mesh	  = buildNavMesh(in);
		const std::vector<Vec2i64> points = randomPoints(rings, kLocatePoints);
		for (auto _ : state) {
			for (const Vec2i64& p : points) {
				benchmark::DoNotOptimize(locateTriangle(mesh, p));
			}
		}
		state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kLocatePoints));
	}

	// --- Path queries and forests ----------------------------------------------------

	// Start/goal pairs from a fixed sequence; the same points on both meshes.
	std::vector<std::pair<Vec2i64, Vec2i64>> queryPairs(const Rings& rings) {
		const std::vector<Vec2i64>				 points = randomPoints(rings, 96);
		std::vector<std::pair<Vec2i64, Vec2i64>> pairs;
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			pairs.push_back({points[i], points[i + 1]});
		}
		return pairs;
	}

	// Every pair at radius 0 and 300 mm per iteration; nodes expanded summed per iteration.
	void runPaths(benchmark::State& state, const NavView& view, const std::vector<std::pair<Vec2i64, Vec2i64>>& pairs) {
		std::int64_t nodes = 0;
		std::int64_t found = 0;
		for (auto _ : state) {
			nodes = 0;
			found = 0;
			for (const auto& [a, b] : pairs) {
				for (const std::int64_t radius : {0, 300}) {
					const PathResult r = pathThrough(view, a, b, radius);
					nodes += r.nodesExpanded;
					found += r.reachable ? 1 : 0;
					benchmark::DoNotOptimize(r);
				}
			}
		}
		state.counters["queries"] = static_cast<double>(pairs.size() * 2);
		state.counters["nodes"]	  = static_cast<double>(nodes);
		state.counters["found"]	  = static_cast<double>(found);
	}

	void BM_PathPatchable(benchmark::State& state, Fixture f) {
		Loaded l;
		load(l, fixtureRings(f));
		l.mesh.rebuildForests();
		runPaths(state, l.mesh.view(), queryPairs(l.rings));
	}

	void BM_PathBuilt(benchmark::State& state, Fixture f) {
		const Rings	 rings = fixtureRings(f);
		NavMeshInput in;
		in.polygons = rings.base;
		in.polygons.insert(in.polygons.end(), rings.trees.begin(), rings.trees.end());
		const NavMesh mesh = buildNavMesh(in);
		runPaths(state, navView(mesh), queryPairs(rings));
	}

	void runForestRebuild(benchmark::State& state, const Rings& rings, bool withTrees) {
		PatchableMesh mesh;
		PatchEdits	  edits = adding(rings.base);
		if (withTrees) {
			edits.adds.insert(edits.adds.end(), rings.trees.begin(), rings.trees.end());
		}
		untimedPatch(mesh, edits);
		for (auto _ : state) {
			mesh.rebuildForests();
			benchmark::DoNotOptimize(mesh.hasForests());
		}
		state.counters["tris"] = static_cast<double>(mesh.liveTriangleCount());
	}

	void BM_ForestRebuild(benchmark::State& state, Fixture f, bool withTrees) {
		runForestRebuild(state, fixtureRings(f), withTrees);
	}

	void BM_ForestRebuildTiledStructural(benchmark::State& state) {
		runForestRebuild(state, tiledControl(state.range(0)), false);
	}

} // namespace

#define PATCH_ROWS(name, fn, ...)                                                                                              \
	BENCHMARK_CAPTURE(fn, manyTrees##name, Fixture::ManyTrees, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond); \
	BENCHMARK_CAPTURE(fn, control##name, Fixture::Control, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond);     \
	BENCHMARK_CAPTURE(fn, realRings##name, Fixture::RealRings, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond)

PATCH_ROWS(_add, BM_PatchLoneTree, true, Refresh::Untimed);
PATCH_ROWS(_add_refreshed, BM_PatchLoneTree, true, Refresh::Timed);
PATCH_ROWS(_remove, BM_PatchLoneTree, false, Refresh::Untimed);
PATCH_ROWS(_remove_refreshed, BM_PatchLoneTree, false, Refresh::Timed);
PATCH_ROWS(_openGround, BM_PatchFootprintMove, false, Refresh::Untimed);
PATCH_ROWS(_openGround_refreshed, BM_PatchFootprintMove, false, Refresh::Timed);
PATCH_ROWS(_throughTrees, BM_PatchFootprintMove, true, Refresh::Untimed);
PATCH_ROWS(_throughTrees_refreshed, BM_PatchFootprintMove, true, Refresh::Timed);
PATCH_ROWS(_add, BM_PatchShorelineTree, true);
PATCH_ROWS(_remove, BM_PatchShorelineTree, false);
#define BOUNDS_ROWS(name, ...)                                                                                                         	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, manyTrees##name, Fixture::ManyTrees, __VA_ARGS__)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond); 	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, control##name, Fixture::Control, __VA_ARGS__)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond);     	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, realRings##name, Fixture::RealRings, __VA_ARGS__)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond)

BOUNDS_ROWS(_border128_add, 0, 0);
BOUNDS_ROWS(_border128_remove, 0, 1);
BOUNDS_ROWS(_border128_footprint, 0, 2);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_add, Fixture::Control, 512000, 0)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_remove, Fixture::Control, 512000, 1)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_footprint, Fixture::Control, 512000, 2)->Apply(boundsSplits)->UseManualTime()->Unit(benchmark::kMicrosecond);
PATCH_ROWS(_allAtOnce, BM_PatchTreesEnter, std::size_t{0}, Refresh::Untimed);
PATCH_ROWS(_allAtOnce_refreshed, BM_PatchTreesEnter, std::size_t{0}, Refresh::Timed);
PATCH_ROWS(_batch64, BM_PatchTreesEnter, std::size_t{64}, Refresh::Untimed);
PATCH_ROWS(_batch16, BM_PatchTreesEnter, std::size_t{16}, Refresh::Untimed);

BENCHMARK_CAPTURE(BM_PatchFirstBuild, manyTrees, Fixture::ManyTrees)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PatchFirstBuild, control, Fixture::Control)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PatchFirstBuild, realRings, Fixture::RealRings)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PatchFirstBuildTiled)->Arg(2)->Arg(4)->UseManualTime()->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_LocatePatchable, manyTrees, Fixture::ManyTrees)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LocatePatchable, control, Fixture::Control)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LocatePatchable, realRings, Fixture::RealRings)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LocateBuilt, manyTrees, Fixture::ManyTrees)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LocateBuilt, control, Fixture::Control)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LocateBuilt, realRings, Fixture::RealRings)->Unit(benchmark::kMicrosecond);

BENCHMARK_CAPTURE(BM_PathPatchable, manyTrees, Fixture::ManyTrees)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PathPatchable, control, Fixture::Control)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PathPatchable, realRings, Fixture::RealRings)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PathBuilt, manyTrees, Fixture::ManyTrees)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PathBuilt, control, Fixture::Control)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PathBuilt, realRings, Fixture::RealRings)->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_ForestRebuild, manyTrees_structural, Fixture::ManyTrees, false)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ForestRebuild, control_structural, Fixture::Control, false)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ForestRebuild, realRings_structural, Fixture::RealRings, false)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ForestRebuild, manyTrees_full, Fixture::ManyTrees, true)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ForestRebuild, control_full, Fixture::Control, true)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ForestRebuild, realRings_full, Fixture::RealRings, true)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ForestRebuildTiledStructural)->Arg(2)->Arg(4)->Unit(benchmark::kMillisecond);
