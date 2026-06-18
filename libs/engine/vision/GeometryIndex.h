#pragma once

// Vision occluder source: turns the construction wall graph into the opaque
// occluder segments a visibility polygon is built from. The Vision System owns a
// GeometryIndex, calls rebuildIfStale() each tick, and feeds queryOccluders()
// into geometry::computeVisibilityPolygon / hasLineOfSight.
//
// Walls occlude sight; doors AND windows pass it. This is the key difference from
// navigation: nav treats a door as a movement gap but a window as a movement-solid
// band, whereas for SIGHT both a door and a window are gaps -- the glass and the
// open doorway are equally transparent. So a transparentToSight opening is a GAP in
// the occluder line, and only the solid wall portions between/around openings emit
// occluders. The centerline gap span uses the SAME half-extent formula as the
// navmesh (NavInputBuilder::extractWalls), so the sight gap lines up with the nav
// gap for the same opening.
//
// Extraction is synchronous and CPU-light (centerline segments, no triangulation,
// no offsetting), so unlike NavigationSystem there is no async/worker path: the
// rebuild runs inline, version-gated against ConstructionWorld::version().

#include <construction/ConstructionWorld.h> // SegmentId/OpeningId typedefs used in records

#include <core/Vec2i64.h>
#include <visibility/Visibility.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

	class GeometryIndex {
	  public:
		// An opaque occluder plus the wall segment it came from. queryOccluders
		// outputs bare geometry::OccluderSegment; the source id lets a later pass
		// attribute a sight blockage to a wall and supports per-segment culling.
		struct OccluderRecord {
			engine::construction::SegmentId source = 0;
			geometry::OccluderSegment		seg;
		};

		// A built wall segment's centerline endpoints, kept so the V3
		// structure-as-observable pass can find which walls a visibility polygon
		// touches without re-walking the topology.
		struct SegmentRecord {
			engine::construction::SegmentId id = 0;
			geometry::Vec2i64				a;
			geometry::Vec2i64				b;
		};

		// A built opening, kept for the same V3 pass. jambA/jambB are the two
		// centerline points bounding the opening's clear span (the gap endpoints),
		// computed from the same [t0,t1] used to cut the occluder line.
		struct OpeningRecord {
			engine::construction::OpeningId openingId = 0;
			engine::construction::SegmentId segment	  = 0;
			std::string						type;
			geometry::Vec2i64				jambA;
			geometry::Vec2i64				jambB;
			bool							transparentToSight = false;
		};

		// Wire the topology source. Passing nullptr leaves the index inert (no
		// rebuild, no occluders). Does not take ownership.
		void setConstructionWorld(const engine::construction::ConstructionWorld* world);

		// Rebuild the occluder cache iff the wired world's version() has moved since
		// the last rebuild. Cheap synchronous extraction; safe to call every tick.
		void rebuildIfStale();

		// Append every cached opaque occluder whose source segment lies within
		// radiusMm of center to `out` (clearing it first). Exact integer range test
		// (geometry::withinDistanceOfSegment on the occluder endpoints). Linear scan
		// over the cache: at the modest wall counts a colony reaches this is fine; a
		// flat spatial grid is the obvious future optimization if it ever isn't.
		void queryOccluders(geometry::Vec2i64 center, std::int64_t radiusMm,
							 std::vector<geometry::OccluderSegment>& out) const;

		[[nodiscard]] std::size_t occluderCount() const { return m_occluders.size(); }
		[[nodiscard]] bool		  hasWorld() const { return m_world != nullptr; }

		// Accessors for the V3 structure-as-observable pass.
		[[nodiscard]] const std::vector<OccluderRecord>& occluders() const { return m_occluders; }
		[[nodiscard]] const std::vector<SegmentRecord>&	 builtSegments() const { return m_segments; }
		[[nodiscard]] const std::vector<OpeningRecord>&	 builtOpenings() const { return m_openings; }

	  private:
		void rebuild();

		const engine::construction::ConstructionWorld* m_world = nullptr;

		// kInvalidVersion forces the first rebuildIfStale() to build, since a fresh
		// ConstructionWorld starts at version 0 (which a 0-init m_builtVersion would
		// match and wrongly skip).
		static constexpr std::uint64_t kInvalidVersion = ~0ULL;
		std::uint64_t				   m_builtVersion = kInvalidVersion;

		std::vector<OccluderRecord> m_occluders;
		std::vector<SegmentRecord>	m_segments;
		std::vector<OpeningRecord>	m_openings;
	};

} // namespace ecs
