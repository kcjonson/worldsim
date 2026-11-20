// The cover's pieces (sight-softening.md 3 and 7.2): the feather, a soft body's triangle and fins, an
// opaque shadow edge's band and fin, the corner followed and its ease, and when one colonist's drawn
// shadows are built. Each expected value is worked out here from the spec's formulas and limits, not
// read back from the code under test.

#include "world/rendering/CoverPieces.h"
#include "world/rendering/DrawnShadows.h"
#include "world/rendering/VisibilityCoverRenderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using worldview::ValueTriangle;

	constexpr double kDegree = std::numbers::pi / 180.0;
	// 8 pixels a metre, the game's default zoom.
	constexpr double kPixelsPerMm = 0.008;

	// The greatest value any piece holds at the point, or nothing when none covers it, each value
	// interpolated over its triangle.
	std::optional<double> valueAt(const std::vector<ValueTriangle>& pieces, glm::dvec2 point) {
		std::optional<double> greatest;
		for (const ValueTriangle& piece : pieces) {
			const glm::dvec2 origin = piece.points[0];
			const glm::dvec2 near = piece.points[1];
			const glm::dvec2 far = piece.points[2];
			const double	 area = (near.x - origin.x) * (far.y - origin.y) - (near.y - origin.y) * (far.x - origin.x);
			if (area == 0.0) {
				continue;
			}
			const double	 first = ((near.x - point.x) * (far.y - point.y) - (near.y - point.y) * (far.x - point.x)) / area;
			const double	 second = ((far.x - point.x) * (origin.y - point.y) - (far.y - point.y) * (origin.x - point.x)) / area;
			const double	 third = 1.0 - first - second;
			constexpr double kEdge = -1e-9;
			if (first >= kEdge && second >= kEdge && third >= kEdge) {
				const double value = first * piece.values[0] + second * piece.values[1] + third * piece.values[2];
				greatest = std::max(greatest.value_or(value), value);
			}
		}
		return greatest;
	}

	double valueOrZero(const std::vector<ValueTriangle>& pieces, glm::dvec2 point) {
		return valueAt(pieces, point).value_or(0.0);
	}

	vision::SightBody bodyOf(const std::vector<Vec2i64>& corners, vision::BodyKind kind, std::uint64_t id) {
		vision::SightBody body;
		body.kind = kind;
		body.id = id;
		Vec2i64 low = corners[0];
		Vec2i64 high = corners[0];
		double	sumX = 0.0;
		double	sumY = 0.0;
		for (std::size_t corner = 0; corner < corners.size(); ++corner) {
			body.edges.push_back({corners[corner], corners[(corner + 1) % corners.size()]});
			body.previousEdge.push_back(static_cast<std::uint32_t>((corner + corners.size() - 1) % corners.size()));
			sumX += static_cast<double>(corners[corner].x);
			sumY += static_cast<double>(corners[corner].y);
			low = {std::min(low.x, corners[corner].x), std::min(low.y, corners[corner].y)};
			high = {std::max(high.x, corners[corner].x), std::max(high.y, corners[corner].y)};
		}
		if (kind == vision::BodyKind::Opaque) {
			body.faces = vision::facesOf(body.edges, body.previousEdge);
		}
		body.centre = {std::llround(sumX / static_cast<double>(corners.size())), std::llround(sumY / static_cast<double>(corners.size()))};
		body.diameterMm = std::llround(std::hypot(static_cast<double>(high.x - low.x), static_cast<double>(high.y - low.y)));
		return body;
	}

	// A placed ring as vision hands it out: a counter-clockwise rectangle centred at (x, y) millimetres,
	// turned by `turn` radians about its centre.
	vision::SightBody
	rectangleBody(double x, double y, double width, double height, vision::BodyKind kind, std::uint64_t id = 1, double turn = 0.0) {
		std::vector<Vec2i64> corners;
		for (const glm::dvec2 offset :
			 {glm::dvec2{-width, -height}, glm::dvec2{width, -height}, glm::dvec2{width, height}, glm::dvec2{-width, height}}) {
			const glm::dvec2 half = offset / 2.0;
			corners.push_back(
				{std::llround(x + half.x * std::cos(turn) - half.y * std::sin(turn)),
				 std::llround(y + half.x * std::sin(turn) + half.y * std::cos(turn))}
			);
		}
		return bodyOf(corners, kind, id);
	}

	// A counter-clockwise regular polygon of `sides` corners `radius` from (x, y).
	std::vector<Vec2i64> regularRing(double x, double y, double radius, std::size_t sides) {
		std::vector<Vec2i64> corners;
		for (std::size_t corner = 0; corner < sides; ++corner) {
			const double angle = 2.0 * std::numbers::pi * static_cast<double>(corner) / static_cast<double>(sides);
			corners.push_back({std::llround(x + radius * std::cos(angle)), std::llround(y + radius * std::sin(angle))});
		}
		return corners;
	}

	// A bright outline that cuts nothing near the origin.
	const std::vector<Vec2i64> kOpenBright = {{-40000, -40000}, {40000, -40000}, {40000, 40000}, {-40000, 40000}};

	glm::dvec2 along(double angle, double range, glm::dvec2 from = {0.0, 0.0}) {
		return from + range * glm::dvec2{std::cos(angle), std::sin(angle)};
	}

	double strengthOf(const vision::SightTunables& tunables) {
		return static_cast<double>(tunables.softShadowStrengthPercent) / 100.0;
	}

	// One colonist's drawn shadows frame by frame, every due body built: the budget is the cover's.
	struct Drawn {
		worldview::DrawnShadows					  shadows;
		std::uint64_t							  frame = 0;
		std::vector<worldview::DrawnShadows::Due> due;

		std::size_t step(
			const ecs::VisionSystem::SightBodies& sight,
			std::uint64_t						  revision,
			glm::dvec2							  eye,
			double								  walkedMm,
			const vision::SightTunables&		  tunables,
			double								  brightReachMm = 30000.0,
			double								  seconds = 1.0 / 60.0
		) {
			++frame;
			shadows.update(sight, revision, eye, walkedMm, seconds, kOpenBright, brightReachMm, kPixelsPerMm, tunables, frame);
			due.clear();
			shadows.appendDue(due);
			for (const worldview::DrawnShadows::Due& ring : due) {
				shadows.build(ring.ring);
			}
			return due.size();
		}

		[[nodiscard]] std::vector<ValueTriangle> softPieces() const {
			std::vector<ValueTriangle> pieces;
			for (const worldview::DrawnShadows::Ring& ring : shadows.rings()) {
				pieces.insert(pieces.end(), ring.pieces.begin(), ring.pieces.end());
			}
			return pieces;
		}

		// The body as the nearest ring holding it last drew it, or none.
		[[nodiscard]] const worldview::DrawnShadows::SoftDrawn* body(std::uint64_t id) const {
			for (const worldview::DrawnShadows::Ring& ring : shadows.rings()) {
				for (const worldview::DrawnShadows::SoftDrawn& drawn : ring.drawn) {
					if (drawn.bodyId == id) {
						return &drawn;
					}
				}
			}
			return nullptr;
		}
	};

} // namespace

// An edge's feather is 1 on the edge and falls linearly to 0 a feather into the area on its left.
TEST(CoverPieces, AnEdgesFeatherFallsAcrossItsWidth) {
	const std::vector<worldview::DirectedEdge> edges = {{Vec2i64{0, 0}, Vec2i64{10000, 0}}};
	std::vector<ValueTriangle>				   pieces;
	worldview::appendFeather(edges, 1500.0, pieces);
	EXPECT_NEAR(valueOrZero(pieces, {5000.0, 0.0}), 1.0, 1e-6);
	EXPECT_NEAR(valueOrZero(pieces, {5000.0, 750.0}), 0.5, 1e-6);
	EXPECT_NEAR(valueOrZero(pieces, {2000.0, 1200.0}), 0.2, 1e-6);
	EXPECT_FALSE(valueAt(pieces, {5000.0, 1600.0}).has_value()) << "past the feather";
	EXPECT_FALSE(valueAt(pieces, {5000.0, -10.0}).has_value()) << "outside the area";
}

