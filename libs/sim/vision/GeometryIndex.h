#pragma once

// Vision occluder source: turns the construction wall graph into the opaque
// occluder segments a visibility polygon is built from. The Vision System owns a
// GeometryIndex, calls rebuildIfStale() each tick, and feeds queryOccluders()
// into geometry::computeVisibilityPolygon / hasLineOfSight.
//
// The occluders are the edges of the built walls' outline, the shape the wall
// drawing outlines (geometry::wallPiecesAndOutline over each connected graph of
// built walls): both faces, end caps, mitred and trimmed junctions, and the jambs
// of every gap. Sight stops at a wall's near face, so the band itself is never in
// sight. A graph the band offsetter rejects occludes as each wall's untrimmed band,
// as nav blocks it.
//
// Doors AND windows pass sight. This is the difference from navigation: nav treats
// a door as a movement gap but a window as a movement-solid band, whereas for sight
// a transparentToSight opening is a gap in the outline, cut at the span
// construction::openingGaps gives it, the span the wall drawing cuts.
//
// The rebuild resolves bands for every built wall, as nav's wall extraction does,
// and runs inline, version-gated against ConstructionWorld::version().

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
		// outputs bare geometry::OccluderSegment; the source id lets the V3
		// structure-as-observable pass find which walls a visibility polygon
		// touches without re-walking the topology.
		struct OccluderRecord {
			construction::SegmentId source = 0;
			geometry::OccluderSegment		seg;
		};

		// A built opening, kept for the same V3 pass. jambA/jambB are the two
		// centerline points bounding the opening's span (construction::openingGaps',
		// clamped to the segment), the span a transparentToSight opening cuts.
		struct OpeningRecord {
			construction::OpeningId openingId = 0;
			construction::SegmentId segment	  = 0;
			std::string						type;
			geometry::Vec2i64				jambA;
			geometry::Vec2i64				jambB;
			bool							transparentToSight = false;
		};

		// Wire the topology source. Passing nullptr leaves the index inert (no
		// rebuild, no occluders). Does not take ownership.
		void setConstructionWorld(const construction::ConstructionWorld* world);

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

		// Monotonic counter bumped on each actual rebuild() (not on no-op
		// rebuildIfStale calls). A consumer caching anything derived from the
		// occluder set (e.g. the Vision System's per-observer visibility polygons)
		// invalidates its cache when this moves. Starts at 0; the first rebuild
		// makes it 1, so a never-built index (generation 0) is distinguishable.
		[[nodiscard]] std::uint64_t generation() const { return m_generation; }

		// Accessors for the V3 structure-as-observable pass.
		[[nodiscard]] const std::vector<OccluderRecord>& occluders() const { return m_occluders; }
		[[nodiscard]] const std::vector<OpeningRecord>&	 builtOpenings() const { return m_openings; }

	  private:
		void rebuild();

		const construction::ConstructionWorld* m_world = nullptr;

		// kInvalidVersion forces the first rebuildIfStale() to build, since a fresh
		// ConstructionWorld starts at version 0 (which a 0-init m_builtVersion would
		// match and wrongly skip).
		static constexpr std::uint64_t kInvalidVersion = ~0ULL;
		std::uint64_t				   m_builtVersion = kInvalidVersion;

		// Bumped by rebuild(); see generation().
		std::uint64_t m_generation = 0;

		std::vector<OccluderRecord> m_occluders;
		std::vector<OpeningRecord>	m_openings;
	};

} // namespace ecs
