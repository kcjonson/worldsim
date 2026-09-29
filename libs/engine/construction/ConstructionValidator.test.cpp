#include "ConstructionValidator.h"

#include "ConstructionWorld.h"
#include "SnapEngine.h"

#include "../ecs/components/StructureBlueprint.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

using namespace engine::construction;
using engine::assets::ConstraintConfig;
using engine::assets::ConstructionRegistry;
using engine::assets::ThicknessPreset;
using ::Foundation::Vec2;

namespace {

	// Default config matches assets/config/construction/constraints.xml defaults:
	// minVertexSpacing 0.5 m, minCornerAngle 30 deg, segmentClearance 1.0 m,
	// minArea 4 m^2, maxArea 2500 m^2, maxPoints 32.
	ConstraintConfig defaults() {
		return ConstraintConfig{};
	}

	// A clean 5x5 m square: well above min area, right-angle corners, ample spacing.
	std::vector<Vec2> squareRing() {
		return {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	}

} // namespace

TEST(ConstructionValidator, FirstPointAlwaysOk) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validatePoint({}, {0.0F, 0.0F}).ok());
}

TEST(ConstructionValidator, GoodSquareCommits) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validateRing(squareRing()).ok());
}

// Non-square rings are first-class: the tools draw freeform polygons (3+ points),
// not just rectangles. A triangle (60 deg corners), a regular pentagon, and a
// concave L all clear every shape constraint and must validate.
TEST(ConstructionValidator, AcceptsTriangle) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Equilateral-ish triangle, ~10 m base: corners ~60 deg, area well above 4 m^2.
	std::vector<Vec2> triangle = {{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 8.66F}};
	EXPECT_TRUE(validator.validateRing(triangle).ok());
}

TEST(ConstructionValidator, AcceptsPentagon) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Regular pentagon, radius 6 m: interior corners 108 deg, edges ~7 m apart.
	std::vector<Vec2> pentagon;
	for (int i = 0; i < 5; ++i) {
		const float angle = static_cast<float>(i) * (2.0F * 3.14159265F / 5.0F) - 3.14159265F / 2.0F;
		pentagon.push_back({6.0F * std::cos(angle), 6.0F * std::sin(angle)});
	}
	EXPECT_TRUE(validator.validateRing(pentagon).ok());
}

TEST(ConstructionValidator, AcceptsConcaveLShape) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// L-shape with a 90 deg concave (reflex-interior) notch. Simple, no self-cross,
	// every corner >= 30 deg, opposite faces > 1 m apart.
	std::vector<Vec2> lshape = {{0.0F, 0.0F}, {8.0F, 0.0F}, {8.0F, 3.0F}, {3.0F, 3.0F}, {3.0F, 8.0F}, {0.0F, 8.0F}};
	EXPECT_TRUE(validator.validateRing(lshape).ok());
}

TEST(ConstructionValidator, RejectsTooFewPoints) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	std::vector<Vec2>	  two = {{0.0F, 0.0F}, {5.0F, 0.0F}};
	EXPECT_EQ(validator.validateRing(two).code, ValidationCode::TooFewPoints);
}

TEST(ConstructionValidator, RejectsVerticesTooClose) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Candidate only 0.2 m from the previous point (< 0.5 m spacing).
	std::vector<Vec2> chain = {{0.0F, 0.0F}};
	auto			  r = validator.validatePoint(chain, {0.2F, 0.0F});
	EXPECT_EQ(r.code, ValidationCode::VerticesTooClose);
}

TEST(ConstructionValidator, RejectsSharpCorner) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Two points going +x, candidate doubles back nearly on top of the line: the
	// corner at the middle vertex is far below 30 deg.
	std::vector<Vec2> chain = {{0.0F, 0.0F}, {5.0F, 0.0F}};
	auto			  r = validator.validatePoint(chain, {0.5F, 0.2F});
	EXPECT_EQ(r.code, ValidationCode::AngleTooSharp);
}

TEST(ConstructionValidator, RejectsSelfIntersection) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Bowtie: the closing edges cross.
	std::vector<Vec2> bowtie = {{0.0F, 0.0F}, {5.0F, 5.0F}, {5.0F, 0.0F}, {0.0F, 5.0F}};
	EXPECT_EQ(validator.validateRing(bowtie).code, ValidationCode::SelfIntersects);
}

TEST(ConstructionValidator, RejectsAreaTooSmall) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// 1.9x1.9 m square = 3.61 m^2, below the 4 m^2 floor, but with opposite edges
	// 1.9 m apart (clears the 1.0 m segment clearance) so area is the live reason.
	std::vector<Vec2> tiny = {{0.0F, 0.0F}, {1.9F, 0.0F}, {1.9F, 1.9F}, {0.0F, 1.9F}};
	EXPECT_EQ(validator.validateRing(tiny).code, ValidationCode::AreaTooSmall);
}

TEST(ConstructionValidator, RejectsTooManyPoints) {
	ConstraintConfig cfg = defaults();
	cfg.maxPoints = 4;
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	std::vector<Vec2>	  chain = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	auto				  r = validator.validatePoint(chain, {-1.0F, 2.5F});
	EXPECT_EQ(r.code, ValidationCode::TooManyPoints);
}