// A right turn (the area's inside angle past 180 degrees) takes a fan that fades with the distance to
// the corner, to within the chords' 1%, in triangles of at most 14 degrees; a left turn takes none.
TEST(CoverPieces, AReflexCornerTakesAFanAndAConvexOneNone) {
	constexpr double						   kFeather = 1500.0;
	const std::vector<worldview::DirectedEdge> reflex = {{Vec2i64{0, 0}, Vec2i64{10000, 0}}, {Vec2i64{10000, 0}, Vec2i64{10000, -10000}}};
	std::vector<ValueTriangle>				   pieces;
	worldview::appendFeather(reflex, kFeather, pieces);
	EXPECT_EQ(pieces.size(), 4U + 7U) << "two rectangles of two triangles and a 90 degree fan in steps of at most 14 degrees";
	for (const double angle : {10.0, 45.0, 80.0}) {
		for (const double distance : {300.0, 750.0, 1400.0}) {
			const double expected = 1.0 - distance / kFeather;
			EXPECT_NEAR(valueOrZero(pieces, along(angle * kDegree, distance, {10000.0, 0.0})), expected, 0.01)
				<< angle << " degrees, " << distance;
		}
	}

	const std::vector<worldview::DirectedEdge> convex = {{Vec2i64{0, 0}, Vec2i64{10000, 0}}, {Vec2i64{10000, 0}, Vec2i64{10000, 10000}}};
	pieces.clear();
	worldview::appendFeather(convex, kFeather, pieces);
	EXPECT_EQ(pieces.size(), 4U);
	EXPECT_NEAR(valueOrZero(pieces, {9500.0, 1000.0}), 1.0 - 500.0 / kFeather, 1e-6) << "the nearer side's fade, never darker";
}

// A soft body seen face on (3.2): its foot value is the strength times its width over the ray, its
// triangle closes shadowLength = distance x width / (ray - width) behind its near face, fading along
// it, nothing is drawn in front of it, and a fin runs beside each side past its corner's range.
TEST(CoverPieces, ASoftBodysTriangleCollapsesBehindIt) {
	const vision::SightTunables				   tunables;
	const vision::SightBody					   trunk = rectangleBody(5000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft);
	std::vector<ValueTriangle>				   pieces;
	const std::optional<worldview::SoftShadow> shadow = worldview::appendSoftShadow(trunk, {0.0, 0.0}, tunables, kOpenBright, pieces);
	ASSERT_TRUE(shadow.has_value());
	const double foot = strengthOf(tunables) * 400.0 / 3000.0;
	const double length = 4800.0 * 400.0 / (3000.0 - 400.0);
	EXPECT_NEAR(shadow->widthAsSeen, 400.0, 1e-6);
	EXPECT_NEAR(shadow->distance, 4800.0, 1e-6);
	EXPECT_NEAR(shadow->footValue, foot, 1e-9);
	EXPECT_NEAR(shadow->shadowLength, length, 1e-6);

	EXPECT_NEAR(valueOrZero(pieces, {4800.5, 0.0}), foot, 1e-3) << "the foot, on the near face";
	EXPECT_NEAR(valueOrZero(pieces, {5250.0, 0.0}), foot * (1.0 - 450.0 / length), 1e-3) << "inside the body's own footprint";
	EXPECT_NEAR(valueOrZero(pieces, {4800.0 + 0.9 * length, 0.0}), foot * 0.1, 1e-3);
	EXPECT_EQ(valueOrZero(pieces, {4800.0 + length + 20.0, 0.0}), 0.0) << "closed";
	EXPECT_EQ(valueOrZero(pieces, {4700.0, 0.0}), 0.0) << "nothing in front of the body";

	// At 90% of the way the wedge is a tenth of the silhouette's half angle wide, and the fin reaches
	// (shadowEdgeWidthMm / 2) x (range - cornerDistance) / cornerDistance past it.
	const double range = 4800.0 + 0.9 * length;
	const double halfWidth = range * std::tan(0.1 * std::atan(200.0 / 4800.0));
	const double corner = std::hypot(4800.0, 200.0);
	const double fin = 600.0 * (range - corner) / corner;
	EXPECT_GT(valueOrZero(pieces, {range, halfWidth + fin / 2.0}), 0.0) << "inside the fin";
	EXPECT_LT(valueOrZero(pieces, {range, halfWidth + fin / 2.0}), foot * 0.1 + 0.012 / worldview::kSeenDimAlpha);
	EXPECT_EQ(valueOrZero(pieces, {range, halfWidth + fin + 20.0}), 0.0) << "past the fin";
}

// Every point of a sliced triangle and its fins holds the rule to within softShadowToleranceThousandths on
// screen: footValue x (1 - progress) inside the silhouette shrunk by progress, and the side's
// value falling linearly across the fin, sampled along the whole length wherever the slices fall. A
// trunk whose foot is within the tolerance of nothing draws no piece.
TEST(CoverPieces, ASlicedTriangleHoldsTheRuleToTheTolerance) {
	const vision::SightTunables tunables;
	const double tolerance = static_cast<double>(tunables.softShadowToleranceThousandths) / 1000.0 / worldview::kSeenDimAlpha + 1e-6;
	for (const double width : {340.0, 820.0, 1385.0}) {
		for (const double distance : {5000.0, 12000.0}) {
			const vision::SightBody	   trunk = rectangleBody(distance + width / 2.0, 0.0, width, width, vision::BodyKind::Soft);
			std::vector<ValueTriangle> pieces;
			const std::optional<worldview::SoftShadow> shadow =
				worldview::appendSoftShadow(trunk, {0.0, 0.0}, tunables, kOpenBright, pieces);
			ASSERT_TRUE(shadow.has_value());
			const double halfAngle = std::atan2(width / 2.0, distance);
			const double corner = std::hypot(distance, width / 2.0);
			for (double progress = 0.05; progress < 0.97; progress += 0.03) {
				const double range = shadow->distance + progress * shadow->shadowLength;
				const double inside = halfAngle * (1.0 - progress);
				const double value = shadow->footValue * (1.0 - progress);
				for (const double share : {-0.9, -0.5, 0.0, 0.5, 0.9}) {
					EXPECT_NEAR(valueOrZero(pieces, along(share * inside, range)), value, tolerance)
						<< width << " at " << distance << ", " << progress;
				}
				// Across the fin beside the counter-clockwise side, past where it is wider than 5 mm.
				const double finWidth = 600.0 * (range - corner) / corner;
				if (finWidth < 20.0) {
					continue;
				}
				const glm::dvec2 side = along(inside, range);
				const glm::dvec2 outward = glm::dvec2{-std::sin(inside), std::cos(inside)};
				// The curved side is held to the hard side's 5 mm where its fin is narrow, so the rule is read
				// over the points that close.
				const double shift = worldview::kDrawnShadowSliceToleranceMm / finWidth;
				for (const double across : {0.25, 0.5, 0.75}) {
					const double drawnValue = valueOrZero(pieces, side + across * finWidth * outward);
					EXPECT_LE(drawnValue, value * (1.0 - std::max(0.0, across - shift)) + tolerance)
						<< width << " at " << distance << ", " << progress << " across " << across;
					EXPECT_GE(drawnValue, value * (1.0 - std::min(1.0, across + shift)) - tolerance)
						<< width << " at " << distance << ", " << progress << " across " << across;
				}
			}
		}
	}
	std::vector<ValueTriangle>				   faint;
	const std::optional<worldview::SoftShadow> far = worldview::appendSoftShadow(
		rectangleBody(25000.0, 0.0, 50.0, 50.0, vision::BodyKind::Soft), {0.0, 0.0}, tunables, kOpenBright, faint
	);
	ASSERT_TRUE(far.has_value());
	ASSERT_LT(far->footValue, static_cast<double>(tunables.softShadowToleranceThousandths) / 1000.0 / worldview::kSeenDimAlpha);
	EXPECT_TRUE(faint.empty());
}

