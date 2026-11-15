#include "BuilderSpots.h"

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using construction::builderSpotCount;
using construction::builderSpots;
using construction::ringPerimeterMeters;
using geometry::Ring;

namespace {

	constexpr float kInsetMeters = 0.5F;

	Ring reversed(Ring ring) {
		std::reverse(ring.begin(), ring.end());
		return ring;
	}

	// Integer mm, counter-clockwise unless a test reverses them.
	Ring rectangle() { return Ring{{0, 0}, {8000, 0}, {8000, 4000}, {0, 4000}}; }
	Ring square() { return Ring{{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}}; }
	Ring fourByFive() { return Ring{{0, 0}, {4000, 0}, {4000, 5000}, {0, 5000}}; }
	Ring lShape() { return Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}; }
	Ring thinStrip() { return Ring{{0, 0}, {10000, 0}, {10000, 400}, {0, 400}}; }
	Ring sharpTriangle() { return Ring{{0, 0}, {10000, 0}, {0, 1500}}; }

	std::vector<std::pair<std::string, Ring>> shapes() {
		return {
			{"rectangle", rectangle()},
			{"clockwise rectangle", reversed(rectangle())},
			{"L shape", lShape()},
			{"clockwise L shape", reversed(lShape())},
			{"thin strip", thinStrip()},
			{"sharp triangle", sharpTriangle()},
		};
	}

	glm::dvec2 meters(const geometry::Vec2i64& point) {
		return {static_cast<double>(point.x) / 1000.0, static_cast<double>(point.y) / 1000.0};
	}

	/// The point on the ring `arcMeters` along it from its first vertex.
	glm::dvec2 pointAtArc(const Ring& ring, double arcMeters) {
		for (std::size_t index = 0; index < ring.size(); ++index) {
			const glm::dvec2 from = meters(ring[index]);
			const glm::dvec2 to = meters(ring[(index + 1) % ring.size()]);
			const double	 edgeLength = glm::length(to - from);
			if (arcMeters <= edgeLength) {
				return from + (to - from) * (arcMeters / edgeLength);
			}
			arcMeters -= edgeLength;
		}
		return meters(ring.front());
	}

	/// The arc length from the ring's first vertex to the point on it nearest `position`.
	double arcOf(const Ring& ring, glm::dvec2 position) {
		double nearestDistance = std::numeric_limits<double>::max();
		double nearestArc = 0.0;
		double edgeStartArc = 0.0;
		for (std::size_t index = 0; index < ring.size(); ++index) {
			const glm::dvec2 from = meters(ring[index]);
			const glm::dvec2 to = meters(ring[(index + 1) % ring.size()]);
			const glm::dvec2 edge = to - from;
			const double	 edgeLength = glm::length(edge);
			const double	 along = std::clamp(glm::dot(position - from, edge) / (edgeLength * edgeLength), 0.0, 1.0);
			const double	 distance = glm::length(position - (from + edge * along));
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearestArc = edgeStartArc + along * edgeLength;
			}
			edgeStartArc += edgeLength;
		}
		return nearestArc;
	}

	double metersToRing(const Ring& ring, glm::dvec2 position) {
		return glm::length(position - pointAtArc(ring, arcOf(ring, position)));
	}

	/// Inside or on the ring, allowing the float metres a spot is stored in to round a boundary spot
	/// a fraction of a millimetre outside.
	bool insideOrOn(const Ring& ring, glm::vec2 spot) {
		const glm::dvec2 position{spot.x, spot.y};
		return geometry::pointInPolygon(geometry::quantize({spot.x, spot.y}), ring) != geometry::PointInPolygon::Outside ||
			   metersToRing(ring, position) < 0.001;
	}

} // namespace

TEST(BuilderSpotCountTests, TheTwentySquareMeterExampleGivesThree) {
	EXPECT_EQ(builderSpotCount(20.0, 18.0, 1, 0.1F, 1.0F), 3);
}

TEST(BuilderSpotCountTests, AStepComesAtEachWholeTenSquareMeters) {
	EXPECT_EQ(builderSpotCount(9.99, 100.0, 1, 0.1F, 1.0F), 1);
	EXPECT_EQ(builderSpotCount(10.0, 100.0, 1, 0.1F, 1.0F), 2);
	EXPECT_EQ(builderSpotCount(19.99, 100.0, 1, 0.1F, 1.0F), 2);
	EXPECT_EQ(builderSpotCount(30.0, 100.0, 1, 0.1F, 1.0F), 4);
}

