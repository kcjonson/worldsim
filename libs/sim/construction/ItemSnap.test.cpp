#include "ItemSnap.h"

#include "ConstructionWorld.h"
#include "SnapTestFixtures.h"

#include <assets/ConstructionRegistry.h>
#include <assets/TurnQuanta.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

using namespace construction;
using assets::ConstructionRegistry;
using assets::SnappingConfig;
using ::Foundation::Vec2;
using snap_test::millimeters;
using snap_test::polar;

namespace {

	const auto kQuantaPerDegree = static_cast<uint32_t>(std::lround(1.0 / assets::kTurnQuantumDegrees));

	uint32_t quanta(double degrees) {
		return assets::turnQuantaFromDegrees(degrees);
	}

	// Rotatable-placed-items 14.3 and 20: the shipped config, a Wood Standard wall (0.2 m thick) at 20 degrees from
	// the origin, 6 m long, and an item 1.2 m by 0.6 m.
	class ItemSnapTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__;
			folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(ConstructionRegistry::Get().load(folder.string()));
			const Vec2 end = polar({0.0F, 0.0F}, kWallDegrees, 6.0);
			ASSERT_TRUE(
				world.commitSegment(millimeters(0.0, 0.0), millimeters(end.x, end.y), "Wood", "Standard", kInvalidFoundation, 0).ok()
			);
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		[[nodiscard]] const SnappingConfig& snapping() const { return ConstructionRegistry::Get().snapping(); }

		// A point `along` the wall from its start and `off` from its centreline, on its left (the side of +90).
		static Vec2 besideWall(double along, double off) {
			return polar(polar({0.0F, 0.0F}, kWallDegrees, along), kWallDegrees + 90.0, off);
		}

		// The item's rect, 1.2 m by 0.6 m unless given, at `pivot` and `turnDegrees`, its back (local -y) edge toward
		// the wall.
		static std::vector<Vec2> itemRect(Vec2 pivot, double turnDegrees, float halfWidth = 0.6F, float halfDepth = 0.3F) {
			std::vector<Vec2> ring;
			const double	  radians = turnDegrees * snap_test::kRadiansPerDegree;
			for (const Vec2 corner :
				 {Vec2{-halfWidth, -halfDepth}, Vec2{halfWidth, -halfDepth}, Vec2{halfWidth, halfDepth}, Vec2{-halfWidth, halfDepth}}) {
				ring.push_back(
					{pivot.x + static_cast<float>((corner.x * std::cos(radians)) - (corner.y * std::sin(radians))),
					 pivot.y + static_cast<float>((corner.x * std::sin(radians)) + (corner.y * std::cos(radians)))}
				);
			}
			return ring;
		}

		static constexpr double kWallDegrees = 20.0;
		ConstructionWorld		world;
	};

} // namespace

TEST_F(ItemSnapTest, BesideNothingTheTurnRoundsToTheAngleStep) {
	EXPECT_EQ(snapItemTurn(92.0, std::nullopt, snapping(), false), quanta(90.0));
	EXPECT_EQ(snapItemTurn(-8.0, std::nullopt, snapping(), false), quanta(345.0));
	EXPECT_EQ(snapItemTurn(359.0, std::nullopt, snapping(), false), 0U) << "rounds then wraps";
}

TEST_F(ItemSnapTest, AWallInRangeTurnsTheItemSquareToIt) {
	const std::optional<uint32_t> alignment = itemAlignmentQuanta(world, besideWall(3.0, 1.0), snapping());
	ASSERT_TRUE(alignment.has_value());
	EXPECT_EQ(*alignment, quanta(kWallDegrees));
	EXPECT_EQ(snapItemTurn(108.0, alignment, snapping(), false), quanta(110.0)) << "a quarter turn of the wall";
	EXPECT_EQ(snapItemTurn(195.0, alignment, snapping(), false), quanta(200.0));
	EXPECT_EQ(snapItemTurn(60.0, alignment, snapping(), false), quanta(60.0)) << "outside the window: the angle step";
	EXPECT_FALSE(itemAlignmentQuanta(world, besideWall(3.0, 2.5), snapping()).has_value()) << "2.4 m from its face";
}

TEST_F(ItemSnapTest, AFoundationEdgeInRangeTurnsTheItemSquareToIt) {
	ConstructionWorld foundations;
	ASSERT_TRUE(foundations.commitFoundation(snap_test::rectangle(0.0, 0.0, 35.0, 5.0, 4.0), "Wood").ok());
	const std::optional<uint32_t> alignment = itemAlignmentQuanta(foundations, polar({0.0F, 0.0F}, 35.0, 2.5), snapping());
	ASSERT_TRUE(alignment.has_value());
	EXPECT_NEAR(static_cast<double>(*alignment % (90 * kQuantaPerDegree)), static_cast<double>(quanta(35.0)), 1.0)
		<< "corners rounded to the mm";
}