TEST(ConstructionValidator, RejectsOverlapWithCommitted) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// Commit a 5x5 m square at the origin.
	auto first = world.commitFoundation(squareRing(), "Wood");
	ASSERT_TRUE(first.ok());

	ConstructionValidator validator(cfg, world);
	// A square overlapping the committed one's interior.
	std::vector<Vec2> overlapping = {{2.0F, 2.0F}, {7.0F, 2.0F}, {7.0F, 7.0F}, {2.0F, 7.0F}};
	EXPECT_EQ(validator.validateRing(overlapping).code, ValidationCode::OverlapsExisting);
}

TEST(ConstructionValidator, EdgeClearanceAtThresholdPasses) {
	ConstraintConfig	  cfg = defaults(); // segmentClearance 1.0 m
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// A 5 x 1.0 m rectangle: the long top/bottom edges are exactly 1.0 m apart,
	// equal to segmentClearance. Strict-< clearance must permit the at-threshold
	// gap (area 5.0 m^2 clears the floor, every edge >= 0.5 m spacing).
	std::vector<Vec2> atThreshold = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 1.0F}, {0.0F, 1.0F}};
	EXPECT_TRUE(validator.validateRing(atThreshold).ok());
}

TEST(ConstructionValidator, EdgeClearanceJustBelowThresholdFails) {
	ConstraintConfig	  cfg = defaults(); // segmentClearance 1.0 m
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// One mm closer than the threshold (0.999 m gap): edge clearance must reject.
	std::vector<Vec2> tooClose = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 0.999F}, {0.0F, 0.999F}};
	EXPECT_EQ(validator.validateRing(tooClose).code, ValidationCode::EdgeClearanceTooSmall);
}

// --- Snap-to-close routing (the foundation tool's dead-zone fix) -----------
// The DrawingSystem fix routes a near-start blocked click to a close instead of
// erroring. These two pin the validator halves it relies on: the imperfect hand-
// drawn square is a VALID closed ring (so the rescue commits a real foundation),
// while the same near-origin point placed as a 5th OPEN-chain vertex is still
// rejected (so the fix lives in click-routing, not in loosening checkShape).

TEST(ConstructionValidator, SlightlyLongSquareClosesClean) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// A hand-drawn "square" whose third segment overshoots a little (top edge 5.3 m
	// vs the 5.0 m bottom): still a clean closed quad -- right-ish angles, edges far
	// apart, area ~26 m^2.
	std::vector<Vec2> imperfect = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.3F, 5.0F}, {0.0F, 5.0F}};
	EXPECT_TRUE(validator.validateRing(imperfect).ok());
}

TEST(ConstructionValidator, NearOriginClosingVertexStillRejectedAsOpenChain) {
	ConstraintConfig	  cfg = defaults(); // segmentClearance 1.0 m
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// Three sides of a square placed; a candidate near the start (0.2, 0.5) is ~0.5 m
	// from the first edge, so as a 5th open-chain vertex its closing edge violates the
	// 1.0 m edge clearance -- the very "too close" block the rescue sidesteps by
	// closing instead. checkShape must still reject it here.
	std::vector<Vec2> chain = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	EXPECT_EQ(validator.validatePoint(chain, {0.2F, 0.5F}).code, ValidationCode::EdgeClearanceTooSmall);
}

TEST(ConstructionValidator, DisjointFoundationOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	auto			  first = world.commitFoundation(squareRing(), "Wood");
	ASSERT_TRUE(first.ok());

	ConstructionValidator validator(cfg, world);
	// A square well clear of the first one.
	std::vector<Vec2> far = {{20.0F, 20.0F}, {25.0F, 20.0F}, {25.0F, 25.0F}, {20.0F, 25.0F}};
	EXPECT_TRUE(validator.validateRing(far).ok());
}

// --- Wall validation ------------------------------------------------------

namespace {

	geometry::Vec2i64 mm(float x, float y) {
		return geometry::quantize(Vec2{x, y});
	}

	// Standard Wood preset: 0.20 m thick -> 100 mm half-thickness. Matches
	// materials.xml so registry-resolved existing-wall thickness lines up.
	ThicknessPreset standardWood() {
		ThicknessPreset p;
		p.name = "Standard";
		p.thicknessMeters = 0.20F;
		p.thicknessMm = 200;
		p.halfThicknessMm = 100;
		return p;
	}

	// A 12x12 m Wood foundation as the host. Returns its id.
	FoundationId hostFoundation(ConstructionWorld& world) {
		std::vector<Vec2> sq = {{0.0F, 0.0F}, {12.0F, 0.0F}, {12.0F, 12.0F}, {0.0F, 12.0F}};
		auto			  f = world.commitFoundation(sq, "Wood");
		EXPECT_TRUE(f.ok());
		return f.id;
	}

	// Tests that exercise overlap/clearance against EXISTING walls need the registry
	// loaded so the validator can resolve the committed segment's half-thickness.
	std::filesystem::path constructionConfigFolder() {
		std::filesystem::path p = __FILE__; // libs/engine/construction/...test.cpp
		return p.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
	}

} // namespace

TEST(ConstructionValidator, WallFirstPointAlwaysOk) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validateWallPoint({}, {2.0F, 2.0F}, standardWood(), host).ok());
}

