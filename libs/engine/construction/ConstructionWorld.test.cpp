#include "ConstructionWorld.h"

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <gtest/gtest.h>

namespace cw = engine::construction;
using cw::Aabb;
using cw::CommitResult;
using cw::CommitStatus;
using cw::ConstructionWorld;
using cw::FoundationState;
using cw::kInvalidFoundation;
using cw::kInvalidOpening;
using cw::kInvalidSegment;
using cw::kInvalidVertex;
using cw::SegmentCommitResult;
using cw::SegmentStatus;
using geometry::Ring;
using geometry::Vec2i64;

// `Foundation` alone is ambiguous here: the struct vs. the math namespace from
// <math/Types.h>. Spell the topology record out where it is needed.
using FoundationRecord = cw::Foundation;

namespace {

	// 1000 mm == 1 m. Helper builds a CCW axis-aligned box [x0,x1] x [y0,y1].
	Ring box(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	// Same box wound clockwise, to exercise winding normalization.
	Ring boxCw(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}};
	}

} // namespace

// ============================================================================
// Commit: structural validation and winding
// ============================================================================

TEST(ConstructionWorldTests, CommitValidSquareReturnsId) {
	ConstructionWorld world;
	CommitResult	  result = world.commitFoundation(box(0, 0, 4000, 4000), "wood");

	ASSERT_TRUE(result.ok());
	EXPECT_NE(result.id, kInvalidFoundation);

	const FoundationRecord* foundation = world.get(result.id);
	ASSERT_NE(foundation, nullptr);
	EXPECT_EQ(foundation->material, "wood");
	EXPECT_EQ(foundation->state, FoundationState::Blueprint);
	EXPECT_EQ(foundation->entity, ecs::kInvalidEntity);
	EXPECT_EQ(geometry::windingOrder(foundation->ring), geometry::Winding::CounterClockwise);
	EXPECT_FLOAT_EQ(world.areaSquareMeters(result.id), 16.0F);
}

TEST(ConstructionWorldTests, ClockwiseInputIsNormalizedToCcw) {
	ConstructionWorld world;
	CommitResult	  result = world.commitFoundation(boxCw(0, 0, 4000, 4000), "stone");

	ASSERT_TRUE(result.ok());
	const FoundationRecord* foundation = world.get(result.id);
	ASSERT_NE(foundation, nullptr);
	EXPECT_EQ(geometry::windingOrder(foundation->ring), geometry::Winding::CounterClockwise);
}

TEST(ConstructionWorldTests, MetersOverloadQuantizesToMillimeters) {
	ConstructionWorld				world;
	std::vector<::Foundation::Vec2> meters = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}};
	CommitResult					result = world.commitFoundation(meters, "wood");

	ASSERT_TRUE(result.ok());
	const FoundationRecord* foundation = world.get(result.id);
	ASSERT_NE(foundation, nullptr);
	EXPECT_EQ(foundation->ring.front(), Vec2i64(0, 0));
	EXPECT_FLOAT_EQ(world.areaSquareMeters(result.id), 16.0F);
}

TEST(ConstructionWorldTests, RejectsFewerThanThreeVertices) {
	ConstructionWorld world;
	Ring			  two = {{0, 0}, {1000, 0}};
	EXPECT_EQ(world.commitFoundation(two, "wood").status, CommitStatus::TooFewVertices);
	EXPECT_EQ(world.foundations().size(), 0u);
}

TEST(ConstructionWorldTests, RejectsNonSimpleRing) {
	ConstructionWorld world;
	// Bow-tie / figure-eight: edges cross.
	Ring bowtie = {{0, 0}, {4000, 4000}, {4000, 0}, {0, 4000}};
	EXPECT_EQ(world.commitFoundation(bowtie, "wood").status, CommitStatus::NotSimple);
	EXPECT_EQ(world.foundations().size(), 0u);
}

TEST(ConstructionWorldTests, RejectsZeroAreaRing) {
	ConstructionWorld world;
	// Three collinear points: zero signed area.
	Ring collinear = {{0, 0}, {2000, 0}, {4000, 0}};
	EXPECT_EQ(world.commitFoundation(collinear, "wood").status, CommitStatus::DegenerateArea);
	EXPECT_EQ(world.foundations().size(), 0u);
}

// ============================================================================
// Commit: overlap rejection vs. legal adjacency
// ============================================================================

TEST(ConstructionWorldTests, RejectsInteriorOverlap) {
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(box(0, 0, 4000, 4000), "wood").ok());

	CommitResult overlapping = world.commitFoundation(box(2000, 2000, 6000, 6000), "wood");
	EXPECT_EQ(overlapping.status, CommitStatus::OverlapsExisting);
	EXPECT_EQ(world.foundations().size(), 1u);
}

