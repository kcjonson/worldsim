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

		// A hit snaps to the millimetre, so one within 0.71 mm of the eye lands on it.
		constexpr std::int64_t kHitSnapsOntoTheEyeMm = 1;

		// The centerline point `alongMm` from `a` toward `b`, clamped to the segment
		// and rounded to the millimetre.
		geometry::Vec2i64 pointAlong(const geometry::Vec2i64& a, const geometry::Vec2i64& b, double lengthMm, std::int64_t alongMm) {
			const double fraction = std::clamp(static_cast<double>(alongMm) / lengthMm, 0.0, 1.0);
			return {a.x + std::llround(static_cast<double>(b.x - a.x) * fraction), a.y + std::llround(static_cast<double>(b.y - a.y) * fraction)};
		}

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

		const cw::ConstructionWorld&		world	 = *m_world;
		const assets::ConstructionRegistry& registry = assets::ConstructionRegistry::Get();

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
				const cw::Opening*			  opening = world.getOpening(openings.openings[index]);
				const assets::OpeningTypeDef* type	  = opening != nullptr ? registry.getOpeningType(opening->type) : nullptr;
				if (type == nullptr) {
					continue;
				}
				const geometry::WallGap& gap = openings.gaps[index];
				m_openings.push_back({opening->id, segment.id, opening->type, pointAlong(a, b, lengthMm, gap.fromMm),
									  pointAlong(a, b, lengthMm, gap.toMm), type->transparentToSight});
				if (type->transparentToSight) {
					gaps.push_back(gap);
				}
			}
			walls.push_back({a, b, half});
			sightGaps.push_back(std::move(gaps));
			wallIds.push_back(segment.id);
		}

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
					const geometry::WallBands	 rawBand{.bands = {geometry::band(wall.a, wall.b, wall.halfThicknessMm)}, .trimAtA = {0}, .trimAtB = {0}};
					for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline({wall}, {sightGaps[index]}, rawBand).outline) {
						m_occluders.push_back({wallIds[index], {edge.from, edge.to}});
					}
				}
				continue;
			}
			for (const geometry::OutlineEdge& edge : geometry::wallPiecesAndOutline(members, memberGaps, bands).outline) {
				m_occluders.push_back({wallIds[graph[edge.index]], {edge.from, edge.to}});
			}
		}
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
			if (geometry::withinDistanceOfSegment(center, rec.seg.a, rec.seg.b, radiusMm)) {
				out.push_back(rec.seg);
			}
		}
	}

	void GeometryIndex::queryFacingOccluders(geometry::Vec2i64 eye, std::int64_t radiusMm,
											 std::vector<geometry::OccluderSegment>& out) const {
		queryOccluders(eye, radiusMm, out);
		if (backFacesHiddenFrom(eye, radiusMm)) {
			std::erase_if(out, [&](const geometry::OccluderSegment& edge) { return facesAway(edge, eye); });
		}
	}

	bool GeometryIndex::backFacesHiddenFrom(geometry::Vec2i64 eye, std::int64_t radiusMm) const {
		// computeVisibilityPolygon blocks a ray that runs along an occluder on both of its
		// sides and takes the polygon's next edge from the next direction round, which a back
		// face's end can be; and it drops a hit that snaps onto the eye, where the back face
		// behind it is what stops the ray.
		for (const OccluderRecord& rec : m_occluders) {
			if (geometry::withinDistanceOfSegment(eye, rec.seg.a, rec.seg.b, radiusMm) &&
				(geometry::cross(rec.seg.b - rec.seg.a, eye - rec.seg.a).sign() == 0 ||
				 geometry::withinDistanceOfSegment(eye, rec.seg.a, rec.seg.b, kHitSnapsOntoTheEyeMm))) {
				return false;
			}
		}
		return !insideSolid(eye);
	}

	bool GeometryIndex::facesAway(const geometry::OccluderSegment& edge, geometry::Vec2i64 eye) {
		return geometry::cross(edge.b - edge.a, eye - edge.a).sign() > 0;
	}

	bool GeometryIndex::insideSolid(geometry::Vec2i64 point) const {
		int winding = 0;
		for (const OccluderRecord& rec : m_occluders) {
			const geometry::Vec2i64& from = rec.seg.a;
			const geometry::Vec2i64& to	  = rec.seg.b;
			const int				 side = geometry::cross(to - from, point - from).sign();
			if (side == 0 && point.x >= std::min(from.x, to.x) && point.x <= std::max(from.x, to.x) && point.y >= std::min(from.y, to.y) &&
				point.y <= std::max(from.y, to.y)) {
				return true;
			}
			if (from.y <= point.y) {
				if (to.y > point.y && side > 0) {
					++winding;
				}
			} else if (to.y <= point.y && side < 0) {
				--winding;
			}
		}
		return winding != 0;
	}

} // namespace ecs