TEST(BuilderSpotCountTests, ARateJustUnderItsDecimalStillStepsOnTheWholeNumber) {
	EXPECT_EQ(builderSpotCount(10.0, 100.0, 0, 0.7F, 1.0F), 7);
}

TEST(BuilderSpotCountTests, AClockwiseRingsNegativeAreaCountsTheSame) {
	EXPECT_EQ(builderSpotCount(-20.0, 18.0, 1, 0.1F, 1.0F), 3);
}

TEST(BuilderSpotCountTests, SpacingCapsASmallShape) {
	// 2 m x 2 m: by area 1 + floor(4 * 10) = 41, but 8 m of perimeter holds 8 spots a metre apart.
	EXPECT_EQ(builderSpotCount(4.0, 8.0, 1, 10.0F, 1.0F), 8);
	EXPECT_EQ(builderSpotCount(4.0, 8.0, 1, 10.0F, 3.0F), 2);
}

TEST(BuilderSpotCountTests, AnAbsurdConfigurationStopsAtTheMaximum) {
	// By area 1e18 and by spacing 1e12 spots, both far past an int.
	EXPECT_EQ(builderSpotCount(1.0e9, 1.0e9, 1, 1.0e9F, 0.001F), construction::kMaxBuilderSpots);
	EXPECT_EQ(builderSpotCount(4.0, 8.0, 1, 1.0e9F, 1.0F), 8) << "below the maximum the spacing still caps";
}

TEST(BuilderSpotCountTests, NeverZero) {
	EXPECT_EQ(builderSpotCount(0.0, 0.0, 0, 0.0F, 1.0F), 1);
	EXPECT_EQ(builderSpotCount(0.2, 0.5, 1, 0.1F, 1.0F), 1) << "a perimeter shorter than the spacing still holds one";
	EXPECT_EQ(builderSpotCount(20.0, 18.0, 0, 0.0F, 1.0F), 1);
}

TEST(BuilderSpotsTests, ARingOfTooFewVerticesOrNoCountHasNoSpots) {
	EXPECT_TRUE(builderSpots(Ring{{0, 0}, {1000, 0}}, 3, kInsetMeters).empty());
	EXPECT_TRUE(builderSpots(rectangle(), 0, kInsetMeters).empty());
}

TEST(BuilderSpotsTests, PerimeterClosesTheRing) {
	EXPECT_DOUBLE_EQ(ringPerimeterMeters(rectangle()), 24.0);
	EXPECT_DOUBLE_EQ(ringPerimeterMeters(lShape()), 32.0);
}

TEST(BuilderSpotsTests, OnTheRingConsecutiveSpotsAreAnEvenArcApart) {
	for (const auto& [name, ring] : shapes()) {
		const double perimeter = ringPerimeterMeters(ring);
		for (int count : {1, 3, 7, 12}) {
			const auto spots = builderSpots(ring, count, 0.0F);
			ASSERT_EQ(spots.size(), static_cast<std::size_t>(count)) << name;
			for (int spot = 0; spot < count; ++spot) {
				const double arc = arcOf(ring, {spots[spot].x, spots[spot].y});
				EXPECT_NEAR(arc, (spot + 0.5) * perimeter / count, 1e-3) << name << " spot " << spot << " of " << count;
			}
		}
	}
}

TEST(BuilderSpotsTests, EverySpotIsInsideOrOnTheRingAndAnInsetFromItsArcPoint) {
	for (const auto& [name, ring] : shapes()) {
		const double perimeter = ringPerimeterMeters(ring);
		for (int count : {1, 3, 7, 12, 25}) {
			const auto spots = builderSpots(ring, count, kInsetMeters);
			ASSERT_EQ(spots.size(), static_cast<std::size_t>(count)) << name;
			for (int spot = 0; spot < count; ++spot) {
				EXPECT_TRUE(insideOrOn(ring, spots[spot])) << name << " spot " << spot << " of " << count;
				const glm::dvec2 arcPoint = pointAtArc(ring, (spot + 0.5) * perimeter / count);
				const double	 offset = glm::length(glm::dvec2{spots[spot].x, spots[spot].y} - arcPoint);
				const bool		 onRing = offset < 1e-3;
				const bool		 inset = std::abs(offset - kInsetMeters) < 1e-3;
				EXPECT_TRUE(onRing || inset) << name << " spot " << spot << " of " << count << " is " << offset << " m from its arc point";
			}
		}
	}
}

