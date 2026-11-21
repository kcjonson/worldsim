#pragma once

// One entity under a click, as SelectionSystem's stations and furniture loops weigh it (rotatable-placed-items 15).

#include <ecs/EntityID.h>

namespace world_sim {

	struct SelectionPick {
		ecs::EntityID entity = 0;		// 0: no candidate yet
		float		  anchorY = 0.0F;	// the depth it draws at under the click; larger draws later, in front
		float		  distance = 0.0F;	// from the click to the entity's position

		/// A total order over candidates: the larger anchor first, then the nearer, then the lesser entity id, so the
		/// winner never depends on the order the candidates come in. The empty pick beats nothing and loses to any.
		[[nodiscard]] bool beats(const SelectionPick& other) const {
			if (entity == 0) {
				return false;
			}
			if (other.entity == 0) {
				return true;
			}
			if (anchorY != other.anchorY) {
				return anchorY > other.anchorY;
			}
			if (distance != other.distance) {
				return distance < other.distance;
			}
			return entity < other.entity;
		}
	};

} // namespace world_sim
