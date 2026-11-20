#include "SightBody.h"

#include <predicates/Predicates.h>
#include <visibility/SightClip.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <map>

namespace vision {

	namespace {
		double crossOf(glm::dvec2 first, glm::dvec2 second) { return first.x * second.y - first.y * second.x; }

		glm::dvec2 toDouble(const geometry::Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		// The corner at one end of the silhouette: `turn` -1 for the clockwise end, every other corner
		// counter-clockwise of it, +1 for the other. None when some corner lies past it, so the arc
		// spans a half turn or more.
		std::optional<std::uint32_t> silhouetteEnd(const SightBody& body, glm::dvec2 eyeMm, int turn) {
			if (body.edges.empty()) {
				return std::nullopt;
			}
			const auto offsetOf = [&](std::uint32_t corner) { return toDouble(body.edges[corner].a) - eyeMm; };
			// Positive when `second` lies the far way round from `first`, past the end being looked for.
			const auto beyond = [&](glm::dvec2 first, glm::dvec2 second) { return crossOf(first, second) * static_cast<double>(turn); };
			std::uint32_t end = 0;
			for (std::uint32_t corner = 1; corner < body.edges.size(); ++corner) {
				const double turned = beyond(offsetOf(end), offsetOf(corner));
				if (turned > 0.0 || (turned == 0.0 && glm::dot(offsetOf(corner), offsetOf(end)) > 0.0 &&
									 glm::dot(offsetOf(corner), offsetOf(corner)) > glm::dot(offsetOf(end), offsetOf(end)))) {
					end = corner;
				}
			}
			for (std::uint32_t corner = 0; corner < body.edges.size(); ++corner) {
				if (beyond(offsetOf(end), offsetOf(corner)) > 0.0 ||
					(corner != end && crossOf(offsetOf(end), offsetOf(corner)) == 0.0 && glm::dot(offsetOf(end), offsetOf(corner)) < 0.0)) {
					return std::nullopt;
				}
			}
			return end;
		}

		double distanceToSegment(glm::dvec2 point, glm::dvec2 from, glm::dvec2 to) {
			const glm::dvec2 along	= to - from;
			const double	 length = glm::dot(along, along);
			const double	 t		= length > 0.0 ? std::clamp(glm::dot(point - from, along) / length, 0.0, 1.0) : 0.0;
			return glm::length(point - (from + t * along));
		}

		// Whether a face from `from` to `to` goes on straight to `beyond`: onward, and `beyond` within
		// kFaceStraightMm of its line.
		bool goesOnTo(const geometry::Vec2i64& from, const geometry::Vec2i64& to, const geometry::Vec2i64& beyond) {
			const glm::dvec2 along	= toDouble(to) - toDouble(from);
			const glm::dvec2 onward = toDouble(beyond) - toDouble(to);
			const double	 length = glm::length(along);
			return length > 0.0 && glm::dot(along, onward) > 0.0 && std::abs(crossOf(along, onward)) <= kFaceStraightMm * length;
		}
	} // namespace

	std::uint32_t nextEdgeOf(const SightBody& body, std::uint32_t edge) {
		const auto	  count = static_cast<std::uint32_t>(body.edges.size());
		std::uint32_t guess = (edge + 1) % count;
		if (body.previousEdge[guess] == edge) {
			return guess;
		}
		for (std::uint32_t other = 0; other < count; ++other) {
			if (body.previousEdge[other] == edge) {
				return other;
			}
		}
		return edge;
	}

	Face faceOf(const SightBody& body, std::uint32_t edge) {
		Face		  face{body.edges[edge].a, body.edges[edge].b};
		std::uint32_t last	= edge;
		std::uint32_t first = edge;
		for (std::size_t step = 1; step < body.edges.size(); ++step) {
			const std::uint32_t next = nextEdgeOf(body, last);
			if (next == first || !goesOnTo(face.from, face.to, body.edges[next].b)) {
				break;
			}
			face.to = body.edges[next].b;
			last	= next;
		}
		for (std::size_t step = 1; step < body.edges.size(); ++step) {
			const std::uint32_t previous = body.previousEdge[first];
			if (previous == last || !goesOnTo(face.to, face.from, body.edges[previous].a)) {
				break;
			}
			face.from = body.edges[previous].a;
			first	  = previous;
		}
		return face;
	}

