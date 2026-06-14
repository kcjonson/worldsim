#include "ConstructionValidator.h"

#include "ConstructionWorld.h"

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