// Triangles a body, by count, seen face on: a trunk (0.34 m) and a wall stub (0.54 m) at 5, 10, and 20
// m, and a wide oak (1.385 m). The fin's fade, a product, takes |value step| / (4 x tolerance) columns
// a slice; the side's curve takes slices only past what its fin hides.
TEST(CoverPieces, TrianglesABodyByCount) {
	const vision::SightTunables tunables;
	struct Case {
		double		width;
		double		distance;
		std::size_t most;
	};
	for (const Case shape :
		 {Case{340.0, 5000.0, 9},
		  Case{340.0, 10000.0, 9},
		  Case{340.0, 20000.0, 9},
		  Case{540.0, 5000.0, 12},
		  Case{540.0, 10000.0, 12},
		  Case{540.0, 20000.0, 12},
		  Case{1385.0, 5000.0, 37},
		  Case{1385.0, 10000.0, 37},
		  Case{1385.0, 20000.0, 37}}) {
		std::vector<ValueTriangle> pieces;
		worldview::appendSoftShadow(
			rectangleBody(shape.distance + shape.width / 2.0, 0.0, shape.width, shape.width, vision::BodyKind::Soft),
			{0.0, 0.0},
			tunables,
			kOpenBright,
			pieces
		);
		EXPECT_LE(pieces.size(), shape.most) << shape.width << " at " << shape.distance;
		EXPECT_GE(pieces.size(), 3U);
	}
}

// Scaled by width and capped per object: a veteran oak (0.82 m square) seen face on and at its diagonal
// from 5 m, and no soft body darker than the strength allows.
TEST(CoverPieces, ATrunksShadowScalesWithItsWidthAsSeen) {
	const vision::SightTunables				   tunables;
	const double							   strength = strengthOf(tunables);
	std::vector<ValueTriangle>				   pieces;
	const vision::SightBody					   faceOn = rectangleBody(5410.0, 0.0, 820.0, 820.0, vision::BodyKind::Soft);
	const std::optional<worldview::SoftShadow> square = worldview::appendSoftShadow(faceOn, {0.0, 0.0}, tunables, kOpenBright, pieces);
	ASSERT_TRUE(square.has_value());
	EXPECT_NEAR(square->footValue, strength * 820.0 / 3000.0, 0.001);
	EXPECT_NEAR(square->shadowLength, 5000.0 * 820.0 / 2180.0, 1.0) << "1.88 m";

	vision::SightBody diagonal = faceOn;
	for (geometry::OccluderSegment& edge : diagonal.edges) {
		for (Vec2i64* point : {&edge.a, &edge.b}) {
			const double x = static_cast<double>(point->x) - 5410.0;
			const double y = static_cast<double>(point->y);
			*point = {std::llround(5000.0 + 820.0 / std::sqrt(2.0) + (x - y) / std::sqrt(2.0)), std::llround((x + y) / std::sqrt(2.0))};
		}
	}
	const std::optional<worldview::SoftShadow> turned = worldview::appendSoftShadow(diagonal, {0.0, 0.0}, tunables, kOpenBright, pieces);
	ASSERT_TRUE(turned.has_value());
	EXPECT_NEAR(turned->widthAsSeen, 820.0 * std::sqrt(2.0), 2.0);
	EXPECT_NEAR(turned->footValue, strength * 820.0 * std::sqrt(2.0) / 3000.0, 0.001);
	EXPECT_NEAR(turned->shadowLength, 3150.0, 10.0) << "3.15 m";

	for (const double width : {150.0, 600.0, 1390.0}) {
		pieces.clear();
		const std::optional<worldview::SoftShadow> soft = worldview::appendSoftShadow(
			rectangleBody(8000.0, 0.0, 100.0, width, vision::BodyKind::Soft), {0.0, 0.0}, tunables, kOpenBright, pieces
		);
		ASSERT_TRUE(soft.has_value());
		EXPECT_LE(soft->footValue, strength * 1400.0 / 3000.0) << "capped below the opaque threshold over the ray";
		EXPECT_NEAR(soft->footValue, strength * width / 3000.0, 0.002) << "scaled by width";
	}
}

// Limit 1: a pine (0.28 m square) 5 m off, from any angle, darkens visible ground by at most 0.04 on
// screen at its foot, and nothing of it, fins included, is left 1 m behind its near face.
TEST(CoverPieces, LimitOneAPineFiveMetresOff) {
	const vision::SightTunables tunables;
	for (double turn = 0.0; turn <= 90.0; turn += 7.5) {
		const vision::SightBody	   pine = rectangleBody(5000.0, 0.0, 280.0, 280.0, vision::BodyKind::Soft, 1, turn * kDegree);
		std::vector<ValueTriangle> pieces;
		const std::optional<worldview::SoftShadow> shadow = worldview::appendSoftShadow(pine, {0.0, 0.0}, tunables, kOpenBright, pieces);
		ASSERT_TRUE(shadow.has_value());
		EXPECT_LE(shadow->footValue * worldview::kSeenDimAlpha, 0.04) << turn << " degrees";
		for (const ValueTriangle& piece : pieces) {
			for (std::size_t corner = 0; corner < 3; ++corner) {
				if (piece.values[corner] > 0.0F) {
					EXPECT_LE(glm::length(piece.points[corner]), shadow->distance + 1000.0)
						<< turn << " degrees: a piece holding value past 1 m behind";
				}
			}
		}
	}
}

// Limit 3: the widest soft body (just under placedOpaqueLeastDiameterMm) darkens its foot by at most
// 0.1 on screen from any distance, and is gone within its own distance behind it; a veteran oak whose
// triangle runs past the bright rim carries at most 0.05 on screen where the rim cuts it.
TEST(CoverPieces, LimitThreeTheWidestTrunkAndItsStreakToTheRim) {
	const vision::SightTunables tunables;
	const double				widest = static_cast<double>(tunables.placedOpaqueLeastDiameterMm - 1);
	for (const double distance : {1500.0, 5000.0, 10000.0, 20000.0}) {
		std::vector<ValueTriangle>				   pieces;
		const std::optional<worldview::SoftShadow> shadow = worldview::appendSoftShadow(
			rectangleBody(distance + 50.0, 0.0, 100.0, widest, vision::BodyKind::Soft), {0.0, 0.0}, tunables, kOpenBright, pieces
		);
		ASSERT_TRUE(shadow.has_value());
		EXPECT_LE(shadow->footValue * worldview::kSeenDimAlpha, 0.1) << distance;
		EXPECT_LE(shadow->shadowLength, shadow->distance) << distance;
	}

	// His bright shape a 64-gon 29.84 m round him, a veteran oak (1.385 m across as seen) 25 m off: its
	// 21 m triangle meets the rim a fifth of the way along, where it would carry 0.15.
	const std::vector<Vec2i64>				   rim = regularRing(0.0, 0.0, 29840.0, 64);
	std::vector<ValueTriangle>				   pieces;
	const std::optional<worldview::SoftShadow> oak =
		worldview::appendSoftShadow(rectangleBody(25050.0, 0.0, 100.0, 1385.0, vision::BodyKind::Soft), {0.0, 0.0}, tunables, rim, pieces);
	ASSERT_TRUE(oak.has_value());
	const double uncapped = strengthOf(tunables) * 1385.0 / 3000.0 * (1.0 - 4840.0 / (25000.0 * 1385.0 / 1615.0));
	ASSERT_GT(uncapped * worldview::kSeenDimAlpha, 0.05);
	EXPECT_LE(valueOrZero(pieces, {29839.0, 0.0}) * worldview::kSeenDimAlpha, 0.05 + 1e-6);
	EXPECT_GT(valueOrZero(pieces, {29839.0, 0.0}) * worldview::kSeenDimAlpha, 0.04) << "faded to the limit, not dropped";
}

// A trunk past 16 faces (a ring of 40 corners) draws as a square one does: nothing in front of its
// near faces, its foot on them, and the triangle closing behind.
TEST(CoverPieces, ARoundTrunkOfManyFaces) {
	const vision::SightTunables				   tunables;
	const vision::SightBody					   round = bodyOf(regularRing(6000.0, 0.0, 300.0, 40), vision::BodyKind::Soft, 1);
	std::vector<ValueTriangle>				   pieces;
	const std::optional<worldview::SoftShadow> shadow = worldview::appendSoftShadow(round, {0.0, 0.0}, tunables, kOpenBright, pieces);
	ASSERT_TRUE(shadow.has_value());
	EXPECT_NEAR(shadow->distance, 5700.0, 2.0);
	EXPECT_NEAR(shadow->widthAsSeen, 600.0, 5.0);
	EXPECT_NEAR(valueOrZero(pieces, {5701.0, 0.0}), shadow->footValue, 2e-3);
	for (double angle = -2.0; angle <= 2.0; angle += 0.5) {
		const glm::dvec2 offFace = along(angle * kDegree, 5650.0);
		EXPECT_EQ(valueOrZero(pieces, offFace), 0.0) << "in front of the round face at " << angle << " degrees";
	}
	EXPECT_EQ(valueOrZero(pieces, {5700.0 + shadow->shadowLength + 20.0, 0.0}), 0.0);
}

