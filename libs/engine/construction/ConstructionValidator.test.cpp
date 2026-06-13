#include "ConstructionValidator.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <vector>
#include <gtest/gtest.h>

using namespace engine::construction;
using engine::assets::ConstraintConfig;
using ::Foundation::Vec2;

namespace {

	// Default config matches assets/config/construction/constraints.xml defaults:
	// minVertexSpacing 0.5 m, minCornerAngle 30 deg, segmentClearance 1.0 m,
	// minArea 4 m^2, maxArea 2500 m^2, maxPoints 32.
	ConstraintConfig defaults() { return ConstraintConfig{}; }

	// A clean 5x5 m square: well above min area, right-angle corners, ample spacing.
	std::vector<Vec2> squareRing() {
		return {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	}

} // namespace

TEST(ConstructionValidator, FirstPointAlwaysOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validatePoint({}, {0.0F, 0.0F}).ok());
}

TEST(ConstructionValidator, GoodSquareCommits) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	EXPECT_TRUE(validator.validateRing(squareRing()).ok());
}

TEST(ConstructionValidator, RejectsTooFewPoints) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	std::vector<Vec2> two = {{0.0F, 0.0F}, {5.0F, 0.0F}};
	EXPECT_EQ(validator.validateRing(two).code, ValidationCode::TooFewPoints);
}

TEST(ConstructionValidator, RejectsVerticesTooClose) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	// Candidate only 0.2 m from the previous point (< 0.5 m spacing).
	std::vector<Vec2> chain = {{0.0F, 0.0F}};
	auto			  r		= validator.validatePoint(chain, {0.2F, 0.0F});
	EXPECT_EQ(r.code, ValidationCode::VerticesTooClose);
}

TEST(ConstructionValidator, RejectsSharpCorner) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	// Two points going +x, candidate doubles back nearly on top of the line: the
	// corner at the middle vertex is far below 30 deg.
	std::vector<Vec2> chain = {{0.0F, 0.0F}, {5.0F, 0.0F}};
	auto			  r		= validator.validatePoint(chain, {0.5F, 0.2F});
	EXPECT_EQ(r.code, ValidationCode::AngleTooSharp);
}

TEST(ConstructionValidator, RejectsSelfIntersection) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	// Bowtie: the closing edges cross.
	std::vector<Vec2> bowtie = {{0.0F, 0.0F}, {5.0F, 5.0F}, {5.0F, 0.0F}, {0.0F, 5.0F}};
	EXPECT_EQ(validator.validateRing(bowtie).code, ValidationCode::SelfIntersects);
}

TEST(ConstructionValidator, RejectsAreaTooSmall) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	// 1.9x1.9 m square = 3.61 m^2, below the 4 m^2 floor, but with opposite edges
	// 1.9 m apart (clears the 1.0 m segment clearance) so area is the live reason.
	std::vector<Vec2> tiny = {{0.0F, 0.0F}, {1.9F, 0.0F}, {1.9F, 1.9F}, {0.0F, 1.9F}};
	EXPECT_EQ(validator.validateRing(tiny).code, ValidationCode::AreaTooSmall);
}

TEST(ConstructionValidator, RejectsTooManyPoints) {
	ConstraintConfig cfg = defaults();
	cfg.maxPoints		 = 4;
	ConstructionWorld world;
	ConstructionValidator validator(cfg, world);
	std::vector<Vec2> chain = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	auto			  r		= validator.validatePoint(chain, {-1.0F, 2.5F});
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

TEST(ConstructionValidator, DisjointFoundationOk) {
	ConstraintConfig  cfg = defaults();
	ConstructionWorld world;
	auto first = world.commitFoundation(squareRing(), "Wood");
	ASSERT_TRUE(first.ok());

	ConstructionValidator validator(cfg, world);
	// A square well clear of the first one.
	std::vector<Vec2> far = {{20.0F, 20.0F}, {25.0F, 20.0F}, {25.0F, 25.0F}, {20.0F, 25.0F}};
	EXPECT_TRUE(validator.validateRing(far).ok());
}
