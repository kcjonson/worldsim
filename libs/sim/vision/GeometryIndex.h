#pragma once

// Vision occluder source: turns the construction wall graph into the opaque
// occluder segments a visibility polygon is built from. The Vision System owns a
// GeometryIndex, calls rebuildIfStale() each tick, and feeds queryFacingOccluders()
// into geometry::computeVisibilityPolygon, and queryOccluders() into hasLineOfSight.
//
// The occluders are the edges of the built walls' outline, the shape the wall
// drawing outlines (geometry::wallPiecesAndOutline over each connected graph of
// built walls): both faces, end caps, mitred and trimmed junctions, and the jambs
// of every gap. Sight stops at a wall's near face, so the band itself is never in
// sight. A graph the band offsetter rejects (by nav's test) occludes as each wall's
// untrimmed band outlined on its own, cut at its transparent openings.
//
// Every occluder lies on a closed loop with the solid on its left, so an eye
// outside the solid strictly on an edge's left is behind that edge's own solid,
// and the edge (a back face) can never bound what the eye sees. Where loops cross
// (a rejected graph's bands, or two graphs' walls that meet without a joint) the
// back faces' ends are what turn the crossing into a polygon vertex, so the facing
// query keeps them near such an occluder.
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
			// It belongs to a rejected graph, or touches, crosses or runs along an occluder of
			// another graph.
			bool crossesAnotherLoop = false;
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

		// Append every cached opaque occluder that lies within radiusMm of center
		// to `out` (clearing it first). Exact integer range test
		// (geometry::withinDistanceOfSegment on the occluder endpoints). Linear scan
		// over the cache: at the modest wall counts a colony reaches this is fine; a
		// flat spatial grid is the obvious future optimization if it ever isn't.
		void queryOccluders(geometry::Vec2i64 center, std::int64_t radiusMm,
							 std::vector<geometry::OccluderSegment>& out) const;

		// queryOccluders less the back faces for an eye at `eye` when backFacesHiddenFrom it,
		// for a visibility polygon from that eye alone: the polygon is the same, but for where
		// a ray aimed at a back face's hidden end hits a slanted face, a vertex that rounds off
		// the face by under a millimetre. Returns whether it culled them: backFacesHiddenFrom(eye,
		// radiusMm) when an occluder is in range, false when none is.
		bool queryFacingOccluders(geometry::Vec2i64 eye, std::int64_t radiusMm,
								  std::vector<geometry::OccluderSegment>& out) const;

		// Whether queryFacingOccluders(eye, radiusMm) would give any occluder, without listing them.
		[[nodiscard]] bool anyFacingOccluder(geometry::Vec2i64 eye, std::int64_t radiusMm) const;

		// Whether no back face within radiusMm can bound what an eye at `eye` sees: it is
		// outside the solid (insideSolid), which it would see from within, over a
		// millimetre from the line of every occluder in range, where the polygon's grazing
		// rays need the back faces, and no occluder in range crosses another loop.
		[[nodiscard]] bool backFacesHiddenFrom(geometry::Vec2i64 eye, std::int64_t radiusMm) const;

		// Whether `point` is inside or on the solid the occluder loops enclose: the loops'
		// winding number round it is not zero (outer loops wind counter-clockwise, the
		// inside of a closed room clockwise), or it lies on an occluder. Exact.
		[[nodiscard]] bool insideSolid(geometry::Vec2i64 point) const;

		// The eye strictly on the edge's solid side, the edge's left.
		[[nodiscard]] static bool facesAway(const geometry::OccluderSegment& edge, geometry::Vec2i64 eye);

		[[nodiscard]] std::size_t occluderCount() const { return m_occluders.size(); }
		[[nodiscard]] bool		  hasWorld() const { return m_world != nullptr; }

		// Monotonic counter bumped on each actual rebuild() (not on no-op
		// rebuildIfStale calls). A consumer caching anything derived from the
		// occluder set (e.g. the Vision System's per-observer visibility polygons)
		// invalidates its cache when this moves. Starts at 0; the first rebuild
		// makes it 1, so a never-built index (generation 0) is distinguishable.
		[[nodiscard]] std::uint64_t generation() const { return m_generation; }

		// Accessors for the V3 structure-as-observable pass. Each source's occluders are
		// listed together.
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