TEST(ConstructionValidator, WallGoodSegmentOk) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// A 6 m wall comfortably inside the 12x12 foundation.
	std::vector<Vec2> chain = {{3.0F, 3.0F}};
	EXPECT_TRUE(validator.validateWallPoint(chain, {9.0F, 3.0F}, standardWood(), host).ok());
	EXPECT_TRUE(validator.validateWallSegment({3.0F, 3.0F}, {9.0F, 3.0F}, standardWood(), host).ok());
}

TEST(ConstructionValidator, WallSegmentExactlyMinLengthPasses) {
	ConstraintConfig	  cfg = defaults(); // minSegmentLength 0.5 m
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// Exactly 0.5 m: strict-< rejects only BELOW the threshold, so this passes.
	std::vector<Vec2> chain = {{3.0F, 3.0F}};
	auto			  r = validator.validateWallPoint(chain, {3.5F, 3.0F}, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "0.5 m segment should pass at threshold";
}

TEST(ConstructionValidator, WallSegmentJustUnderMinLengthFails) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// 0.499 m: one mm under, must reject.
	std::vector<Vec2> chain = {{3.0F, 3.0F}};
	auto			  r = validator.validateWallPoint(chain, {3.499F, 3.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::SegmentTooShort);
}

TEST(ConstructionValidator, WallJunctionAngleExactlyAtThresholdPasses) {
	ConstraintConfig	  cfg = defaults(); // minWallJunctionAngle 30 deg
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// Prior segment along +x into the corner at (6,6); candidate turns by exactly
	// 30 deg from straight-through, i.e. the interior junction angle is 150 deg,
	// well above 30. Build a tight-but-legal 30 deg interior angle instead: prior
	// runs +x, candidate doubles back at 30 deg above the reverse direction.
	// interior angle between (prev->before)=(-x) and (prev->cand) = 30 deg.
	const float		  rad = 30.0F * 3.14159265F / 180.0F;
	std::vector<Vec2> chain = {{2.0F, 6.0F}, {6.0F, 6.0F}};
	const float		  len = 3.0F;
	Vec2			  cand{6.0F - len * std::cos(rad), 6.0F + len * std::sin(rad)};
	auto			  r = validator.validateWallPoint(chain, cand, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "30 deg junction should pass at threshold (measured " << r.measuredValue << ")";
}

TEST(ConstructionValidator, WallJunctionAngleTooSharpFails) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// Candidate doubles back almost on top of the prior segment: ~10 deg interior
	// junction angle, far below 30.
	const float		  rad = 10.0F * 3.14159265F / 180.0F;
	std::vector<Vec2> chain = {{2.0F, 6.0F}, {6.0F, 6.0F}};
	const float		  len = 3.0F;
	Vec2			  cand{6.0F - len * std::cos(rad), 6.0F + len * std::sin(rad)};
	auto			  r = validator.validateWallPoint(chain, cand, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::AngleTooSharp);
}

TEST(ConstructionValidator, WallChainSelfIntersectionFails) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// A chain that loops back across an earlier segment. Points form three legs;
	// the candidate fourth leg crosses the first leg.
	std::vector<Vec2> chain = {{2.0F, 2.0F}, {8.0F, 2.0F}, {8.0F, 5.0F}, {5.0F, 5.0F}};
	auto			  r = validator.validateWallPoint(chain, {5.0F, 1.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::SelfIntersects);
}

TEST(ConstructionValidator, WallBandInsideHostPasses) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// Centered well inside; the 0.1 m half-thickness band stays inside the ring.
	EXPECT_TRUE(validator.validateWallSegment({2.0F, 2.0F}, {10.0F, 2.0F}, standardWood(), host).ok());
}

TEST(ConstructionValidator, WallBandPokingOutsideHostFails) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// Centerline ON the bottom edge (y = 0): the band's lower half (100 mm) hangs
	// outside the foundation, so containment must fail.
	auto r = validator.validateWallSegment({2.0F, 0.0F}, {10.0F, 0.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::NotContainedInHostFoundation);
}

TEST(ConstructionValidator, WallEndpointOutsideHostFails) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world);
	ConstructionValidator validator(cfg, world);
	// One endpoint past the right edge (x = 14 > 12): clearly outside the host.
	auto r = validator.validateWallSegment({3.0F, 6.0F}, {14.0F, 6.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::NotContainedInHostFoundation);
}

// Tracing a wall along every foundation edge is the core build flow ("build walls
// all around its edges"). Each perimeter wall spans the two outer-face-flush mitered
// corners the WallTool's snap produces, so its full band sits ON the foundation and
// containment passes. This is the regression guard for "can't build walls along
// foundation edges": the validator stays strict (a raw on-edge centerline is still
// rejected, see WallBandPokingOutsideHostFails); the inset snap is what keeps the
// band inside.
TEST(ConstructionValidator, PerimeterWallsAlongEdgesAreContained) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	FoundationId		  host = hostFoundation(world); // 12x12 axis-aligned
	ConstructionValidator validator(cfg, world);

	const auto&		  ring = world.get(host)->ring;
	const std::size_t n = ring.size();
	for (std::size_t i = 0; i < n; ++i) {
		const Vec2 a = outerFaceFlushCorner(ring, i, standardWood().halfThicknessMm);
		const Vec2 b = outerFaceFlushCorner(ring, (i + 1) % n, standardWood().halfThicknessMm);
		EXPECT_TRUE(validator.validateWallSegment(a, b, standardWood(), host).ok())
			<< "perimeter wall on edge " << i << " should be contained after outer-face-flush inset";
	}
}

