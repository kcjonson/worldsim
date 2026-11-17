#pragma once

#include <glm/vec2.hpp>

#include <cmath>

// The single local<->world affine for an asset instance, shared by every render path,
// the selection outline (forward, local->world), the selection hit-test (inverse,
// world->local), and a placed entity's collider, so they can never drift:
//
//     world = R(rotation) * (local * scale) + position
//
// `local` is in the def's pivot frame (AssetRegistry builds every template mesh and
// collision shape about the def's pivot), and an entity's position is its pivot in the
// world, static or dynamic alike; a dynamic entity has rotation 0.
//
// The baked static loop (BakedEntityMesh), the GPU instancing shader, and
// nav::placedObstacleBox apply this same formula; this struct is the reference
// definition and is guarded against drift by AssetInstanceTransform.test.cpp.

namespace worldview {

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

	inline AssetInstanceTransform instanceTransform(glm::vec2 position, float rotation, float scale) {
		return {position, rotation, scale};
	}

} // namespace worldview
