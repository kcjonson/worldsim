#pragma once

// One entity under a click, as SelectionSystem's stations and furniture loops weigh it (rotatable-placed-items 15).

#include <ecs/EntityID.h>

#include <optional>

namespace world_sim {

	struct SelectionPick {
		ecs::EntityID		 entity = 0;
		float				 distance = 0.0F;	// from the click to the entity's position
		std::optional<float> frontAnchor;		// a placed parts item's: the depth it draws at under the click

		/// A total order: candidates with a front anchor first, the larger anchor (drawn later, so in front) first,
		/// then the rest by distance, nearer first; equal keys by distance, then by entity id, so the winner never
		/// depends on the order the candidates come in.
		[[nodiscard]] bool beats(const SelectionPick& other) const {
			if (other.entity == 0) {
				return entity != 0;
			}
			if (frontAnchor.has_value() != other.frontAnchor.has_value()) {
				return frontAnchor.has_value();
			}
			if (frontAnchor.has_value() && *frontAnchor != *other.frontAnchor) {
				return *frontAnchor > *other.frontAnchor;
			}
			if (distance != other.distance) {
				return distance < other.distance;
			}
			return entity < other.entity;
		}
	};

} // namespace world_sim