TEST(ConstructionValidator, PerimeterWallsOnDiagonalEdgesAreContained) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// A diamond host (square rotated 45 deg): every edge is diagonal, so the inward
	// offset is irrational and stresses the millimeter-rounding safety bias.
	std::vector<Vec2> diamond = {{12.0F, 0.0F}, {24.0F, 12.0F}, {12.0F, 24.0F}, {0.0F, 12.0F}};
	auto			  f = world.commitFoundation(diamond, "Wood");
	ASSERT_TRUE(f.ok());
	ConstructionValidator validator(cfg, world);

	const auto&		  ring = world.get(f.id)->ring;
	const std::size_t n = ring.size();
	for (std::size_t i = 0; i < n; ++i) {
		const Vec2 a = outerFaceFlushCorner(ring, i, standardWood().halfThicknessMm);
		const Vec2 b = outerFaceFlushCorner(ring, (i + 1) % n, standardWood().halfThicknessMm);
		EXPECT_TRUE(validator.validateWallSegment(a, b, standardWood(), f.id).ok())
			<< "diagonal perimeter wall on edge " << i << " should be contained";
	}
}

TEST(ConstructionValidator, PerimeterWallsOnAcuteCornerFoundationContained) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// A tall isosceles triangle whose apex is a 30 deg interior corner (the legal
	// minimum, minCornerAngle). The outer-face-flush miter extends farthest from the
	// corner at the sharpest angle (d / sin(theta/2) ~ 3.9*d here), so this is the
	// worst case for the miter landing outside or the band poking out. All three
	// perimeter walls must still be contained.
	std::vector<Vec2> triangle = {{0.0F, 10.0F}, {-2.68F, 0.0F}, {2.68F, 0.0F}};
	auto			  f = world.commitFoundation(triangle, "Wood");
	ASSERT_TRUE(f.ok());
	ConstructionValidator validator(cfg, world);

	const auto&		  ring = world.get(f.id)->ring;
	const std::size_t n = ring.size();
	for (std::size_t i = 0; i < n; ++i) {
		const Vec2 a = outerFaceFlushCorner(ring, i, standardWood().halfThicknessMm);
		const Vec2 b = outerFaceFlushCorner(ring, (i + 1) % n, standardWood().halfThicknessMm);
		EXPECT_TRUE(validator.validateWallSegment(a, b, standardWood(), f.id).ok())
			<< "acute-corner perimeter wall on edge " << i << " should be contained";
	}
}

// Overlap / clearance / crossing all compare against existing committed walls, so
// they need the registry loaded to resolve the existing wall's thickness.
class WallVsExisting : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()))
			<< "load construction config from " << constructionConfigFolder().string();
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }
};

TEST_F(WallVsExisting, ParallelWallsAtClearancePass) {
	ConstraintConfig  cfg = defaults(); // minParallelClearance 0.8 m
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing wall at y=2, Wood/Standard (0.2 m thick, faces at 1.9 and 2.1).
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 2.0F), mm(10.0F, 2.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// New parallel wall whose near face is exactly 0.8 m from the existing near
	// face. Existing top face at y=2.1; new bottom face must be at 2.9, so the new
	// centerline (0.1 m half) sits at y=3.0. Face-to-face gap = 2.9 - 2.1 = 0.8 m.
	auto r = validator.validateWallSegment({2.0F, 3.0F}, {10.0F, 3.0F}, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "0.8 m face-to-face clearance should pass at threshold";
}

TEST_F(WallVsExisting, ParallelWallsBelowClearanceFail) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 2.0F), mm(10.0F, 2.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// New centerline at y=2.95: bottom face at 2.85, gap to existing top (2.1) is
	// 0.75 m < 0.8 m. Must reject.
	auto r = validator.validateWallSegment({2.0F, 2.95F}, {10.0F, 2.95F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::ParallelClearanceTooSmall);
}

TEST_F(WallVsExisting, NonParallelNearbyWallPasses) {
	ConstraintConfig  cfg = defaults(); // minParallelClearance 0.8 m
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing horizontal wall at y=2 (top face at 2.1).
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 2.0F), mm(10.0F, 2.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A candidate at an ANGLE that starts within the parallel-clearance distance of
	// the existing wall (bottom face ~0.65 m above the existing top face) but is not
	// parallel, does not overlap, and does not cross. Parallel-clearance must not
	// apply to non-parallel runs, so this is a legal placement.
	auto r = validator.validateWallSegment({4.0F, 2.85F}, {7.0F, 4.5F}, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "non-parallel nearby wall must not be rejected for parallel clearance (code " << static_cast<int>(r.code) << ")";
}

TEST_F(WallVsExisting, OverlappingWallsFail) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A wall running right on top of the existing one (same centerline, parallel):
	// the bands coincide, a clear overlap. Offset slightly in x so they don't share
	// an endpoint vertex (which would make them "joined" and exempt).
	auto r = validator.validateWallSegment({3.0F, 6.0F}, {9.0F, 6.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::WallsOverlap);
}

TEST_F(WallVsExisting, CrossingWallsReportXCrossing) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing horizontal wall.
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A vertical wall whose centerline properly crosses the existing one's interior.
	auto r = validator.validateWallSegment({6.0F, 2.0F}, {6.0F, 10.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::XCrossing);
}

TEST_F(WallVsExisting, JoinedWallsAtJunctionPass) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing wall ends at the shared vertex (6,6).
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 6.0F), mm(6.0F, 6.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// New wall starts at the same vertex and turns 90 deg up. Their bands meet at
	// the junction (gap zero) but joining is allowed: must pass.
	auto r = validator.validateWallSegment({6.0F, 6.0F}, {6.0F, 10.0F}, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "walls joined at a junction must be exempt from overlap/clearance";
}

TEST_F(WallVsExisting, TJunctionOnExistingInteriorPasses) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing horizontal wall (2,6)->(10,6), Wood/Standard. Its vertices are the
	// two endpoints; (6,6) is interior, NOT a stored vertex.
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A perpendicular wall whose lower endpoint lands ON the existing wall's
	// interior at (6,6) and rises to (6,10). The two bands necessarily touch at
	// that junction, but it is a legitimate T-junction (commitSegment splits the
	// host there), so it must be exempt from overlap/clearance and PASS -- not be
	// mis-rejected as WallsOverlap the way a shared-endpoint join is exempt.
	auto r = validator.validateWallSegment({6.0F, 6.0F}, {6.0F, 10.0F}, standardWood(), host);
	EXPECT_TRUE(r.ok()) << "T-junction (endpoint on existing interior) must pass; got code " << static_cast<int>(r.code);
}

TEST_F(WallVsExisting, ParallelOverlapStillFailsDespiteTouchingJunction) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	// Existing horizontal wall (2,6)->(10,6).
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A wall collinear with the existing one and overlapping it along its length
	// (4,6)->(8,6): same line, bands fully coincident. intersectSegments reports
	// CollinearOverlap (not EndpointTouch), so the T-junction exemption must NOT
	// fire and the overlap must still be rejected.
	auto r = validator.validateWallSegment({4.0F, 6.0F}, {8.0F, 6.0F}, standardWood(), host);
	EXPECT_EQ(r.code, ValidationCode::WallsOverlap);
}

