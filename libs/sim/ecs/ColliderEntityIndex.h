#pragma once

// The ECS entities that are placed obstacles: every entity with a Position and an Appearance whose
// def has a blocking collider at its age class, movers (AgentRadius) aside. Each is held as the
// assets::PlacedEntity the placement executor would hold for it (def, position, rotation, scale, age
// class), so nav, collision, and sight put it in the world through nav::placedObstacleOutline like
// any placed obstacle (rotatable-placed-items.md 11.2).
//
// Derived data: refresh() reads the ECS and diffs it against what the index held, so it is never
// saved and a world whose entities were restored is indexed by its first refresh. Nothing creating,
// moving, or destroying an entity has to tell it.

#include "EntityID.h"

#include <assets/placement/SpatialIndex.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <map>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	class World;

	class ColliderEntityIndex {
	  public:
		struct Entry {
			EntityID			 entity = kInvalidEntity;
			assets::PlacedEntity placed;
		};

		// What a refresh changed, each list in entity order. An entity that left, lost its collider, or
		// changed def, position, rotation, scale, or age class is a removal of the entry it had; one that
		// came, gained a collider, or changed is an addition of its new entry.
		struct Changes {
			std::vector<Entry> removed;
			std::vector<Entry> added;
		};

		// Brings the index to what `world` holds.
		[[nodiscard]] Changes refresh(World& world, const assets::AssetRegistry& registry);

		[[nodiscard]] const Entry* find(EntityID entity) const;
		// The entries whose position lies in the box, in entity order, appended to `out`. A linear walk:
		// the index holds what spawning creates, a handful against the placement executor's thousands.
		void					  queryRect(glm::vec2 lo, glm::vec2 hi, std::vector<const Entry*>& out) const;
		[[nodiscard]] std::size_t size() const { return entries.size(); }

	  private:
		std::map<EntityID, Entry> entries;
	};

} // namespace ecs
