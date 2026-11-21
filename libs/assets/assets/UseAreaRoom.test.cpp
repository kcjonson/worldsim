#include "assets/UseAreaRoom.h"

#include <gtest/gtest.h>

#include <vector>

using namespace assets;

namespace {

	std::vector<glm::vec2> rect(float minX, float minY, float maxX, float maxY) {
		return {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
	}

	std::vector<AreaPolygon> area(float minX, float minY, float maxX, float maxY) {
		return {{.outer = rect(minX, minY, maxX, maxY)}};
	}

	geometry::Ring millimetres(const std::vector<glm::vec2>& ring) {
		geometry::Ring out;
		for (const glm::vec2 point : ring) {
			out.push_back({std::llround(point.x * 1000.0F), std::llround(point.y * 1000.0F)});
		}
		return out;
	}

	// Open ground over a rectangle, as two triangles, ending all round it.
	OpenGround groundOver(float minX, float minY, float maxX, float maxY) {
		const geometry::Ring corners = millimetres(rect(minX, minY, maxX, maxY));
		OpenGround			 ground;
		ground.faces.push_back({corners[0], corners[1], corners[2]});
		ground.faces.push_back({corners[0], corners[2], corners[3]});
		for (size_t i = 0; i < 4; ++i) {
			ground.edges.emplace_back(corners[i], corners[(i + 1) % 4]);
		}
		return ground;
	}

	constexpr float kRadius = kLargestColonistRadiusMeters;

} // namespace

// One colonist fits in a strip a millimetre wider than his disc, and none in one a millimetre narrower; a square smaller
// than the disc erodes to nothing (rotatable-placed-items 4.2).
TEST(UseAreaRoomTest, ADiscFitsByAMillimetreOrNotAtAll) {
	EXPECT_FALSE(standingRegion(area(0.0F, 0.0F, 3.0F, 0.601F), {}, nullptr, kRadius).empty());
	EXPECT_TRUE(standingRegion(area(0.0F, 0.0F, 3.0F, 0.599F), {}, nullptr, kRadius).empty());
	EXPECT_TRUE(standingRegion(area(0.0F, 0.0F, 0.5F, 0.5F), {}, nullptr, kRadius).empty()) << "eroded to nothing";
	// Far from the origin, where the world's float positions stand.
	EXPECT_FALSE(standingRegion(area(4000.0F, -2500.0F, 4003.0F, -2499.399F), {}, nullptr, kRadius).empty());
}

// A body in the area takes its ground and a disc's width round it; the room left beside it decides.
TEST(UseAreaRoomTest, ABodyInTheAreaTakesItsRoom) {
	const std::vector<AreaPolygon> strip = area(0.0F, 0.0F, 2.0F, 1.0F);
	EXPECT_TRUE(standingRegion(strip, {rect(0.5F, -0.5F, 1.5F, 1.5F)}, nullptr, kRadius).empty()) << "0.5 m left either side";
	EXPECT_FALSE(standingRegion(strip, {rect(0.7F, -0.5F, 1.3F, 1.5F)}, nullptr, kRadius).empty()) << "0.7 m left either side";
	EXPECT_TRUE(standingRegion(strip, {rect(-1.0F, 0.32F, 3.0F, 2.0F)}, nullptr, kRadius).empty()) << "2 cm left along the edge";
}

// With open ground, what is off it (water, a wall, a placed obstacle, ground not meshed) is no place to stand: ground
// ending 0.5 m into a 1 m area leaves no room, ground over the whole of it does.
TEST(UseAreaRoomTest, GroundEndingInTheAreaTakesItsRoom) {
	const std::vector<AreaPolygon> square = area(0.0F, 0.0F, 1.0F, 1.0F);
	const OpenGround			   half = groundOver(-1.0F, -1.0F, 0.5F, 2.0F);
	const OpenGround			   whole = groundOver(-1.0F, -1.0F, 2.0F, 2.0F);
	EXPECT_TRUE(standingRegion(square, {}, &half, kRadius).empty());
	EXPECT_FALSE(standingRegion(square, {}, &whole, kRadius).empty());
}

// Two areas each have their own room: one taken by a body, the other still fits a colonist.
TEST(UseAreaRoomTest, EachAreaHasItsOwnRoom) {
	const std::vector<std::vector<glm::vec2>> body{rect(-1.5F, -0.4F, -0.5F, 0.4F)};
	EXPECT_TRUE(standingRegion(area(-1.4F, -0.3F, -0.6F, 0.3F), body, nullptr, kRadius).empty());
	EXPECT_FALSE(standingRegion(area(0.6F, -0.4F, 1.4F, 0.4F), body, nullptr, kRadius).empty());
}

// A stand point is the colonist himself when he stands in the region, else the region's nearest point, a little in
// from its edge.
TEST(UseAreaRoomTest, TheStandPointIsTheNearestPointOfTheRegion) {
	const StandingRegion region = standingRegion(area(0.0F, 0.0F, 2.0F, 2.0F), {}, nullptr, kRadius);
	ASSERT_EQ(region.pieces.size(), 1U);
	const geometry::RegionPolygon& piece = region.pieces.front();
	EXPECT_EQ(nearestPointIn(piece, {1.0F, 1.2F}), glm::vec2(1.0F, 1.2F));
	const glm::vec2 fromLeft = nearestPointIn(piece, {-5.0F, 1.0F});
	EXPECT_NEAR(fromLeft.x, 0.32F, 0.002F);
	EXPECT_NEAR(fromLeft.y, 1.0F, 0.002F);
	EXPECT_TRUE(pieceHolds(piece, fromLeft));
	EXPECT_TRUE(pieceHolds(piece, pointInside(piece)));
}
