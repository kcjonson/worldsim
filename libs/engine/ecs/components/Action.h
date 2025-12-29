#pragma once

// Action Component for Colonist Activities
//
// Actions represent what a colonist is doing at a location. This design uses
// std::variant for polymorphic effect data - each action category has its own
// effect struct containing only relevant data.
//
// Design Rationale (see /docs/technical/ecs-patterns.md):
// - std::variant is ECS-idiomatic: no heap allocation, type-safe, contiguous memory
// - Each effect type contains only the data it needs (no confusing unused fields)
// - Adding new action categories = add a new effect struct + variant alternative
//
// Related docs:
// - /docs/design/game-systems/colonists/ai-behavior.md
// - /docs/design/game-systems/world/entity-capabilities.md

#include "Needs.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace ecs {

	// ============================================================================
	// Action Types (what the colonist is doing)
	// ============================================================================

	/// Action types that colonists can perform
	enum class ActionType : uint8_t {
		None = 0,

		// Need Fulfillment Actions
		Eat,	// Consuming food item from inventory
		Drink,	// Drinking from water tile (Pond)
		Sleep,	// Sleeping on ground or bed
		Toilet, // Using toilet or ground relief

		// Resource Collection Actions
		Pickup,	 // Pick up ground item directly into inventory
		Harvest, // Harvest from entity (bush, plant) into inventory

		// Work Actions
		Craft,	 // Creating items at workbench
		Deposit, // Depositing items into storage container
				 // Build,    // Constructing structures
				 // Repair,   // Fixing damaged structures
				 // Clean,    // Cleaning areas
	};

	/// Action state machine
	enum class ActionState : uint8_t {
		Starting,	// Action just began, initialization frame
		InProgress, // Action is ongoing
		Complete	// Action finished, ready for cleanup
	};

	// ============================================================================
	// Effect Types (what happens when the action completes)
	// ============================================================================

	/// Effect for need fulfillment actions (Eat, Drink, Sleep, Toilet)
	/// Restores one need, optionally affects another as a side effect.
	struct NeedEffect {
		/// The primary need being restored
		NeedType need = NeedType::Count;

		/// Amount to restore (0-100 scale)
		float restoreAmount = 0.0F;

		/// Optional side effect need (e.g., drinking affects bladder)
		NeedType sideEffectNeed = NeedType::Count;

		/// Side effect amount (positive = restore, negative = drain)
		float sideEffectAmount = 0.0F;
	};

	/// Effect for item collection actions (Pickup, Harvest)
	/// Adds items to inventory and optionally affects the source entity.
	struct CollectionEffect {
		/// Item definition name to add to inventory
		std::string itemDefName;

		/// Quantity of items to collect
		uint32_t quantity = 1;

		/// Position of the source entity (for removal/cooldown)
		glm::vec2 sourcePosition{0.0F, 0.0F};

		/// DefName of the source entity (for removal/cooldown)
		std::string sourceDefName;

		/// If true, source entity is destroyed after collection
		bool destroySource = true;

		/// If destroySource is false and this > 0, entity enters cooldown (regrowth)
		float regrowthTime = 0.0F;
	};

	/// Effect for consuming items from inventory (Eat action)
	/// Removes item from inventory and restores a need, with optional side effect.
	struct ConsumptionEffect {
		/// Item definition name to consume from inventory
		std::string itemDefName;

		/// Quantity to consume
		uint32_t quantity = 1;

		/// Which need to restore
		NeedType need = NeedType::Hunger;

		/// Amount to restore (0-100 scale)
		float restoreAmount = 30.0F;

		/// Optional side effect need (e.g., eating fills digestion)
		NeedType sideEffectNeed = NeedType::Count;

		/// Side effect amount (positive = restore, negative = drain)
		float sideEffectAmount = 0.0F;
	};

	/// Effect for progress actions (Build, Repair)
	/// Advances construction/repair progress. Stub for Phase 2+.
	struct ProgressEffect {
		/// Target entity being built/repaired
		uint64_t targetEntityId = 0;

		/// Amount of progress to add (0-1 scale)
		float progressAmount = 0.0F;
	};

	/// Effect for entity spawning (Toilet creates Bio Pile)
	struct SpawnEffect {
		/// Position to spawn entity
		glm::vec2 position{0.0F, 0.0F};

		// TODO: When entity spawning exists:
		// std::string entityDefName;
	};

	/// Effect for crafting actions (Craft at station)
	/// Consumes inputs from inventory and produces outputs.
	struct CraftingEffect {
		/// Recipe being crafted
		std::string recipeDefName;

		/// Station entity ID (for updating WorkQueue)
		uint64_t stationEntityId = 0;

		/// Input items to consume (defName -> count)
		std::vector<std::pair<std::string, uint32_t>> inputs;

		/// Output items to produce (defName -> count)
		std::vector<std::pair<std::string, uint32_t>> outputs;
	};

	/// Effect for deposit actions (putting items into storage containers)
	/// Moves item from colonist inventory to storage container inventory.
	struct DepositEffect {
		/// Item definition name to deposit
		std::string itemDefName;

		/// Quantity to deposit
		uint32_t quantity = 1;

		/// Target storage container entity ID
		uint64_t storageEntityId = 0;
	};

	/// Variant holding the effect data for the current action
	using ActionEffect =
		std::variant<std::monostate, NeedEffect, CollectionEffect, ConsumptionEffect, ProgressEffect, SpawnEffect, CraftingEffect, DepositEffect>;

	// ============================================================================
	// Action Component
	// ============================================================================

	/// Action component - tracks a colonist's current action and its effect
	struct Action {
		// --- Common action state (shared by all action types) ---

		ActionType	type = ActionType::None;
		ActionState state = ActionState::Starting;

		/// Duration of the action in seconds
		float duration = 0.0F;

		/// Elapsed time in this action (seconds)
		float elapsed = 0.0F;

		/// Target position (used for location-based effects like spawning)
		glm::vec2 targetPosition{0.0F, 0.0F};

		/// Whether this action can be interrupted by higher-priority tasks.
		/// Biological needs (Eat, Drink, Toilet) are NOT interruptable.
		/// Sleep can be interrupted for critical needs.
		bool interruptable = true;

		/// Whether this action spawns a Bio Pile on completion (for Toilet/poop)
		bool spawnBioPile = false;

		// --- Effect data (variant - contains type-specific data) ---

		ActionEffect effect;

		// --- Query methods ---

		/// Check if an action is currently in progress
		[[nodiscard]] bool isActive() const { return type != ActionType::None; }

		/// Check if action is complete
		[[nodiscard]] bool isComplete() const { return state == ActionState::Complete; }

		/// Get progress as 0.0 - 1.0
		[[nodiscard]] float progress() const {
			if (duration <= 0.0F) {
				return 1.0F;
			}
			return elapsed / duration;
		}

		/// Check if this action has a need effect
		[[nodiscard]] bool hasNeedEffect() const { return std::holds_alternative<NeedEffect>(effect); }

		/// Get the need effect (call hasNeedEffect() first or use std::get_if)
		[[nodiscard]] const NeedEffect& needEffect() const { return std::get<NeedEffect>(effect); }
		[[nodiscard]] NeedEffect&		needEffect() { return std::get<NeedEffect>(effect); }

		/// Check if this action has a spawn effect
		[[nodiscard]] bool hasSpawnEffect() const { return std::holds_alternative<SpawnEffect>(effect); }

		/// Get the spawn effect
		[[nodiscard]] const SpawnEffect& spawnEffect() const { return std::get<SpawnEffect>(effect); }

		/// Check if this action has a collection effect
		[[nodiscard]] bool hasCollectionEffect() const { return std::holds_alternative<CollectionEffect>(effect); }

		/// Get the collection effect
		[[nodiscard]] const CollectionEffect& collectionEffect() const { return std::get<CollectionEffect>(effect); }
		[[nodiscard]] CollectionEffect&		  collectionEffect() { return std::get<CollectionEffect>(effect); }

		/// Check if this action has a consumption effect
		[[nodiscard]] bool hasConsumptionEffect() const { return std::holds_alternative<ConsumptionEffect>(effect); }

		/// Get the consumption effect
		[[nodiscard]] const ConsumptionEffect& consumptionEffect() const { return std::get<ConsumptionEffect>(effect); }
		[[nodiscard]] ConsumptionEffect&	   consumptionEffect() { return std::get<ConsumptionEffect>(effect); }

		/// Check if this action has a crafting effect
		[[nodiscard]] bool hasCraftingEffect() const { return std::holds_alternative<CraftingEffect>(effect); }

		/// Get the crafting effect
		[[nodiscard]] const CraftingEffect& craftingEffect() const { return std::get<CraftingEffect>(effect); }
		[[nodiscard]] CraftingEffect&		craftingEffect() { return std::get<CraftingEffect>(effect); }

		/// Check if this action has a deposit effect
		[[nodiscard]] bool hasDepositEffect() const { return std::holds_alternative<DepositEffect>(effect); }

		/// Get the deposit effect
		[[nodiscard]] const DepositEffect& depositEffect() const { return std::get<DepositEffect>(effect); }
		[[nodiscard]] DepositEffect&	   depositEffect() { return std::get<DepositEffect>(effect); }

		// --- Mutation methods ---

		/// Reset action to default state
		void clear() {
			type = ActionType::None;
			state = ActionState::Starting;
			duration = 0.0F;
			elapsed = 0.0F;
			targetPosition = glm::vec2{0.0F, 0.0F};
			interruptable = true;
			spawnBioPile = false;
			effect = std::monostate{};
		}

		// --- Factory methods for creating actions ---

		/// Factory: Eat action - consume food from inventory
		/// Colonists always eat from inventory. Food must be harvested/collected first.
		/// Eating restores hunger and fills digestion (food enters gut).
		/// @param itemDefName Item to consume from inventory
		/// @param nutrition Amount of hunger to restore (0-1 scale)
		static Action Eat(const std::string& itemDefName, float nutrition) {
			Action action;
			action.type = ActionType::Eat;
			action.state = ActionState::Starting;
			action.duration = 2.0F;		  // 2 seconds to eat
			action.interruptable = false; // Can't stop mid-bite!

			ConsumptionEffect consumeEff;
			consumeEff.itemDefName = itemDefName;
			consumeEff.quantity = 1;
			consumeEff.need = NeedType::Hunger;
			consumeEff.restoreAmount = nutrition * 100.0F;
			// Eating fills the gut - digestion need DECREASES (becomes more urgent over time)
			consumeEff.sideEffectNeed = NeedType::Digestion;
			consumeEff.sideEffectAmount = -nutrition * 100.0F; // Negative = drain (fill gut)
			action.effect = consumeEff;

			return action;
		}

		/// Factory: Drink action - restores thirst fully, fills bladder
		/// Water tiles are inexhaustible, so drinking always fully restores thirst.
		static Action Drink() {
			Action action;
			action.type = ActionType::Drink;
			action.state = ActionState::Starting;
			action.duration = 1.5F;		  // 1.5 seconds to drink
			action.interruptable = false; // Can't stop mid-gulp!

			NeedEffect needEff;
			needEff.need = NeedType::Thirst;
			needEff.restoreAmount = 100.0F; // Full thirst restoration from water tiles
			needEff.sideEffectNeed = NeedType::Bladder;
			needEff.sideEffectAmount = -15.0F; // Drinking DECREASES bladder (fills it up)
			action.effect = needEff;

			return action;
		}

		/// Factory: Sleep action - restores energy
		/// @param quality Sleep quality affects restoration rate (0.5 for ground, 1.0 for bed)
		static Action Sleep(float quality = 0.5F) {
			Action action;
			action.type = ActionType::Sleep;
			action.state = ActionState::Starting;
			action.duration = 8.0F;		 // 8 seconds of sleep (game time scaled)
			action.interruptable = true; // Can be woken for emergencies

			NeedEffect needEff;
			needEff.need = NeedType::Energy;
			needEff.restoreAmount = 60.0F * quality; // Quality affects restoration
			action.effect = needEff;

			return action;
		}

		/// Factory: Smart Toilet action - handles peeing and/or pooping based on needs
		/// @param position Position for Bio Pile if pooping
		/// @param doPee Whether to relieve bladder (peeing)
		/// @param doPoop Whether to relieve digestion (pooping, creates Bio Pile)
		///
		/// Duration logic:
		///   - Both pee and poop: 5.0s (combined action takes longest)
		///   - Poop only: 4.0s (pooping takes longer than peeing)
		///   - Pee only: 2.0s (quick action)
		static Action Toilet(glm::vec2 position, bool doPee, bool doPoop) {
			Action action;
			action.type = ActionType::Toilet;
			action.state = ActionState::Starting;
			action.targetPosition = position;
			action.interruptable = false; // Definitely can't stop this!

			// Duration depends on what we're doing
			if (doPee && doPoop) {
				action.duration = 5.0F; // Both takes longer
			} else if (doPoop) {
				action.duration = 4.0F; // Pooping takes longer
			} else {
				action.duration = 2.0F; // Just peeing is quick
			}

			// Store what we're doing in effect
			// We'll use NeedEffect for the primary action, and flags for secondary
			NeedEffect needEff;
			if (doPee && doPoop) {
				// Both - primary is bladder, side effect is digestion
				needEff.need = NeedType::Bladder;
				needEff.restoreAmount = 100.0F;
				needEff.sideEffectNeed = NeedType::Digestion;
				needEff.sideEffectAmount = 100.0F; // Positive = restore (relief)
			} else if (doPoop) {
				// Just pooping
				needEff.need = NeedType::Digestion;
				needEff.restoreAmount = 100.0F;
			} else if (doPee) {
				// Just peeing
				needEff.need = NeedType::Bladder;
				needEff.restoreAmount = 100.0F;
			} else {
				// Neither pee nor poop - shouldn't happen, but return a no-op action
				// This is a programming error; caller should ensure at least one is true
				needEff.need = NeedType::Bladder;
				needEff.restoreAmount = 0.0F;
			}
			action.effect = needEff;

			// Store poop flag for Bio Pile spawning (checked in ActionSystem)
			action.spawnBioPile = doPoop;

			return action;
		}

		/// Factory: Pickup action - instantly pick up a ground item
		/// @param itemDefName Item definition to add to inventory
		/// @param quantity Number of items to pick up
		/// @param sourcePos Position of the source entity
		/// @param sourceDefName DefName of the source entity (for removal)
		static Action Pickup(const std::string& itemDefName, uint32_t quantity, glm::vec2 sourcePos, const std::string& sourceDefName) {
			Action action;
			action.type = ActionType::Pickup;
			action.state = ActionState::Starting;
			action.duration = 0.5F; // Quick pickup
			action.targetPosition = sourcePos;
			action.interruptable = false; // Don't interrupt mid-pickup

			CollectionEffect collEff;
			collEff.itemDefName = itemDefName;
			collEff.quantity = quantity;
			collEff.sourcePosition = sourcePos;
			collEff.sourceDefName = sourceDefName;
			collEff.destroySource = true; // Picking up removes the ground item
			collEff.regrowthTime = 0.0F;
			action.effect = collEff;

			return action;
		}

		/// Factory: Harvest action - harvest items from an entity
		/// @param itemDefName Item definition to yield
		/// @param quantity Number of items to harvest
		/// @param harvestDuration Time to complete harvest
		/// @param sourcePos Position of the source entity
		/// @param sourceDefName DefName of the source entity
		/// @param destructive If true, entity is destroyed after harvest
		/// @param regrowthTime If not destructive, time until harvestable again
		static Action Harvest(
			const std::string& itemDefName,
			uint32_t		   quantity,
			float			   harvestDuration,
			glm::vec2		   sourcePos,
			const std::string& sourceDefName,
			bool			   destructive,
			float			   regrowthTime
		) {
			Action action;
			action.type = ActionType::Harvest;
			action.state = ActionState::Starting;
			action.duration = harvestDuration;
			action.targetPosition = sourcePos;
			action.interruptable = false; // Don't interrupt mid-harvest

			CollectionEffect collEff;
			collEff.itemDefName = itemDefName;
			collEff.quantity = quantity;
			collEff.sourcePosition = sourcePos;
			collEff.sourceDefName = sourceDefName;
			collEff.destroySource = destructive;
			collEff.regrowthTime = regrowthTime;
			action.effect = collEff;

			return action;
		}

		/// Factory: Craft action - craft items at a station
		/// @param recipeDefName Recipe to craft
		/// @param stationEntityId Entity ID of the crafting station
		/// @param stationPos Position of the station
		/// @param workAmount Work ticks to complete (converted to seconds)
		/// @param inputs Input items to consume (defName -> count)
		/// @param outputs Output items to produce (defName -> count)
		static Action Craft(
			const std::string&									 recipeDefName,
			uint64_t											 stationEntityId,
			glm::vec2											 stationPos,
			float												 workAmount,
			const std::vector<std::pair<std::string, uint32_t>>& inputs,
			const std::vector<std::pair<std::string, uint32_t>>& outputs
		) {
			Action action;
			action.type = ActionType::Craft;
			action.state = ActionState::Starting;
			// Convert work amount to duration (work ticks -> seconds)
			constexpr float kWorkTicksPerSecond = 100.0F;
			action.duration = workAmount / kWorkTicksPerSecond;
			action.targetPosition = stationPos;
			action.interruptable = false; // Don't interrupt mid-craft

			CraftingEffect craftEff;
			craftEff.recipeDefName = recipeDefName;
			craftEff.stationEntityId = stationEntityId;
			craftEff.inputs = inputs;
			craftEff.outputs = outputs;
			action.effect = craftEff;

			return action;
		}

		/// Factory: Deposit action - deposit items into a storage container
		/// @param itemDefName Item to deposit from inventory
		/// @param quantity Number of items to deposit
		/// @param storageEntityId Entity ID of the target storage container
		/// @param storagePos Position of the storage container
		static Action Deposit(
			const std::string& itemDefName,
			uint32_t		   quantity,
			uint64_t		   storageEntityId,
			glm::vec2		   storagePos
		) {
			Action action;
			action.type = ActionType::Deposit;
			action.state = ActionState::Starting;
			action.duration = 1.0F; // 1 second to deposit items
			action.targetPosition = storagePos;
			action.interruptable = false; // Don't interrupt mid-deposit

			DepositEffect depEff;
			depEff.itemDefName = itemDefName;
			depEff.quantity = quantity;
			depEff.storageEntityId = storageEntityId;
			action.effect = depEff;

			return action;
		}
	};

	/// Get human-readable name for action type (for debug logging)
	[[nodiscard]] inline const char* actionTypeName(ActionType type) {
		switch (type) {
			case ActionType::None:
				return "None";
			case ActionType::Eat:
				return "Eat";
			case ActionType::Drink:
				return "Drink";
			case ActionType::Sleep:
				return "Sleep";
			case ActionType::Toilet:
				return "Toilet";
			case ActionType::Pickup:
				return "Pickup";
			case ActionType::Harvest:
				return "Harvest";
			case ActionType::Craft:
				return "Craft";
			case ActionType::Deposit:
				return "Deposit";
		}
		return "Unknown";
	}

} // namespace ecs