	void appendFaces(const SightBody& body, std::vector<Face>& out) {
		for (std::uint32_t edge = 0; edge < body.edges.size(); ++edge) {
			const Face face = faceOf(body, edge);
			if (face.from == body.edges[edge].a) {
				out.push_back(face);
			}
		}
	}

	bool liesOnAFace(std::span<const Face> faces, const geometry::Vec2i64& from, const geometry::Vec2i64& to) {
		return std::ranges::any_of(faces, [&](const Face& face) {
			return geometry::withinDistanceOfSegment(from, face.from, face.to, kFaceDriftMm) &&
				   geometry::withinDistanceOfSegment(to, face.from, face.to, kFaceDriftMm);
		});
	}

	geometry::Int128 diameterSquared(std::span<const geometry::Vec2i64> vertices) {
		geometry::Int128 greatest(0);
		for (std::size_t first = 0; first < vertices.size(); ++first) {
			for (std::size_t second = first + 1; second < vertices.size(); ++second) {
				const geometry::Vec2i64 span = vertices[second] - vertices[first];
				greatest					 = std::max(greatest, geometry::dot(span, span));
			}
		}
		return greatest;
	}

	BodyKind kindOf(const geometry::Int128& diameterSquared, std::int64_t leastOpaqueMm) {
		return diameterSquared >= geometry::Int128::product(leastOpaqueMm, leastOpaqueMm) ? BodyKind::Opaque : BodyKind::Soft;
	}

	std::int64_t wholeSquareRoot(const geometry::Int128& value) {
		assert(value.sign() >= 0);
		std::int64_t least = 0;
		std::int64_t most  = std::int64_t{1} << 32;
		while (least < most) {
			const std::int64_t middle = least + (most - least + 1) / 2;
			if (geometry::Int128::product(middle, middle) <= value) {
				least = middle;
			} else {
				most = middle - 1;
			}
		}
		return least;
	}

	std::vector<ShadowEdge> tagShadowEdges(const geometry::Ring& polygon, const geometry::Vec2i64& eye, std::span<const SightBody> bodies) {
		std::multimap<geometry::Vec2i64, std::pair<std::uint32_t, std::uint32_t>> corners;
		for (std::uint32_t body = 0; body < bodies.size(); ++body) {
			if (bodies[body].kind != BodyKind::Opaque) {
				continue;
			}
			for (std::uint32_t corner = 0; corner < bodies[body].edges.size(); ++corner) {
				corners.emplace(bodies[body].edges[corner].a, std::pair{body, corner});
			}
		}
		std::vector<ShadowEdge> tagged;
		if (corners.empty() || polygon.size() < 3) {
			return tagged;
		}
		// The tag for a side from corner `vertex` to `far`, whose lit side lies `litTurn` of it.
		const auto tag = [&](const geometry::Vec2i64& vertex, const geometry::Vec2i64& far, int litTurn) -> std::optional<ShadowEdge> {
			const auto [first, last] = corners.equal_range(vertex);
			if (first == last) {
				return std::nullopt;
			}
			const geometry::Vec2i64 toCorner = vertex - eye;
			const geometry::Vec2i64 toFar	 = far - eye;
			const geometry::Int128	nearSquared = geometry::dot(toCorner, toCorner);
			if (nearSquared.sign() == 0 || geometry::dot(toFar, toFar) <= nearSquared || geometry::dot(toCorner, toFar).sign() <= 0) {
				return std::nullopt;
			}
			// `far` within 1 mm of the ray: cross^2 <= |toCorner|^2 x 1^2.
			if (geometry::Int128::compareSquareToProduct(geometry::cross(toCorner, toFar), nearSquared, geometry::Int128(1)) > 0) {
				return std::nullopt;
			}
			for (auto candidate = first; candidate != last; ++candidate) {
				const auto [body, corner]  = candidate->second;
				const SightBody&	 owner = bodies[body];
				const geometry::Vec2i64 next	 = owner.edges[corner].b;
				const geometry::Vec2i64 previous = owner.edges[owner.previousEdge[corner]].a;
				const int				nextSide	 = geometry::cross(toCorner, next - eye).sign();
				const int				previousSide = geometry::cross(toCorner, previous - eye).sign();
				if (nextSide == -litTurn && previousSide == -litTurn) {
					return ShadowEdge{.body = body, .corner = corner, .litTurn = litTurn};
				}
			}
			return std::nullopt;
		};
		for (std::size_t index = 0; index < polygon.size(); ++index) {
			const geometry::Vec2i64& from = polygon[index];
			const geometry::Vec2i64& to	  = polygon[(index + 1) % polygon.size()];
			if (const std::optional<ShadowEdge> outward = tag(from, to, 1)) {
				tagged.push_back(*outward);
			} else if (const std::optional<ShadowEdge> inward = tag(to, from, -1)) {
				tagged.push_back(*inward);
			}
		}
		return tagged;
	}

