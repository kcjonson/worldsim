#include "nav/MeshComponents.h"

#include "nav/NavMesh.h"
#include "nav/NavMeshRealRings.test.h"
#include "nav/PatchableMesh.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>

using geometry::Vec2i64;
using namespace geometry::nav;

namespace {

	constexpr std::int64_t kWater	  = -1;
	constexpr std::int64_t kPlaced	  = -2;
	constexpr std::int64_t kBorder	  = -3;
	constexpr std::int64_t kFootprint = -5;
	constexpr std::int64_t kWall	  = 7;

	NavInputPolygon blocked(std::vector<Vec2i64> ring, std::int64_t tag = kPlaced, bool holeCapable = false) {
		return NavInputPolygon{std::move(ring), true, tag, kNoOpening, holeCapable};
	}

	NavInputPolygon bounds(std::vector<Vec2i64> ring) {
		return NavInputPolygon{std::move(ring), false, kBorder};
	}

	// Test coordinates are in units of kUnit mm, so ground is at game scale.
	constexpr std::int64_t kUnit = 100;

	std::vector<Vec2i64> rect(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0 * kUnit, y0 * kUnit}, {x1 * kUnit, y0 * kUnit}, {x1 * kUnit, y1 * kUnit}, {x0 * kUnit, y1 * kUnit}};
	}

	std::vector<Vec2i64> translated(std::vector<Vec2i64> ring, Vec2i64 by) {
		for (Vec2i64& p : ring) {
			p = p + Vec2i64{by.x * kUnit, by.y * kUnit};
		}
		return ring;
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

	// The labels against a from-scratch flood of the live passable triangles: the same
	// partition, every size right, and no label on a dead or blocked slot.
	::testing::AssertionResult matchesFlood(const PatchableMesh& mesh) {
		const NavView		  view	= mesh.view();
		const MeshComponents& comps = mesh.components();
		const std::int32_t	  slots = view.triangleSlots();
		std::vector<std::int32_t> flood(static_cast<std::size_t>(slots), -1);
		std::vector<std::int32_t> floodSize;
		for (std::int32_t s = 0; s < slots; ++s) {
			if (!view.isLive(s) || !comps.passable(view.triangle(s)) || flood[static_cast<std::size_t>(s)] >= 0) {
				continue;
			}
			const std::int32_t		  id = static_cast<std::int32_t>(floodSize.size());
			std::vector<std::int32_t> queue{s};
			flood[static_cast<std::size_t>(s)] = id;
			for (std::size_t head = 0; head < queue.size(); ++head) {
				for (const std::int32_t nb : view.triangle(queue[head]).neighbor) {
					if (nb >= 0 && flood[static_cast<std::size_t>(nb)] < 0 && comps.passable(view.triangle(nb))) {
						flood[static_cast<std::size_t>(nb)] = id;
						queue.push_back(nb);
					}
				}
			}
			floodSize.push_back(static_cast<std::int32_t>(queue.size()));
		}
		std::map<std::int32_t, ComponentLabel> ours;
		std::map<ComponentLabel, std::int32_t> theirs;
		for (std::int32_t t = 0; t < slots; ++t) {
			const std::int32_t	 f = flood[static_cast<std::size_t>(t)];
			const ComponentLabel l = comps.label(t);
			if (f < 0) {
				if (l != kNoComponent) {
					return ::testing::AssertionFailure() << "triangle " << t << " is dead or blocked but has label " << l;
				}
				continue;
			}
			if (l == kNoComponent) {
				return ::testing::AssertionFailure() << "passable triangle " << t << " has no label";
			}
			const auto [o, newOurs]	  = ours.emplace(f, l);
			const auto [r, newTheirs] = theirs.emplace(l, f);
			if (o->second != l || r->second != f) {
				return ::testing::AssertionFailure() << "triangle " << t << ": label " << l << " and flood component " << f << " disagree";
			}
		}
		for (const auto& [f, l] : ours) {
			if (comps.componentSize(l) != floodSize[static_cast<std::size_t>(f)]) {
				return ::testing::AssertionFailure() << "label " << l << " counts " << comps.componentSize(l) << ", the flood " << floodSize[static_cast<std::size_t>(f)];
			}
		}
		return ::testing::AssertionSuccess();
	}

	struct Snapshot {
		std::vector<NavTriangle>  tris;
		std::vector<std::uint8_t> live;
	};

	Snapshot snapshot(const PatchableMesh& mesh) {
		Snapshot s;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			s.tris.push_back(mesh.triangles()[static_cast<std::size_t>(t)]);
			s.live.push_back(mesh.triangleLive(t) ? 1 : 0);
		}
		return s;
	}

	std::uint8_t whyOf(const PatchReport& r, std::int32_t t) {
		const auto it = std::lower_bound(r.changes.begin(), r.changes.end(), t, [](const ChangedTriangle& c, std::int32_t id) { return c.id < id; });
		return it != r.changes.end() && it->id == t ? it->why : 0;
	}

	// Every id whose triangle, liveness, or neighbours changed is in the patch report with
	// the right reason; every width the refresh changed is in its report.
	::testing::AssertionResult reportsCover(const Snapshot& before, const PatchableMesh& mesh, const PatchReport& patch, const Snapshot& patched,
											const PatchReport& refresh) {
		for (std::size_t i = 1; i < patch.changes.size(); ++i) {
			if (patch.changes[i - 1].id >= patch.changes[i].id) {
				return ::testing::AssertionFailure() << "patch changes not ascending and unique";
			}
		}
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(patched.tris.size()); ++t) {
			const std::size_t  i	   = static_cast<std::size_t>(t);
			const bool		   wasLive = i < before.live.size() && before.live[i] != 0;
			const bool		   isLive  = patched.live[i] != 0;
			const std::uint8_t why	   = whyOf(patch, t);
			if (wasLive && !isLive && (why & kTriangleFreed) == 0) {
				return ::testing::AssertionFailure() << "triangle " << t << " went without kTriangleFreed";
			}
			if (!isLive) {
				continue;
			}
			const bool sameCorners = wasLive && before.tris[i].v == patched.tris[i].v;
			if (!sameCorners && (why & kTriangleCreated) == 0) {
				return ::testing::AssertionFailure() << "triangle " << t << " is new without kTriangleCreated";
			}
			if (sameCorners && before.tris[i].neighbor != patched.tris[i].neighbor && (why & (kTriangleBorder | kTriangleCreated)) == 0) {
				return ::testing::AssertionFailure() << "triangle " << t << "'s neighbours changed without kTriangleBorder";
			}
			if ((why & kTriangleBorder) != 0 && (why & kTriangleCreated) == 0 && !sameCorners) {
				return ::testing::AssertionFailure() << "border triangle " << t << " is not a kept one";
			}
		}
		const auto&		  tris = mesh.triangles();
		std::size_t		  next = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(tris.size()); ++t) {
			const std::size_t i		  = static_cast<std::size_t>(t);
			const bool		  changed = tris[i].edgePairWidthMm != patched.tris[i].edgePairWidthMm || tris[i].structuralWidthMm != patched.tris[i].structuralWidthMm;
			const bool		  listed  = next < refresh.changes.size() && refresh.changes[next].id == t;
			if (changed != listed) {
				return ::testing::AssertionFailure() << "triangle " << t << (changed ? "'s widths changed but are not reported" : " is reported with no width change");
			}
			if (listed && refresh.changes[next++].why != kTriangleWidths) {
				return ::testing::AssertionFailure() << "refresh reports " << t << " for something other than widths";
			}
		}
		return ::testing::AssertionSuccess();
	}

	std::uint64_t pick(std::mt19937& rng, std::uint64_t n) {
		return static_cast<std::uint64_t>(rng()) % n;
	}

	std::int64_t between(std::mt19937& rng, std::int64_t lo, std::int64_t hi) {
		return lo + static_cast<std::int64_t>(pick(rng, static_cast<std::uint64_t>(hi - lo + 1)));
	}

	constexpr std::int64_t kSquare = 400;

	// Posts on a jittered grid over a square: they cut its triangles small, so a patch's
	// hole is a small part of the ground and the labels' local paths do the work.
	std::vector<NavInputPolygon> posts(std::mt19937& rng, std::int64_t x0) {
		std::vector<NavInputPolygon> out;
		for (std::int64_t y = 10; y < kSquare; y += 25) {
			for (std::int64_t x = x0 + 10; x < x0 + kSquare; x += 25) {
				const std::int64_t px = x + between(rng, -6, 6);
				const std::int64_t py = y + between(rng, -6, 6);
				out.push_back(blocked(rect(px, py, px + 2, py + 2)));
			}
		}
		return out;
	}

	// Rings meant to cut the ground: bars, boxes, a closed pen of four bars (a pocket),
	// and water with an island, under every tag class the labels distinguish.
	std::vector<NavInputPolygon> randomRings(std::mt19937& rng) {
		static constexpr std::int64_t kTags[] = {kPlaced, kPlaced, kPlaced, kWall, kFootprint};
		const std::int64_t			  tag	  = kTags[pick(rng, 5)];
		const std::int64_t			  x		  = between(rng, -10, 2 * kSquare - 40);
		const std::int64_t			  y		  = between(rng, -10, kSquare - 40);
		switch (pick(rng, 5)) {
			case 0:
				return {blocked(rect(x, y, x + between(rng, 30, 160), y + between(rng, 2, 6)), tag)};
			case 1:
				return {blocked(rect(x, y, x + between(rng, 2, 6), y + between(rng, 30, 160)), tag)};
			case 2:
				return {blocked(rect(x, y, x + between(rng, 4, 30), y + between(rng, 4, 30)), tag)};
			case 3: {
				const std::int64_t s = between(rng, 12, 40);
				const std::int64_t t = between(rng, 2, 4);
				return {blocked(rect(x, y, x + s, y + t), tag), blocked(rect(x, y + s - t, x + s, y + s), tag), blocked(rect(x, y, x + t, y + s), tag),
						blocked(rect(x + s - t, y, x + s, y + s), tag)};
			}
			default: {
				const std::int64_t w = between(rng, 20, 60);
				return {blocked(rect(x, y, x + w, y + w), kWater, true), blocked(rect(x + 5, y + 5, x + w - 5, y + w - 5), kWater, true)};
			}
		}
	}

	struct Totals {
		std::int64_t patches = 0;
		std::int64_t splits	 = 0;
		std::int64_t merges	 = 0;
		std::int64_t fresh	 = 0;
		std::int64_t emptied = 0;
		std::int64_t visited = 0;

		void add(const ComponentUpkeep& u) {
			++patches;
			splits += u.splits;
			merges += u.merges;
			fresh += u.fresh;
			emptied += u.emptied;
			visited += u.visited;
		}
	};

	// Two posted squares side by side; the second comes, goes, and comes back. Every patch
	// is checked against a flood and its report against the mesh it changed.
	void runSequence(std::uint32_t seed, int steps, Totals& totals) {
		std::mt19937		rng(seed);
		PatchableMesh		mesh({.maxBoundsEdgeMm = seed % 2 == 0 ? 50 * kUnit : 0, .componentOpenTags = {kFootprint}});
		const NavInputPolygon west = bounds(rect(0, 0, kSquare, kSquare));
		const NavInputPolygon east = bounds(rect(kSquare, 0, 2 * kSquare, kSquare));
		std::vector<NavInputPolygon> base = posts(rng, 0);
		for (NavInputPolygon& p : posts(rng, kSquare)) {
			base.push_back(std::move(p));
		}
		base.push_back(west);
		mesh.applyPatch(adding(base));
		mesh.refreshWidths();
		RingId							   second = kNoRing;
		std::map<RingId, NavInputPolygon> placed;
		for (int step = 0; step < steps; ++step) {
			PatchEdits edits;
			if (second == kNoRing && pick(rng, 6) == 0) {
				edits.adds.push_back(east);
			} else if (second != kNoRing && pick(rng, 15) == 0) {
				edits.removes.push_back(second);
				second = kNoRing;
			}
			const bool		boundsAdded = !edits.adds.empty();
			const unsigned op			= static_cast<unsigned>(pick(rng, 10));
			if (op < 5 || placed.size() < 3) {
				for (std::size_t i = 1 + pick(rng, 2); i > 0; --i) {
					for (NavInputPolygon& r : randomRings(rng)) {
						edits.adds.push_back(std::move(r));
					}
				}
			} else if (op < 8) {
				for (std::size_t i = 1 + pick(rng, 2); i > 0 && !placed.empty(); --i) {
					auto it = placed.begin();
					std::advance(it, static_cast<std::ptrdiff_t>(pick(rng, placed.size())));
					edits.removes.push_back(it->first);
					placed.erase(it);
				}
			} else {
				auto it = placed.begin();
				std::advance(it, static_cast<std::ptrdiff_t>(pick(rng, placed.size())));
				it->second.ring = translated(it->second.ring, {between(rng, -12, 12), between(rng, -12, 12)});
				edits.moves.push_back({it->first, it->second.ring});
			}
			const Snapshot		 before	 = snapshot(mesh);
			const std::uint64_t	 epoch	 = mesh.components().epoch();
			const PatchReport	 report	 = mesh.applyPatch(edits);
			const ComponentUpkeep upkeep = mesh.components().lastUpkeep();
			const Snapshot		 patched = snapshot(mesh);
			const PatchReport	 refresh = mesh.refreshWidths();
			totals.add(upkeep);
			for (std::size_t i = 0; i < report.added.size(); ++i) {
				if (boundsAdded && i == 0) {
					second = report.added[i];
				} else {
					placed[report.added[i]] = edits.adds[i];
				}
			}
			ASSERT_TRUE(matchesFlood(mesh)) << "seed " << seed << " step " << step;
			ASSERT_TRUE(reportsCover(before, mesh, report, patched, refresh)) << "seed " << seed << " step " << step;
			const bool shouldMove = report.opens || upkeep.splits > 0 || upkeep.merges > 0 || upkeep.fresh > 0;
			ASSERT_EQ(mesh.components().epoch() != epoch, shouldMove) << "seed " << seed << " step " << step;
		}
	}

	Vec2i64 at(std::int64_t x, std::int64_t y) {
		return {x * kUnit, y * kUnit};
	}

} // namespace

