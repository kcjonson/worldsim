// Cost of a PatchableMesh patch on real game inputs: one tree in and out, a footprint
// moving over open ground and through trees, trees on the shoreline, rings across a
// long bounds edge, detail entering an area, a first build, point location, and the
// component labels' upkeep per patch.
//
// Every patch row times only the applyPatch call (plus refreshWidths in the _refreshed
// rows), with a steady clock around it and UseManualTime, so the untimed setup between
// patches (taking a tree out before timing its add, building the mesh) never reaches
// the reported time.

#include "nav/LimitPolygon.h"
#include "nav/MeshComponents.h"
#include "nav/NavMesh.h"
#include "nav/NavMeshDumpRings.test.h"
#include "nav/NavMeshRealRings.test.h"
#include "nav/PatchableMesh.h"
#include "nav/PathQuery.h"

#include "predicates/Predicates.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using namespace geometry::nav;

	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenancePlaced	 = -2;
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
				rings.trees.push_back(NavInputPolygon{tree, true, kProvenancePlaced});
			}
			return rings;
		}
		for (const td::DumpRing& r : f == Fixture::ManyTrees ? td::manyTreesDump() : td::controlDump()) {
			NavInputPolygon poly{r.ring, r.blocked, r.provenanceId, kNoOpening, r.holeCapable};
			(r.provenanceId == kProvenancePlaced ? rings.trees : rings.base).push_back(std::move(poly));
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
			for (const ChangedTriangle& c : r.changes) {
				freed += (c.why & kTriangleFreed) != 0 ? 1.0 : 0.0;
				created += (c.why & kTriangleCreated) != 0 ? 1.0 : 0.0;
			}
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
		Rings					 rings;
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
				rings.trees.push_back(NavInputPolygon{translated(tree.ring, Vec2i64{-lo.x - bandWidth / 2, -lo.y + y0}), true, kProvenancePlaced});
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

	// what: 0 add a straddling tree, 1 remove it, 2 a footprint walking across, 3 add an
	// 8 m wall across the side. Without trees the mesh is what the structural layer holds.
	void BM_PatchAcrossBounds(benchmark::State& state, Fixture f, std::int64_t side, int what, bool withTrees = true) {
		Loaded l;
		Rings  rings = borderRings(f, side);
		if (!withTrees) {
			rings.trees.clear();
		}
		load(l, std::move(rings));
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(l.rings.base.front().ring, lo, hi);
		const std::int64_t x = crossingX(l, side);
		if (what == 2) {
			const Vec2i64		 from{x - 3000, (lo.y + hi.y) / 2 + 211};
			runFootprint(state, l.mesh, pathAlong(from, 21), Refresh::Untimed);
			return;
		}
		NavInputPolygon tree{straddlingRect(l, x, lo.y, hi.y), true, kProvenancePlaced};
		if (what == 3) {
			const std::int64_t y = tree.ring.front().y;
			tree				 = NavInputPolygon{rect(x - 4000, y, x + 4000, y + 200), true, 40};
		}
		PatchTally tally;
		RingId	   id = kNoRing;
		for (auto _ : state) {
			if (what == 0 || what == 3) {
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

	// One 512 m bounds square with realRings' water tiled 4 x 4 over it, in one patch (structural only, no trees).
	void BM_PatchChunkGround(benchmark::State& state) {
		namespace td = geometry::nav::testdata;
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(td::realBorderRing(), lo, hi);
		const std::int64_t			 side = hi.x - lo.x;
		std::vector<NavInputPolygon> ground{NavInputPolygon{rect(lo.x, lo.y, lo.x + 4 * side, lo.y + 4 * side), false, kProvenanceBorder}};
		std::size_t					 waterVerts = 0;
		for (std::int64_t j = 0; j < 4; ++j) {
			for (std::int64_t i = 0; i < 4; ++i) {
				ground.push_back(NavInputPolygon{translated(td::realWaterRing(), {i * side, j * side}), true, kProvenanceWater, kNoOpening, true});
				waterVerts += ground.back().ring.size();
			}
		}
		const PatchEdits edits = adding(ground);
		std::size_t		 tris  = 0;
		for (auto _ : state) {
			PatchableMesh mesh;
			timedPatch(state, mesh, edits);
			tris = static_cast<std::size_t>(mesh.liveTriangleCount());
		}
		state.counters["sideM"]		 = static_cast<double>(4 * side) / 1000.0;
		state.counters["waterVerts"] = static_cast<double>(waterVerts);
		state.counters["tris"]		 = static_cast<double>(tris);
	}

	// --- A chunk's square entering beside loaded ground -------------------------------

	// What a chunk holds: bounds only, realRings' water tiled over it, or its water and
	// trees (what the nav mesh holds; the structural mesh has no trees).
	enum class Fill { Open, Water, WaterTrees };

	// Where the loaded chunks sit around the new one at (0, 0).
	enum class Beside { Nothing, Side, TwoSides, Corner, AllSides };

	constexpr std::int64_t kRealTileMm = 128000;

	// Chunk (i, j) of side tiles * 128 m: its square, and for water realRings' water (and
	// trees) tiled tiles x tiles over it.
	std::vector<NavInputPolygon> chunkRings(std::int64_t i, std::int64_t j, std::int64_t tiles, Fill fill) {
		namespace td			 = geometry::nav::testdata;
		const std::int64_t side	 = tiles * kRealTileMm;
		std::vector<NavInputPolygon> out{NavInputPolygon{rect(i * side, j * side, (i + 1) * side, (j + 1) * side), false, kProvenanceBorder}};
		if (fill == Fill::Open) {
			return out;
		}
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(td::realBorderRing(), lo, hi);
		for (std::int64_t b = 0; b < tiles; ++b) {
			for (std::int64_t a = 0; a < tiles; ++a) {
				const Vec2i64 by{i * side + a * kRealTileMm - lo.x, j * side + b * kRealTileMm - lo.y};
				out.push_back(NavInputPolygon{translated(td::realWaterRing(), by), true, kProvenanceWater, kNoOpening, true});
				if (fill == Fill::WaterTrees) {
					for (const std::vector<Vec2i64>& tree : td::realTreeRings()) {
						out.push_back(NavInputPolygon{translated(tree, by), true, kProvenancePlaced});
					}
				}
			}
		}
		return out;
	}

	std::vector<std::pair<std::int64_t, std::int64_t>> besideCells(Beside beside) {
		switch (beside) {
			case Beside::Nothing:
				return {};
			case Beside::Side:
				return {{-1, 0}};
			case Beside::TwoSides:
				return {{-1, 0}, {0, -1}, {-1, -1}};
			case Beside::Corner:
				return {{-1, -1}};
			case Beside::AllSides:
				return {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
		}
		return {};
	}

	// The new chunk's rings added in one timed patch, then taken out untimed. "slots" is
	// the live triangles the chunk brings.
	void BM_ChunkEnters(benchmark::State& state, Beside beside, Fill besideFill, Fill fill, std::int64_t tiles) {
		PatchableMesh				 mesh;
		std::vector<NavInputPolygon> loaded;
		for (const auto& [i, j] : besideCells(beside)) {
			const std::vector<NavInputPolygon> rings = chunkRings(i, j, tiles, besideFill);
			loaded.insert(loaded.end(), rings.begin(), rings.end());
		}
		if (!loaded.empty()) {
			untimedPatch(mesh, adding(loaded));
		}
		const PatchEdits edits = adding(chunkRings(0, 0, tiles, fill));
		const std::int32_t before = mesh.liveTriangleCount();
		PatchTally		   tally;
		std::int32_t	   slots = 0;
		for (auto _ : state) {
			const PatchReport r = timedPatch(state, mesh, edits);
			tally.add(r);
			slots = mesh.liveTriangleCount() - before;
			PatchEdits out;
			out.removes = r.added;
			untimedPatch(mesh, out);
		}
		tally.report(state);
		state.counters["slots"] = static_cast<double>(slots);
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

	// --- Large meshes: per-patch work must not grow with the mesh ----------------------

	// kind 0: control tiled 4x4 under one 512 m border. kind 1: four adjacent 512 m bounds
	// squares, each holding that tiling.
	Rings largeRings(int kind) {
		const Rings tile = tiledControl(4);
		if (kind == 0) {
			return tile;
		}
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(tile.base.front().ring, lo, hi);
		const std::int64_t side = hi.x - lo.x;
		Rings			   rings;
		for (std::int64_t j = 0; j < 2; ++j) {
			for (std::int64_t i = 0; i < 2; ++i) {
				const Vec2i64 offset{i * side, j * side};
				for (const NavInputPolygon& p : tile.base) {
					NavInputPolygon moved = p;
					moved.ring			  = translated(p.ring, offset);
					rings.base.push_back(std::move(moved));
				}
				for (const NavInputPolygon& p : tile.trees) {
					NavInputPolygon moved = p;
					moved.ring			  = translated(p.ring, offset);
					rings.trees.push_back(std::move(moved));
				}
			}
		}
		return rings;
	}

	// The first few lone or shoreline trees of the first tile, found without comparing
	// every tree with every ring.
	std::vector<std::size_t> firstTileTrees(const Loaded& l, bool shoreline) {
		const std::size_t		 tileTrees = fixtureRings(Fixture::Control).trees.size();
		std::vector<std::size_t> out;
		for (std::size_t i = 0; i < tileTrees && out.size() < 24; ++i) {
			const std::vector<Vec2i64>& tree = l.rings.trees[i].ring;
			bool						near = false;
			bool						crossesWater = false;
			for (const NavInputPolygon& other : l.all) {
				if (&other.ring == &l.all[l.rings.base.size() + i].ring || !boxesNear(tree, other.ring, 50) ||
					(other.provenanceId == kProvenanceBorder && !ringsCross(tree, other.ring))) {
					continue;
				}
				near		 = true;
				crossesWater = crossesWater || (other.provenanceId == kProvenanceWater && ringsCross(tree, other.ring));
			}
			if (shoreline ? crossesWater : !near) {
				out.push_back(i);
			}
		}
		return out;
	}

	void BM_LargeTree(benchmark::State& state, int kind, bool shoreline, bool add) {
		Loaded l;
		load(l, largeRings(kind));
		cycleTrees(state, l, firstTileTrees(l, shoreline), add, Refresh::Untimed);
		state.counters["tris"] = static_cast<double>(l.mesh.liveTriangleCount());
	}

	// A 0.6 m tree across the first square's west side (kind 0) or the side two squares
	// share (kind 1), added and taken out again.
	void BM_LargeBoundsTree(benchmark::State& state, int kind) {
		Loaded l;
		load(l, largeRings(kind));
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(l.rings.base.front().ring, lo, hi);
		const NavInputPolygon tree{straddlingRect(l, kind == 0 ? lo.x : hi.x, lo.y, hi.y), true, kProvenancePlaced};
		PatchTally			  tally;
		for (auto _ : state) {
			const PatchReport r = timedPatch(state, l.mesh, adding({tree}));
			tally.add(r);
			untimedPatch(l.mesh, removing(r.added.front()));
		}
		tally.report(state);
		state.counters["tris"] = static_cast<double>(l.mesh.liveTriangleCount());
	}

	// --- Path queries ----------------------------------------------------------------

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

	// The game's query shape: many movers walking 20 to 100 m to a few goals through
	// meshed flora, the trees blocking.
	struct RequestShape {
		PatchableMesh						 detail;
		std::vector<Vec2i64>				 goals;
		std::vector<std::pair<Vec2i64, int>> starts; // start, goal index
	};

	// Where the goals sit: anywhere in one bounds square; or, with the square cut in two
	// down its middle, 32 m from that shared side or 12 m from it. The Chunk
	// placements do the same on two of the game's 512 m chunks side by side, each 4 x 4
	// tiles of the fixture's water and trees: in a chunk's middle, or 12 m from the side.
	enum class Colony { Anywhere, Middle, NearSide, ChunkMiddle, ChunkSide };

	void loadRequestShape(RequestShape& s, Fixture f, Colony colony = Colony::Anywhere) {
		const Rings					 rings = fixtureRings(f);
		std::vector<NavInputPolygon> all   = rings.base;
		all.insert(all.end(), rings.trees.begin(), rings.trees.end());
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(rings.base.front().ring, lo, hi);
		std::int64_t mid = (lo.x + hi.x) / 2;
		const bool	 chunks = colony == Colony::ChunkMiddle || colony == Colony::ChunkSide;
		if (chunks) {
			const std::int64_t tile = hi.x - lo.x;
			const std::int64_t side = 4 * tile;
			std::vector<NavInputPolygon> tiled{NavInputPolygon{rect(lo.x, lo.y, lo.x + side, lo.y + side), false, kProvenanceBorder},
											   NavInputPolygon{rect(lo.x + side, lo.y, lo.x + 2 * side, lo.y + side), false, kProvenanceBorder}};
			for (std::int64_t b = 0; b < 4; ++b) {
				for (std::int64_t a = 0; a < 8; ++a) {
					for (std::size_t k = 1; k < all.size(); ++k) {
						tiled.push_back(all[k]);
						tiled.back().ring = translated(tiled.back().ring, {a * tile, b * tile});
					}
				}
			}
			all = std::move(tiled);
			hi	= {lo.x + 2 * side, lo.y + side};
			mid = lo.x + side;
		} else if (colony != Colony::Anywhere) {
			all.front() = NavInputPolygon{rect(lo.x, lo.y, mid, hi.y), false, kProvenanceBorder};
			all.insert(all.begin() + 1, NavInputPolygon{rect(mid, lo.y, hi.x, hi.y), false, kProvenanceBorder});
		}
		untimedPatch(s.detail, adding(all));
		std::mt19937 rng(91);
		auto		 walkable = [&](const Vec2i64& p) {
			const std::int32_t t = s.detail.locate(p);
			return t >= 0 && isFloorFace(s.detail.triangles()[static_cast<std::size_t>(t)]);
		};
		while (s.goals.size() < 4) {
			Vec2i64 g{lo.x + 20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.x - lo.x - 40000)),
					  lo.y + 20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.y - lo.y - 40000))};
			if (colony != Colony::Anywhere) {
				const std::int64_t back = colony == Colony::NearSide || colony == Colony::ChunkSide ? 12000 : (chunks ? (mid - lo.x) / 2 : 32000);
				g.x						= mid - back + static_cast<std::int64_t>(rng() % 2000) - 1000;
				if (chunks) {
					g.y = (lo.y + hi.y) / 2 + static_cast<std::int64_t>(rng() % 40000) - 20000;
				}
			}
			if (walkable(g)) {
				s.goals.push_back(g);
			}
		}
		for (int gi = 0; gi < 4; ++gi) {
			int placed = 0;
			while (placed < 16) {
				const double  angle = static_cast<double>(rng() % 6283) / 1000.0;
				const double  dist	= 20000.0 + static_cast<double>(rng() % 80000);
				const Vec2i64 p{s.goals[static_cast<std::size_t>(gi)].x + static_cast<std::int64_t>(dist * std::cos(angle)),
								s.goals[static_cast<std::size_t>(gi)].y + static_cast<std::int64_t>(dist * std::sin(angle))};
				if (p.x > lo.x && p.y > lo.y && p.x < hi.x && p.y < hi.y && walkable(p) &&
					pathThrough(s.detail.view(), p, s.goals[static_cast<std::size_t>(gi)], 300).reachable) {
					s.starts.push_back({p, gi});
					++placed;
				}
			}
		}
	}

	void BM_RequestShape(benchmark::State& state, Fixture f, Colony colony = Colony::Anywhere) {
		RequestShape s;
		loadRequestShape(s, f, colony);
		std::int64_t nodes = 0;
		for (auto _ : state) {
			nodes		 = 0;
			double total = 0.0;
			for (const auto& [start, gi] : s.starts) {
				PathResult r;
				total += secondsOf([&] { r = pathThrough(s.detail.view(), start, s.goals[static_cast<std::size_t>(gi)], 300); });
				nodes += r.nodesExpanded;
				benchmark::DoNotOptimize(r);
			}
			state.SetIterationTime(total);
		}
		state.counters["usPerQuery"] = benchmark::Counter(static_cast<double>(s.starts.size()) * static_cast<double>(state.iterations()),
														  benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
		state.counters["nodes"]		 = static_cast<double>(nodes) / static_cast<double>(s.starts.size());
	}

	// --- Flora cells entering a large mesh -------------------------------------------

	// tiles x tiles of realRings' 128 m tile (water and trees) under one bounds square, trees
	// detail-class, the game's 64 m bounds sides. The trees of `cells` 16 m cells in the
	// middle of tile (0, 0) (or one tree, for 0 cells) go out untimed and come back timed with the width refresh,
	// which is the game's flora job; "refreshUs" is the refresh alone. Its cost must not
	// depend on how much mesh lies elsewhere. With `openTile`, tile (0, 0) holds only those
	// trees, on open ground, as an unforested chunk does.
	void BM_FloraCellsEnter(benchmark::State& state, int cells, bool openTile) {
		namespace td		   = geometry::nav::testdata;
		const std::int64_t tiles = state.range(0);
		Vec2i64			   lo;
		Vec2i64			   hi;
		boxOf(td::realBorderRing(), lo, hi);
		PatchableMesh				 mesh;
		std::vector<NavInputPolygon> ground{NavInputPolygon{rect(0, 0, tiles * kRealTileMm, tiles * kRealTileMm), false, kProvenanceBorder}};
		std::vector<NavInputPolygon> moving;
		for (std::int64_t b = 0; b < tiles; ++b) {
			for (std::int64_t a = 0; a < tiles; ++a) {
				const Vec2i64 by{a * kRealTileMm - lo.x, b * kRealTileMm - lo.y};
				const bool open = openTile && a == 0 && b == 0;
				if (!open) {
					ground.push_back(NavInputPolygon{translated(td::realWaterRing(), by), true, kProvenanceWater, kNoOpening, true});
				}
				for (const std::vector<Vec2i64>& tree : td::realTreeRings()) {
					NavInputPolygon ring{translated(tree, by), true, kProvenancePlaced};
					Vec2i64			tLo;
					Vec2i64			tHi;
					boxOf(ring.ring, tLo, tHi);
					const bool inCells = a == 0 && b == 0 && tLo.x >= 64000 && tLo.x < 64000 + 16000 * std::max(cells, 1) && tLo.y >= 64000 && tLo.y < 80000 &&
											 (cells > 0 || moving.empty());
					if (inCells) {
						moving.push_back(std::move(ring));
					} else if (!open) {
						ground.push_back(std::move(ring));
					}
				}
			}
		}
		untimedPatch(mesh, adding(ground));
		double refreshSeconds = 0.0;
		for (auto _ : state) {
			const PatchReport in = untimedPatch(mesh, adding(moving));
			PatchEdits		  out;
			out.removes = in.added;
			untimedPatch(mesh, out);
			PatchReport back;
			double		refresh = 0.0;
			state.SetIterationTime(secondsOf([&] {
				back	= mesh.applyPatch(adding(moving));
				refresh = secondsOf([&] { mesh.refreshWidths(); });
			}));
			refreshSeconds += refresh;
			PatchEdits again;
			again.removes = back.added;
			untimedPatch(mesh, again);
		}
		state.counters["slots"]		= static_cast<double>(mesh.triangles().size());
		state.counters["trees"]		= static_cast<double>(moving.size());
		state.counters["refreshUs"] = 1e6 * refreshSeconds / static_cast<double>(state.iterations());
	}

	// --- A mesh growing past its storage -------------------------------------------

	// tiles x tiles of realRings' water under one bounds square, then their trees entering
	// a 16 m cell row at a time (the game's flora jobs), each patch timed with its width
	// refresh. The mesh grows through every size a slot table reallocates at; no patch may
	// pay to copy the mesh, so the worst patch stays near the typical one.
	void BM_MeshGrowsWorstPatch(benchmark::State& state) {
		namespace td		   = geometry::nav::testdata;
		const std::int64_t tiles = state.range(0);
		Vec2i64			   lo;
		Vec2i64			   hi;
		boxOf(td::realBorderRing(), lo, hi);
		std::vector<NavInputPolygon>			  ground{NavInputPolygon{rect(0, 0, tiles * kRealTileMm, tiles * kRealTileMm), false, kProvenanceBorder}};
		std::vector<std::vector<NavInputPolygon>> rows;
		for (std::int64_t b = 0; b < tiles; ++b) {
			for (std::int64_t a = 0; a < tiles; ++a) {
				const Vec2i64 by{a * kRealTileMm - lo.x, b * kRealTileMm - lo.y};
				ground.push_back(NavInputPolygon{translated(td::realWaterRing(), by), true, kProvenanceWater, kNoOpening, true});
				std::vector<std::vector<NavInputPolygon>> tileRows(8);
				for (const std::vector<Vec2i64>& tree : td::realTreeRings()) {
					NavInputPolygon ring{translated(tree, by), true, kProvenancePlaced};
					Vec2i64			tLo;
					Vec2i64			tHi;
					boxOf(ring.ring, tLo, tHi);
					const std::int64_t row = std::clamp<std::int64_t>((tLo.y - b * kRealTileMm) / 16000, 0, 7);
					tileRows[static_cast<std::size_t>(row)].push_back(std::move(ring));
				}
				for (std::vector<NavInputPolygon>& row : tileRows) {
					if (!row.empty()) {
						rows.push_back(std::move(row));
					}
				}
			}
		}
		double		worst = 0.0;
		double		total = 0.0;
		std::size_t slots = 0;
		for (auto _ : state) {
			PatchableMesh mesh;
			untimedPatch(mesh, adding(ground));
			worst = 0.0;
			total = 0.0;
			for (const std::vector<NavInputPolygon>& row : rows) {
				const PatchEdits edits = adding(row);
				const double	 took  = secondsOf([&] {
					  mesh.applyPatch(edits);
					  mesh.refreshWidths();
				  });
				worst				   = std::max(worst, took);
				total += took;
			}
			slots = mesh.triangles().size();
			state.SetIterationTime(total);
		}
		state.counters["patches"] = static_cast<double>(rows.size());
		state.counters["slots"]	  = static_cast<double>(slots);
		state.counters["meanUs"]  = 1e6 * total / static_cast<double>(rows.size());
		state.counters["worstUs"] = 1e6 * worst;
	}

	// --- Limits on a query -------------------------------------------------------------

	// tiles x tiles of realRings' 128 m tile (water, and trees unless `trees` is false)
	// under one bounds square, trees detail-class.
	void loadTiledGround(PatchableMesh& mesh, std::int64_t tiles, bool trees, std::vector<NavInputPolygon> extra = {}) {
		namespace td = geometry::nav::testdata;
		Vec2i64		 lo;
		Vec2i64		 hi;
		boxOf(td::realBorderRing(), lo, hi);
		std::vector<NavInputPolygon> ground{NavInputPolygon{rect(0, 0, tiles * kRealTileMm, tiles * kRealTileMm), false, kProvenanceBorder}};
		for (std::int64_t b = 0; b < tiles; ++b) {
			for (std::int64_t a = 0; a < tiles; ++a) {
				const Vec2i64 by{a * kRealTileMm - lo.x, b * kRealTileMm - lo.y};
				ground.push_back(NavInputPolygon{translated(td::realWaterRing(), by), true, kProvenanceWater, kNoOpening, true});
				if (trees) {
					for (const std::vector<Vec2i64>& tree : td::realTreeRings()) {
						ground.push_back(NavInputPolygon{translated(tree, by), true, kProvenancePlaced});
					}
				}
			}
		}
		ground.insert(ground.end(), extra.begin(), extra.end());
		untimedPatch(mesh, adding(ground));
	}

	// A goal the belief knows is walled in: a 6 m room of four known wall segments in the
	// middle of tiles x tiles of forested ground, and starts 30 to 60 m off. The search can
	// only fail: run dry, or with `capped` stop at maxExpansions 2000.
	void BM_WalledGoal(benchmark::State& state, bool capped) {
		const std::int64_t tiles = state.range(0);
		PatchableMesh	   mesh;
		const std::int64_t side = tiles * kRealTileMm;
		// The room's centre: open floor near the middle, found on a trial mesh of the tile.
		Vec2i64			   g{side / 2, side / 2};
		{
			PatchableMesh trial;
			loadTiledGround(trial, tiles, true);
			std::mt19937 rng(5);
			auto		 clear = [&](const Vec2i64& p) {
				for (std::int64_t dy = -4000; dy <= 4000; dy += 500) {
					for (std::int64_t dx = -4000; dx <= 4000; dx += 500) {
						const std::int32_t t = trial.locate(p + Vec2i64{dx, dy});
						if (t < 0 || !isFloorFace(trial.triangles()[static_cast<std::size_t>(t)])) {
							return false;
						}
					}
				}
				return true;
			};
			while (!clear(g)) {
				g = {side / 2 + static_cast<std::int64_t>(rng() % 60000) - 30000, side / 2 + static_cast<std::int64_t>(rng() % 60000) - 30000};
			}
		}
		constexpr std::int64_t kHalf = 3000;
		constexpr std::int64_t kWall = 200;
		std::vector<NavInputPolygon> walls{
			NavInputPolygon{rect(g.x - kHalf, g.y - kHalf, g.x + kHalf, g.y - kHalf + kWall), true, 101},
			NavInputPolygon{rect(g.x - kHalf, g.y + kHalf - kWall, g.x + kHalf, g.y + kHalf), true, 102},
			NavInputPolygon{rect(g.x - kHalf, g.y - kHalf, g.x - kHalf + kWall, g.y + kHalf), true, 103},
			NavInputPolygon{rect(g.x + kHalf - kWall, g.y - kHalf, g.x + kHalf, g.y + kHalf), true, 104}};
		loadTiledGround(mesh, tiles, true, walls);
		std::unordered_set<std::uint64_t> known{101, 102, 103, 104};
		std::unordered_set<std::uint64_t> noDoors;
		BeliefFilter					  belief{&known, &noDoors};
		std::mt19937		 rng(17);
		std::vector<Vec2i64> starts;
		while (starts.size() < 8) {
			const double  angle = static_cast<double>(rng() % 6283) / 1000.0;
			const double  dist	= 30000.0 + static_cast<double>(rng() % 30000);
			const Vec2i64 p{g.x + static_cast<std::int64_t>(dist * std::cos(angle)), g.y + static_cast<std::int64_t>(dist * std::sin(angle))};
			const std::int32_t t = mesh.locate(p);
			if (t >= 0 && isFloorFace(mesh.triangles()[static_cast<std::size_t>(t)])) {
				starts.push_back(p);
			}
		}
		double		  expansions = 0.0;
		int			  limited	 = 0;
		for (auto _ : state) {
			double total = 0.0;
			expansions	 = 0.0;
			limited		 = 0;
			for (const Vec2i64& s : starts) {
				PathResult r;
				total += secondsOf([&] { r = pathThrough(mesh.view(), s, g, 300, belief, {.maxExpansions = capped ? 2000U : 0U}); });
				expansions += r.expansions;
				limited += r.stop == PathStop::LimitReached ? 1 : 0;
				benchmark::DoNotOptimize(r);
			}
			state.SetIterationTime(total);
		}
		state.counters["slots"]		  = static_cast<double>(mesh.triangles().size());
		state.counters["usPerQuery"]  = benchmark::Counter(static_cast<double>(starts.size()) * static_cast<double>(state.iterations()),
														   benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
		state.counters["expansions"] = expansions / static_cast<double>(starts.size());
		state.counters["limited"]	  = limited;
	}

	// The game's walking-distance question on its structural mesh (water, no trees): pairs 10 to 120 m apart, the cost capped at `capTimes` the straight line. Time
	// per query by how it ended: found, stopped on the cap, or no path.
	void BM_WalkingDistance(benchmark::State& state, double capTimes) {
		const std::int64_t tiles = 5;
		PatchableMesh	   mesh;
		loadTiledGround(mesh, tiles, false);
		const std::int64_t side = tiles * kRealTileMm;
		std::mt19937	   rng(29);
		auto			   floor = [&](const Vec2i64& p) {
			const std::int32_t t = mesh.locate(p);
			return t >= 0 && isFloorFace(mesh.triangles()[static_cast<std::size_t>(t)]);
		};
		std::vector<std::pair<Vec2i64, Vec2i64>> pairs;
		while (pairs.size() < 64) {
			const Vec2i64 a{20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side - 40000)),
							20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side - 40000))};
			const double  angle = static_cast<double>(rng() % 6283) / 1000.0;
			const double  dist	= 10000.0 + static_cast<double>(rng() % 110000);
			const Vec2i64 b{a.x + static_cast<std::int64_t>(dist * std::cos(angle)), a.y + static_cast<std::int64_t>(dist * std::sin(angle))};
			if (floor(a) && floor(b)) {
				pairs.push_back({a, b});
			}
		}
		std::array<double, 3> seconds{};
		std::array<int, 3>	  count{};
		for (auto _ : state) {
			seconds = {};
			count	= {};
			double total = 0.0;
			for (const auto& [a, b] : pairs) {
				const double straight = std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
				PathResult	 r;
				const double took = secondsOf([&] { r = pathThrough(mesh.view(), a, b, 300, {}, {.maxCost = capTimes * straight}); });
				const int	 kind = r.stop == PathStop::Found ? 0 : (r.stop == PathStop::LimitReached ? 1 : 2);
				seconds[static_cast<std::size_t>(kind)] += took;
				++count[static_cast<std::size_t>(kind)];
				total += took;
				benchmark::DoNotOptimize(r);
			}
			state.SetIterationTime(total);
		}
		const char* names[] = {"found", "cap", "noPath"};
		for (std::size_t k = 0; k < 3; ++k) {
			state.counters[std::string(names[k]) + "N"]	   = count[k];
			state.counters[std::string(names[k]) + "UsEach"] = count[k] > 0 ? 1e6 * seconds[k] / count[k] : 0.0;
		}
		state.counters["usPerQuery"] = benchmark::Counter(static_cast<double>(pairs.size()) * static_cast<double>(state.iterations()),
														  benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
	}

	// Component-label upkeep alone, per patch, for every tree taken out and put back in
	// turn (lone and shoreline alike). A second MeshComponents follows the mesh's reports;
	// only its updates are timed.
	void BM_LabelUpkeep(benchmark::State& state, Fixture f) {
		const Rings					 rings = fixtureRings(f);
		PatchableMesh				 mesh;
		MeshComponents				 labels;
		std::vector<NavInputPolygon> all = rings.base;
		all.insert(all.end(), rings.trees.begin(), rings.trees.end());
		const PatchReport first = mesh.applyPatch(adding(all));
		labels.update(mesh.view(), first);
		std::vector<RingId> ids(first.added.begin() + static_cast<std::ptrdiff_t>(rings.base.size()), first.added.end());
		double				visited = 0;
		double				splits	= 0;
		double				patches = 0;
		std::size_t			next	= 0;
		for (auto _ : state) {
			const std::size_t i		= next++ % ids.size();
			const PatchReport out	= mesh.applyPatch(removing(ids[i]));
			double			  spent = secondsOf([&] { labels.update(mesh.view(), out); });
			visited += static_cast<double>(labels.lastUpkeep().visited);
			splits += labels.lastUpkeep().splits;
			const PatchReport in = mesh.applyPatch(adding({rings.trees[i]}));
			spent += secondsOf([&] { labels.update(mesh.view(), in); });
			visited += static_cast<double>(labels.lastUpkeep().visited);
			splits += labels.lastUpkeep().splits;
			ids[i] = in.added.front();
			patches += 2;
			state.SetIterationTime(spent / 2);
		}
		state.counters["visited"] = visited / std::max(1.0, patches);
		state.counters["splits"]  = splits / std::max(1.0, patches);
	}

	// Label upkeep when an obstacle closes a gap in a long barrier whose two sides still
	// join round the barrier's far end, and when it leaves again. Closing, the split check
	// floods both sides in lockstep until they meet round the end: no split, and the flood
	// covers both sides of the barrier. Posts every 2.5 m keep the ground at game density;
	// the barrier runs `state.range(0)` metres.
	void BM_LabelUpkeepRoundBarrier(benchmark::State& state) {
		const std::int64_t			 lengthMm = state.range(0) * 1000;
		const std::int64_t			 widthMm  = lengthMm + 20000;
		auto						 rect	  = [](std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
			  return std::vector<Vec2i64>{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
		};
		std::vector<NavInputPolygon> rings{{rect(0, 0, widthMm, 40000), false, kProvenanceBorder}};
		rings.push_back({rect(0, 19500, 10000, 20500), true, kProvenancePlaced});
		rings.push_back({rect(11000, 19500, lengthMm, 20500), true, kProvenancePlaced});
		for (std::int64_t x = 1250; x < widthMm; x += 2500) {
			for (std::int64_t y = 1250; y < 40000; y += 2500) {
				if (y < 18000 || y > 21500) {
					rings.push_back({rect(x, y, x + 300, y + 300), true, kProvenancePlaced});
				}
			}
		}
		PatchableMesh	mesh;
		MeshComponents	labels;
		labels.update(mesh.view(), mesh.applyPatch(adding(rings)));
		const NavInputPolygon plug{rect(10000, 19500, 11000, 20500), true, kProvenancePlaced};
		double				  visited = 0;
		double				  patches = 0;
		for (auto _ : state) {
			const PatchReport in	= mesh.applyPatch(adding({plug}));
			double			  spent = secondsOf([&] { labels.update(mesh.view(), in); });
			visited += static_cast<double>(labels.lastUpkeep().visited);
			const PatchReport out = mesh.applyPatch(removing(in.added.front()));
			spent += secondsOf([&] { labels.update(mesh.view(), out); });
			visited += static_cast<double>(labels.lastUpkeep().visited);
			patches += 2;
			state.SetIterationTime(spent / 2);
		}
		state.counters["visited"] = visited / std::max(1.0, patches);
		state.counters["tris"]	  = static_cast<double>(mesh.liveTriangleCount());
	}

	// --- Visibility limits: the seen area as a union of ground outlines ---------------------
	//
	// A mover's visibility limit is a 16-gon whose sides lie kLimitRangeMm from where he stands,
	// retaken every `step` he walks; the seen area is the union of every limit taken. Placed
	// obstacles whose boxes meet a new limit's circumscribed disc and are not yet in the mesh go
	// in first, as one patch over whatever ground is there; then the limit's ground, skipped
	// where groundCovers says the union already holds it. Every row runs once and reports per
	// patch on stderr and as counters.

	namespace td = geometry::nav::testdata;

	constexpr std::int64_t kLimitRangeMm	= 46000; // a colonist's sight plus the buffer
	constexpr std::int64_t kWalkLengthMm	= 300000;
	constexpr std::int64_t kDenseSpacingMm	= 3953;	 // one placed obstacle per lattice cell: 0.064 per m2
	constexpr std::int64_t kSparseSpacingMm = 14142; // 0.005 per m2

	double meanOf(const std::vector<double>& v) {
		double sum = 0.0;
		for (const double x : v) {
			sum += x;
		}
		return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
	}

	double percentileOf(std::vector<double> v, double q) {
		if (v.empty()) {
			return 0.0;
		}
		std::sort(v.begin(), v.end());
		return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5))];
	}

	double maxOf(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }

	std::uint64_t mixBits(std::uint64_t x) {
		x += 0x9E3779B97F4A7C15ULL;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
		return x ^ (x >> 31);
	}

	// Square placed obstacles, one per lattice cell of side `spacing` over [lo, hi), nudged up
	// to 1.4 m, half extents 0.175, 0.275 or 0.41 m (an oak's age classes and the pad) in
	// 15 / 60 / 25 shares. None overlap at either spacing used here.
	std::vector<NavInputPolygon> obstacleField(const Vec2i64& lo, const Vec2i64& hi, std::int64_t spacing) {
		std::vector<NavInputPolygon> out;
		for (std::int64_t x = lo.x; x < hi.x; x += spacing) {
			for (std::int64_t y = lo.y; y < hi.y; y += spacing) {
				const std::uint64_t h = mixBits(static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(y));
				const Vec2i64		c{x + static_cast<std::int64_t>(h % 1401), y + static_cast<std::int64_t>((h >> 16) % 1401)};
				const std::uint64_t k = (h >> 32) % 100;
				const std::int64_t	e = k < 15 ? 175 : (k < 75 ? 275 : 410);
				out.push_back(NavInputPolygon{rect(c.x - e, c.y - e, c.x + e, c.y + e), true, kProvenancePlaced});
			}
		}
		return out;
	}

	enum class Ground { Open, Sparse, Dense };
	enum class Walk { Straight, Ell };

	std::int64_t spacingOf(Ground g) { return g == Ground::Dense ? kDenseSpacingMm : kSparseSpacingMm; }

	// Where the mover stands at each retake: every `step` along a walk of `length`, east, or
	// east for half of it and then north.
	std::vector<Vec2i64> walkCentres(Walk walk, std::int64_t length, std::int64_t step) {
		std::vector<Vec2i64> out;
		for (std::int64_t d = 0; d <= length; d += step) {
			if (walk == Walk::Straight || d <= length / 2) {
				out.push_back({d, 0});
			} else {
				out.push_back({length / 2, d - length / 2});
			}
		}
		return out;
	}

	// A sweep of the square [0, side]^2 in rows kSweepRowMm apart (less than a limit's
	// diameter, so the union has no gaps), every row and turn walked at `step`.
	constexpr std::int64_t kSweepRowMm = 80000;

	std::vector<Vec2i64> sweepCentres(std::int64_t side, std::int64_t step) {
		std::vector<Vec2i64> out;
		bool				 east = true;
		for (std::int64_t y = 0; y <= side; y += kSweepRowMm) {
			for (std::int64_t d = 0; d <= side; d += step) {
				out.push_back({east ? d : side - d, y});
			}
			for (std::int64_t d = step; d < kSweepRowMm && y + kSweepRowMm <= side; d += step) {
				out.push_back({east ? side : 0, y + d});
			}
			east = !east;
		}
		return out;
	}

	struct RetakeStats {
		std::vector<double> limitMs;
		std::vector<double> refreshMs;
		std::vector<double> limitRefreshMs;
		std::vector<double> obstacleMs; // the retake's obstacle patch and its width refresh
		std::vector<double> coversUs;
		std::vector<double> obstacles;
		std::vector<double> freed;
		std::vector<double> created;
		std::vector<double> flipped;
		std::vector<double> passes;
		std::vector<double> boxM2;
		int					covered = 0;

		void patch(const PatchReport& r, double ms, double refreshMsTaken) {
			double f = 0;
			double c = 0;
			double l = 0;
			for (const ChangedTriangle& ch : r.changes) {
				f += (ch.why & kTriangleFreed) != 0 ? 1.0 : 0.0;
				c += (ch.why & kTriangleCreated) != 0 ? 1.0 : 0.0;
				l += (ch.why & kTriangleFlipped) != 0 ? 1.0 : 0.0;
			}
			limitMs.push_back(ms);
			refreshMs.push_back(refreshMsTaken);
			limitRefreshMs.push_back(ms + refreshMsTaken);
			freed.push_back(f);
			created.push_back(c);
			flipped.push_back(l);
			passes.push_back(r.passes);
			boxM2.push_back(r.changes.empty() ? 0.0 : static_cast<double>(r.hi.x - r.lo.x) * static_cast<double>(r.hi.y - r.lo.y) / 1e6);
		}

		void print(const char* label) const {
			std::fprintf(stderr,
						 "[%s] %zu limits (%d skipped as covered)\n"
						 "  limit patch ms mean %.3f p99 %.3f max %.3f; refreshWidths ms mean %.3f p99 %.3f max %.3f; both mean %.3f p99 %.3f max %.3f\n"
						 "  obstacles per retake mean %.1f max %.0f; their patch+refresh ms mean %.3f p99 %.3f max %.3f; groundCovers us mean %.1f max %.1f\n"
						 "  hole (freed) mean %.0f max %.0f; created mean %.0f; flipped mean %.1f; passes mean %.2f max %.0f; report box m2 mean %.0f max %.0f\n",
						 label, limitMs.size(), covered, meanOf(limitMs), percentileOf(limitMs, 0.99), maxOf(limitMs), meanOf(refreshMs),
						 percentileOf(refreshMs, 0.99), maxOf(refreshMs), meanOf(limitRefreshMs), percentileOf(limitRefreshMs, 0.99), maxOf(limitRefreshMs),
						 meanOf(obstacles), maxOf(obstacles), meanOf(obstacleMs), percentileOf(obstacleMs, 0.99), maxOf(obstacleMs), meanOf(coversUs),
						 maxOf(coversUs), meanOf(freed), maxOf(freed), meanOf(created), meanOf(flipped), meanOf(passes), maxOf(passes), meanOf(boxM2),
						 maxOf(boxM2));
		}

		void count(benchmark::State& state) const {
			state.counters["limits"]		  = static_cast<double>(limitMs.size());
			state.counters["covered"]		  = covered;
			state.counters["limitMeanMs"]	  = meanOf(limitMs);
			state.counters["limitP99Ms"]	  = percentileOf(limitMs, 0.99);
			state.counters["limitMaxMs"]	  = maxOf(limitMs);
			state.counters["refreshMeanMs"]	  = meanOf(refreshMs);
			state.counters["bothP99Ms"]		  = percentileOf(limitRefreshMs, 0.99);
			state.counters["obstacleMeanMs"]  = meanOf(obstacleMs);
			state.counters["obstacleP99Ms"]	  = percentileOf(obstacleMs, 0.99);
			state.counters["holeMean"]		  = meanOf(freed);
			state.counters["passesMean"]	  = meanOf(passes);
		}
	};

	double msOf(double seconds) { return seconds * 1e3; }

	// A mesh the limits of one walk go into, over a field of placed obstacles not yet in it.
	struct LimitWalker {
		PatchableMesh				 mesh;
		std::vector<NavInputPolygon> field;
		std::vector<Vec2i64>		 fieldLo;
		std::vector<Vec2i64>		 fieldHi;
		std::vector<std::uint8_t>	 inMesh;
		std::vector<RingId>			 fieldIds;

		void setField(std::vector<NavInputPolygon> obstacles) {
			field = std::move(obstacles);
			fieldLo.resize(field.size());
			fieldHi.resize(field.size());
			for (std::size_t i = 0; i < field.size(); ++i) {
				boxOf(field[i].ring, fieldLo[i], fieldHi[i]);
			}
			inMesh.assign(field.size(), 0);
			fieldIds.assign(field.size(), kNoRing);
		}

		// The limit round c: its new obstacles, then its ground unless the union covers it.
		void retake(const Vec2i64& c, RetakeStats* stats) {
			const std::int64_t		 reach = limitCircumradius(kLimitRangeMm);
			PatchEdits				 obstacles;
			std::vector<std::size_t> which;
			for (std::size_t i = 0; i < field.size(); ++i) {
				const std::int64_t dx = std::max({fieldLo[i].x - c.x, std::int64_t{0}, c.x - fieldHi[i].x});
				const std::int64_t dy = std::max({fieldLo[i].y - c.y, std::int64_t{0}, c.y - fieldHi[i].y});
				if (inMesh[i] == 0 && dx * dx + dy * dy <= reach * reach) {
					obstacles.adds.push_back(field[i]);
					which.push_back(i);
					inMesh[i] = 1;
				}
			}
			double obstacleSeconds = 0.0;
			if (!obstacles.adds.empty()) {
				PatchReport r;
				obstacleSeconds = secondsOf([&] {
					r = mesh.applyPatch(obstacles);
					mesh.refreshWidths();
				});
				for (std::size_t k = 0; k < which.size(); ++k) {
					fieldIds[which[k]] = r.added[k];
				}
			}
			const std::vector<Vec2i64> outline = limitPolygon(c, kLimitRangeMm);
			bool					   covers  = false;
			const double			   coversS = secondsOf([&] { covers = mesh.groundCovers(outline); });
			if (stats != nullptr) {
				stats->obstacleMs.push_back(msOf(obstacleSeconds));
				stats->obstacles.push_back(static_cast<double>(obstacles.adds.size()));
				stats->coversUs.push_back(coversS * 1e6);
			}
			if (covers) {
				if (stats != nullptr) {
					++stats->covered;
				}
				return;
			}
			PatchReport	 r;
			const double patchS	  = secondsOf([&] { r = mesh.applyPatch(adding({NavInputPolygon{outline, false, kProvenanceBorder}})); });
			const double refreshS = secondsOf([&] { mesh.refreshWidths(); });
			if (stats != nullptr) {
				stats->patch(r, msOf(patchS), msOf(refreshS));
			}
		}
	};

	// The field of `ground` over every box a limit of the walk can reach.
	std::vector<NavInputPolygon> fieldFor(const std::vector<Vec2i64>& centres, Ground ground) {
		if (ground == Ground::Open) {
			return {};
		}
		Vec2i64 lo;
		Vec2i64 hi;
		boxOf(centres, lo, hi);
		const Vec2i64 reach{limitCircumradius(kLimitRangeMm) + 1000, limitCircumradius(kLimitRangeMm) + 1000};
		return obstacleField(lo - reach, hi + reach, spacingOf(ground));
	}

	// The walk's limits in turn; the first (a fresh mover's) is reported apart.
	void walkLimits(LimitWalker& w, const std::vector<Vec2i64>& centres, RetakeStats& stats, RetakeStats& first) {
		w.retake(centres.front(), &first);
		for (std::size_t i = 1; i < centres.size(); ++i) {
			w.retake(centres[i], &stats);
		}
	}

	void BM_LimitWalk(benchmark::State& state, Walk walk, Ground ground) {
		const std::int64_t			step	= state.range(0);
		const std::vector<Vec2i64>	centres = walkCentres(walk, kWalkLengthMm, step);
		for (auto _ : state) {
			LimitWalker w;
			w.setField(fieldFor(centres, ground));
			RetakeStats stats;
			RetakeStats first;
			walkLimits(w, centres, stats, first);
			state.SetIterationTime(meanOf(stats.limitMs) * static_cast<double>(stats.limitMs.size()) / 1e3);
			char label[96];
			std::snprintf(label, sizeof(label), "limit walk %s %s step %lld m", walk == Walk::Straight ? "straight" : "L",
						  ground == Ground::Open ? "open" : (ground == Ground::Sparse ? "sparse" : "dense"), static_cast<long long>(step / 1000));
			stats.print(label);
			std::fprintf(stderr, "  first limit: %.0f obstacles in %.3f ms, ground %.3f ms + refresh %.3f ms; mesh %d live triangles at the end\n",
						 first.obstacles.front(), first.obstacleMs.front(), first.limitMs.front(), first.refreshMs.front(), w.mesh.liveTriangleCount());
			stats.count(state);
			state.counters["tris"] = w.mesh.liveTriangleCount();
		}
	}

	// --- Open ground: a seen area with no interior vertices ----------------------------

	struct MeshShape {
		double tris		  = 0.0;
		double longestM	  = 0.0;
		double edgeMeanM  = 0.0;
		double edgeP99M	  = 0.0;
		double aspectMean = 0.0;
		double aspectP99  = 0.0;
		double aspectMax  = 0.0;
		double over23m	  = 0.0;
	};

	double lengthM(const Vec2i64& a, const Vec2i64& b) {
		return std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)) / 1000.0;
	}

	// Live triangles `keep` accepts by centroid: their longest edge and aspect (longest edge
	// squared over twice the area, 1.15 for an equilateral).
	template <typename Keep> MeshShape shapeOf(const PatchableMesh& mesh, Keep keep) {
		std::vector<double> edges;
		std::vector<double> aspects;
		MeshShape			out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			const Vec2i64&	   a   = mesh.vertices()[tri.v[0]];
			const Vec2i64&	   b   = mesh.vertices()[tri.v[1]];
			const Vec2i64&	   c   = mesh.vertices()[tri.v[2]];
			if (!keep(Vec2i64{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3})) {
				continue;
			}
			const double longest = std::max({lengthM(a, b), lengthM(b, c), lengthM(c, a)});
			const double area2	 = std::abs(static_cast<double>(b.x - a.x) * static_cast<double>(c.y - a.y) - static_cast<double>(b.y - a.y) * static_cast<double>(c.x - a.x)) / 1e6;
			edges.push_back(longest);
			aspects.push_back(area2 > 0.0 ? longest * longest / area2 : std::numeric_limits<double>::infinity());
			out.over23m += longest > 23.0 ? 1.0 : 0.0;
		}
		out.tris	   = static_cast<double>(edges.size());
		out.longestM   = maxOf(edges);
		out.edgeMeanM  = meanOf(edges);
		out.edgeP99M   = percentileOf(edges, 0.99);
		out.aspectMean = meanOf(aspects);
		out.aspectP99  = percentileOf(aspects, 0.99);
		out.aspectMax  = maxOf(aspects);
		return out;
	}

	struct LocateCost {
		double meanNs	 = 0.0;
		double p99Ns	 = 0.0;
		double boxesMean = 0.0;
		double boxesP99	 = 0.0;
		double offMesh	 = 0.0;
	};

	// Each point located 16 times over; over the first 1,000, how many live triangles' boxes
	// hold the point (what the point grid can test, at most).
	LocateCost locateCost(const PatchableMesh& mesh, const std::vector<Vec2i64>& points) {
		constexpr int		kRepeats = 16;
		std::vector<double> ns;
		LocateCost			out;
		for (const Vec2i64& p : points) {
			std::int32_t t	= -1;
			const double s	= secondsOf([&] {
				 for (int r = 0; r < kRepeats; ++r) {
					 t = mesh.locate(p);
					 benchmark::DoNotOptimize(t);
				 }
			 });
			out.offMesh += t < 0 ? 1.0 : 0.0;
			ns.push_back(s * 1e9 / kRepeats);
		}
		std::vector<double> boxes;
		for (std::size_t i = 0; i < std::min<std::size_t>(points.size(), 1000); ++i) {
			const Vec2i64& p	 = points[i];
			int			   count = 0;
			for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
				if (!mesh.triangleLive(t)) {
					continue;
				}
				const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
				const Vec2i64&	   a   = mesh.vertices()[tri.v[0]];
				const Vec2i64&	   b   = mesh.vertices()[tri.v[1]];
				const Vec2i64&	   c   = mesh.vertices()[tri.v[2]];
				count += std::min({a.x, b.x, c.x}) <= p.x && p.x <= std::max({a.x, b.x, c.x}) && std::min({a.y, b.y, c.y}) <= p.y &&
								 p.y <= std::max({a.y, b.y, c.y})
							 ? 1
							 : 0;
			}
			boxes.push_back(count);
		}
		out.meanNs	  = meanOf(ns);
		out.p99Ns	  = percentileOf(ns, 0.99);
		out.boxesMean = meanOf(boxes);
		out.boxesP99  = percentileOf(boxes, 0.99);
		return out;
	}

	struct RouteCost {
		double expMean	= 0.0;
		double expP99	= 0.0;
		double usMean	= 0.0;
		double usP99	= 0.0;
		double notFound = 0.0;
	};

	RouteCost routeCost(const PatchableMesh& mesh, const std::vector<std::pair<Vec2i64, Vec2i64>>& pairs) {
		const NavView		view = mesh.view();
		std::vector<double> expansions;
		std::vector<double> us;
		RouteCost			out;
		for (const auto& [a, b] : pairs) {
			PathResult	 r;
			const double s = secondsOf([&] { r = pathThrough(view, a, b, 300); });
			us.push_back(s * 1e6);
			expansions.push_back(static_cast<double>(r.nodesExpanded));
			out.notFound += r.reachable ? 0.0 : 1.0;
		}
		out.expMean = meanOf(expansions);
		out.expP99	= percentileOf(expansions, 0.99);
		out.usMean	= meanOf(us);
		out.usP99	= percentileOf(us, 0.99);
		return out;
	}

	bool onFloor(const PatchableMesh& mesh, const Vec2i64& p) {
		const std::int32_t t = mesh.locate(p);
		return t >= 0 && isFloorFace(mesh.triangles()[static_cast<std::size_t>(t)]);
	}

	// Pairs `apart` from each other, both on floor inside [lo, hi].
	std::vector<std::pair<Vec2i64, Vec2i64>> pairsApart(const PatchableMesh& mesh, std::mt19937& rng, const Vec2i64& lo, const Vec2i64& hi, double apartMm,
														std::size_t n) {
		std::vector<std::pair<Vec2i64, Vec2i64>> out;
		auto within = [&](const Vec2i64& p) { return p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y; };
		while (out.size() < n) {
			const Vec2i64 a{lo.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.x - lo.x)),
							lo.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.y - lo.y))};
			const double  angle = static_cast<double>(rng() % 6283) / 1000.0;
			const Vec2i64 b{a.x + static_cast<std::int64_t>(apartMm * std::cos(angle)), a.y + static_cast<std::int64_t>(apartMm * std::sin(angle))};
			if (within(b) && onFloor(mesh, a) && onFloor(mesh, b)) {
				out.push_back({a, b});
			}
		}
		return out;
	}

	void printShape(const char* label, const MeshShape& s) {
		std::fprintf(stderr, "  %s: %.0f triangles; longest edge m mean %.2f p99 %.2f max %.2f; over 23 m %.0f; aspect mean %.1f p99 %.1f max %.0f\n", label,
					 s.tris, s.edgeMeanM, s.edgeP99M, s.longestM, s.over23m, s.aspectMean, s.aspectP99, s.aspectMax);
	}

	void printLocate(const char* label, const LocateCost& l) {
		std::fprintf(stderr, "  %s locate ns mean %.0f p99 %.0f; triangle boxes holding a point mean %.1f p99 %.0f; off mesh %.0f\n", label, l.meanNs, l.p99Ns,
					 l.boxesMean, l.boxesP99, l.offMesh);
	}

	void printRoutes(const char* label, const RouteCost& r) {
		std::fprintf(stderr, "  %s routes: expansions mean %.0f p99 %.0f; us mean %.0f p99 %.0f; not found %.0f\n", label, r.expMean, r.expP99, r.usMean, r.usP99,
					 r.notFound);
	}

	// A mover sweeps a square of side state.range(0) m at the retake step with no placed
	// obstacles, so the seen area ends as one open region with vertices only on its outer edge.
	// Then 200 placed obstacles land one at a time at random interior points, and one more
	// limit reaches past the edge.
	void BM_OpenGroundGrowth(benchmark::State& state) {
		const std::int64_t side = state.range(0) * 1000;
		const std::int64_t step = 4000;
		for (auto _ : state) {
			LimitWalker				   w;
			const std::vector<Vec2i64> centres = sweepCentres(side, step);
			RetakeStats				   all;
			std::vector<RetakeStats>   rows(static_cast<std::size_t>(side / kSweepRowMm + 1));
			for (const Vec2i64& c : centres) {
				RetakeStats one;
				w.retake(c, &one);
				RetakeStats& row = rows[static_cast<std::size_t>(c.y / kSweepRowMm)];
				row.covered += one.covered;
				all.covered += one.covered;
				for (RetakeStats* s : {&row, &all}) {
					for (std::size_t k = 0; k < one.limitMs.size(); ++k) {
						s->limitMs.push_back(one.limitMs[k]);
						s->refreshMs.push_back(one.refreshMs[k]);
						s->limitRefreshMs.push_back(one.limitRefreshMs[k]);
						s->freed.push_back(one.freed[k]);
						s->created.push_back(one.created[k]);
						s->flipped.push_back(one.flipped[k]);
						s->passes.push_back(one.passes[k]);
						s->boxM2.push_back(one.boxM2[k]);
					}
					s->coversUs.insert(s->coversUs.end(), one.coversUs.begin(), one.coversUs.end());
				}
			}
			char label[96];
			std::snprintf(label, sizeof(label), "open sweep %lld m", static_cast<long long>(side / 1000));
			all.print(label);
			for (std::size_t r = 0; r < rows.size(); ++r) {
				std::fprintf(stderr, "  row %zu: %zu limits, patch ms mean %.3f max %.3f, refresh ms mean %.3f, hole mean %.0f max %.0f, box m2 mean %.0f max %.0f\n", r,
							 rows[r].limitMs.size(), meanOf(rows[r].limitMs), maxOf(rows[r].limitMs), meanOf(rows[r].refreshMs), meanOf(rows[r].freed),
							 maxOf(rows[r].freed), meanOf(rows[r].boxM2), maxOf(rows[r].boxM2));
			}
			const MeshShape shape = shapeOf(w.mesh, [](const Vec2i64&) { return true; });
			printShape("whole mesh", shape);

			std::mt19937		 rng(13);
			std::vector<Vec2i64> points;
			while (points.size() < 10000) {
				points.push_back({static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side)), static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side))});
			}
			const LocateCost locate = locateCost(w.mesh, points);
			printLocate("interior", locate);
			const RouteCost near = routeCost(w.mesh, pairsApart(w.mesh, rng, {0, 0}, {side, side}, 40000.0, 200));
			const RouteCost far	 = routeCost(w.mesh, pairsApart(w.mesh, rng, {0, 0}, {side, side}, 400000.0, 100));
			printRoutes("40 m", near);
			printRoutes("400 m", far);

			RetakeStats drops;
			for (int i = 0; i < 200; ++i) {
				const Vec2i64	   c{20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side - 40000)),
								 20000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(side - 40000))};
				const std::int64_t e = 275;
				PatchReport		   r;
				const double	   patchS	= secondsOf([&] { r = w.mesh.applyPatch(adding({NavInputPolygon{rect(c.x - e, c.y - e, c.x + e, c.y + e), true, kProvenancePlaced}})); });
				const double	   refreshS = secondsOf([&] { w.mesh.refreshWidths(); });
				drops.patch(r, msOf(patchS), msOf(refreshS));
			}
			std::snprintf(label, sizeof(label), "open sweep %lld m: placed obstacles dropped one at a time", static_cast<long long>(side / 1000));
			drops.print(label);
			RetakeStats edge;
			w.retake({side + 40000, side / 2}, &edge);
			std::snprintf(label, sizeof(label), "open sweep %lld m: one more limit past the edge", static_cast<long long>(side / 1000));
			edge.print(label);
			printShape("whole mesh after", shapeOf(w.mesh, [](const Vec2i64&) { return true; }));

			state.SetIterationTime(meanOf(all.limitMs) * static_cast<double>(all.limitMs.size()) / 1e3);
			all.count(state);
			state.counters["tris"]			 = shape.tris;
			state.counters["longestM"]		 = shape.longestM;
			state.counters["locateNs"]		 = locate.meanNs;
			state.counters["locateP99Ns"]	 = locate.p99Ns;
			state.counters["boxesAtPoint"]	 = locate.boxesMean;
			state.counters["route40Exp"]	 = near.expMean;
			state.counters["route400Exp"]	 = far.expMean;
			state.counters["dropBothP99Ms"]	 = percentileOf(drops.limitRefreshMs, 0.99);
			state.counters["dropBothMaxMs"]	 = maxOf(drops.limitRefreshMs);
			state.counters["edgeLimitMs"]	 = edge.limitMs.empty() ? 0.0 : edge.limitMs.front();
		}
	}

	// --- A walked band's edge, and the edge index under it --------------------------------

	// A 300 m straight walk at the retake step through dense placed obstacles.
	void walkDenseBand(LimitWalker& w) {
		const std::vector<Vec2i64> centres = walkCentres(Walk::Straight, kWalkLengthMm, 4000);
		w.setField(fieldFor(centres, Ground::Dense));
		RetakeStats stats;
		RetakeStats first;
		walkLimits(w, centres, stats, first);
	}

	// Where the band's middle third lies: clear of the walk's two ends.
	constexpr std::int64_t kBandFromMm = 100000;
	constexpr std::int64_t kBandToMm   = 200000;

	// The mesh boundary (the union's edge, every edge with nothing across it) beside the
	// band's middle third.
	std::vector<std::pair<Vec2i64, Vec2i64>> boundaryBeside(const PatchableMesh& mesh) {
		std::vector<std::pair<Vec2i64, Vec2i64>> out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& p = mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& q = mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if (tri.neighbor[static_cast<std::size_t>(e)] < 0 && std::max(p.x, q.x) >= kBandFromMm - 10000 && std::min(p.x, q.x) <= kBandToMm + 10000) {
					out.push_back({p, q});
				}
			}
		}
		return out;
	}

	double distanceToMm(const Vec2i64& p, const std::vector<std::pair<Vec2i64, Vec2i64>>& segments) {
		double best = std::numeric_limits<double>::infinity();
		for (const auto& [a, b] : segments) {
			const double dx = static_cast<double>(b.x - a.x);
			const double dy = static_cast<double>(b.y - a.y);
			const double px = static_cast<double>(p.x - a.x);
			const double py = static_cast<double>(p.y - a.y);
			const double t	= std::clamp((px * dx + py * dy) / std::max(1.0, dx * dx + dy * dy), 0.0, 1.0);
			best			= std::min(best, std::hypot(px - t * dx, py - t * dy));
		}
		return best;
	}

	constexpr double kEdgeBandMm = 4000.0;

	// Plan M2: triangles within 4 m of the band's side against its interior (within 20 m of
	// the walk), over the band's middle third.
	void BM_MeshEdge(benchmark::State& state) {
		for (auto _ : state) {
			LimitWalker w;
			walkDenseBand(w);
			const std::vector<std::pair<Vec2i64, Vec2i64>> boundary = boundaryBeside(w.mesh);
			auto inThird = [](const Vec2i64& p) { return p.x >= kBandFromMm && p.x <= kBandToMm; };
			auto atEdge	 = [&](const Vec2i64& p) { return inThird(p) && distanceToMm(p, boundary) <= kEdgeBandMm; };
			auto inside	 = [&](const Vec2i64& p) { return inThird(p) && std::abs(p.y) <= 20000; };
			const MeshShape edgeShape	= shapeOf(w.mesh, atEdge);
			const MeshShape innerShape	= shapeOf(w.mesh, inside);
			const MeshShape wholeShape	= shapeOf(w.mesh, [](const Vec2i64&) { return true; });

			std::mt19937		 rng(21);
			std::vector<Vec2i64> edgePoints;
			std::vector<Vec2i64> innerPoints;
			const std::int64_t	 rc = limitCircumradius(kLimitRangeMm);
			while (edgePoints.size() < 10000) {
				const std::int64_t y = kLimitRangeMm - static_cast<std::int64_t>(kEdgeBandMm) + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(rc - kLimitRangeMm + static_cast<std::int64_t>(kEdgeBandMm)));
				const Vec2i64	   p{kBandFromMm + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(kBandToMm - kBandFromMm)), (rng() % 2 == 0) ? y : -y};
				if (w.mesh.locate(p) >= 0 && distanceToMm(p, boundary) <= kEdgeBandMm) {
					edgePoints.push_back(p);
				}
			}
			while (innerPoints.size() < 10000) {
				innerPoints.push_back({kBandFromMm + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(kBandToMm - kBandFromMm)),
									   static_cast<std::int64_t>(rng() % 40001) - 20000});
			}
			const LocateCost edgeLocate	 = locateCost(w.mesh, edgePoints);
			const LocateCost innerLocate = locateCost(w.mesh, innerPoints);

			// 40 m along the side, 2 m in from where every limit's side lies, and 40 m pairs in
			// any direction inside.
			std::vector<std::pair<Vec2i64, Vec2i64>> edgePairs;
			while (edgePairs.size() < 500) {
				const std::int64_t y = (rng() % 2 == 0 ? 1 : -1) * (kLimitRangeMm - 2000);
				const Vec2i64	   a{kBandFromMm + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(kBandToMm - kBandFromMm - 40000)), y};
				const Vec2i64	   b{a.x + 40000, y};
				if (onFloor(w.mesh, a) && onFloor(w.mesh, b)) {
					edgePairs.push_back({a, b});
				}
			}
			const RouteCost edgeRoutes	= routeCost(w.mesh, edgePairs);
			const RouteCost innerRoutes = routeCost(w.mesh, pairsApart(w.mesh, rng, {kBandFromMm, -20000}, {kBandToMm, 20000}, 40000.0, 500));

			std::fprintf(stderr, "[mesh edge] dense straight band, step 4 m; %zu boundary segments beside the middle third\n", boundary.size());
			printShape("side (within 4 m)", edgeShape);
			printShape("interior", innerShape);
			printShape("whole mesh", wholeShape);
			printLocate("side", edgeLocate);
			printLocate("interior", innerLocate);
			printRoutes("side 40 m", edgeRoutes);
			printRoutes("interior 40 m", innerRoutes);
			state.SetIterationTime(1e-9 * edgeLocate.meanNs);
			state.counters["sideLongestM"]	  = edgeShape.longestM;
			state.counters["sideAspectP99"]	  = edgeShape.aspectP99;
			state.counters["sideLocateNs"]	  = edgeLocate.meanNs;
			state.counters["sideLocateP99Ns"] = edgeLocate.p99Ns;
			state.counters["sideExpMean"]	  = edgeRoutes.expMean;
			state.counters["innerLocateNs"]	  = innerLocate.meanNs;
			state.counters["innerExpMean"]	  = innerRoutes.expMean;
			state.counters["over23m"]		  = wholeShape.over23m;
		}
	}

	// PatchableMesh's private box queries over its ring edge indexes, reached through explicit
	// instantiations, which the language exempts from access checking. Nothing here writes.
	using EdgesInBox = void (PatchableMesh::*)(const Vec2i64&, const Vec2i64&, std::vector<std::int32_t>&, std::vector<EdgeSource>&) const;
	struct BlockedEdgesKey {
		friend EdgesInBox edgesInBoxOf(BlockedEdgesKey);
	};
	struct BoundaryEdgesKey {
		friend EdgesInBox edgesInBoxOf(BoundaryEdgesKey);
	};
	template <typename Key, EdgesInBox Member> struct EdgesInBoxReach {
		friend EdgesInBox edgesInBoxOf(Key) { return Member; }
	};
	template struct EdgesInBoxReach<BlockedEdgesKey, &PatchableMesh::blockedEdgesInBox>;
	template struct EdgesInBoxReach<BoundaryEdgesKey, &PatchableMesh::boundaryEdgesInBox>;

	// Plan M3: the 6 m box queries over the ring edge indexes that touchSegment makes (blocked
	// edges, then bounds edges with boundary intervals; placed obstacles' patches ask the
	// first), in the walked dense band, against the same placed obstacles under one ground
	// rectangle with no limits; and a 0.55 m placed obstacle added at clear points of the band's
	// middle third, timed with its width refresh, in each.
	void BM_EdgeIndexQuery(benchmark::State& state) {
		for (auto _ : state) {
			LimitWalker w;
			walkDenseBand(w);
			PatchableMesh				 field;
			std::vector<NavInputPolygon> rings;
			Vec2i64						 lo{std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max()};
			Vec2i64						 hi{std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()};
			for (std::int32_t t = 0; t < static_cast<std::int32_t>(w.mesh.triangles().size()); ++t) {
				if (w.mesh.triangleLive(t)) {
					for (const std::uint32_t v : w.mesh.triangles()[static_cast<std::size_t>(t)].v) {
						lo = {std::min(lo.x, w.mesh.vertices()[v].x), std::min(lo.y, w.mesh.vertices()[v].y)};
						hi = {std::max(hi.x, w.mesh.vertices()[v].x), std::max(hi.y, w.mesh.vertices()[v].y)};
					}
				}
			}
			rings.push_back(NavInputPolygon{rect(lo.x, lo.y, hi.x, hi.y), false, kProvenanceBorder});
			for (std::size_t i = 0; i < w.field.size(); ++i) {
				if (w.inMesh[i] != 0) {
					rings.push_back(w.field[i]);
				}
			}
			untimedPatch(field, adding(rings));

			const EdgesInBox		  blocked	  = edgesInBoxOf(BlockedEdgesKey{});
			const EdgesInBox		  boundaries = edgesInBoxOf(BoundaryEdgesKey{});
			std::mt19937			  rng(31);
			std::vector<Vec2i64>	  centres;
			while (centres.size() < 10000) {
				centres.push_back({kBandFromMm + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(kBandToMm - kBandFromMm)),
								   static_cast<std::int64_t>(rng() % 80001) - 40000});
			}
			std::vector<std::int32_t> ids;
			std::vector<EdgeSource>	  found;
			const Vec2i64			  half{3000, 3000};
			auto					  run = [&](const PatchableMesh& m, double& ns, double& edges) {
				 edges = 0.0;
				 for (const Vec2i64& c : centres) {
					 for (const EdgesInBox query : {blocked, boundaries}) {
						 (m.*query)(c - half, c + half, ids, found);
						 edges += static_cast<double>(found.size());
					 }
				 }
				 ns = 1e9 * secondsOf([&] {
					 for (int r = 0; r < 4; ++r) {
						 for (const Vec2i64& c : centres) {
							 for (const EdgesInBox query : {blocked, boundaries}) {
								 (m.*query)(c - half, c + half, ids, found);
								 benchmark::DoNotOptimize(found.data());
							 }
						 }
					 }
				 }) / (4.0 * static_cast<double>(centres.size()));
				 edges /= static_cast<double>(centres.size());
			};
			double bandNs	 = 0.0;
			double bandEdges = 0.0;
			double fieldNs	 = 0.0;
			double fieldEdges = 0.0;
			run(w.mesh, bandNs, bandEdges);
			run(field, fieldNs, fieldEdges);

			std::vector<Vec2i64> spots;
			while (spots.size() < 200) {
				const Vec2i64 c{kBandFromMm + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(kBandToMm - kBandFromMm)),
								static_cast<std::int64_t>(rng() % 60001) - 30000};
				bool clear = true;
				for (std::size_t i = 0; i < w.field.size() && clear; ++i) {
					clear = w.fieldHi[i].x < c.x - 1000 || w.fieldLo[i].x > c.x + 1000 || w.fieldHi[i].y < c.y - 1000 || w.fieldLo[i].y > c.y + 1000;
				}
				if (clear) {
					spots.push_back(c);
				}
			}
			auto drop = [&](PatchableMesh& m) {
				std::vector<double> ms;
				for (const Vec2i64& c : spots) {
					PatchReport	 r;
					const double s = secondsOf([&] {
						r = m.applyPatch(adding({NavInputPolygon{rect(c.x - 275, c.y - 275, c.x + 275, c.y + 275), true, kProvenancePlaced}}));
						m.refreshWidths();
					});
					ms.push_back(msOf(s));
					untimedPatch(m, removing(r.added.front()));
				}
				return ms;
			};
			const std::vector<double> bandDrop	= drop(w.mesh);
			const std::vector<double> fieldDrop = drop(field);
			std::fprintf(stderr,
						 "[edge index] 6 m box query: walked band %.0f ns, %.1f ring edges; one ground rectangle %.0f ns, %.1f ring edges; ratio %.2f\n"
						 "  placed obstacle add+refresh ms: walked band mean %.3f p99 %.3f; one ground rectangle mean %.3f p99 %.3f\n",
						 bandNs, bandEdges, fieldNs, fieldEdges, bandNs / fieldNs, meanOf(bandDrop), percentileOf(bandDrop, 0.99), meanOf(fieldDrop),
						 percentileOf(fieldDrop, 0.99));
			state.SetIterationTime(1e-9 * bandNs);
			state.counters["bandNs"]		 = bandNs;
			state.counters["fieldNs"]		 = fieldNs;
			state.counters["ratio"]			 = bandNs / fieldNs;
			state.counters["bandEdges"]		 = bandEdges;
			state.counters["fieldEdges"]	 = fieldEdges;
			state.counters["bandDropMs"]	 = meanOf(bandDrop);
			state.counters["fieldDropMs"]	 = meanOf(fieldDrop);
		}
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
BENCHMARK_CAPTURE(BM_LabelUpkeep, manyTrees, Fixture::ManyTrees)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LabelUpkeep, control, Fixture::Control)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LabelUpkeep, realRings, Fixture::RealRings)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LabelUpkeepRoundBarrier)->Arg(100)->Arg(400)->UseManualTime()->Unit(benchmark::kMicrosecond);
#define BOUNDS_ROWS(name, ...)                                                                                                         	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, manyTrees##name, Fixture::ManyTrees, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond); 	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, control##name, Fixture::Control, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond);     	BENCHMARK_CAPTURE(BM_PatchAcrossBounds, realRings##name, Fixture::RealRings, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond)

