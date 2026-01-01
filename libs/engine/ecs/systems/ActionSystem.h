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

	/// Set callback for removing harvested entities from the world
	/// Called when a destructive harvest completes (entity should be removed)
	using RemoveEntityCallback = std::function<void(const std::string& defName, float x, float y)>;
	void setRemoveEntityCallback(RemoveEntityCallback callback) { m_onRemoveEntity = std::move(callback); }

	/// Set callback for setting harvest cooldown on entities
	/// Called when a non-destructive harvest completes (entity enters regrowth)
	using SetCooldownCallback = std::function<void(const std::string& defName, float x, float y, float cooldownSeconds)>;
	void setEntityCooldownCallback(SetCooldownCallback callback) { m_onSetCooldown = std::move(callback); }

	/// Set callback for decrementing entity resource count
	/// Called when harvesting an entity with resource pool. Returns true if resources remain.
	using DecrementResourceCallback = std::function<bool(const std::string& defName, float x, float y)>;
	void setDecrementResourceCallback(DecrementResourceCallback callback) { m_onDecrementResource = std::move(callback); }

private:
	/// Random number generator for yield calculations
	std::mt19937 m_rng;

	/// Start an action based on the task's need type
	void startAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Memory& memory,
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

	/// Start a crafting action based on the task's recipe
	void startCraftAction(
		struct Task& task,
		struct Action& action,
		const struct Inventory& inventory
	);

	/// Start a gather action (pickup or harvest) for crafting materials
	void startGatherAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Memory& memory
	);

	/// Start a haul action (pickup from source, then deposit to storage)
	void startHaulAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Memory& memory
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

	/// Callback for removing harvested entities from world
	RemoveEntityCallback m_onRemoveEntity = nullptr;

	/// Callback for setting harvest cooldown on entities
	SetCooldownCallback m_onSetCooldown = nullptr;

	/// Callback for decrementing entity resource count
	DecrementResourceCallback m_onDecrementResource = nullptr;
};

} // namespace ecs
