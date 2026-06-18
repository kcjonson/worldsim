#include "GeometryIndex.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace ecs {

	namespace {

		namespace cw = engine::construction;

		// Centerline lerp between integer-mm endpoints at parameter t, rounded to mm
		// (same rounding policy as NavInputBuilder's flank lerp).
		geometry::Vec2i64 lerp(const geometry::Vec2i64& a, const geometry::Vec2i64& b, float t) {
			const double ax = static_cast<double>(a.x);
			const double ay = static_cast<double>(a.y);
			const double bx = static_cast<double>(b.x);
			const double by = static_cast<double>(b.y);
			return {static_cast<std::int64_t>(std::llround(ax + (bx - ax) * static_cast<double>(t))),
					static_cast<std::int64_t>(std::llround(ay + (by - ay) * static_cast<double>(t)))};
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
		m_segments.clear();
		m_openings.clear();
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
		m_occluders.clear();
		m_segments.clear();
		m_openings.clear();

		const cw::ConstructionWorld&			   world = *m_world;
		const engine::assets::ConstructionRegistry& reg	  = engine::assets::ConstructionRegistry::Get();

		// A transparent gap span on a segment's centerline, in [0,1] from v0->v1.
		struct Gap {
			float t0;
			float t1;
		};

		// Pre-group built openings by host segment so the per-segment loop is
		// O(segments + openings), not O(segments * openings). (Mirrors how
		// NavInputBuilder groups its door cuts by segment.)
		std::unordered_map<cw::SegmentId, std::vector<const cw::Opening*>> openingsBySegment;
		for (const cw::Opening& op : world.openings()) {
			if (op.state == cw::FoundationState::Built) {
				openingsBySegment[op.segment].push_back(&op);
			}
		}

		for (const cw::WallSegment& seg : world.segments()) {
			if (seg.state != cw::FoundationState::Built) {
				continue; // blueprint walls do not occlude
			}
			const cw::Vertex* v0 = world.getVertex(seg.v0);
			const cw::Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const geometry::Vec2i64 a = v0->pos;
			const geometry::Vec2i64 b = v1->pos;

			m_segments.push_back({seg.id, a, b});

			const double dx		  = static_cast<double>(b.x - a.x);
			const double dy		  = static_cast<double>(b.y - a.y);
			const double lengthMm = std::sqrt(dx * dx + dy * dy);
			if (lengthMm <= 0.0) {
				continue; // degenerate; a built zero-length wall should not exist
			}

			// Gather this segment's BUILT transparent-to-sight openings as gap spans.
			// The half-extent formula matches NavInputBuilder::extractWalls exactly, so
			// a given opening's sight gap and nav gap span the same centerline range.
			std::vector<Gap> gaps;
			auto			 openIt = openingsBySegment.find(seg.id);
			if (openIt != openingsBySegment.end()) {
				for (const cw::Opening* op : openIt->second) {
					const engine::assets::OpeningTypeDef* type = reg.getOpeningType(op->type);
					if (type == nullptr) {
						continue;
					}
					const float halfExtent = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
					const float t0		   = std::clamp(op->t - halfExtent, 0.0F, 1.0F);
					const float t1		   = std::clamp(op->t + halfExtent, 0.0F, 1.0F);

					m_openings.push_back({op->id, seg.id, op->type, lerp(a, b, t0), lerp(a, b, t1), type->transparentToSight});

					if (type->transparentToSight) {
						gaps.push_back({t0, t1});
					}
				}
			}

			// Subtract the gap spans from [0,1] and emit each remaining solid sub-span
			// as one opaque occluder along the centerline. With no transparent opening
			// this is the single full-centerline occluder.
			std::sort(gaps.begin(), gaps.end(), [](const Gap& l, const Gap& r) { return l.t0 < r.t0; });

			float cursor = 0.0F;
			for (const Gap& g : gaps) {
				if (g.t0 > cursor + 1e-6F) {
					m_occluders.push_back({seg.id, {lerp(a, b, cursor), lerp(a, b, g.t0)}});
				}
				cursor = std::max(cursor, g.t1);
			}
			if (cursor < 1.0F - 1e-6F) {
				m_occluders.push_back({seg.id, {lerp(a, b, cursor), lerp(a, b, 1.0F)}});
			}
		}
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

} // namespace ecs