// The band (3.1): past the cap distance both of its sides turn asin(widening / cornerDistance) from the
// ray; near the corner the lit side holds at 10 degrees and the shadow side ramps toward 180, which it
// reaches within `widening`. The fin falls from 1 to 0 across finAngle beyond the lit line.
TEST(CoverPieces, AShadowEdgesBandAndFin) {
	const vision::SightTunables tunables;
	const worldview::ShadowBand far = worldview::shadowBandAt({0.0, 0.0}, {5000.0, 0.0}, 1, 670.0, tunables);
	const double				spread = std::asin(670.0 / 5000.0);
	EXPECT_NEAR(far.litAngle, spread, 1e-9);
	EXPECT_NEAR(far.shadowAngle, spread, 1e-9);
	EXPECT_NEAR(far.finAngle, std::atan(1200.0 / 10000.0), 1e-9);

	std::vector<ValueTriangle> pieces;
	worldview::appendShadowBand(far, 30000.0, pieces);
	const glm::dvec2 apex{5000.0, 0.0};
	EXPECT_NEAR(valueOrZero(pieces, along(0.9 * spread, 3000.0, apex)), 1.0, 1e-6) << "the band's lit side";
	EXPECT_NEAR(valueOrZero(pieces, along(-0.9 * spread, 3000.0, apex)), 1.0, 1e-6) << "the band's shadow side";
	EXPECT_NEAR(valueOrZero(pieces, along(spread + far.finAngle / 2.0, 20000.0, apex)), 0.5, 0.02) << "mid fin";
	EXPECT_NEAR(valueOrZero(pieces, along(spread + 0.8 * far.finAngle, 5000.0, apex)), 0.2, 0.02);
	EXPECT_EQ(valueOrZero(pieces, along(spread + far.finAngle + 0.01, 5000.0, apex)), 0.0) << "past the fin";
	EXPECT_EQ(valueOrZero(pieces, along(-spread - 0.01, 5000.0, apex)), 0.0) << "past the shadow line";

	// litTurn -1 mirrors it.
	pieces.clear();
	worldview::appendShadowBand(worldview::shadowBandAt({0.0, 0.0}, {5000.0, 0.0}, -1, 670.0, tunables), 30000.0, pieces);
	EXPECT_NEAR(valueOrZero(pieces, along(-(spread + far.finAngle / 2.0), 20000.0, apex)), 0.5, 0.02);
	EXPECT_EQ(valueOrZero(pieces, along(spread + far.finAngle / 2.0, 20000.0, apex)), 0.0);

	const worldview::ShadowBand near = worldview::shadowBandAt({0.0, 0.0}, {1000.0, 0.0}, 1, 670.0, tunables);
	EXPECT_NEAR(near.litAngle, 10.0 * kDegree, 1e-9) << "capped";
	EXPECT_NEAR(near.shadowAngle, std::asin(0.67) + std::numbers::pi / 2.0 * (2.0 - 1000.0 / 670.0), 1e-9) << "uncapped, ramping";
	EXPECT_NEAR(worldview::shadowBandAt({0.0, 0.0}, {500.0, 0.0}, 1, 670.0, tunables).shadowAngle, std::numbers::pi, 1e-9)
		<< "within widening";
}

// The corner switch (3.1): the corner followed from him is the tangent vertex on the build corner's
// side; crossing the line of a face moves it along the face, and the band's apex eases there over
// shadowCornerEaseMm of his walk.
TEST(CoverPieces, TheFollowedCornerSwitchesAcrossAFacesLineAndTheApexEases) {
	const vision::SightBody block = rectangleBody(500.0, 500.0, 1000.0, 1000.0, vision::BodyKind::Opaque);
	// Corners 0 (0,0), 1 (1000,0), 2 (1000,1000), 3 (0,1000); from the west the top silhouette corner
	// is 3, its lit side counter-clockwise of its shadow side.
	EXPECT_EQ(worldview::followedCorner(block, 3, 1, {-3000.0, 800.0}), 3U);
	EXPECT_EQ(worldview::followedCorner(block, 3, 1, {-3000.0, 1200.0}), 2U) << "above the top face's line";
	EXPECT_EQ(worldview::followedCorner(block, 2, 1, {-3000.0, 800.0}), 3U) << "and back";

	const std::vector<vision::SightBody>  bodies = {block};
	const std::vector<vision::ShadowEdge> shadowEdges = {{.body = 0, .corner = 3, .litTurn = 1}};
	const ecs::VisionSystem::SightBodies  sight{
		 .bodies = &bodies, .shadowEdges = &shadowEdges, .leadMm = 670, .reachMm = 30800, .eyeCentre = {-3000, 800}
	 };
	const vision::SightTunables tunables;
	Drawn						drawn;
	drawn.step(sight, 1, {-3000.0, 800.0}, 0.0, tunables);
	ASSERT_EQ(drawn.shadows.edges().size(), 1U);
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, glm::dvec2(0.0, 1000.0));
	drawn.step(sight, 1, {-3000.0, 1200.0}, 400.0, tunables);
	EXPECT_EQ(drawn.shadows.edges()[0].followedCorner, 2U);
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, glm::dvec2(0.0, 1000.0)) << "the switch moves nothing on its frame";
	drawn.step(sight, 1, {-3000.0, 1200.0}, 750.0, tunables);
	EXPECT_NEAR(drawn.shadows.edges()[0].band.apex.x, 500.0, 1e-6) << "half the ease, half the face";
	EXPECT_NEAR(drawn.shadows.edges()[0].band.apex.y, 1000.0, 1e-6);
	drawn.step(sight, 1, {-3000.0, 1200.0}, 1500.0, tunables);
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, glm::dvec2(1000.0, 1000.0));

	// A new build whose edge is tagged at the corner the ease reached picks it up where it is.
	const std::vector<vision::ShadowEdge> rebuilt = {{.body = 0, .corner = 2, .litTurn = 1}};
	const ecs::VisionSystem::SightBodies  next{
		 .bodies = &bodies, .shadowEdges = &rebuilt, .leadMm = 670, .reachMm = 30800, .eyeCentre = {-3000, 1200}
	 };
	drawn.step(next, 2, {-3000.0, 1200.0}, 0.0, tunables);
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, glm::dvec2(1000.0, 1000.0));
	EXPECT_EQ(drawn.shadows.edges()[0].ease, 1.0);
}

// Limit 8e on a ring of many faces (a 24-sided rock 1 m round, 5 m off): walking north past it, the
// tangent corner moves a corner every 1.3 m or so and each build every 0.5 m tags it from its own eye,
// some builds several corners on from where the apex is easing. The apex never jumps: each frame it
// moves at most the face's length times that frame's walk over shadowCornerEaseMm, and a running ease
// is never dropped for a build's corner.
TEST(CoverPieces, ACornerSwitchOnAManySidedRingNeverSnaps) {
	const std::vector<Vec2i64>			 ring = regularRing(0.0, 0.0, 1000.0, 24);
	const std::vector<vision::SightBody> bodies = {bodyOf(ring, vision::BodyKind::Opaque, 7)};
	const vision::SightTunables			 tunables;
	const double						 face = 2.0 * 1000.0 * std::sin(std::numbers::pi / 24.0);
	// The counter-clockwise-most corner seen from the eye: the lit side is counter-clockwise of it.
	const auto tangentFrom = [&](glm::dvec2 eye) {
		std::uint32_t best = 0;
		double		  bestAngle = -10.0;
		for (std::uint32_t corner = 0; corner < ring.size(); ++corner) {
			const glm::dvec2 offset{static_cast<double>(ring[corner].x) - eye.x, static_cast<double>(ring[corner].y) - eye.y};
			const double	 angle = std::atan2(offset.y, offset.x);
			if (angle > bestAngle) {
				bestAngle = angle;
				best = corner;
			}
		}
		return best;
	};
	constexpr double				kStepMm = 33.0;
	constexpr int					kFramesABuild = 15;
	Drawn							drawn;
	std::vector<vision::ShadowEdge> edges;
	std::uint64_t					revision = 0;
	Vec2i64							buildEye{0, 0};
	std::optional<glm::dvec2>		lastApex;
	int								switches = 0;
	std::uint32_t					lastFollowed = 0;
	for (int frame = 0; frame < 240; ++frame) {
		const glm::dvec2 eye{-5000.0, -2000.0 + kStepMm * static_cast<double>(frame)};
		if (frame % kFramesABuild == 0) {
			edges = {{.body = 0, .corner = tangentFrom(eye), .litTurn = 1}};
			buildEye = {std::llround(eye.x), std::llround(eye.y)};
			++revision;
		}
		const ecs::VisionSystem::SightBodies sight{
			.bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = buildEye
		};
		drawn.step(sight, revision, eye, frame == 0 ? 0.0 : kStepMm, tunables);
		ASSERT_EQ(drawn.shadows.edges().size(), 1U) << "frame " << frame;
		const worldview::DrawnShadows::EdgeDrawn& edge = drawn.shadows.edges()[0];
		if (lastApex.has_value()) {
			// An ease restarted mid-face runs to a corner at most two faces off.
			EXPECT_LE(
				glm::length(edge.band.apex - *lastApex), 2.0 * face * kStepMm / static_cast<double>(tunables.shadowCornerEaseMm) + 1e-6
			) << "frame "
			  << frame;
			switches += edge.followedCorner != lastFollowed ? 1 : 0;
		}
		lastApex = edge.band.apex;
		lastFollowed = edge.followedCorner;
	}
	EXPECT_GE(switches, 3) << "the walk crosses several faces' lines";
}

