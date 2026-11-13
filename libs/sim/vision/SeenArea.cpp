#include "SeenArea.h"

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/SightCover.h>
#include <predicates/Predicates.h>
#include <visibility/SightPolygon.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace vision {

	using geometry::Vec2i64;

	namespace {

		// How many millimetres lastSeenToward steps back from a stretch's end for a point that rounds
		// onto seen ground.
		constexpr int kStepsBack = 8;

		bool inLine(const Vec2i64& origin, const Vec2i64& first, const Vec2i64& second) {
			return geometry::cross(first - origin, second - origin).sign() == 0;
		}

		// Drops repeated vertices and every vertex in line with its neighbours, round the ring.
		void dropStraightVertices(std::vector<Vec2i64>& ring) {
			bool changed = true;
			while (changed && ring.size() >= 3) {
				changed = false;
				for (std::size_t i = 0; i < ring.size() && ring.size() >= 3; ++i) {
					const Vec2i64& previous = ring[(i + ring.size() - 1) % ring.size()];
					const Vec2i64& next		= ring[(i + 1) % ring.size()];
					if (ring[i] == previous || inLine(previous, ring[i], next)) {
						ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
						changed = true;
						--i;
					}
				}
			}
		}

		bool convex(const std::vector<Vec2i64>& ring) {
			for (std::size_t i = 0; i < ring.size(); ++i) {
				const Vec2i64& previous = ring[(i + ring.size() - 1) % ring.size()];
				const Vec2i64& next		= ring[(i + 1) % ring.size()];
				if (geometry::cross(ring[i] - previous, next - ring[i]).sign() < 0) {
					return false;
				}
			}
			return true;
		}

		void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
			low	 = ring.front();
			high = ring.front();
			for (const Vec2i64& point : ring) {
				low	 = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
		}

		bool boxesMeet(const Vec2i64& firstLow, const Vec2i64& firstHigh, const Vec2i64& secondLow, const Vec2i64& secondHigh) {
			return firstLow.x <= secondHigh.x && secondLow.x <= firstHigh.x && firstLow.y <= secondHigh.y && secondLow.y <= firstHigh.y;
		}

		bool insideBox(const Vec2i64& point, const Vec2i64& low, const Vec2i64& high) {
			return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y;
		}

		double crossOf(double firstX, double firstY, double secondX, double secondY) { return firstX * secondY - firstY * secondX; }

		// Where the segment from `from` along `direction` meets the edge (edgeStart, edgeEnd), as
		// parameters along the segment in [0, 1]: one for a crossing, the overlap's ends for an edge
		// in line with it.
		void meetings(const Vec2i64& from, const Vec2i64& direction, const Vec2i64& edgeStart, const Vec2i64& edgeEnd, std::vector<double>& out) {
			const Vec2i64 edge	 = edgeEnd - edgeStart;
			const Vec2i64 offset = edgeStart - from;
			const int	  turn	 = geometry::cross(direction, edge).sign();
			const double  directionX = static_cast<double>(direction.x);
			const double  directionY = static_cast<double>(direction.y);
			if (turn == 0) {
				if (geometry::cross(offset, direction).sign() != 0) {
					return;
				}
				const double lengthSquared = directionX * directionX + directionY * directionY;
				for (const Vec2i64& end : {edgeStart, edgeEnd}) {
					const Vec2i64 reach = end - from;
					const double  along = (static_cast<double>(reach.x) * directionX + static_cast<double>(reach.y) * directionY) / lengthSquared;
					if (along >= 0.0 && along <= 1.0) {
						out.push_back(along);
					}
				}
				return;
			}
			const double denominator = crossOf(directionX, directionY, static_cast<double>(edge.x), static_cast<double>(edge.y));
			const double along = crossOf(static_cast<double>(offset.x), static_cast<double>(offset.y), static_cast<double>(edge.x), static_cast<double>(edge.y)) / denominator;
			const double onEdge = crossOf(static_cast<double>(offset.x), static_cast<double>(offset.y), directionX, directionY) / denominator;
			if (along >= 0.0 && along <= 1.0 && onEdge >= 0.0 && onEdge <= 1.0) {
				out.push_back(along);
			}
		}

		Vec2i64 pointAlong(const Vec2i64& from, const Vec2i64& direction, double along) {
			return {from.x + std::llround(static_cast<double>(direction.x) * along), from.y + std::llround(static_cast<double>(direction.y) * along)};
		}

	} // namespace

	std::int64_t drawnVisibleRadiusMm(std::int64_t sightMm) {
		std::int64_t diagonal = 0;
		while (diagonal * diagonal < 2 * kSeenSlackMm * kSeenSlackMm) {
			++diagonal;
		}
		return geometry::sightInradius(sightMm - kSeenSlackMm) - diagonal;
	}

	// The hull of a convex polygon and its copy moved by the leg: each side faces back along the
	// leg or not, the back sides stay at the start, the rest move to the end, and where the two
	// kinds meet the hull runs along the leg from one copy to the other.
	std::vector<Vec2i64> legCapsule(const Vec2i64& start, const Vec2i64& end, std::int64_t radiusMm) {
		const std::vector<Vec2i64> atStart = geometry::sightPolygon(start, radiusMm);
		const Vec2i64			   leg	   = end - start;
		const std::size_t		   sides   = atStart.size();
		std::vector<bool>		   facesBack(sides);
		for (std::size_t k = 0; k < sides; ++k) {
			facesBack[k] = geometry::cross(leg, atStart[(k + 1) % sides] - atStart[k]).sign() < 0;
		}
		std::vector<Vec2i64> out;
		out.reserve(sides + 4);
		for (std::size_t k = 0; k < sides; ++k) {
			const bool before = facesBack[(k + sides - 1) % sides];
			const bool after  = facesBack[k];
			if (before && after) {
				out.push_back(atStart[k]);
			} else if (!before && !after) {
				out.push_back(atStart[k] + leg);
			} else if (before) {
				out.push_back(atStart[k]);
				out.push_back(atStart[k] + leg);
			} else {
				out.push_back(atStart[k] + leg);
				out.push_back(atStart[k]);
			}
		}
		dropStraightVertices(out);
		return out;
	}

	SeenArea::SeenArea()
		: mesh(std::make_unique<geometry::nav::PatchableMesh>()),
		  scratch(std::make_unique<geometry::nav::SightCoverScratch>()) {}

	SeenArea::~SeenArea() = default;

	SeenArea::PendingShape SeenArea::pendingOf(std::vector<Vec2i64> outline, const Vec2i64& centre) {
		PendingShape shape;
		boxOf(outline, shape.low, shape.high);
		shape.convex  = convex(outline);
		shape.outline = std::move(outline);
		shape.centre  = centre;
		return shape;
	}

	void SeenArea::commit(const PendingShape& shape) {
		if (geometry::nav::sightShapeCovered(*mesh, shape.outline, shape.centre, *scratch)) {
			++dropped;
			return;
		}
		geometry::nav::PatchEdits edits;
		edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = shape.outline});
		const geometry::nav::PatchReport report = mesh->applyPatch(edits);
		if (!report.added.empty() && report.added.front() != geometry::nav::kNoRing) {
			registered.push_back(report.added.front());
		}
		mesh->refreshWidths();
	}

	void SeenArea::commitNow(const std::vector<Vec2i64>& shape, const Vec2i64& centre) { commit(pendingOf(shape, centre)); }

	void SeenArea::queue(std::vector<Vec2i64> shape, const Vec2i64& centre) { waiting.push_back(pendingOf(std::move(shape), centre)); }

	void SeenArea::setOpenLeg(LegId leg, const Vec2i64& start, const Vec2i64& end, std::int64_t radiusMm) {
		assert(radiusMm >= kSeenLeastRadiusMm);
		auto found = openLegs.find(leg);
		if (found != openLegs.end() && found->second.centre == start && found->second.legEnd == end && found->second.radius == radiusMm) {
			return;
		}
		PendingShape shape = pendingOf(legCapsule(start, end, radiusMm), start);
		shape.legEnd	   = end;
		shape.radius	   = radiusMm;
		openLegs.insert_or_assign(leg, std::move(shape));
	}

	void SeenArea::closeLeg(LegId leg) {
		auto found = openLegs.find(leg);
		if (found == openLegs.end()) {
			return;
		}
		waiting.push_back(std::move(found->second));
		openLegs.erase(found);
	}

	void SeenArea::update() {
		for (std::size_t count = 0; count < kSeenRingsPerTick && !waiting.empty(); ++count) {
			commit(waiting.front());
			waiting.pop_front();
		}
	}

	template <typename Visit> void SeenArea::forEachPending(Visit visit) const {
		for (const auto& [leg, shape] : openLegs) {
			visit(shape);
		}
		for (const PendingShape& shape : waiting) {
			visit(shape);
		}
	}

	bool SeenArea::isSeen(const Vec2i64& point) const {
		if (mesh->locate(point) >= 0) {
			return true;
		}
		bool seen = false;
		forEachPending([&](const PendingShape& shape) {
			seen = seen || (insideBox(point, shape.low, shape.high) && geometry::pointInPolygon(point, shape.outline) != geometry::PointInPolygon::Outside);
		});
		return seen;
	}

	bool SeenArea::covers(const std::vector<Vec2i64>& outline) const {
		if (outline.size() < 3) {
			return std::all_of(outline.begin(), outline.end(), [this](const Vec2i64& point) { return isSeen(point); });
		}
		if (mesh->groundCovers(outline)) {
			return true;
		}
		Vec2i64 low;
		Vec2i64 high;
		boxOf(outline, low, high);
		bool held	 = false;
		bool nearAny = false;
		forEachPending([&](const PendingShape& shape) {
			if (held || !boxesMeet(low, high, shape.low, shape.high)) {
				return;
			}
			nearAny = true;
			held	= shape.convex && std::all_of(outline.begin(), outline.end(), [&shape](const Vec2i64& point) {
						  return geometry::pointInPolygon(point, shape.outline) != geometry::PointInPolygon::Outside;
					  });
		});
		if (held || !nearAny) {
			return held;
		}

		// Ground from the record and the pending shapes together near the outline: the record's
		// triangles there and the pending shapes that reach it, each a ground ring.
		geometry::nav::PatchEdits edits;
		forEachPending([&](const PendingShape& shape) {
			if (boxesMeet(low, high, shape.low, shape.high)) {
				edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = shape.outline});
			}
		});
		std::vector<Triangle> triangles;
		appendTriangles(triangles);
		for (const Triangle& triangle : triangles) {
			const std::vector<Vec2i64> ring(triangle.corners.begin(), triangle.corners.end());
			Vec2i64					   triangleLow;
			Vec2i64					   triangleHigh;
			boxOf(ring, triangleLow, triangleHigh);
			if (boxesMeet(low, high, triangleLow, triangleHigh)) {
				edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = ring});
			}
		}
		geometry::nav::PatchableMesh together;
		together.applyPatch(edits);
		return together.groundCovers(outline);
	}

	std::optional<Vec2i64> SeenArea::lastSeenToward(const Vec2i64& from, const Vec2i64& to) const {
		if (isSeen(to)) {
			return to;
		}
		const Vec2i64 direction = to - from;
		Vec2i64		  low{std::min(from.x, to.x), std::min(from.y, to.y)};
		Vec2i64		  high{std::max(from.x, to.x), std::max(from.y, to.y)};

		std::vector<double>						 along{0.0, 1.0};
		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		mesh->meshEdgesNear(low, high, edgeIds, edges);
		for (const auto& [edgeStart, edgeEnd] : edges) {
			meetings(from, direction, edgeStart, edgeEnd, along);
		}
		forEachPending([&](const PendingShape& shape) {
			if (!boxesMeet(low, high, shape.low, shape.high)) {
				return;
			}
			for (std::size_t k = 0; k < shape.outline.size(); ++k) {
				meetings(from, direction, shape.outline[k], shape.outline[(k + 1) % shape.outline.size()], along);
			}
		});
		std::sort(along.begin(), along.end());
		along.erase(std::unique(along.begin(), along.end()), along.end());

		// Between two neighbouring meetings the segment is wholly seen or wholly not, so the last
		// seen stretch ends at the meeting after the last stretch whose middle is seen. Rounding to
		// the millimetre can put that end a hair outside, so it steps back toward the middle.
		const double lengthMm = std::sqrt(static_cast<double>(direction.x) * static_cast<double>(direction.x) +
										  static_cast<double>(direction.y) * static_cast<double>(direction.y));
		for (std::size_t i = along.size() - 1; i > 0; --i) {
			const double  middle	  = (along[i - 1] + along[i]) / 2.0;
			const Vec2i64 middlePoint = pointAlong(from, direction, middle);
			if (!isSeen(middlePoint)) {
				continue;
			}
			for (int millimetres = 0; millimetres <= kStepsBack && along[i] - millimetres / lengthMm > middle; ++millimetres) {
				const Vec2i64 candidate = pointAlong(from, direction, along[i] - millimetres / lengthMm);
				if (isSeen(candidate)) {
					return candidate;
				}
			}
			return middlePoint;
		}
		return isSeen(from) ? std::optional<Vec2i64>(from) : std::nullopt;
	}

	std::uint64_t SeenArea::epoch() const { return mesh->epoch(); }

	void SeenArea::appendTriangles(std::vector<Triangle>& out) const {
		const auto& triangles = mesh->triangles();
		const auto& vertices  = mesh->vertices();
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(triangles.size()); ++t) {
			if (!mesh->triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& triangle = triangles[static_cast<std::size_t>(t)];
			out.push_back({{vertices[triangle.v[0]], vertices[triangle.v[1]], vertices[triangle.v[2]]}});
		}
	}

	void SeenArea::appendPendingShapes(std::vector<const std::vector<Vec2i64>*>& out) const {
		forEachPending([&out](const PendingShape& shape) { out.push_back(&shape.outline); });
	}

	double SeenArea::committedSquareMeters() const {
		std::vector<Triangle> triangles;
		appendTriangles(triangles);
		double doubled = 0.0;
		for (const Triangle& triangle : triangles) {
			doubled += geometry::cross(triangle.corners[1] - triangle.corners[0], triangle.corners[2] - triangle.corners[0]).toDouble();
		}
		return doubled / 2e6;
	}

	const std::vector<Vec2i64>& SeenArea::registeredRing(std::size_t index) const { return mesh->ring(registered[index]).polygon.ring; }

} // namespace vision
