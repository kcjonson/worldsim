#pragma once

// The cover's partial pieces (sight-softening.md 3 and 7): the feather on an area's boundary, a soft
// body's triangle and its fins, and an opaque shadow edge's band and fin. Each piece is a triangle in
// world millimetres with a value at each corner, linear over it, which the cover uploads as a mesh
// whose vertices carry their values (salal docs/masks.md, a mask's mesh element). Float
// arithmetic, drawn only: nothing here is read by the sim.

#include <vision/SightBody.h>
#include <vision/SightTunables.h>

#include <core/Vec2i64.h>

#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace worldview {

	// How far a soft body's triangle may stray from its curved sides (sight-softening.md 9).
	constexpr double kDrawnShadowSliceToleranceMm = 5.0;
	// How far a drawn piece's linear value may stray from the value it stands for, of the whole 0 to 1.
	constexpr double kDrawnValueTolerancePercent = 2.0;
	// The most a soft triangle carries where the bright rim cuts it: limit 3's 0.05 on screen under the
	// tint's 0.5 (sight-softening.md 10).
	constexpr double kSoftShadowRimMostValue = 0.1;
	// The widest triangle of a reflex corner's fan.
	constexpr double kFeatherFanStepDegrees = 15.0;

	// A triangle whose value is linear over it: values[k] at points[k], in world millimetres.
	struct ValueTriangle {
		std::array<glm::dvec2, 3> points{};
		std::array<float, 3>	  values{};
	};

	using DirectedEdge = std::pair<geometry::Vec2i64, geometry::Vec2i64>;

	// The pieces as meshes in world meters less `anchorMm`, each vertex's value its colour's alpha, split
	// so no mesh passes 65,535 vertices. A mesh drawn under a camera moved by the anchor lies where its
	// pieces do, so pieces that keep their shape about a moving point need no rebuild.
	std::vector<renderer::TessellatedMesh> valueMeshes(std::span<const ValueTriangle> pieces, glm::dvec2 anchorMm = {0.0, 0.0});

	// The feather of an area on one side of its boundary (7.2): for each edge, its area on the left, the
	// rectangle `featherMm` into the area, 1 on the edge and 0 on its far side; and where an edge ending
	// at a point meets one starting there with a right turn (the area's inside angle past 180 degrees), a
	// fan of that radius over the gap between their rectangles, 1 at the corner and 0 on the arc, in
	// triangles of at most kFeatherFanStepDegrees. Every piece's value at a point is at most the fade of
	// the point's distance to the edges, so an edge or a fan more than the boundary needs never draws
	// darker than it.
	void appendFeather(std::span<const DirectedEdge> edges, double featherMm, std::vector<ValueTriangle>& out);

	// The fan at `corner` between an edge arriving along `incoming` and one leaving along `outgoing`
	// (directions, any length), when the turn between them is to the right; nothing otherwise.
	void appendFeatherFan(glm::dvec2 corner, glm::dvec2 incoming, glm::dvec2 outgoing, double featherMm, std::vector<ValueTriangle>& out);

	// A soft body's measures from an eye (3.2).
	struct SoftShadow {
		double widthAsSeen = 0.0;
		double distance = 0.0;
		double footValue = 0.0;
		double shadowLength = 0.0;
	};

	// The soft body's triangle and its two fins as seen from `eyeMm` (3.2): the silhouette shrunk toward
	// its bisector by `progress` from the body's near face out to `shadowLength` behind it, valued
	// footValue x (1 - progress), cut into slices along its length (7.3): a hard side held within
	// kDrawnShadowSliceToleranceMm of its curve, a side with a fin beside it within what moves the value
	// by kDrawnValueTolerancePercent; and beside each side a fin from its silhouette corner's range, whose
	// outer edge lies (shadowEdgeWidthMm / 2) x (range - cornerDistance) / cornerDistance across the ray
	// from the side, falling from the triangle's value to 0. footValue carries the body's arrival
	// fraction, and fades so the triangle carries at most kSoftShadowRimMostValue where `bright`, the
	// colonist's bright outline, cuts it short. None when the eye is inside the body's hull.
	std::optional<SoftShadow> appendSoftShadow(
		const vision::SightBody&		   body,
		glm::dvec2						   eyeMm,
		const vision::SightTunables&	   tunables,
		std::span<const geometry::Vec2i64> bright,
		std::vector<ValueTriangle>&		   out
	);

	// An opaque shadow edge's band and fin as seen from `eyeMm` (3.1).
	struct ShadowBand {
		glm::dvec2 apex{0.0};
		glm::dvec2 ray{1.0, 0.0}; // the unit direction from the eye through the apex
		double	   cornerDistance = 0.0;
		double	   widening = 0.0;
		double	   litAngle = 0.0; // radians, each turned from the ray from the eye through the apex
		double	   shadowAngle = 0.0;
		double	   finAngle = 0.0;
		int		   litTurn = 1;
	};

	// The band's angles for an apex seen from `eyeMm` with the shadow's lit side `litTurn` (+1
	// counter-clockwise of its shadow side) and `wideningMm` the greater of the lead and his distance
	// from the build eye: litAngle = asin(min(widening / cornerDistance, sin(shadowWideningMostDegrees))),
	// shadowAngle = min(180 degrees, asin(min(1, widening / cornerDistance)) + 90 degrees x clamp(2 -
	// cornerDistance / widening, 0, 1)), finAngle = min(shadowFinMostDegrees, atan(shadowEdgeWidthMm /
	// (2 x cornerDistance))).
	ShadowBand shadowBandAt(glm::dvec2 eyeMm, glm::dvec2 apexMm, int litTurn, double wideningMm, const vision::SightTunables& tunables);

	// How many triangles a fin is cut into across: its value steps from 1 to 0 in that many, each exact
	// on its far side, so no point strays from the rule by more than half a step,
	// kDrawnValueTolerancePercent.
	constexpr std::size_t kShadowFinSlices = 25;

	// The band at 1, the wedge at the apex from its lit line round through the eye's ray to its shadow
	// line, and the fin beside its lit line falling across to 0, each out to `farMm` from the apex.
	void appendShadowBand(const ShadowBand& band, double farMm, std::vector<ValueTriangle>& out);

	// The corner of the body's outline that his shadow side runs past from `eyeMm`, followed from the
	// build's corner `buildCorner` along the outline toward whichever neighbour lies on the lit side of
	// the ray through it until both neighbours lie on its shadow side (3.1). Wall outlines have short
	// junction and cut edges, so this can pass several corners.
	std::uint32_t followedCorner(const vision::SightBody& body, std::uint32_t buildCorner, int litTurn, glm::dvec2 eyeMm);

} // namespace worldview