BOUNDS_ROWS(_border128_add, 0, 0);
BOUNDS_ROWS(_border128_remove, 0, 1);
BOUNDS_ROWS(_border128_footprint, 0, 2);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_add, Fixture::Control, 512000, 0)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_remove, Fixture::Control, 512000, 1)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_footprint, Fixture::Control, 512000, 2)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_wall_add, Fixture::Control, 512000, 3)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_PatchAcrossBounds, shared512_structural_wall_add, Fixture::Control, 512000, 3, false)->UseManualTime()->Unit(benchmark::kMicrosecond);
PATCH_ROWS(_allAtOnce, BM_PatchTreesEnter, std::size_t{0}, Refresh::Untimed);
PATCH_ROWS(_allAtOnce_refreshed, BM_PatchTreesEnter, std::size_t{0}, Refresh::Timed);
PATCH_ROWS(_batch64, BM_PatchTreesEnter, std::size_t{64}, Refresh::Untimed);
PATCH_ROWS(_batch16, BM_PatchTreesEnter, std::size_t{16}, Refresh::Untimed);

BENCHMARK_CAPTURE(BM_PatchFirstBuild, manyTrees, Fixture::ManyTrees)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PatchFirstBuild, control, Fixture::Control)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_PatchFirstBuild, realRings, Fixture::RealRings)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PatchFirstBuildTiled)->Arg(2)->Arg(4)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PatchChunkGround)->UseManualTime()->Unit(benchmark::kMillisecond);