TEST(ConstructionWorldTests, AllowsEdgeAdjacentFoundation) {
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(box(0, 0, 4000, 4000), "wood").ok());

	// Shares the full x=4000 edge: no interior overlap, the legal snapped case.
	CommitResult adjacent = world.commitFoundation(box(4000, 0, 8000, 4000), "stone");
	EXPECT_TRUE(adjacent.ok());
	EXPECT_EQ(world.foundations().size(), 2u);
}

TEST(ConstructionWorldTests, AllowsVertexAdjacentFoundation) {
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(box(0, 0, 4000, 4000), "wood").ok());

	// Touches only at the (4000,4000) corner: no interior overlap.
	CommitResult adjacent = world.commitFoundation(box(4000, 4000, 8000, 8000), "stone");
	EXPECT_TRUE(adjacent.ok());
	EXPECT_EQ(world.foundations().size(), 2u);
}

// ============================================================================
// Editing: add / subtract / remove
// ============================================================================

TEST(ConstructionWorldTests, AddExtendsRingAndBumpsVersion) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	const std::uint64_t before = world.version();
	const float			areaBefore = world.areaSquareMeters(base.id);

	// Addend shares the x=4000 edge; union is a single 4x8 m rectangle.
	EXPECT_EQ(world.addToFoundation(base.id, box(4000, 0, 8000, 4000)), CommitStatus::Ok);
	EXPECT_GT(world.version(), before);
	EXPECT_GT(world.areaSquareMeters(base.id), areaBefore);
	EXPECT_FLOAT_EQ(world.areaSquareMeters(base.id), 32.0F);
}

TEST(ConstructionWorldTests, SubtractShrinksRingAndBumpsVersion) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 8000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	const std::uint64_t before = world.version();
	const float			areaBefore = world.areaSquareMeters(base.id);

	// Carve the right half off; the cut crosses the boundary, remainder is one
	// simple rectangle.
	EXPECT_EQ(world.subtractFromFoundation(base.id, box(4000, 0, 8000, 4000)), CommitStatus::Ok);
	EXPECT_GT(world.version(), before);
	EXPECT_LT(world.areaSquareMeters(base.id), areaBefore);
}

TEST(ConstructionWorldTests, SubtractInteriorHoleRejected) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 10000, 10000), "wood");
	ASSERT_TRUE(base.ok());

	const Ring			before = world.get(base.id)->ring;
	const std::uint64_t versionBefore = world.version();

	// A region strictly interior to the foundation would carve an enclosed hole.
	CommitStatus status = world.subtractFromFoundation(base.id, box(3000, 3000, 7000, 7000));
	EXPECT_EQ(status, CommitStatus::BooleanResultHasHole);
	// Ring unchanged and no version bump on rejection (reject, don't repair).
	EXPECT_EQ(world.get(base.id)->ring, before);
	EXPECT_EQ(world.version(), versionBefore);
}

TEST(ConstructionWorldTests, SubtractConsumingFoundationRejected) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	// Subtrahend fully covers the foundation: nothing would remain.
	CommitStatus status = world.subtractFromFoundation(base.id, box(-1000, -1000, 5000, 5000));
	EXPECT_EQ(status, CommitStatus::BooleanConsumesInput);
}

TEST(ConstructionWorldTests, EditOnUnknownFoundationRejected) {
	ConstructionWorld world;
	EXPECT_EQ(world.addToFoundation(999, box(0, 0, 1000, 1000)), CommitStatus::UnknownFoundation);
	EXPECT_EQ(world.subtractFromFoundation(999, box(0, 0, 1000, 1000)), CommitStatus::UnknownFoundation);
}

TEST(ConstructionWorldTests, RemoveFoundation) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	const std::uint64_t before = world.version();
	EXPECT_TRUE(world.removeFoundation(base.id));
	EXPECT_EQ(world.get(base.id), nullptr);
	EXPECT_EQ(world.foundations().size(), 0u);
	EXPECT_GT(world.version(), before);

	EXPECT_FALSE(world.removeFoundation(base.id));
}

// ============================================================================
// Queries: foundationAt, AABB, area
// ============================================================================

TEST(ConstructionWorldTests, FoundationAtHitMissBoundary) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	EXPECT_EQ(world.foundationAt({2000, 2000}), base.id);			 // interior hit
	EXPECT_EQ(world.foundationAt({9000, 9000}), kInvalidFoundation); // miss
	EXPECT_EQ(world.foundationAt({0, 2000}), base.id);				 // on boundary edge
}

TEST(ConstructionWorldTests, FoundationAtSharedEdgeTieBreaksToHighestId) {
	ConstructionWorld world;
	CommitResult	  first = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	CommitResult	  second = world.commitFoundation(box(4000, 0, 8000, 4000), "stone");
	ASSERT_TRUE(first.ok());
	ASSERT_TRUE(second.ok());
	EXPECT_GT(second.id, first.id);

	// The shared x=4000 edge is a boundary of both; tie-break is the highest id.
	EXPECT_EQ(world.foundationAt({4000, 2000}), second.id);
}