// The randomized check the labels are built on: after every add, remove, and move (bounds
// arriving and leaving, bars cutting the ground, water, walls, footprints), the labels equal
// a from-scratch flood. The totals show splits, merges, and new components all happened.
TEST(MeshComponents, LabelsEqualAFloodAfterEveryPatch) {
	Totals totals;
	for (std::uint32_t seed = 1; seed <= 30; ++seed) {
		runSequence(seed, 150, totals);
		if (HasFatalFailure()) {
			return;
		}
	}
	EXPECT_GT(totals.splits, 10);
	EXPECT_GT(totals.merges, 20);
	EXPECT_GT(totals.fresh, 20);
	EXPECT_GT(totals.emptied, 0);
	std::ostringstream visitedPerPatch;
	visitedPerPatch << static_cast<double>(totals.visited) / static_cast<double>(totals.patches) << " over " << totals.patches << " patches, " << totals.splits
					<< " splits, " << totals.merges << " merges, " << totals.fresh << " fresh, " << totals.emptied << " emptied";
	RecordProperty("VisitedPerPatch", visitedPerPatch.str());
}

// Real game rings: trees in and out over a chunk with water. Labels stay a flood's, and the
// per-patch flood work stays local.
TEST(MeshComponents, RealRingsTreesInAndOut) {
	namespace td = geometry::nav::testdata;
	PatchableMesh mesh;
	mesh.applyPatch(adding({bounds(td::realBorderRing()), blocked(td::realWaterRing(), kWater, true)}));
	ASSERT_TRUE(matchesFlood(mesh));
	std::mt19937		rng(5);
	std::vector<RingId> placed;
	Totals				totals;
	const auto&			trees = td::realTreeRings();
	for (std::size_t i = 0; i < trees.size(); ++i) {
		const PatchReport r = mesh.applyPatch(adding({blocked(trees[i])}));
		placed.push_back(r.added.front());
		totals.add(mesh.components().lastUpkeep());
		if (i % 25 == 0) {
			ASSERT_TRUE(matchesFlood(mesh)) << "tree " << i;
		}
	}
	ASSERT_TRUE(matchesFlood(mesh));
	std::shuffle(placed.begin(), placed.end(), rng);
	for (std::size_t i = 0; i < placed.size(); ++i) {
		mesh.applyPatch(removing(placed[i]));
		totals.add(mesh.components().lastUpkeep());
		if (i % 25 == 0) {
			ASSERT_TRUE(matchesFlood(mesh)) << "removal " << i;
		}
	}
	ASSERT_TRUE(matchesFlood(mesh));
	std::ostringstream visitedPerPatch;
	visitedPerPatch << static_cast<double>(totals.visited) / static_cast<double>(totals.patches) << " over " << totals.patches << " patches, "
					<< totals.splits << " splits";
	RecordProperty("VisitedPerPatch", visitedPerPatch.str());
}

