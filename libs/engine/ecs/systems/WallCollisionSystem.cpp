#include "WallCollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ecs {

namespace {

	namespace cons = engine::construction;

	// One built wall resolved to float meters: centerline endpoints, the band's
	// half-thickness, and its precomputed door-gap spans (the [t0,t1] centerline
	// ranges of built pathable openings). All resolved once per update() so the
	// per-agent inner loop stays cheap -- no registry lookups, no mm<->m
	// conversion, and no rescan of every opening per agent.
	struct WallBand {
		glm::vec2							 v0;
		glm::vec2							 v1;
		float								 halfThicknessMeters;
		std::vector<std::pair<float, float>> doorSpans;
	};

	constexpr float kMillimetersPerMeter = 1000.0F;

	// Convert an integer-mm vertex position to float meters (same rule as
	// nav::toMeters; duplicated here to avoid pulling the nav coords header,
	// which drags in chunk types this system doesn't need).
	glm::vec2 toMeters(const geometry::Vec2i64& mm) {
		return {static_cast<float>(static_cast<double>(mm.x) / kMillimetersPerMeter),
				static_cast<float>(static_cast<double>(mm.y) / kMillimetersPerMeter)};
	}

} // namespace

void WallCollisionSystem::update(float /*deltaTime*/) {
	if (m_construction == nullptr) {
		return; // not wired (e.g. headless tests, early init): nothing to collide with
	}

	const engine::assets::ConstructionRegistry& registry = engine::assets::ConstructionRegistry::Get();

	// Gather the BUILT wall bands once. A blueprint wall is not physical, so it
	// never collides (mirrors extractWalls' Built-only gate). A zero/unknown
	// thickness preset contributes no band.
	std::vector<WallBand> bands;
	bands.reserve(m_construction->segments().size());
	for (const cons::WallSegment& seg : m_construction->segments()) {
		if (seg.state != cons::FoundationState::Built) {
			continue;
		}
		const cons::Vertex* v0 = m_construction->getVertex(seg.v0);
		const cons::Vertex* v1 = m_construction->getVertex(seg.v1);
		if (v0 == nullptr || v1 == nullptr) {
			continue;
		}
		const auto* preset = registry.getThicknessPreset(seg.material, seg.thicknessPreset);
		if (preset == nullptr || preset->halfThicknessMm <= 0) {
			continue;
		}

		WallBand band;
		band.v0					 = toMeters(v0->pos);
		band.v1					 = toMeters(v1->pos);
		band.halfThicknessMeters = static_cast<float>(preset->halfThicknessMm) / kMillimetersPerMeter;

		// Precompute this segment's door gaps ONCE here, not per agent: the
		// centerline span [t0,t1] of every BUILT, PATHABLE opening. Windows
		// (non-pathable) and blueprint openings leave the band solid. The span uses
		// the same clear-width formula NavInputBuilder cuts the band at, so the
		// collision gap matches the navmesh gap the path was solved against.
		const float segLengthMm = glm::length(band.v1 - band.v0) * kMillimetersPerMeter;
		if (segLengthMm > 0.0F) {
			for (const cons::Opening& op : m_construction->openings()) {
				if (op.segment != seg.id || op.state != cons::FoundationState::Built) {
					continue;
				}
				const auto* type = registry.getOpeningType(op.type);
				if (type == nullptr || !type->pathable) {
					continue;
				}
				const float halfExtent = (static_cast<float>(type->widthMm) * 0.5F) / segLengthMm;
				band.doorSpans.emplace_back(std::clamp(op.t - halfExtent, 0.0F, 1.0F),
											std::clamp(op.t + halfExtent, 0.0F, 1.0F));
			}
		}
		bands.push_back(std::move(band));
	}

	if (bands.empty()) {
		return; // no built walls: no-op (and no per-agent work)
	}

	// Two relaxation iterations so an agent wedged into a corner between two
	// perpendicular walls settles against BOTH: iteration 1 clears the wall it
	// penetrates deepest, iteration 2 (re-projecting against the moved position)
	// clears the other. Same pattern as CollisionSystem.
	for (int iter = 0; iter < 2; ++iter) {
		for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
			glm::vec2 p = pos.value;
			const float r = radius.radiusMeters;

			for (const WallBand& band : bands) {
				const glm::vec2 d  = band.v1 - band.v0;
				const float		len2 = glm::dot(d, d);
				if (len2 <= 1e-12F) {
					continue; // degenerate segment (should not happen: ZeroLength is rejected)
				}

				// Closest point on segment [v0,v1] to p, clamped to the segment.
				float tProj = glm::dot(p - band.v0, d) / len2;
				tProj		= std::clamp(tProj, 0.0F, 1.0F);
				const glm::vec2 closest = band.v0 + d * tProj;

				const glm::vec2 toAgent = p - closest;
				const float		dist	= glm::length(toAgent);

				const float clearance = band.halfThicknessMeters + r;
				if (dist >= clearance) {
					continue; // disc doesn't overlap this wall's band
				}

				// Door-gap exemption: if the projection lands in one of this band's
				// precomputed pathable-opening spans, the wall has a real gap there
				// and the agent may pass through it.
				bool inDoorGap = false;
				for (const std::pair<float, float>& span : band.doorSpans) {
					if (tProj >= span.first && tProj <= span.second) {
						inDoorGap = true;
						break;
					}
				}
				if (inDoorGap) {
					continue;
				}

				const float segLength = std::sqrt(len2); // for the on-centerline normal fallback

				glm::vec2 normal;
				if (dist > 1e-4F) {
					normal = toAgent / dist; // unit perpendicular pointing from the line toward p
				} else {
					// On the centerline: `toAgent` has no usable direction. Pick a
					// deterministic perpendicular from the segment direction (rotate
					// +90 deg) so the push is repeatable rather than NaN.
					const glm::vec2 dir = d / segLength;
					normal				= {-dir.y, dir.x};
				}

				// Push the agent out along the normal to exactly clear the band: its
				// distance to the centerline becomes halfThickness + r.
				p += normal * (clearance - dist);
			}

			pos.value = p;
		}
	}
}

} // namespace ecs