TEST(ConstructionWorldTests, FootprintAabb) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(1000, 2000, 5000, 9000), "wood");
	ASSERT_TRUE(base.ok());

	Aabb aabb = world.footprintAabb(base.id);
	EXPECT_EQ(aabb.min, Vec2i64(1000, 2000));
	EXPECT_EQ(aabb.max, Vec2i64(5000, 9000));

	Aabb empty = world.footprintAabb(12345);
	EXPECT_EQ(empty.min, Vec2i64(0, 0));
	EXPECT_EQ(empty.max, Vec2i64(0, 0));
}

// ============================================================================
// Lifecycle / ECS wiring mutators
// ============================================================================

TEST(ConstructionWorldTests, SetStateAndEntity) {
	ConstructionWorld world;
	CommitResult	  base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());

	const std::uint64_t v0 = world.version();
	EXPECT_TRUE(world.setState(base.id, FoundationState::Built));
	EXPECT_EQ(world.get(base.id)->state, FoundationState::Built);
	EXPECT_GT(world.version(), v0);

	const std::uint64_t v1 = world.version();
	const ecs::EntityID handle = ecs::makeEntityID(7, 1);
	EXPECT_TRUE(world.setEntity(base.id, handle));
	EXPECT_EQ(world.get(base.id)->entity, handle);
	EXPECT_GT(world.version(), v1);

	EXPECT_FALSE(world.setState(999, FoundationState::Built));
	EXPECT_FALSE(world.setEntity(999, handle));
}

// ============================================================================
// Versioning & deterministic iteration
// ============================================================================

TEST(ConstructionWorldTests, VersionBumpsOnMutationNotOnQuery) {
	ConstructionWorld world;
	EXPECT_EQ(world.version(), 0u);

	CommitResult base = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
	ASSERT_TRUE(base.ok());
	const std::uint64_t afterCommit = world.version();
	EXPECT_GT(afterCommit, 0u);

	// Pure queries do not bump the version.
	(void)world.foundationAt({2000, 2000});
	(void)world.footprintAabb(base.id);
	(void)world.areaSquareMeters(base.id);
	(void)world.get(base.id);
	(void)world.foundations();
	EXPECT_EQ(world.version(), afterCommit);

	// A rejected commit does not bump the version either.
	(void)world.commitFoundation(box(1000, 1000, 3000, 3000), "wood"); // overlaps
	EXPECT_EQ(world.version(), afterCommit);
}

TEST(ConstructionWorldTests, DeterministicInsertionOrder) {
	ConstructionWorld world;
	CommitResult	  a = world.commitFoundation(box(0, 0, 1000, 1000), "wood");
	CommitResult	  b = world.commitFoundation(box(2000, 0, 3000, 1000), "wood");
	CommitResult	  c = world.commitFoundation(box(4000, 0, 5000, 1000), "wood");
	ASSERT_TRUE(a.ok());
	ASSERT_TRUE(b.ok());
	ASSERT_TRUE(c.ok());

	const auto& all = world.foundations();
	ASSERT_EQ(all.size(), 3u);
	EXPECT_EQ(all[0].id, a.id);
	EXPECT_EQ(all[1].id, b.id);
	EXPECT_EQ(all[2].id, c.id);
	EXPECT_LT(a.id, b.id);
	EXPECT_LT(b.id, c.id);

	// Removing the middle preserves the relative order of the rest.
	ASSERT_TRUE(world.removeFoundation(b.id));
	const auto& after = world.foundations();
	ASSERT_EQ(after.size(), 2u);
	EXPECT_EQ(after[0].id, a.id);
	EXPECT_EQ(after[1].id, c.id);
}

// ============================================================================
// Walls: helpers and a host foundation
// ============================================================================

namespace {

	// Most wall tests need a foundation to host segments. A generous 100x100 m
	// box covers every coordinate used below.
	cw::FoundationId makeHost(ConstructionWorld& world) {
		CommitResult host = world.commitFoundation(box(-10000, -10000, 100000, 100000), "wood");
		EXPECT_TRUE(host.ok());
		return host.id;
	}

	// Degree (incident-segment count) of the vertex at an exact position, or 0
	// if there is no vertex there.
	std::size_t degreeAt(const ConstructionWorld& world, const Vec2i64& pos) {
		const cw::VertexId vid = world.vertexAt(pos);
		return vid == kInvalidVertex ? 0u : world.segmentsOfVertex(vid).size();
	}

} // namespace

// ============================================================================
// Walls: commit basics
// ============================================================================

