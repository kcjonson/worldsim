#include "Silhouette.h"

#include "../boolean/RingBoolean.h"
#include "../offset/WallOffset.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace geometry {

	namespace {

		// The regular octagon whose edges touch the circle of `radius` round the origin, CCW. tan(22.5 degrees)
		// rounded up, so the octagon never falls inside the circle.
		std::vector<Vec2i64> octagon(std::int64_t radius) {
			const std::int64_t side = (radius * 4'142'136 + 9'999'999) / 10'000'000;
			return {{radius, side}, {side, radius}, {-side, radius}, {-radius, side}, {-radius, -side}, {-side, -radius}, {side, -radius}, {radius, -side}};
		}

		// Convex hull, CCW, by the monotone chain; collinear points dropped.
		Ring convexHullOf(std::vector<Vec2i64> points) {
			std::sort(points.begin(), points.end());
			points.erase(std::unique(points.begin(), points.end()), points.end());
			if (points.size() < 3) {
				return points;
			}
			Ring hull(points.size() * 2);
			std::size_t count = 0;
			for (std::size_t i = 0; i < points.size(); ++i) {
				while (count >= 2 && cross(hull[count - 1] - hull[count - 2], points[i] - hull[count - 2]).sign() <= 0) {
					--count;
				}
				hull[count++] = points[i];
			}
			const std::size_t lowerCount = count + 1;
			for (std::size_t i = points.size() - 1; i-- > 0;) {
				while (count >= lowerCount && cross(hull[count - 1] - hull[count - 2], points[i] - hull[count - 2]).sign() <= 0) {
					--count;
				}
				hull[count++] = points[i];
			}
			hull.resize(count - 1);
			return hull;
		}

		// Every edge of the outline swept by the kernel, each a convex ring.
		void appendEdgeSweeps(const std::vector<Ring>& outline, const std::vector<Vec2i64>& kernel, std::vector<Ring>& out) {
			std::vector<Vec2i64> points;
			for (const Ring& ring : outline) {
				for (std::size_t i = 0; i < ring.size(); ++i) {
					points.clear();
					for (const Vec2i64& offset : kernel) {
						points.push_back(ring[i] + offset);
						points.push_back(ring[(i + 1) % ring.size()] + offset);
					}
					out.push_back(convexHullOf(points));
				}
			}
		}

		// At most this many rings go into one direct union; more are split in two, each half unioned, and the
		// halves' results unioned.
		constexpr std::size_t kLeafRings = 8;

		// The nonzero union of CCW rings (each filling its inside, so pieces from two halves, outer rings CCW and
		// holes CW, union by the same rule). One union of thousands of overlapping triangles cuts every edge
		// against every edge it crosses; unioning in halves only ever cuts outlines against outlines, so a
		// triangle's edges inside the shape are gone before they meet the rest. The rings' order is kept, so
		// neighbours in the input (one path's triangles, which tile without crossing) meet first. Each half's
		// rings are simplified by `tolerance` before the halves meet (0 keeps them exact). With `fillHoles` a
		// half's holes are left out where the halves meet, since the whole's would be filled anyway; the result
		// still has its own.
		std::vector<RegionPolygon> unionInHalves(
			const std::vector<Ring>& rings, std::size_t first, std::size_t last, bool fillHoles, std::int64_t tolerance
		) {
			std::vector<Ring> group;
			if (last - first <= kLeafRings) {
				group.assign(rings.begin() + static_cast<std::ptrdiff_t>(first), rings.begin() + static_cast<std::ptrdiff_t>(last));
			} else {
				const std::size_t middle = first + (last - first) / 2;
				const auto		  take = [&](Ring&& ring) {
					   if (tolerance > 0) {
						   simplifyRing(ring, tolerance);
					   }
					   group.push_back(std::move(ring));
				};
				for (std::vector<RegionPolygon> half :
					 {unionInHalves(rings, first, middle, fillHoles, tolerance), unionInHalves(rings, middle, last, fillHoles, tolerance)}) {
					for (RegionPolygon& piece : half) {
						take(std::move(piece.outer));
						for (Ring& hole : piece.holes) {
							if (!fillHoles) {
								take(std::move(hole));
							}
						}
					}
				}
			}
			return unionOfFills({group});
		}

		// The outer rings of a union's pieces, less any piece standing in another's hole (filled with it).
		std::vector<Ring> outersOf(std::vector<RegionPolygon> region) {
			std::vector<std::size_t> holed;
			for (std::size_t i = 0; i < region.size(); ++i) {
				if (!region[i].holes.empty()) {
					holed.push_back(i);
				}
			}
			std::vector<Ring> outline;
			for (std::size_t i = 0; i < region.size(); ++i) {
				bool inHole = false;
				for (const std::size_t other : holed) {
					if (other == i) {
						continue;
					}
					for (const Vec2i64& vertex : region[i].outer) {
						const PointInPolygon where = pointInPolygon(vertex, region[other].outer);
						if (where != PointInPolygon::OnBoundary) {
							inHole = where == PointInPolygon::Inside;
							break;
						}
					}
					if (inHole) {
						break;
					}
				}
				if (!inHole) {
					outline.push_back(std::move(region[i].outer));
				}
			}
			return outline;
		}

		// The outline shrunk by the kernel: the complement inside a box clear of the outline by more than the
		// kernel's reach, grown by the kernel, has the eroded outline as its holes.
		std::vector<Ring> erodeOutline(const std::vector<Ring>& outline, std::int64_t radius, std::int64_t stepTolerance) {
			if (outline.empty()) {
				return {};
			}
			Vec2i64 low = outline.front().front();
			Vec2i64 high = low;
			for (const Ring& ring : outline) {
				for (const Vec2i64& vertex : ring) {
					low = {std::min(low.x, vertex.x), std::min(low.y, vertex.y)};
					high = {std::max(high.x, vertex.x), std::max(high.y, vertex.y)};
				}
			}
			const std::int64_t margin = 3 * radius + 1;
			std::vector<Ring> complement{{{low.x - margin, low.y - margin}, {high.x + margin, low.y - margin}, {high.x + margin, high.y + margin}, {low.x - margin, high.y + margin}}};
			for (const Ring& ring : outline) {
				complement.emplace_back(ring.rbegin(), ring.rend());
			}
			std::vector<Ring> sweeps;
			appendEdgeSweeps(outline, octagon(radius), sweeps);
			for (RegionPolygon& band : unionInHalves(sweeps, 0, sweeps.size(), false, stepTolerance)) {
				complement.push_back(std::move(band.outer));
				for (Ring& hole : band.holes) {
					complement.push_back(std::move(hole));
				}
			}
			std::vector<Ring> eroded;
			for (RegionPolygon& piece : unionOfFills({complement})) {
				for (Ring& hole : piece.holes) {
					std::reverse(hole.begin(), hole.end());
					eroded.push_back(std::move(hole));
				}
			}
			return eroded;
		}

		std::vector<Ring> unionOutline(std::vector<Ring> rings, std::int64_t stepTolerance) {
			std::erase_if(rings, [](const Ring& ring) { return ring.size() < 3 || signedAreaDoubled(ring).sign() == 0; });
			if (rings.empty()) {
				return {};
			}
			for (Ring& ring : rings) {
				ensureCounterClockwise(ring);
			}
			return outersOf(unionInHalves(rings, 0, rings.size(), true, stepTolerance));
		}

		std::vector<Ring> grownOutline(const std::vector<Ring>& outline, std::int64_t radius, std::int64_t stepTolerance) {
			if (radius <= 0 || outline.empty()) {
				return outline;
			}
			std::vector<Ring> grown = outline;
			appendEdgeSweeps(outline, octagon(radius), grown);
			return unionOutline(std::move(grown), stepTolerance);
		}

	} // namespace

	std::vector<Ring> outlineOfUnion(std::vector<Ring> rings) {
		return unionOutline(std::move(rings), 0);
	}

	std::vector<Ring> dilateOutline(const std::vector<Ring>& outline, std::int64_t radius) {
		return grownOutline(outline, radius, 0);
	}

	std::vector<Ring> closeOutline(const std::vector<Ring>& outline, std::int64_t radius, std::int64_t tolerance) {
		if (radius <= 0 || outline.empty()) {
			return outline;
		}
		std::vector<Ring> simplified = outline;
		for (Ring& ring : simplified) {
			simplifyRing(ring, tolerance);
		}
		return simplifyOutline(erodeOutline(grownOutline(simplified, radius, tolerance), radius, tolerance), tolerance);
	}

	std::vector<Ring> simplifyOutline(std::vector<Ring> outline, std::int64_t tolerance) {
		for (Ring& ring : outline) {
			simplifyRing(ring, tolerance);
		}
		return outlineOfUnion(std::move(outline));
	}

	std::vector<Ring> silhouetteOfTriangles(const std::vector<Vec2i64>& triangleVertices, std::int64_t closeRadius, std::int64_t tolerance) {
		std::vector<Ring> triangles;
		triangles.reserve(triangleVertices.size() / 3);
		for (std::size_t first = 0; first + 3 <= triangleVertices.size(); first += 3) {
			const Vec2i64& a = triangleVertices[first];
			const Vec2i64& b = triangleVertices[first + 1];
			const Vec2i64& c = triangleVertices[first + 2];
			if (orientation(a, b, c) != Orientation::Collinear) {
				triangles.push_back({a, b, c});
			}
		}
		const std::vector<Ring> outline = simplifyOutline(unionOutline(std::move(triangles), tolerance), tolerance);
		return closeOutline(outline, closeRadius, tolerance);
	}

} // namespace geometry
