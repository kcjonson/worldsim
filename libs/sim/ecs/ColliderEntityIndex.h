#pragma once

// The ECS entities that are placed obstacles: every entity with a Position and an Appearance whose
// def has a blocking collider at its age class (a parts def's collider rings, rotatable-placed-items.md
// 11.1), movers (AgentRadius) and packaged things (Packaged) aside. Each is held as the
// assets::PlacedEntity the placement executor would hold for it (def, position, rotation, scale, age
// class), so nav, collision, and sight put each of its rings (nav::placedObstacleColliders) in the
// world through nav::placedObstacleOutline like any placed obstacle (rotatable-placed-items.md 11.2).
//
// Derived data: refresh() reads the ECS and diffs it against what the index held, so it is never
// saved and a world whose entities were restored is indexed by its first refresh. Nothing creating,
// moving, or destroying an entity has to tell it.

#include "EntityID.h"

#include <assets/placement/SpatialIndex.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
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
		void					  queryRect(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const;
		[[nodiscard]] std::size_t size() const { return entries.size(); }
		/// Moves on every refresh that changed anything, so a reader deriving from the entries re-reads them only then.
		[[nodiscard]] std::uint64_t revision() const { return revisionCount; }
		/// Every entry, in entity order.
		[[nodiscard]] const std::map<EntityID, Entry>& all() const { return entries; }

	  private:
		// Whether the entity's def has a collider at any age class, looked up in the registry only when
		// the entity is new or its def name changed, so the piles and props with none cost a compare a
		// refresh. A registry hot reload does not refresh it: a def that gains a collider mid-game leaves its
		// existing entities non-blocking until they are re-created (a stale true is harmless, since
		// refresh() re-checks the def's colliders).
		[[nodiscard]] bool mayCollide(EntityID entity, const std::string& defName, const assets::AssetRegistry& registry);

		struct DefSeen {
			std::string	  defName;
			bool		  collides = false;
			std::uint64_t pass	   = 0; // the refresh that last saw the entity
		};

		std::map<EntityID, Entry>			  entries;
		std::unordered_map<EntityID, DefSeen> defsSeen;
		std::uint64_t						  pass = 0;
		std::uint64_t						  revisionCount = 0;
	};

} // namespace ecs
