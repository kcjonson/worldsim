#include "GeometryIndex.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <predicates/Predicates.h>

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using ecs::GeometryIndex;
using engine::assets::ConstructionRegistry;
using engine::construction::ConstructionWorld;
using engine::construction::FoundationState;
using engine::construction::kInvalidFoundation;
using engine::construction::kInvalidOpening;
using engine::construction::OpeningId;
using engine::construction::SegmentCommitResult;
using engine::construction::SegmentId;
using geometry::OccluderSegment;
using geometry::Vec2i64;

namespace {

	// Project root from __FILE__: this file lives at <root>/libs/engine/vision/.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	std::string constructionConfigFolder() {
		return (projectRoot() / "assets" / "config" / "construction").string();
	}

	// Build a single built wall segment (every created sub-segment marked Built).
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	// Add a BUILT opening at parameter t.
	OpeningId addBuiltOpening(ConstructionWorld& cw, SegmentId seg, float t, const std::string& type) {
		OpeningId op = cw.addOpening(seg, t, type, "Wood");
		EXPECT_NE(op, kInvalidOpening);
		EXPECT_TRUE(cw.setOpeningState(op, FoundationState::Built));
		return op;
	}

	// Is point p covered by (within 1 mm of) any occluder returned by the query?
	bool coveredByOccluder(const std::vector<OccluderSegment>& occluders, Vec2i64 p) {
		for (const OccluderSegment& o : occluders) {
			if (geometry::withinDistanceOfSegment(p, o.a, o.b, 1)) {
				return true;
			}
		}
		return false;
	}

	class GeometryIndexTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

} // namespace

// --- Transparency config -----------------------------------------------------

// Door and Window are both transparent to sight; pathable is independent of it
// (a window blocks movement but passes sight).
TEST_F(GeometryIndexTest, TransparencyConfigFlags) {
	const auto* door = ConstructionRegistry::Get().getOpeningType("Door");
	const auto* window = ConstructionRegistry::Get().getOpeningType("Window");
	ASSERT_NE(door, nullptr);
	ASSERT_NE(window, nullptr);

	EXPECT_TRUE(door->transparentToSight);
	EXPECT_TRUE(window->transparentToSight);

	// The two flags are independent: a window is sight-transparent but not pathable.
	EXPECT_FALSE(window->pathable);
	EXPECT_TRUE(window->transparentToSight);
	EXPECT_TRUE(door->pathable);
}

// --- Occluder extraction -----------------------------------------------------

// A built wall with no openings -> one occluder spanning the full centerline.
TEST_F(GeometryIndexTest, SolidWallEmitsOneFullOccluder) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	ASSERT_EQ(index.occluderCount(), 1u);
	const OccluderSegment& seg = index.occluders().front().seg;
	// Endpoints are the two vertices (order may follow v0->v1).
	const bool forward = (seg.a == Vec2i64{0, 0} && seg.b == Vec2i64{4000, 0});
	const bool reverse = (seg.a == Vec2i64{4000, 0} && seg.b == Vec2i64{0, 0});
	EXPECT_TRUE(forward || reverse);
}

// A built wall with a Door at t=0.5 -> two solid flanks with a sight gap over the
// door span; the door-span midpoint is not covered by any occluder.
TEST_F(GeometryIndexTest, DoorLeavesSightGap) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Door");

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	EXPECT_EQ(index.occluderCount(), 2u);

	std::vector<OccluderSegment> occ;
	index.queryOccluders({2000, 0}, 100000, occ);
	ASSERT_EQ(occ.size(), 2u);

	// Midpoint of the wall (== door center) sits in the gap: uncovered.
	EXPECT_FALSE(coveredByOccluder(occ, {2000, 0}));
	// The far flanks are still solid.
	EXPECT_TRUE(coveredByOccluder(occ, {100, 0}));
	EXPECT_TRUE(coveredByOccluder(occ, {3900, 0}));
}

// A Window leaves the same sight gap as a door, even though it is NOT pathable.
// This is the key divergence from nav (where a window stays a solid band).
TEST_F(GeometryIndexTest, WindowLeavesSightGap) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Window");

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	EXPECT_EQ(index.occluderCount(), 2u);

	std::vector<OccluderSegment> occ;
	index.queryOccluders({2000, 0}, 100000, occ);
	ASSERT_EQ(occ.size(), 2u);
	EXPECT_FALSE(coveredByOccluder(occ, {2000, 0})) << "window must leave a sight gap";
}

