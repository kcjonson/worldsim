#include "assets/UseAreaRoom.h"

#include <glm/common.hpp>

#include <cmath>
#include <limits>
#include <numbers>

namespace assets {

	namespace {

		constexpr double kMicrometresPerMeter = 1.0e6;
		constexpr std::int64_t kMicrometresPerMillimetre = 1000;

		/// The disc that sweeps an edge: a polygon round the colonist's circle (so the erosion never leaves less room than
		/// a true disc would), with flat sides facing the axes, so an axis-aligned strip exactly two radii wide erodes to
		/// nothing and one a millimetre wider keeps a millimetre. 32 sides reach under 0.5% of the radius past the circle.
		constexpr int kDiscSides = 32;

		/// How far a stand point on a region's edge is taken inside it: a route ending there keeps the 2 cm nav keeps
		/// between a standing disc and what it stands beside, rather than ending exactly touching it.
		constexpr double kStandInsetMeters = 0.02;

		geometry::Vec2i64 micrometres(glm::vec2 meters) {
			return {std::llround(static_cast<double>(meters.x) * kMicrometresPerMeter), std::llround(static_cast<double>(meters.y) * kMicrometresPerMeter)};
		}

		glm::dvec2 metersOf(const geometry::Vec2i64& point) {
			return {static_cast<double>(point.x) / kMicrometresPerMeter, static_cast<double>(point.y) / kMicrometresPerMeter};
		}

		geometry::Ring micrometreRing(const std::vector<glm::vec2>& ring) {
			geometry::Ring out;
			out.reserve(ring.size());
			for (const glm::vec2 point : ring) {
				out.push_back(micrometres(point));
			}
			return out;
		}

		struct Box {
			geometry::Vec2i64 low{std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max()};
			geometry::Vec2i64 high{std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()};

			void add(const geometry::Vec2i64& point) {
				low = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
			[[nodiscard]] bool meetsSegment(const geometry::Vec2i64& from, const geometry::Vec2i64& to, std::int64_t reach) const {
				return std::max(from.x, to.x) >= low.x - reach && std::min(from.x, to.x) <= high.x + reach && std::max(from.y, to.y) >= low.y - reach &&
					   std::min(from.y, to.y) <= high.y + reach;
			}
		};

		/// The edge from `from` to `to` swept by the disc: the hull of the disc at both ends, counter-clockwise.
		geometry::Ring capsule(const geometry::Vec2i64& from, const geometry::Vec2i64& to, const std::vector<geometry::Vec2i64>& disc) {
			std::vector<glm::vec2> points;
			points.reserve(disc.size() * 2);
			// Hulled in metres relative to the edge's start, where a float holds a micrometre.
			for (const geometry::Vec2i64& end : {from, to}) {
				for (const geometry::Vec2i64& offset : disc) {
					const geometry::Vec2i64 point = end + offset - from;
					points.push_back({static_cast<float>(static_cast<double>(point.x) / kMicrometresPerMeter),
									  static_cast<float>(static_cast<double>(point.y) / kMicrometresPerMeter)});
				}
			}
			geometry::Ring ring;
			for (const glm::vec2 point : convexHull(std::move(points))) {
				ring.push_back(from + micrometres(point));
			}
			return ring;
		}

		bool insideRing(const glm::dvec2& point, const geometry::Ring& ring) {
			bool inside = false;
			for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
				const glm::dvec2 from = metersOf(ring[i]);
				const glm::dvec2 to = metersOf(ring[j]);
				if ((from.y > point.y) != (to.y > point.y) && point.x < (to.x - from.x) * (point.y - from.y) / (to.y - from.y) + from.x) {
					inside = !inside;
				}
			}
			return inside;
		}

		bool holds(const geometry::RegionPolygon& piece, const glm::dvec2& point) {
			if (!insideRing(point, piece.outer)) {
				return false;
			}
			for (const geometry::Ring& hole : piece.holes) {
				if (insideRing(point, hole)) {
					return false;
				}
			}
			return true;
		}

		/// Calls `visit(from, to)` for every edge of the piece, outer and holes, each wound with the piece on its left.
		template <typename Visit> void eachEdge(const geometry::RegionPolygon& piece, const Visit& visit) {
			const auto ringEdges = [&](const geometry::Ring& ring) {
				for (size_t i = 0; i < ring.size(); ++i) {
					visit(metersOf(ring[i]), metersOf(ring[(i + 1) % ring.size()]));
				}
			};
			ringEdges(piece.outer);
			for (const geometry::Ring& hole : piece.holes) {
				ringEdges(hole);
			}
		}