	std::vector<ShadowEdge>
	shadowEdgesKeptByClip(std::span<const ShadowEdge> tagged, const geometry::Ring& clipped, std::span<const SightBody> bodies) {
		std::vector<geometry::Vec2i64> vertices(clipped.begin(), clipped.end());
		std::ranges::sort(vertices);
		std::vector<ShadowEdge> kept;
		for (const ShadowEdge& edge : tagged) {
			if (std::ranges::binary_search(vertices, bodies[edge.body].edges[edge.corner].a)) {
				kept.push_back(edge);
			}
		}
		return kept;
	}

	ForestCut cutByForest(
		const geometry::Ring&	   hard,
		const geometry::Vec2i64&   eye,
		const geometry::Ring&	   forest,
		std::vector<ShadowEdge>	   tagged,
		std::span<const SightBody> bodies
	) {
		geometry::Ring clipped = geometry::starClippedToConvex(hard, eye, forest);
		if (clipped.empty()) {
			return ForestCut{.polygon = hard, .shadowEdges = std::move(tagged), .cut = false};
		}
		std::vector<ShadowEdge> kept = shadowEdgesKeptByClip(tagged, clipped, bodies);
		return ForestCut{.polygon = std::move(clipped), .shadowEdges = std::move(kept), .cut = true};
	}

	std::optional<Silhouette> silhouetteFrom(const SightBody& body, glm::dvec2 eyeMm) {
		const std::optional<std::uint32_t> clockwise		= silhouetteEnd(body, eyeMm, -1);
		const std::optional<std::uint32_t> counterClockwise = silhouetteEnd(body, eyeMm, 1);
		if (!clockwise.has_value() || !counterClockwise.has_value()) {
			return std::nullopt;
		}
		return Silhouette{.clockwise = *clockwise, .counterClockwise = *counterClockwise};
	}

	std::optional<double> widthAsSeen(const SightBody& body, glm::dvec2 eyeMm) {
		const std::optional<Silhouette> silhouette = silhouetteFrom(body, eyeMm);
		if (!silhouette.has_value()) {
			return std::nullopt;
		}
		const glm::dvec2 bisector = glm::normalize(glm::normalize(toDouble(body.edges[silhouette->clockwise].a) - eyeMm) +
												   glm::normalize(toDouble(body.edges[silhouette->counterClockwise].a) - eyeMm));
		const glm::dvec2 across{-bisector.y, bisector.x};
		double			 least = std::numeric_limits<double>::max();
		double			 most  = std::numeric_limits<double>::lowest();
		for (const geometry::OccluderSegment& edge : body.edges) {
			const double along = glm::dot(toDouble(edge.a) - eyeMm, across);
			least			   = std::min(least, along);
			most			   = std::max(most, along);
		}
		return most - least;
	}

	double distanceTo(const SightBody& body, glm::dvec2 eyeMm) {
		double nearest = std::numeric_limits<double>::max();
		for (const geometry::OccluderSegment& edge : body.edges) {
			nearest = std::min(nearest, distanceToSegment(eyeMm, toDouble(edge.a), toDouble(edge.b)));
		}
		return nearest;
	}

	double cornerDistance(const SightBody& body, std::uint32_t corner, glm::dvec2 eyeMm) {
		return glm::length(toDouble(body.edges[corner].a) - eyeMm);
	}

} // namespace vision
