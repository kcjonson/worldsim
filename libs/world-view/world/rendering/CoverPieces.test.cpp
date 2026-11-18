// The cover's pieces (sight-softening.md 3 and 7.2): the feather, a soft body's triangle and fins, an
// opaque shadow edge's band and fin, the corner followed and its ease. Each expected value is worked out
// here from the spec's formulas, not read back from the code under test.

#include "world/rendering/CoverPieces.h"
#include "world/rendering/DrawnShadows.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using worldview::ValueTriangle;

	constexpr double kDegree = std::numbers::pi / 180.0;

	// The greatest value any piece holds at the point, or nothing when none covers it: each polygon
	// fanned from its first point, its value interpolated over each triangle.
	std::optional<double> valueAt(const std::vector<ValueTriangle>& pieces, glm::dvec2 point) {
		std::optional<double> greatest;
		for (const ValueTriangle& piece : pieces) {
			for (std::size_t corner = 1; corner + 1 < piece.points.size(); ++corner) {
				const glm::dvec2 a = piece.points[0];
				const glm::dvec2 b = piece.points[corner];
				const glm::dvec2 c = piece.points[corner + 1];
				const double	 area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
				if (area == 0.0) {
					continue;
				}
				const double first	= ((b.x - point.x) * (c.y - point.y) - (b.y - point.y) * (c.x - point.x)) / area;
				const double second = ((c.x - point.x) * (a.y - point.y) - (c.y - point.y) * (a.x - point.x)) / area;
				const double third	= 1.0 - first - second;
				constexpr double kEdge = -1e-9;
				if (first >= kEdge && second >= kEdge && third >= kEdge) {
					const double value = first * piece.values[0] + second * piece.values[corner] + third * piece.values[corner + 1];
					greatest		   = std::max(greatest.value_or(value), value);
				}
			}
		}
		return greatest;
	}

	double valueOrZero(const std::vector<ValueTriangle>& pieces, glm::dvec2 point) { return valueAt(pieces, point).value_or(0.0); }

	// A placed ring as vision hands it out: a counter-clockwise rectangle centred at (x, y) millimetres.
	vision::SightBody rectangleBody(double x, double y, double width, double height, vision::BodyKind kind) {
		const std::vector<Vec2i64> corners = {
			{std::llround(x - width / 2.0), std::llround(y - height / 2.0)},
			{std::llround(x + width / 2.0), std::llround(y - height / 2.0)},
			{std::llround(x + width / 2.0), std::llround(y + height / 2.0)},
			{std::llround(x - width / 2.0), std::llround(y + height / 2.0)},
		};
		vision::SightBody body;
		body.kind = kind;
		for (std::size_t k = 0; k < corners.size(); ++k) {
			body.edges.push_back({corners[k], corners[(k + 1) % corners.size()]});
			body.previousEdge.push_back(static_cast<std::uint32_t>((k + corners.size() - 1) % corners.size()));
		}
		body.centre = {std::llround(x), std::llround(y)};
		return body;
	}

	glm::dvec2 along(double angle, double range, glm::dvec2 from = {0.0, 0.0}) { return from + range * glm::dvec2{std::cos(angle), std::sin(angle)}; }

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
// the corner, to within the chords' 1%, in triangles of at most 15 degrees; a left turn takes none.
TEST(CoverPieces, AReflexCornerTakesAFanAndAConvexOneNone) {
	constexpr double						   kFeather = 1500.0;
	const std::vector<worldview::DirectedEdge> reflex	= {{Vec2i64{0, 0}, Vec2i64{10000, 0}}, {Vec2i64{10000, 0}, Vec2i64{10000, -10000}}};
	std::vector<ValueTriangle>				   pieces;
	worldview::appendFeather(reflex, kFeather, pieces);
	EXPECT_EQ(pieces.size(), 4U + 6U) << "two rectangles of two triangles and a 90 degree fan in 15 degree steps";
	for (const double angle : {10.0, 45.0, 80.0}) {
		for (const double distance : {300.0, 750.0, 1400.0}) {
			const double expected = 1.0 - distance / kFeather;
			EXPECT_NEAR(valueOrZero(pieces, along(angle * kDegree, distance, {10000.0, 0.0})), expected, 0.01) << angle << " degrees, " << distance;
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
	const vision::SightTunables tunables;
	const vision::SightBody		trunk = rectangleBody(5000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft);
	std::vector<ValueTriangle>	pieces;
	const std::optional<worldview::SoftShadow> shadow = worldview::appendSoftShadow(trunk, {0.0, 0.0}, tunables, pieces);
	ASSERT_TRUE(shadow.has_value());
	const double foot	= 0.5 * 400.0 / 3000.0;
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
	const double range	   = 4800.0 + 0.9 * length;
	const double halfWidth = range * std::tan(0.1 * std::atan(200.0 / 4800.0));
	const double corner	   = std::hypot(4800.0, 200.0);
	const double fin	   = 600.0 * (range - corner) / corner;
	EXPECT_GT(valueOrZero(pieces, {range, halfWidth + fin / 2.0}), 0.0) << "inside the fin";
	EXPECT_LT(valueOrZero(pieces, {range, halfWidth + fin / 2.0}), foot * 0.1);
	EXPECT_EQ(valueOrZero(pieces, {range, halfWidth + fin + 20.0}), 0.0) << "past the fin";
}

// Scaled by width and capped per object: the start values' table of 3.2, a veteran oak (0.82 m square)
// seen face on and at its diagonal from 5 m, and no soft body darker than the strength allows.
TEST(CoverPieces, ATrunksShadowScalesWithItsWidthAsSeen) {
	const vision::SightTunables tunables;
	std::vector<ValueTriangle>	pieces;
	const vision::SightBody		faceOn = rectangleBody(5410.0, 0.0, 820.0, 820.0, vision::BodyKind::Soft);
	const std::optional<worldview::SoftShadow> square = worldview::appendSoftShadow(faceOn, {0.0, 0.0}, tunables, pieces);
	ASSERT_TRUE(square.has_value());
	EXPECT_NEAR(square->footValue, 0.137, 0.001);
	EXPECT_NEAR(square->shadowLength, 5000.0 * 820.0 / 2180.0, 1.0) << "1.88 m";

	vision::SightBody diagonal = faceOn;
	for (geometry::OccluderSegment& edge : diagonal.edges) {
		for (Vec2i64* point : {&edge.a, &edge.b}) {
			const double x = static_cast<double>(point->x) - 5410.0;
			const double y = static_cast<double>(point->y);
			*point		   = {std::llround(5000.0 + 820.0 / std::sqrt(2.0) + (x - y) / std::sqrt(2.0)), std::llround((x + y) / std::sqrt(2.0))};
		}
	}
	const std::optional<worldview::SoftShadow> turned = worldview::appendSoftShadow(diagonal, {0.0, 0.0}, tunables, pieces);
	ASSERT_TRUE(turned.has_value());
	EXPECT_NEAR(turned->widthAsSeen, 820.0 * std::sqrt(2.0), 2.0);
	EXPECT_NEAR(turned->footValue, 0.193, 0.001);
	EXPECT_NEAR(turned->shadowLength, 3150.0, 10.0) << "3.15 m";

	for (const double width : {150.0, 600.0, 1390.0}) {
		pieces.clear();
		const std::optional<worldview::SoftShadow> soft =
			worldview::appendSoftShadow(rectangleBody(8000.0, 0.0, 100.0, width, vision::BodyKind::Soft), {0.0, 0.0}, tunables, pieces);
		ASSERT_TRUE(soft.has_value());
		EXPECT_LE(soft->footValue, 0.5 * 1400.0 / 3000.0) << "capped below the opaque threshold over the ray";
		EXPECT_NEAR(soft->footValue, 0.5 * width / 3000.0, 0.002) << "scaled by width";
	}
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
	EXPECT_NEAR(worldview::shadowBandAt({0.0, 0.0}, {500.0, 0.0}, 1, 670.0, tunables).shadowAngle, std::numbers::pi, 1e-9) << "within widening";
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

	const std::vector<vision::SightBody>  bodies	  = {block};
	const std::vector<vision::ShadowEdge> shadowEdges = {{.body = 0, .corner = 3, .litTurn = 1}};
	const ecs::VisionSystem::SightBodies  sight{.bodies = &bodies, .shadowEdges = &shadowEdges, .leadMm = 670, .reachMm = 30800, .eyeCentre = {-3000, 800}};
	const vision::SightTunables			  tunables;
	worldview::DrawnShadows				  shadows;
	shadows.update(sight, 1, {-3000.0, 800.0}, 0.0, 30000.0, tunables, true);
	ASSERT_EQ(shadows.edges().size(), 1U);
	EXPECT_EQ(shadows.edges()[0].band.apex, glm::dvec2(0.0, 1000.0));
	shadows.update(sight, 1, {-3000.0, 1200.0}, 400.0, 30000.0, tunables, true);
	EXPECT_EQ(shadows.edges()[0].followedCorner, 2U);
	EXPECT_EQ(shadows.edges()[0].band.apex, glm::dvec2(0.0, 1000.0)) << "the switch moves nothing on its frame";
	shadows.update(sight, 1, {-3000.0, 1200.0}, 750.0, 30000.0, tunables, true);
	EXPECT_NEAR(shadows.edges()[0].band.apex.x, 500.0, 1e-6) << "half the ease, half the face";
	EXPECT_NEAR(shadows.edges()[0].band.apex.y, 1000.0, 1e-6);
	shadows.update(sight, 1, {-3000.0, 1200.0}, 1500.0, 30000.0, tunables, true);
	EXPECT_EQ(shadows.edges()[0].band.apex, glm::dvec2(1000.0, 1000.0));

	// A new build whose edge is tagged at the corner the ease reached picks it up where it is.
	const std::vector<vision::ShadowEdge> rebuilt = {{.body = 0, .corner = 2, .litTurn = 1}};
	const ecs::VisionSystem::SightBodies  next{.bodies = &bodies, .shadowEdges = &rebuilt, .leadMm = 670, .reachMm = 30800, .eyeCentre = {-3000, 1200}};
	shadows.update(next, 2, {-3000.0, 1200.0}, 0.0, 30000.0, tunables, true);
	EXPECT_EQ(shadows.edges()[0].band.apex, glm::dvec2(1000.0, 1000.0));
	EXPECT_EQ(shadows.edges()[0].ease, 1.0);
}

// The drawn shadows reach no farther than the least of drawnShadowReachMm and his bright shape.
TEST(CoverPieces, TheDrawnShadowsReachNoFartherThanTheyCanShow) {
	const std::vector<vision::SightBody> bodies = {
		rectangleBody(5000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft), rectangleBody(15000.0, 0.0, 400.0, 400.0, vision::BodyKind::Soft)
	};
	const std::vector<vision::ShadowEdge> none;
	const ecs::VisionSystem::SightBodies  sight{.bodies = &bodies, .shadowEdges = &none, .leadMm = 670, .reachMm = 30800, .eyeCentre = {0, 0}};
	vision::SightTunables				  tunables;
	worldview::DrawnShadows				  shadows;
	shadows.update(sight, 1, {0.0, 0.0}, 0.0, 30000.0, tunables, true);
	EXPECT_EQ(shadows.softBodies().size(), 2U);
	shadows.update(sight, 1, {0.0, 0.0}, 0.0, 10000.0, tunables, true);
	EXPECT_EQ(shadows.softBodies().size(), 1U) << "past his bright shape";
	tunables.drawnShadowReachMm = 4000;
	shadows.update(sight, 1, {0.0, 0.0}, 0.0, 30000.0, tunables, true);
	EXPECT_EQ(shadows.softBodies().size(), 0U);
	EXPECT_TRUE(shadows.softPieces().empty());
}

// The soft triangles fall due every drawnShadowRebuildMm of his walk and are drawn as built between;
// the bands are rebuilt on every frame he moves and on none he stands still. A rebuild's pieces are a
// fresh build's from the same point.
TEST(CoverPieces, TheSoftTrianglesKeepTheirCadenceAndTheBandsFollowHim) {
	const std::vector<vision::SightBody> bodies = {
		rectangleBody(5000.0, 3000.0, 400.0, 400.0, vision::BodyKind::Soft), rectangleBody(9000.0, -2000.0, 600.0, 600.0, vision::BodyKind::Soft),
		rectangleBody(7000.0, 0.0, 1000.0, 1400.0, vision::BodyKind::Opaque)
	};
	const std::vector<vision::ShadowEdge> edges = {{.body = 2, .corner = 3, .litTurn = 1}};
	const ecs::VisionSystem::SightBodies  sight{.bodies = &bodies, .shadowEdges = &edges, .leadMm = 670, .reachMm = 30800, .eyeCentre = {0, 0}};
	const vision::SightTunables			  tunables;
	ASSERT_EQ(tunables.drawnShadowRebuildMm, 500);
	worldview::DrawnShadows shadows;
	std::size_t				softRebuilds = 0;
	std::size_t				bandRebuilds = 0;
	constexpr double		kFrameWalkMm = 25.0;
	constexpr int			kFrames		 = 160; // 4 m
	for (int frame = 0; frame <= kFrames; ++frame) {
		const glm::dvec2 eye{0.0, -kFrameWalkMm * static_cast<double>(frame)};
		const bool		 due = shadows.softDue(eye, tunables);
		shadows.update(sight, 1, eye, frame == 0 ? 0.0 : kFrameWalkMm, 30000.0, tunables, due);
		softRebuilds += shadows.counts().softRebuilt ? 1 : 0;
		bandRebuilds += shadows.counts().bandsRebuilt ? 1 : 0;
		if (shadows.counts().softRebuilt) {
			worldview::DrawnShadows fresh;
			fresh.update(sight, 1, eye, 0.0, 30000.0, tunables, true);
			ASSERT_EQ(fresh.softPieces().size(), shadows.softPieces().size());
			for (std::size_t piece = 0; piece < fresh.softPieces().size(); ++piece) {
				EXPECT_EQ(fresh.softPieces()[piece].points, shadows.softPieces()[piece].points);
				EXPECT_EQ(fresh.softPieces()[piece].values, shadows.softPieces()[piece].values);
			}
		}
	}
	EXPECT_EQ(softRebuilds, 1U + 4000U / 500U) << "the first frame, then every 500 mm of 4 m";
	EXPECT_EQ(bandRebuilds, static_cast<std::size_t>(kFrames) + 1U) << "every frame he moves";

	const glm::dvec2 still{0.0, -kFrameWalkMm * static_cast<double>(kFrames)};
	for (int frame = 0; frame < 30; ++frame) {
		shadows.update(sight, 1, still, 0.0, 30000.0, tunables, shadows.softDue(still, tunables));
		EXPECT_FALSE(shadows.counts().softRebuilt);
		EXPECT_FALSE(shadows.counts().bandsRebuilt) << "standing still nothing is built";
	}
}