TEST_F(ItemSnapTest, AltLeavesTheRawTurn) {
	const std::optional<uint32_t> alignment = itemAlignmentQuanta(world, besideWall(3.0, 1.0), snapping());
	EXPECT_EQ(snapItemTurn(92.0, std::nullopt, snapping(), true), quanta(92.0));
	EXPECT_EQ(snapItemTurn(108.0, alignment, snapping(), true), quanta(108.0));
	EXPECT_EQ(snapItemTurn(92.004, std::nullopt, snapping(), true), quanta(92.0)) << "rounded to the quantum";
}

TEST_F(ItemSnapTest, QAndEStepThroughTheAngleStepAndTheWallsQuarterTurns) {
	const std::optional<uint32_t> alignment = itemAlignmentQuanta(world, besideWall(3.0, 1.0), snapping());
	uint32_t					  turn = 0;
	turn = stepItemTurn(turn, 1, alignment, snapping(), false);
	EXPECT_EQ(turn, quanta(15.0));
	turn = stepItemTurn(turn, 1, alignment, snapping(), false);
	EXPECT_EQ(turn, quanta(20.0));
	turn = stepItemTurn(turn, 1, alignment, snapping(), false);
	EXPECT_EQ(turn, quanta(30.0));
	EXPECT_EQ(stepItemTurn(0, -1, alignment, snapping(), false), quanta(345.0));
	EXPECT_EQ(stepItemTurn(quanta(17.5), -1, std::nullopt, snapping(), false), quanta(15.0)) << "off a stop, to the stop behind";
	EXPECT_EQ(stepItemTurn(0, -1, std::nullopt, snapping(), true), quanta(359.0)) << "Alt: one degree";
	EXPECT_EQ(stepItemTurn(quanta(17.5), 1, alignment, snapping(), true), quanta(18.5));
}

TEST_F(ItemSnapTest, TheDragPointsTheFrontAtThePointer) {
	const Vec2 pivot{4.0F, -2.0F};
	EXPECT_EQ(quanta(turnDegreesToward(pivot, pivot + Vec2{0.0F, 3.0F})), 0U) << "below: front down";
	EXPECT_EQ(quanta(turnDegreesToward(pivot, pivot + Vec2{3.0F, 0.0F})), quanta(270.0)) << "right: front (1, 0)";
	EXPECT_EQ(quanta(turnDegreesToward(pivot, pivot + Vec2{-3.0F, 0.0F})), quanta(90.0));
	EXPECT_EQ(quanta(turnDegreesToward(pivot, pivot + Vec2{0.0F, -3.0F})), quanta(180.0));
}

// The item's back edge 0.15 m off the wall's face and square to it slides onto the face, within 1 mm.
TEST_F(ItemSnapTest, TheFlushSlidePutsTheBackEdgeOnTheWallFace) {
	const double				 wallFace = 0.1;
	const Vec2					 pivot = besideWall(3.0, wallFace + 0.15 + 0.3);
	const std::vector<Vec2>		 item = itemRect(pivot, kWallDegrees);
	const std::vector<FlushFace> faces = wallFaces(world, pivot, 2.0F);
	ASSERT_EQ(faces.size(), 2U);

	const Vec2 slide = flushSlide({item}, faces, snapping());
	EXPECT_NEAR(glm::length(slide), 0.15F, 0.001F);
	for (const Vec2 corner : {item[0], item[1]}) {
		const Vec2	 moved = corner + slide;
		const Vec2	 normal = polar({0.0F, 0.0F}, kWallDegrees + 90.0, 1.0);
		const double off = glm::dot(moved, normal);
		EXPECT_NEAR(off, wallFace, 0.001) << "on the face";
	}
}

TEST_F(ItemSnapTest, TheFlushSlideNeedsTheEdgeNearParallelAndFacingTheWall) {
	const std::vector<FlushFace> faces = wallFaces(world, besideWall(3.0, 1.0), 3.0F);
	const Vec2					 near = besideWall(3.0, 0.1 + 0.15 + 0.3);
	EXPECT_EQ(flushSlide({itemRect(near, kWallDegrees + 5.0)}, faces, snapping()), Vec2(0.0F, 0.0F)) << "5 degrees off";
	EXPECT_EQ(flushSlide({itemRect(besideWall(3.0, 0.1 + 0.5 + 0.3), kWallDegrees)}, faces, snapping()), Vec2(0.0F, 0.0F)) << "0.5 m off";
	EXPECT_EQ(flushSlide({itemRect(besideWall(8.0, 0.1 + 0.15 + 0.3), kWallDegrees)}, faces, snapping()), Vec2(0.0F, 0.0F))
		<< "past the wall's end";
}