// The drawn shadows reach no farther than the least of drawnShadowReachMm and his bright shape.
TEST(CoverPieces, TheDrawnShadowsReachNoFartherThanTheyCanShow) {
	const std::vector<vision::SightBody> bodies = {
		rectangleBody(5000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft, 1),
		rectangleBody(15000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft, 2)
	};
	const std::vector<vision::ShadowEdge> none;
	const ecs::VisionSystem::SightBodies  sight{
		 .bodies = &bodies, .shadowEdges = &none, .leadMm = 670, .reachMm = 30800, .eyeCentre = {0, 0}
	 };
	vision::SightTunables tunables;
	Drawn				  drawn;
	drawn.step(sight, 1, {0.0, 0.0}, 0.0, tunables);
	EXPECT_EQ(drawn.shadows.softBodyCount(), 2U);
	drawn.step(sight, 1, {0.0, 0.0}, 0.0, tunables, 10000.0);
	EXPECT_EQ(drawn.shadows.softBodyCount(), 1U) << "past his bright shape";
	tunables.drawnShadowReachMm = 4000;
	drawn.step(sight, 1, {0.0, 0.0}, 0.0, tunables);
	EXPECT_EQ(drawn.shadows.softBodyCount(), 0U);
	EXPECT_TRUE(drawn.softPieces().empty());
}

namespace {

	// A trunk 5 m off and one 20 m off, and a rock 7 m off with one shadow edge.
	struct Stand {
		std::vector<vision::SightBody> bodies = {
			rectangleBody(5000.0, 3000.0, 400.0, 400.0, vision::BodyKind::Soft, 1),
			rectangleBody(20000.0, -2000.0, 600.0, 600.0, vision::BodyKind::Soft, 2),
			rectangleBody(7000.0, 0.0, 1000.0, 1400.0, vision::BodyKind::Opaque, 3)
		};
		std::vector<vision::ShadowEdge>				 edges = {{.body = 2, .corner = 3, .litTurn = 1}};
		[[nodiscard]] ecs::VisionSystem::SightBodies sight(Vec2i64 eyeCentre = {0, 0}) const {
			return {.bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = eyeCentre};
		}
	};

	void expectSamePieces(const std::vector<ValueTriangle>& first, const std::vector<ValueTriangle>& second, const char* what) {
		ASSERT_EQ(first.size(), second.size()) << what;
		for (std::size_t piece = 0; piece < first.size(); ++piece) {
			EXPECT_EQ(first[piece].points, second[piece].points) << what;
			EXPECT_EQ(first[piece].values, second[piece].values) << what;
		}
	}

} // namespace

// Each ring falls due when he has walked softShadowRebuildPercent of its nearest distance since it was
// built: the trunk 5.6 m off is in the ring from 4 m, rebuilt every 360 mm, and the one 20 m off in the
// ring from 16 m, every 1,440 mm; builds arriving every 0.5 m with the same bodies make none due, and
// the bands are rebuilt on every frame he moves. A rebuilt ring is a fresh build's from the same point.
TEST(CoverPieces, EachRingKeepsItsCadenceAndTheBandsFollowHim) {
	const Stand					stand;
	const vision::SightTunables tunables;
	ASSERT_EQ(tunables.softShadowRebuildPercent, 10);
	Drawn			   drawn;
	std::array<int, 4> builds{};
	std::size_t		   bandRebuilds = 0;
	constexpr double   kFrameWalkMm = 25.0;
	constexpr int	   kFrames = 160; // 4 m
	std::uint64_t	   revision = 0;
	Vec2i64			   buildEye{0, 0};
	for (int frame = 0; frame <= kFrames; ++frame) {
		const glm::dvec2 eye{0.0, -kFrameWalkMm * static_cast<double>(frame)};
		if (frame % 20 == 0) {
			++revision;
			buildEye = {0, std::llround(eye.y)};
		}
		drawn.step(stand.sight(buildEye), revision, eye, frame == 0 ? 0.0 : kFrameWalkMm, tunables);
		bandRebuilds += drawn.shadows.counts().bandsRebuilt ? 1 : 0;
		Drawn fresh;
		fresh.step(stand.sight(buildEye), revision, eye, 0.0, tunables);
		for (std::size_t ring = 0; ring < worldview::DrawnShadows::kSoftRings; ++ring) {
			if ((drawn.shadows.counts().softChangedRings & (1U << ring)) != 0U) {
				++builds[ring];
				expectSamePieces(fresh.shadows.rings()[ring].pieces, drawn.shadows.rings()[ring].pieces, "a rebuilt ring");
			}
		}
	}
	// The first frame, then every 375 mm (15 frames) and every 1,450 mm (58 frames) of 4 m.
	EXPECT_EQ(builds[1], 1 + 10);
	EXPECT_EQ(builds[3], 1 + 2);
	EXPECT_EQ(bandRebuilds, static_cast<std::size_t>(kFrames) + 1U) << "every frame he moves";
}

// Walking toward a stand that starts out of his reach, each build holding the bodies within 30.8 m of
// its eye: every trunk that comes within reach on the walk has its triangle by the end, with no stop.
TEST(CoverPieces, BodiesEnteringReachOnAWalkAreDrawn) {
	std::vector<vision::SightBody> stand;
	for (std::uint64_t index = 0; index < 20; ++index) {
		stand.push_back(rectangleBody(
			36000.0 + 1500.0 * static_cast<double>(index % 5),
			-6000.0 + 3000.0 * static_cast<double>(index / 5),
			400.0,
			400.0,
			vision::BodyKind::Soft,
			index + 1
		));
	}
	const vision::SightTunables			  tunables;
	const std::vector<vision::ShadowEdge> none;
	std::vector<vision::SightBody>		  inReach;
	Drawn								  drawn;
	std::uint64_t						  revision = 0;
	Vec2i64								  buildEye{0, 0};
	constexpr double					  kStepMm = 33.0;
	glm::dvec2							  eye{0.0, 0.0};
	for (int frame = 0; frame < 600; ++frame) {
		eye = {kStepMm * static_cast<double>(frame), 0.0};
		if (frame % 15 == 0) {
			++revision;
			buildEye = {std::llround(eye.x), 0};
			inReach.clear();
			for (const vision::SightBody& body : stand) {
				if (glm::length(glm::dvec2{static_cast<double>(body.centre.x), static_cast<double>(body.centre.y)} - eye) < 30800.0) {
					inReach.push_back(body);
				}
			}
		}
		const ecs::VisionSystem::SightBodies sight{
			.bodies = &inReach, .shadowEdges = &none, .leadMm = 670, .reachMm = 30800, .eyeCentre = buildEye
		};
		drawn.step(sight, revision, eye, frame == 0 ? 0.0 : kStepMm, tunables, 29840.0);
	}
	for (const vision::SightBody& body : stand) {
		const double distance = worldview::DrawnShadows::distanceOf(body, eye);
		if (distance < 29840.0 - 1500.0) {
			EXPECT_NE(drawn.body(body.id), nullptr) << "body " << body.id << " at " << distance;
		}
	}
}