		/// The boundary point nearest `from` taken kStandInsetMeters in along its edge's inward normal, or left on the
		/// edge where the piece is too narrow for that.
		glm::dvec2 nearestInside(const geometry::RegionPolygon& piece, const glm::dvec2& from) {
			const auto closestOn = [](const glm::dvec2& point, const glm::dvec2& start, const glm::dvec2& along, double lengthSquared) {
				return start + along * std::clamp(((point.x - start.x) * along.x + (point.y - start.y) * along.y) / lengthSquared, 0.0, 1.0);
			};
			double	   best = std::numeric_limits<double>::max();
			glm::dvec2 nearest = metersOf(piece.outer.front());
			eachEdge(piece, [&](const glm::dvec2& start, const glm::dvec2& end) {
				const glm::dvec2 along = end - start;
				const double	 lengthSquared = along.x * along.x + along.y * along.y;
				if (lengthSquared <= 0.0) {
					return;
				}
				const glm::dvec2 point = closestOn(from, start, along, lengthSquared);
				const double	 distance = (point.x - from.x) * (point.x - from.x) + (point.y - from.y) * (point.y - from.y);
				if (distance < best) {
					best = distance;
					nearest = point;
				}
			});
			// In along the inward normals of every edge through the point: one edge's, or at a corner the two's together.
			glm::dvec2 inward{0.0, 0.0};
			eachEdge(piece, [&](const glm::dvec2& start, const glm::dvec2& end) {
				const glm::dvec2 along = end - start;
				const double	 lengthSquared = along.x * along.x + along.y * along.y;
				if (lengthSquared > 0.0) {
					const glm::dvec2 point = closestOn(nearest, start, along, lengthSquared);
					if ((point.x - nearest.x) * (point.x - nearest.x) + (point.y - nearest.y) * (point.y - nearest.y) < 1.0e-14) {
						inward += glm::dvec2{-along.y, along.x} / std::sqrt(lengthSquared);
					}
				}
			});
			const double length = std::sqrt(inward.x * inward.x + inward.y * inward.y);
			if (length <= 0.0) {
				return nearest;
			}
			const glm::dvec2 inset = nearest + inward * (kStandInsetMeters / length);
			return holds(piece, inset) ? inset : nearest;
		}

	} // namespace

	StandingRegion standingRegion(
		const std::vector<AreaPolygon>& area, const std::vector<std::vector<glm::vec2>>& obstacles, const OpenGround* ground, float radiusMeters
	) {
		std::vector<geometry::Ring> areaRings;
		Box							box;
		for (const AreaPolygon& polygon : area) {
			areaRings.push_back(micrometreRing(polygon.outer));
			for (const std::vector<glm::vec2>& hole : polygon.holes) {
				areaRings.push_back(micrometreRing(hole));
			}
			for (const geometry::Vec2i64& point : areaRings[areaRings.size() - 1 - polygon.holes.size()]) {
				box.add(point);
			}
		}
		if (areaRings.empty()) {
			return {};
		}

		const double				   circumradius = static_cast<double>(radiusMeters) / std::cos(std::numbers::pi / kDiscSides);
		std::vector<geometry::Vec2i64> disc;
		disc.reserve(kDiscSides);
		for (int i = 0; i < kDiscSides; ++i) {
			const double angle = (static_cast<double>(i) + 0.5) * 2.0 * std::numbers::pi / kDiscSides;
			disc.push_back({std::llround(circumradius * std::cos(angle) * kMicrometresPerMeter), std::llround(circumradius * std::sin(angle) * kMicrometresPerMeter)});
		}
		const std::int64_t reach = std::llround(circumradius * kMicrometresPerMeter) + 1;

		// Each swept edge is a group of its own, so the boolean tests a point only against the few whose box holds it.
		std::vector<std::vector<geometry::Ring>> cut;
		const auto								 sweep = [&](const geometry::Vec2i64& from, const geometry::Vec2i64& to) {
			  if (from != to && box.meetsSegment(from, to, reach)) {
				  cut.push_back({capsule(from, to, disc)});
			  }
		};
		for (const geometry::Ring& ring : areaRings) {
			for (size_t i = 0; i < ring.size(); ++i) {
				sweep(ring[i], ring[(i + 1) % ring.size()]);
			}
		}
		for (const std::vector<glm::vec2>& obstacle : obstacles) {
			const geometry::Ring ring = micrometreRing(obstacle);
			bool				 near = false;
			for (size_t i = 0; i < ring.size(); ++i) {
				near = near || box.meetsSegment(ring[i], ring[(i + 1) % ring.size()], reach);
				sweep(ring[i], ring[(i + 1) % ring.size()]);
			}
			if (near) {
				cut.push_back({ring});
			}
		}

		std::vector<std::vector<geometry::Ring>> within{areaRings};
		if (ground != nullptr) {
			std::vector<geometry::Ring>& faces = within.emplace_back();
			for (const geometry::Ring& face : ground->faces) {
				geometry::Ring& scaled = faces.emplace_back();
				scaled.reserve(face.size());
				for (const geometry::Vec2i64& point : face) {
					scaled.push_back(point * kMicrometresPerMillimetre);
				}
			}
			for (const auto& [from, to] : ground->edges) {
				sweep(from * kMicrometresPerMillimetre, to * kMicrometresPerMillimetre);
			}
		}
		return {.pieces = geometry::regionOfFills(within, cut)};
	}

	glm::vec2 nearestPointIn(const geometry::RegionPolygon& piece, glm::vec2 from) {
		const glm::dvec2 point(from);
		return glm::vec2(holds(piece, point) ? point : nearestInside(piece, point));
	}

	glm::vec2 pointInside(const geometry::RegionPolygon& piece) {
		const glm::dvec2 first = metersOf(piece.outer[0]);
		const glm::dvec2 second = metersOf(piece.outer[1 % piece.outer.size()]);
		return glm::vec2(nearestInside(piece, (first + second) * 0.5));
	}

	bool pieceHolds(const geometry::RegionPolygon& piece, glm::vec2 meters) {
		return holds(piece, glm::dvec2(meters));
	}

} // namespace assets