#define CHUNK_ROW(name, ...) BENCHMARK_CAPTURE(BM_ChunkEnters, name, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond)
CHUNK_ROW(open512_alone, Beside::Nothing, Fill::Open, Fill::Open, 4);
CHUNK_ROW(open512_side, Beside::Side, Fill::Open, Fill::Open, 4);
CHUNK_ROW(open512_twoSides, Beside::TwoSides, Fill::Open, Fill::Open, 4);
CHUNK_ROW(open512_corner, Beside::Corner, Fill::Open, Fill::Open, 4);
CHUNK_ROW(open512_allSides, Beside::AllSides, Fill::Open, Fill::Open, 4);
CHUNK_ROW(open512_side_waterNeighbour, Beside::Side, Fill::Water, Fill::Open, 4);
CHUNK_ROW(open512_side_treeNeighbour, Beside::Side, Fill::WaterTrees, Fill::Open, 4);
CHUNK_ROW(open128_side, Beside::Side, Fill::Open, Fill::Open, 1);
CHUNK_ROW(water512_alone, Beside::Nothing, Fill::Open, Fill::Water, 4);
CHUNK_ROW(water512_side, Beside::Side, Fill::Open, Fill::Water, 4);
CHUNK_ROW(water512_side_waterNeighbour, Beside::Side, Fill::Water, Fill::Water, 4);
CHUNK_ROW(water128_side_waterNeighbour, Beside::Side, Fill::Water, Fill::Water, 1);

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
#define REQUEST_ROW(name, ...) BENCHMARK_CAPTURE(BM_RequestShape, name, __VA_ARGS__)->UseManualTime()->Unit(benchmark::kMicrosecond)
REQUEST_ROW(control, Fixture::Control);
REQUEST_ROW(realRings, Fixture::RealRings);
REQUEST_ROW(control_middle, Fixture::Control, Colony::Middle);
REQUEST_ROW(control_side12, Fixture::Control, Colony::NearSide);
REQUEST_ROW(realRings_middle, Fixture::RealRings, Colony::Middle);
REQUEST_ROW(realRings_side12, Fixture::RealRings, Colony::NearSide);
REQUEST_ROW(realRings_chunkMiddle, Fixture::RealRings, Colony::ChunkMiddle);
REQUEST_ROW(realRings_chunkSide12, Fixture::RealRings, Colony::ChunkSide);

