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
// the baked and batched statics, the animated path, piles, the packaged crate, the depth anchor,
// the selection outline and hit-test, the placement ghost and footprint, the canopy measure, and
// nav's placed obstacles. The GPU instancing shader (salal) applies the same formula;
// InstanceTransform.BakedStaticsEmitIt pins the bake to it.

namespace assets {

	class InstanceTransform {
	  public:
		InstanceTransform(glm::vec2 instancePosition, float rotation, float instanceScale)
			: position(instancePosition),
			  scale(instanceScale),
			  cosine(rotation == 0.0F ? 1.0 : std::cos(static_cast<double>(rotation))),
			  sine(rotation == 0.0F ? 0.0 : std::sin(static_cast<double>(rotation))) {}

		[[nodiscard]] glm::vec2 toWorld(glm::vec2 local) const {
			const float cosineF = static_cast<float>(cosine);
			const float sineF	= static_cast<float>(sine);
			const float scaledX = local.x * scale;
			const float scaledY = local.y * scale;
			return {scaledX * cosineF - scaledY * sineF + position.x, scaledX * sineF + scaledY * cosineF + position.y};
		}

		// In double, for measures that keep the precision (the canopy grid).
		[[nodiscard]] glm::dvec2 toWorldPrecise(glm::dvec2 local) const {
			const double scaledX = local.x * scale;
			const double scaledY = local.y * scale;
			return {scaledX * cosine - scaledY * sine + position.x, scaledX * sine + scaledY * cosine + position.y};
		}

		// The inverse: a world point in the pivot frame. {0,0} at scale 0.
		[[nodiscard]] glm::vec2 toLocal(glm::vec2 world) const {
			if (scale == 0.0F) {
				return {0.0F, 0.0F};
			}
			const float cosineF = static_cast<float>(cosine);
			const float sineF	= static_cast<float>(sine);
			const float offsetX = world.x - position.x;
			const float offsetY = world.y - position.y;
			return {(offsetX * cosineF + offsetY * sineF) / scale, (-offsetX * sineF + offsetY * cosineF) / scale};
		}

		// The pivot frame's x and y axes in the world: unit vectors, turned by the rotation.
		[[nodiscard]] glm::vec2 axisX() const { return {static_cast<float>(cosine), static_cast<float>(sine)}; }
		[[nodiscard]] glm::vec2 axisY() const { return {-static_cast<float>(sine), static_cast<float>(cosine)}; }

		glm::vec2 position;
		float	  scale;

	  private:
		double cosine;
		double sine;
	};

	// An instance that is never turned or scaled: what the player places (furniture, stations) is
	// drawn upright at scale 1.
	[[nodiscard]] inline InstanceTransform uprightInstance(glm::vec2 position) {
		return {position, 0.0F, 1.0F};
	}

} // namespace assets