TEST_F(WallVsExisting, DistantParallelWallOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = hostFoundation(world);
	ASSERT_TRUE(world.commitSegment(mm(2.0F, 2.0F), mm(10.0F, 2.0F), "Wood", "Standard", host).ok());

	ConstructionValidator validator(cfg, world);
	// A parallel wall 5 m away: ample clearance, no overlap.
	auto r = validator.validateWallSegment({2.0F, 7.0F}, {10.0F, 7.0F}, standardWood(), host);
	EXPECT_TRUE(r.ok());
}

// --- Opening validation ---------------------------------------------------
//
// validateOpening resolves the opening type's width and the material from the
// ConstructionRegistry (the same way validateWallSegment resolves existing-wall
// thickness), so these tests load the real construction config.

class OpeningValidation : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()))
			<< "load construction config from " << constructionConfigFolder().string();
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }

	// Commit a single wall of `lengthMeters` along +x at y=6 inside a 20x20 host,
	// returning its segment id. The host is wide enough for any test length here.
	SegmentId buildWall(ConstructionWorld& world, float lengthMeters) {
		std::vector<Vec2> sq = {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}};
		auto			  f = world.commitFoundation(sq, "Wood");
		EXPECT_TRUE(f.ok());
		auto seg = world.commitSegment(mm(2.0F, 6.0F), mm(2.0F + lengthMeters, 6.0F), "Wood", "Standard", f.id);
		EXPECT_TRUE(seg.ok());
		return seg.id;
	}
};

TEST_F(OpeningValidation, DoorAtMidpointValid) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	SegmentId		  seg = buildWall(world, 6.0F); // plenty long for a 0.9 m door

	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validateOpening(seg, 0.5F, "Door", "Wood").ok());
}

TEST_F(OpeningValidation, UnknownSegmentRejected) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// No segment with id 999 exists.
	EXPECT_EQ(validator.validateOpening(999, 0.5F, "Door", "Wood").code, ValidationCode::OpeningSegmentInvalid);
}

TEST_F(OpeningValidation, UnknownTypeRejected) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	SegmentId		  seg = buildWall(world, 6.0F);

	ConstructionValidator validator(cfg, world);
	EXPECT_EQ(validator.validateOpening(seg, 0.5F, "Skylight", "Wood").code, ValidationCode::OpeningTypeInvalid);
}

TEST_F(OpeningValidation, UnknownMaterialRejected) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	SegmentId		  seg = buildWall(world, 6.0F);

	ConstructionValidator validator(cfg, world);
	EXPECT_EQ(validator.validateOpening(seg, 0.5F, "Door", "Adamantium").code, ValidationCode::OpeningMaterialInvalid);
}

TEST_F(OpeningValidation, ParamOutOfRangeRejected) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	SegmentId		  seg = buildWall(world, 6.0F);

	ConstructionValidator validator(cfg, world);
	EXPECT_EQ(validator.validateOpening(seg, 1.5F, "Door", "Wood").code, ValidationCode::OpeningParamOutOfRange);
	EXPECT_EQ(validator.validateOpening(seg, -0.1F, "Door", "Wood").code, ValidationCode::OpeningParamOutOfRange);
}