TEST(ConstructionWorldWallTests, CommitSegmentCreatesIdsAndVertices) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult result = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(result.ok());
	EXPECT_NE(result.id, kInvalidSegment);

	// A simple commit reports exactly one created segment, and `id` mirrors it.
	ASSERT_EQ(result.createdSegments.size(), 1u);
	EXPECT_EQ(result.createdSegments.front(), result.id);

	ASSERT_EQ(world.segments().size(), 1u);
	ASSERT_EQ(world.vertices().size(), 2u);

	const cw::WallSegment* segment = world.getSegment(result.id);
	ASSERT_NE(segment, nullptr);
	EXPECT_EQ(segment->material, "wood");
	EXPECT_EQ(segment->thicknessPreset, "thin");
	EXPECT_EQ(segment->hostFoundation, host);
	EXPECT_EQ(segment->state, FoundationState::Blueprint);
	EXPECT_EQ(segment->entity, ecs::kInvalidEntity);

	// Both endpoints exist as degree-1 vertices joined to this segment.
	EXPECT_EQ(degreeAt(world, {0, 0}), 1u);
	EXPECT_EQ(degreeAt(world, {4000, 0}), 1u);
}

TEST(ConstructionWorldWallTests, RejectsZeroLength) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	const std::uint64_t before = world.version();
	SegmentCommitResult result = world.commitSegment({2000, 2000}, {2000, 2000}, "wood", "thin", host);
	EXPECT_EQ(result.status, SegmentStatus::ZeroLength);
	EXPECT_EQ(result.id, kInvalidSegment);
	EXPECT_EQ(world.segments().size(), 0u);
	EXPECT_EQ(world.version(), before);
}

TEST(ConstructionWorldWallTests, RejectsUnknownHost) {
	ConstructionWorld	world;
	SegmentCommitResult result = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", 999);
	EXPECT_EQ(result.status, SegmentStatus::UnknownHost);
	EXPECT_EQ(world.segments().size(), 0u);
}

TEST(ConstructionWorldWallTests, SharedEndpointMergesVertex) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult first = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	SegmentCommitResult second = world.commitSegment({4000, 0}, {4000, 4000}, "wood", "thin", host);
	ASSERT_TRUE(first.ok());
	ASSERT_TRUE(second.ok());

	// Three vertices total: the shared (4000,0) is one merged vertex of degree 2.
	EXPECT_EQ(world.vertices().size(), 3u);
	EXPECT_EQ(world.segments().size(), 2u);
	EXPECT_EQ(degreeAt(world, {4000, 0}), 2u);
}

TEST(ConstructionWorldWallTests, RejectsExactDuplicate) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	ASSERT_TRUE(world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host).ok());
	const std::uint64_t before = world.version();

	// Same endpoints (either order) join the same vertex pair: a duplicate.
	SegmentCommitResult dupForward = world.commitSegment({0, 0}, {4000, 0}, "stone", "thick", host);
	EXPECT_EQ(dupForward.status, SegmentStatus::Duplicate);
	SegmentCommitResult dupReverse = world.commitSegment({4000, 0}, {0, 0}, "stone", "thick", host);
	EXPECT_EQ(dupReverse.status, SegmentStatus::Duplicate);

	EXPECT_EQ(world.segments().size(), 1u);
	EXPECT_EQ(world.version(), before);
}

TEST(ConstructionWorldWallTests, RejectedDuplicateRequiringSplitIsAtomic) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// One long base wall (0,0)->(8000,0).
	SegmentCommitResult base = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(base.ok());
	const cw::SegmentId baseId = base.id;

	// Snapshot topology before the rejected commit.
	const std::size_t	beforeSegments = world.segments().size();
	const std::size_t	beforeVertices = world.vertices().size();
	const std::uint64_t beforeVersion = world.version();

	// Commit (0,0)->(4000,0): (4000,0) lands on the base's interior, so resolving
	// it would split the base into (0,0)..(4000,0) and (4000,0)..(8000,0). But the
	// span (0,0)..(4000,0) then duplicates the first half, so the commit creates
	// nothing and must reject Duplicate -- WITHOUT having performed the split.
	SegmentCommitResult rejected = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	EXPECT_EQ(rejected.status, SegmentStatus::Duplicate);
	EXPECT_EQ(rejected.id, kInvalidSegment);

	// The split must not have happened: the base segment still exists by its
	// original id, no new vertex at (4000,0), and counts + version are unchanged.
	EXPECT_NE(world.getSegment(baseId), nullptr);
	EXPECT_EQ(world.vertexAt({4000, 0}), kInvalidVertex);
	EXPECT_EQ(world.segments().size(), beforeSegments);
	EXPECT_EQ(world.vertices().size(), beforeVertices);
	EXPECT_EQ(world.version(), beforeVersion);
}

// ============================================================================
// Walls: T-junction splitting
// ============================================================================