// A wall with BOTH a door and a window -> solid sub-spans between/around both
// openings, with a gap over each opening.
TEST_F(GeometryIndexTest, DoorAndWindowEmitFlanksAndTwoGaps) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {6000, 0});
	addBuiltOpening(cw, seg, 0.25F, "Door");	// gap centered at x=1500
	addBuiltOpening(cw, seg, 0.75F, "Window");	// gap centered at x=4500

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	// Two openings well inside -> three solid sub-spans (front flank, middle, back).
	EXPECT_EQ(index.occluderCount(), 3u);

	std::vector<OccluderSegment> occ;
	index.queryOccluders({3000, 0}, 100000, occ);
	ASSERT_EQ(occ.size(), 3u);

	// Gap over each opening center.
	EXPECT_FALSE(coveredByOccluder(occ, {1500, 0})) << "door gap";
	EXPECT_FALSE(coveredByOccluder(occ, {4500, 0})) << "window gap";
	// Solid between and around the openings.
	EXPECT_TRUE(coveredByOccluder(occ, {100, 0}));	 // front flank
	EXPECT_TRUE(coveredByOccluder(occ, {3000, 0}));	 // middle solid
	EXPECT_TRUE(coveredByOccluder(occ, {5900, 0}));	 // back flank
}

// --- Blueprint handling ------------------------------------------------------

// A blueprint segment contributes no occluder; a blueprint opening does NOT cut
// the wall (only built transparent openings make gaps).
TEST_F(GeometryIndexTest, BlueprintSegmentAndOpeningContributeNoCut) {
	// Blueprint wall: commit but leave state Blueprint -> no occluder.
	{
		ConstructionWorld cw;
		SegmentCommitResult r = cw.commitSegment({0, 0}, {4000, 0}, "Wood", "Standard", kInvalidFoundation);
		ASSERT_TRUE(r.ok());
		// Deliberately do NOT mark Built.

		GeometryIndex index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();
		EXPECT_EQ(index.occluderCount(), 0u);
		EXPECT_TRUE(index.builtSegments().empty());
	}

	// Built wall + blueprint opening: the opening is not Built, so the wall stays
	// solid (one full occluder, no gap).
	{
		ConstructionWorld cw;
		SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
		OpeningId op = cw.addOpening(seg, 0.5F, "Door", "Wood");
		ASSERT_NE(op, kInvalidOpening);
		// Opening left Blueprint.

		GeometryIndex index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();

		ASSERT_EQ(index.occluderCount(), 1u);
		std::vector<OccluderSegment> occ;
		index.queryOccluders({2000, 0}, 100000, occ);
		EXPECT_TRUE(coveredByOccluder(occ, {2000, 0})) << "blueprint opening must not cut the wall";
	}
}

// --- queryOccluders range ----------------------------------------------------

// An observer within radius gets the nearby occluder; one far beyond it gets none.
TEST_F(GeometryIndexTest, QueryRespectsRadius) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	std::vector<OccluderSegment> near;
	index.queryOccluders({2000, 500}, 1000, near); // 500 mm off the wall, 1 m radius
	EXPECT_EQ(near.size(), 1u);

	std::vector<OccluderSegment> far;
	index.queryOccluders({2000, 50000}, 1000, far); // 50 m off the wall, 1 m radius
	EXPECT_TRUE(far.empty());
}

// --- rebuildIfStale version gating -------------------------------------------

// After a rebuild, mutating the world (adding a door bumps version) is reflected by
// the next rebuildIfStale; a call with unchanged version does not rebuild.
TEST_F(GeometryIndexTest, RebuildIfStaleTracksVersion) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	ASSERT_EQ(index.occluderCount(), 1u); // solid wall

	// No mutation: another rebuildIfStale is a no-op and the cache is unchanged.
	const std::uint64_t versionBefore = cw.version();
	index.rebuildIfStale();
	EXPECT_EQ(index.occluderCount(), 1u);

	// Cut a built door: version bumps, next rebuildIfStale reflects the gap.
	addBuiltOpening(cw, seg, 0.5F, "Door");
	EXPECT_NE(cw.version(), versionBefore);

	index.rebuildIfStale();
	EXPECT_EQ(index.occluderCount(), 2u);

	std::vector<OccluderSegment> occ;
	index.queryOccluders({2000, 0}, 100000, occ);
	EXPECT_FALSE(coveredByOccluder(occ, {2000, 0})) << "rebuild should now show the door gap";
}

// hasWorld / no-world inertness.
TEST_F(GeometryIndexTest, NoWorldIsInert) {
	GeometryIndex index;
	EXPECT_FALSE(index.hasWorld());
	index.rebuildIfStale(); // safe no-op
	EXPECT_EQ(index.occluderCount(), 0u);
}

// builtOpenings record carries the jamb points and transparency flag for V3.
TEST_F(GeometryIndexTest, OpeningRecordsExposedForV3) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Window");

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	ASSERT_EQ(index.builtSegments().size(), 1u);
	ASSERT_EQ(index.builtOpenings().size(), 1u);
	const GeometryIndex::OpeningRecord& rec = index.builtOpenings().front();
	EXPECT_EQ(rec.segment, seg);
	EXPECT_EQ(rec.type, "Window");
	EXPECT_TRUE(rec.transparentToSight);
	// Jamb points straddle the wall centerline midpoint (the window center).
	EXPECT_LT(rec.jambA.x, 2000);
	EXPECT_GT(rec.jambB.x, 2000);
}