TEST_F(OpeningValidation, TooCloseToEndRejected) {
	ConstraintConfig  cfg = defaults(); // openingMargin 0.3 m
	ConstructionWorld world;
	// 6 m wall. A 0.9 m door (half 0.45) at t=0.05 sits 0.3 m from the v0 end:
	// near-edge clearance = 0.05*6 - 0.45 = -0.15 m, well below the 0.3 m margin.
	SegmentId seg = buildWall(world, 6.0F);

	ConstructionValidator validator(cfg, world);
	auto				  r = validator.validateOpening(seg, 0.05F, "Door", "Wood");
	EXPECT_EQ(r.code, ValidationCode::OpeningMarginTooSmall);
}

TEST_F(OpeningValidation, WallTooShortRejected) {
	ConstraintConfig  cfg = defaults(); // openingMargin 0.3 m
	ConstructionWorld world;
	// A 1.0 m wall cannot host a 0.9 m door plus 2*0.3 m margins (needs 1.5 m).
	SegmentId seg = buildWall(world, 1.0F);

	ConstructionValidator validator(cfg, world);
	EXPECT_EQ(validator.validateOpening(seg, 0.5F, "Door", "Wood").code, ValidationCode::OpeningWallTooShort);
}

TEST_F(OpeningValidation, SecondOpeningOverlappingFirstRejected) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// 8 m wall: room for two doors if spaced apart. Place the first at t=0.4.
	SegmentId seg = buildWall(world, 8.0F);
	ASSERT_NE(world.addOpening(seg, 0.4F, "Door", "Wood"), kInvalidOpening);

	ConstructionValidator validator(cfg, world);
	// A second door at t=0.45 (0.4 m apart along an 8 m wall = 0.4 m between
	// centers). Each half-width is 0.45 m, so the near edges already overlap; the
	// inter-opening margin makes it worse. Must reject as OpeningOverlap.
	auto r = validator.validateOpening(seg, 0.45F, "Door", "Wood");
	EXPECT_EQ(r.code, ValidationCode::OpeningOverlap);
}

TEST_F(OpeningValidation, SecondOpeningClearOfFirstValid) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// 8 m wall. First door near the v0 end, second near the v1 end: well clear.
	SegmentId seg = buildWall(world, 8.0F);
	ASSERT_NE(world.addOpening(seg, 0.2F, "Door", "Wood"), kInvalidOpening);

	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validateOpening(seg, 0.8F, "Door", "Wood").ok());
}

TEST(ConstructionValidator, FoundationValidationUnchangedWithWalls) {
	// Committing a wall must not perturb foundation validatePoint/validateRing.
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	auto			  f = world.commitFoundation(squareRing(), "Wood");
	ASSERT_TRUE(f.ok());
	ASSERT_TRUE(world.commitSegment(mm(1.0F, 1.0F), mm(4.0F, 1.0F), "Wood", "Standard", f.id).ok());

	ConstructionValidator validator(cfg, world);
	// A disjoint square still validates clean.
	std::vector<Vec2> far = {{20.0F, 20.0F}, {25.0F, 20.0F}, {25.0F, 25.0F}, {20.0F, 25.0F}};
	EXPECT_TRUE(validator.validateRing(far).ok());
	// And a good in-progress foundation point still passes.
	std::vector<Vec2> chain = {{20.0F, 20.0F}, {25.0F, 20.0F}};
	EXPECT_TRUE(validator.validatePoint(chain, {25.0F, 25.0F}).ok());
}

// --- Foundation clearance -------------------------------------------------

TEST(ConstructionValidator, FoundationTooCloseToAnotherRejected) {
	ConstraintConfig  cfg = defaults(); // pathingClearance 0.7 m
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(squareRing(), "Wood").ok());

	ConstructionValidator validator(cfg, world);
	std::vector<Vec2>	  sliver = {{5.3F, 0.0F}, {10.0F, 0.0F}, {10.0F, 5.0F}, {5.3F, 5.0F}};
	EXPECT_EQ(validator.validateRing(sliver).code, ValidationCode::TooCloseToFoundation);
}

TEST(ConstructionValidator, FoundationSnappedEdgeToEdgeOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(squareRing(), "Wood").ok());

	ConstructionValidator validator(cfg, world);
	std::vector<Vec2>	  adjacent = {{5.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 5.0F}, {5.0F, 5.0F}};
	EXPECT_TRUE(validator.validateRing(adjacent).ok());
	std::vector<Vec2> clear = {{5.7F, 0.0F}, {10.0F, 0.0F}, {10.0F, 5.0F}, {5.7F, 5.0F}};
	EXPECT_TRUE(validator.validateRing(clear).ok());
}

TEST(ConstructionValidator, TouchingOneLegDoesNotExcuseASliverAtTheOther) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	// An L: its notch is the square x 4..10, y 4..10.
	std::vector<Vec2> ell = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 4.0F}, {4.0F, 4.0F}, {4.0F, 10.0F}, {0.0F, 10.0F}};
	ASSERT_TRUE(world.commitFoundation(ell, "Wood").ok());

	ConstructionValidator validator(cfg, world);
	// Flush against the notch's vertical leg, 0.3 m off its horizontal one.
	std::vector<Vec2> inNotch = {{4.0F, 4.3F}, {9.0F, 4.3F}, {9.0F, 9.0F}, {4.0F, 9.0F}};
	EXPECT_EQ(validator.validateRing(inNotch).code, ValidationCode::TooCloseToFoundation);
	// Flush against both legs is fine.
	std::vector<Vec2> fitted = {{4.0F, 4.0F}, {9.0F, 4.0F}, {9.0F, 9.0F}, {4.0F, 9.0F}};
	EXPECT_TRUE(validator.validateRing(fitted).ok());
}

