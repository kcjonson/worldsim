#pragma once

// Action System for Colonist Need Fulfillment
// Processes colonists that have arrived at their task targets and performs
// the appropriate action (eating, drinking, sleeping, toilet).
// Actions have duration and apply effects when complete.
// See /docs/design/game-systems/colonists/needs.md for design details.

#include "../EntityID.h"
#include "../ISystem.h"

#include <glm/vec2.hpp>

#include <functional>
#include <optional>
#include <random>
#include <string>

namespace ecs {

class ActionSystem : public ISystem {
public:
	/// Construct ActionSystem with optional RNG seed (for deterministic testing)
	explicit ActionSystem(std::optional<uint32_t> rngSeed = std::nullopt)
		: m_rng(rngSeed.value_or(std::random_device{}())) {}

	void update(float deltaTime) override;

	/// Priority 350: Runs after movement/physics, processes arrived colonists
	[[nodiscard]] int priority() const override { return 350; }
	[[nodiscard]] const char* name() const override { return "Action"; }

	/// Set callback for item crafted notifications
	/// Called with item label when colonist finishes crafting something
	using ItemCraftedCallback = std::function<void(const std::string& itemLabel)>;
	void setItemCraftedCallback(ItemCraftedCallback callback) { m_onItemCrafted = std::move(callback); }

	/// Set callback for dropping items on the ground
	/// Called when colonist crafts an item that can't fit in inventory (handsRequired >= 2)
	using DropItemCallback = std::function<void(const std::string& defName, float x, float y)>;
	void setDropItemCallback(DropItemCallback callback) { m_onDropItem = std::move(callback); }

	/// Set callback for dropping a loose, haulable resource pile of `quantity` units.
	/// Called when a fell yields more than the colonist could carry off: the remainder lands
	/// as a single ground entity carrying a ResourceStack, hauled away in later armfuls. Unlike
	/// DropItemCallback (crafted output awaiting placement), this pile is not Packaged.
	using DropResourceCallback = std::function<void(const std::string& defName, float x, float y, uint32_t quantity)>;
	void setDropResourceCallback(DropResourceCallback callback) { m_onDropResource = std::move(callback); }

	/// Set callback for removing harvested entities from the world
	/// Called when a destructive harvest completes (entity should be removed)
	using RemoveEntityCallback = std::function<void(const std::string& defName, float x, float y)>;
	void setRemoveEntityCallback(RemoveEntityCallback callback) { m_onRemoveEntity = std::move(callback); }

	/// Set callback for setting harvest cooldown on entities
	/// Called when a non-destructive harvest completes (entity enters regrowth)
	using SetCooldownCallback = std::function<void(const std::string& defName, float x, float y, float cooldownSeconds)>;
	void setEntityCooldownCallback(SetCooldownCallback callback) { m_onSetCooldown = std::move(callback); }

	/// Result of withdrawing from a harvestable's resource pool.
	struct ResourceDraw {
		uint32_t removed = 0;	  // Units actually taken (clamped to what the pool held)
		bool	 depleted = false; // True if the pool is now empty (entity should be removed)
	};

	/// Set callback for withdrawing from an entity's resource pool.
	/// Called when harvesting an entity with a resource pool. `requested` is how many yield
	/// units the colonist wants this chop; the callback removes up to that from the pool and
	/// reports how many it actually took plus whether the pool is now empty.
	using DecrementResourceCallback = std::function<ResourceDraw(const std::string& defName, float x, float y, uint32_t requested)>;
	void setDecrementResourceCallback(DecrementResourceCallback callback) { m_onDecrementResource = std::move(callback); }

	/// Set callback fired when a Build action completes a structure (workDone reached workTotal).
	/// ActionSystem lives in libs/engine and does not know ConstructionWorld; the app wires this
	/// to flip the ConstructionWorld structure state to Built and mark its render dirty.
	/// The StructureBlueprint component's phase is already set to Complete before this fires.
	using StructureCompletedCallback = std::function<void(EntityID blueprintEntity)>;
	void setStructureCompletedCallback(StructureCompletedCallback callback) { m_onStructureCompleted = std::move(callback); }

	/// Set callback fired when a Deconstruct action finishes tearing down a structure
	/// (workDone reached 0). The app wires this to remove the structure from ConstructionWorld,
	/// issue the material refund, and run the demolish cascade. See building-construction D7.
	using StructureDeconstructedCallback = std::function<void(EntityID blueprintEntity)>;
	void setStructureDeconstructedCallback(StructureDeconstructedCallback callback) {
		m_onStructureDeconstructed = std::move(callback);
	}

private:
	/// Random number generator for yield calculations
	std::mt19937 m_rng;

	/// Start an action based on the task's need type
	void startAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct NeedsComponent& needs,
		const struct Inventory& inventory
	);