// Ground 200 by 40 with posts every 10, so a bar across it re-derives only the triangles
// near the bar and the kept ground either side keeps its label.
namespace {

	PatchableMesh postedStrip(std::vector<std::int64_t> componentOpenTags = {}) {
		PatchableMesh				 mesh({.maxBoundsEdgeMm = 10 * kUnit, .componentOpenTags = std::move(componentOpenTags)});
		std::vector<NavInputPolygon> rings{bounds(rect(0, 0, 200, 40))};
		for (std::int64_t y = 5; y < 40; y += 10) {
			for (std::int64_t x = 5; x < 200; x += 10) {
				rings.push_back(blocked(rect(x, y, x + 2, y + 2)));
			}
		}
		mesh.applyPatch(adding(rings));
		return mesh;
	}

} // namespace

// A wall across the ground leaves one component (walls read open); an obstacle across it
// splits it, moving the epoch; taking the obstacle out joins it again, moving it again.
TEST(MeshComponents, WallsReadOpenObstaclesSplit) {
	PatchableMesh		  mesh	= postedStrip();
	const RingId		  wall	= mesh.applyPatch(adding({blocked(rect(99, 0, 102, 40), kWall)})).added.front();
	const MeshComponents& comps = mesh.components();
	EXPECT_TRUE(comps.sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(190, 20))));

	const std::uint64_t beforeSplit = comps.epoch();
	PatchEdits			swap;
	swap.removes.push_back(wall);
	swap.adds.push_back(blocked(rect(99, 0, 102, 40)));
	const RingId bar = mesh.applyPatch(swap).added.front();
	EXPECT_FALSE(comps.sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(190, 20))));
	EXPECT_EQ(comps.lastUpkeep().splits, 1);
	EXPECT_NE(comps.epoch(), beforeSplit);
	EXPECT_TRUE(matchesFlood(mesh));

	const std::uint64_t beforeMerge = comps.epoch();
	mesh.applyPatch(removing(bar));
	EXPECT_TRUE(comps.sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(190, 20))));
	EXPECT_EQ(comps.lastUpkeep().merges, 1);
	EXPECT_NE(comps.epoch(), beforeMerge);
	EXPECT_TRUE(matchesFlood(mesh));
}

