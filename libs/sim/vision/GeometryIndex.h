#pragma once

// Vision occluder source: turns the construction wall graph into wall bodies and the occluder
// segments a visibility polygon is built from. The Vision System owns a GeometryIndex, calls
// rebuildIfStale() each tick, and feeds queryFacingOccluders() into
// geometry::computeVisibilityPolygon; StoreOrDrop reads queryOccluders().
//
// Each connected graph of built walls is one body (sight-softening.md 2), whose kind is decided by
// its diameter when the index is rebuilt: opaque from the opaque least diameter across, soft below
// it. Only opaque bodies occlude: queryFacingOccluders, backFacesHiddenFrom, and insideSolid read
// them alone, so the visibility polygon never sees a soft stub. queryOccluders keeps every wall, and
// the structure pass reads every record, so a stub a colonist sees still joins memory.
//
// The occluders are the edges of the built walls' outline, the shape the wall
// drawing outlines (geometry::wallPiecesAndOutline over each connected graph of
// built walls): both faces, end caps, mitred and trimmed junctions, and the jambs
// of every gap. Sight stops at a wall's near face, so the band itself is never in
// sight. A graph the band offsetter rejects (by nav's test) occludes as each wall's
// untrimmed band outlined on its own, cut at its transparent openings; its body is still the whole
// graph, its outline the set of those band outlines and its diameter taken over all their vertices.
//
// Every occluder lies on a closed loop with the solid on its left, so an eye
// outside the solid strictly on an edge's left is behind that edge's own solid,
// and the edge (a back face) can never bound what the eye sees. Where occluders
// cross or touch other than end to end (a rejected graph's bands at its joints, two
// graphs' walls that meet, two walls of one graph overlapping away from a joint)
// the back faces' ends are what turn the crossing into a polygon vertex, so the
// facing query keeps them near such an occluder.
//
// Doors AND windows pass sight. This is the difference from navigation: nav treats
// a door as a movement gap but a window as a movement-solid band, whereas for sight
// a transparentToSight opening is a gap in the outline, cut at the span
// construction::openingGaps gives it, the span the wall drawing cuts.
//
// The rebuild resolves bands for every built wall, as nav's wall extraction does,
// and runs inline, version-gated against ConstructionWorld::version().

#include "SightBody.h"
#include "SightTunables.h"