TEST(ConstructionWorldWallTests, TJunctionSplitsHostSegment) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// Horizontal host wall, then a perpendicular wall whose endpoint lands on its
	// interior at the midpoint.
	SegmentCommitResult base = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(base.ok());
	const cw::SegmentId baseId = base.id;

	SegmentCommitResult branch = world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host);
	ASSERT_TRUE(branch.ok());

	// Three segments: the base split into two halves plus the branch.
	EXPECT_EQ(world.segments().size(), 3u);
	// The original base segment id is gone (replaced by two new ids).
	EXPECT_EQ(world.getSegment(baseId), nullptr);

	// The mid vertex has degree 3: two base halves + the branch.
	EXPECT_EQ(degreeAt(world, {4000, 0}), 3u);

	// Both halves carry the original base's material/thickness/host.
	int halvesFound = 0;
	for (const cw::WallSegment& s : world.segments()) {
		const Vec2i64 p0 = world.getVertex(s.v0)->pos;
		const Vec2i64 p1 = world.getVertex(s.v1)->pos;
		const bool	  isHalf = (p0 == Vec2i64(0, 0) && p1 == Vec2i64(4000, 0)) || (p0 == Vec2i64(4000, 0) && p1 == Vec2i64(8000, 0));
		if (isHalf) {
			++halvesFound;
			EXPECT_EQ(s.material, "stone");
			EXPECT_EQ(s.thicknessPreset, "thick");
			EXPECT_EQ(s.hostFoundation, host);
		}
	}
	EXPECT_EQ(halvesFound, 2);
}

TEST(ConstructionWorldWallTests, NewSegmentSplitsAtExistingVertex) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// A T-stem rising from (4000,0) creates an isolated junction vertex there
	// (degree 1). Then a horizontal wall drawn straight through that vertex must
	// split into two halves at it.
	ASSERT_TRUE(world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host).ok());
	EXPECT_EQ(degreeAt(world, {4000, 0}), 1u);

	SegmentCommitResult through = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(through.ok());

	// The horizontal wall is two segments (0..4000 and 4000..8000) plus the stem.
	EXPECT_EQ(world.segments().size(), 3u);
	// The junction is now degree 3.
	EXPECT_EQ(degreeAt(world, {4000, 0}), 3u);
}

TEST(ConstructionWorldWallTests, RejectsXCrossing) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// Two segments whose interiors properly cross at (4000,4000).
	ASSERT_TRUE(world.commitSegment({0, 4000}, {8000, 4000}, "wood", "thin", host).ok());
	const std::uint64_t before = world.version();

	SegmentCommitResult crossing = world.commitSegment({4000, 0}, {4000, 8000}, "wood", "thin", host);
	EXPECT_EQ(crossing.status, SegmentStatus::XCrossing);
	EXPECT_EQ(crossing.id, kInvalidSegment);
	EXPECT_EQ(world.segments().size(), 1u);
	EXPECT_EQ(world.version(), before);
}

// ============================================================================
// Walls: commitSegment reports every created/changed segment
// ============================================================================
//
// The ECS-spawning caller (DrawingSystem) must give EVERY segment a correctly-
// sized blueprint entity, so commitSegment reports all segment ids it created:
// the new chain span(s) AND the two halves of any existing segment a T-junction
// split. These tests pin that contract.

namespace {

	// True if `id` names a live segment in the store.
	bool isLiveSegment(const ConstructionWorld& world, cw::SegmentId id) {
		return world.getSegment(id) != nullptr;
	}

	// True if `ids` contains a segment whose endpoints (either order) are p0/p1.
	bool createdHasSpan(const ConstructionWorld& world, const std::vector<cw::SegmentId>& ids, const Vec2i64& p0, const Vec2i64& p1) {
		for (const cw::SegmentId id : ids) {
			const cw::WallSegment* s = world.getSegment(id);
			if (s == nullptr) {
				continue;
			}
			const Vec2i64 a = world.getVertex(s->v0)->pos;
			const Vec2i64 b = world.getVertex(s->v1)->pos;
			if ((a == p0 && b == p1) || (a == p1 && b == p0)) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST(ConstructionWorldWallTests, ReportsSingleCreatedSegmentForSimpleCommit) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult r = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(r.ok());
	ASSERT_EQ(r.createdSegments.size(), 1u);
	EXPECT_TRUE(isLiveSegment(world, r.createdSegments.front()));
	EXPECT_TRUE(createdHasSpan(world, r.createdSegments, {0, 0}, {4000, 0}));
}

TEST(ConstructionWorldWallTests, ReportsBothSegmentsWhenSpanCrossesExistingVertex) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// A stem creates an isolated junction vertex at (4000,0). A horizontal wall
	// drawn straight through it splits into two new segments at that vertex; both
	// must be reported so each gets its own entity.
	ASSERT_TRUE(world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host).ok());

	SegmentCommitResult through = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(through.ok());

	ASSERT_EQ(through.createdSegments.size(), 2u);
	for (const cw::SegmentId id : through.createdSegments) {
		EXPECT_TRUE(isLiveSegment(world, id));
	}
	EXPECT_TRUE(createdHasSpan(world, through.createdSegments, {0, 0}, {4000, 0}));
	EXPECT_TRUE(createdHasSpan(world, through.createdSegments, {4000, 0}, {8000, 0}));
	// `id` is one of the reported segments.
	EXPECT_TRUE(isLiveSegment(world, through.id));
}