// The label rule is a tag class: a footprint across the ground cuts nothing when its tag is
// one the labels read as open, and cuts like any obstacle when it is not.
TEST(MeshComponents, OpenTagClassNeverCuts) {
	for (const bool footprintsOpen : {true, false}) {
		PatchableMesh mesh = postedStrip(footprintsOpen ? std::vector<std::int64_t>{kFootprint} : std::vector<std::int64_t>{});
		const std::uint64_t before = mesh.components().epoch();
		mesh.applyPatch(adding({blocked(rect(99, 0, 102, 40), kFootprint)}));
		EXPECT_EQ(mesh.components().sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(190, 20))), footprintsOpen);
		EXPECT_EQ(mesh.components().lastUpkeep().splits, footprintsOpen ? 0 : 1);
		EXPECT_EQ(mesh.components().epoch() != before, !footprintsOpen) << "an arriving footprint moves the epoch only by a split";
		EXPECT_TRUE(matchesFlood(mesh));
	}
}

// An obstacle arriving in open ground closes only: no split, epoch kept. Ground arriving,
// even unjoined, may open a way: epoch moves, and the new square is a component of its own.
TEST(MeshComponents, EpochMovesOnlyWhenAWayMayOpen) {
	PatchableMesh		  mesh	= postedStrip();
	const MeshComponents& comps = mesh.components();

	std::uint64_t	  before = comps.epoch();
	const PatchReport tree	 = mesh.applyPatch(adding({blocked(rect(10, 10, 13, 13))}));
	EXPECT_FALSE(tree.opens);
	EXPECT_EQ(comps.epoch(), before);

	before = comps.epoch();
	mesh.applyPatch(adding({bounds(rect(300, 0, 400, 40))}));
	EXPECT_NE(comps.epoch(), before);
	EXPECT_EQ(comps.lastUpkeep().fresh, 1);
	EXPECT_FALSE(comps.sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(350, 20))));

	before = comps.epoch();
	mesh.applyPatch(adding({bounds(rect(200, 0, 300, 40))}));
	EXPECT_TRUE(comps.sameComponent(mesh.locate(at(10, 20)), mesh.locate(at(350, 20))));
	EXPECT_EQ(comps.lastUpkeep().merges, 1);
	EXPECT_NE(comps.epoch(), before);
	EXPECT_TRUE(matchesFlood(mesh));
}

TEST(MeshComponents, OffMeshOrBlockedIsNeverSame) {
	PatchableMesh mesh;
	mesh.applyPatch(adding({bounds(rect(0, 0, 100, 40)), blocked(rect(20, 10, 30, 20))}));
	const MeshComponents& comps = mesh.components();
	const std::int32_t	  floor = mesh.locate(at(5, 5));
	const std::int32_t	  tree	= mesh.locate(at(25, 15));
	EXPECT_TRUE(comps.sameComponent(floor, floor));
	EXPECT_FALSE(comps.sameComponent(tree, tree));
	EXPECT_FALSE(comps.sameComponent(floor, -1));
	EXPECT_FALSE(comps.sameComponent(-1, -1));
}
