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
	ConstructionWorld			 world;
	std::vector<::Foundation::Vec2> meters = {
		{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}};
	CommitResult result = world.commitFoundation(meters, "wood");

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

	const std::uint64_t before	   = world.version();
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

	const Ring			before	 = world.get(base.id)->ring;
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

	EXPECT_EQ(world.foundationAt({2000, 2000}), base.id);		   // interior hit
	EXPECT_EQ(world.foundationAt({9000, 9000}), kInvalidFoundation); // miss
	EXPECT_EQ(world.foundationAt({0, 2000}), base.id);			   // on boundary edge
}

TEST(ConstructionWorldTests, FoundationAtSharedEdgeTieBreaksToHighestId) {
	ConstructionWorld world;
	CommitResult	  first	 = world.commitFoundation(box(0, 0, 4000, 4000), "wood");
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

	const std::uint64_t v1	   = world.version();
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
