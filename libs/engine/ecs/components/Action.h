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
#include <variant>

namespace ecs {

	// ============================================================================
	// Action Types (what the colonist is doing)
	// ============================================================================

	/// Action types that colonists can perform
	enum class ActionType : uint8_t {
		None = 0,

		// Need Fulfillment Actions (Phase 1 MVP)
		Eat,	// Consuming Edible entity (Berry Bush)
		Drink,	// Drinking from water tile (Pond)
		Sleep,	// Sleeping on ground or bed
		Toilet, // Using toilet or ground relief

		// Work Actions (Phase 2+)
		// Harvest,  // Gathering from harvestable entity
		// Craft,    // Creating items at workbench
		// Build,    // Constructing structures
		// Repair,   // Fixing damaged structures
		// Haul,     // Moving items
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

	/// Effect for production actions (Harvest, Craft)
	/// Will produce items when complete. Stub for Phase 2+.
	struct ProductionEffect {
		// TODO: When inventory system exists:
		// std::string recipeId;
		// std::vector<ItemStack> outputs;

		/// Source entity being harvested/consumed (if applicable)
		uint64_t sourceEntityId = 0;
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

	/// Variant holding the effect data for the current action
	using ActionEffect = std::variant<std::monostate, NeedEffect, ProductionEffect, ProgressEffect, SpawnEffect>;

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

		/// Factory: Eat action - restores hunger, fills digestion
		/// @param nutrition Amount of hunger to restore (0.0-1.0 maps to 0-100%)
		static Action Eat(float nutrition) {
			Action action;
			action.type = ActionType::Eat;
			action.state = ActionState::Starting;
			action.duration = 2.0F; // 2 seconds to eat
			action.interruptable = false; // Can't stop mid-bite!

			NeedEffect needEff;
			needEff.need = NeedType::Hunger;
			needEff.restoreAmount = nutrition * 100.0F;
			needEff.sideEffectNeed = NeedType::Digestion;
			needEff.sideEffectAmount = -20.0F; // Eating DECREASES digestion (fills gut)
			action.effect = needEff;

			return action;
		}

		/// Factory: Drink action - restores thirst fully, fills bladder
		/// Water tiles are inexhaustible, so drinking always fully restores thirst.
		static Action Drink() {
			Action action;
			action.type = ActionType::Drink;
			action.state = ActionState::Starting;
			action.duration = 1.5F; // 1.5 seconds to drink
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
			action.duration = 8.0F; // 8 seconds of sleep (game time scaled)
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
			} else {
				// Just peeing (or nothing, but shouldn't happen)
				needEff.need = NeedType::Bladder;
				needEff.restoreAmount = 100.0F;
			}
			action.effect = needEff;

			// Store poop flag for Bio Pile spawning (checked in ActionSystem)
			action.spawnBioPile = doPoop;

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
		}
		return "Unknown";
	}

} // namespace ecs
