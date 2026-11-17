#include "GeometryIndex.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <construction/OpeningGeometry.h>

#include <offset/WallOffset.h>
#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace ecs {

	namespace {

		namespace cw = construction;

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

		// An occluder from `from` to `to` of wall `source`, with its box.
		GeometryIndex::OccluderRecord record(cw::SegmentId source, geometry::Vec2i64 from, geometry::Vec2i64 to) {
			return {.source = source,
					.seg	= {from, to},
					.low	= {std::min(from.x, to.x), std::min(from.y, to.y)},
					.high	= {std::max(from.x, to.x), std::max(from.y, to.y)}};
		}

		// Whether the two edges share an end.
		bool shareAnEnd(const geometry::OccluderSegment& first, const geometry::OccluderSegment& second) {
			return first.a == second.a || first.a == second.b || first.b == second.a || first.b == second.b;
		}

		// Marks every occluder that touches, crosses or runs along another: any contact with an
		// occluder of another loop group, and within one any but two edges meeting end to end, how a
		// loop runs on from an edge or passes a vertex again. Two walls of one graph can still
		// overlap where they share no vertex, since the outline cancels edges only between rings
		// meeting at one. `loop` numbers each occluder's group (one per offset graph, one per wall
		// of a rejected graph). Occluders are swept in order of their boxes' low x, and only those
		// whose boxes overlap are tested.
		void markCrossings(std::vector<GeometryIndex::OccluderRecord>& occluders, const std::vector<std::size_t>& loop) {
			std::vector<std::size_t> byLowX(occluders.size());
			for (std::size_t index = 0; index < byLowX.size(); ++index) {
				byLowX[index] = index;
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

	void GeometryIndex::setConstructionWorld(const cw::ConstructionWorld* world) {
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

		const cw::ConstructionWorld& world = *m_world;

		// The built walls as the offsetter takes them, the spans their transparent
		// openings leave open, and the id of each.
		const std::unordered_map<cw::SegmentId, cw::OpeningGaps> openingsByWall = cw::openingGapsBySegment(world, {.builtOnly = true});
		const cw::OpeningGaps									 noOpenings;
		std::vector<geometry::WallSegment>						 walls;
		std::vector<std::vector<geometry::WallGap>> sightGaps;
		std::vector<cw::SegmentId>					wallIds;
		for (const cw::WallSegment& segment : world.segments()) {
			if (!cw::isBuilt(segment)) {
				continue; // blueprint walls do not occlude
			}
			const cw::Vertex*  v0	= world.getVertex(segment.v0);
			const cw::Vertex*  v1	= world.getVertex(segment.v1);
			const std::int64_t half = cw::halfThicknessMm(segment);
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
			const cw::OpeningGaps&			openings = found != openingsByWall.end() ? found->second : noOpenings;
			std::vector<geometry::WallGap>	gaps;
			for (std::size_t index = 0; index < openings.openings.size(); ++index) {
				const assets::OpeningTypeDef& type = *openings.types[index];
				const geometry::WallGap&	  gap  = openings.gaps[index];
				m_openings.push_back({openings.openings[index], segment.id, type.name, pointAlong(a, b, lengthMm, gap.fromMm),
									  pointAlong(a, b, lengthMm, gap.toMm), type.transparentToSight});
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
			std::vector<geometry::WallSegment>			members;
			std::vector<std::vector<geometry::WallGap>> memberGaps;
			members.reserve(graph.size());
			memberGaps.reserve(graph.size());
			for (const std::size_t index : graph) {
				members.push_back(walls[index]);
				memberGaps.push_back(sightGaps[index]);
			}
			const geometry::WallBands bands = geometry::resolveWallBands(members, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok || bands.bands.size() != members.size()) {
				// Rejected as nav rejects it: each wall is its raw band, untrimmed, outlined as a
				// lone wall's, so its transparent openings still pass sight.
				for (const std::size_t index : graph) {
					const geometry::WallSegment& wall = walls[index];
					const geometry::WallBands	 rawBand{
						   .bands = {geometry::band(wall.a, wall.b, wall.halfThicknessMm)}, .trimAtA = {0}, .trimAtB = {0}
					   };
					for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline({wall}, {sightGaps[index]}, rawBand).outline) {
						m_occluders.push_back(record(wallIds[index], edge.from, edge.to));
						loop.push_back(loops);
					}
					++loops;
				}
				continue;
			}
			for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline(members, memberGaps, bands).outline) {
				m_occluders.push_back(record(wallIds[graph[edge.index]], edge.from, edge.to));
				loop.push_back(loops);
			}
			++loops;
		}
		markCrossings(m_occluders, loop);
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
		const auto inRange = [&](const OccluderRecord& rec) { return rec.within(eye, radiusMm); };
		// Outdoors no occluder is in range, and the range test is all the query costs.
		const auto first = std::ranges::find_if(m_occluders, inRange);
		if (first == m_occluders.end()) {
			return false;
		}
		// The winding round the eye counts every occluder, the occluders before the first in range too.
		EyeTally tally(eye);
		for (auto occluder = m_occluders.begin(); occluder != m_occluders.end(); ++occluder) {
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

	bool GeometryIndex::anyFacingOccluder(geometry::Vec2i64 eye, std::int64_t radiusMm) const {
		if (radiusMm <= 0) {
			return false;
		}
		bool anyInRange = false;
		for (const OccluderRecord& rec : m_occluders) {
			if (rec.within(eye, radiusMm)) {
				if (!facesAway(rec.seg, eye)) {
					return true; // kept, culled or not
				}
				anyInRange = true;
			}
		}
		// Every occluder in range faces away: rare enough to pay the guards' walk.
		return anyInRange && !backFacesHiddenFrom(eye, radiusMm);
	}

	bool GeometryIndex::backFacesHiddenFrom(geometry::Vec2i64 eye, std::int64_t radiusMm) const {
		EyeTally tally(eye);
		for (const OccluderRecord& rec : m_occluders) {
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
			tally.add(rec, false);
			if (tally.settled()) {
				break; // on an occluder
			}
		}
		return tally.insideSolid();
	}

} // namespace ecs