TEST(ConstructionWorldWallTests, ReportsSplitHalvesAndNewSegmentForTJunction) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// A long base wall; then a branch whose endpoint lands on the base interior.
	// The commit splits the base into two halves AND adds the branch: three new
	// segments, all reported (the base's old id and its entity are orphaned).
	SegmentCommitResult base = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(base.ok());
	const cw::SegmentId baseId = base.id;

	SegmentCommitResult branch = world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host);
	ASSERT_TRUE(branch.ok());

	// The two split halves plus the branch == three created segments.
	ASSERT_EQ(branch.createdSegments.size(), 3u);
	for (const cw::SegmentId id : branch.createdSegments) {
		EXPECT_TRUE(isLiveSegment(world, id));
		EXPECT_NE(id, baseId); // the old base id is gone, never reported
	}

	// The created/changed set includes BOTH halves of the split base and the branch.
	EXPECT_TRUE(createdHasSpan(world, branch.createdSegments, {0, 0}, {4000, 0}));
	EXPECT_TRUE(createdHasSpan(world, branch.createdSegments, {4000, 0}, {8000, 0}));
	EXPECT_TRUE(createdHasSpan(world, branch.createdSegments, {4000, 0}, {4000, 5000}));

	// The old base id is dead: a caller mirroring it must destroy that entity.
	EXPECT_EQ(world.getSegment(baseId), nullptr);
}

// ============================================================================
// Walls: opening re-attach across a split
// ============================================================================

TEST(ConstructionWorldWallTests, OpeningsReattachByParamRangeAcrossSplit) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult base = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(base.ok());

	// Two openings: one in each half once the segment is split at t=0.5.
	cw::OpeningId low = world.addOpening(base.id, 0.25F, "door", "wood");
	cw::OpeningId high = world.addOpening(base.id, 0.75F, "window", "glass");
	ASSERT_NE(low, kInvalidOpening);
	ASSERT_NE(high, kInvalidOpening);

	// Split at the midpoint (param 0.5 along the 8 m base).
	ASSERT_TRUE(world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host).ok());

	// The low opening (t=0.25) maps to the first half (0..4000), rescaled to 0.5.
	const cw::Opening* lowAfter = world.getOpening(low);
	ASSERT_NE(lowAfter, nullptr);
	const cw::WallSegment* lowSeg = world.getSegment(lowAfter->segment);
	ASSERT_NE(lowSeg, nullptr);
	EXPECT_EQ(world.getVertex(lowSeg->v0)->pos, Vec2i64(0, 0));
	EXPECT_EQ(world.getVertex(lowSeg->v1)->pos, Vec2i64(4000, 0));
	EXPECT_FLOAT_EQ(lowAfter->t, 0.5F);

	// The high opening (t=0.75) maps to the second half (4000..8000), rescaled to
	// (0.75-0.5)/0.5 = 0.5.
	const cw::Opening* highAfter = world.getOpening(high);
	ASSERT_NE(highAfter, nullptr);
	const cw::WallSegment* highSeg = world.getSegment(highAfter->segment);
	ASSERT_NE(highSeg, nullptr);
	EXPECT_EQ(world.getVertex(highSeg->v0)->pos, Vec2i64(4000, 0));
	EXPECT_EQ(world.getVertex(highSeg->v1)->pos, Vec2i64(8000, 0));
	EXPECT_FLOAT_EQ(highAfter->t, 0.5F);
}

TEST(ConstructionWorldWallTests, SplitClearsOldSegmentEntity) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult base = world.commitSegment({0, 0}, {8000, 0}, "stone", "thick", host);
	ASSERT_TRUE(base.ok());
	ASSERT_TRUE(world.setSegmentEntity(base.id, ecs::makeEntityID(42, 1)));

	ASSERT_TRUE(world.commitSegment({4000, 0}, {4000, 5000}, "wood", "thin", host).ok());

	// The old segment id no longer resolves; its entity is not carried onto
	// either half (both halves start with kInvalidEntity for the caller to wire).
	EXPECT_EQ(world.getSegment(base.id), nullptr);
	for (const cw::WallSegment& s : world.segments()) {
		EXPECT_EQ(s.entity, ecs::kInvalidEntity);
	}
}

// ============================================================================
// Walls: segmentAt hit-testing
// ============================================================================