namespace {
	// How far each of a ring's first two corners (the back edge of itemRect) stands off the wall's centreline.
	std::pair<double, double> backCornerOffsets(const std::vector<Vec2>& ring, Vec2 slide, double wallDegrees) {
		const Vec2 normal = polar({0.0F, 0.0F}, wallDegrees + 90.0, 1.0);
		return {glm::dot(ring[0] + slide, normal), glm::dot(ring[1] + slide, normal)};
	}
} // namespace

// A 3 m back edge 0.8 degrees off the wall (inside the 1 degree tolerance, 42 mm out over its length): the end that
// reaches furthest in goes onto the face, so neither end ends up inside it.
TEST_F(ItemSnapTest, TheFlushSlideKeepsBothEndsOfANearlyParallelEdgeOutOfTheFace) {
	const std::vector<FlushFace> faces = wallFaces(world, besideWall(3.0, 1.0), 3.0F);
	for (const double off : {0.1 + 0.2 + 0.3, 0.1 - 0.1 + 0.3}) {
		const std::vector<Vec2> item = itemRect(besideWall(3.0, off), kWallDegrees + 0.8, 1.5F, 0.3F);
		const auto [first, second] = backCornerOffsets(item, flushSlide({item}, faces, snapping()), kWallDegrees);
		EXPECT_NEAR(std::min(first, second), 0.1, 0.001) << "the inner end on the face, from " << off;
		EXPECT_GT(std::max(first, second), 0.1 + 0.03) << "the outer end off it";
	}
}

// An edge already in the wall by 0.20 or 0.25 m is pushed back out onto the face.
TEST_F(ItemSnapTest, TheFlushSlidePushesAPenetratingEdgeBackOntoTheFace) {
	const std::vector<FlushFace> faces = wallFaces(world, besideWall(3.0, 1.0), 3.0F);
	for (const double inside : {0.20, 0.25}) {
		const std::vector<Vec2> item = itemRect(besideWall(3.0, 0.1 - inside + 0.3), kWallDegrees);
		const auto [first, second] = backCornerOffsets(item, flushSlide({item}, faces, snapping()), kWallDegrees);
		EXPECT_NEAR(first, 0.1, 0.001) << inside;
		EXPECT_NEAR(second, 0.1, 0.001) << inside;
	}
}

// Under Alt the turn is free, and an edge it happens to leave parallel still slides flush (rotatable-placed-items 20).
TEST_F(ItemSnapTest, UnderAltTheFlushSlideStillPutsAParallelEdgeOnTheWall) {
	const std::optional<uint32_t> alignment = itemAlignmentQuanta(world, besideWall(3.0, 0.6), snapping());
	const uint32_t				  turn = snapItemTurn(kWallDegrees + 0.4, alignment, snapping(), true);
	EXPECT_EQ(turn, quanta(kWallDegrees + 0.4)) << "no snap under Alt";
	const std::vector<Vec2> item = itemRect(besideWall(3.0, 0.1 + 0.2 + 0.3), static_cast<double>(turn) / kQuantaPerDegree);
	const auto [first, second] =
		backCornerOffsets(item, flushSlide({item}, wallFaces(world, besideWall(3.0, 0.6), 3.0F), snapping()), kWallDegrees);
	EXPECT_NEAR(std::min(first, second), 0.1, 0.001);
}

// A side of another item's reserved region is a face too: a second box slides up against the first, side to side.
TEST_F(ItemSnapTest, TheFlushSlidePutsASideAgainstAnotherItem) {
	std::vector<FlushFace> faces;
	appendRingFaces(itemRect({0.0F, 0.0F}, 0.0), faces);
	const std::vector<Vec2> second = itemRect({1.4F, 0.05F}, 0.0);
	const Vec2				slide = flushSlide({second}, faces, snapping());
	EXPECT_NEAR(slide.x, -0.2F, 0.0001F);
	EXPECT_NEAR(slide.y, 0.0F, 0.0001F);

	std::vector<Vec2> reversed(second.rbegin(), second.rend());
	EXPECT_EQ(flushSlide({reversed}, faces, snapping()), slide) << "either winding";
}
