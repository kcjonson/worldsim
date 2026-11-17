#pragma once

// Sight bodies (sight-softening.md 2). A body is one placed obstacle's collision ring or one connected
// wall graph's outline, and its kind is fixed by its own size, never by where it is seen from: an
// opaque body casts its exact shadow, a soft one casts none in the sim. Here are the kind, the exact
// diameter it is decided by, the measures taken from an eye (2.3), and the shadow edges a build's
// polygon is tagged with (3.1).

#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <visibility/ForestDepth.h>
#include <visibility/Visibility.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vision {

	enum class BodyKind : std::uint8_t { Soft, Opaque };

	// The greatest squared distance between two of `vertices`, exact.
	[[nodiscard]] geometry::Int128 diameterSquared(std::span<const geometry::Vec2i64> vertices);

	// Opaque when the diameter is at least `leastOpaqueMm`, compared squared.
	[[nodiscard]] BodyKind kindOf(const geometry::Int128& diameterSquared, std::int64_t leastOpaqueMm);

	// The largest whole number whose square is at most `value` (non-negative, under 2^64).
	[[nodiscard]] std::int64_t wholeSquareRoot(const geometry::Int128& value);

	// One body as vision hands it out (VisionSystem::sightBodies): its outline as edges with the solid
	// on the left of each, a placed ring's counter-clockwise, and a corner for each edge, its start.
	struct SightBody {
		enum class Source : std::uint8_t { Wall, Placed };

		Source	 source = Source::Placed;
		BodyKind kind	= BodyKind::Soft;
		// The wall body's index in GeometryIndex::bodies(), or the placed ring's serial.
		std::uint64_t						   id = 0;
		std::vector<geometry::OccluderSegment> edges;
		// Per edge, the edge that ends at its corner.
		std::vector<std::uint32_t> previousEdge;
		std::int64_t			   diameterMm = 0;
		// The mean of its corners.
		geometry::Vec2i64 centre{0, 0};
		// A placed body's def name; empty for a wall.
		std::string defName;
		// How far a placed ring has come in since it arrived (2.4), of geometry::kFractionOne.
		std::int64_t arrival = geometry::kFractionOne;
	};

	// A side of a build's polygon that runs out along a ray from the build eye past an opaque body's
	// corner (3.1): the body's index in the list it was tagged against, the corner's index in that
	// body's edges, and +1 when the lit side lies counter-clockwise of the shadow side about the
	// corner, -1 when clockwise.
	struct ShadowEdge {
		std::uint32_t body	  = 0;
		std::uint32_t corner  = 0;
		int			  litTurn = 1;

		bool operator==(const ShadowEdge&) const = default;
	};

	// The shadow edges of `polygon`, the counter-clockwise visibility polygon computeVisibilityPolygon
	// built round `eye`, against the opaque ones among `bodies` (soft bodies are passed over). A side
	// with a corner `v` of a body at one end is a shadow edge when `v` is a silhouette vertex from the
	// eye (both its neighbours strictly on the shadow side of the ray through it), its other end lies
	// farther from the eye, and that end lies within 1 mm of the ray. A corner two bodies share takes
	// the body whose outline lies on the shadow side there. Integers throughout.
	[[nodiscard]] std::vector<ShadowEdge>
	tagShadowEdges(const geometry::Ring& polygon, const geometry::Vec2i64& eye, std::span<const SightBody> bodies);

	// The two corners at the ends of the body's silhouette from `eyeMm` (2.3): `clockwise` the corner at
	// its clockwise end, every other corner counter-clockwise of it or on its ray, and `counterClockwise`
	// the other way; on a ray holding several corners, the farthest. None when the arc is not less than a
	// half turn, as from inside the body's hull.
	struct Silhouette {
		std::uint32_t clockwise		   = 0;
		std::uint32_t counterClockwise = 0;
	};
	[[nodiscard]] std::optional<Silhouette> silhouetteFrom(const SightBody& body, glm::dvec2 eyeMm);

	// The body's extent across the silhouette's bisector, in mm: the greatest less the least of
	// dot(corner - eye, across). None where the silhouette is.
	[[nodiscard]] std::optional<double> widthAsSeen(const SightBody& body, glm::dvec2 eyeMm);

	// From the eye to the body's nearest point, in mm.
	[[nodiscard]] double distanceTo(const SightBody& body, glm::dvec2 eyeMm);

	// From the eye to the body's corner `corner`, in mm.
	[[nodiscard]] double cornerDistance(const SightBody& body, std::uint32_t corner, glm::dvec2 eyeMm);

} // namespace vision
