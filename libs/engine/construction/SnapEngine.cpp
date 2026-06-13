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
			t		= std::clamp(t, 0.0F, 1.0F);
			return {a.x + abx * t, a.y + aby * t};
		}

	} // namespace

	bool SnapEngine::snapToVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const {
		const float radius = snapping_->vertexSnapRadiusMeters;
		float		best   = radius;
		bool		found  = false;
		for (const Foundation& f : world_->foundations()) {
			for (const auto& v : f.ring) {
				const ::Foundation::Vec2 vm = geometry::dequantize(v);
				const float			   d  = distanceMeters(cursor, vm);
				if (d <= best) {
					best  = d;
					out	  = vm;
					found = true;
				}
			}
		}
		return found;
	}

	bool SnapEngine::snapToEdge(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		float		best   = radius;
		bool		found  = false;
		for (const Foundation& f : world_->foundations()) {
			const std::size_t n = f.ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const ::Foundation::Vec2 a = geometry::dequantize(f.ring[i]);
				const ::Foundation::Vec2 b = geometry::dequantize(f.ring[(i + 1) % n]);
				const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
				const float			   d = distanceMeters(cursor, c);
				if (d <= best) {
					best  = d;
					out	  = c;
					found = true;
				}
			}
		}
		return found;
	}

	SnapResult SnapEngine::snapAngle(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor) const {
		const ::Foundation::Vec2 prev = points.back();
		const float			   dx	= cursor.x - prev.x;
		const float			   dy	= cursor.y - prev.y;
		const float			   dist = std::sqrt(dx * dx + dy * dy);
		if (dist <= 0.0F) {
			return {cursor, SnapKind::None};
		}

		// Reference direction: the previous segment if there is one, else +x axis.
		float refAngle = 0.0F;
		if (points.size() >= 2) {
			const ::Foundation::Vec2 before = points[points.size() - 2];
			refAngle					  = std::atan2(prev.y - before.y, prev.x - before.x);
		}

		const float increment = snapping_->angleIncrementDegrees * (static_cast<float>(std::numbers::pi) / 180.0F);
		if (increment <= 0.0F) {
			return {cursor, SnapKind::None};
		}

		const float rawAngle	 = std::atan2(dy, dx);
		const float rel			 = rawAngle - refAngle;
		const float snappedRel	 = std::round(rel / increment) * increment;
		const float snappedAngle = refAngle + snappedRel;

		const ::Foundation::Vec2 snapped{prev.x + std::cos(snappedAngle) * dist, prev.y + std::sin(snappedAngle) * dist};

		SnapResult result;
		result.point	 = snapped;
		result.kind		 = SnapKind::Angle;
		result.guideFrom = prev;
		result.guideTo	 = snapped;
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

} // namespace engine::construction