// Standing still: one frame without a step is not a stop; once he has stood a few frames every ring
// built from elsewhere is built from where he stands, so standing it is exact, and then nothing is
// built while nothing changes. A new build that changes a body's arrival rebuilds its ring with its
// foot scaled; one that drops a body (a tree felled) takes its pieces away.
TEST(CoverPieces, StandingStillTheDrawnShadowsAreExactAndFollowTheBuild) {
	Stand						stand;
	const vision::SightTunables tunables;
	Drawn						drawn;
	for (int frame = 0; frame <= 40; ++frame) {
		drawn.step(stand.sight(), 1, {0.0, -25.0 * frame}, frame == 0 ? 0.0 : 25.0, tunables);
	}
	drawn.step(stand.sight(), 1, {0.0, -1000.0}, 0.0, tunables);
	EXPECT_EQ(drawn.shadows.counts().ringsBuilt, 0U) << "a frame without a step";
	drawn.step(stand.sight(), 1, {0.0, -1025.0}, 25.0, tunables);
	const glm::dvec2 still{0.0, -1025.0};
	std::size_t		 builtElsewhere = 0;
	for (const worldview::DrawnShadows::Ring& ring : drawn.shadows.rings()) {
		builtElsewhere += ring.builtFrom != still ? 1 : 0;
	}
	ASSERT_GT(builtElsewhere, 0U);
	std::size_t stopBuilt = 0;
	for (int frame = 0; frame < static_cast<int>(std::ceil(worldview::DrawnShadows::kStillSeconds * 60.0)) + 1; ++frame) {
		stopBuilt += drawn.step(stand.sight(), 1, still, 0.0, tunables);
	}
	EXPECT_EQ(stopBuilt, builtElsewhere) << "he stops";
	Drawn fresh;
	fresh.step(stand.sight(), 1, still, 0.0, tunables);
	expectSamePieces(fresh.softPieces(), drawn.softPieces(), "standing is exact");
	for (int frame = 0; frame < 30; ++frame) {
		EXPECT_EQ(drawn.step(stand.sight(), 1, still, 0.0, tunables), 0U);
		EXPECT_FALSE(drawn.shadows.counts().bandsRebuilt) << "standing still nothing is built";
		EXPECT_EQ(drawn.shadows.counts().softChangedRings, 0U);
	}

	// The near trunk arriving: its foot at half, then whole.
	ASSERT_NE(drawn.body(1), nullptr);
	const double whole = drawn.body(1)->shadow.footValue;
	stand.bodies[0].arrival = geometry::kFractionOne / 2;
	EXPECT_GE(drawn.step(stand.sight(), 2, still, 0.0, tunables), 1U);
	EXPECT_NEAR(drawn.body(1)->shadow.footValue, whole / 2.0, 1e-9);
	stand.bodies[0].arrival = geometry::kFractionOne;
	EXPECT_GE(drawn.step(stand.sight(), 3, still, 0.0, tunables), 1U);
	EXPECT_NEAR(drawn.body(1)->shadow.footValue, whole, 1e-9);

	// The near trunk felled.
	const std::size_t before = drawn.softPieces().size();
	const std::size_t near = drawn.body(1)->pieces;
	stand.bodies.erase(stand.bodies.begin());
	stand.edges = {{.body = 1, .corner = 3, .litTurn = 1}};
	drawn.step(stand.sight(), 4, still, 0.0, tunables);
	EXPECT_EQ(drawn.body(1), nullptr);
	EXPECT_NE(drawn.shadows.counts().softChangedRings, 0U);
	EXPECT_EQ(drawn.softPieces().size(), before - near);
}

// Wall bodies are numbered within one build of the walls' geometry, so after a wall changes the same id
// can name another building. Standing beside one building when another is built: the new build's
// shadow edge, under the old one's id, takes up no ease from the old corner, and a wall stub whose key
// now names another stub elsewhere is drawn where the new stub is.
TEST(CoverPieces, AWallBuiltOrRemovedWhileHeStandsTakesUpNothingOfTheOld) {
	const vision::SightTunables tunables;
	vision::SightBody			first = rectangleBody(3000.0, 2000.0, 2000.0, 300.0, vision::BodyKind::Opaque, 0);
	first.source = vision::SightBody::Source::Wall;
	vision::SightBody stub = rectangleBody(4000.0, -2000.0, 500.0, 150.0, vision::BodyKind::Soft, 1);
	stub.source = vision::SightBody::Source::Wall;
	std::vector<vision::SightBody>	bodies = {first, stub};
	std::vector<vision::ShadowEdge> edges = {{.body = 0, .corner = 3, .litTurn = 1}};
	Drawn							drawn;
	const glm::dvec2				still{0.0, 0.0};
	const auto						sight = [&](Vec2i64 eyeCentre) {
		 return ecs::VisionSystem::SightBodies{
								 .bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = eyeCentre
		 };
	};
	for (int frame = 0; frame < 10; ++frame) {
		drawn.step(sight({0, 0}), 1, still, 0.0, tunables);
	}
	ASSERT_EQ(drawn.shadows.edges().size(), 1U);
	ASSERT_NE(drawn.body(1), nullptr);
	const double oldStubDistance = drawn.body(1)->shadow.distance;

	// A new building far off takes id 0, the first is now id 2, and the stub's id 1 names a stub 9 m off.
	vision::SightBody second = rectangleBody(-9000.0, 6000.0, 3000.0, 300.0, vision::BodyKind::Opaque, 0);
	second.source = vision::SightBody::Source::Wall;
	vision::SightBody moved = rectangleBody(9000.0, 0.0, 500.0, 150.0, vision::BodyKind::Soft, 1);
	moved.source = vision::SightBody::Source::Wall;
	first.id = 2;
	bodies = {second, moved, first};
	edges = {{.body = 0, .corner = 2, .litTurn = 1}, {.body = 2, .corner = 3, .litTurn = 1}};
	drawn.step(sight({0, 0}), 2, still, 0.0, tunables);
	ASSERT_EQ(drawn.shadows.edges().size(), 2U);
	EXPECT_EQ(drawn.shadows.edges()[0].ease, 1.0) << "no ease taken up from the other building";
	const Vec2i64 followed = second.edges[drawn.shadows.edges()[0].followedCorner].a;
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, glm::dvec2(followed.x, followed.y)) << "at the new building's own corner";
	ASSERT_NE(drawn.body(1), nullptr);
	EXPECT_GT(drawn.body(1)->shadow.distance, oldStubDistance + 3000.0) << "the stub is the new one, 9 m off";

	// The stub removed.
	bodies = {second, first};
	edges = {{.body = 0, .corner = 2, .litTurn = 1}, {.body = 1, .corner = 3, .litTurn = 1}};
	drawn.step(sight({0, 0}), 3, still, 0.0, tunables);
	EXPECT_EQ(drawn.body(1), nullptr);
	EXPECT_TRUE(drawn.softPieces().empty());
}

// His reach follows his bright rim, which moves on most frames he walks in forest: a step that moves it
// without taking a body in or out, past the nearest ring's cadence and short of the next, re-dues the
// nearest ring alone.
TEST(CoverPieces, AMovingRimReDuesOnlyTheRingsItCrosses) {
	const std::vector<vision::SightBody> bodies = {
		rectangleBody(2000.0, 500.0, 300.0, 300.0, vision::BodyKind::Soft, 1),
		rectangleBody(10000.0, 2000.0, 400.0, 400.0, vision::BodyKind::Soft, 2),
		rectangleBody(20000.0, -3000.0, 500.0, 500.0, vision::BodyKind::Soft, 3)
	};
	const std::vector<vision::ShadowEdge> none;
	const ecs::VisionSystem::SightBodies  sight{
		 .bodies = &bodies, .shadowEdges = &none, .leadMm = 670, .reachMm = 30800, .eyeCentre = {0, 0}
	 };
	const vision::SightTunables tunables;
	Drawn						drawn;
	drawn.step(sight, 1, {0.0, 0.0}, 0.0, tunables, 28000.0);
	drawn.step(sight, 1, {0.0, -150.0}, 150.0, tunables, 27900.0);
	EXPECT_EQ(drawn.shadows.counts().ringsBuilt, 1U);
	EXPECT_EQ(drawn.shadows.counts().softChangedRings, 1U) << "the nearest ring";
	drawn.step(sight, 1, {0.0, -150.0}, 0.0, tunables, 19000.0);
	EXPECT_EQ(drawn.shadows.counts().softChangedRings, 1U << 3U) << "the rim crossing the far trunk";
}