	/// Start a need-fulfillment action (Hunger/Thirst/Energy/Bladder/Digestion).
	/// Extracted from startAction's FulfillNeed switch; handles the inventory-food, harvestable-food,
	/// drink, sleep, and toilet cases.
	void startNeedAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct NeedsComponent& needs,
		const struct Inventory& inventory
	);

	/// Process an in-progress action
	void processAction(
		float deltaTime,
		struct Action& action,
		struct NeedsComponent& needs,
		struct Task& task
	);

	/// Complete an action and apply effects
	void completeAction(
		EntityID entity,
		struct Action& action,
		struct NeedsComponent& needs,
		struct Task& task,
		struct Inventory& inventory,
		struct Memory& memory
	);

	/// Apply a completed action's need-restoration effect (primary + side effect).
	void applyNeedEffect(const struct Action& action, struct NeedsComponent& needs);

	/// Apply a completed collection effect (Pickup/Harvest): pool-withdraw vs single-shot,
	/// source removal/cooldown, and harvest-goal delivery recording.
	void applyCollectionEffect(
		const struct Action& action,
		struct Task& task,
		struct Inventory& inventory,
		struct Memory& memory
	);

	/// Apply a completed consumption effect (Eat): remove item, restore need + side effect.
	void applyConsumptionEffect(const struct Action& action, struct NeedsComponent& needs, struct Inventory& inventory);

	/// Apply a completed spawn effect (pooping spawns a Bio Pile).
	void applySpawnEffect(const struct Action& action);

	/// Apply a completed crafting effect: consume inputs, add/drop outputs, notify, update WorkQueue.
	void applyCraftingEffect(const struct Action& action, struct Inventory& inventory);

	/// Apply a completed deposit effect (storage deposit or deliver-to-craft-station).
	void applyDepositEffect(const struct Action& action, struct Task& task, struct Inventory& inventory);

	/// Apply a completed construction progress effect (Build/Deconstruct completion signal).
	void applyProgressEffect(const struct Action& action);

	/// Apply a completed PlacePackaged effect (phase 2): move the carried entity to target and unpackage.
	void applyPlacePackagedEffect(const struct Action& action, struct Inventory& inventory);

	/// Start a crafting action based on the task's recipe
	void startCraftAction(
		struct Task& task,
		struct Action& action,
		const struct Inventory& inventory
	);

	/// Start a Build or Deconstruct action on the task's blueprint.
	/// Reads the colonist's Construction skill (if any) to scale the work rate, and validates
	/// the blueprint is in a workable phase. @param entity is the builder (for skill lookup).
	void startBuildAction(
		EntityID entity,
		struct Task& task,
		struct Action& action
	);

	/// Advance continuous construction work on the blueprint targeted by a Build/Deconstruct
	/// action. Moves StructureBlueprint.workDone by constructionWorkRate(skill) x dt (up for
	/// Build, down for Deconstruct), clamps it, and marks the action Complete when the work
	/// bound is reached. Separated from processAction because completion is gated by workDone,
	/// not elapsed time. @return true if the action reached completion this tick.
	bool advanceConstructionWork(float deltaTime, struct Action& action);

	/// Start a gather action (pickup or harvest) for crafting materials
	void startGatherAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Memory& memory
	);

	/// Start a harvest action for goal-driven harvesting (crafting materials)
	/// Finds harvestable at target position that yields the required item type.
	/// Memory is mutable so a vanished target can be forgotten (prevents re-selection).
	void startHarvestAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct Inventory& inventory
	);

	/// Start a haul action (pickup from source, then deposit to storage)
	/// Memory is mutable so a vanished pickup target can be forgotten.
	void startHaulAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory
	);

	/// Start a place packaged action (pickup packaged item, then place at target)
	/// Two-phase task like Haul: Phase 1 = PickupPackaged at source, Phase 2 = PlacePackaged at target
	/// Uses both position and inventory state to determine which phase to execute
	void startPlacePackagedAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Inventory& inventory
	);

	/// Clear colonist hands for two-handed pickup
	/// Stows items to backpack if possible, drops if not (2-handed or full backpack)
	/// @param inventory Colonist's inventory to modify
	/// @param dropPosition Position to drop items that can't be stowed
	void clearHandsForTwoHandedPickup(
		struct Inventory& inventory,
		glm::vec2 dropPosition
	);

	/// Clear a single hand slot
	/// Helper for clearHandsForTwoHandedPickup - stows to backpack or drops
	/// @param handSlot The hand slot to clear (leftHand or rightHand)
	/// @param inventory Colonist's inventory for backpack storage
	/// @param dropPosition Position to drop items that can't be stowed
	/// @param handName Name for logging ("left" or "right")
	void clearHandItem(
		std::optional<struct ItemStack>& handSlot,
		struct Inventory& inventory,
		glm::vec2 dropPosition,
		const char* handName
	);

	/// Callback for item crafted notifications
	ItemCraftedCallback m_onItemCrafted = nullptr;

	/// Callback for dropping items on the ground (non-backpackable items)
	DropItemCallback m_onDropItem = nullptr;

	/// Callback for dropping a loose, haulable resource pile (felling remainder)
	DropResourceCallback m_onDropResource = nullptr;

	/// Callback for removing harvested entities from world
	RemoveEntityCallback m_onRemoveEntity = nullptr;

	/// Callback for setting harvest cooldown on entities
	SetCooldownCallback m_onSetCooldown = nullptr;

	/// Callback for decrementing entity resource count
	DecrementResourceCallback m_onDecrementResource = nullptr;

	/// Callback fired when a Build action completes a structure
	StructureCompletedCallback m_onStructureCompleted = nullptr;

	/// Callback fired when a Deconstruct action finishes tearing down a structure
	StructureDeconstructedCallback m_onStructureDeconstructed = nullptr;
};

} // namespace ecs
