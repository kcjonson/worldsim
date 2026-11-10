#pragma once

// EntityFactory - the one place that builds the entities the sim creates at runtime: colonists,
// stations, storage and other world things, ground piles, and packaged items. The game's landing,
// spawn drains, placement tool, and dev verbs all create through it.

#include <ecs/EntityID.h>
#include <ecs/components/Skills.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {

	class World;

	class EntityFactory {
	  public:
		/// placementExecutor owns the harvestable resource pools a world entity seeds.
		EntityFactory(World& ecsWorld, assets::PlacementExecutor& placementExecutor);

		/// The skills a colonist starts with when nothing rolled a set for it.
		[[nodiscard]] static Skills starterSkills();

		/// A colonist with needs, inventory, memory, and the AI's task state. Its attributes are
		/// rolled from the entity id, so a given id always rolls the same colonist.
		EntityID createColonist(glm::vec2 position, const std::string& name, Skills skills);

		/// A world thing from its def: a crafting station gets a work queue and a material store,
		/// storage gets an inventory and accept-everything rules, a harvestable gets its resource
		/// pool rolled from the position.
		EntityID createWorldEntity(const std::string& defName, glm::vec2 position);

		/// Haulable ground piles of quantity items, split at the item's stack size into piles
		/// spread around position. The one ground-pile drop path.
		void dropResourcePiles(const std::string& defName, glm::vec2 position, uint32_t quantity);

		/// A packaged item, at target when it has one (a colonist then installs it there), else at
		/// position to wait for the player to place it.
		EntityID createPackaged(const std::string& defName, glm::vec2 position, std::optional<glm::vec2> target);

	  private:
		World&					   world;
		assets::PlacementExecutor& placement;
	};

} // namespace ecs
