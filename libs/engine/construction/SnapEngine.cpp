#include "SnapEngine.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

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

	} // namespace

	bool SnapEngine::snapToVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const {
		const float radius = snapping_->vertexSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const Foundation& f : world_->foundations()) {
			for (const auto& v : f.ring) {
				const ::Foundation::Vec2 vm = geometry::dequantize(v);
				const float				 d = distanceMeters(cursor, vm);
				if (d <= best) {
					best = d;
					out = vm;
					found = true;
				}
			}
		}
		return found;
	}

	bool SnapEngine::snapToEdge(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const Foundation& f : world_->foundations()) {
			const std::size_t n = f.ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const ::Foundation::Vec2 a = geometry::dequantize(f.ring[i]);
				const ::Foundation::Vec2 b = geometry::dequantize(f.ring[(i + 1) % n]);
				const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
				const float				 d = distanceMeters(cursor, c);
				if (d <= best) {
					best = d;
					out = c;
					found = true;
				}
			}
		}
		return found;
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

	SnapResult SnapEngine::snapWall(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool freeform) const {
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

		::Foundation::Vec2 v{};
		if (snapToVertex(cursor, v)) {
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

		::Foundation::Vec2 e{};
		if (snapToEdge(cursor, e)) {
			return {e, SnapKind::Edge};
		}

		if (!freeform && !points.empty()) {
			return snapAngle(points, cursor);
		}

		return {cursor, SnapKind::None};
	}

} // namespace engine::construction