TEST(ConstructionWorldWallTests, SegmentAtHitMissAndRadiusBoundary) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult seg = world.commitSegment({0, 0}, {8000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());

	// On the centerline: hit at any radius.
	EXPECT_EQ(world.segmentAt({4000, 0}, 100), seg.id);
	// 200 mm off, radius 300: within tolerance, hit.
	EXPECT_EQ(world.segmentAt({4000, 200}, 300), seg.id);
	// 200 mm off, radius 100: outside tolerance, miss.
	EXPECT_EQ(world.segmentAt({4000, 200}, 100), kInvalidSegment);
	// Exactly at the radius boundary: at-threshold passes (<=).
	EXPECT_EQ(world.segmentAt({4000, 200}, 200), seg.id);
	// Far away: miss.
	EXPECT_EQ(world.segmentAt({4000, 5000}, 300), kInvalidSegment);
}

TEST(ConstructionWorldWallTests, SegmentAtTieBreaksToHighestId) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// Two parallel segments equidistant (500 mm each) from y=500: tie-break to the
	// higher id (committed second).
	SegmentCommitResult lower = world.commitSegment({0, 0}, {8000, 0}, "wood", "thin", host);
	SegmentCommitResult upper = world.commitSegment({0, 1000}, {8000, 1000}, "wood", "thin", host);
	ASSERT_TRUE(lower.ok());
	ASSERT_TRUE(upper.ok());
	EXPECT_GT(upper.id, lower.id);

	EXPECT_EQ(world.segmentAt({4000, 500}, 600), upper.id);
}

// ============================================================================
// Walls: removeSegment, adjacency, orphan cleanup
// ============================================================================

TEST(ConstructionWorldWallTests, RemoveSegmentCleansAdjacencyAndOrphans) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult a = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	SegmentCommitResult b = world.commitSegment({4000, 0}, {4000, 4000}, "wood", "thin", host);
	ASSERT_TRUE(a.ok());
	ASSERT_TRUE(b.ok());
	ASSERT_EQ(world.vertices().size(), 3u);

	const std::uint64_t before = world.version();
	EXPECT_TRUE(world.removeSegment(a.id));
	EXPECT_GT(world.version(), before);

	// Segment a is gone; its lone endpoint (0,0) is now orphaned and pruned. The
	// shared vertex (4000,0) survives (still held by b) and drops to degree 1.
	EXPECT_EQ(world.getSegment(a.id), nullptr);
	EXPECT_EQ(world.vertexAt({0, 0}), kInvalidVertex);
	EXPECT_EQ(degreeAt(world, {4000, 0}), 1u);
	EXPECT_EQ(world.vertices().size(), 2u);

	// Removing an unknown segment is a no-op false.
	EXPECT_FALSE(world.removeSegment(a.id));
}

TEST(ConstructionWorldWallTests, RemoveSegmentDropsItsOpenings) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult seg = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());
	cw::OpeningId opening = world.addOpening(seg.id, 0.5F, "door", "wood");
	ASSERT_NE(opening, kInvalidOpening);
	ASSERT_EQ(world.openings().size(), 1u);

	EXPECT_TRUE(world.removeSegment(seg.id));
	EXPECT_EQ(world.openings().size(), 0u);
	EXPECT_EQ(world.getOpening(opening), nullptr);
}

TEST(ConstructionWorldWallTests, RemoveSegmentSurfacesOpeningEntitiesForDespawn) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult seg = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());
	const cw::OpeningId linked = world.addOpening(seg.id, 0.4F, "door", "wood");
	const cw::OpeningId unlinked = world.addOpening(seg.id, 0.7F, "window", "wood");
	ASSERT_NE(linked, kInvalidOpening);
	ASSERT_NE(unlinked, kInvalidOpening);
	ASSERT_TRUE(world.setOpeningEntity(linked, ecs::EntityID{42}));
	// `unlinked` keeps kInvalidEntity (never mirrored to an ECS entity).

	// removeSegment must hand back the live ECS handles of the openings it drops,
	// so the caller can despawn them (no leaked door/window blueprint entities).
	std::vector<ecs::EntityID> removed;
	EXPECT_TRUE(world.removeSegment(seg.id, &removed));
	ASSERT_EQ(removed.size(), 1u); // only the linked opening had a live handle
	EXPECT_EQ(removed[0], ecs::EntityID{42});
	EXPECT_EQ(world.openings().size(), 0u);
}