// Under a starved queue a body leaving a ring stays in it until the ring that now holds it has been
// built with it: walking away from a trunk until it passes the 4 to 8 m ring, building that ring alone.
TEST(CoverPieces, ABodyLeavingARingStaysUntilItsNewRingHoldsIt) {
	const std::vector<vision::SightBody>  bodies = {rectangleBody(7000.0, 0.0, 300.0, 300.0, vision::BodyKind::Soft, 1)};
	const std::vector<vision::ShadowEdge> none;
	const ecs::VisionSystem::SightBodies  sight{
		 .bodies = &bodies, .shadowEdges = &none, .leadMm = 670, .reachMm = 30800, .eyeCentre = {0, 0}
	 };
	const vision::SightTunables tunables;
	worldview::DrawnShadows		shadows;
	std::uint64_t				frame = 1;
	const auto					update = [&](double eyeX, double walked) {
		 shadows.update(sight, 1, {eyeX, 0.0}, walked, 1.0 / 60.0, kOpenBright, 30000.0, kPixelsPerMm, tunables, frame++);
	};
	update(0.0, 0.0);
	for (std::size_t ring = 0; ring < worldview::DrawnShadows::kSoftRings; ++ring) {
		shadows.build(ring);
	}
	const auto holds = [&](std::size_t ring) {
		return std::ranges::any_of(shadows.rings()[ring].drawn, [](const auto& drawn) { return drawn.bodyId == 1U; });
	};
	ASSERT_TRUE(holds(1));
	ASSERT_FALSE(holds(2));
	for (double eyeX = -100.0; eyeX >= -3000.0; eyeX -= 100.0) {
		update(eyeX, 100.0);
		shadows.build(1);
		EXPECT_TRUE(holds(1)) << "at " << eyeX << ": its new ring has not been built with it";
	}
	shadows.build(2);
	ASSERT_TRUE(holds(2));
	update(-3100.0, 100.0);
	shadows.build(1);
	EXPECT_FALSE(holds(1)) << "now that ring two holds it";
}

// A crawl is not a stop: a colonist creeping at 0.03 m/s, at 60 and at 144 frames a second, never stays
// within kStillMm of one point for kStillSeconds, so no frame rebuilds his rings as a stop does, and his
// 60 mm in two seconds is short of the nearest ring's 100 mm cadence.
TEST(CoverPieces, ACrawlIsNotAStop) {
	const Stand					stand;
	const vision::SightTunables tunables;
	for (const double framesPerSecond : {60.0, 144.0}) {
		Drawn drawn;
		for (int frame = 0; frame < 30; ++frame) {
			drawn.step(stand.sight(), 1, {0.0, 0.0}, 0.0, tunables, 30000.0, 1.0 / framesPerSecond);
		}
		const double stepMm = 30.0 / framesPerSecond;
		std::size_t	 built = 0;
		for (int frame = 1; frame <= static_cast<int>(2.0 * framesPerSecond); ++frame) {
			drawn.step(stand.sight(), 1, {0.0, -stepMm * frame}, stepMm, tunables, 30000.0, 1.0 / framesPerSecond);
			built += drawn.shadows.counts().ringsBuilt;
		}
		EXPECT_EQ(built, 0U) << framesPerSecond << " frames a second";
	}
}

// A wall renumbering leaves an unrelated building's running ease alone: the block's ease is half way
// along its face when a new build gives its id to another building, and the ease runs on from there.
TEST(CoverPieces, ARenumberingKeepsAnotherBuildingsEase) {
	vision::SightBody block = rectangleBody(500.0, 500.0, 1000.0, 1000.0, vision::BodyKind::Opaque, 1);
	block.source = vision::SightBody::Source::Wall;
	vision::SightBody other = rectangleBody(-6000.0, -9000.0, 3000.0, 300.0, vision::BodyKind::Opaque, 0);
	other.source = vision::SightBody::Source::Wall;
	std::vector<vision::SightBody>	bodies = {other, block};
	std::vector<vision::ShadowEdge> edges = {{.body = 1, .corner = 3, .litTurn = 1}};
	const auto						sight = [&](Vec2i64 eyeCentre) {
		 return ecs::VisionSystem::SightBodies{
								 .bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = eyeCentre
		 };
	};
	const vision::SightTunables tunables;
	Drawn						drawn;
	drawn.step(sight({-3000, 800}), 1, {-3000.0, 800.0}, 0.0, tunables);
	drawn.step(sight({-3000, 800}), 1, {-3000.0, 1200.0}, 400.0, tunables);
	drawn.step(sight({-3000, 800}), 1, {-3000.0, 1200.0}, 750.0, tunables);
	ASSERT_EQ(drawn.shadows.edges().size(), 1U);
	const glm::dvec2 apex = drawn.shadows.edges()[0].band.apex;
	ASSERT_NEAR(drawn.shadows.edges()[0].ease, 0.5, 1e-9);

	block.id = 0;
	other.id = 1;
	bodies = {block, other};
	edges = {{.body = 0, .corner = 2, .litTurn = 1}};
	drawn.step(sight({-3000, 1200}), 2, {-3000.0, 1200.0}, 0.0, tunables);
	ASSERT_EQ(drawn.shadows.edges().size(), 1U);
	EXPECT_NEAR(drawn.shadows.edges()[0].ease, 0.5, 1e-9) << "the ease taken up, not started again";
	EXPECT_EQ(drawn.shadows.edges()[0].band.apex, apex);
}

// Walking west past a room's north-west corner, the silhouette's west end switches from that corner to
// the south-west one along the west face, which is three edges (a junction's short end at each corner
// and the wall between). A build lands one frame after the switch, its edge tagged at the south-west
// corner while the apex has eased only 114 mm, still on the north-west junction's short end: the face is
// the whole straight run, so the build takes the ease up and the apex never jumps. Each frame it moves
// at most the face's 5.2 m times that frame's walk over shadowCornerEaseMm.
TEST(CoverPieces, ACornerSwitchAlongARoomsFaceNeverSnaps) {
	// The room's outer outline, 7 x 5 m inside walls 0.2 m thick, counter-clockwise.
	const std::vector<Vec2i64> outline = {
		{20900, 4900},
		{21100, 4900},
		{27900, 4900},
		{28100, 4900},
		{28100, 5100},
		{28100, 9900},
		{28100, 10100},
		{27900, 10100},
		{21100, 10100},
		{20900, 10100},
		{20900, 9900},
		{20900, 5100}
	};
	std::vector<vision::SightBody> bodies = {bodyOf(outline, vision::BodyKind::Opaque, 3)};
	bodies[0].source = vision::SightBody::Source::Wall;
	const vision::SightTunables tunables;
	constexpr double			kStepMm = 33.0;
	constexpr int				kFramesABuild = 15;
	// Frame 104 is the first west of the face's line at x = 20.9 m, and a build lands at frame 105.
	constexpr double kStartMm = 20900.0 + kStepMm * 104.0 - 10.0;
	const auto		 eyeAt = [&](int frame) { return glm::dvec2{kStartMm - kStepMm * static_cast<double>(frame), 0.0}; };
	// The silhouette's end on the west side, from the first eye, and that end's corner from each build's.
	const std::optional<vision::Silhouette> first = vision::silhouetteFrom(bodies[0], eyeAt(0));
	ASSERT_TRUE(first.has_value());
	const bool westIsClockwise = outline[first->clockwise].x < outline[first->counterClockwise].x;
	const int  litTurn = westIsClockwise ? -1 : 1;
	ASSERT_EQ(outline[westIsClockwise ? first->clockwise : first->counterClockwise], (Vec2i64{20900, 4900}));

	const double					maxStep = 5200.0 * kStepMm / static_cast<double>(tunables.shadowCornerEaseMm) + 1e-6;
	Drawn							drawn;
	std::vector<vision::ShadowEdge> edges;
	std::uint64_t					revision = 0;
	Vec2i64							buildEye{0, 0};
	std::optional<glm::dvec2>		lastApex;
	for (int frame = 0; frame < 170; ++frame) {
		const glm::dvec2 eye = eyeAt(frame);
		if (frame % kFramesABuild == 0) {
			const std::optional<vision::Silhouette> silhouette = vision::silhouetteFrom(bodies[0], eye);
			ASSERT_TRUE(silhouette.has_value());
			edges = {{.body = 0, .corner = westIsClockwise ? silhouette->clockwise : silhouette->counterClockwise, .litTurn = litTurn}};
			buildEye = {std::llround(eye.x), std::llround(eye.y)};
			++revision;
		}
		const ecs::VisionSystem::SightBodies sight{
			.bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = buildEye
		};
		drawn.step(sight, revision, eye, frame == 0 ? 0.0 : kStepMm, tunables);
		ASSERT_EQ(drawn.shadows.edges().size(), 1U) << "frame " << frame;
		const glm::dvec2 apex = drawn.shadows.edges()[0].band.apex;
		if (lastApex.has_value()) {
			EXPECT_LE(glm::length(apex - *lastApex), maxStep) << "frame " << frame;
		}
		lastApex = apex;
	}
	EXPECT_EQ(*lastApex, glm::dvec2(20900.0, 10100.0)) << "the ease reached the south-west corner";
}