TEST(ConstructionValidator, SharedEdgeThatAnglesAwayOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(squareRing(), "Wood").ok());

	ConstructionValidator validator(cfg, world);
	// Shares x = 5 from y 0 to 3 with the 5x5 square, then leaves it at an angle.
	// The vertex at the end of the shared run touches; nothing else comes close.
	std::vector<Vec2> angled = {{5.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 6.0F}, {7.0F, 6.0F}, {5.0F, 3.0F}};
	EXPECT_TRUE(validator.validateRing(angled).ok());
}

// --- Foundation Add / Subtract (G3) ---------------------------------------

namespace {

	std::vector<Vec2> rect(float x0, float y0, float x1, float y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	double areaOf(const geometry::Ring& ring) {
		return std::abs(geometry::signedAreaSquareMeters(ring));
	}

} // namespace

class FoundationEditValidation : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
		const auto committed = world.commitFoundation(rect(0.0F, 0.0F, 10.0F, 10.0F), "Wood");
		ASSERT_TRUE(committed.ok());
		target = committed.id;
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }

	FoundationEditResult edit(const std::vector<Vec2>& drawn, FoundationEditMode mode) const {
		return ConstructionValidator(cfg, world).validateFoundationEdit(target, drawn, mode, &blueprint);
	}

	ConstraintConfig		cfg = defaults();
	ConstructionWorld		world;
	FoundationId			target = kInvalidFoundation;
	ecs::StructureBlueprint blueprint; // the target's mirror: fresh, so editable
};

TEST_F(FoundationEditValidation, BlueprintAddUnionsInPlace) {
	const auto r = edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add);
	ASSERT_TRUE(r.ok()) << validationReason(r.validation.code);
	EXPECT_NEAR(areaOf(r.outline), 124.0, 1e-6);
	EXPECT_TRUE(r.extension.empty());
	// A blueprint Add checks the whole drawing (the added part may be several pieces).
	EXPECT_NEAR(areaOf(r.buildsOn), 36.0, 1e-6);
}

TEST_F(FoundationEditValidation, BlueprintSubtractCarvesNotch) {
	const auto r = edit(rect(7.0F, -2.0F, 12.0F, 4.0F), FoundationEditMode::Subtract);
	ASSERT_TRUE(r.ok()) << validationReason(r.validation.code);
	EXPECT_NEAR(areaOf(r.outline), 88.0, 1e-6);
}

