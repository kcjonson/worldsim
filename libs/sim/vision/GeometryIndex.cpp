#include "GeometryIndex.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <construction/OpeningGeometry.h>

#include <offset/WallOffset.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace ecs {

	namespace {

		// A hit snaps to the millimetre, so one within 0.71 mm of the eye or of a ray lands on it.
		constexpr std::int64_t kHitSnapsOntoTheRayMm = 1;

		// The centerline point `alongMm` from `a` toward `b`, clamped to the segment
		// and rounded to the millimetre.
		geometry::Vec2i64 pointAlong(const geometry::Vec2i64& a, const geometry::Vec2i64& b, double lengthMm, std::int64_t alongMm) {
			const double fraction = std::clamp(static_cast<double>(alongMm) / lengthMm, 0.0, 1.0);
			return {
				a.x + std::llround(static_cast<double>(b.x - a.x) * fraction), a.y + std::llround(static_cast<double>(b.y - a.y) * fraction)
			};
		}

		// An occluder from `from` to `to` of wall `source` in body `body`, with its box.
		GeometryIndex::OccluderRecord record(construction::SegmentId source, std::size_t body, geometry::Vec2i64 from, geometry::Vec2i64 to) {
			return {.source = source,
					.seg	= {from, to},
					.body	= body,
					.low	= {std::min(from.x, to.x), std::min(from.y, to.y)},
					.high	= {std::max(from.x, to.x), std::max(from.y, to.y)}};
		}

		// The body's box, centre, diameter, and kind from its edges, and each edge's previous edge, found
		// in its own loop group (`edgeLoops`, one an edge): a rejected graph's bands share corners.
		void measure(GeometryIndex::WallBody& body, const std::vector<std::size_t>& edgeLoops, std::int64_t opaqueLeastDiameterMm) {
			std::vector<geometry::Vec2i64>											corners;
			std::map<std::pair<std::size_t, geometry::Vec2i64>, std::uint32_t> edgeEndingAt;
			corners.reserve(body.edges.size());
			body.low  = body.edges.front().a;
			body.high = body.edges.front().a;
			geometry::Vec2i64 sum{0, 0};
			for (std::uint32_t index = 0; index < body.edges.size(); ++index) {
				const geometry::Vec2i64& corner = body.edges[index].a;
				corners.push_back(corner);
				edgeEndingAt.emplace(std::pair{edgeLoops[index], body.edges[index].b}, index);
				body.low  = {std::min(body.low.x, corner.x), std::min(body.low.y, corner.y)};
				body.high = {std::max(body.high.x, corner.x), std::max(body.high.y, corner.y)};
				sum		  = sum + corner;
			}
			const auto count = static_cast<std::int64_t>(corners.size());
			body.centre		 = {sum.x / count, sum.y / count};
			body.previousEdge.resize(body.edges.size());
			for (std::uint32_t index = 0; index < body.edges.size(); ++index) {
				// Every outline loop closes, so some edge ends at each corner.
				const auto previous = edgeEndingAt.find(std::pair{edgeLoops[index], body.edges[index].a});
				assert(previous != edgeEndingAt.end());
				body.previousEdge[index] = previous != edgeEndingAt.end() ? previous->second : index;
			}
			body.diameterSquared = vision::diameterSquared(corners);
			body.diameterMm		 = vision::wholeSquareRoot(body.diameterSquared);
			body.kind			 = vision::kindOf(body.diameterSquared, opaqueLeastDiameterMm);
		}

		// How far past the face an opening's face span lies. Three roundings can each move it up to
		// 0.71 mm toward the wall: its own ends to the millimetre, the outline's corners, which set
		// the face edge, and the snapped vertices of the polygon run along that edge.
		constexpr std::int64_t kFaceSpanOutsideMm = 3;

		// The record of `opening` of type `type`, spanning `gap` on the wall from `a` to `b`,
		// `lengthMm` long and `halfThicknessMm` either side of its centerline.
		GeometryIndex::OpeningRecord openingRecord(
			construction::OpeningId		  opening,
			construction::SegmentId		  segment,
			const assets::OpeningTypeDef& type,
			geometry::Vec2i64			  a,
			geometry::Vec2i64			  b,
			double						  lengthMm,
			std::int64_t				  halfThicknessMm,
			const geometry::WallGap&	  gap
		) {
			const double alongX = static_cast<double>(b.x - a.x) / lengthMm;
			const double alongY = static_cast<double>(b.y - a.y) / lengthMm;
			const double reach	= static_cast<double>(halfThicknessMm + kFaceSpanOutsideMm);
			// The point `alongMm` along the centerline, clamped to it, `side` times reach to its left,
			// rounded once.
			const auto faceAt = [&](std::int64_t alongMm, double side) {
				const double distance = std::clamp(static_cast<double>(alongMm), 0.0, lengthMm);
				return geometry::Vec2i64{
					a.x + std::llround(alongX * distance - alongY * reach * side),
					a.y + std::llround(alongY * distance + alongX * reach * side)
				};
			};
			GeometryIndex::OpeningRecord record{
				.openingId			= opening,
				.segment			= segment,
				.type				= type.name,
				.leftFace			= {faceAt(gap.fromMm, 1.0), faceAt(gap.toMm, 1.0)},
				.rightFace			= {faceAt(gap.fromMm, -1.0), faceAt(gap.toMm, -1.0)},
				.transparentToSight = type.transparentToSight,
			};
			record.low	= record.leftFace.start;
			record.high = record.leftFace.start;
			for (const geometry::Vec2i64& corner : {record.leftFace.end, record.rightFace.start, record.rightFace.end}) {
				record.low	= {std::min(record.low.x, corner.x), std::min(record.low.y, corner.y)};
				record.high = {std::max(record.high.x, corner.x), std::max(record.high.y, corner.y)};
			}
			return record;
		}

		// Whether the two edges share an end.
		bool shareAnEnd(const geometry::OccluderSegment& first, const geometry::OccluderSegment& second) {
			return first.a == second.a || first.a == second.b || first.b == second.a || first.b == second.b;
		}

		// Marks every opaque body's occluder that touches, crosses or runs along another opaque
		// body's: any contact with an occluder of another loop group, and within one any but two edges
		// meeting end to end, how a loop runs on from an edge or passes a vertex again. Two walls of
		// one graph can still overlap where they share no vertex, since the outline cancels edges only
		// between rings meeting at one. `loop` numbers each occluder's group (one per offset graph, one
		// per wall of a rejected graph). Soft bodies never reach a visibility polygon, so they cross
		// nothing. Occluders are swept in order of their boxes' low x, and only those whose boxes
		// overlap are tested.
		void markCrossings(std::vector<GeometryIndex::OccluderRecord>& occluders, const std::vector<std::size_t>& loop,
						   const std::vector<GeometryIndex::WallBody>& bodies) {
			std::vector<std::size_t> byLowX;
			for (std::size_t index = 0; index < occluders.size(); ++index) {
				if (bodies[occluders[index].body].kind == vision::BodyKind::Opaque) {
					byLowX.push_back(index);
				}
			}
			std::ranges::sort(byLowX, {}, [&](std::size_t index) { return occluders[index].low.x; });
			std::vector<std::size_t> open;
			for (const std::size_t current : byLowX) {
				GeometryIndex::OccluderRecord& first = occluders[current];
				std::erase_if(open, [&](std::size_t other) { return occluders[other].high.x < first.low.x; });
				for (const std::size_t other : open) {
					GeometryIndex::OccluderRecord& second = occluders[other];
					if (second.high.y < first.low.y || first.high.y < second.low.y ||
						(first.crossesAnotherOccluder && second.crossesAnotherOccluder)) {
						continue;
					}
					const geometry::SegmentRelation relation =
						geometry::intersectSegments(first.seg.a, first.seg.b, second.seg.a, second.seg.b).relation;
					const bool joined = loop[other] == loop[current] && relation == geometry::SegmentRelation::EndpointTouch &&
										shareAnEnd(first.seg, second.seg);
					if (relation != geometry::SegmentRelation::Disjoint && !joined) {
						first.crossesAnotherOccluder  = true;
						second.crossesAnotherOccluder = true;
					}
				}
				open.push_back(current);
			}
		}

		// An eye against the occluder loops, one occluder at a time: the winding number round it,
		// whether it lies on an occluder, and whether it grazes one in range, within a millimetre
		// of its line, or one in range crosses another.
		//
		// computeVisibilityPolygon blocks a ray that runs along an occluder on both of its sides and
		// takes the polygon's next edge from the next direction round, which a back face's end can
		// be; a ray from under a millimetre off the line snaps its hits onto it the same way; and it
		// drops a hit that snaps onto the eye, where the back face behind it is what stops the ray.
		// Where loops cross, the faces meet without an end at the crossing, and the back faces' ends
		// are what make it a vertex. Each needs the back faces of the occluders in range.
		class EyeTally {
		  public:
			explicit EyeTally(geometry::Vec2i64 point)
				: eye(point) {}

			void add(const GeometryIndex::OccluderRecord& occluder, bool inRange) {
				// Out of range, an edge wholly above or below the eye neither holds it nor winds round it.
				if (!inRange && (occluder.high.y < eye.y || occluder.low.y > eye.y)) {
					return;
				}
				const geometry::Vec2i64& from = occluder.seg.a;
				const geometry::Vec2i64& to = occluder.seg.b;
				const geometry::Int128	 offLine = geometry::cross(to - from, eye - from);
				const int				 side = offLine.sign();
				if (inRange &&
					(occluder.crossesAnotherOccluder || geometry::Int128::compareSquareToProduct(
															offLine,
															geometry::Int128::product(kHitSnapsOntoTheRayMm, kHitSnapsOntoTheRayMm),
															geometry::dot(to - from, to - from)
														) <= 0)) {
					grazed = true;
				}
				if (side == 0 && eye.x >= std::min(from.x, to.x) && eye.x <= std::max(from.x, to.x) && eye.y >= std::min(from.y, to.y) &&
					eye.y <= std::max(from.y, to.y)) {
					onOccluder = true;
					return;
				}
				if (from.y <= eye.y) {
					if (to.y > eye.y && side > 0) {
						++winding;
					}
				} else if (to.y <= eye.y && side < 0) {
					--winding;
				}
			}

			// Nothing added after can hide the back faces again, nor take the eye off an occluder.
			[[nodiscard]] bool settled() const { return onOccluder || grazed; }

			[[nodiscard]] bool insideSolid() const { return onOccluder || winding != 0; }

			[[nodiscard]] bool backFacesHidden() const { return !grazed && !insideSolid(); }

		  private:
			geometry::Vec2i64 eye;
			int				  winding = 0;
			bool			  onOccluder = false;
			bool			  grazed	   = false;
		};

	} // namespace

	void GeometryIndex::setConstructionWorld(const construction::ConstructionWorld* world) {
		if (world == m_world) {
			return;
		}
		m_world = world;
		// Force a rebuild against the new source on the next tick: a different world
		// (or back to null) can share a version number with the old one.
		m_builtVersion = kInvalidVersion;
		// Drop the previous world's caches now so the index is inert (no stale
		// occluders) until the next rebuild -- a null world stays inert, as the
		// header promises; a non-null world repopulates on the next rebuildIfStale.
		m_occluders.clear();
		m_openings.clear();
		wallBodies.clear();
		// Clearing the occluder set is itself a change consumers must see, so move
		// the generation -- a cache keyed on it (e.g. VisionSystem's polygons) would
		// otherwise keep serving occluders from the old world after a swap to null.
		++m_generation;
	}

	void GeometryIndex::rebuildIfStale() {
		if (m_world == nullptr) {
			return;
		}
		if (m_world->version() == m_builtVersion) {
			return;
		}
		rebuild();
		m_builtVersion = m_world->version();
	}

	void GeometryIndex::rebuild() {
		++m_generation; // every actual rebuild moves the generation a cache can gate on
		m_occluders.clear();
		m_openings.clear();
		wallBodies.clear();

		const construction::ConstructionWorld& world = *m_world;

		// The built walls as the offsetter takes them, the spans their transparent
		// openings leave open, and the id of each.
		const std::unordered_map<construction::SegmentId, construction::OpeningGaps> openingsByWall =
			construction::openingGapsBySegment(world, {.builtOnly = true});
		const construction::OpeningGaps							 noOpenings;
		std::vector<geometry::WallSegment>						 walls;
		std::vector<std::vector<geometry::WallGap>> sightGaps;
		std::vector<construction::SegmentId>					 wallIds;
		for (const construction::WallSegment& segment : world.segments()) {
			if (!construction::isBuilt(segment)) {
				continue; // blueprint walls do not occlude
			}
			const construction::Vertex* v0 = world.getVertex(segment.v0);
			const construction::Vertex* v1 = world.getVertex(segment.v1);
			const std::int64_t			half = construction::halfThicknessMm(segment);
			if (v0 == nullptr || v1 == nullptr || half <= 0) {
				continue;
			}
			const geometry::Vec2i64 a		 = v0->pos;
			const geometry::Vec2i64 b		 = v1->pos;
			const double			lengthMm = std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
			if (lengthMm <= 0.0) {
				continue; // degenerate; a built zero-length wall should not exist
			}

			const auto						found	 = openingsByWall.find(segment.id);
			const construction::OpeningGaps& openings = found != openingsByWall.end() ? found->second : noOpenings;
			std::vector<geometry::WallGap>	gaps;
			for (std::size_t index = 0; index < openings.openings.size(); ++index) {
				const assets::OpeningTypeDef& type = *openings.types[index];
				const geometry::WallGap&	  gap  = openings.gaps[index];
				m_openings.push_back(openingRecord(openings.openings[index], segment.id, type, a, b, lengthMm, half, gap));
				if (type.transparentToSight) {
					gaps.push_back(gap);
				}
			}
			walls.push_back({a, b, half});
			sightGaps.push_back(std::move(gaps));
			wallIds.push_back(segment.id);
		}

		// The loop group of each occluder, for markCrossings.
		std::vector<std::size_t> loop;
		std::size_t				 loops = 0;
		for (const std::vector<std::size_t>& graph : geometry::connectedWallGraphs(walls)) {
			const std::size_t		 body = wallBodies.size();
			std::vector<std::size_t> edgeLoops;
			WallBody&		  outline = wallBodies.emplace_back();
			const auto		  add	  = [&](construction::SegmentId source, geometry::Vec2i64 from, geometry::Vec2i64 to) {
				 m_occluders.push_back(record(source, body, from, to));
				 outline.edges.push_back({from, to});
				 edgeLoops.push_back(loops);
				 loop.push_back(loops);
			};
			std::vector<geometry::WallSegment>			members;
			std::vector<std::vector<geometry::WallGap>> memberGaps;
			members.reserve(graph.size());
			memberGaps.reserve(graph.size());
			for (const std::size_t index : graph) {
				members.push_back(walls[index]);
				memberGaps.push_back(sightGaps[index]);
			}
			const geometry::WallBands bands = geometry::resolveWallBands(members, geometry::kDefaultMiterLimit);
			if (!geometry::bandsAccepted(bands)) {
				// Rejected as nav rejects it: each wall is its raw band, untrimmed, outlined as a
				// lone wall's, so its transparent openings still pass sight. The graph is still one body.
				for (const std::size_t index : graph) {
					const geometry::WallSegment& wall = walls[index];
					const geometry::WallBands	 rawBand{
						   .bands = {geometry::band(wall.a, wall.b, wall.halfThicknessMm)}, .trimAtA = {0}, .trimAtB = {0}
					   };
					for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline({wall}, {sightGaps[index]}, rawBand).outline) {
						add(wallIds[index], edge.from, edge.to);
					}
					++loops;
				}
			} else {
				for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline(members, memberGaps, bands).outline) {
					add(wallIds[graph[edge.index]], edge.from, edge.to);
				}
				++loops;
			}
			if (outline.edges.empty()) {
				wallBodies.pop_back();
				continue;
			}
			measure(outline, edgeLoops, opaqueLeastDiameterMm);
		}
		markCrossings(m_occluders, loop, wallBodies);
		// Each wall's edges together, so the structure pass is done with a wall at its first seen edge.
		std::ranges::stable_sort(m_occluders, {}, &OccluderRecord::source);
	}

	void GeometryIndex::queryOccluders(geometry::Vec2i64 center, std::int64_t radiusMm,
									   std::vector<geometry::OccluderSegment>& out) const {
		out.clear();
		if (radiusMm <= 0) {
			return; // non-positive radius sees nothing
		}
		for (const OccluderRecord& rec : m_occluders) {
			// Exact integer range test: keep an occluder if any part of it lies within
			// the sight radius of the observer.
			if (rec.within(center, radiusMm)) {
				out.push_back(rec.seg);
			}
		}
	}

	bool GeometryIndex::queryFacingOccluders(geometry::Vec2i64 eye, std::int64_t radiusMm,
											 std::vector<geometry::OccluderSegment>& out) const {
		out.clear();
		if (radiusMm <= 0) {
			return false;
		}
		const auto opaque  = [&](const OccluderRecord& rec) { return wallBodies[rec.body].kind == vision::BodyKind::Opaque; };
		const auto inRange = [&](const OccluderRecord& rec) { return opaque(rec) && rec.within(eye, radiusMm); };
		// Outdoors no occluder is in range, and the range test is all the query costs.
		const auto first = std::ranges::find_if(m_occluders, inRange);
		if (first == m_occluders.end()) {
			return false;
		}
		// The winding round the eye counts every opaque occluder, those before the first in range too.
		EyeTally tally(eye);
		for (auto occluder = m_occluders.begin(); occluder != m_occluders.end(); ++occluder) {
			if (!opaque(*occluder)) {
				continue;
			}
			const bool kept = occluder >= first && (occluder == first || inRange(*occluder));
			if (kept) {
				out.push_back(occluder->seg);
			}
			if (!tally.settled()) {
				tally.add(*occluder, kept);
			}
		}
		const bool culled = tally.backFacesHidden();
		if (culled) {
			std::erase_if(out, [&](const geometry::OccluderSegment& edge) { return facesAway(edge, eye); });
		}
		return culled;
	}

	void GeometryIndex::bodiesWithin(geometry::Vec2i64 point, std::int64_t radiusMm, std::vector<std::size_t>& out) const {
		out.clear();
		if (radiusMm < 0) {
			return;
		}
		for (std::size_t index = 0; index < wallBodies.size(); ++index) {
			if (wallBodies[index].within(point, radiusMm)) {
				out.push_back(index);
			}
		}
	}

	bool GeometryIndex::WallBody::within(geometry::Vec2i64 point, std::int64_t radiusMm) const {
		if (point.x + radiusMm < low.x || point.x - radiusMm > high.x || point.y + radiusMm < low.y || point.y - radiusMm > high.y) {
			return false;
		}
		return std::ranges::any_of(edges, [&](const geometry::OccluderSegment& edge) {
			return geometry::withinDistanceOfSegment(point, edge.a, edge.b, radiusMm);
		});
	}

	void GeometryIndex::setOpaqueLeastDiameterMm(std::int64_t leastMm) {
		if (leastMm == opaqueLeastDiameterMm) {
			return;
		}
		opaqueLeastDiameterMm = leastMm;
		m_builtVersion			= kInvalidVersion;
	}

	bool GeometryIndex::backFacesHiddenFrom(geometry::Vec2i64 eye, std::int64_t radiusMm) const {
		EyeTally tally(eye);
		for (const OccluderRecord& rec : m_occluders) {
			if (wallBodies[rec.body].kind != vision::BodyKind::Opaque) {
				continue;
			}
			tally.add(rec, rec.within(eye, radiusMm));
			if (tally.settled()) {
				return false;
			}
		}
		return tally.backFacesHidden();
	}

	bool GeometryIndex::facesAway(const geometry::OccluderSegment& edge, geometry::Vec2i64 eye) {
		return geometry::cross(edge.b - edge.a, eye - edge.a).sign() > 0;
	}

	bool GeometryIndex::insideSolid(geometry::Vec2i64 point) const {
		EyeTally tally(point);
		for (const OccluderRecord& rec : m_occluders) {
			if (wallBodies[rec.body].kind != vision::BodyKind::Opaque) {
				continue;
			}
			tally.add(rec, false);
			if (tally.settled()) {
				break; // on an occluder
			}
		}
		return tally.insideSolid();
	}

} // namespace ecs
