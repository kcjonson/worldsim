#include "session/EntityFactory.h"

#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/AnimationState.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Knowledge.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>

#include <bit>
#include <cstdint>
#include <random>
#include <utility>

namespace ecs {

	EntityFactory::EntityFactory(World& ecsWorld, assets::PlacementExecutor& placementExecutor)
		: world(ecsWorld),
		  placement(placementExecutor) {}

	Skills EntityFactory::starterSkills() {
		Skills skills;
		skills.setLevel("Farming", 3.0F);
		skills.setLevel("Crafting", 2.0F);
		skills.setLevel("Construction", 1.0F);
		return skills;
	}

	EntityID EntityFactory::createColonist(glm::vec2 position, const std::string& name, Skills skills) {
		const EntityID entity = world.createEntity();

		world.addComponent<Position>(entity, Position{position});
		world.addComponent<Rotation>(entity, Rotation{0.0F});
		world.addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world.addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world.addComponent<FacingDirection>(entity, FacingDirection{});
		world.addComponent<AnimationState>(entity, AnimationState{});
		world.addComponent<Appearance>(entity, Appearance{"Colonist", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world.addComponent<Colonist>(entity, Colonist{name});
		world.addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());

		// Attributes run 0-20. Only Strength is read today, for carry capacity.
		Attributes	 attributes;
		std::mt19937 attributeRng(static_cast<uint32_t>(entity));
		attributes.strength = std::uniform_real_distribution<float>(6.0F, 14.0F)(attributeRng);
		world.addComponent<Attributes>(entity, attributes);

		auto inventory = Inventory::createForColonist();
		inventory.carryCapacityKg = Attributes::carryCapacityKg(attributes.strength);
		world.addComponent<Inventory>(entity, std::move(inventory));
		world.addComponent<Knowledge>(entity, Knowledge{});
		world.addComponent<Memory>(entity, Memory{.owner = entity});
		world.addComponent<Task>(entity, Task{});
		world.addComponent<DecisionTrace>(entity, DecisionTrace{});
		world.addComponent<Action>(entity, Action{});
		world.addComponent<AgentRadius>(entity, AgentRadius{});
		world.addComponent<Skills>(entity, std::move(skills));

		LOG_INFO(Engine, "Spawned colonist '%s' at (%.1f, %.1f)", name.c_str(), position.x, position.y);
		return entity;
	}

	EntityID EntityFactory::createWorldEntity(const std::string& defName, glm::vec2 position) {
		const EntityID entity = world.createEntity();
		world.addComponent<Position>(entity, Position{position});
		world.addComponent<Rotation>(entity, Rotation{0.0F});
		world.addComponent<Appearance>(entity, Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});

		const auto* def = assets::AssetRegistry::Get().getDefinition(defName);

		if (def != nullptr && def->capabilities.craftable.has_value()) {
			// Colonists haul recipe inputs into the station's store, and Craft consumes them there.
			world.addComponent<WorkQueue>(entity, WorkQueue{});
			world.addComponent<Inventory>(entity, Inventory::createForStorage());
			LOG_INFO(Engine, "Spawned station '%s' at (%.1f, %.1f)", defName.c_str(), position.x, position.y);
		} else if (def != nullptr && def->capabilities.storage.has_value()) {
			const auto& storage = def->capabilities.storage.value();
			Inventory	inventory{};
			inventory.maxCapacity = storage.maxCapacity; // slot count; each item's own stackSize caps a stack
			world.addComponent<Inventory>(entity, inventory);
			world.addComponent<StorageConfiguration>(
				entity,
				storage.acceptedCategories.empty() ? StorageConfiguration::createAcceptEverything()
												   : StorageConfiguration::createAcceptAll(storage.acceptedCategories)
			);
			LOG_INFO(Engine, "Spawned storage '%s' at (%.1f, %.1f)", defName.c_str(), position.x, position.y);
		} else {
			LOG_INFO(Engine, "Spawned '%s' at (%.1f, %.1f)", defName.c_str(), position.x, position.y);
		}

		// A harvestable needs its resource pool seeded the way chunk placement seeds it, or a chop
		// withdraws nothing.
		if (def != nullptr && def->capabilities.harvestable.has_value()) {
			const auto& harvestable = def->capabilities.harvestable.value();
			if (harvestable.totalResourceMin > 0 && harvestable.totalResourceMax > 0) {
				// Seeded from the position's raw float bits (std::hash<float> is implementation-defined),
				// so a given spot rolls the same amount on every run.
				const uint32_t							bx = std::bit_cast<uint32_t>(position.x);
				const uint32_t							by = std::bit_cast<uint32_t>(position.y);
				std::mt19937							rng((bx * 0x9E3779B9U) ^ (by + 0x85EBCA6BU + (bx << 6) + (bx >> 2)));
				std::uniform_int_distribution<uint32_t> amount(harvestable.totalResourceMin, harvestable.totalResourceMax);
				placement.initResourceCount(world::worldToChunk({position.x, position.y}), {position.x, position.y}, defName, amount(rng));
			}
		}

		return entity;
	}

	void EntityFactory::dropResourcePiles(const std::string& defName, glm::vec2 position, uint32_t quantity) {
		const auto*	   def = assets::AssetRegistry::Get().getDefinition(defName);
		const uint32_t cap = (def != nullptr && def->itemProperties.has_value()) ? def->itemProperties->stackSize : UINT32_MAX;
		const auto	   piles = resourcePileDrops(cap, quantity, position);
		for (const auto& [at, count] : piles) {
			const EntityID entity = createWorldEntity(defName, at);
			world.addComponent<ResourceStack>(entity, ResourceStack{count});
		}
		LOG_INFO(Engine, "Dropped %u x '%s' as %zu stacks at (%.1f, %.1f)", quantity, defName.c_str(), piles.size(), position.x, position.y);
	}

	EntityID EntityFactory::createPackaged(const std::string& defName, glm::vec2 position, std::optional<glm::vec2> target) {
		const EntityID entity = createWorldEntity(defName, target.value_or(position));
		world.addComponent<Packaged>(entity, Packaged{.targetPosition = target});
		return entity;
	}

} // namespace ecs