TEST(ConstructionWorldWallTests, SegmentsOnFoundationReturnsHostedInStableOrder) {
	ConstructionWorld world;
	// Two non-overlapping host foundations and a freestanding (host=0) wall.
	const cw::FoundationId hostA = makeHost(world);
	CommitResult		   b = world.commitFoundation(box(200000, 0, 240000, 40000), "wood");
	ASSERT_TRUE(b.ok());
	const cw::FoundationId hostB = b.id;

	SegmentCommitResult a0 = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", hostA);
	SegmentCommitResult a1 = world.commitSegment({4000, 0}, {4000, 4000}, "wood", "thin", hostA);
	SegmentCommitResult onB = world.commitSegment({201000, 1000}, {205000, 1000}, "wood", "thin", hostB);
	SegmentCommitResult free = world.commitSegment({0, 20000}, {4000, 20000}, "wood", "thin", kInvalidFoundation);
	ASSERT_TRUE(a0.ok());
	ASSERT_TRUE(a1.ok());
	ASSERT_TRUE(onB.ok());
	ASSERT_TRUE(free.ok());

	// hostA returns exactly its two walls, in segments() order; the other-host and
	// freestanding walls are excluded.
	const std::vector<cw::SegmentId> onA = world.segmentsOnFoundation(hostA);
	ASSERT_EQ(onA.size(), 2u);
	EXPECT_EQ(onA[0], a0.id);
	EXPECT_EQ(onA[1], a1.id);

	EXPECT_EQ(world.segmentsOnFoundation(hostB).size(), 1u);
	EXPECT_EQ(world.segmentsOnFoundation(hostB).front(), onB.id);

	// An unknown id and the invalid sentinel both return empty.
	EXPECT_TRUE(world.segmentsOnFoundation(98765).empty());
	EXPECT_TRUE(world.segmentsOnFoundation(kInvalidFoundation).empty());
}

// ============================================================================
// Walls: ECS-mirror mutators and versioning
// ============================================================================

TEST(ConstructionWorldWallTests, SegmentStateAndEntityMutators) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);
	SegmentCommitResult	   seg = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());

	const std::uint64_t v0 = world.version();
	EXPECT_TRUE(world.setSegmentState(seg.id, FoundationState::Built));
	EXPECT_EQ(world.getSegment(seg.id)->state, FoundationState::Built);
	EXPECT_GT(world.version(), v0);

	const std::uint64_t v1 = world.version();
	const ecs::EntityID handle = ecs::makeEntityID(9, 2);
	EXPECT_TRUE(world.setSegmentEntity(seg.id, handle));
	EXPECT_EQ(world.getSegment(seg.id)->entity, handle);
	EXPECT_GT(world.version(), v1);

	EXPECT_FALSE(world.setSegmentState(12345, FoundationState::Built));
	EXPECT_FALSE(world.setSegmentEntity(12345, handle));
}

TEST(ConstructionWorldWallTests, VersionBumpsOnWallMutationNotOnQuery) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	const std::uint64_t v0 = world.version();
	SegmentCommitResult seg = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());
	const std::uint64_t afterCommit = world.version();
	EXPECT_GT(afterCommit, v0);

	// Pure wall queries do not bump the version.
	(void)world.segmentAt({2000, 0}, 100);
	(void)world.vertexAt({0, 0});
	(void)world.getSegment(seg.id);
	(void)world.getVertex(world.vertexAt({0, 0}));
	(void)world.segmentsOfVertex(world.vertexAt({0, 0}));
	(void)world.segments();
	(void)world.vertices();
	EXPECT_EQ(world.version(), afterCommit);

	// A rejected commit (zero-length) does not bump either.
	(void)world.commitSegment({1000, 0}, {1000, 0}, "wood", "thin", host);
	EXPECT_EQ(world.version(), afterCommit);

	// addOpening bumps; split (a real commit) bumps.
	const std::uint64_t beforeOpening = world.version();
	(void)world.addOpening(seg.id, 0.5F, "door", "wood");
	EXPECT_GT(world.version(), beforeOpening);
}

TEST(ConstructionWorldWallTests, DeterministicIterationAcrossInsertionOrders) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	SegmentCommitResult a = world.commitSegment({0, 0}, {1000, 0}, "wood", "thin", host);
	SegmentCommitResult b = world.commitSegment({2000, 0}, {3000, 0}, "wood", "thin", host);
	SegmentCommitResult c = world.commitSegment({4000, 0}, {5000, 0}, "wood", "thin", host);
	ASSERT_TRUE(a.ok());
	ASSERT_TRUE(b.ok());
	ASSERT_TRUE(c.ok());

	const auto& segs = world.segments();
	ASSERT_EQ(segs.size(), 3u);
	EXPECT_EQ(segs[0].id, a.id);
	EXPECT_EQ(segs[1].id, b.id);
	EXPECT_EQ(segs[2].id, c.id);
	EXPECT_LT(a.id, b.id);
	EXPECT_LT(b.id, c.id);

	// Removing the middle preserves the relative order of the rest.
	ASSERT_TRUE(world.removeSegment(b.id));
	const auto& after = world.segments();
	ASSERT_EQ(after.size(), 2u);
	EXPECT_EQ(after[0].id, a.id);
	EXPECT_EQ(after[1].id, c.id);
}

TEST(ConstructionWorldWallTests, FoundationAndWallIdSpacesAreIndependent) {
	ConstructionWorld	   world;
	const cw::FoundationId host = makeHost(world);

	// First segment gets SegmentId 1 even though FoundationId 1 is already taken:
	// separate counters, separate id spaces.
	SegmentCommitResult seg = world.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host);
	ASSERT_TRUE(seg.ok());
	EXPECT_EQ(seg.id, 1u);
	EXPECT_EQ(host, 1u);
}
