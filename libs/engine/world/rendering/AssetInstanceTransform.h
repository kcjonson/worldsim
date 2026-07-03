#pragma once

#include <glm/vec2.hpp>

#include <cmath>

// The single local<->world affine for an asset instance, shared by the dynamic
// ECS render path (placed.position), the selection outline (forward, local->world),
// and the selection hit-test (inverse, world->local) so they can never drift.
//
//     world = R(rotation) * (local * scale) + origin
//
// Two conventions map onto the one form via (origin, rotation):
//   Static / baked:  origin = entity.position,       rotation = entity.rotation   (uncentered)
//   Dynamic / ECS:   origin = pos.value - boundsCenter, rotation = 0
//     where boundsCenter = (min+max)/2 of the template mesh bbox (so the stored render
//     centerOffset = -boundsCenter). A null/empty template gives boundsCenter = {0,0},
//     collapsing origin to pos.value exactly as DynamicEntityRenderSystem does.
//
// The baked static loop (BakedEntityMesh) and the GPU instancing shader apply this
// same formula; this struct is the reference definition and is guarded against drift
// by AssetInstanceTransform.test.cpp.

namespace engine::world {

	struct AssetInstanceTransform {
		glm::vec2 origin{0.0F, 0.0F};
		float	  rotation = 0.0F;
		float	  scale	   = 1.0F;

		glm::vec2 toWorld(glm::vec2 local) const {
			const glm::vec2 s{local.x * scale, local.y * scale};
			if (rotation == 0.0F) {
				return {s.x + origin.x, s.y + origin.y};
			}
			const float c  = std::cos(rotation);
			const float sn = std::sin(rotation);
			return {s.x * c - s.y * sn + origin.x, s.x * sn + s.y * c + origin.y};
		}

		glm::vec2 toLocal(glm::vec2 world) const {
			glm::vec2 d{world.x - origin.x, world.y - origin.y};
			if (rotation != 0.0F) {
				const float c  = std::cos(rotation);
				const float sn = std::sin(rotation);
				d = {d.x * c + d.y * sn, -d.x * sn + d.y * c}; // R(-rotation)
			}
			if (scale == 0.0F) {
				return {0.0F, 0.0F};
			}
			return {d.x / scale, d.y / scale};
		}
	};

	inline AssetInstanceTransform staticInstanceTransform(glm::vec2 position, float rotation, float scale) {
		return {position, rotation, scale};
	}

	inline AssetInstanceTransform dynamicInstanceTransform(glm::vec2 posValue, float scale, glm::vec2 boundsCenter) {
		return {{posValue.x - boundsCenter.x, posValue.y - boundsCenter.y}, 0.0F, scale};
	}

} // namespace engine::world
