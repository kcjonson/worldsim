// Player visibility's seen record (player-visibility.md 3.3) as a second, standalone
// PatchableMesh holding only sight outlines as ground rings: legs walked in the open, the
// landing polygon, and visibility polygons near walls. Each shape goes through frontierOf, a
// prototype below that registers only what a shape adds (step 3b), and its pieces are patched in.
// The rows measure what that record costs: rings registered and bytes held, patch and covered-test
// time, point location, the nav upkeep every patch runs, a walk of the union's edge, a base of
// rooms walked by colonists, and how often a visibility polygon is not a simple ring. Every row runs once and prints its figures on stderr;
// the counters carry the headline numbers.

#include "nav/MeshComponents.h"
#include "nav/NavMesh.h"
#include "nav/PatchableMesh.h"
#include "nav/SightCover.h"
#include "polygon/Polygon.h"
#include "predicates/Predicates.h"
#include "visibility/Buildings.test.h"
#include "visibility/SightPolygon.h"
#include "visibility/Visibility.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

	using geometry::UnwrappedAngle;
	using geometry::Vec2i64;
	using namespace geometry::nav;

	constexpr std::int64_t kSightMm		   = 30000;
	constexpr std::int64_t kSlackMm		   = 50;
	constexpr std::int64_t kLegRadiusMm	   = kSightMm - kSlackMm;
	constexpr std::int64_t kLandingMm	   = 150000;
	constexpr std::int64_t kSeenProvenance = -3;
	constexpr double	   kPi			   = 3.14159265358979323846;
	constexpr double	   kWalkMps		   = 2.0; // Movement::speed's default

	// PatchableMesh's private storage and door tagging, reached through explicit
	// instantiations, which the language exempts from access checking. Nothing here writes
	// except applyDoorTags, which sets the tags a patch already set.
	using RingsMember		= geometry::PagedVector<RegisteredRing> PatchableMesh::*;
	using PiecesMember		= geometry::PagedVector<std::vector<std::int32_t>> PatchableMesh::*;
	using EdgeOfMember		= geometry::PagedVector<EdgeSource> PatchableMesh::*;
	using FreshWidthsMember = std::vector<std::int32_t> PatchableMesh::*;
	using DoorTagsMember	= void (PatchableMesh::*)(std::int32_t);
	struct RingsKey {
		friend RingsMember memberOf(RingsKey);
	};
	struct PiecesKey {
		friend PiecesMember memberOf(PiecesKey);
	};
	struct EdgeOfKey {
		friend EdgeOfMember memberOf(EdgeOfKey);
	};
	struct FreshWidthsKey {
		friend FreshWidthsMember memberOf(FreshWidthsKey);
	};
	struct DoorTagsKey {
		friend DoorTagsMember memberOf(DoorTagsKey);
	};
	template <typename Key, typename Member, Member Value> struct Reach {
		friend Member memberOf(Key) { return Value; }
	};
	template struct Reach<RingsKey, RingsMember, &PatchableMesh::rings>;
	template struct Reach<PiecesKey, PiecesMember, &PatchableMesh::pieces>;
	template struct Reach<EdgeOfKey, EdgeOfMember, &PatchableMesh::edgeOf>;
	template struct Reach<FreshWidthsKey, FreshWidthsMember, &PatchableMesh::freshWidthTriangles>;
	template struct Reach<DoorTagsKey, DoorTagsMember, &PatchableMesh::applyDoorTags>;

	// The width seed type is private, so its list is measured by a function the explicit
	// instantiation sets.
	struct ListSize {
		std::size_t count = 0;
		std::size_t bytes = 0;
	};
	using ListSizeOf		 = ListSize (*)(const PatchableMesh&);
	ListSizeOf widthSeedsOf = nullptr;
	template <auto Member> struct ReachList {
		static inline const bool set = (widthSeedsOf = [](const PatchableMesh& mesh) {
			const auto& list = mesh.*Member;
			return ListSize{list.size(), list.capacity() * sizeof(*list.data())};
		}, true);
	};
	template struct ReachList<&PatchableMesh::pendingWidthSeeds>;

	template <typename Work> double secondsOf(Work work) {
		const auto start = std::chrono::steady_clock::now();
		work();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double x : values) {
			sum += x;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	double sumOf(const std::vector<double>& values) { return meanOf(values) * static_cast<double>(values.size()); }

	double percentileOf(std::vector<double> values, double quantile) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(quantile * static_cast<double>(values.size() - 1) + 0.5))];
	}

	double maxOf(const std::vector<double>& values) { return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end()); }

	void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
		low	 = ring.front();
		high = ring.front();
		for (const Vec2i64& point : ring) {
			low	 = {std::min(low.x, point.x), std::min(low.y, point.y)};
			high = {std::max(high.x, point.x), std::max(high.y, point.y)};
		}
	}

	std::int64_t cross3(const Vec2i64& origin, const Vec2i64& first, const Vec2i64& second) {
		return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
	}

	// Convex hull, counter-clockwise, collinear points dropped.
	std::vector<Vec2i64> hullOf(std::vector<Vec2i64> pts) {
		std::sort(pts.begin(), pts.end());
		pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
		std::vector<Vec2i64> hull(2 * pts.size());
		std::size_t			 size = 0;
		for (const Vec2i64& point : pts) {
			while (size >= 2 && cross3(hull[size - 2], hull[size - 1], point) <= 0) {
				--size;
			}
			hull[size++] = point;
		}
		const std::size_t lower = size + 1;
		for (std::size_t i = pts.size() - 1; i-- > 0;) {
			while (size >= lower && cross3(hull[size - 2], hull[size - 1], pts[i]) <= 0) {
				--size;
			}
			hull[size++] = pts[i];
		}
		hull.resize(size - 1);
		return hull;
	}

	// A leg's capsule: the hull of the leg-radius sight polygons at its two ends.
	std::vector<Vec2i64> legRing(const Vec2i64& start, const Vec2i64& end) {
		std::vector<Vec2i64>	   pts	 = geometry::sightPolygon(start, kLegRadiusMm);
		const std::vector<Vec2i64> atEnd = geometry::sightPolygon(end, kLegRadiusMm);
		pts.insert(pts.end(), atEnd.begin(), atEnd.end());
		return hullOf(std::move(pts));
	}

	Vec2i64 stepFrom(const Vec2i64& point, double heading, double lengthMm) {
		return {point.x + std::llround(std::cos(heading) * lengthMm), point.y + std::llround(std::sin(heading) * lengthMm)};
	}

	// Bytes the mesh holds, from its containers' sizes. The registry: each ring slot with its
	// outline, sample and edge ids, and the edge index (per ring edge its source, flag and
	// half-edge list). The triangles: each triangle slot with its edge sources, cover and
	// live flag, and each vertex slot with its use count. Box grids, hash tables and
	// interned lists are left out of both.
	struct Bytes {
		double registry	 = 0.0;
		double triangles = 0.0;
	};

	Bytes bytesOf(const PatchableMesh& mesh) {
		Bytes		out;
		const auto& rings = mesh.*memberOf(RingsKey{});
		for (std::size_t i = 0; i < rings.size(); ++i) {
			const RegisteredRing& registeredRing = rings[i];
			out.registry += static_cast<double>(sizeof(RegisteredRing) + registeredRing.polygon.ring.capacity() * sizeof(Vec2i64) +
												registeredRing.sample.ring3.capacity() * sizeof(Vec2i64) + registeredRing.edgeIds.capacity() * sizeof(std::int32_t));
		}
		const auto& edgeOf = mesh.*memberOf(EdgeOfKey{});
		const auto& pieces = mesh.*memberOf(PiecesKey{});
		out.registry += static_cast<double>(edgeOf.size() * (sizeof(EdgeSource) + 1));
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			out.registry += static_cast<double>(sizeof(std::vector<std::int32_t>) + pieces[i].capacity() * sizeof(std::int32_t));
		}
		out.triangles = static_cast<double>(mesh.triangles().size() * (sizeof(NavTriangle) + 3 * sizeof(std::int32_t) + sizeof(std::int32_t) + 1) +
											mesh.vertices().size() * (sizeof(Vec2i64) + sizeof(std::int32_t)));
		return out;
	}

	std::vector<Vec2i64> liveVertices(const PatchableMesh& mesh) {
		std::vector<std::uint8_t> used(mesh.vertices().size(), 0);
		std::vector<Vec2i64>	  out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			for (const std::uint32_t vertex : mesh.triangles()[static_cast<std::size_t>(t)].v) {
				if (used[vertex] == 0) {
					out.push_back(mesh.vertices()[vertex]);
				}
				used[vertex] = 1;
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// The union's area in square metres, from its live triangles.
	double areaOf(const PatchableMesh& mesh) {
		double doubled = 0.0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			doubled += std::abs(static_cast<double>(cross3(mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]])));
		}
		return doubled / 2e6;
	}

	// The union's boundary: every live triangle edge with nothing across it, collected in
	// one pass over the triangle slots.
	std::vector<std::pair<Vec2i64, Vec2i64>> boundaryOf(const PatchableMesh& mesh) {
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (std::size_t e = 0; e < 3; ++e) {
				if (tri.neighbor[e] < 0) {
					edges.emplace_back(mesh.vertices()[tri.v[e]], mesh.vertices()[tri.v[(e + 1) % 3]]);
				}
			}
		}
		std::sort(edges.begin(), edges.end());
		return edges;
	}

	// Best of five passes of boundaryOf.
	struct EdgeWalk {
		double		micros = 0.0;
		std::size_t edges  = 0;
	};

	EdgeWalk walkBoundary(const PatchableMesh& mesh) {
		EdgeWalk out;
		out.micros = 1e30;
		for (int run = 0; run < 5; ++run) {
			std::size_t	 count	 = 0;
			const double seconds = secondsOf([&] {
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
					if (!mesh.triangleLive(t)) {
						continue;
					}
					const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
					for (std::size_t e = 0; e < 3; ++e) {
						count += tri.neighbor[e] < 0 ? 1 : 0;
					}
				}
			});
			benchmark::DoNotOptimize(count);
			out.micros = std::min(out.micros, seconds * 1e6);
			out.edges  = count;
		}
		return out;
	}

	// --- A prototype: register only what a shape adds (step 3b) ------------------------
	//
	// Kept here, not in the library, because it does not yet meet its acceptance
	// (player-visibility.md 6.3). A shape it calls covered is one sightShapeCovered would.

	// A piece's inner edge is pulled this far back inside the record, so the piece and the record
	// overlap rather than abut: nothing depends on two snapped edges coinciding.
	constexpr std::int64_t kSeenPieceOverlapMm = 100;

	enum class Frontier {
		Covered, // nothing to register
		Pieces,	 // register the pieces
		Whole,	 // register the whole shape (the one piece)
	};

	// A mesh edge touching side `side`'s triangle, relative to the centre.
	struct FrontierContact {
		std::size_t side  = 0;
		double		fromX = 0.0;
		double		fromY = 0.0;
		double		toX	  = 0.0;
		double		toY	  = 0.0;
	};

	// A wedge of a side's triangle, between two rays from the centre, that adds ground: distances
	// along each ray to the nearest mesh edge (inner) and to the side (outer).
	struct FrontierWedge {
		std::size_t side	   = 0;
		double		fromX	   = 0.0;
		double		fromY	   = 0.0;
		double		toX		   = 0.0;
		double		toY		   = 0.0;
		bool		fromVertex = false; // the ray runs to the shape's vertex
		bool		toVertex   = false;
		double		innerFrom  = 0.0;
		double		innerTo	   = 0.0;
		double		outerFrom  = 0.0;
		double		outerTo	   = 0.0;
	};

	// A ray of a run of wedges: its unit direction, the shape's vertex it runs to (the shape's size
	// when none), and the distances along it to the shape's side and to the nearest contact.
	struct FrontierRay {
		double		unitX  = 0.0;
		double		unitY  = 0.0;
		std::size_t vertex = 0;
		double		outer  = 0.0;
		double		nearer = 0.0;
	};

	struct FrontierScratch {
		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> meshEdges;
		std::vector<Vec2i64>					 offsets;
		std::vector<double>						 sideLengths;
		std::vector<UnwrappedAngle>				 angles;
		std::vector<UnwrappedAngle>				 highestSoFar;	 // per side, the highest angle of it and every side before
		std::vector<UnwrappedAngle>				 lowestFromHere; // per side, the lowest angle of it and every side after
		std::vector<UnwrappedAngle>				 wedgeStart;	 // per side, where its core's wedge starts and ends
		std::vector<UnwrappedAngle>				 wedgeEnd;
		std::vector<std::uint8_t>				 reached;		 // per side, the record's edge meets its core
		std::vector<FrontierContact>			 contacts;
		std::vector<FrontierWedge>			 wedges;
		std::vector<std::array<double, 2>>		 rays;
		std::vector<FrontierRay>				 runRays;
	};

	// Fan `shape` from `centre` into one triangle per side. A counter-clockwise triangle's core is
	// the part of it farther than kSeenCoverToleranceMm from its side's line, strictly inside its
	// wedge, the wedge narrowed to leave out every angle the shape steps back over (a snapped
	// shadow edge does, by under a millimetre). A core the record's edge does not meet lies wholly
	// in the record, being joined to the centre, which is on it.
	//
	// Covered: the centre is on the record and no core is met. What the record can lack of the
	// shape lies within the tolerance of a side's line, or in the wedges the shape steps back over.
	//
	// Pieces: each met triangle's wedge is split by rays at every end of a record edge inside it
	// and wherever a record edge crosses the side's line or the tolerance line. Between two
	// neighbouring rays no record edge ends and record edges never cross, so the nearest one is the
	// same all the way across, and everything nearer the centre than it is on the record. A split
	// wedge adds ground where that nearest edge lies deeper than the tolerance. Each run of adding
	// wedges that share rays is one ring. Its two end rays turn outward by kSeenPieceOverlapMm at
	// the run's farthest reach; its outer chain is the shape's own boundary between them; its inner
	// chain stands kSeenPieceOverlapMm back from every record edge point near it, so the piece
	// overlaps the record on every side rather than abutting it. Inner points nearer the centre
	// than the overlap become the centre, and a run under the overlap wide is fanned from the
	// centre. The record and the pieces together hold the shape, but for the tolerance strips above.
	//
	// Whole: the centre is off the record, an edge of the record passes through it, every core is
	// met, a run's rays do not meet the boundary in order, or a piece would not be a simple ring;
	// `pieces` holds the shape.
	//
	// `shape` must be simple and wind once round `centre` (simpleAround). `pieces` is cleared first.

	double crossOf(double firstX, double firstY, double secondX, double secondY) {
		return firstX * secondY - firstY * secondX;
	}

	// Narrows [first, last] to where from + t (to - from) > bound, or >= bound when `closed`.
	void narrow(double from, double to, double bound, bool closed, double& first, double& last) {
		const double change = to - from;
		if (change == 0.0) {
			if (closed ? from < bound : from <= bound) {
				first = 1.0;
				last  = 0.0;
			}
			return;
		}
		const double at = (bound - from) / change;
		if (change > 0.0) {
			first = std::max(first, at);
		} else {
			last = std::min(last, at);
		}
	}

	// Inner points closer together than this are merged; the overlap absorbs it.
	constexpr double kInnerPointSpacingMm = 5.0;

	Frontier frontierOf(const PatchableMesh& record, const std::vector<Vec2i64>& shape, const Vec2i64& centre, FrontierScratch& scratch,
									 std::vector<std::vector<Vec2i64>>& pieces) {
		pieces.clear();
		auto whole = [&] {
			pieces.push_back(shape);
			return Frontier::Whole;
		};
		if (record.locate(centre) < 0) {
			return whole();
		}
		const std::size_t			 count		 = shape.size();
		std::vector<Vec2i64>&		 offsets	 = scratch.offsets;
		std::vector<double>&		 sideLengths = scratch.sideLengths;
		std::vector<UnwrappedAngle>& angles		 = scratch.angles;
		std::vector<UnwrappedAngle>& highest	 = scratch.highestSoFar;
		std::vector<UnwrappedAngle>& lowest		 = scratch.lowestFromHere;
		unwrapAngles(shape, centre, angles);
		offsets.clear();
		sideLengths.clear();
		highest.clear();
		lowest.assign(count, UnwrappedAngle{});
		Vec2i64 boxLow			 = shape.front();
		Vec2i64 boxHigh			 = boxLow;
		double	reachSquared = 0.0;
		for (std::size_t i = 0; i < count; ++i) {
			const Vec2i64& vertex = shape[i];
			const Vec2i64  offset = vertex - centre;
			offsets.push_back(offset);
			boxLow			 = {std::min(boxLow.x, vertex.x), std::min(boxLow.y, vertex.y)};
			boxHigh			 = {std::max(boxHigh.x, vertex.x), std::max(boxHigh.y, vertex.y)};
			reachSquared = std::max(reachSquared, static_cast<double>(offset.x) * static_cast<double>(offset.x) + static_cast<double>(offset.y) * static_cast<double>(offset.y));
			const Vec2i64 side = shape[(i + 1) % count] - vertex;
			sideLengths.push_back(std::sqrt(static_cast<double>(side.x) * static_cast<double>(side.x) + static_cast<double>(side.y) * static_cast<double>(side.y)));
			const UnwrappedAngle& high = angleBefore(angles[i], angles[i + 1]) ? angles[i + 1] : angles[i];
			highest.push_back(i == 0 || angleBefore(highest.back(), high) ? high : highest.back());
		}
		for (std::size_t i = count; i-- > 0;) {
			const UnwrappedAngle& low = angleBefore(angles[i + 1], angles[i]) ? angles[i + 1] : angles[i];
			lowest[i]				  = i + 1 == count || angleBefore(low, lowest[i + 1]) ? low : lowest[i + 1];
		}
		// Side i's wedge runs from the highest vertex angle up to vertex i to the lowest from vertex
		// i + 1 on, every vertex also counted a turn back and a turn on.
		std::vector<UnwrappedAngle>& wedgeStart = scratch.wedgeStart;
		std::vector<UnwrappedAngle>& wedgeEnd	= scratch.wedgeEnd;
		wedgeStart.assign(count, UnwrappedAngle{});
		wedgeEnd.assign(count, UnwrappedAngle{});
		UnwrappedAngle running{angles.front().turns - 1, angles.front().direction};
		for (std::size_t i = 0; i < count; ++i) {
			const UnwrappedAngle turnBack{angles[i].turns - 1, angles[i].direction};
			running = angleBefore(running, turnBack) ? turnBack : running;
		}
		for (std::size_t i = 0; i < count; ++i) {
			running		  = angleBefore(running, angles[i]) ? angles[i] : running;
			wedgeStart[i] = running;
		}
		running = UnwrappedAngle{angles[1].turns + 1, angles[1].direction};
		for (std::size_t i = 1; i <= count; ++i) {
			const UnwrappedAngle turnOn{angles[i].turns + 1, angles[i].direction};
			running = angleBefore(turnOn, running) ? turnOn : running;
		}
		for (std::size_t i = count; i-- > 0;) {
			running		= angleBefore(angles[i + 1], running) ? angles[i + 1] : running;
			wedgeEnd[i] = running;
		}

		const double tolerance = static_cast<double>(kSeenCoverToleranceMm);
		const double reach	   = std::sqrt(reachSquared);
		std::vector<std::uint8_t>&		 reached  = scratch.reached;
		std::vector<FrontierContact>& contacts = scratch.contacts;
		reached.assign(count, 0);
		contacts.clear();
		// For the mesh edge from + t (to - from), t in [0, 1], and side i's triangle: whether the edge
		// touches the closed triangle (a contact, kept for the pieces) and whether it meets the core.
		auto measure = [&](std::size_t i, double fromX, double fromY, double toX, double toY) {
			if (cross(offsets[i], offsets[(i + 1) % count]).sign() <= 0) {
				return;
			}
			const double leftX	  = static_cast<double>(offsets[i].x);
			const double leftY	  = static_cast<double>(offsets[i].y);
			const double rightX	  = static_cast<double>(offsets[(i + 1) % count].x);
			const double rightY	  = static_cast<double>(offsets[(i + 1) % count].y);
			const double sideFrom = crossOf(rightX - leftX, rightY - leftY, fromX - leftX, fromY - leftY);
			const double sideTo	  = crossOf(rightX - leftX, rightY - leftY, toX - leftX, toY - leftY);
			double		 first	  = 0.0;
			double		 last	  = 1.0;
			narrow(crossOf(leftX, leftY, fromX, fromY), crossOf(leftX, leftY, toX, toY), 0.0, true, first, last);
			narrow(crossOf(fromX, fromY, rightX, rightY), crossOf(toX, toY, rightX, rightY), 0.0, true, first, last);
			narrow(sideFrom, sideTo, 0.0, true, first, last);
			if (first > last) {
				return;
			}
			contacts.push_back({i, fromX, fromY, toX, toY});
			if (!angleBefore(wedgeStart[i], wedgeEnd[i])) {
				return;
			}
			const double startX = static_cast<double>(wedgeStart[i].direction.x);
			const double startY = static_cast<double>(wedgeStart[i].direction.y);
			const double endX	= static_cast<double>(wedgeEnd[i].direction.x);
			const double endY	= static_cast<double>(wedgeEnd[i].direction.y);
			first				= 0.0;
			last				= 1.0;
			narrow(crossOf(startX, startY, fromX, fromY), crossOf(startX, startY, toX, toY), 0.0, false, first, last);
			narrow(crossOf(fromX, fromY, endX, endY), crossOf(toX, toY, endX, endY), 0.0, false, first, last);
			narrow(sideFrom, sideTo, tolerance * sideLengths[i], false, first, last);
			reached[i] = reached[i] != 0 || first < last ? 1 : 0;
		};

		record.meshEdgesNear(boxLow, boxHigh, scratch.edgeIds, scratch.meshEdges);
		for (const auto& [edgeStart, edgeEnd] : scratch.meshEdges) {
			if (std::max(edgeStart.x, edgeEnd.x) < boxLow.x || boxHigh.x < std::min(edgeStart.x, edgeEnd.x) || std::max(edgeStart.y, edgeEnd.y) < boxLow.y ||
				boxHigh.y < std::min(edgeStart.y, edgeEnd.y)) {
				continue;
			}
			if (closestOnSegment(centre, edgeStart, edgeEnd).distanceMm > reach) {
				continue;
			}
			Vec2i64 from = edgeStart - centre;
			Vec2i64 to	 = edgeEnd - centre;
			if (from == Vec2i64{0, 0} || to == Vec2i64{0, 0}) {
				return whole();
			}
			const int turn = cross(from, to).sign();
			if (turn == 0 && dot(from, to).sign() < 0) {
				return whole();
			}
			if (turn < 0) {
				std::swap(from, to);
			}
			// The edge's angular span, starting in the shape's first turn.
			const UnwrappedAngle start{angleLess(from, angles.front().direction) ? 1 : 0, from};
			const UnwrappedAngle end{start.turns + (angleLess(to, from) ? 1 : 0), to};
			const double		 fromX = static_cast<double>(from.x);
			const double		 fromY = static_cast<double>(from.y);
			const double		 toX   = static_cast<double>(to.x);
			const double		 toY   = static_cast<double>(to.y);
			for (const std::int64_t shift : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1}}) {
				const UnwrappedAngle low{start.turns + shift, start.direction};
				const UnwrappedAngle high{end.turns + shift, end.direction};
				// Every side before `first` ends before the span, and every side from `past` on
				// starts after it.
				const auto first = static_cast<std::size_t>(
					std::partition_point(highest.begin(), highest.end(), [&](const UnwrappedAngle& angle) { return angleBefore(angle, low); }) - highest.begin());
				const auto past = static_cast<std::size_t>(
					std::partition_point(lowest.begin(), lowest.end(), [&](const UnwrappedAngle& angle) { return !angleBefore(high, angle); }) - lowest.begin());
				for (std::size_t i = first; i < past; ++i) {
					measure(i, fromX, fromY, toX, toY);
				}
			}
		}

		std::size_t met = 0;
		for (std::size_t i = 0; i < count; ++i) {
			met += reached[i];
		}
		if (met == 0) {
			return Frontier::Covered;
		}
		if (met == count) {
			return whole();
		}
		std::stable_sort(contacts.begin(), contacts.end(), [](const FrontierContact& first, const FrontierContact& second) { return first.side < second.side; });

		// Split each met side's wedge at every contact's end inside it and wherever a contact crosses
		// the side's line or the tolerance line. Between two neighbouring rays no contact ends, and
		// contacts never cross, so the nearest contact along the middle ray is the nearest all the
		// way across: everything nearer the centre than it is on the record, and the wedge adds the
		// ground between it and the side, when that is deeper than the tolerance.
		std::vector<FrontierWedge>& wedges = scratch.wedges;
		std::vector<std::array<double, 2>>& rays = scratch.rays;
		wedges.clear();
		std::size_t contactAt = 0;
		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t contactsFrom = contactAt;
			while (contactAt < contacts.size() && contacts[contactAt].side == i) {
				++contactAt;
			}
			if (reached[i] == 0) {
				continue;
			}
			const double leftX	= static_cast<double>(offsets[i].x);
			const double leftY	= static_cast<double>(offsets[i].y);
			const double rightX = static_cast<double>(offsets[(i + 1) % count].x);
			const double rightY = static_cast<double>(offsets[(i + 1) % count].y);
			const double sideX	= rightX - leftX;
			const double sideY	= rightY - leftY;
			const double startX = static_cast<double>(wedgeStart[i].direction.x);
			const double startY = static_cast<double>(wedgeStart[i].direction.y);
			const double endX	= static_cast<double>(wedgeEnd[i].direction.x);
			const double endY	= static_cast<double>(wedgeEnd[i].direction.y);
			auto		 insideWedge = [&](double pointX, double pointY) {
				return crossOf(startX, startY, pointX, pointY) > 0.0 && crossOf(pointX, pointY, endX, endY) > 0.0;
			};
			rays.clear();
			for (std::size_t c = contactsFrom; c < contactAt; ++c) {
				const FrontierContact& contact = contacts[c];
				for (const auto& [pointX, pointY] : {std::array<double, 2>{contact.fromX, contact.fromY}, std::array<double, 2>{contact.toX, contact.toY}}) {
					if (insideWedge(pointX, pointY)) {
						rays.push_back({pointX, pointY});
					}
				}
				const double depthFrom = crossOf(sideX, sideY, contact.fromX - leftX, contact.fromY - leftY);
				const double depthTo   = crossOf(sideX, sideY, contact.toX - leftX, contact.toY - leftY);
				for (const double bound : {0.0, tolerance * sideLengths[i]}) {
					if ((depthFrom - bound) * (depthTo - bound) < 0.0) {
						const double along	= (bound - depthFrom) / (depthTo - depthFrom);
						const double pointX = contact.fromX + along * (contact.toX - contact.fromX);
						const double pointY = contact.fromY + along * (contact.toY - contact.fromY);
						if (insideWedge(pointX, pointY)) {
							rays.push_back({pointX, pointY});
						}
					}
				}
			}
			std::sort(rays.begin(), rays.end(), [](const std::array<double, 2>& first, const std::array<double, 2>& second) {
				return crossOf(first[0], first[1], second[0], second[1]) > 0.0;
			});
			rays.insert(rays.begin(), {startX, startY});
			rays.push_back({endX, endY});
			// Where a ray meets a line through `point` along `along`, as a distance along the ray's unit
			// direction; 0 when it does not meet it ahead.
			auto distanceAlong = [](double rayX, double rayY, double pointX, double pointY, double alongX, double alongY) {
				const double length	 = std::hypot(rayX, rayY);
				const double turning = crossOf(rayX / length, rayY / length, alongX, alongY);
				const double reachAlong = turning != 0.0 ? crossOf(pointX, pointY, alongX, alongY) / turning : 0.0;
				return std::isfinite(reachAlong) && reachAlong > 0.0 ? reachAlong : 0.0;
			};
			for (std::size_t r = 0; r + 1 < rays.size(); ++r) {
				const std::array<double, 2>& from = rays[r];
				const std::array<double, 2>& to	  = rays[r + 1];
				if (crossOf(from[0], from[1], to[0], to[1]) <= 0.0) {
					continue;
				}
				const double fromLength = std::hypot(from[0], from[1]);
				const double toLength	= std::hypot(to[0], to[1]);
				const double middleX	= from[0] / fromLength + to[0] / toLength;
				const double middleY	= from[1] / fromLength + to[1] / toLength;
				double		 nearestAt	   = std::numeric_limits<double>::infinity();
				std::size_t	 nearestContact = contactAt;
				for (std::size_t c = contactsFrom; c < contactAt; ++c) {
					const FrontierContact& contact = contacts[c];
					const double			 alongX	 = contact.toX - contact.fromX;
					const double			 alongY	 = contact.toY - contact.fromY;
					const double			 turning = crossOf(middleX, middleY, alongX, alongY);
					if (turning == 0.0) {
						continue;
					}
					const double rayParameter  = crossOf(contact.fromX, contact.fromY, alongX, alongY) / turning;
					const double edgeParameter = crossOf(contact.fromX, contact.fromY, middleX, middleY) / turning;
					const double length		   = rayParameter * std::hypot(middleX, middleY);
					if (rayParameter > 0.0 && edgeParameter >= 0.0 && edgeParameter <= 1.0 && length < nearestAt) {
						nearestAt	   = length;
						nearestContact = c;
					}
				}
				const double middleLength = std::hypot(middleX, middleY);
				const bool	 adds		  = nearestContact < contactAt &&
								  crossOf(sideX, sideY, middleX / middleLength * nearestAt - leftX, middleY / middleLength * nearestAt - leftY) > tolerance * sideLengths[i];
				if (!adds) {
					continue;
				}
				const FrontierContact& contact = contacts[nearestContact];
				const double			 alongX	 = contact.toX - contact.fromX;
				const double			 alongY	 = contact.toY - contact.fromY;
				FrontierWedge			 wedge;
				wedge.side		  = i;
				wedge.fromX		  = from[0];
				wedge.fromY		  = from[1];
				wedge.toX		  = to[0];
				wedge.toY		  = to[1];
				wedge.fromVertex  = r == 0 && wedgeStart[i].direction == offsets[i];
				wedge.toVertex	  = r + 2 == rays.size() && wedgeEnd[i].direction == offsets[(i + 1) % count];
				wedge.innerFrom	  = distanceAlong(from[0], from[1], contact.fromX, contact.fromY, alongX, alongY);
				wedge.innerTo	  = distanceAlong(to[0], to[1], contact.fromX, contact.fromY, alongX, alongY);
				wedge.outerFrom	  = distanceAlong(from[0], from[1], leftX, leftY, sideX, sideY);
				wedge.outerTo	  = distanceAlong(to[0], to[1], leftX, leftY, sideX, sideY);
				wedges.push_back(wedge);
			}
		}
		if (wedges.empty()) {
			return Frontier::Covered;
		}

		// Runs of wedges that share a ray become pieces: the shape's side over the run, then back
		// along the run's rays at the nearest contact beside each, less the overlap. A ray between
		// two sides joins them only at the shape's own vertex.
		auto sharesRay = [&](const FrontierWedge& before, const FrontierWedge& after) {
			if (after.side == before.side) {
				return before.toX == after.fromX && before.toY == after.fromY;
			}
			return after.side == (before.side + 1) % count && before.toVertex && after.fromVertex;
		};
		std::size_t first = 0;
		if (sharesRay(wedges.back(), wedges.front())) {
			first = wedges.size();
			for (std::size_t w = 1; w < wedges.size(); ++w) {
				if (!sharesRay(wedges[w - 1], wedges[w])) {
					first = w;
					break;
				}
			}
			if (first == wedges.size()) {
				return whole();
			}
		}
		const double overlap = static_cast<double>(kSeenPieceOverlapMm);
		auto		 pointAt = [&](double rayX, double rayY, double distance) {
			const double length = std::hypot(rayX, rayY);
			return Vec2i64{centre.x + std::llround(rayX / length * distance), centre.y + std::llround(rayY / length * distance)};
		};
		// Where a ray from the centre first meets the shape's boundary: the side and the distance.
		auto boundaryHit = [&](double rayX, double rayY, std::size_t& side, double& distance) {
			const double length = std::hypot(rayX, rayY);
			distance			= std::numeric_limits<double>::infinity();
			for (std::size_t j = 0; j < count; ++j) {
				const double fromX	 = static_cast<double>(offsets[j].x);
				const double fromY	 = static_cast<double>(offsets[j].y);
				const double alongX	 = static_cast<double>(offsets[(j + 1) % count].x) - fromX;
				const double alongY	 = static_cast<double>(offsets[(j + 1) % count].y) - fromY;
				const double turning = crossOf(rayX / length, rayY / length, alongX, alongY);
				if (turning == 0.0) {
					continue;
				}
				const double reachAlong = crossOf(fromX, fromY, alongX, alongY) / turning;
				const double onSide		= crossOf(fromX, fromY, rayX / length, rayY / length) / turning;
				if (reachAlong > 0.0 && onSide >= 0.0 && onSide <= 1.0 && reachAlong < distance) {
					distance = reachAlong;
					side	 = j;
				}
			}
			return std::isfinite(distance);
		};
		// The nearest contact along a ray from the centre, any side's; infinite when none.
		auto nearestContactAlong = [&](double unitX, double unitY) {
			double nearestAt = std::numeric_limits<double>::infinity();
			for (const FrontierContact& contact : contacts) {
				const double alongX	 = contact.toX - contact.fromX;
				const double alongY	 = contact.toY - contact.fromY;
				const double turning = crossOf(unitX, unitY, alongX, alongY);
				if (turning == 0.0) {
					continue;
				}
				const double reachAlong = crossOf(contact.fromX, contact.fromY, alongX, alongY) / turning;
				const double onEdge		= crossOf(contact.fromX, contact.fromY, unitX, unitY) / turning;
				if (reachAlong > 0.0 && onEdge >= 0.0 && onEdge <= 1.0) {
					nearestAt = std::min(nearestAt, reachAlong);
				}
			}
			return nearestAt;
		};
		auto apartBy = [](const Vec2i64& first, const Vec2i64& second, double spacing) {
			const Vec2i64 gap = first - second;
			return static_cast<double>(gap.x) * static_cast<double>(gap.x) + static_cast<double>(gap.y) * static_cast<double>(gap.y) > spacing * spacing;
		};
		std::vector<FrontierRay>& runRays = scratch.runRays;
		std::vector<Vec2i64>		piece;
		std::vector<std::uint8_t>	pinned; // per piece vertex: one of the shape's own
		std::vector<Vec2i64>		inner;
		for (std::size_t done = 0; done < wedges.size();) {
			const std::size_t runStart	= (first + done) % wedges.size();
			std::size_t		  runLength = 1;
			while (done + runLength < wedges.size() && sharesRay(wedges[(runStart + runLength - 1) % wedges.size()], wedges[(runStart + runLength) % wedges.size()])) {
				++runLength;
			}
			const FrontierWedge& runFirst = wedges[runStart];
			const FrontierWedge& runLast	= wedges[(runStart + runLength - 1) % wedges.size()];
			double				   reachOut = 0.0;
			for (std::size_t k = 0; k < runLength; ++k) {
				const FrontierWedge& wedge = wedges[(runStart + k) % wedges.size()];
				reachOut					 = std::max({reachOut, wedge.outerFrom, wedge.outerTo});
			}
			// The run's two end rays turn outward by the overlap at its farthest reach (a slope, not an
			// angle, so no trigonometry runs), so the piece overlaps the record beside the run too
			// rather than abutting it along a ray. What it takes in there is shape the record holds.
			// A turned ray must still meet the boundary on a side at or before the run's first (or at
			// or after its last); where turning breaks that (a run round most of the shape), the run's
			// own rays serve.
			const std::size_t runSides	 = (runLast.side + count - runFirst.side) % count;
			double			  startX	 = runFirst.fromX;
			double			  startY	 = runFirst.fromY;
			double			  endX		 = runLast.toX;
			double			  endY		 = runLast.toY;
			std::size_t		  startSide	 = 0;
			std::size_t		  endSide	 = 0;
			double			  startReach = 0.0;
			double			  endReach	 = 0.0;
			bool			  found		 = false;
			for (const double slope : {overlap / std::max(reachOut, overlap), 0.0}) {
				startX = runFirst.fromX + slope * runFirst.fromY;
				startY = runFirst.fromY - slope * runFirst.fromX;
				endX   = runLast.toX - slope * runLast.toY;
				endY   = runLast.toY + slope * runLast.toX;
				found  = boundaryHit(startX, startY, startSide, startReach) && boundaryHit(endX, endY, endSide, endReach) &&
						(runFirst.side + count - startSide) % count + runSides + (endSide + count - runLast.side) % count == (endSide + count - startSide) % count;
				if (found) {
					break;
				}
			}
			if (!found) {
				pieces.clear();
				return whole();
			}
			// The outer chain is the shape's own boundary between the turned rays, a millimetre short
			// of it where they cross a side.
			piece.clear();
			pinned.clear();
			piece.push_back(pointAt(startX, startY, startReach - 1.0));
			pinned.push_back(0);
			for (std::size_t j = (startSide + 1) % count; j != (endSide + 1) % count; j = (j + 1) % count) {
				if (piece.back() != shape[j]) {
					piece.push_back(shape[j]);
					pinned.push_back(1);
				}
			}
			const Vec2i64 outerEnd = pointAt(endX, endY, endReach - 1.0);
			if (piece.back() != outerEnd) {
				piece.push_back(outerEnd);
				pinned.push_back(0);
			}

			// The inner chain stands the overlap back from every contact point near it, along the run's
			// rays and the two turned ones: where the record's edge runs nearly along the rays, a pull
			// along the ray alone would leave the piece abutting it. So a few millimetres either way
			// do not matter on it: points closer together than kInnerPointSpacingMm are merged,
			// keeping the nearer, and points within the overlap of the centre become the centre. A run
			// under the overlap wide is fanned from the centre.
			runRays.clear();
			for (std::size_t k = 0; k <= runLength; ++k) {
				const FrontierWedge* before = k > 0 ? &wedges[(runStart + k - 1) % wedges.size()] : nullptr;
				const FrontierWedge* after  = k < runLength ? &wedges[(runStart + k) % wedges.size()] : nullptr;
				const double		   rayX	  = after != nullptr ? after->fromX : before->toX;
				const double		   rayY	  = after != nullptr ? after->fromY : before->toY;
				const double		   length = std::hypot(rayX, rayY);
				FrontierRay		   ray;
				ray.unitX  = rayX / length;
				ray.unitY  = rayY / length;
				ray.outer  = std::numeric_limits<double>::infinity();
				ray.nearer = std::numeric_limits<double>::infinity();
				if (before != nullptr) {
					ray.outer  = std::min(ray.outer, before->outerTo);
					ray.nearer = std::min(ray.nearer, before->innerTo);
				}
				if (after != nullptr) {
					ray.outer  = std::min(ray.outer, after->outerFrom);
					ray.nearer = std::min(ray.nearer, after->innerFrom);
				}
				ray.nearer = std::min(ray.nearer, ray.outer);
				runRays.push_back(ray);
			}
			const double			turn   = crossOf(runFirst.fromX, runFirst.fromY, runLast.toX, runLast.toY) /
									 (std::hypot(runFirst.fromX, runFirst.fromY) * std::hypot(runLast.toX, runLast.toY));
			const bool				sliver = turn * reachOut < overlap;
			const FrontierRay		firstRay = runRays.front();
			const FrontierRay		lastRay	 = runRays.back();
			const double			startLength = std::hypot(startX, startY);
			const double			endLength	= std::hypot(endX, endY);
			runRays.insert(runRays.begin(), FrontierRay{startX / startLength, startY / startLength, count, startReach,
														  std::min({firstRay.nearer, startReach, nearestContactAlong(startX / startLength, startY / startLength)})});
			runRays.push_back(FrontierRay{endX / endLength, endY / endLength, count, endReach, std::min({lastRay.nearer, endReach, nearestContactAlong(endX / endLength, endY / endLength)})});
			inner.clear();
			for (std::size_t k = 0; k < runRays.size() && !sliver; ++k) {
				const FrontierRay& ray   = runRays[k];
				double				 depth = ray.nearer - overlap;
				for (const FrontierRay& other : runRays) {
					const double sideways = std::abs(crossOf(ray.unitX, ray.unitY, other.unitX, other.unitY)) * other.nearer;
					const double along	  = (ray.unitX * other.unitX + ray.unitY * other.unitY) * other.nearer;
					if (along > 0.0 && sideways < overlap) {
						depth = std::min(depth, along - std::sqrt(overlap * overlap - sideways * sideways));
					}
				}
				const Vec2i64 innerPoint = depth >= overlap ? pointAt(ray.unitX, ray.unitY, depth) : centre;
				bool		  keep		 = true;
				while (keep && innerPoint != centre && !inner.empty() && inner.back() != centre) {
					const Vec2i64 lastPoint = inner.back() - centre;
					const Vec2i64 next		= innerPoint - centre;
					if (cross(lastPoint, next).sign() > 0 && apartBy(inner.back(), innerPoint, kInnerPointSpacingMm)) {
						break;
					}
					if (dot(next, next) < dot(lastPoint, lastPoint)) {
						inner.pop_back();
					} else {
						keep = false;
					}
				}
				if (keep && (inner.empty() || inner.back() != innerPoint)) {
					inner.push_back(innerPoint);
				}
			}
			if (inner.empty()) {
				inner.push_back(centre);
			}
			// Back to the centre more than once would pinch the piece there; everything between is
			// nearer the centre than the record's edge, so it goes too.
			const auto firstCentre = std::find(inner.begin(), inner.end(), centre);
			const auto lastCentre  = std::find(inner.rbegin(), inner.rend(), centre).base();
			if (firstCentre != inner.end() && firstCentre + 1 < lastCentre) {
				inner.erase(firstCentre + 1, lastCentre);
			}
			for (std::size_t k = inner.size(); k-- > 0;) {
				if (piece.back() != inner[k]) {
					piece.push_back(inner[k]);
					pinned.push_back(0);
				}
			}
			if (piece.size() >= 2 && piece.front() == piece.back()) {
				piece.pop_back();
				pinned.pop_back();
			}
			// No side shorter than the patch's snap reach: of two vertices that close, one that is not
			// the shape's own goes (an end ray's crossing beside a corner, or an inner point).
			for (bool merged = true; merged && piece.size() > 3;) {
				merged = false;
				for (std::size_t i = 0; i < piece.size(); ++i) {
					const std::size_t next = (i + 1) % piece.size();
					if (apartBy(piece[i], piece[next], static_cast<double>(kOffGroundReachMm))) {
						continue;
					}
					const std::size_t drop = pinned[next] == 0 ? next : (pinned[i] == 0 ? i : piece.size());
					if (drop < piece.size()) {
						piece.erase(piece.begin() + static_cast<std::ptrdiff_t>(drop));
						pinned.erase(pinned.begin() + static_cast<std::ptrdiff_t>(drop));
						merged = true;
						break;
					}
				}
			}
			if (piece.size() < 3 || !isSimple(piece).pass) {
				pieces.clear();
				return whole();
			}
			pieces.push_back(piece);
			done += runLength;
		}
		return pieces.empty() ? Frontier::Covered : Frontier::Pieces;
	}


	// The record: each shape's frontier taken with frontierOf, and its pieces (or the
	// whole shape) each registered in a patch of its own, as player-visibility.md 3.3 has it. K for
	// a ring is the registered rings whose boxes meet its box: what groundCovers and the patch read
	// there. Where `exact` is set, groundCovers runs beside it on every shape, timed, to count where
	// the two differ: a shape the ground holds exactly must always be covered (exactNotCovered
	// stays 0), and coveredNotExact counts the shapes the tolerance let through.
	struct SeenRecord {
		PatchableMesh							 mesh;
		FrontierScratch						 scratch;
		std::vector<std::vector<Vec2i64>>		 pieces;
		bool									 exact	   = true;
		bool									 keepRings = false;
		std::vector<std::pair<Vec2i64, Vec2i64>> boxes;
		std::vector<std::vector<Vec2i64>>		 rings; // registered, when keepRings
		std::vector<double>						 patchMs; // per registered ring
		std::vector<double>						 coveredUs; // frontierOf
		std::vector<double>						 groundCoversUs;
		std::vector<double>						 coversK;
		std::vector<double>						 ringK; // K where each ring registered
		std::vector<double>						 ringArea; // square metres
		std::vector<double>						 piecesPerShape; // shapes that gave pieces
		std::vector<double>						 passes;
		int										 exactNotCovered = 0;
		int										 coveredNotExact = 0;
		int										 wholeShapes	 = 0;
		int										 wholeOnRecord	 = 0; // whole though the centre was on the record
		PatchReport								 last;
		int										 dropped = 0;
		std::int64_t							 unread	 = 0;

		double ringsOver(const Vec2i64& low, const Vec2i64& high) const {
			double count = 0.0;
			for (const auto& [otherLow, otherHigh] : boxes) {
				count += otherLow.x <= high.x && low.x <= otherHigh.x && otherLow.y <= high.y && low.y <= otherHigh.y ? 1.0 : 0.0;
			}
			return count;
		}

		bool commit(const std::vector<Vec2i64>& ring, const Vec2i64& centre) {
			Vec2i64 low;
			Vec2i64 high;
			boxOf(ring, low, high);
			if (exact) {
				coversK.push_back(ringsOver(low, high));
			}
			Frontier frontier = Frontier::Covered;
			coveredUs.push_back(secondsOf([&] { frontier = frontierOf(mesh, ring, centre, scratch, pieces); }) * 1e6);
			const bool covered = frontier == Frontier::Covered;
			if (exact) {
				bool held = false;
				groundCoversUs.push_back(secondsOf([&] { held = mesh.groundCovers(ring); }) * 1e6);
				exactNotCovered += held && !covered ? 1 : 0;
				coveredNotExact += covered && !held ? 1 : 0;
			}
			if (covered) {
				++dropped;
				return false;
			}
			if (frontier == Frontier::Whole) {
				++wholeShapes;
				wholeOnRecord += mesh.locate(centre) >= 0 ? 1 : 0;
			} else {
				piecesPerShape.push_back(static_cast<double>(pieces.size()));
			}
			for (const std::vector<Vec2i64>& piece : pieces) {
				Vec2i64 pieceLow;
				Vec2i64 pieceHigh;
				boxOf(piece, pieceLow, pieceHigh);
				ringK.push_back(ringsOver(pieceLow, pieceHigh));
				ringArea.push_back(std::abs(geometry::signedAreaDoubled(piece).toDouble()) / 2e6);
				PatchEdits edits;
				edits.adds.push_back(NavInputPolygon{piece, false, kSeenProvenance});
				patchMs.push_back(secondsOf([&] { last = mesh.applyPatch(edits); }) * 1e3);
				passes.push_back(last.passes);
				unread += last.unreadCovers;
				boxes.push_back({pieceLow, pieceHigh});
				if (keepRings) {
					rings.push_back(piece);
				}
			}
			return true;
		}

		std::size_t registered() const { return boxes.size(); }

		// Whether a mesh built from every registered ring in one patch has the same live
		// vertices and the same boundary.
		bool equalsFreshBuild() const {
			PatchableMesh fresh;
			PatchEdits	  edits;
			for (const std::vector<Vec2i64>& ring : rings) {
				edits.adds.push_back(NavInputPolygon{ring, false, kSeenProvenance});
			}
			fresh.applyPatch(edits);
			return liveVertices(fresh) == liveVertices(mesh) && boundaryOf(fresh) == boundaryOf(mesh);
		}
	};

	struct LocateCost {
		double meanNs  = 0.0;
		double p99Ns   = 0.0;
		double offMesh = 0.0;
	};

	LocateCost locateCost(const PatchableMesh& mesh, const std::vector<Vec2i64>& points) {
		constexpr int		kRepeats = 16;
		std::vector<double> nanos;
		LocateCost			out;
		for (const Vec2i64& point : points) {
			std::int32_t triangle = -1;
			const double seconds  = secondsOf([&] {
				 for (int repeat = 0; repeat < kRepeats; ++repeat) {
					 triangle = mesh.locate(point);
					 benchmark::DoNotOptimize(triangle);
				 }
			 });
			out.offMesh += triangle < 0 ? 1.0 : 0.0;
			nanos.push_back(seconds * 1e9 / kRepeats);
		}
		out.meanNs = meanOf(nanos);
		out.p99Ns  = percentileOf(nanos, 0.99);
		return out;
	}

	// Points within 25 m of a walked path, on a random leg.
	std::vector<Vec2i64> pointsNear(const std::vector<Vec2i64>& path, std::mt19937& rng, std::size_t count) {
		std::vector<Vec2i64> out;
		while (out.size() < count) {
			const std::size_t  leg		= rng() % (path.size() - 1);
			const double	   fraction = static_cast<double>(rng() % 1001) / 1000.0;
			const std::int64_t offsetX	= static_cast<std::int64_t>(rng() % 50001) - 25000;
			const std::int64_t offsetY	= static_cast<std::int64_t>(rng() % 50001) - 25000;
			const Vec2i64&	   first	= path[leg];
			const Vec2i64&	   second	= path[leg + 1];
			out.push_back({first.x + std::llround(fraction * static_cast<double>(second.x - first.x)) + offsetX,
						   first.y + std::llround(fraction * static_cast<double>(second.y - first.y)) + offsetY});
		}
		return out;
	}

	// An explorer's walk eastward: legs of `legMm`, each turning up to 30 degrees from the
	// last and kept within 60 degrees of east, so the walk keeps reaching new ground.
	std::vector<Vec2i64> exploreWalk(std::int64_t legMm, std::int64_t totalMm, std::uint32_t seed) {
		std::mt19937		 rng(seed);
		std::vector<Vec2i64> out{{0, 0}};
		double				 heading = 0.0;
		for (std::int64_t distance = 0; distance < totalMm; distance += legMm) {
			const double turn = (static_cast<double>(rng() % 1201) - 600.0) / 600.0 * kPi / 6.0;
			heading			  = std::clamp(heading + turn, -kPi / 3.0, kPi / 3.0);
			out.push_back(stepFrom(out.back(), heading, static_cast<double>(legMm)));
		}
		return out;
	}

	void printTimes(const char* what, const std::vector<double>& values, const char* unit) {
		std::fprintf(stderr, "  %s %s mean %.3f p99 %.3f max %.3f (n %zu)\n", what, unit, meanOf(values), percentileOf(values, 0.99), maxOf(values), values.size());
	}

	void printCovered(const SeenRecord& rec) {
		printTimes("frontierOf", rec.coveredUs, "us");
		std::fprintf(stderr, "  whole shapes %d (%d with the centre on the record); pieces per shape that gave pieces mean %.2f max %.0f\n", rec.wholeShapes,
					 rec.wholeOnRecord, meanOf(rec.piecesPerShape), maxOf(rec.piecesPerShape));
		if (rec.exact) {
			printTimes("groundCovers", rec.groundCoversUs, "us");
			std::fprintf(stderr, "  held exactly but not covered %d; covered within the tolerance but not exactly %d\n", rec.exactNotCovered, rec.coveredNotExact);
		}
	}

	// --- Exploration: legs of state.range(0) m over state.range(1) km --------------------

	void BM_SeenExplore(benchmark::State& state) {
		const std::int64_t legMm   = state.range(0) * 1000;
		const std::int64_t totalMm = state.range(1) * 1000000;
		for (auto _ : state) {
			const std::vector<Vec2i64> path = exploreWalk(legMm, totalMm, 7);
			SeenRecord				   rec;
			for (std::size_t i = 1; i < path.size(); ++i) {
				rec.commit(legRing(path[i - 1], path[i]), path[i - 1]);
			}
			const double	   kilometres = static_cast<double>(totalMm) / 1e6;
			const Bytes		   bytes	  = bytesOf(rec.mesh);
			const auto		   verts	  = static_cast<std::int64_t>(liveVertices(rec.mesh).size());
			std::mt19937	   rng(11);
			const LocateCost   locate = locateCost(rec.mesh, pointsNear(path, rng, 10000));
			const EdgeWalk	   edge	  = walkBoundary(rec.mesh);
			const std::string  valid  = rec.mesh.validate();
			std::fprintf(stderr,
						 "[seen explore] legs %lld m, %.0f km: %zu legs, %zu rings registered, %d dropped as covered\n"
						 "  registry %.0f KB (%.1f KB/km), triangles %.0f KB (%.1f KB/km); %d live triangles, %lld live vertices\n",
						 static_cast<long long>(legMm / 1000), kilometres, path.size() - 1, rec.registered(), rec.dropped, bytes.registry / 1024.0,
						 bytes.registry / 1024.0 / kilometres, bytes.triangles / 1024.0, bytes.triangles / 1024.0 / kilometres, rec.mesh.liveTriangleCount(),
						 static_cast<long long>(verts));
			printTimes("patch", rec.patchMs, "ms");
			printCovered(rec);
			std::fprintf(stderr, "  K mean %.1f max %.0f; passes mean %.2f max %.0f; unreadCovers %lld\n", meanOf(rec.coversK), maxOf(rec.coversK),
						 meanOf(rec.passes), maxOf(rec.passes), static_cast<long long>(rec.unread));
			std::fprintf(stderr, "  locate ns mean %.0f p99 %.0f, off mesh %.0f of 10000; boundary walk %.0f us for %zu edges; validate %s\n", locate.meanNs,
						 locate.p99Ns, locate.offMesh, edge.micros, edge.edges, valid.empty() ? "clean" : valid.c_str());
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["rings"]		  = static_cast<double>(rec.registered());
			state.counters["regKBPerKm"]  = bytes.registry / 1024.0 / kilometres;
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["coveredUs"]	  = meanOf(rec.coveredUs);
			state.counters["groundUs"]	  = meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]	  = rec.exactNotCovered;
			state.counters["tris"]		  = rec.mesh.liveTriangleCount();
			state.counters["locateNs"]	  = locate.meanNs;
		}
	}

	// --- Covered test against K ------------------------------------------------------
	//
	// state.range(0) legs of 500 m run out from one hub in directions spread round the
	// circle in bit-reversed order, so every one reaches new ground and all overlap at the
	// hub. Then 1,000 short legs (2 to 15 m) within 5 m of the hub, every one covered.

	std::uint32_t bitReversed(std::uint32_t i, int bits) {
		std::uint32_t out = 0;
		for (int bit = 0; bit < bits; ++bit) {
			out = (out << 1) | ((i >> bit) & 1U);
		}
		return out;
	}

	void BM_SeenCoveredVsK(benchmark::State& state) {
		const auto count = static_cast<std::uint32_t>(state.range(0));
		int		   bits	 = 0;
		while ((1U << bits) < count) {
			++bits;
		}
		for (auto _ : state) {
			SeenRecord rec;
			for (std::uint32_t i = 0; i < count; ++i) {
				const double heading = 2.0 * kPi * static_cast<double>(bitReversed(i, bits)) / static_cast<double>(1U << bits);
				rec.commit(legRing({0, 0}, stepFrom({0, 0}, heading, 500000.0)), {0, 0});
			}
			std::mt19937		rng(5);
			std::vector<double> groundUs;
			std::vector<double> coveredUs;
			std::vector<double> ringCounts;
			int					held	= 0;
			int					covered = 0;
			for (int i = 0; i < 1000; ++i) {
				const std::int64_t		   x	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const std::int64_t		   y	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const Vec2i64			   first{x, y};
				const double			   heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double			   length  = 2000.0 + static_cast<double>(rng() % 13001);
				const std::vector<Vec2i64> ring	   = legRing(first, stepFrom(first, heading, length));
				Vec2i64					   low;
				Vec2i64					   high;
				boxOf(ring, low, high);
				ringCounts.push_back(rec.ringsOver(low, high));
				bool probeHeld = false;
				groundUs.push_back(secondsOf([&] { probeHeld = rec.mesh.groundCovers(ring); }) * 1e6);
				bool probeCovered = false;
				coveredUs.push_back(secondsOf([&] { probeCovered = frontierOf(rec.mesh, ring, first, rec.scratch, rec.pieces) == Frontier::Covered; }) * 1e6);
				held += probeHeld ? 1 : 0;
				covered += probeCovered ? 1 : 0;
			}
			std::fprintf(stderr,
						 "[seen covered vs K] %u spokes of 500 m: %zu registered; probes held %d and covered %d of 1000, K mean %.1f\n"
						 "  groundCovers us mean %.2f p99 %.2f; sightShapeCovered us mean %.2f p99 %.2f max %.2f\n",
						 count, rec.registered(), held, covered, meanOf(ringCounts), meanOf(groundUs), percentileOf(groundUs, 0.99), meanOf(coveredUs),
						 percentileOf(coveredUs, 0.99), maxOf(coveredUs));
			state.SetIterationTime(meanOf(coveredUs) / 1e6);
			state.counters["K"]			   = meanOf(ringCounts);
			state.counters["groundUs"]	   = meanOf(groundUs);
			state.counters["coveredUs"]	   = meanOf(coveredUs);
			state.counters["coveredP99Us"] = percentileOf(coveredUs, 0.99);
		}
	}

	// --- Wandering inside seen ground ------------------------------------------------
	//
	// A region about 300 m across seen by a sweep (7 rows 40 m apart, 240 m long), then
	// 5,000 legs of 2 to 15 m at random headings by a walker kept in the sweep's box.

	void BM_SeenWander(benchmark::State& state) {
		constexpr std::int64_t kHalf = 120000;
		for (auto _ : state) {
			SeenRecord			 rec;
			std::vector<Vec2i64> sweep;
			bool				 east = true;
			for (std::int64_t y = -kHalf; y <= kHalf; y += 40000) {
				sweep.push_back({east ? -kHalf : kHalf, y});
				sweep.push_back({east ? kHalf : -kHalf, y});
				east = !east;
			}
			for (std::size_t i = 1; i < sweep.size(); ++i) {
				rec.commit(legRing(sweep[i - 1], sweep[i]), sweep[i - 1]);
			}
			const std::size_t seedRings = rec.registered();
			rec.patchMs.clear();
			rec.coveredUs.clear();
			rec.groundCoversUs.clear();
			rec.coversK.clear();
			rec.dropped = 0;

			std::mt19937			 rng(3);
			Vec2i64					 position{0, 0};
			std::vector<std::size_t> registeredAt; // after each 1,000 legs
			for (int i = 0; i < 5000; ++i) {
				const double heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
				Vec2i64		 next	 = stepFrom(position, heading, length);
				next.x				 = next.x > kHalf ? 2 * kHalf - next.x : (next.x < -kHalf ? -2 * kHalf - next.x : next.x);
				next.y				 = next.y > kHalf ? 2 * kHalf - next.y : (next.y < -kHalf ? -2 * kHalf - next.y : next.y);
				rec.commit(legRing(position, next), position);
				position = next;
				if ((i + 1) % 1000 == 0) {
					registeredAt.push_back(rec.registered());
				}
			}
			std::fprintf(stderr, "[seen wander] sweep registered %zu rings; 5000 legs: %d covered, %zu registered; registry after each 1000 legs:", seedRings,
						 rec.dropped, rec.registered() - seedRings);
			for (const std::size_t registered : registeredAt) {
				std::fprintf(stderr, " %zu", registered);
			}
			std::fprintf(stderr, "\n");
			printCovered(rec);
			printTimes("patch (registered legs)", rec.patchMs, "ms");
			std::fprintf(stderr, "  K mean %.1f max %.0f\n", meanOf(rec.coversK), maxOf(rec.coversK));
			state.SetIterationTime(sumOf(rec.coveredUs) / 1e6);
			state.counters["covered"]	   = rec.dropped;
			state.counters["registered"]   = static_cast<double>(rec.registered() - seedRings);
			state.counters["coveredUs"]	   = meanOf(rec.coveredUs);
			state.counters["coveredP99Us"] = percentileOf(rec.coveredUs, 0.99);
			state.counters["groundUs"]	   = meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]	   = rec.exactNotCovered;
		}
	}

	// --- Re-walks: a leg walked again a few state.range(0) mm to either side ------------
	//
	// A 100 m leg at 17.3 degrees, then 40 re-walks offset alternately left and right by
	// 1 to 40 times the unit, each with its far end 3 mm further over than its near end:
	// near-coincident, nearly parallel edges.

	void BM_SeenRewalk(benchmark::State& state) {
		const std::int64_t unit = state.range(0);
		for (auto _ : state) {
			SeenRecord	  rec;
			const double  heading = 17.3 * kPi / 180.0;
			const Vec2i64 origin{37123456, -18456789};
			const Vec2i64 end	  = stepFrom(origin, heading, 100000.0);
			const double  normalX = -std::sin(heading);
			const double  normalY = std::cos(heading);
			rec.commit(legRing(origin, end), origin);
			for (std::int64_t i = 1; i <= 40; ++i) {
				const double  off = static_cast<double>((i % 2 == 0 ? 1 : -1) * i * unit);
				const Vec2i64 first{origin.x + std::llround(normalX * off), origin.y + std::llround(normalY * off)};
				const Vec2i64 second{end.x + std::llround(normalX * (off + 3.0)), end.y + std::llround(normalY * (off + 3.0))};
				rec.commit(legRing(first, second), first);
			}
			const std::string valid = rec.mesh.validate();
			std::fprintf(stderr, "[seen rewalk] offsets of %lld mm: %zu of 41 registered; %d live triangles; unreadCovers %lld; validate %s\n",
						 static_cast<long long>(unit), rec.registered(), rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread),
						 valid.empty() ? "clean" : valid.c_str());
			printTimes("patch", rec.patchMs, "ms");
			printCovered(rec);
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["registered"]  = static_cast<double>(rec.registered());
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["unread"]	  = static_cast<double>(rec.unread);
			state.counters["valid"]		  = valid.empty() ? 1.0 : 0.0;
		}
	}

	// --- Indoors: visibility polygons at a doorway -----------------------------------
	//
	// computeVisibilityPolygon at 30 m from viewpoints 0.5 m apart walking past a 1 m
	// doorway in a 0.2 m wall, along one jamb's face, and through it twice (the scene of
	// the PatchableMesh doorway test), each committed as its own ring with its viewpoint as
	// the centre.

	void BM_SeenDoorway(benchmark::State& state) {
		std::vector<geometry::OccluderSegment> walls;
		for (const auto& [x0, x1] : {std::pair<std::int64_t, std::int64_t>{-20000, -500}, std::pair<std::int64_t, std::int64_t>{500, 20000}}) {
			walls.push_back({{x0, 0}, {x1, 0}});
			walls.push_back({{x1, 0}, {x1, 200}});
			walls.push_back({{x1, 200}, {x0, 200}});
			walls.push_back({{x0, 200}, {x0, 0}});
		}
		std::vector<Vec2i64> viewpoints;
		for (std::int64_t x = -10000; x <= 10000; x += 500) {
			viewpoints.push_back({x, -3000});
		}
		for (std::int64_t y = -8000; y <= -500; y += 500) {
			viewpoints.push_back({500, y});
		}
		for (std::int64_t y = -8000; y <= 8000; y += 500) {
			viewpoints.push_back({0, y});
		}
		for (std::int64_t k = -17; k <= 17; ++k) {
			viewpoints.push_back({k * 350, k * 350 + 100});
		}
		for (auto _ : state) {
			SeenRecord rec;
			rec.keepRings = true;
			std::vector<double> buildUs;
			std::vector<double> simpleUs;
			std::vector<double> sizes;
			int					notSimple = 0;
			for (const Vec2i64& point : viewpoints) {
				geometry::Ring ring;
				buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(point, kSightMm, walls); }) * 1e6);
				sizes.push_back(static_cast<double>(ring.size()));
				bool simple = false;
				simpleUs.push_back(secondsOf([&] { simple = geometry::simpleAround(ring, point); }) * 1e6);
				if (!simple) {
					++notSimple;
					continue;
				}
				rec.commit(ring, point);
			}
			const std::string valid = rec.mesh.validate();
			const bool		  fresh = rec.equalsFreshBuild();
			std::fprintf(stderr,
						 "[seen doorway] %zu viewpoints: %zu registered, %d covered, %d not simple; ring vertices mean %.0f max %.0f; %d live triangles; "
						 "unreadCovers %lld; validate %s; equals a fresh build %s\n",
						 viewpoints.size(), rec.registered(), rec.dropped, notSimple, meanOf(sizes), maxOf(sizes), rec.mesh.liveTriangleCount(),
						 static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str(), fresh ? "yes" : "NO");
			printTimes("computeVisibilityPolygon", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			printCovered(rec);
			printTimes("patch", rec.patchMs, "ms");
			std::fprintf(stderr, "  K mean %.1f max %.0f\n", meanOf(rec.coversK), maxOf(rec.coversK));
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["ringVerts"]	  = meanOf(sizes);
			state.counters["buildUs"]	  = meanOf(buildUs);
			state.counters["coveredUs"]	  = meanOf(rec.coveredUs);
			state.counters["groundUs"]	  = meanOf(rec.groundCoversUs);
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["patchMaxMs"]  = maxOf(rec.patchMs);
			state.counters["registered"]  = static_cast<double>(rec.registered());
			state.counters["fresh"]		  = fresh ? 1.0 : 0.0;
		}
	}

	// --- A base: state.range(0) colonists walking a building of eight rooms ---------------
	//
	// state.range(1): 1 when the landing polygon, centred 40 m west of the building, goes in first,
	// 0 for a record that starts empty (a base beyond the landing area). state.range(2) simulated
	// seconds, reported every state.range(3). The empty-record row takes about 12 minutes, nearly
	// all of it patches; with 20 colonists it did not reach its first simulated minute in 15.
	//
	// Rooms 4 to 8 m across in two rows, walls 0.2 m thick with a door in every inner wall and
	// in some outer ones, turned 20 degrees and 41 km from the origin. Each colonist walks at
	// 2 m/s from room to room through the doors, and now and then out of an outer door to points
	// 3 to 60 m out and back.
	// Sight runs every 0.1 s. With any of the building within 30 m (vision's rule: a wall in
	// sight) the colonist's visibility polygon is rebuilt after 0.5 m of travel and committed
	// with him as its centre; farther out he walks legs (spec 3.3), each ended when a position
	// held since it began strays more than kSlackMm off its line, committed as its capsule.

	struct Walker {
		Vec2i64				 position;
		std::vector<Vec2i64> route;
		std::size_t			 room		= 0;
		bool				 hasPolygon = false;
		Vec2i64				 builtAt;
		bool				 legOpen = false;
		Vec2i64				 legStart;
		std::vector<Vec2i64> held; // positions since the leg began
	};

	void BM_SeenBase(benchmark::State& state) {
		const auto walkers = static_cast<std::size_t>(state.range(0));
		const bool landed  = state.range(1) != 0;
		for (auto _ : state) {
			std::mt19937					   rng(31);
			const geometry::buildings::Building building =
				geometry::buildings::makeBuilding({5000, 8000, 4000, 6500}, {6000, 4500}, 20.0 * kPi / 180.0, {37123456, -18456789}, 0.4, rng);
			std::vector<std::size_t> outerDoors;
			for (std::size_t i = 0; i < building.doors.size(); ++i) {
				outerDoors.push_back(i);
			}
			outerDoors.erase(std::remove_if(outerDoors.begin(), outerDoors.end(),
											[&](std::size_t door) { return building.doors[door].other != geometry::buildings::kOutside; }),
							 outerDoors.end());
			const std::size_t rooms = building.roomCentres.size();

			// Doors in and out of each room, and the route between two rooms by breadth-first search.
			std::vector<std::vector<std::size_t>> doorsOf(rooms);
			for (std::size_t i = 0; i < building.doors.size(); ++i) {
				doorsOf[building.doors[i].room].push_back(i);
				if (building.doors[i].other != geometry::buildings::kOutside) {
					doorsOf[building.doors[i].other].push_back(i);
				}
			}
			auto routeBetween = [&](std::size_t from, std::size_t to, std::vector<Vec2i64>& route) {
				std::vector<std::size_t> cameBy(rooms, building.doors.size());
				std::vector<std::size_t> queue{from};
				std::vector<std::uint8_t> reached(rooms, 0);
				reached[from] = 1;
				for (std::size_t head = 0; head < queue.size(); ++head) {
					for (const std::size_t door : doorsOf[queue[head]]) {
						const geometry::buildings::Door& through = building.doors[door];
						if (through.other == geometry::buildings::kOutside) {
							continue;
						}
						const std::size_t next = through.room == queue[head] ? through.other : through.room;
						if (reached[next] == 0) {
							reached[next] = 1;
							cameBy[next]  = door;
							queue.push_back(next);
						}
					}
				}
				std::vector<Vec2i64> backwards;
				for (std::size_t room = to; room != from;) {
					const geometry::buildings::Door& door = building.doors[cameBy[room]];
					backwards.push_back(building.roomCentres[room]);
					backwards.push_back(door.centre);
					room = door.room == room ? door.other : door.room;
				}
				route.insert(route.end(), backwards.rbegin(), backwards.rend());
			};

			SeenRecord rec;
			rec.exact = false;
			if (landed) {
				const Vec2i64 drop = building.roomCentres.front() + Vec2i64{-40000, 0};
				rec.commit(geometry::sightPolygon(drop, kLandingMm), drop);
			}
			const std::size_t landingRings = rec.registered();
			rec.coveredUs.clear();
			rec.patchMs.clear();

			std::vector<Walker> crew(walkers);
			for (std::size_t i = 0; i < walkers; ++i) {
				crew[i].room	 = i % rooms;
				crew[i].position = building.roomCentres[crew[i].room];
			}
			std::vector<double>		 polygonCoveredUs;
			std::vector<double>		 legCoveredUs;
			std::vector<double>		 polygonPatchMs;
			std::vector<double>		 legPatchMs;
			std::vector<double>		 buildUs;
			std::vector<double>		 simpleUs;
			std::vector<double>		 vertexCounts;
			int						 polygons		  = 0;
			int						 polygonsCovered  = 0;
			int						 legs			  = 0;
			int						 legsCovered	  = 0;
			int						 notSimple		  = 0;
			std::vector<std::size_t> registeredByPeriod;
			std::vector<double>		 bytesByPeriod;
			std::vector<int>		 trianglesByPeriod;

			// Each registered ring's area in square metres, bucketed: under 0.01 (a sliver), 0.1, 1
			// and 10, and the rest; K where each registered; and every 25th polygon, kept for the
			// oracle at the end.
			std::array<int, 5>				  gainBuckets{};
			std::vector<double>				  registeredK;
			std::vector<double>				  sliverPatchMs;
			std::vector<std::vector<Vec2i64>> sampled;
			auto commitShape = [&](const std::vector<Vec2i64>& shape, const Vec2i64& centre, bool polygon) {
				const std::size_t before = rec.patchMs.size();
				Vec2i64			  low;
				Vec2i64			  high;
				boxOf(shape, low, high);
				const bool added = rec.commit(shape, centre);
				(polygon ? polygonCoveredUs : legCoveredUs).push_back(rec.coveredUs.back());
				for (std::size_t ring = before; ring < rec.patchMs.size(); ++ring) {
					(polygon ? polygonPatchMs : legPatchMs).push_back(rec.patchMs[ring]);
					registeredK.push_back(rec.ringK[ring]);
					const double area = rec.ringArea[ring];
					++gainBuckets[area < 0.01 ? 0 : (area < 0.1 ? 1 : (area < 1.0 ? 2 : (area < 10.0 ? 3 : 4)))];
					if (area < 0.01) {
						sliverPatchMs.push_back(rec.patchMs[ring]);
					}
				}
				if (polygon && polygons % 25 == 0 && sampled.size() < 2000) {
					sampled.push_back(shape);
				}
				if (polygon) {
					++polygons;
					polygonsCovered += added ? 0 : 1;
				} else {
					++legs;
					legsCovered += added ? 0 : 1;
				}
			};
			auto closeLeg = [&](Walker& walker, const Vec2i64& end) {
				if (walker.legOpen && end != walker.legStart) {
					commitShape(legRing(walker.legStart, end), walker.legStart, false);
				}
				walker.legOpen = false;
				walker.held.clear();
			};
			auto planNext = [&](Walker& walker) {
				if (rng() % 4 == 0 && !outerDoors.empty()) {
					const geometry::buildings::Door& door = building.doors[outerDoors[rng() % outerDoors.size()]];
					routeBetween(walker.room, door.room, walker.route);
					walker.route.push_back(door.centre);
					walker.route.push_back(door.outward);
					const double outX	  = static_cast<double>(door.outward.x - door.centre.x) / 1000.0;
					const double outY	  = static_cast<double>(door.outward.y - door.centre.y) / 1000.0;
					const int	 stops	  = 1 + static_cast<int>(rng() % 3);
					for (int stop = 0; stop < stops; ++stop) {
						const double out	 = 3000.0 + static_cast<double>(rng() % 57001);
						const double lateral = static_cast<double>(rng() % 40001) - 20000.0;
						walker.route.push_back({door.centre.x + std::llround(outX * out - outY * lateral), door.centre.y + std::llround(outY * out + outX * lateral)});
					}
					walker.route.push_back(door.outward);
					walker.route.push_back(door.centre);
					walker.route.push_back(building.roomCentres[door.room]);
					walker.room = door.room;
				} else {
					const std::size_t target = rng() % rooms;
					routeBetween(walker.room, target, walker.route);
					const std::int64_t offsetX = static_cast<std::int64_t>(rng() % 2001) - 1000;
					const std::int64_t offsetY = static_cast<std::int64_t>(rng() % 2001) - 1000;
					walker.route.push_back(building.roomCentres[target] + Vec2i64{offsetX, offsetY});
					walker.room = target;
				}
			};

			constexpr double kStepSeconds = 0.1;
			const int		 periodSeconds = static_cast<int>(state.range(3));
			const int		 periods	   = static_cast<int>(state.range(2)) / periodSeconds;
			const double	 stepMm		  = kWalkMps * 1000.0 * kStepSeconds;
			const int		 stepsPerPeriod = static_cast<int>(periodSeconds / kStepSeconds + 0.5);
			for (int period = 0; period < periods; ++period) {
				for (int step = 0; step < stepsPerPeriod; ++step) {
					for (Walker& walker : crew) {
						double remaining = stepMm;
						while (remaining > 0.0) {
							if (walker.route.empty()) {
								planNext(walker);
							}
							const Vec2i64 target   = walker.route.front();
							const double  toX	   = static_cast<double>(target.x - walker.position.x);
							const double  toY	   = static_cast<double>(target.y - walker.position.y);
							const double  distance = std::sqrt(toX * toX + toY * toY);
							if (distance <= remaining) {
								walker.position = target;
								walker.route.erase(walker.route.begin());
								remaining -= distance;
							} else {
								walker.position = {walker.position.x + std::llround(toX / distance * remaining), walker.position.y + std::llround(toY / distance * remaining)};
								remaining		= 0.0;
							}
						}
						const bool indoors = building.distanceTo(walker.position) <= static_cast<double>(kSightMm);
						if (indoors) {
							if (walker.legOpen) {
								closeLeg(walker, walker.held.empty() ? walker.position : walker.held.back());
							}
							const double movedX = static_cast<double>(walker.position.x - walker.builtAt.x);
							const double movedY = static_cast<double>(walker.position.y - walker.builtAt.y);
							if (walker.hasPolygon && movedX * movedX + movedY * movedY <= 500.0 * 500.0) {
								continue;
							}
							walker.hasPolygon = true;
							walker.builtAt	  = walker.position;
							geometry::Ring ring;
							buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(walker.position, kSightMm, building.walls); }) * 1e6);
							vertexCounts.push_back(static_cast<double>(ring.size()));
							bool simple = false;
							simpleUs.push_back(secondsOf([&] { simple = geometry::simpleAround(ring, walker.position); }) * 1e6);
							if (!simple) {
								++notSimple;
								continue;
							}
							commitShape(ring, walker.position, true);
							continue;
						}
						walker.hasPolygon = false;
						if (!walker.legOpen) {
							walker.legOpen	= true;
							walker.legStart = walker.position;
							walker.held.clear();
							continue;
						}
						// The leg ends where a position held since it began strays off the line
						// from its start to here; it is committed up to the last position before.
						const double lineX	= static_cast<double>(walker.position.x - walker.legStart.x);
						const double lineY	= static_cast<double>(walker.position.y - walker.legStart.y);
						const double length = std::sqrt(lineX * lineX + lineY * lineY);
						bool		 strayed = false;
						for (const Vec2i64& point : walker.held) {
							const double offX = static_cast<double>(point.x - walker.legStart.x);
							const double offY = static_cast<double>(point.y - walker.legStart.y);
							if (length > 0.0 && std::abs(lineX * offY - lineY * offX) / length > static_cast<double>(kSlackMm)) {
								strayed = true;
								break;
							}
						}
						if (strayed) {
							const Vec2i64 corner = walker.held.back();
							closeLeg(walker, corner);
							walker.legOpen	= true;
							walker.legStart = corner;
						}
						walker.held.push_back(walker.position);
					}
				}
				registeredByPeriod.push_back(rec.registered() - landingRings);
				bytesByPeriod.push_back(bytesOf(rec.mesh).registry);
				trianglesByPeriod.push_back(rec.mesh.liveTriangleCount());
				std::fprintf(stderr, "  %d s: %zu rings registered, %d polygons, %d legs, %d whole; patches %.0f ms, covered tests %.0f ms so far\n",
							 (period + 1) * periodSeconds, rec.registered() - landingRings, polygons, legs, rec.wholeShapes, sumOf(polygonPatchMs) + sumOf(legPatchMs),
							 (sumOf(polygonCoveredUs) + sumOf(legCoveredUs)) / 1e3);
			}
			const std::string valid	  = rec.mesh.validate();
			// The oracle: points deeper than the tolerance inside a sampled polygon are on the record.
			std::mt19937 oracleRng(5);
			int			 oracleSamples = 0;
			int			 oracleMissing = 0;
			for (const std::vector<Vec2i64>& polygon : sampled) {
				Vec2i64 low;
				Vec2i64 high;
				boxOf(polygon, low, high);
				for (int attempt = 0; attempt < 200; ++attempt) {
					const std::int64_t x = low.x + static_cast<std::int64_t>(oracleRng() % static_cast<std::uint32_t>(high.x - low.x + 1));
					const std::int64_t y = low.y + static_cast<std::int64_t>(oracleRng() % static_cast<std::uint32_t>(high.y - low.y + 1));
					const Vec2i64	   point{x, y};
					if (geometry::pointInPolygon(point, polygon) != geometry::PointInPolygon::Inside) {
						continue;
					}
					double nearest = 1e300;
					for (std::size_t i = 0; i < polygon.size(); ++i) {
						nearest = std::min(nearest, geometry::distanceToSegment(point, polygon[i], polygon[(i + 1) % polygon.size()]));
					}
					if (nearest <= static_cast<double>(kSeenCoverToleranceMm) + 1.0) {
						continue;
					}
					++oracleSamples;
					oracleMissing += rec.mesh.locate(point) < 0 ? 1 : 0;
				}
			}
			const double unionArea = areaOf(rec.mesh);
			const double	  seconds = static_cast<double>(periodSeconds) * periods;
			const double recordMs = (sumOf(simpleUs) + sumOf(polygonCoveredUs) + sumOf(legCoveredUs)) / 1e3 + sumOf(polygonPatchMs) + sumOf(legPatchMs);
			std::fprintf(stderr,
						 "[seen base] %zu colonists, %s, %d rooms, %zu outer doors, %.0f min: %d polygons (%d covered, %.1f%%), %d legs (%d covered), %d not simple; "
						 "%zu rings registered; %d live triangles; unreadCovers %lld; validate %s\n",
						 walkers, landed ? "landed 40 m away" : "no landing", static_cast<int>(rooms), outerDoors.size(), seconds / 60.0, polygons, polygonsCovered,
						 100.0 * static_cast<double>(polygonsCovered) / std::max(1, polygons), legs, legsCovered, notSimple, (rec.registered() - landingRings),
						 rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str());
			std::fprintf(stderr, "  polygon vertices mean %.0f max %.0f; whole shapes %d (%d with the centre on the record); pieces per shape mean %.2f max %.0f\n",
						 meanOf(vertexCounts), maxOf(vertexCounts), rec.wholeShapes, rec.wholeOnRecord, meanOf(rec.piecesPerShape), maxOf(rec.piecesPerShape));
			printTimes("computeVisibilityPolygon (vision's cost)", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			printTimes("frontierOf, polygons", polygonCoveredUs, "us");
			printTimes("frontierOf, legs", legCoveredUs, "us");
			printTimes("patch per ring, from polygons", polygonPatchMs, "ms");
			printTimes("patch per ring, from legs", legPatchMs, "ms");
			printTimes("patch per sliver ring (under 0.01 m2)", sliverPatchMs, "ms");
			std::fprintf(stderr, "  K where registered mean %.0f max %.0f; ring areas: under 0.01 m2 %d, 0.01 to 0.1 %d, 0.1 to 1 %d, 1 to 10 %d, over 10 %d; union %.0f m2\n",
						 meanOf(registeredK), maxOf(registeredK), gainBuckets[0], gainBuckets[1], gainBuckets[2], gainBuckets[3], gainBuckets[4], unionArea);
			std::fprintf(stderr, "  oracle: %d points deeper than the tolerance in %zu polygons, %d off the record\n", oracleSamples, sampled.size(), oracleMissing);
			std::fprintf(stderr, "  by period of %d s: rings registered", periodSeconds);
			for (std::size_t period = 0; period < registeredByPeriod.size(); ++period) {
				std::fprintf(stderr, " %zu", registeredByPeriod[period] - (period == 0 ? 0 : registeredByPeriod[period - 1]));
			}
			std::fprintf(stderr, "; registry KB");
			for (const double bytes : bytesByPeriod) {
				std::fprintf(stderr, " %.0f", bytes / 1024.0);
			}
			std::fprintf(stderr, "; live triangles");
			for (const int triangles : trianglesByPeriod) {
				std::fprintf(stderr, " %d", triangles);
			}
			std::fprintf(stderr, "\n  record's main-thread cost %.3f ms per simulated second (simple check, covered test, patches); vision's polygons %.3f ms per second\n",
						 recordMs / seconds, sumOf(buildUs) / 1e3 / seconds);
			state.SetIterationTime(recordMs / 1e3);
			state.counters["polygons"]		   = polygons;
			state.counters["coveredShare"]	   = static_cast<double>(polygonsCovered) / std::max(1, polygons);
			state.counters["coveredUs"]		   = meanOf(polygonCoveredUs);
			state.counters["coveredP99Us"]	   = percentileOf(polygonCoveredUs, 0.99);
			state.counters["patchMeanMs"]	   = meanOf(polygonPatchMs);
			state.counters["registered"]	   = static_cast<double>(rec.registered() - landingRings);
			state.counters["lastPeriod"]	   = static_cast<double>(registeredByPeriod.back() - (registeredByPeriod.size() > 1 ? registeredByPeriod[registeredByPeriod.size() - 2] : 0));
			state.counters["msPerSimSecond"]   = recordMs / seconds;
			state.counters["notSimple"]		   = notSimple;
			state.counters["oracleMissing"]	   = oracleMissing;
			state.counters["patchMaxMs"]	   = std::max(maxOf(polygonPatchMs), maxOf(legPatchMs));
		}
	}

	// --- A camp: state.range(0) walkers wander a 150 m landing disc for an hour -----------
	//
	// The landing polygon goes in first. Each walker starts within 20 m of the drop and
	// walks legs of 2 to 15 m at random headings at 2 m/s, turning back toward the drop when
	// a leg would end more than 150 m from it; walkers take turns a minute at a time.

	void BM_SeenCamp(benchmark::State& state) {
		const auto walkers = static_cast<std::size_t>(state.range(0));
		for (auto _ : state) {
			SeenRecord rec;
			rec.commit(geometry::sightPolygon({0, 0}, kLandingMm), {0, 0});
			std::mt19937		 rng(17);
			std::vector<Vec2i64> positions(walkers);
			for (Vec2i64& point : positions) {
				const std::int64_t x = static_cast<std::int64_t>(rng() % 28001) - 14000;
				const std::int64_t y = static_cast<std::int64_t>(rng() % 28001) - 14000;
				point				 = {x, y};
			}
			const double			 perMinuteMm = kWalkMps * 60.0 * 1000.0;
			std::vector<std::size_t> byMinute;
			for (int minute = 0; minute < 60; ++minute) {
				for (Vec2i64& point : positions) {
					double walked = 0.0;
					while (walked < perMinuteMm) {
						double		 heading = static_cast<double>(rng() % 6284) / 1000.0;
						const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
						Vec2i64		 next	 = stepFrom(point, heading, length);
						if (std::hypot(static_cast<double>(next.x), static_cast<double>(next.y)) > static_cast<double>(kLandingMm)) {
							heading = std::atan2(-static_cast<double>(point.y), -static_cast<double>(point.x));
							next	= stepFrom(point, heading, length);
						}
						rec.commit(legRing(point, next), point);
						point = next;
						walked += length;
					}
				}
				byMinute.push_back(rec.registered());
			}
			const double registered = static_cast<double>(rec.registered() - 1);
			std::fprintf(stderr,
						 "[seen camp] %zu walkers, 1 h: %zu legs, %.0f registered (%.1f per walker-hour); registered by minute 10 %zu, 30 %zu, 50 %zu, 60 %zu\n",
						 walkers, rec.coveredUs.size() - 1, registered, registered / static_cast<double>(walkers), byMinute[9] - 1, byMinute[29] - 1,
						 byMinute[49] - 1, byMinute[59] - 1);
			printCovered(rec);
			printTimes("patch", rec.patchMs, "ms");
			const Bytes bytes = bytesOf(rec.mesh);
			std::fprintf(stderr, "  K mean %.1f max %.0f; registry %.0f KB, triangles %.0f KB; %d live triangles; unreadCovers %lld\n", meanOf(rec.coversK),
						 maxOf(rec.coversK), bytes.registry / 1024.0, bytes.triangles / 1024.0, rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread));
			state.SetIterationTime(sumOf(rec.coveredUs) / 1e6);
			state.counters["perWalkerHour"] = registered / static_cast<double>(walkers);
			state.counters["coveredUs"]		= meanOf(rec.coveredUs);
			state.counters["coveredP99Us"]	= percentileOf(rec.coveredUs, 0.99);
			state.counters["groundUs"]		= meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]		= rec.exactNotCovered;
			state.counters["Kmax"]			= maxOf(rec.coversK);
		}
	}

	// --- Simple rings: visibility polygons from observers in and round random buildings ----
	//
	// state.range(0) random buildings (2 to 4 by 1 to 3 rooms of 3 to 9 m, at random angles
	// and up to 40 km out), 25 observers each: on open floor, 1 to 300 mm off wall corners,
	// and in doorways. Each polygon is checked by simpleAround and by an exact test of every
	// pair of edges.

	void BM_SeenSimplicityFuzz(benchmark::State& state) {
		for (auto _ : state) {
			std::mt19937		rng(0x5EE4u);
			int					polygons	= 0;
			int					turnedAway	= 0;
			int					notSimple	= 0;
			int					disagree	= 0;
			std::vector<double> buildUs;
			std::vector<double> simpleUs;
			std::vector<double> vertexCounts;
			for (std::int64_t scene = 0; scene < state.range(0); ++scene) {
				const geometry::buildings::Building building = geometry::buildings::randomBuilding(rng);
				for (const Vec2i64& observer : geometry::buildings::observersFor(building, rng, 25)) {
					geometry::Ring ring;
					buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(observer, kSightMm, building.walls); }) * 1e6);
					vertexCounts.push_back(static_cast<double>(ring.size()));
					bool accepted = false;
					simpleUs.push_back(secondsOf([&] { accepted = geometry::simpleAround(ring, observer); }) * 1e6);
					bool simple = ring.size() >= 3;
					for (std::size_t i = 0; i < ring.size() && simple; ++i) {
						for (std::size_t j = i + 1; j < ring.size() && simple; ++j) {
							const geometry::SegmentRelation relation =
								geometry::intersectSegments(ring[i], ring[(i + 1) % ring.size()], ring[j], ring[(j + 1) % ring.size()]).relation;
							const bool adjacent = j == i + 1 || (i == 0 && j == ring.size() - 1);
							simple = relation == geometry::SegmentRelation::Disjoint || (adjacent && relation == geometry::SegmentRelation::EndpointTouch);
						}
					}
					++polygons;
					turnedAway += accepted ? 0 : 1;
					notSimple += simple ? 0 : 1;
					disagree += accepted != simple ? 1 : 0;
				}
			}
			std::fprintf(stderr, "[seen simple rings] %d polygons from %lld buildings: %d turned away by simpleAround, %d not simple by every pair, %d disagree\n",
						 polygons, static_cast<long long>(state.range(0)), turnedAway, notSimple, disagree);
			std::fprintf(stderr, "  vertices mean %.0f max %.0f\n", meanOf(vertexCounts), maxOf(vertexCounts));
			printTimes("computeVisibilityPolygon", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			state.SetIterationTime(sumOf(simpleUs) / 1e6);
			state.counters["polygons"]	 = polygons;
			state.counters["turnedAway"] = turnedAway;
			state.counters["notSimple"]	 = notSimple;
			state.counters["disagree"]	 = disagree;
		}
	}

	// --- Nav upkeep a seen record pays for nothing -------------------------------------
	//
	// The 10 km walk in 20 m legs, twice. First with widths never refreshed: per patch, the
	// component labels' update (a second MeshComponents fed the same report, the work the
	// mesh does inside applyPatch) and the door tags of every created triangle (the mesh's
	// own applyDoorTags, run again), and how far the width lists grow. Then refreshWidths
	// after every patch.

	void BM_SeenUpkeep(benchmark::State& state) {
		for (auto _ : state) {
			const std::vector<Vec2i64> path = exploreWalk(20000, 10000000, 7);
			SeenRecord				   rec;
			rec.exact = false;
			MeshComponents		labels;
			std::vector<double> labelUs;
			std::vector<double> doorUs;
			const auto			doorTags = memberOf(DoorTagsKey{});
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (!rec.commit(legRing(path[i - 1], path[i]), path[i - 1])) {
					continue;
				}
				labelUs.push_back(secondsOf([&] { labels.update(rec.mesh.view(), rec.last); }) * 1e6);
				doorUs.push_back(secondsOf([&] {
									 for (const ChangedTriangle& changed : rec.last.changes) {
										 if ((changed.why & kTriangleCreated) != 0) {
											 (rec.mesh.*doorTags)(changed.id);
										 }
									 }
								 }) *
								 1e6);
			}
			const auto&		  fresh		 = rec.mesh.*memberOf(FreshWidthsKey{});
			const ListSize	  seeds		 = widthSeedsOf(rec.mesh);
			const double	  listBytes	 = static_cast<double>(fresh.capacity() * sizeof(std::int32_t) + seeds.bytes);
			const std::size_t freshCount = fresh.size();
			const std::size_t seedCount	 = seeds.count;
			const double	  catchUpMs	 = secondsOf([&] { rec.mesh.refreshWidths(); }) * 1e3;

			SeenRecord eager;
			eager.exact = false;
			std::vector<double> refreshMs;
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (eager.commit(legRing(path[i - 1], path[i]), path[i - 1])) {
					refreshMs.push_back(secondsOf([&] { eager.mesh.refreshWidths(); }) * 1e3);
				}
			}
			std::fprintf(stderr, "[seen upkeep] 10 km in 20 m legs, %zu patches\n", rec.patchMs.size());
			printTimes("patch (labels and door tags inside)", rec.patchMs, "ms");
			printTimes("labels update", labelUs, "us");
			printTimes("door tags", doorUs, "us");
			std::fprintf(stderr, "  widths never refreshed: %zu fresh triangle ids and %zu seeds held, %.0f KB; one refresh then %.1f ms\n", freshCount, seedCount,
						 listBytes / 1024.0, catchUpMs);
			printTimes("refreshWidths after every patch", refreshMs, "ms");
			state.SetIterationTime(meanOf(labelUs) * 1e-6);
			state.counters["patchMeanMs"]	= meanOf(rec.patchMs);
			state.counters["labelMeanUs"]	= meanOf(labelUs);
			state.counters["doorMeanUs"]	= meanOf(doorUs);
			state.counters["widthListKB"]	= listBytes / 1024.0;
			state.counters["refreshMeanMs"] = meanOf(refreshMs);
		}
	}

} // namespace

BENCHMARK(BM_SeenExplore)
	->Args({20, 10})
	->Args({50, 10})
	->Args({100, 10})
	->Args({200, 10})
	->Args({20, 50})
	->Args({50, 50})
	->Args({100, 50})
	->Args({200, 50})
	->Iterations(1)
	->UseManualTime()
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenCoveredVsK)->Arg(1)->Arg(4)->Arg(16)->Arg(64)->Arg(256)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SeenWander)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenDoorway)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenBase)->Args({20, 1, 600, 60})->Args({50, 1, 600, 60})->Args({2, 0, 600, 60})->Args({20, 0, 600, 60})->Args({50, 0, 600, 60})->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenSimplicityFuzz)->Arg(400)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenRewalk)->Arg(1)->Arg(10)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenCamp)->Arg(10)->Arg(50)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenUpkeep)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