TEST_F(FoundationEditValidation, DeliveredBlueprintRejected) {
	blueprint.delivered = {{"Wood", 1}};
	EXPECT_EQ(edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add).validation.code, ValidationCode::EditMaterialsDelivered);
	EXPECT_EQ(edit(rect(7.0F, -2.0F, 12.0F, 4.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditMaterialsDelivered);
}

TEST_F(FoundationEditValidation, FoundationMarkedForDemolitionRejected) {
	blueprint.demolishing = true;
	EXPECT_EQ(edit(rect(7.0F, -2.0F, 12.0F, 4.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::BeingDemolished);

	// A built one too: an extension made now would hold the teardown open.
	world.setState(target, FoundationState::Built);
	blueprint.phase = ecs::StructureBlueprint::BuildPhase::Complete;
	EXPECT_EQ(edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add).validation.code, ValidationCode::BeingDemolished);
	blueprint.demolishing = false;
	EXPECT_TRUE(edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add).ok());
}

TEST_F(FoundationEditValidation, BuiltSubtractRejected) {
	world.setState(target, FoundationState::Built);
	EXPECT_EQ(edit(rect(7.0F, -2.0F, 12.0F, 4.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::BuiltNeverShrinks);
}

TEST_F(FoundationEditValidation, BuiltAddYieldsExtensionOfTheAddedRegionOnly) {
	world.setState(target, FoundationState::Built);
	const auto r = edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add);
	ASSERT_TRUE(r.ok()) << validationReason(r.validation.code);
	EXPECT_NEAR(areaOf(r.extension), 24.0, 1e-6);
	EXPECT_NEAR(areaOf(r.outline), 124.0, 1e-6);
	// Only the extension is new ground; the built footprint is not re-checked.
	EXPECT_EQ(r.buildsOn, r.extension);
	EXPECT_TRUE(world.commitExtension(target, r.extension).ok());
}

TEST_F(FoundationEditValidation, BuiltAddSharingAnEdgeExtendsByTheWholeDrawing) {
	world.setState(target, FoundationState::Built);
	const auto r = edit(rect(10.0F, 0.0F, 14.0F, 10.0F), FoundationEditMode::Add);
	ASSERT_TRUE(r.ok()) << validationReason(r.validation.code);
	EXPECT_NEAR(areaOf(r.extension), 40.0, 1e-6);
	EXPECT_NEAR(areaOf(r.outline), 140.0, 1e-6);
}

TEST_F(FoundationEditValidation, SecondAddWhileExtensionPendingRejected) {
	world.setState(target, FoundationState::Built);
	const auto first = edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add);
	ASSERT_TRUE(first.ok());
	const auto ext = world.commitExtension(target, first.extension);
	ASSERT_TRUE(ext.ok());

	EXPECT_EQ(edit(rect(2.0F, 8.0F, 8.0F, 14.0F), FoundationEditMode::Add).validation.code, ValidationCode::ExtensionPending);
	const auto onExtension =
		ConstructionValidator(cfg, world).validateFoundationEdit(ext.id, rect(12.0F, 2.0F, 16.0F, 8.0F), FoundationEditMode::Add, &blueprint);
	EXPECT_EQ(onExtension.validation.code, ValidationCode::ExtensionNotEditable);
}

TEST_F(FoundationEditValidation, BooleanFailuresMapToReasons) {
	EXPECT_EQ(edit(rect(20.0F, 20.0F, 25.0F, 25.0F), FoundationEditMode::Add).validation.code, ValidationCode::EditDisjoint);
	EXPECT_EQ(edit(rect(2.0F, 2.0F, 5.0F, 5.0F), FoundationEditMode::Add).validation.code, ValidationCode::EditNothingToAdd);
	EXPECT_EQ(edit(rect(3.0F, 3.0F, 6.0F, 6.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditCutsHole);
	EXPECT_EQ(edit(rect(4.0F, -2.0F, 6.0F, 12.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditSplits);
	EXPECT_EQ(edit(rect(-1.0F, -1.0F, 11.0F, 11.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditConsumes);
	EXPECT_EQ(edit(rect(20.0F, 20.0F, 25.0F, 25.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditNothingToRemove);
	EXPECT_EQ(edit(rect(10.0F, 10.0F, 14.0F, 14.0F), FoundationEditMode::Add).validation.code, ValidationCode::EditPinch);
}

TEST_F(FoundationEditValidation, BuiltAddRegionMustBeOnePiece) {
	world.setState(target, FoundationState::Built);
	EXPECT_EQ(edit(rect(-2.0F, -2.0F, 12.0F, 12.0F), FoundationEditMode::Add).validation.code, ValidationCode::ExtensionWrapsAround);
	EXPECT_EQ(edit(rect(-3.0F, 4.0F, 13.0F, 6.0F), FoundationEditMode::Add).validation.code, ValidationCode::ExtensionSplits);
}

TEST_F(FoundationEditValidation, OutlineChecksIgnoreTargetButNotOthers) {
	ASSERT_TRUE(world.commitFoundation(rect(12.0F, 0.0F, 16.0F, 10.0F), "Wood").ok());
	EXPECT_EQ(edit(rect(8.0F, 2.0F, 14.0F, 8.0F), FoundationEditMode::Add).validation.code, ValidationCode::OverlapsExisting);
	EXPECT_EQ(edit(rect(8.0F, 2.0F, 11.5F, 8.0F), FoundationEditMode::Add).validation.code, ValidationCode::TooCloseToFoundation);
	EXPECT_TRUE(edit(rect(8.0F, 2.0F, 12.0F, 8.0F), FoundationEditMode::Add).ok());
}

TEST_F(FoundationEditValidation, OutlineMustKeepShapeConstraints) {
	// A 0.6 m tongue: its two long edges sit closer than the 1 m edge clearance.
	EXPECT_EQ(edit(rect(8.0F, 4.0F, 14.0F, 4.6F), FoundationEditMode::Add).validation.code, ValidationCode::EdgeClearanceTooSmall);
}

TEST_F(FoundationEditValidation, SubtractMayNotCutUnderAWall) {
	// Standard Wood is 0.2 m thick: the centerline sits 5 cm above the notch, but
	// the lower face of the band dips into it.
	const auto wall = world.commitSegment(mm(7.5F, 6.05F), mm(9.5F, 6.05F), "Wood", "Standard", target);
	ASSERT_TRUE(wall.ok());
	EXPECT_EQ(edit(rect(7.0F, -2.0F, 12.0F, 6.0F), FoundationEditMode::Subtract).validation.code, ValidationCode::EditUnderWall);
	EXPECT_TRUE(edit(rect(7.0F, -2.0F, 12.0F, 5.0F), FoundationEditMode::Subtract).ok());
}

TEST(ConstructionValidator, EditPointJudgesOnlyTheCutter) {
	ConstraintConfig	  cfg = defaults();
	ConstructionWorld	  world;
	ConstructionValidator validator(cfg, world);
	// A 10 degree corner is fine for a cutter (validatePoint would reject it)...
	const std::vector<Vec2> chain = {{0.0F, 0.0F}, {10.0F, 0.0F}};
	const Vec2				sharp{0.15F, 1.7F};
	EXPECT_EQ(validator.validatePoint(chain, sharp).code, ValidationCode::AngleTooSharp);
	EXPECT_TRUE(validator.validateEditPoint(chain, sharp).ok());
	// ...but it must stay a simple polygon with spaced vertices.
	const std::vector<Vec2> zig = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 5.0F}};
	EXPECT_EQ(validator.validateEditPoint(zig, {5.0F, -3.0F}).code, ValidationCode::SelfIntersects);
	EXPECT_EQ(validator.validateEditPoint(zig, {10.2F, 5.1F}).code, ValidationCode::VerticesTooClose);
}