namespace {

	// Whether `point` lies in the ring `corners` or within 2 mm of its boundary.
	bool inOrOnRing(const std::vector<Vec2i64>& corners, glm::dvec2 point) {
		bool inside = false;
		for (std::size_t corner = 0; corner < corners.size(); ++corner) {
			const glm::dvec2 from{static_cast<double>(corners[corner].x), static_cast<double>(corners[corner].y)};
			const glm::dvec2 to{
				static_cast<double>(corners[(corner + 1) % corners.size()].x), static_cast<double>(corners[(corner + 1) % corners.size()].y)
			};
			const glm::dvec2 along = to - from;
			const double	 share = std::clamp(glm::dot(point - from, along) / glm::dot(along, along), 0.0, 1.0);
			if (glm::length(point - (from + share * along)) <= 2.0) {
				return true;
			}
			if ((from.y > point.y) != (to.y > point.y) && point.x < from.x + (point.y - from.y) / (to.y - from.y) * (to.x - from.x)) {
				inside = !inside;
			}
		}
		return inside;
	}

	// A build tags the shadow edge at `first` from `firstEye`; the next, after he walks to `secondEye`,
	// tags it at `second` across a notch. Every apex drawn over the walk after lies on the body or in it,
	// never across the notch's open ground.
	void expectNoEaseAcrossTheNotch(
		const std::vector<Vec2i64>& corners,
		std::uint32_t				first,
		std::uint32_t				second,
		glm::dvec2					firstEye,
		glm::dvec2					secondEye
	) {
		const std::vector<vision::SightBody> bodies = {bodyOf(corners, vision::BodyKind::Opaque, 5)};
		const vision::SightTunables			 tunables;
		// The end of the silhouette that is `first` from the first eye is `second` from the second.
		const std::optional<vision::Silhouette> before = vision::silhouetteFrom(bodies[0], firstEye);
		const std::optional<vision::Silhouette> after = vision::silhouetteFrom(bodies[0], secondEye);
		ASSERT_TRUE(before.has_value() && after.has_value());
		const bool clockwise = before->clockwise == first;
		ASSERT_EQ(clockwise ? before->clockwise : before->counterClockwise, first);
		ASSERT_EQ(clockwise ? after->clockwise : after->counterClockwise, second);
		const int litTurn = clockwise ? -1 : 1;

		std::vector<vision::ShadowEdge>		 edges = {{.body = 0, .corner = first, .litTurn = litTurn}};
		const auto							 sight = [&](glm::dvec2 eye) {
			  return ecs::VisionSystem::SightBodies{
				  .bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = {std::llround(eye.x), std::llround(eye.y)}
			  };
		};
		Drawn drawn;
		drawn.step(sight(firstEye), 1, firstEye, 0.0, tunables);
		edges = {{.body = 0, .corner = second, .litTurn = litTurn}};
		for (int frame = 1; frame <= 60; ++frame) {
			const glm::dvec2 eye = secondEye;
			drawn.step(sight(eye), 2, eye, 33.0, tunables);
			ASSERT_EQ(drawn.shadows.edges().size(), 1U);
			const glm::dvec2 apex = drawn.shadows.edges()[0].band.apex;
			EXPECT_TRUE(inOrOnRing(corners, apex)) << "frame " << frame << ", apex " << apex.x << "," << apex.y;
		}
	}

} // namespace

// An L whose notch is its +x +y quarter: a build tags its tip (6, 2) m, the next, past the line
// through the two tips, tags the other tip (2, 6) m, two faces on round the notch's inside corner. The
// ease is not taken up across the notch, which would carry the apex over its open ground; the band
// starts at the new tip.
TEST(CoverPieces, AnEaseNeverCrossesAnLsNotch) {
	const std::vector<Vec2i64> corners = {{0, 0}, {6000, 0}, {6000, 2000}, {2000, 2000}, {2000, 6000}, {0, 6000}};
	expectNoEaseAcrossTheNotch(corners, 2, 4, {10000.0, -2500.0}, {10000.0, -1500.0});
}

// Close round a thin wall's end (4 m long, 0.15 m thick), a build tags the silhouette's end at the far
// corner of the face he has been looking along, (0, -0.075) m, and the next, after he has crossed the
// lines of both faces at the near end, tags the end's other corner (4, 0.075) m: the switch skips the
// end cap. The cap's corner is convex, so the running ease is taken up, and the apex never moves more in
// a frame than the ease allows across the wall's diagonal; it never snaps the wall's length.
TEST(CoverPieces, AnEaseRoundAThinWallsEndNeverSnaps) {
	const std::vector<vision::SightBody> bodies = {rectangleBody(2000.0, 0.0, 4000.0, 150.0, vision::BodyKind::Opaque)};
	const vision::SightTunables			 tunables;
	const glm::dvec2					 firstEye{4400.0, -100.0};
	const glm::dvec2					 secondEye{3900.0, 400.0};
	ASSERT_EQ(vision::silhouetteFrom(bodies[0], firstEye)->counterClockwise, 0U);
	ASSERT_EQ(vision::silhouetteFrom(bodies[0], secondEye)->counterClockwise, 2U);
	std::vector<vision::ShadowEdge> edges = {{.body = 0, .corner = 0, .litTurn = 1}};
	const auto						sight = [&](glm::dvec2 eye) {
		 return ecs::VisionSystem::SightBodies{
			 .bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = {std::llround(eye.x), std::llround(eye.y)}
		 };
	};
	constexpr double kStepMm = 33.0;
	const double	 maxStep = std::hypot(4000.0, 150.0) * kStepMm / static_cast<double>(tunables.shadowCornerEaseMm) + 1e-6;
	Drawn			 drawn;
	drawn.step(sight(firstEye), 1, firstEye, 0.0, tunables);
	glm::dvec2 last = drawn.shadows.edges()[0].band.apex;
	ASSERT_EQ(last, glm::dvec2(0.0, -75.0));
	edges = {{.body = 0, .corner = 2, .litTurn = 1}};
	for (int frame = 1; frame <= 60; ++frame) {
		drawn.step(sight(secondEye), 2, secondEye, kStepMm, tunables);
		ASSERT_EQ(drawn.shadows.edges().size(), 1U);
		const glm::dvec2 apex = drawn.shadows.edges()[0].band.apex;
		EXPECT_LE(glm::length(apex - last), maxStep) << "frame " << frame;
		last = apex;
	}
	EXPECT_EQ(last, glm::dvec2(4000.0, 75.0)) << "the ease reached the new corner";
}

// A U whose notch opens on its +y side: walking across the line of its two outer tips, the silhouette's
// end switches from (0, 6) m to (6, 6) m, five faces apart round the notch, and the ease is not taken
// across its mouth.
TEST(CoverPieces, AnEaseNeverCrossesAUsNotch) {
	const std::vector<Vec2i64> corners = {{0, 0}, {6000, 0}, {6000, 6000}, {4000, 6000}, {4000, 2000}, {2000, 2000}, {2000, 6000}, {0, 6000}};
	expectNoEaseAcrossTheNotch(corners, 7, 2, {12000.0, 6300.0}, {12000.0, 5700.0});
}