#include <construction/ConstructionWorld.h> // SegmentId/OpeningId typedefs used in records

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>
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
			// Its body's index in bodies().
			std::size_t body = 0;
			// It touches, crosses or runs along another occluder: another graph's; in a rejected graph
			// another wall's band, which overlap at every joint; or in its own graph one it does not
			// meet end to end, where two walls' bands overlap without a shared vertex.
			bool crossesAnotherOccluder = false;
			// Its bounding box.
			geometry::Vec2i64 low;
			geometry::Vec2i64 high;

			// Whether any of it lies within radiusMm of `point`: off the box by more than that
			// on either axis it does not, else geometry::withinDistanceOfSegment decides.
			[[nodiscard]] bool within(geometry::Vec2i64 point, std::int64_t radiusMm) const {
				if (point.x + radiusMm < low.x || point.x - radiusMm > high.x || point.y + radiusMm < low.y ||
					point.y - radiusMm > high.y) {
					return false;
				}
				return geometry::withinDistanceOfSegment(point, seg.a, seg.b, radiusMm);
			}
		};

		// A built opening, kept for the structure pass. Its span along the wall is
		// construction::openingGaps' (clamped to the segment), the span a transparentToSight
		// opening cuts; the opening is seen where either wall face across that span is, so
		// each face's stretch is kept, 3 mm outside the face: from the jamb at the
		// span's start to the one at its end, on the wall's left (`leftFace`) and right.
		struct OpeningRecord {
			struct FaceSpan {
				geometry::Vec2i64 start;
				geometry::Vec2i64 end;
			};

			construction::OpeningId openingId = 0;
			construction::SegmentId segment	  = 0;
			std::string						type;
			FaceSpan						leftFace;
			FaceSpan						rightFace;
			bool							transparentToSight = false;
			// The box round both face spans.
			geometry::Vec2i64 low;
			geometry::Vec2i64 high;

			// Whether either face span comes within radiusMm of `point`: off the box by more than
			// that on either axis neither does, else geometry::withinDistanceOfSegment decides.
			[[nodiscard]] bool within(geometry::Vec2i64 point, std::int64_t radiusMm) const {
				if (point.x + radiusMm < low.x || point.x - radiusMm > high.x || point.y + radiusMm < low.y ||
					point.y - radiusMm > high.y) {
					return false;
				}
				return geometry::withinDistanceOfSegment(point, leftFace.start, leftFace.end, radiusMm) ||
					   geometry::withinDistanceOfSegment(point, rightFace.start, rightFace.end, radiusMm);
			}
		};

		// One connected wall graph's outline: its edges (solid on the left of each), per edge the edge
		// ending at its start, its box, the mean of its corners, its diameter, and its kind.
		struct WallBody {
			std::vector<geometry::OccluderSegment> edges;
			std::vector<std::uint32_t>			   previousEdge;
			geometry::Vec2i64					   low;
			geometry::Vec2i64					   high;
			geometry::Vec2i64					   centre;
			geometry::Int128					   diameterSquared;
			std::int64_t						   diameterMm = 0;
			vision::BodyKind					   kind		  = vision::BodyKind::Opaque;

			// Whether any of its outline lies within radiusMm of `point`.
			[[nodiscard]] bool within(geometry::Vec2i64 point, std::int64_t radiusMm) const;
		};

		// Wire the topology source. Passing nullptr leaves the index inert (no
		// rebuild, no occluders). Does not take ownership.
		void setConstructionWorld(const construction::ConstructionWorld* world);

		// Rebuild the occluder cache iff the wired world's version() has moved since
		// the last rebuild. Cheap synchronous extraction; safe to call every tick.
		void rebuildIfStale();

		// A wall body is opaque from this diameter, soft below it. A change takes effect at the next
		// rebuildIfStale, which rebuilds.
		void setOpaqueLeastDiameterMm(std::int64_t leastMm);

		// Append every cached occluder that lies within radiusMm of center, soft bodies' included,
		// to `out` (clearing it first). Exact integer range test
		// (geometry::withinDistanceOfSegment on the whole occluder). Linear scan
		// over the cache: at the modest wall counts a colony reaches this is fine; a
		// flat spatial grid is the obvious future optimization if it ever isn't.
		void queryOccluders(geometry::Vec2i64 center, std::int64_t radiusMm,
							 std::vector<geometry::OccluderSegment>& out) const;

		// The opaque bodies' occluders within radiusMm of `eye`, less the back faces for an eye at
		// `eye` when backFacesHiddenFrom it,
		// for a visibility polygon from that eye alone: the polygon is the same, but for where
		// a ray aimed at a back face's hidden end hits a slanted face, a vertex that rounds off
		// the face by under a millimetre. Each ring's vertices are snapped that way, so a vertex
		// of one lies within two snaps, about 1.5 mm, of the other's edges. Returns whether it
		// culled them: backFacesHiddenFrom(eye, radiusMm) when an occluder is in range, false
		// when none is.
		bool queryFacingOccluders(geometry::Vec2i64 eye, std::int64_t radiusMm,
								  std::vector<geometry::OccluderSegment>& out) const;

		// The index in bodies() of every body, soft or opaque, with some of its outline within
		// radiusMm of `point`, in order, into `out` (cleared first).
		void bodiesWithin(geometry::Vec2i64 point, std::int64_t radiusMm, std::vector<std::size_t>& out) const;

		// Whether no opaque body's back face within radiusMm can bound what an eye at `eye` sees: it is
		// outside the solid (insideSolid), which it would see from within, over a
		// millimetre from the line of every occluder in range, where the polygon's grazing
		// rays need the back faces, and no occluder in range crosses or touches another other
		// than end to end.
		[[nodiscard]] bool backFacesHiddenFrom(geometry::Vec2i64 eye, std::int64_t radiusMm) const;

		// Whether `point` is inside or on the solid the opaque bodies' occluder loops enclose: the loops'
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
		[[nodiscard]] const std::vector<WallBody>&		 bodies() const { return wallBodies; }

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

		std::int64_t opaqueLeastDiameterMm = vision::SightTunables{}.wallOpaqueLeastDiameterMm;

		std::vector<OccluderRecord> m_occluders;
		std::vector<OpeningRecord>	m_openings;
		std::vector<WallBody>		wallBodies;
	};

} // namespace ecs
