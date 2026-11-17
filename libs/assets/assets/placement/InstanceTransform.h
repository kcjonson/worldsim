#pragma once

#include <glm/vec2.hpp>

#include <cmath>

// The one local<->world affine for an asset instance:
//
//     world = R(rotation) * (local * scale) + position
//
// `local` is in the def's pivot frame (AssetRegistry builds every template mesh and collision
// shape about the def's pivot), and an entity's position is its pivot in the world, static or
// dynamic alike. Every C++ consumer places a mesh, a silhouette, or a collider through this:
// the baked and batched statics, the animated path, the selection outline and hit-test, the
// placement ghost and footprint, the canopy measure, and nav's placed obstacles. The GPU
// instancing shader (salal) applies the same formula; BakedEntityMesh's test pins it.

namespace assets {

	class InstanceTransform {
	  public:
		InstanceTransform(glm::vec2 position, float rotation, float scale)
			: position(position),
			  scale(scale),
			  cosRotation(rotation == 0.0F ? 1.0 : std::cos(static_cast<double>(rotation))),
			  sinRotation(rotation == 0.0F ? 0.0 : std::sin(static_cast<double>(rotation))) {}

		[[nodiscard]] glm::vec2 toWorld(glm::vec2 local) const {
			const float c  = static_cast<float>(cosRotation);
			const float s  = static_cast<float>(sinRotation);
			const float sx = local.x * scale;
			const float sy = local.y * scale;
			return {sx * c - sy * s + position.x, sx * s + sy * c + position.y};
		}

		// In double, for measures that keep the precision (the canopy grid).
		[[nodiscard]] glm::dvec2 toWorldPrecise(glm::dvec2 local) const {
			const double sx = local.x * scale;
			const double sy = local.y * scale;
			return {sx * cosRotation - sy * sinRotation + position.x, sx * sinRotation + sy * cosRotation + position.y};
		}

		// The inverse: a world point in the pivot frame. {0,0} at scale 0.
		[[nodiscard]] glm::vec2 toLocal(glm::vec2 world) const {
			if (scale == 0.0F) {
				return {0.0F, 0.0F};
			}
			const float c  = static_cast<float>(cosRotation);
			const float s  = static_cast<float>(sinRotation);
			const float dx = world.x - position.x;
			const float dy = world.y - position.y;
			return {(dx * c + dy * s) / scale, (-dx * s + dy * c) / scale};
		}

		// The pivot frame's x and y axes in the world: unit vectors, turned by the rotation.
		[[nodiscard]] glm::vec2 axisX() const { return {static_cast<float>(cosRotation), static_cast<float>(sinRotation)}; }
		[[nodiscard]] glm::vec2 axisY() const { return {-static_cast<float>(sinRotation), static_cast<float>(cosRotation)}; }

		glm::vec2 position;
		float	  scale;

	  private:
		double cosRotation;
		double sinRotation;
	};

	// An instance that is never turned or scaled: what the player places (furniture, stations) is
	// drawn upright at scale 1.
	[[nodiscard]] inline InstanceTransform uprightInstance(glm::vec2 position) {
		return {position, 0.0F, 1.0F};
	}

} // namespace assets
