#include "SnapEngine.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::construction {

	namespace {

		float distanceMeters(::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float dx = a.x - b.x;
			const float dy = a.y - b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		// Nearest point on segment [a,b] to p, all in world meters.
		::Foundation::Vec2 closestOnSegment(::Foundation::Vec2 p, ::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float abx = b.x - a.x;
			const float aby = b.y - a.y;
			const float lenSq = abx * abx + aby * aby;
			if (lenSq <= 0.0F) {
				return a;
			}
			float t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
			t = std::clamp(t, 0.0F, 1.0F);
			return {a.x + abx * t, a.y + aby * t};
		}

		// Unclamped projection parameter of p onto the line through a->b. A value
		// strictly in (0,1) means the foot of the perpendicular lands on the
		// segment's interior; <=0 or >=1 means it clamps to an endpoint. Returns
		// 0 for a degenerate (zero-length) segment.
		float projectionParam(::Foundation::Vec2 p, ::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float abx = b.x - a.x;
			const float aby = b.y - a.y;
			const float lenSq = abx * abx + aby * aby;
			if (lenSq <= 0.0F) {
				return 0.0F;
			}
			return ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
		}

		// Safety bias added to the outer-face-flush inset, applied to EVERY edge. Without
		// it the flush corner would land the band's outer face right on the foundation
		// edge, but the two mm roundings on a diagonal edge (the inset point's quantize
		// plus geometry::band's perpendicular llround) can together push that corner
		// ~1.4 mm outside the ring, tripping the validator's containment check. Biasing
		// the inset 2 mm further in keeps the corner inside-or-on after rounding. Axis-
		// aligned edges have no rounding, so the bias simply seats them a flat 2 mm inside
		// the edge rather than exactly on it; 2 mm is sub-pixel at any zoom.
		constexpr std::int64_t kFlushInsetSafetyMm = 2;

		// The inward offset distance (meters) for an outer-face-flush wall of the given
		// half-thickness: half-thickness plus the rounding safety bias.
		float flushInsetMeters(std::int64_t halfThicknessMm) {
			return static_cast<float>(halfThicknessMm + kFlushInsetSafetyMm) / static_cast<float>(geometry::kMillimetersPerMeter);
		}

		// Unit interior normal (meters) of ring edge `edgeIndex` (ring[i] -> ring[i+1]),
		// pointing toward the polygon interior. The candidate left normal is flipped
		// when a probe just off the edge midpoint lands outside the ring, so this is
		// correct for either winding without relying on a coordinate-handedness
		// assumption. Zero for a degenerate (zero-length) edge.
		::Foundation::Vec2 inwardNormalMeters(const geometry::Ring& ring, std::size_t edgeIndex) {
			const std::size_t		 n = ring.size();
			const ::Foundation::Vec2 a = geometry::dequantize(ring[edgeIndex]);
			const ::Foundation::Vec2 b = geometry::dequantize(ring[(edgeIndex + 1) % n]);
			const float				 dx = b.x - a.x;
			const float				 dy = b.y - a.y;
			const float				 len = std::sqrt(dx * dx + dy * dy);
			if (len <= 0.0F) {
				return {0.0F, 0.0F};
			}
			float nx = -dy / len;
			float ny = dx / len;
			// Disambiguate the interior side exactly: a 10 mm probe off the midpoint
			// (well within any legal foundation, min spacing 0.5 m) that lands outside
			// means we picked the exterior normal, so flip.
			const ::Foundation::Vec2 mid{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
			const float				 probe = 0.01F;
			const geometry::Vec2i64	 q = geometry::quantize({mid.x + nx * probe, mid.y + ny * probe});
			if (geometry::pointInPolygon(q, ring) == geometry::PointInPolygon::Outside) {
				nx = -nx;
				ny = -ny;
			}
			return {nx, ny};
		}

	} // namespace

	::Foundation::Vec2 outerFaceFlushCorner(const geometry::Ring& ring, std::size_t vertexIndex, std::int64_t halfThicknessMm) {
		const std::size_t n = ring.size();
		if (vertexIndex >= n) {
			return {0.0F, 0.0F}; // out-of-range index (covers an empty ring): nothing to inset
		}
		const ::Foundation::Vec2 v = geometry::dequantize(ring[vertexIndex]);
		if (halfThicknessMm <= 0 || n < 3) {
			return v; // zero-thickness or degenerate ring: the corner is returned unchanged
		}

		const float				 d = flushInsetMeters(halfThicknessMm);
		const std::size_t		 prevEdge = (vertexIndex + n - 1) % n; // edge prev -> v
		const std::size_t		 nextEdge = vertexIndex;			   // edge v -> next
		const ::Foundation::Vec2 prev = geometry::dequantize(ring[prevEdge]);
		const ::Foundation::Vec2 next = geometry::dequantize(ring[(nextEdge + 1) % n]);
		const ::Foundation::Vec2 n1 = inwardNormalMeters(ring, prevEdge);
		const ::Foundation::Vec2 n2 = inwardNormalMeters(ring, nextEdge);

		// Each incident edge offset inward by d defines a line; the corner is their
		// intersection (the miter). Line 1 passes through A1 along d1 (prev->v); line 2
		// through A2 along d2 (v->next).
		const ::Foundation::Vec2 d1{v.x - prev.x, v.y - prev.y};
		const ::Foundation::Vec2 d2{next.x - v.x, next.y - v.y};
		const ::Foundation::Vec2 a1{v.x + n1.x * d, v.y + n1.y * d};
		const ::Foundation::Vec2 a2{v.x + n2.x * d, v.y + n2.y * d};

		const float denom = d1.x * d2.y - d1.y * d2.x;
		if (std::abs(denom) < 1e-6F) {
			// (Near-)collinear edges (a straight pass-through vertex): the two inset
			// lines coincide, so a single-edge offset is the miter.
			return a1;
		}
		const float s = ((a2.x - a1.x) * d2.y - (a2.y - a1.y) * d2.x) / denom;
		return {a1.x + s * d1.x, a1.y + s * d1.y};
	}

	bool SnapEngine::snapToVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, std::int64_t insetMm) const {
		const float		  radius = snapping_->vertexSnapRadiusMeters;
		float			  best = radius;
		const Foundation* bestF = nullptr;
		std::size_t		  bestIndex = 0;
		for (const Foundation& f : world_->foundations()) {
			for (std::size_t i = 0; i < f.ring.size(); ++i) {
				const ::Foundation::Vec2 vm = geometry::dequantize(f.ring[i]);
				const float				 d = distanceMeters(cursor, vm);
				if (d <= best) {
					best = d;
					bestF = &f;
					bestIndex = i;
				}
			}
		}
		if (bestF == nullptr) {
			return false;
		}
		// Proximity is to the raw corner; the returned point is the outer-face-flush
		// miter when an inset is requested (the wall tool), else the raw corner (the
		// foundation tool).
		out = insetMm > 0 ? outerFaceFlushCorner(bestF->ring, bestIndex, insetMm) : geometry::dequantize(bestF->ring[bestIndex]);
		return true;
	}

	bool SnapEngine::snapToEdge(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, std::int64_t insetMm) const {
		const float		   radius = snapping_->edgeSnapRadiusMeters;
		float			   best = radius;
		const Foundation*  bestF = nullptr;
		std::size_t		   bestEdge = 0;
		::Foundation::Vec2 bestFoot{};
		for (const Foundation& f : world_->foundations()) {
			const std::size_t n = f.ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const ::Foundation::Vec2 a = geometry::dequantize(f.ring[i]);
				const ::Foundation::Vec2 b = geometry::dequantize(f.ring[(i + 1) % n]);
				const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
				const float				 d = distanceMeters(cursor, c);
				if (d <= best) {
					best = d;
					bestF = &f;
					bestEdge = i;
					bestFoot = c;
				}
			}
		}
		if (bestF == nullptr) {
			return false;
		}
		// Inset the foot inward along the edge's interior normal for outer-face-flush
		// alignment (the wall tool); the foundation tool passes insetMm == 0 and gets
		// the raw foot.
		if (insetMm > 0) {
			const ::Foundation::Vec2 nrm = inwardNormalMeters(bestF->ring, bestEdge);
			const float				 d = flushInsetMeters(insetMm);
			out = {bestFoot.x + nrm.x * d, bestFoot.y + nrm.y * d};
		} else {
			out = bestFoot;
		}
		return true;
	}

	bool SnapEngine::snapToWallVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, VertexId& outVertex) const {
		const float radius = snapping_->vertexSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const Vertex& v : world_->vertices()) {
			const ::Foundation::Vec2 vm = geometry::dequantize(v.pos);
			const float				 d = distanceMeters(cursor, vm);
			// Strict-< on a tie keeps the lowest-id vertex (vertices() is in
			// ascending-id insertion order), so ties resolve deterministically.
			if (d < best) {
				best = d;
				out = vm;
				outVertex = v.id;
				found = true;
			}
		}
		return found;
	}

	bool SnapEngine::snapToWallSegment(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, SegmentId& outSegment) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const WallSegment& s : world_->segments()) {
			const Vertex* v0 = world_->getVertex(s.v0);
			const Vertex* v1 = world_->getVertex(s.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 a = geometry::dequantize(v0->pos);
			const ::Foundation::Vec2 b = geometry::dequantize(v1->pos);
			// Endpoints are explicitly NOT segment hits: a projection that clamps to
			// either end is an endpoint join (the wall-vertex tier owns it), not a
			// T-junction. Only a foot of perpendicular landing strictly inside the
			// span (0 < t < 1) is a genuine interior point-on-wall, so skip the rest.
			// Without this, a cursor near a segment's end but outside the vertex
			// radius would report WallSegment with hitSegment set, telling the
			// WallTool to T-split when it should plain-join the endpoint.
			const float t = projectionParam(cursor, a, b);
			if (t <= 0.0F || t >= 1.0F) {
				continue;
			}
			const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
			const float				 d = distanceMeters(cursor, c);
			// Strict-< keeps the lowest-id segment on a tie; segments() is in
			// ascending-id order. (segmentAt's stored tie-break is highest-id, but
			// that is the pick query, not snapping; here the wall vertices already
			// handle endpoint priority, so the segment tie-break only ever picks
			// among true interior projections and lowest-id is the simplest rule.)
			if (d < best) {
				best = d;
				out = c;
				outSegment = s.id;
				found = true;
			}
		}
		return found;
	}

	SnapResult SnapEngine::snapAngle(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor) const {
		const ::Foundation::Vec2 prev = points.back();
		const float				 dx = cursor.x - prev.x;
		const float				 dy = cursor.y - prev.y;
		const float				 dist = std::sqrt(dx * dx + dy * dy);
		if (dist <= 0.0F) {
			return {cursor, SnapKind::None};
		}

		// Reference direction: the previous segment if there is one, else +x axis.
		float refAngle = 0.0F;
		if (points.size() >= 2) {
			const ::Foundation::Vec2 before = points[points.size() - 2];
			refAngle = std::atan2(prev.y - before.y, prev.x - before.x);
		}

		const float increment = snapping_->angleIncrementDegrees * (static_cast<float>(std::numbers::pi) / 180.0F);
		if (increment <= 0.0F) {
			return {cursor, SnapKind::None};
		}

		const float rawAngle = std::atan2(dy, dx);
		const float rel = rawAngle - refAngle;
		const float snappedRel = std::round(rel / increment) * increment;
		const float snappedAngle = refAngle + snappedRel;

		const ::Foundation::Vec2 snapped{prev.x + std::cos(snappedAngle) * dist, prev.y + std::sin(snappedAngle) * dist};

		SnapResult result;
		result.point = snapped;
		result.kind = SnapKind::Angle;
		result.guideFrom = prev;
		result.guideTo = snapped;
		return result;
	}

	SnapResult SnapEngine::snap(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool freeform) const {
		// Origin-close: once a closeable shape exists (>= 3 points), being within
		// the origin radius snaps onto the first point and signals a close.
		if (points.size() >= 3) {
			const float d = distanceMeters(cursor, points.front());
			if (d <= snapping_->originCloseRadiusMeters) {
				return {points.front(), SnapKind::Origin};
			}
		}

		// Existing-geometry snapping (always on): vertices beat edges.
		::Foundation::Vec2 v{};
		if (snapToVertex(cursor, v)) {
			return {v, SnapKind::Vertex};
		}
		::Foundation::Vec2 e{};
		if (snapToEdge(cursor, e)) {
			return {e, SnapKind::Edge};
		}

		// Angle snap relative to the previous segment, unless freeform or there is
		// no previous point to anchor the angle to.
		if (!freeform && !points.empty()) {
			return snapAngle(points, cursor);
		}

		return {cursor, SnapKind::None};
	}

	OpeningSnap SnapEngine::snapOpening(::Foundation::Vec2 cursor, float openingWidthMeters) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		// End margin is a construction CONSTRAINT, not a snapping one; read it from the
		// registry (the production source of truth) the same way the validator resolves
		// config it doesn't directly hold. An unloaded registry yields the struct
		// default (0.3 m), which is deterministic.
		const float margin = engine::assets::ConstructionRegistry::Get().constraints().openingMarginMeters;
		const float halfWidth = openingWidthMeters * 0.5F;

		float		best = radius;
		OpeningSnap result;
		for (const WallSegment& s : world_->segments()) {
			// Only finished walls can host an opening; a blueprint has no surface yet.
			if (s.state != FoundationState::Built) {
				continue;
			}
			const Vertex* v0 = world_->getVertex(s.v0);
			const Vertex* v1 = world_->getVertex(s.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 a = geometry::dequantize(v0->pos);
			const ::Foundation::Vec2 b = geometry::dequantize(v1->pos);
			const float				 length = distanceMeters(a, b);
			if (length <= 0.0F) {
				continue;
			}
			// Too short to host the opening within the end margins: skip it (a closer
			// short wall must not shadow a farther wall that could actually hold it).
			const float halfFrac = (halfWidth + margin) / length;
			if (halfFrac > 0.5F) {
				continue;
			}

			const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
			const float				 d = distanceMeters(cursor, c);
			// Strict-< keeps the lowest-id segment on a tie; segments() is in
			// ascending-id insertion order (deterministic).
			if (d < best) {
				best = d;
				// Clamp t so the opening honors the end margins on both ends.
				float t = projectionParam(cursor, a, b);
				t = std::clamp(t, halfFrac, 1.0F - halfFrac);
				result.segment = s.id;
				result.t = t;
				result.point = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
				result.valid = true;
			}
		}
		return result;
	}

	SnapResult SnapEngine::snapWall(
		const std::vector<::Foundation::Vec2>& points,
		::Foundation::Vec2					   cursor,
		bool								   freeform,
		std::int64_t						   wallHalfThicknessMm
	) const {
		// Priority, most specific first (design Walls > Drawing). No origin-close:
		// the chain is open and never closes onto its first point.
		::Foundation::Vec2 wv{};
		VertexId		   wvId = kInvalidVertex;
		if (snapToWallVertex(cursor, wv, wvId)) {
			SnapResult r;
			r.point = wv;
			r.kind = SnapKind::WallEndpoint;
			r.hitVertex = wvId;
			return r;
		}

		// Foundation corner: inset to the outer-face-flush miter so a perimeter
		// wall sits ON the foundation (design "Alignment").
		::Foundation::Vec2 v{};
		if (snapToVertex(cursor, v, wallHalfThicknessMm)) {
			return {v, SnapKind::Vertex};
		}

		::Foundation::Vec2 ws{};
		SegmentId		   wsId = kInvalidSegment;
		if (snapToWallSegment(cursor, ws, wsId)) {
			SnapResult r;
			r.point = ws;
			r.kind = SnapKind::WallSegment;
			r.hitSegment = wsId;
			return r;
		}

		// Foundation edge: inset inward by the half-thickness (outer-face-flush).
		::Foundation::Vec2 e{};
		if (snapToEdge(cursor, e, wallHalfThicknessMm)) {
			return {e, SnapKind::Edge};
		}

		if (!freeform && !points.empty()) {
			return snapAngle(points, cursor);
		}

		return {cursor, SnapKind::None};
	}

} // namespace engine::construction