TEST(BuilderSpotsTests, ARectangleInsetsEverySpotWhicheverWayItWinds) {
	for (const Ring& ring : {rectangle(), reversed(rectangle())}) {
		for (const glm::vec2 spot : builderSpots(ring, 12, kInsetMeters)) {
			EXPECT_EQ(geometry::pointInPolygon(geometry::quantize({spot.x, spot.y}), ring), geometry::PointInPolygon::Inside);
			EXPECT_NEAR(metersToRing(ring, {spot.x, spot.y}), kInsetMeters, 1e-3);
		}
	}
}

TEST(BuilderSpotsTests, ASpotOnACornerInsetsIntoTheFootprintOrStaysOnItsRingPoint) {
	// Each case puts at least one spot's arc point on a vertex: the square's corners at arc 4 and 12,
	// the 4 x 5 rectangle's at arc 9, the L's convex corners at 12 and 20 (counter-clockwise; its
	// clockwise twin's at 4 and 12), and the L's reflex corner at 16 with count 1 counter-clockwise
	// and at 8 with count 2 clockwise.
	const std::vector<std::tuple<std::string, Ring, int>> cases = {
		{"square", square(), 2},
		{"clockwise square", reversed(square()), 2},
		{"4 x 5 rectangle", fourByFive(), 3},
		{"clockwise 4 x 5 rectangle", reversed(fourByFive()), 3},
		{"L shape", lShape(), 4},
		{"clockwise L shape", reversed(lShape()), 4},
		{"L shape reflex corner", lShape(), 1},
		{"clockwise L shape reflex corner", reversed(lShape()), 2},
	};
	for (const auto& [name, ring, count] : cases) {
		const double perimeter = ringPerimeterMeters(ring);
		const auto	 spots = builderSpots(ring, count, kInsetMeters);
		ASSERT_EQ(spots.size(), static_cast<std::size_t>(count)) << name;
		bool anyAtVertex = false;
		for (int spot = 0; spot < count; ++spot) {
			const glm::dvec2 arcPoint = pointAtArc(ring, (spot + 0.5) * perimeter / count);
			const glm::dvec2 position{spots[spot].x, spots[spot].y};
			const bool		 atVertex = std::any_of(ring.begin(), ring.end(), [&](const geometry::Vec2i64& vertex) {
				  return glm::length(meters(vertex) - arcPoint) < 1e-6;
			  });
			anyAtVertex = anyAtVertex || atVertex;
			const bool inside =
				geometry::pointInPolygon(geometry::quantize({spots[spot].x, spots[spot].y}), ring) == geometry::PointInPolygon::Inside &&
				metersToRing(ring, position) > 0.1;
			const bool onRingPoint = glm::length(position - arcPoint) < 1e-3;
			EXPECT_TRUE(inside || onRingPoint) << name << " spot " << spot << " at (" << position.x << ", " << position.y << ")";
			if (atVertex) {
				EXPECT_TRUE(inside) << name << " spot " << spot << ": a right-angled corner has room for the inset";
				EXPECT_NEAR(glm::length(position - arcPoint), kInsetMeters, 1e-3) << name << " spot " << spot;
			}
		}
		EXPECT_TRUE(anyAtVertex) << name << " should put a spot on a corner";
	}
}

TEST(BuilderSpotsTests, AStripThinnerThanTheInsetKeepsItsLongEdgeSpotsOnTheRing) {
	const Ring ring = thinStrip();
	for (const glm::vec2 spot : builderSpots(ring, 10, kInsetMeters)) {
		EXPECT_LT(metersToRing(ring, {spot.x, spot.y}), 1e-3) << "an inset across a 0.4 m strip would leave it";
	}
}

TEST(BuilderSpotsTests, ASpotAtASharpCornerStaysOnTheRing) {
	// Toward the 8.5 degree corner at (10, 0) the triangle is thinner than the inset, so an inset from
	// either edge there crosses the other.
	const Ring ring = sharpTriangle();
	const auto spots = builderSpots(ring, 8, kInsetMeters);
	ASSERT_EQ(spots.size(), 8U);
	bool anyOnRing = false;
	bool anyInset = false;
	for (const glm::vec2 spot : spots) {
		const double distance = metersToRing(ring, {spot.x, spot.y});
		anyOnRing = anyOnRing || distance < 1e-3;
		anyInset = anyInset || distance > 0.1;
	}
	EXPECT_TRUE(anyOnRing) << "spots near the sharp corner cannot inset";
	EXPECT_TRUE(anyInset) << "spots by the wide end still inset";
}