BENCHMARK_CAPTURE(BM_LargeTree, tiled_lone_add, 0, false, true)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeTree, tiled_lone_remove, 0, false, false)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeTree, squares_lone_add, 1, false, true)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeTree, squares_lone_remove, 1, false, false)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeTree, tiled_shoreline_add, 0, true, true)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeTree, squares_shoreline_add, 1, true, true)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeBoundsTree, tiled_border, 0)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_LargeBoundsTree, squares_shared_side, 1)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_MeshGrowsWorstPatch)->Arg(10)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_WalledGoal, unlimited, false)->Arg(3)->Arg(6)->Arg(9)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_WalledGoal, maxExpansions2000, true)->Arg(3)->Arg(6)->Arg(9)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_WalkingDistance, cap1x, 1.0)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_WalkingDistance, cap2x, 2.0)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_WalkingDistance, cap4x, 4.0)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_FloraCellsEnter, twoCells, 2, false)->Arg(1)->Arg(3)->Arg(9)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_FloraCellsEnter, twoCells_openTile, 2, true)->Arg(1)->Arg(3)->Arg(9)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_FloraCellsEnter, loneTree_openTile, 0, true)->Arg(1)->Arg(3)->Arg(9)->UseManualTime()->Unit(benchmark::kMicrosecond);

// The argument of every limit walk row is the retake step in mm; of the sweep, its side in m.
#define LIMIT_WALK_ROW(name, ...) \
	BENCHMARK_CAPTURE(BM_LimitWalk, name, __VA_ARGS__)->Arg(4000)->Arg(16000)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond)
LIMIT_WALK_ROW(straight_open, Walk::Straight, Ground::Open);
LIMIT_WALK_ROW(straight_sparse, Walk::Straight, Ground::Sparse);
LIMIT_WALK_ROW(straight_dense, Walk::Straight, Ground::Dense);
LIMIT_WALK_ROW(ell_open, Walk::Ell, Ground::Open);
LIMIT_WALK_ROW(ell_sparse, Walk::Ell, Ground::Sparse);
LIMIT_WALK_ROW(ell_dense, Walk::Ell, Ground::Dense);
BENCHMARK(BM_OpenGroundGrowth)->Arg(500)->Arg(1000)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_MeshEdge)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_EdgeIndexQuery)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
